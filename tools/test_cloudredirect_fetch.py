#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Unit test for setup.sh's fetch_cloudredirect_so() and the caller that decides
# what to do when it fails.
#
# Why this test exists: since upstream CloudRedirect was deleted (2026-10-16) the
# .so is shipped from our own release asset behind a sha256 pin, and a failed or
# mismatching download must NOT abort the install any more (it used to be a
# `die`). Both halves only matter on the unhappy path, which nobody exercises by
# hand, so they are pinned here.
#
# The function is EXTRACTED FROM setup.sh rather than copied, so this tests the
# program that actually ships. Downloads go through file:// URLs, so the test
# runs anywhere curl exists and never touches the network.
#
#   python3 tools/test_cloudredirect_fetch.py     # from anywhere
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SETUP = os.path.join(ROOT, "setup.sh")


def extract_function(src: str, name: str) -> str:
    m = re.search(rf"^{name}\(\) \{{\n.*?^\}}\n", src, re.S | re.M)
    if not m:
        sys.exit(f"FAIL could not find {name}() in setup.sh — did it change?")
    return m.group(0)


def run(fn: str, workdir: str, url: str, pin: str, override: bool, existing: bytes | None):
    tmp = os.path.join(workdir, "tmp"); cr = os.path.join(workdir, "cr")
    shutil.rmtree(workdir, ignore_errors=True); os.makedirs(tmp); os.makedirs(cr)
    if existing is not None:
        with open(os.path.join(cr, "cloud_redirect.so"), "wb") as fh:
            fh.write(existing)
    env_line = f'CLOUDREDIRECT_URL="{url}"\n' if override else ""
    script = f"""set -uo pipefail
info() {{ echo "INFO: $*"; }}; ok() {{ echo "OK: $*"; }}; warn() {{ echo "WARN: $*"; }}
die() {{ echo "DIE: $*"; exit 99; }}
TMP_DIR="{tmp}"; CR_DIR="{cr}"
{env_line}CR_SO_URL="{url}"; CR_SO_SHA256="{pin}"
{fn}
if ! fetch_cloudredirect_so; then
    if [[ -s "${{CR_DIR}}/cloud_redirect.so" ]]; then warn "Keeping"; else warn "No CloudRedirect"; fi
fi
echo "rc=$?"
"""
    p = subprocess.run(["bash", "-c", script], capture_output=True, text=True)
    installed = None
    path = os.path.join(cr, "cloud_redirect.so")
    if os.path.exists(path):
        with open(path, "rb") as fh:
            installed = fh.read()
    return p.stdout + p.stderr, installed


def main() -> int:
    with open(SETUP, encoding="utf-8") as fh:
        fn = extract_function(fh.read(), "fetch_cloudredirect_so")
    base = tempfile.mkdtemp()
    payload = b"\x7fELF-not-really-" + b"x" * 64
    good = os.path.join(base, "good.so")
    with open(good, "wb") as fh:
        fh.write(payload)
    pin = hashlib.sha256(payload).hexdigest()
    good_url = "file://" + good
    bad_url = "file://" + os.path.join(base, "missing.so")
    work = os.path.join(base, "w")
    failures = 0

    def check(label, cond):
        nonlocal failures
        print(("ok   " if cond else "FAIL ") + label)
        if not cond:
            failures += 1

    out, inst = run(fn, work, good_url, pin, False, None)
    check("pinned download installs", inst == payload and "OK: Deployed" in out)
    check("no die on the happy path", "DIE" not in out and "rc=0" in out)

    out, inst = run(fn, work, bad_url, pin, False, None)
    check("failed download, nothing on disk -> warns and continues",
          inst is None and "No CloudRedirect" in out and "rc=0" in out and "DIE" not in out)

    out, inst = run(fn, work, bad_url, pin, False, b"OLD")
    check("failed download keeps the installed .so", inst == b"OLD" and "Keeping" in out)

    out, inst = run(fn, work, good_url, "0" * 64, False, b"OLD")
    check("sha mismatch is refused and keeps the installed .so",
          inst == b"OLD" and "does not match the pinned sha256" in out and "Keeping" in out)

    out, inst = run(fn, work, good_url, "0" * 64, True, None)
    check("CLOUDREDIRECT_URL override skips the pin", inst == payload and "OK: Deployed" in out)

    shutil.rmtree(base, ignore_errors=True)
    print("\nall ok" if not failures else f"\n{failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
