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
// 64-bit client: a reconcile owed to the next SLSsteam config reload. See
// Reconcile() for why the broadcast must wait for it there.
std::atomic<bool> g_onReload{false};
void Broadcast(void* user);

void SetUser(void* cuser) {
    if (!cuser) return;
    void* expected = nullptr;
    g_pCUser.compare_exchange_strong(expected, cuser,
                                     std::memory_order_release,
                                     std::memory_order_relaxed);
}

// SLSsteam has just finished reloading its config (reported by the plugin, on
// SLSsteam's config-reload thread, outside any Steam call). Fire what is owed:
// the 64-bit client's deferred broadcast, or one skipped for want of a CUser.
// The achievement guard also captures the pointer but runs INSIDE a Steam call
// into SLSsteam, no place to call back into Steam, so it never fires anything.
void OnSlsConfigReload() {
    void* user = g_pCUser.load(std::memory_order_acquire);
    if (!user) return;
    const bool owed = g_onReload.exchange(false, std::memory_order_acq_rel);
    const bool pending = g_pending.exchange(false, std::memory_order_acq_rel);
    if (!owed && !pending) return;
    Log::Info("Reconcile: SLSsteam reloaded its config — broadcasting now%s",
              pending ? " (a reconcile was waiting for the CUser)" : "");
    Broadcast(user);
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

// The actual call: NotifyLicensesUpdated(CUser). Unique-match-or-no-op: a
// wrong-build pattern resolves to 0 and we degrade to a no-op instead of
// calling garbage.
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

// Called by the package-0 finder right after it injected a freshly added
// game's depots (keys.txt changed).
//
// 32-bit client: broadcast now (measured since v0.16.16: the install that
// follows finds its depots, no restart).
//
// 64-bit client: NOT now. Adding a game writes SLSsteam's AdditionalApps and
// keys.txt a millisecond apart and both react asynchronously; the finder is
// faster than SLSsteam's YAML reload, so a broadcast here reaches Steam while
// SLSsteam still answers "not owned" for the game. Steam evaluates a new
// game ONCE per session and keeps that answer: with the early broadcast the
// install ended in "0 target depots" and neither a broadcast at SLSsteam's
// reload nor one minutes later changed it (2026-10-24, 17:02 and 17:05);
// with no broadcast until SLSsteam's reload, the game downloaded at the
// first try (14:39, 1/1 — and the earlier Lua-only experiments, 3/3). So here
// the broadcast is only owed, and OnSlsConfigReload pays it when SLSsteam
// says it has applied its config, the game listed. Without the plugin
// (older installs) nothing fires: the restart path, as before this change.
void Reconcile() {
    if (!Enabled()) return;
#ifdef LUMA_X86_64
    g_onReload.store(true, std::memory_order_release);
    Log::Info("Reconcile: depots injected — broadcast owed to SLSsteam's next config reload");
#else
    void* user = g_pCUser.load(std::memory_order_acquire);
    if (!user) {
        g_pending.store(true, std::memory_order_release);
        Log::Warn("Reconcile: no CUser captured yet — deferred until one arrives "
                  "(restart still works)");
        return;
    }
    Broadcast(user);
#endif
}

}  // namespace LicenseReconcile

// The CUser hand-over from SLSsteam's plugin system (res/plugins/lumalinux-cuser.lua,
// installed by setup.sh). SLSsteam exposes Steam's CUser to its Lua plugins
// (SLS.steamEngine:getUser(0)); the plugin passes the raw pointer here through
// LuaJIT's ffi, calling the ADDRESS status.json publishes as "cuser_handover":
// a symbol lookup from inside SLSsteam cannot find us (it is an LD_AUDIT
// module, in the loader's audit namespace; measured 2026-10-24: "undefined
// symbol: luma_set_cuser" with this library loaded). On the 32-bit client the achievement
// guard patch captures the same pointer; on the 64-bit client that patch does
// not apply (SLSsteam's build is stripped) and this is the only source. Without
// it Reconcile never fires on the 64-bit client, and a game added without any
// related appinfo arriving afterwards (no DLC, no shared depot, no app token)
// installs with "0 target depots" until Steam restarts (measured 2026-10-24,
// Into the Breach, 2/2; with the pointer 1/1 at the first try).
extern "C" __attribute__((visibility("default")))
void luma_set_cuser(void* cuser) {
    if (!cuser) return;
    Log::Debug("Reconcile: CUser %p handed over by the SLSsteam plugin", cuser);
    LicenseReconcile::SetUser(cuser);
    LicenseReconcile::OnSlsConfigReload();
}
