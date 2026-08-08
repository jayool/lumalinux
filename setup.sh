#!/usr/bin/env bash
# lumalinux setup — self-contained installer for the unlock stack.
#
# Unlike install.sh (which requires SLSsteam already installed by Headcrab and
# patches Headcrab's steam.sh), this script installs the WHOLE stack itself and
# injects it via a WRAPPER reached from the Steam .desktop entries — never by
# patching steam.sh. steam.sh is SHA-verified and re-extracted by Steam whenever
# its size differs from the manifest, so a block there does not survive Steam
# self-updates (see docs/slsteam-moon-findings.md M7, docs/decouple-headcrab-plan.md).
#
# What it does:
#   1. Fetches SLSsteam.so + library-inject.so (AceSLS/SLSsteam release, .7z),
#      cloud_redirect.so (Selectively11), liblumalinux.so (this repo), and the
#      netsock.so online-route lib (yesyes0649/steamnetsock-patch).
#   2. Installs the CloudRedirect GUI app (flatpak) for provider sign-in
#      (best-effort; skip with LUMA_SKIP_CR_APP=1).
#   3. Writes a wrapper at ~/.local/share/SLSsteam/path/steam that exports
#      LD_AUDIT (SLSsteam) + LD_PRELOAD (CloudRedirect + lumalinux) and execs
#      the real Steam.
#   4. Points the Steam .desktop entries (steam / steam-jupiter / bazzite-steam)
#      at the wrapper — Desktop-mode launch.
#   5. Adds a PATH drop-in (wrapper dir first on PATH via the shell rc files) —
#      the Game Mode / gamescope launch resolves `steam` from PATH.
#
# No root required. Desktop mode is covered by the patched .desktop entries; Game
# Mode by the PATH drop-in (efficacy on gamescope boot is the one real-Deck test,
# see decouple-headcrab-plan.md WS5). Still follow-ups (WS1.2): a systemd re-assert
# guardian and the crash fail-safe. Steam-as-Flatpak paths are not handled.
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/jayool/lumalinux/main/setup.sh | bash
#   curl -fsSL .../setup.sh | bash -s -- --uninstall
#
# Overrides (reversible self-testing):
#   LUMALINUX_SO_URL   install a specific liblumalinux.so (e.g. a -test asset)
#   SLSSTEAM_7Z_URL    install a specific SLSsteam-Any.7z
#   CLOUDREDIRECT_URL  install a specific cloud_redirect.so

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

LL_SO_NAME="liblumalinux.so"
LL_SO_URL="${LUMALINUX_SO_URL:-https://github.com/${REPO}/releases/latest/download/${LL_SO_NAME}}"
SLS_7Z_URL="${SLSSTEAM_7Z_URL:-https://github.com/AceSLS/SLSsteam/releases/latest/download/SLSsteam-Any.7z}"
CR_URL="${CLOUDREDIRECT_URL:-https://github.com/Selectively11/h3adcr-b/releases/download/linux-test/cloud_redirect.so}"

# netsock (native SteamNetworkingSockets online route). LumaDeck's Online Fixes
# consume this .so from ~/.config/SLSsteam/tools/netsock/netsock.so — it must be
# on disk or per-game netsock fixes fail ("run Install Dependencies first").
NETSOCK_SO_URL="${NETSOCK_SO_URL:-https://github.com/yesyes0649/steamnetsock-patch/releases/latest/download/fix.so}"
NETSOCK_DIR="${SLS_CFG_DIR}/tools/netsock"

# CloudRedirect GUI app (flatpak). The .so does the redirect at runtime, but
# signing into a cloud-save provider is GUI-only inside this flatpak.
CR_FLATPAK_ID="org.cloudredirect.CloudRedirect"
CR_FLATPAK_RUNTIME="${CR_FLATPAK_RUNTIME:-org.kde.Platform//6.10}"
CR_FLATPAKREPO="https://raw.githubusercontent.com/Selectively11/CloudRedirect/refs/heads/gh-pages/cloudredirect.flatpakrepo"
FLATHUB_REPO="https://dl.flathub.org/repo/flathub.flatpakrepo"

RUNTIME_TOOLS=( steamidra_lite.py vdf_inject_keys.py )
TOOLS_BASE_URL="https://raw.githubusercontent.com/${REPO}/main/tools"

# Tag we drop into every .desktop we patch, so we can find + undo exactly ours.
DT_TAG="X-LumaLinux-Wrapped=1"

# ── helpers ────────────────────────────────────────────────────────────────
c_info='\e[0;36m'; c_ok='\e[0;32m'; c_warn='\e[0;33m'; c_err='\e[0;31m'; c_rst='\e[0m'
info() { printf "${c_info}[*]${c_rst} %s\n" "$*"; }
ok()   { printf "${c_ok}[+]${c_rst} %s\n" "$*"; }
warn() { printf "${c_warn}[!]${c_rst} %s\n" "$*" >&2; }
die()  { printf "${c_err}[X]${c_rst} %s\n" "$*" >&2; exit 1; }

