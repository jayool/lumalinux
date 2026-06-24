#include "depot_key_hook.hpp"
#include "../patterns.hpp"
#include "../key_store.hpp"
#include "../lmhook.hpp"
#include "../log.hpp"

#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace {

// ── Function signature ──────────────────────────────────────────────────────
//
// We hook the inner KeyValues accessor, exactly like LumaCore (DepotKeys.cpp).
// Verified empirically on Steam Deck / codespace by resolving the virtual call
// the cache makes and disassembling it:
//
//   int32_t LoadDepotDecryptionKey(
//       void*     pObject,    // arg0  KeyValues store object
//       uint32_t  foo,        // arg1  value-type selector (1 for binary keys)
//       char*     KeyName,    // arg2  "Software\Valve\Steam\Depots\<depot>\DecryptionKey"
//       char*     Key,        // arg3  output buffer
//       uint32_t  KeySize);   // arg4  size of Key — we MUST honour it
//   returns bytes written on success (32), 0 on miss.
//
// Hooking HERE (not the outer dispatcher) is what makes this not crash: we only
// answer the KeyValues query. Steam's cache then sees a hit and the dispatcher
// proceeds through its normal owned-or-unowned path — no short-circuit, no heap
// corruption. Works for every depot, owned and unowned, with no static list.
// See RESEARCH.md §12.

using LoadDepotKeyFn = int32_t (*)(void* /*pObject*/, uint32_t /*foo*/,
                                   const char* /*KeyName*/, char* /*Key*/,
                                   uint32_t /*KeySize*/);

LoadDepotKeyFn g_origFn = nullptr;

constexpr size_t kDepotKeyBytes = 32;

// Parse the depot id out of "Software\Valve\Steam\Depots\<depot>\DecryptionKey".
// Returns 0 if the name isn't a depot-key path (so we passthrough). Mirrors
// LumaCore: find "\DecryptionKey", then the backslash before it bounds the id.
uint32_t DepotIdFromKeyName(const char* keyName) {
    if (!keyName) return 0;
    std::string name(keyName);
    size_t dk = name.find("\\DecryptionKey");
    if (dk == std::string::npos || dk == 0) return 0;
    size_t start = name.find_last_of('\\', dk - 1);
    if (start == std::string::npos) return 0;
    std::string id = name.substr(start + 1, dk - start - 1);
    if (id.empty()) return 0;
    for (char c : id) if (c < '0' || c > '9') return 0;
    return static_cast<uint32_t>(std::strtoul(id.c_str(), nullptr, 10));
}

// ── Hook implementation (LumaCore-style) ─────────────────────────────────────

int32_t HookFn(void* pObject, uint32_t foo, const char* keyName,
               char* key, uint32_t keySize) {
    uint32_t depot = DepotIdFromKeyName(keyName);
    if (depot != 0) {
        if (auto k = KeyStore::Lookup(depot)) {
            static_assert(kDepotKeyBytes == 32, "depot keys are 32 bytes (AES-256)");
            if (keySize >= kDepotKeyBytes && key != nullptr) {
                std::memcpy(key, k->data(), kDepotKeyBytes);
                Log::Info("LoadDepotKey: SERVED local key for depot %u "
                          "(KeyName accessor, KeySize=%u)", depot, keySize);
                return static_cast<int32_t>(kDepotKeyBytes);
            }
            Log::Warn("LoadDepotKey: depot %u key too large for buffer "
                      "(KeySize=%u < 32) — passthrough", depot, keySize);
        }
        // NOTE: presence-only (keyless) entries — the app-id / shader pre-cache
        // depot — deliberately fall through to Steam's original loader. We do
        // NOT synthesise a placeholder key: the shader depot's MANIFEST is
        // itself encrypted with the real key, so a zero key fails to decrypt it
        // ("Invalid content configuration") and Steam's "Shader Priority"
        // suspends the whole install. The keyless shader pre-cache can never
        // succeed (we have no key), so it is suppressed out-of-band by
        // `DisableShaderCache=1` in config.vdf (written by steamidra_lite at
        // add-game time — the same flag Steam's own "Shader Pre-Caching" toggle
        // sets). See RESEARCH §13.8.
    }

    if (!g_origFn) {
        Log::Error("LoadDepotKey: no original fn pointer — should never happen");
        return 0;
    }
    return g_origFn(pObject, foo, keyName, key, keySize);
}

} // namespace

namespace Hooks::DepotKey {

bool Install() {
    uintptr_t target = Patterns::FindDepotKeyFunction();
    if (!target) {
        Log::Error("DepotKey hook: target not found");
        Log::Warn("Hook install: name=DepotKey method=pattern outcome=pattern_miss");
        return false;
    }
    void* tramp = nullptr;
    if (!LmHook::Install(target, reinterpret_cast<void*>(&HookFn), &tramp)) {
        Log::Error("DepotKey hook: LmHook::Install failed (target=0x%lx)",
                   (unsigned long)target);
        Log::Warn("Hook install: name=DepotKey method=pattern target=0x%lx outcome=hook_install_failed",
                  (unsigned long)target);
        return false;
    }
    g_origFn = reinterpret_cast<LoadDepotKeyFn>(tramp);

    Log::Info("DepotKey hook: INSTALLED (KeyValues accessor, target=0x%lx, "
              "trampoline=%p, %zu keys loaded)",
              (unsigned long)target, (void*)g_origFn, KeyStore::Size());
    uintptr_t base = Patterns::FindSteamclientBase();
    Log::Info("Hook install: name=DepotKey method=pattern target=0x%lx rva=0x%lx outcome=installed",
              (unsigned long)target, (unsigned long)(base ? target - base : 0));
    return true;
}

void Uninstall() { g_origFn = nullptr; }

} // namespace Hooks::DepotKey