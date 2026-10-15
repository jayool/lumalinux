#!/usr/bin/env bash
# lumalinux — Steam client downgrade + pin (break-recovery escape-hatch).
#
# Aligns the Steam CLIENT down to a supported build and pins it there, so the
# injection stack (whose hooks broke under a Steam update) hooks again. It does
# NOT run headcrab, does NOT replace steam.sh, and does NOT reinstall
# SLSsteam/CloudRedirect/netsock/lumalinux — that is setup.sh's job.
#
# MECHANISM — what headcrab's dgsc/dlm era did, measured on a Deck on 2026-10-14
# (docs/nosotros.md §5.2 F1), self-contained:
#   1. fetch the target client manifest (Deadboy666/SteamTracking@headcrab) and
#      download every package it lists from Valve's CDN (which keeps old client
#      packages online) into a temp dir — BEFORE touching Steam, so a network
#      failure aborts with Steam intact
#   2. stop Steam and wait until it is really gone
#   3. stage the manifest in $STEAM/package, write the steam.cfg pin
#      (BootStrapperInhibitAll / BootStrapperForceSelfUpdate)
#   4. serve the temp dir on http://127.0.0.1:1666/ (python3 http.server) and
#      relaunch Steam headless against it:
#        steam -forcesteamupdate -forcepackagedownload \
#          -overridepackageurl http://127.0.0.1:1666/ -exitsteam
#   5. verify by the SHA-256 of ubuntu12_32/steamclient.so; undo on failure
#
# WHY A LOCAL SERVER. With -forcesteamupdate the bootstrapper does NOT use the
# manifest staged in package/: it downloads the branch manifest
# (steam_client_<beta>_ubuntu12) fresh from its first download host, and if that
# host 404s it falls back to Valve's CDN, which serves the CURRENT manifest — so
# a mirror that only carries packages (headcrab.bifrosthub.ru does) "downgrades"
# straight back to the build you are leaving. The only host that can answer with
# the OLD manifest is one we run ourselves. That is what headcrab's dgsc did, and
# what the current headcrab.sh no longer does (it points the bootstrapper at
# localhost:1666 with dgsc commented out, so it does not downgrade either).
#
# Other lessons from the live runs, all enforced here:
#   * WAIT for Steam to be gone before relaunching: a bootstrapper started while
#     the old client is still shutting down hands its flags to the dying
#     instance and exits; that instance restarts itself without them.
#   * The SteamOS launcher (steam-jupiter -> steam.sh) RETURNS BEFORE the
#     bootstrapper exists: wait for a `steam` process to appear, then to be gone.
#   * VERIFY by the client binary's hash, never by what is in package/:
#     steam-jupiter drops a `beta` file there on every launch.
#   * Pin BEFORE the relaunch (headcrab's order) is safe: package/ is only the
#     download cache and the extracted client keeps running under the pin.
#
# Must run in Desktop (Steam is stopped and relaunched headless). No root.
#
# Env overrides (all optional): DECK_MANIFEST_URL LINUX_MANIFEST_URL PACKAGE_CDN
#   PACKAGE_CDN_FALLBACK UPDATES_YAML_URL EXPECTED_SHA256 STEAM_DIR_OVERRIDE
#   LOCAL_PORT STEAM_STOP_TIMEOUT STEAM_START_TIMEOUT STEAM_UPDATE_TIMEOUT
#
# Exit codes: 0 done (pinned on the target; Steam is NOT started — -exitsteam),
# 1 failed and undone (Steam left as it was), 2 undecided (the bootstrapper was
# still running at the timeout; nothing was undone — wait for it, then re-run:
# a client already on the target is just pinned).

set -uo pipefail

