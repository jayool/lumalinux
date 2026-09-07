// RTTI vtable-slot resolver — mirrors CloudRedirect's
// src/platform/linux/vtable_hook.cpp::FindVtableByRTTIName, stripped to
// RESOLVE-ONLY (no vtable writes: lumalinux detours the resolved function, it
// does not swap the slot). Linux i386, same target (steamclient.so). See
// rtti.hpp and RESEARCH §15.
#include "rtti.hpp"
#include "gmrc_xref_core.hpp"   // Region + DeriveGotBaseConsensus + ScanLeaAll
#include "log.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

struct Range { uintptr_t start; uintptr_t end; };

// Safe read probe: write() to /dev/null returns EFAULT instead of SIGSEGV on an
// unreadable address, so we can test a page before dereferencing it.
bool CanReadMemory(const void* addr, size_t len) {
    static int devnull = -1;
    if (devnull < 0) devnull = open("/dev/null", O_WRONLY | O_CLOEXEC);
    if (devnull < 0) return false;
    return write(devnull, addr, len) == static_cast<ssize_t>(len);
}

// Steam's loader hides steamclient.so from dl_iterate_phdr, so parse
// /proc/self/maps. Split PT_LOAD mappings mean the lowest labeled address can be
// offset into the file; anchor the true base on the ELF magic scanning backward.
uintptr_t FindElfBaseBackward(uintptr_t hint) {
    const long pageSize = sysconf(_SC_PAGESIZE);
    const uintptr_t pageMask = ~static_cast<uintptr_t>(pageSize - 1);
    uintptr_t page = hint & pageMask;
    const uintptr_t limit = (page > (64ULL * 1024 * 1024)) ? page - (64ULL * 1024 * 1024) : 0;
    while (page >= limit && page != 0) {
        if (CanReadMemory(reinterpret_cast<void*>(page), 4)) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(page);
            if (p[0] == 0x7F && p[1] == 'E' && p[2] == 'L' && p[3] == 'F')
                return page;
        }
        if (page < static_cast<uintptr_t>(pageSize)) break;
        page -= pageSize;
    }
    return 0;
}

// Gather steamclient.so's base/size and its readable mappings (.rodata and
// .data.rel.ro — where the string/type_info/vtable live — are r-- / rw-, not
// r-x, so we cannot reuse the executable-only range the pattern scanner uses).
bool MapSteamclient(uintptr_t& base, size_t& size, std::vector<Range>& readable) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return false;

    uintptr_t labeledMin = 0, labeledMax = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        unsigned long s, e;   // sscanf %lx target; assigned to uintptr_t below
        if (sscanf(line, "%lx-%lx", &s, &e) < 2) continue;
        if (strstr(line, "steamclient.so")) {
            if (labeledMin == 0 || s < labeledMin) labeledMin = s;
            if (e > labeledMax) labeledMax = e;
        }
    }
    if (labeledMin == 0) { fclose(f); return false; }

    base = FindElfBaseBackward(labeledMin);
    if (base == 0) base = labeledMin;

    // .data.rel.ro can land in anonymous mappings just outside the labeled span;
    // capture readable ranges within +/-16 MiB and bound the walk by [base,size].
    const uintptr_t adj = 16ULL * 1024 * 1024;
    const uintptr_t lo = (base > adj) ? base - adj : 0;
    const uintptr_t hi = labeledMax + adj;
    rewind(f);
    while (fgets(line, sizeof(line), f)) {
        unsigned long s, e; char perms[5] = {};   // %lx targets
        if (sscanf(line, "%lx-%lx %4s", &s, &e, perms) < 3) continue;
        if (e <= lo || s >= hi) continue;
        if (perms[0] == 'r') readable.push_back({s, e});
    }
    fclose(f);
    size = labeledMax - base;
    return true;
}

