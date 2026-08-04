#!/usr/bin/env bash
# verify-setup.sh — sanity-check that the CachyOS port-testing env came up with
# EVERYTHING the Dockerfile + post-create were supposed to create. Read-only, no
# side effects. Run it after the codespace is built:
#
#   bash /workspaces/lumalinux/.devcontainer/port-testing/verify-setup.sh
#   # (or ~/verify-setup.sh once post-create has copied it there)
#
# Distinct from ~/validate-port.sh: THAT runs the functional port validation
# (unit suite + core cold-check). THIS just confirms the environment/setup is
# present and wired, so you know the box is ready before you start testing.

PASS=0; FAIL=0; WARN=0
ok()   { echo "  ✅ $*"; PASS=$((PASS+1)); }
bad()  { echo "  ❌ $*"; FAIL=$((FAIL+1)); }
warn() { echo "  ⚠️  $*"; WARN=$((WARN+1)); }
have() { command -v "$1" >/dev/null 2>&1; }

LUMADECK="${LUMADECK:-$HOME/LumaDeck}"
PORT_BRANCH="${LUMADECK_PORT_BRANCH:-claude/cachyos-support}"

# Guard: are we even in the CachyOS port-testing env, or did the codespace get
# created from the ROOT Arch devcontainer by mistake? The root env is plain Arch
# with none of the port-testing extras, and its tell is unmistakable: ID=arch +
# no cachyos repos + no ~/validate-port.sh. Call it out loudly so a wrong-config
# create doesn't read as "CachyOS build broken".
_ID="$(. /etc/os-release 2>/dev/null; echo "$ID")"
if [ "$_ID" != "cachyos" ] && ! grep -qE '^\[cachyos' /etc/pacman.conf 2>/dev/null && [ ! -e "$HOME/validate-port.sh" ]; then
    echo "=============================================================="
    echo "  ⛔ WRONG ENVIRONMENT — this is the ROOT Arch devcontainer,"
    echo "     NOT the CachyOS port-testing one."
    echo
    echo "  The CachyOS Dockerfile never ran (ID=$_ID, no [cachyos*] repos,"
    echo "  no ~/validate-port.sh). Codespaces defaults to the root config."
    echo
    echo "  FIX: create the codespace selecting the right configuration —"
    echo "    GitHub -> <> Code -> Codespaces -> ... -> \"New with options…\""
    echo "    -> Dev container configuration:"
    echo "         \"Steam validation env (CachyOS)\""
    echo "    (Rebuild does NOT switch configs; create a new one with options.)"
    echo "=============================================================="
    echo "  (Running the full check anyway for completeness:)"
    echo
fi

echo "== A) CachyOS identity & repos (the whole point of this env) =="
ID="$(. /etc/os-release 2>/dev/null; echo "$ID")"
[ "$ID" = "cachyos" ] && ok "/etc/os-release ID=cachyos" || bad "ID='$ID' (expected cachyos)"
if grep -qE '^\[cachyos' /etc/pacman.conf 2>/dev/null; then
    ok "CachyOS repos in pacman.conf ($(grep -oE '^\[cachyos[^]]*' /etc/pacman.conf | tr '\n' ' '))"
else
    bad "no [cachyos*] repo in /etc/pacman.conf (cachyos-repo.sh didn't take)"
fi
# Are installed packages actually coming from the CachyOS repos? (optimized build)
if have pacman; then
    NCACHY="$(pacman -Qm 2>/dev/null | wc -l)"  # foreign; informational only
    CACHYPKGS="$(pacman -Qq 2>/dev/null | grep -c . )"
    ok "pacman works ($CACHYPKGS packages installed)"
else
    bad "pacman missing?!"
fi

echo "== B) Steam + graphics + sandbox (Dockerfile) =="
have steam && ok "steam present ($(command -v steam))" || bad "steam MISSING"
for b in Xvfb x11vnc openbox; do have "$b" && ok "$b present" || bad "$b MISSING"; done
if [ -e /usr/share/vulkan/icd.d/lvp_icd.i686.json ] || ls /usr/share/vulkan/icd.d/ 2>/dev/null | grep -qi 'lvp\|swrast'; then
    ok "software Vulkan ICD present ($(ls /usr/share/vulkan/icd.d/ 2>/dev/null | tr '\n' ' '))"
else
    warn "no lavapipe/swrast Vulkan ICD found under /usr/share/vulkan/icd.d (headless Steam needs SW Vulkan)"
fi
if [ -u /usr/bin/bwrap ]; then ok "bwrap is setuid root"; else bad "bwrap NOT setuid (bwrap: Failed to make / slave)"; fi

