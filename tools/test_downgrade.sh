#!/usr/bin/env bash
# Exercises downgrade.sh's control flow against a FAKE Steam: a `steam` stub on
# PATH that either installs a new steamclient.so when asked to update, or does
# nothing. No network (manifest + whitelist served from file://), no real Steam.
# Three scenarios, all asserted on what is left on disk:
#   ok       — stub installs the target: pin written, package/ kept, backup gone
#   nothing  — stub installs nothing (the 2026-10-14 failure): no pin, package/ and
#              steam.cfg restored, exit 1
#   already  — steamclient.so already matches the target: pin written, no relaunch
# Run from the repo root:  bash tools/test_downgrade.sh
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT="$HERE/downgrade.sh"
fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }

setup() {   # setup MODE  -> exports TMP STEAM_DIR and PATH with the stub first
    TMP="$(mktemp -d)"
    STEAM_DIR="$TMP/Steam"; mkdir -p "$STEAM_DIR/package" "$STEAM_DIR/ubuntu12_32" "$TMP/bin"
    printf 'OLD CLIENT\n' > "$STEAM_DIR/ubuntu12_32/steamclient.so"
    printf '"ubuntu12"\n{\n\t"version"\t\t"1791918634"\n}\n' > "$STEAM_DIR/package/steam_client_ubuntu12.manifest"
    printf 'old-package\n' > "$STEAM_DIR/package/bins_ubuntu12.zip.deadbeef"
    printf 'keep=me\n' > "$STEAM_DIR/steam.cfg"
    printf '"ubuntu12"\n{\n\t"version"\t\t"1788291500"\n}\n' > "$TMP/manifest"
    NEW_SHA="$(printf 'NEW CLIENT\n' | sha256sum | cut -d' ' -f1)"
    printf 'SafeModeHashes:\n  1:\n    # steam_version: 1788291500\n    - %s\n' "$NEW_SHA" > "$TMP/updates.yaml"
    cat > "$TMP/bin/steam" <<STUB
#!/usr/bin/env bash
echo "stub steam \$*" >> "$TMP/calls"
case "\$*" in *-forcesteamupdate*) [[ "$1" == ok ]] && printf 'NEW CLIENT\n' > "$STEAM_DIR/ubuntu12_32/steamclient.so" ;; esac
exit 0
STUB
    chmod +x "$TMP/bin/steam"
    export STEAM_DIR_OVERRIDE="$STEAM_DIR" \
           LINUX_MANIFEST_URL="file://$TMP/manifest" DECK_MANIFEST_URL="file://$TMP/manifest" \
           UPDATES_YAML_URL="file://$TMP/updates.yaml" DOWNGRADE_URL="http://mirror.invalid" \
           STEAM_STOP_TIMEOUT=3 STEAM_UPDATE_TIMEOUT=3 HOME="$TMP/home"
    mkdir -p "$HOME"
    PATH="$TMP/bin:$PATH"
}

echo "== scenario: ok =="
setup ok
bash "$SCRIPT" > "$TMP/out" 2>&1; rc=$?
[[ $rc -eq 0 ]] || { cat "$TMP/out"; fail "ok: exit $rc"; }
grep -q 'forcesteamupdate.*overridepackageurl http://mirror.invalid.*-exitsteam' "$TMP/calls" || fail "ok: stub never relaunched with the flags"
grep -q '^# lumalinux$' "$STEAM_DIR/steam.cfg" && grep -q 'BootStrapperInhibitAll=enable' "$STEAM_DIR/steam.cfg" || fail "ok: pin not written"
grep -q '^keep=me$' "$STEAM_DIR/steam.cfg" || fail "ok: existing steam.cfg content lost"
[[ -f "$STEAM_DIR/package/steam_client_ubuntu12.manifest" ]] || fail "ok: staged manifest missing"
grep -q 1788291500 "$STEAM_DIR/package/steam_client_ubuntu12.manifest" || fail "ok: wrong manifest staged"
[[ ! -d "$STEAM_DIR/package.lumalinux.bak" ]] || fail "ok: backup not removed"
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
echo "all downgrade.sh scenarios pass"
