#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# fetch_steamclient.py — download the CURRENT 32-bit steamclient.so from Valve.
#
# Used by .github/workflows/watch-steam.yml to get the ubuntu12_32/steamclient.so
# whose SHA-256 goes into res/updates.yaml so check_patterns.py can validate it
# after a Steam update — with NO Steam login, NO steamcmd (steamcmd ships a
# DIFFERENT build), just Valve's public client CDN.
#
# CHANNEL (important): defaults to the STEAMDECK_STABLE manifest, because that is
# the client a real Steam Deck loads and is the authoritative source for what to
# whitelist. The generic steam_client_ubuntu12 (desktop Linux) is a SEPARATE
# manifest with its own version number and release cadence — so at any given
# moment the two channels can point at different builds. Historically their
# steamclient.so hashes also differed; for current builds (since ~Jan 2026) the
# two channels resolve to a BYTE-IDENTICAL steamclient.so (SLSsteam's upstream
# whitelist annotates one hash as `ubuntu12_32 & steamdeck_stable`). Defaulting
# to steamdeck_stable is still correct: it tracks exactly what Decks run rather
# than depending on the channels staying converged. Override with
# LUMA_STEAM_MANIFEST (see MANIFEST_NAME below) — e.g. the CachyOS/desktop port
# validator points it at the generic channel to prove the binary matches.
#
# How the client is distributed (same path SteamDB / the Headcrab downgrader use):
#   1. GET https://media.steampowered.com/client/<MANIFEST_NAME>
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
import os
import struct
import sys
import urllib.request
import zipfile

CDN = "https://media.steampowered.com/client/"
# A Steam Deck loads the steamdeck_stable channel, NOT the generic desktop-Linux
# steam_client_ubuntu12. These are SEPARATE manifests with independent version
# numbers, so they can point at different builds at any moment (headcrab pins via
# the steamdeck manifests too). For current builds the steamclient.so they
# resolve to is byte-identical across channels, but watch-steam exists to
# validate what real Decks actually run, so default to the steamdeck_stable
# manifest rather than assuming the channels stay converged. Earlier this read
# steam_client_ubuntu12 and silently validated the wrong client for every Deck
# user. Override via LUMA_STEAM_MANIFEST for the generic ("steam_client_ubuntu12",
# used by the port validator) or beta ("steam_client_steamdeck_publicbeta_ubuntu12").
MANIFEST_NAME = os.environ.get("LUMA_STEAM_MANIFEST", "steam_client_steamdeck_stable_ubuntu12")
MANIFEST_URL = CDN + MANIFEST_NAME
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


def _packages_container(man):
    """The dict whose values are package nodes (each carrying a 'file'). In the
    real manifest that is the top-level "ubuntu12" wrapper."""
    for v in man.values():
        if isinstance(v, dict) and any(
                isinstance(x, dict) and "file" in x for x in v.values()):
            return v
    return man


def _package_rank(key):
    # steamclient.so lives in the 32-bit client binaries package (bins_ubuntu12);
    # try it (then sibling bins_*_ubuntu12) before the big runtime/webkit blobs
    # so we download ~40 MB instead of ~600 MB.
    if key == "bins_ubuntu12":
        return 0
    if "bins" in key and "ubuntu12" in key and "steamrt" not in key:
        return 1
    if "ubuntu12" in key and "steamrt" not in key:
        return 2
    return 3


def package_entries(man):
    """[(key, plain_file, vz_file), ...] for every package, ordered so the one
    most likely to hold steamclient.so comes first."""
    cont = _packages_container(man)
    out = []
    for key, val in cont.items():
        if isinstance(val, dict) and isinstance(val.get("file"), str):
            out.append((key, val["file"], val.get("zipvz")))
    out.sort(key=lambda e: _package_rank(e[0]))
    return out


def extract_steamclient(man, out_path):
    """Download packages (the vz-compressed variant Valve actually serves) until
    we find steamclient.so; extract it to out_path. Returns the member name.
    Diagnostics go to stderr (captured in the workflow log)."""
    want = "steamclient.so"
    for key, plain, vz in package_entries(man):
        # Each package lists a plain "file" AND a "zipvz". The CDN serves the
        # .vz (LZMA) one; the plain path is usually absent -> try vz first.
        variants = []
        if vz:
            variants.append((vz, True))
        if plain:
            variants.append((plain, False))
        for fname, is_vz in variants:
            try:
                blob = http_get(CDN + fname)
            except Exception as e:           # noqa: BLE001
                print("  (%s: download failed: %s)" % (key, e), file=sys.stderr)
                continue
            try:
                if is_vz or blob[:2] == b"VZ":
                    blob = vz_decompress(blob)
                if blob[:2] != b"PK":
                    print("  (%s: not a zip after decompress, head=%r)"
                          % (key, blob[:4]), file=sys.stderr)
                    continue
                zf = zipfile.ZipFile(io.BytesIO(blob))
            except Exception as e:           # noqa: BLE001
                print("  (%s: decompress/open failed: %s)" % (key, e), file=sys.stderr)
                continue
            members = zf.namelist()
            # lumalinux hooks the 32-bit binary — exclude the 64-bit one.
            cand = [m for m in members if m.endswith(want)
                    and "ubuntu12_64" not in m and "linux64" not in m]
            if cand:
                cand.sort(key=lambda m: (0 if ("ubuntu12_32" in m or "linux32" in m)
                                         else 1, len(m)))
                print("  (found steamclient.so in package %s: %s)" % (key, cand[0]),
                      file=sys.stderr)
                with zf.open(cand[0]) as src, open(out_path, "wb") as dst:
                    dst.write(src.read())
                return cand[0]
            print("  (%s: %d members, no steamclient.so)" % (key, len(members)),
                  file=sys.stderr)
            break    # got a valid zip but no match; don't retry the plain variant
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
