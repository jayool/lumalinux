#!/usr/bin/env bash
# lumalinux — Steam client downgrade + pin (break-recovery escape-hatch).
#
# A port of headcrab's downgrade (prepdowngrade + overideupdate) and NOTHING
# else. It aligns the Steam CLIENT down to a supported build and pins it there,
# so the injection stack (whose hooks broke under a Steam update) hooks again. It
# does NOT run headcrab, does NOT replace steam.sh, and does NOT reinstall
# SLSsteam/CloudRedirect/netsock/lumalinux — that is setup.sh's job.
#
# MECHANISM — the same three moves headcrab makes, in headcrab's order:
#   1. stage the target client manifest (deck/linux) in $STEAM/package
#   2. write the steam.cfg pin (BootStrapperInhibitAll / BootStrapperForceSelfUpdate)
#   3. relaunch Steam headless so its bootstrapper pulls the target packages from
#      headcrab's mirror:
#        steam -forcesteamupdate -forcepackagedownload \
#          -overridepackageurl https://headcrab.bifrosthub.ru/client-stable -exitsteam
#
# What headcrab does NOT do, and this script does (both learnt the hard way on a
# Deck on 2026-10-14, the first live run — see docs/nosotros.md §5.2 F1):
#   * WAIT for Steam to be gone before relaunching. headcrab does `killall` and
#     moves on; a bootstrapper started while the old client is still shutting
#     down hands its arguments to that dying instance and exits, the dying
#     instance restarts itself WITHOUT our flags, nothing is downloaded, and
#     nobody notices. headcrab only gets away with it because its network
#     downloads between the kill and the relaunch usually give Steam time to die.
#   * VERIFY the result by the SHA-256 of ubuntu12_32/steamclient.so, against the
#     hash lumalinux's res/updates.yaml records for the target build. The earlier
#     "package/ holds more than the manifest" check was satisfied by the `beta`
#     file steam-jupiter writes on EVERY launch, so it could never fail.
#   * UNDO on failure: strip the pin again and restore package/, so Steam
#     self-heals to the current client on its next launch instead of sitting
#     pinned on a build it never downloaded.
#
# Writing the pin BEFORE the download (headcrab's order) is safe: package/ is only
# the bootstrapper's download cache, the extracted client keeps running with the
# pin in place, and the pin is what keeps the bootstrapper from "updating" the
# staged manifest back to the current build when it is relaunched.
#
# Must run in Desktop (Steam is stopped and relaunched headless). No root.
#
# Env overrides (all optional): DOWNGRADE_URL DECK_MANIFEST_URL LINUX_MANIFEST_URL
#   UPDATES_YAML_URL EXPECTED_SHA256 STEAM_DIR_OVERRIDE STEAM_STOP_TIMEOUT
#   STEAM_START_TIMEOUT STEAM_UPDATE_TIMEOUT
#
# Exit codes: 0 done (pinned on the target), 1 failed and undone (Steam left as
# it was), 2 undecided (the bootstrapper was still running at the timeout;
# nothing was undone — wait for it, then re-run: a client already on the target
# is just pinned).

set -uo pipefail

