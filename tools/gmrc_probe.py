#!/usr/bin/env python3
"""
gmrc_probe.py — diagnose the manifest-request-code (GMRC) providers END TO END.

Two questions, answered separately, because they have different fixes:

  1. Does each provider (opensteamtool / wudrm / steamrun) still ANSWER with a
     number for a given manifest gid?  (transport, Cloudflare, 5xx, empty body)
  2. Is the number it returns still ACCEPTED by Valve's CDN?  A request code is
     only worth anything if `GET /depot/<depot>/manifest/<gid>/5/<code>` on a
     Steam content server returns 200.  A provider can be "up" and still hand
     out codes the CDN rejects (expired, minted for a different gid, or a
     Valve-side change in what a code is bound to).

lumalinux itself cannot tell 1 from 2: gmrc_store.hpp only parses the body,
and the hook returns whatever it parsed.  Steam then fails downstream with
"Access Denied" / "No connection" either way.  This script does what the hook
cannot: it takes the code to the CDN and asks.

Run it on a machine with open internet (a SteamOS codespace, a Deck in desktop
mode).  No Steam needed, no login, stdlib only.

    python3 tools/gmrc_probe.py                       # defaults + keys.txt
    python3 tools/gmrc_probe.py --target 2379780:2379781:3512319404653808464
    python3 tools/gmrc_probe.py --no-cdn              # providers only
    python3 tools/gmrc_probe.py --json out.json       # machine-readable copy

Targets come from three places, all optional:
  - built-in controls (see CONTROLS): a FREE depot every account owns
    (Steamworks Common Redistributables) — if the CDN rejects a provider code
    even for that, the code itself is the problem, not ownership.
  - ~/.config/lumalinux/keys.txt Extended lines (depot;app;gid;size;key).
    gid=0 (no-pin) lines get their CURRENT gid from api.steamcmd.net.
  - --target APP:DEPOT[:GID] on the command line (GID resolved if omitted).

The verdict per (target, provider) is one of:
  DOWN            transport error (DNS / connect / TLS / timeout)
  CHALLENGED      HTML body (Cloudflare interstitial)
  HTTP_<status>   non-2xx with a non-numeric body
  NO_CODE         2xx but no uint64 in the body
  CODE_UNTESTED   got a code, CDN check skipped (--no-cdn / no CDN host)
  CODE_REJECTED   got a code, CDN answered non-200 for it
  CODE_VALID      got a code, CDN served the manifest with it (200)
"""

import argparse
import json
import os
import re
import socket
import ssl
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

# ── the same table as src/gmrc_store.hpp ────────────────────────────────────
PROVIDERS = [
    ("opensteamtool", "https://manifest.opensteamtool.com/{gid}",      "plain"),
    ("wudrm",         "http://gmrc.wudrm.com/manifest/{gid}",          "plain"),
    ("steamrun",      "https://manifest.steam.run/api/manifest/{gid}", "json"),
]
UA_OST   = "OpenSteamTool/1.0"           # what gmrc_store.hpp sends
UA_CURL  = "curl/8.0"                    # what Cloudflare challenges (RESEARCH §7)
UA_STEAM = "Valve/Steam HTTP Client 1.0" # what the Steam client sends to the CDN

# (app, depot) controls. The gid is resolved live; the fallback is the last
# value RESEARCH §7 recorded, which may be stale (a stale gid is still a fair
# test of "does the provider know this manifest at all").
CONTROLS = [
    ("Steamworks Common Redistributables (FREE, everyone owns it)",
     228980, 228989, 3514306556860204959),
    ("Balatro (RESEARCH §7 reference)",
     2379780, 2379781, 3512319404653808464),
]

STEAMCMD_INFO = "https://api.steamcmd.net/v1/info/{app}"
CSD_SERVERS   = ("https://api.steampowered.com/IContentServerDirectoryService/"
                 "GetServersForSteamPipe/v1/?cell_id={cell}&max_servers=30")


