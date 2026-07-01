#include "depot_dependency_hook.hpp"
#include "../patterns.hpp"
#include "../key_store.hpp"
#include "../lmhook.hpp"
#include "../log.hpp"
#include "../steam_types.hpp"   // CUtlVector<T>, DepotEntry

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace {

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

// Remove the base app depot (DepotId == AppId) from pDepotInfo when it carries
// no content — i.e. it's not one of our keyed content depots. The appid-numbered
// depot is, by Steam convention, a licence/DRM placeholder with nothing to
// download. Steam still plans an install for it (the app is owned via SLSsteam)
// and asks GMRC for its manifest code; when gmrc.wudrm.com 502s that surfaces as
// the "No internet connection" popup (e.g. Silksong's depot 1030300) even though
// the real content installs fine from the Hubcab-seeded manifests. Dropping it
// from the dependency list Steam plans from — the same list BuildDep already
// patches — is lumalinux's equivalent of slsteam-moon's size-0 empty-depot drop.
//
// "Content depot" is KeyStore::IsContentDepot (parent_app_id != 0), which is
// true even in the default no-pin mode where content depots carry gid=0; the
// base placeholder is stored legacy (parent_app_id == 0) so it reads false and
// is dropped, while a game that genuinely keys its appid-depot as content reads
// true and is kept. DepotEntry is POD, so removal is a plain shift-down +
// m_Size-- (CUtlVector::Remove); the buffer (m_nAllocationCount) is untouched so
// Steam still frees it.
size_t RemoveContentlessBaseDepot(CUtlVector<DepotEntry>* v, uint32_t AppId) {
    if (!v || v->m_Size == 0 || !v->m_pMemory) return 0;
    if (v->m_Size > 256) return 0;
    size_t removed = 0;
    for (uint32_t i = 0; i < v->m_Size; /* advance below */) {
        const DepotEntry& e = v->m_pMemory[i];
        if (e.DepotId == AppId && !KeyStore::IsContentDepot(e.DepotId)) {
            Log::Info("BuildDep: DROP base app depot=%u (contentless placeholder, "
                      "gid=%llu size=%llu) so Steam won't probe its manifest",
                      e.DepotId, (unsigned long long)e.ManifestGid,
                      (unsigned long long)e.ManifestSize);
            for (uint32_t j = i + 1; j < v->m_Size; ++j)
                v->m_pMemory[j - 1] = v->m_pMemory[j];
            v->m_Size--;
            removed++;
            // don't advance i — the next entry slid into this slot
        } else {
            ++i;
        }
    }
    return removed;
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

    // LumaCore guards on result before touching pDepotInfo (ManifestBind.cpp:55).
    // If Steam's BuildDepotDependency returned false the output vector may be
    // partially constructed or invalid; iterating and writing into it is at
    // best a no-op and at worst memory corruption. Mirror the guard.
    if (!result) return result;

    // Act only on lumalinux-managed apps. HasDepot(AppId) is true when keys.txt
    // carries this app's own entry (steamidra_lite always writes it) and, unlike
    // GetDepotsForApp, doesn't need a gid pin — so this also fires in the default
    // no-pin mode where content depots carry gid=0. A genuinely-owned game not
    // managed by us has no keys.txt entry, so we leave its dependency list alone.
    if (!KeyStore::HasDepot(AppId)) return result;

    LogDepotVector("pDepotInfo(pre-patch)", pDepotInfo);
    LogDepotVector("pSharedDepotInfo(pre-patch)", pSharedDepotInfo);

    // One INFO line listing the planned depot ids, so the log shows plainly
    // whether the contentless base depot even reached pDepotInfo — diagnosing the
    // "No connection" popup no longer needs debug-level logging.
    if (pDepotInfo && pDepotInfo->m_pMemory && pDepotInfo->m_Size <= 256) {
        std::string ids;
        char b[16];
        for (uint32_t i = 0; i < pDepotInfo->m_Size; ++i) {
            std::snprintf(b, sizeof b, "%u ", pDepotInfo->m_pMemory[i].DepotId);
            ids += b;
        }
        Log::Info("BuildDep: app %u pDepotInfo depots: [ %s]", AppId, ids.c_str());
    }

    // Drop the contentless base app depot so Steam never plans/probes it (the
    // "No internet connection" popup). Runs regardless of pin mode — this was the
    // bug in v0.15.4: the drop sat behind the gid-pin early-return, which is empty
    // in no-pin mode, so it never ran.
    size_t dropped = RemoveContentlessBaseDepot(pDepotInfo, AppId);
    if (dropped > 0)
        Log::Info("BuildDep: dropped %zu contentless base depot(s) from pDepotInfo "
                  "for app %u", dropped, AppId);

    // GID pinning — no-op in the default no-pin mode (GetDepotsForApp filters
    // gid!=0). Patch existing entries only when we have pinned gids. Never inject
    // (v0.5.4 crash), never touch pSharedDepotInfo (Formula Legends heap
    // corruption — see RESEARCH.md §11.4).
    auto depots = KeyStore::GetDepotsForApp(AppId);
    if (!depots.empty()) {
        std::unordered_map<uint32_t, KeyStore::DepotInfo> byDepot;
        byDepot.reserve(depots.size());
        for (const auto& d : depots) byDepot.emplace(d.depot_id, d);
        size_t patched = PatchVector(pDepotInfo, AppId, byDepot);
        if (patched > 0)
            Log::Info("BuildDep: patched %zu depot entr(ies) in pDepotInfo for app %u",
                      patched, AppId);
    }
    return result;
}

} // namespace

namespace Hooks::DepotDependency {

bool Install() {
    uintptr_t target = Patterns::FindBuildDepotDependencyFunction();
    if (!target) {
        Log::Error("DepotDependency hook: cannot install — target not found");
        Log::Warn("Hook install: name=BuildDep method=pattern outcome=pattern_miss");
        return false;
    }

    void* tramp = nullptr;
    if (!LmHook::Install(target, reinterpret_cast<void*>(&HookFn), &tramp)) {
        Log::Error("DepotDependency hook: LmHook::Install failed (target=0x%lx)",
                   (unsigned long)target);
        Log::Warn("Hook install: name=BuildDep method=pattern target=0x%lx outcome=hook_install_failed",
                  (unsigned long)target);
        return false;
    }
    g_origFn = reinterpret_cast<BuildDepotDependencyFn>(tramp);

    Log::Info("DepotDependency hook: INSTALLED (target=0x%lx, trampoline=%p)",
              (unsigned long)target, (void*)g_origFn);
    uintptr_t base = Patterns::FindSteamclientBase();
    Log::Info("Hook install: name=BuildDep method=pattern target=0x%lx rva=0x%lx outcome=installed",
              (unsigned long)target, (unsigned long)(base ? target - base : 0));
    return true;
}

void Uninstall() {
    g_origFn = nullptr;
}

} // namespace Hooks::DepotDependency
