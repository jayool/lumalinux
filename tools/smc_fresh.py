#!/usr/bin/env python3
"""
smc_fresh.py — is P-ToyStore/SteamManifestCache_Pro a LIVE manifest source?

Found on 2026-09-10 via Fluent-Steam-Lua (3a.lol thread 22124): a GitHub repo
by pjy612 where branch = AppId and each branch carries
<depot>_<gid>.manifest for the current public build, committed by a bot
("Update App [id] name Build Id:N Last Update Time:..."). The bot committed
Valheim's 2026-09-09 update 15 minutes after Valve published it, i.e. AFTER
the request-code providers died, so it runs on accounts that own the games.

Files are gzip-with-a-twist: 10-byte header (78 da 08 00 00 00 00 00 02 03)
followed by a raw deflate stream of a standard depotcache manifest.

For every app (or a built-in list) this script:
  1. reads Valve's CURRENT public gid + timeupdated per depot from
     api.steamcmd.net (a PICS mirror);
  2. fetches the branch README (branch exists? bot's Build Id / time);
  3. HEADs raw.githubusercontent.com/<app>/<depot>_<gid>.manifest with
     Valve's current gid -> FRESH / STALE / NOBRANCH;
  4. with --cdn, downloads one FRESH manifest, inflates it, and HEADs its
     first chunk on a Steam content server: 200 proves the manifest matches
     real content Steam serves (chunks need no request code).

    python3 tools/smc_fresh.py
    python3 tools/smc_fresh.py --app 892970 --app 3751260 --cdn
    python3 tools/smc_fresh.py --hours 48          # only apps Valve updated recently
"""

import argparse
import os
import re
import struct
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gmrc_probe as gp  # noqa: E402

OWNER, REPO = "P-ToyStore", "SteamManifestCache_Pro"
RAW = "https://raw.githubusercontent.com/{owner}/{repo}/{branch}/{name}"

# 3a.lol front page of 2026-09-10 (all updated/released 9.7–9.11) + Western
# titles that update often. Pass --app to override.
DEFAULT_APPS = [
    892970,    # Valheim (Valve 9.9 20:54 CST)
    3751260,   # The Blood of Dawnwalker (9.9)
    3353800,   # Golden Swirl (9.11 01:35 CST)
    1510440,   # Honeycomb (9.10)
    3624140,   # Wanderburg (9.10)
    3967820,   # Push Push (9.10)
    872410,    # ROTK XIV (9.10)
    3971950,   # In Falsus (9.9)
    3084810,   # Tower Lab (9.9)
    2875150,   # Backpack Boy
    2379780,   # Balatro
    1030300,   # Silksong
    1942280,   # Brotato
    1794680,   # Vampire Survivors
    1966720,   # Lethal Company
    1145350,   # Hades II
    1086940,   # Baldur's Gate 3
    1623730,   # Palworld
    413150,    # Stardew Valley
    1091500,   # Cyberpunk 2077
    2358720,   # Black Myth: Wukong
    1245620,   # Elden Ring
    3164500,   # Schedule I
    2767030,   # Marvel Rivals
    262060,    # Darkest Dungeon (owner-tested in gmrc_mint)
]


def steamcmd_depots(app):
    """name, {depot: (gid, timeupdated)} for the public branch."""
    r = gp.http_get(gp.STEAMCMD_INFO.format(app=app), gp.UA_CURL, timeout=25)
    if r["status"] != 200:
        return None, {}, f"steamcmd.net HTTP {r['status']} {r['error'] or ''}".strip()
    try:
        import json
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


def raw_url(branch, name):
    return RAW.format(owner=OWNER, repo=REPO, branch=branch, name=name)


def head(url):
    r = gp.http_get(url, gp.UA_CURL, timeout=25, want_body=False)
    return r["status"], r["error"]


def branch_readme(app):
    r = gp.http_get(raw_url(app, "README.md"), gp.UA_CURL, timeout=25)
    if r["status"] != 200:
        return None
    txt = r["body"].decode("utf-8", "replace")
    b = re.search(r"Build Id\*\*\s*`(\d+)`", txt)
    t = re.search(r"Last Update Time\*\*\s*`([^`]+)`", txt)
    return {"build": b.group(1) if b else "?", "time": t.group(1) if t else "?"}


def inflate_pro(raw):
    """Pro-format -> plain depotcache manifest bytes (or the input if it already is one)."""
    if raw[:4] == b"\xd0\x17\xf6\x71":
        return raw
    for off in (10, 2, 0):
        try:
            d = zlib.decompressobj(-15).decompress(raw[off:])
            if d[:4] == b"\xd0\x17\xf6\x71":
                return d
        except zlib.error:
            pass
    return None


def _varint(b, i):
    r = s = 0
    while True:
        c = b[i]; i += 1; r |= (c & 0x7f) << s; s += 7
        if not c & 0x80:
            return r, i


def _fields(b):
    i, out = 0, {}
    while i < len(b):
        k, i = _varint(b, i); f, t = k >> 3, k & 7
        if t == 0:
            v, i = _varint(b, i)
        elif t == 2:
            n, i = _varint(b, i); v = b[i:i + n]; i += n
        elif t == 1:
            v = b[i:i + 8]; i += 8
        elif t == 5:
            v = b[i:i + 4]; i += 4
        else:
            break
        out.setdefault(f, []).append(v)
    return out


