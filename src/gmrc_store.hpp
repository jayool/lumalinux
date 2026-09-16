#pragma once

// Manifest-request-code provider. Fetches the per-manifest "request code" (the
// token Valve hands out to authorize a manifest download) from a CASCADE of
// community endpoints and caches results. The GMRC hook calls GetCode() to fill
// in the code the Steam server denied for unowned content.
//
// What a request code is, and what changed on 2026-09-09: the code is a bearer
// token. Valve signs it for (depot, manifest) and the CDN serves the manifest to
// whoever presents it — the CDN GET is anonymous, so a code minted elsewhere
// works from any machine (measured 2026-09-15: a code from 20770407.xyz fetched
// the manifest from steampipe.akamaized.net out of a codespace). The check is
// AT ISSUE TIME: since 09-09 Valve only mints a code for an account that holds a
// licence for the app. opensteamtool, wudrm and steam.run lived off the missing
// check (bare accounts asking for anything) and died that day. The provider
// below (20770407.xyz, found in huanyuejue/OpenSteamTool commit 224931d) runs a
// pool of accounts that DO own the games: Valve mints the code for them, the
// token serves everyone. Hence its Valve-shaped signature (depot AND gid — it
// resolves the app id itself, "Auto getAppid") and its structural limit: a game
// nobody in the pool owns gets "Unauthorized", the same answer as a bad gid.
//
// The provider's own status page: 10 requests per 10 s per IP (Cloudflare
// 429 with body "error code: 1015" beyond that), ~170k requests/day from
// ~70k users, and it degrades for EVERYONE when one client floods it (31 %
// failures for an hour on 2026-09-15, body "Service temporarily unavailable -
// please retry"). So this client throttles itself to one request per second,
// waits and retries a bounded number of times on those two answers, and gives
// up definitively on "Unauthorized". A caller that gets nullopt falls through
// to Steam's own path; with the manifest already in depotcache/ (LumaDeck's
// pins) Steam never asks in the first place — that is the fallback.
//
// Transport: Curl::getString (libcurl, dlopen'd at runtime). It returns 0 on
// transport success WITHOUT failing on HTTP status; the status is reported via
// its out-param and the body is classified below. A parsed uint64 is the only
// success; anything else is one of the failure classes in `Outcome`.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "curl.hpp"
#include "log.hpp"
#include "status.hpp"
#include "version.hpp"

