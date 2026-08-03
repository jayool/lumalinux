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

echo
echo "=============================================================="
echo " port-testing env ready."
echo "   Run the port validation (no Game Mode needed):"
echo "     ~/validate-port.sh"
echo "   Full live flow (real Steam + inject): ~/finish-setup.sh"
echo "=============================================================="