# ── config ───────────────────────────────────────────────────────────────────
DECK_MANIFEST_URL="${DECK_MANIFEST_URL:-https://raw.githubusercontent.com/Deadboy666/SteamTracking/refs/heads/headcrab/ClientManifest/steam_client_steamdeck_stable_ubuntu12}"
LINUX_MANIFEST_URL="${LINUX_MANIFEST_URL:-https://raw.githubusercontent.com/Deadboy666/SteamTracking/refs/heads/headcrab/ClientManifest/steam_client_ubuntu12}"
# Where the packages the manifest lists are fetched from. Valve's own CDN keeps
# old client packages online (verified 2026-10-14 for the 1788291500 set); the
# akamai host is the same CDN. headcrab's package mirror is the last resort.
PACKAGE_CDN="${PACKAGE_CDN:-https://client-update.fastly.steamstatic.com}"
PACKAGE_CDN_FALLBACK="${PACKAGE_CDN_FALLBACK:-https://client-update.akamai.steamstatic.com https://headcrab.bifrosthub.ru/client-stable}"
# lumalinux's whitelist: its `# steam_version: <build>` annotations name the
# steamclient.so hash of each build, which is how the result is verified.
UPDATES_YAML_URL="${UPDATES_YAML_URL:-https://raw.githubusercontent.com/jayool/lumalinux/main/res/updates.yaml}"
# Port of the local package server the bootstrapper is pointed at (headcrab's
# dgsc used 1666 too).
LOCAL_PORT="${LOCAL_PORT:-1666}"
# Seconds to wait for Steam to exit after -shutdown (then SIGTERM, then give up),
# for the bootstrapper to appear after the relaunch, and for it to finish.
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
# forwards its arguments to steam.sh, which forwards them to the bootstrapper
# (measured: the flags arrive).
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

# The bootstrapper's own log is the deterministic signal for the relaunch: it
# writes `Startup - Steam Client launched with: ... -overridepackageurl ...`
# when it starts and `Shutdown` when it exits (-exitsteam). pgrep alone missed
# the whole 27-second run on a Deck (2026-10-14), so both are watched.
BOOT_LOG=""; LOG_MARK=0
log_mark()         { LOG_MARK="$(wc -l < "$BOOT_LOG" 2>/dev/null || echo 0)"; }
log_since()        { tail -n "+$((LOG_MARK + 1))" "$BOOT_LOG" 2>/dev/null; }
bootstrap_seen()   { log_since | grep -q -- '-overridepackageurl'; }
bootstrap_done()   { log_since | grep -q 'Shutdown'; }

# Stop Steam and WAIT until it is really gone. -shutdown first (clean exit, like
# the QAM's Restart), SIGTERM if it ignores that, and only then give up. Nothing
# below may run while an old client is alive: the relaunch would hand its
# arguments to it and be lost.
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

