# -*- coding: utf-8 -*-
# derive_patterns.py — Ghidra headless postScript.
#
# Semi-automatic pattern maintainer for lumalinux. Run it against the CURRENT
# steamclient.so after a Steam update; it re-locates each hook function and
# prints a fresh byte pattern for src/patterns.hpp, so fixing an update is a
# command instead of a full RE session. Also verifies the runtime anchors the
# package-0 finder depends on (§13).
#
# Strategy per piece:
#   - BuildDep, GMRC, ShaderDepot: anchored on a stable Steam string
#     ("BuildDepotDependency", "ContentServerDirectory.GetManifestRequestCode#1",
#     "shadercachedepot"). Find the string, find the referencing function, emit a
#     fresh prologue pattern (PIC call rel32, the GOT `add reg,imm32`, and any
#     `[picbase+disp32]` global-load offset are wildcarded — those offsets shift
#     per build, RESEARCH §13.10). ShaderDepot is non-critical: a miss only loses
#     the per-game shader skip, installs still work.
#   - DepotKey and Reconcile (NotifyLicensesUpdated): NOT derived here. Both
#     used to have an "indirect" walk in this script (dispatcher -> vcall
#     [+0x18] for DepotKey; type_info xref for Reconcile) and neither ever
#     resolved on a real binary: headless Ghidra does not resolve indirect calls
#     (selftest run #7, 2026-09-14), and NotifyLicensesUpdated posts its
#     callback by NUMBER (0x7d) so nothing references the type_info at all
#     (2026-09-22). CI derives them in Python — tools/derive_depotkey_byname.py
#     (RTTI name -> vtable slot) and tools/derive_reconcile_byanchor.py
#     (callback 125 + this-read + jle) — through the shared
#     tools/derive_from_address.py core. Here they are only VALIDATED: does the
#     current patterns.hpp literal still match uniquely on this binary?
#   - LoadPackage: diagnostic-only since v0.13.1. NOT installed by default; only
#     enabled by LUMA_LOADPKG_DEBUG=1. A broken pattern here doesn't break
#     installs (the package-0 finder injects, not this hook). Output is
#     downgraded to a hint accordingly.
#   - Package-0 finder anchors (§13.5): the finder doesn't use kPatternsHpp
#     constants — it derives addresses at runtime from two anchors. We don't
#     re-derive them (they ARE derived at runtime), but we VERIFY they're still
#     present in this binary so the maintainer knows whether a Steam update
#     also broke the finder path (which would be a code change in
#     package_zero_finder.cpp, not in patterns.hpp).
#       (a) The cache-access idiom:
#           `lea r1,[GOT+X] ; mov r2,[r1] ; mov r3,[r2+0xc58]`,
#           anchored on the stable 0xc58 root-index offset of
#           CPackageInfoCache.
#       (b) The GMRC prologue tail that survives the hook detour:
#           `05 imm32 ; 55 89 E5 57 56 53 81 EC 10 01 00 00 8B 7D 08 8B 4D 20`
#           — the `add eax,imm32` followed by the function's real prologue,
#           the part DeriveGotBase() scans for.
#
# Usage (after importing/analyzing steamclient.so into a Ghidra project):
#   analyzeHeadless <proj> <name> -process steamclient.so \
#       -noanalysis -scriptPath tools -postScript derive_patterns.py
# (or -import steamclient.so for a fresh project; analysis takes ~5-30 min).
import json
from ghidra.util.task import ConsoleTaskMonitor

prog    = currentProgram
fm      = prog.getFunctionManager()
mem     = prog.getMemory()
listing = prog.getListing()
refmgr  = prog.getReferenceManager()
mon     = ConsoleTaskMonitor()

PROLOGUE_BYTES = 28   # how many bytes of prologue to capture

# Optional machine-readable output for CI auto-derivation (watch-steam.yml).
# Pass `--json <path>` as a postScript arg and we additionally dump every hook
# we re-derived to a UNIQUE pattern as {const: {pattern, matches, rva}}. Without
# the arg the script behaves exactly as before (human-readable stdout only), so
# manual Ghidra runs are unaffected. tools/apply_derived_pattern.py consumes it.
_JSON_OUT = None
try:
    _args = list(getScriptArgs())
    if "--json" in _args:
        _i = _args.index("--json")
        if _i + 1 < len(_args):
            _JSON_OUT = _args[_i + 1]
except Exception:
    _JSON_OUT = None

