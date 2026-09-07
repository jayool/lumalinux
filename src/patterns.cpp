#include "patterns.hpp"
#include "gmrc_xref.hpp"
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

bool MatchesAt(uintptr_t addr, const char* pattern) {
    if (!addr) return false;
    auto p = ParsePattern(pattern);
    if (p.bytes.empty()) return false;

    // Bound-check against the module's r-x span: `addr` comes from outside (the
    // feed), so it must not be dereferenced on trust.
    ModuleRange r = FindModuleRangeFromMaps("steamclient.so");
    if (!r.base) return false;
    if (addr < r.base || addr + p.bytes.size() > r.base + r.size) return false;

    const uint8_t* at = reinterpret_cast<const uint8_t*>(addr);
    for (size_t j = 0; j < p.bytes.size(); ++j)
        if (p.fixed[j] && at[j] != p.bytes[j]) return false;
    return true;
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
    // Byte pattern PRIMARY, job-name xref (#13 Part 1) as the RESCUE — a rebuild
    // that reshuffles the getter's prologue but leaves the function intact breaks
    // the pattern while the xref still resolves.
    //
    // UNIQUE-match required since v0.16.x, same reasoning as FindDepotKeyFunction:
    // FindInSteamclient returns the FIRST match without counting, so an ambiguous
    // pattern yields a plausible wrong address and the caller detours it — no
    // error, no FAILED, status.json green. That risk was accepted while a 0 meant
    // "no manifest request code, no download at all"; it no longer does. The xref
    // rescues, and since 2026-09-07 it resolves the entry from .eh_frame_hdr's
    // function table (src/eh_frame.hpp) instead of inferring it from code shape,
    // so the net underneath is exact rather than a heuristic that could itself be
    // wrong.
    //
    // The xref is computed ONLY when the pattern came up empty. It used to run
    // unconditionally so a DRIFT line could be logged when the two disagreed;
    // that cost (a module-wide string scan, the GOT consensus, a lea scan, and
    // now building the .eh_frame_hdr table — order of 60 ms) bought a log line
    // that changed no decision, since the pattern won regardless. Since M2 the
    // nightly check does that comparison against every new build and can open a
    // PR, which is strictly more than a line nobody reads until something is
    // already broken. Same rule as DepotKey: compute a resolver only when it can
    // change which function gets hooked.
    uintptr_t viaPattern =
        FindUniqueInSteamclient(kGmrcFunctionPattern, "GMRC getter (GetManifestRequestCode)");
    if (viaPattern) {
        Log::Info("GMRC locate: method=pattern target=0x%lx", (unsigned long)viaPattern);
        return viaPattern;
    }

    // "MISSED" now covers two cases: the pattern matched nowhere, or it matched
    // more than once and FindUniqueInSteamclient refused to guess. Both mean the
    // same thing here — the xref decides.
    uintptr_t viaXref = GmrcXref::FindGmrcFunction();
    if (viaXref) {
        Log::Warn("GMRC locate: method=xref target=0x%lx — byte pattern MISSED or "
                  "AMBIGUOUS (Steam likely reshuffled the prologue); xref rescued "
                  "the hook", (unsigned long)viaXref);
        return viaXref;
    }

    return 0;
}

} // namespace Patterns
