#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Unit test for derive_reconcile_byanchor.py (the locator) and the masking
# options of derive_from_address.py it relies on. Synthetic bytes only — no
# steamclient.so, no network — so it runs in build.yml with the other tests.
#
#   python3 tools/test_derive_reconcile_byanchor.py     # from the repo root
#
# Pins three things:
#   1. the locator's rule: of the functions that post callback 125, keep the
#      ones that read a field of `this` and bail on <= 0; exactly ONE or refuse
#      (NOT_FOUND / AMBIGUOUS). The member offset must NOT be a criterion.
#   2. derive_at's mask_frame / mask_disp32: the frame size and every
#      [reg+disp32] displacement come out as wildcards, so a layout drift
#      (0x1b14 -> 0x1b10 on the beta) never lands in patterns.hpp as a literal.
#   3. end to end on a synthetic .text: the derived pattern matches the real
#      function only, and carries at least every wildcard the shipped
#      kNotifyLicensesUpdatedPattern carries.
import os
import random
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import derive_reconcile_byanchor as drb  # noqa: E402
from check_patterns import scan_rvas  # noqa: E402
from derive_from_address import derive_at, wildcard_prologue  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VADDR = 0x1000
fails = 0


def check(cond, msg):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        fails += 1


def shipped(const):
    hpp = open(os.path.join(REPO, "src", "patterns.hpp")).read()
    m = re.search(r'\b' + const + r'\s*=\s*"([0-9A-Fa-f? ]+)"', hpp)
    return m.group(1).split()


# ── byte builders ────────────────────────────────────────────────────────────

def reconcile_like(member_disp=0x1b14, frame=0x1bc, reg=7):
    """A function shaped like CUser::NotifyLicensesUpdated: PIC prologue,
    `sub esp,frame`, mov eax,[ebp+8] ; mov reg,[eax+member] ; spill ;
    test reg,reg ; jle ; … ; push 0x7d ; push eax ; call ; ret."""
    r = reg & 7
    body = (bytes.fromhex("5589E5575653") +                 # push ebp; mov ebp,esp; push edi/esi/ebx
            b"\xE8" + b"\x00\x00\x00\x00" +                 # call get_pc_thunk.bx
            b"\x81\xC3" + struct.pack("<I", 0x1ff000) +    # add ebx, imm32
            b"\x81\xEC" + struct.pack("<I", frame) +       # sub esp, frame
            b"\x8B\x45\x08" +                               # mov eax,[ebp+8]
            bytes([0x8B, 0x80 | (r << 3)]) + struct.pack("<I", member_disp) +  # mov reg,[eax+member]
            b"\x89\x9D" + struct.pack("<I", 0xfffffe54) +  # mov [ebp-0x1ac], ebx (spill)
            bytes([0x85, 0xC0 | (r << 3) | r]) +            # test reg,reg
            b"\x0F\x8E" + struct.pack("<I", 0x40) +         # jle rel32
            b"\x90" * 8 +
            b"\x6A\x7D\x50\xE8" + b"\x00\x00\x00\x00" +     # push 0x7d; push eax; call
            b"\xC3")
    return body


def poster_only():
    """Posts callback 125 but does not read `this` first (like the other ten)."""
    return (bytes.fromhex("5557565383EC2C") + b"\x90" * 6 +
            b"\x6A\x7D\x50\xE8" + b"\x00\x00\x00\x00" + b"\xC3")


def this_read_no_bail():
    """Reads a field of `this` but never tests it (c2 without c3)."""
    return (bytes.fromhex("5589E5575653") + b"\xE8\x00\x00\x00\x00" +
            b"\x8B\x45\x08\x8B\xB8" + struct.pack("<I", 0x80) + b"\x90" * 8 +
            b"\x6A\x7D\x50\xE8" + b"\x00\x00\x00\x00" + b"\xC3")


def layout(*fns):
    """Lay functions out 0x100 apart; returns (text, starts, read_va)."""
    buf = bytearray(0x100 * (len(fns) + 1))
    starts = []
    for i, f in enumerate(fns):
        off = 0x100 * (i + 1) - 0x80
        buf[off:off + len(f)] = f
        starts.append(VADDR + off)
    text = bytes(buf)

    def read_va(va, n):
        o = va - VADDR
        return text[o:o + n] if 0 <= o < len(text) else b""
    return text, sorted(starts), read_va


