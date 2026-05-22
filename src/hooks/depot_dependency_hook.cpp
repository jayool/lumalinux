#include "depot_dependency_hook.hpp"
#include "../patterns.hpp"
#include "../key_store.hpp"
#include "../log.hpp"

#include <subhook.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// CUtlVector<T> (Source SDK pattern, 16-byte header):
//   T* m_pMemory;            int m_nAllocationCount;
//   int m_nGrowSize;         uint32_t m_Size;

struct DepotEntry {
    uint32_t DepotId;
    uint32_t AppId;
    uint64_t ManifestGid;
    uint64_t ManifestSize;
    uint32_t DlcAppId;
    uint8_t  LcsRequired;
    uint8_t  bNotNewTarget;
    uint8_t  SharedInstall;
    uint8_t  _pad;
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
        Log::Warn("    %s: implausible vector (Size=%u, mem=%p)",
                  label, v->m_Size, (void*)v->m_pMemory);
        return;
    }
    Log::Debug("    %s: Size=%u", label, v->m_Size);
    for (uint32_t i = 0; i < v->m_Size; ++i) {
        const DepotEntry& e = v->m_pMemory[i];
        Log::Debug("      [%u] DepotId=%u AppId=%u Gid=%llu Size=%llu",
                   i, e.DepotId, e.AppId,
                   (unsigned long long)e.ManifestGid,
                   (unsigned long long)e.ManifestSize);
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

    Log::Debug("BuildDepotDependency: AppId=%u result=%d buildId=%u",
               AppId, (int)result, pBuildId ? *pBuildId : 0);
    LogDepotVector("pDepotInfo      ", pDepotInfo);
    LogDepotVector("pSharedDepotInfo", pSharedDepotInfo);

    // v0.4 recon: injection DISABLED. The KV-tree mutation in keyvalues_hook
    // is the new injection point — by the time we get here, pDepotInfo has
    // already been built from a (potentially) mutated appinfo tree.

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

    Log::Info("DepotDependency hook: INSTALLED (target=0x%lx, trampoline=%p) [diagnostic only]",
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
