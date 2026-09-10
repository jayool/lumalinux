#!/usr/bin/env python3
"""
gmrc_mint.py — the POSITIVE CONTROL for gmrc_probe.py: mint a manifest request
code ourselves, through a real Steam session, and take it to the same CDN.

gmrc_probe.py showed (2026-09-10) that wudrm hands out numbers Valve's CDN
answers 401 to, even for a depot every account owns. That has two readings and
they lead to different places:

  (a) wudrm is serving STALE codes — its minting backend broke and it is
      falling back to a cache older than the code's validity window;
  (b) Valve changed what a code is bound to (session, IP, cell, ...), so no
      code minted elsewhere can ever work again.

The only way to tell them apart is to mint a code the honest way — the same
`ContentServerDirectory.GetManifestRequestCode#1` call the Steam client makes,
from a logged-in session — and hit the CDN with it from THIS machine:

  our code -> 200  and wudrm's -> 401 : (a) — the scheme still works, the
                                            providers are what broke.
  our code -> 401 too                 : the URL/CDN check itself is suspect
                                            (or (b)); read the eresult line.
  AccessDenied on mint                : this account does not own the depot;
                                            re-run with --user for one that does.

It also re-fetches wudrm's code for each target and, given the JSON of a
previous gmrc_probe run (--probe-json), says whether wudrm returned the SAME
number as before — a code that never changes across runs minutes apart is a
cached one.

Needs the ValvePython `steam` package WITH its `client` extra (gevent +
protobuf); a bare `pip install steam` only brings the Web API half. Stdlib
otherwise:

    pip install --user --break-system-packages 'steam[client]'
    python3 tools/gmrc_mint.py --target 2875150:2875151:275796854305563747 \\
        --probe-json /tmp/gmrc.json

Fallback if the Steam client library cannot be installed: DepotDownloader
(self-contained release, no .NET needed) mints its own code internally, so an
ANONYMOUS `-manifest-only` download of the free control depot succeeding today
proves the same thing as the "our code -> 200" line above (minus the URL check):

    curl -fsSL -o /tmp/dd.zip https://github.com/SteamRE/DepotDownloader/releases/latest/download/DepotDownloader-linux-x64.zip
    mkdir -p /tmp/dd && unzip -qo /tmp/dd.zip -d /tmp/dd && chmod +x /tmp/dd/DepotDownloader
    /tmp/dd/DepotDownloader -app 228980 -depot 228989 -manifest-only -dir /tmp/dd-out

Login: anonymous by default (enough for free depots such as the control).
For a depot only an owning account can mint for, pass --user USERNAME; the
password and the Steam Guard code are prompted, nothing is stored.
"""

import argparse
import getpass
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gmrc_probe as gp  # noqa: E402  (reuses http_get / cdn_hosts / cdn_check / parsers)


def mint(client, app, depot, gid, branch="public"):
    """Returns (eresult_name, code or None, raw_body_repr)."""
    resp = client.send_um_and_wait(
        "ContentServerDirectory.GetManifestRequestCode#1",
        {"app_id": app, "depot_id": depot, "manifest_id": gid, "app_branch": branch},
        timeout=20,
    )
    if resp is None:
        return "TIMEOUT", None, None
    from steam.enums import EResult
    er = EResult(resp.header.eresult)
    code = getattr(resp.body, "manifest_request_code", 0)
    return er.name, (code or None), str(resp.body).strip().replace("\n", " ")


# Apps an ANONYMOUS login can mint for (free tools + the dedicated-server
# package 17906). Their depots are resolved live; together they give a few
# dozen distinct (depot, gid) pairs, enough to see a per-account rate limit
# without touching anything paid.
BURST_APPS = [228980, 1007, 740, 232250, 232330, 232370, 4020, 90, 1690800, 2394010]


