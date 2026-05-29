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
                   "Steam may have updated; re-extract via tools/analyze_steamclient.py.",
                   logName);
        return 0;
    }

    Log::Info("Patterns: %s found at 0x%lx (RVA 0x%lx)",
              logName,
              (unsigned long)found,
              (unsigned long)(found - r.base));
    return found;
}

} // namespace

namespace Patterns {

uintptr_t FindSteamclientBase() {
    ModuleRange r = FindModuleRangeFromMaps("steamclient.so");
    if (r.base) {
        Log::Info("Patterns: steamclient.so r-x mapping at 0x%lx (size 0x%lx)",
                  (unsigned long)r.base, (unsigned long)r.size);
    } else {
        Log::Error("Patterns: steamclient.so r-x mapping not found in /proc/self/maps");
    }
    return r.base;
}

uintptr_t FindDepotKeyFunction() {
    return FindInSteamclient(kDepotKeyFnPattern, "depot key function");
}

uintptr_t FindBuildDepotDependencyFunction() {
    return FindInSteamclient(kBuildDepotDependencyPattern, "BuildDepotDependency");
}

uintptr_t FindKeyValuesReadAsBinaryFunction() {
    return FindInSteamclient(kKeyValuesReadAsBinaryPattern, "KeyValues::ReadAsBinary");
}

uintptr_t FindGmrcFunction() {
    return FindInSteamclient(kGmrcFunctionPattern, "GMRC getter (GetManifestRequestCode)");
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

uintptr_t FindInitFromPacketFunction() {
    // The prologue of InitFromPacket is NOT unique (several funcs share
    // `55 89 e5 57 e8 .. 81 c7 ..`). Resolve it the reliable way: find the
    // call site `call InitFromPacket; pop eax; mov eax,[ebp+x]; mov ecx,[..]`
    // and follow the rel32. Verify the target actually has the expected
    // get_pc_thunk prologue; if a match resolves elsewhere, try the next.
    ModuleRange r = FindModuleRangeFromMaps("steamclient.so");
    if (!r.base) return 0;

    auto parsed = ParsePattern("E8 ?? ?? ?? ?? 58 8B 45 ?? 8B 8D");
    if (parsed.bytes.empty()) return 0;

    const uint8_t* hay = reinterpret_cast<const uint8_t*>(r.base);
    const size_t   patLen = parsed.bytes.size();
    for (size_t i = 0; i + patLen <= r.size; ++i) {
        bool match = true;
        for (size_t j = 0; j < patLen; ++j) {
            if (parsed.fixed[j] && hay[i + j] != parsed.bytes[j]) { match = false; break; }
        }
        if (!match) continue;

        uintptr_t callSite = r.base + i;
        int32_t rel = *reinterpret_cast<const int32_t*>(callSite + 1);
        uintptr_t target = callSite + 5 + static_cast<uintptr_t>(static_cast<intptr_t>(rel));
        if (target < r.base || target >= r.base + r.size) continue;

        const uint8_t* tp = reinterpret_cast<const uint8_t*>(target);
        // Accept either:
        //  - Original prologue:  55 89 E5 57 E8 .. .. .. .. 81 C7
        //  - Already-hooked:     E9 .. .. .. ..  (a previous loader, e.g. SLSsteam,
        //                                          has placed its detour here)
        // In the second case we trust the call-site verification — we've found
        // a real `call InitFromPacket`, so the target IS InitFromPacket, just
        // already detoured. Our LmHook's RelocateChainedJmp handles chaining.
        bool prologueOK = tp[0] == 0x55 && tp[1] == 0x89 && tp[2] == 0xE5 &&
                          tp[3] == 0x57 && tp[4] == 0xE8 &&
                          tp[9] == 0x81 && tp[10] == 0xC7;
        bool alreadyHooked = tp[0] == 0xE9;
        if (prologueOK || alreadyHooked) {
            Log::Info("Patterns: InitFromPacket call@0x%lx -> target 0x%lx (RVA 0x%lx)%s",
                      (unsigned long)callSite, (unsigned long)target,
                      (unsigned long)(target - r.base),
                      alreadyHooked ? " [already E9-hooked]" : "");
            return target;
        }
    }
    Log::Error("Patterns: InitFromPacket — no call-site resolved to a valid prologue");
    return 0;
}

} // namespace Patterns
