#include "license_reconcile.hpp"
#include "patterns.hpp"
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
std::mutex              g_wakeMtx;
std::condition_variable g_wakeCv;

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
// no-ops rather than firing at a garbage address. docs/rva-feed-design.md.
uintptr_t ResolveAddr() {
    static const uintptr_t addr = []() -> uintptr_t {
        if (uintptr_t feed = RvaFeed::Resolve("Reconcile")) {
            if (uintptr_t p = Patterns::FindNotifyLicensesUpdatedFunction(); p && p != feed)
                Log::Warn("Reconcile: feed 0x%lx != pattern 0x%lx — using feed; "
                          "investigate drift", (unsigned long)feed, (unsigned long)p);
            return feed;
        }
        return Patterns::FindNotifyLicensesUpdatedFunction();
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

void SetUser(void* cuser) {
    if (!cuser) return;
    void* expected = nullptr;
    g_pCUser.compare_exchange_strong(expected, cuser,
                                     std::memory_order_release,
                                     std::memory_order_relaxed);
}

void NotifyKeysChanged() {
    // Set the flag under the wake mutex, THEN notify, so the finder — which
    // checks the flag under the same mutex in WaitForKeysChangeOr — can never
    // miss the wakeup (every set-true is locked + notified). Still safe from the
    // inotify thread: it touches only this primitive, never Steam.
    {
        std::lock_guard<std::mutex> lk(g_wakeMtx);
        g_keysChanged.store(true, std::memory_order_release);
    }
    g_wakeCv.notify_all();
}

bool TakeKeysChanged() {
    return g_keysChanged.exchange(false, std::memory_order_acq_rel);
}

void WaitForKeysChangeOr(int timeoutSec) {
    std::unique_lock<std::mutex> lk(g_wakeMtx);
    // Wake early if a change is already pending or arrives during the wait; else
    // time out at the finder's normal cadence. Does NOT consume the flag (the
    // finder consumes it via TakeKeysChanged() after re-injecting). A false-going
    // transition from TakeKeysChanged() — not under this mutex — can only cause a
    // harmless spurious re-wait, never a missed wake, since every true-transition
    // goes through the locked notify above.
    g_wakeCv.wait_for(lk, std::chrono::seconds(timeoutSec),
                      [] { return g_keysChanged.load(std::memory_order_acquire); });
}

void Reconcile() {
    if (!Enabled()) return;

    void* user = g_pCUser.load(std::memory_order_acquire);
    if (!user) {
        Log::Warn("Reconcile: no CUser captured yet — skipping (restart still works)");
        return;
    }

    // Reuse the cached resolution (same address Resolve() reported to status).
    // Unique-match-or-no-op: a wrong-build pattern is 0 here and we degrade to a
    // no-op instead of calling garbage.
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

}  // namespace LicenseReconcile