# The package files a client manifest makes the bootstrapper download: every
# "zipvz" (the compressed form it prefers) and every "file" (the plain zip, used
# when an entry has no vz). Both are plain KeyValues string pairs.
manifest_package_files() {
    grep -oE '"(zipvz|file)"[[:space:]]+"[^"]+"' "$1" 2>/dev/null | cut -d'"' -f4 | awk '!seen[$0]++'
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

# Local package server (headcrab's dgsc, minus the binary): python3's http.server
# on the loopback, serving SERVE_DIR. Stopped by the EXIT trap.
SERVER_PID=""
start_server() {
    command -v python3 >/dev/null 2>&1 || return 1
    python3 -m http.server "$LOCAL_PORT" --bind 127.0.0.1 --directory "$SERVE_DIR" >/dev/null 2>&1 &
    SERVER_PID=$!
    local i
    for i in 1 2 3 4 5 6 7 8 9 10; do
        if curl -fsS -o /dev/null "http://127.0.0.1:${LOCAL_PORT}/${MANIFEST_HOST_NAME}" 2>/dev/null; then return 0; fi
        kill -0 "$SERVER_PID" 2>/dev/null || return 1
        sleep 0.5
    done
    return 1
}
stop_server() { [[ -n "$SERVER_PID" ]] && kill "$SERVER_PID" 2>/dev/null; SERVER_PID=""; }

# ── main ─────────────────────────────────────────────────────────────────────
STEAM_DIR="$(resolve_steam_dir)" || die "Steam install dir not found"
PKG_DIR="${STEAM_DIR}/package"
STEAM_CFG="${STEAM_DIR}/steam.cfg"
CLIENT_SO="${STEAM_DIR}/ubuntu12_32/steamclient.so"
BOOT_LOG="${STEAM_DIR}/logs/bootstrap_log.txt"
info "Steam dir: $STEAM_DIR"
if ! { [[ -d "$FLATPAK_STEAM_DIR" ]] && command -v flatpak >/dev/null 2>&1; }; then
    STEAM_REAL="$(resolve_real_steam)" || die "real steam binary not found — Steam left untouched"
    info "Steam launcher: $STEAM_REAL"
fi
command -v python3 >/dev/null 2>&1 || die "python3 is required (for the local package server) — Steam left untouched"

# Pick the manifest for this platform (Deck vs generic Linux). The bootstrapper
# asks its download host for `steam_client_<branch>_ubuntu12` where <branch> is
# the package/beta file (steam-jupiter writes `steamdeck_stable` there), or
# `steam_client_ubuntu12` with no branch — that is the name the local server
# must answer to. package/ keeps the staged copy under the .manifest name Steam
# reads for its installed version.
if is_steamos; then
    info "SteamOS/Deck detected — deck client manifest."
    MANIFEST_URL="$DECK_MANIFEST_URL"; MANIFEST_NAME="steam_client_steamdeck_stable_ubuntu12.manifest"
else
    info "Generic Linux — linux client manifest."
    MANIFEST_URL="$LINUX_MANIFEST_URL"; MANIFEST_NAME="steam_client_ubuntu12.manifest"
fi
BRANCH="$(tr -d '[:space:]' < "$PKG_DIR/beta" 2>/dev/null || true)"
if [[ -n "$BRANCH" ]]; then MANIFEST_HOST_NAME="steam_client_${BRANCH}_ubuntu12"; else MANIFEST_HOST_NAME="steam_client_ubuntu12"; fi

# 1. Fetch the target manifest to a temp and verify it BEFORE touching anything,
#    so a network failure aborts with Steam completely intact.
info "Downloading target client manifest..."
TMP_DIR="$(mktemp -d)"
trap 'stop_server; rm -rf "$TMP_DIR" 2>/dev/null || true' EXIT
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

# 3. Download every package the manifest lists into the dir the local server
#    will serve, still BEFORE touching Steam. A failed download aborts here with
#    nothing changed. (This is headcrab's dlm step, from the same CDN.)
SERVE_DIR="$TMP_DIR/serve"
mkdir -p "$SERVE_DIR"
cp -f "$TMP_MANIFEST" "$SERVE_DIR/$MANIFEST_HOST_NAME"
if [[ "$ALREADY_THERE" == 0 ]]; then
    mapfile -t PKG_FILES < <(manifest_package_files "$TMP_MANIFEST")
    [[ ${#PKG_FILES[@]} -gt 0 ]] || die "manifest lists no package files — Steam left untouched"
    info "Downloading ${#PKG_FILES[@]} client packages for build $TARGET_VERSION (a few hundred MB)..."
    n=0
    for f in "${PKG_FILES[@]}"; do
        n=$((n + 1))
        got=0
        for base in "$PACKAGE_CDN" $PACKAGE_CDN_FALLBACK; do
            if fetch "${base%/}/$f" "$SERVE_DIR/$f" 2>/dev/null && [[ -s "$SERVE_DIR/$f" ]]; then got=1; break; fi
            rm -f "$SERVE_DIR/$f" 2>/dev/null
        done
        [[ "$got" == 1 ]] || die "could not download $f from $PACKAGE_CDN (nor the fallbacks) — Steam left untouched"
        printf '\r    %d/%d  %s' "$n" "${#PKG_FILES[@]}" "$f"
    done
    printf '\n'
    ok "All packages downloaded."
fi

# 4. Steam must be OFF, and verifiably so, before package/ or steam.cfg change.
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

# 5. Back up package/ (do NOT destroy it) and stage the target manifest in a fresh
#    one, keeping the branch file so the bootstrapper asks for the same branch.
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
[[ -n "$BRANCH" ]] && printf '%s' "$BRANCH" > "$PKG_DIR/beta"

# 6. Pin FIRST (headcrab's order), then the local server, then the relaunch.
info "Writing Steam-update pin (steam.cfg)..."
write_pin || { rollback_pkg; die "could not write pin at $STEAM_CFG — Steam package restored, NOT pinned"; }

info "Serving manifest + packages on http://127.0.0.1:${LOCAL_PORT}/ ..."
start_server || { restore_cfg; rollback_pkg; die "could not start the local package server on port $LOCAL_PORT (in use?) — steam.cfg and package/ restored, nothing changed"; }

# 7. Relaunch Steam headless against the local server so its bootstrapper
#    fetches the OLD manifest and the packages, installs them and exits
#    (-exitsteam: it does NOT start Steam afterwards). SLSsteam-audited when
#    present (headcrab's export_sls); vanilla otherwise. The launcher returns at
#    once, so wait for the bootstrapper to appear, then to be gone; only then is
#    steamclient.so final.
OVERRIDE_URL="http://127.0.0.1:${LOCAL_PORT}/"
LD_AUDIT_ARG=""
if [[ -s "$SLS_DIR/SLSsteam.so" ]]; then
    if [[ -s "$SLS_DIR/library-inject.so" ]]; then
        LD_AUDIT_ARG="$SLS_DIR/library-inject.so:$SLS_DIR/SLSsteam.so"
    else
        LD_AUDIT_ARG="$SLS_DIR/SLSsteam.so"
    fi
fi
info "Relaunching Steam to install build $TARGET_VERSION from the local server (headless)..."
log_mark
if [[ -n "$LD_AUDIT_ARG" ]]; then
    LD_AUDIT="$LD_AUDIT_ARG" steam_cmd -forcesteamupdate -forcepackagedownload \
        -overridepackageurl "$OVERRIDE_URL" -exitsteam >/dev/null 2>&1 || true
else
    steam_cmd -forcesteamupdate -forcepackagedownload \
        -overridepackageurl "$OVERRIDE_URL" -exitsteam >/dev/null 2>&1 || true
fi
# Appear: the bootstrapper logged its start, or a steam process exists.
appeared=0; deadline=$(( SECONDS + STEAM_START_TIMEOUT ))
while :; do
    if bootstrap_seen || steam_running; then appeared=1; break; fi
    (( SECONDS >= deadline )) && break
    sleep 1
done
if [[ "$appeared" -ne 1 ]]; then
    warn "No bootstrapper appeared within ${STEAM_START_TIMEOUT}s of the relaunch."
    # Fall through to the hash check: if nothing started, nothing changed, and
    # the failure path below undoes the staging.
else
    # Done: no steam process, and (when the log saw it start) the log saw it
    # exit too. Never undo package/ or the pin underneath a bootstrapper that is
    # still writing: at the timeout leave everything staged and let the user
    # re-run once it is done (a client already on the target just gets pinned).
    deadline=$(( SECONDS + STEAM_UPDATE_TIMEOUT )); finished=0
    while :; do
        if ! steam_running && { ! bootstrap_seen || bootstrap_done; }; then finished=1; break; fi
        (( SECONDS >= deadline )) && break
        sleep 1
    done
    if [[ "$finished" -ne 1 ]]; then
        warn "The bootstrapper is still running ${STEAM_UPDATE_TIMEOUT}s after the relaunch."
        warn "Nothing has been undone. Wait for the update window to finish, do NOT start Steam, then re-run this script."
        exit 2
    fi
    sleep 1
fi
stop_server

# 8. Verify by the ONLY thing that matters: the client binary on disk. Against the
#    whitelist hash when we have one, else against "it changed".
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
    die "Downgrade FAILED: steamclient.so is ${AFTER_SHA:0:12}… (expected ${EXPECTED_SHA:-a different file}; was ${BEFORE_SHA:0:12}…). The bootstrapper did not install build $TARGET_VERSION from the local server. steam.cfg and package/ have been RESTORED, so Steam starts and updates normally. Check ${STEAM_DIR}/logs/bootstrap_log.txt and retry."
fi

rm -rf "$BAK" 2>/dev/null || true
ok "steamclient.so now matches build $TARGET_VERSION (${AFTER_SHA:0:12}…)."
ok "Downgrade + pin applied. Steam is held at build $TARGET_VERSION."
warn "Steam is NOT running: the bootstrapper exits after installing (-exitsteam). Start it yourself (or let the LumaDeck hand-off return to Game Mode)."
info "Next: setup.sh re-establishes the wrapper, then start Steam; the QAM offers the update once the stack supports a newer build."