def main():
    # ── 1. the locator's rule ───────────────────────────────────────────────
    CASES = [
        ("one real, ten posters",
         [reconcile_like()] + [poster_only()] * 10, "UNIQUE"),
        ("beta layout (member 0x1b10, frame 0x1cc, other reg)",
         [poster_only(), reconcile_like(0x1b10, 0x1cc, reg=6), poster_only()], "UNIQUE"),
        ("member offset far away is still accepted (not a criterion)",
         [reconcile_like(0x80)], "UNIQUE"),
        ("two real -> refuse",
         [reconcile_like(), poster_only(), reconcile_like(0x1b10)], "AMBIGUOUS"),
        ("only posters -> refuse",
         [poster_only()] * 3, "NOT_FOUND"),
        ("this-read without the bail (c2, no c3) -> refuse",
         [this_read_no_bail(), poster_only()], "NOT_FOUND"),
        ("nothing posts 125",
         [bytes.fromhex("5589E5C3")], "NOT_FOUND"),
    ]
    for name, fns, want in CASES:
        text, starts, read_va = layout(*fns)
        status, rva, info = drb.locate(text, VADDR, starts, read_va)
        check(status == want, "%-56s -> %s %s" % (name, status, info.get("kept")))
        if want == "UNIQUE":
            real = [VADDR + 0x100 * (i + 1) - 0x80 for i, f in enumerate(fns)
                    if f[:6] == bytes.fromhex("5589E5575653") and b"\x0F\x8E" in f]
            check(rva == real[0], "%-56s    lands on the real function" % "")

    # a callback site with no function table entry before it is ignored
    text, starts, read_va = layout(reconcile_like())
    status, _rva, _info = drb.locate(text, VADDR, [], read_va)
    check(status == "NOT_FOUND", "no .eh_frame_hdr entries -> NOT_FOUND, not a crash")

    # ── 2. the masking options ──────────────────────────────────────────────
    f = reconcile_like()
    toks = wildcard_prologue(f, 40, mask_frame=True, mask_disp32=True)
    hexs = " ".join(toks)
    check(toks[7:11] == ["??"] * 4, "call rel32 masked")
    check(toks[13:17] == ["??"] * 4, "PIC add imm32 masked")
    check(toks[19:23] == ["??"] * 4, "sub esp,imm32 masked with mask_frame")
    check(toks[27:32] == ["??"] * 5, "member load: register ModR/M and disp32 masked: %s" % hexs)
    check(toks[34:38] == ["??"] * 4, "spill disp32 masked with mask_disp32")
    check(toks[38] == "85" and toks[39] == "??", "test r,r: register masked")
    toks_off = wildcard_prologue(f, 40)
    check(toks_off[19:23] != ["??"] * 4 and toks_off[28:32] != ["??"] * 4,
          "without the options the frame and the member stay literal (DepotKey behaviour)")

    # ── 3. end to end: derived pattern matches the real function only ───────
    random.seed(7)
    text, starts, read_va = layout(reconcile_like(), poster_only(), reconcile_like(0x1b10, 0x1cc, 6))
    # make the third one a decoy that differs only in layout numbers -> ambiguous
    status, rva, info = drb.locate(text, VADDR, starts, read_va)
    check(status == "AMBIGUOUS", "two layout-variants of the same function -> AMBIGUOUS (fail closed)")
    text, starts, read_va = layout(reconcile_like(), poster_only(), this_read_no_bail())
    status, rva, info = drb.locate(text, VADDR, starts, read_va)
    entry, note = derive_at("synthetic.so", rva, "callback-anchor", 28, 96,
                            mask_frame=True, mask_disp32=True, log=lambda *_: None,
                            segments=[(VADDR, text)])
    check(entry is not None, "derive_at on the located function (%s)" % note)
    if entry:
        hits = scan_rvas([(VADDR, text)], entry["pattern"])
        check(hits == [rva], "derived pattern matches exactly the real function")
        # every wildcard the shipped pattern carries is also a wildcard here
        ship = shipped("kNotifyLicensesUpdatedPattern")
        got = entry["pattern"].split()
        n = min(len(ship), len(got))
        missing = [i for i in range(n) if ship[i] == "??" and got[i] != "??"]
        check(not missing, "derived pattern is at least as masked as the shipped one (%s)" % missing)
        # the member offset must not be in the pattern as a literal
        check("14 1B 00 00" not in entry["pattern"], "member offset 0x1b14 is not a literal in the pattern")

    print("\n%s" % ("FAILURES: %d" % fails if fails else "all ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
