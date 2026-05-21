#include "patterns.hpp"
#include "log.hpp"

#include <libmem/libmem.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <climits>

namespace {

// Read /proc/self/maps and find the load base of a module whose path contains
// the given substring (e.g. "steamclient.so").
uintptr_t FindModuleBaseFromMaps(const char* needle) {
    std::ifstream maps("/proc/self/maps");
    if (!maps.is_open()) return 0;

    std::string line;
    while (std::getline(maps, line)) {
        if (line.find(needle) == std::string::npos) continue;
        // Lines look like: "f7c00000-f8e00000 r-xp 00000000 08:01 12345 /path/to/steamclient.so"
        // We want the start of the FIRST executable mapping (r-xp).
        if (line.find("r-x") == std::string::npos) continue;
        auto dash = line.find('-');
        if (dash == std::string::npos) continue;
        std::string startHex = line.substr(0, dash);
        uintptr_t base = std::strtoul(startHex.c_str(), nullptr, 16);
        return base;
    }
    return 0;
}

} // namespace

namespace Patterns {

uintptr_t FindSteamclientBase() {
    uintptr_t base = FindModuleBaseFromMaps("steamclient.so");
    if (base) {
        Log::Info("Patterns: steamclient.so loaded at 0x%lx", (unsigned long)base);
    } else {
        Log::Error("Patterns: steamclient.so not found in /proc/self/maps — Steam may not have loaded it yet");
    }
    return base;
}

uintptr_t FindDepotKeyFunction() {
    // Use libmem's SigScan over the steamclient.so module.
    // LM_SigScan expects the pattern in IDA-style "AA BB ? CC" notation.
    uintptr_t base = FindSteamclientBase();
    if (!base) return 0;

    lm_module_t mod;
    std::memset(&mod, 0, sizeof(mod));
    if (LM_FindModule("steamclient.so", &mod) != LM_TRUE) {
        Log::Error("Patterns: LM_FindModule failed for steamclient.so");
        return 0;
    }

    lm_address_t found = LM_SigScan(kDepotKeyFnPattern, mod.base, mod.size);
    if (found == LM_ADDRESS_BAD || found == 0) {
        Log::Error("Patterns: depot key function pattern NOT FOUND in steamclient.so. "
                   "Steam may have updated. Pattern needs to be re-extracted via Ghidra.");
        return 0;
    }

    Log::Info("Patterns: depot key function found at 0x%lx (RVA 0x%lx)",
              (unsigned long)found,
              (unsigned long)(found - mod.base));
    return static_cast<uintptr_t>(found);
}

} // namespace Patterns
