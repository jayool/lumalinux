#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# derive_bytext.py — re-derive the TEXT-ANCHORED hook patterns WITHOUT Ghidra:
# GMRC, BuildDep and ShaderDepot.
#
# Each of these functions uses one fixed string that nothing else in
# steamclient.so loads the same way, and that string is what derive_patterns.py
# (the Ghidra postScript) anchors on. The locator here is the runtime's own
# GMRC rescue (src/gmrc_xref.cpp), already mirrored in check_patterns.py as
# gmrc_xref_derive: string -> GOT base by consensus -> the UNIQUE
# `lea reg,[got+disp32]` that loads it -> the containing function per
# .eh_frame_hdr. Measured 2026-09-22 on bc54101b and 9cf4720f: every one of the
# three strings has exactly ONE such site per build, and its function is where
# check_patterns.py resolves the shipped pattern (GMRC 0x1371ac0/0x14f6900,
# BuildDep 0xffca80/0x11781b0, ShaderDepot 0x1048840/0x11c46a0).
#
# From that address derive_from_address.derive_at fabricates the pattern with
# the frame size masked (GMRC's `sub esp` grew 0x110 -> 0x120 on the beta and
# nothing else in its prologue changed) but member/spill offsets literal, the
# same rules the Ghidra postScript applies to these three.
#
# Usage:
#   tools/derive_bytext.py steamclient.so [--only kGmrcFunctionPattern[,…]]
#       [--derived derived.json] [--min-bytes 28] [--max-bytes 96]
# Without --only, every hook in TEXT_ANCHORS is attempted; the exit code
# reflects the WORST outcome, but every UNIQUE result is still written.
# Exit codes: 0 all requested derived UNIQUE | 2 a text anchor did not resolve
#             to one function | 3 could not build a unique pattern | 4 sanity.
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_patterns import gmrc_xref_derive, load_exec_segments  # noqa: E402
from derive_from_address import (PROLOGUE_BYTES, derive_at,  # noqa: E402
                                 exit_code_for, write_derived)

# patterns.hpp constant -> (display label, the string the function loads).
# Same anchors as ANCHORED_HOOKS in derive_patterns.py; keep the two in step.
TEXT_ANCHORS = {
    "kGmrcFunctionPattern":         ("GMRC",        "ContentServerDirectory.GetManifestRequestCode#1"),
    "kBuildDepotDependencyPattern": ("BuildDep",    "BuildDepotDependency"),
    "kShaderCacheDepotPattern":     ("ShaderDepot", "shadercachedepot"),
}


def locate(path, anchor):
    """(status, rva, info): the function that loads `anchor`, or why not."""
    info, note = gmrc_xref_derive(path, anchor)
    if info.get("status") != "UNIQUE":
        return info.get("status", "?"), None, note
    return "UNIQUE", int(info["rva"], 16), note


def derive_one(path, const, min_bytes=PROLOGUE_BYTES, max_bytes=96, segments=None):
    label, anchor = TEXT_ANCHORS[const]
    status, rva, note = locate(path, anchor)
    if status != "UNIQUE":
        return None, "text anchor %r did not resolve (%s): %s" % (anchor, status, note)
    print("  %s: %r -> function @ 0x%x" % (label, anchor, rva))
    return derive_at(path, rva, "text-xref", min_bytes, max_bytes,
                     mask_frame=True, segments=segments)


def main():
    ap = argparse.ArgumentParser(description="derive the text-anchored patterns (GMRC, BuildDep, ShaderDepot) without Ghidra")
    ap.add_argument("steamclient")
    ap.add_argument("--only", default=None,
                    help="comma-separated patterns.hpp constants (default: all of TEXT_ANCHORS)")
    ap.add_argument("--derived", default=None,
                    help="derived.json to create/merge results into (derive_patterns.py format)")
    ap.add_argument("--min-bytes", type=int, default=PROLOGUE_BYTES)
    ap.add_argument("--max-bytes", type=int, default=96)
    args = ap.parse_args()

    wanted = [c for c in args.only.split(",") if c] if args.only else list(TEXT_ANCHORS)
    unknown = [c for c in wanted if c not in TEXT_ANCHORS]
    if unknown:
        print("not text-anchored: %s (known: %s)" % (", ".join(unknown), ", ".join(TEXT_ANCHORS)))
        return 1
    segments = load_exec_segments(args.steamclient)
    worst = 0
    for const in wanted:
        print("---- %s (text anchor, no Ghidra) ----" % const)
        entry, note = derive_one(args.steamclient, const, args.min_bytes, args.max_bytes, segments)
        if entry is None:
            print("  NOT DERIVED: %s" % note)
            worst = max(worst, 2 if note.startswith("text anchor") else exit_code_for(note))
            continue
        print("  UNIQUE @ %s : %s" % (entry["rva"], entry["pattern"]))
        if args.derived:
            write_derived(args.derived, const, entry)
    return worst


if __name__ == "__main__":
    sys.exit(main())
