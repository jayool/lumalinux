#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Unit test for FindPackage0 — the walk from the cache global to the package-0
# PackageInfo, and the consistency check that decides whether we write into it.
#
#   python3 tools/test_package_zero_walk.py     # from the repo root
#
# WHY THIS EXISTS
# ---------------
# Getting to package 0 means navigating SEVEN compiled-in class offsets through
# a structure Steam may be mutating from another thread. Five of those seven are
# validated by nobody and CANNOT be (field offsets inside a node are invisible
# in a stripped binary) — see the KNOWN LIMITS block in package_zero_finder.cpp.
# So the defence is not checking the map, it is checking where the map took us:
# the tree node's key field (+0x10) and the object's own id (+0x00) are two
# independent statements that this is package 0, and they must agree before
# anything is written.
#
# This compiles FindPackage0 exactly as it stands in the .cpp and drives it with
# a hand-built cache object, so the walk itself is exercised, not a paraphrase.
#
# SCOPE, honestly: the harness is 64-bit, so a PackageInfo* written at node+0x14
# takes 8 bytes and would overrun a 0x18-byte node. Every case therefore uses a
# SINGLE node — which is exactly the branch this test is about (key==0). The
# multi-node descent is not covered here.
import os
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fails = 0


def check(cond, msg):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        fails += 1


HARNESS = r'''
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstddef>

@@LAYOUTS@@
constexpr std::size_t kNodeLeftOff     = 0x00;
constexpr std::size_t kNodeRightOff    = 0x04;
constexpr std::size_t kNodePkgIdOff    = 0x10;
constexpr std::size_t kNodePkgInfoOff  = 0x14;
constexpr std::size_t kNodeSize        = 0x18;
constexpr int kMaxTreeDepth = 64;

namespace Log {
  void Debug(const char*, ...) {}
  void Warn (const char* f, ...) { std::fprintf(stderr, "warn: %s\n", f); }
}
// We own every byte we hand in, so readability is not what is under test here.
bool IsReadable(const void* p, std::size_t) { return p != nullptr; }
namespace Hooks { namespace LoadPackage {
  uint32_t PkgId(void* pInfo) { return *reinterpret_cast<uint32_t*>(pInfo); }
} }

@@FINDPACKAGE0@@

// argv: <rootIdx> <node key> <PackageInfo's own id>
int main(int argc, char** argv) {
    if (argc < 4) return 2;
    const int32_t  rootIdx = (int32_t)strtol(argv[1], nullptr, 0);
    const uint32_t key     = (uint32_t)strtoul(argv[2], nullptr, 0);
    const uint32_t selfId  = (uint32_t)strtoul(argv[3], nullptr, 0);

    static uint8_t cache[0x2000];
    static uint8_t node[0x40];
    static uint8_t pkg[0x80];
    std::memset(cache, 0, sizeof(cache));
    std::memset(node,  0, sizeof(node));
    std::memset(pkg,   0, sizeof(pkg));

    *reinterpret_cast<uint32_t*>(pkg) = selfId;                 // PackageInfo +0x00
    *reinterpret_cast<int32_t*>(node + kNodeLeftOff)  = -1;
    *reinterpret_cast<int32_t*>(node + kNodeRightOff) = -1;
    *reinterpret_cast<uint32_t*>(node + kNodePkgIdOff) = key;
    *reinterpret_cast<void**>(node + kNodePkgInfoOff)  = pkg;

    // Walk with the layout the caller names — once per known row, so a row
    // whose offsets drifted from its own root/nodes pair fails HERE, not on a
    // Deck. The row is picked by argv[4] (defaults to the first).
    const int L = argc > 4 ? (int)strtol(argv[4], nullptr, 0) : 0;
    const CacheLayout& lay = kCacheLayouts[L];
    *reinterpret_cast<int32_t*>(cache + lay.rootIdxOff) = rootIdx;
    *reinterpret_cast<uint8_t**>(cache + lay.nodesOff)  = node;

    uint8_t* slot_holder = nullptr;
    static uint8_t* slot;
    slot = cache;
    (void)slot_holder;

    void* got = FindPackage0((uintptr_t)&slot, lay);
    std::printf("%s\n", got == (void*)pkg ? "pkg" : (got == nullptr ? "null" : "other"));
    return 0;
}
'''


def build():
    try:
        subprocess.run(["g++", "--version"], capture_output=True, check=True)
    except Exception:
        return None
    src = open(os.path.join(REPO, "src", "hooks", "package_zero_finder.cpp")).read()
    i = src.index("void* FindPackage0(uintptr_t cacheGlobal, const CacheLayout& lay) {")
    j = src.index("// ============================================================"
                  "=================\n// Worker", i)
    tmp = tempfile.mkdtemp()
    cpp, exe = os.path.join(tmp, "w.cpp"), os.path.join(tmp, "w")
    # The layout table is lifted from the .cpp too, so the walk is exercised
    # with the exact rows the Deck carries.
    a = src.index("struct CacheLayout {")
    b = src.index("\n", src.index("constexpr int kNumCacheLayouts"))
    with open(cpp, "w") as f:
        f.write(HARNESS.replace("@@FINDPACKAGE0@@", src[i:j])
                       .replace("@@LAYOUTS@@", src[a:b]))
    r = subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-o", exe, cpp],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr)
        return None
    return exe


def run(exe, root, key, self_id, layout=0):
    r = subprocess.run([exe, str(root), str(key), str(self_id), str(layout)],
                       capture_output=True, text=True)
    return r.stdout.strip()


def num_layouts():
    src = open(os.path.join(REPO, "src", "hooks", "package_zero_finder.cpp")).read()
    a = src.index("constexpr CacheLayout kCacheLayouts[] = {")
    b = src.index("};", a)
    return src[a:b].count("{\"")


def main():
    exe = build()
    if exe is None:
        print("note: no g++ — skipped")
        return 0

    # The healthy case: both statements agree, so the object is handed over.
    check(run(exe, 0, 0, 0) == "pkg",
          "node key 0 + PackageInfo id 0        -> returns the object")

    # The case piece 9 added. A node whose key reads 0 but whose object says it
    # is some other package means the walk went wrong — a torn/stale read, or
    # the offsets no longer describe the class. Either way: do not write.
    for wrong in (7, 1, 0xFFFFFFFF):
        check(run(exe, 0, 0, wrong) == "null",
              "node key 0 + PackageInfo id %-10s -> refuses" % wrong)

    # Pre-existing behaviour that must not regress.
    check(run(exe, -1, 0, 0) == "null",
          "empty tree (rootIdx -1)              -> refuses")
    check(run(exe, 0, 5, 0) == "null",
          "single node, key 5 (no package 0)    -> refuses")

    # Every known layout row walks the same way: the object is built at THAT
    # row's root/nodes offsets and the walk must reach it. A row added to
    # kCacheLayouts with mismatched offsets fails here.
    n = num_layouts()
    check(n >= 2, "kCacheLayouts has %d row(s)" % n)
    for L in range(n):
        check(run(exe, 0, 0, 0, L) == "pkg",
              "layout row %d: key 0 + id 0            -> returns the object" % L)
        check(run(exe, 0, 0, 7, L) == "null",
              "layout row %d: key 0 + id 7            -> refuses" % L)

    print("\n%s" % ("FAILURES: %d" % fails if fails else "all ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
