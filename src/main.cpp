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
#include "license_reconcile.hpp"
#include "hooks/depot_key_hook.hpp"
#include "hooks/depot_dependency_hook.hpp"
#include "hooks/load_package_hook.hpp"
#include "hooks/package_zero_finder.hpp"
#include "hooks/gmrc_hook.hpp"
#include "hooks/shader_depot_hook.hpp"
#include "sls_update_unblock.hpp"
#include "sls_achievement_unblock.hpp"
#include "patterns.hpp"
#include "globals.hpp"
#include "status.hpp"
#ifndef LUMA_NO_UPDATE
#include "update.hpp"
#endif
#include "version.hpp"

#include "libmem/libmem.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <link.h>
#include <string>
#include <thread>
#include <vector>
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
    Log::Info("lumalinux " LUMALINUX_VERSION_STRING " preinit — hooks install once steamclient.so is mapped");

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

    // Hash gate, SLSsteam-style. Refuse to install any hook unless the loaded
    // steamclient.so hashes to one of the entries in res/updates.yaml under our
    // compile-time LUMALINUX_SAFEMODE_VERSION group. Always fail-closed: there
    // is no override, no config.yaml — the only way out is to ship a new release
    // (or add the hash to res/updates.yaml on main; cache refreshes on next boot).
    //
    // Write status.json before returning so external UIs (LumaDeck) can tell
    // "hash blocked" apart from "Steam never started this session". Without
    // this, a blocked session is invisible.
#ifndef LUMA_NO_UPDATE
    if (!Updater::init() || !Updater::verifySafeModeHash()) {
        Log::Notify(LUMALINUX_VERSION_STRING " blocked: steamclient.so hash not "
                    "in verified list (Steam updated past this lumalinux build). "
                    "Installed games still work; new downloads disabled until "
                    "lumalinux is updated. See ~/.cache/lumalinux/lumalinux.log");
        Status::SetBlocked("hash_unverified");
        Status::Write();
        return;
    }
#else
    // Built without the SafeMode update-check (no libcurl/libcrypto dependency).
    // The hash gate is skipped — hooks install unconditionally. Validation /
    // codespace builds only; NOT for distribution.
    Log::Warn("Install: SafeMode update-check compiled out (LUMA_NO_UPDATE) — "
              "installing hooks WITHOUT hash verification");
