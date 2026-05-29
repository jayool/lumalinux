#include "depot_dependency_hook.hpp"
#include "../patterns.hpp"
#include "../key_store.hpp"
#include "../lmhook.hpp"
#include "../log.hpp"

#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace {

// CUtlVector<T> (Source SDK, 16-byte header).
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

// PATCH existing entries' gid/size to match KeyStore. Never injects new
// entries (LumaCore-style): injection requires the depot to appear in
// appinfo, which is what the LoadPackage hook arranges. By the time we get
// here, the depots SHOULD already be in pDepotInfo with gid=0/size=0 (because
// Steam fetched the appinfo via PackageId=0 injection), and we just fill in
// the manifest pinning.
size_t PatchVector(CUtlVector<DepotEntry>* v, uint32_t AppId,
                   const std::unordered_map<uint32_t, KeyStore::DepotInfo>& byDepot) {
    if (!v || v->m_Size == 0 || !v->m_pMemory) return 0;
    if (v->m_Size > 256) return 0;
    size_t patched = 0;
    for (uint32_t i = 0; i < v->m_Size; ++i) {
        DepotEntry& e = v->m_pMemory[i];
        auto it = byDepot.find(e.DepotId);
        if (it == byDepot.end()) continue;
        const auto& info = it->second;
        // Match LumaCore (ManifestBind.cpp:66): if the override size is 0,
        // keep the original size — it affects the download-progress display
        // but not the actual download. Always pin the gid.
        uint64_t newSize = info.manifest_size ? info.manifest_size : e.ManifestSize;
        if (e.ManifestGid != info.manifest_gid || e.ManifestSize != newSize) {
            Log::Info("BuildDep: PATCH app=%u depot=%u gid %llu -> %llu, size %llu -> %llu",
                      AppId, e.DepotId,
                      (unsigned long long)e.ManifestGid,
                      (unsigned long long)info.manifest_gid,
                      (unsigned long long)e.ManifestSize,
                      (unsigned long long)newSize);
            e.ManifestGid  = info.manifest_gid;
            e.ManifestSize = newSize;
            patched++;
        }
    }
    return patched;
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

    auto depots = KeyStore::GetDepotsForApp(AppId);
    if (depots.empty()) return result;

    LogDepotVector("pDepotInfo(pre-patch)", pDepotInfo);
    LogDepotVector("pSharedDepotInfo(pre-patch)", pSharedDepotInfo);

    std::unordered_map<uint32_t, KeyStore::DepotInfo> byDepot;
    byDepot.reserve(depots.size());
    for (const auto& d : depots) byDepot.emplace(d.depot_id, d);

    // PATCH ONLY — LumaCore-style. We never inject depots that aren't already
    // in pDepotInfo/pSharedDepotInfo. Steam must surface them itself, which
    // it does once SLSsteam's ownership injection + our LoadPackage are in
    // place. Injection from here was tested in v0.5.4 and produced a crash
    // on app selection — Steam doesn't tolerate depots that bypass its
    // license-filter pass.
    size_t patched = 0;
    patched += PatchVector(pDepotInfo,       AppId, byDepot);
    patched += PatchVector(pSharedDepotInfo, AppId, byDepot);

    if (patched > 0) {
        Log::Info("BuildDep: patched %zu depot entr(ies) for app %u", patched, AppId);
    } else {
        Log::Warn("BuildDep: app %u has %zu KeyStore depots but NONE matched in "
                  "pDepotInfo/pSharedDepotInfo — Steam filtered them out "
                  "(missing license? appinfo missing depots?)",
                  AppId, depots.size());
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

    void* tramp = nullptr;
    if (!LmHook::Install(target, reinterpret_cast<void*>(&HookFn), &tramp)) {
        Log::Error("DepotDependency hook: LmHook::Install failed (target=0x%lx)",
                   (unsigned long)target);
        return false;
    }
    g_origFn = reinterpret_cast<BuildDepotDependencyFn>(tramp);

    Log::Info("DepotDependency hook: INSTALLED (target=0x%lx, trampoline=%p)",
              (unsigned long)target, (void*)g_origFn);
    return true;
}

void Uninstall() {
    g_origFn = nullptr;
}

} // namespace Hooks::DepotDependency