// First occurrence of `needle` across the readable ranges (used for the RTTI
// type-name string, which is a NUL-terminated literal in .rodata).
const uint8_t* FindBytes(const std::vector<Range>& readable, const void* needle, size_t len) {
    for (const auto& r : readable) {
        if (r.end - r.start < len) continue;
        const uint8_t* start = reinterpret_cast<const uint8_t*>(r.start);
        const uint8_t* end = reinterpret_cast<const uint8_t*>(r.end);
        for (const uint8_t* p = start; p <= end - len; ++p)
            if (memcmp(p, needle, len) == 0) return p;
    }
    return nullptr;
}

// Address of the first word equal to `primary` (then `fallback`) across readable
// ranges. The type_info's name pointer holds the string's absolute address once
// relocated, or the file vaddr before relocation — try both.
const uintptr_t* FindPointerValue(const std::vector<Range>& readable,
                                  uintptr_t primary, uintptr_t fallback) {
    for (int pass = 0; pass < 2; ++pass) {
        uintptr_t target = (pass == 0) ? primary : fallback;
        if (pass == 1 && primary == fallback) break;
        for (const auto& r : readable) {
            const uintptr_t* p = reinterpret_cast<const uintptr_t*>(
                (r.start + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1));
            const uintptr_t* e = reinterpret_cast<const uintptr_t*>(
                r.end & ~(sizeof(uintptr_t) - 1));
            for (; p < e; ++p)
                if (*p == target) return p;
        }
    }
    return nullptr;
}

// Walk RTTI for `mangledName`: type-name string -> type_info -> vtable. On
// success sets `base`/`size` (the steamclient.so span) and `vtable` (pointing at
// the first virtual fn pointer, index 0) and returns true. Shared by both public
// resolvers so the fixed-slot and derive-by-signature paths use the exact same
// anchor walk (incl. the .data.rel.ro relocation wait).
bool FindTypeVtable(const char* mangledName, uintptr_t& base, size_t& size,
                    void**& vtable) {
    base = 0; size = 0; vtable = nullptr;
    std::vector<Range> readable;
    if (!MapSteamclient(base, size, readable) || readable.empty()) {
        Log::Warn("RTTI: steamclient.so not mapped / no readable ranges");
        return false;
    }

    const size_t nameLen = strlen(mangledName);
    const uint8_t* str = FindBytes(readable, mangledName, nameLen + 1);
    if (!str) {
        Log::Warn("RTTI: type-name '%s' not found in steamclient.so", mangledName);
        return false;
    }
    uintptr_t strAddr = reinterpret_cast<uintptr_t>(str);
    uintptr_t strVaddr = strAddr - base;  // pre-relocation file vaddr

    const uintptr_t* nameField = FindPointerValue(readable, strAddr, strVaddr);
    if (!nameField) {
        Log::Warn("RTTI: type_info for '%s' not found", mangledName);
        return false;
    }
    // type_info begins one pointer slot before its name field.
    const uintptr_t* typeinfo = nameField - 1;
    uintptr_t typeinfoAddr = reinterpret_cast<uintptr_t>(typeinfo);

    // The vtable's type_info pointer holds the ABSOLUTE typeinfoAddr only once
    // .data.rel.ro relocations are applied. Poll (cap 30s) if pending; normally
    // already done by the time hooks install, so this exits immediately.
    if (*nameField != strAddr) {
        volatile const uintptr_t* slot0 = nameField;
        int waitMs = 0;
        while (*slot0 != strAddr && waitMs < 30000) { usleep(50000); waitMs += 50; }
        if (*slot0 != strAddr) {
            Log::Warn("RTTI: relocations for '%s' did not complete in %dms", mangledName, waitMs);
            return false;
        }
        if (waitMs) Log::Info("RTTI: relocations for '%s' completed after %dms", mangledName, waitMs);
    }

    // vtable: Itanium header is [offset_to_top=0, type_info*]; the virtual fn
    // pointers start right after it.
    for (const auto& r : readable) {
        const uintptr_t* p = reinterpret_cast<const uintptr_t*>(
            (r.start + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1));
        const uintptr_t* e = reinterpret_cast<const uintptr_t*>(
            r.end & ~(sizeof(uintptr_t) - 1));
        for (; p + 1 < e; ++p) {
            if (*p == 0 && *(p + 1) == typeinfoAddr) {
                vtable = reinterpret_cast<void**>(const_cast<uintptr_t*>(p + 2));
                break;
            }
        }
        if (vtable) break;
    }
    if (!vtable) {
        Log::Warn("RTTI: vtable for '%s' not found (typeinfo=0x%lx)",
                  mangledName, (unsigned long)typeinfoAddr);
        return false;
    }
    return true;
}

