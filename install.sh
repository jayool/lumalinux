#!/usr/bin/env bash
# lumalinux installer — fetches the .so from the latest GitHub release,
# deploys it under $HOME/.local/share/lumalinux, and patches /usr/bin/steam
# to load it via LD_PRELOAD. Idempotent. Reversible with --uninstall.
#
# Designed for SteamOS / Steam Deck. Assumes /usr/bin/steam exists and
# already contains an LD_AUDIT line from SLSsteam (install SLSsteam first
# via enter-the-wired).
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
LAUNCHER="/usr/bin/steam"

MARKER_BEGIN="# >>> lumalinux launcher patch >>> (managed by install.sh - do not edit)"
MARKER_END="# <<< lumalinux launcher patch <<<"

# ── helpers ───────────────────────────────────────────────────────────────
c_info='\e[0;36m'; c_ok='\e[0;32m'; c_warn='\e[0;33m'; c_err='\e[0;31m'; c_rst='\e[0m'
info()  { printf "${c_info}[*]${c_rst} %s\n" "$*"; }
ok()    { printf "${c_ok}[+]${c_rst} %s\n" "$*"; }
warn()  { printf "${c_warn}[!]${c_rst} %s\n" "$*" >&2; }
die()   { printf "${c_err}[X]${c_rst} %s\n" "$*" >&2; exit 1; }

TMP_SO=""
NEW_LAUNCHER=""
READONLY_WAS_DISABLED=0

cleanup() {
    [[ -n "$TMP_SO" && -f "$TMP_SO" ]] && rm -f "$TMP_SO" || true
    [[ -n "$NEW_LAUNCHER" && -f "$NEW_LAUNCHER" ]] && rm -f "$NEW_LAUNCHER" || true
    if (( READONLY_WAS_DISABLED )) && command -v steamos-readonly &>/dev/null; then
        sudo steamos-readonly enable 2>/dev/null || true
    fi
}
trap cleanup EXIT

readonly_disable() {
    if command -v steamos-readonly &>/dev/null; then
        sudo steamos-readonly disable
        READONLY_WAS_DISABLED=1
    fi
}
readonly_enable() {
    if (( READONLY_WAS_DISABLED )) && command -v steamos-readonly &>/dev/null; then
        sudo steamos-readonly enable
        READONLY_WAS_DISABLED=0
    fi
}

# ── pre-flight ────────────────────────────────────────────────────────────
[[ -f "$LAUNCHER" ]] || die "$LAUNCHER does not exist. Is Steam installed?"

if ! grep -q "LD_AUDIT.*SLSsteam" "$LAUNCHER"; then
    die "SLSsteam is not patched into $LAUNCHER. Install it first:
    curl -fsSL https://raw.githubusercontent.com/ciscosweater/enter-the-wired/main/enter-the-wired | bash"
fi

# ── uninstall ─────────────────────────────────────────────────────────────
if [[ "${1:-}" == "--uninstall" ]]; then
    info "Uninstalling lumalinux..."

    if grep -qF "$MARKER_BEGIN" "$LAUNCHER" 2>/dev/null; then
        readonly_disable
        BACKUP="${LAUNCHER}.lumalinux.bak.$(date +%Y%m%d%H%M%S)"
        sudo cp "$LAUNCHER" "$BACKUP"
        info "Backed up $LAUNCHER -> $BACKUP"
        sudo sed -i "/^# >>> lumalinux launcher patch >>>/,/^# <<< lumalinux launcher patch <<</d" "$LAUNCHER"
        readonly_enable
        ok "Removed lumalinux managed block from $LAUNCHER"
    else
        info "$LAUNCHER has no lumalinux managed block - skipping"
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
    die "Failed to download $SO_URL - is a release published yet?"
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

# ── patch /usr/bin/steam ──────────────────────────────────────────────────
EXPECTED_LINE='export LD_PRELOAD="$HOME/.local/share/lumalinux/'"${SO_NAME}"'${LD_PRELOAD:+:}${LD_PRELOAD:-}"'

if grep -qF "$MARKER_BEGIN" "$LAUNCHER"; then
    EXISTING_BLOCK=$(sed -n "/^# >>> lumalinux launcher patch >>>/,/^# <<< lumalinux launcher patch <<</p" "$LAUNCHER")
    if echo "$EXISTING_BLOCK" | grep -qF "$EXPECTED_LINE"; then
        ok "$LAUNCHER managed block is already up to date. Nothing to patch."
        info "Restart Steam to ensure the .so is loaded."
        exit 0
    fi
    info "$LAUNCHER has a managed block but contents differ - rewriting."
fi

info "Patching $LAUNCHER (needs sudo)..."
readonly_disable

BACKUP="${LAUNCHER}.lumalinux.bak.$(date +%Y%m%d%H%M%S)"
sudo cp "$LAUNCHER" "$BACKUP"
info "Backed up $LAUNCHER -> $BACKUP"

# If a stale managed block exists, remove it first so we always insert clean
if grep -qF "$MARKER_BEGIN" "$LAUNCHER"; then
    sudo sed -i "/^# >>> lumalinux launcher patch >>>/,/^# <<< lumalinux launcher patch <<</d" "$LAUNCHER"
fi

# Build the block to insert. Single-quoted so $HOME and ${LD_PRELOAD:...}
# are NOT expanded by THIS shell - they go into /usr/bin/steam literally
# and get expanded by bash when Steam launches.
BLOCK="${MARKER_BEGIN}
${EXPECTED_LINE}
${MARKER_END}
"

# Insert the block right before the first 'exec /usr/lib/steam ...' or
# 'exec steam ...' line. awk does this in one pass; we write to a tmp file
# then sudo-mv into place.
NEW_LAUNCHER="$(mktemp)"
awk -v block="$BLOCK" '
    BEGIN { inserted=0 }
    /^exec[[:space:]]+(\/usr\/lib\/)?steam/ && !inserted {
        printf "%s", block
        inserted=1
    }
    { print }
    END {
        if (!inserted) exit 1
    }
' "$LAUNCHER" > "$NEW_LAUNCHER" || die "Could not find an 'exec .../steam' line in $LAUNCHER. Refusing to patch."

sudo install -m 0755 "$NEW_LAUNCHER" "$LAUNCHER"
rm -f "$NEW_LAUNCHER"; NEW_LAUNCHER=""
readonly_enable

ok "Patched $LAUNCHER with lumalinux LD_PRELOAD block."
ok "Done. Restart Steam to load lumalinux."
info "On startup, look for a desktop toast: 'lumalinux: vX.Y.Z loaded - 4/4 hooks active'"
info "If anything goes wrong, restore from backup: sudo cp $BACKUP $LAUNCHER"