TMP_DIR=""
cleanup() { [[ -n "$TMP_DIR" && -d "$TMP_DIR" ]] && rm -rf "$TMP_DIR" || true; }
trap cleanup EXIT

# First available of a set of commands, or empty.
first_cmd() { local c; for c in "$@"; do command -v "$c" 2>/dev/null && return 0; done; return 1; }

# Verify a file is a 32-bit i386 ELF shared object (best-effort; skipped if no `file`).
verify_i386_so() {
    local f="$1" name="$2"
    if command -v file &>/dev/null; then
        file "$f" | grep -qE "ELF 32-bit.*Intel (80386|i386)" \
            || die "$name is not a 32-bit i386 ELF shared object. Aborting."
    fi
}

# Install the CloudRedirect GUI app via flatpak. Best-effort: it is heavy (pulls
# the KDE runtime), skippable with LUMA_SKIP_CR_APP=1, and never fatal — the .so
# already redirects at runtime; only the provider sign-in needs this GUI.
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

# ── dependency check ───────────────────────────────────────────────────────
command -v curl &>/dev/null || die "curl is required."
SEVENZIP="$(first_cmd 7z 7za 7zr || true)"
[[ -n "$SEVENZIP" ]] || die "7z is required to extract SLSsteam-Any.7z (install p7zip / 7zip)."

# ── uninstall ──────────────────────────────────────────────────────────────
# Restore every .desktop we patched from its backup, then drop the wrapper.
# We do NOT delete the .so's config/keys — those are the user's data.
restore_desktop_entries() {
    local restored=0 f bak
    # Our backups are "<file>.lumalinux.bak" next to each patched entry.
    shopt -s nullglob
    for bak in \
        "${HOME}/.local/share/applications/"*steam*.desktop.lumalinux.bak \
        "${HOME}/.config/autostart/"*steam*.desktop.lumalinux.bak; do
        f="${bak%.lumalinux.bak}"
        mv -f "$bak" "$f"
        ok "Restored $f"
        restored=1
    done
    # User-local OVERRIDE shadows we created (no prior file existed) are tagged;
    # delete them outright instead of restoring.
    for f in \
        "${HOME}/.local/share/applications/"*steam*.desktop \
        "${HOME}/.config/autostart/"*steam*.desktop; do
        [[ -f "$f" ]] || continue
        if grep -qF "$DT_TAG" "$f" 2>/dev/null && grep -qF "# lumalinux-override-shadow" "$f" 2>/dev/null; then
            rm -f "$f"
            ok "Removed lumalinux override $f"
            restored=1
        fi
    done
    shopt -u nullglob
    [[ "$restored" -eq 1 ]] || info "No patched .desktop entries found."
}

# PATH drop-in — put the wrapper dir first on PATH via the shell rc files. This is
# what covers the SteamOS **Game Mode / gamescope** launch, which resolves `steam`
# from PATH rather than from a clicked .desktop. Idempotent + removable.
# (Efficacy on Game Mode boot is the one thing that needs a real-Deck test: it
# assumes the gamescope session inherits this PATH and resolves steam via PATH.)
PATH_MARK_BEGIN="# >>> lumalinux wrapper PATH >>> (managed by setup.sh)"
PATH_MARK_END="# <<< lumalinux wrapper PATH <<<"
PATH_LINE='export PATH="$HOME/.local/share/SLSsteam/path:$PATH"'

install_path_dropin() {
    local rc found=0
    for rc in "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile"; do
        [[ -f "$rc" ]] || continue
        found=1
        if grep -qF "$PATH_MARK_BEGIN" "$rc" 2>/dev/null; then
            info "PATH drop-in already in $(basename "$rc")"
        else
            printf '\n%s\n%s\n%s\n' "$PATH_MARK_BEGIN" "$PATH_LINE" "$PATH_MARK_END" >> "$rc"
            ok "Added wrapper to PATH in $(basename "$rc")"
        fi
    done
    if [[ "$found" -eq 0 ]]; then
        printf '%s\n%s\n%s\n' "$PATH_MARK_BEGIN" "$PATH_LINE" "$PATH_MARK_END" > "$HOME/.bashrc"
        ok "Created ~/.bashrc with wrapper PATH"
    fi
}

remove_path_dropin() {
    local rc
    for rc in "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile"; do
        [[ -f "$rc" ]] || continue
        if grep -qF "$PATH_MARK_BEGIN" "$rc" 2>/dev/null; then
            sed -i "/^# >>> lumalinux wrapper PATH >>>/,/^# <<< lumalinux wrapper PATH <<</d" "$rc"
            ok "Removed PATH drop-in from $(basename "$rc")"
        fi
    done
}

