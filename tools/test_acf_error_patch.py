#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Tests for patch_acf_error_state — it runs when Steam already wrote an
# appmanifest and clears its stale error state. It never creates one.
#
# See LumaDeck/docs/dev-multi-library.md, defect D5. Three properties:
#   1. Real error residue IS cleared (UpdateResult, the Bytes* counters, and the
#      Update-Required bit).
#   2. Steam's own scheduled work is NOT touched — ScheduledAutoUpdate is a
#      future appointment and FullValidateAfterNextUpdate is an instruction Steam
#      left itself. Zeroing them cancels real work. Both were measured carrying
#      non-zero values on real libraries (Halo: Campaign Evolved 2806050 and
#      Steamworks Common Redistributables 228980).
#   3. A healthy manifest is left BYTE-IDENTICAL and gets no .acf.bak. A key that
#      is simply absent is not an error to clean: 12 of 13 real manifests carry
#      no FullValidateAfterNextUpdate, so treating "missing" as "wrong" rewrote
#      Steam's file on every add of an already-installed game.
#
#   python3 tools/test_acf_error_patch.py     # from the repo root
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import steamidra_lite as S  # noqa: E402

APPID = 480
fails = 0


def check(cond, msg):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        fails += 1


def run(app_state):
    """Write app_state as an .acf, patch it, return (result, before, after, bak)."""
    tmp = tempfile.mkdtemp(prefix="acf_patch_")
    try:
        acf = S.Path(tmp) / "steamapps" / ("appmanifest_%d.acf" % APPID)
        acf.parent.mkdir(parents=True)
        S._vdf_dump_acf(acf, {"AppState": app_state})
        before = acf.read_text(encoding="utf-8")
        result = S.patch_acf_error_state(S.Path(tmp), APPID)
        after = acf.read_text(encoding="utf-8")
        bak = acf.with_suffix(".acf.bak").exists()
        return result, before, after, bak, S._vdf_load_acf(acf).get("AppState", {})
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


print("patch_acf_error_state\n")

# --- 1. residuo de error real: se limpia ------------------------------------
res, _, _, bak, st = run({
    "appid": str(APPID), "name": "Fake",
    "StateFlags": "22",            # 4 installed | 2 | 16 update-required
    "installdir": "Fake",
    "SizeOnDisk": "12345678",
    "UpdateResult": "8",
    "BytesToDownload": "9999", "BytesDownloaded": "1234",
})
check(res == "patched", "stuck error state: reports 'patched'")
check(st.get("UpdateResult") == "0", "  UpdateResult 8 -> 0")
check(st.get("BytesToDownload") == "0" and st.get("BytesDownloaded") == "0",
      "  Bytes* counters zeroed")
check(st.get("StateFlags") == "6", "  Update-Required bit (16) cleared, 22 -> 6")
check(bak, "  a .acf.bak is written when we actually change the file")

# --- 2. el trabajo programado por Steam: NO se toca -------------------------
res, before, after, bak, st = run({
    "appid": str(APPID), "name": "Fake",
    "StateFlags": "6",
    "installdir": "Fake",
    "SizeOnDisk": "12345678",
    "UpdateResult": "0",
    "BytesToDownload": "0", "BytesDownloaded": "0",
    "ScheduledAutoUpdate": "1788144179",       # a real future appointment
    "FullValidateAfterNextUpdate": "1",        # a real pending instruction
})
check(st.get("ScheduledAutoUpdate") == "1788144179",
      "scheduled auto-update survives (we must not cancel Steam's appointment)")
check(st.get("FullValidateAfterNextUpdate") == "1",
      "pending full-validate survives (we must not cancel Steam's instruction)")
check(res == "clean" and before == after,
      "  and with nothing else wrong the file is untouched")

# --- 3. manifest sano: ni se reescribe ni deja .bak -------------------------
res, before, after, bak, _ = run({
    "appid": str(APPID), "name": "Fake",
    "StateFlags": "4",
    "installdir": "Fake",
    "SizeOnDisk": "12345678",
    # sin UpdateResult, sin Bytes*, sin FullValidateAfterNextUpdate — como las
    # fichas reales de Steam, que a menudo no traen esas claves
})
check(res == "clean", "healthy manifest missing the optional keys: reports 'clean'")
check(before == after, "  file left byte-identical (no round-trip through our writer)")
check(not bak, "  no .acf.bak litter")

# --- 4. sin .acf: no se crea nada -------------------------------------------
tmp = tempfile.mkdtemp(prefix="acf_none_")
try:
    (S.Path(tmp) / "steamapps").mkdir(parents=True)
    res = S.patch_acf_error_state(S.Path(tmp), APPID)
    made = list((S.Path(tmp) / "steamapps").glob("appmanifest_*.acf"))
    check(res.startswith("none"), "no .acf at all: reports 'none', writes nothing")
    check(not made, "  and the steamapps dir is still empty (Steam seeds it, not us)")
finally:
    shutil.rmtree(tmp, ignore_errors=True)

print("")
if fails:
    print("%d CHECK(S) FAILED" % fails)
    sys.exit(1)
print("all checks passed")
