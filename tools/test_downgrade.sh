#!/usr/bin/env bash
# Exercises downgrade.sh's control flow against a FAKE Steam: a `steam` stub on
# PATH that, when asked to update, fetches the manifest and a package from the
# local server the script started (so the server and the names it serves are
# tested too) and installs a new steamclient.so — or does nothing. No network:
# manifest, whitelist and the "CDN" are served from file://. No real Steam.
# Scenarios, all asserted on what is left on disk:
#   ok        — stub installs the target: pin written, package/ kept, backup gone
#   nothing   — stub installs nothing (the 2026-10-14 failure): no pin, package/
#               and steam.cfg restored, exit 1
#   already   — steamclient.so already matches the target: pin, no relaunch
#   nobinary  — no steam launcher: nothing touched, exit 1
#   nocdn     — a package is missing from the CDN: nothing touched, Steam never
#               launched, exit 1
#   timeout   — the bootstrapper outlives the timeout: nothing undone, exit 2
# Run from the repo root:  bash tools/test_downgrade.sh
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT="$HERE/downgrade.sh"
PORT=18666
fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }

setup() {   # setup MODE  -> exports TMP STEAM_DIR and PATH with the stub first
    TMP="$(mktemp -d)"
    STEAM_DIR="$TMP/Steam"; mkdir -p "$STEAM_DIR/package" "$STEAM_DIR/ubuntu12_32" "$TMP/bin" "$TMP/cdn" "$TMP/fake"
    printf 'OLD CLIENT\n' > "$STEAM_DIR/ubuntu12_32/steamclient.so"
    printf '"ubuntu12"\n{\n\t"version"\t\t"1791918634"\n}\n' > "$STEAM_DIR/package/steam_client_ubuntu12.manifest"
    printf 'old-package\n' > "$STEAM_DIR/package/bins_ubuntu12.zip.deadbeef"
    printf 'steamdeck_stable' > "$STEAM_DIR/package/beta"
    printf 'keep=me\n' > "$STEAM_DIR/steam.cfg"
    # Target manifest: two entries, one with a vz form, one plain.
    printf '"ubuntu12"\n{\n\t"version"\t\t"1788291500"\n\t"bins_ubuntu12"\n\t{\n\t\t"file"\t\t"bins_ubuntu12.zip.aaa"\n\t\t"zipvz"\t\t"bins_ubuntu12.zip.vz.bbb_123"\n\t}\n\t"steamui_all"\n\t{\n\t\t"file"\t\t"steamui_all.zip.ccc"\n\t}\n}\n' > "$TMP/manifest"
    for f in bins_ubuntu12.zip.aaa bins_ubuntu12.zip.vz.bbb_123 steamui_all.zip.ccc; do printf 'pkg %s\n' "$f" > "$TMP/cdn/$f"; done
    NEW_SHA="$(printf 'NEW CLIENT\n' | sha256sum | cut -d' ' -f1)"
    printf 'SafeModeHashes:\n  1:\n    # steam_version: 1788291500\n    - %s\n' "$NEW_SHA" > "$TMP/updates.yaml"
    # Like the real launcher: returns at once, while a background process named
    # `steam` (a copy of sleep, so pgrep -x sees it) plays the bootstrapper. In
    # "ok" mode it first proves the local server answers for the branch manifest
    # and a package — the names the real bootstrapper asks for — then installs.
    cp "$(command -v sleep)" "$TMP/fake/steam"
    cat > "$TMP/bin/steam" <<STUB
#!/usr/bin/env bash
echo "stub steam \$*" >> "$TMP/calls"
url=""; prev=""
for a in "\$@"; do [[ "\$prev" == "-overridepackageurl" ]] && url="\$a"; prev="\$a"; done
case "\$*" in *-forcesteamupdate*)
    (
      sleep 1
      mkdir -p "$STEAM_DIR/logs"; echo "[t] Startup - Steam Client launched with: 'steam' \$*" >> "$STEAM_DIR/logs/bootstrap_log.txt"
      "$TMP/fake/steam" 2
      if [[ "$1" == ok ]]; then
          curl -fsS -o /dev/null "\${url}steam_client_steamdeck_stable_ubuntu12" && \\
          curl -fsS -o /dev/null "\${url}bins_ubuntu12.zip.vz.bbb_123" && \\
          printf 'NEW CLIENT\\n' > "$STEAM_DIR/ubuntu12_32/steamclient.so" || echo "server check failed" >> "$TMP/calls"
      fi
      echo "[t] Shutdown" >> "$STEAM_DIR/logs/bootstrap_log.txt"
    ) >/dev/null 2>&1 &
    disown ;;
esac
exit 0
STUB
    chmod +x "$TMP/bin/steam"
    export STEAM_DIR_OVERRIDE="$STEAM_DIR" \
           LINUX_MANIFEST_URL="file://$TMP/manifest" DECK_MANIFEST_URL="file://$TMP/manifest" \
           UPDATES_YAML_URL="file://$TMP/updates.yaml" \
           PACKAGE_CDN="file://$TMP/cdn" PACKAGE_CDN_FALLBACK="" LOCAL_PORT="$PORT" \
           STEAM_STOP_TIMEOUT=3 STEAM_START_TIMEOUT=5 STEAM_UPDATE_TIMEOUT=15 HOME="$TMP/home"
    mkdir -p "$HOME"
    PATH="$TMP/bin:$PATH"
}