# ── config (mirror + manifests match headcrab's, overridable) ────────────────
# The package mirror Steam is pointed at (headcrab's Headcrab_Downgrade_URL, the
# /client-stable tier). Verified serving packages on 2026-10-14.
DOWNGRADE_URL="${DOWNGRADE_URL:-https://headcrab.bifrosthub.ru/client-stable}"
DECK_MANIFEST_URL="${DECK_MANIFEST_URL:-https://raw.githubusercontent.com/Deadboy666/SteamTracking/refs/heads/headcrab/ClientManifest/steam_client_steamdeck_stable_ubuntu12}"
LINUX_MANIFEST_URL="${LINUX_MANIFEST_URL:-https://raw.githubusercontent.com/Deadboy666/SteamTracking/refs/heads/headcrab/ClientManifest/steam_client_ubuntu12}"
# lumalinux's whitelist: its `# steam_version: <build>` annotations name the
# steamclient.so hash of each build, which is how the result is verified.
UPDATES_YAML_URL="${UPDATES_YAML_URL:-https://raw.githubusercontent.com/jayool/lumalinux/main/res/updates.yaml}"
# Seconds to wait for Steam to exit after -shutdown (then SIGTERM, then give up),
# and for the headless update relaunch to finish (a client download).
STEAM_STOP_TIMEOUT="${STEAM_STOP_TIMEOUT:-60}"
STEAM_START_TIMEOUT="${STEAM_START_TIMEOUT:-60}"
STEAM_UPDATE_TIMEOUT="${STEAM_UPDATE_TIMEOUT:-900}"

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
# (its guard would override our explicit LD_AUDIT). Mirrors the wrapper's own
# real-steam resolution. On SteamOS this is /usr/bin/steam -> steam-jupiter, which
# forwards its arguments to steam.sh, which forwards them to the bootstrapper.
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
# never the wrapper. STEAM_REAL is resolved ONCE in main, before anything is
# touched, so a missing binary aborts with Steam intact instead of mid-run.
STEAM_REAL=""
steam_cmd() {
    if [[ -d "$FLATPAK_STEAM_DIR" ]] && command -v flatpak >/dev/null 2>&1; then
        flatpak run com.valvesoftware.Steam "$@"
    else
        "$STEAM_REAL" "$@"
    fi
}

fetch() {  # fetch URL DEST — curl or wget, like the rest of the stack
    local url="$1" dest="$2"
    if command -v curl >/dev/null 2>&1; then curl -fsSL "$url" -o "$dest"
    elif command -v wget >/dev/null 2>&1; then wget -qO "$dest" "$url"
    else die "need curl or wget"; fi
}

# The 32-bit client process is literally named `steam`; steamwebhelper and the
# games die with it. steamos-manager & co. don't match -x.
steam_running() { pgrep -x -u "$(id -u)" steam >/dev/null 2>&1; }

# Wait up to $1 seconds for every `steam` process of this user to be gone.
wait_steam_gone() {
    local deadline=$(( SECONDS + $1 ))
    while steam_running; do
        (( SECONDS >= deadline )) && return 1
        sleep 1
    done
    return 0
}

# Wait up to $1 seconds for a `steam` process to APPEAR. The SteamOS launcher
# (steam-jupiter -> steam.sh) returns before the bootstrapper exists, so right
# after a launch "no steam process" means "not started yet", not "finished".
wait_steam_appear() {
    local deadline=$(( SECONDS + $1 ))
    until steam_running; do
        (( SECONDS >= deadline )) && return 1
        sleep 1
    done
    return 0
}

# Stop Steam and WAIT until it is really gone. -shutdown first (clean exit, like
# the QAM's Restart), SIGTERM if it ignores that, and only then give up. Nothing
# below may run while an old client is alive: the relaunch would hand its
# arguments to it and be lost (the 2026-10-14 failure).
stop_steam() {
    if ! steam_running; then info "Steam is not running."; return 0; fi
    info "Stopping Steam (clean shutdown)..."
    steam_cmd -shutdown >/dev/null 2>&1 || true
    if wait_steam_gone "$STEAM_STOP_TIMEOUT"; then ok "Steam exited."; sleep 2; return 0; fi
    warn "Steam ignored -shutdown for ${STEAM_STOP_TIMEOUT}s — sending SIGTERM."
    killall -u "$(id -u)" steam >/dev/null 2>&1 || true
    if wait_steam_gone 30; then ok "Steam exited."; sleep 2; return 0; fi
    return 1
}

# "version" line of a Steam client manifest (KeyValues: "version"  "1788291500").
manifest_version() {
    sed -n 's/^[[:space:]]*"version"[[:space:]]*"\([0-9]\+\)".*/\1/p' "$1" 2>/dev/null | head -n1
}

sha256_of() { sha256sum "$1" 2>/dev/null | cut -d' ' -f1; }

