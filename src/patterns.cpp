#include "patterns.hpp"
#include "log.hpp"

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>

namespace {

struct ModuleRange {
    uintptr_t base = 0;
    size_t    size = 0;
};

// Aggregate all r-x mappings of the .so identified by `needle` into a single
// [min_start, max_end] range. Modern Steam may split steamclient.so into many
// adjacent r-xp regions via mprotect; they're contiguous so we can treat them
// as one big scan range.
ModuleRange FindModuleRangeFromMaps(const char* needle) {
    std::ifstream maps("/proc/self/maps");
    if (!maps.is_open()) return {};

    uintptr_t min_start = static_cast<uintptr_t>(-1);
    uintptr_t max_end   = 0;
    int       count     = 0;

    std::string line;
    while (std::getline(maps, line)) {
        if (line.find(needle) == std::string::npos) continue;
        if (line.find("r-x") == std::string::npos) continue;
        auto dash = line.find('-');
        auto sp   = line.find(' ');
        if (dash == std::string::npos || sp == std::string::npos) continue;

        std::string startHex = line.substr(0, dash);
        std::string endHex   = line.substr(dash + 1, sp - dash - 1);
        uintptr_t start = std::strtoul(startHex.c_str(), nullptr, 16);
        uintptr_t end   = std::strtoul(endHex.c_str(),   nullptr, 16);
        if (end <= start) continue;

        if (start < min_start) min_start = start;
        if (end > max_end)     max_end   = end;
        count++;
    }

    if (count == 0) return {};
    return ModuleRange{min_start, max_end - min_start};
}

struct ParsedPattern {
    std::vector<uint8_t> bytes;
    std::vector<bool>    fixed;
};

ParsedPattern ParsePattern(const char* sig) {
    ParsedPattern p;
    std::istringstream iss(sig);
    std::string tok;
    while (iss >> tok) {
        if (tok == "?" || tok == "??") {
            p.bytes.push_back(0);
            p.fixed.push_back(false);
        } else {
            p.bytes.push_back(static_cast<uint8_t>(std::strtoul(tok.c_str(), nullptr, 16)));
            p.fixed.push_back(true);
        }
    }
    return p;
}

uintptr_t SigScan(uintptr_t base, size_t size, const ParsedPattern& p) {
    if (p.bytes.empty() || size < p.bytes.size()) return 0;
    const uint8_t* haystack = reinterpret_cast<const uint8_t*>(base);
    const size_t   patLen   = p.bytes.size();
    const size_t   scanEnd  = size - patLen;

    for (size_t i = 0; i <= scanEnd; i++) {
        bool match = true;
        for (size_t j = 0; j < patLen; j++) {
            if (p.fixed[j] && haystack[i + j] != p.bytes[j]) {
                match = false;
                break;
            }
        }
        if (match) return base + i;
    }
    return 0;
}

uintptr_t FindInSteamclient(const char* pattern, const char* logName) {
    ModuleRange r = FindModuleRangeFromMaps("steamclient.so");
    if (!r.base) return 0;

    auto parsed = ParsePattern(pattern);
    if (parsed.bytes.empty()) {
        Log::Error("Patterns: %s — empty/invalid pattern string", logName);
        return 0;
    }

    uintptr_t found = SigScan(r.base, r.size, parsed);
    if (!found) {
        Log::Error("Patterns: %s — pattern NOT FOUND in steamclient.so. "
                   "Steam likely updated; re-derive the patterns (see docs/RESEARCH.md "
                   "section 8, and tools/ghidra_find_gmrc.py).",
                   logName);
        return 0;
    }

    Log::Info("Patterns: %s found at 0x%lx (RVA 0x%lx)",
              logName,
              (unsigned long)found,
              (unsigned long)(found - r.base));
    return found;
}

// Like FindInSteamclient but requires a UNIQUE match: returns the address only if
// the pattern matches EXACTLY ONE site, else 0. FindInSteamclient takes the FIRST
// match without counting, so on a Steam update that makes a pattern ambiguous it
// returns a plausible wrong address and the caller detours it — mid-function at
// best, a crash at worst, and silently either way: no error, no FAILED, status.json
// green.
//
// Originally for non-critical hooks only, on the reasoning that a load-bearing
// hook cannot afford to resolve to nothing. That reasoning held only while there
// was no second opinion: a 0 meant the feature was gone. It no longer does — every
// caller here has at least one resolver behind it, so 0 means "next resolver",
// not "give up". Ambiguity is a real signal and guessing past it is never right.
uintptr_t FindUniqueInSteamclient(const char* pattern, const char* logName) {
    ModuleRange r = FindModuleRangeFromMaps("steamclient.so");
    if (!r.base) return 0;

    auto parsed = ParsePattern(pattern);
    if (parsed.bytes.empty()) {
        Log::Error("Patterns: %s — empty/invalid pattern string", logName);
        return 0;
    }

    const uint8_t* hay = reinterpret_cast<const uint8_t*>(r.base);
    const size_t patLen = parsed.bytes.size();
    uintptr_t hit = 0;
    size_t count = 0;
    for (size_t i = 0; i + patLen <= r.size; ++i) {
        bool match = true;
        for (size_t j = 0; j < patLen; ++j) {
            if (parsed.fixed[j] && hay[i + j] != parsed.bytes[j]) { match = false; break; }
        }
        if (match) {
            if (++count == 1) hit = r.base + i;
            else break;  // >1 -> ambiguous, stop
        }
    }
    if (count != 1) {
        // Neutral wording: what a 0 costs depends on who asked, so the caller
        // logs the consequence. Here we only report the ambiguity itself.
        Log::Warn("Patterns: %s — %zu match(es), need exactly 1; refusing to guess",
                  logName, count);
        return 0;
    }
    Log::Info("Patterns: %s found at 0x%lx (RVA 0x%lx)",
              logName, (unsigned long)hit, (unsigned long)(hit - r.base));
    return hit;
}

} // namespace

