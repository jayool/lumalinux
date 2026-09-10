#!/usr/bin/env python3
"""
hubcap_fresh.py — is Hubcap a LIVE manifest source, or a cache?

With the request-code providers gone (2026-09-09), a manifest that is already
in depotcache/ is the only way Steam downloads anything. Hubcap's zips carry
manifests, and it had the 2026-09-08 Backpack Boy one — so Hubcap can mint
codes. The question that decides whether it can replace the providers is
FRESHNESS: when Valve publishes a new manifest, does Hubcap's zip carry it on
the next request (generated on demand), or a stale one (cached for days)?

For every app given (or a built-in list of frequently-updated games), this
script:
  1. reads the CURRENT public manifest gid + timeupdated per depot from
     api.steamcmd.net (a PICS mirror), and keeps the apps updated within
     --hours (default 72) — those are the only ones that can tell cache from
     live;
  2. fetches Hubcap's zip for each of those (Bearer key), timing the call;
  3. lists the <depot>_<gid>.manifest files inside and grades each content
     depot FRESH (zip gid == Valve's current), STALE (older gid) or MISSING.

The Hubcap key is read from LumaDeck's api.json if present, or pass --key.

    python3 tools/hubcap_fresh.py --key HUBCAP_KEY
    python3 tools/hubcap_fresh.py --key K --app 2875150 --app 1030300 --hours 240
    python3 tools/hubcap_fresh.py --key K --all     # fetch every app, not only recent ones
"""

import argparse
import io
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gmrc_probe as gp  # noqa: E402  (http_get, UA constants)

HUBCAP_ZIP   = "https://hubcapmanifest.com/api/v1/manifest/{app}"
HUBCAP_STATS = "https://hubcapmanifest.com/api/v1/user/stats"

# Frequently-updated, well-known titles. Only the ones Valve updated within
# --hours get fetched, so a long list costs nothing on the Hubcap side.
DEFAULT_APPS = [
    2875150,   # Backpack Boy (the 2026-09-08 zip that started this)
    2379780,   # Balatro
    1030300,   # Hollow Knight: Silksong
    1942280,   # Brotato
    1794680,   # Vampire Survivors
    1966720,   # Lethal Company
    1145350,   # Hades II
    1086940,   # Baldur's Gate 3
    1623730,   # Palworld
    105600,    # Terraria
    413150,    # Stardew Valley
    1091500,   # Cyberpunk 2077
    2358720,   # Black Myth: Wukong
    1245620,   # Elden Ring
    892970,    # Valheim
    1868140,   # Dave the Diver
    2138710,   # Sons of the Forest
    1063730,   # New World
    2050650,   # Resident Evil 4
    2246340,   # Monster Hunter Wilds
    1174180,   # Red Dead Redemption 2
    3164500,   # Schedule I
    2767030,   # Marvel Rivals (free, many depots)
]


def steamcmd_depots(app):
    """{depot: (gid, timeupdated)} for the public branch, plus the app name."""
    r = gp.http_get(gp.STEAMCMD_INFO.format(app=app), gp.UA_CURL, timeout=25)
    if r["status"] != 200:
        return None, {}, f"steamcmd.net HTTP {r['status']} {r['error'] or ''}".strip()
    try:
        d = json.loads(r["body"].decode("utf-8", "replace"))["data"][str(app)]
    except Exception as e:
        return None, {}, f"unparseable: {e}"
    name = (d.get("common") or {}).get("name")
    out = {}
    for did, dep in (d.get("depots") or {}).items():
        if not did.isdigit() or not isinstance(dep, dict):
            continue
        pub = ((dep.get("manifests") or {}).get("public") or {})
        gid = pub.get("gid")
        if gid and str(gid).isdigit():
            out[int(did)] = (int(gid), int(pub.get("timeupdated") or 0))
    return name, out, None


def hubcap_zip(app, key):
    req = urllib.request.Request(HUBCAP_ZIP.format(app=app),
                                 headers={"Authorization": f"Bearer {key}", "User-Agent": gp.UA_CURL})
    t0 = time.time()
    try:
        with urllib.request.urlopen(req, timeout=120) as r:
            body = r.read()
            return r.status, body, dict(r.headers), time.time() - t0
    except urllib.error.HTTPError as e:
        return e.code, e.read(), dict(e.headers), time.time() - t0
    except Exception as e:
        return None, str(e).encode(), {}, time.time() - t0