def manifest_info(data):
    """(depot, gid, first_chunk_sha_hex) from a plain depotcache manifest."""
    i, payload, meta = 0, None, None
    while i + 8 <= len(data):
        m, n = struct.unpack("<II", data[i:i + 8]); i += 8
        if m == 0x71F617D0:
            payload = data[i:i + n]
        elif m == 0x1F4812BE:
            meta = data[i:i + n]
        elif m != 0x1B81B817:
            break
        i += n
    if not meta:
        return None
    mf = _fields(meta)
    depot = mf.get(1, [None])[0]
    gid = mf.get(2, [None])[0]
    chunk = None
    if payload:
        for f in _fields(payload).get(1, []):
            for c in _fields(f).get(6, []):
                sha = _fields(c).get(1, [b""])[0]
                if sha:
                    chunk = sha.hex(); break
            if chunk:
                break
    return depot, gid, chunk


def cdn_chunk_check(hosts, depot, chunk):
    tried = []
    for host, vhost in hosts[:3]:
        url = f"https://{host}/depot/{depot}/chunk/{chunk}"
        r = gp.http_get(url, gp.UA_STEAM, timeout=25, host_header=vhost, want_body=False)
        tried.append(f"{host}:{r['status'] or r['error']}")
        if r["status"] == 200 or (r["status"] and 400 <= r["status"] < 500):
            return r["status"], tried
    return None, tried


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--app", action="append", type=int, default=[])
    ap.add_argument("--hours", type=float, default=0, help="only apps Valve updated within this many hours (0 = all)")
    ap.add_argument("--cdn", action="store_true", help="download one FRESH manifest per app and verify a chunk on Steam's CDN")
    args = ap.parse_args()
    apps = args.app or DEFAULT_APPS
    now = time.time()
    print(f"smc_fresh — {time.strftime('%Y-%m-%d %H:%M:%S %Z')}  repo {OWNER}/{REPO}  apps={len(apps)}")

    hosts = []
    if args.cdn:
        hosts, err = gp.cdn_hosts()
        print(f"CDN hosts: {len(hosts)} {err or ''}")
    print()

    rows = []
    for app in apps:
        name, depots, err = steamcmd_depots(app)
        if err:
            print(f"--- {app}: {err}"); continue
        content = {d: v for d, v in depots.items() if d != app}
        if not content:
            print(f"--- {app} {name}: no public content depots on steamcmd.net"); continue
        newest = max(t for _, t in content.values())
        age_h = (now - newest) / 3600 if newest else float("inf")
        if args.hours and age_h > args.hours:
            print(f"--- {app} {name}: Valve update {age_h/24:.1f} d ago — skipped"); continue
        rd = branch_readme(app)
        if rd is None:
            print(f"--- {app} {name}: Valve updated {age_h:.1f} h ago; repo: NO BRANCH")
            rows.append((app, name, "NOBRANCH")); continue
        print(f"--- {app} {name}: Valve updated {age_h:.1f} h ago; repo build {rd['build']} @ {rd['time']} CST")
        verdicts, fresh_target = [], None
        for d, (gid, t) in sorted(content.items()):
            st, e = head(raw_url(app, f"{d}_{gid}.manifest"))
            v = "FRESH" if st == 200 else ("STALE" if st == 404 else f"HTTP_{st or e}")
            verdicts.append(v)
            if v == "FRESH" and fresh_target is None:
                fresh_target = (d, gid)
            when = time.strftime("%Y-%m-%d %H:%M", time.gmtime(t)) if t else "?"
            print(f"      depot {d:<9} valve gid {gid} ({when} UTC)  -> {v}")
        if args.cdn and fresh_target and hosts:
            d, gid = fresh_target
            r = gp.http_get(raw_url(app, f"{d}_{gid}.manifest"), gp.UA_CURL, timeout=60)
            body = r["body"]
            # http_get caps the body at 64 KB; re-read fully for big manifests
            if r["status"] == 200 and r["headers"].get("content-length") and int(r["headers"]["content-length"]) > len(body):
                import urllib.request
                body = urllib.request.urlopen(urllib.request.Request(raw_url(app, f"{d}_{gid}.manifest"), headers={"User-Agent": gp.UA_CURL}), timeout=120).read()
            plain = inflate_pro(body) if r["status"] == 200 else None
            info = manifest_info(plain) if plain else None
            if not info:
                print(f"      cdn: manifest {d}_{gid} could not be inflated/parsed ({len(body)} bytes)")
                verdicts.append("BADFILE")
            else:
                pd, pg, chunk = info
                ok = (pd == d and pg == gid)
                st, tried = cdn_chunk_check(hosts, d, chunk) if chunk else (None, [])
                print(f"      cdn: inside depot {pd} gid {pg} {'match' if ok else 'MISMATCH'}; first chunk {chunk[:16] if chunk else '-'}… -> HTTP {st} {tried}")
                verdicts.append("CDN_OK" if (ok and st == 200) else "CDN_FAIL")
        rows.append((app, name, "/".join(sorted(set(verdicts)))))
        print()

    print("=== SUMMARY")
    for app, name, v in rows:
        print(f"  {app:<9} {v:<28} {name}")
    print()
    print("FRESH on apps Valve updated hours ago -> the bot still runs on owning accounts: a live manifest source.")
    print("STALE there                            -> cache only, like Hubcap.")
    print("CDN_OK                                 -> the file's chunks exist on Steam's CDN: it is the real manifest.")


if __name__ == "__main__":
    main()
