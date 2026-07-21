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
// the pattern matches EXACTLY ONE site, else 0. For NON-CRITICAL hooks that must
// degrade to a clean no-op rather than risk hooking the WRONG function when a
// Steam update makes the pattern ambiguous — FindInSteamclient takes the first
// match, and a wrong first-match on a moved pattern could crash Steam. A 0 here
// disables only that non-critical feature; installs are unaffected. Used by
// ShaderDepot and NotifyLicensesUpdated (both non-load-bearing).
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
        Log::Warn("Patterns: %s — %zu match(es), need exactly 1; feature disabled "
                  "(non-critical; installs unaffected)", logName, count);
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
    return FindInSteamclient(kDepotKeyFnPattern, "depot key KeyValues accessor");
}

uintptr_t FindBuildDepotDependencyFunction() {
    return FindInSteamclient(kBuildDepotDependencyPattern, "BuildDepotDependency");
}

uintptr_t FindGmrcFunction() {
    return FindInSteamclient(kGmrcFunctionPattern, "GMRC getter (GetManifestRequestCode)");
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

} // namespace Patterns
