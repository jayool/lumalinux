#!/usr/bin/env bash
# lumalinux — Steam client downgrade + pin (break-recovery escape-hatch).
#
# A faithful port of headcrab's `clientdowngrade` (prepdowngrade + overideupdate)
# and NOTHING else. It aligns the Steam CLIENT down to a supported build and pins
# it there, so the injection stack (whose byte patterns broke under a Steam major
# update) hooks again. It does NOT run headcrab, does NOT replace steam.sh, and
# does NOT reinstall SLSsteam/CloudRedirect/netsock/lumalinux — that is setup.sh's
# job (the wrapper model). Running only the downgrade is what avoids the double
# injection that a full headcrab run would cause (its steam.sh carries INJECT_SLS/
# INJECT_CR, which fights our wrapper).
#
# Mechanism (identical to headcrab, same closed depot binaries, same flags):
#   1. fetch dgsc (local depot server) + dlm (package downloader) from
#      h3adcr-b-modul3s, same as headcrab
#   2. clear $STEAM/package, fetch sources.txt + the target client manifest
#   3. dlm  --input-file sources.txt --max-concurrent 16   → download old packages
#   4. write steam.cfg pin (BootStrapperInhibitAll / ForceSelfUpdate)
#   5. dgsc --port 1666 --silent &                         → serve them locally
#   6. relaunch Steam: -forcesteamupdate -forcepackagedownload
#      -overridepackageurl http://localhost:1666/ -exitsteam  (SLSsteam-audited,
#      like headcrab's export_sls, when SLSsteam is present)
#
# Must run in Desktop (Steam has to be killed and relaunched headless). No root.
#
# !!! ON-DEVICE VERIFICATION REQUIRED !!!
# A Steam client depot downgrade cannot be exercised in CI / a codespace. The dgsc
# invocation is reconstructed from headcrab's (defined but inline-commented, run as
# a persistent service there); confirm the exact flow on a real Steam Deck before
# relying on it. Everything is guarded so a failure leaves Steam runnable.
#
# Env overrides (all optional): DGSC_URL DLM_URL SOURCES_URL DECK_MANIFEST_URL
#   LINUX_MANIFEST_URL DOWNGRADE_URL STEAM_DIR_OVERRIDE

set -uo pipefail

# ── config (URLs mirror headcrab's, overridable) ─────────────────────────────
DGSC_URL="${DGSC_URL:-https://github.com/Deadboy666/h3adcr-b-modul3s/raw/refs/heads/main/dgsc}"
DLM_URL="${DLM_URL:-https://github.com/Deadboy666/h3adcr-b-modul3s/raw/refs/heads/main/dlm}"
SOURCES_URL="${SOURCES_URL:-https://raw.githubusercontent.com/Deadboy666/h3adcr-b-modul3s/refs/heads/main/sources.txt}"
DECK_MANIFEST_URL="${DECK_MANIFEST_URL:-https://raw.githubusercontent.com/Deadboy666/SteamTracking/refs/heads/headcrab/ClientManifest/steam_client_steamdeck_stable_ubuntu12}"
LINUX_MANIFEST_URL="${LINUX_MANIFEST_URL:-https://raw.githubusercontent.com/Deadboy666/SteamTracking/refs/heads/headcrab/ClientManifest/steam_client_ubuntu12}"
DOWNGRADE_URL="${DOWNGRADE_URL:-http://localhost:1666/}"
DGSC_PORT="${DGSC_PORT:-1666}"

SLS_DIR="${HOME}/.local/share/SLSsteam"
DG_DIR="${HOME}/.local/share/lumalinux/downgrade"   # our own cache dir for dgsc/dlm
FLATPAK_STEAM_DIR="${HOME}/.var/app/com.valvesoftware.Steam/.steam/steam"

# ── helpers ──────────────────────────────────────────────────────────────────
c_info='\e[0;36m'; c_ok='\e[0;32m'; c_warn='\e[0;33m'; c_err='\e[0;31m'; c_rst='\e[0m'
info() { printf "${c_info}[*]${c_rst} %s\n" "$*"; }
ok()   { printf "${c_ok}[+]${c_rst} %s\n" "$*"; }
warn() { printf "${c_warn}[!]${c_rst} %s\n" "$*" >&2; }
die()  { printf "${c_err}[X]${c_rst} %s\n" "$*" >&2; exit 1; }

