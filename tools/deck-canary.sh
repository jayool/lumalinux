#!/usr/bin/env bash
# deck-canary.sh — SAFE, reversible probe to answer ONE question on a Steam Deck:
# does the Game Mode (gamescope) Steam launch go through a PATH-based wrapper, and
# under which launcher name (steam / steam-jupiter / ...)?
#
# It does NOT inject anything and does NOT touch headcrab's steam.sh, the .so's, or
# any config. It drops tiny "shim" launchers on PATH that ONLY log that they were
# invoked and then exec the REAL Steam binary of that name — so Steam keeps working
# exactly as now (headcrab still injects). Then you boot Game Mode and check the log.
#
# Usage (on the Deck, Desktop mode):
#   curl -fsSL https://raw.githubusercontent.com/jayool/lumalinux/claude/steam-update-gating/tools/deck-canary.sh -o ~/deck-canary.sh
#   bash ~/deck-canary.sh            # install the canary
#   ...reboot to Game Mode, let Steam start, go back to Desktop...
#   cat ~/deck-canary.log            # did a shim fire in Game Mode? which name?
#   bash ~/deck-canary.sh --revert   # undo everything, Deck back to exactly as before

set -euo pipefail

PATH_DIR="${HOME}/.local/share/SLSsteam/path"
LOG="${HOME}/deck-canary.log"
# Launcher names Game Mode / Desktop might use. The shim is installed as each.
NAMES=(steam steam-jupiter steam-jupiter-stable bazzite-steam)
RC_FILES=("${HOME}/.bashrc" "${HOME}/.bash_profile" "${HOME}/.profile" "${HOME}/.zshrc")
PATH_MARK_BEGIN="# >>> deck-canary PATH >>>"
PATH_MARK_END="# <<< deck-canary PATH <<<"
PATH_LINE='export PATH="$HOME/.local/share/SLSsteam/path:$PATH"'

c_i='\e[0;36m'; c_o='\e[0;32m'; c_w='\e[0;33m'; c_r='\e[0m'
info(){ printf "${c_i}[*]${c_r} %s\n" "$*"; }
ok(){   printf "${c_o}[+]${c_r} %s\n" "$*"; }
warn(){ printf "${c_w}[!]${c_r} %s\n" "$*" >&2; }

# ── revert ──────────────────────────────────────────────────────────────────
if [[ "${1:-}" == "--revert" ]]; then
    info "Reverting deck-canary..."
    for n in "${NAMES[@]}"; do
        if [[ -e "${PATH_DIR}/${n}" ]] && head -3 "${PATH_DIR}/${n}" 2>/dev/null | grep -q "deck-canary shim"; then
            rm -f "${PATH_DIR}/${n}"; ok "Removed shim ${PATH_DIR}/${n}"
        fi
        # restore anything we moved aside
        if [[ -e "${PATH_DIR}/${n}.canary-orig" ]]; then
            mv -f "${PATH_DIR}/${n}.canary-orig" "${PATH_DIR}/${n}"; ok "Restored original ${PATH_DIR}/${n}"
        fi
    done
    rmdir "$PATH_DIR" 2>/dev/null || true
    for rc in "${RC_FILES[@]}"; do
        [[ -f "$rc" ]] || continue
        if grep -qF "$PATH_MARK_BEGIN" "$rc" 2>/dev/null; then
            sed -i "/^# >>> deck-canary PATH >>>/,/^# <<< deck-canary PATH <<</d" "$rc"
            ok "Removed PATH drop-in from $(basename "$rc")"
        fi
    done
    info "Done. Your Deck is back to exactly as before (headcrab untouched)."
    info "Log kept at $LOG (delete it manually if you want)."
    exit 0
fi

# ── install ─────────────────────────────────────────────────────────────────
info "Installing deck-canary (safe, reversible — headcrab/injection untouched)..."
mkdir -p "$PATH_DIR"