# The steamclient.so SHA-256 lumalinux's res/updates.yaml records for build $2:
# the first `- <64 hex>` entry after a `# steam_version: $2` annotation. A build
# may carry several annotations on one hash (Valve relabels builds without
# changing the file: 1788291500 == 1788652215), which this handles because an
# unrelated annotation in between does not clear the match. Empty when unknown.
# (No {n} interval in the regex: mawk lacks them; gawk on SteamOS has them.)
hash_for_build() {  # hash_for_build FILE BUILD
    awk -v b="$2" '
        /^[[:space:]]*#[[:space:]]*steam_version:[[:space:]]*[0-9]+/ {
            if ($0 ~ ("steam_version:[[:space:]]*" b "([^0-9]|$)")) want = 1
            next
        }
        want && /^[[:space:]]*-[[:space:]]*[0-9a-f]+/ {
            sub(/^[[:space:]]*-[[:space:]]*/, "")
            h = substr($0, 1, 64)
            if (length(h) == 64 && h ~ /^[0-9a-f]+$/) { print h; exit }
        }
    ' "$1" 2>/dev/null
}

# ── main ─────────────────────────────────────────────────────────────────────
STEAM_DIR="$(resolve_steam_dir)" || die "Steam install dir not found"
PKG_DIR="${STEAM_DIR}/package"
STEAM_CFG="${STEAM_DIR}/steam.cfg"
CLIENT_SO="${STEAM_DIR}/ubuntu12_32/steamclient.so"
info "Steam dir: $STEAM_DIR"
if ! { [[ -d "$FLATPAK_STEAM_DIR" ]] && command -v flatpak >/dev/null 2>&1; }; then
    STEAM_REAL="$(resolve_real_steam)" || die "real steam binary not found — Steam left untouched"
    info "Steam launcher: $STEAM_REAL"
fi

# Pick the manifest for this platform (Deck vs generic Linux). Both saved with the
# .manifest extension — the name Steam reads.
if is_steamos; then
    info "SteamOS/Deck detected — deck client manifest."
    MANIFEST_URL="$DECK_MANIFEST_URL"; MANIFEST_NAME="steam_client_steamdeck_stable_ubuntu12.manifest"
else
    info "Generic Linux — linux client manifest."
    MANIFEST_URL="$LINUX_MANIFEST_URL"; MANIFEST_NAME="steam_client_ubuntu12.manifest"
fi

# 1. Fetch the target manifest to a temp and verify it BEFORE touching anything,
#    so a network failure aborts with Steam completely intact.
info "Downloading target client manifest..."
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR" 2>/dev/null || true' EXIT
TMP_MANIFEST="$TMP_DIR/manifest"
fetch "$MANIFEST_URL" "$TMP_MANIFEST" || die "manifest download failed — Steam left untouched"
[[ -s "$TMP_MANIFEST" ]] || die "manifest is empty — Steam left untouched"
TARGET_VERSION="$(manifest_version "$TMP_MANIFEST")"
[[ -n "$TARGET_VERSION" ]] || die "manifest looks invalid (no \"version\" line) — Steam left untouched"
info "Target client build: $TARGET_VERSION"

# 2. What the result must look like. The expected steamclient.so hash comes from
#    lumalinux's whitelist (overridable with EXPECTED_SHA256 for testing); when
#    the feed has no annotation for the target, fall back to "the file changed".
CURRENT_VERSION=""
for m in "$PKG_DIR"/steam_client_*.manifest; do
    [[ -f "$m" ]] && { CURRENT_VERSION="$(manifest_version "$m")"; break; }
done
BEFORE_SHA="$(sha256_of "$CLIENT_SO")"
[[ -n "$BEFORE_SHA" ]] || warn "no $CLIENT_SO on disk yet — the result can only be checked against the whitelist hash."
EXPECTED_SHA="${EXPECTED_SHA256:-}"
if [[ -z "$EXPECTED_SHA" ]]; then
    if fetch "$UPDATES_YAML_URL" "$TMP_DIR/updates.yaml" && grep -q '^SafeModeHashes:' "$TMP_DIR/updates.yaml"; then
        EXPECTED_SHA="$(hash_for_build "$TMP_DIR/updates.yaml" "$TARGET_VERSION")"
    fi
fi
if [[ -n "$EXPECTED_SHA" ]]; then
    info "Expected steamclient.so for $TARGET_VERSION: ${EXPECTED_SHA:0:12}…"
else
    warn "No whitelist hash for build $TARGET_VERSION — will only check that steamclient.so changed."
