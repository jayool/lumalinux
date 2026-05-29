#!/usr/bin/env bash
# lumalinux installer.
#
# Deploys liblumalinux.so to ~/.local/share/lumalinux and creates the keys dir.
# It does NOT edit the Steam launcher automatically: the launcher path/format
# varies (SteamOS patches /usr/bin/steam; other setups use steam.sh), and the
# correct, working load method is LD_PRELOAD added right before the final exec.
# This script prints the exact line to add.
#
# Usage:  ./install.sh            # deploy the .so + dirs, print the launcher line
#         ./install.sh --uninstall
set -euo pipefail

SO_NAME="liblumalinux.so"
INSTALL_DIR="${HOME}/.local/share/lumalinux"
KEYS_DIR="${HOME}/.config/lumalinux"

if [[ "${1:-}" == "--uninstall" ]]; then
    rm -f "${INSTALL_DIR}/${SO_NAME}"
    echo "Removed ${INSTALL_DIR}/${SO_NAME}."
    echo "Also remove the lumalinux LD_PRELOAD line you added to your Steam launcher."
    exit 0
fi

# locate the built .so
so_path=""
for c in "$(pwd)/${SO_NAME}" "$(pwd)/build/${SO_NAME}" "${INSTALL_DIR}/${SO_NAME}"; do
    [[ -f "$c" ]] && { so_path="$c"; break; }
done
if [[ -z "$so_path" ]]; then
    echo "ERROR: ${SO_NAME} not found. Build it first:" >&2
    echo "  mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j" >&2
    exit 1
fi

mkdir -p "$INSTALL_DIR" "$KEYS_DIR"
cp "$so_path" "${INSTALL_DIR}/${SO_NAME}"
chmod +x "${INSTALL_DIR}/${SO_NAME}"
echo "Deployed: ${INSTALL_DIR}/${SO_NAME}"

if [[ ! -f "${KEYS_DIR}/keys.txt" ]]; then
    cat > "${KEYS_DIR}/keys.txt" <<'EOF'
# lumalinux depot key store
# Extended format (one per line):
#   <depot_id>;<parent_app_id>;<manifest_gid>;<manifest_size>;<64-hex-key>
# Lines starting with # are ignored. Populate with tools/steamidra_lite.py.
EOF
    echo "Created empty keys file: ${KEYS_DIR}/keys.txt"
fi

cat <<EOF

Now make Steam load it via LD_PRELOAD. Add this line to your Steam launcher
(on SteamOS: /usr/bin/steam — run 'sudo steamos-readonly disable' first), just
BEFORE the final 'exec .../steam ...' line, after any SLSsteam LD_AUDIT export:

    export LD_PRELOAD="\$HOME/.local/share/lumalinux/${SO_NAME}\${LD_PRELOAD:+:}\${LD_PRELOAD:-}"

Then restart Steam. Log: ~/.cache/lumalinux/lumalinux.log
Set up a game with: python3 tools/steamidra_lite.py <appid>.zip
EOF
