#!/usr/bin/env bash
# lumalinux installer — downloads the .so from the latest GitHub release,
# deploys it under $HOME/.local/share/lumalinux, and patches Headcrab's
# user-local Steam launcher (~/.local/share/Steam/steam.sh) to load it via
# LD_PRELOAD. No root needed.
#
# Prerequisites: SLSsteam installed via enter-the-wired AND Steam launched
# once after that so Headcrab can bootstrap its steam.sh wrapper.
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/jayool/lumalinux/main/install.sh | bash
#   curl -fsSL https://raw.githubusercontent.com/jayool/lumalinux/main/install.sh | bash -s -- --uninstall

set -euo pipefail

# ── config ────────────────────────────────────────────────────────────────
REPO="jayool/lumalinux"
SO_NAME="liblumalinux.so"
SO_URL="https://github.com/${REPO}/releases/latest/download/${SO_NAME}"

INSTALL_DIR="${HOME}/.local/share/lumalinux"
KEYS_DIR="${HOME}/.config/lumalinux"
STEAM_SH="${HOME}/.local/share/Steam/steam.sh"
SLS_SO="${HOME}/.local/share/SLSsteam/SLSsteam.so"
CR_SO="${HOME}/.local/share/CloudRedirect/cloud_redirect.so"

MARKER_BEGIN="# >>> lumalinux launcher patch >>> (managed by install.sh - do not edit)"
MARKER_END="# <<< lumalinux launcher patch <<<"

# Anchor: where to insert the block inside Headcrab's GameLauncher(). The
# `source $STEAM_CLIENT "$@"` line is where steam.sh delegates to the vanilla
# Valve launcher; we insert right before it so any prior `export $INJECT_*`
# (SLSsteam, CloudRedirect) has already run.
INSERT_ANCHOR='source $STEAM_CLIENT'

# ── helpers ───────────────────────────────────────────────────────────────
c_info='\e[0;36m'; c_ok='\e[0;32m'; c_warn='\e[0;33m'; c_err='\e[0;31m'; c_rst='\e[0m'
info()  { printf "${c_info}[*]${c_rst} %s\n" "$*"; }
ok()    { printf "${c_ok}[+]${c_rst} %s\n" "$*"; }
warn()  { printf "${c_warn}[!]${c_rst} %s\n" "$*" >&2; }
die()   { printf "${c_err}[X]${c_rst} %s\n" "$*" >&2; exit 1; }

TMP_SO=""
NEW_STEAM_SH=""

cleanup() {
    [[ -n "$TMP_SO" && -f "$TMP_SO" ]] && rm -f "$TMP_SO" || true
    [[ -n "$NEW_STEAM_SH" && -f "$NEW_STEAM_SH" ]] && rm -f "$NEW_STEAM_SH" || true
}
trap cleanup EXIT

# ── pre-flight ────────────────────────────────────────────────────────────
[[ -f "$SLS_SO" ]] || die "SLSsteam is not installed (no $SLS_SO).
Install it first:
    curl -fsSL https://raw.githubusercontent.com/ciscosweater/enter-the-wired/main/enter-the-wired | bash"

[[ -f "$STEAM_SH" ]] || die "$STEAM_SH does not exist.
Run Steam once after installing SLSsteam so Headcrab can bootstrap its
launcher wrapper, then retry."

# Headcrab steam.sh always defines INJECT_SLS near the top. Vanilla Valve
# steam.sh does not. This catches the case where the user has a steam.sh but
# Headcrab never patched it (e.g. they ran enter-the-wired with the install
# failing midway).
if ! grep -q '^[[:space:]]*INJECT_SLS=' "$STEAM_SH"; then
    die "$STEAM_SH is not the Headcrab-patched launcher (no INJECT_SLS).
Reinstall SLSsteam via enter-the-wired and retry."
fi

# ── uninstall ─────────────────────────────────────────────────────────────
if [[ "${1:-}" == "--uninstall" ]]; then
    info "Uninstalling lumalinux..."

    if grep -qF "$MARKER_BEGIN" "$STEAM_SH" 2>/dev/null; then
        BACKUP="${STEAM_SH}.lumalinux.bak.$(date +%Y%m%d%H%M%S)"
        cp "$STEAM_SH" "$BACKUP"
        info "Backed up $STEAM_SH -> $BACKUP"

        chmod u+w "$STEAM_SH"
        sed -i "/^[[:space:]]*# >>> lumalinux launcher patch >>>/,/^[[:space:]]*# <<< lumalinux launcher patch <<</d" "$STEAM_SH"
        chmod 555 "$STEAM_SH"

        ok "Removed lumalinux managed block from $STEAM_SH"
    else
        info "$STEAM_SH has no lumalinux managed block — skipping"
    fi

    if [[ -f "${INSTALL_DIR}/${SO_NAME}" ]]; then
        rm -f "${INSTALL_DIR}/${SO_NAME}"
        ok "Removed ${INSTALL_DIR}/${SO_NAME}"
    fi

    info "Kept ${KEYS_DIR}/keys.txt (delete it manually if you want)"
    exit 0