#endif

    // v0.13.1 — three install-path hooks + the package-0 finder. Loaded via
    // LD_PRELOAD (NOT LD_AUDIT; the audit namespace corrupts the heap → realloc
    // abort). SLSsteam gives ownership but Steam's fresh appinfo for an unowned
    // app does NOT surface its content depots as download targets ("0 target
    // depots"). The package-0 finder (started below, on its own thread) injects
    // the depot ids into PackageId=0 so the per-depot license check passes and
    // the depots surface; BuildDep PATCHes their gid/size; DepotKey serves the
    // keys; GMRC injects the manifest request code. Mirrors LumaCore.
    //
    // The legacy LoadPackage hook is OPT-IN as a diagnostic only — set
    // LUMA_LOADPKG_DEBUG=1 to install it and log every PackageId+AppIdVec
    // triple. It no longer injects (the finder does), and the v0.13.0
    // regression analysis (see RESEARCH §13) showed it doesn't fire reliably
    // anyway when Steam keeps PackageId=0 cached. Installing it unconditionally
    // means a useless detour and a misleading "LoadPackage FAILED" toast every
    // time its byte pattern moves on a Steam update, even though installs work.
    //
    // Each hook reports ok / disabled / FAILED. A FAILED hook almost always
    // means its byte pattern stopped matching after a Steam update — that's the
    // early-warning signal surfaced in the startup toast.
    struct HookSpec { const char* name; const char* disableEnv; bool (*install)(); };
    std::vector<HookSpec> specs = {
        {"DepotKey",    "LUMA_NO_DEPOTKEY", &Hooks::DepotKey::Install},
        {"GMRC",        "LUMA_NO_GMRC",     &Hooks::Gmrc::Install},
        {"ShaderDepot", "LUMA_NO_SHADERSKIP", &Hooks::ShaderDepot::Install},
    };
    // BuildDep is OFF by default since SLSsteam 20260714 hooks
    // BuildDepotDependency itself (for its ManifestIds / DepotBlacklist
    // features) and loads first (LD_AUDIT before our LD_PRELOAD), overwriting
    // the prologue. Our pattern scan then can't match and reports a false
    // FAILED, which LumaDeck used to surface as "Steam build not supported".
    // The hook is pin-only and inert in the default no-pin flow, so disabling
    // it loses nothing there; version pinning moves to SLSsteam's ManifestIds.
    // Force it back on for testing with LUMA_FORCE_BUILDDEP.
    const bool forceBuildDep = std::getenv("LUMA_FORCE_BUILDDEP") != nullptr;
    if (forceBuildDep) {
        specs.push_back({"BuildDep", "LUMA_NO_BUILDDEP", &Hooks::DepotDependency::Install});
    }
    if (const char* dbg = std::getenv("LUMA_LOADPKG_DEBUG"); dbg && dbg[0] && dbg[0] != '0') {
        specs.push_back({"LoadPackage", "LUMA_NO_LOADPKG", &Hooks::LoadPackage::Install});
    }

    int active = 0, expected = 0;
    std::string failed;
    std::string installed;   // names of the pieces actually installed, derived
                             // from the loop so the summary never goes stale
    for (const auto& s : specs) {
        if (std::getenv(s.disableEnv)) {
            Log::Warn("Install: %s hook DISABLED via %s", s.name, s.disableEnv);
            Status::RecordHook(s.name, Status::DISABLED);
            continue;
        }
        ++expected;
        if (s.install()) {
            ++active;
            if (!installed.empty()) installed += ", ";
            installed += s.name;
            Status::RecordHook(s.name, Status::INSTALLED);
        } else {
            Log::Error("Install: %s hook FAILED (pattern not found? Steam may have "
                       "updated — see docs/RESEARCH.md to re-derive patterns)", s.name);
            if (!failed.empty()) failed += ", ";
            failed += s.name;
            Status::RecordHook(s.name, Status::FAILED);
        }
    }

    if (!forceBuildDep) {
        // Record the intentional off-state so status.json is self-documenting:
        // DISABLED, not FAILED. LumaDeck keys "Steam build not supported" off a
        // failed critical hook; BuildDep is neither critical nor actually failing.
        Status::RecordHook("BuildDep", Status::DISABLED);
    }

    Log::Info("Install: lumalinux active (%d/%d hooks)", active, expected);
    if (failed.empty()) {
        Log::Notify(LUMALINUX_VERSION_STRING " loaded — %d/%d hooks active", active, expected);
    } else {
        Log::Notify(LUMALINUX_VERSION_STRING ": %d/%d hooks — %s FAILED. Steam update? See "
                    "~/.cache/lumalinux/lumalinux.log", active, expected, failed.c_str());
    }

    // Package-0 finder: the SOLE depot injector. The LoadPackage hook is passive
    // and misses the case where Steam keeps PackageId=0 cached and never calls
    // LoadPackage, so the finder walks the cache directly and injects there. ON
    // BY DEFAULT (disable with LUMA_NO_PKG0_FINDER); the hook no longer injects.
    if (std::getenv("LUMA_NO_PKG0_FINDER")) {
        Status::RecordHook("PackageZeroFinder", Status::DISABLED);
    } else {
        Hooks::PackageZeroFinder::Start();
        Status::RecordHook("PackageZeroFinder", Status::INSTALLED);
        if (!installed.empty()) installed += ", ";
        installed += "package-0 finder";
    }

    // Coexistence patch: undo SLSsteam's 20260705 update-block (it clears
    // APPSTATE_UPDATE_* for AdditionalApps / non-owned games, killing auto-update
    // for everything LumaDeck manages). One in-memory 4-byte no-op on SLSsteam.so;
    // re-applied every launch so it survives headcrab re-downloading the .so.
    // Fail-safe: if SLSsteam isn't loaded or the anchor moved, it no-ops and
    // updates just fall back to manual. See docs/slssteam-analysis.md.
    if (std::getenv("LUMA_NO_SLS_UNBLOCK")) {
        Status::RecordHook("SlsUpdateUnblock", Status::DISABLED);
    } else if (SlsUpdateUnblock::Apply()) {
        Status::RecordHook("SlsUpdateUnblock", Status::INSTALLED);
        if (!installed.empty()) installed += ", ";
        installed += "SLS-unblock";
    } else {
        // Not fatal — no SLSsteam, or anchor not found (safe degradation).
        Status::RecordHook("SlsUpdateUnblock", Status::DISABLED);
    }

    // Coexistence patch #2: scope SLSsteam's native-achievement "legit app" guard
    // so the schema borrow also fires for AdditionalApps (LumaDeck) games while
    // leaving genuinely-owned games untouched. Repoints two `call isSubscribed`
    // sites in SLSsteam.so to a combined `isSubscribed && !isAddedAppId` check.
    // Fail-safe: no SLSsteam / missing symbols / moved codegen → no-op, native
    // achievements just stay off. See docs/slssteam-analysis.md.
    if (std::getenv("LUMA_NO_SLS_ACH_UNBLOCK")) {
        Status::RecordHook("SlsAchievementUnblock", Status::DISABLED);
    } else if (SlsAchievementUnblock::Apply()) {
        Status::RecordHook("SlsAchievementUnblock", Status::INSTALLED);
        if (!installed.empty()) installed += ", ";
        installed += "SLS-ach";
    } else {
        Status::RecordHook("SlsAchievementUnblock", Status::DISABLED);
    }

    // Derived from the install table above (+ the finder), so it always reflects
    // exactly what loaded — never a hand-maintained list that drifts.
    Log::Info("Install: lumalinux active pieces: %s", installed.empty() ? "none" : installed.c_str());

    // Snapshot for external consumers (LumaDeck reads $XDG_RUNTIME_DIR/lumalinux/
    // status.json to render the Settings health badge). Best-effort.
    Status::Write();
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

    // no-restart experiment (LUMA_NO_RESTART / marker): watch keys.txt so a
    // game added while Steam runs gets its keys loaded and the license reconcile
    // armed — no Steam restart. Off by default; the watcher only starts, and the
    // reconcile only fires, when the flag is set.
    if (LicenseReconcile::Enabled()) {
        KeyStore::StartWatcher(KeyStore::DefaultPath());
        Log::Info("no-restart experiment ENABLED (LUMA_NO_RESTART) — keys.txt watcher started");
    }
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

