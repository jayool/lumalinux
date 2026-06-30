#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# fetch_steamclient.py — download the CURRENT 32-bit steamclient.so from Valve.
#
# Used by .github/workflows/watch-steam.yml to get the latest desktop-client
# ubuntu12_32/steamclient.so (the exact binary a Steam Deck loads, and the one
# whose SHA-256 goes into res/updates.yaml) so check_patterns.py can validate
# it after a Steam update — with NO Steam login, NO steamcmd (steamcmd ships a
# DIFFERENT build than the desktop client), just Valve's public client CDN.
#
# How the desktop client is distributed (same path SteamDB / the Headcrab
# downgrader use):
#   1. GET https://media.steampowered.com/client/steam_client_ubuntu12
#      -> a text VDF manifest with a top-level "version" and a set of package
#         entries, each a subkey carrying a "file" (and sha2/size/zipvz).
#   2. Each package file lives at  https://media.steampowered.com/client/<file>
#      and is an LZMA-compressed ("VZ") zip archive.
#   3. steamclient.so lives inside one of those packages. We decompress each and
#      search the zip members for it (so we don't depend on the exact package
#      name, which Valve has renamed across the years).
#
# Modes:
#   --version-only   print just the manifest "version" (cheap; the cron's
#                    level-1 gate — if unchanged, the workflow skips the heavy
#                    download entirely).
#   --output PATH    download + extract steamclient.so to PATH, print its
#                    SHA-256 to stdout as "sha256=<hex>".
#
# NOTE: the live download path is exercised on a GitHub runner (open internet).
# The Steam CDN is firewalled from the dev sandbox, so only the VDF parser and
# the VZ decompressor are unit-tested locally; treat a first workflow_dispatch
# run as the end-to-end validation.
import argparse
import hashlib
import io
import lzma
import struct
import sys
import urllib.request
import zipfile

CDN = "https://media.steampowered.com/client/"
MANIFEST_URL = CDN + "steam_client_ubuntu12"
UA = "Mozilla/5.0 (X11; Linux x86_64) lumalinux-watch-steam/1.0"


# ── HTTP ─────────────────────────────────────────────────────────────────────

def http_get(url, timeout=120):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


# ── tiny Valve text-VDF parser ───────────────────────────────────────────────
# Steam's client manifest is the classic KeyValues text format: "key" "value"
# or "key" { ... }, with // line comments. We only need a nested dict.

def vdf_parse(text):
    if isinstance(text, bytes):
        text = text.decode("utf-8", "replace")
    toks = _vdf_tokens(text)
    pos = [0]

    def parse_block():
        d = {}
        while pos[0] < len(toks):
            t = toks[pos[0]]
            if t == "}":
                pos[0] += 1
                return d
            pos[0] += 1                      # consume key
            key = t
            nxt = toks[pos[0]] if pos[0] < len(toks) else None
            if nxt == "{":
                pos[0] += 1                  # consume "{"
                d[key] = parse_block()
            else:
                pos[0] += 1                  # consume value
                d[key] = nxt
        return d

    # top level may or may not be wrapped in a single root key
    return parse_block()


def _vdf_tokens(text):
    toks, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c in " \t\r\n":
            i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
        elif c == '"':
            i += 1
            start = i
            buf = []
            while i < n and text[i] != '"':
                if text[i] == "\\" and i + 1 < n:
                    buf.append(text[i + 1]); i += 2
                else:
                    buf.append(text[i]); i += 1
            toks.append("".join(buf)); i += 1
        elif c in "{}":
            toks.append(c); i += 1
        else:                                # bare token
            start = i
            while i < n and text[i] not in ' \t\r\n"{}':
                i += 1
            toks.append(text[start:i])
    return toks


# ── VZ (Valve LZMA) decompression ────────────────────────────────────────────
# Layout: "VZ" + version byte ('a') + uint32  | 5-byte LZMA1 props | LZMA1 body
#         | footer: uint32 crc + uint32 uncompressed_size + "zv".