if [[ "${1:-}" == "--uninstall" ]]; then
    info "Uninstalling lumalinux wrapper stack..."
    restore_desktop_entries
    remove_path_dropin
    [[ -f "$WRAPPER" ]] && { rm -f "$WRAPPER"; ok "Removed $WRAPPER"; }
    [[ -d "$WRAPPER_DIR" ]] && rmdir "$WRAPPER_DIR" 2>/dev/null || true
    info "Kept the .so's, SLSsteam/lumalinux config, and keys.txt (delete manually if desired)."
    info "Restart Steam to return to a vanilla launch."
    exit 0
fi

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
# Seed SLSsteam config only if absent (never clobber a config LumaDeck/the user manages).
if [[ ! -f "${SLS_CFG_DIR}/config.yaml" && -f "${TMP_DIR}/sls/res/config.yaml" ]]; then
    install -m 0644 "${TMP_DIR}/sls/res/config.yaml" "${SLS_CFG_DIR}/config.yaml"
    ok "Seeded ${SLS_CFG_DIR}/config.yaml (default)"
fi

# CloudRedirect (optional but bundled by default — inert until a provider is set).
info "Downloading CloudRedirect..."
if curl -fL --progress-bar -o "${TMP_DIR}/cloud_redirect.so" "$CR_URL"; then
    verify_i386_so "${TMP_DIR}/cloud_redirect.so" "cloud_redirect.so"
    install -m 0755 "${TMP_DIR}/cloud_redirect.so" "${CR_DIR}/cloud_redirect.so"
    ok "Deployed ${CR_DIR}/cloud_redirect.so"
else
    warn "CloudRedirect download failed — continuing without it (cloud saves stay off)."
fi

# CloudRedirect GUI app (flatpak) — for provider sign-in. Best-effort/skippable.
install_cloudredirect_app

# netsock (native online route) — required on disk for LumaDeck's per-game
# netsock fixes; headcrab used to be the only thing that fetched it.
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

# keys.txt + runtime tools (same as install.sh — a bare .so can't be fed keys).
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

# ── write the wrapper ──────────────────────────────────────────────────────
# Single-quoted heredoc: everything expands at Steam-launch time, not now.
info "Writing injection wrapper at $WRAPPER..."
cat > "$WRAPPER" <<'WRAP'
#!/bin/sh
# lumalinux injection wrapper (managed by setup.sh — do not edit).
# Exports the loader env for the unlock stack, then execs the real Steam.
# SLSsteam -> LD_AUDIT (library-inject.so FIRST). CloudRedirect + lumalinux ->
# LD_PRELOAD (NEVER LD_AUDIT: CloudRedirect as an auditor corrupts the client
# heap). Reached via the patched *steam*.desktop Exec= lines.
SLS_DIR="$HOME/.local/share/SLSsteam"
CR_SO="$HOME/.local/share/CloudRedirect/cloud_redirect.so"
LL_SO="$HOME/.local/share/lumalinux/liblumalinux.so"

# Drop our own auditors from an inherited LD_AUDIT (a re-entrant launch must not
# stack them); preserve any third-party auditor.
_inherited_audit=""
_old_ifs="$IFS"; IFS=:
for _a in ${LD_AUDIT:-}; do
    case "$_a" in
        ''|*/SLSsteam.so|*/library-inject.so) : ;;
        *) _inherited_audit="${_inherited_audit:+$_inherited_audit:}$_a" ;;
    esac
done
IFS="$_old_ifs"

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

# CloudRedirect + lumalinux via LD_PRELOAD.
for _p in "$CR_SO" "$LL_SO"; do
    [ -f "$_p" ] && LD_PRELOAD="$_p${LD_PRELOAD:+:$LD_PRELOAD}"
done
[ -n "${LD_PRELOAD:-}" ] && export LD_PRELOAD

# Steam Input on Wayland: replicate the distro launcher's libextest preload
# (we exec steam directly, so we must add it ourselves).
if [ "${XDG_SESSION_TYPE:-}" = "wayland" ]; then
    for _e in /usr/lib/extest/libextest.so /usr/lib64/extest/libextest.so \
              /usr/lib/x86_64-linux-gnu/extest/libextest.so; do
        [ -f "$_e" ] && { export LD_PRELOAD="${LD_PRELOAD:+$LD_PRELOAD:}$_e"; break; }
    done
fi

# SLSsteam via LD_AUDIT (library-inject.so first if present).
if [ -f "$SLS_DIR/library-inject.so" ]; then
    _audit="$SLS_DIR/library-inject.so:$SLS_DIR/SLSsteam.so"
