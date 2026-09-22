#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# derive_reconcile_byanchor.py — re-derive kNotifyLicensesUpdatedPattern
# (the "Reconcile" hook, CUser::NotifyLicensesUpdated) WITHOUT Ghidra.
#
# Why this exists (2026-09-22): derive_patterns.py reaches Reconcile by
# following the type_info of LicensesUpdated_t, and that walk fails on every
# build ("type_info not referenced by any function") — for a reason we now
# understand: the function posts the callback by its NUMBER (ECallbackType
# 0x7d = 125), it never touches the struct's type_info. So a moved Reconcile
# went straight to an issue and a human re-derived it by hand.
#
# The locator anchors on what the function DOES, with no struct-layout number
# and no prologue shape in the criteria (measured with
# tools/experiment_reconcile_xref.py on bc54101b and 9cf4720f):
#   c1  it posts callback 125:            6A 7D 50 E8 …   (push 0x7d; push eax; call)
#       — 11 functions per build do this; the poster is the anchor, not the key
#   c2  it reads a field of `this` first:  8B 45 08 8B /r [eax+disp32]
#       — mov eax,[ebp+8] ; mov r,[eax+disp32], disp32 FREE (it moved
#         0x1b14 -> 0x1b10 between those two builds; it is NOT a criterion)
#   c3  and bails when that value is <= 0: 85 /r … 0F 8E   (test r,r ; jle)
#   c1+c2+c3 leaves exactly ONE function on both builds, the right one
#   (0x188c950 / 0x1a0d010). Two or zero -> refuse, never guess.
# Function boundaries come from .eh_frame_hdr (check_patterns.eh_frame_starts,
# the table GMRC's rescue already relies on).
#
# The pattern itself is fabricated by derive_from_address.derive_at with the
# frame size and every [reg+disp32] displacement masked (mask_frame,
# mask_disp32): the member offset and the spill are layout numbers, exactly
# what drifted, so they never go back into patterns.hpp as literals.
#
# Usage:
#   tools/derive_reconcile_byanchor.py steamclient.so [--derived derived.json]
#       [--min-bytes 28] [--max-bytes 96]
# Exit codes: 0 derived UNIQUE (and written) | 2 the anchor did not resolve
#             to exactly one function | 3 could not build a unique pattern |
#             4 internal sanity failure.
import argparse
import bisect
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_patterns import (_load_sections, _read_va, eh_frame_starts,  # noqa: E402
                            load_exec_segments)
from derive_from_address import (PROLOGUE_BYTES, derive_at,  # noqa: E402
                                 exit_code_for, write_derived)

LABEL = "kNotifyLicensesUpdatedPattern"
CALLBACK_POST = b"\x6A\x7D\x50\xE8"      # push 0x7d ; push eax ; call rel32
HEAD_BYTES = 96                          # how far into the function c2/c3 may sit
JLE_WINDOW = 16                          # bytes after the this-read for test+jle


def function_of(starts, va):
    """Start of the function containing va, per the sorted .eh_frame_hdr table."""
    i = bisect.bisect_right(starts, va) - 1
    return starts[i] if i >= 0 else None


def function_bounds(starts, va):
    """(start, end) of the function containing va; end is the next start, or
    None for the last one. The head inspected for c2/c3 is bounded by `end`:
    a small function's 96-byte window would otherwise run into its neighbour
    and borrow its this-read (found with a gcc -m32 object, 2026-09-22; the
    C++ port in src/reconcile_anchor_core.hpp applies the same bound)."""
    i = bisect.bisect_right(starts, va) - 1
    if i < 0:
        return None, None
    return starts[i], (starts[i + 1] if i + 1 < len(starts) else None)


def posts_callback_125(text, text_va):
    """Every `push 0x7d ; push eax ; call` site in .text (c1), as RVAs."""
    out, i = [], 0
    while True:
        j = text.find(CALLBACK_POST, i)
        if j < 0:
            return out
        i = j + 1
        out.append(text_va + j)