// Minimal "AA BB ?? .." matcher (same syntax as the k*Pattern literals), kept
// local so rtti keeps its own memory-safety (CanReadMemory before dereferencing
// a vtable target) and stays independent of the Patterns module. Returns the
// matched byte length, or 0 on parse error / mismatch / unreadable target.
size_t MatchSignature(uintptr_t addr, const char* sig) {
    std::vector<uint8_t> bytes;
    std::vector<bool> fixed;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (const char* p = sig; *p; ) {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (p[0] == '?') { bytes.push_back(0); fixed.push_back(false); ++p; if (*p == '?') ++p; }
        else {
            int hi = hex(p[0]);
            int lo = p[1] ? hex(p[1]) : -1;
            if (hi < 0 || lo < 0) return 0;   // malformed pattern
            bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
            fixed.push_back(true);
            p += 2;
        }
    }
    if (bytes.empty()) return 0;
    if (!CanReadMemory(reinterpret_cast<void*>(addr), bytes.size())) return 0;
    const uint8_t* mem = reinterpret_cast<const uint8_t*>(addr);
    for (size_t i = 0; i < bytes.size(); ++i)
        if (fixed[i] && mem[i] != bytes[i]) return 0;
    return bytes.size();
}

// Aggregate steamclient.so's r-x mappings into one executable Region. Separate
// from MapSteamclient's `readable` list (which is r--/rw- too) because the GOT
// consensus and the lea scan must not walk data pages.
GmrcXrefCore::Region ExecRegion() {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return {};
    uintptr_t lo = ~uintptr_t(0), hi = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        unsigned long s, e; char perms[5] = {};
        if (sscanf(line, "%lx-%lx %4s", &s, &e, perms) < 3) continue;
        if (!strstr(line, "steamclient.so")) continue;
        if (perms[0] != 'r' || perms[2] != 'x') continue;
        if (s < lo) lo = s;
        if (e > hi) hi = e;
    }
    fclose(f);
    if (hi <= lo) return {};
    return { reinterpret_cast<const uint8_t*>(lo),
             static_cast<std::size_t>(hi - lo), lo };
}

// Every occurrence of `needle` across the readable ranges (FindBytes returns
// only the first). Steam emits its interface name tables more than once, so a
// method name legitimately appears several times and the caller must try them
// all — see docs/slssteam-plugins-analysis.md §7.5.a.
void FindAllBytes(const std::vector<Range>& readable, const void* needle,
                  size_t len, std::vector<uintptr_t>& out, size_t maxHits) {
    for (const auto& r : readable) {
        if (r.end - r.start < len) continue;
        const uint8_t* start = reinterpret_cast<const uint8_t*>(r.start);
        const uint8_t* end = reinterpret_cast<const uint8_t*>(r.end);
        for (const uint8_t* p = start; p <= end - len; ++p) {
            if (memcmp(p, needle, len) != 0) continue;
            // Require a NUL before it too: `needle` already carries its own
            // terminator (so "GetBinary\0" cannot match inside
            // "GetBinaryWatermarked"), but without this a name could still match
            // the TAIL of a longer literal ending in the same word.
            if (p > start && p[-1] != 0) continue;
            out.push_back(reinterpret_cast<uintptr_t>(p));
            if (out.size() >= maxHits) return;
        }
    }
}

// Read up to `maxSlots` code pointers out of an already-located vtable.
void ReadSlots(void** vtable, uintptr_t base, size_t size, int maxSlots,
               std::vector<uintptr_t>& out) {
    for (int i = 0; i < maxSlots; ++i) {
        if (!CanReadMemory(&vtable[i], sizeof(void*))) break;
        uintptr_t fn = reinterpret_cast<uintptr_t>(vtable[i]);
        if (fn < base || fn >= base + size) break;   // past the end of the vtable
        out.push_back(fn);
    }
}

} // namespace

