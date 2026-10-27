#!/usr/bin/env bash
# Steam smoke: inside the smoke container, as `deck`. Installs the stack with
# the repo's setup.sh, starts Steam headless on the requested channel, waits
# until the client is up (login screen) or gives up, and writes a report plus
# the logs to $OUT. No Steam account: everything that needs one (package 0,
# library, adding a game) is out of scope; what is in scope is "the stack
# loads in this Steam build and nothing dies": the library of the running
# client mapped, hooks installed, the feed read, SLSsteam and its plugin up.
#
# Env (all optional): CHANNEL=stable|beta, SETUP=path to setup.sh,
# LUMALINUX_SO_URL / LUMALINUX64_SO_URL / SLSSTEAM64_URL (setup.sh's own
# overrides), WAIT_MIN (default 15), OUT (default /work/smoke-out),
# FEED=auto|derive (auto: use main's feed for this client build, derive one
# with the repo's tool only when main has none; derive: always derive, and
# compare with main's when it exists — a live check of the tool).
set -u
CHANNEL="${CHANNEL:-beta}"
SETUP="${SETUP:-/work/setup.sh}"
WAIT_MIN="${WAIT_MIN:-15}"
FEED="${FEED:-auto}"
OUT="${OUT:-/work/smoke-out}"
mkdir -p "$OUT"
REPORT="$OUT/report.md"
: > "$REPORT"
say() { printf '%s\n' "$*" | tee -a "$REPORT"; }
now() { date +%s; }
steam_alive() { pgrep -f 'steam\.sh|ubuntu12_32/steam|steamrt64/steam|/usr/bin/steam|steam-runtime' >/dev/null; }
step() { printf '\n== %s\n' "$*" | tee -a "$REPORT"; }

collect_logs() {
    [ -n "${COLLECTED:-}" ] && return; COLLECTED=1
    mkdir -p "$OUT/logs"
    cp "$HOME/.cache/lumalinux/lumalinux.log" "$OUT/logs/lumalinux.log" 2>/dev/null || true
    cp "$HOME/.SLSsteam.log" "$OUT/logs/SLSsteam.log" 2>/dev/null || true
    for f in "$XDG_RUNTIME_DIR/lumalinux/status.json" "$HOME/.cache/lumalinux/status.json"; do [ -s "$f" ] && cp "$f" "$OUT/logs/status.json" 2>/dev/null; done
    for f in bootstrap_log.txt console-linux.txt content_log.txt appinfo_log.txt; do cp "$HOME/.local/share/Steam/logs/$f" "$OUT/logs/" 2>/dev/null || true; done
    cp "$HOME/.config/SLSsteam/config.yaml" "$OUT/logs/slssteam-config.yaml" 2>/dev/null || true
    ps -eo pid,etimes,comm,args --sort=pid 2>/dev/null | cut -c1-160 > "$OUT/logs/ps.txt" || true
    timeout 20 steam -shutdown >/dev/null 2>&1 || true
    sleep 3; pkill -9 -f 'steamwebhelper|ubuntu12_32/steam|steamrt64/steam|steam\.sh|/usr/bin/steam' 2>/dev/null || true
}
trap 'collect_logs' EXIT

export HOME=/home/deck
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DISPLAY=:1
export LIBGL_ALWAYS_SOFTWARE=1
export PATH="$HOME/.local/bin:$PATH"

step "host"
say "- glibc: $(ldd --version | head -n 1)"
say "- os-release: $(grep -E '^(ID|VERSION_ID)=' /etc/os-release | tr '\n' ' ')"
say "- kernel: $(uname -r)"
say "- channel requested: $CHANNEL"