namespace Gmrc {

namespace detail {

// Per-provider User-Agent. A non-'curl' UA is needed behind Cloudflare (the
// old opensteamtool WAF challenged the default one; 20770407.xyz is fronted by
// Cloudflare too). We identify as ourselves rather than as OpenSteamTool: the
// 20770407 status page shows a client flooding it, its users blame "OST
// scraping" (3a.lol, 2026-09-15), and if the operator ever filters or quotas
// by client we want to be a small, well-behaved client with its own name, not
// share OST's bucket. manifestdex is the exception: it answers ONLY to its own
// string (OpenSteamTool PR #200: "Sends the required User-Agent:
// ManifestDeX/1.0").
inline constexpr const char* kUserAgentLuma = "lumalinux/" LUMALINUX_VERSION_STRING;
inline constexpr const char* kUserAgentMdx  = "ManifestDeX/1.0";

// Self-imposed pacing. The provider allows 10 requests / 10 s per IP; Steam
// asks one code per depot of an install (a big game has 5–10), so one per
// second never trips the limit and never contributes to the floods that take
// the service down for everyone.
inline constexpr int kMinGapMs        = 1000;
// Bounded waits. A 429 window is 10 s by the provider's own rule; a backend
// "unavailable" clears in seconds when transient. Three attempts in total.
inline constexpr int kRateLimitWaitMs = 10000;
inline constexpr int kTransientWaitMs = 5000;
inline constexpr int kMaxAttempts     = 3;
// A provider that exhausted its attempts (or died at transport) is skipped for
// this long before being asked again. Without it, with the first provider
// down, EVERY depot of an install paid the three attempts (~12 s measured on
// 2026-09-15) before the next provider answered — a big game pays that per
// depot. A DENIED or a CDN-rejected code is an answer, not a failure, and does
// not trip this.
inline constexpr int kProviderDownSec = 60;
// Codes rotate on Valve's side (~15–20 min measured) and the provider caches
// them itself for a while, so a long-lived entry could hand Steam a stale code
// (which the CDN rejects, and Steam then abandons the whole install with
// "Unknown error" — the 2026-09-10 wudrm failure). Steam only re-asks within
// seconds (a retry of the same job), so a short TTL keeps that benefit and
// nothing else. A denial is remembered for the same span so a retrying job
// does not re-spend the request budget on a depot the pool does not own.
inline constexpr int kCacheTtlSec     = 120;
// Provider request timeouts: measured P95 is under a second; a wedged provider
// must not hold Steam's manifest job for the SafeMode-sized 15 s / 30 s.
inline constexpr long kConnectTimeoutSec = 5;
inline constexpr long kTotalTimeoutSec   = 10;

// Parse a bare uint64 body. Rejects anything that isn't a run of digits after
// optional leading whitespace. A parsed 0 is treated as "no code".
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
    // printf template. needsDepot: two %llu — depot id, then manifest gid
    // (Valve's own GetManifestRequestCode shape). Otherwise one %llu, the gid.
    const char* urlTemplate;
    bool needsDepot;
    std::optional<uint64_t> (*parse)(const std::string&);
    const char* userAgent;
};

// The cascade, in order. There are two POOLS of licensed accounts behind the
// four live providers, not four providers (measured 2026-09-15/16 over 42
// depots: wudrm, steam.run and manifestdex hand out the SAME code for the same
// manifest often enough that they share a backend, each front with its own
// cache of a few minutes; 20770407 never matches anyone). They died together
// on 09-09 and came back together on 09-16. opensteamtool is gone (Cloudflare
// 403 for any User-Agent).
//
//   20770407  pool A. Asks Valve per request: a gid nobody in its pool owns,
//             or a bad gid, comes back "Unauthorized" — a clean DENIED, the
//             cascade moves on. The only one that says "no" honestly, hence
//             first. Home-hosted, 10 req/10 s, seen degraded and down.
//   manifestdex  pool B, fastest front (120–500 ms), HTTPS, no rate limit at
//             1 req/s, needs its own User-Agent. Answers a NUMBER for any
//             gid, a made-up one included — CdnAcceptsCode is what makes it
//             usable. The free endpoint of a commercial catalogue
//             (manifestdex.com, OpenSteamTool PR #200).
//   wudrm     pool B, the pre-09-09 classic, plain HTTP, gid only. Serves a
//             Cloudflare JS challenge to the `curl` User-Agent, not to ours.
//   steamrun  pool B, JSON body, ~1 s.
// The later fronts of pool B only help when manifestdex's front is down and
// the pool is not; they cost nothing while kProviderDownSec skips the dead.
inline const Provider kProviders[] = {
    {"20770407",    "https://20770407.xyz/manifest/%llu/%llu",       true,  &ParsePlainUint,    kUserAgentLuma},
    {"manifestdex", "https://manifest.manifestdex.com/%llu",         false, &ParsePlainUint,    kUserAgentMdx},
    {"wudrm",       "http://gmrc.wudrm.com/manifest/%llu",           false, &ParsePlainUint,    kUserAgentLuma},
    {"steamrun",    "https://manifest.steam.run/api/manifest/%llu",  false, &ParseSteamRunJson, kUserAgentLuma},
};
inline constexpr size_t kProviderCount = sizeof kProviders / sizeof kProviders[0];

// Per-provider "skip until" (index into kProviders; the env override uses the
// slot past the table). Set by GetCode when a provider fails outright, read by
// GetCode and ProvidersReachable.
inline std::chrono::steady_clock::time_point& DownUntil(size_t idx) {
    static std::chrono::steady_clock::time_point t[kProviderCount + 1]{};
    return t[idx < kProviderCount ? idx : kProviderCount];
}
inline bool IsDown(size_t idx) {
    return std::chrono::steady_clock::now() < DownUntil(idx);
}
inline void MarkDown(size_t idx, const char* name, const char* why) {
    DownUntil(idx) = std::chrono::steady_clock::now() + std::chrono::seconds(kProviderDownSec);
    Log::Warn("GMRC: %s %s — skipping it for %d s", name, why, kProviderDownSec);
}

// LUMA_GMRC_URL=<template> replaces the table with a single provider for
// testing: two %llu (depot, gid). Point it at a black hole to exercise the
// pins fallback, or at a local server that answers a bogus number to check
// that a wrong code cannot corrupt an install.
inline const Provider* OverrideProvider() {
    static Provider ov{"env-override", nullptr, true, &ParsePlainUint, kUserAgentLuma};
    static const char* tmpl = std::getenv("LUMA_GMRC_URL");
    if (!tmpl || !tmpl[0]) return nullptr;
    ov.urlTemplate = tmpl;
    return &ov;
}

enum class Outcome { CODE, RATE_LIMITED, TRANSIENT, DENIED, NO_CODE, TRANSPORT };

inline const char* OutcomeName(Outcome o) {
    switch (o) {
        case Outcome::CODE:         return "code";
        case Outcome::RATE_LIMITED: return "rate-limited (429)";
        case Outcome::TRANSIENT:    return "unavailable (5xx / retry)";
        case Outcome::DENIED:       return "denied (no licence in pool / bad gid)";
        case Outcome::NO_CODE:      return "no usable code in body";
        case Outcome::TRANSPORT:    return "transport error";
    }
    return "?";
}

inline bool StartsWith(const char* s, const char* prefix) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') ++s;
    return std::strncmp(s, prefix, std::strlen(prefix)) == 0;
}