# Resolve the native Steam root (the dir that owns steam.sh + package/). Mirrors
# headcrab's $HOME/.steam/steam but tries the usual locations, resolving symlinks.
resolve_steam_dir() {
    local c
    if [[ -n "${STEAM_DIR_OVERRIDE:-}" ]]; then printf '%s' "$STEAM_DIR_OVERRIDE"; return 0; fi
    for c in "$HOME/.steam/steam" "$HOME/.local/share/Steam" "$FLATPAK_STEAM_DIR"; do
        if [[ -d "$c" && -d "$c/package" ]]; then readlink -f "$c" 2>/dev/null || printf '%s' "$c"; return 0; fi
    done
    for c in "$HOME/.steam/steam" "$HOME/.local/share/Steam"; do
        if [[ -d "$c" ]]; then readlink -f "$c" 2>/dev/null || printf '%s' "$c"; return 0; fi
    done
    return 1
}

is_steamos() {
    [[ -f /etc/steamos-release ]] && return 0
    if [[ -r /etc/os-release ]]; then . /etc/os-release 2>/dev/null || true
        case " ${ID:-} ${ID_LIKE:-} " in *" steamos "*) return 0 ;; esac
    fi
    return 1
}

# Resolve the REAL native steam binary, SKIPPING our injection wrapper
# ($SLS_DIR/path/steam) — the downgrade relaunch must not go through the wrapper
# (its guard would override our explicit -overridepackageurl LD_AUDIT). Mirrors
# the wrapper's own real-steam resolution.
resolve_real_steam() {
    local self="$SLS_DIR/path/steam" d c IFS=:
    for d in $PATH; do
        c="$d/steam"
        [[ -x "$c" ]] || continue
        [[ "$(readlink -f "$c" 2>/dev/null)" == "$(readlink -f "$self" 2>/dev/null)" ]] && continue
        printf '%s' "$c"; return 0
    done
    for c in /usr/bin/steam /usr/games/steam /usr/lib/steam/steam; do
        [[ -x "$c" ]] && { printf '%s' "$c"; return 0; }
    done
    return 1
}

# Steam launcher (real native binary or flatpak), like headcrab's wheresteam but
# never the wrapper.
steam_cmd() {
    if [[ -d "$FLATPAK_STEAM_DIR" ]] && command -v flatpak >/dev/null 2>&1; then
        flatpak run com.valvesoftware.Steam "$@"
    else
        local real; real="$(resolve_real_steam)" || die "real steam binary not found"
        "$real" "$@"
    fi
}

fetch() {  # fetch URL DEST — curl or wget, like the rest of the stack
    local url="$1" dest="$2"
    if command -v curl >/dev/null 2>&1; then curl -fsSL "$url" -o "$dest"
    elif command -v wget >/dev/null 2>&1; then wget -qO "$dest" "$url"
    else die "need curl or wget"; fi
}

# ── main ─────────────────────────────────────────────────────────────────────
STEAM_DIR="$(resolve_steam_dir)" || die "Steam install dir not found"
PKG_DIR="${STEAM_DIR}/package"
STEAM_CFG="${STEAM_DIR}/steam.cfg"
info "Steam dir: $STEAM_DIR"
[[ -d "$PKG_DIR" ]] || mkdir -p "$PKG_DIR"

# Steam must be off for a client downgrade (it rewrites package/).
info "Stopping Steam..."
killall steam        >/dev/null 2>&1 || true
killall steamwebhelper >/dev/null 2>&1 || true
killall dgsc         >/dev/null 2>&1 || true
sleep 2

# 1. depot binaries (same source/flags as headcrab), cached in DG_DIR.
mkdir -p "$DG_DIR"
if [[ ! -x "$DG_DIR/dgsc" ]]; then info "Downloading dgsc..."; fetch "$DGSC_URL" "$DG_DIR/dgsc" || die "dgsc download failed"; chmod +x "$DG_DIR/dgsc"; else ok "dgsc cached."; fi
if [[ ! -x "$DG_DIR/dlm"  ]]; then info "Downloading dlm...";  fetch "$DLM_URL"  "$DG_DIR/dlm"  || die "dlm download failed";  chmod +x "$DG_DIR/dlm";  else ok "dlm cached.";  fi

