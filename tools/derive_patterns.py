# -*- coding: utf-8 -*-
# derive_patterns.py — Ghidra headless postScript.
#
# Semi-automatic pattern maintainer for lumalinux. Run it against the CURRENT
# steamclient.so after a Steam update; it re-locates each hook function and
# prints a fresh byte pattern for src/patterns.hpp, so fixing an update is a
# command instead of a full RE session.
#
# Strategy per function:
#   - If it references a stable anchor STRING (BuildDep, GMRC): find the string,
#     find the function(s) referencing it, and emit a freshly-extracted prologue
#     pattern (PIC call rel32 + the GOT `add reg,imm32` are wildcarded).
#   - If it has NO string anchor (DepotKey, LoadPackage are leaf-ish, log nothing):
#     re-validate the CURRENT pattern against this binary. If it still matches
#     uniquely, keep it; otherwise flag it for manual re-derivation (Ghidra GUI:
#     locate the function, copy its prologue, wildcard the PIC bytes).
#
# Usage (after importing/analyzing steamclient.so into a Ghidra project):
#   analyzeHeadless <proj> <name> -process steamclient.so \
#       -noanalysis -scriptPath tools -postScript derive_patterns.py
# (or -import steamclient.so for a fresh project; analysis takes ~5-15 min).
from ghidra.util.task import ConsoleTaskMonitor

prog    = currentProgram
fm      = prog.getFunctionManager()
mem     = prog.getMemory()
listing = prog.getListing()
refmgr  = prog.getReferenceManager()
mon     = ConsoleTaskMonitor()

PROLOGUE_BYTES = 28   # how many bytes of prologue to capture

# (label, anchor_string_or_None, current_pattern_for_fallback_validation, multi_ok)
#   multi_ok=True  -> a multi-match is EXPECTED and fine (the runtime enumerates
#                     candidates and picks one by index, e.g. LUMA_LOADPKG_IDX).
#   multi_ok=False -> the pattern must resolve to exactly one site.
HOOKS = [
    ("kDepotKeyFnPattern", None,
     "55 57 56 53 E8 ?? ?? ?? ?? 81 C3 ?? ?? ?? ?? 83 EC 20 8B 74 24 34 8B 7C 24 3C 8B 6C 24 40",
     False),
    ("kBuildDepotDependencyPattern", "BuildDepotDependency",
     "55 89 E5 57 56 E8 ?? ?? ?? ?? 81 C6 ?? ?? ?? ?? 53 81 EC 2C 02 00 00 8B 45 08 89 85",
     False),
    ("kLoadPackagePattern", None,
     "55 89 E5 57 E8 ?? ?? ?? ?? 81 C7 ?? ?? ?? ?? 56 53 81 EC 1C 01 00 00",
     True),
    ("kGmrcFunctionPattern", "ContentServerDirectory.GetManifestRequestCode#1",
     "E8 ?? ?? ?? ?? 05 ?? ?? ?? ?? 55 89 E5 57 56 53 81 EC 10 01 00 00 8B 7D 08 8B 4D 20",
     False),
]


def find_string_addrs(s):
    out = []
    b = bytes(bytearray(s, "ascii"))
    a = prog.getMinAddress()
    while True:
        a = mem.findBytes(a, b, None, True, mon)
        if a is None:
            break
        out.append(a)
        a = a.add(1)
    return out


def funcs_referencing(straddrs):
    fns = []
    seen = set()
    for sa in straddrs:
        for r in refmgr.getReferencesTo(sa):
            f = fm.getFunctionContaining(r.getFromAddress())
            if f and f.getEntryPoint().getOffset() not in seen:
                seen.add(f.getEntryPoint().getOffset())
                fns.append(f)
    return fns


def extract_pattern(entry):
    """Disassemble PROLOGUE_BYTES of the function and build a mask pattern,
    wildcarding the PIC get_pc_thunk call rel32 and the GOT `add reg,imm32`."""
    toks = []
    addr = entry
    total = 0
    prev_was_call = False
    while total < PROLOGUE_BYTES:
        ins = listing.getInstructionAt(addr)
        if ins is None:
            break
        raw = ins.getBytes()             # signed java bytes
        n = len(raw)
        mn = ins.getMnemonicString().upper()
        wild = [False] * n
        if mn == "CALL" and n == 5:
            for i in range(1, 5):        # wildcard rel32
                wild[i] = True
        elif mn == "ADD" and prev_was_call:
            # the GOT-relative `add reg, imm32` right after get_pc_thunk — wildcard
            # the imm32. Two encodings: 0x05 (add eax,imm32 = 5 bytes) and
            # 0x81 /0 (add r/m32,imm32 = 6 bytes).
            if n == 5 and (raw[0] & 0xFF) == 0x05:
                for i in range(1, 5):
                    wild[i] = True
            elif n == 6 and (raw[0] & 0xFF) == 0x81:
                for i in range(2, 6):
                    wild[i] = True
        for i in range(n):
            toks.append("??" if wild[i] else ("%02X" % (raw[i] & 0xFF)))
        prev_was_call = (mn == "CALL")
        total += n
        addr = addr.add(n)
    return " ".join(toks)


def pattern_matches(patstr):
    bs = bytearray(); mk = bytearray()
    for t in patstr.split():
        if t.startswith("?"):
            bs.append(0); mk.append(0)
        else:
            bs.append(int(t, 16)); mk.append(0xFF)
    hits = []
    a = prog.getMinAddress()
    while True:
        a = mem.findBytes(a, bytes(bs), bytes(mk), True, mon)
        if a is None:
            break
        hits.append(a); a = a.add(1)
    return hits


print("\n================= lumalinux derive_patterns =================")
for label, anchor, cur, multi_ok in HOOKS:
    print("\n---- %s ----" % label)
    if anchor:
        sa = find_string_addrs(anchor)
        if not sa:
            print("  anchor string %r NOT FOUND — Steam removed/renamed it. Re-derive manually." % anchor)
            continue
        fns = funcs_referencing(sa)
        if not fns:
            print("  anchor found but no referencing function resolved (analysis incomplete?).")
            continue
        for f in fns:
            pat = extract_pattern(f.getEntryPoint())
            n = len(pattern_matches(pat))
            print("  candidate @ %s : %s   (%s)" %
                  (f.getEntryPoint(), pat, "UNIQUE" if n == 1 else ("%d matches" % n)))
        if len(fns) == 1:
            print("  -> use the pattern above for %s" % label)
        else:
            print("  -> multiple candidates; pick the UNIQUE one whose prologue matches the hook")
    else:
        hits = pattern_matches(cur)
        if len(hits) == 0:
            print("  no string anchor and CURRENT pattern NO LONGER MATCHES — re-derive manually:")
            print("    open steamclient.so in Ghidra, locate the function (see docs/RESEARCH.md §4),")
            print("    copy its first ~28 prologue bytes, wildcard the get_pc_thunk call rel32 and")
            print("    the following `add reg,imm32` GOT offset.")
        elif multi_ok:
            # multi-match is by design: runtime enumerates and picks by index.
            print("  no string anchor; CURRENT pattern matches %d site(s) — OK (runtime picks by"
                  " index, e.g. LUMA_LOADPKG_IDX). Candidates:" % len(hits))
            for h in hits:
                print("      @ %s" % h)
        elif len(hits) == 1:
            print("  no string anchor; CURRENT pattern still matches UNIQUELY @ %s — keep it." % hits[0])
        else:
            print("  no string anchor; CURRENT pattern matches %d places (ambiguous) — tighten it." % len(hits))

print("\n================= done. Paste UNIQUE patterns into src/patterns.hpp =================")
