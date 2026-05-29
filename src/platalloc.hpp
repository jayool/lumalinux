#pragma once

// Allocator helpers for buffers we hand back to steamclient.so (e.g. injected
// CUtlVector storage). Two options:
//
//   - Plat::Alloc()        — calls libtier0_s.so's Plat_Alloc. This is a
//     plain libc-malloc wrapper. Cheap. **But**: if Steam later frees the
//     buffer via its own IMemAlloc::Free (vtable on g_pMemAllocSteam), that
//     Free reads a hidden allocation header BEFORE the user pointer — which
//     Plat_Alloc never wrote — and libc's free detects corruption → abort().
//     Only safe if Steam never touches the buffer with its own Free.
//
//   - Plat::MemAllocAlloc() — calls g_pMemAllocSteam's vtable[0] (Alloc).
//     This is the same allocator Steam uses internally, so it writes the
//     header that its vtable[2] (Free) expects. Steam's destructors call
//     vtable[2] on m_pMemory and the round-trip is clean.
//
// For our BuildDep injection — where we hand a buffer to steamclient.so via
// pDepotInfo->m_pMemory and Steam later frees it — MemAllocAlloc is the
// correct choice.

#include <cstdint>
#include <cstddef>
#include <dlfcn.h>
#include <link.h>

namespace Plat {

using AllocFn = void* (*)(uint32_t);
using FreeFn  = void  (*)(void*);

// IMemAlloc vtable layout (Source SDK Linux i386):
//   [0]: void* Alloc(this, size_t size)
//   [1]: void* Realloc(this, void* mem, size_t size)
//   [2]: void  Free(this, void* mem)
//   ... (more entries we don't need)
using MemAllocAllocFn = void* (*)(void* this_, std::size_t size);

// Audit-namespace-aware libtier0_s.so resolver.
//
// When lumalinux is loaded via LD_AUDIT, our code runs in a separate link
// namespace; a plain dlopen() searches THAT namespace, not the main one
// where steamclient.so loaded libtier0_s.so. So we try, in order:
//   1) RTLD_NOLOAD in our own namespace (cheap; works if main namespace
//      happens to have placed it here, or if we're LD_PRELOAD-loaded).
//   2) dlmopen(LM_ID_BASE, ...) to explicitly target the main namespace.
//   3) Last resort: dlopen() — loads a fresh copy in our namespace. Symbol
//      values resolved this way may not match Steam's actual instance, so
//      this path is best-effort.
inline void* ResolveLibtier0() {
    static void* h = []() -> void* {
        if (void* tmp = dlopen("libtier0_s.so", RTLD_NOLOAD | RTLD_NOW))
            return tmp;
        if (void* tmp = dlmopen(LM_ID_BASE, "libtier0_s.so", RTLD_NOLOAD | RTLD_NOW))
            return tmp;
        if (void* tmp = dlmopen(LM_ID_BASE, "libtier0_s.so", RTLD_NOW))
            return tmp;
        return dlopen("libtier0_s.so", RTLD_NOW);
    }();
    return h;
}

inline AllocFn ResolveAlloc() {
    static AllocFn fn = []() -> AllocFn {
        void* h = ResolveLibtier0();
        if (!h) return nullptr;
        return reinterpret_cast<AllocFn>(dlsym(h, "Plat_Alloc"));
    }();
    return fn;
}

inline void* Alloc(uint32_t n) {
    AllocFn fn = ResolveAlloc();
    if (!fn) return nullptr;
    return fn(n);
}

// Resolves the g_pMemAllocSteam interface pointer. dlsym returns the address
// of the global variable (a pointer-to-IMemAlloc), which we dereference once
// to get the actual IMemAlloc*. Cached on first successful resolve.
inline void* ResolveMemAllocInterface() {
    static void* iface = []() -> void* {
        void* h = ResolveLibtier0();
        if (!h) return nullptr;
        void** symAddr = reinterpret_cast<void**>(dlsym(h, "g_pMemAllocSteam"));
        if (!symAddr) return nullptr;
        return *symAddr;  // dereference: get IMemAlloc*
    }();
    return iface;
}

// Allocates via g_pMemAllocSteam->Alloc(size). The returned pointer carries
// the allocator-private header expected by g_pMemAllocSteam->Free. Returns
// nullptr if the symbol couldn't be resolved (caller should fall back).
inline void* MemAllocAlloc(std::size_t size) {
    void* iface = ResolveMemAllocInterface();
    if (!iface) return nullptr;
    void** vtable = *reinterpret_cast<void***>(iface);
    auto Alloc = reinterpret_cast<MemAllocAllocFn>(vtable[0]);
    return Alloc(iface, size);
}

} // namespace Plat