else
    _audit="$SLS_DIR/SLSsteam.so"
fi
if [ -n "$_inherited_audit" ]; then
    export LD_AUDIT="$_audit:$_inherited_audit"
else
    export LD_AUDIT="$_audit"
fi

exec "$STEAM_BIN" "$@"
WRAP
chmod 0755 "$WRAPPER"
ok "Wrapper written."

# ── coverage: point Steam .desktop entries at the wrapper ──────────────────
# Rewrite Exec= (keeping trailing field codes like %U) and tag the file so we can
# undo exactly ours. For a system entry we can't write, drop a user-local
# override of the same basename (XDG precedence makes it win) marked as a shadow.
USER_APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
USER_AUTOSTART="${XDG_CONFIG_HOME:-$HOME/.config}/autostart"
mkdir -p "$USER_APPS" "$USER_AUTOSTART"

# Steam launcher basenames we rewrite: generic + SteamOS Game Mode
# (steam-jupiter) + Bazzite (bazzite-steam). Matched on the Exec= program token,
# so an Exec whose ARGS merely mention "steam" is left alone.
_LAUNCHER_RE='^(steam|steam-jupiter|bazzite-steam)$'

# Rewrite the Exec= program token to the wrapper on a writable .desktop, keeping
# args. Only backs up + tags if a real launcher line was actually rewritten.
patch_desktop_inplace() {
    local f="$1" out
    grep -qF "$DT_TAG" "$f" 2>/dev/null && { info "Already wrapped: $f"; return 0; }
    out="$(awk -v w="$WRAPPER" -v re="$_LAUNCHER_RE" '
        function bn(p){ sub(/.*\//,"",p); return p }
        /^Exec=/ {
            cmd=$0; sub(/^Exec=/,"",cmd); prog=cmd; sub(/ .*/,"",prog)
            if (bn(prog) ~ re) { rest=cmd; sub(/^[^ ]*/,"",rest); print "Exec=" w rest; changed=1; next }
        }
        { print }
        END { exit (changed ? 0 : 3) }
    ' "$f")" || return 0   # exit 3 -> no steam launcher line here -> skip
    cp -f "$f" "${f}.lumalinux.bak"
    printf '%s\n%s\n' "$out" "$DT_TAG" > "$f"
    ok "Wrapped $f"
}

# Create a user-local override shadowing a system entry we can't modify.
create_override_shadow() {
    local src="$1" dest="$2"
    [[ -f "$dest" ]] && return 0   # a real/user entry already there; leave it to patch_desktop_inplace
    awk -v w="$WRAPPER" -v tag="$DT_TAG" -v re="$_LAUNCHER_RE" '
        function bn(p){ sub(/.*\//,"",p); return p }
        /^Exec=/ {
            cmd=$0; sub(/^Exec=/,"",cmd); prog=cmd; sub(/ .*/,"",prog)
            if (bn(prog) ~ re) { rest=cmd; sub(/^[^ ]*/,"",rest); print "Exec=" w rest; next }
        }
        { print }
        END { print "# lumalinux-override-shadow"; print tag }
    ' "$src" > "$dest"
    ok "Created override $dest (shadows $src)"
}

covered=0
shopt -s nullglob
# 1. Writable user + system application entries.
for f in "$USER_APPS/"*steam*.desktop /usr/share/applications/*steam*.desktop; do
    [[ -f "$f" ]] || continue
    if [[ -w "$f" ]]; then
        patch_desktop_inplace "$f"; covered=1
    else
        create_override_shadow "$f" "$USER_APPS/$(basename "$f")"; covered=1
    fi
done
# 2. Autostart (Desktop session auto-launches Steam via /etc/xdg/autostart).
for f in "$USER_AUTOSTART/"steam*.desktop; do
    [[ -f "$f" ]] && { patch_desktop_inplace "$f"; covered=1; }
done
if [[ -f /etc/xdg/autostart/steam.desktop ]]; then
    create_override_shadow /etc/xdg/autostart/steam.desktop "$USER_AUTOSTART/steam.desktop"; covered=1
fi
shopt -u nullglob

[[ "$covered" -eq 1 ]] || warn "No steam .desktop entries found to wrap — is Steam installed?"

# PATH drop-in — the Game Mode / gamescope coverage layer.
install_path_dropin

ok "Done. Restart Steam to load the stack via the wrapper."
info "On startup, look for the toast: 'lumalinux: vX.Y.Z loaded - N/N hooks active'."
info "Desktop mode: covered by the patched .desktop entries."
warn "Game Mode: covered by the PATH drop-in — VERIFY on a real Deck that gamescope"
warn "picks up the wrapper (log in to Desktop once so the shell rc files take effect)."
