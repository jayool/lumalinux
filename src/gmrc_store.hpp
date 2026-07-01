#pragma once

// Manifest-request-code provider. Fetches the per-manifest "request code" (the
// token Valve hands out to authorize a manifest download) from a CASCADE of
// community endpoints and caches results. The GMRC hook calls GetCode() to fill
// in the code the Steam server denied for unowned content.
//
// The three providers below are exactly the ones OpenSteamTool ships
// (ManifestClient.cpp `kProviders`): opensteamtool, wudrm, steamrun. Earlier
// versions of lumalinux hit only wudrm over a raw HTTP socket, so when wudrm
// 502'd (which it does often) the shader/base-depot manifest could not be
// resolved and Steam surfaced the cosmetic "No connection" popup even though
// the game content itself installed fine. We now try all three in order and
// take the first that returns a usable code.
//
// Transport: all three go through Curl::getString (libcurl, dlopen'd at
// runtime) because opensteamtool and steam.run are HTTPS. A non-'curl' User-
// Agent is REQUIRED for opensteamtool: its Cloudflare WAF challenges the default
// curl UA (returns the "Just a moment..." JS interstitial) but lets any other
// UA straight through to the plain uint64 body. This was verified on-device:
//   curl -s "https://manifest.opensteamtool.com/<gid>"                 -> CF challenge
//   curl -s -A "OpenSteamTool/1.0" "https://manifest.opensteamtool.com/<gid>" -> <code>
// so we send OpenSteamTool/1.0 (the same UA their WinHTTP client uses).
//
// Curl::getString returns 0 on transport success WITHOUT checking HTTP status,
// so a Cloudflare challenge page, a 502, or a 404 all come back as a non-numeric
// body — the per-provider parser rejects those (returns nullopt) and we fall
// through to the next provider. nullopt from GetCode = no code from anyone
// (download falls through to Steam's server path / the popup).

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "curl.hpp"
#include "log.hpp"

namespace Gmrc {

namespace detail {

// The UA that gets past manifest.opensteamtool.com's Cloudflare WAF (see header
// comment). Harmless for the other two providers.
inline constexpr const char* kUserAgent = "OpenSteamTool/1.0";

// Parse a bare uint64 body (opensteamtool, wudrm). Rejects anything that isn't a
// run of digits after optional leading whitespace — which is how a Cloudflare
// HTML challenge / 5xx / 404 body gets rejected so the caller tries the next
// provider. A parsed 0 is treated as "no code".
inline std::optional<uint64_t> ParsePlainUint(const std::string& body) {
    const char* p = body.c_str();
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p < '0' || *p > '9') return std::nullopt;
    uint64_t code = strtoull(p, nullptr, 10);
    if (code == 0) return std::nullopt;
    return code;
}

// Parse steam.run's JSON body: {"content":"<uint64>", ...} — pull the string
// value of "content". Mirrors OpenSteamTool's ParseSteamRunJson.
inline std::optional<uint64_t> ParseSteamRunJson(const std::string& body) {
    size_t key = body.find("\"content\"");
    if (key == std::string::npos) return std::nullopt;
    size_t q1 = body.find('"', key + 9);
    if (q1 == std::string::npos) return std::nullopt;
    size_t q2 = body.find('"', q1 + 1);
    if (q2 == std::string::npos) return std::nullopt;
    return ParsePlainUint(body.substr(q1 + 1, q2 - q1 - 1));
}

struct Provider {
    const char* name;
    const char* urlTemplate;   // exactly one %llu (the manifest gid)
    std::optional<uint64_t> (*parse)(const std::string&);
};

// opensteamtool first: it responds where wudrm has been 502'ing. Order otherwise
// mirrors OpenSteamTool's own table.
inline const Provider kProviders[] = {
    {"opensteamtool", "https://manifest.opensteamtool.com/%llu",      &ParsePlainUint},
    {"wudrm",         "http://gmrc.wudrm.com/manifest/%llu",          &ParsePlainUint},
    {"steamrun",      "https://manifest.steam.run/api/manifest/%llu", &ParseSteamRunJson},
};

inline std::map<uint64_t, uint64_t>& Cache() {
    static std::map<uint64_t, uint64_t> c;
    return c;
}
inline std::mutex& Mtx() { static std::mutex m; return m; }

} // namespace detail

// Returns the manifest request code for `gid`, fetching from the provider
// cascade on first use and caching it. Tries each provider in order and takes
// the first usable code; a provider that errors, is Cloudflare-challenged, or
// returns a non-numeric body is skipped. nullopt = no provider had a code.
inline std::optional<uint64_t> GetCode(uint64_t gid) {
    {
        std::lock_guard<std::mutex> lk(detail::Mtx());
        auto it = detail::Cache().find(gid);
        if (it != detail::Cache().end()) return it->second;
    }

    for (const auto& p : detail::kProviders) {
        char url[256];
        std::snprintf(url, sizeof url, p.urlTemplate, (unsigned long long)gid);

        Log::Info("GMRC: fetching request code for manifest %llu from %s...",
                  (unsigned long long)gid, p.name);

        std::string body;
        int rc = Curl::getString(url, body, detail::kUserAgent);
        if (rc != 0) {
            Log::Warn("GMRC: %s transport error (curl rc=%d) for manifest %llu "
                      "— trying next provider", p.name, rc, (unsigned long long)gid);
            continue;
        }

        auto code = p.parse(body);
        if (code) {
            std::lock_guard<std::mutex> lk(detail::Mtx());
            detail::Cache()[gid] = *code;
            Log::Info("GMRC: got code %llu for manifest %llu (via %s)",
                      (unsigned long long)*code, (unsigned long long)gid, p.name);
            return code;
        }

        Log::Warn("GMRC: %s returned no usable code for manifest %llu "
                  "(Cloudflare challenge / 5xx / empty) — trying next provider",
                  p.name, (unsigned long long)gid);
    }

    Log::Warn("GMRC: no code for manifest %llu from any provider "
              "(download will fall through to Steam's server path)",
              (unsigned long long)gid);
    return std::nullopt;
}

} // namespace Gmrc
