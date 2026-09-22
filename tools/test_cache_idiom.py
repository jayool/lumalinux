#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Unit test for the cache-access idiom locator — the finder's only route to
# CPackageInfoCache. Uses synthetic exec segments, so it needs no real
# steamclient.so and runs anywhere.
#
#   python3 tools/test_cache_idiom.py     # from the repo root
#
# WHY THIS EXISTS
# ---------------
# The same 14-byte idiom is recognised in FOUR places, and they drifted:
#
#   src/hooks/package_zero_finder.cpp   FindCacheGlobalDisp   (the runtime)
#   tools/check_patterns.py             verify_cache_idiom    (the nightly gate)
#   tools/derive_patterns.py            verify_finder_cache_idiom (the Ghidra leg)
#   tools/experiment_cache_idiom.py     scan_idiom            (the offline probe)
#
# Before 2026-09-08 the two CI legs checked only the three opcodes — not the mod
# fields, not the rm exclusions, not the register chaining — so they were LOOSER
# than the runtime and could green-light a build the Deck then refuses. A gate
# that passes what the product rejects is worse than no gate.
#
# So this test does two things: it pins the PREDICATES (what counts as a match)
# and it pins the AGREEMENT between the implementations. Adding a rule to one
# and not the others must fail here.
#
# The idiom, 14 bytes:
#   8D /r mod=10 disp32        lea  r1,[GOT + X]      <- X is what we want
#   8B /r mod=00               mov  r2,[r1]
#   8B /r mod=10 <root LE>     mov  r3,[r2 + root]    <- root is the anchor
# plus CHAINED registers: reg(lea)==rm(mov1) and reg(mov1)==rm(mov2).
# Deliberately NOT required: a fixed base register for the lea — the two real
# sites on bc54101b29 use different ones (esi and eax).
#
# Since 2026-09-22 `root` is not one number but a TABLE of known layouts
# (0xc58 stable, 0xf90 on the 9cf4720f beta) and all four copies scan every
# row with a two-level rule: exactly ONE row may have sites, and that row must
# name exactly ONE disp32. This test pins the table (the four must list the
# same rows), the rule (one row / no row / two rows), and the GMRC tail's
# wildcarded frame-size bytes (0x110 and 0x120 both derive the GOT).
import os
import struct
import subprocess
import sys
import tempfile
import textwrap

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_patterns import (CACHE_LAYOUTS, GMRC_PROLOGUE_TAIL,  # noqa: E402
                            GMRC_TAIL_WILDCARD, classify_cache_idiom,
                            classify_cache_layouts, classify_gmrc_got,
                            emit_rvas_file, verify_cache_idiom,
                            verify_cache_layouts, verify_gmrc_got)
import experiment_cache_idiom as probe  # noqa: E402

ROOT_STABLE = 0xC58
ROOT_BETA = 0xF90

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VADDR = 0x1000
fails = 0


def check(cond, msg):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        fails += 1


# ── byte builders ────────────────────────────────────────────────────────────

def chained(disp, lea_base=3, root=ROOT_STABLE):
    """A real idiom. lea_base picks the GOT register (3=ebx, 0=eax, 6=esi) —
    varying it must NOT change the outcome. `root` picks the layout row the
    site belongs to (the anchor bytes)."""
    m1 = 0x80 | (1 << 3) | lea_base   # mod=10 reg=ecx rm=<base>  lea ecx,[base+d]
    m2 = 0x00 | (2 << 3) | 1          # mod=00 reg=edx rm=ecx     mov edx,[ecx]
    m3 = 0x80 | (0 << 3) | 2          # mod=10 reg=eax rm=edx     mov eax,[edx+root]
    return (bytes([0x8D, m1]) + struct.pack("<i", disp) +
            bytes([0x8B, m2, 0x8B, m3]) + struct.pack("<I", root))


def unchained(disp):
    """Right shapes, unrelated registers: lea esi,[eax+d]; mov edi,[ecx];
    mov eax,[edx+0xc58]. This is what the pre-2026-09-08 opcode-only check
    accepted, and every such accident is a bogus candidate X."""
    return (bytes([0x8D, 0xB0]) + struct.pack("<i", disp) +
            bytes([0x8B, 0x39, 0x8B, 0x82, 0x58, 0x0C, 0x00, 0x00]))


