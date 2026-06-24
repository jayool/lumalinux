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
#   - DepotKey: NO usable in-function anchor. BUT there's an indirect anchor:
#     the dispatcher (outer LoadDepotDecryptionKey) references the KeyValues
#     path "Software\Valve\Steam\Depots\". From the dispatcher we can follow a
#     virtual call (this->vtable[+0x18]) to the inner accessor we actually hook
#     (RESEARCH §12.5). Tries that automatically; falls back to validating the
#     current pattern if the vcall walk fails.
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
from ghidra.util.task import ConsoleTaskMonitor

prog    = currentProgram
fm      = prog.getFunctionManager()
mem     = prog.getMemory()
listing = prog.getListing()
refmgr  = prog.getReferenceManager()
mon     = ConsoleTaskMonitor()

PROLOGUE_BYTES = 28   # how many bytes of prologue to capture

# String-anchored hooks: (label, anchor_string, current_pattern_for_validation)
ANCHORED_HOOKS = [
    ("kBuildDepotDependencyPattern", "BuildDepotDependency",
     "55 89 E5 57 56 E8 ?? ?? ?? ?? 81 C6 ?? ?? ?? ?? 53 81 EC 2C 02 00 00 8B 45 08 89 85"),
    ("kGmrcFunctionPattern", "ContentServerDirectory.GetManifestRequestCode#1",
     "E8 ?? ?? ?? ?? 05 ?? ?? ?? ?? 55 89 E5 57 56 53 81 EC 10 01 00 00 8B 7D 08 8B 4D 20"),
    # ShaderDepot (v0.14): GetShaderCacheDepot reads the appinfo KeyValues
    # "shadercachedepot" and references that string literal directly inside
    # itself — so it's a clean direct anchor exactly like BuildDep. Its prologue
    # loads the shader-manager global (mov eax,[picbase+0x2b758]); extract_pattern
    # now wildcards that per-build disp32 automatically (RESEARCH §13.10). A miss
    # here does NOT break installs — only the per-game shader skip.
    ("kShaderCacheDepotPattern", "shadercachedepot",
     "57 56 53 E8 ?? ?? ?? ?? 81 C3 ?? ?? ?? ?? 8B 83 ?? ?? ?? ?? 8B 40 44 85 C0 75 0D 5B 31 C0"),
]

# DepotKey: indirect-anchored. The dispatcher constructs / refs the KeyValues
# path; we follow the vcall to reach the inner accessor we hook.
DEPOTKEY_LABEL = "kDepotKeyFnPattern"
DEPOTKEY_CURRENT = "55 57 56 53 E8 ?? ?? ?? ?? 81 C3 ?? ?? ?? ?? 83 EC 20 8B 74 24 34 8B 7C 24 3C 8B 6C 24 40"
DEPOTKEY_ANCHOR  = "Software\\Valve\\Steam\\Depots\\"
DEPOTKEY_VTABLE_OFFSET = 0x18  # see RESEARCH §12.5: fn = *(vtable + 0x18)

# LoadPackage: diagnostic-only in v0.13.1+ (opt-in via LUMA_LOADPKG_DEBUG=1).
# A broken pattern here does NOT break installs.
LOADPKG_LABEL    = "kLoadPackagePattern"
LOADPKG_CURRENT  = "55 89 E5 57 E8 ?? ?? ?? ?? 81 C7 ?? ?? ?? ?? 56 53 81 EC 1C 01 00 00"

# Package-0 finder anchors (§13.5)
CACHE_ROOT_OFFSET = 0xc58
GMRC_PROLOGUE_TAIL = "55 89 E5 57 56 53 81 EC 10 01 00 00 8B 7D 08 8B 4D 20"


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


def try_derive_depotkey():
    """Follow the dispatcher → vtable[+0x18] indirection to reach the inner
    accessor we hook (RESEARCH §12.5). Returns (label, addr, pattern_str) or
    None if any step fails."""
    sa = find_string_addrs(DEPOTKEY_ANCHOR)
    if not sa:
        return None, "anchor string %r not found in this binary" % DEPOTKEY_ANCHOR
    dispatchers = funcs_referencing(sa)
    if not dispatchers:
        return None, "anchor found at %d site(s) but no enclosing function resolved" % len(sa)
    # The dispatcher we want is the one that performs the indirect call into
    # vtable[+0x18]. Walk each candidate's instructions looking for a CALL
    # whose target is `[reg + 0x18]` (8B/FF mod=01 disp8=0x18). When we find
    # one and its callee resolves to a concrete function, that's the inner
    # accessor we hook.
    for disp in dispatchers:
        body = disp.getBody()
        ins = listing.getInstructions(body, True)
        for i in ins:
            if i.getMnemonicString().upper() != "CALL":
                continue
            # CALL [reg + 0x18] : opcode FF, ModR/M mod=01 reg=010 (/2)
            raw = i.getBytes()
            if len(raw) < 3:
                continue
            if (raw[0] & 0xFF) != 0xFF:
                continue
            modrm = raw[1] & 0xFF
            # /2 = call near indirect, mod=01 (disp8), check disp8 == 0x18
            if ((modrm >> 3) & 7) != 2:
                continue
            if (modrm >> 6) != 1:
                continue
            if (raw[2] & 0xFF) != DEPOTKEY_VTABLE_OFFSET:
                continue
            # This CALL targets [reg + 0x18]. Ask Ghidra for its resolved
            # references (any vtable Ghidra recognised will surface here).
            for ref in i.getReferencesFromInstruction(0) if hasattr(i, "getReferencesFromInstruction") else []:
                pass  # not portable; rely on FlowReferences below
            for ref in i.getReferencesFrom():
                ra = ref.getToAddress()
                if ra is None:
                    continue
                fn = fm.getFunctionAt(ra)
                if fn is None:
                    fn = fm.getFunctionContaining(ra)
                if fn is None:
                    continue
                # Got the inner accessor candidate.
                pat = extract_pattern(fn.getEntryPoint())
                hits = pattern_matches(pat)
                return (fn.getEntryPoint(), pat, len(hits)), None
    return None, "found %d dispatcher candidate(s) but none had a resolvable CALL [reg+0x18]" % len(dispatchers)