echo "== scenario: ok =="
setup ok
bash "$SCRIPT" > "$TMP/out" 2>&1; rc=$?
[[ $rc -eq 0 ]] || { cat "$TMP/out"; fail "ok: exit $rc"; }
grep -q "forcesteamupdate.*overridepackageurl http://127.0.0.1:$PORT/.*-exitsteam" "$TMP/calls" || fail "ok: stub never relaunched with the flags"
! grep -q 'server check failed' "$TMP/calls" || fail "ok: the local server did not answer for the manifest/package names"
grep -q '^# lumalinux$' "$STEAM_DIR/steam.cfg" && grep -q 'BootStrapperInhibitAll=enable' "$STEAM_DIR/steam.cfg" || fail "ok: pin not written"
grep -q '^keep=me$' "$STEAM_DIR/steam.cfg" || fail "ok: existing steam.cfg content lost"
grep -q 1788291500 "$STEAM_DIR/package/steam_client_ubuntu12.manifest" || fail "ok: wrong manifest staged"
[[ "$(cat "$STEAM_DIR/package/beta")" == "steamdeck_stable" ]] || fail "ok: branch file not kept"
[[ ! -d "$STEAM_DIR/package.lumalinux.bak" ]] || fail "ok: backup not removed"
! pgrep -f "http.server $PORT" >/dev/null || fail "ok: local server left running"
echo "   pass"

echo "== scenario: nothing =="
setup nothing
bash "$SCRIPT" > "$TMP/out" 2>&1; rc=$?
[[ $rc -eq 1 ]] || { cat "$TMP/out"; fail "nothing: exit $rc (expected 1)"; }
grep -q 'Downgrade FAILED' "$TMP/out" || fail "nothing: no failure message"
grep -q 'forcesteamupdate' "$TMP/calls" || fail "nothing: stub never relaunched"
[[ "$(cat "$STEAM_DIR/steam.cfg")" == "keep=me" ]] || fail "nothing: steam.cfg not restored ($(cat "$STEAM_DIR/steam.cfg"))"
[[ -f "$STEAM_DIR/package/bins_ubuntu12.zip.deadbeef" ]] || fail "nothing: package/ not restored"
grep -q 1791918634 "$STEAM_DIR/package/steam_client_ubuntu12.manifest" || fail "nothing: old manifest not restored"
[[ ! -d "$STEAM_DIR/package.lumalinux.bak" ]] || fail "nothing: backup left behind"
! pgrep -f "http.server $PORT" >/dev/null || fail "nothing: local server left running"
echo "   pass"

echo "== scenario: already =="
setup ok
printf 'NEW CLIENT\n' > "$STEAM_DIR/ubuntu12_32/steamclient.so"
bash "$SCRIPT" > "$TMP/out" 2>&1; rc=$?
[[ $rc -eq 0 ]] || { cat "$TMP/out"; fail "already: exit $rc"; }
[[ ! -f "$TMP/calls" ]] || fail "already: Steam was launched for nothing"
grep -q 'BootStrapperInhibitAll=enable' "$STEAM_DIR/steam.cfg" || fail "already: pin not written"
[[ -f "$STEAM_DIR/package/bins_ubuntu12.zip.deadbeef" ]] || fail "already: package/ touched"
echo "   pass"

echo "== scenario: no steam binary =="
setup ok
rm "$TMP/bin/steam"                     # no stub, and no /usr/bin/steam in this environment
bash "$SCRIPT" > "$TMP/out" 2>&1; rc=$?
[[ $rc -eq 1 ]] || fail "nobinary: exit $rc"
[[ "$(cat "$STEAM_DIR/steam.cfg")" == "keep=me" ]] || fail "nobinary: steam.cfg touched"
[[ -f "$STEAM_DIR/package/bins_ubuntu12.zip.deadbeef" ]] || fail "nobinary: package/ touched"
echo "   pass"

echo "== scenario: package missing from the CDN =="
setup ok
rm "$TMP/cdn/steamui_all.zip.ccc"
bash "$SCRIPT" > "$TMP/out" 2>&1; rc=$?
[[ $rc -eq 1 ]] || { cat "$TMP/out"; fail "nocdn: exit $rc"; }
grep -q 'could not download steamui_all.zip.ccc' "$TMP/out" || fail "nocdn: wrong failure message"
[[ ! -f "$TMP/calls" ]] || fail "nocdn: Steam was launched although a package was missing"
[[ "$(cat "$STEAM_DIR/steam.cfg")" == "keep=me" ]] || fail "nocdn: steam.cfg touched"
[[ -f "$STEAM_DIR/package/bins_ubuntu12.zip.deadbeef" ]] || fail "nocdn: package/ touched"
echo "   pass"

echo "== scenario: still running at timeout =="
setup ok
cat > "$TMP/bin/steam" <<STUB
#!/usr/bin/env bash
case "\$*" in *-forcesteamupdate*) ( "$TMP/fake/steam" 30 ) >/dev/null 2>&1 & disown ;; esac
exit 0
STUB
STEAM_UPDATE_TIMEOUT=3 bash "$SCRIPT" > "$TMP/out" 2>&1; rc=$?
pkill -f "$TMP/fake/steam" 2>/dev/null
[[ $rc -eq 2 ]] || { cat "$TMP/out"; fail "timeout: exit $rc (expected 2)"; }
grep -q 'BootStrapperInhibitAll=enable' "$STEAM_DIR/steam.cfg" || fail "timeout: pin was undone under a live bootstrapper"
grep -q 1788291500 "$STEAM_DIR/package/steam_client_ubuntu12.manifest" || fail "timeout: staged manifest was undone"
[[ -d "$STEAM_DIR/package.lumalinux.bak" ]] || fail "timeout: backup removed too early"
echo "   pass"
echo "all downgrade.sh scenarios pass"
