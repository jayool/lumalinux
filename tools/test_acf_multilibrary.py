#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Test for write_or_patch_acf's create-vs-patch decision when the user has more
# than one Steam library (SD card, second partition). Builds a synthetic Steam
# tree, so it needs no Steam, no network and no account.
#
# See LumaDeck/docs/dev-multi-library.md, defect D1.
#
# The decision is made from `steam_root` alone (:743), so a game already
# installed in a SECOND library is invisible: the function plants an orphan stub
# in the root AND skips the patch it exists to perform. The two single-library
# cases are controls and pass today.
#
# EXPECTED TO FAIL until D1 is fixed. The failing check is marked XFAIL and does
# not fail the run; when the fix lands it flips to XPASS and the run DOES fail,
# so you cannot land the fix without deleting the marker here.
#
#   python3 tools/test_acf_multilibrary.py     # from the repo root
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import steamidra_lite as S  # noqa: E402

APPID = 480
NAME = "Fake Game"

# A real, installed manifest carrying the stuck error state the patch must clear.
DIRTY = {"AppState": {
    "appid": str(APPID), "Universe": "1", "name": NAME,
    "StateFlags": "22",                 # 4 installed | 2 | 16 update-required
    "installdir": NAME,
    "LastUpdated": "1750000000",
    "SizeOnDisk": "12345678",
    "UpdateResult": "8",                # <- the stuck error
    "BytesToDownload": "9999", "BytesDownloaded": "1234",
    "BytesToStage": "0", "BytesStaged": "0", "StagingSize": "0",
    "InstalledDepots": {"481": {"manifest": "111", "size": "12345678"}},
}}

fails = 0
xfails = 0


def check(cond, msg, xfail=False):
    """xfail=True: a known defect. Reported, but only fails the run if it PASSES."""
    global fails, xfails
    if xfail:
        print(("XPASS" if cond else "xfail") + " " + msg)
        if cond:
            print("       ^ this now passes -> D1 is fixed; drop xfail=True here")
            xfails += 1
        return
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        fails += 1


def build(tmp, installed_in):
    """installed_in: 'lib2' | 'root' | None"""
    root = os.path.join(tmp, "Steam")
    lib2 = os.path.join(tmp, "sdcard")
    os.makedirs(os.path.join(root, "steamapps"))
    os.makedirs(os.path.join(root, "config"))
    os.makedirs(os.path.join(lib2, "steamapps"))
    with open(os.path.join(root, "config", "libraryfolders.vdf"), "w",
              encoding="utf-8") as fh:
        fh.write('"libraryfolders"\n{\n'
                 '\t"0"\n\t{\n\t\t"path"\t\t"%s"\n\t}\n'
                 '\t"1"\n\t{\n\t\t"path"\t\t"%s"\n\t}\n}\n' % (root, lib2))
    if installed_in:
        target = {"root": root, "lib2": lib2}[installed_in]
        S._vdf_dump_acf(
            S.Path(target) / "steamapps" / ("appmanifest_%d.acf" % APPID), DIRTY)
    return root, lib2


def state(path):
    p = S.Path(path)
    if not p.exists():
        return None
    return S._vdf_load_acf(p).get("AppState", {})


def scenario(installed_in):
    tmp = tempfile.mkdtemp(prefix="acf_multilib_")
    try:
        root, lib2 = build(tmp, installed_in)
        S.write_or_patch_acf(S.Path(root), APPID, {}, name_override=NAME)
        return (state(os.path.join(root, "steamapps", "appmanifest_%d.acf" % APPID)),
                state(os.path.join(lib2, "steamapps", "appmanifest_%d.acf" % APPID)))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


print("write_or_patch_acf with two Steam libraries\n")

# --- the defect: the game is already installed in the SECOND library ---------
root_acf, lib2_acf = scenario("lib2")
check(root_acf is None,
      "game in lib2: no orphan stub is planted in the root", xfail=True)
check(lib2_acf is not None and lib2_acf.get("UpdateResult") == "0",
      "game in lib2: the real manifest in lib2 gets patched", xfail=True)

# --- control: single library, game installed there ---------------------------
root_acf, lib2_acf = scenario("root")
check(root_acf is not None and root_acf.get("UpdateResult") == "0"
      and root_acf.get("StateFlags") == "6",
      "control, game in the root: patched (UpdateResult 8->0, StateFlags 22->6)")
check(lib2_acf is None, "control, game in the root: lib2 untouched")

# --- control: the game is not installed anywhere -> the stub is correct ------
root_acf, lib2_acf = scenario(None)
check(root_acf is not None and root_acf.get("StateFlags") == "1"
      and "InstalledDepots" not in root_acf,
      "control, not installed: a clean stub is created in the root")
check(lib2_acf is None, "control, not installed: lib2 untouched")

print("")
if xfails:
    print("%d KNOWN-DEFECT CHECK(S) NOW PASS — update this test" % xfails)
    sys.exit(1)
if fails:
    print("%d CHECK(S) FAILED" % fails)
    sys.exit(1)
print("all checks passed (D1 still open: see the xfail lines)")