namespace Patterns {

uintptr_t FindSteamclientBase() {
    ModuleRange r = FindModuleRangeFromMaps("steamclient.so");
    if (r.base) {
        Log::Info("Patterns: steamclient.so r-x mapping at 0x%lx (size 0x%lx)",
                  (unsigned long)r.base, (unsigned long)r.size);
    } else {
        // Expected during early init: the ctor polls this until steamclient.so
        // is mapped (typically a few attempts), so a "not found" here is normal
        // noise, not an error. Callers (InstallHooks, the ctor poll loop) log the
        // real outcome — success, the final "never appeared", or the abort — at
        // the appropriate level, so keep this at DEBUG.
        Log::Debug("Patterns: steamclient.so r-x mapping not found yet in /proc/self/maps");
    }
    return r.base;
}

uintptr_t FindNotifyLicensesUpdatedFunction() {
    // UNIQUE-match required: this pattern is ported from moon and NOT re-verified
    // against the user's exact steamclient.so, so if it matches 0 or >1 places
    // we bail (the license reconcile then no-ops and the restart path is used).
    return FindUniqueInSteamclient(kNotifyLicensesUpdatedPattern,
                                   "NotifyLicensesUpdated");
}


uintptr_t FindDepotKeyFunction() {
    // UNIQUE-match required, since v0.16.x. This used to take the first match
    // because DepotKey is load-bearing and a 0 meant no downloads at all — but
    // that trade is gone: the hook now has the RTTI vtable constraint and the
    // name-derived resolver behind it (depot_key_hook.cpp), so an ambiguous
    // pattern degrades to "let the next resolver decide" instead of "hook
    // whatever matched first". Note the two remaining paths can still succeed
    // where this one bails: a pattern that is ambiguous across the whole module
    // may still be unique WITHIN CConfigStore's vtable, which is exactly the
    // constraint docs/rva-feed-design.md §13 describes.
    return FindUniqueInSteamclient(kDepotKeyFnPattern, "depot key KeyValues accessor");
}

uintptr_t FindBuildDepotDependencyFunction() {
    return FindInSteamclient(kBuildDepotDependencyPattern, "BuildDepotDependency");
}

uintptr_t FindGmrcFunction() {
    // Byte pattern ONLY, like every other Find*Function here: this module means
    // "locate it with the compiled byte pattern", nothing else. It used to also
    // call GmrcXref and pick between the two, which made it the one function in
    // Patterns that could return an address Patterns had not found, forced this
    // file to #include a hook-specific locator, and left the caller unable to say
    // which resolver won (it could only log method=locator). That choice now
    // lives in gmrc_hook.cpp, next to the feed, exactly like depot_key_hook.cpp.
    //
    // UNIQUE-match required since v0.16.x: FindInSteamclient returns the FIRST
    // match without counting, so an ambiguous pattern yields a plausible wrong
    // address and the caller detours it — no error, no FAILED, status.json green.
    // Returning 0 is safe now because the caller falls back to the job-name xref.
    return FindUniqueInSteamclient(kGmrcFunctionPattern,
                                   "GMRC getter (GetManifestRequestCode)");
}

uintptr_t FindLoadPackageFunction() {
    // The prologue `55 89 E5 57 ... 81 EC 1C 01 00 00` may match multiple
    // functions. Enumerate all matches; pick by index via LUMA_LOADPKG_IDX
    // (default 0). Logs all candidates so we can re-tune if a Steam update
    // shifts the offset.
    ModuleRange r = FindModuleRangeFromMaps("steamclient.so");
    if (!r.base) return 0;

    auto parsed = ParsePattern(kLoadPackagePattern);
    if (parsed.bytes.empty()) return 0;

    std::vector<uintptr_t> hits;
    const uint8_t* hay = reinterpret_cast<const uint8_t*>(r.base);
    const size_t   patLen = parsed.bytes.size();
    for (size_t i = 0; i + patLen <= r.size; ++i) {
        bool match = true;
        for (size_t j = 0; j < patLen; ++j) {
            if (parsed.fixed[j] && hay[i + j] != parsed.bytes[j]) { match = false; break; }
        }
        if (match) hits.push_back(r.base + i);
    }

    if (hits.empty()) {
        Log::Error("Patterns: LoadPackage — no candidates found");
        return 0;
    }

    for (size_t i = 0; i < hits.size(); ++i) {
        Log::Info("Patterns: LoadPackage candidate[%zu] at 0x%lx (RVA 0x%lx)",
                  i, (unsigned long)hits[i], (unsigned long)(hits[i] - r.base));
    }

    size_t idx = 0;
    if (const char* env = std::getenv("LUMA_LOADPKG_IDX")) {
        idx = static_cast<size_t>(std::strtoul(env, nullptr, 10));
        if (idx >= hits.size()) {
            Log::Warn("Patterns: LUMA_LOADPKG_IDX=%zu out of range (%zu) — using 0",
                      idx, hits.size());
            idx = 0;
        }
    }

    Log::Info("Patterns: LoadPackage selected candidate[%zu] = 0x%lx (RVA 0x%lx)",
              idx, (unsigned long)hits[idx], (unsigned long)(hits[idx] - r.base));
    return hits[idx];
}

uintptr_t FindShaderCacheDepotFunction() {
    // UNIQUE-match (not FindInSteamclient's first-match): if a Steam update makes
    // this pattern ambiguous, hooking the first (possibly wrong) match could crash
    // Steam. Non-critical, so bail to a clean no-op instead — the per-game shader
    // skip is lost (keyless games regress to the §13.8 loop, DisableShaderCache is
    // the global stop-gap), but installs are unaffected.
    return FindUniqueInSteamclient(kShaderCacheDepotPattern,
                                   "GetShaderCacheDepot (per-game shader skip)");
}

} // namespace Patterns
