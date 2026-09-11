#!/usr/bin/env python3
"""snap.py APPID LABEL — foto de todo lo que un juego LumaDeck tiene en disco.
   python3 ~/gmrc/snap.py 2875150 before
   (desinstalar desde Steam)
   python3 ~/gmrc/snap.py 2875150 after      -> imprime el diff contra 'before'
"""
import glob, os, re, sys, difflib, hashlib

app = int(sys.argv[1]); label = sys.argv[2]
H = os.path.expanduser("~")
STEAM = f"{H}/.local/share/Steam"
out = []
def sec(t): out.append(f"\n### {t}")

# depots del juego: keys.txt (parent==app) + lua
keys_path = f"{H}/.config/lumalinux/keys.txt"
depots = set()
klines = []
if os.path.exists(keys_path):
    for l in open(keys_path):
        p = l.strip().split(";")
        if len(p) == 5 and p[1] == str(app):
            depots.add(int(p[0])); klines.append(l.rstrip())
        elif len(p) == 2 and p[0] == str(app):
            klines.append(l.rstrip())
lua = f"{STEAM}/config/stplug-in/{app}.lua"
lua_txt = open(lua, errors="replace").read() if os.path.exists(lua) else ""
for m in re.finditer(r"addappid\(\s*(\d+)", lua_txt):
    if int(m.group(1)) != app: depots.add(int(m.group(1)))
for m in re.finditer(r"setManifestid\(\s*(\d+)", lua_txt):
    depots.add(int(m.group(1)))
depots.add(app)

sec("keys.txt (lineas del app)"); out += klines or ["<ninguna>"]
sec("stplug-in lua"); out.append(f"{lua}: {'EXISTS %d bytes' % len(lua_txt) if lua_txt else 'MISSING'}")

sec("SLSsteam config.yaml (ManifestIds / AdditionalApps del app)")
cfg = f"{H}/.config/SLSsteam/config.yaml"
if os.path.exists(cfg):
    for l in open(cfg):
        s = l.strip()
        if any(s.startswith(f"{d}:") or s == f"- {d}" for d in depots) or s.startswith(f"{app}:") or s == f"- {app}":
            out.append(l.rstrip())
else: out.append("<no config.yaml>")

sec("depotcache manifests del app")
for d in sorted(depots):
    for f in sorted(glob.glob(f"{STEAM}/depotcache/{d}_*.manifest")):
        out.append(f"{os.path.basename(f)}  {os.path.getsize(f)}")
sec("config/depotcache manifests del app")
for d in sorted(depots):
    for f in sorted(glob.glob(f"{STEAM}/config/depotcache/{d}_*.manifest")):
        out.append(f"{os.path.basename(f)}  {os.path.getsize(f)}")

sec("config.vdf: DecryptionKeys del app + CompatToolMapping")
vdf = f"{STEAM}/config/config.vdf"
if os.path.exists(vdf):
    txt = open(vdf, errors="replace").read()
    out.append(f"config.vdf size={len(txt)} sha={hashlib.sha1(txt.encode()).hexdigest()[:10]}")
    for d in sorted(depots):
        m = re.search(r'"%d"\s*\{\s*"DecryptionKey"\s*"([0-9a-fA-F]+)"' % d, txt)
        out.append(f"  depot {d}: {'key ' + m.group(1)[:8] + '…' if m else 'NO KEY'}")
    m = re.search(r'"%d"\s*\{[^}]*"name"\s*"([^"]*)"' % app, txt[txt.find('"CompatToolMapping"'):] if '"CompatToolMapping"' in txt else "")
    out.append(f"  CompatToolMapping {app}: {m.group(1) if m else 'none'}")
else: out.append("<no config.vdf>")

sec("appmanifest .acf en todas las librerias")
for acf in glob.glob(f"{STEAM}/steamapps/appmanifest_{app}.acf") + glob.glob(f"/run/media/*/*/steamapps/appmanifest_{app}.acf") + glob.glob(f"{H}/**/steamapps/appmanifest_{app}.acf", recursive=True):
    t = open(acf, errors="replace").read()
    sf = re.search(r'"StateFlags"\s*"(\d+)"', t); b = re.search(r'"buildid"\s*"(\d+)"', t)
    out.append(f"{acf}: StateFlags={sf.group(1) if sf else '?'} buildid={b.group(1) if b else '?'}")
    for m in re.finditer(r'"(\d+)"\s*\{\s*"manifest"\s*"(\d+)"', t):
        out.append(f"  InstalledDepot {m.group(1)} -> {m.group(2)}")
if not out[-1].startswith("/") and not out[-1].startswith("  "): out.append("<ningun .acf>")

txt = "\n".join(out) + "\n"
os.makedirs(f"{H}/gmrc", exist_ok=True)
path = f"{H}/gmrc/snap_{app}_{label}.txt"
open(path, "w").write(txt)
print(txt)
print(f"\n== guardado en {path}")
if label != "before":
    before = f"{H}/gmrc/snap_{app}_before.txt"
    if os.path.exists(before):
        print("\n========== DIFF before -> " + label + " ==========")
        for l in difflib.unified_diff(open(before).read().splitlines(), txt.splitlines(), "before", label, lineterm="", n=1):
            print(l)