def burst(client, n, gid_cache):
    """Mint n codes as fast as the CM answers, cycling over every depot of
    BURST_APPS. A provider serves thousands of these per hour from a handful of
    accounts; if Valve now throttles or denies past some count, it shows here
    as a change in eresult after a certain point."""
    import collections
    from steam.enums import EResult
    pairs = []
    for app in BURST_APPS:
        if app not in gid_cache:
            gid_cache[app] = gp.resolve_gids(app)[0]
        for depot, gid in sorted(gid_cache[app].items()):
            pairs.append((app, depot, gid))
    if not pairs:
        print("burst: no depots resolved (api.steamcmd.net down?)")
        return
    print(f"=== BURST: {n} mints over {len(pairs)} distinct (depot, gid) pairs from {len(BURST_APPS)} free apps")
    counts = collections.Counter()
    first_bad = None
    t0 = time.time()
    lat = []
    for i in range(n):
        app, depot, gid = pairs[i % len(pairs)]
        t = time.time()
        er, code, _ = mint(client, app, depot, gid)
        lat.append(time.time() - t)
        counts[er] += 1
        if er != "OK" and first_bad is None:
            first_bad = (i + 1, er, app, depot)
        if (i + 1) % 25 == 0:
            print(f"  {i+1:4d} done  {time.time()-t0:5.1f}s  " + ", ".join(f"{k}={v}" for k, v in counts.items()))
    lat.sort()
    print(f"  total {n} in {time.time()-t0:.1f}s; latency p50={lat[len(lat)//2]*1000:.0f}ms "
          f"p95={lat[int(len(lat)*0.95)]*1000:.0f}ms max={lat[-1]*1000:.0f}ms")
    print("  eresults: " + ", ".join(f"{k}={v}" for k, v in counts.items()))
    if first_bad:
        print(f"  first non-OK at request #{first_bad[0]}: {first_bad[1]} (app {first_bad[2]} depot {first_bad[3]})")
        print("  -> a threshold like this is what a provider minting for everyone would hit constantly")
    else:
        print("  -> no throttling seen at this volume; a rate limit is not what killed the providers")


def watch(client, hosts, target, minutes):
    """Re-mint the same manifest every 60s and keep testing the FIRST code
    against the CDN, to measure how long a code stays valid and when the CM
    starts handing out a different one."""
    label, app, depot, gid = target
    print(f"=== WATCH ({minutes} min): {label} depot={depot} gid={gid}")
    er, first, _ = mint(client, app, depot, gid)
    if not first:
        print(f"  cannot mint ({er}); nothing to watch")
        return
    t0 = time.time()
    print(f"  t+0s  minted {first}")
    while time.time() - t0 < minutes * 60:
        time.sleep(60)
        el = int(time.time() - t0)
        st_first, _ = gp.cdn_check(hosts, depot, gid, first)
        er, now, _ = mint(client, app, depot, gid)
        same = "same" if now == first else f"ROTATED -> {now}"
        print(f"  t+{el:4d}s  first code CDN -> {st_first}   fresh mint: {er} {same}")


