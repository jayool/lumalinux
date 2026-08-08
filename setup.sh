#!/usr/bin/env bash
# lumalinux setup — self-contained installer for the unlock stack.
#
# Unlike install.sh (which requires SLSsteam already installed by Headcrab and
# patches Headcrab's steam.sh), this script installs the WHOLE stack itself and
# injects it via a WRAPPER reached from the Steam launchers — never by patching
# steam.sh. steam.sh is SHA-verified and re-extracted by Steam whenever its size
# differs from the manifest, so a block there does not survive Steam self-updates
# (see docs/slsteam-moon-findings.md M7, docs/decouple-headcrab-plan.md).
#
# What it does:
#   1. Fetches SLSsteam.so + library-inject.so (AceSLS/SLSsteam release, .7z),
#      cloud_redirect.so (Selectively11), liblumalinux.so (this repo), and the
#      netsock.so online-route lib (yesyes0649/steamnetsock-patch).
#   2. Installs the CloudRedirect GUI app (flatpak) for provider sign-in
#      (best-effort; skip with LUMA_SKIP_CR_APP=1).
#   3. Writes a wrapper at ~/.local/share/SLSsteam/path/steam that exports
#      LD_AUDIT (SLSsteam) + LD_PRELOAD (CloudRedirect + lumalinux), guards
#      against a boot crash-loop (fail-safe), and execs the real Steam.
#   4. Points the Steam launchers (steam / steam-jupiter / bazzite-steam) at the
#      wrapper (Desktop mode) and puts the wrapper dir first on PATH (Game Mode).
#   5. Installs a systemd --user guardian that re-asserts coverage after Steam
#      updates regenerate the .desktop entries.
#
# No root required. Desktop mode → patched .desktop; Game Mode → PATH drop-in
# (efficacy on gamescope boot is the one real-Deck test, see plan WS5). Not
# handled: Steam-as-Flatpak paths.
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/jayool/lumalinux/main/setup.sh | bash
#   curl -fsSL .../setup.sh | bash -s -- --uninstall
#
# Overrides (reversible self-testing):
#   LUMALINUX_SO_URL   install a specific liblumalinux.so (e.g. a -test asset)
#   SLSSTEAM_7Z_URL    install a specific SLSsteam-Any.7z
#   CLOUDREDIRECT_URL  install a specific cloud_redirect.so
#   NETSOCK_SO_URL     install a specific netsock fix.so
#   LUMA_SKIP_CR_APP=1 skip the CloudRedirect flatpak (GUI sign-in) install

set -euo pipefail

# ── config ─────────────────────────────────────────────────────────────────
REPO="jayool/lumalinux"

SLS_DIR="${HOME}/.local/share/SLSsteam"
SLS_CFG_DIR="${HOME}/.config/SLSsteam"
CR_DIR="${HOME}/.local/share/CloudRedirect"
LL_DIR="${HOME}/.local/share/lumalinux"
KEYS_DIR="${HOME}/.config/lumalinux"
TOOLS_DIR="${LL_DIR}/tools"
WRAPPER_DIR="${SLS_DIR}/path"
WRAPPER="${WRAPPER_DIR}/steam"
ENSURE_SCRIPT="${SLS_DIR}/ensure-desktop-coverage.sh"

LL_SO_NAME="liblumalinux.so"
LL_SO_URL="${LUMALINUX_SO_URL:-https://github.com/${REPO}/releases/latest/download/${LL_SO_NAME}}"
SLS_7Z_URL="${SLSSTEAM_7Z_URL:-https://github.com/AceSLS/SLSsteam/releases/latest/download/SLSsteam-Any.7z}"
CR_URL="${CLOUDREDIRECT_URL:-https://github.com/Selectively11/h3adcr-b/releases/download/linux-test/cloud_redirect.so}"
NETSOCK_SO_URL="${NETSOCK_SO_URL:-https://github.com/yesyes0649/steamnetsock-patch/releases/latest/download/fix.so}"
NETSOCK_DIR="${SLS_CFG_DIR}/tools/netsock"

CR_FLATPAK_ID="org.cloudredirect.CloudRedirect"
CR_FLATPAK_RUNTIME="${CR_FLATPAK_RUNTIME:-org.kde.Platform//6.10}"
CR_FLATPAKREPO="https://raw.githubusercontent.com/Selectively11/CloudRedirect/refs/heads/gh-pages/cloudredirect.flatpakrepo"
FLATHUB_REPO="https://dl.flathub.org/repo/flathub.flatpakrepo"