# label (the patterns.hpp constant name) -> {"pattern", "matches", "rva"}.
# Only populated for hooks we re-derived to a single UNIQUE match — anything
# ambiguous or anchor-less is left out so a human keeps the call.
DERIVED = {}

# The CURRENT patterns (what we validate against when a walk fails) are READ
# FROM src/patterns.hpp at run time — never copied here. A copy drifts: this
# script carried a stale kDepotKeyFnPattern for months and, when the vcall walk
# failed, "validated" it UNIQUE at a function that was not DepotKey at all
# (selftest run #7, 2026-09-14: stale copy matched @ RVA 0x189fca0, the real
# accessor is @ 0x11a4500). "keep it" on the wrong function is worse than no
# fallback. Path: next to this script (../src/patterns.hpp), else cwd, else
# $LUMA_PATTERNS_HPP.
import os
import re


def _locate_patterns_hpp():
    cands = []
    if os.environ.get("LUMA_PATTERNS_HPP"):
        cands.append(os.environ["LUMA_PATTERNS_HPP"])
    try:
        here = os.path.dirname(getSourceFile().getAbsolutePath())
        cands.append(os.path.join(here, "..", "src", "patterns.hpp"))
    except Exception:
        pass
    cands.append(os.path.join(os.getcwd(), "src", "patterns.hpp"))
    for c in cands:
        if os.path.isfile(c):
            return c
    return None


def _load_current_patterns():
    path = _locate_patterns_hpp()
    if path is None:
        print("WARNING: src/patterns.hpp not found — current patterns cannot be validated")
        return {}
    txt = open(path).read()
    cur = {}
    for m in re.finditer(r'\b(k\w+Pattern)\s*=\s*"([0-9A-Fa-f? ]+)"', txt):
        cur[m.group(1)] = " ".join(m.group(2).split())
    print("current patterns read from %s (%d constants)" % (path, len(cur)))
    return cur


CURRENT = _load_current_patterns()


def current_pattern(label):
    """The shipped pattern for `label`, or None if patterns.hpp was not found."""
    return CURRENT.get(label)


# String-anchored hooks: (label, anchor_string)
ANCHORED_HOOKS = [
    ("kBuildDepotDependencyPattern", "BuildDepotDependency"),
    ("kGmrcFunctionPattern", "ContentServerDirectory.GetManifestRequestCode#1"),
    # ShaderDepot (v0.14): GetShaderCacheDepot reads the appinfo KeyValues
    # "shadercachedepot" and references that string literal directly inside
    # itself — so it's a clean direct anchor exactly like BuildDep. Its prologue
    # loads the shader-manager global (mov eax,[picbase+0x2b758]); extract_pattern
    # now wildcards that per-build disp32 automatically (RESEARCH §13.10). A miss
    # here does NOT break installs — only the per-game shader skip.
    ("kShaderCacheDepotPattern", "shadercachedepot"),
]

# DepotKey: validated only (derived in Python by RTTI name — see the header).
DEPOTKEY_LABEL = "kDepotKeyFnPattern"

# LoadPackage: diagnostic-only in v0.13.1+ (opt-in via LUMA_LOADPKG_DEBUG=1).
# A broken pattern here does NOT break installs.
LOADPKG_LABEL    = "kLoadPackagePattern"

# NotifyLicensesUpdated (v0.16.15, the no-restart licence reconcile): validated
# only (derived in Python by its callback anchor — see the header).
# Non-load-bearing: a miss only disables no-restart (Add Game falls back to
# needing a Steam restart), never blocks installs.
NOTIFY_LABEL   = "kNotifyLicensesUpdatedPattern"

# Package-0 finder anchors (§13.5)
# One row per KNOWN CPackageInfoCache layout: (name, root-index offset,
# node-array offset). Mirrors kCacheLayouts in src/hooks/package_zero_finder.cpp
# and CACHE_LAYOUTS in tools/check_patterns.py — the four copies change
# together (tools/test_cache_idiom.py pins them). 2026-09-22: the 9cf4720f
# beta moved every field of the object by 0x338.
CACHE_LAYOUTS = [
    ("stable-0xc58", 0xc58, 0xc6c),
    ("beta-0xf90",   0xf90, 0xfa4),
]
GMRC_PROLOGUE_TAIL = "55 89 E5 57 56 53 81 EC 10 01 00 00 8B 7D 08 8B 4D 20"
GMRC_TAIL_WILDCARD = (8, 9)   # the frame-size bytes (`sub esp,imm32`): any value