def fetch_wudrm(gid):
    name, tmpl, kind = gp.PROVIDERS[1]
    assert name == "wudrm"
    r = gp.http_get(tmpl.format(gid=gid), gp.UA_OST)
    if r["error"]:
        return None, r["error"]
    return gp.PARSERS[kind](r["body"].decode("utf-8", "replace")), f"http={r['status']}"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--target", action="append", default=[], metavar="APP:DEPOT[:GID]")
    ap.add_argument("--lua", action="append", default=[], metavar="FILE")
    ap.add_argument("--no-controls", action="store_true")
    ap.add_argument("--user", metavar="USERNAME", help="log in as this account instead of anonymously")
    ap.add_argument("--probe-json", metavar="FILE", help="gmrc_probe.py --json output to compare wudrm codes against")
    ap.add_argument("--cell", type=int, default=0)
    ap.add_argument("--burst", type=int, default=0, metavar="N",
                    help="rate-limit probe: mint N codes back-to-back over every depot of the free "
                         "apps this login owns, and report the eresult distribution + latency")
    ap.add_argument("--watch", type=int, default=0, metavar="MINUTES",
                    help="validity-window probe: re-mint the first target every 60s for this long, "
                         "and keep asking the CDN whether the FIRST code still works")
    args = ap.parse_args()

    try:
        from steam.client import SteamClient
        from steam.enums import EResult
    except ImportError as e:
        print(f"cannot import the ValvePython Steam client: {e}\n"
              "A bare 'pip install steam' is NOT enough — the CM client lives behind the\n"
              "'client' extra (gevent + protobuf). Install it with:\n"
              "    pip install --user --break-system-packages 'steam[client]'\n"
              "If gevent has no wheel for this Python, use a venv on an older one, or run\n"
              "the DepotDownloader fallback described in the docstring of this script.")
        sys.exit(2)

    print(f"gmrc_mint — {time.strftime('%Y-%m-%d %H:%M:%S %Z')}")

    # targets (same sources as the probe, minus keys.txt)
    targets = [] if args.no_controls else list(gp.CONTROLS)
    for f in args.lua:
        targets += gp.read_lua_targets(f)
    for t in args.target:
        p = t.split(":")
        if len(p) < 2 or not all(x.isdigit() for x in p):
            ap.error(f"bad --target {t!r}")
        targets.append((f"--target {t}", int(p[0]), int(p[1]), int(p[2]) if len(p) > 2 else None))
    gid_cache = {}
    resolved = []
    for label, app, depot, gid in targets:
        if app not in gid_cache:
            gid_cache[app] = gp.resolve_gids(app)[0]
        use = gid_cache[app].get(depot) or gid
        if not use:
            print(f"  SKIP {label}: no gid")
            continue
        resolved.append((label, app, depot, use))

    previous = {}
    if args.probe_json:
        try:
            for r in json.load(open(args.probe_json))["results"]:
                if r["provider"] == "wudrm" and r.get("code"):
                    previous[int(r["gid"])] = (int(r["code"]), r.get("cdn_status"))
        except Exception as e:
            print(f"--probe-json: could not read ({e}); skipping the staleness comparison")

    hosts, err = gp.cdn_hosts(args.cell)
    if not hosts:
        print(f"CDN discovery failed ({err}); cannot validate codes")
        sys.exit(1)
    print(f"CDN: using {', '.join(h for h, _ in hosts[:3])}")

    # login
    client = SteamClient()
    if args.user:
        pw = getpass.getpass(f"Steam password for {args.user}: ")
        res = client.login(args.user, pw)
        while res in (EResult.AccountLogonDenied, EResult.AccountLoginDeniedNeedTwoFactor,
                      EResult.TwoFactorCodeMismatch, EResult.InvalidLoginAuthCode):
            code = input("Steam Guard code: ").strip()
            if res in (EResult.AccountLoginDeniedNeedTwoFactor, EResult.TwoFactorCodeMismatch):
                res = client.login(args.user, pw, two_factor_code=code)
            else:
                res = client.login(args.user, pw, auth_code=code)
    else:
        res = client.anonymous_login()
    print(f"login: {EResult(res).name}  steamid={client.steam_id}  cell={client.cell_id}")
    if res != EResult.OK:
        sys.exit(1)

    print()
    verdicts = []
    for label, app, depot, gid in resolved:
        print(f"=== {label}  app={app} depot={depot} gid={gid}")
        er, ours, raw = mint(client, app, depot, gid)
        line = f"  mint (this session): eresult={er} code={ours}"
        if ours:
            st, tried = gp.cdn_check(hosts, depot, gid, ours)
            line += f"   CDN -> {st}"
            verdicts.append(("ours", label, st))
        else:
            line += f"   body={raw}"
        print(line)

        wcode, wmeta = fetch_wudrm(gid)
        line = f"  wudrm:               code={wcode} ({wmeta})"
        if wcode:
            st, _ = gp.cdn_check(hosts, depot, gid, wcode)
            line += f"   CDN -> {st}"
            verdicts.append(("wudrm", label, st))
            if gid in previous:
                pcode, pst = previous[gid]
                line += ("   SAME code as the earlier probe run -> cached/stale" if pcode == wcode
                         else f"   differs from the earlier run ({pcode}) -> not a frozen cache")
            if ours:
                line += "   (== ours!)" if ours == wcode else "   (!= ours)"
        print(line)
        print()

    if args.burst:
        burst(client, args.burst, gid_cache)
    if args.watch and resolved:
        watch(client, hosts, resolved[0], args.watch)

    ok_ours = [v for v in verdicts if v[0] == "ours" and v[2] == 200]
    bad_ours = [v for v in verdicts if v[0] == "ours" and v[2] != 200]
    print("=== VERDICT")
    if ok_ours and not bad_ours:
        print("  Freshly minted codes are ACCEPTED by the CDN from this machine: the request-code")
        print("  scheme is unchanged and the CDN check in gmrc_probe.py is sound. The providers")
        print("  are what broke — wudrm's numbers are not valid codes for these manifests.")
    elif bad_ours and not ok_ours:
        print("  Even our own freshly minted codes get rejected. Either the CDN URL scheme changed")
        print("  (compare with what the Steam client logs) or codes are now bound to something")
        print("  this request does not carry. Look at the eresult and the CDN status above.")
    elif not verdicts:
        print("  Nothing minted (see eresult per target). AccessDenied = this login does not own")
        print("  the depot; re-run with --user for an account that does.")
    else:
        print("  Mixed: ours accepted for some targets, rejected for others — ownership-specific.")


if __name__ == "__main__":
    main()
