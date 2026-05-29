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

    // Only intervene for manifests of our forced depots; everything else goes
    // to Steam's normal (owned) path.
    if (out_code && KeyStore::HasManifestGid(gid)) {
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
        return false;
    }
    void* tramp = nullptr;
    if (!LmHook::Install(target, reinterpret_cast<void*>(&HookFn), &tramp)) {
        Log::Error("GMRC hook: LmHook::Install failed (target=0x%lx)",
                   (unsigned long)target);
        return false;
    }
    g_origFn = reinterpret_cast<GmrcFn>(tramp);
    Log::Info("GMRC hook: INSTALLED (target=0x%lx, trampoline=%p)",
              (unsigned long)target, (void*)g_origFn);
    return true;
}

void Uninstall() {
    g_origFn = nullptr;
}

} // namespace Hooks::Gmrc
