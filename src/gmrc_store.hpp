#pragma once

// Manifest-request-code provider. Fetches the per-manifest "request code" (the
// token Valve hands out to authorize a manifest download) from the community
// endpoint gmrc.wudrm.com — the same endpoint SteaMidra uses — over plain HTTP
// (no TLS needed), and caches results. The GMRC hook calls GetCode() to fill in
// the code the Steam server denied for unowned content.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "log.hpp"

namespace Gmrc {

namespace detail {

// Outcome of one fetch attempt. `retryable` is true only for TRANSIENT failures
// (5xx, or a DNS/connect/send/truncated blip) where another attempt might
// succeed; a definitive answer (4xx, or a 200 with no usable code) sets it false
// so GetCode() stops hammering immediately.
struct FetchOutcome {
    std::optional<uint64_t> code;
    bool retryable = false;
};

inline FetchOutcome HttpFetch(uint64_t gid) {
    struct addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo("gmrc.wudrm.com", "80", &hints, &res) != 0 || !res) {
        Log::Warn("GMRC: DNS resolve failed for gmrc.wudrm.com");
        return {std::nullopt, true};
    }
    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return {std::nullopt, true}; }

    struct timeval tv{20, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

    if (::connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        Log::Warn("GMRC: connect to gmrc.wudrm.com failed");
        freeaddrinfo(res); ::close(fd);
        return {std::nullopt, true};
    }
    freeaddrinfo(res);

    // HTTP/1.0 so the server closes after the body (no chunked encoding to parse).
    char req[256];
    int n = std::snprintf(req, sizeof req,
        "GET /manifest/%llu HTTP/1.0\r\n"
        "Host: gmrc.wudrm.com\r\n"
        "Referer: http://gmrc.wudrm.com\r\n"
        "User-Agent: lumalinux\r\n"
        "Connection: close\r\n\r\n",
        (unsigned long long)gid);
    if (::send(fd, req, n, 0) != n) { ::close(fd); return {std::nullopt, true}; }

    std::string resp;
    char buf[1024];
    ssize_t r;
    while ((r = ::recv(fd, buf, sizeof buf, 0)) > 0) {
        resp.append(buf, (size_t)r);
        if (resp.size() > 65536) break;
    }
    ::close(fd);

    size_t hdr = resp.find("\r\n\r\n");
    if (hdr == std::string::npos) {
        Log::Warn("GMRC: malformed/truncated HTTP response for gid %llu (will retry)",
                  (unsigned long long)gid);
        return {std::nullopt, true};
    }
    // Status check: expect "HTTP/1.x 200". A 5xx is a transient server-side error
    // (e.g. the 502 from gmrc.wudrm.com that Steam mislabels as "No connection")
    // and is worth retrying; a 4xx is the server's definitive "no" — don't.
    if (resp.size() < 12 || resp.compare(9, 3, "200") != 0) {
        bool transient = (resp.size() > 9 && resp[9] == '5');
        Log::Warn("GMRC: non-200 for gid %llu: '%.*s'%s",
                  (unsigned long long)gid, 12, resp.c_str(),
                  transient ? " (transient — will retry)" : "");
        return {std::nullopt, transient};
    }
    const char* p = resp.c_str() + hdr + 4;
    while (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t') ++p;
    if (*p < '0' || *p > '9') {
        Log::Warn("GMRC: body not numeric for gid %llu", (unsigned long long)gid);
        return {std::nullopt, false};
    }
    uint64_t code = strtoull(p, nullptr, 10);
    if (code == 0) return {std::nullopt, false};
    return {code, false};
}

inline std::map<uint64_t, uint64_t>& Cache() {
    static std::map<uint64_t, uint64_t> c;
    return c;
}
inline std::mutex& Mtx() { static std::mutex m; return m; }

} // namespace detail

// Returns the manifest request code for `gid`, fetching from gmrc.wudrm.com on
// first use and caching it. On a TRANSIENT failure (a 5xx like the 502 that
// Steam mislabels as "No connection", or a DNS/connect/truncated blip) it
// retries a few times with exponential backoff before giving up; a definitive
// answer (4xx, or a 200 with no usable code) returns immediately so we don't
// hammer the endpoint. nullopt = no code (download falls through to the server).
inline std::optional<uint64_t> GetCode(uint64_t gid) {
    {
        std::lock_guard<std::mutex> lk(detail::Mtx());
        auto it = detail::Cache().find(gid);
        if (it != detail::Cache().end()) return it->second;
    }

    const int kMaxAttempts = 3;
    unsigned  backoffUs    = 250000;  // 250ms, then 500ms between attempts
    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        if (attempt == 1)
            Log::Info("GMRC: fetching request code for manifest %llu from gmrc.wudrm.com...",
                      (unsigned long long)gid);
        else
            Log::Info("GMRC: retry %d/%d for manifest %llu (gmrc.wudrm.com)...",
                      attempt, kMaxAttempts, (unsigned long long)gid);

        detail::FetchOutcome out = detail::HttpFetch(gid);
        if (out.code) {
            std::lock_guard<std::mutex> lk(detail::Mtx());
            detail::Cache()[gid] = *out.code;
            Log::Info("GMRC: got code %llu for manifest %llu%s",
                      (unsigned long long)*out.code, (unsigned long long)gid,
                      attempt > 1 ? " (after retry)" : "");
            return out.code;
        }
        if (!out.retryable) break;          // definitive failure — stop now
        if (attempt < kMaxAttempts) {
            ::usleep(backoffUs);
            backoffUs *= 2;
        }
    }
    Log::Warn("GMRC: no code for manifest %llu after %d attempt(s)",
              (unsigned long long)gid, kMaxAttempts);
    return std::nullopt;
}

} // namespace Gmrc
