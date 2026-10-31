// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 jayool — https://github.com/jayool/lumalinux
#include "license_reconcile.hpp"
#include "patterns.hpp"
#include "reconcile_anchor.hpp"
#include "rva_feed.hpp"
#include "log.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>

#include <sys/stat.h>

namespace {

std::atomic<void*> g_pCUser{nullptr};
std::atomic<bool>  g_keysChanged{false};

// Wake primitive for the package-0 finder: lets it sleep on a timeout but wake
// the instant a game is added (keys.txt change), instead of on its next slow
// tick. The flag above doubles as the CV predicate.
//
// Heap-allocated once and never destroyed, on purpose. As plain globals they
// had destructors, and at Steam's exit ~condition_variable ran while the finder
// was still asleep on it: pthread_cond_destroy blocks until every waiter has
// left, so Steam's main thread sat in __cxa_finalize for whatever was left of
// the finder's 15 s nap (measured 2026-10-19: 9-18 s with the stack vs 3 s
// without; 3 s again with the finder off). The process ends with _exit(0)
// right after anyway, so nothing is lost by skipping the destructors; the
// kernel reclaims the two objects with the rest of the address space.
std::mutex&              WakeMtx() { static auto* m = new std::mutex; return *m; }
std::condition_variable& WakeCv()  { static auto* cv = new std::condition_variable; return *cv; }

// CUser::NotifyLicensesUpdated is cdecl, single arg (`this`). It rebuilds the
// LicensesUpdated_t callback from this user's own license vector and posts it.
using NotifyFn = void (*)(void*);

// Resolve the NotifyLicensesUpdated address ONCE and cache it. Deterministic per
// process (steamclient.so base is fixed after load), so a static is safe.
// Shared by Resolve() (status, called at hook-install time) and Reconcile()
// (the fire).
//
// RVA feed first (prologue-independent, keyed by the steamclient.so hash), else
// the byte pattern. FindNotifyLicensesUpdatedFunction requires a UNIQUE match
// and returns 0 otherwise, so a wrong-build pattern caches 0 -> the feature
// no-ops rather than firing at a garbage address. docs/design/rva-feed-design.md.
uintptr_t ResolveAddr() {
    static const uintptr_t addr = []() -> uintptr_t {
        if (uintptr_t feed = RvaFeed::Resolve("Reconcile")) {
            if (uintptr_t p = Patterns::FindNotifyLicensesUpdatedFunction(); p && p != feed)
                Log::Warn("Reconcile: feed 0x%lx != pattern 0x%lx — using feed; "
                          "investigate drift", (unsigned long)feed, (unsigned long)p);
            return feed;
        }
        if (uintptr_t p = Patterns::FindNotifyLicensesUpdatedFunction()) return p;
        // Last resort (2026-09-22): the callback-125 anchor — the same locator CI
        // derives the pattern with (tools/derive_reconcile_byanchor.py), so a
        // rebuild that moves the prologue leaves add-without-restart working
        // until the monitor's PR lands. Unique or 0, like everything above.
        if (uintptr_t a = ReconcileAnchor::FindNotifyLicensesUpdated()) {
            Log::Warn("Reconcile: feed and pattern both MISSED or AMBIGUOUS; callback "
                      "anchor resolved 0x%lx — Steam likely reshuffled the prologue "
                      "(method=anchor(rescue))", (unsigned long)a);
            return a;
        }
        return 0;
    }();
    return addr;
}

}  // namespace

