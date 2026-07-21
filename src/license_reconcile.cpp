#include "license_reconcile.hpp"
#include "patterns.hpp"
#include "log.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <string>

#include <sys/stat.h>

namespace {

std::atomic<void*> g_pCUser{nullptr};
std::atomic<bool>  g_keysChanged{false};

// CUser::NotifyLicensesUpdated is cdecl, single arg (`this`). It rebuilds the
// LicensesUpdated_t callback from this user's own license vector and posts it.
using NotifyFn = void (*)(void*);

// Resolve the NotifyLicensesUpdated address ONCE and cache it. Deterministic per
// process (steamclient.so base is fixed after load), so a static is safe.
// FindNotifyLicensesUpdatedFunction requires a UNIQUE match and returns 0
// otherwise, so a wrong-build pattern caches 0 -> the feature no-ops. Shared by
// Resolve() (status, called at hook-install time) and Reconcile() (the fire).
uintptr_t ResolveAddr() {
    static const uintptr_t addr = Patterns::FindNotifyLicensesUpdatedFunction();
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
    g_keysChanged.store(true, std::memory_order_release);
}

bool TakeKeysChanged() {
    return g_keysChanged.exchange(false, std::memory_order_acq_rel);
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