fi
info "Installed client: ${CURRENT_VERSION:-unknown} (steamclient.so ${BEFORE_SHA:0:12}…)"

# Already on the target binary: nothing to download, just make sure it stays.
if [[ -n "$EXPECTED_SHA" && -n "$BEFORE_SHA" && "$EXPECTED_SHA" == "$BEFORE_SHA" ]]; then
    ok "steamclient.so already matches build $TARGET_VERSION — no download needed."
    ALREADY_THERE=1
else
    ALREADY_THERE=0
fi

# 3. Steam must be OFF, and verifiably so, before package/ or steam.cfg change.
stop_steam || die "Could not stop Steam — nothing changed. Close it and retry."

# Roll package/ back to its exact pre-downgrade state. Used on any failure below.
rollback_pkg() {
    rm -rf "$PKG_DIR" 2>/dev/null || true
    if [[ "$had_pkg" == 1 ]]; then
        mv -f "$BAK" "$PKG_DIR" 2>/dev/null || true
    fi
}

# Put steam.cfg back exactly as it was before this run (or remove it if there was
# none). Used with rollback_pkg so a failed run leaves Steam free to self-update.
restore_cfg() {
    if [[ -f "$TMP_DIR/steam.cfg.before" ]]; then
        cp -f "$TMP_DIR/steam.cfg.before" "$STEAM_CFG" 2>/dev/null || true
    else
        rm -f "$STEAM_CFG" 2>/dev/null || true
    fi
}

# Write the pin. Update-in-place: replace any existing BootStrapper* lines, keep
# the rest, re-emitting kept lines with printf '%s\n' so the block always ends in
# a newline before the appended keys. The `# lumalinux` signature lets LumaDeck
# tell OUR pin (an active break-recovery downgrade, lifted once the stack catches
# up) from a FOREIGN one (headcrab's, lifted on sight); Steam ignores non-key
# lines. Un-pinning is LumaDeck's lift_freeze(), which strips exactly these lines.
write_pin() {
    local kept=""
    if [[ -f "$STEAM_CFG" ]]; then
        kept="$(grep -viE '^[[:space:]]*(BootStrapper(InhibitAll|ForceSelfUpdate)[[:space:]]*=|#[[:space:]]*lumalinux[[:space:]]*$)' "$STEAM_CFG" 2>/dev/null || true)"
    fi
    {
        [[ -n "$kept" ]] && printf '%s\n' "$kept"
        printf '# lumalinux\nBootStrapperInhibitAll=enable\nBootStrapperForceSelfUpdate=disable\n'
    } > "${STEAM_CFG}.tmp" && mv -f "${STEAM_CFG}.tmp" "$STEAM_CFG"
}

[[ -f "$STEAM_CFG" ]] && cp -f "$STEAM_CFG" "$TMP_DIR/steam.cfg.before" 2>/dev/null

if [[ "$ALREADY_THERE" == 1 ]]; then
    write_pin || die "could not write pin at $STEAM_CFG — nothing else changed"
    ok "Pin written at $STEAM_CFG. Steam is held at build $TARGET_VERSION."
    exit 0
fi

# 4. Back up package/ (do NOT destroy it) and stage the target manifest in a fresh
#    one. The manifest tells the bootstrapper which build to pull.
info "Backing up package dir..."
BAK="${PKG_DIR}.lumalinux.bak"
had_pkg=0
if [[ -d "$PKG_DIR" ]]; then
    had_pkg=1
    rm -rf "$BAK" 2>/dev/null || true          # drop any stale backup from an interrupted run
    mv -f "$PKG_DIR" "$BAK" || die "could not back up $PKG_DIR — Steam left untouched"
fi
mkdir -p "$PKG_DIR"
cp -f "$TMP_MANIFEST" "$PKG_DIR/$MANIFEST_NAME" \
    || { rollback_pkg; die "could not place manifest in $PKG_DIR — Steam restored, nothing changed"; }

# 5. Pin FIRST (headcrab's order): with the staged manifest in place and the pin
#    forbidding the bootstrapper's own update check, the relaunch below can only
#    pull the staged build. (Without the pin it would fetch the current manifest
#    from Valve and "update" straight back to the client we are leaving.)
info "Writing Steam-update pin (steam.cfg)..."
write_pin || { rollback_pkg; die "could not write pin at $STEAM_CFG — Steam package restored, NOT pinned"; }

