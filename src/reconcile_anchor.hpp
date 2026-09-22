#pragma once

#include <cstdint>

// Runtime rescue for CUser::NotifyLicensesUpdated (the "Reconcile" function),
// third resolver after the RVA feed and the byte pattern (2026-09-22). The
// locator is ReconcileAnchorCore (reconcile_anchor_core.hpp): the function is
// the ONE that posts callback 125 (LicensesUpdated_t, a public-SDK number) and
// reads a field of `this` first, bailing when it is <= 0. No layout number, no
// prologue shape — the two things a Steam rebuild moves.
//
// Read-only and fail-closed: 0 when zero or several functions qualify, when
// steamclient.so's executable span is not visible, or when .eh_frame_hdr is
// unusable (there is no walk-back here; without the function table a callback
// site cannot be turned into an exact entry, and an inexact one would be
// CALLED — LicenseReconcile::Reconcile calls the address directly).
namespace ReconcileAnchor {

// Absolute runtime address of CUser::NotifyLicensesUpdated, or 0.
uintptr_t FindNotifyLicensesUpdated();

} // namespace ReconcileAnchor
