#!/usr/bin/env bash
# validate-port.sh — validate the LumaDeck + lumalinux CachyOS / multi-distro
# port WITHOUT a handheld. No Game Mode needed.
#
#   Part 1 — ENV LAYER: LumaDeck unit suite; platform_info detecting a faked
#            CachyOS (os-release override, restored); real_user resolving a
#            non-1000 Steam-owner (the bug the review caught).
#   Part 2 — CORE COLD-CHECK: download the DESKTOP steamclient.so, confirm its
#            sha256 is whitelisted in lumalinux res/updates.yaml (SafeMode gate),
#            and run check_patterns.py (do the hooks resolve on it?).
#
# What it does NOT cover: the gamescope crash-loop of issue #31 (needs a real
# Game-Mode session; not reproducible headless). That stays a real-device test.
#
# Non-destructive: the os-release override is always restored.
set -u

LUMALINUX="${LUMALINUX:-/workspaces/lumalinux}"
[ -d "$LUMALINUX" ] || LUMALINUX="$HOME/lumalinux"
[ -d "$LUMALINUX" ] || LUMALINUX="$PWD"
LUMADECK="${LUMADECK:-$HOME/LumaDeck}"
PORT_BRANCH="${LUMADECK_PORT_BRANCH:-claude/cachyos-support}"

PASS=0; FAIL=0
ok()  { echo "  ✅ $*"; PASS=$((PASS+1)); }
bad() { echo "  ❌ $*"; FAIL=$((FAIL+1)); }

echo "== 0) sanity =="
[ -f "$LUMALINUX/tools/check_patterns.py" ] || { echo "lumalinux repo not found (set LUMALINUX=)"; exit 1; }
[ -d "$LUMADECK/backend" ] || { echo "LumaDeck repo not found at $LUMADECK (set LUMADECK=)"; exit 1; }
BR="$(git -C "$LUMADECK" rev-parse --abbrev-ref HEAD 2>/dev/null || echo '?')"
[ "$BR" = "$PORT_BRANCH" ] && ok "LumaDeck on $BR" || bad "LumaDeck on '$BR' (expected $PORT_BRANCH)"

echo "== 1) env layer =="
if ( cd "$LUMADECK" && python3 -m unittest discover -s tests ) >/tmp/vp-ut.log 2>&1; then
    ok "LumaDeck unit suite passed ($(grep -oE 'Ran [0-9]+ tests' /tmp/vp-ut.log | tail -1))"
else
    bad "LumaDeck unit suite FAILED (tail /tmp/vp-ut.log)"; tail -8 /tmp/vp-ut.log
fi

# os-release override -> CachyOS detection (restored no matter what)
OSR_BAK="$(mktemp)"; sudo cp /etc/os-release "$OSR_BAK"
trap 'sudo cp "$OSR_BAK" /etc/os-release 2>/dev/null; rm -f "$OSR_BAK"' EXIT
printf 'NAME="CachyOS"\nID=cachyos\nID_LIKE=arch\n' | sudo tee /etc/os-release >/dev/null
DIST="$(cd "$LUMADECK/backend" && python3 -c "import platform_info as p; print(p.summary()['distro'])" 2>/dev/null || echo '?')"
sudo cp "$OSR_BAK" /etc/os-release
[ "$DIST" = "cachyos" ] && ok "platform_info detects distro=cachyos" || bad "detected '$DIST', expected cachyos"

# non-1000 Steam-owner resolution (the review's HIGH-fix scenario)
if python3 - "$LUMADECK/backend" <<'PY'
import sys; sys.path.insert(0, sys.argv[1])
import platform_info as p
# Steam owned by jayo, but uid 1000 is a different account (builder): must pick jayo.
sys.exit(0 if p._resolve_real_user({}, "jayo", "builder", 0) == "jayo" else 1)
PY
then ok "real_user resolves the Steam-owner (jayo), not uid 1000"
else bad "real_user resolution wrong for a non-1000 user"; fi

echo "== 2) core cold-check (DESKTOP client) =="
SO="/tmp/desktop_steamclient.so"
if LUMA_STEAM_MANIFEST=steam_client_ubuntu12 \
     python3 "$LUMALINUX/tools/fetch_steamclient.py" --output "$SO" >/tmp/vp-fetch.log 2>&1; then
    SHA="$(sha256sum "$SO" | awk '{print $1}')"
    echo "  desktop steam_client_ubuntu12 steamclient.so sha256=$SHA"
    if grep -qi "$SHA" "$LUMALINUX/res/updates.yaml"; then
        ok "hash IS whitelisted in res/updates.yaml — SafeMode gate PASSES on desktop client"
    else
        bad "hash NOT in res/updates.yaml — SafeMode would block on this build (whitelist it)"
    fi
    python3 "$LUMALINUX/tools/check_patterns.py" "$SO" --hpp "$LUMALINUX/src/patterns.hpp" >/tmp/vp-pat.log 2>&1
    case $? in
      0) ok "check_patterns: CLEAN — all hooks resolve on the desktop client" ;;
      2) ok "check_patterns: criticals OK (only ShaderDepot moved — non-critical)" ;;
      3) bad "check_patterns: BLOCKING — a critical pattern failed (tail /tmp/vp-pat.log)"; tail -6 /tmp/vp-pat.log ;;
      *) bad "check_patterns: error (tail /tmp/vp-pat.log)"; tail -6 /tmp/vp-pat.log ;;
    esac
else
    bad "could not download the desktop steamclient.so (Steam CDN needs open internet)"
    echo "     tail /tmp/vp-fetch.log:"; tail -4 /tmp/vp-fetch.log
fi

echo
echo "=============================================================="
echo "  PORT VALIDATION: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] && echo "  RESULT: PASS ✅" || echo "  RESULT: FAIL ❌"
echo "  (Not covered here: the gamescope crash-loop / issue #31 — real device.)"
echo "=============================================================="
exit "$FAIL"
