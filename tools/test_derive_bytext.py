#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Unit test for derive_bytext.py — the text-anchored deriver (GMRC, BuildDep,
# ShaderDepot). The locator itself is check_patterns.gmrc_xref_derive, which
# needs a real ELF (sections, GOT consensus, .eh_frame_hdr) and is measured
# against real builds in the field; here it is STUBBED, and what is pinned is:
#   1. the table: the three constants, each with the anchor derive_patterns.py
#      uses (the two must stay in step);
#   2. the refusal rule: a locator verdict other than UNIQUE derives nothing;
#   3. the masking: derived with mask_frame, so a GMRC prologue whose
#      `sub esp,imm32` grew (0x110 -> 0x120 on the 9cf4720f beta) yields the
#      SAME pattern from both builds, and that pattern matches only the function;
#   4. --only rejects a constant that is not text-anchored.
#
#   python3 tools/test_derive_bytext.py     # from the repo root
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import derive_bytext as dbt  # noqa: E402
from check_patterns import scan_rvas  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VADDR = 0x1000
fails = 0


def check(cond, msg):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        fails += 1


def gmrc_like(frame):
    """GMRC's prologue: call thunk ; add eax,imm32 ; push ebp … sub esp,frame ;
    mov edi,[ebp+8] ; mov ecx,[ebp+0x20] ; then some body."""
    return (b"\xE8\x00\x00\x00\x00" + b"\x05" + struct.pack("<I", 0x1ff000) +
            bytes.fromhex("5589E5575653") + b"\x81\xEC" + struct.pack("<I", frame) +
            bytes.fromhex("8B7D08" "8B4D20") + bytes.fromhex("8B45 0C 89 45 D0".replace(" ", "")) +
            b"\x90" * 16 + b"\xC3")


def main():
    # ── 1. the table mirrors derive_patterns.py's ANCHORED_HOOKS ────────────
    src = open(os.path.join(REPO, "tools", "derive_patterns.py")).read()
    i = src.index("ANCHORED_HOOKS = [")
    j = src.index("]", src.index("kShaderCacheDepotPattern", i)) + 1
    ghidra = dict(re.findall(r'\("(k\w+)",\s*"([^"]+)"\)', src[i:j]))
    ours = {c: a for c, (_l, a) in dbt.TEXT_ANCHORS.items()}
    check(ghidra == ours, "TEXT_ANCHORS == derive_patterns.ANCHORED_HOOKS %s" % sorted(ours))
    check(set(ours) == {"kGmrcFunctionPattern", "kBuildDepotDependencyPattern", "kShaderCacheDepotPattern"},
          "the three text-anchored constants, no more")

    # ── 2. refusal on a non-UNIQUE locator verdict ──────────────────────────
    for status in ("AMBIGUOUS", "NO_SITE", "STRING_NOT_FOUND", "NO_EH_FRAME"):
        dbt.gmrc_xref_derive = lambda path, anchor, s=status: ({"status": s}, "stub")
        entry, note = dbt.derive_one("x.so", "kGmrcFunctionPattern", segments=[(VADDR, b"\x90" * 64)])
        check(entry is None and note.startswith("text anchor"), "locator %-16s -> refused" % status)

    # ── 3. mask_frame: same pattern from a 0x110 and a 0x120 build ──────────
    pats = {}
    for frame in (0x110, 0x120):
        f = gmrc_like(frame)
        text = b"\x90" * 0x80 + f + b"\x90" * 0x80
        rva = VADDR + 0x80
        dbt.gmrc_xref_derive = lambda path, anchor, r=rva: ({"status": "UNIQUE", "rva": "0x%x" % r}, "ok")
        entry, note = dbt.derive_one("x.so", "kGmrcFunctionPattern", segments=[(VADDR, text)])
        check(entry is not None, "frame 0x%x: derived (%s)" % (frame, note))
        if entry:
            check(scan_rvas([(VADDR, text)], entry["pattern"]) == [rva],
                  "frame 0x%x: pattern matches only the function" % frame)
            pats[frame] = entry["pattern"]
    check(len(set(pats.values())) == 1, "the two builds yield the SAME pattern (frame masked): %s"
          % pats.get(0x110))
    if pats:
        toks = pats[0x110].split()
        check(toks[16:22] == ["81", "EC", "??", "??", "??", "??"], "sub esp,imm32 is wildcarded")
        check(toks[22:28] == ["8B", "7D", "08", "8B", "4D", "20"], "the register bytes after it stay literal")

    # ── 4. --only rejects a non-text-anchored constant ──────────────────────
    sys.argv = ["derive_bytext.py", "x.so", "--only", "kDepotKeyFnPattern"]
    dbt.load_exec_segments = lambda p: [(VADDR, b"\x90" * 64)]
    check(dbt.main() == 1, "--only kDepotKeyFnPattern -> usage error, not a derivation")

    print("\n%s" % ("FAILURES: %d" % fails if fails else "all ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