RUNTIME_TOOLS=( steamidra_lite.py vdf_inject_keys.py )
TOOLS_BASE_URL="https://raw.githubusercontent.com/${REPO}/main/tools"

# systemd --user guardian units.
SYSTEMD_USER_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
GUARD_SERVICE="lumalinux-desktop-guardian.service"
GUARD_PATH_UNIT="lumalinux-desktop-guardian.path"
GUARD_TIMER="lumalinux-desktop-guardian.timer"
GUARD_STATE_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/lumalinux"

# ── helpers ────────────────────────────────────────────────────────────────
c_info='\e[0;36m'; c_ok='\e[0;32m'; c_warn='\e[0;33m'; c_err='\e[0;31m'; c_rst='\e[0m'
info() { printf "${c_info}[*]${c_rst} %s\n" "$*"; }
ok()   { printf "${c_ok}[+]${c_rst} %s\n" "$*"; }
warn() { printf "${c_warn}[!]${c_rst} %s\n" "$*" >&2; }
die()  { printf "${c_err}[X]${c_rst} %s\n" "$*" >&2; exit 1; }

TMP_DIR=""
cleanup() { [[ -n "$TMP_DIR" && -d "$TMP_DIR" ]] && rm -rf "$TMP_DIR" || true; }
trap cleanup EXIT

first_cmd() { local c; for c in "$@"; do command -v "$c" 2>/dev/null && return 0; done; return 1; }

verify_i386_so() {
    local f="$1" name="$2"
    if command -v file &>/dev/null; then
        file "$f" | grep -qE "ELF 32-bit.*Intel (80386|i386)" \
            || die "$name is not a 32-bit i386 ELF shared object. Aborting."
    fi
}

# CloudRedirect GUI app via flatpak. Best-effort, skippable, never fatal.
install_cloudredirect_app() {
    [[ "${LUMA_SKIP_CR_APP:-0}" == "1" ]] && { info "Skipping CloudRedirect app (LUMA_SKIP_CR_APP=1)."; return 0; }
    if ! command -v flatpak &>/dev/null; then
        warn "flatpak not found — skipping CloudRedirect app (sign-in GUI). Cloud saves stay off until it is installed."
        return 0
    fi
    info "Installing CloudRedirect app (flatpak; may pull the KDE runtime)..."
    flatpak remote-add --user --if-not-exists cloudredirect "$CR_FLATPAKREPO" 2>/dev/null || true
    flatpak remote-add --user --if-not-exists flathub "$FLATHUB_REPO" 2>/dev/null || true
    if flatpak install --user -y --noninteractive flathub "$CR_FLATPAK_RUNTIME" 2>/dev/null \
       && flatpak install --user -y --noninteractive --reinstall "$CR_FLATPAK_ID" 2>/dev/null; then
        command -v update-desktop-database &>/dev/null \
            && update-desktop-database "${XDG_DATA_HOME:-$HOME/.local/share}/applications" 2>/dev/null || true
        ok "CloudRedirect app installed (sign into your provider from Desktop)."
    else
        warn "CloudRedirect app install failed (network/runtime) — the .so still works; sign-in GUI unavailable until installed."
    fi
}

# ── coverage script (deployed; also run by the guardian) ───────────────────
# Written to $ENSURE_SCRIPT. Owns .desktop patching + PATH drop-in (+ restore),
# so setup and the systemd guardian share one implementation.
write_ensure_script() {
    mkdir -p "$SLS_DIR"
    cat > "$ENSURE_SCRIPT" <<'ENSURE'
#!/usr/bin/env bash
# lumalinux launch coverage (managed by setup.sh — do not edit).
# Points every Steam launcher (steam / steam-jupiter / bazzite-steam) at the
# injection wrapper and keeps the wrapper dir first on PATH. Idempotent; run at
# install AND by the systemd guardian on every .desktop change.
# Modes: default/--user (verbose), --guardian (quiet), --uninstall (restore).
set -euo pipefail

WRAPPER="${WRAPPER:-$HOME/.local/share/SLSsteam/path/steam}"
DT_TAG="X-LumaLinux-Wrapped=1"
_LAUNCHER_RE='^(steam|steam-jupiter|bazzite-steam)$'
USER_APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
USER_AUTOSTART="${XDG_CONFIG_HOME:-$HOME/.config}/autostart"
PATH_MARK_BEGIN="# >>> lumalinux wrapper PATH >>> (managed by setup.sh)"
PATH_MARK_END="# <<< lumalinux wrapper PATH <<<"
PATH_LINE='export PATH="$HOME/.local/share/SLSsteam/path:$PATH"'

QUIET=0; MODE=apply
case "${1:-}" in
    --guardian) QUIET=1 ;;
    --uninstall) MODE=uninstall ;;
    --user|"") ;;
    *) ;;
