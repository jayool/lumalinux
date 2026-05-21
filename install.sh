#!/usr/bin/env bash
# lumalinux installer — patches Steam's launcher script to load liblumalinux.so
# alongside whatever else is already preloaded (SLSsteam, etc.).
#
# Usage:  ./install.sh
#         ./install.sh --uninstall

set -euo pipefail

LUMA_SO_NAME="liblumalinux.so"
LUMA_INSTALL_DIR="${HOME}/.local/share/lumalinux"
LUMA_KEYS_DIR="${HOME}/.config/lumalinux"

detect_steam_sh() {
    # Native Steam install
    local candidates=(
        "${HOME}/.steam/steam/steam.sh"
        "${HOME}/.local/share/Steam/steam.sh"
        # Flatpak install
        "${HOME}/.var/app/com.valvesoftware.Steam/.steam/steam/steam.sh"
    )
    for c in "${candidates[@]}"; do
        if [[ -f "$c" ]]; then
            echo "$c"
            return 0
        fi
    done
    return 1
}

is_steam_running() {
    pgrep -x steam > /dev/null
}

uninstall() {
    local steam_sh
    if ! steam_sh="$(detect_steam_sh)"; then
        echo "Steam launcher script not found — nothing to uninstall." >&2
        exit 1
    fi

    if grep -q "lumalinux" "$steam_sh"; then
        sed -i.bak '/# lumalinux start/,/# lumalinux end/d' "$steam_sh"
        echo "Removed lumalinux block from $steam_sh"
        echo "Backup at ${steam_sh}.bak"
    else
        echo "lumalinux not found in $steam_sh — nothing to remove."
    fi

    if [[ -d "$LUMA_INSTALL_DIR" ]]; then
        echo "Install dir kept at $LUMA_INSTALL_DIR (delete manually if desired)"
    fi
}

install() {
    if is_steam_running; then
        echo "Steam is currently running. Exit Steam before running this script." >&2
        exit 1
    fi

    local steam_sh
    if ! steam_sh="$(detect_steam_sh)"; then
        echo "Steam launcher script not found in any known location." >&2
        echo "Expected at one of:" >&2
        echo "  ~/.steam/steam/steam.sh" >&2
        echo "  ~/.local/share/Steam/steam.sh" >&2
        echo "  ~/.var/app/com.valvesoftware.Steam/.steam/steam/steam.sh (Flatpak)" >&2
        exit 1
    fi
    echo "Detected steam.sh at: $steam_sh"

    # Find our .so — check current dir, build dir, install dir
    local so_path=""
    for candidate in \
        "$(pwd)/${LUMA_SO_NAME}" \
        "$(pwd)/build/${LUMA_SO_NAME}" \
        "$(pwd)/build/Release/${LUMA_SO_NAME}" \
        "${LUMA_INSTALL_DIR}/${LUMA_SO_NAME}"
    do
        if [[ -f "$candidate" ]]; then
            so_path="$candidate"
            break
        fi
    done

    if [[ -z "$so_path" ]]; then
        echo "${LUMA_SO_NAME} not found. Build it first:" >&2
        echo "  mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make" >&2
        exit 1
    fi
    echo "Found .so at: $so_path"

    # Copy to install dir
    mkdir -p "$LUMA_INSTALL_DIR"
    cp "$so_path" "$LUMA_INSTALL_DIR/${LUMA_SO_NAME}"
    chmod +x "$LUMA_INSTALL_DIR/${LUMA_SO_NAME}"
    echo "Installed to: $LUMA_INSTALL_DIR/${LUMA_SO_NAME}"

    # Ensure keys dir exists
    mkdir -p "$LUMA_KEYS_DIR"
    if [[ ! -f "$LUMA_KEYS_DIR/keys.txt" ]]; then
        cat > "$LUMA_KEYS_DIR/keys.txt" <<'EOF'
# lumalinux depot key store
#
# Format: <depot_id>;<64 hex chars representing 32-byte AES-256 key>
#
# One key per line. Lines starting with # are comments.
#
# Example:
#   246621;a1b2c3d4e5f6...   (64 hex chars total)
#
EOF
        echo "Created empty keys file at: $LUMA_KEYS_DIR/keys.txt"
    fi

    # Idempotency: remove any previous lumalinux block first
    if grep -q "# lumalinux start" "$steam_sh"; then
        sed -i.bak '/# lumalinux start/,/# lumalinux end/d' "$steam_sh"
    fi

    # Insert LD_PRELOAD extension near the top of steam.sh (right after the shebang)
    local tmpfile
    tmpfile="$(mktemp)"
    awk -v so="$LUMA_INSTALL_DIR/${LUMA_SO_NAME}" '
        BEGIN { inserted = 0 }
        /^#!/ && !inserted {
            print
            print ""
            print "# lumalinux start"
            print "export LD_PRELOAD=\"" so "${LD_PRELOAD:+:}$LD_PRELOAD\""
            print "# lumalinux end"
            print ""
            inserted = 1
            next
        }
        { print }
    ' "$steam_sh" > "$tmpfile"

    cp "$steam_sh" "${steam_sh}.bak"
    mv "$tmpfile" "$steam_sh"
    chmod +x "$steam_sh"

    echo "Patched $steam_sh (backup at ${steam_sh}.bak)"
    echo ""
    echo "Done. To activate:"
    echo "  1. Start Steam normally"
    echo "  2. Watch the log at ~/.cache/lumalinux/lumalinux.log"
    echo "  3. Populate $LUMA_KEYS_DIR/keys.txt with your depot keys"
}

if [[ "${1:-}" == "--uninstall" ]]; then
    uninstall
else
    install
fi