def bad_mod(disp):
    """mod=01 on the lea (8-bit displacement): the instruction is 3 bytes, not
    6, so the 0xc58 would not be where we think. Must be rejected."""
    return (bytes([0x8D, 0x4B]) + struct.pack("<i", disp) +
            bytes([0x8B, 0x11, 0x8B, 0x82, 0x58, 0x0C, 0x00, 0x00]))


def naked_needle():
    """The 0xc58 anchor with no idiom in front — a field at offset 0xc58 of some
    other class, which is exactly the collision the anchor cannot rule out."""
    return b"\x90" * 10 + bytes([0x58, 0x0C, 0x00, 0x00])


def gmrc(got_rva, at, frame=None):
    """A GMRC prologue whose `add eax,imm32` derives `got_rva` when the 0x05
    byte lands at RVA `at`: imm = got - rva(0x05). The leading `E8 rel32` is
    included for realism but is NOT what the scan anchors on — our own detour
    overwrites exactly those 5 bytes at runtime. `frame` overrides the
    `sub esp,imm32` size in the tail (0x110 as shipped; 0x120 on the beta)."""
    tail = bytearray(int(t, 16) for t in GMRC_PROLOGUE_TAIL.split())
    if frame is not None:
        lo, hi = GMRC_TAIL_WILDCARD
        tail[lo:hi + 3] = struct.pack("<I", frame)      # the whole sub esp,imm32
    return (bytes([0xE8, 0, 0, 0, 0]) +
            bytes([0x05]) + struct.pack("<i", got_rva - at) + bytes(tail))


def tail_without_add():
    """The 18 tail bytes with something other than 0x05 in front: the shape is
    there but the instruction that computes the GOT is not."""
    tail = bytes(int(t, 16) for t in GMRC_PROLOGUE_TAIL.split())
    return bytes([0x90] * 5) + tail


def seg(*chunks):
    buf = bytearray()
    for c in chunks:
        buf += c
        buf += b"\x90" * 8
    return [(VADDR, bytes(buf))]


# ── the Ghidra leg, extracted from its own file ──────────────────────────────
# derive_patterns.py runs under Jython inside Ghidra, so it cannot be imported.
# We lift its predicate block verbatim and drive it with a byte buffer: if
# someone edits the predicates there and not here, this stops matching.

def ghidra_layouts():
    """derive_patterns.CACHE_LAYOUTS, read without importing the Jython script."""
    src = open(os.path.join(REPO, "tools", "derive_patterns.py")).read()
    i = src.index("CACHE_LAYOUTS = [")
    j = src.index("]", i) + 1
    ns = {}
    exec(src[i:j], ns)
    return ns["CACHE_LAYOUTS"]


def ghidra_scan(segments, root=ROOT_STABLE):
    src = open(os.path.join(REPO, "tools", "derive_patterns.py")).read()
    i = src.index("            # Same predicates as Hooks::PackageZeroFinder")
    j = src.index("            matches.append((base, disp))") + \
        len("            matches.append((base, disp))")
    block = textwrap.dedent(src[i:j])
    ns = {"ROOT": root}
    exec("def _scan(buf, vaddr):\n"
         "    import struct\n"
         "    def b(a, o): return buf[a + o]\n"
         "    matches = []\n"
         "    needle = struct.pack('<I', ROOT); start = 0\n"
         "    while True:\n"
         "        h = buf.find(needle, start)\n"
         "        if h < 0: break\n"
         "        start = h + 1\n"
         "        if h < 10: continue\n"
         "        base = h - 10\n"
         "        if base + 14 > len(buf): continue\n"
         + textwrap.indent(block, "        ") + "\n"
         "    return [(vaddr + bs, d) for bs, d in matches]\n", ns)
    return sorted(x for v, b in segments for x in ns["_scan"](b, v))


