// RTTI vtable-slot resolver — mirrors CloudRedirect's
// src/platform/linux/vtable_hook.cpp::FindVtableByRTTIName, stripped to
// RESOLVE-ONLY (no vtable writes: lumalinux detours the resolved function, it
// does not swap the slot). Linux i386, same target (steamclient.so). See
// rtti.hpp and RESEARCH §15.
#include "rtti.hpp"
#include "log.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <unordered_map>
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

    // steamclient.so's segments can be mapped far apart (observed ~70 MiB between
    // .text and .data.rel.ro on some builds), so a fixed +/-16 MiB window around
    // the labeled span can miss the mapping that holds the vtable. Capture EVERY
    // readable mapping LABELED steamclient.so unconditionally (it belongs to the
    // module however far it sits), plus readable ANONYMOUS mappings within
    // +/-16 MiB of the labeled span (for a .data.rel.ro that landed just outside
    // with no name).
    const uintptr_t adj = 16ULL * 1024 * 1024;
    const uintptr_t lo = (labeledMin > adj) ? labeledMin - adj : 0;
    const uintptr_t hi = labeledMax + adj;
    rewind(f);
    while (fgets(line, sizeof(line), f)) {
        unsigned long s, e; char perms[5] = {};   // %lx targets
        if (sscanf(line, "%lx-%lx %4s", &s, &e, perms) < 3) continue;
        if (perms[0] != 'r') continue;
        const bool labeled  = strstr(line, "steamclient.so") != nullptr;
        const bool inWindow = !(e <= lo || s >= hi);
        if (labeled || inWindow) readable.push_back({s, e});
    }
    fclose(f);
    Log::Debug("RTTI: captured %zu readable range(s) for steamclient.so", readable.size());
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

    // Resolve the vtable. The first pointer-to-name-string is not always the real
    // type_info (there can be decoy references, and builds that map segments at
    // different biases make single-base RVA math unreliable), so enumerate EVERY
    // candidate type_info and accept the vtable header [offset_to_top=0, &type_info]
    // for any of them — matching the type_info pointer in either relocated
    // (absolute address) or unrelocated (file vaddr = addr - base) form.
    // Fail-closed: a wrong vtable is harmless because ResolveVtableSlotBySignature
    // only accepts a slot whose target matches the accessor byte pattern.
    std::vector<uintptr_t> tiCandidates;   // runtime addresses of type_info objects
    for (int pass = 0; pass < 2; ++pass) {
        uintptr_t target = (pass == 0) ? strAddr : strVaddr;
        if (pass == 1 && strVaddr == strAddr) break;
        for (const auto& r : readable) {
            const uintptr_t* p = reinterpret_cast<const uintptr_t*>(
                (r.start + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1));
            const uintptr_t* e = reinterpret_cast<const uintptr_t*>(
                r.end & ~(sizeof(uintptr_t) - 1));
            for (; p < e; ++p)
                if (*p == target)
                    tiCandidates.push_back(reinterpret_cast<uintptr_t>(p - 1));
        }
    }

    // Values a vtable's type_info slot may hold -> the type_info it points at.
    std::unordered_map<uintptr_t, uintptr_t> accept;
    for (uintptr_t ti : tiCandidates) {
        accept[ti] = ti;                        // relocated (absolute)
        if (ti > base) accept[ti - base] = ti;  // unrelocated (file vaddr)
    }

    for (const auto& r : readable) {
        const uintptr_t* p = reinterpret_cast<const uintptr_t*>(
            (r.start + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1));
        const uintptr_t* e = reinterpret_cast<const uintptr_t*>(
            r.end & ~(sizeof(uintptr_t) - 1));
        for (; p + 1 < e; ++p) {
            if (*p != 0) continue;
            auto it = accept.find(*(p + 1));
            if (it != accept.end()) {
                vtable = reinterpret_cast<void**>(const_cast<uintptr_t*>(p + 2));
                Log::Info("RTTI: '%s' vtable at %p (typeinfo=0x%lx, %zu candidate(s))",
                          mangledName, (void*)vtable, (unsigned long)it->second,
                          tiCandidates.size());
                return true;
            }
        }
    }

    Log::Warn("RTTI: vtable for '%s' not found (%zu type-name pointer candidate(s), "
              "first typeinfo=0x%lx)", mangledName, tiCandidates.size(),
              (unsigned long)typeinfoAddr);
    return false;
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

} // namespace Rtti
