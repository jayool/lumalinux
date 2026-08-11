#include "gmrc_xref.hpp"

#include "log.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>

// i386 string-xref locator for the GMRC getter. See gmrc_xref.hpp for the idea;
// this mirrors the derivation in tools/verify_gmrc_anchor.py (validated oracle).
namespace {

constexpr const char* kJobName =
    "ContentServerDirectory.GetManifestRequestCode#1";

struct Span {
    uintptr_t base = 0;
    std::size_t size = 0;
    explicit operator bool() const { return base != 0 && size != 0; }
};

// Aggregate steamclient.so's r-x mappings into one [base, size) span (same
// approach as package_zero_finder's GetSteamclientRx).
Span GetSteamclientRx() {
    std::ifstream maps("/proc/self/maps");
    if (!maps.is_open()) return {};
    uintptr_t lo = ~uintptr_t(0), hi = 0;
    std::string line;
    while (std::getline(maps, line)) {
        if (line.find("steamclient.so") == std::string::npos) continue;
        if (line.find("r-x") == std::string::npos) continue;
        auto dash = line.find('-');
        auto sp = line.find(' ');
        if (dash == std::string::npos || sp == std::string::npos) continue;
        uintptr_t s = std::strtoul(line.substr(0, dash).c_str(), nullptr, 16);
        uintptr_t e = std::strtoul(line.substr(dash + 1, sp - dash - 1).c_str(),
                                   nullptr, 16);
        if (e <= s) continue;
        if (s < lo) lo = s;
        if (e > hi) hi = e;
    }
    if (hi <= lo) return {};
    return { lo, static_cast<std::size_t>(hi - lo) };
}

// Search steamclient.so's READABLE (r--/r-x, non-writable is fine too) mappings
// for the job-name string and return its loaded address, or 0. The string lives
// in a read-only data section; we scan every readable steamclient.so mapping so
// we don't depend on section names in /proc/self/maps.
uintptr_t FindStringLoaded(const char* needle) {
    const std::size_t nlen = std::strlen(needle);
    std::ifstream maps("/proc/self/maps");
    if (!maps.is_open()) return 0;
    std::string line;
    while (std::getline(maps, line)) {
        if (line.find("steamclient.so") == std::string::npos) continue;
        if (line.size() < 5 || line[0] == '\0') continue;
        // perms field starts right after the address range + a space
        auto dash = line.find('-');
        auto sp = line.find(' ');
        if (dash == std::string::npos || sp == std::string::npos) continue;
        const std::string perms = line.substr(sp + 1, 4);
        if (perms.size() < 1 || perms[0] != 'r') continue;   // must be readable
        uintptr_t s = std::strtoul(line.substr(0, dash).c_str(), nullptr, 16);
        uintptr_t e = std::strtoul(line.substr(dash + 1, sp - dash - 1).c_str(),
                                   nullptr, 16);
        if (e <= s || (e - s) < nlen) continue;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
        const std::size_t span = static_cast<std::size_t>(e - s) - nlen;
        for (std::size_t i = 0; i <= span; ++i) {
            if (p[i] == static_cast<uint8_t>(needle[0]) &&
                std::memcmp(p + i, needle, nlen) == 0) {
                return s + i;
            }
        }
    }
    return 0;
}

// Module GOT base by consensus over PIC preambles: `call get_pc_thunk` (E8 rel32)
// immediately followed by `add reg,imm32` (05 imm32 for eax, or 81 /0 = 81 C? for
// the others). GOT = (addr after the call) + imm32; this is _GLOBAL_OFFSET_TABLE_,
// a single module-wide value, so the correct answer dominates the vote. Derived
// independently of GMRC (unlike package_zero_finder's DeriveGotBase, which anchors
// on GMRC's own prologue).
uintptr_t DeriveGotBaseConsensus(Span rx) {
    if (!rx || rx.size < 11) return 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(rx.base);
    const std::size_t n = rx.size - 11;
    std::unordered_map<uintptr_t, uint32_t> votes;
    for (std::size_t i = 0; i <= n; ++i) {
        if (p[i] != 0xE8) continue;
        const uint8_t op = p[i + 5];
        uintptr_t ret_va = rx.base + i + 5;            // eax/reg = addr after call
        int32_t imm;
        if (op == 0x05) {                              // add eax, imm32
            std::memcpy(&imm, p + i + 6, 4);
        } else if (op == 0x81 && (p[i + 6] & 0xF8) == 0xC0) {  // add r32, imm32
            std::memcpy(&imm, p + i + 7, 4);
        } else {
            continue;
        }
        uintptr_t got = ret_va + static_cast<uintptr_t>(static_cast<intptr_t>(imm));
        ++votes[got];
    }
    uintptr_t best = 0;
    uint32_t bestCount = 0;
    for (const auto& kv : votes) {
        if (kv.second > bestCount) { bestCount = kv.second; best = kv.first; }
    }
    return best;
}

// Scan .text for the UNIQUE `lea reg,[base + disp32]` (8D, mod=10, base != SIB)
// whose disp32 == disp. Returns the single site's address, or 0 on zero OR
// multiple matches (fail-closed — a reused string is not a clean anchor).
uintptr_t ScanLeaUnique(Span rx, int32_t disp) {
    if (!rx || rx.size < 6) return 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(rx.base);
    const std::size_t n = rx.size - 6;
    uintptr_t hit = 0;
    uint32_t count = 0;
    for (std::size_t i = 0; i <= n; ++i) {
        if (p[i] != 0x8D) continue;
        const uint8_t m = p[i + 1];
        if ((m & 0xC0) != 0x80 || (m & 0x07) == 0x04) continue;
        int32_t d;
        std::memcpy(&d, p + i + 2, 4);
        if (d != disp) continue;
        if (++count > 1) return 0;                     // ambiguous
        hit = rx.base + i;
    }
    return count == 1 ? hit : 0;
}

// From a code address, walk back to the nearest PIC prologue (`E8 rel32; add
// reg,imm32`) = the containing function's entry. Bounded scan.
uintptr_t WalkBackToPrologue(Span rx, uintptr_t site, std::size_t maxBack = 0x8000) {
    if (!rx || site < rx.base) return 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(rx.base);
    std::size_t off = static_cast<std::size_t>(site - rx.base);
    std::size_t lo = (off > maxBack) ? off - maxBack : 0;
    for (std::size_t k = off; ; --k) {
        if (k + 6 < rx.size && p[k] == 0xE8) {
            const uint8_t nb = p[k + 5];
            if (nb == 0x05 || (nb == 0x81 && (p[k + 6] & 0xF8) == 0xC0)) {
                return rx.base + k;
            }
        }
        if (k == lo) break;
    }
    return 0;
}

} // namespace

