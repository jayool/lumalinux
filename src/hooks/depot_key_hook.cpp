#include "depot_key_hook.hpp"
#include "../patterns.hpp"
#include "../key_store.hpp"
#include "../log.hpp"

#include <subhook.h>

#include <cstring>
#include <cstdint>

namespace {

// ── Function signature ──────────────────────────────────────────────────────
//
// Deduced from static analysis of steamclient.so:
//   EResult LoadDepotDecryptionKey(
//       void*     this_,           // CClientUser instance pointer
//       uint32_t  app_id,
//       uint32_t  depot_id,
//       void*     out_key_buffer); // 32 bytes filled with AES-256 key on success
//
// Steam EResult values (verified from SteamKit/Resources/SteamLanguage/eresult.steamd):
//   1 = OK
//   2 = Fail
//
// ★ THIS SIGNATURE IS DEDUCED — VERIFY EMPIRICALLY ★

using LoadDepotKeyFn = int32_t (*)(void* /*this*/, uint32_t /*app_id*/,
                                   uint32_t /*depot_id*/, void* /*out_key*/);

subhook_t      g_hook = nullptr;
LoadDepotKeyFn g_origFn = nullptr;

constexpr int32_t kResultOK   = 1;
constexpr int32_t kResultFail = 2;

constexpr size_t  kDepotKeyBytes = 32;

// ── Hook implementation ─────────────────────────────────────────────────────

int32_t HookFn(void* this_, uint32_t app_id, uint32_t depot_id, void* out_key) {
    Log::Debug("LoadDepotKey hook fired: app_id=%u depot_id=%u out_buf=%p",
               app_id, depot_id, out_key);

    if (out_key == nullptr) {
        Log::Warn("LoadDepotKey: null output buffer, falling through to original");
        if (!g_origFn) return kResultFail;
        return g_origFn(this_, app_id, depot_id, out_key);
    }

    if (auto key = KeyStore::Lookup(depot_id)) {
        std::memcpy(out_key, key->data(), kDepotKeyBytes);
        Log::Info("LoadDepotKey: SERVED local key for depot %u (app %u)", depot_id, app_id);
        return kResultOK;
    }

    if (!g_origFn) {
        Log::Error("LoadDepotKey: no original fn pointer — should never happen");
        return kResultFail;
    }

    int32_t result = g_origFn(this_, app_id, depot_id, out_key);
    Log::Debug("LoadDepotKey: passthrough to original returned %d", result);
    return result;
}

} // namespace

namespace Hooks::DepotKey {

bool Install() {
    uintptr_t target = Patterns::FindDepotKeyFunction();
    if (!target) {
        Log::Error("DepotKey hook: cannot install — target function not found");
        return false;
    }

    g_hook = subhook_new(reinterpret_cast<void*>(target),
                         reinterpret_cast<void*>(&HookFn),
                         static_cast<subhook_flags_t>(0));
    if (!g_hook) {
        Log::Error("DepotKey hook: subhook_new failed");
        return false;
    }

    if (subhook_install(g_hook) != 0) {
        Log::Error("DepotKey hook: subhook_install failed (target=0x%lx)",
                   (unsigned long)target);
        subhook_free(g_hook);
        g_hook = nullptr;
        return false;
    }

    g_origFn = reinterpret_cast<LoadDepotKeyFn>(subhook_get_trampoline(g_hook));
    if (!g_origFn) {
        Log::Error("DepotKey hook: subhook_get_trampoline returned null — "
                   "function prologue may have instructions subhook can't relocate");
        subhook_remove(g_hook);
        subhook_free(g_hook);
        g_hook = nullptr;
        return false;
    }

    // ── DEBUG ──
    Log::Info("DEBUG: pre-INSTALL Size=%zu", KeyStore::Size());
    auto kx = KeyStore::Lookup(246621);
    Log::Info("DEBUG: pre-INSTALL Lookup(246621)=%s", kx ? "FOUND" : "MISSING");
    // ── /DEBUG ──

    Log::Info("DepotKey hook: INSTALLED (target=0x%lx, trampoline=%p, %zu keys loaded)",
              (unsigned long)target, (void*)g_origFn, KeyStore::Size());
    return true;
}

void Uninstall() {
    if (!g_hook) return;
    subhook_remove(g_hook);
    subhook_free(g_hook);
    g_hook = nullptr;
    g_origFn = nullptr;
    Log::Info("DepotKey hook: uninstalled");
}

} // namespace Hooks::DepotKey