step "session bus, display"
sudo mkdir -p "$XDG_RUNTIME_DIR" /tmp/.X11-unix /run/dbus
sudo chown "$(id -u):$(id -g)" "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"; sudo chmod 1777 /tmp/.X11-unix
sudo dbus-daemon --system --fork 2>/dev/null || true
sudo NetworkManager >/dev/null 2>&1 &
dbus-daemon --session --address="unix:path=$XDG_RUNTIME_DIR/bus" --fork
export DBUS_SESSION_BUS_ADDRESS="unix:path=$XDG_RUNTIME_DIR/bus"
Xvfb :1 -screen 0 1280x800x24 -nolisten tcp >"$OUT/xvfb.log" 2>&1 &
for i in $(seq 1 50); do xdpyinfo >/dev/null 2>&1 && break; sleep 0.2; done
if xdpyinfo >/dev/null 2>&1; then say "- display: up"; else say "- display: DOWN — Xvfb log:"; sed 's/^/    /' "$OUT/xvfb.log" | tee -a "$REPORT"; say "- verdict: FAIL (no display)"; exit 1; fi
say "- session bus: $(dbus-send --session --dest=org.freedesktop.DBus --type=method_call --print-reply /org/freedesktop/DBus org.freedesktop.DBus.ListNames >/dev/null 2>&1 && echo up || echo DOWN)"

step "first Steam start (vanilla: bootstrap the client, no stack yet)"
# steam.sh needs the package dir to exist to honour the channel file; the
# bootstrap creates ~/.local/share/Steam on first run. We run vanilla once so
# setup.sh finds a Steam tree, then stop it.
mkdir -p "$HOME/.local/share/Steam/package"
if [ "$CHANNEL" = beta ]; then echo publicbeta > "$HOME/.local/share/Steam/package/beta"; else rm -f "$HOME/.local/share/Steam/package/beta"; fi
( steam -no-cef-sandbox >"$OUT/steam-vanilla.log" 2>&1 & )
DEADLINE=$(( $(now) + WAIT_MIN * 60 ))
while [ "$(now)" -lt "$DEADLINE" ]; do
    if [ -s "$HOME/.local/share/Steam/ubuntu12_32/steamclient.so" ] && pgrep -f 'ubuntu12_32/steam|steamrt64/steam' >/dev/null; then sleep 20; break; fi
    steam_alive || { sleep 5; steam_alive || { say "- vanilla Steam exited early; tail of its output:"; tail -n 15 "$OUT/steam-vanilla.log" | sed 's/^/    /' | tee -a "$REPORT"; break; }; }
    sleep 3
done
say "- client dir: $(ls -d "$HOME"/.local/share/Steam/ubuntu12_32 "$HOME"/.local/share/Steam/steamrt64 2>/dev/null | tr '\n' ' ')"
say "- vanilla processes: $(pgrep -af '(ubuntu12_32|steamrt64)/steam' | grep -oE '(ubuntu12_32|steamrt64)/steam' | sort | uniq -c | tr '\n' ' ')"
timeout 20 steam -shutdown >/dev/null 2>&1 || true
for i in $(seq 1 60); do steam_alive || break; sleep 1; done
pkill -9 -f 'steamwebhelper|ubuntu12_32/steam|steamrt64/steam|steam\.sh|/usr/bin/steam' 2>/dev/null || true
rm -f "$HOME/.steam/steam.pid" "$HOME/.steam/steam.pipe" 2>/dev/null || true
say "- vanilla output tail:"; tail -n 8 "$OUT/steam-vanilla.log" 2>/dev/null | sed 's/^/    /' | tee -a "$REPORT"
say "- bootstrap log tail:"; tail -n 5 "$HOME/.local/share/Steam/logs/bootstrap_log.txt" 2>/dev/null | sed 's/^/    /' | tee -a "$REPORT"

