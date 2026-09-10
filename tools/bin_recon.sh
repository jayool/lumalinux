#!/usr/bin/env bash
# bin_recon.sh — unpack a Windows installer/app WITHOUT running it and list the
# servers it talks to. Used on 2026-09-10 to see where a third-party "library
# import" tool that still claimed to download games was getting its manifests.
#
#   tools/bin_recon.sh <file.exe|zip> [outdir]
#
# Handles Inno Setup (innoextract), NSIS / 7z / zip / cab (7z), Electron
# (app.asar), PyInstaller (pyinstxtractor), .NET (UTF-16 strings). Everything is
# static: nothing inside the package is executed.
set -u
IN="${1:?usage: bin_recon.sh <file> [outdir]}"
OUT="${2:-${IN%.*}_recon}"
mkdir -p "$OUT"
say() { printf '\n== %s\n' "$*"; }

need() { command -v "$1" >/dev/null 2>&1; }
if ! need 7z || ! need innoextract || ! need strings; then
    say "installing tools (7z, innoextract, binutils)"
    sudo pacman -S --noconfirm --needed p7zip innoextract binutils >/dev/null 2>&1 || \
    sudo apt-get install -y -qq p7zip-full innoextract binutils >/dev/null 2>&1 || true
fi

say "file"
file "$IN"; ls -l "$IN"

say "unpack -> $OUT"
if innoextract -t "$IN" >/dev/null 2>&1; then
    echo "Inno Setup"; innoextract -s -d "$OUT/inno" "$IN"
else
    echo "trying 7z"; 7z x -y -o"$OUT/7z" "$IN" >/dev/null 2>&1 && echo "7z ok" || echo "7z: not an archive it knows"
fi

# second-level: Electron asar, PyInstaller, nested zips/7z/msi
find "$OUT" -type f \( -iname '*.asar' -o -iname '*.zip' -o -iname '*.7z' -o -iname '*.msi' -o -iname '*.cab' \) 2>/dev/null | while read -r f; do
    case "${f,,}" in
        *.asar)
            say "electron asar: $f"
            if ! need asar; then npm i -g @electron/asar >/dev/null 2>&1 || true; fi
            (need asar && asar extract "$f" "$f.extracted") || \
            python3 - "$f" "$f.extracted" <<'PY'
import json, os, struct, sys
src, dst = sys.argv[1], sys.argv[2]
with open(src, "rb") as fh:
    fh.seek(4); hdr_len = struct.unpack("<I", fh.read(4))[0]
    fh.seek(16); hdr = json.loads(fh.read(hdr_len - 8).rstrip(b"\0").decode())
    base = 16 + hdr_len - 8
    base += (4 - (hdr_len % 4)) % 4
    def walk(node, path):
        for name, ent in node.get("files", {}).items():
            p = os.path.join(path, name)
            if "files" in ent: walk(ent, p); continue
            if "offset" not in ent: continue
            os.makedirs(os.path.dirname(os.path.join(dst, p)), exist_ok=True)
            fh.seek(base + int(ent["offset"])); data = fh.read(ent["size"])
            open(os.path.join(dst, p), "wb").write(data)
    walk(hdr, "")
print("asar extracted (python)")
PY
            ;;
        *) say "nested archive: $f"; 7z x -y -o"$f.extracted" "$f" >/dev/null 2>&1 || true ;;
    esac
done
# PyInstaller
find "$OUT" -type f -iname '*.exe' 2>/dev/null | while read -r f; do
    if grep -aq 'PyInstaller' "$f" 2>/dev/null || grep -aq 'pyi-' "$f" 2>/dev/null; then
        say "pyinstaller exe: $f"
        [ -f /tmp/pyinstxtractor.py ] || curl -fsSL -o /tmp/pyinstxtractor.py https://raw.githubusercontent.com/extremecoders-re/pyinstxtractor/master/pyinstxtractor.py
        (cd "$(dirname "$f")" && python3 /tmp/pyinstxtractor.py "$(basename "$f")" >/dev/null 2>&1) && echo "extracted"
    fi
done

say "inventory (top 60 by size)"
find "$OUT" -type f -printf '%10s  %p\n' 2>/dev/null | sort -rn | head -60

say "text configs (lua/json/ini/toml/yaml/txt/js) that mention hosts"
find "$OUT" -type f \( -iname '*.lua' -o -iname '*.json' -o -iname '*.ini' -o -iname '*.toml' -o -iname '*.yml' -o -iname '*.yaml' -o -iname '*.txt' -o -iname '*.cfg' -o -iname '*.js' -o -iname '*.py' -o -iname '*.pyc' \) -size -4M 2>/dev/null | while read -r f; do
    hits=$(grep -aoE 'https?://[A-Za-z0-9._~:/?#@!$&()*+,;=%-]+' "$f" 2>/dev/null | sort -u | head -20)
    [ -n "$hits" ] && { echo "--- $f"; echo "$hits"; }
done

say "URLs / hosts in binaries (ASCII + UTF-16)"
find "$OUT" -type f \( -iname '*.exe' -o -iname '*.dll' -o -iname '*.node' -o -iname '*.so' -o -iname '*.bin' -o -iname '*.dat' -o -iname '*.pyd' \) 2>/dev/null | while read -r f; do
    hits=$( { strings -n 6 "$f"; strings -n 6 -e l "$f"; } 2>/dev/null | grep -aoE 'https?://[A-Za-z0-9._~:/?#@!$&()*+,;=%-]+|[A-Za-z0-9-]+\.(com|cn|xyz|top|run|net|org|io|me|cc|app|dev|cloud|site|vip|fun|cf|tk|link|club)(/[A-Za-z0-9._~:/?#@!$&()*+,;=%-]*)?' | grep -viE 'microsoft\.com|windows\.net|w3\.org|schemas\.|verisign|digicert|globalsign|sectigo|comodo|symantec|godaddy|entrust|thawte|openssl|zlib|apache|nodejs|electronjs|github\.com/(electron|nodejs|microsoft)|npmjs|chromium|google\.com/(chrome|update)|gstatic|googleapis\.com/(auth|oauth)|python\.org|pypi|sqlite|ffmpeg' | sort | uniq -c | sort -rn | head -60)
    [ -n "$hits" ] && { echo "--- $f"; echo "$hits"; }
done

say "keywords (manifest / gmrc / request code / depot / steam api) in binaries"
find "$OUT" -type f \( -iname '*.exe' -o -iname '*.dll' -o -iname '*.node' -o -iname '*.pyd' \) 2>/dev/null | while read -r f; do
    hits=$( { strings -n 8 "$f"; strings -n 8 -e l "$f"; } 2>/dev/null | grep -aiE 'manifest|gmrc|request.?code|depotcache|depot_?key|stplug|appmanifest|steamclient|IClientAppManager|hubcap|ryuu|wudrm|opensteamtool|steam\.run|manifesthub|luatools|depotbox' | sort -u | head -40)
    [ -n "$hits" ] && { echo "--- $f"; echo "$hits"; }
done

say "done — everything under $OUT ; nothing was executed"
