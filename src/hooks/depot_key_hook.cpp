#include "depot_key_hook.hpp"
#include "../patterns.hpp"
#include "../key_store.hpp"
#include "../lmhook.hpp"
#include "../log.hpp"

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

LoadDepotKeyFn g_origFn = nullptr;

// Last depot id whose key we served from the KeyStore — read by the GMRC hook
// for response→manifest correlation. Plain volatile is fine: single writer
// (this hook), single reader (GMRC hook), and a stale value at worst injects
// the wrong code (recoverable), never crashes.
volatile uint32_t g_lastServedDepot = 0;

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
        // We assume Steam always provides a 32-byte buffer for `out_key`
        // (that's the size of a depot AES key, and what Steam stores
        // internally). Unlike LumaCore — which hooks the KeyValues-path
        // variant LoadDepotDecryptionKey(KeyName, Key, KeySize) and
        // validates KeySize >= 32 — the function we hook here doesn't pass
        // an explicit buffer size, so this assumption is unchecked. If a
        // future Steam build calls this with a smaller buffer we'd write
        // beyond it. Flagged in RESEARCH.md §11.3.
        static_assert(kDepotKeyBytes == 32, "depot keys are 32-byte AES");
        std::memcpy(out_key, key->data(), kDepotKeyBytes);
        g_lastServedDepot = depot_id;
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

    // libmem hook + get_pc_thunk fixup (thread-safe; handles the PIC prologue).
    void* tramp = nullptr;
    if (!LmHook::Install(target, reinterpret_cast<void*>(&HookFn), &tramp)) {
        Log::Error("DepotKey hook: LmHook::Install failed (target=0x%lx)",
                   (unsigned long)target);
        return false;
    }
    g_origFn = reinterpret_cast<LoadDepotKeyFn>(tramp);

    Log::Info("DepotKey hook: INSTALLED (target=0x%lx, trampoline=%p, %zu keys loaded)",
              (unsigned long)target, (void*)g_origFn, KeyStore::Size());
    return true;
}

void Uninstall() {
    // Hook persists for process lifetime.
    g_origFn = nullptr;
}

uint32_t LastServedDepot() {
    return g_lastServedDepot;
}

} // namespace Hooks::DepotKey
