// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 jayool — https://github.com/jayool/lumalinux
#pragma once

// The no-restart Add Game path (default ON since v0.16.16).
//
// After a game is added while Steam is running, Steam has neither the new
// game's keys loaded (KeyStore is preinit-only) nor a refreshed appinfo/depot
// list for it (only refreshes on restart) — so an install before restarting
// shows "0 target depots" / "Fully Installed, files missing".
//
// This module wires the missing "license reconcile" (moon/OST): call
// CUser::NotifyLicensesUpdated (LicensesUpdated_t) so Steam re-reads
// ownership + appinfo for the newly-owned apps WITHOUT a restart, and, on a
// client whose Steam only rebuilds its app<->package table from its main loop
// (the beta client, 2026-10), set the two request flags that loop consults —
// the feed says where (reconcile.licenses_check_off). The keys are refreshed
// by the keys.txt watcher (gated on Enabled()); the depots are re-injected by
// the existing package-0 finder; then the finder fires the reconcile.
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

// Capture a live CUser* — from the SLSsteam achievement guard (32-bit client,
// which receives one) or from SLSsteam's plugin system (luma_set_cuser, the
// exported entry res/plugins/lumalinux-cuser.lua calls; the only source on the
// 64-bit client). First non-null wins. Needed to call NotifyLicensesUpdated.
// A Reconcile() skipped for want of the pointer is fired as soon as it lands.
void SetUser(void* cuser);
// SLSsteam has finished reloading its config (the plugin hand-over calls this
// on every reload, never from inside a Steam call): fire a reconcile skipped
// for want of the pointer, or re-arm the licence re-check flags a Reconcile()
// just set (second arming, after SLSsteam lists the game).
void OnSlsConfigReload();

// Mark that keys.txt changed (a game was added). Called from the keys.txt
// watcher (inotify thread) — it must NOT call into Steam itself.
void NotifyKeysChanged();

// Atomically consume the keys-changed flag. Called from the package-0 finder
// thread after it re-injects, so the reconcile fires on that thread (which
// already touches Steam structures) and AFTER the new depots are injected.
bool TakeKeysChanged();

// Sleep up to `timeoutSec`, but wake IMMEDIATELY if keys.txt changed
// (NotifyKeysChanged fired) — returns at once if a change is already pending.
// The package-0 finder calls this in place of a plain sleep so a freshly-added
// game is injected + reconciled without waiting for the next slow poll tick
// (the #38 "install too soon -> 0 target depots" window). Does NOT consume the
// flag; the finder still calls TakeKeysChanged() after re-injecting. Call from
// the finder thread only.
void WaitForKeysChangeOr(int timeoutSec);

// Fire CUser::NotifyLicensesUpdated once, if enabled + a CUser is known + the
// function resolves uniquely (Steam refreshes appinfo/ownership, no restart),
// and set the licence re-check flags when the feed names them. Safe no-op
// otherwise. Call from a Steam thread (the finder), never from the inotify
// thread.
void Reconcile();

}  // namespace LicenseReconcile

// The hand-over entry SLSsteam's plugin calls (res/plugins/lumalinux-cuser.lua).
// Its ADDRESS is published in status.json ("cuser_handover"): SLSsteam is an
// LD_AUDIT module and lives in the dynamic loader's audit namespace, where a
// dlsym() cannot see the application's libraries, lumalinux included; a raw
// address call works across namespaces (same process memory).
extern "C" void luma_set_cuser(void* cuser);
