// lumalinux — function-level hooks for Steam Linux client.
//
// v0.1: depot key resolution hook (LoadDepotDecryptionKey equivalent)
// v0.2: BuildDepotDependency hook in diagnostic mode
// v0.3: BuildDepotDependency injection (allocator issues — abandoned)
// v0.4: KeyValues::ReadAsBinary recon (mapping call patterns) — abandoned
// v0.5: LD_AUDIT migration + LoadPackage injection (LumaCore-style).
//       LoadPackage hook adds forced appids into PackageId=0 so Steam fetches
//       their appinfo. BuildDep hook then PATCHES existing depot entries' gid
//       and size (never injects).
// v0.5.1: Dual-load support. Works both as LD_AUDIT (la_preinit / la_objopen
//         fire) and as LD_PRELOAD (the __attribute__((constructor)) kicks in
//         and polls /proc/self/maps for steamclient.so). On 64-bit hosts that
//         reject 32-bit audit libs (most Steam launchers), LD_PRELOAD is the
//         only path that runs, and the constructor handles it.
// v0.5.6: Aligned exactly to LumaCore's hook logic, no extras (avoids crashes):
//         - LoadPackage injects the DEPOT ids (== LumaCore GetAllDepotIds),
//           not the parent app id, so BuildDep's per-depot license filter
//           keeps them. In-place append only (no risky manual realloc).
//         - BuildDep is PATCH-only and honours size==0 → keep original.
//         - Packet/858 hook REMOVED: SLSsteam already covers ownership
//           (CheckAppOwnership) and PICS access tokens. Nothing extra.

#include "log.hpp"
#include "key_store.hpp"
#include "hooks/depot_key_hook.hpp"
#include "hooks/depot_dependency_hook.hpp"
#include "hooks/load_package_hook.hpp"
#include "hooks/gmrc_hook.hpp"
#include "patterns.hpp"
#include "globals.hpp"
#include "update.hpp"

#include "libmem/libmem.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <link.h>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

std::atomic<bool> g_preinitDone{false};
std::atomic<bool> g_hooksInstalled{false};

// Process allowlist: lumalinux is loaded into every child of steam.sh via
// LD_PRELOAD (steamerrorreporter, steam-runtime-launcher-service, game
// processes via Proton, etc). Only steam and steamwebhelper actually load
// steamclient.so; everywhere else we end up logging banners + spawning a
// worker that times out after ~5 minutes looking for a mapping that will
// never appear. Filter on /proc/self/comm so non-Steam processes return
// immediately from the constructor with zero side effects.
//
// Override with LUMA_PROCESS_ANY=1 for setups where a different process
// is the steamclient.so host (custom gamescope-session, debug, etc.).
bool IsAllowedProcess() {
    if (std::getenv("LUMA_PROCESS_ANY")) return true;

    FILE* f = std::fopen("/proc/self/comm", "r");
    if (!f) return true;  // fail-open: if we can't read /proc, allow

    char comm[256] = {0};
    char* got = std::fgets(comm, sizeof(comm), f);
    std::fclose(f);
    if (!got) return true;  // fail-open

    // Trim the trailing newline that /proc/self/comm always includes
    size_t len = std::strlen(comm);
    if (len > 0 && comm[len - 1] == '\n') comm[len - 1] = '\0';

    static const char* const allowed[] = {
        "steam",
        "steamwebhelper",
    };
    for (const char* a : allowed) {
        if (std::strcmp(comm, a) == 0) return true;
    }
    return false;
}

// Called once, before Steam's main thread starts running user code, by
// the dynamic linker auditing API. Set up logging and load the key file
// so the hooks (which can install later in la_objopen) have data ready.
void DoPreinit() {
    if (g_preinitDone.exchange(true)) return;

    Log::Init();
    Log::Info("DEBUG: &g_keys (preinit) = %p", KeyStore::DebugKeysAddr());
    Log::Info("lumalinux v0.8.1 preinit (LoadPackage + DepotKey + BuildDep + GMRC inject)");

    std::string keysPath = KeyStore::DefaultPath();
    KeyStore::LoadFromFile(keysPath);
    Log::Info("DEBUG: post-load Size=%zu", KeyStore::Size());

    if (KeyStore::Size() == 0) {
        Log::Warn("Preinit: no keys loaded. Hooks install but won't intercept anything.");
        Log::Warn("Preinit: populate %s — see example/keys.txt for format.",
                  keysPath.c_str());
    }
}