def read_key_from_lumadeck():
    for p in (os.path.expanduser("~/homebrew/plugins/LumaDeck/backend/data/api.json"),
              os.path.expanduser("~/LumaDeck/backend/data/api.json")):
        try:
            for entry in json.load(open(p)):
                url = entry.get("url", "")
                if "hubcapmanifest.com" in url or "morrenus.xyz" in url:
                    m = re.search(r"api_key=([^&]+)", url)
                    if m and "<" not in m.group(1):
                        return m.group(1)
        except Exception:
            pass
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--key", help="Hubcap API key (default: read from LumaDeck's api.json)")
    ap.add_argument("--app", action="append", type=int, default=[], help="app to test (repeatable)")
    ap.add_argument("--hours", type=float, default=72, help="only fetch apps Valve updated within this many hours")
    ap.add_argument("--all", action="store_true", help="fetch every app regardless of update age")
    args = ap.parse_args()

    key = args.key or read_key_from_lumadeck()
    if not key:
        ap.error("no Hubcap key: pass --key or deploy LumaDeck with one saved")
    apps = args.app or DEFAULT_APPS
    now = time.time()
    print(f"hubcap_fresh — {time.strftime('%Y-%m-%d %H:%M:%S %Z')}  apps={len(apps)} window={args.hours}h")

    req = urllib.request.Request(HUBCAP_STATS, headers={"Authorization": f"Bearer {key}", "User-Agent": gp.UA_CURL})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            stats = json.loads(r.read().decode())
            print(f"Hubcap key: expires {stats.get('api_key_expires_at')}  usage {stats.get('daily_usage')}/{stats.get('daily_limit')} today")
    except Exception as e:
        print(f"Hubcap stats: {e}")
    print()

    rows = []
    for app in apps:
        name, depots, err = steamcmd_depots(app)
        if err:
            print(f"--- {app}: {err}")
            continue
        content = {d: v for d, v in depots.items() if d != app}   # depot==app is the shader depot
        if not content:
            print(f"--- {app} {name}: no public content depots on steamcmd.net")
            continue
        newest = max(t for _, t in content.values())
        age_h = (now - newest) / 3600 if newest else float("inf")
        if not args.all and age_h > args.hours:
            print(f"--- {app} {name}: last Valve update {age_h/24:.1f} d ago — skipped (not recent)")
            continue
        code, body, hdrs, secs = hubcap_zip(app, key)
        tag = f"--- {app} {name}: Valve updated {age_h:.1f} h ago; Hubcap HTTP {code} in {secs:.1f}s"
        if code != 200 or not body.startswith(b"PK"):
            print(tag + f"  -> no zip ({body[:120]!r})")
            rows.append((app, name, "NOZIP"))
            continue
        zf = zipfile.ZipFile(io.BytesIO(body))
        names = zf.namelist()
        manifests = {}
        for n in names:
            m = re.match(r"(?:.*/)?(\d+)_(\d+)\.manifest$", n)
            if m:
                manifests.setdefault(int(m.group(1)), set()).add(int(m.group(2)))
        lua_gids = {}
        for n in names:
            if n.endswith(".lua"):
                for d, g in re.findall(r'setManifestid\(\s*(\d+)\s*,\s*"(\d+)"', zf.read(n).decode("utf-8", "replace")):
                    lua_gids[int(d)] = int(g)
        zdate = None
        for n in names:
            if n.endswith(".lua"):
                m = re.search(r"Created:\s*(.+)", zf.read(n).decode("utf-8", "replace"))
                if m:
                    zdate = m.group(1).strip()
        print(tag + f"  ({len(body)//1024} KB, {len(manifests)} depots with manifests, lua created: {zdate})")
        verdicts = []
        for d, (gid, t) in sorted(content.items()):
            have = manifests.get(d, set())
            if gid in have:
                v = "FRESH"
            elif have:
                v = "STALE"
            else:
                v = "MISSING"
            verdicts.append(v)
            when = time.strftime("%Y-%m-%d %H:%M", time.gmtime(t)) if t else "?"
            print(f"      depot {d:<9} valve {gid} ({when})  zip {sorted(have) or '-'}  lua {lua_gids.get(d, '-')}  -> {v}")
        rows.append((app, name, "/".join(sorted(set(verdicts)))))
        print()

    print("=== SUMMARY")
    for app, name, v in rows:
        print(f"  {app:<9} {v:<22} {name}")
    print()
    print("FRESH on apps Valve updated hours ago  -> Hubcap generates on request: it can stand in for the providers.")
    print("STALE there                            -> Hubcap serves a cache; useful for installs, not for updates.")


if __name__ == "__main__":
    main()
