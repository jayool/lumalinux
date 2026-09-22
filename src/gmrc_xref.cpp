#include "gmrc_xref.hpp"

#include "eh_frame.hpp"
#include "gmrc_xref_core.hpp"
#include "log.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

// Runtime half of the string-xref locator: source the executable span and the
// anchor string from /proc/self/maps (LOADED memory), then delegate the byte
// scanning to GmrcXrefCore (shared with tools/gmrc_xref_selftest.cpp). GMRC's
// job name is the original anchor; since 2026-09-22 the same steps serve any
// string a hook can anchor on (FindFunctionByString).
namespace {

using GmrcXrefCore::Region;

constexpr const char* kJobName =
    "ContentServerDirectory.GetManifestRequestCode#1";

// One /proc/self/maps line -> [start, end) + perms, for steamclient.so only.
struct MapLine {
    uintptr_t start = 0, end = 0;
    char perms[5] = {0};
    bool ok = false;
};

MapLine ParseSteamclientLine(const std::string& line) {
    MapLine m;
    if (line.find("steamclient.so") == std::string::npos) return m;
    auto dash = line.find('-');
    auto sp = line.find(' ');
    if (dash == std::string::npos || sp == std::string::npos) return m;
    m.start = std::strtoul(line.substr(0, dash).c_str(), nullptr, 16);
    m.end = std::strtoul(line.substr(dash + 1, sp - dash - 1).c_str(), nullptr, 16);
    std::string perms = line.substr(sp + 1, 4);
    std::strncpy(m.perms, perms.c_str(), 4);
    m.ok = (m.end > m.start);
    return m;
}

// Aggregate steamclient.so's r-x mappings into one executable Region.
Region GetExecRegion() {
    std::ifstream maps("/proc/self/maps");
    if (!maps.is_open()) return {};
    uintptr_t lo = ~uintptr_t(0), hi = 0;
    std::string line;
    while (std::getline(maps, line)) {
        MapLine m = ParseSteamclientLine(line);
        if (!m.ok || m.perms[2] != 'x' || m.perms[0] != 'r') continue;
        if (m.start < lo) lo = m.start;
        if (m.end > hi) hi = m.end;
    }
    if (hi <= lo) return {};
    return { reinterpret_cast<const uint8_t*>(lo),
             static_cast<std::size_t>(hi - lo), lo };
}

// Find `needle` (nlen bytes) in any readable steamclient.so mapping -> loaded addr.
uintptr_t FindStringLoaded(const char* needle, std::size_t nlen) {
    std::ifstream maps("/proc/self/maps");
    if (!maps.is_open()) return 0;
    std::string line;
    while (std::getline(maps, line)) {
        MapLine m = ParseSteamclientLine(line);
        if (!m.ok || m.perms[0] != 'r') continue;
        Region r{ reinterpret_cast<const uint8_t*>(m.start),
                  static_cast<std::size_t>(m.end - m.start), m.start };
        uintptr_t hit = GmrcXrefCore::FindBytes(r, needle, nlen);
        if (hit) return hit;
    }
    return 0;
}

// Steps 1-4 for one string. `walkBack` enables the step-4 fallback for a build
// with no usable .eh_frame_hdr — right only for GMRC's preamble-first shape.
uintptr_t Locate(const char* needle, std::size_t nlen, const char* tag, bool walkBack) {
    Region rx = GetExecRegion();
    if (!rx) {
        Log::Debug("%s: steamclient.so r-x mapping not found", tag);
        return 0;
    }

    uintptr_t s = FindStringLoaded(needle, nlen);
    if (!s) {
        Log::Debug("%s: anchor string not found in steamclient.so", tag);
        return 0;
    }

    // Steps 1-3 (GOT base, the unique lea) are prologue-independent; step 4 —
    // turning the site into the function ENTRY — is the fragile one, and the one
    // the KNOWN LIMIT in gmrc_xref.hpp is about.
    uintptr_t got = 0;
    const uintptr_t site = GmrcXrefCore::FindUniqueLeaForString(rx, s, &got);
    if (!got) {
        Log::Debug("%s: could not derive GOT base", tag);
        return 0;
    }
    if (!site) {
        Log::Debug("%s: no unique lea for the anchor string (ambiguous or absent)", tag);
        return 0;
    }

    // Step 4, done properly: ask the binary instead of inferring from code shape.
    // .eh_frame_hdr carries the compiler's own sorted table of function starts, so
    // the entry containing `site` is a binary search away — exact, ~17 comparisons
    // instead of a backward scan of up to 32 KB, and it REJECTS an address that is
    // in no function rather than returning the nearest thing that looks like one.
    // Measured viable on build bc54101b29 (tools/experiment_eh_frame.py).
    uintptr_t ehStart = 0, ehEnd = 0;
    if (EhFrame::FindFunction(site, rx.addr + rx.size, ehStart, ehEnd)) {
        if (walkBack) {
            const uintptr_t wb = GmrcXrefCore::WalkBackToPrologue(rx, site);
            if (wb && wb != ehStart) {
                // Expected on any function whose PIC preamble is not at offset 0 —
                // the walk-back's two documented failure modes. Recorded rather
                // than silently corrected, so the frequency is visible in real logs.
                Log::Warn("%s: walk-back said 0x%lx, .eh_frame_hdr says 0x%lx "
                          "(delta %+ld) — using .eh_frame_hdr (exact)",
                          tag, (unsigned long)wb, (unsigned long)ehStart,
                          (long)(static_cast<intptr_t>(wb) - static_cast<intptr_t>(ehStart)));
            }
        }
        Log::Info("%s: function at 0x%lx (RVA 0x%lx, size %lu) via string "
                  "anchor + .eh_frame_hdr",
                  tag, (unsigned long)ehStart, (unsigned long)(ehStart - rx.addr),
                  (unsigned long)(ehEnd - ehStart));
        return ehStart;
    }

    if (!walkBack) {
        // No table -> no answer. A walk-back result for this prologue shape would
        // be a plausible WRONG address, and the caller would detour it.
        Log::Warn("%s: .eh_frame_hdr unavailable — lea @ 0x%lx found but no exact "
                  "function entry; refusing (no walk-back for this anchor)",
                  tag, (unsigned long)site);
        return 0;
    }

    // No table (old build, unreadable module, unimplemented encoding): fall back
    // to the walk-back, with its limits, rather than losing the rescue entirely.
    const uintptr_t entry = GmrcXrefCore::WalkBackToPrologue(rx, site);
    if (!entry) {
        Log::Debug("%s: lea @ 0x%lx has no reachable prologue and no "
                   ".eh_frame_hdr entry", tag, (unsigned long)site);
        return 0;
    }
    Log::Warn("%s: .eh_frame_hdr unavailable — falling back to the "
              "walk-back for 0x%lx (see the KNOWN LIMIT in gmrc_xref.hpp)",
              tag, (unsigned long)site);
    Log::Info("%s: derived function at 0x%lx (RVA 0x%lx) via string anchor",
              tag, (unsigned long)entry, (unsigned long)(entry - rx.addr));
    return entry;
}

} // namespace

namespace GmrcXref {

uintptr_t FindGmrcFunction() {
    // The bare job name, as before 2026-09-22: the string is unique in the
    // module either way, and keeping the exact needle keeps this path's
    // behaviour byte-for-byte (verified by tools/gmrc_xref_selftest.cpp).
    return Locate(kJobName, std::strlen(kJobName), "GMRC xref", /*walkBack=*/true);
}

uintptr_t FindFunctionByString(const char* needle, const char* tag) {
    // WITH the NUL terminator: "shadercachedepot" must not be found inside a
    // longer string first (a hit at the wrong address means disp is wrong and
    // the lea scan silently finds nothing). Same rule as check_patterns.py's
    // gmrc_xref_derive, the CI mirror that verified both strings on two builds.
    return Locate(needle, std::strlen(needle) + 1, tag, /*walkBack=*/false);
}

} // namespace GmrcXref
