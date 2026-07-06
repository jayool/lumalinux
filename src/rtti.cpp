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
        uintptr_t s, e;
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
        uintptr_t s, e; char perms[5] = {};
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

} // namespace

namespace Rtti {

uintptr_t ResolveVtableSlot(const char* mangledName, int slot) {
    uintptr_t base = 0; size_t size = 0;
    std::vector<Range> readable;
    if (!MapSteamclient(base, size, readable) || readable.empty()) {
        Log::Warn("RTTI: steamclient.so not mapped / no readable ranges");
        return 0;
    }

    const size_t nameLen = strlen(mangledName);
    const uint8_t* str = FindBytes(readable, mangledName, nameLen + 1);
    if (!str) {
        Log::Warn("RTTI: type-name '%s' not found in steamclient.so", mangledName);
        return 0;
    }
    uintptr_t strAddr = reinterpret_cast<uintptr_t>(str);
    uintptr_t strVaddr = strAddr - base;  // pre-relocation file vaddr

    const uintptr_t* nameField = FindPointerValue(readable, strAddr, strVaddr);
    if (!nameField) {
        Log::Warn("RTTI: type_info for '%s' not found", mangledName);
        return 0;
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
            return 0;
        }
        if (waitMs) Log::Info("RTTI: relocations for '%s' completed after %dms", mangledName, waitMs);
    }

    // vtable: Itanium header is [offset_to_top=0, type_info*]; the virtual fn
    // pointers start right after it.
    void** vtable = nullptr;
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
        return 0;
    }

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

} // namespace Rtti