def reads_this_then_bails(head):
    """c2 + c3 on the first bytes of a function. Returns (ok, member_disp) —
    member_disp is informational only, never a criterion."""
    for k in range(0, max(0, len(head) - 9)):
        if head[k:k + 3] == b"\x8B\x45\x08" and head[k + 3] == 0x8B \
                and (head[k + 4] & 0xC0) == 0x80 and (head[k + 4] & 7) == 0:
            disp = struct.unpack_from("<I", head, k + 5)[0]
            window = head[k + 9:k + 9 + JLE_WINDOW]
            for m in range(max(0, len(window) - 3)):
                if window[m] == 0x85 and window[m + 2:m + 4] == b"\x0F\x8E":
                    return True, disp
            return False, disp
    return False, None


def locate(text, text_va, starts, read_va):
    """The pure part: candidates by c1, filtered by c2+c3. Returns
    (status, rva, info) with status UNIQUE / NOT_FOUND / AMBIGUOUS."""
    sites = posts_callback_125(text, text_va)
    bounds = {}
    for s_ in sites:
        f, e = function_bounds(starts, s_)
        if f is not None:
            bounds[f] = e
    fns = sorted(bounds)
    kept = []
    for f in fns:
        n = HEAD_BYTES if bounds[f] is None else min(HEAD_BYTES, bounds[f] - f)
        ok, disp = reads_this_then_bails(read_va(f, n))
        if ok:
            kept.append((f, disp))
    info = {"callback_sites": len(sites), "candidate_fns": len(fns),
            "kept": ["0x%x" % f for f, _ in kept]}
    if len(kept) == 1:
        info["member_disp"] = "0x%x" % kept[0][1]
        return "UNIQUE", kept[0][0], info
    return ("NOT_FOUND" if not kept else "AMBIGUOUS"), None, info


def locate_in(path):
    """locate() over a real steamclient.so."""
    data, secs = _load_sections(path)
    if ".text" not in secs:
        return "NO_TEXT", None, {}
    starts, _info, note = eh_frame_starts(path)
    if not starts:
        return "NO_EH_FRAME", None, {"note": note}
    tx_a, tx_o, tx_s = secs[".text"]
    return locate(data[tx_o:tx_o + tx_s], tx_a, starts, _read_va(data, secs))


def derive(path, min_bytes=PROLOGUE_BYTES, max_bytes=96):
    status, rva, info = locate_in(path)
    if status != "UNIQUE":
        return None, "anchor did not resolve (%s): %s" % (status, info)
    print("  anchor: %d `push 0x7d;push eax;call` site(s) in %d function(s); "
          "this-read + jle keeps 1 -> 0x%x (member disp %s, informational)"
          % (info["callback_sites"], info["candidate_fns"], rva, info["member_disp"]))
    return derive_at(path, rva, "callback-anchor", min_bytes, max_bytes,
                     mask_frame=True, mask_disp32=True,
                     segments=load_exec_segments(path))


def main():
    ap = argparse.ArgumentParser(description="derive kNotifyLicensesUpdatedPattern by its callback anchor")
    ap.add_argument("steamclient")
    ap.add_argument("--derived", default=None,
                    help="derived.json to create/merge the result into (derive_patterns.py format)")
    ap.add_argument("--min-bytes", type=int, default=PROLOGUE_BYTES)
    ap.add_argument("--max-bytes", type=int, default=96)
    args = ap.parse_args()

    print("---- %s (callback anchor, no Ghidra) ----" % LABEL)
    entry, note = derive(args.steamclient, args.min_bytes, args.max_bytes)
    if entry is None:
        print("  NOT DERIVED: %s" % note)
        return 2 if note.startswith("anchor") else exit_code_for(note)
    print("  UNIQUE @ %s : %s" % (entry["rva"], entry["pattern"]))
    if args.derived:
        write_derived(args.derived, LABEL, entry)
    return 0


if __name__ == "__main__":
    sys.exit(main())
