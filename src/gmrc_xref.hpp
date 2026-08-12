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
namespace GmrcXref {

// Absolute runtime address of the GMRC getter derived via the job-name xref, or
// 0 on any failure/ambiguity. Must be called while steamclient.so is mapped and
// BEFORE the GMRC detour is installed (the walk-back reads the intact prologue).
uintptr_t FindGmrcFunction();

} // namespace GmrcXref