step "install the stack (setup.sh)"
say "- setup: $SETUP"
say "- LUMALINUX_SO_URL=${LUMALINUX_SO_URL:-<default>} LUMALINUX64_SO_URL=${LUMALINUX64_SO_URL:-<default>} SLSSTEAM64_URL=${SLSSTEAM64_URL:-<none>}"
bash "$SETUP" >"$OUT/setup.log" 2>&1; rc=$?
say "- setup.sh exit: $rc"
[ "$rc" = 0 ] || { say "- setup.sh output tail:"; tail -n 25 "$OUT/setup.log" | sed 's/^/    /' | tee -a "$REPORT"; }
grep -E '^\[\+\]|^\[!\]|^\[X\]|Deployed|SLSsteam|plugin' "$OUT/setup.log" | sed 's/^/    /' | tail -n 25 | tee -a "$REPORT"
say "- installed: $(ls "$HOME/.local/share/lumalinux/" 2>/dev/null | tr '\n' ' ')"
say "- SLSsteam: $(ls "$HOME/.local/share/SLSsteam/SLSsteam.so" "$HOME/.local/share/SLSsteam/x86_64/SLSsteam.so" 2>/dev/null | tr '\n' ' ')"
say "- plugins: $(ls "$HOME/.config/SLSsteam/plugins/" 2>/dev/null | tr '\n' ' ')"
grep -n "^Plugins:\|^MaxSchemaTries:\|^SafeMode:" "$HOME/.config/SLSsteam/config.yaml" 2>/dev/null | sed 's/^/    config: /' | tee -a "$REPORT"

step "feed for this client build ($FEED)"
# The runtime fetches res/rvas/<sha>.yaml from main and reads its disk copy
# first; a copy placed in the cache is what it uses. A build newer than the
# last daily watch-steam run has no feed on main yet — the smoke must not
# fail on that: derive one here with the same tool the daily job runs.
if [ "$CHANNEL" = beta ]; then
    SC="$HOME/.local/share/Steam/steamrt64/steamclient.so"; TOOL="python3 /work/tools/check_patterns_x64.py"
else
    SC="$HOME/.local/share/Steam/ubuntu12_32/steamclient.so"; TOOL="python3 /work/tools/check_patterns.py --hpp /work/src/patterns.hpp"
fi
FEED_SRC="none"
if [ -s "$SC" ]; then
    SHA="$(sha256sum "$SC" | cut -d' ' -f1)"
    say "- steamclient.so: $SC"; say "- sha256: $SHA"
    mkdir -p "$HOME/.cache/lumalinux/rvas" "$OUT/feed"
    MAINYAML="$OUT/feed/main-$SHA.yaml"
    if curl -fsSL -o "$MAINYAML" "https://raw.githubusercontent.com/jayool/lumalinux/main/res/rvas/$SHA.yaml" 2>/dev/null && grep -q '^hooks:' "$MAINYAML"; then
        say "- main has a feed for this build"
    else
        rm -f "$MAINYAML"; say "- main has NO feed for this build yet (newer than the last watch-steam run)"
    fi
    if [ "$FEED" = derive ] || [ ! -s "$MAINYAML" ]; then
        if $TOOL "$SC" --emit-rvas "$OUT/feed/derived" >"$OUT/feed/derive.log" 2>&1 && [ -s "$OUT/feed/derived/$SHA.yaml" ]; then
            grep -E "licensechk|finder|VERDICT|CLEAN|BLOCKING|wrote" "$OUT/feed/derive.log" | tail -n 6 | sed 's/^/    /' | tee -a "$REPORT"
            if [ -s "$MAINYAML" ]; then
                if diff -q <(grep -v '^#' "$MAINYAML" | grep -v '^steam_version') <(grep -v '^#' "$OUT/feed/derived/$SHA.yaml" | grep -v '^steam_version') >/dev/null; then
                    say "- derived feed MATCHES main's"; FEED_SRC="main (derived matches)"
                else
                    say "- derived feed DIFFERS from main's:"; diff <(grep -v '^#' "$MAINYAML") <(grep -v '^#' "$OUT/feed/derived/$SHA.yaml") | sed 's/^/    /' | tee -a "$REPORT"; FEED_SRC="main (derived differs!)"
                fi
                cp "$MAINYAML" "$HOME/.cache/lumalinux/rvas/$SHA.yaml"
            else
                cp "$OUT/feed/derived/$SHA.yaml" "$HOME/.cache/lumalinux/rvas/$SHA.yaml"; FEED_SRC="derived here (not on main yet)"
            fi
        else
            say "- derivation FAILED; tool output tail:"; tail -n 15 "$OUT/feed/derive.log" | sed 's/^/    /' | tee -a "$REPORT"
            [ -s "$MAINYAML" ] && { cp "$MAINYAML" "$HOME/.cache/lumalinux/rvas/$SHA.yaml"; FEED_SRC="main (derivation failed)"; }
        fi
    else
        cp "$MAINYAML" "$HOME/.cache/lumalinux/rvas/$SHA.yaml"; FEED_SRC="main"
    fi
