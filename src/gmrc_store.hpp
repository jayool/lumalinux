#pragma once

// Manifest-request-code provider. Fetches the per-manifest "request code" (the
// token Valve hands out to authorize a manifest download) from a cascade of
// community mirrors and caches results. The GMRC hook calls GetCode() to fill in
// the code the Steam server denied for unowned content.
//
// Mirror cascade (mirrors SteaMidra / OpenSteamTool — first numeric code wins):
//   1. manifest.opensteamtool.com   (HTTPS, default upstream for OpenSteamTool)
//   2. manifest.steam.run           (HTTPS)
//   3. gmrc.wudrm.com               (HTTP, historical single provider — last)
// wudrm alone used to tank installs whenever its gateway 502'd (Steam mislabels
// that as "No internet connection"); with the cascade a wudrm outage is harmless
// because opensteamtool/steam.run answer first. The two HTTPS hosts go through
// `curl` (no TLS stack linked into the hook); wudrm keeps the dependency-free raw
// socket, so GMRC still works exactly as before even if curl is unavailable.

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
// so GetCode() stops hammering this mirror and moves on immediately.
struct FetchOutcome {
    std::optional<uint64_t> code;
    bool retryable = false;
};

// One upstream. `url_prefix + <gid>` is the full request URL. HTTPS mirrors are
// fetched via curl; the lone HTTP mirror (wudrm) uses the raw socket below.
struct Mirror {
    const char* name;
    const char* url_prefix;
    const char* referer;
    bool https;
};

inline const Mirror kMirrors[] = {
    {"opensteamtool", "https://manifest.opensteamtool.com/",      "https://manifest.opensteamtool.com", true},
    {"steam.run",     "https://manifest.steam.run/api/manifest/", "https://manifest.steam.run",         true},
    {"wudrm",         "http://gmrc.wudrm.com/manifest/",          "http://gmrc.wudrm.com",              false},
};

// Parse a fetched (status, body) pair into an outcome. Shared by both transports.
// 2xx + all-digit body → code; 5xx / transport blip → retryable; everything else
// (4xx, 2xx-but-not-numeric, code 0) → definitive.
inline FetchOutcome Classify(const char* name, uint64_t gid,
                             const std::string& status, const std::string& body,
                             bool transportOk) {
    if (!transportOk || status.empty() || status == "000") {
        Log::Warn("GMRC[%s]: transport failure for gid %llu (will retry)",
                  name, (unsigned long long)gid);
        return {std::nullopt, true};
    }
    if (status.size() < 1 || status[0] != '2') {
        bool transient = status[0] == '5';
        Log::Warn("GMRC[%s]: HTTP %s for gid %llu%s",
                  name, status.c_str(), (unsigned long long)gid,
                  transient ? " (transient — will retry)" : "");
        return {std::nullopt, transient};
    }
    size_t s = body.find_first_not_of(" \t\r\n");
    if (s == std::string::npos || body[s] < '0' || body[s] > '9') {
        Log::Warn("GMRC[%s]: body not numeric for gid %llu", name, (unsigned long long)gid);
        return {std::nullopt, false};
    }
    uint64_t code = strtoull(body.c_str() + s, nullptr, 10);
    if (code == 0) return {std::nullopt, false};
    return {code, false};
}

// HTTPS fetch via curl. curl handles TLS/redirects/timeout; `-w '\n%{http_code}'`
// appends the status after the body. The URL/referer are built from fixed
// templates + a numeric gid, so single-quoting them for the shell is safe.
inline FetchOutcome FetchViaCurl(const Mirror& m, uint64_t gid) {
    std::string url = std::string(m.url_prefix) + std::to_string(gid);
    std::string cmd =
        "curl -sS --max-time 15 -A lumalinux -H 'Referer: " + std::string(m.referer) +
        "' -w '\\n%{http_code}' '" + url + "' 2>/dev/null";

    FILE* pipe = ::popen(cmd.c_str(), "r");
    if (!pipe) return {std::nullopt, true};
    std::string out;
    char buf[1024];
    size_t rd;
    while ((rd = std::fread(buf, 1, sizeof buf, pipe)) > 0) {
        out.append(buf, rd);
        if (out.size() > 65536) break;
    }
    int rc = ::pclose(pipe);

    // Split the trailing "\n<http_code>" that -w appended.
    size_t nl = out.find_last_of('\n');
    std::string status = (nl == std::string::npos) ? out : out.substr(nl + 1);
    std::string body   = (nl == std::string::npos) ? std::string() : out.substr(0, nl);
    return Classify(m.name, gid, status, body, rc == 0);
}