# ── helpers ───────────────────────────────────────────────────────────────

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
    wildcarding the per-build pieces:
      - the PIC get_pc_thunk `call` rel32,
      - the GOT `add reg, imm32` that follows it,
      - and any `mov/lea r32, [picbase + disp32]` global load, where `picbase`
        is the register the `add` just loaded the GOT into. Those disp32s are
        GOT-relative offsets to globals that SHIFT between Steam builds (the
        package-0 cache global moved 0x3a1bc->0x3967c across builds, §13.5; the
        shader-manager global is the same story, RESEARCH §13.10). Hardcoding
        one makes a pattern miss on the next update for a reason unrelated to
        the function's identity, so we wildcard it. The uniqueness check below
        catches the rare case where this over-generalises."""
    toks = []
    addr = entry
    total = 0
    prev_was_call = False
    picbase_reg = None   # x86 reg number holding the GOT base after the `add`
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
            # the GOT-relative `add reg, imm32` right after get_pc_thunk —
            # wildcard the imm32. Two encodings: 0x05 (add eax,imm32 = 5 bytes)
            # and 0x81 /0 (add r/m32,imm32 = 6 bytes). Record which register now
            # holds the GOT base so we can spot global loads off it below.
            if n == 5 and (raw[0] & 0xFF) == 0x05:
                for i in range(1, 5):
                    wild[i] = True
                picbase_reg = 0          # eax
            elif n == 6 and (raw[0] & 0xFF) == 0x81:
                for i in range(2, 6):
                    wild[i] = True
                picbase_reg = (raw[1] & 0x07)   # rm field of ModR/M = dest reg
        elif mn in ("MOV", "LEA") and picbase_reg is not None and n == 6:
            # mov/lea r32, [picbase + disp32]: opcode 8B (mov r,r/m) or 8D (lea),
            # ModR/M mod=10 (disp32) and rm == picbase_reg (rm=100 would be SIB —
            # skip). The trailing 4 bytes are the GOT-relative disp32 → wildcard.
            op = raw[0] & 0xFF
            modrm = raw[1] & 0xFF
            if op in (0x8B, 0x8D) and (modrm >> 6) == 2 \
               and (modrm & 0x07) == picbase_reg and (modrm & 0x07) != 4:
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


def verify_finder_cache_idiom(root_off):
    """The cache-access idiom anchored on one layout's root offset:
        lea r1, [GOT+X]   = 8D /MODRM(mod=10) disp32      (6 bytes)
        mov r2, [r1]      = 8B /MODRM(mod=00)             (2 bytes)
        mov r3, [r2+root] = 8B /MODRM(mod=10) <root LE>   (6 bytes)

    We anchor on the trailing literal (the root offset as a little-endian
    disp32, e.g. `58 0C 00 00` for 0xc58 — an unambiguous needle for
    findBytes). Called once per CACHE_LAYOUTS row. For each hit we
    walk backwards and verify the rest of the idiom byte-by-byte: 8B at h-2
    and h-4 (the two `mov`s), 8D at h-10 (the lea), then extract the lea's
    disp32 from h-8..h-5 (= X). Doing it this way (instead of asking
    findBytes for a wildcarded 8B?? prefix) sidesteps a Jython quirk where
    short patterns with full-byte wildcards in the middle yield zero hits
    even when the bytes exist."""
    needle = "%02X %02X %02X %02X" % (root_off & 0xFF, (root_off >> 8) & 0xFF,
                                      (root_off >> 16) & 0xFF, (root_off >> 24) & 0xFF)
    hits = pattern_matches(needle)
    matches = []
    def b(addr, off):
        """Read one byte at addr+off, return 0..255 (already promoted)."""
        return mem.getByte(addr.add(off)) & 0xFF
    for h in hits:
        try:
            if h.getOffset() < 10:
                continue
            base = h.subtract(10)  # start of the lea
            # layout: [0]=8D [1]=MR_lea [2..5]=disp32  [6]=8B [7]=MR
            #         [8]=8B [9]=MR [10..13]=58 0C 00 00
            # Same predicates as Hooks::PackageZeroFinder::FindCacheGlobalDisp
            # and check_patterns.py's verify_cache_idiom — mod fields, the rm
            # exclusions that pin the 6+2+6 lengths, and the register chaining.
            # This used to check only the three opcodes, which made it looser
            # than the runtime: it counted matches the Deck rejects. NOT a fixed
            # base register for the lea: the two real sites on bc54101b29 use
            # different ones (esi, eax).
            if b(base, 0) != 0x8D: continue
            m1 = b(base, 1)
            if (m1 & 0xC0) != 0x80 or (m1 & 0x07) == 0x04: continue
            if b(base, 6) != 0x8B: continue
            m2 = b(base, 7)
            if (m2 & 0xC0) != 0x00 or (m2 & 0x07) == 0x04 or (m2 & 0x07) == 0x05: continue
            if b(base, 8) != 0x8B: continue
            m3 = b(base, 9)
            if (m3 & 0xC0) != 0x80 or (m3 & 0x07) == 0x04: continue
            if ((m1 >> 3) & 0x07) != (m2 & 0x07): continue   # reg(lea)  == rm(mov1)
            if ((m2 >> 3) & 0x07) != (m3 & 0x07): continue   # reg(mov1) == rm(mov2)
            # Extract the lea's disp32 (little-endian, signed).
            disp = (b(base, 2)
                    | (b(base, 3) << 8)
                    | (b(base, 4) << 16)
                    | (b(base, 5) << 24))
            if disp >= 0x80000000:
                disp -= 0x100000000
            matches.append((base, disp))
        except Exception:
            continue
    return matches


def verify_gmrc_prologue_tail():
    """The post-detour-survivor tail: the 18 bytes that follow the 5-byte
    `05 imm32` in GMRC's prologue. Even after the GMRC hook overwrites the
    first 5 bytes (`E8 <call thunk>`) with a `jmp` detour, these 18 bytes
    survive — that's what DeriveGotBase() relies on.

    Returns [(addr_of_the_0x05_byte, derived_got)] rather than the raw tail
    hits, because the match count is the wrong axis to judge this on: if the
    same prologue shape occurs in a second function, that function is PIC too
    and its own `add eax,imm32` derives THE SAME GOT. Several sites agreeing is
    harmless; only sites that DISAGREE break DeriveGotBase, which now fails
    closed on them."""
    tail = [int(t, 16) for t in GMRC_PROLOGUE_TAIL.split()]
    head = " ".join("%02X" % b for b in tail[:min(GMRC_TAIL_WILDCARD)])
    out = []
    for h in pattern_matches(head):
        try:
            # The rest of the tail, byte by byte, with the frame-size bytes
            # free — the same comparison as DeriveGotBase's tailMatches() and
            # check_patterns.gmrc_tail_matches. (Anchoring on the fixed head
            # and checking by hand sidesteps the Jython findBytes wildcard
            # quirk noted in verify_finder_cache_idiom.)
            ok = True
            for k in range(len(tail)):
                if k in GMRC_TAIL_WILDCARD:
                    continue
                if (mem.getByte(h.add(k)) & 0xFF) != tail[k]:
                    ok = False
                    break
            if not ok:
                continue
            if h.getOffset() < 5:
                continue
            base = h.subtract(5)                       # the 0x05 of add eax,imm32
            if (mem.getByte(base) & 0xFF) != 0x05:
                continue
            imm = ((mem.getByte(base.add(1)) & 0xFF)
                   | ((mem.getByte(base.add(2)) & 0xFF) << 8)
                   | ((mem.getByte(base.add(3)) & 0xFF) << 16)
                   | ((mem.getByte(base.add(4)) & 0xFF) << 24))
            if imm >= 0x80000000:
                imm -= 0x100000000
            out.append((base, (base.getOffset() + imm) & 0xFFFFFFFF))
        except Exception:
            continue
    return out


# ── main ──────────────────────────────────────────────────────────────────

print("\n================= lumalinux derive_patterns =================")

# 1) String-anchored: BuildDep, GMRC
for label, anchor in ANCHORED_HOOKS:
    print("\n---- %s ----" % label)
    sa = find_string_addrs(anchor)
    if not sa:
        print("  anchor string %r NOT FOUND — Steam removed/renamed it. Re-derive manually." % anchor)
        continue
    fns = funcs_referencing(sa)
    if not fns:
        print("  anchor found but no referencing function resolved (analysis incomplete?).")
        continue
    cands = []
    for f in fns:
        pat = extract_pattern(f.getEntryPoint())
        n = len(pattern_matches(pat))
        cands.append((f, pat, n))
        print("  candidate @ %s : %s   (%s)" %
              (f.getEntryPoint(), pat, "UNIQUE" if n == 1 else ("%d matches" % n)))
    # Auto-derivation is only trustworthy when there's exactly ONE referencing
    # function and its fresh prologue matches UNIQUELY. Anything else (multiple
    # referrers, or a single referrer whose prologue over-generalises) needs a
    # human, so we don't record it for CI.
    uniq = [(f, pat) for (f, pat, n) in cands if n == 1]
    if len(fns) == 1 and len(uniq) == 1:
        f, pat = uniq[0]
        DERIVED[label] = {"pattern": pat, "matches": 1,
                          "rva": "0x%x" % f.getEntryPoint().getOffset()}
        print("  -> use the pattern above for %s" % label)
    elif len(fns) == 1:
        print("  -> single candidate but not UNIQUE; needs manual tightening")
    else:
        print("  -> multiple candidates; pick the UNIQUE one whose prologue matches the hook")

# 2) DepotKey and Reconcile: VALIDATE the shipped literal, do not derive.
#    Derivation is Python-side (see the header); a miss here tells the human
#    which tool to run, instead of pretending a walk that never worked.
def validate_only(label, python_tool, manual_hint):
    print("\n---- %s ----" % label)
    print("  (not derived by Ghidra — CI derives it with tools/%s;" % python_tool)
    print("   here the CURRENT patterns.hpp literal is only validated.)")
    _cur = current_pattern(label)
    hits = pattern_matches(_cur) if _cur else []
    if _cur is None:
        print("  CURRENT pattern unknown (patterns.hpp not found) — nothing to validate.")
    elif len(hits) == 1:
        print("  CURRENT pattern still matches UNIQUELY @ %s — keep it." % hits[0])
    elif len(hits) == 0:
        print("  CURRENT pattern NO LONGER MATCHES — run:")
        print("    python3 tools/%s steamclient.so --derived derived.json" % python_tool)
        print("  %s" % manual_hint)
    else:
        print("  CURRENT pattern matches %d places (ambiguous) — tighten it." % len(hits))


validate_only(DEPOTKEY_LABEL, "derive_depotkey_byname.py",
              "(manual fallback: RESEARCH §12.5 / §15 — CConfigStore vtable slot 6.)")
validate_only(NOTIFY_LABEL, "derive_reconcile_byanchor.py",
              "(manual fallback: the function that `push 0x7d ; push eax ; call`s AND reads a\n"
              "   field of `this` then `jle`; see tools/experiment_reconcile_xref.py.)")

# 3) LoadPackage: diagnostic-only, downgraded output
print("\n---- %s ----" % LOADPKG_LABEL)
print("  (diagnostic-only since v0.13.1: installed only with LUMA_LOADPKG_DEBUG=1.")
print("   a broken pattern here does NOT break installs — the package-0 finder injects.)")
_cur = current_pattern(LOADPKG_LABEL)
hits = pattern_matches(_cur) if _cur else []
if _cur is None:
    print("  CURRENT pattern unknown (patterns.hpp not found) — nothing to validate.")
elif len(hits) == 0:
    print("  CURRENT pattern no longer matches. Diagnostic is unavailable until re-derived.")
    print("  (Optional: locate CPackageInfoCache::LoadPackage in Ghidra, re-extract prologue.)")
elif len(hits) >= 1:
    print("  CURRENT pattern matches %d site(s) — diagnostic still works (runtime picks via" % len(hits))
    print("  LUMA_LOADPKG_IDX). Candidates:")
    for h in hits:
        print("      @ %s" % h)

# 4) Package-0 finder anchors (§13.5): verify, don't re-derive
print("\n---- package-0 finder anchors (§13.5) ----")
print("  The finder derives GOT and cache_global at RUNTIME — it doesn't use")
print("  patterns.hpp. We verify the two anchors it depends on are still")
print("  present in this binary.")

# 4a) Cache-access idiom, one scan per known layout (CACHE_LAYOUTS). Same
#     two-level rule as FindCacheGlobalDisp: exactly one row may have sites,
#     and that row must name exactly one disp32.
print("\n  [a] cache-access idiom (one scan per known layout):")
_per_layout = [(name, root, verify_finder_cache_idiom(root)) for name, root, _n in CACHE_LAYOUTS]
for name, root, sites in _per_layout:
    print("      layout %-14s root 0x%x: %d site(s)" % (name, root, len(sites)))
_hit = [(name, sites) for name, _r, sites in _per_layout if sites]
idiom = _hit[0][1] if len(_hit) == 1 else []
if not _hit:
    print("      NOT FOUND — no known layout's root offset is read with the idiom;")
    print("      CPackageInfoCache changed again.")
    print("      ACTION: run tools/experiment_cache_idiom_free.py on this binary to")
    print("              find the new root (docs/maintenance.md §C), then ADD a row")
    print("              to kCacheLayouts in src/hooks/package_zero_finder.cpp and")
    print("              its three mirrors (check_patterns, derive_patterns,")
    print("              experiment_cache_idiom). Never replace the old row.")
elif len(_hit) > 1:
    print("      AMBIGUOUS-LAYOUT — %d layouts have sites: %s"
          % (len(_hit), ", ".join(n for n, _ in _hit)))
    print("      NOT OK — FindCacheGlobalDisp refuses when more than one row")
    print("      resolves; the finder will not inject on this binary.")
else:
    print("      layout %s resolved:" % _hit[0][0])
    for base, disp in idiom:
        print("      PRESENT @ %s   disp32=0x%x  (cache_global = GOT + disp32)" %
              (base, disp & 0xFFFFFFFF))
    _disps = sorted(set(d & 0xFFFFFFFF for _, d in idiom))
    if len(_disps) == 1:
        print("      UNIQUE (%d site(s), disp32=0x%x)" % (len(idiom), _disps[0]))
        print("      OK — finder's cache locator will work on this binary.")
    else:
        # Mirrors what [b] below already does for the GMRC tail. Since
        # FindCacheGlobalDisp refuses to guess between disagreeing sites, this
        # is NOT an "OK": on this binary the finder resolves nothing and
        # injects nothing.
        print("      AMBIGUOUS — %d site(s) name %d different disp32: %s"
              % (len(idiom), len(_disps), " ".join("0x%x" % d for d in _disps)))
        print("      NOT OK — FindCacheGlobalDisp fails closed on disagreement,")
        print("      so the finder will not resolve or inject on this binary.")
        print("      ACTION: the idiom needs a more specific anchor than this row's")
        print("              root offset, which is shared with whatever class matched too.")

# 4b) GMRC prologue tail
print("\n  [b] GMRC prologue tail (survives the hook detour):")
tail = verify_gmrc_prologue_tail()
if not tail:
    print("      NOT FOUND — Steam may have reorganised GMRC's prologue (the")
    print("      frame-size bytes are already wildcarded, so this is more than")
    print("      a locals-only change).")
    print("      ACTION: update the `tail` byte array in DeriveGotBase() in")
    print("              src/hooks/package_zero_finder.cpp to the new sequence,")
    print("              and its mirrors (check_patterns, derive_patterns,")
    print("              experiment_cache_idiom).")
else:
    _gots = sorted(set(g for _, g in tail))
    for base, got in tail:
        print("      PRESENT @ %s   -> GOT 0x%x" % (base, got))
    if len(_gots) == 1:
        print("      UNIQUE (%d site(s), got=0x%x)" % (len(tail), _gots[0]))
        print("      OK — finder's GOT derivation will work on this binary.")
    else:
        print("      AMBIGUOUS — %d site(s) derive %d different GOT base(s): %s"
              % (len(tail), len(_gots), " ".join("0x%x" % g for g in _gots)))
        print("      NOT OK — DeriveGotBase fails closed on disagreement, so the")
        print("      finder will not resolve or inject on this binary.")
        print("      ACTION: DeriveGotBase needs a more specific anchor than the")
        print("              current tail bytes.")

# 5) Machine-readable dump for CI (watch-steam.yml auto-derivation)
if _JSON_OUT:
    try:
        f = open(_JSON_OUT, "w")
        try:
            json.dump(DERIVED, f, indent=2, sort_keys=True)
        finally:
            f.close()
        print("\nWrote %d re-derived hook(s) to %s: %s"
              % (len(DERIVED), _JSON_OUT, ", ".join(sorted(DERIVED)) or "(none)"))
    except Exception as e:
        print("\nWARNING: could not write --json %s: %s" % (_JSON_OUT, e))

print("\n================= done. Paste UNIQUE patterns into src/patterns.hpp =================")
print("Reminders:")
print("  * Patterns above go in src/patterns.hpp (then rebuild + release).")
print("  * Finder anchor results are diagnostics for src/hooks/package_zero_finder.cpp;")
print("    if either says NOT FOUND, the finder needs a code fix (not a pattern bump).")
