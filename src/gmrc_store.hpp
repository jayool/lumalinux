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

inline std::optional<uint64_t> HttpFetch(uint64_t gid) {
    struct addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo("gmrc.wudrm.com", "80", &hints, &res) != 0 || !res) {
        Log::Warn("GMRC: DNS resolve failed for gmrc.wudrm.com");
        return std::nullopt;
    }
    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return std::nullopt; }

    struct timeval tv{20, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

    if (::connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        Log::Warn("GMRC: connect to gmrc.wudrm.com failed");
        freeaddrinfo(res); ::close(fd);
        return std::nullopt;
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
    if (::send(fd, req, n, 0) != n) { ::close(fd); return std::nullopt; }

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
        Log::Warn("GMRC: malformed HTTP response for gid %llu", (unsigned long long)gid);
        return std::nullopt;
    }
    // Status check: expect "HTTP/1.x 200"
    if (resp.compare(9, 3, "200") != 0) {
        Log::Warn("GMRC: non-200 for gid %llu: '%.*s'",
                  (unsigned long long)gid, 12, resp.c_str());
        return std::nullopt;
    }
    const char* p = resp.c_str() + hdr + 4;
    while (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t') ++p;
    if (*p < '0' || *p > '9') {
        Log::Warn("GMRC: body not numeric for gid %llu", (unsigned long long)gid);
        return std::nullopt;
    }
    uint64_t code = strtoull(p, nullptr, 10);
    if (code == 0) return std::nullopt;
    return code;
}

inline std::map<uint64_t, uint64_t>& Cache() {
    static std::map<uint64_t, uint64_t> c;
    return c;
}
inline std::mutex& Mtx() { static std::mutex m; return m; }

} // namespace detail

// Returns the manifest request code for `gid`, fetching from gmrc.wudrm.com on
// first use and caching it. nullopt if the endpoint is unreachable/denied.
inline std::optional<uint64_t> GetCode(uint64_t gid) {
    {
        std::lock_guard<std::mutex> lk(detail::Mtx());
        auto it = detail::Cache().find(gid);
        if (it != detail::Cache().end()) return it->second;
    }
    Log::Info("GMRC: fetching request code for manifest %llu from gmrc.wudrm.com...",
              (unsigned long long)gid);
    auto code = detail::HttpFetch(gid);
    if (code) {
        std::lock_guard<std::mutex> lk(detail::Mtx());
        detail::Cache()[gid] = *code;
        Log::Info("GMRC: got code %llu for manifest %llu",
                  (unsigned long long)*code, (unsigned long long)gid);
    }
    return code;
}

} // namespace Gmrc