void InstallHooks() {
    if (g_hooksInstalled.exchange(true)) return;

    if (Patterns::FindSteamclientBase() == 0) {
        Log::Warn("Install: steamclient.so r-x not visible yet — backing off 200ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (Patterns::FindSteamclientBase() == 0) {
            Log::Error("Install: steamclient.so still not visible — aborting");
            return;
        }
    }

    // Locate steamclient.so via libmem and stash the module info — Updater::
    // verifySafeModeHash() reads g_modSteamClient.path to hash the file off
    // disk. Mirrors SLSsteam's globals.cpp / pattern population.
    if (LM_FindModule("steamclient.so", &g_modSteamClient) == LM_FALSE) {
        Log::Error("Install: LM_FindModule(steamclient.so) failed — aborting");
        return;
    }

    // SafeMode hash gate, SLSsteam-style. Refuse to install any hook unless
    // the loaded steamclient.so hashes to one of the entries in res/updates.yaml
    // under our compile-time LUMALINUX_SAFEMODE_VERSION group. Same fail-closed
    // semantics as SLSsteam: network → cache → false. No override.
    if (!Updater::init() || !Updater::verifySafeModeHash()) {
        Log::Notify("SafeMode: steamclient.so hash not in verified list — "
                    "hooks skipped. See ~/.cache/lumalinux/lumalinux.log");
        return;
    }

    // v0.6.1 — full hook set, loaded via LD_PRELOAD (NOT LD_AUDIT; the audit
    // namespace corrupts the heap → realloc abort). SLSsteam gives ownership
    // but Steam's fresh appinfo for an unowned app does NOT surface its content
    // depots as download targets ("0 target depots"). LoadPackage injects the
    // depot ids into PackageId=0 so the per-depot license check passes and the
    // depots surface; BuildDep PATCHes their gid/size; DepotKey serves the
    // keys; GMRC injects the manifest request code. Mirrors LumaCore.
    //
    // Each hook reports ok / disabled / FAILED. A FAILED hook almost always
    // means its byte pattern stopped matching after a Steam update — that's the
    // early-warning signal surfaced in the startup toast.
    struct HookSpec { const char* name; const char* disableEnv; bool (*install)(); };
    const HookSpec specs[] = {
        {"LoadPackage", "LUMA_NO_LOADPKG",  &Hooks::LoadPackage::Install},
        {"DepotKey",    "LUMA_NO_DEPOTKEY", &Hooks::DepotKey::Install},
        {"BuildDep",    "LUMA_NO_BUILDDEP", &Hooks::DepotDependency::Install},
        {"GMRC",        "LUMA_NO_GMRC",     &Hooks::Gmrc::Install},
    };

    int active = 0, expected = 0;
    std::string failed;
    for (const auto& s : specs) {
        if (std::getenv(s.disableEnv)) {
            Log::Warn("Install: %s hook DISABLED via %s", s.name, s.disableEnv);
            continue;
        }
        ++expected;
        if (s.install()) {
            ++active;
        } else {
            Log::Error("Install: %s hook FAILED (pattern not found? Steam may have "
                       "updated — see docs/RESEARCH.md to re-derive patterns)", s.name);
            if (!failed.empty()) failed += ", ";
            failed += s.name;
        }
    }

    Log::Info("Install: lumalinux active (%d/%d hooks)", active, expected);
    if (failed.empty()) {
        Log::Notify("v0.8.1 loaded — %d/%d hooks active", active, expected);
    } else {
        Log::Notify("v0.8.1: %d/%d hooks — %s FAILED. Steam update? See "
                    "~/.cache/lumalinux/lumalinux.log", active, expected, failed.c_str());
    }
}

bool IsSteamclient(const char* name) {
    if (!name) return false;
    std::string s(name);
    return s.size() >= std::string("steamclient.so").size() &&
           (s.ends_with("/steamclient.so") || s == "steamclient.so");
}

} // namespace