// Body + status -> class. The literal strings are what the provider answers
// (measured 2026-09-15); the status codes are the generic reading so a provider
// that changes its wording still lands in the right class.
inline Outcome Classify(long status, const std::string& body,
                        std::optional<uint64_t>* code,
                        std::optional<uint64_t> (*parse)(const std::string&)) {
    if (auto c = parse(body)) { *code = c; return Outcome::CODE; }
    const char* b = body.c_str();
    if (status == 429 || body.find("error code: 1015") != std::string::npos)
        return Outcome::RATE_LIMITED;
    if (status >= 500 || StartsWith(b, "Service temporarily unavailable"))
        return Outcome::TRANSIENT;
    if (status == 401 || status == 403 || StartsWith(b, "Unauthorized"))
        return Outcome::DENIED;
    return Outcome::NO_CODE;
}

struct CacheEntry {
    uint64_t code;      // 0 = remembered denial
    std::chrono::steady_clock::time_point until;
};
inline std::map<uint64_t, CacheEntry>& Cache() {
    static std::map<uint64_t, CacheEntry> c;
    return c;
}
inline std::mutex& Mtx() { static std::mutex m; return m; }

// Global pacing: at most one provider request per kMinGapMs, across threads.
inline void Pace() {
    static std::mutex m;
    static std::chrono::steady_clock::time_point last{};
    std::lock_guard<std::mutex> lk(m);
    auto now  = std::chrono::steady_clock::now();
    auto wait = std::chrono::milliseconds(kMinGapMs) - (now - last);
    if (last.time_since_epoch().count() != 0 && wait.count() > 0) {
        std::this_thread::sleep_for(wait);
        now = std::chrono::steady_clock::now();
    }
    last = now;
}

inline void FormatUrl(char* out, size_t n, const Provider& p, uint32_t depot, uint64_t gid) {
    if (p.needsDepot)
        std::snprintf(out, n, p.urlTemplate, (unsigned long long)depot, (unsigned long long)gid);
    else
        std::snprintf(out, n, p.urlTemplate, (unsigned long long)gid);
}

// One paced request. Fills *code on Outcome::CODE.
inline Outcome Request(const Provider& p, uint32_t depot, uint64_t gid,
                       std::optional<uint64_t>* code,
                       long connectSec = kConnectTimeoutSec, long totalSec = kTotalTimeoutSec) {
    char url[512];
    FormatUrl(url, sizeof url, p, depot, gid);
    Pace();
    std::string body;
    long status = 0;
    int rc = Curl::getString(url, body, p.userAgent, connectSec, totalSec, &status);
    if (rc != 0) {
        Log::Warn("GMRC: %s transport error (curl rc=%d) depot %u manifest %llu",
                  p.name, rc, depot, (unsigned long long)gid);
        return Outcome::TRANSPORT;
    }
    Outcome o = Classify(status, body, code, p.parse);
    if (o != Outcome::CODE) {
        std::string head = body.substr(0, 60);
        for (auto& ch : head) if (ch == '\n' || ch == '\r') ch = ' ';
        Log::Warn("GMRC: %s -> %s (HTTP %ld, body \"%s\") depot %u manifest %llu",
                  p.name, OutcomeName(o), status, head.c_str(), depot,
                  (unsigned long long)gid);
    }
    return o;
}