def vz_decompress(data):
    if data[:2] != b"VZ":
        raise ValueError("not a VZ blob (bad header)")
    if data[-2:] != b"zv":
        raise ValueError("not a VZ blob (bad footer)")
    crc, out_size = struct.unpack("<II", data[-10:-2])
    props = data[7:12]                       # 5-byte LZMA1 properties
    filt = lzma._decode_filter_properties(lzma.FILTER_LZMA1, props)
    dec = lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=[filt])
    out = dec.decompress(data[12:], max_length=out_size)
    if len(out) != out_size:
        raise ValueError("VZ size mismatch: got %d, expected %d" % (len(out), out_size))
    return out


# ── manifest helpers ─────────────────────────────────────────────────────────

def get_manifest():
    return vdf_parse(http_get(MANIFEST_URL))


def manifest_version(man):
    """The manifest's client version. Walk the (possibly root-wrapped) dict for
    the first 'version' key."""
    def find(d):
        if not isinstance(d, dict):
            return None
        if "version" in d and isinstance(d["version"], str):
            return d["version"]
        for v in d.values():
            r = find(v)
            if r:
                return r
        return None
    v = find(man)
    if not v:
        raise ValueError("no 'version' in client manifest")
    return v


def package_files(man):
    """Every package 'file' referenced anywhere in the manifest. Prefer ones
    that look 32-bit (ubuntu12 and NOT _64) so we try the likely package first,
    then fall back to the rest."""
    files = []

    def walk(d):
        if not isinstance(d, dict):
            return
        f = d.get("file")
        if isinstance(f, str) and f:
            files.append(f)
        for v in d.values():
            walk(v)
    walk(man)
    # de-dup, preserve order, prefer 32-bit-looking packages
    seen, ordered = set(), []
    for f in files:
        if f not in seen:
            seen.add(f); ordered.append(f)
    pref = [f for f in ordered if "64" not in f]
    rest = [f for f in ordered if "64" in f]
    return pref + rest


def extract_steamclient(man, out_path):
    """Download packages until we find steamclient.so; extract it to out_path.
    Returns the zip member name it came from."""
    want = "steamclient.so"
    for f in package_files(man):
        try:
            blob = http_get(CDN + f)
            if blob[:2] == b"VZ":
                blob = vz_decompress(blob)
            if blob[:2] != b"PK":            # not a zip -> skip
                continue
            zf = zipfile.ZipFile(io.BytesIO(blob))
        except Exception as e:               # noqa: BLE001 — try the next package
            print("  (skip %s: %s)" % (f, e), file=sys.stderr)
            continue
        # prefer the ubuntu12_32 path, else any steamclient.so
        members = zf.namelist()
        cand = [m for m in members if m.endswith(want)]
        cand.sort(key=lambda m: (0 if "ubuntu12_32" in m or "linux32" in m else 1, len(m)))
        if cand:
            with zf.open(cand[0]) as src, open(out_path, "wb") as dst:
                dst.write(src.read())
            return cand[0]
    raise RuntimeError("steamclient.so not found in any client package")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# ── main ─────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description="Fetch the current 32-bit steamclient.so from Valve's client CDN.")
    ap.add_argument("--version-only", action="store_true",
                    help="print just the manifest version and exit (cheap gate)")
    ap.add_argument("--output", help="path to write the extracted steamclient.so")
    args = ap.parse_args()

    try:
        man = get_manifest()
        ver = manifest_version(man)
        if args.version_only:
            print(ver)
            return 0
        if not args.output:
            ap.error("--output is required unless --version-only")
        member = extract_steamclient(man, args.output)
        sha = sha256_file(args.output)
        print("version=%s" % ver, file=sys.stderr)
        print("member=%s" % member, file=sys.stderr)
        print("sha256=%s" % sha)
        return 0
    except Exception as e:                    # noqa: BLE001
        print("ERROR: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
