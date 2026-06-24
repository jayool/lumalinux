#include "manifest_probe_hook.hpp"
#include "../patterns.hpp"
#include "../key_store.hpp"
#include "../lmhook.hpp"
#include "../log.hpp"

#include <cstdint>

namespace {

// bool CDepotDownloadMgr::BYldRequestDepotManifest(
//        void* this_, uint32_t appId, uint32_t depotId, uint64_t manifestId,
//        const char* branch, void* arg20)
//
// Steam calls this to request a depot's manifest during a download/update plan.
// DIAGNOSTIC ONLY: we call the original, log what happened for our depots (and
// the shader/app-id depot), and return whatever the original returned. No
// behaviour change. See RESEARCH §13.8.
using BYldFn = bool (*)(void*, uint32_t, uint32_t, uint64_t, const char*, void*);
BYldFn g_origFn = nullptr;

bool HookFn(void* this_, uint32_t appId, uint32_t depotId, uint64_t manifestId,
            const char* branch, void* arg20) {
    // Defer to the original FIRST so we can log its verdict. This is the whole
    // point of a probe: observe, never intervene.
    const bool ret = g_origFn
        ? g_origFn(this_, appId, depotId, manifestId, branch, arg20)
        : false;

    // Only log calls that are interesting: depots we manage, or a depot whose id
    // equals the app id (the shader pre-cache depot convention). Everything else
    // is genuinely-owned content traffic we don't care about here.
    const bool ours     = KeyStore::HasDepot(depotId);
    const bool appDepot = (depotId == appId);
    if (ours || appDepot) {
        Log::Info("ManifestProbe: BYldRequestDepotManifest app=%u depot=%u "
                  "manifest=%llu branch=%s ours=%d appDepot(shader?)=%d -> returned=%d",
                  appId, depotId, (unsigned long long)manifestId,
                  branch ? branch : "(null)", (int)ours, (int)appDepot, (int)ret);
    }
    return ret;
}

} // namespace

namespace Hooks::ManifestProbe {

bool Install() {
    uintptr_t target = Patterns::FindBYldRequestDepotManifestFunction();
    if (!target) {
        Log::Warn("ManifestProbe: BYldRequestDepotManifest pattern not found — "
                  "probe not installed (non-fatal; core hooks unaffected)");
        return false;
    }
    void* tramp = nullptr;
    if (!LmHook::Install(target, reinterpret_cast<void*>(&HookFn), &tramp)) {
        Log::Warn("ManifestProbe: LmHook::Install failed (target=0x%lx) — non-fatal",
                  (unsigned long)target);
        return false;
    }
    g_origFn = reinterpret_cast<BYldFn>(tramp);
    Log::Info("ManifestProbe: INSTALLED (diagnostic-only, target=0x%lx, trampoline=%p)",
              (unsigned long)target, (void*)g_origFn);
    return true;
}

void Uninstall() { g_origFn = nullptr; }

} // namespace Hooks::ManifestProbe