// =============================================================================
// LD_AUDIT entry points
// =============================================================================
//
// The dynamic linker calls these when our library is loaded via LD_AUDIT.
// We pick up steamclient.so being loaded (la_objopen) and install our hooks
// right then — earlier than the LD_PRELOAD constructor approach, which is
// what enables hooking LoadPackage before Steam parses its first PICS
// response.
extern "C" {

#define LUMA_EXPORT __attribute__((visibility("default")))

// Return whatever ld.so asked for, clamped to what we support. Returning a
// hard-coded LAV_CURRENT compiled against a newer glibc would get us silently
// disabled on older runtimes (e.g. SteamOS ld.so), which is exactly what
// happened in v0.5.0.
LUMA_EXPORT unsigned int la_version(unsigned int version) {
    return (version > LAV_CURRENT) ? LAV_CURRENT : version;
}

LUMA_EXPORT void la_preinit(uintptr_t* cookie) {
    (void)cookie;
    if (!IsAllowedProcess()) return;
    DoPreinit();
}

LUMA_EXPORT unsigned int la_objopen(struct link_map* map, Lmid_t lmid, uintptr_t* cookie) {
    (void)lmid;
    (void)cookie;
    if (!IsAllowedProcess()) return 0;
    if (!map || !map->l_name) return 0;
    if (!IsSteamclient(map->l_name)) return 0;

    // la_preinit only runs for the main executable's auditing pass. For
    // safety (e.g. if Steam dlopens steamclient.so before any other audit
    // event reaches us), make sure preinit ran.
    DoPreinit();

    Log::Info("la_objopen: steamclient.so detected at link_map=%p (name=%s) — "
              "installing hooks", (void*)map, map->l_name);
    InstallHooks();
    return 0;
}

} // extern "C"

namespace {

// LD_PRELOAD fallback path. Fires when the library is loaded via LD_PRELOAD
// (no LD_AUDIT path active). Polls /proc/self/maps for steamclient.so, then
// installs hooks. Idempotent with the LD_AUDIT path via the g_hooksInstalled
// atomic in InstallHooks().
__attribute__((constructor))
void LumalinuxCtor() {
    if (!IsAllowedProcess()) return;
    DoPreinit();

    // Avoid a SIGABRT during exit cleanup when SLSsteam + CloudRedirect are
    // also loaded. Their per-lib pthread watch threads + OpenSSL/libcurl
    // teardown race against _dl_fini, crashing the process before the
    // OOBE finishes (~73% reproduction rate with the full stack). _exit(0)
    // skips atexit chains and library destructors — Steam has already
    // persisted state by this point, and the kernel reclaims memory on
    // process exit. Registered here so it fires FIRST (atexit handlers run
    // LIFO, lumalinux is the last LD_PRELOAD slot in steam.sh).
    std::atexit([]() { _exit(0); });

    std::thread([] {
        using namespace std::chrono_literals;
        constexpr int kMaxAttempts = 300;  // ~5 min
        for (int i = 0; i < kMaxAttempts; ++i) {
            if (g_hooksInstalled.load()) return;  // la_objopen path already ran
            if (Patterns::FindSteamclientBase() != 0) {
                Log::Info("Ctor: steamclient.so detected (attempt %d) — installing hooks", i + 1);
                std::this_thread::sleep_for(200ms);
                InstallHooks();
                return;
            }
            std::this_thread::sleep_for(1s);
        }
        Log::Warn("Ctor: steamclient.so never appeared — giving up");
    }).detach();
}

__attribute__((destructor))
void LumalinuxShutdown() {
    // If the constructor opted out (non-allowed process), no hooks were
    // ever installed and the log was never opened — Uninstall/Shutdown
    // on those is a no-op but skip them explicitly for clarity.
    if (!g_preinitDone.load()) return;
    Hooks::Gmrc::Uninstall();
    Hooks::DepotDependency::Uninstall();
    Hooks::LoadPackage::Uninstall();
    Hooks::DepotKey::Uninstall();
    Log::Shutdown();
}

} // namespace

