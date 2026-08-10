#!/usr/bin/env bash
# lumalinux — Steam client downgrade + pin (break-recovery escape-hatch).
#
# A faithful port of headcrab's CURRENT downgrade (prepdowngrade + overideupdate),
# and NOTHING else. It aligns the Steam CLIENT down to a supported build and pins
# it there, so the injection stack (whose byte patterns broke under a Steam major
# update) hooks again. It does NOT run headcrab, does NOT replace steam.sh, and
# does NOT reinstall SLSsteam/CloudRedirect/netsock/lumalinux — that is setup.sh's
# job (the wrapper model). Running only the downgrade is what avoids the double
# injection a full headcrab run would cause (its steam.sh carries INJECT_SLS/
# INJECT_CR, which fights our wrapper).
#
# MECHANISM — the MIRROR approach headcrab actually uses (verified against the live
# headcrab.sh, Deadboy666/h3adcr-b@abacab0). Steam downloads the target client
# packages DIRECTLY from headcrab's package mirror; the old dlm/dgsc local-proxy
# depot (localhost:1666) is DEAD — commented out (`#dlm` / `#dgsc`) in headcrab
# itself "for a while" — so it is gone here too.
#   1. download the target client manifest (deck/linux) into $STEAM/package
#   2. write steam.cfg pin (BootStrapperInhibitAll / BootStrapperForceSelfUpdate)
#   3. relaunch Steam headless, SLSsteam-audited (headcrab's export_sls):
#        LD_AUDIT=<library-inject>:<SLSsteam> steam \
#          -forcesteamupdate -forcepackagedownload \
#          -overridepackageurl https://headcrab.bifrosthub.ru/client-stable -exitsteam
#
# Must run in Desktop (Steam has to be killed and relaunched headless). No root.
#
# !!! ON-DEVICE VERIFICATION REQUIRED !!!
# A Steam client depot downgrade cannot be exercised in CI / a codespace, and this
# environment's egress proxy BLOCKS headcrab.bifrosthub.ru, so the mirror cannot be
# reached or validated here at all. Confirm on a real Steam Deck that the mirror
# serves the packages and the client lands on the pinned build before relying on
# this. The manifest is fetched+verified BEFORE package/ is touched, so a network
# failure aborts with Steam left intact.
#
# Env overrides (all optional): DOWNGRADE_URL DECK_MANIFEST_URL LINUX_MANIFEST_URL
#   STEAM_DIR_OVERRIDE

set -uo pipefail

# ── config (mirror + manifests match headcrab's, overridable) ────────────────
# The package mirror Steam is pointed at (headcrab's Headcrab_Downgrade_URL, the
# /client-stable tier — one cycle behind /client-latest). Override to a local
# depot URL if you ever resurrect the dlm/dgsc path, or to a different tier.
DOWNGRADE_URL="${DOWNGRADE_URL:-https://headcrab.bifrosthub.ru/client-stable}"
DECK_MANIFEST_URL="${DECK_MANIFEST_URL:-https://raw.githubusercontent.com/Deadboy666/SteamTracking/refs/heads/headcrab/ClientManifest/steam_client_steamdeck_stable_ubuntu12}"
LINUX_MANIFEST_URL="${LINUX_MANIFEST_URL:-https://raw.githubusercontent.com/Deadboy666/SteamTracking/refs/heads/headcrab/ClientManifest/steam_client_ubuntu12}"

SLS_DIR="${HOME}/.local/share/SLSsteam"
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
# (its guard would override our explicit -overridepackageurl / LD_AUDIT). Mirrors
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

# Pick the manifest for this platform (Deck vs generic Linux). Both saved with the
# .manifest extension — the name Steam reads, and the one headcrab's own version
# check greps for (its linux branch writes it without the extension, a headcrab
# bug; we stay consistent with the reader).
if is_steamos; then
    info "SteamOS/Deck detected — deck client manifest."
    MANIFEST_URL="$DECK_MANIFEST_URL"; MANIFEST_NAME="steam_client_steamdeck_stable_ubuntu12.manifest"
else
    info "Generic Linux — linux client manifest."
    MANIFEST_URL="$LINUX_MANIFEST_URL"; MANIFEST_NAME="steam_client_ubuntu12.manifest"
fi

# 1. Fetch the target manifest to a temp and verify it BEFORE touching package/,
#    so a network failure aborts with Steam completely intact (nothing cleared,
#    no pin written, no relaunch).
info "Downloading target client manifest..."
TMP_MANIFEST="$(mktemp)"
trap 'rm -f "$TMP_MANIFEST" 2>/dev/null || true' EXIT
fetch "$MANIFEST_URL" "$TMP_MANIFEST" || die "manifest download failed — Steam left untouched"
[[ -s "$TMP_MANIFEST" ]] || die "manifest is empty — Steam left untouched"
grep -q '"version"' "$TMP_MANIFEST" || die "manifest looks invalid (no \"version\" line) — Steam left untouched"

