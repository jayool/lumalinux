// lumalinux — function-level hooks for Steam Linux client.
//
// v0.1: depot key resolution hook (LoadDepotDecryptionKey equivalent)
// v0.2: BuildDepotDependency hook in diagnostic mode
// v0.3: BuildDepotDependency injection (allocator issues — abandoned)
// v0.4: KeyValues::ReadAsBinary diagnostic hook for appinfo identification.
//       Injection currently DISABLED — recon phase to map call patterns
//       before implementing KV-tree mutation in v0.5.

#include "log.hpp"
#include "key_store.hpp"
#include "hooks/depot_key_hook.hpp"
#include "hooks/depot_dependency_hook.hpp"
#include "hooks/keyvalues_hook.hpp"
#include "patterns.hpp"

#include <thread>
#include <chrono>
#include <atomic>

namespace {

std::atomic<bool> g_initialized{false};
std::thread       g_initThread;

void InitWorker() {
    using namespace std::chrono_literals;

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

    std::this_thread::sleep_for(500ms);

    if (!Hooks::DepotKey::Install()) {
        Log::Error("Init: depot key hook installation failed");
        return;
    }

    if (!Hooks::DepotDependency::Install()) {
        Log::Warn("Init: BuildDepotDependency hook failed (diagnostic only — non-fatal)");
    }

    if (!Hooks::KeyValues::Install()) {
        Log::Warn("Init: KeyValues hook failed (recon mode — non-fatal)");
    }

    Log::Info("Init: lumalinux active");
}

__attribute__((constructor))
void LumalinuxInit() {
    if (g_initialized.exchange(true)) return;

    Log::Init();
    Log::Info("DEBUG: &g_keys (main) = %p", KeyStore::DebugKeysAddr());
    Log::Info("lumalinux v0.4.0 loading...");

    std::string keysPath = KeyStore::DefaultPath();
    KeyStore::LoadFromFile(keysPath);
    Log::Info("DEBUG: post-load Size=%zu", KeyStore::Size());

    if (KeyStore::Size() == 0) {
        Log::Warn("Init: no keys loaded. Hooks install but won't intercept anything.");
        Log::Warn("Init: populate %s — see example/keys.txt for format.",
                  keysPath.c_str());
    }

    g_initThread = std::thread(InitWorker);
    g_initThread.detach();
}

__attribute__((destructor))
void LumalinuxShutdown() {
    Hooks::KeyValues::Uninstall();
    Hooks::DepotDependency::Uninstall();
    Hooks::DepotKey::Uninstall();
    Log::Shutdown();
}

} // namespace
