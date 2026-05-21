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

// Read /proc/self/maps and find the first r-x mapping of the .so identified by `needle`.
// Returns base address and size of that executable mapping. {0, 0} on failure.
ModuleRange FindModuleRangeFromMaps(const char* needle) {
    std::ifstream maps("/proc/self/maps");
    if (!maps.is_open()) return {};

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
        return ModuleRange{start, end - start};
    }
    return {};
}

// Parse "55 57 ?? BF 03" → bytes + mask. Mask is true (=fixed) or false (=wildcard).
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
    ModuleRange r = FindModuleRangeFromMaps("steamclient.so");
    if (!r.base) return 0;

    auto parsed = ParsePattern(kDepotKeyFnPattern);
    if (parsed.bytes.empty()) {
        Log::Error("Patterns: empty/invalid pattern string");
        return 0;
    }

    uintptr_t found = SigScan(r.base, r.size, parsed);
    if (!found) {
        Log::Error("Patterns: depot key function pattern NOT FOUND in steamclient.so. "
                   "Steam may have updated. Pattern needs to be re-extracted via Ghidra.");
        return 0;
    }

    Log::Info("Patterns: depot key function found at 0x%lx (RVA 0x%lx)",
              (unsigned long)found,
              (unsigned long)(found - r.base));
    return found;
}

} // namespace Patterns
