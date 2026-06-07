#pragma once

// Manifest-request-code provider. Fetches the per-manifest "request code" (the
// token Valve hands out to authorize a manifest download) from the community
// endpoint gmrc.wudrm.com — the same endpoint SteaMidra uses — over plain HTTP
// (no TLS needed), and caches results both in memory and on disk so the
// per-gid round-trip happens at most once per machine.
//
// Resilience knobs:
//   - LUMA_GMRC_HOST env var overrides the default host (e.g. mirror).
//   - Successful codes are written to ~/.cache/lumalinux/gmrc_cache.json and
//     loaded back on the next startup. Surviving a wudrm outage requires the
//     gid to have been fetched at least once previously.
//   - On transient connect failure, retries once after 1 second before giving up.
//
// The GMRC hook calls GetCode() to fill in the code the Steam server denied
// for unowned content.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "log.hpp"

namespace Gmrc {

namespace detail {

inline const char* GetHost() {
    // Resolved once and cached. Setting LUMA_GMRC_HOST after this process has
    // started has no effect — that mirrors the rest of the env-var handling
    // in lumalinux (LUMA_NO_*, LUMA_LOADPKG_IDX), which are also one-shot.
    static const char* host = []() -> const char* {
        const char* env = std::getenv("LUMA_GMRC_HOST");
        if (env && *env) return env;
        return "gmrc.wudrm.com";
    }();
    return host;
}

inline std::string CachePath() {
    // XDG_CACHE_HOME first (matches Log::ResolveLogPath), fall back to
    // $HOME/.cache, last resort /tmp. Stored as plain text (gid + code per
    // line) to avoid pulling in a JSON parser for what is essentially a
    // uint64 -> uint64 map.
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    std::string dir;
    if (xdg && *xdg) {
        dir = xdg;
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        dir = std::string(home) + "/.cache";
    } else {
        dir = "/tmp";
    }
    dir += "/lumalinux";
    // best-effort mkdir; if it fails, the open() below will too and we'll log
    std::string cmd = "mkdir -p '" + dir + "' 2>/dev/null";
    int rc = std::system(cmd.c_str());
    (void)rc;
    return dir + "/gmrc_cache.txt";
}

inline std::map<uint64_t, uint64_t> LoadDiskCache() {
    std::map<uint64_t, uint64_t> result;
    std::ifstream f(CachePath());
    if (!f.is_open()) return result;

    std::string line;
    size_t loaded = 0, skipped = 0;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        uint64_t gid = 0, code = 0;
        if ((iss >> gid >> code) && gid != 0 && code != 0) {
            result[gid] = code;
            ++loaded;
        } else {
            ++skipped;
        }
    }
    if (loaded > 0 || skipped > 0) {
        Log::Info("GMRC: disk cache loaded %zu entries (skipped %zu malformed)",
                  loaded, skipped);
    }
    return result;
}

inline void SaveDiskCache(const std::map<uint64_t, uint64_t>& cache) {
    std::string path = CachePath();
    std::string tmp  = path + ".tmp";

    {
        std::ofstream f(tmp);
        if (!f.is_open()) {
            Log::Warn("GMRC: cannot open cache for writing: %s", tmp.c_str());
            return;
        }
        f << "# lumalinux GMRC cache - <manifest_gid> <request_code>, one per line\n";
        for (const auto& [gid, code] : cache) {
            f << gid << ' ' << code << '\n';
        }
    }

    // Atomic replace so a crash mid-write can't leave a half-written file
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        Log::Warn("GMRC: cannot rename cache %s -> %s", tmp.c_str(), path.c_str());
        std::remove(tmp.c_str());  // best-effort cleanup
    }
}

inline std::optional<uint64_t> HttpFetchOnce(uint64_t gid) {
    const char* host = GetHost();

    struct addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host, "80", &hints, &res) != 0 || !res) {
        Log::Warn("GMRC: DNS resolve failed for %s", host);
        return std::nullopt;
    }
    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return std::nullopt; }

    struct timeval tv{20, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

    if (::connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        Log::Warn("GMRC: connect to %s failed", host);
        freeaddrinfo(res); ::close(fd);
        return std::nullopt;
    }
    freeaddrinfo(res);

    // HTTP/1.0 so the server closes after the body (no chunked encoding to parse).
    char req[256];
    int n = std::snprintf(req, sizeof req,
        "GET /manifest/%llu HTTP/1.0\r\n"
        "Host: %s\r\n"
        "Referer: http://%s\r\n"
        "User-Agent: lumalinux\r\n"
        "Connection: close\r\n\r\n",
        (unsigned long long)gid, host, host);
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

inline std::optional<uint64_t> HttpFetch(uint64_t gid) {
    // One attempt + one retry after a 1-second backoff. Transient failures
    // (TCP RST during DNS race, brief unreachable, server hiccup) get a
    // second chance; persistent failures still surface as nullopt after ~21s.
    if (auto code = HttpFetchOnce(gid); code) return code;
    Log::Info("GMRC: first attempt for gid %llu failed, retrying once after 1s",
              (unsigned long long)gid);
    ::sleep(1);
    return HttpFetchOnce(gid);
}

inline std::mutex& MemMtx()  { static std::mutex m; return m; }
inline std::mutex& DiskMtx() { static std::mutex m; return m; }

inline std::map<uint64_t, uint64_t>& Cache() {
    // The static initializer runs once and loads the disk cache into memory.
    // Thread-safe under C++11+ ("magic statics").
    static std::map<uint64_t, uint64_t> c = LoadDiskCache();
    return c;
}

} // namespace detail

// Returns the manifest request code for `gid`, fetching from the configured
// host (`gmrc.wudrm.com` by default, or `$LUMA_GMRC_HOST` if set) on first
// use, caching in memory and on disk for subsequent calls. nullopt if the
// endpoint is unreachable and the gid was never cached before.
inline std::optional<uint64_t> GetCode(uint64_t gid) {
    // Layer 1: in-memory cache (also covers disk-cache hits because the disk
    // cache is loaded into this map at first call via the static initialiser).
    {
        std::lock_guard<std::mutex> lk(detail::MemMtx());
        auto it = detail::Cache().find(gid);
        if (it != detail::Cache().end()) {
            Log::Debug("GMRC: cache hit for manifest %llu (code %llu)",
                       (unsigned long long)gid,
                       (unsigned long long)it->second);
            return it->second;
        }
    }

    // Layer 2: network fetch from configured host (with one retry).
    Log::Info("GMRC: fetching request code for manifest %llu from %s...",
              (unsigned long long)gid, detail::GetHost());
    auto code = detail::HttpFetch(gid);

    if (code) {
        // Cache in memory + persist to disk for future runs / network outages.
        std::map<uint64_t, uint64_t> snapshot;
        {
            std::lock_guard<std::mutex> lk(detail::MemMtx());
            detail::Cache()[gid] = *code;
            snapshot = detail::Cache();
        }
        Log::Info("GMRC: network hit — got code %llu for manifest %llu",
                  (unsigned long long)*code, (unsigned long long)gid);
        {
            std::lock_guard<std::mutex> lk(detail::DiskMtx());
            detail::SaveDiskCache(snapshot);
        }
        return code;
    }

    // Layer 3: total miss. Host unreachable AND not seen before.
    Log::Warn("GMRC: total miss for manifest %llu — host %s unreachable and "
              "no cached entry. Install will fail with Access Denied.",
              (unsigned long long)gid, detail::GetHost());
    return std::nullopt;
}

} // namespace Gmrc
