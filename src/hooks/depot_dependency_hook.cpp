#include "depot_dependency_hook.hpp"
#include "../patterns.hpp"
#include "../key_store.hpp"
#include "../log.hpp"

#include <subhook.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// ── Valve internal data layouts ─────────────────────────────────────────────
//
// CUtlVector<T> (Source SDK pattern, 16 bytes header):
//   T*       m_pMemory;            // offset 0
//   int      m_nAllocationCount;   // offset 4
//   int      m_nGrowSize;          // offset 8   (set to -1 = "external buffer")
//   uint32_t m_Size;               // offset 12
//
// DepotEntry — 0x20 bytes per entry.

struct DepotEntry {
    uint32_t DepotId;          // 0x00
    uint32_t AppId;            // 0x04
    uint64_t ManifestGid;      // 0x08
    uint64_t ManifestSize;     // 0x10
    uint32_t DlcAppId;         // 0x18
    uint8_t  LcsRequired;      // 0x1C
    uint8_t  bNotNewTarget;    // 0x1D
    uint8_t  SharedInstall;    // 0x1E
    uint8_t  _pad;             // 0x1F
};
static_assert(sizeof(DepotEntry) == 0x20, "DepotEntry must be 32 bytes");

template<typename T>
struct CUtlVector {
    T*       m_pMemory;
    int      m_nAllocationCount;
    int      m_nGrowSize;
    uint32_t m_Size;
};
static_assert(sizeof(CUtlVector<DepotEntry>) == 16, "CUtlVector header must be 16 bytes");


// ── Function signature ──────────────────────────────────────────────────────
// Linux i386 cdecl — all args on stack at [ebp+0x8 .. ebp+0x24], return in eax.
using BuildDepotDependencyFn = bool (*)(
    void*                    this_,
    uint32_t                 AppId,
    void*                    pUserConfig,
    CUtlVector<DepotEntry>*  pDepotInfo,
    CUtlVector<DepotEntry>*  pSharedDepotInfo,
    void*                    pSteamApp,
    uint32_t*                pBuildId,
    bool*                    pbBetaFallback);

subhook_t              g_hook   = nullptr;
BuildDepotDependencyFn g_origFn = nullptr;


void LogDepotVector(const char* label, CUtlVector<DepotEntry>* v) {
    if (!v) {
        Log::Debug("    %s: <null>", label);
        return;
    }
    if (v->m_Size > 256 || (v->m_Size > 0 && v->m_pMemory == nullptr)) {
        Log::Warn("    %s: implausible vector (Size=%u, mem=%p) — wrong signature?",
                  label, v->m_Size, (void*)v->m_pMemory);
        return;
    }
    Log::Debug("    %s: Size=%u", label, v->m_Size);
    for (uint32_t i = 0; i < v->m_Size; ++i) {
        const DepotEntry& e = v->m_pMemory[i];
        Log::Debug("      [%u] DepotId=%u AppId=%u Gid=%llu Size=%llu DlcAppId=%u Lcs=%u Shared=%u",
                   i, e.DepotId, e.AppId,
                   (unsigned long long)e.ManifestGid,
                   (unsigned long long)e.ManifestSize,
                   e.DlcAppId,
                   (unsigned)e.LcsRequired,
                   (unsigned)e.SharedInstall);
    }
}


// Inject extra depots into pDepotInfo by replacing m_pMemory with a fresh
// malloc()-allocated buffer.
//
// Why malloc and not a static buffer? Empirically, Valve's MemAlloc_Free
// (in libtier0_s.so) ends up calling libc free() on m_pMemory when the
// CUtlVector goes out of scope. libc free() of a non-heap pointer aborts.
// malloc()'d memory IS valid input to free(), so the destructor succeeds.
//
// We leak the ORIGINAL m_pMemory because we don't know its allocator (it
// might be Valve's pool, not libc) — calling free() on it would also abort.
// The leak is bounded: a few dozen bytes per BuildDepotDependency call for
// the apps we inject into. Acceptable for a Steam session.
//
// Capped at 64 entries — more than enough for any sensible game.
constexpr size_t kMaxDepots = 64;

