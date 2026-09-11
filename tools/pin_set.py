#!/usr/bin/env python3
"""pin_set.py — mueve pins en ManifestIds del config.yaml de SLSsteam (prueba).

    python3 tools/pin_set.py                      # muestra ManifestIds
    python3 tools/pin_set.py 2875151:275796854305563747 [DEPOT:GID ...]
    python3 tools/pin_set.py --rm 2875151 [DEPOT ...]

Usa _read_manifest_ids/_write_manifest_ids de steamidra_lite (hace .bak).
SLSsteam recarga el config solo (inotify); el objetivo de la prueba es ver si
Steam se entera del cambio de gid sin reiniciar.
"""
import os, sys, time
from pathlib import Path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import steamidra_lite as s  # noqa: E402

cfg = Path.home() / ".config/SLSsteam/config.yaml"
ids = s._read_manifest_ids(cfg)
args = sys.argv[1:]
if not args:
    print(f"ManifestIds en {cfg}:")
    for d, g in sorted(ids.items()):
        print(f"  {d}: {g}")
    if not ids:
        print("  <vacio>")
    sys.exit(0)
if args[0] == "--rm":
    for d in args[1:]:
        ids.pop(int(d), None)
else:
    for a in args:
        d, g = a.split(":")
        ids[int(d)] = int(g)
s._write_manifest_ids(cfg, ids)
print(f"[{time.strftime('%H:%M:%S')}] ManifestIds escrito:")
for d, g in sorted(ids.items()):
    print(f"  {d}: {g}")