else
    say "- no steamclient.so for the $CHANNEL client at $SC"
fi
say "- feed in use: $FEED_SRC"

step "Steam with the stack ($CHANNEL)"
WRAPPER="$HOME/.local/share/SLSsteam/path/steam"
if [ -x "$WRAPPER" ]; then LAUNCH="$WRAPPER"; else LAUNCH="steam"; say "- WARNING: wrapper missing, launching vanilla steam"; fi
: > "$HOME/.cache/lumalinux/lumalinux.log" 2>/dev/null || true
T0=$(date +%s)
( "$LAUNCH" -no-cef-sandbox -cef-enable-debugging >"$OUT/steam-stack.log" 2>&1 & )
LLLOG="$HOME/.cache/lumalinux/lumalinux.log"
DONE=""
DEADLINE=$(( $(now) + WAIT_MIN * 60 ))
while [ "$(now)" -lt "$DEADLINE" ]; do
    DONE=""
    if grep -qE 'Status: wrote|FAILED|installs nothing' "$LLLOG" 2>/dev/null; then DONE=lumalinux; fi
    if curl -fsS --max-time 2 http://127.0.0.1:8080/json/version >/dev/null 2>&1; then DONE="${DONE:+$DONE,}cef"; fi
    [[ "$DONE" == *cef* ]] && [[ "$DONE" == *lumalinux* ]] && break
    steam_alive || { sleep 5; steam_alive || { DONE="steam-exited"; break; }; }
    sleep 3
done
sleep 10   # let the hooks' first pass and the plugin run settle
T1=$(date +%s)
say "- waited: $((T1 - T0)) s, outcome: ${DONE:-timeout}"
say "- processes: $(pgrep -af '(ubuntu12_32|steamrt64)/steam' | grep -oE '(ubuntu12_32|steamrt64)/steam' | sort | uniq -c | tr '\n' ' ')"
say "- mapped per steam pid:"
for p in $(pgrep -x steam); do
    say "    pid $p: $(grep -oE 'liblumalinux[^ ]*\.so|SLSsteam\.so|cloud_redirect[^ ]*\.so' /proc/$p/maps 2>/dev/null | sort -u | tr '\n' ' ') $(grep -qz '^PRESSURE_VESSEL_RUNTIME=' /proc/$p/environ 2>/dev/null && echo '[rt3 container]')"
done
say "- loader notices:"; grep -h 'ERROR: ld.so' "$OUT/steam-stack.log" 2>/dev/null | sed 's/^.*ERROR: ld.so/ld.so/' | sort -u | sed 's/^/    /' | tee -a "$REPORT"
say "- CEF: $(curl -fsS --max-time 3 http://127.0.0.1:8080/json/version 2>/dev/null | jq -r '.Browser' 2>/dev/null || echo 'not responding')"
if ! pgrep -f 'ubuntu12_32/steam|steamrt64/steam' >/dev/null; then
    say "- Steam is NOT running any more; tail of its output:"; tail -n 25 "$OUT/steam-stack.log" 2>/dev/null | cut -c1-160 | sed 's/^/    /' | tee -a "$REPORT"
    say "- SLSsteam log tail:"; tail -n 15 "$HOME/.SLSsteam.log" 2>/dev/null | cut -c1-160 | sed 's/^/    /' | tee -a "$REPORT"
    say "- processes:"; ps -eo pid,etimes,comm,args --sort=pid 2>/dev/null | grep -iE 'steam|bwrap|pressure|reaper|dbus|Xvfb' | grep -v grep | cut -c1-150 | sed 's/^/    /' | tee -a "$REPORT"