# ── http ────────────────────────────────────────────────────────────────────
def http_get(url, ua, timeout=20, host_header=None, want_body=True):
    """GET url. Returns dict(status, headers, body, error, ms). Never raises.
    Follows redirects (like CURLOPT_FOLLOWLOCATION in curl.cpp)."""
    req = urllib.request.Request(url, headers={"User-Agent": ua})
    if host_header:
        req.add_header("Host", host_header)
    t0 = time.time()
    out = {"url": url, "status": None, "headers": {}, "body": b"", "error": None}
    try:
        ctx = ssl.create_default_context()
        with urllib.request.urlopen(req, timeout=timeout, context=ctx) as r:
            out["status"] = r.status
            out["headers"] = {k.lower(): v for k, v in r.headers.items()}
            out["body"] = r.read(65536) if want_body else b""
    except urllib.error.HTTPError as e:
        out["status"] = e.code
        out["headers"] = {k.lower(): v for k, v in e.headers.items()}
        try:
            out["body"] = e.read(65536)
        except Exception:
            pass
    except (urllib.error.URLError, socket.timeout, ssl.SSLError, OSError) as e:
        out["error"] = f"{type(e).__name__}: {e}"
    out["ms"] = int((time.time() - t0) * 1000)
    return out


def snippet(body, n=160):
    s = body[:n].decode("utf-8", "replace").replace("\n", "\\n").replace("\r", "")
    return s + ("…" if len(body) > n else "")


# ── the two parsers, ported verbatim from gmrc_store.hpp ────────────────────
def parse_plain_uint(text):
    m = re.match(r"^[ \t\r\n]*([0-9]+)", text)
    if not m:
        return None
    code = int(m.group(1))
    return code or None


def parse_steamrun_json(text):
    key = text.find('"content"')
    if key < 0:
        return None
    q1 = text.find('"', key + 9)
    if q1 < 0:
        return None
    q2 = text.find('"', q1 + 1)
    if q2 < 0:
        return None
    return parse_plain_uint(text[q1 + 1:q2])


PARSERS = {"plain": parse_plain_uint, "json": parse_steamrun_json}


# ── gid resolution (api.steamcmd.net mirrors PICS appinfo) ──────────────────
def resolve_gids(app):
    """{depot_id: gid} for the app's public branch, or {} on any failure."""
    r = http_get(STEAMCMD_INFO.format(app=app), UA_CURL, timeout=25)
    if r["status"] != 200:
        return {}, f"steamcmd.net HTTP {r['status']} {r['error'] or ''}".strip()
    try:
        data = json.loads(r["body"].decode("utf-8", "replace"))
        depots = data["data"][str(app)]["depots"]
    except Exception as e:
        return {}, f"steamcmd.net unparseable: {e}"
    out = {}
    for did, d in depots.items():
        if not did.isdigit() or not isinstance(d, dict):
            continue
        gid = (((d.get("manifests") or {}).get("public") or {}).get("gid"))
        if gid and str(gid).isdigit():
            out[int(did)] = int(gid)
    return out, None


# ── CDN discovery ───────────────────────────────────────────────────────────
def cdn_hosts(cell=0):
    """[(host, vhost)] of HTTPS-capable content servers, best first."""
    r = http_get(CSD_SERVERS.format(cell=cell), UA_STEAM, timeout=25)
    if r["status"] != 200:
        return [], f"GetServersForSteamPipe HTTP {r['status']} {r['error'] or ''}".strip()
    try:
        servers = json.loads(r["body"].decode("utf-8", "replace"))["response"]["servers"]
    except Exception as e:
        return [], f"GetServersForSteamPipe unparseable: {e}"
    picked = []
    for s in servers:
        if s.get("type") not in ("SteamCache", "CDN"):
            continue
        if s.get("https_support") not in ("mandatory", "optional"):
            continue
        picked.append((s.get("weighted_load", 9e9), s["host"], s.get("vhost") or s["host"]))
    picked.sort()
    return [(h, v) for _, h, v in picked], None


def cdn_check(hosts, depot, gid, code):
    """Ask up to 3 content servers for the manifest with `code`. Returns the
    first decisive answer (200 or a 4xx); 5xx / transport errors move on."""
    tried = []
    for host, vhost in hosts[:3]:
        url = f"https://{host}/depot/{depot}/manifest/{gid}/5/{code}"
        r = http_get(url, UA_STEAM, timeout=25, host_header=vhost, want_body=False)
        tried.append({"host": host, "status": r["status"], "error": r["error"],
                      "len": r["headers"].get("content-length"), "ms": r["ms"]})
        if r["status"] == 200 or (r["status"] and 400 <= r["status"] < 500):
            return r["status"], tried
    return None, tried


