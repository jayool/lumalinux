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

}  // namespace

namespace LicenseReconcile {

bool Enabled() {
    static const bool on = []() -> bool {
        if (std::getenv("LUMA_NO_RESTART") != nullptr) return true;
        if (const char* home = std::getenv("HOME")) {
            std::string m = std::string(home) + "/.config/lumalinux/no_restart";
            struct stat st;
            if (::stat(m.c_str(), &st) == 0) return true;
        }
        return false;
    }();
    return on;
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

    // Resolve once. FindNotifyLicensesUpdatedFunction requires a UNIQUE match
    // and returns 0 otherwise (ambiguous/absent), so a wrong-build pattern
    // degrades to a no-op instead of calling garbage. Cached: if it's 0 the
    // first time (with a user + steamclient.so mapped), it stays 0.
    static const uintptr_t addr = Patterns::FindNotifyLicensesUpdatedFunction();
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
