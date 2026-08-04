#!/usr/bin/env bash
# Port-testing post-create. REUSES the shared .devcontainer/post-create.sh
# (never edits it), then adds the two port-specific bits:
#   1. put the LumaDeck checkout on the port branch and redeploy it,
#   2. install ~/validate-port.sh (the one-command PASS/FAIL validator).
# The root Arch env does NOT run this file, so it stays byte-identical.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
PORT_BRANCH="${LUMADECK_PORT_BRANCH:-claude/cachyos-support}"

# 1) Full shared setup: Arch + Steam + headless display + Decky + LumaDeck.
bash "$HERE/../post-create.sh"

# 2) Switch the LumaDeck checkout to the port branch and redeploy so the env
#    exercises the PORT code, not main.
if [ -d "$HOME/LumaDeck/.git" ]; then
    echo "== port-testing: switching LumaDeck to $PORT_BRANCH =="
    git -C "$HOME/LumaDeck" fetch origin "$PORT_BRANCH" 2>&1 | tail -1 || true
    if git -C "$HOME/LumaDeck" checkout "$PORT_BRANCH" 2>&1 | tail -1; then
        [ -x "$HOME/deploy-lumadeck.sh" ] && "$HOME/deploy-lumadeck.sh" 2>&1 | tail -3 || true
    else
        echo "  (couldn't checkout $PORT_BRANCH — does the branch exist on origin?)"
    fi
fi

# 3) Install the validator.
cp "$HERE/validate-port.sh" "$HOME/validate-port.sh"
chmod +x "$HOME/validate-port.sh"
cp "$HERE/verify-setup.sh" "$HOME/verify-setup.sh"
chmod +x "$HOME/verify-setup.sh"

# The shared post-create writes ~/README.dev.md describing an "Arch" base; this
# env is genuine CachyOS. Drop a note so that's not confusing.
cat >> "$HOME/README.dev.md" <<'EOF'

---
## NOTE: this is the CachyOS port-testing env (not Arch)

Base = **real CachyOS userland** (CachyOS optimized repos, `ID=cachyos`, and the
real `gamescope-session-cachyos` session scripts when the package installed).
Everything else — Steam, headless display/noVNC, Decky, the build/deploy scripts
— works exactly like the Arch env. What a container still can't do: run the
gamescope Game Mode compositor (no GPU/seat). The desktop-mode downgrade +
steam.sh behaviour (issue #31's primary symptom) IS reproducible here.
EOF

echo
echo "=============================================================="
echo " CachyOS port-testing env ready (real CachyOS userland)."
echo "   Verify the setup provisioned correctly:"
echo "     ~/verify-setup.sh"
echo "   Run the port validation (no Game Mode needed):"
echo "     ~/validate-port.sh"
echo "   Full live flow (real Steam + inject): ~/finish-setup.sh"
echo "=============================================================="