// Ask Valve's CDN whether `code` really unlocks (depot, gid): the same GET
// Steam is about to issue, but for one byte (Range: 0-0). 200/206 = Valve
// accepts the code; anything else = it does not, or we could not tell. Only an
// accepted code is ever handed to Steam, because a rejected one is not a soft
// failure: the CDN 401s, Steam cancels the install ("Unspecified Error"),
// parks it in Update Paused and removes it from the schedule — measured
// 2026-09-15 with a bogus code (LUMA_GMRC_URL to a local server), and the
// wudrm failure users saw on 09-10. Two CDN hosts, in case one edge is off.
// No pacing: this is Valve's CDN, not a provider.
inline const char* const kCdnHosts[] = {
    "https://steampipe.akamaized.net",
    "https://fastly.cdn.steampipe.steamcontent.com",
};

inline bool CdnAcceptsCode(uint32_t depot, uint64_t gid, uint64_t code) {
    for (const char* host : kCdnHosts) {
        char url[256];
        std::snprintf(url, sizeof url, "%s/depot/%u/manifest/%llu/5/%llu",
                      host, depot, (unsigned long long)gid, (unsigned long long)code);
        std::string body;
        long status = 0;
        int rc = Curl::getString(url, body, kUserAgentLuma, kConnectTimeoutSec, kTotalTimeoutSec,
                                 &status, "0-0");
        if (rc != 0) {
            Log::Warn("GMRC: CDN check transport error (curl rc=%d) at %s — trying next host", rc, host);
            continue;
        }
        if (status == 200 || status == 206) {
            Log::Info("GMRC: CDN accepted code %llu for depot %u manifest %llu (HTTP %ld, %s)",
                      (unsigned long long)code, depot, (unsigned long long)gid, status, host);
            return true;
        }
        Log::Warn("GMRC: CDN REJECTED code %llu for depot %u manifest %llu (HTTP %ld, %s) — "
                  "not handing it to Steam", (unsigned long long)code, depot,
                  (unsigned long long)gid, status, host);
        return false;
    }
    Log::Warn("GMRC: could not reach any CDN host to check code %llu for depot %u manifest "
              "%llu — not handing it to Steam", (unsigned long long)code, depot,
              (unsigned long long)gid);
    return false;
}

} // namespace detail

// Returns the manifest request code for (depot, gid), fetching from the
// provider cascade on first use and caching it briefly. Per provider: up to
// kMaxAttempts paced attempts, waiting on RATE_LIMITED / TRANSIENT, giving up
// at once on DENIED (that provider has no licence for the depot) and on
// TRANSPORT / NO_CODE (dead or changed). A code is returned only after Valve's
// CDN accepted it (CdnAcceptsCode); a rejected one counts as that provider's
// DENIED. nullopt = no provider had a code Valve accepts.
inline std::optional<uint64_t> GetCode(uint32_t depot, uint64_t gid) {
    using namespace detail;
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lk(Mtx());
        auto it = Cache().find(gid);
        if (it != Cache().end()) {
            if (now < it->second.until) {
                if (it->second.code) return it->second.code;
                Log::Info("GMRC: manifest %llu denied %ds ago — not re-asking yet",
                          (unsigned long long)gid, kCacheTtlSec);
                return std::nullopt;
            }
            Cache().erase(it);
        }
    }

    auto remember = [&](uint64_t code) {
        std::lock_guard<std::mutex> lk(Mtx());
        Cache()[gid] = {code, std::chrono::steady_clock::now() + std::chrono::seconds(kCacheTtlSec)};
    };

    const Provider* ov = OverrideProvider();
    const Provider* first = ov ? ov : kProviders;
    const Provider* last  = ov ? ov + 1 : kProviders + kProviderCount;

    // "Up" = at least one provider gave an answer (a code, accepted or not,
    // or a DENIED). Only a run where every provider was dead or skipped counts
    // as the providers being down — a game no pool owns is not an outage.
    bool anyAnswered = false;
    for (const Provider* p = first; p != last; ++p) {
        const size_t idx = ov ? kProviderCount : (size_t)(p - kProviders);
        if (IsDown(idx)) {
            Log::Info("GMRC: %s marked down — skipping", p->name);
            continue;
        }
        Log::Info("GMRC: fetching request code for depot %u manifest %llu from %s...",
                  depot, (unsigned long long)gid, p->name);
        Outcome lastOutcome = Outcome::TRANSPORT;
        for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
            std::optional<uint64_t> code;
            Outcome o = Request(*p, depot, gid, &code);
            lastOutcome = o;
            if (o == Outcome::CODE) {
                anyAnswered = true;
                Log::Info("GMRC: got code %llu for depot %u manifest %llu (via %s, attempt %d) — checking with the CDN",
                          (unsigned long long)*code, depot, (unsigned long long)gid,
                          p->name, attempt);
                if (CdnAcceptsCode(depot, gid, *code)) {
                    remember(*code);
                    Status::RecordGmrc(true);
                    return code;
                }
                // A number Valve does not accept: for this provider it is a "no"
                // (manifestdex answers a number for gids it cannot serve).
                remember(0);
                break;
            }
            if (o == Outcome::DENIED) {
                anyAnswered = true;
                remember(0);
                break;  // definitive for this provider: next provider, if any
            }
            if (o == Outcome::RATE_LIMITED || o == Outcome::TRANSIENT) {
                if (attempt == kMaxAttempts) break;
                int wait = (o == Outcome::RATE_LIMITED) ? kRateLimitWaitMs : kTransientWaitMs;
                Log::Info("GMRC: waiting %d ms before attempt %d/%d", wait, attempt + 1, kMaxAttempts);
                std::this_thread::sleep_for(std::chrono::milliseconds(wait));
                continue;
            }
            break;  // TRANSPORT / NO_CODE: this provider is dead or changed shape
        }
        if (lastOutcome == Outcome::TRANSPORT || lastOutcome == Outcome::NO_CODE ||
            lastOutcome == Outcome::RATE_LIMITED || lastOutcome == Outcome::TRANSIENT)
            MarkDown(idx, p->name, OutcomeName(lastOutcome));
    }

    Status::RecordGmrc(anyAnswered);
    Log::Warn("GMRC: no code for depot %u manifest %llu from any provider%s "
              "(download falls through to Steam's own path)",
              depot, (unsigned long long)gid,
              anyAnswered ? "" : " — providers DOWN");
    return std::nullopt;
}

