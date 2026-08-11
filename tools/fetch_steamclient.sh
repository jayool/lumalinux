#!/usr/bin/env bash
# fetch_steamclient.sh — obtain a real 32-bit steamclient.so and run the GMRC
# anchor verifier on it. Intended for a GitHub Codespace / any Linux box with
# UNRESTRICTED network (Valve's CDN is not reachable from the sandboxed agent
# environment — org egress policy blocks it).
#
# It bootstraps steamcmd, which downloads linux32/steamclient.so (the same
# content-system library the desktop client ships as ubuntu12_32/steamclient.so;
# equivalent for the #13 Part 1 anchor-uniqueness question). Then it runs
# tools/verify_gmrc_anchor.py.
#
#   bash tools/fetch_steamclient.sh
#
# Env overrides:
#   WORKDIR=/path   where to bootstrap steamcmd (default: ./.steamcmd)
#   SKIP_DEPS=1     skip the apt 32-bit runtime install (if already present)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
WORKDIR="${WORKDIR:-$HERE/../.steamcmd}"
mkdir -p "$WORKDIR"

log() { printf '\033[1;36m[fetch]\033[0m %s\n' "$*"; }

# --- 1. 32-bit runtime deps (steamcmd is a 32-bit ELF that self-updates) -------
if [ "${SKIP_DEPS:-0}" != "1" ]; then
  if command -v apt-get >/dev/null 2>&1; then
    log "installing 32-bit runtime deps (needs sudo)…"
    sudo dpkg --add-architecture i386 || true
    sudo apt-get update -y || true
    # lib32gcc-s1 is the modern name; fall back to the old one on older images.
    sudo apt-get install -y ca-certificates curl lib32gcc-s1 libc6-i386 \
      || sudo apt-get install -y ca-certificates curl lib32gcc1 libc6-i386 \
      || log "WARN: dep install failed — steamcmd may not run; continuing"
  else
    log "no apt-get; ensure 32-bit libc/libgcc are present yourself"
  fi
fi

# --- 2. download + bootstrap steamcmd ------------------------------------------
TARBALL="$WORKDIR/steamcmd_linux.tar.gz"
URLS=(
  "https://steamcdn-a.akamaihd.net/client/installer/steamcmd_linux.tar.gz"
  "https://media.steampowered.com/installer/steamcmd_linux.tar.gz"
)
if [ ! -s "$TARBALL" ]; then
  ok=0
  for u in "${URLS[@]}"; do
    log "downloading steamcmd from $u"
    if curl -fsSL --max-time 120 "$u" -o "$TARBALL"; then ok=1; break; fi
  done
  [ "$ok" = 1 ] || { echo "ERROR: could not download steamcmd (network blocked?)"; exit 1; }
fi
tar -xzf "$TARBALL" -C "$WORKDIR"

log "bootstrapping steamcmd (downloads steamclient.so; ~1-2 min first run)…"
# +quit makes it self-update (pulling the client packages incl. steamclient.so)
# then exit. Non-zero exit is common on headless runs; we check for the .so next.
( cd "$WORKDIR" && ./steamcmd.sh +quit ) || log "steamcmd exited non-zero (expected headless) — checking for the .so anyway"

# --- 3. locate the 32-bit steamclient.so ---------------------------------------
SO=""
while IFS= read -r cand; do
  if file "$cand" 2>/dev/null | grep -q "ELF 32-bit"; then SO="$cand"; break; fi
done < <(find "$WORKDIR" "$HOME/Steam" "$HOME/.steam" -name steamclient.so 2>/dev/null)

if [ -z "$SO" ]; then
  echo "ERROR: steamclient.so (32-bit) not found after bootstrap."
  echo "       Look under $WORKDIR/linux32/ and \$HOME/Steam/ ; re-run once more —"
  echo "       steamcmd sometimes needs a second pass to finish the self-update."
  exit 1
fi
log "found 32-bit steamclient.so: $SO"
file "$SO"

# --- 4. run the anchor verifier ------------------------------------------------
log "running verify_gmrc_anchor.py…"
python3 "$HERE/verify_gmrc_anchor.py" "$SO"
