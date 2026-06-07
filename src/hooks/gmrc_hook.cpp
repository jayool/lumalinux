#include "gmrc_hook.hpp"
#include "../patterns.hpp"
#include "../key_store.hpp"
#include "../gmrc_store.hpp"
#include "../lmhook.hpp"
#include "../log.hpp"

#include <cstdint>

namespace {

// int32 GetManifestRequestCode(void* this_, uint32 app_id, uint32 depot_id,
//                              uint32 manifest_lo, uint32 manifest_hi,
//                              char* branch, uint64* out_code)
// On success: writes the request code to *out_code and returns 1.
// For unowned content the Steam server denies it -> returns 0 (failure).
using GmrcFn = int32_t (*)(void*, uint32_t, uint32_t, uint32_t, uint32_t,
                           char*, uint64_t*);
GmrcFn g_origFn = nullptr;

int32_t HookFn(void* this_, uint32_t app_id, uint32_t depot_id,
               uint32_t manifest_lo, uint32_t manifest_hi,
               char* branch, uint64_t* out_code) {
    uint64_t gid = (uint64_t)manifest_lo | ((uint64_t)manifest_hi << 32);

    Log::Debug("GMRC: GetManifestRequestCode app=%u depot=%u manifest=%llu out=%p",
               app_id, depot_id, (unsigned long long)gid, (void*)out_code);

    // Intervene for manifests of our forced depots. Two ways a depot qualifies
    // as "ours":
    //   (a) its manifest_gid is pre-registered in keys.txt (Extended format) —
    //       the common case for content depots seeded by steamidra_lite.
    //   (b) its depot_id is in the KeyStore but the specific manifest_gid was
    //       not pre-registered. Happens for the app/shader depot when Steam
    //       requests its own manifest (e.g. shader pre-cache: depot==appid)
    //       using a manifest_gid that came from PICS appinfo, not from the
    //       Hubcap .lua. Without (b) the hook falls through and the CDN
    //       denies the download with Access Denied, which Steam surfaces as
    //       "No connection" in the UI.
    // Everything else still goes to Steam's normal (owned) path.
    if (out_code && (KeyStore::HasManifestGid(gid) || KeyStore::HasDepot(depot_id))) {
        auto code = Gmrc::GetCode(gid);
        if (code) {
            *out_code = *code;
            Log::Info("GMRC: INJECTED request code %llu for manifest %llu (depot %u)",
                      (unsigned long long)*code, (unsigned long long)gid, depot_id);
            return 1;  // success — Steam proceeds to download the manifest
        }
        Log::Warn("GMRC: no code available for manifest %llu — falling through "
                  "(download will fail with Access Denied)", (unsigned long long)gid);
    }

    if (!g_origFn) return 0;
    return g_origFn(this_, app_id, depot_id, manifest_lo, manifest_hi, branch, out_code);
}

} // namespace

namespace Hooks::Gmrc {

bool Install() {
    uintptr_t target = Patterns::FindGmrcFunction();
    if (!target) {
        Log::Error("GMRC hook: GetManifestRequestCode function not found");
        Log::Warn("Hook install: name=GMRC method=pattern outcome=pattern_miss");
        return false;
    }
    void* tramp = nullptr;
    if (!LmHook::Install(target, reinterpret_cast<void*>(&HookFn), &tramp)) {
        Log::Error("GMRC hook: LmHook::Install failed (target=0x%lx)",
                   (unsigned long)target);
        Log::Warn("Hook install: name=GMRC method=pattern target=0x%lx outcome=hook_install_failed",
                  (unsigned long)target);
        return false;
    }
    g_origFn = reinterpret_cast<GmrcFn>(tramp);
    Log::Info("GMRC hook: INSTALLED (target=0x%lx, trampoline=%p)",
              (unsigned long)target, (void*)g_origFn);
    uintptr_t base = Patterns::FindSteamclientBase();
    Log::Info("Hook install: name=GMRC method=pattern target=0x%lx rva=0x%lx outcome=installed",
              (unsigned long)target, (unsigned long)(base ? target - base : 0));
    return true;
}

void Uninstall() {
    g_origFn = nullptr;
}

} // namespace Hooks::Gmrc