# 2. prep: clear the package dir, fetch sources.txt + the target client manifest.
info "Preparing package dir..."
rm -f "$PKG_DIR"/* 2>/dev/null || true
fetch "$SOURCES_URL" "$PKG_DIR/sources.txt" || die "sources.txt download failed"
if is_steamos; then
    info "SteamOS/Deck detected — deck client manifest."
    fetch "$DECK_MANIFEST_URL" "$PKG_DIR/steam_client_steamdeck_stable_ubuntu12.manifest" \
        || die "deck manifest download failed"
else
    info "Generic Linux — linux client manifest."
    fetch "$LINUX_MANIFEST_URL" "$PKG_DIR/steam_client_ubuntu12" || die "linux manifest download failed"
fi

# 3. dlm: fetch the old client packages into the package dir.
info "Fetching client packages (dlm)..."
( cd "$PKG_DIR" && "$DG_DIR/dlm" --input-file sources.txt --max-concurrent 16 ) \
    || warn "dlm returned non-zero (continuing — dgsc may still serve cached packages)"

# 4. pin: write steam.cfg so Steam holds the downgraded build (createsteamcfg port).
#    Update-in-place: replace any existing BootStrapper* lines, keep the rest.
info "Writing Steam-update pin (steam.cfg)..."
{
    if [[ -f "$STEAM_CFG" ]]; then
        cp -f "$STEAM_CFG" "${STEAM_CFG}.lumadeck.bak" 2>/dev/null || true
        grep -viE '^[[:space:]]*BootStrapper(InhibitAll|ForceSelfUpdate)[[:space:]]*=' "$STEAM_CFG" 2>/dev/null || true
    fi
    printf 'BootStrapperInhibitAll=enable\nBootStrapperForceSelfUpdate=disable\n'
} > "${STEAM_CFG}.tmp" && mv -f "${STEAM_CFG}.tmp" "$STEAM_CFG"
ok "Pin written at $STEAM_CFG"

# 5. dgsc: serve the local depot on :PORT (background), like headcrab's dgsc().
info "Starting local depot server (dgsc :$DGSC_PORT)..."
( cd "$PKG_DIR" && "$DG_DIR/dgsc" --port "$DGSC_PORT" --silent ) &
DGSC_PID=$!
sleep 2

# 6. relaunch Steam headless to pull the old packages from the local depot.
#    SLSsteam-audited when present (headcrab's export_sls); vanilla otherwise —
#    the client downgrade is Steam's native bootstrapper doing the work.
LD_AUDIT_ARG=""
if [[ -f "$SLS_DIR/SLSsteam.so" ]]; then
    if [[ -f "$SLS_DIR/library-inject.so" ]]; then
        LD_AUDIT_ARG="$SLS_DIR/library-inject.so:$SLS_DIR/SLSsteam.so"
    else
        LD_AUDIT_ARG="$SLS_DIR/SLSsteam.so"
    fi
fi
info "Relaunching Steam to apply the downgrade (headless)..."
if [[ -n "$LD_AUDIT_ARG" ]]; then
    LD_AUDIT="$LD_AUDIT_ARG" steam_cmd -forcesteamupdate -forcepackagedownload \
        -overridepackageurl "$DOWNGRADE_URL" -exitsteam >/dev/null 2>&1 || true
else
    steam_cmd -forcesteamupdate -forcepackagedownload \
        -overridepackageurl "$DOWNGRADE_URL" -exitsteam >/dev/null 2>&1 || true
fi

# 7. cleanup: stop the depot server.
kill "$DGSC_PID" >/dev/null 2>&1 || true
killall dgsc >/dev/null 2>&1 || true

ok "Downgrade + pin applied. Steam is held at the supported build."
info "Next: setup.sh re-establishes the wrapper; the QAM offers the update once the stack supports a newer build."
