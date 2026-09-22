#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Unit test for the C++ Reconcile anchor locator (src/reconcile_anchor_core.hpp)
# against its Python original (tools/derive_reconcile_byanchor.py): the runtime
# rescue and the CI deriver must give the SAME verdict on the same bytes, or the
# Deck could rescue a function CI would refuse (or the reverse). Synthetic
# functions come from test_derive_reconcile_byanchor.py, so one set of builders
# feeds both. Compiled 64-bit with g++ (skipped if there is no compiler).
#
#   python3 tools/test_reconcile_anchor.py     # from the repo root
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import derive_reconcile_byanchor as drb  # noqa: E402
from test_derive_reconcile_byanchor import (VADDR, layout, poster_only,  # noqa: E402
                                            reconcile_like, this_read_no_bail)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fails = 0


def check(cond, msg):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        fails += 1


HARNESS = r'''
#include "reconcile_anchor_core.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace ReconcileAnchorCore;
int main(int argc, char** argv) {
    static uint8_t buf[65536];
    FILE* f = fopen(argv[1], "rb"); if (!f) return 2;
    std::size_t n = fread(buf, 1, sizeof(buf), f); fclose(f);
    Region rx{ buf, n, (uintptr_t)strtoul(argv[2], nullptr, 16) };
    std::vector<uintptr_t> starts;                   // the ".eh_frame_hdr" of the test
    for (int i = 3; i < argc; ++i) starts.push_back(strtoul(argv[i], nullptr, 16));
    auto fnOf = [&](uintptr_t site, uintptr_t& s, uintptr_t& e) -> bool {   // like Python's function_bounds
        auto it = std::upper_bound(starts.begin(), starts.end(), site);
        if (it == starts.begin()) return false;
        s = *(it - 1);
        e = (it != starts.end()) ? *it : rx.addr + rx.size;
        return true;
    };
    Info info;
    uintptr_t r = LocateNotifyLicensesUpdated(rx, fnOf, &info);
    printf("%lx %zu %zu %zu %x\n", (unsigned long)r, info.sites, info.candidates, info.kept, info.memberDisp);
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


def cxx(exe, text, starts):
    tmp = tempfile.mktemp()
    open(tmp, "wb").write(text)
    out = subprocess.run([exe, tmp, "%x" % VADDR] + ["%x" % s for s in starts],
                         capture_output=True, text=True).stdout.split()
    rva, sites, cands, kept, disp = out
    return int(rva, 16), int(sites), int(cands), int(kept), int(disp, 16)


def main():
    exe = build()
    if not exe:
        print("skip: no g++ — the C++ leg is compiled in build.yml")
        return 0

    CASES = [
        ("one real, ten posters", [reconcile_like()] + [poster_only()] * 10),
        ("beta layout (member 0x1b10, frame 0x1cc, other reg)",
         [poster_only(), reconcile_like(0x1b10, 0x1cc, reg=6), poster_only()]),
        ("member offset far away", [reconcile_like(0x80)]),
        ("two real -> refuse", [reconcile_like(), poster_only(), reconcile_like(0x1b10)]),
        ("only posters -> refuse", [poster_only()] * 3),
        ("this-read without the bail -> refuse", [this_read_no_bail(), poster_only()]),
        ("nothing posts 125", [bytes.fromhex("5589E5C3")]),
        ("real + no-bail decoy", [reconcile_like(), poster_only(), this_read_no_bail()]),
    ]
    for name, fns in CASES:
        text, starts, read_va = layout(*fns)
        py_status, py_rva, py_info = drb.locate(text, VADDR, starts, read_va)
        rva, sites, cands, kept, disp = cxx(exe, text, starts)
        py_rva = py_rva or 0
        same = (rva == py_rva and sites == py_info["callback_sites"]
                and cands == py_info["candidate_fns"] and kept == len(py_info["kept"]))
        check(same, "%-52s py=%s/0x%x c++=0x%x (sites %d, fns %d, kept %d)"
              % (name, py_status, py_rva, rva, sites, cands, kept))
        if py_status == "UNIQUE":
            check(disp == int(py_info["member_disp"], 16),
                  "%-52s member disp agrees: 0x%x" % ("", disp))

    # a SMALL neighbour before the real one must not borrow its this-read: the
    # head is bounded by the function's end (reproduced with a gcc -m32 object)
    small = bytes.fromhex("5589E5") + b"\x6A\x7D\x50\xE8\x00\x00\x00\x00" + b"\xC3"   # 12 bytes
    text = bytearray(0x300)
    text[0x40:0x40 + len(small)] = small
    real = reconcile_like()
    text[0x40 + len(small):0x40 + len(small) + len(real)] = real          # right after it
    text = bytes(text)
    starts = [VADDR + 0x40, VADDR + 0x40 + len(small)]
    read_va = lambda va, n: text[va - VADDR:va - VADDR + n]
    py_status, py_rva, py_info = drb.locate(text, VADDR, starts, read_va)
    rva, sites, cands, kept, disp = cxx(exe, text, starts)
    check(py_status == "UNIQUE" and py_rva == starts[1], "adjacent small poster: Python keeps only the real one (%s)" % py_status)
    check(rva == starts[1] and kept == 1, "adjacent small poster: C++ keeps only the real one (kept %d)" % kept)

    # no function table at all -> every site is unplaced -> 0, no crash
    text, starts, read_va = layout(reconcile_like())
    rva, sites, cands, kept, disp = cxx(exe, text, [])
    check(rva == 0 and sites == 1 and cands == 0, "no table entries -> 0 (sites %d, fns %d)" % (sites, cands))

    # the callback number is the SDK's, named, and the pattern bytes follow from it
    core = open(os.path.join(REPO, "src", "reconcile_anchor_core.hpp")).read()
    check("kLicensesUpdatedCallback = 125" in core and "0x6A, kLicensesUpdatedCallback, 0x50, 0xE8" in core,
          "callback id is the named SDK constant 125, not a bare 0x7d")
    check(drb.CALLBACK_POST == b"\x6A\x7D\x50\xE8", "Python deriver anchors on the same bytes")

    # and the runtime wires it as the THIRD resolver, after feed and pattern
    lr = open(os.path.join(REPO, "src", "license_reconcile.cpp")).read()
    i_feed = lr.index('RvaFeed::Resolve("Reconcile")')
    i_pat = lr.index("Patterns::FindNotifyLicensesUpdatedFunction()) return p;")
    i_anc = lr.index("ReconcileAnchor::FindNotifyLicensesUpdated()")
    check(i_feed < i_pat < i_anc, "license_reconcile.cpp: feed, then pattern, then anchor(rescue)")

    print("\n%s" % ("FAILURES: %d" % fails if fails else "all ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