namespace Rtti {

uintptr_t ResolveVtableSlot(const char* mangledName, int slot) {
    uintptr_t base = 0; size_t size = 0; void** vtable = nullptr;
    if (!FindTypeVtable(mangledName, base, size, vtable)) return 0;

    if (!CanReadMemory(vtable, (slot + 1) * sizeof(void*))) {
        Log::Warn("RTTI: vtable for '%s' at %p but slot %d not readable",
                  mangledName, (void*)vtable, slot);
        return 0;
    }
    uintptr_t fn = reinterpret_cast<uintptr_t>(vtable[slot]);
    if (fn < base || fn >= base + size) {
        Log::Warn("RTTI: '%s' slot %d (0x%lx) outside steamclient [0x%lx,0x%lx)",
                  mangledName, slot, (unsigned long)fn, (unsigned long)base,
                  (unsigned long)(base + size));
        return 0;
    }

    Log::Info("RTTI: '%s' vtable at %p, slot %d -> 0x%lx (RVA 0x%lx)",
              mangledName, (void*)vtable, slot, (unsigned long)fn,
              (unsigned long)(fn - base));
    return fn;
}

uintptr_t ResolveVtableSlotBySignature(const char* mangledName,
                                       const char* sigPattern,
                                       int maxSlots, int* outSlot) {
    uintptr_t base = 0; size_t size = 0; void** vtable = nullptr;
    if (!FindTypeVtable(mangledName, base, size, vtable)) return 0;

    uintptr_t hitFn = 0;
    int hitSlot = -1;
    int matches = 0;
    for (int i = 0; i < maxSlots; ++i) {
        // Bound the scan: stop the moment a slot pointer itself is unreadable
        // (walked past the mapped vtable). Out-of-module / non-code entries are
        // skipped, not fatal — only the accessor's prologue can match the sig.
        if (!CanReadMemory(&vtable[i], sizeof(void*))) break;
        uintptr_t fn = reinterpret_cast<uintptr_t>(vtable[i]);
        if (fn < base || fn >= base + size) continue;
        if (MatchSignature(fn, sigPattern)) {
            ++matches;
            hitFn = fn;
            hitSlot = i;
            Log::Info("RTTI: '%s' slot %d -> 0x%lx (RVA 0x%lx) matches accessor signature",
                      mangledName, i, (unsigned long)fn, (unsigned long)(fn - base));
        }
    }
    if (matches != 1) {
        Log::Warn("RTTI: '%s' signature scan matched %d slots (need exactly 1) — fail closed",
                  mangledName, matches);
        return 0;
    }
    if (outSlot) *outSlot = hitSlot;
    return hitFn;
}

// See rtti.hpp. Mirrors SLSsteam's VFTableInfo_t::init(), with the lookup
// inverted: it builds the whole `name -> index` map for an interface because it
// needs 37 of them; we need one, so we find the string first and then ask which
// slot references it. Verified against build bc54101b29 by
// tools/experiment_ifacemap_slot.py (§7.5.a): slot 6 -> 0x11a4500, the same
// address the byte pattern yields, with zero false positives on the lea scan.
uintptr_t ResolveVtableSlotByName(const char* mapMangledName,
                                  const char* methodName,
                                  const char* implMangledName,
                                  int maxSlots, int* outSlot) {
    if (outSlot) *outSlot = -1;

    uintptr_t base = 0; size_t size = 0;
    std::vector<Range> readable;
    if (!MapSteamclient(base, size, readable) || readable.empty()) {
        Log::Warn("RTTI byname: steamclient.so not mapped");
        return 0;
    }

    // (1) every exact, NUL-terminated occurrence of the method name.
    const size_t nlen = strlen(methodName);
    std::vector<uint8_t> needle(nlen + 1);
    memcpy(needle.data(), methodName, nlen + 1);
    std::vector<uintptr_t> strAddrs;
    FindAllBytes(readable, needle.data(), nlen + 1, strAddrs, /*maxHits=*/16);
    if (strAddrs.empty()) {
        Log::Warn("RTTI byname: method name '%s' not found as a string", methodName);
        return 0;
    }

    // (2) module GOT base — the anchor the PIC lea displacements are relative to.
    GmrcXrefCore::Region rx = ExecRegion();
    if (!rx) {
        Log::Warn("RTTI byname: steamclient.so r-x mapping not found");
        return 0;
    }
    const uintptr_t got = GmrcXrefCore::DeriveGotBaseConsensus(rx);
    if (!got) {
        Log::Warn("RTTI byname: could not derive GOT base");
        return 0;
    }

    // (3) the interface-map vtable, and its slots sorted by address so a
    // reference can be attributed to the function that contains it.
    uintptr_t mapBase = 0; size_t mapSize = 0; void** mapVt = nullptr;
    if (!FindTypeVtable(mapMangledName, mapBase, mapSize, mapVt)) return 0;
    std::vector<uintptr_t> slots;
    ReadSlots(mapVt, mapBase, mapSize, maxSlots, slots);
    if (slots.empty()) {
        Log::Warn("RTTI byname: '%s' vtable has no code slots", mapMangledName);
        return 0;
    }
    std::vector<std::pair<uintptr_t, int>> byAddr;   // (fn, slot index)
    byAddr.reserve(slots.size());
    for (size_t i = 0; i < slots.size(); ++i)
        byAddr.emplace_back(slots[i], static_cast<int>(i));
    std::sort(byAddr.begin(), byAddr.end());

    // (4) which slot references which occurrence. A reference is attributed to
    // the nearest PRECEDING slot function and only if it lands within a sane
    // function size — the same name is also referenced from unrelated code far
    // from this vtable, and attributing that would pick the wrong slot.
    constexpr size_t kMaxFnSize = 512;
    constexpr uint32_t kMaxRefs = 32;
    int best = -1;
    for (uintptr_t s : strAddrs) {
        const int32_t disp = static_cast<int32_t>(
            static_cast<intptr_t>(s) - static_cast<intptr_t>(got));
        uintptr_t refs[kMaxRefs];
        uint32_t total = 0;
        const uint32_t n = GmrcXrefCore::ScanLeaAll(rx, disp, refs, kMaxRefs, &total);
        if (total > kMaxRefs)
            Log::Debug("RTTI byname: '%s' @0x%lx has %u refs, examining %u",
                       methodName, (unsigned long)s, total, n);
        for (uint32_t k = 0; k < n; ++k) {
            auto it = std::upper_bound(
                byAddr.begin(), byAddr.end(),
                std::make_pair(refs[k], std::numeric_limits<int>::max()));
            if (it == byAddr.begin()) continue;         // before the first slot
            --it;
            if (refs[k] - it->first > kMaxFnSize) continue;  // inside some other fn
            Log::Debug("RTTI byname: '%s' @0x%lx referenced from slot %d (fn 0x%lx +%lu)",
                       methodName, (unsigned long)s, it->second,
                       (unsigned long)it->first, (unsigned long)(refs[k] - it->first));
            // Overloads: each has its OWN string in its OWN slot. A bare name
            // means the first one (SLSsteam indexes the rest as name2, name3...).
            if (best < 0 || it->second < best) best = it->second;
        }
    }
    if (best < 0) {
        Log::Warn("RTTI byname: no '%s' slot of '%s' references the name",
                  methodName, mapMangledName);
        return 0;
    }

    // (5) apply the derived index to the concrete class.
    const uintptr_t fn = ResolveVtableSlot(implMangledName, best);
    if (!fn) return 0;
    if (outSlot) *outSlot = best;
    Log::Info("RTTI byname: %s::%s -> slot %d -> 0x%lx (RVA 0x%lx)",
              implMangledName, methodName, best, (unsigned long)fn,
              (unsigned long)(fn - base));
    return fn;
}

} // namespace Rtti