def verify_finder_cache_idiom():
    """The cache-access idiom anchored on 0xc58:
        lea r1, [GOT+X]   = 8D /MODRM(mod=10) disp32      (6 bytes)
        mov r2, [r1]      = 8B /MODRM(mod=00)             (2 bytes)
        mov r3, [r2+0xc58]= 8B /MODRM(mod=10) 58 0C 00 00 (6 bytes)

    We anchor on the trailing literal `58 0C 00 00` (the 0xc58 disp32,
    little-endian — easy unambiguous needle for findBytes). For each hit we
    walk backwards and verify the rest of the idiom byte-by-byte: 8B at h-2
    and h-4 (the two `mov`s), 8D at h-10 (the lea), then extract the lea's
    disp32 from h-8..h-5 (= X). Doing it this way (instead of asking
    findBytes for a wildcarded 8B?? prefix) sidesteps a Jython quirk where
    short patterns with full-byte wildcards in the middle yield zero hits
    even when the bytes exist."""
    needle = "58 0C 00 00"
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
            if b(base, 0)  != 0x8D: continue
            if b(base, 6)  != 0x8B: continue
            if b(base, 8)  != 0x8B: continue
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
    survive — that's what DeriveGotBase() relies on."""
    hits = pattern_matches(GMRC_PROLOGUE_TAIL)
    return hits


# ── main ──────────────────────────────────────────────────────────────────

print("\n================= lumalinux derive_patterns =================")

# 1) String-anchored: BuildDep, GMRC
for label, anchor, cur in ANCHORED_HOOKS:
    print("\n---- %s ----" % label)
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

# 2) DepotKey: dispatcher → vcall → inner accessor (RESEARCH §12.5)
print("\n---- %s ----" % DEPOTKEY_LABEL)
result, errmsg = try_derive_depotkey()
if result is not None:
    addr, pat, n = result
    tag = "UNIQUE" if n == 1 else ("%d matches" % n)
    print("  vcall-derived @ %s : %s   (%s)" % (addr, pat, tag))
    if n == 1:
        print("  -> derived via dispatcher vcall — use the pattern above")
    else:
        print("  -> derived but not UNIQUE; double-check or tighten manually")
else:
    print("  vcall derivation failed: %s" % errmsg)
    hits = pattern_matches(DEPOTKEY_CURRENT)
    if len(hits) == 1:
        print("  CURRENT pattern still matches UNIQUELY @ %s — keep it." % hits[0])
    elif len(hits) == 0:
        print("  CURRENT pattern NO LONGER MATCHES — re-derive manually:")
        print("    open steamclient.so in Ghidra, locate the function (RESEARCH §12.5),")
        print("    copy its first ~28 prologue bytes, wildcard the get_pc_thunk call rel32")
        print("    and the following `add reg,imm32`.")
    else:
        print("  CURRENT pattern matches %d places (ambiguous) — tighten it." % len(hits))

# 3) LoadPackage: diagnostic-only, downgraded output
print("\n---- %s ----" % LOADPKG_LABEL)
print("  (diagnostic-only since v0.13.1: installed only with LUMA_LOADPKG_DEBUG=1.")
print("   a broken pattern here does NOT break installs — the package-0 finder injects.)")
hits = pattern_matches(LOADPKG_CURRENT)
if len(hits) == 0:
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

# 4a) Cache-access idiom (0xc58)
print("\n  [a] cache-access idiom (anchored on tree-root offset 0x%x):" % CACHE_ROOT_OFFSET)
idiom = verify_finder_cache_idiom()
if not idiom:
    print("      NOT FOUND — the 0x%x root offset of CPackageInfoCache likely moved." % CACHE_ROOT_OFFSET)
    print("      ACTION: update kCacheRootIdxOff / kCacheNodesOff in")
    print("              src/hooks/package_zero_finder.cpp (and the 0x%x literal" % CACHE_ROOT_OFFSET)
    print("              in FindCacheGlobalDisp) to the new offset.")
else:
    for base, disp in idiom:
        print("      PRESENT @ %s   disp32=0x%x  (cache_global = GOT + disp32)" %
              (base, disp & 0xFFFFFFFF))
    print("      OK — finder's cache locator will work on this binary.")

# 4b) GMRC prologue tail
print("\n  [b] GMRC prologue tail (survives the hook detour):")
tail = verify_gmrc_prologue_tail()
if not tail:
    print("      NOT FOUND — Steam may have reorganised GMRC's prologue.")
    print("      ACTION: update the `tail` byte array in DeriveGotBase() in")
    print("              src/hooks/package_zero_finder.cpp to the new sequence.")
elif len(tail) == 1:
    print("      PRESENT @ %s   (unique)" % tail[0])
    print("      OK — finder's GOT derivation will work on this binary.")
else:
    print("      PRESENT but matches %d sites — DeriveGotBase needs a more" % len(tail))
    print("      specific anchor than the current tail bytes.")
    for h in tail[:6]:
        print("      @ %s" % h)

print("\n================= done. Paste UNIQUE patterns into src/patterns.hpp =================")
print("Reminders:")
print("  * Patterns above go in src/patterns.hpp (then rebuild + release).")
print("  * Finder anchor results are diagnostics for src/hooks/package_zero_finder.cpp;")
print("    if either says NOT FOUND, the finder needs a code fix (not a pattern bump).")
