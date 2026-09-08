#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Unit test for fetch_steamclient.py's --extract-tree, with synthetic client
# packages so it needs no network.
#
#   python3 tools/test_fetch_tree.py     # from the repo root
#
# WHY THIS EXISTS
# ---------------
# The runtime smoke test used to run against a 329 MB tarball pinned in the
# test-deps-v1 release (uploaded 2026-06-13, never refreshed) with only
# steamclient.so swapped for the current one — a three-month-old tree carrying a
# brand-new client library. extract_client_tree() pulls the WHOLE ubuntu12_32
# tree from the same manifest steamclient.so comes from, so the tree is current
# and self-consistent. This pins the parts that are easy to get wrong: which
# members are taken, where they land, and that the 64-bit tree is excluded.
import io
import os
import sys
import tempfile
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fetch_steamclient as fs  # noqa: E402

fails = 0


def check(cond, msg):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        fails += 1


def zip_bytes(members):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        for name, data in members.items():
            z.writestr(name, data)
    return buf.getvalue()


PACKAGES = {
    # rank 0 — the bins package: the 32-bit client tree, plus a 64-bit sibling
    # that must NOT be taken, plus a nested path that must keep its shape.
    "bins_ubuntu12": zip_bytes({
        "ubuntu12_32/steamclient.so": b"\x7fELF-32-client",
        "ubuntu12_32/libtier0_s.so": b"\x7fELF-tier0",
        "ubuntu12_32/steam": b"\x7fELF-binary",
        "ubuntu12_32/subdir/extra.txt": b"plain",
        "ubuntu12_64/steamclient.so": b"\x7fELF-64-client",
    }),
    # rank 1 — a sibling bins package with one more 32-bit library.
    "bins_misc_ubuntu12": zip_bytes({
        "ubuntu12_32/libvstdlib_s.so": b"\x7fELF-vstdlib",
    }),
    # rank 3 — a huge unrelated blob that must never be downloaded once the
    # client library has been found.
    "steamrt_scout": zip_bytes({"runtime/huge.bin": b"x" * 64}),
}

downloaded = []


def fake_entries(man):
    return [(k, k + ".zip", None) for k in
            sorted(PACKAGES, key=lambda k: fs._package_rank(k))]


def fake_http_get(url, timeout=120):
    key = url.rsplit("/", 1)[-1][:-4]
    downloaded.append(key)
    return PACKAGES[key]


def main():
    fs.package_entries = fake_entries
    fs.http_get = fake_http_get

    out = tempfile.mkdtemp()
    n, names = fs.extract_client_tree({}, out)

    got = sorted(names)
    want = sorted(["ubuntu12_32/steamclient.so", "ubuntu12_32/libtier0_s.so",
                   "ubuntu12_32/steam", "ubuntu12_32/subdir/extra.txt",
                   "ubuntu12_32/libvstdlib_s.so"])
    check(got == want, "extracts exactly the 32-bit members: %s" % got)
    check(n == 5, "count is %d" % n)

    check(os.path.isfile(os.path.join(out, "ubuntu12_32", "steamclient.so")),
          "steamclient.so lands at <dir>/ubuntu12_32/steamclient.so")
    check(os.path.isfile(os.path.join(out, "ubuntu12_32", "subdir", "extra.txt")),
          "nested paths keep their shape")
    check(not os.path.exists(os.path.join(out, "ubuntu12_64")),
          "the 64-bit tree is NOT extracted")

    # The zip carries no usable mode bits; .so files and top-level binaries must
    # come out executable or the dlopen host cannot run them.
    for leaf in ("steamclient.so", "libtier0_s.so", "steam"):
        mode = os.stat(os.path.join(out, "ubuntu12_32", leaf)).st_mode
        check(bool(mode & 0o111), "%s is executable" % leaf)

    # The rank-3 blob must be skipped once the client library is in hand: that
    # is the difference between a ~40 MB download and a ~600 MB one.
    check("steamrt_scout" not in downloaded,
          "the big rank-3 blob is never downloaded (got %s)" % downloaded)

    # And it must fail loudly rather than hand back half a tree.
    fs.package_entries = lambda man: [("empty", "empty.zip", None)]
    PACKAGES["empty"] = zip_bytes({"readme.txt": b"nothing here"})
    try:
        fs.extract_client_tree({}, tempfile.mkdtemp())
        check(False, "a package set without steamclient.so must raise")
    except RuntimeError as e:
        check("steamclient.so not found" in str(e), "raises when the tree is absent")

    print("\n%s" % ("FAILURES: %d" % fails if fails else "all ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
