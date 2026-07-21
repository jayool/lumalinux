#pragma once

// The no-restart Add Game path (default ON since v0.16.16).
//
// After a game is added while Steam is running, Steam has neither the new
// game's keys loaded (KeyStore is preinit-only) nor a refreshed appinfo/depot
// list for it (only refreshes on restart) — so an install before restarting
// shows "0 target depots" / "Fully Installed, files missing".
//
// This module wires the missing "license reconcile" (moon/OST): broadcast
// CUser::NotifyLicensesUpdated (LicensesUpdated_t) so Steam re-reads
// ownership + appinfo for the newly-owned apps WITHOUT a restart. The keys
// are refreshed by the keys.txt watcher (gated on Enabled()); the depots are
// re-injected by the existing package-0 finder; then the finder fires the
// reconcile.
//
// ALL of this degrades gracefully. Worst case: the NotifyLicensesUpdated
// pattern doesn't resolve uniquely on this Steam build -> no-op -> the restart
// path still works. The kill-switch LUMA_NO_RECONCILE forces that fallback.
namespace LicenseReconcile {

// True unless the kill-switch is set: env LUMA_NO_RECONCILE, or the marker
// file ~/.config/lumalinux/no_reconcile exists. Default ON. Read once.
bool Enabled();

// Resolution state of the NotifyLicensesUpdated function, for status reporting.
//   Disabled   — the kill-switch is set; the feature is off on purpose.
//   Resolved   — enabled AND the pattern resolved uniquely (no-restart works).
//   Unresolved — enabled but the pattern didn't resolve on this build (a Steam
//                update moved it): Reconcile() no-ops, Add Game falls back to
//                needing a restart. LumaDeck reads this (status.json hook
//                "Reconcile") to suppress the premature library appearance.
enum class Resolution { Disabled, Resolved, Unresolved };

// Resolve the function once (cached) and report its state. Call at hook-install
// time, when steamclient.so is already mapped (the other patterns resolve there
// too). Reconcile() reuses the same cached address.
Resolution Resolve();

// Capture a live CUser* (called from the SLSsteam achievement guard, which
// receives one). First non-null wins. Needed to call NotifyLicensesUpdated.
void SetUser(void* cuser);

// Mark that keys.txt changed (a game was added). Called from the keys.txt
// watcher (inotify thread) — it must NOT call into Steam itself.
void NotifyKeysChanged();

// Atomically consume the keys-changed flag. Called from the package-0 finder
// thread after it re-injects, so the reconcile fires on that thread (which
// already touches Steam structures) and AFTER the new depots are injected.
bool TakeKeysChanged();

// Fire CUser::NotifyLicensesUpdated once, if enabled + a CUser is known + the
// function resolves uniquely. Broadcasts LicensesUpdated_t -> Steam refreshes
// appinfo/ownership, no restart. Safe no-op otherwise. Call from a Steam
// thread (the finder), never from the inotify thread.
void Reconcile();

}  // namespace LicenseReconcile