def ghidra_gmrc_got(segments):
    """derive_patterns.verify_gmrc_prologue_tail(), extracted whole and driven
    through a stand-in for the Ghidra API it expects (Address arithmetic, a
    memory reader, and pattern_matches). Nobody can run Ghidra locally, so this
    leg is the one most likely to rot unnoticed."""
    src = open(os.path.join(REPO, "tools", "derive_patterns.py")).read()
    i = src.index("def verify_gmrc_prologue_tail():")
    j = src.index("\n\n", src.index("    return out", i))
    vaddr, buf = segments[0]

    class Addr(int):
        def getOffset(self): return int(self)
        def add(self, n): return Addr(int(self) + n)
        def subtract(self, n): return Addr(int(self) - n)

    class Mem(object):
        def getByte(self, a):
            b = buf[int(a) - vaddr]
            return b - 256 if b > 127 else b        # Ghidra returns signed

    def pattern_matches(pat):
        needle = bytes(int(t, 16) for t in pat.split())
        out, start = [], 0
        while True:
            o = buf.find(needle, start)
            if o < 0:
                return out
            start = o + 1
            out.append(Addr(vaddr + o))

    ns = {"mem": Mem(), "pattern_matches": pattern_matches,
          "GMRC_PROLOGUE_TAIL": GMRC_PROLOGUE_TAIL,
          "GMRC_TAIL_WILDCARD": GMRC_TAIL_WILDCARD, "Exception": Exception}
    exec(src[i:j], ns)
    return [(int(a), g) for a, g in ns["verify_gmrc_prologue_tail"]()]


# ── the C++ leg, extracted from its own file ─────────────────────────────────
# Compiled 64-bit with the surrounding stubs: the function only walks bytes, so
# pointer width is irrelevant to what it decides. Skipped if g++ is missing.

