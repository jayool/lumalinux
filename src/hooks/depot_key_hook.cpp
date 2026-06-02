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

// LumaCore-style: hook the inner KeyValues accessor, not the outer dispatcher.
//   int32_t LoadDepotDecryptionKey(void* pObject, uint32_t foo,
//                                  char* KeyName, char* Key, uint32_t KeySize);
//   KeyName = "Software\Valve\Steam\Depots\<depot>\DecryptionKey"
// Answering the query here (instead of short-circuiting the dispatcher) is what
// keeps Steam from corrupting its heap. See RESEARCH.md §12.
using LoadDepotKeyFn = int32_t (*)(void*, uint32_t, const char*, char*, uint32_t);
LoadDepotKeyFn g_origFn = nullptr;

volatile uint32_t g_lastServedDepot = 0;
constexpr size_t kDepotKeyBytes = 32;

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

int32_t HookFn(void* pObject, uint32_t foo, const char* keyName,
               char* key, uint32_t keySize) {
    uint32_t depot = DepotIdFromKeyName(keyName);
    if (depot != 0) {
        if (auto k = KeyStore::Lookup(depot)) {
            static_assert(kDepotKeyBytes == 32, "depot keys are 32-byte AES");
            if (keySize >= kDepotKeyBytes && key != nullptr) {
                std::memcpy(key, k->data(), kDepotKeyBytes);
                g_lastServedDepot = depot;
                Log::Info("LoadDepotKey: SERVED local key for depot %u (KeySize=%u)",
                          depot, keySize);
                return static_cast<int32_t>(kDepotKeyBytes);
            }
            Log::Warn("LoadDepotKey: depot %u buffer too small (KeySize=%u) — passthrough",
                      depot, keySize);
        }
    }
    if (!g_origFn) { Log::Error("LoadDepotKey: no orig fn"); return 0; }
    return g_origFn(pObject, foo, keyName, key, keySize);
}

} // namespace

namespace Hooks::DepotKey {

bool Install() {
    uintptr_t target = Patterns::FindDepotKeyFunction();
    if (!target) { Log::Error("DepotKey hook: target not found"); return false; }
    void* tramp = nullptr;
    if (!LmHook::Install(target, reinterpret_cast<void*>(&HookFn), &tramp)) {
        Log::Error("DepotKey hook: LmHook::Install failed (target=0x%lx)",
                   (unsigned long)target);
        return false;
    }
    g_origFn = reinterpret_cast<LoadDepotKeyFn>(tramp);
    Log::Info("DepotKey hook: INSTALLED (KeyValues accessor, target=0x%lx, %zu keys)",
              (unsigned long)target, KeyStore::Size());
    return true;
}

void Uninstall() { g_origFn = nullptr; }
uint32_t LastServedDepot() { return g_lastServedDepot; }

} // namespace Hooks::DepotKey