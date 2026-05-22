// lumalinux — function-level depot key & depot-list hooks for Steam Linux client.
//
// Entry point: this .so is loaded via LD_PRELOAD into the Steam process by
// patching steam.sh (see install.sh). On load, __attribute__((constructor))
// runs LumalinuxInit which:
//   1. Initializes logging
//   2. Loads local depot keys from ~/.config/lumalinux/keys.txt
//   3. Spawns a worker thread that waits for steamclient.so to load, then
//      installs the hooks
//
// v0.1 hooks: depot key resolution (LoadDepotDecryptionKey equivalent)
// v0.2 hooks: BuildDepotDependency — DIAGNOSTIC ONLY (logs Steam's depot list)
//
// We use a worker thread because steamclient.so may not be loaded yet at
// LD_PRELOAD time (Steam loads it later in its init sequence).

#include "log.hpp"
#include "key_store.hpp"
#include "hooks/depot_key_hook.hpp"
#include "hooks/depot_dependency_hook.hpp"
#include "patterns.hpp"

#include <thread>
#include <chrono>
#include <atomic>

namespace {

std::atomic<bool> g_initialized{false};
std::thread       g_initThread;

void InitWorker() {
    using namespace std::chrono_literals;

    // Wait up to 5 minutes for steamclient.so to appear in this process.
    constexpr int kMaxAttempts = 300;
    for (int i = 0; i < kMaxAttempts; i++) {
        if (Patterns::FindSteamclientBase() != 0) {
            Log::Info("Init: steamclient.so detected (attempt %d)", i + 1);
            break;
        }
        if (i == kMaxAttempts - 1) {
            Log::Error("Init: steamclient.so never loaded — giving up after 5min");
            return;
        }
        std::this_thread::sleep_for(1s);
    }

    // Give Steam a moment to fully relocate steamclient.so before we scan
    std::this_thread::sleep_for(500ms);

    if (!Hooks::DepotKey::Install()) {
        Log::Error("Init: depot key hook installation failed");
        return;
    }

    // v0.2 — diagnostic hook on BuildDepotDependency. Non-fatal if it fails;
    // depot-key hook still works on its own for single-depot games.
    if (!Hooks::DepotDependency::Install()) {
        Log::Warn("Init: BuildDepotDependency hook failed (diagnostic — non-fatal)");
    }

    Log::Info("Init: lumalinux active");
}

__attribute__((constructor))
void LumalinuxInit() {
    if (g_initialized.exchange(true)) return;

    Log::Init();
    Log::Info("DEBUG: &g_keys (main) = %p", KeyStore::DebugKeysAddr());
    Log::Info("lumalinux v0.2.0 loading...");

    // Load local key store
    std::string keysPath = KeyStore::DefaultPath();
    KeyStore::LoadFromFile(keysPath);

    // ── DEBUG ──
    Log::Info("DEBUG: post-load Size=%zu", KeyStore::Size());
    auto k1 = KeyStore::Lookup(246621);
    auto k2 = KeyStore::Lookup(1145360);
    Log::Info("DEBUG: Lookup(246621)=%s, Lookup(1145360)=%s",
              k1 ? "FOUND" : "MISSING",
              k2 ? "FOUND" : "MISSING");
    // ── /DEBUG ──

    if (KeyStore::Size() == 0) {
        Log::Warn("Init: no keys loaded. Hook will install but won't intercept anything.");
        Log::Warn("Init: populate %s with lines of format: <depot_id>;<64-hex-char key>",
                  keysPath.c_str());
    }

    // Detach the init thread so it runs in background and doesn't block Steam's startup
    g_initThread = std::thread(InitWorker);
    g_initThread.detach();
}

__attribute__((destructor))
void LumalinuxShutdown() {
    Hooks::DepotDependency::Uninstall();
    Hooks::DepotKey::Uninstall();
    Log::Shutdown();
}

} // namespace