CXX_HARNESS = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstddef>
struct ScRange { uintptr_t base = 0; std::size_t size = 0; };
%(LAYOUTS)s
namespace Log { void Info(const char*, ...) {} void Error(const char*, ...) {} }
%(FN)s
int main(int argc, char** argv) {
    static uint8_t buf[65536];
    std::size_t n = 0; FILE* f = fopen(argv[1], "rb");
    if (!f) return 2;
    n = fread(buf, 1, sizeof(buf), f); fclose(f);
    ScRange rx; rx.base = (uintptr_t)buf; rx.size = n;
    // "got" is reported as an OFFSET from the buffer start so the test can
    // compare it against an RVA without knowing where malloc put us.
    if (argc > 2 && std::strcmp(argv[2], "got") == 0) {
        uintptr_t g = DeriveGotBase(rx);
        printf("%%lld\n", g ? (long long)(g - rx.base) : -1LL);
    } else if (argc > 2 && std::strcmp(argv[2], "layout") == 0) {
        int L = -1; FindCacheGlobalDisp(rx, &L);
        printf("%%s\n", L >= 0 ? kCacheLayouts[L].name : "-");
    } else {
        printf("%%d\n", (int)FindCacheGlobalDisp(rx));
    }
    return 0;
}
'''


def build_cxx():
    """Compile FindCacheGlobalDisp exactly as it stands in the .cpp. Returns the
    binary path, or None if there is no compiler."""
    try:
        subprocess.run(["g++", "--version"], capture_output=True, check=True)
    except Exception:
        return None
    src = open(os.path.join(REPO, "src", "hooks", "package_zero_finder.cpp")).read()
    i = src.index("uintptr_t DeriveGotBase(ScRange rx) {")
    j = src.index("// ============================================================"
                  "=================\n// Readability gate")
    # The layout TABLE is lifted from the .cpp too (struct + kCacheLayouts +
    # kNumCacheLayouts), so the C++ leg scans exactly the rows the Deck does.
    a = src.index("struct CacheLayout {")
    b = src.index("\n", src.index("constexpr int kNumCacheLayouts"))
    tmp = tempfile.mkdtemp()
    cpp, exe = os.path.join(tmp, "h.cpp"), os.path.join(tmp, "h")
    with open(cpp, "w") as f:
        f.write(CXX_HARNESS % {"FN": src[i:j], "LAYOUTS": src[a:b]})
    r = subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-o", exe, cpp],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr)
        return None
    return exe


def cxx_got(exe, segments):
    """DeriveGotBase's answer as an offset from the buffer start (-1 = refused),
    which is directly comparable to an RVA measured from VADDR."""
    tmp = tempfile.mktemp()
    with open(tmp, "wb") as f:
        f.write(segments[0][1])
    out = subprocess.run([exe, tmp, "got"], capture_output=True, text=True).stdout.strip()
    return int(out)


def cxx_disp(exe, segments):
    blob = segments[0][1]
    tmp = tempfile.mktemp()
    with open(tmp, "wb") as f:
        f.write(blob)
    out = subprocess.run([exe, tmp], capture_output=True, text=True).stdout.strip()
    return int(out) & 0xFFFFFFFF


def cxx_layout(exe, segments):
    """The kCacheLayouts row name the C++ scan resolved, or "-"."""
    tmp = tempfile.mktemp()
    with open(tmp, "wb") as f:
        f.write(segments[0][1])
    return subprocess.run([exe, tmp, "layout"], capture_output=True, text=True).stdout.strip()


# ── cases ────────────────────────────────────────────────────────────────────

X, Y = 0x3b7d4, 0x18240

CASES = [
    # (name, segments, expected status, expected distinct disps)
    ("one chained site", seg(chained(X)), "UNIQUE", [X]),
    ("two sites, same disp", seg(chained(X), chained(X, lea_base=0)), "UNIQUE", [X]),
    ("two sites, different disps", seg(chained(X), chained(Y)), "AMBIGUOUS", [Y, X]),
    ("unchained garbage only", seg(unchained(Y)), "NOT_FOUND", []),
    ("unchained garbage + one good", seg(unchained(Y), chained(X)), "UNIQUE", [X]),
    ("wrong mod on the lea", seg(bad_mod(X)), "NOT_FOUND", []),
    ("bare 0xc58, no idiom", seg(naked_needle()), "NOT_FOUND", []),
    ("empty", seg(b"\x90" * 32), "NOT_FOUND", []),
]


def main():
    exe = build_cxx()
    if exe is None:
        print("note: no g++ — the C++ leg is skipped (python legs still run)")

    # ── the layout TABLE: the four copies must list the same rows ──────────
    cxx_src = open(os.path.join(REPO, "src", "hooks", "package_zero_finder.cpp")).read()
    a = cxx_src.index("constexpr CacheLayout kCacheLayouts[] = {")
    b = cxx_src.index("};", a)
    cxx_rows = [(m.group(1), int(m.group(2), 16), int(m.group(3), 16)) for m in
                __import__("re").finditer(r'\{"([^"]+)",\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+)\}',
                                          cxx_src[a:b])]
    rows = [tuple(r) for r in CACHE_LAYOUTS]
    check(cxx_rows == rows, "layout table: package_zero_finder.cpp == check_patterns %s"
          % [(n, hex(r), hex(x)) for n, r, x in rows])
    check([tuple(r) for r in ghidra_layouts()] == rows, "layout table: derive_patterns agrees")
    check([tuple(r) for r in probe.CACHE_LAYOUTS] == rows, "layout table: experiment_cache_idiom agrees")
    check(all(nodes == root + 0x14 for _n, root, nodes in rows), "every row: nodes == root + 0x14")
    check(all(r in (ROOT_STABLE, ROOT_BETA) for _n, r, _x in rows) and len(rows) == 2,
          "table rows are exactly 0xc58 (stable) and 0xf90 (beta) — update this test when adding one")

    for name, segs, want_status, want_disps in CASES:
        idiom = verify_cache_idiom(segs, ROOT_STABLE)
        status, disps = classify_cache_idiom(idiom)
        check(status == want_status,
              "%-34s check_patterns -> %s" % (name, status))
        check(sorted(d & 0xFFFFFFFF for d in disps) ==
              sorted(d & 0xFFFFFFFF for d in want_disps),
              "%-34s disps %s" % (name, [hex(d & 0xFFFFFFFF) for d in disps]))
        check(sorted(idiom) == ghidra_scan(segs),
              "%-34s derive_patterns agrees" % name)
        check(sorted((s.rva, s.disp) for s in probe.scan_idiom(segs, ROOT_STABLE) if s.chained)
              == sorted(idiom), "%-34s experiment_cache_idiom agrees" % name)
        if exe is not None:
            want = disps[0] & 0xFFFFFFFF if status == "UNIQUE" else 0
            check(cxx_disp(exe, segs) == want,
                  "%-34s C++ returns 0x%x" % (name, want))

    # ── the two-level rule across layouts ───────────────────────────────────
    # Exactly one row may have sites (else AMBIGUOUS_LAYOUT), and that row must
    # name exactly one disp32. Measured 2026-09-22: stable has 0 sites at 0xf90,
    # the beta 0 at 0xc58 — so on every real build seen, one row answers.
    LAYOUT_CASES = [
        ("stable row only", seg(chained(X), chained(X, lea_base=0)),
         "UNIQUE", [X], "stable-0xc58"),
        ("beta row only", seg(chained(X, root=ROOT_BETA), chained(X, lea_base=6, root=ROOT_BETA)),
         "UNIQUE", [X], "beta-0xf90"),
        ("beta row, disagreeing disps", seg(chained(X, root=ROOT_BETA), chained(Y, root=ROOT_BETA)),
         "AMBIGUOUS", [Y, X], "beta-0xf90"),
        ("both rows have sites", seg(chained(X), chained(X, root=ROOT_BETA)),
         "AMBIGUOUS_LAYOUT", [X], None),
        ("neither row", seg(chained(X, root=0xD00)), "NOT_FOUND", [], None),
    ]
    for name, segs, want_status, want_disps, want_layout in LAYOUT_CASES:
        per = verify_cache_layouts(segs)
        status, disps, layout = classify_cache_layouts(per)
        check(status == want_status and layout == want_layout,
              "%-34s check_patterns -> %s / %s" % (name, status, layout))
        check(sorted(d & 0xFFFFFFFF for d in disps) == sorted(d & 0xFFFFFFFF for d in want_disps),
              "%-34s disps %s" % (name, [hex(d & 0xFFFFFFFF) for d in disps]))
        for lname, lroot, _n in CACHE_LAYOUTS:
            check(sorted(per[lname]) == ghidra_scan(segs, lroot),
                  "%-34s derive_patterns agrees on %s" % (name, lname))
        if exe is not None:
            want = disps[0] & 0xFFFFFFFF if status == "UNIQUE" else 0
            check(cxx_disp(exe, segs) == want,
                  "%-34s C++ returns 0x%x" % (name, want))
            check(cxx_layout(exe, segs) == (want_layout if status == "UNIQUE" else "-"),
                  "%-34s C++ names layout %s" % (name, want_layout if status == "UNIQUE" else "-"))

    # ── the GOT leg (DeriveGotBase) ──────────────────────────────────────────
    # Classified over the DERIVED GOT, not the match count: a second PIC
    # prologue of the same shape computes the SAME base, so agreeing sites are
    # healthy. Only disagreement is fatal, and it now fails closed.
    G1, G2 = 0x2f4a34c, 0x2f40000
    P = 5 + 5 + 18                       # E8 rel32 + 05 imm32 + the 18-byte tail
    GOT_CASES = [
        ("one prologue",
         seg(gmrc(G1, VADDR + 5)), "UNIQUE", [G1]),
        ("two prologues, same GOT",
         seg(gmrc(G1, VADDR + 5), gmrc(G1, VADDR + P + 8 + 5)), "UNIQUE", [G1]),
        ("two prologues, different GOT",
         seg(gmrc(G1, VADDR + 5), gmrc(G2, VADDR + P + 8 + 5)), "AMBIGUOUS", [G2, G1]),
        ("tail present, no add eax",
         seg(tail_without_add()), "NOT_FOUND", []),
        ("nothing",
         seg(b"\x90" * 64), "NOT_FOUND", []),
        # The frame-size bytes are free: 0x120 (the 9cf4720f beta) derives the
        # GOT exactly like 0x110, and a frame >= 64 KB (high bytes != 00 00)
        # does not match at all.
        ("beta frame 0x120",
         seg(gmrc(G1, VADDR + 5, frame=0x120)), "UNIQUE", [G1]),
        ("frame 0x110 and 0x120, same GOT",
         seg(gmrc(G1, VADDR + 5), gmrc(G1, VADDR + P + 8 + 5, frame=0x120)), "UNIQUE", [G1]),
        ("frame 0x10120 (high bytes set)",
         seg(gmrc(G1, VADDR + 5, frame=0x10120)), "NOT_FOUND", []),
    ]
    for name, segs, want_status, want_gots in GOT_CASES:
        sites = verify_gmrc_got(segs)
        status, gots = classify_gmrc_got(sites)
        check(status == want_status, "%-34s check_patterns -> %s" % (name, status))
        check(sorted(gots) == sorted(want_gots),
              "%-34s gots %s" % (name, [hex(g) for g in gots]))
        check(sorted(sites) == sorted(ghidra_gmrc_got(segs)),
              "%-34s derive_patterns agrees" % name)
        if exe is not None:
            want = (gots[0] - VADDR) if status == "UNIQUE" else -1
            check(cxx_got(exe, segs) == want,
                  "%-34s C++ derives %s" % (name, hex(want) if want >= 0 else "nothing"))

    # The lea's base register must not matter: pinning it would have discarded a
    # real site on bc54101b29 (esi at 0xfdd2dd, eax at 0x18964e5).
    for base, reg in ((3, "ebx"), (0, "eax"), (6, "esi")):
        st, dd = classify_cache_idiom(verify_cache_idiom(seg(chained(X, base)), ROOT_STABLE))
        check(st == "UNIQUE" and (dd[0] & 0xFFFFFFFF) == X,
              "GOT register %-4s is accepted" % reg)

    # The feed must publish X only when every site agreed on it, and must not be
    # revived by a stale pre-2026-09-08 "PRESENT".
    for status, disps, want in (("UNIQUE", ["0x3b7d4"], True),
                                ("AMBIGUOUS", ["0x3b7d4", "0x18240"], False),
                                ("NOT_FOUND", [], False),
                                ("PRESENT", ["0x3b7d4"], False)):
        d = tempfile.mkdtemp()
        emit_rvas_file({"steamclient_sha256": "0" * 64, "hooks": {}, "rtti": {},
                        "finder": {"cache_idiom": {"status": status,
                                                   "distinct_disp32": disps,
                                                   "sites_total": len(disps),
                                                   "sites": []}}},
                       d, "0")
        txt = open(os.path.join(d, "0" * 64 + ".yaml")).read()
        check(("cache_global_disp" in txt) == want,
              "feed publishes X on %-9s -> %s" % (status, want))

    # got_rva, the finder's OTHER number, gates on its OWN anchor's verdict.
    for status, gots, want in (("UNIQUE", ["0x2f4a34c"], True),
                               ("AMBIGUOUS", ["0x2f4a34c", "0x2f40000"], False),
                               ("NOT_FOUND", [], False)):
        d = tempfile.mkdtemp()
        emit_rvas_file({"steamclient_sha256": "0" * 64, "hooks": {}, "rtti": {},
                        "finder": {"gmrc_tail": {"status": status,
                                                 "distinct_got": gots,
                                                 "count": len(gots), "rvas": []}}},
                       d, "0")
        txt = open(os.path.join(d, "0" * 64 + ".yaml")).read()
        check(("got_rva" in txt) == want,
              "feed publishes GOT on %-9s -> %s" % (status, want))

    # The two are independent: a build whose idiom is ambiguous can still have an
    # unambiguous GOT, and half a ficha beats none. This is the case that would
    # silently regress if someone folded them back into one `if`.
    d = tempfile.mkdtemp()
    emit_rvas_file({"steamclient_sha256": "0" * 64, "hooks": {}, "rtti": {},
                    "finder": {"cache_idiom": {"status": "AMBIGUOUS",
                                               "distinct_disp32": ["0x1", "0x2"],
                                               "sites_total": 2, "sites": []},
                               "gmrc_tail": {"status": "UNIQUE",
                                             "distinct_got": ["0x2f4a34c"],
                                             "count": 1, "rvas": []}}},
                   d, "0")
    txt = open(os.path.join(d, "0" * 64 + ".yaml")).read()
    check("got_rva" in txt and "cache_global_disp" not in txt,
          "one anchor ambiguous, the other unique -> only the unique half ships")

    # And the header must not be emitted with nothing under it: `finder:` alone
    # is a YAML null, which the runtime's `finder.IsMap()` check would reject —
    # harmlessly, but it would also make a hand-inspected feed file misleading.
    d = tempfile.mkdtemp()
    emit_rvas_file({"steamclient_sha256": "0" * 64, "hooks": {}, "rtti": {},
                    "finder": {"cache_idiom": {"status": "NOT_FOUND",
                                               "distinct_disp32": [],
                                               "sites_total": 0, "sites": []},
                               "gmrc_tail": {"status": "NOT_FOUND",
                                             "distinct_got": [], "count": 0,
                                             "rvas": []}}},
                   d, "0")
    txt = open(os.path.join(d, "0" * 64 + ".yaml")).read()
    check("finder:" not in txt, "no finder values -> no empty `finder:` header")

    print("\n%s" % ("FAILURES: %d" % fails if fails else "all ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
