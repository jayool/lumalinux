#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Unit test for derive_depotkey_byname.py's pure parts — the x86-32 length
# decoder and the prologue wildcarder — with no steamclient.so. The claim under
# test: on raw bytes, wildcard_prologue reproduces EXACTLY what Ghidra's
# extract_pattern produced for every auto-derivable shipped pattern, whatever
# the per-build bytes (rel32 / imm32 / disp32) happen to be. NotifyLicenses is
# excluded on purpose: its shipped mask carries hand-made wildcards (frame
# size, member offset) that neither extract_pattern nor this tool emits.
#
#   python3 tools/test_derive_depotkey_byname.py     # from repo root
import os
import random
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from derive_depotkey_byname import (Undecodable, insn_len,  # noqa: E402
                                    wildcard_prologue)

HPP = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "patterns.hpp")
AUTO_DERIVED = ["kDepotKeyFnPattern", "kBuildDepotDependencyPattern",
                "kGmrcFunctionPattern", "kShaderCacheDepotPattern",
                "kLoadPackagePattern"]
fails = 0


def check(cond, msg):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        fails += 1


def shipped(const):
    txt = open(HPP).read()
    m = re.search(r'\b' + re.escape(const) + r'\s*=\s*"([0-9A-Fa-f? ]+)"', txt)
    return m.group(1).split() if m else None


random.seed(20260914)
for const in AUTO_DERIVED:
    toks = shipped(const)
    check(toks is not None, "%s present in patterns.hpp" % const)
    if not toks:
        continue
    bad = None
    for trial in range(300):
        raw = bytes(random.randrange(256) if t == "??" else int(t, 16) for t in toks)
        raw += bytes(random.randrange(256) for _ in range(64))
        try:
            out = wildcard_prologue(raw, len(toks))
        except Undecodable as e:
            bad = "undecodable: %s" % e
            break
        if out[:len(toks)] != toks:
            bad = "trial %d: got %s" % (trial, " ".join(out[:len(toks)]))
            break
    check(bad is None, "%s reproduced byte-for-byte over 300 random fills%s"
          % (const, "" if bad is None else " — " + bad))

# Length decoder spot checks (bytes -> expected length).
cases = [
    ("55", 1), ("89 E5", 2), ("E8 11 22 33 44", 5), ("81 C3 11 22 33 44", 6),
    ("05 11 22 33 44", 5), ("83 EC 24", 3), ("81 EC 2C 02 00 00", 6),
    ("8B 44 24 44", 4), ("89 85 1C FE FF FF", 6), ("8B 83 11 22 33 44", 6),
    ("8B 04 85 11 22 33 44", 7), ("8B 05 11 22 33 44", 6), ("8D 74 26 00", 4),
    ("75 0D", 2), ("0F 84 11 22 33 44", 6), ("0F B6 45 08", 4),
    ("66 90", 2), ("66 0F 1F 44 00 00", 6), ("F7 45 08 11 22 33 44", 7),
    ("F7 D8", 2), ("C7 45 F0 11 22 33 44", 7), ("FF 75 08", 3), ("6A 00", 2),
    ("68 11 22 33 44", 5), ("B8 11 22 33 44", 5), ("C3", 1), ("EB 05", 2),
    ("31 C0", 2), ("85 C0", 2), ("F3 0F 10 45 F8", 5),
]
for hexs, want in cases:
    b = bytes(int(x, 16) for x in hexs.split())
    try:
        got = insn_len(b, 0)
    except Undecodable as e:
        got = "undecodable(%s)" % e
    check(got == want, "insn_len(%s) == %s (got %s)" % (hexs, want, got))

# Refusal, not a guess, on opcodes outside the table.
for hexs in ["27", "D6", "0F 0D 00"]:
    b = bytes(int(x, 16) for x in hexs.split())
    try:
        insn_len(b, 0)
        check(False, "insn_len(%s) refuses" % hexs)
    except Undecodable:
        check(True, "insn_len(%s) refuses" % hexs)

# The wildcarder never adds a wildcard where extract_pattern would not: an
# `add ebx, imm32` that does NOT follow a call keeps its immediate.
raw = bytes.fromhex("55 81 C3 11 22 33 44 83 EC 24".replace(" ", "")) + bytes(32)
out = wildcard_prologue(raw, 10)
check(out[:7] == ["55", "81", "C3", "11", "22", "33", "44"],
      "add imm32 without a preceding call is kept literal")

# derive() end to end on synthetic segments: by-name is stubbed to point at a
# function whose first 28 bytes are shared with a decoy, so the tool has to
# GROW the pattern until it is unique — and must land on the by-name address.
import derive_depotkey_byname as ddb  # noqa: E402

VADDR = 0x1000
toks = shipped("kDepotKeyFnPattern")
random.seed(1)
fill = lambda: bytes(random.randrange(256) if t == "??" else int(t, 16) for t in toks)  # noqa: E731
real = fill()
decoy = bytearray(fill())
decoy[30] ^= 0xFF                          # differs only past the 28-byte prologue
buf = bytearray(0x800)
buf[0x100:0x100 + len(real)] = real
buf[0x400:0x400 + len(decoy)] = bytes(decoy)
ddb.load_exec_segments = lambda path: [(VADDR, bytes(buf))]
ddb.rtti_derive_slot_byname = lambda *a, **k: ({"status": "UNIQUE", "slot": 6,
                                                "rva": "0x%x" % (VADDR + 0x100)}, "ok")
entry, note = ddb.derive("synthetic.so")
check(entry is not None, "derive() succeeds on synthetic segments (%s)" % note)
if entry:
    check(entry["rva"] == "0x%x" % (VADDR + 0x100), "derive() lands on the by-name address")
    n = len(entry["pattern"].split())
    check(n > 28, "pattern grew past the shared 28-byte prologue (%d bytes)" % n)
    check(entry["matches"] == 1 and entry["source"] == "rtti-byname", "entry shaped for derived.json")
# and with no decoy it must not grow beyond the first whole-instruction cut
buf[0x400:0x400 + len(decoy)] = bytes(len(decoy))
ddb.load_exec_segments = lambda path: [(VADDR, bytes(buf))]
entry, note = ddb.derive("synthetic.so")
cut = len(wildcard_prologue(real, 28))          # first whole-instruction cut >= 28
check(entry is not None and len(entry["pattern"].split()) == cut,
      "no decoy: pattern stops at the first whole-instruction cut >= 28 (%d bytes)" % cut)
ddb.rtti_derive_slot_byname = lambda *a, **k: ({"status": "NOT_RESOLVED"}, "no slot")
entry, note = ddb.derive("synthetic.so")
check(entry is None and note.startswith("by-name"), "by-name miss is reported as such")

print("\n%d failure(s)" % fails)
sys.exit(1 if fails else 0)
