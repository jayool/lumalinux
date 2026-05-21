#include "depot_key_hook.hpp"
#include "../patterns.hpp"
#include "../key_store.hpp"
#include "../log.hpp"

#include <libmem/libmem.h>

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
// If the calling convention or arg order is different in your build, the hook
// may crash or return garbage. Use Frida on a few real install attempts to
// confirm arg layout before trusting this code in production.

using LoadDepotKeyFn = int32_t (*)(void* /*this*/, uint32_t /*app_id*/,
                                   uint32_t /*depot_id*/, void* /*out_key*/);

LoadDepotKeyFn g_origFn = nullptr;
lm_hook_t      g_hookHandle = 0;

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

    // Check our local key store
    if (auto key = KeyStore::Lookup(depot_id)) {
        std::memcpy(out_key, key->data(), kDepotKeyBytes);
        Log::Info("LoadDepotKey: SERVED local key for depot %u (app %u)", depot_id, app_id);
        return kResultOK;
    }

    // No local key — let Steam handle it normally (will fail server-side if user doesn't own,
    // succeed if they do own or game is free/family-shared)
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

    // libmem's LM_HookCode installs an inline detour: writes a JMP at the
    // target's prologue and copies displaced instructions to a trampoline.
    // The trampoline is returned as `g_origFn`.
    lm_address_t trampoline = 0;
    g_hookHandle = LM_HookCode(target, reinterpret_cast<lm_address_t>(&HookFn), &trampoline);
    if (g_hookHandle == 0 || trampoline == 0) {
        Log::Error("DepotKey hook: LM_HookCode failed (target=0x%lx)", (unsigned long)target);
        return false;
    }

    g_origFn = reinterpret_cast<LoadDepotKeyFn>(trampoline);
    Log::Info("DepotKey hook: INSTALLED (target=0x%lx, trampoline=0x%lx, %zu keys loaded)",
              (unsigned long)target, (unsigned long)trampoline, KeyStore::Size());
    return true;
}

void Uninstall() {
    if (g_hookHandle == 0) return;
    LM_UnhookCode(reinterpret_cast<lm_address_t>(g_origFn),
                  reinterpret_cast<lm_address_t>(g_origFn),
                  g_hookHandle);
    g_hookHandle = 0;
    g_origFn = nullptr;
    Log::Info("DepotKey hook: uninstalled");
}

} // namespace Hooks::DepotKey
