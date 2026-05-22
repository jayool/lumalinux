#include "depot_dependency_hook.hpp"
#include "../patterns.hpp"
#include "../log.hpp"

#include <subhook.h>

#include <cstdint>

namespace {

// ── Valve internal data layouts ─────────────────────────────────────────────
//
// CUtlVector<T> (Source SDK pattern, 12 + 4 = 16 bytes header):
//   T*       m_pMemory;            // offset 0
//   int      m_nAllocationCount;   // offset 4
//   int      m_nGrowSize;          // offset 8
//   uint32_t m_Size;               // offset 12
//
// DepotEntry — 0x20 bytes per entry. Field offsets match LumaCore's
// Windows-side reverse engineering (Structs.h).

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
//
// Linux i386 cdecl — all args passed on stack, return value in eax.
//
// ★ SIGNATURE DEDUCED from prologue offsets ([ebp+0x8], [ebp+0xC], ...)
//   and LumaCore Windows source. NOT YET runtime-verified — first log lines
//   will tell us if pDepotInfo is sane (m_Size < 256, m_pMemory non-garbage).

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
    // Sanity: an app with >256 depots is implausible; if we see one, our
    // signature is likely wrong and we're reading garbage.
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
    LogDepotVector("pDepotInfo      ", pDepotInfo);
    LogDepotVector("pSharedDepotInfo", pSharedDepotInfo);

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
        Log::Error("DepotDependency hook: subhook_get_trampoline returned null — "
                   "function prologue may have instructions subhook can't relocate");
        subhook_remove(g_hook);
        subhook_free(g_hook);
        g_hook = nullptr;
        return false;
    }

    Log::Info("DepotDependency hook: INSTALLED (target=0x%lx, trampoline=%p) [diagnostic mode]",
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