# 6. Relaunch Steam headless so its bootstrapper pulls the target packages from
#    the mirror and exits (-exitsteam). SLSsteam-audited when present (headcrab's
#    export_sls); vanilla otherwise. The launcher RETURNS AT ONCE (measured on a
#    Deck, 2026-10-14: steam-jupiter/steam.sh come back before the bootstrapper
#    is even a process), so its exit status means nothing and "no steam process"
#    right after it means "not started yet". Wait for the bootstrapper to appear,
#    then to be gone; only then is steamclient.so final.
LD_AUDIT_ARG=""
if [[ -s "$SLS_DIR/SLSsteam.so" ]]; then
    if [[ -s "$SLS_DIR/library-inject.so" ]]; then
        LD_AUDIT_ARG="$SLS_DIR/library-inject.so:$SLS_DIR/SLSsteam.so"
    else
        LD_AUDIT_ARG="$SLS_DIR/SLSsteam.so"
    fi
fi
info "Relaunching Steam to apply the downgrade from the mirror (headless, this downloads the client)..."
info "  mirror: $DOWNGRADE_URL"
if [[ -n "$LD_AUDIT_ARG" ]]; then
    LD_AUDIT="$LD_AUDIT_ARG" steam_cmd -forcesteamupdate -forcepackagedownload \
        -overridepackageurl "$DOWNGRADE_URL" -exitsteam >/dev/null 2>&1 || true
else
    steam_cmd -forcesteamupdate -forcepackagedownload \
        -overridepackageurl "$DOWNGRADE_URL" -exitsteam >/dev/null 2>&1 || true
fi
if ! wait_steam_appear "$STEAM_START_TIMEOUT"; then
    warn "No Steam process appeared within ${STEAM_START_TIMEOUT}s of the relaunch."
    # Fall through to the hash check: if nothing started, nothing changed, and
    # the failure path below undoes the staging.
elif ! wait_steam_gone "$STEAM_UPDATE_TIMEOUT"; then
    # Never undo package/ or the pin underneath a bootstrapper that is still
    # writing: leave everything staged and let the user re-run once it is done
    # (a client that already matches the target just gets its pin written).
    warn "Steam is still running ${STEAM_UPDATE_TIMEOUT}s after the update relaunch."
    warn "Nothing has been undone. Wait for the update window to finish, do NOT start Steam, then re-run this script."
    exit 2
fi

# 7. Verify by the ONLY thing that matters: the client binary on disk. Against the
#    whitelist hash when we have one, else against "it changed". package/ is not
#    evidence (steam-jupiter drops a `beta` file there on every launch).
AFTER_SHA="$(sha256_of "$CLIENT_SO")"
verified=0
if [[ -n "$EXPECTED_SHA" ]]; then
    [[ "$AFTER_SHA" == "$EXPECTED_SHA" ]] && verified=1
elif [[ -n "$AFTER_SHA" && "$AFTER_SHA" != "$BEFORE_SHA" ]]; then
    verified=1
fi

if [[ "$verified" -ne 1 ]]; then
    # Nothing (or the wrong thing) landed. Undo everything: steam.cfg back to what
    # it was, package/ back to what it was. Steam then starts normally and
    # self-updates to the current client on its own.
    restore_cfg
    rollback_pkg
    die "Downgrade FAILED: steamclient.so is ${AFTER_SHA:0:12}… (expected ${EXPECTED_SHA:-a different file}; was ${BEFORE_SHA:0:12}…). The bootstrapper did not install build $TARGET_VERSION from $DOWNGRADE_URL. steam.cfg and package/ have been RESTORED, so Steam starts and updates normally. Check ${STEAM_DIR}/logs/bootstrap_log.txt and retry."
fi

rm -rf "$BAK" 2>/dev/null || true
ok "steamclient.so now matches build $TARGET_VERSION (${AFTER_SHA:0:12}…)."
ok "Downgrade + pin applied. Steam is held at build $TARGET_VERSION."
info "Next: setup.sh re-establishes the wrapper; the QAM offers the update once the stack supports a newer build."