// HTTP fetch for wudrm over a raw socket — no curl / TLS dependency, so GMRC
// keeps working via wudrm even if curl is missing. HTTP/1.0 so the server closes
// after the body (no chunked encoding to parse).
inline FetchOutcome HttpFetch(uint64_t gid) {
    struct addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo("gmrc.wudrm.com", "80", &hints, &res) != 0 || !res) {
        Log::Warn("GMRC[wudrm]: DNS resolve failed for gmrc.wudrm.com");
        return {std::nullopt, true};
    }
    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return {std::nullopt, true}; }

    struct timeval tv{20, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

    if (::connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        Log::Warn("GMRC[wudrm]: connect to gmrc.wudrm.com failed");
        freeaddrinfo(res); ::close(fd);
        return {std::nullopt, true};
    }
    freeaddrinfo(res);

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
        Log::Warn("GMRC[wudrm]: malformed/truncated HTTP response for gid %llu (will retry)",
                  (unsigned long long)gid);
        return {std::nullopt, true};
    }
    // Reuse the shared classifier: extract "HTTP/1.x <status>" and the body.
    std::string status = (resp.size() >= 12) ? resp.substr(9, 3) : std::string();
    std::string body   = resp.substr(hdr + 4);
    return Classify("wudrm", gid, status, body, true);
}

inline FetchOutcome Fetch(const Mirror& m, uint64_t gid) {
    return m.https ? FetchViaCurl(m, gid) : HttpFetch(gid);
}

inline std::map<uint64_t, uint64_t>& Cache() {
    static std::map<uint64_t, uint64_t> c;
    return c;
}
inline std::mutex& Mtx() { static std::mutex m; return m; }

} // namespace detail

// Returns the manifest request code for `gid`, fetching from the mirror cascade
// on first use and caching it. Each mirror is tried in order; a TRANSIENT failure
// (5xx, or a DNS/connect/TLS/truncated blip) is retried once with a short backoff
// before falling through to the next mirror, while a definitive answer (4xx, or a
// 200 with no usable code) moves to the next mirror immediately. The first numeric
// code wins and is cached; failures are not. nullopt = no code from any mirror
// (download falls through to the server → Steam surfaces "Access Denied").
inline std::optional<uint64_t> GetCode(uint64_t gid) {
    {
        std::lock_guard<std::mutex> lk(detail::Mtx());
        auto it = detail::Cache().find(gid);
        if (it != detail::Cache().end()) return it->second;
    }

    const int kAttemptsPerMirror = 2;   // one retry per mirror on a transient blip
    for (const auto& m : detail::kMirrors) {
        unsigned backoffUs = 250000;    // 250ms before the single retry
        for (int attempt = 1; attempt <= kAttemptsPerMirror; ++attempt) {
            Log::Info("GMRC[%s]: fetching request code for manifest %llu%s...",
                      m.name, (unsigned long long)gid, attempt > 1 ? " (retry)" : "");

            detail::FetchOutcome out = detail::Fetch(m, gid);
            if (out.code) {
                std::lock_guard<std::mutex> lk(detail::Mtx());
                detail::Cache()[gid] = *out.code;
                Log::Info("GMRC[%s]: got code %llu for manifest %llu",
                          m.name, (unsigned long long)*out.code, (unsigned long long)gid);
                return out.code;
            }
            if (!out.retryable) break;  // this mirror's definitive no → next mirror
            if (attempt < kAttemptsPerMirror) {
                ::usleep(backoffUs);
                backoffUs *= 2;
            }
        }
        // mirror exhausted (definitive no, or retry also failed) → next mirror
    }
    Log::Warn("GMRC: no code for manifest %llu from any mirror "
              "(opensteamtool/steam.run/wudrm)", (unsigned long long)gid);
    return std::nullopt;
}

} // namespace Gmrc
