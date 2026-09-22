#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Unit test for the string-xref core (src/gmrc_xref_core.hpp) — the locator
# GMRC has used as its rescue since 2026-09-07 and that ShaderDepot and
# BuildDep use as theirs since 2026-09-22 (GmrcXref::FindFunctionByString).
# Synthetic bytes, compiled 64-bit with g++ (skipped if there is no compiler).
#
#   python3 tools/test_xref_core.py     # from the repo root
#
# Pins the three shared steps and the one rule that makes them safe:
#   1. GOT base by consensus over PIC preambles (the majority wins);
#   2. the UNIQUE lea: one referencing site -> that site; zero or two -> 0;
#   3. the search is by disp32 == string - GOT, so a string found at the WRONG
#      address (a bare needle matching inside a longer string) yields no site,
#      never a wrong one — which is why the runtime searches WITH the NUL;
#   4. the walk-back is right for a preamble-first prologue (GMRC's shape) and
#      a few bytes off for `push …; call thunk` prologues (BuildDep,
#      ShaderDepot) — the reason FindFunctionByString refuses without
#      .eh_frame_hdr instead of falling back to it.
import os
import struct
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VADDR = 0x100000            # where the synthetic .text "is"
GOT = 0x300000              # the module's _GLOBAL_OFFSET_TABLE_
fails = 0


def check(cond, msg):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        fails += 1


HARNESS = r'''
#include "gmrc_xref_core.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace GmrcXrefCore;
int main(int argc, char** argv) {
    static uint8_t buf[65536];
    FILE* f = fopen(argv[1], "rb"); if (!f) return 2;
    std::size_t n = fread(buf, 1, sizeof(buf), f); fclose(f);
    // The buffer plays .text at VADDR; addresses are in that space.
    Region rx{ buf, n, (uintptr_t)strtoul(argv[2], nullptr, 16) };
    uintptr_t strAddr = strtoul(argv[3], nullptr, 16);
    uintptr_t got = 0;
    uintptr_t site = FindUniqueLeaForString(rx, strAddr, &got);
    uintptr_t wb = site ? WalkBackToPrologue(rx, site) : 0;
    printf("%lx %lx %lx\n", (unsigned long)got, (unsigned long)site, (unsigned long)wb);
    return 0;
}
'''


def build():
    try:
        subprocess.run(["g++", "--version"], capture_output=True, check=True)
    except Exception:
        return None
    tmp = tempfile.mkdtemp()
    cpp, exe = os.path.join(tmp, "h.cpp"), os.path.join(tmp, "h")
    open(cpp, "w").write(HARNESS)
    r = subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-I", os.path.join(REPO, "src"),
                        "-o", exe, cpp], capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr)
        return None
    return exe


def run(exe, text, str_addr):
    tmp = tempfile.mktemp()
    open(tmp, "wb").write(text)
    out = subprocess.run([exe, tmp, "%x" % VADDR, "%x" % str_addr],
                         capture_output=True, text=True).stdout.split()
    return tuple(int(x, 16) for x in out)


# ── byte builders (i386 PIC) ─────────────────────────────────────────────────

def preamble(at, reg_eax=True):
    """`call get_pc_thunk ; add reg, imm32` at address `at`, with imm32 chosen
    so that (address after the call) + imm32 == GOT — what the consensus reads."""
    after_call = at + 5
    imm = (GOT - after_call) & 0xFFFFFFFF
    if reg_eax:
        return b"\xE8\x00\x00\x00\x00" + b"\x05" + struct.pack("<I", imm)
    return b"\xE8\x00\x00\x00\x00" + b"\x81\xC3" + struct.pack("<I", imm)   # add ebx


def lea_str(str_addr):
    """`lea eax,[ebx + (str - GOT)]` — the GOT-relative string load."""
    return b"\x8D\x83" + struct.pack("<I", (str_addr - GOT) & 0xFFFFFFFF)


def gmrc_shaped(at, str_addr):
    """preamble FIRST (GMRC's shape), then push ebp…, then the lea."""
    return preamble(at) + bytes.fromhex("5589E5575653") + b"\x90" * 4 + lea_str(str_addr) + b"\x90" * 8 + b"\xC3"