// Liveness probe for the ShaderDepot hook. Returns true if a provider is
// answering RIGHT NOW — i.e. a request for a keyed game's shader-depot manifest
// has a chance. A keyed game's shader manifest is never in the Hubcap zip, so
// the shader pre-cache job asks GMRC for a code; if no provider can answer,
// Valve's CDN denies the manifest and Steam shows the cosmetic "No internet
// connection" popup and stalls 30 s. The hook calls this FIRST: provider up ->
// let the job run; down -> skip the pre-cache this once (Steam's own clean
// path). See RESEARCH §13.11.
//
// "Up" = one paced request with a throwaway (depot 1, gid 1) and SHORT
// timeouts comes back with anything but a transport error or a backend
// "unavailable". The throwaway is DENIED by design (nobody owns depot 1) and
// a 429 still means alive. Cached 15 s: GetShaderCacheDepot can be called more
// than once per install and the probe spends one request of the budget.
inline bool ProvidersReachable() {
    using namespace detail;
    static std::mutex probeMtx;
    static bool  cachedValid  = false;
    static bool  cachedResult = false;
    static std::chrono::steady_clock::time_point cachedAt;

    {
        std::lock_guard<std::mutex> lk(probeMtx);
        if (cachedValid &&
            std::chrono::steady_clock::now() - cachedAt < std::chrono::seconds(15))
            return cachedResult;
    }

    const Provider* ov = OverrideProvider();
    const Provider* first = ov ? ov : kProviders;
    const Provider* last  = ov ? ov + 1 : kProviders + kProviderCount;

    bool reachable = false;
    const char* which = "none";
    for (const Provider* p = first; p != last; ++p) {
        const size_t idx = ov ? kProviderCount : (size_t)(p - kProviders);
        if (IsDown(idx)) continue;   // GetCode just found it dead; don't re-spend
        std::optional<uint64_t> code;
        Outcome o = Request(*p, 1, 1, &code, /*connect*/ 3, /*total*/ 5);
        if (o == Outcome::TRANSPORT || o == Outcome::TRANSIENT) continue;
        reachable = true; which = p->name;
        break;
    }

    {
        std::lock_guard<std::mutex> lk(probeMtx);
        cachedResult = reachable;
        cachedAt     = std::chrono::steady_clock::now();
        cachedValid  = true;
    }
    Log::Info("GMRC: provider liveness probe -> %s (%s)",
              reachable ? "reachable" : "ALL DOWN", which);
    return reachable;
}

} // namespace Gmrc