# ── main ────────────────────────────────────────────────────────────────────
def probe_provider(name, tmpl, kind, gid, ua):
    url = tmpl.format(gid=gid)
    r = http_get(url, ua)
    res = {"provider": name, "ua": ua, "url": url, "status": r["status"],
           "ms": r["ms"], "error": r["error"],
           "server": r["headers"].get("server"), "cf_ray": r["headers"].get("cf-ray"),
           "content_type": r["headers"].get("content-type"),
           "body": snippet(r["body"]), "code": None, "verdict": None}
    if r["error"]:
        res["verdict"] = "DOWN"
        return res
    text = r["body"].decode("utf-8", "replace")
    code = PARSERS[kind](text)
    if code:
        res["code"] = code
        res["verdict"] = "CODE_UNTESTED"
    elif text.lstrip()[:1] == "<":
        res["verdict"] = "CHALLENGED" if "cloudflare" in text.lower() or "just a moment" in text.lower() else f"HTML_{r['status']}"
    elif r["status"] and not (200 <= r["status"] < 300):
        res["verdict"] = f"HTTP_{r['status']}"
    else:
        res["verdict"] = "NO_CODE"
    return res


def read_keys_targets(path):
    out = []
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parts = line.split(";")
                if len(parts) == 5 and all(p.isdigit() for p in parts[:4]):
                    depot, app, gid = int(parts[0]), int(parts[1]), int(parts[2])
                    out.append((f"keys.txt depot {depot} (app {app})", app, depot, gid or None))
    except FileNotFoundError:
        pass
    return out


