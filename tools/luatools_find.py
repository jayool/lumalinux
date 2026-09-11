#!/usr/bin/env python3
"""luatools_find.py — juegos del catálogo de lua.tools con fix de un autor (p.ej.
voices38) que tengan versión (manifest), ordenados por tamaño del depot Windows.

    python3 tools/luatools_find.py voices38
    python3 tools/luatools_find.py voices38 --max 40

Primero intenta endpoints de listado global; si no hay, saca los appids de la
página https://lua.tools/fixes y pregunta fix a fix (endpoint público).
Tamaño: suma de los depots Windows públicos según api.steamcmd.net.
"""
import json, re, sys, time, urllib.request

UA = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/128 Safari/537.36"
who = (sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("--") else "voices38").lower()
MAX = int(sys.argv[sys.argv.index("--max") + 1]) if "--max" in sys.argv else 25

def get(url, timeout=25):
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "application/json, text/plain, */*"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read() if e.fp else b""
    except Exception as e:
        return 0, str(e).encode()

# 1. listado global (a ver qué contesta)
fixes_all = None
for u in ("https://lua.tools/api/denuvo/fixes", "https://lua.tools/api/denuvo/games",
          "https://lua.tools/api/denuvo/catalog", "https://lua.tools/api/denuvo/all",
          "https://lua.tools/api/fixes"):
    st, body = get(u)
    print(f"[probe] {u} -> {st} {len(body)}B {body[:80]!r}")
    if st == 200 and body[:1] in (b"[", b"{"):
        try:
            d = json.loads(body)
            if isinstance(d, list) and d and isinstance(d[0], dict):
                fixes_all = d; break
            if isinstance(d, dict):
                for k in ("fixes", "games", "items", "data"):
                    if isinstance(d.get(k), list) and d[k]:
                        fixes_all = d[k]; break
                if fixes_all: break
        except Exception:
            pass

# 2. appids de la página
appids = []
st, html = get("https://lua.tools/fixes")
print(f"[page] https://lua.tools/fixes -> {st} {len(html)}B")
txt = html.decode("utf-8", "replace")
appids = sorted({int(x) for x in re.findall(r"/fixes/(\d+)", txt)})
apis = sorted(set(re.findall(r"/api/[A-Za-z0-9_/\-\.]+", txt)))
print(f"[page] appids en la página: {len(appids)}; APIs citadas: {apis[:10]}")
for m in re.findall(r'"(/_next/data/[^"]+\.json)"', txt)[:3]:
    print("[page] next data:", m)

# 3. fixes por app
def fix_text(f):
    return " ".join(str(x) for x in [f.get("title"), f.get("description"), f.get("author"), f.get("uploader")] + list(f.get("tags") or [])).lower()

rows = []
if fixes_all:
    print(f"[global] {len(fixes_all)} entradas; claves: {sorted(fixes_all[0].keys())[:15]}")
    by_app = {}
    for f in fixes_all:
        a = f.get("appId") or f.get("appid") or f.get("app_id")
        if a: by_app.setdefault(int(a), []).append(f)
    appids = sorted(by_app) if by_app else appids
for i, a in enumerate(appids):
    if fixes_all and by_app.get(a) and "fixes" not in by_app[a][0]:
        fx = by_app[a]
    else:
        st, body = get(f"https://lua.tools/api/denuvo/fixes?appid={a}", 15)
        if st != 200: continue
        try: fx = json.loads(body).get("fixes", [])
        except Exception: continue
        time.sleep(0.15)
    for f in fx:
        if who in fix_text(f) and f.get("hasManifest"):
            rows.append((a, f))
    if i % 25 == 0: print(f"  ... {i}/{len(appids)} apps, {len(rows)} candidatos", file=sys.stderr)

print(f"\n{len(rows)} fixes de '{who}' con manifest. Calculando tamaños (steamcmd.net)...")
out = []
seen = set()
for a, f in rows:
    if a in seen: continue
    seen.add(a)
    st, body = get(f"https://api.steamcmd.net/v1/info/{a}", 20)
    name, size, build = f"app {a}", 0, "?"
    if st == 200:
        try:
            d = json.loads(body)["data"][str(a)]
            name = (d.get("common") or {}).get("name", name)
            build = ((d.get("depots") or {}).get("branches") or {}).get("public", {}).get("buildid", "?")
            for did, dep in (d.get("depots") or {}).items():
                if did.isdigit() and isinstance(dep, dict):
                    cfg = dep.get("config") or {}
                    pub = (dep.get("manifests") or {}).get("public") or {}
                    if cfg.get("oslist", "") in ("", "windows") and not dep.get("dlcappid") and str(dep.get("sharedinstall", "0")) != "1":
                        size += int(pub.get("size") or 0)
        except Exception: pass
    out.append((size, a, name, build, f))
out.sort()
print(f"\n{'GB':>6}  {'appid':>8}  {'valve build':>11}  {'fix build':>10}  name  [fix title]")
for size, a, name, build, f in out[:MAX]:
    tag = re.search(r"(\d{6,})", str(f.get("title", "")))
    print(f"{size/1e9:6.1f}  {a:>8}  {build:>11}  {tag.group(1) if tag else '?':>10}  {name}  [{f.get('title')}]")