namespace LicenseReconcile {

bool Enabled() {
    // Default ON since v0.16.16 (no-restart promoted to default). KILL-SWITCH:
    // set env LUMA_NO_RECONCILE=1 or drop a marker ~/.config/lumalinux/no_reconcile
    // to force the old restart-based behaviour — e.g. if a Steam update ever
    // breaks the pattern in a way the unique-match no-op guard doesn't catch, or
    // for debugging. (The old opt-IN LUMA_NO_RESTART is gone; it's the default.)
    static const bool on = []() -> bool {
        if (std::getenv("LUMA_NO_RECONCILE") != nullptr) return false;
        if (const char* home = std::getenv("HOME")) {
            std::string m = std::string(home) + "/.config/lumalinux/no_reconcile";
            struct stat st;
            if (::stat(m.c_str(), &st) == 0) return false;
        }
        return true;
    }();
    return on;
}

Resolution Resolve() {
    if (!Enabled()) return Resolution::Disabled;
    return ResolveAddr() ? Resolution::Resolved : Resolution::Unresolved;
}

// A reconcile asked for before any CUser arrived is remembered here and fired
// when the pointer lands (the plugin hand-over, see luma_set_cuser).
std::atomic<bool> g_pending{false};
// A licence re-check request owed to SLSsteam's next config reload: the second
// arming on a client that has the request flags (see RequestLicenseCheck).
std::atomic<bool> g_rearmOnReload{false};
void Broadcast(void* user);
bool RequestLicenseCheck(void* user, const char* why);

void SetUser(void* cuser) {
    if (!cuser) return;
    void* expected = nullptr;
    g_pCUser.compare_exchange_strong(expected, cuser,
                                     std::memory_order_release,
                                     std::memory_order_relaxed);
}

// SLSsteam has just finished reloading its config (reported by the plugin, on
// SLSsteam's config-reload thread, outside any Steam call). Fire what is owed:
// a reconcile skipped for want of a CUser, or the second arming of the
// re-check flags. The achievement guard also captures the pointer but runs
// INSIDE a Steam call into SLSsteam, no place to call back into Steam, so it
// never fires anything.
void OnSlsConfigReload() {
    void* user = g_pCUser.load(std::memory_order_acquire);
    if (!user) return;
    if (g_pending.exchange(false, std::memory_order_acq_rel)) {
        Log::Info("Reconcile: SLSsteam reloaded its config — firing the reconcile that "
                  "was waiting for the CUser");
        g_rearmOnReload.store(false, std::memory_order_release);
        Broadcast(user);
        RequestLicenseCheck(user, "SLSsteam config reload");
        return;
    }
    if (g_rearmOnReload.exchange(false, std::memory_order_acq_rel))
        RequestLicenseCheck(user, "SLSsteam config reload");
}

void NotifyKeysChanged() {
    // Set the flag under the wake mutex, THEN notify, so the finder — which
    // checks the flag under the same mutex in WaitForKeysChangeOr — can never
    // miss the wakeup (every set-true is locked + notified). Still safe from the
    // inotify thread: it touches only this primitive, never Steam.
    {
        std::lock_guard<std::mutex> lk(WakeMtx());
        g_keysChanged.store(true, std::memory_order_release);
    }
    WakeCv().notify_all();
}

bool TakeKeysChanged() {
    return g_keysChanged.exchange(false, std::memory_order_acq_rel);
}

void WaitForKeysChangeOr(int timeoutSec) {
    std::unique_lock<std::mutex> lk(WakeMtx());
    // Wake early if a change is already pending or arrives during the wait; else
    // time out at the finder's normal cadence. Does NOT consume the flag (the
    // finder consumes it via TakeKeysChanged() after re-injecting). A false-going
    // transition from TakeKeysChanged() — not under this mutex — can only cause a
    // harmless spurious re-wait, never a missed wake, since every true-transition
    // goes through the locked notify above.
    WakeCv().wait_for(lk, std::chrono::seconds(timeoutSec),
                      [] { return g_keysChanged.load(std::memory_order_acquire); });
}

// The call: NotifyLicensesUpdated(CUser). Unique-match-or-no-op: a wrong-build
// pattern resolves to 0 and we degrade to a no-op instead of calling garbage.
//
// What the call does depends on the client. On the stable client (32-bit
// c770a8b1, 64-bit 17652d59) it is the whole job: Steam rebuilds its
// app<->package table from the package list in memory (our injected ids
// included) and posts LicensesUpdated_t; the install that follows finds its
// depots (measured 2026-10-24 19:08, Into the Breach added mid-session on the
// 32-bit stable client: "1 mounted depots"). On the beta client (1792711043,
// steamrt64 c3247a95) Valve split that function: the one at this address only
// posts the callbacks and requests the appinfo of the apps it lists; the table
// is rebuilt elsewhere, see RequestLicenseCheck. Same call, both clients: on
// the beta it is still what makes Steam fetch the records of the injected ids.
void Broadcast(void* user) {
    const uintptr_t addr = ResolveAddr();
    if (!addr) {
        Log::Warn("Reconcile: NotifyLicensesUpdated unresolved — no-op "
                  "(restart still works)");
        return;
    }
    reinterpret_cast<NotifyFn>(addr)(user);
    Log::Info("Reconcile: broadcast LicensesUpdated_t (CUser=%p) — "
              "appinfo/ownership refresh, no restart", user);
}

// The beta client's licence re-check request. CUser carries two flag bytes
// (offset from the feed, reconcile.licenses_check_off; 0x2370 on c3247a95):
//   [off]   "re-check licences": CUser::RunFrame clears it and runs the check
//           ("CUser::RunFrame - BCheckUserLicensesUpdate");
//   [off+1] "rebuild the table": the check clears it and calls
//           RebuildLicensesCache, then SendAppLicenseUpdates (the function
//           Broadcast calls) — once every package of the licence list is
//           loaded and every app record it lists is settled.
// Steam itself stores the word 0x0101 there when an appinfo update changed
// something (a new record arrived); with nothing new ("apps updated 0") it
// requests at most the check, and the table stays as it was. That is exactly
// what a game without DLC or shared depot looked like (Into the Breach,
// 2026-10-24, 5/5 on the beta): the ids reach package 0, Steam fetches their
// (empty) records, nothing changed, no rebuild, "0 target depots" — while a
// DLC record arriving for ANY added game rebuilt the table for all of them
// (Brotato added 18:06 → Into the Breach "Legacy Free Sub" without being
// touched). So after injecting we store the same two bytes Steam would, and
// Steam's own main loop does the rest, in order, on its thread. Byte stores
// on x86: no tearing, no lock. A build without the key (the stable client
// today) has no such flags: there Broadcast is the whole job, as before.
bool RequestLicenseCheck(void* user, const char* why) {
    static const std::size_t off = RvaFeed::LicensesCheckOff();
    if (!off) return false;
    volatile uint8_t* flags = static_cast<volatile uint8_t*>(user) + off;
    flags[1] = 1;    // rebuild the table...
    flags[0] = 1;    // ...on the next frame's check
    Log::Info("Reconcile: licence re-check requested (CUser=%p +0x%zx, %s) — Steam "
              "rebuilds its app/package table on its next frame", user, off, why);
    return true;
}

// Called by the package-0 finder right after it injected a freshly added
// game's depots (keys.txt changed). Broadcast now (Steam requests the records
// of the new ids and, on the stable client, rebuilds its table right here);
// on a client with the request flags, arm them too, now and again when
// SLSsteam reports its config reload (its AdditionalApps entry for the game
// lands a moment after keys.txt; Steam's check runs on its own frame, so the
// second arming costs nothing and covers a check that ran too early).
void Reconcile() {
    if (!Enabled()) return;
    // Measurement switch: LUMA_NO_LICENCE_NUDGE keeps the keys.txt watcher and
    // the injection (unlike LUMA_NO_RECONCILE) and only skips the broadcast and
    // the request flags, so a run shows what Steam and SLSsteam's own config
    // reload do with the injected depots on their own.
    if (std::getenv("LUMA_NO_LICENCE_NUDGE") != nullptr) {
        Log::Info("Reconcile: skipped (LUMA_NO_LICENCE_NUDGE) — depots injected, no broadcast, no request flags");
        return;
    }
    void* user = g_pCUser.load(std::memory_order_acquire);
    if (!user) {
        g_pending.store(true, std::memory_order_release);
        Log::Warn("Reconcile: no CUser captured yet — deferred until one arrives "
                  "(restart still works)");
        return;
    }
    Broadcast(user);
    if (RequestLicenseCheck(user, "depots injected"))
        g_rearmOnReload.store(true, std::memory_order_release);
}

}  // namespace LicenseReconcile

// The CUser hand-over from SLSsteam's plugin system (res/plugins/lumalinux-cuser.lua,
// installed by setup.sh). SLSsteam exposes Steam's CUser to its Lua plugins
// (SLS.steamEngine:getUser(0)); the plugin passes the raw pointer here through
// LuaJIT's ffi, calling the ADDRESS status.json publishes as "cuser_handover":
// a symbol lookup from inside SLSsteam cannot find us (it is an LD_AUDIT
// module, in the loader's audit namespace; measured 2026-10-24: "undefined
// symbol: luma_set_cuser" with this library loaded). On the 32-bit client the
// achievement guard patch captures the same pointer; on the 64-bit client that
// patch does not apply (SLSsteam's build is stripped) and this is the only
// source. Without it Reconcile never fires there and an added game needs a
// Steam restart. The plugin also reports every config reload of SLSsteam
// (SLSsteam::configLoaded), which is the second arming point above.
extern "C" __attribute__((visibility("default")))
void luma_set_cuser(void* cuser) {
    if (!cuser) return;
    Log::Debug("Reconcile: CUser %p handed over by the SLSsteam plugin", cuser);
    LicenseReconcile::SetUser(cuser);
    LicenseReconcile::OnSlsConfigReload();
}
