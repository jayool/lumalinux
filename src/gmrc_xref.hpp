#pragma once

#include <cstdint>

// GMRC string-xref locator (update-resilient, in-client — issue #13 Part 1).
//
// The byte-pattern locator (kGmrcFunctionPattern) breaks when a Steam rebuild
// reshuffles the getter's prologue (register order / stack size) even though the
// function itself is unchanged. This locator instead anchors on a stable datum
// that lives INSIDE the function: the IPC job-name string
// "ContentServerDirectory.GetManifestRequestCode#1", which BYieldingGet-
// ManifestRequestCode loads to build its request. None of {string bytes, its
// single in-function reference} moves on a mere recompile, so the getter is
// found wherever the rebuild put it.
//
// Derivation (i386 PIC, mirrors tools/verify_gmrc_anchor.py, which is ground-
// truth-validated against a real steamclient.so: xref entry == byte-pattern
// entry on build 1c168d2f):
//   1. find the job-name string in steamclient.so's readable mappings  -> S
//   2. derive the module GOT base by consensus over PIC preambles       -> G
//      (independent of GMRC; any `call get_pc_thunk; add reg,imm32` site)
//   3. scan .text for the UNIQUE `lea reg,[base + (S - G)]`             -> site
//   4. walk back to that function's PIC prologue                        -> entry
//
// Read-only and fail-closed: returns 0 on any miss OR ambiguity (string not
// found, no unique lea, walk-back fails). The caller keeps the byte pattern as
// the primary path and uses this only to rescue a pattern miss, logging drift
// when both resolve and disagree — so this can never regress GMRC location.
//
// KNOWN LIMIT of step 4 — it is NOT fail-closed, and the caveat is load-bearing.
// WalkBackToPrologue does not find "the function entry"; it finds the nearest
// preceding `E8 rel32` whose next byte is `05` / `81 C?` — i.e. the PIC preamble
// idiom — and returns THAT. Those coincide only when the preamble sits at
// offset 0 of the function. Two ways they don't, both reproduced against a
// synthetic .text (2026-09-03, not observed on a real build):
//   (a) A prologue that pushes before the thunk call — the shape our OWN
//       DepotKey target has (`55 57 56 53 E8 …`) — yields entry+4.
//   (b) `call X; add eax,imm32` is also ordinary code (`return f() + k;`), so a
//       body carrying one between its entry and the lea site wins the backward
//       scan; a synthetic case returned entry+0x48.
// Neither returns 0: they return a plausible WRONG address, and the caller
// installs a detour there — mid-instruction, so a crash rather than a dead hook.
//
// Why this matters more than the numbers suggest: the rescue path only runs
// when the byte pattern already missed, i.e. exactly when there is no second
// opinion to drift-check against. And the event that triggers the rescue (Steam
// reshuffling the prologue) is the same event that can move the preamble off
// offset 0. Case (b) is at least self-announcing — while the pattern still
// resolves, a spurious hit shows up as a DRIFT warning — but case (a) is not.
//
// MEASURED 2026-09-07 — the name-derived resolver is NOT an option here, so
// this walk-back cannot simply be replaced. Rtti::ResolveVtableSlotByName gave
// DepotKey a resolver that reads no prologue at all (docs/slssteam-plugins-
// analysis.md §7.5.a); the obvious follow-up was to do the same for GMRC and
// retire step 4. It does not apply: "GetManifestRequestCode" exists exactly once
// in build bc54101b29 and NONE of the 52 `*IClient…Map` interface-map vtables has
// a slot referencing it (swept with tools/experiment_ifacemap_slot.py over every
// map class; control run with GetBinary/21IClientConfigStoreMap hit slots 6 and 7
// as expected, so the sweep was sound). The getter is not an interface method —
// that lone string is the IPC job name this locator already anchors on. So GMRC
// keeps the xref, and the fixes below are the way out, not a workaround.
//
// RESOLVED for the normal path (2026-09-07): step 4 now queries .eh_frame_hdr's
// sorted function-start table (src/eh_frame.hpp) instead of inferring the entry
// from code shape — exact, and it rejects an address in no function instead of
// returning the nearest thing that looks like an entry. WalkBackToPrologue stays
// ONLY as the fallback for a build with no usable table, so everything below
// still describes it accurately, and still applies whenever that fallback runs.
//
// RETIREMENT CONDITION, so this does not linger as scaffolding the way the RVA
// feed's phase-3 drift scan did: if real logs never emit ".eh_frame_hdr
// unavailable — falling back to the walk-back" (gmrc_xref.cpp), delete
// WalkBackToPrologue and its two failure modes with it. Until then it is the only
// thing standing between "no table" and "no rescue at all".
//
// Not urgent, on our own evidence: across the 11 builds whitelisted since
// 2026-06-11 no prologue has moved at all (`res/updates.yaml` still has a single
// pattern-set group). Cheap fix if this file is touched: on a hit, look at the
// bytes immediately before the preamble and keep walking over a push run — that
// closes (a), the case with no early warning. Proper fix: resolve the entry from
// `.eh_frame_hdr`'s sorted function-start table instead of scanning backwards,
// which closes both and drops the prologue assumption this locator exists to
// avoid.
namespace GmrcXref {

// Absolute runtime address of the GMRC getter derived via the job-name xref, or
// 0 on any failure/ambiguity. Must be called while steamclient.so is mapped and
// BEFORE the GMRC detour is installed (the walk-back reads the intact prologue).
uintptr_t FindGmrcFunction();

} // namespace GmrcXref
