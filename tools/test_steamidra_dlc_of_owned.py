#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# --dlc-of-owned: the account owns the base game, so AdditionalApps gets the
# DLC AppIDs (the keyless addappid lines) and NOT the base AppID, keys.txt and
# depotcache get the DLC depots exactly as in the normal flow, and the base
# game's .acf is left alone. Without the flag the base AppID goes in, as always.
# RESEARCH §21 run E is the measured shape this reproduces.
#
#   python3 tools/test_steamidra_dlc_of_owned.py     # from the repo root
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import steamidra_lite as S  # noqa: E402

APP = 262060
DLC_A, DLC_B = 702540, 580100
DEP_A, DEP_B = 702542, 580102
KEY = "ab" * 32

LUA = f"""addappid({APP})
addappid({DLC_A})
addappid({DEP_A},1,"{KEY}")
setManifestid({DEP_A},"4258374143576351227",40024569)
addappid({DLC_B})
addappid({DEP_B},1,"{KEY}")
setManifestid({DEP_B},"1111111111111111111",123)
"""


class DlcOfOwned(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        # Keep anything that resolves Path.home() inside the sandbox.
        self._home = os.environ.get("HOME")
        os.environ["HOME"] = str(self.tmp)
        self.root = self.tmp / "Steam"
        (self.root / "depotcache").mkdir(parents=True)
        (self.root / "steamapps").mkdir()
        self.acf = self.root / "steamapps" / f"appmanifest_{APP}.acf"
        self.acf.write_text('"AppState"\n{\n\t"appid"\t\t"262060"\n\t"UpdateResult"\t\t"4"\n}\n')
        self.acf_before = self.acf.read_bytes()
        self.cfg = self.tmp / "config.yaml"
        self.cfg.write_text("AdditionalApps:\nAppTokens:\nManifestIds:\n")
        self.keys = self.tmp / "keys.txt"
        self.mdir = self.tmp / "manifests"
        self.mdir.mkdir()
        for d, g in ((DEP_A, "4258374143576351227"), (DEP_B, "1111111111111111111")):
            (self.mdir / f"{d}_{g}.manifest").write_bytes(b"\x00" * 16)
        self.lua = self.tmp / f"{APP}.lua"
        self.lua.write_text(LUA)

    def tearDown(self):
        if self._home is not None:
            os.environ["HOME"] = self._home

    def run_main(self, *extra):
        argv = ["steamidra_lite.py", str(self.lua), "--manifests-dir", str(self.mdir),
                "--steam-root", str(self.root), "--sls-config", str(self.cfg),
                "--luma-keys", str(self.keys), "--no-vdf", *extra]
        old = sys.argv
        sys.argv = argv
        try:
            S.main()
        finally:
            sys.argv = old

    def additional_apps(self):
        out, inside = [], False
        for line in self.cfg.read_text().splitlines():
            if line.startswith("AdditionalApps:"):
                inside = True
                continue
            if inside and line.strip().startswith("- "):
                out.append(int(line.strip()[2:].split()[0]))
            elif inside and line and not line.startswith(" "):
                inside = False
        return sorted(out)

    def test_owned_registers_dlc_apps_not_base(self):
        self.run_main("--dlc-of-owned")
        self.assertEqual(self.additional_apps(), sorted([DLC_A, DLC_B]))
        keys = self.keys.read_text()
        self.assertIn(f"{DEP_A};{APP};", keys)
        self.assertIn(f"{DEP_B};{APP};", keys)
        self.assertTrue(any(p.name.startswith(f"{DEP_A}_") for p in (self.root / "depotcache").iterdir()))
        self.assertEqual(self.acf.read_bytes(), self.acf_before, ".acf of the owned game must not be touched")

    def test_owned_flag_only_dlc_writes_no_keys(self):
        # A game whose DLC carry no depots (LumaDeck dropped every keyed line
        # because they all belonged to the base game): AdditionalApps gets the
        # DLC AppIDs, keys.txt gets no depot line, nothing lands in depotcache,
        # and the run does not fail. Measured need: 2026-10-06 (flag-only DLC).
        self.lua.write_text(f"addappid({APP})\naddappid({DLC_A})\naddappid({DLC_B})\n")
        for p in self.mdir.iterdir():
            p.unlink()
        self.run_main("--dlc-of-owned")
        self.assertEqual(self.additional_apps(), sorted([DLC_A, DLC_B]))
        keys = self.keys.read_text() if self.keys.exists() else ""
        self.assertNotIn(f"{DEP_A};", keys)
        self.assertNotIn(f"{DEP_B};", keys)
        self.assertEqual(list((self.root / "depotcache").iterdir()), [])
        self.assertEqual(self.acf.read_bytes(), self.acf_before)

    def test_default_registers_base(self):
        self.run_main()
        self.assertEqual(self.additional_apps(), [APP])
        self.assertNotEqual(self.acf.read_bytes(), self.acf_before, "default flow resets the .acf error state")


if __name__ == "__main__":
    unittest.main()
