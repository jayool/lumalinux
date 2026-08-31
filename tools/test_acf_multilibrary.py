#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Test for patch_acf_error_state when the user has more than one Steam library
# (SD card, second partition). Builds a synthetic Steam tree, so it needs no
# Steam, no network and no account.
#
# See LumaDeck/docs/dev-multi-library.md, defect D1.
#
# The function used to look for the manifest under `steam_root` alone, so a game
# already installed in a SECOND library was invisible and never got its stale
# error state cleared — the one job the function has. It now searches every
# library (_all_library_paths), and these checks pin that.
#
# It also used to plant an orphan stub in the root when it found nothing. That
# branch is gone — we no longer write manifests at all — so the "nothing is
# created" checks guard the removal.
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


def build(tmp, installed_in, vdf_in=("config",)):
    """installed_in: 'lib2' | 'root' | None
    vdf_in: dónde escribir libraryfolders.vdf — 'config', 'steamapps' o ambos.
    Steam CARGA el de steamapps/ y mantiene los dos sincronizados; leemos los dos."""
    root = os.path.join(tmp, "Steam")
    lib2 = os.path.join(tmp, "sdcard")
    os.makedirs(os.path.join(root, "steamapps"))
    os.makedirs(os.path.join(root, "config"))
    os.makedirs(os.path.join(lib2, "steamapps"))
    vdf_text = ('"libraryfolders"\n{\n'
                '\t"0"\n\t{\n\t\t"path"\t\t"%s"\n\t}\n'
                '\t"1"\n\t{\n\t\t"path"\t\t"%s"\n\t}\n}\n' % (root, lib2))
    for where in vdf_in:
        with open(os.path.join(root, where, "libraryfolders.vdf"), "w",
                  encoding="utf-8") as fh:
            fh.write(vdf_text)
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


def scenario(installed_in, vdf_in=("config",)):
    tmp = tempfile.mkdtemp(prefix="acf_multilib_")
    try:
        root, lib2 = build(tmp, installed_in, vdf_in)
        S.patch_acf_error_state(S.Path(root), APPID)
        return (state(os.path.join(root, "steamapps", "appmanifest_%d.acf" % APPID)),
                state(os.path.join(lib2, "steamapps", "appmanifest_%d.acf" % APPID)))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


print("patch_acf_error_state with two Steam libraries\n")

# --- the game is already installed in the SECOND library ---------------------
root_acf, lib2_acf = scenario("lib2")
check(root_acf is None,
      "game in lib2: nothing is written to the root (no seeding any more)")
check(lib2_acf is not None and lib2_acf.get("UpdateResult") == "0",
      "game in lib2: the real manifest in lib2 gets its error state cleared")
check(lib2_acf is not None and lib2_acf.get("StateFlags") == "6",
      "game in lib2: its Update-Required bit is cleared too (22 -> 6)")

# --- the library list lives in steamapps/, which is the copy Steam LOADS ----
root_acf, lib2_acf = scenario("lib2", vdf_in=("steamapps",))
check(lib2_acf is not None and lib2_acf.get("UpdateResult") == "0",
      "library list only in steamapps/libraryfolders.vdf: still found")

# --- no library list at all -> degrade to the root, never crash --------------
root_acf, lib2_acf = scenario("root", vdf_in=())
check(root_acf is not None and root_acf.get("UpdateResult") == "0",
      "no libraryfolders.vdf anywhere: the root still gets patched")

# --- control: single library, game installed there ---------------------------
root_acf, lib2_acf = scenario("root")
check(root_acf is not None and root_acf.get("UpdateResult") == "0"
      and root_acf.get("StateFlags") == "6",
      "control, game in the root: patched (UpdateResult 8->0, StateFlags 22->6)")
check(lib2_acf is None, "control, game in the root: lib2 untouched")

# --- the game is not installed anywhere -> we write nothing, Steam will ------
root_acf, lib2_acf = scenario(None)
check(root_acf is None, "not installed: no manifest is created in the root")
check(lib2_acf is None, "not installed: none in lib2 either")

print("")
if xfails:
    print("%d KNOWN-DEFECT CHECK(S) NOW PASS — update this test" % xfails)
    sys.exit(1)
if fails:
    print("%d CHECK(S) FAILED" % fails)
    sys.exit(1)
print("all checks passed")
