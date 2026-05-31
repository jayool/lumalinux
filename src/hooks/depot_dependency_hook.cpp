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

// PATCH existing entries' MANIFEST GID only — never touches ManifestSize and
// never inject new entries (LumaCore-style):
//
//  - Size: LumaCore explicitly forces the override size to 0 with a verbatim
//    comment "to prevent incorrect size from breaking Steam" (LuaLoader.cpp:172).
//    Replicating that means leaving e.ManifestSize untouched here — Steam keeps
//    whatever size it computed from the appinfo. Earlier versions of this hook
//    overwrote it with our stored size, which is what RESEARCH.md §11.1 flags
//    as a deviation from LumaCore. Removed.
//
//  - Injection: requires the depot to appear in appinfo, which is what the
//    LoadPackage hook arranges. v0.5.4 tried injecting depots that weren't in
//    the appinfo and Steam crashed when the app was selected; never inject.
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
        if (info.manifest_gid != 0 && e.ManifestGid != info.manifest_gid) {
            Log::Info("BuildDep: PATCH app=%u depot=%u gid %llu -> %llu (size kept at %llu)",
                      AppId, e.DepotId,
                      (unsigned long long)e.ManifestGid,
                      (unsigned long long)info.manifest_gid,
                      (unsigned long long)e.ManifestSize);
            e.ManifestGid = info.manifest_gid;
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
    // in pDepotInfo. Steam must surface them itself, which it does once
    // SLSsteam's ownership injection + our LoadPackage are in place. Injection
    // from here was tested in v0.5.4 and produced a crash on app selection.
    //
    // Only pDepotInfo is patched. LumaCore explicitly skips pSharedDepotInfo
    // (ManifestBind.cpp loops only the primary vector); shared depots belong
    // to their own parent app (e.g. VC 2022 Redist = 228989 under app 228980)
    // and Steam already has them with the legitimate gids from that app's
    // appinfo. Overwriting those broke Formula Legends (heap corruption →
    // SIGSEGV in libc malloc_usable_size when Steam reached the install step).
    // See RESEARCH.md §11.4 for the diagnosis.
    size_t patched = PatchVector(pDepotInfo, AppId, byDepot);

    if (patched > 0) {
        Log::Info("BuildDep: patched %zu depot entr(ies) in pDepotInfo for app %u",
                  patched, AppId);
    } else {
        // Note: this is normal when Steam's appinfo already has the right
        // gids (no patch needed), or when our keystore has depots for this
        // app that aren't surfaced in pDepotInfo (Steam filtered them out by
        // OS/branch/language — e.g. a Windows-only redist on a Linux native
        // install).
        Log::Debug("BuildDep: app %u has %zu KeyStore depots — none required "
                   "patching in pDepotInfo (Steam already had right gids, or "
                   "Steam filtered the depots out by OS/branch).",
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