echo "== C) Real CachyOS session scripts (best-effort in Dockerfile) =="
# These are the #31-relevant bits. If gamescope-session-cachyos failed to install
# (container-incompatible dep), these WARN — the rest of the env is still usable.
for f in /usr/bin/steamos-session-select /usr/lib/steamos/steam-short-session-tracker; do
    [ -f "$f" ] && ok "$(basename "$f") present ($f)" || warn "$f MISSING (gamescope-session-cachyos not installed?)"
done
if pacman -Q gamescope-session-cachyos >/dev/null 2>&1; then
    ok "gamescope-session-cachyos installed ($(pacman -Q gamescope-session-cachyos | awk '{print $2}'))"
else
    warn "gamescope-session-cachyos NOT installed — #31 session-script tests need it; install by hand: sudo pacman -S gamescope-session-cachyos"
fi

echo "== D) Build toolchain (lumalinux + LumaDeck) =="
for b in gcc cmake ninja pkgconf node npm python3 pip unzip git; do
    have "$b" && ok "$b" || bad "$b MISSING"
done

echo "== E) Headless display stack (post-create) =="
[ -x "$HOME/.local/bin/websockify" ] && ok "websockify (~/.local/bin)" || warn "websockify missing (pip --user step)"
[ -f "$HOME/.local/share/novnc/vnc.html" ] && ok "noVNC (~/.local/share/novnc/vnc.html)" || bad "noVNC not cloned"
grep -q 'LIBGL_ALWAYS_SOFTWARE' "$HOME/.bashrc" 2>/dev/null && ok "~/.bashrc has SW-render env" || warn "~/.bashrc missing SW-render env"

echo "== F) Runtime helper scripts (post-create) =="
for s in start-display.sh start-net.sh start-decky.sh finish-setup.sh build-lumalinux.sh deploy-lumadeck.sh; do
    [ -x "$HOME/$s" ] && ok "~/$s" || bad "~/$s MISSING"
done
[ -x "$HOME/validate-port.sh" ] && ok "~/validate-port.sh (port validator)" || warn "~/validate-port.sh missing (run from repo path)"
[ -f "$HOME/README.dev.md" ] && ok "~/README.dev.md" || warn "~/README.dev.md missing"

echo "== G) Decky + LumaDeck (pre-deployed, on the port branch) =="
[ -x "$HOME/homebrew/services/PluginLoader" ] && ok "Decky PluginLoader downloaded" || bad "PluginLoader MISSING (~/homebrew/services)"
[ -f "$HOME/.local/share/Steam/.cef-enable-remote-debugging" ] && ok "CEF debug marker set (Decky can inject)" || warn "CEF debug marker missing"
if [ -d "$LUMADECK/.git" ]; then
    BR="$(git -C "$LUMADECK" rev-parse --abbrev-ref HEAD 2>/dev/null)"
    [ "$BR" = "$PORT_BRANCH" ] && ok "LumaDeck checkout on $BR" || warn "LumaDeck on '$BR' (expected $PORT_BRANCH)"
else
    bad "LumaDeck repo not cloned at $LUMADECK"
fi
if [ -f "$HOME/homebrew/plugins/LumaDeck/plugin.json" ]; then
    ok "LumaDeck pre-deployed to ~/homebrew/plugins/LumaDeck"
else
    warn "LumaDeck not pre-deployed (run ~/deploy-lumadeck.sh)"
fi

echo "== H) platform_info detects CachyOS from the backend =="
if [ -d "$LUMADECK/backend" ]; then
    DIST="$(cd "$LUMADECK/backend" && python3 -c "import platform_info as p; print(p.summary()['distro'])" 2>/dev/null || echo '?')"
    [ "$DIST" = "cachyos" ] && ok "platform_info.summary() distro=cachyos" || bad "platform_info says distro='$DIST'"
else
    warn "LumaDeck/backend not found — can't check platform_info"
fi

echo
echo "=============================================================="
echo "  SETUP CHECK: $PASS ok, $WARN warn, $FAIL fail"
if [ "$FAIL" -eq 0 ] && [ "$WARN" -eq 0 ]; then
    echo "  RESULT: ALL GREEN ✅ — env fully provisioned."
elif [ "$FAIL" -eq 0 ]; then
    echo "  RESULT: OK with warnings ⚠️  — usable; see WARN lines (usually the"
    echo "          optional CachyOS session package or a redo-able post-create bit)."
else
    echo "  RESULT: FAIL ❌ — something core didn't provision (see ❌ lines)."
fi
echo "  Next: ~/validate-port.sh (functional)  ·  ~/finish-setup.sh (live flow)"
echo "=============================================================="
exit "$FAIL"