void InjectDepots(uint32_t AppId, CUtlVector<DepotEntry>* pDepotInfo,
                  const std::vector<KeyStore::DepotInfo>& injects) {
    uint32_t total = pDepotInfo->m_Size + static_cast<uint32_t>(injects.size());
    if (total > kMaxDepots) {
        Log::Warn("BuildDepotDependency: too many depots (orig=%u + inject=%zu > cap=%zu) — skipping",
                  pDepotInfo->m_Size, injects.size(), kMaxDepots);
        return;
    }

    auto* new_buf = static_cast<DepotEntry*>(std::malloc(total * sizeof(DepotEntry)));
    if (!new_buf) {
        Log::Error("BuildDepotDependency: malloc(%u entries) failed", total);
        return;
    }
    std::memset(new_buf, 0, total * sizeof(DepotEntry));

    // Copy original entries (could be 0..N).
    for (uint32_t i = 0; i < pDepotInfo->m_Size; ++i) {
        new_buf[i] = pDepotInfo->m_pMemory[i];
    }

    // Append injected entries.
    for (size_t i = 0; i < injects.size(); ++i) {
        DepotEntry& e = new_buf[pDepotInfo->m_Size + i];
        e.DepotId      = injects[i].depot_id;
        e.AppId        = AppId;
        e.ManifestGid  = injects[i].manifest_gid;
        e.ManifestSize = injects[i].manifest_size;
        Log::Info("BuildDepotDependency: injected DepotId=%u AppId=%u Gid=%llu Size=%llu",
                  injects[i].depot_id, AppId,
                  (unsigned long long)injects[i].manifest_gid,
                  (unsigned long long)injects[i].manifest_size);
    }

    // ★ LEAK original m_pMemory: we don't know which allocator owns it.
    // Replace with our malloc()'d buffer — Valve's MemAlloc_Free will call
    // libc free() on it, which is safe for malloc()'d memory.
    pDepotInfo->m_pMemory          = new_buf;
    pDepotInfo->m_Size             = total;
    pDepotInfo->m_nAllocationCount = static_cast<int>(total);
    // m_nGrowSize left as-is. If Valve grows the vector, realloc will hit
    // libc realloc which handles malloc'd memory fine.

    Log::Debug("BuildDepotDependency: AppId=%u — pDepotInfo now Size=%u (orig=%u + injected=%zu, buf=%p)",
               AppId, total, total - static_cast<uint32_t>(injects.size()),
               injects.size(), (void*)new_buf);
}


bool HookFn(void* this_, uint32_t AppId, void* pUserConfig,
            CUtlVector<DepotEntry>* pDepotInfo,
            CUtlVector<DepotEntry>* pSharedDepotInfo,
            void* pSteamApp, uint32_t* pBuildId, bool* pbBetaFallback) {

    if (!g_origFn) {
        Log::Error("BuildDepotDependency: no original fn pointer");
        return false;
    }

    bool result = g_origFn(this_, AppId, pUserConfig, pDepotInfo,
                           pSharedDepotInfo, pSteamApp, pBuildId, pbBetaFallback);

    Log::Debug("BuildDepotDependency: AppId=%u result=%d buildId=%u betaFallback=%d",
               AppId, (int)result,
               pBuildId ? *pBuildId : 0,
               pbBetaFallback ? (int)*pbBetaFallback : -1);
    LogDepotVector("pDepotInfo (orig)", pDepotInfo);
    LogDepotVector("pSharedDepotInfo ", pSharedDepotInfo);

    // v0.3 — inject content depots we know about for this AppId.
    if (pDepotInfo) {
        auto injects = KeyStore::GetDepotsForApp(AppId);
        if (!injects.empty()) {
            InjectDepots(AppId, pDepotInfo, injects);
            LogDepotVector("pDepotInfo (post)", pDepotInfo);
        }
    }

    return result;
}

} // namespace

namespace Hooks::DepotDependency {

bool Install() {
    uintptr_t target = Patterns::FindBuildDepotDependencyFunction();
    if (!target) {
        Log::Error("DepotDependency hook: cannot install — target not found");
        return false;
    }

    g_hook = subhook_new(reinterpret_cast<void*>(target),
                         reinterpret_cast<void*>(&HookFn),
                         static_cast<subhook_flags_t>(0));
    if (!g_hook) {
        Log::Error("DepotDependency hook: subhook_new failed");
        return false;
    }

    if (subhook_install(g_hook) != 0) {
        Log::Error("DepotDependency hook: subhook_install failed (target=0x%lx)",
                   (unsigned long)target);
        subhook_free(g_hook);
        g_hook = nullptr;
        return false;
    }

    g_origFn = reinterpret_cast<BuildDepotDependencyFn>(subhook_get_trampoline(g_hook));
    if (!g_origFn) {
        Log::Error("DepotDependency hook: subhook_get_trampoline returned null");
        subhook_remove(g_hook);
        subhook_free(g_hook);
        g_hook = nullptr;
        return false;
    }

    Log::Info("DepotDependency hook: INSTALLED (target=0x%lx, trampoline=%p) [injection mode]",
              (unsigned long)target, (void*)g_origFn);
    return true;
}

void Uninstall() {
    if (!g_hook) return;
    subhook_remove(g_hook);
    subhook_free(g_hook);
    g_hook = nullptr;
    g_origFn = nullptr;
    Log::Info("DepotDependency hook: uninstalled");
}

} // namespace Hooks::DepotDependency