# The shim: log the invocation, then exec the REAL binary of this name (skipping
# our own dir so it never calls itself). No injection — headcrab's steam.sh does
# that downstream, so Steam behaves exactly as it does today.
write_shim() {
    local dest="$1"
    cat > "$dest" <<'SHIM'
#!/bin/sh
# deck-canary shim (managed by deck-canary.sh — safe to delete).
_name="$(basename "$0")"
_selfdir="$HOME/.local/share/SLSsteam/path"
{
    printf '%s CANARY-HIT name=%s argv0=%s session=%s desktop=%s gamescope=%s args=[%s]\n' \
        "$(date '+%F %T' 2>/dev/null)" "$_name" "$0" \
        "${XDG_SESSION_TYPE:-?}" "${XDG_CURRENT_DESKTOP:-?}" \
        "${GAMESCOPE_WAYLAND_DISPLAY:-no}" "$*"
} >> "$HOME/deck-canary.log" 2>/dev/null || true

# Resolve the REAL binary of this name, skipping our shim dir.
_real=""
_oifs="$IFS"; IFS=:
for _d in $PATH; do
    [ "$_d" = "$_selfdir" ] && continue
    if [ -x "$_d/$_name" ]; then _real="$_d/$_name"; break; fi
done
IFS="$_oifs"
if [ -z "$_real" ]; then
    for _c in "/usr/bin/$_name" "/usr/games/$_name" "/usr/lib/steam/$_name"; do
        [ -x "$_c" ] && { _real="$_c"; break; }
    done
fi
if [ -z "$_real" ]; then
    printf '%s CANARY-ERROR: real %s not found — falling back to /usr/bin/steam\n' \
        "$(date '+%F %T' 2>/dev/null)" "$_name" >> "$HOME/deck-canary.log" 2>/dev/null || true
    _real="/usr/bin/steam"   # last-resort so Game Mode still starts
fi
exec "$_real" "$@"
SHIM
    chmod 0755 "$dest"
}

for n in "${NAMES[@]}"; do
    # back up any pre-existing file at this path (unlikely — headcrab renames it away)
    if [[ -e "${PATH_DIR}/${n}" ]] && ! head -3 "${PATH_DIR}/${n}" 2>/dev/null | grep -q "deck-canary shim"; then
        mv -f "${PATH_DIR}/${n}" "${PATH_DIR}/${n}.canary-orig"
        warn "Moved existing ${PATH_DIR}/${n} -> .canary-orig (restored on --revert)"
    fi
    write_shim "${PATH_DIR}/${n}"
    ok "Installed shim: ${PATH_DIR}/${n}"
done

# PATH drop-in so a PATH-resolved `steam` (or steam-jupiter) hits our shim.
for rc in "${RC_FILES[@]}"; do
    [[ -f "$rc" ]] || continue
    if grep -qF "$PATH_MARK_BEGIN" "$rc" 2>/dev/null; then
        info "PATH drop-in already in $(basename "$rc")"
    else
        printf '\n%s\n%s\n%s\n' "$PATH_MARK_BEGIN" "$PATH_LINE" "$PATH_MARK_END" >> "$rc"
        ok "Added PATH drop-in to $(basename "$rc")"
    fi
done
: > "$LOG" 2>/dev/null || true

echo
ok "Canary installed. Now:"
info "  1. Reboot the Deck (or switch to Game Mode) so the session picks up the PATH."
info "  2. Let Steam start in Game Mode."
info "  3. Back in Desktop:  cat ~/deck-canary.log"
info "     - a 'CANARY-HIT ... name=<steam|steam-jupiter|...>' line during Game Mode"
info "       => gamescope goes through the PATH wrapper (injection would work there)."
info "     - empty / no Game Mode hit => gamescope bypasses PATH; we adjust."
info "  4. Undo everything:  bash ~/deck-canary.sh --revert"
warn "During the test, do NOT run LumaDeck's Quick Install — headcrab would remove the shims."