def tail_grep(path, pattern, n=20):
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            lines = [l.rstrip("\n") for l in fh if re.search(pattern, l)]
        return lines[-n:]
    except FileNotFoundError:
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--target", action="append", default=[], metavar="APP:DEPOT[:GID]",
                    help="extra target (repeatable). GID resolved from api.steamcmd.net if omitted")
    ap.add_argument("--keys", default=os.path.expanduser("~/.config/lumalinux/keys.txt"),
                    help="keys.txt to pull Extended-format depots from (default: %(default)s)")
    ap.add_argument("--no-keys", action="store_true", help="ignore keys.txt")
    ap.add_argument("--no-controls", action="store_true", help="skip the built-in control depots")
    ap.add_argument("--no-cdn", action="store_true", help="skip the CDN validation of returned codes")
    ap.add_argument("--cell", type=int, default=0, help="Steam cell id for CDN discovery (default 0)")
    ap.add_argument("--json", metavar="FILE", help="also write the full results as JSON")
    args = ap.parse_args()

    print(f"gmrc_probe — {time.strftime('%Y-%m-%d %H:%M:%S %Z')}  python {sys.version.split()[0]}")
    print()

    # 1. targets
    targets = []
    if not args.no_controls:
        targets += CONTROLS
    if not args.no_keys:
        kt = read_keys_targets(args.keys)
        print(f"keys.txt: {args.keys} -> {len(kt)} Extended depot(s)")
        targets += kt
    for t in args.target:
        p = t.split(":")
        if len(p) < 2 or not all(x.isdigit() for x in p):
            ap.error(f"bad --target {t!r}")
        targets.append((f"--target {t}", int(p[0]), int(p[1]), int(p[2]) if len(p) > 2 else None))

    # 2. resolve current gids per app (one steamcmd.net call per app)
    gid_cache, gid_err = {}, {}
    for label, app, depot, gid in targets:
        if app not in gid_cache:
            gid_cache[app], gid_err[app] = resolve_gids(app)
    resolved = []
    print("targets:")
    for label, app, depot, gid in targets:
        live = gid_cache[app].get(depot)
        use = live or gid
        note = ""
        if live and gid and live != gid:
            note = f"  (Valve now: {live}; using it — the fallback {gid} is stale)"
        elif live:
            note = "  (current per api.steamcmd.net)"
        elif gid:
            note = f"  (api.steamcmd.net unavailable: {gid_err.get(app)}; using fallback gid)"
        if not use:
            print(f"  - SKIP {label}: no gid ({gid_err.get(app) or 'not in appinfo'})")
            continue
        print(f"  - {label}: app={app} depot={depot} gid={use}{note}")
        resolved.append((label, app, depot, use, live, gid))
    print()

    # 3. CDN hosts
    hosts, host_err = ([], "skipped (--no-cdn)") if args.no_cdn else cdn_hosts(args.cell)
    if hosts:
        print(f"CDN: {len(hosts)} HTTPS content server(s); will use up to 3: "
              + ", ".join(h for h, _ in hosts[:3]))
    else:
        print(f"CDN: none ({host_err}) — codes will be reported CODE_UNTESTED")
    print()

    # 4. probe
    results = []
    for label, app, depot, gid, live, fallback in resolved:
        print(f"=== {label}  depot={depot} gid={gid}")
        if hosts:
            # Baselines so a 200 means something: a garbage code and no code.
            st_bad, _ = cdn_check(hosts, depot, gid, 1)
            print(f"  CDN baseline: bogus code -> HTTP {st_bad}   (expect 401/403; a 200 here would mean the CDN is not checking codes at all)")
        for name, tmpl, kind in PROVIDERS:
            uas = [UA_OST] + ([UA_CURL] if name == "opensteamtool" else [])
            for ua in uas:
                r = probe_provider(name, tmpl, kind, gid, ua)
                r.update({"label": label, "app": app, "depot": depot, "gid": gid})
                if r["code"] and hosts:
                    st, tried = cdn_check(hosts, depot, gid, r["code"])
                    r["cdn"] = tried
                    r["verdict"] = "CODE_VALID" if st == 200 else ("CODE_REJECTED" if st else "CODE_UNTESTED")
                    r["cdn_status"] = st
                results.append(r)
                ua_tag = "" if ua == UA_OST else f" [UA={ua}]"
                head = f"  {name:13s}{ua_tag:14s} -> {r['verdict']:14s}"
                if r["error"]:
                    print(f"{head} {r['error']}  ({r['ms']} ms)")
                    continue
                meta = f"http={r['status']} {r['ms']}ms server={r['server']!s:.20} ct={r['content_type']!s:.30}"
                if r["cf_ray"]:
                    meta += f" cf-ray={r['cf_ray']}"
                print(f"{head} {meta}")
                print(f"      body: {r['body']}")
                if r["code"]:
                    line = f"      code: {r['code']}"
                    if r.get("cdn"):
                        line += "   CDN: " + "; ".join(
                            f"{t['host']} -> {t['status'] or t['error']}"
                            + (f" len={t['len']}" if t.get('len') else "") for t in r["cdn"])
                    print(line)
        print()

    # 5. local evidence, if this box has the stack
    for path, pat, title in (
        (os.path.expanduser("~/.cache/lumalinux/lumalinux.log"), r"GMRC", "lumalinux.log (GMRC lines)"),
        (os.path.expanduser("~/.local/share/Steam/logs/content_log.txt"),
         r"(?i)manifest request code|manifest.*(denied|unavailable|failed)|request code", "content_log.txt"),
    ):
        lines = tail_grep(path, pat)
        if lines is None:
            continue
        print(f"--- {title}: {path}  (last {len(lines)})")
        for l in lines:
            print("   " + l)
        print()

    # 6. summary
    print("=== SUMMARY (per provider, across targets)")
    for name, _, _ in PROVIDERS:
        vs = [r["verdict"] for r in results if r["provider"] == name and r["ua"] == UA_OST]
        counts = {}
        for v in vs:
            counts[v] = counts.get(v, 0) + 1
        print(f"  {name:13s} " + ", ".join(f"{k}x{v}" for k, v in sorted(counts.items())))
    print()
    print("How to read it:")
    print("  DOWN/CHALLENGED/HTTP_5xx/NO_CODE on all three  -> providers are not answering (mode A).")
    print("  CODE_REJECTED, incl. on the FREE control depot -> they answer but Valve refuses the code (mode B).")
    print("  CODE_VALID on the control but REJECTED on ours -> ownership-specific: the provider's backing")
    print("                                                    account no longer holds those games.")
    print("  CODE_VALID everywhere                          -> providers are fine; the fault is elsewhere")
    print("                                                    (hook, keys.txt gating, Steam build).")

    if args.json:
        with open(args.json, "w") as fh:
            json.dump({"targets": [dict(zip(("label", "app", "depot", "gid", "live_gid", "fallback_gid"), t)) for t in resolved],
                       "cdn_hosts": hosts[:3], "results": results}, fh, indent=2, default=str)
        print(f"\nJSON written to {args.json}")


if __name__ == "__main__":
    main()