def pushes_first(at, str_addr, npush=3):
    """`push ebp; mov ebp,esp; push edi; push esi …` THEN the thunk call
    (BuildDep `55 89 E5 57 56 E8`, ShaderDepot `57 56 53 E8`)."""
    head = bytes.fromhex("5589E5575653")[:npush + 3] if npush == 3 else bytes.fromhex("575653")
    return head + preamble(at + len(head), reg_eax=False) + b"\x90" * 4 + lea_str(str_addr) + b"\x90" * 8 + b"\xC3"


def filler_fn(at):
    """An unrelated PIC function: one more vote for the GOT, no string lea."""
    return preamble(at) + b"\x90" * 6 + b"\xC3"


def layout(*fns):
    """Functions 0x100 apart; each builder gets its own address."""
    buf = bytearray(0x100 * (len(fns) + 1))
    starts = []
    for i, build in enumerate(fns):
        at = VADDR + 0x100 * (i + 1) - 0x80
        body = build(at)
        buf[at - VADDR:at - VADDR + len(body)] = body
        starts.append(at)
    return bytes(buf), starts


def main():
    exe = build()
    if not exe:
        print("skip: no g++ — the C++ leg is compiled in build.yml")
        return 0
    S = 0x280000        # where the anchor string "is" (read-only data)
    S2 = 0x280010       # a different string

    # ── 1+2. consensus GOT and the unique lea ───────────────────────────────
    text, starts = layout(filler_fn, filler_fn, lambda at: gmrc_shaped(at, S), filler_fn)
    got, site, wb = run(exe, text, S)
    check(got == GOT, "GOT by consensus over 4 preambles: 0x%x" % got)
    check(site == starts[2] + 10 + 6 + 4, "unique lea site inside the function: 0x%x" % site)
    check(wb == starts[2], "walk-back on a preamble-first prologue = entry (GMRC shape)")

    # ── 2. zero or two referencing sites -> 0 (fail closed) ─────────────────
    text, starts = layout(filler_fn, lambda at: gmrc_shaped(at, S), lambda at: gmrc_shaped(at, S))
    got, site, wb = run(exe, text, S)
    check(got == GOT and site == 0, "two sites for the same string -> no site (0x%x)" % site)
    text, starts = layout(filler_fn, lambda at: gmrc_shaped(at, S2))
    got, site, wb = run(exe, text, S)
    check(site == 0, "no site for the string -> 0")

    # ── 3. the string found at the wrong address yields nothing, not a hit ──
    text, starts = layout(filler_fn, lambda at: gmrc_shaped(at, S))
    got, site, wb = run(exe, text, S - 13)     # "found" 13 bytes early (inside a longer string)
    check(site == 0, "wrong string address -> disp mismatch -> no site (why the NUL matters)")

    # ── 4. walk-back is off for push-first prologues ────────────────────────
    text, starts = layout(filler_fn, lambda at: pushes_first(at, S, npush=3))       # 55 89 E5 57 56 53 E8
    got, site, wb = run(exe, text, S)
    check(site != 0, "BuildDep-shaped: lea site found 0x%x" % site)
    check(wb == starts[1] + 6, "BuildDep-shaped: walk-back lands entry+6 (0x%x vs entry 0x%x)" % (wb, starts[1]))
    text, starts = layout(filler_fn, lambda at: pushes_first(at, S, npush=0))       # 57 56 53 E8
    got, site, wb = run(exe, text, S)
    check(wb == starts[1] + 3, "ShaderDepot-shaped: walk-back lands entry+3 — never a detour target")

    # ── the runtime never walks back for these: pinned in the source ────────
    src = open(os.path.join(REPO, "src", "gmrc_xref.cpp")).read()
    check('Locate(needle, std::strlen(needle) + 1, tag, /*walkBack=*/false)' in src,
          "FindFunctionByString: NUL-terminated search, no walk-back")
    check('Locate(kJobName, std::strlen(kJobName), "GMRC xref", /*walkBack=*/true)' in src,
          "FindGmrcFunction: unchanged needle and walk-back fallback")
    for hook, needle in (("shader_depot_hook.cpp", "shadercachedepot"),
                         ("depot_dependency_hook.cpp", "BuildDepotDependency")):
        h = open(os.path.join(REPO, "src", "hooks", hook)).read()
        check('FindFunctionByString("%s"' % needle in h and 'xref(rescue)' in h,
              "%s: rescue anchored on %r" % (hook, needle))

    print("\n%s" % ("FAILURES: %d" % fails if fails else "all ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
