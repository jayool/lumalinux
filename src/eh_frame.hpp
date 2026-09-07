#pragma once

#include <cstdint>

// Exact function boundaries from `.eh_frame_hdr` — the binary's own sorted table
// of function starts, emitted by the compiler for C++ exception unwinding.
//
// Why this exists (src/gmrc_xref.hpp, KNOWN LIMIT). The string-anchor locator
// finds a site INSIDE the target function and then has to answer "where does this
// function begin?". WalkBackToPrologue answers it by scanning backwards for the
// PIC-preamble idiom, which is the entry only when the preamble sits at offset 0.
// Two reproduced ways it is not: a prologue that pushes before the thunk call
// (the shape our own DepotKey target has) yields entry+4, and an ordinary
// `call X; add eax,imm32` in the body wins the backward scan. Neither returns 0 —
// both return a plausible wrong address, and the caller detours it mid-instruction.
//
// The binary already knows the answer. Every function compiled with unwind tables
// has an FDE, and `.eh_frame_hdr` carries a binary-search table of their start
// addresses, sorted. So instead of guessing the boundary from code shape, look it
// up. Measured on build bc54101b29 (tools/experiment_eh_frame.py):
//
//   version 1; encodings pcrel|sdata4 / udata4 / datarel|sdata4 (all fixed-size)
//   133 049 functions, sorted, 1 MB table
//   all four published hook RVAs resolve EXACTLY; interior addresses resolve to
//   their true entry; out-of-range addresses are rejected
//
// It replaces a backward byte scan of up to 32 KB with ~17 comparisons, and it is
// exact rather than heuristic. No FDE or CIE decoding is needed: a function's end
// is the next entry's start, which the table already has sorted; only the last
// entry needs an external bound, and the caller's executable span supplies it.
namespace EhFrame {

// Function containing `addr`. On success sets `start` (its entry) and `end` (the
// next function's entry, or `execEnd` for the last one) and returns true.
//
// Fail-closed, returning false on: no PT_GNU_EH_FRAME, a header version other
// than 1, an encoding this parser does not implement (rather than misreading it),
// a table that is not sorted, an address outside every function, or a
// steamclient.so that cannot be read. A false here means the caller must fall
// back — never that the address is fine to use.
//
// `execEnd` bounds the last entry; pass the end of steamclient.so's r-x span.
bool FindFunction(uintptr_t addr, uintptr_t execEnd,
                  uintptr_t& start, uintptr_t& end);

} // namespace EhFrame