fi

step "lumalinux"
grep -E 'hash is|RvaFeed|Hook install|licenses_check_off|PKG0_FINDER|Status: wrote|FAILED|installs nothing|Error|crash|Reconcile' "$LLLOG" 2>/dev/null | cut -c1-160 | sed 's/^/    /' | tee -a "$REPORT"
ST=""; for f in "$XDG_RUNTIME_DIR/lumalinux/status.json" "$HOME/.cache/lumalinux/status.json"; do [ -s "$f" ] && ST="$f"; done
if [ -n "$ST" ]; then say "- status.json ($ST): $(jq -c '{version, blocked, hooks: (.hooks | to_entries | map("\(.key)=\(.value)") | join(" "))}' "$ST" 2>/dev/null || head -c 300 "$ST")"; else say "- status.json: none"; fi

step "SLSsteam"
grep -nE 'SLSsteam|Ran |plugin|lumalinux|Error|error|crash|Failed|Config' "$HOME/.SLSsteam.log" 2>/dev/null | cut -c1-160 | tail -n 25 | sed 's/^/    /' | tee -a "$REPORT"

step "verdict"
V=0
pgrep -f "$([ "$CHANNEL" = beta ] && echo steamrt64/steam || echo ubuntu12_32/steam)" >/dev/null && say "- OK   client for channel '$CHANNEL' is running" || { say "- FAIL expected client for '$CHANNEL' not running"; V=1; }
grep -q 'Status: wrote' "$LLLOG" 2>/dev/null && say "- OK   lumalinux wrote status.json" || { say "- FAIL lumalinux did not reach Status: wrote"; V=1; }
case "$FEED_SRC" in
    main) say "- OK   feed from main" ;;
    "main (derived matches)") say "- OK   feed from main, and the tool derives the same one" ;;
    "derived here (not on main yet)") say "- OK   feed derived here (main has none yet; the daily watch-steam will publish it)" ;;
    "main (derived differs!)") say "- FAIL the tool derives a feed that differs from main's (see above)"; V=1 ;;
    *) say "- FAIL no usable feed ($FEED_SRC)"; V=1 ;;
esac
if grep -q 'no feed for build' "$LLLOG" 2>/dev/null; then
    say "- FAIL the runtime found no feed for this build"
    V=1
elif grep -q 'FAILED' "$LLLOG" 2>/dev/null; then say "- FAIL a hook FAILED"; V=1
else say "- OK   no hook FAILED"; fi
if [ "$CHANNEL" = beta ]; then grep -q 'licenses_check_off' "$LLLOG" 2>/dev/null && say "- OK   feed carries licenses_check_off" || say "- WARN feed without licenses_check_off (fine on a build without the flags)"; fi
grep -qE 'Ran .*lumalinux-cuser|lumalinux-cuser' "$HOME/.SLSsteam.log" 2>/dev/null && say "- OK   SLSsteam ran the lumalinux-cuser plugin" || say "- WARN no trace of the lumalinux-cuser plugin in SLSsteam's log"
curl -fsS --max-time 3 http://127.0.0.1:8080/json/version >/dev/null 2>&1 && say "- OK   CEF responds (login screen reached)" || say "- WARN CEF not responding"
say "- verdict: $([ $V = 0 ] && echo PASS || echo FAIL)"

step "collect"
collect_logs
exit $V

