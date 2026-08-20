#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Unit test for the release picker inside setup.sh's resolve_cloudredirect_asset().
#
# Why this test exists: that awk only runs in the rare case — when CloudRedirect's
# newest release has no Linux build — so it can rot for months without anyone
# noticing, and the fast path around it will keep working the whole time. It also
# parses a third party's API response by shape, which is exactly the kind of thing
# that breaks quietly when the other side changes.
#
# The awk is EXTRACTED FROM setup.sh rather than copied here, so this tests the
# program that actually ships. A copy would drift and pass while the real one
# broke.
#
# The fixtures are faithful in both halves:
#   * Format — GitHub's API response is byte-for-byte json.dumps(indent=2)
#     (verified against a real api.github.com response), which is what
#     _api_json() emits. The awk keys off that layout, so it matters.
#   * Data — the asset lists are the real ones, measured asset by asset against
#     GitHub on 2026-08-20. v2.6.2, v2.6.1, v2.5.3 and v2.5.2 genuinely publish
#     CloudRedirect.exe and no cloud_redirect.so: two separate runs of two
#     consecutive Windows-only releases, which is the whole reason
#     /releases/latest is not enough on its own.
#
#   python3 tools/test_cloudredirect_release_pick.py     # from anywhere
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

SETUP_SH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "setup.sh")
ASSET = "cloud_redirect.so"
OWNER_REPO = "Selectively11/CloudRedirect"

fails = 0


def check(cond, msg):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        fails += 1


def extract_awk() -> str:
    """The awk program out of setup.sh, so this tests what ships."""
    src = open(SETUP_SH, encoding="utf-8").read()
    m = re.search(r"""\| awk -v asset="\$CR_SO_ASSET" '(.*?)\n    '\)""", src, re.S)
    if not m:
        print("FAIL could not find the awk in setup.sh — did resolve_cloudredirect_asset change?")
        sys.exit(1)
    return m.group(1)


def api_json(releases) -> str:
    """A GitHub /releases response. `releases` is [(tag, [asset names])] newest
    first, or (tag, assets, {"draft": True}) to set a flag."""
    out = []
    for entry in releases:
        tag, assets = entry[0], entry[1]
        flags = entry[2] if len(entry) > 2 else {}
        out.append({
            "tag_name": tag,
            "draft": flags.get("draft", False),
            "prerelease": flags.get("prerelease", False),
            "body": flags.get("body", ""),
            "html_url": f"https://github.com/{OWNER_REPO}/releases/tag/{tag}",
            "assets": [{
                "name": name,
                "browser_download_url":
                    f"https://github.com/{OWNER_REPO}/releases/download/{tag}/{name}",
            } for name in assets],
        })
    return json.dumps(out, indent=2) + "\n"


def pick(awk_program: str, payload: str, asset: str = ASSET) -> str:
    """Run the extracted awk exactly as setup.sh pipes into it."""
    proc = subprocess.run(
        ["awk", "-v", f"asset={re.escape(asset)}", awk_program],
        input=payload, capture_output=True, text=True,
    )
    return proc.stdout.strip()


def url(tag: str, asset: str = ASSET) -> str:
    return f"https://github.com/{OWNER_REPO}/releases/download/{tag}/{asset}"


# Real asset layout, measured against GitHub 2026-08-20. W = Windows-only.
WIN = ["CloudRedirect.exe", "cloud_redirect.dll"]
BOTH = WIN + ["cloud_redirect.so", "cloudredirect.flatpak"]
REAL = [
    ("v2.6.5", BOTH), ("v2.6.4", BOTH), ("v2.6.3", BOTH),
    ("v2.6.2", WIN),  ("v2.6.1", WIN),                      # W run #1
    ("v2.6.0", BOTH), ("v2.5.4", BOTH),
    ("v2.5.3", WIN),  ("v2.5.2", WIN),                      # W run #2
    ("v2.5.1", BOTH),
]


def main() -> int:
    if not shutil.which("awk"):
        print("FAIL awk not found — setup.sh needs it too")
        return 1
    awk = extract_awk()

    check(pick(awk, api_json(REAL)) == url("v2.6.5"),
          "newest release ships the asset -> takes it")

    # The state that actually occurred while v2.6.2 was the newest release.
    check(pick(awk, api_json(REAL[3:])) == url("v2.6.0"),
          "two consecutive Windows-only releases -> walks back past both")

    check(pick(awk, api_json(REAL[7:])) == url("v2.5.1"),
          "the second Windows-only run -> walks back past both")

    check(pick(awk, api_json([("v2.6.5", BOTH, {"prerelease": True})] + REAL[1:])) == url("v2.6.4"),
          "a prerelease at the head is skipped")

    check(pick(awk, api_json([("v2.6.5", BOTH, {"draft": True})] + REAL[1:])) == url("v2.6.4"),
          "a draft at the head is skipped")

    # A flag must not leak past its own release: v2.6.5 draft, v2.6.4 normal.
    check(pick(awk, api_json([("v2.6.5", BOTH, {"draft": True}), ("v2.6.4", BOTH)])) == url("v2.6.4"),
          "draft/prerelease state does not carry into the next release")

    check(pick(awk, api_json([("v2.6.2", WIN), ("v2.6.1", WIN)])) == "",
          "no release ships it -> nothing (setup.sh then dies with a message)")

    # cloud_redirect_cli is not published in any release; it comes from the
    # flatpak. Asking for it must not match cloud_redirect.so by prefix.
    check(pick(awk, api_json(REAL), "cloud_redirect_cli") == "",
          "an asset that exists in no release -> nothing, and no prefix match")

    # A release body is one long JSON string on a single line, and bodies quote
    # asset URLs often enough (generate_release_notes pulls in commit and PR
    # text) that a Windows-only release can carry a .so URL in its notes. Picking
    # that up would hand everyone the wrong build.
    #
    # What actually prevents it is JSON escaping, not the strict line match: the
    # inner quotes arrive as \" so nothing in a body can look like a real field.
    # The invariant is worth pinning anyway — it is the shape of input most
    # likely to be mistaken for an asset.
    poisoned = [("v2.6.2", WIN, {"body": (
        'See the Linux build: "browser_download_url": '
        f'"{url("v9.9.9")}" for details'
    )})] + REAL[5:]
    check(pick(awk, api_json(poisoned)) == url("v2.6.0"),
          "an asset url quoted inside a release body is not mistaken for an asset")

    # If GitHub ever stops pretty-printing, the strict url line stops matching and
    # we get nothing — a clear failure, not a lucky wrong answer.
    compact = json.dumps(json.loads(api_json(REAL)), separators=(",", ":"))
    check(pick(awk, compact) == "",
          "unexpected (compact) JSON -> nothing rather than a wrong guess")

    print()
    print("FAILED" if fails else "all good")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