esac
log() { [[ "$QUIET" -eq 1 ]] || printf '%s\n' "$*"; }

patch_desktop_inplace() {
    local f="$1" out
    grep -qF "$DT_TAG" "$f" 2>/dev/null && return 0
    out="$(awk -v w="$WRAPPER" -v re="$_LAUNCHER_RE" '
        function bn(p){ sub(/.*\//,"",p); return p }
        /^Exec=/ {
            cmd=$0; sub(/^Exec=/,"",cmd); prog=cmd; sub(/ .*/,"",prog)
            if (bn(prog) ~ re) { rest=cmd; sub(/^[^ ]*/,"",rest); print "Exec=" w rest; changed=1; next }
        }
        { print }
        END { exit (changed ? 0 : 3) }
    ' "$f")" || return 0
    cp -f "$f" "${f}.lumalinux.bak"
    printf '%s\n%s\n' "$out" "$DT_TAG" > "$f"
    log "[+] Wrapped $f"
}

create_override_shadow() {
    local src="$1" dest="$2"
    [[ -f "$dest" ]] && return 0
    awk -v w="$WRAPPER" -v tag="$DT_TAG" -v re="$_LAUNCHER_RE" '
        function bn(p){ sub(/.*\//,"",p); return p }
        /^Exec=/ {
            cmd=$0; sub(/^Exec=/,"",cmd); prog=cmd; sub(/ .*/,"",prog)
            if (bn(prog) ~ re) { rest=cmd; sub(/^[^ ]*/,"",rest); print "Exec=" w rest; next }
        }
        { print }
        END { print "# lumalinux-override-shadow"; print tag }
    ' "$src" > "$dest"
    log "[+] Created override $dest (shadows $src)"
}

install_path_dropin() {
    local rc found=0
    for rc in "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile"; do
        [[ -f "$rc" ]] || continue
        found=1
        grep -qF "$PATH_MARK_BEGIN" "$rc" 2>/dev/null && continue
        printf '\n%s\n%s\n%s\n' "$PATH_MARK_BEGIN" "$PATH_LINE" "$PATH_MARK_END" >> "$rc"
        log "[+] Added wrapper to PATH in $(basename "$rc")"
    done
    if [[ "$found" -eq 0 ]]; then
        printf '%s\n%s\n%s\n' "$PATH_MARK_BEGIN" "$PATH_LINE" "$PATH_MARK_END" > "$HOME/.bashrc"
        log "[+] Created ~/.bashrc with wrapper PATH"
    fi
}

restore_desktop_entries() {
    local f bak
    shopt -s nullglob
    for bak in "$USER_APPS/"*steam*.desktop.lumalinux.bak \
               "$USER_AUTOSTART/"*steam*.desktop.lumalinux.bak; do
        f="${bak%.lumalinux.bak}"; mv -f "$bak" "$f"; log "[+] Restored $f"
    done
    for f in "$USER_APPS/"*steam*.desktop "$USER_AUTOSTART/"*steam*.desktop; do
        [[ -f "$f" ]] || continue
        if grep -qF "$DT_TAG" "$f" 2>/dev/null && grep -qF "# lumalinux-override-shadow" "$f" 2>/dev/null; then
            rm -f "$f"; log "[+] Removed override $f"
        fi
    done
    shopt -u nullglob
}

remove_path_dropin() {
    local rc
    for rc in "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile"; do
        [[ -f "$rc" ]] || continue
        grep -qF "$PATH_MARK_BEGIN" "$rc" 2>/dev/null || continue
        sed -i "/^# >>> lumalinux wrapper PATH >>>/,/^# <<< lumalinux wrapper PATH <<</d" "$rc"
        log "[+] Removed PATH drop-in from $(basename "$rc")"
    done
}

if [[ "$MODE" == "uninstall" ]]; then
    restore_desktop_entries
    remove_path_dropin
    exit 0
fi

mkdir -p "$USER_APPS" "$USER_AUTOSTART"
shopt -s nullglob
for f in "$USER_APPS/"*steam*.desktop /usr/share/applications/*steam*.desktop; do
    [[ -f "$f" ]] || continue
    if [[ -w "$f" ]]; then patch_desktop_inplace "$f"
    else create_override_shadow "$f" "$USER_APPS/$(basename "$f")"; fi
done
for f in "$USER_AUTOSTART/"steam*.desktop; do
    [[ -f "$f" ]] && patch_desktop_inplace "$f"
done
[[ -f /etc/xdg/autostart/steam.desktop ]] \
    && create_override_shadow /etc/xdg/autostart/steam.desktop "$USER_AUTOSTART/steam.desktop"
shopt -u nullglob
install_path_dropin
log "[+] Coverage reconciled."
ENSURE
    chmod 0755 "$ENSURE_SCRIPT"
}

# ── systemd guardian ───────────────────────────────────────────────────────
# A .path watches the launcher directories and, on any change, runs the oneshot
# .service which re-asserts coverage (ensure-desktop-coverage.sh --guardian). A
# .timer reconciles periodically as a backstop. This is what survives a Steam
# update that regenerates its own .desktop entries. Best-effort: if there is no
# systemd --user session, we warn and rely on the per-launch re-assert instead.
have_user_systemd() { command -v systemctl &>/dev/null && systemctl --user show-environment &>/dev/null; }

install_guardian_units() {
    if ! have_user_systemd; then
        warn "No systemd --user session — guardian not installed (the wrapper re-asserts on each launch instead)."
        return 0
    fi
    mkdir -p "$SYSTEMD_USER_DIR"

    cat > "${SYSTEMD_USER_DIR}/${GUARD_SERVICE}" <<EOF
# X-LumaLinux-GuardianUnit=true
[Unit]
Description=Reconcile Steam launch coverage for lumalinux

[Service]
Type=oneshot
Nice=10
IOSchedulingClass=idle
ExecStart=%h/.local/share/SLSsteam/ensure-desktop-coverage.sh --guardian
EOF

    cat > "${SYSTEMD_USER_DIR}/${GUARD_PATH_UNIT}" <<EOF
# X-LumaLinux-GuardianUnit=true
[Unit]
Description=Watch Steam launch sources for lumalinux reconciliation

[Path]
PathChanged=%h/.local/share/applications
PathChanged=/usr/share/applications
PathChanged=%h/.config/autostart
PathChanged=/etc/xdg/autostart
Unit=${GUARD_SERVICE}

[Install]
WantedBy=default.target
EOF

    cat > "${SYSTEMD_USER_DIR}/${GUARD_TIMER}" <<EOF
# X-LumaLinux-GuardianUnit=true
[Unit]
Description=Periodic Steam launch coverage reconciliation for lumalinux

[Timer]
OnStartupSec=30s
OnUnitActiveSec=5min
Persistent=true
Unit=${GUARD_SERVICE}

[Install]
WantedBy=timers.target
EOF

    systemctl --user daemon-reload 2>/dev/null || true
    if systemctl --user enable --now "$GUARD_PATH_UNIT" "$GUARD_TIMER" 2>/dev/null; then
        ok "Guardian installed (watches launcher dirs + 5-min reconcile)."
    else
        warn "Guardian units written but could not be enabled — the wrapper still re-asserts on each launch."
    fi
}

remove_guardian_units() {
    have_user_systemd || return 0
    systemctl --user disable --now "$GUARD_PATH_UNIT" "$GUARD_TIMER" 2>/dev/null || true
    rm -f "${SYSTEMD_USER_DIR}/${GUARD_SERVICE}" \
          "${SYSTEMD_USER_DIR}/${GUARD_PATH_UNIT}" \
          "${SYSTEMD_USER_DIR}/${GUARD_TIMER}" 2>/dev/null || true
    systemctl --user daemon-reload 2>/dev/null || true
    systemctl --user reset-failed 2>/dev/null || true
    ok "Guardian removed."
}

# ── dependency check ───────────────────────────────────────────────────────
command -v curl &>/dev/null || die "curl is required."

# ── uninstall ──────────────────────────────────────────────────────────────
if [[ "${1:-}" == "--uninstall" ]]; then
    info "Uninstalling lumalinux wrapper stack..."
    remove_guardian_units
    if [[ -x "$ENSURE_SCRIPT" ]]; then
        "$ENSURE_SCRIPT" --uninstall || true
    fi
    rm -f "$ENSURE_SCRIPT" 2>/dev/null || true
    [[ -f "$WRAPPER" ]] && { rm -f "$WRAPPER"; ok "Removed $WRAPPER"; }
    [[ -d "$WRAPPER_DIR" ]] && rmdir "$WRAPPER_DIR" 2>/dev/null || true
    rm -rf "$GUARD_STATE_DIR" 2>/dev/null || true
    info "Kept the .so's, SLSsteam/lumalinux config, and keys.txt (delete manually if desired)."
    info "Restart Steam to return to a vanilla launch."
    exit 0
fi

SEVENZIP="$(first_cmd 7z 7za 7zr || true)"
[[ -n "$SEVENZIP" ]] || die "7z is required to extract SLSsteam-Any.7z (install p7zip / 7zip)."

# ── fetch ──────────────────────────────────────────────────────────────────
info "Installing lumalinux unlock stack (self-contained, wrapper injection)..."
TMP_DIR="$(mktemp -d)"
mkdir -p "$SLS_DIR" "$SLS_CFG_DIR" "$CR_DIR" "$LL_DIR" "$KEYS_DIR" "$WRAPPER_DIR"

# SLSsteam + library-inject (bundled in SLSsteam-Any.7z under bin/).
info "Downloading SLSsteam..."
curl -fL --progress-bar -o "${TMP_DIR}/sls.7z" "$SLS_7Z_URL" \
    || die "Failed to download SLSsteam-Any.7z ($SLS_7Z_URL)."
"$SEVENZIP" x -aoa -o"${TMP_DIR}/sls" "${TMP_DIR}/sls.7z" >/dev/null \
    || die "Failed to extract SLSsteam-Any.7z."
[[ -f "${TMP_DIR}/sls/bin/SLSsteam.so" ]] \
    || die "SLSsteam.so not found in the archive (upstream layout changed?)."
verify_i386_so "${TMP_DIR}/sls/bin/SLSsteam.so" "SLSsteam.so"
install -m 0755 "${TMP_DIR}/sls/bin/SLSsteam.so" "${SLS_DIR}/SLSsteam.so"
ok "Deployed ${SLS_DIR}/SLSsteam.so"
if [[ -f "${TMP_DIR}/sls/bin/library-inject.so" ]]; then
    install -m 0755 "${TMP_DIR}/sls/bin/library-inject.so" "${SLS_DIR}/library-inject.so"
    ok "Deployed ${SLS_DIR}/library-inject.so"
else
    warn "library-inject.so not in the archive — the wrapper will load SLSsteam.so alone."
fi
if [[ ! -f "${SLS_CFG_DIR}/config.yaml" && -f "${TMP_DIR}/sls/res/config.yaml" ]]; then
    install -m 0644 "${TMP_DIR}/sls/res/config.yaml" "${SLS_CFG_DIR}/config.yaml"
    ok "Seeded ${SLS_CFG_DIR}/config.yaml (default)"
fi

# CloudRedirect .so (inert until a provider is signed in) + GUI app.
info "Downloading CloudRedirect..."
if curl -fL --progress-bar -o "${TMP_DIR}/cloud_redirect.so" "$CR_URL"; then
    verify_i386_so "${TMP_DIR}/cloud_redirect.so" "cloud_redirect.so"
    install -m 0755 "${TMP_DIR}/cloud_redirect.so" "${CR_DIR}/cloud_redirect.so"
    ok "Deployed ${CR_DIR}/cloud_redirect.so"
else
    warn "CloudRedirect download failed — continuing without it (cloud saves stay off)."
fi
install_cloudredirect_app

# netsock (required on disk for LumaDeck's per-game online fixes).
info "Downloading netsock..."
mkdir -p "$NETSOCK_DIR"
if curl -fL --progress-bar -o "${TMP_DIR}/netsock.so" "$NETSOCK_SO_URL"; then
    verify_i386_so "${TMP_DIR}/netsock.so" "netsock.so"
    install -m 0755 "${TMP_DIR}/netsock.so" "${NETSOCK_DIR}/netsock.so"
    ok "Deployed ${NETSOCK_DIR}/netsock.so"
else
    warn "netsock download failed — per-game online (netsock) fixes will be unavailable."
fi

# lumalinux.so.
info "Downloading lumalinux..."
curl -fL --progress-bar -o "${TMP_DIR}/${LL_SO_NAME}" "$LL_SO_URL" \
    || die "Failed to download ${LL_SO_NAME} ($LL_SO_URL)."
verify_i386_so "${TMP_DIR}/${LL_SO_NAME}" "$LL_SO_NAME"
install -m 0755 "${TMP_DIR}/${LL_SO_NAME}" "${LL_DIR}/${LL_SO_NAME}"
ok "Deployed ${LL_DIR}/${LL_SO_NAME}"

# keys.txt + runtime tools.
if [[ ! -f "${KEYS_DIR}/keys.txt" ]]; then
    cat > "${KEYS_DIR}/keys.txt" <<'EOF'
# lumalinux depot key store
# Extended format (one per line):
#   <depot_id>;<parent_app_id>;<manifest_gid>;<manifest_size>;<64-hex-key>
# Lines starting with # are ignored. Populate with tools/steamidra_lite.py
# or via LumaDeck.
EOF
    ok "Created ${KEYS_DIR}/keys.txt (empty)"
fi
mkdir -p "$TOOLS_DIR"
for tool in "${RUNTIME_TOOLS[@]}"; do
    curl -fsSL -o "${TOOLS_DIR}/${tool}" "${TOOLS_BASE_URL}/${tool}" \
        && chmod 0755 "${TOOLS_DIR}/${tool}" && ok "Deployed ${TOOLS_DIR}/${tool}" \
        || die "Failed to download ${TOOLS_BASE_URL}/${tool}"
done

# ── write the wrapper (with the crash-loop fail-safe) ──────────────────────
# Single-quoted heredoc: everything expands at Steam-launch time, not now.
info "Writing injection wrapper at $WRAPPER..."
cat > "$WRAPPER" <<'WRAP'
#!/bin/sh
# lumalinux injection wrapper (managed by setup.sh — do not edit).
# Exports the loader env for the unlock stack, then execs the real Steam.
# SLSsteam -> LD_AUDIT (library-inject.so FIRST). CloudRedirect + lumalinux ->
# LD_PRELOAD (NEVER LD_AUDIT: CloudRedirect as an auditor corrupts the client
# heap). Reached via the patched *steam*.desktop Exec= lines and the PATH drop-in.
SLS_DIR="$HOME/.local/share/SLSsteam"
CR_SO="$HOME/.local/share/CloudRedirect/cloud_redirect.so"
LL_SO="$HOME/.local/share/lumalinux/liblumalinux.so"

# Resolve the REAL steam binary, skipping this wrapper.
_self="$HOME/.local/share/SLSsteam/path/steam"
STEAM_BIN=""
if [ -n "${LUMA_STEAM_BIN:-}" ] && [ -x "${LUMA_STEAM_BIN}" ]; then
    STEAM_BIN="$LUMA_STEAM_BIN"
else
    _old_ifs="$IFS"; IFS=:
    for _d in $PATH; do
        _c="$_d/steam"
        [ -x "$_c" ] || continue
        [ "$(readlink -f "$_c" 2>/dev/null)" = "$(readlink -f "$_self" 2>/dev/null)" ] && continue
        STEAM_BIN="$_c"; break
    done
    IFS="$_old_ifs"
    if [ -z "$STEAM_BIN" ]; then
        for _c in /usr/bin/steam /usr/games/steam /usr/lib/steam/steam; do
            [ -x "$_c" ] && { STEAM_BIN="$_c"; break; }
        done
    fi
fi
[ -n "$STEAM_BIN" ] || { echo "lumalinux wrapper: real steam binary not found" >&2; exit 1; }

# exec the real Steam completely vanilla (no injection) — used by the fail-safe.
exec_vanilla() { exec env -u LD_AUDIT -u LD_PRELOAD -u LD_LIBRARY_PATH "$STEAM_BIN" "$@"; }

# ── crash-loop fail-safe (Game Mode boot protection) ───────────────────────
# If the injected stack goes incompatible with a freshly-updated Steam and
# crashes at startup, a gamescope Game Mode session relaunches Steam forever and
# the user can never reach Desktop to update. This guard counts boots that
# crashed at startup (Steam wrote a crash_*.dmp within the first few minutes) and
# latches a vanilla launch past a threshold. A crash on a client that CHANGED
# since the last clean boot latches on the FIRST crash (a fresh client is the
# near-certain cause). Keyed off a real crash dump, never a merely-short session
# (Game Mode<->Desktop switches / self-update restarts / quick quits don't dump).
# Best-effort throughout: any probe failing degrades to "ok", never latches by
# accident. Auto-clears once the payload changes (user updated the stack).
GDIR="${XDG_STATE_HOME:-$HOME/.local/state}/lumalinux"
mkdir -p "$GDIR" 2>/dev/null || true
G_LAST="$GDIR/last_launch"; G_COUNT="$GDIR/boot_fail_count"
G_SAFE="$GDIR/safe_mode"; G_FP="$GDIR/safe_mode_fingerprint"
G_CLIENT_LAST="$GDIR/last_client"; G_CLIENT_GOOD="$GDIR/good_client"; G_LOG="$GDIR/guard.log"
: "${LUMA_GUARD_MAX_FAILS:=3}"; : "${LUMA_GUARD_STARTUP_SECS:=180}"; : "${LUMA_GUARD_DUMPS_DIR:=/tmp/dumps}"

g_log() { printf '%s %s\n' "$(date '+%F %T' 2>/dev/null)" "$1" >> "$G_LOG" 2>/dev/null || true; }
g_notify() { command -v notify-send >/dev/null 2>&1 && notify-send -u normal -t 10000 "Steam recovery mode" "$1" >/dev/null 2>&1 || true; }
g_int() { _v="$(cat "$1" 2>/dev/null)"; case "$_v" in ''|*[!0-9]*) printf 0 ;; *) printf '%s' "$_v" ;; esac; }
g_client_fp() {
    for _r in "$HOME/.steam/steam" "$HOME/.steam/debian-installation" "$HOME/.local/share/Steam"; do
        _c="$_r/ubuntu12_32/steamclient.so"
        [ -e "$_c" ] && { stat -c '%s:%Y' "$_c" 2>/dev/null || stat -f '%z:%m' "$_c" 2>/dev/null || printf '?'; return 0; }
    done; printf ''
}
g_fingerprint() {
    for _f in "$SLS_DIR/SLSsteam.so" "$CR_SO" "$LL_SO" "$HOME/.config/SLSsteam/tools/netsock/netsock.so"; do
        if [ -e "$_f" ]; then stat -c '%s:%Y' "$_f" 2>/dev/null || stat -f '%z:%m' "$_f" 2>/dev/null || printf '?'; else printf -- '-'; fi
        printf '|'
    done
}
g_startup_crash() {   # $1 = ref marker (boot start), $2 = boot start epoch
    [ -d "$LUMA_GUARD_DUMPS_DIR" ] || return 1
    case "$2" in ''|*[!0-9]*) return 1 ;; esac
    [ "$2" -gt 0 ] || return 1
    _ref="$GDIR/.crash_win_ref"
    touch -d "@$(( $2 + LUMA_GUARD_STARTUP_SECS ))" "$_ref" 2>/dev/null || { rm -f "$_ref" 2>/dev/null; return 1; }
    _hit="$(find "$LUMA_GUARD_DUMPS_DIR" -maxdepth 1 -name 'crash_*.dmp' -newer "$1" ! -newer "$_ref" 2>/dev/null | head -n1)"
    rm -f "$_ref" 2>/dev/null
    [ -n "$_hit" ]
}

G_CUR_FP="$(g_fingerprint)"
# Already latched? Stay vanilla until the payload changes.
if [ -f "$G_SAFE" ]; then
    if [ "$(cat "$G_FP" 2>/dev/null)" = "$G_CUR_FP" ]; then
        g_log "safe mode active -> launching Steam vanilla"; exec_vanilla "$@"
    fi
    g_log "payload changed since latch -> clearing safe mode, retrying injection"
    rm -f "$G_SAFE" "$G_FP" "$G_COUNT" "$G_LAST" 2>/dev/null || true
fi
# Assess the PREVIOUS boot.
G_FAILS="$(g_int "$G_COUNT")"; G_CLIENT_CUR="$(g_client_fp)"; g_client_changed=0
if [ -f "$G_LAST" ]; then
    _then="$(stat -c %Y "$G_LAST" 2>/dev/null || stat -f %m "$G_LAST" 2>/dev/null || echo 0)"
    if g_startup_crash "$G_LAST" "$_then"; then
        G_FAILS=$(( G_FAILS + 1 ))
        _prev="$(cat "$G_CLIENT_LAST" 2>/dev/null || true)"; _good="$(cat "$G_CLIENT_GOOD" 2>/dev/null || true)"
        if [ -n "$_good" ] && [ "$_prev" != "$_good" ]; then
            g_client_changed=1; g_log "steamclient.so changed since last clean boot -> first crash = compat break"
        fi
        g_log "previous boot crashed at startup -> fail ${G_FAILS}/${LUMA_GUARD_MAX_FAILS}"
    else
        [ "$G_FAILS" -ne 0 ] && g_log "previous boot ok -> reset fail count"
        G_FAILS=0
        _prev="$(cat "$G_CLIENT_LAST" 2>/dev/null || true)"
        [ -n "$_prev" ] && printf '%s' "$_prev" > "$G_CLIENT_GOOD" 2>/dev/null || true
    fi
fi
printf '%s' "$G_FAILS" > "$G_COUNT" 2>/dev/null || true
if [ "$G_FAILS" -ge "$LUMA_GUARD_MAX_FAILS" ] || { [ "$g_client_changed" = 1 ] && [ "$G_FAILS" -ge 1 ]; }; then
    g_log "latching safe mode (fails=${G_FAILS} client_changed=${g_client_changed}) -> vanilla"
    printf '%s' "$G_CUR_FP" > "$G_FP" 2>/dev/null || true
    : > "$G_SAFE" 2>/dev/null || true
    for _r in "$HOME/.steam/steam" "$HOME/.steam/debian-installation" "$HOME/.local/share/Steam"; do
        [ -f "$_r/appcache/appinfo.vdf" ] && rm -f "$_r/appcache/appinfo.vdf" 2>/dev/null && g_log "removed $_r/appcache/appinfo.vdf"
    done
    g_notify "lumalinux is paused because Steam failed to start after a recent update. Steam is running normally — update the stack to re-enable it."
    exec_vanilla "$@"
fi
# Mark the start of THIS boot + record the client it is about to run.
: > "$G_LAST" 2>/dev/null || true
printf '%s' "$G_CLIENT_CUR" > "$G_CLIENT_LAST" 2>/dev/null || true

# ── injection env ──────────────────────────────────────────────────────────
# CloudRedirect + lumalinux via LD_PRELOAD.
for _p in "$CR_SO" "$LL_SO"; do
    [ -f "$_p" ] && LD_PRELOAD="$_p${LD_PRELOAD:+:$LD_PRELOAD}"
done
[ -n "${LD_PRELOAD:-}" ] && export LD_PRELOAD
# Steam Input on Wayland: replicate the distro launcher's libextest preload.
if [ "${XDG_SESSION_TYPE:-}" = "wayland" ]; then
    for _e in /usr/lib/extest/libextest.so /usr/lib64/extest/libextest.so \
              /usr/lib/x86_64-linux-gnu/extest/libextest.so; do
        [ -f "$_e" ] && { export LD_PRELOAD="${LD_PRELOAD:+$LD_PRELOAD:}$_e"; break; }
    done
fi
# SLSsteam via LD_AUDIT (library-inject.so first if present).
if [ -f "$SLS_DIR/library-inject.so" ]; then
    export LD_AUDIT="$SLS_DIR/library-inject.so:$SLS_DIR/SLSsteam.so"
else
    export LD_AUDIT="$SLS_DIR/SLSsteam.so"
fi

# Best-effort: kick the guardian so coverage re-asserts even without the .path event.
if command -v systemctl >/dev/null 2>&1; then
    systemctl --user start lumalinux-desktop-guardian.service >/dev/null 2>&1 &
fi

exec "$STEAM_BIN" "$@"
WRAP
chmod 0755 "$WRAPPER"
ok "Wrapper written (with crash-loop fail-safe)."

# ── coverage + guardian ────────────────────────────────────────────────────
write_ensure_script
ok "Coverage helper written at $ENSURE_SCRIPT"
WRAPPER="$WRAPPER" "$ENSURE_SCRIPT" --user
install_guardian_units

ok "Done. Restart Steam to load the stack via the wrapper."
info "On startup, look for the toast: 'lumalinux: vX.Y.Z loaded - N/N hooks active'."
info "Desktop mode: covered by the patched .desktop entries + the guardian."
warn "Game Mode: covered by the PATH drop-in — VERIFY on a real Deck that gamescope"
warn "picks up the wrapper (log in to Desktop once so the shell rc files take effect)."