namespace GmrcXref {

uintptr_t FindGmrcFunction() {
    Span rx = GetSteamclientRx();
    if (!rx) {
        Log::Debug("GMRC xref: steamclient.so r-x mapping not found");
        return 0;
    }

    uintptr_t s = FindStringLoaded(kJobName);
    if (!s) {
        Log::Debug("GMRC xref: job-name string not found in steamclient.so");
        return 0;
    }

    uintptr_t got = DeriveGotBaseConsensus(rx);
    if (!got) {
        Log::Debug("GMRC xref: could not derive GOT base");
        return 0;
    }

    int32_t disp = static_cast<int32_t>(static_cast<intptr_t>(s - got));
    uintptr_t site = ScanLeaUnique(rx, disp);
    if (!site) {
        Log::Debug("GMRC xref: no unique lea for the job-name (ambiguous or absent)");
        return 0;
    }

    uintptr_t entry = WalkBackToPrologue(rx, site);
    if (!entry) {
        Log::Debug("GMRC xref: lea @ 0x%lx has no reachable prologue",
                   (unsigned long)site);
        return 0;
    }

    Log::Info("GMRC xref: derived getter at 0x%lx (RVA 0x%lx) via job-name anchor",
              (unsigned long)entry, (unsigned long)(entry - rx.base));
    return entry;
}

} // namespace GmrcXref