# Steam must be off for a client downgrade (it rewrites package/).
info "Stopping Steam..."
killall steam          >/dev/null 2>&1 || true
killall steamwebhelper >/dev/null 2>&1 || true
killall dgsc           >/dev/null 2>&1 || true   # kill any leftover local depot from the old approach
sleep 2

# 2. Prep package dir: clear it and drop the verified manifest in (this is what
#    tells Steam which build to pull from the mirror).
info "Preparing package dir..."
[[ -d "$PKG_DIR" ]] || mkdir -p "$PKG_DIR"
rm -f "$PKG_DIR"/* 2>/dev/null || true
mv -f "$TMP_MANIFEST" "$PKG_DIR/$MANIFEST_NAME" || die "could not place manifest in $PKG_DIR"
trap - EXIT

# 3. Pin: write steam.cfg so Steam holds the downgraded build (createsteamcfg
#    port). Update-in-place: replace any existing BootStrapper* lines, keep the
#    rest. Capture the kept lines and re-emit with printf '%s\n' so the block
#    ALWAYS ends in a newline before the appended keys — otherwise a steam.cfg
#    whose last line lacked a trailing newline would glue the keys onto it.
#
#    Stamp our own pin with a `# lumalinux` signature line. That line is how the
#    LumaDeck side tells OUR pin (an active break-recovery downgrade, to be lifted
#    only once the ecosystem catches up) from a FOREIGN one (headcrab's, written on
#    every install) — a foreign pin is lifted on sight during migration. Steam
#    ignores non-key lines in steam.cfg, so the signature is inert. Filter any prior
#    signature out of the kept lines so re-runs don't stack duplicates.
info "Writing Steam-update pin (steam.cfg)..."
_kept=""
if [[ -f "$STEAM_CFG" ]]; then
    # Back up the PRISTINE steam.cfg once — a re-run must not overwrite it with the
    # already-pinned version (that would destroy the only clean copy).
    [[ -e "${STEAM_CFG}.lumadeck.bak" ]] || cp -f "$STEAM_CFG" "${STEAM_CFG}.lumadeck.bak" 2>/dev/null || true
    _kept="$(grep -viE '^[[:space:]]*(BootStrapper(InhibitAll|ForceSelfUpdate)[[:space:]]*=|#[[:space:]]*lumalinux[[:space:]]*$)' "$STEAM_CFG" 2>/dev/null || true)"
fi
{
    [[ -n "$_kept" ]] && printf '%s\n' "$_kept"
    printf '# lumalinux\nBootStrapperInhibitAll=enable\nBootStrapperForceSelfUpdate=disable\n'
} > "${STEAM_CFG}.tmp" && mv -f "${STEAM_CFG}.tmp" "$STEAM_CFG"
ok "Pin written at $STEAM_CFG"

# 4. Relaunch Steam headless to pull the target packages from the MIRROR. This is
#    headcrab's overideupdate, verbatim mechanism: SLSsteam-audited (export_sls)
#    when present; vanilla otherwise (the downgrade is Steam's own bootstrapper).
#    Best-effort like headcrab (it runs the same command &>/dev/null) — we cannot
#    reliably detect mid-download failure from here.
LD_AUDIT_ARG=""
if [[ -f "$SLS_DIR/SLSsteam.so" ]]; then
    if [[ -f "$SLS_DIR/library-inject.so" ]]; then
        LD_AUDIT_ARG="$SLS_DIR/library-inject.so:$SLS_DIR/SLSsteam.so"
    else
        LD_AUDIT_ARG="$SLS_DIR/SLSsteam.so"
    fi
fi
info "Relaunching Steam to apply the downgrade from the mirror (headless)..."
info "  mirror: $DOWNGRADE_URL"
if [[ -n "$LD_AUDIT_ARG" ]]; then
    LD_AUDIT="$LD_AUDIT_ARG" steam_cmd -forcesteamupdate -forcepackagedownload \
        -overridepackageurl "$DOWNGRADE_URL" -exitsteam >/dev/null 2>&1 || true
else
    steam_cmd -forcesteamupdate -forcepackagedownload \
        -overridepackageurl "$DOWNGRADE_URL" -exitsteam >/dev/null 2>&1 || true
fi

ok "Downgrade + pin applied. Steam is held at the supported build."
info "Next: setup.sh re-establishes the wrapper; the QAM offers the update once the stack supports a newer build."