fi

# ── install ───────────────────────────────────────────────────────────────
info "Installing lumalinux..."

mkdir -p "$INSTALL_DIR" "$KEYS_DIR"

# Download .so from latest release
info "Downloading $SO_NAME from latest release..."
TMP_SO="$(mktemp)"
if ! curl -fL --progress-bar -o "$TMP_SO" "$SO_URL"; then
    die "Failed to download $SO_URL — is a release published yet?"
fi

# Verify the download is a 32-bit ELF i386 shared object
if command -v file &>/dev/null; then
    file "$TMP_SO" | grep -qE "ELF 32-bit.*Intel (80386|i386)" \
        || die "Downloaded file is not a 32-bit ELF i386 shared object. Aborting."
else
    warn "'file' command unavailable; skipping sanity check on the download."
fi

# Deploy the .so
install -m 0755 "$TMP_SO" "${INSTALL_DIR}/${SO_NAME}"
rm -f "$TMP_SO"; TMP_SO=""
ok "Deployed ${INSTALL_DIR}/${SO_NAME}"

# Create keys.txt if absent
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

# ── patch ~/.local/share/Steam/steam.sh ───────────────────────────────────
# The export line we insert. Single-quoted so $HOME and the ${LD_PRELOAD:...}
# expansions go in literally — they get expanded by bash inside steam.sh at
# Steam launch time, not by THIS shell now.
#
# The ${LD_PRELOAD:+:}${LD_PRELOAD:-} suffix preserves whatever LD_PRELOAD
# already had (e.g. CloudRedirect's cloud_redirect.so set by Headcrab's
# INJECT_CR a few lines above). Without it, this export would clobber it.
EXPORT_LINE='export LD_PRELOAD="$HOME/.local/share/lumalinux/'"${SO_NAME}"'${LD_PRELOAD:+:}${LD_PRELOAD:-}"'

# If a managed block already exists and contains exactly this export, no-op.
if grep -qF "$MARKER_BEGIN" "$STEAM_SH"; then
    EXISTING_BLOCK=$(sed -n "/^[[:space:]]*# >>> lumalinux launcher patch >>>/,/^[[:space:]]*# <<< lumalinux launcher patch <<</p" "$STEAM_SH")
    if echo "$EXISTING_BLOCK" | grep -qF "$EXPORT_LINE"; then
        ok "$STEAM_SH managed block is already up to date. Nothing to patch."
        info "Restart Steam to ensure the .so is loaded."
        exit 0
    fi
    info "$STEAM_SH has a managed block but contents differ — rewriting."
fi

info "Patching $STEAM_SH..."

# Headcrab chmod 555's steam.sh to survive Steam's own self-update. We
# temporarily restore write permission, patch, then lock it back down.
chmod u+w "$STEAM_SH"

BACKUP="${STEAM_SH}.lumalinux.bak.$(date +%Y%m%d%H%M%S)"
cp "$STEAM_SH" "$BACKUP"
info "Backed up $STEAM_SH -> $BACKUP"

# Remove any stale managed block so we always insert clean.
sed -i "/^[[:space:]]*# >>> lumalinux launcher patch >>>/,/^[[:space:]]*# <<< lumalinux launcher patch <<</d" "$STEAM_SH"

# Block to insert.
BLOCK="${MARKER_BEGIN}
${EXPORT_LINE}
${MARKER_END}
"

# Insert the block right before the `source $STEAM_CLIENT` line. Preserve
# the leading indentation of that line so the new lines visually align with
# the surrounding `export $INJECT_*` block.
NEW_STEAM_SH="$(mktemp)"
awk -v block="$BLOCK" -v anchor="$INSERT_ANCHOR" '
    BEGIN { inserted=0 }
    index($0, anchor) > 0 && !inserted {
        match($0, /^[[:space:]]*/)
        indent = substr($0, 1, RLENGTH)
        n = split(block, lines, "\n")
        for (i = 1; i <= n; i++) {
            if (lines[i] != "")
                printf "%s%s\n", indent, lines[i]
        }
        inserted = 1
    }
    { print }
    END {
        if (!inserted) exit 1
    }
' "$STEAM_SH" > "$NEW_STEAM_SH" || die "Could not find '$INSERT_ANCHOR' in $STEAM_SH. Refusing to patch."

install -m 0555 "$NEW_STEAM_SH" "$STEAM_SH"
rm -f "$NEW_STEAM_SH"; NEW_STEAM_SH=""

ok "Patched $STEAM_SH with lumalinux LD_PRELOAD block."

if [[ -f "$CR_SO" ]]; then
    info "CloudRedirect detected — both will load (lumalinux preserves the existing LD_PRELOAD)."
fi

ok "Done. Restart Steam to load lumalinux."
info "On startup, look for a desktop toast: 'lumalinux: vX.Y.Z loaded - 4/4 hooks active'"
info "Re-run this installer after any Headcrab Updater run (it regenerates steam.sh)."
info "Backup: $BACKUP"
