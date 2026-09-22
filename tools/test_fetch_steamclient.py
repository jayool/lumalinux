#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Unit test for fetch_steamclient.py's --output path (extract_steamclient), with
# synthetic client packages so it needs no network.
#
#   python3 tools/test_fetch_steamclient.py     # from the repo root
#
# WHY THIS EXISTS
# ---------------
# On 2026-09-22 the CDN answered a 504 for the beta's bins package. The script
# moved on to the next package, found the Steamworks SDK's linux32/steamclient.so
# there (51e450e2…, not the client), extracted it and exited 0. Everything
# downstream then measured the wrong binary. This pins the fix:
#   1. only ubuntu12_32/steamclient.so is accepted — a linux32/ or 64-bit copy
#      never is, whatever package it sits in;
#   2. a download failure is retried with backoff and then succeeds silently;
#   3. a bins package that stays unreachable ends in a RuntimeError naming it,
#      with no output file and no later package downloaded;
#   4. a manifest with no bins package and only an SDK copy is "not found".
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


CLIENT = b"\x7fELF-32-client"
SDK = b"\x7fELF-sdk-linux32"
PACKAGES = {
    "bins_ubuntu12": zip_bytes({
        "ubuntu12_32/steamclient.so": CLIENT,
        "ubuntu12_64/steamclient.so": b"\x7fELF-64-client",
    }),
    "bins_misc_ubuntu12": zip_bytes({"ubuntu12_32/libvstdlib_s.so": b"x"}),
    # rank 3 — the SDK package: carries a steamclient.so that is NOT the client.
    "steamworks_sdk": zip_bytes({"linux32/steamclient.so": SDK}),
}


class Net:
    """Fake CDN: `fail[key]` = how many attempts fail before it serves."""

    def __init__(self, fail=None):
        self.fail = dict(fail or {})
        self.downloaded = []
        self.slept = []

    def http_get(self, url, timeout=120):
        key = url.rsplit("/", 1)[-1][:-4]
        self.downloaded.append(key)
        if self.fail.get(key, 0) > 0:
            self.fail[key] -= 1
            raise OSError("HTTP Error 504: Gateway Time-out")
        return PACKAGES[key]

    def install(self, keys):
        fs.package_entries = lambda man: [(k, k + ".zip", None) for k in keys]
        fs.http_get = self.http_get
        fs.RETRY_SLEEP = self.slept.append


def out_path():
    return os.path.join(tempfile.mkdtemp(), "steamclient.so")


def main():
    order = sorted(PACKAGES, key=fs._package_rank)

    # ── 1. happy path: the bins package serves the client ───────────────────
    net = Net()
    net.install(order)
    out = out_path()
    member = fs.extract_steamclient({}, out)
    check(member == "ubuntu12_32/steamclient.so", "member is %s" % member)
    check(open(out, "rb").read() == CLIENT, "the extracted bytes are the client")
    check(net.downloaded == ["bins_ubuntu12"], "only the bins package is downloaded: %s" % net.downloaded)
    check(net.slept == [], "no retry, no sleep")

    # ── 2. transient failure: two 504s, then it serves ──────────────────────
    net = Net(fail={"bins_ubuntu12": 2})
    net.install(order)
    out = out_path()
    member = fs.extract_steamclient({}, out)
    check(member == "ubuntu12_32/steamclient.so", "after two failed attempts the client is extracted")
    check(net.downloaded == ["bins_ubuntu12"] * 3, "three attempts on the same package: %s" % net.downloaded)
    check(net.slept == [2, 4], "backoff 2s then 4s: %s" % net.slept)

    # ── 3. the bins package stays down: loud failure, no SDK fallback ───────
    net = Net(fail={"bins_ubuntu12": 99})
    net.install(order)
    out = out_path()
    try:
        fs.extract_steamclient({}, out)
        check(False, "an unreachable bins package must raise")
    except RuntimeError as e:
        check("bins_ubuntu12" in str(e) and "not falling back" in str(e),
              "raises naming the package: %s" % e)
    check(not os.path.exists(out), "no output file is written")
    check("steamworks_sdk" not in net.downloaded,
          "the SDK package is never downloaded: %s" % net.downloaded)
    check(net.downloaded.count("bins_ubuntu12") == fs.RETRIES,
          "exactly RETRIES attempts (%d)" % fs.RETRIES)

    # ── 4. no bins package at all: the SDK copy is not accepted ─────────────
    net = Net()
    net.install(["steamworks_sdk"])
    out = out_path()
    try:
        fs.extract_steamclient({}, out)
        check(False, "a manifest with only linux32/steamclient.so must raise")
    except RuntimeError as e:
        check("not found" in str(e), "linux32/steamclient.so is not the client: %s" % e)
    check(not os.path.exists(out), "nothing extracted from the SDK package")

    # ── 5. a bins package with only the 64-bit copy is not accepted either ──
    PACKAGES["bins_only64"] = zip_bytes({"ubuntu12_64/steamclient.so": b"\x7fELF-64"})
    net = Net()
    net.install(["bins_only64"])
    try:
        fs.extract_steamclient({}, out_path())
        check(False, "64-bit only must raise")
    except RuntimeError as e:
        check("not found" in str(e), "ubuntu12_64/steamclient.so is not accepted")

    print("\n%s" % ("FAILURES: %d" % fails if fails else "all ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
