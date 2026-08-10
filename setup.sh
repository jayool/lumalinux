#!/usr/bin/env bash
# lumalinux setup — self-contained installer for the unlock stack.
#
# Unlike install.sh (which requires SLSsteam already installed by Headcrab and
# patches Headcrab's steam.sh), this script installs the WHOLE stack itself and
# injects it via a WRAPPER reached from the Steam launchers — never by patching
# steam.sh. steam.sh is SHA-verified and re-extracted by Steam whenever its size
# differs from the manifest, so a block there does not survive Steam self-updates
# (see docs/slsteam-moon-findings.md M7, docs/decouple-headcrab-plan.md).
#
# What it does:
#   1. Fetches SLSsteam.so + library-inject.so (AceSLS/SLSsteam release, .7z),
#      cloud_redirect.so (Selectively11), liblumalinux.so (this repo), and the
#      netsock.so online-route lib (yesyes0649/steamnetsock-patch).
#   2. Installs the .NET 9 runtime (Steamless needs it; LumaDeck did this too) and
#      the CloudRedirect GUI app (flatpak) for provider sign-in. Both best-effort;
#      skip with LUMA_SKIP_DOTNET=1 / LUMA_SKIP_CR_APP=1.
#   3. Writes a wrapper at ~/.local/share/SLSsteam/path/steam that exports
#      LD_AUDIT (SLSsteam) + LD_PRELOAD (CloudRedirect + lumalinux), guards
#      against a boot crash-loop (fail-safe), and execs the real Steam.
#   4. Desktop mode: points the Steam launchers (steam / steam-jupiter /
#      bazzite-steam) .desktop Exec at the wrapper (+ PATH drop-in for terminals).
#   5. Game Mode: a systemd drop-in on steam-launcher.service routes it through a
#      Game Mode launcher — the PATH drop-in does NOT reach Game Mode (verified
#      on-device). The wrapper and that launcher share ONE crash-loop fail-safe
#      (auto-vanilla on a startup crash), so a bad Steam update can never brick
#      Game Mode. Plus a systemd guardian that re-asserts .desktop coverage.
#
# No root required. Desktop mode → patched .desktop; Game Mode → PATH drop-in
# (efficacy on gamescope boot is the one real-Deck test, see plan WS5). Not
# handled: Steam-as-Flatpak paths.
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/jayool/lumalinux/main/setup.sh | bash
#   curl -fsSL .../setup.sh | bash -s -- --uninstall
#
# Overrides (reversible self-testing):
#   LUMALINUX_SO_URL   install a specific liblumalinux.so (e.g. a -test asset)
#   SLSSTEAM_7Z_URL    install a specific SLSsteam-Any.7z
#   CLOUDREDIRECT_URL  install a specific cloud_redirect.so
#   NETSOCK_SO_URL     install a specific netsock fix.so
#   LUMA_SKIP_CR_APP=1 skip the CloudRedirect flatpak (GUI sign-in) install

set -euo pipefail

# ── config ─────────────────────────────────────────────────────────────────
REPO="jayool/lumalinux"

SLS_DIR="${HOME}/.local/share/SLSsteam"
SLS_CFG_DIR="${HOME}/.config/SLSsteam"
CR_DIR="${HOME}/.local/share/CloudRedirect"
LL_DIR="${HOME}/.local/share/lumalinux"
KEYS_DIR="${HOME}/.config/lumalinux"
TOOLS_DIR="${LL_DIR}/tools"
WRAPPER_DIR="${SLS_DIR}/path"
WRAPPER="${WRAPPER_DIR}/steam"
ENSURE_SCRIPT="${SLS_DIR}/ensure-desktop-coverage.sh"

LL_SO_NAME="liblumalinux.so"
LL_SO_URL="${LUMALINUX_SO_URL:-https://github.com/${REPO}/releases/latest/download/${LL_SO_NAME}}"
SLS_7Z_URL="${SLSSTEAM_7Z_URL:-https://github.com/AceSLS/SLSsteam/releases/latest/download/SLSsteam-Any.7z}"
CR_URL="${CLOUDREDIRECT_URL:-https://github.com/Selectively11/h3adcr-b/releases/download/linux-test/cloud_redirect.so}"
CR_CLI_URL="${CLOUDREDIRECT_CLI_URL:-https://github.com/Selectively11/h3adcr-b/releases/download/linux-test/cloud_redirect_cli}"
NETSOCK_SO_URL="${NETSOCK_SO_URL:-https://github.com/yesyes0649/steamnetsock-patch/releases/latest/download/fix.so}"
NETSOCK_DIR="${SLS_CFG_DIR}/tools/netsock"

CR_FLATPAK_ID="org.cloudredirect.CloudRedirect"
CR_FLATPAK_RUNTIME="${CR_FLATPAK_RUNTIME:-org.kde.Platform//6.10}"
CR_FLATPAKREPO="https://raw.githubusercontent.com/Selectively11/CloudRedirect/refs/heads/gh-pages/cloudredirect.flatpakrepo"
FLATHUB_REPO="https://dl.flathub.org/repo/flathub.flatpakrepo"

RUNTIME_TOOLS=( steamidra_lite.py vdf_inject_keys.py )
TOOLS_BASE_URL="https://raw.githubusercontent.com/${REPO}/main/tools"

# systemd --user guardian units.
SYSTEMD_USER_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
GUARD_SERVICE="lumalinux-desktop-guardian.service"
GUARD_PATH_UNIT="lumalinux-desktop-guardian.path"
GUARD_TIMER="lumalinux-desktop-guardian.timer"
GUARD_STATE_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/lumalinux"
# Shared fail-safe library + Game Mode launcher + its systemd drop-in.
GUARD_LIB="${SLS_DIR}/lumalinux-guard.sh"
GM_LAUNCHER="${SLS_DIR}/lumalinux-steam-launcher"
GM_DROPIN_DIR="${SYSTEMD_USER_DIR}/steam-launcher.service.d"
GM_DROPIN="${GM_DROPIN_DIR}/lumalinux.conf"

# ── helpers ────────────────────────────────────────────────────────────────
c_info='\e[0;36m'; c_ok='\e[0;32m'; c_warn='\e[0;33m'; c_err='\e[0;31m'; c_rst='\e[0m'
info() { printf "${c_info}[*]${c_rst} %s\n" "$*"; }
ok()   { printf "${c_ok}[+]${c_rst} %s\n" "$*"; }
warn() { printf "${c_warn}[!]${c_rst} %s\n" "$*" >&2; }
die()  { printf "${c_err}[X]${c_rst} %s\n" "$*" >&2; exit 1; }

TMP_DIR=""
cleanup() { [[ -n "$TMP_DIR" && -d "$TMP_DIR" ]] && rm -rf "$TMP_DIR" || true; }
trap cleanup EXIT

first_cmd() { local c; for c in "$@"; do command -v "$c" 2>/dev/null && return 0; done; return 1; }

# No ELF/arch verification of the downloads: headcrab doesn't do it, and it
# wrongly rejects legitimate files (e.g. netsock is 64-bit). We download and
# install, exactly like headcrab (minus the downgrade/freeze).

# Install runtime deps per distro — headcrab's preinstallchecks + RemoveArchPkg.
# Best-effort: warn on failure, never abort (headcrab doesn't either).
install_os_deps() {
    local id="" like=""
    if [[ -r /etc/os-release ]]; then . /etc/os-release 2>/dev/null || true; id="${ID:-}"; like="${ID_LIKE:-}"; fi
    case " $id $like " in
        *" arch "*|*" cachyos "*)
            command -v pacman &>/dev/null || return 0
            local pkgs=(wget curl grep awk sed 7zip flatpak) need=() p
            for p in "${pkgs[@]}"; do pacman -Qs "$p" &>/dev/null || need+=("$p"); done
            if [[ ${#need[@]} -gt 0 ]]; then
                info "Installing deps: ${need[*]}"
                sudo pacman -S --noconfirm "${need[@]}" || warn "dependency install failed (continuing)."
            fi
            local sys; sys="$(pacman -Qq 2>/dev/null | grep -E '^slssteam(-git)?$' || true)"
            [[ -n "$sys" ]] && { info "Removing system SLSsteam package: $sys"; sudo pacman -Rns --noconfirm $sys || true; }
            ;;
        *" debian "*|*" ubuntu "*)
            command -v apt-get &>/dev/null || return 0
            local pk="libcurl4"; apt-cache search --names-only '^libcurl4t64$' 2>/dev/null | grep -q libcurl4t64 && pk="libcurl4t64"
            dpkg --print-foreign-architectures 2>/dev/null | grep -q i386 || { sudo dpkg --add-architecture i386 || true; sudo apt-get update >/dev/null 2>&1 || true; }
            sudo apt-get install -y "${pk}:i386" >/dev/null 2>&1 || warn "libcurl i386 install failed (continuing)."
            ;;
        *)
            if [[ " $id " == *" void "* ]] && command -v xbps-install &>/dev/null; then
                sudo xbps-install -y wget curl grep gawk sed 7zip flatpak 2>/dev/null || warn "void deps install failed (continuing)."
            fi
            ;;
    esac
    return 0   # never let a best-effort branch's exit status abort setup.sh (set -e)
}

# Merge the SLSsteam config template into an existing config, preserving the
# user's values and only ADDING new template keys. Faithful port of headcrab's
# updateSLSsteamConfig. $1 = template, $2 = existing config.
merge_slssteam_config() {
    local template="$1" config_file="$2" merged
    [[ -f "$template" && -f "$config_file" ]] || return 0
    grep -q '^DisableFamilyShareLock:' "$template" || { warn "SLSsteam config template invalid — skipping merge."; return 0; }
    merged="$(mktemp "${config_file}.tmp.XXXXXX")" || return 0
    if awk '
        NR == FNR {
            if ($0 ~ /^[A-Za-z_][A-Za-z0-9_]*:/) { key=$0; sub(/:.*/,"",key); current=key; present[key]=1; saved[key]=$0 ORS; next }
            if (current != "" && $0 ~ /^[[:space:]]+/) { saved[current]=saved[current] $0 ORS }
            next
        }
        /^[A-Za-z_][A-Za-z0-9_]*:/ {
            key=$0; sub(/:.*/,"",key); printf "%s", prefix; prefix=""
            if (key in present) { printf "%s", saved[key]; use_template=0 } else { print; use_template=1 }
            have_key=1; next
        }
        have_key && /^[[:space:]]+/ { if (use_template) print; next }
        { prefix=prefix $0 ORS }
        END { printf "%s", prefix }
    ' "$config_file" "$template" > "$merged"; then
        if grep -q '^DisableFamilyShareLock:' "$merged" && ! cmp -s "$config_file" "$merged"; then
            cp -a "$config_file" "${config_file}.lumalinux.bak" 2>/dev/null || true
            mv -f "$merged" "$config_file"; ok "Merged SLSsteam config (new keys added, your values kept)."
        else
            rm -f "$merged"
        fi
    else
        rm -f "$merged"; warn "SLSsteam config merge failed — leaving existing config."
    fi
}

# headcrab's editconfig, but SafeMode: no instead of headcrab's yes. headcrab's
# SafeMode: yes is the update "freno" this project removes; SafeMode: no lets
# SLSsteam hook fresh Steam builds so the wrapper tolerates updates (matches
# LumaDeck's _set_safemode_no + main.cpp's advisory hash check). We set it
# explicitly rather than "leave as-is" so a STANDALONE `curl|bash setup.sh`
# migrating from a headcrab config (SafeMode: yes on disk) also loses the freno —
# LumaDeck flips it via ensure_slssteam_flags, but standalone users don't get that.
# PlayNotOwnedGames is core (treat non-owned as owned); the notify flags are UX.
edit_slssteam_config() {
    local cfg="$1"; [[ -f "$cfg" ]] || return 0
    sed -i "s/^PlayNotOwnedGames:.*/PlayNotOwnedGames: yes/" "$cfg"
    sed -i "s/^NotifyInit:.*/NotifyInit: yes/" "$cfg"
    sed -i "s/^Notifications:.*/Notifications: yes/" "$cfg"
    # We bundle CloudRedirect, so enable it (the old LumaDeck flow set this too).
    sed -i "s/^DisableCloud:.*/DisableCloud: no/" "$cfg"
    # SafeMode: no — never leave a migrated headcrab "yes" in place (the freno).
    if grep -q '^SafeMode:' "$cfg"; then
        sed -i "s/^SafeMode:.*/SafeMode: no/" "$cfg"
    else
        printf 'SafeMode: no\n' >> "$cfg"
    fi
    # SLSsteam's default DisableUpdates: yes blocks auto-updates for the unowned
    # (AdditionalApps) games — but LumaDeck lets those update. Flip it to no, like
    # LumaDeck's _set_disableupdates_no.
    if grep -q '^DisableUpdates:' "$cfg"; then
        sed -i "s/^DisableUpdates:.*/DisableUpdates: no/" "$cfg"
    else
        printf 'DisableUpdates: no\n' >> "$cfg"
    fi
    ok "Applied SLSsteam config (PlayNotOwnedGames/NotifyInit/Notifications=yes, DisableCloud/DisableUpdates/SafeMode=no)."
}

# Restore a clean steam.sh so the WRAPPER is the sole injector, handling the two
# ways the old world touched steam.sh:
#   * headcrab REPLACED steam.sh with its own launcher (toast + GameLauncher +
#     inject). Stripping the inject line alone leaves that launcher (and its "The
#     Headcrab Approaches" toast) running every launch — so we RESTORE vanilla
#     instead: from the untouched Valve steam.sh headcrab keeps alongside as
#     client.sh, else fetched from Valve, else (last resort) line-stripped.
#   * the old lumalinux install.sh ADDED a marked block to a vanilla steam.sh —
#     there the base file is vanilla, so stripping the block restores it.
# On a device migrating from headcrab the stale block/launcher would otherwise
# (a) double-inject / lose the library-inject-first order and (b) defeat the
# crash-loop vanilla fall-back. Best-effort; a clean vanilla steam.sh is a no-op.
# steam.sh is regenerable (Steam re-extracts it), so this is low-risk.
# Markers that identify headcrab's OWN steam.sh. headcrab does NOT add a block to
# a vanilla steam.sh — it REPLACES steam.sh with a custom launcher (a "The Headcrab
# Approaches" notify-send toast, a GameLauncher/CheckClientInfo wrapper, then
# `source $STEAM_CLIENT`). So stripping the inject line alone leaves that launcher —
# and its toast — running on every Steam start. It must be RESTORED to vanilla.
_HEADCRAB_STEAMSH_RE='Headcrab|h3adcr|CheckClientInfo'

neutralize_steam_sh() {
    local r sh bak client vanilla_url tmp
    vanilla_url="${VANILLA_STEAM_SH_URL:-https://raw.githubusercontent.com/SteamDatabase/SteamTracking/master/ClientExtracted/steam.sh}"
    for r in "$HOME/.local/share/Steam" "$HOME/.steam/steam" "$HOME/.steam/root" \
             "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"; do
        sh="$r/steam.sh"
        [[ -f "$sh" ]] || continue

        if grep -qE "$_HEADCRAB_STEAMSH_RE" "$sh" 2>/dev/null; then
            # headcrab's OWN launcher → RESTORE a clean vanilla steam.sh (do NOT
            # edit in place; that leaves headcrab's toast/GameLauncher behind).
            bak="$sh.headcrab.bak"
            [[ -e "$bak" ]] || cp -f "$sh" "$bak" 2>/dev/null || true
            chmod u+w "$sh" 2>/dev/null || true
            client="$r/client.sh"
            if [[ -f "$client" ]] && ! grep -qE "$_HEADCRAB_STEAMSH_RE" "$client" 2>/dev/null; then
                # headcrab keeps the untouched Valve steam.sh alongside as client.sh.
                cp -f "$client" "$sh" 2>/dev/null \
                    && { chmod 0755 "$sh" 2>/dev/null || true; info "Restored vanilla steam.sh from client.sh at $sh (was headcrab's launcher)"; } \
                    || warn "Could not restore steam.sh from client.sh at $sh"
            else
                # No usable client.sh — fetch Valve's steam.sh. (Verify it's vanilla
                # and non-empty before swapping.)
                tmp="$sh.vanilla.tmp"
                if { command -v curl >/dev/null 2>&1 && curl -fsSL "$vanilla_url" -o "$tmp" 2>/dev/null \
                     || command -v wget >/dev/null 2>&1 && wget -qO "$tmp" "$vanilla_url" 2>/dev/null; } \
                   && [[ -s "$tmp" ]] && ! grep -qE "$_HEADCRAB_STEAMSH_RE" "$tmp" 2>/dev/null; then
                    mv -f "$tmp" "$sh"; chmod 0755 "$sh" 2>/dev/null || true
                    info "Restored vanilla steam.sh from Valve source at $sh (no usable client.sh)"
                else
                    rm -f "$tmp" 2>/dev/null || true
                    # Last resort: strip the inject + toast lines so it at least
                    # stops double-injecting and toasting; Steam re-extracts a clean
                    # steam.sh on its next size-drift check anyway.
                    sed -i -E -e '/INJECT_SLS/d' -e '/INJECT_CR/d' -e '/notify-send.*h3adcr/d' \
                        -e '/LD_AUDIT=.*(SLSsteam|library-inject)\.so/d' \
                        -e '/LD_PRELOAD=.*(cloud_redirect|liblumalinux)\.so/d' "$sh" 2>/dev/null || true
                    warn "No vanilla source for $sh — stripped inject+toast (Steam will re-extract a clean steam.sh)"
                fi
            fi
            continue
        fi

        # NOT headcrab's launcher: a vanilla steam.sh that the old lumalinux
        # install.sh patched with a marked block (or a stray injection line).
        # Here the base file IS vanilla, so stripping the block restores it.
        grep -qE 'INJECT_SLS|INJECT_CR|SLSsteam\.so|library-inject\.so|cloud_redirect\.so|liblumalinux\.so|lumalinux launcher patch' "$sh" 2>/dev/null || continue
        bak="$sh.lumalinux.pre-wrapper.bak"
        [[ -e "$bak" ]] || cp -f "$sh" "$bak" 2>/dev/null || true
        chmod u+w "$sh" 2>/dev/null || true
        sed -i -E \
            -e '/# >>> lumalinux launcher patch/d' \
            -e '/# <<< lumalinux/d' \
            -e '/INJECT_SLS/d' \
            -e '/INJECT_CR/d' \
            -e '/LD_AUDIT=.*(SLSsteam|library-inject)\.so/d' \
            -e '/LD_PRELOAD=.*(cloud_redirect|liblumalinux)\.so/d' \
            "$sh" 2>/dev/null \
          && info "Neutralized residual steam.sh injection at $sh (backup: $bak)" || true
    done
    return 0
}

# Sweep orphaned headcrab artefacts on a device migrating from the old
# headcrab + install.sh system. headcrab is decoupled now, so these are DEAD:
# the functional bits (SLSsteam/CR/lumalinux .so, config.yaml, netsock, the
# wrapper) are reinstalled/overwritten by this script and steam.sh is cleaned by
# neutralize_steam_sh — this only removes the leftover clutter, by EXACT path,
# no wildcards over shared dirs. Best-effort; never aborts; a no-op on a clean
# install (nothing matches).
#
# Deliberately NOT swept, to avoid breaking a half-migrated launch:
#   * <steamroot>/client.sh  — a de-injected headcrab steam.sh may still `source`
#     it; removing it could break launch until Steam re-extracts a vanilla
#     steam.sh. Harmless to leave.
#   * <steamroot>/package/*.manifest — Steam owns that directory.
sweep_headcrab_leftovers() {
    local removed=0 p b
    for p in \
        "$HOME/.headcrab" \
        "$HOME/.local/share/applications/headcrab.desktop" \
        "$HOME/.local/share/icons/hicolor/48x48/apps/headcrab.png" \
        "$SLS_CFG_DIR/.headcrabd" \
        "$SLS_DIR/path/steam.bak"; do
        [[ -e "$p" ]] || continue
        rm -rf "$p" 2>/dev/null && { info "Swept headcrab leftover: $p"; removed=1; }
    done
    # Timestamped SLSsteam config backups headcrab leaves (exact prefix).
    for b in "$SLS_CFG_DIR"/config.yaml.headcrab-*; do
        [[ -e "$b" ]] || continue
        rm -f "$b" 2>/dev/null && { info "Swept headcrab leftover: $b"; removed=1; }
    done
    # Refresh the menu so the dead headcrab entry actually disappears.
    if [[ "$removed" == 1 ]]; then
        command -v update-desktop-database >/dev/null 2>&1 \
            && update-desktop-database "$HOME/.local/share/applications" >/dev/null 2>&1 || true
        ok "Cleaned up old headcrab leftovers."
    fi
    return 0
}

# .NET 9 runtime — Steamless (Denuvo/DRM strip) needs it. headcrab does NOT install
# it; LumaDeck does (dotnet.py). Microsoft's own installer. Best-effort/skippable
# (LUMA_SKIP_DOTNET=1); never aborts.
install_dotnet9() {
    [[ "${LUMA_SKIP_DOTNET:-0}" == "1" ]] && { info "Skipping .NET 9 (LUMA_SKIP_DOTNET=1)."; return 0; }
    local root="$HOME/.dotnet" cand
    for cand in "$root/dotnet" "$(command -v dotnet 2>/dev/null || true)"; do
        [[ -n "$cand" && -x "$cand" ]] || continue
        if DOTNET_ROOT="$(dirname "$cand")" "$cand" --list-runtimes 2>/dev/null | grep -q "Microsoft.NETCore.App 9."; then
            ok ".NET 9 runtime already present."; return 0
        fi
    done
    info "Installing .NET 9 runtime (Steamless needs it)..."
    mkdir -p "$root"
    local script="$root/dotnet-install.sh"
    if curl -fsSL -o "$script" "https://dot.net/v1/dotnet-install.sh"; then
        chmod +x "$script"
        if DOTNET_ROOT="$root" HOME="$HOME" "$script" --channel 9.0 --runtime dotnet --install-dir "$root" >/dev/null 2>&1; then
            ok "Deployed .NET 9 runtime to $root"
        else
            warn ".NET 9 install failed — Steamless (Denuvo) fixes unavailable until retried."
        fi
    else
        warn ".NET 9 installer download failed (non-fatal)."
    fi
    return 0
}

# CloudRedirect GUI app via flatpak. Best-effort, skippable, never fatal.
install_cloudredirect_app() {
    [[ "${LUMA_SKIP_CR_APP:-0}" == "1" ]] && { info "Skipping CloudRedirect app (LUMA_SKIP_CR_APP=1)."; return 0; }
    if ! command -v flatpak &>/dev/null; then
        warn "flatpak not found — skipping CloudRedirect app (sign-in GUI). Cloud saves stay off until it is installed."
        return 0
    fi
    info "Installing CloudRedirect app (flatpak; may pull the KDE runtime)..."
    flatpak remote-add --user --if-not-exists cloudredirect "$CR_FLATPAKREPO" 2>/dev/null || true
    flatpak remote-add --user --if-not-exists flathub "$FLATHUB_REPO" 2>/dev/null || true
    if flatpak install --user -y --noninteractive flathub "$CR_FLATPAK_RUNTIME" 2>/dev/null \
       && flatpak install --user -y --noninteractive --reinstall "$CR_FLATPAK_ID" 2>/dev/null; then
        command -v update-desktop-database &>/dev/null \
            && update-desktop-database "${XDG_DATA_HOME:-$HOME/.local/share}/applications" 2>/dev/null || true
        ok "CloudRedirect app installed (sign into your provider from Desktop)."
    else
        warn "CloudRedirect app install failed (network/runtime) — the .so still works; sign-in GUI unavailable until installed."
    fi
}

# ── coverage script (deployed; also run by the guardian) ───────────────────
# Written to $ENSURE_SCRIPT. Owns .desktop patching + PATH drop-in (+ restore),
# so setup and the systemd guardian share one implementation.
write_ensure_script() {
    mkdir -p "$SLS_DIR"
    cat > "$ENSURE_SCRIPT" <<'ENSURE'
#!/usr/bin/env bash
# lumalinux launch coverage (managed by setup.sh — do not edit).
# Points every Steam launcher (steam / steam-jupiter / bazzite-steam) at the
# injection wrapper and keeps the wrapper dir first on PATH. Idempotent; run at
# install AND by the systemd guardian on every .desktop change.
# Modes: default/--user (verbose), --guardian (quiet), --uninstall (restore).
set -euo pipefail

WRAPPER="${WRAPPER:-$HOME/.local/share/SLSsteam/path/steam}"
DT_TAG="X-LumaLinux-Wrapped=1"
_LAUNCHER_RE='^(steam|steam-jupiter(-stable)?|bazzite-steam)$'
USER_APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
USER_AUTOSTART="${XDG_CONFIG_HOME:-$HOME/.config}/autostart"
PATH_MARK_BEGIN="# >>> lumalinux wrapper PATH >>> (managed by setup.sh)"
PATH_MARK_END="# <<< lumalinux wrapper PATH <<<"
PATH_LINE='export PATH="$HOME/.local/share/SLSsteam/path:$PATH"'

QUIET=0; MODE=apply
case "${1:-}" in
    --guardian) QUIET=1 ;;
    --uninstall) MODE=uninstall ;;
    --user|"") ;;
    *) ;;
esac
log() { [[ "$QUIET" -eq 1 ]] || printf '%s\n' "$*"; }

patch_desktop_inplace() {
    local f="$1" out
    grep -qF "$DT_TAG" "$f" 2>/dev/null && return 0
    out="$(awk -v w="$WRAPPER" -v re="$_LAUNCHER_RE" '
        function bn(p){ sub(/.*\//,"",p); return p }
        /^Exec=/ {
            cmd=$0; sub(/^Exec=/,"",cmd); prog=cmd; sub(/ .*/,"",prog)
            if (bn(prog) ~ re) { rest=cmd; sub(/^[^ ]*/,"",rest); print "Exec=env LUMA_STEAM_BIN=" prog " " w rest; changed=1; next }
        }
        { print }
        END { exit (changed ? 0 : 3) }
    ' "$f")" || return 0
    cp -f "$f" "${f}.lumalinux.bak"
    printf '%s\n%s\n' "$out" "$DT_TAG" > "$f"
    log "[+] Wrapped $f"
}

create_override_shadow() {
    local src="$1" dest="$2"
    [[ -f "$dest" ]] && return 0
    # Only write the shadow if an Exec line actually matched a launcher: awk exits
    # 3 (and we discard the half-written dest) otherwise, so we never leave a
    # tagged-but-UNWRAPPED shadow — that would (a) shadow the real system entry
    # with a non-injecting copy and (b) make LumaDeck's _wrapper_desktop_coverage
    # report coverage where none exists.
    if awk -v w="$WRAPPER" -v tag="$DT_TAG" -v re="$_LAUNCHER_RE" '
        function bn(p){ sub(/.*\//,"",p); return p }
        /^Exec=/ {
            cmd=$0; sub(/^Exec=/,"",cmd); prog=cmd; sub(/ .*/,"",prog)
            if (bn(prog) ~ re) { rest=cmd; sub(/^[^ ]*/,"",rest); print "Exec=env LUMA_STEAM_BIN=" prog " " w rest; changed=1; next }
        }
        { print }
        END { if (changed) { print "# lumalinux-override-shadow"; print tag } else { exit 3 } }
    ' "$src" > "$dest"; then
        log "[+] Created override $dest (shadows $src)"
    else
        rm -f "$dest"
    fi
}

install_path_dropin() {
    local rc found=0
    for rc in "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile"; do
        [[ -f "$rc" ]] || continue
        found=1
        grep -qF "$PATH_MARK_BEGIN" "$rc" 2>/dev/null && continue
        printf '\n%s\n%s\n%s\n' "$PATH_MARK_BEGIN" "$PATH_LINE" "$PATH_MARK_END" >> "$rc"
        log "[+] Added wrapper to PATH in $(basename "$rc")"
    done
    if [[ "$found" -eq 0 ]]; then
        printf '%s\n%s\n%s\n' "$PATH_MARK_BEGIN" "$PATH_LINE" "$PATH_MARK_END" > "$HOME/.bashrc"
        log "[+] Created ~/.bashrc with wrapper PATH"
    fi
}

# Resolve the (possibly localized) Desktop dir — SteamOS keeps ~/Desktop but
# other distros/locales may differ; honor xdg-user-dir when present.
_desktop_dir() {
    local d; d="$(xdg-user-dir DESKTOP 2>/dev/null || true)"
    [[ -n "$d" && -d "$d" ]] && { printf '%s' "$d"; return; }
    printf '%s' "$HOME/Desktop"
}

# ~/Desktop/steam.desktop: on SteamOS the Desktop icon is a SYMLINK to the SYSTEM
# entry (/usr/share/applications/steam.desktop), opened by PATH (not by desktop-id),
# so the ~/.local/share/applications shadow does NOT cover it — double-clicking the
# icon launches vanilla Steam with no injection (verified on-device). Re-point it at
# our wrapped shadow. Only when a wrapped shadow exists and the icon isn't already
# pointing there; backs up the original (symlink or file) once so --uninstall can
# restore it.
cover_desktop_shortcut() {
    local ddir desk shadow bak
    ddir="$(_desktop_dir)"; [[ -d "$ddir" ]] || return 0
    shopt -s nullglob
    for desk in "$ddir/"*steam*.desktop; do
        shadow="$USER_APPS/$(basename "$desk")"
        [[ -f "$shadow" ]] && grep -qF "$DT_TAG" "$shadow" 2>/dev/null || continue
        [[ "$(readlink -f "$desk" 2>/dev/null)" == "$(readlink -f "$shadow" 2>/dev/null)" ]] && continue
        bak="${desk}.lumalinux.bak"
        [[ -e "$bak" ]] || cp -P "$desk" "$bak" 2>/dev/null || true
        ln -sfn "$shadow" "$desk"
        log "[+] Re-pointed Desktop icon $desk -> wrapper shadow"
    done
    shopt -u nullglob
}

restore_desktop_entries() {
    local f bak ddir
    ddir="$(_desktop_dir)"
    shopt -s nullglob
    for bak in "$USER_APPS/"*steam*.desktop.lumalinux.bak \
               "$USER_AUTOSTART/"*steam*.desktop.lumalinux.bak \
               "$ddir/"*steam*.desktop.lumalinux.bak; do
        f="${bak%.lumalinux.bak}"; mv -f "$bak" "$f"; log "[+] Restored $f"
    done
    for f in "$USER_APPS/"*steam*.desktop "$USER_AUTOSTART/"*steam*.desktop; do
        [[ -f "$f" ]] || continue
        if grep -qF "$DT_TAG" "$f" 2>/dev/null && grep -qF "# lumalinux-override-shadow" "$f" 2>/dev/null; then
            rm -f "$f"; log "[+] Removed override $f"
        fi
    done
    shopt -u nullglob
}

remove_path_dropin() {
    local rc
    for rc in "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile"; do
        [[ -f "$rc" ]] || continue
        grep -qF "$PATH_MARK_BEGIN" "$rc" 2>/dev/null || continue
        sed -i "/^# >>> lumalinux wrapper PATH >>>/,/^# <<< lumalinux wrapper PATH <<</d" "$rc"
        log "[+] Removed PATH drop-in from $(basename "$rc")"
    done
}

if [[ "$MODE" == "uninstall" ]]; then
    restore_desktop_entries
    remove_path_dropin
    exit 0
fi

mkdir -p "$USER_APPS" "$USER_AUTOSTART"
shopt -s nullglob
for f in "$USER_APPS/"*steam*.desktop /usr/share/applications/*steam*.desktop; do
    [[ -f "$f" ]] || continue
    if [[ -w "$f" ]]; then patch_desktop_inplace "$f"
    else create_override_shadow "$f" "$USER_APPS/$(basename "$f")"; fi
done
for f in "$USER_AUTOSTART/"steam*.desktop; do
    [[ -f "$f" ]] && patch_desktop_inplace "$f"
done
[[ -f /etc/xdg/autostart/steam.desktop ]] \
    && create_override_shadow /etc/xdg/autostart/steam.desktop "$USER_AUTOSTART/steam.desktop"
shopt -u nullglob
cover_desktop_shortcut
install_path_dropin
log "[+] Coverage reconciled."
ENSURE
    chmod 0755 "$ENSURE_SCRIPT"
}

# ── systemd guardian ───────────────────────────────────────────────────────
# A .path watches the launcher directories and, on any change, runs the oneshot
# .service which re-asserts coverage (ensure-desktop-coverage.sh --guardian). A
# .timer reconciles periodically as a backstop. This is what survives a Steam
# update that regenerates its own .desktop entries. Best-effort: if there is no
# systemd --user session, we warn and rely on the per-launch re-assert instead.
have_user_systemd() { command -v systemctl &>/dev/null && systemctl --user show-environment &>/dev/null; }

install_guardian_units() {
    if ! have_user_systemd; then
        warn "No systemd --user session — guardian not installed (the wrapper re-asserts on each launch instead)."
        return 0
    fi
    mkdir -p "$SYSTEMD_USER_DIR"

    cat > "${SYSTEMD_USER_DIR}/${GUARD_SERVICE}" <<EOF
# X-LumaLinux-GuardianUnit=true
[Unit]
Description=Reconcile Steam launch coverage for lumalinux

[Service]
Type=oneshot
Nice=10
IOSchedulingClass=idle
ExecStart=%h/.local/share/SLSsteam/ensure-desktop-coverage.sh --guardian
EOF

    cat > "${SYSTEMD_USER_DIR}/${GUARD_PATH_UNIT}" <<EOF
# X-LumaLinux-GuardianUnit=true
[Unit]
Description=Watch Steam launch sources for lumalinux reconciliation

[Path]
PathChanged=%h/.local/share/applications
PathChanged=/usr/share/applications
PathChanged=%h/.config/autostart
PathChanged=/etc/xdg/autostart
PathChanged=%h/Desktop
Unit=${GUARD_SERVICE}

[Install]
WantedBy=default.target
EOF

    cat > "${SYSTEMD_USER_DIR}/${GUARD_TIMER}" <<EOF
# X-LumaLinux-GuardianUnit=true
[Unit]
Description=Periodic Steam launch coverage reconciliation for lumalinux

[Timer]
OnStartupSec=30s
OnUnitActiveSec=5min
Persistent=true
Unit=${GUARD_SERVICE}

[Install]
WantedBy=timers.target
EOF

    systemctl --user daemon-reload 2>/dev/null || true
    if systemctl --user enable --now "$GUARD_PATH_UNIT" "$GUARD_TIMER" 2>/dev/null; then
        ok "Guardian installed (watches launcher dirs + 5-min reconcile)."
    else
        warn "Guardian units written but could not be enabled — the wrapper still re-asserts on each launch."
    fi
}

remove_guardian_units() {
    have_user_systemd || return 0
    systemctl --user disable --now "$GUARD_PATH_UNIT" "$GUARD_TIMER" 2>/dev/null || true
    rm -f "${SYSTEMD_USER_DIR}/${GUARD_SERVICE}" \
          "${SYSTEMD_USER_DIR}/${GUARD_PATH_UNIT}" \
          "${SYSTEMD_USER_DIR}/${GUARD_TIMER}" 2>/dev/null || true
    systemctl --user daemon-reload 2>/dev/null || true
    systemctl --user reset-failed 2>/dev/null || true
    ok "Guardian removed."
}

# ── crash-loop fail-safe (shared by Desktop wrapper + Game Mode launcher) ──
# The guard library holds the whole fail-safe (crash-dump detection, latch,
# auto-vanilla) so both launch paths use ONE implementation. The Desktop wrapper
# sources it; the Game Mode launcher (below) sources it too.
write_guard_lib() {
    mkdir -p "$SLS_DIR"
    cat > "$GUARD_LIB" <<'GUARD'
#!/bin/sh
# lumalinux crash-loop fail-safe (managed by setup.sh — do not edit).
# Caller sets $STEAM_BIN then calls: luma_guard_run "$@"  — which either execs
# $STEAM_BIN vanilla (if crash-looping / safe mode latched) or exports the
# injection env (LD_AUDIT=SLSsteam, LD_PRELOAD=CloudRedirect+lumalinux) and
# returns. Vanilla unsets both, disabling the whole stack at once. Best-effort:
# any probe failing degrades to "proceed", never latches by accident.
SLS_DIR="$HOME/.local/share/SLSsteam"
CR_SO="$HOME/.local/share/CloudRedirect/cloud_redirect.so"
LL_SO="$HOME/.local/share/lumalinux/liblumalinux.so"
GDIR="${XDG_STATE_HOME:-$HOME/.local/state}/lumalinux"
G_LAST="$GDIR/last_launch"; G_COUNT="$GDIR/boot_fail_count"
G_SAFE="$GDIR/safe_mode"; G_FP="$GDIR/safe_mode_fingerprint"
G_CLIENT_LAST="$GDIR/last_client"; G_CLIENT_GOOD="$GDIR/good_client"; G_LOG="$GDIR/guard.log"
: "${LUMA_GUARD_MAX_FAILS:=3}"; : "${LUMA_GUARD_STARTUP_SECS:=180}"; : "${LUMA_GUARD_DUMPS_DIR:=/tmp/dumps}"

g_log()    { printf '%s %s\n' "$(date '+%F %T' 2>/dev/null)" "$1" >> "$G_LOG" 2>/dev/null || true; }
g_notify() { command -v notify-send >/dev/null 2>&1 && notify-send -u normal -t 10000 "Steam recovery mode" "$1" >/dev/null 2>&1 || true; }
g_int()    { _v="$(cat "$1" 2>/dev/null)"; case "$_v" in ''|*[!0-9]*) printf 0 ;; *) printf '%s' "$_v" ;; esac; }
g_client_fp() {
    for _r in "$HOME/.steam/steam" "$HOME/.steam/debian-installation" "$HOME/.local/share/Steam"; do
        _c="$_r/ubuntu12_32/steamclient.so"
        [ -e "$_c" ] && { stat -c '%s:%Y' "$_c" 2>/dev/null || stat -f '%z:%m' "$_c" 2>/dev/null || printf '?'; return 0; }
    done; printf ''
}
g_fingerprint() {
    for _f in "$SLS_DIR/SLSsteam.so" "$CR_SO" "$LL_SO" "$HOME/.config/SLSsteam/tools/netsock/netsock.so"; do
        if [ -e "$_f" ]; then stat -c '%s:%Y' "$_f" 2>/dev/null || stat -f '%z:%m' "$_f" 2>/dev/null || printf '?'; else printf -- '-'; fi
        printf '|'
    done
}
g_startup_crash() {
    [ -d "$LUMA_GUARD_DUMPS_DIR" ] || return 1
    case "$2" in ''|*[!0-9]*) return 1 ;; esac
    [ "$2" -gt 0 ] || return 1
    _ref="$GDIR/.crash_win_ref"
    touch -d "@$(( $2 + LUMA_GUARD_STARTUP_SECS ))" "$_ref" 2>/dev/null || { rm -f "$_ref" 2>/dev/null; return 1; }
    _hit="$(find "$LUMA_GUARD_DUMPS_DIR" -maxdepth 1 -name 'crash_*.dmp' -newer "$1" ! -newer "$_ref" 2>/dev/null | head -n1)"
    rm -f "$_ref" 2>/dev/null
    [ -n "$_hit" ]
}
luma_exec_vanilla() { exec env -u LD_AUDIT -u LD_PRELOAD -u LD_LIBRARY_PATH "$STEAM_BIN" "$@"; }
luma_set_injection_env() {
    for _p in "$CR_SO" "$LL_SO"; do [ -f "$_p" ] && LD_PRELOAD="$_p${LD_PRELOAD:+:$LD_PRELOAD}"; done
    [ -n "${LD_PRELOAD:-}" ] && export LD_PRELOAD
    if [ "${XDG_SESSION_TYPE:-}" = "wayland" ]; then
        for _e in /usr/lib/extest/libextest.so /usr/lib64/extest/libextest.so \
                  /usr/lib/x86_64-linux-gnu/extest/libextest.so; do
            [ -f "$_e" ] && { export LD_PRELOAD="${LD_PRELOAD:+$LD_PRELOAD:}$_e"; break; }
        done
    fi
    if [ -f "$SLS_DIR/library-inject.so" ]; then
        export LD_AUDIT="$SLS_DIR/library-inject.so:$SLS_DIR/SLSsteam.so"
    else
        export LD_AUDIT="$SLS_DIR/SLSsteam.so"
    fi
}
luma_guard_run() {
    mkdir -p "$GDIR" 2>/dev/null || true
    _cur_fp="$(g_fingerprint)"
    if [ -f "$G_SAFE" ]; then
        if [ "$(cat "$G_FP" 2>/dev/null)" = "$_cur_fp" ]; then
            g_log "safe mode active -> launching Steam vanilla ($STEAM_BIN)"; luma_exec_vanilla "$@"
        fi
        g_log "payload changed since latch -> clearing safe mode, retrying injection"
        rm -f "$G_SAFE" "$G_FP" "$G_COUNT" "$G_LAST" 2>/dev/null || true
    fi
    _fails="$(g_int "$G_COUNT")"; _client_cur="$(g_client_fp)"; _client_changed=0
    if [ -f "$G_LAST" ]; then
        _then="$(stat -c %Y "$G_LAST" 2>/dev/null || stat -f %m "$G_LAST" 2>/dev/null || echo 0)"
        if g_startup_crash "$G_LAST" "$_then"; then
            _fails=$(( _fails + 1 ))
            _prev="$(cat "$G_CLIENT_LAST" 2>/dev/null || true)"; _good="$(cat "$G_CLIENT_GOOD" 2>/dev/null || true)"
            if [ -n "$_good" ] && [ "$_prev" != "$_good" ]; then
                _client_changed=1; g_log "steamclient.so changed since last clean boot -> first crash = compat break"
            fi
            g_log "previous boot crashed at startup -> fail ${_fails}/${LUMA_GUARD_MAX_FAILS}"
        elif [ -d "$LUMA_GUARD_DUMPS_DIR" ]; then
            # Only treat "no crash dump" as a clean boot when the dumps dir EXISTS
            # (so the absence of a dump is real evidence). If the dir is absent we
            # are blind — a non-dumping crash (OOM/hang/breakpad-off) must NOT be
            # read as success: do nothing (leave the counter, and crucially do NOT
            # promote a possibly-bad steamclient.so to the known-good baseline,
            # which would defeat the "client changed -> latch on first crash" path).
            [ "$_fails" -ne 0 ] && g_log "previous boot ok -> reset fail count"
            _fails=0
            _prev="$(cat "$G_CLIENT_LAST" 2>/dev/null || true)"
            [ -n "$_prev" ] && printf '%s' "$_prev" > "$G_CLIENT_GOOD" 2>/dev/null || true
        else
            g_log "no dumps dir -> cannot assess previous boot; leaving fail/good state unchanged"
        fi
    fi
    printf '%s' "$_fails" > "$G_COUNT" 2>/dev/null || true
    if [ "$_fails" -ge "$LUMA_GUARD_MAX_FAILS" ] || { [ "$_client_changed" = 1 ] && [ "$_fails" -ge 1 ]; }; then
        g_log "latching safe mode (fails=${_fails} client_changed=${_client_changed}) -> vanilla ($STEAM_BIN)"
        printf '%s' "$_cur_fp" > "$G_FP" 2>/dev/null || true
        : > "$G_SAFE" 2>/dev/null || true
        for _r in "$HOME/.steam/steam" "$HOME/.steam/debian-installation" "$HOME/.local/share/Steam"; do
            [ -f "$_r/appcache/appinfo.vdf" ] && rm -f "$_r/appcache/appinfo.vdf" 2>/dev/null && g_log "removed $_r/appcache/appinfo.vdf"
        done
        g_notify "lumalinux is paused because Steam failed to start after a recent update. Steam is running normally — update the stack to re-enable it."
        luma_exec_vanilla "$@"
    fi
    : > "$G_LAST" 2>/dev/null || true
    printf '%s' "$_client_cur" > "$G_CLIENT_LAST" 2>/dev/null || true
    luma_set_injection_env
}
GUARD
    chmod 0644 "$GUARD_LIB"
    return 0
}

# Game Mode launcher — wraps SteamOS's steam-launcher (run by steam-launcher.service)
# with the SAME fail-safe + injection as the Desktop wrapper. Reached via a systemd
# drop-in (below), because Game Mode does NOT go through PATH or .desktop.
write_gamemode_launcher() {
    mkdir -p "$SLS_DIR"
    cat > "$GM_LAUNCHER" <<'GML'
#!/bin/sh
# lumalinux Game Mode launcher (managed by setup.sh — do not edit).
STEAM_BIN="/usr/lib/steamos/steam-launcher"
[ -x "$STEAM_BIN" ] || STEAM_BIN="$(command -v steam-launcher 2>/dev/null || echo /usr/lib/steamos/steam-launcher)"
. "$HOME/.local/share/SLSsteam/lumalinux-guard.sh"
luma_guard_run "$@"          # execs vanilla if crash-looping, else sets injection env
exec "$STEAM_BIN" "$@"
GML
    chmod 0755 "$GM_LAUNCHER"
    return 0
}

# systemd drop-in that routes steam-launcher.service (Game Mode) through our
# launcher. ExecStart= resets the unit's ExecStart, then points it at ours; all
# other directives (ExecStartPre tracker, ExecStop, ...) are preserved.
install_gamemode_dropin() {
    if ! have_user_systemd; then
        warn "No systemd --user session — Game Mode drop-in not installed (Desktop coverage still active)."
        return 0
    fi
    mkdir -p "$GM_DROPIN_DIR"
    cat > "$GM_DROPIN" <<EOF
# lumalinux Game Mode injection (managed by setup.sh — safe to delete)
[Service]
ExecStart=
ExecStart=%h/.local/share/SLSsteam/lumalinux-steam-launcher
EOF
    systemctl --user daemon-reload 2>/dev/null || true
    ok "Game Mode injection installed (steam-launcher.service drop-in, via fail-safe)."
    return 0
}
remove_gamemode_dropin() {
    have_user_systemd || return 0
    if [[ -f "$GM_DROPIN" ]]; then
        rm -f "$GM_DROPIN"; rmdir "$GM_DROPIN_DIR" 2>/dev/null || true
        systemctl --user daemon-reload 2>/dev/null || true
        ok "Removed Game Mode drop-in."
    fi
    return 0
}

# ── dependency check ───────────────────────────────────────────────────────
command -v curl &>/dev/null || die "curl is required."

# ── uninstall ──────────────────────────────────────────────────────────────
if [[ "${1:-}" == "--uninstall" ]]; then
    info "Uninstalling lumalinux wrapper stack..."
    remove_gamemode_dropin
    remove_guardian_units
    if [[ -x "$ENSURE_SCRIPT" ]]; then
        "$ENSURE_SCRIPT" --uninstall || true
    fi
    rm -f "$ENSURE_SCRIPT" "$GUARD_LIB" "$GM_LAUNCHER" 2>/dev/null || true
    [[ -f "$WRAPPER" ]] && { rm -f "$WRAPPER"; ok "Removed $WRAPPER"; }
    [[ -d "$WRAPPER_DIR" ]] && rmdir "$WRAPPER_DIR" 2>/dev/null || true
    rm -rf "$GUARD_STATE_DIR" 2>/dev/null || true
    info "Kept the .so's, SLSsteam/lumalinux config, and keys.txt (delete manually if desired)."
    info "Restart Steam to return to a vanilla launch."
    exit 0
fi

install_os_deps

SEVENZIP="$(first_cmd 7z 7za 7zr || true)"
[[ -n "$SEVENZIP" ]] || die "7z is required to extract SLSsteam-Any.7z (install p7zip / 7zip)."

# ── fetch ──────────────────────────────────────────────────────────────────
info "Installing lumalinux unlock stack (self-contained, wrapper injection)..."
TMP_DIR="$(mktemp -d)"
mkdir -p "$SLS_DIR" "$SLS_CFG_DIR" "$CR_DIR" "$LL_DIR" "$KEYS_DIR" "$WRAPPER_DIR"

# SLSsteam + library-inject (bundled in SLSsteam-Any.7z under bin/).
info "Downloading SLSsteam..."
curl -fL --progress-bar -o "${TMP_DIR}/sls.7z" "$SLS_7Z_URL" \
    || die "Failed to download SLSsteam-Any.7z ($SLS_7Z_URL)."
"$SEVENZIP" x -aoa -o"${TMP_DIR}/sls" "${TMP_DIR}/sls.7z" >/dev/null \
    || die "Failed to extract SLSsteam-Any.7z."
[[ -f "${TMP_DIR}/sls/bin/SLSsteam.so" ]] \
    || die "SLSsteam.so not found in the archive (upstream layout changed?)."
install -m 0755 "${TMP_DIR}/sls/bin/SLSsteam.so" "${SLS_DIR}/SLSsteam.so"
ok "Deployed ${SLS_DIR}/SLSsteam.so"
if [[ -f "${TMP_DIR}/sls/bin/library-inject.so" ]]; then
    install -m 0755 "${TMP_DIR}/sls/bin/library-inject.so" "${SLS_DIR}/library-inject.so"
    ok "Deployed ${SLS_DIR}/library-inject.so"
else
    warn "library-inject.so not in the archive — the wrapper will load SLSsteam.so alone."
fi

# Record the installed SLSsteam version so LumaDeck can detect updates. SLSsteam
# embeds its version (a build timestamp) ONLY inside the compiled .so and writes
# nothing readable to disk — unlike lumalinux, whose injected .so drops a
# status.json with its version. So we capture the release tag we just pulled from
# /releases/latest/ (its tag IS that timestamp) and stash it where the plugin's
# get_sls_version() reads it. Best-effort: on API failure we write nothing (the
# plugin then reports "no update", the safe default). Skipped when a custom
# SLSSTEAM_7Z_URL is set, since the AceSLS 'latest' tag would not describe it.
if [[ -z "${SLSSTEAM_7Z_URL:-}" ]]; then
    # Resolve the release tag the SAME way headcrab does: follow the /releases/latest
    # redirect and read the FINAL url (.../releases/tag/<tag>), then strip to the tag
    # with pure shell. This deliberately avoids api.github.com (rate-limited) and has
    # no pipe (so no SIGPIPE under `set -o pipefail`). `|| true` keeps a network
    # failure from aborting the install; the guards below reject a non-redirect
    # (offline → url still ends in "latest") so we never record a bogus tag.
    _sls_tag="$(curl -sSL --connect-timeout 15 --max-time 30 -o /dev/null \
        -w '%{url_effective}' "https://github.com/AceSLS/SLSsteam/releases/latest" 2>/dev/null || true)"
    _sls_tag="${_sls_tag##*/}"
    if [[ -n "$_sls_tag" && "$_sls_tag" != "latest" ]]; then
        printf '%s\n' "$_sls_tag" > "$SLS_CFG_DIR/.slssteam.version" 2>/dev/null \
            && ok "Recorded SLSsteam version: $_sls_tag" \
            || warn "Could not write SLSsteam version file (update detection will be skipped)."
    else
        info "Could not resolve SLSsteam release tag — update detection will be skipped."
    fi
fi

# Config: seed from the template if absent, else merge (preserving your values);
# then apply headcrab's functional settings (minus SafeMode — the freno).
if [[ -f "${TMP_DIR}/sls/res/config.yaml" ]]; then
    if [[ ! -f "${SLS_CFG_DIR}/config.yaml" ]]; then
        install -m 0644 "${TMP_DIR}/sls/res/config.yaml" "${SLS_CFG_DIR}/config.yaml"
        ok "Seeded ${SLS_CFG_DIR}/config.yaml (default)"
    else
        merge_slssteam_config "${TMP_DIR}/sls/res/config.yaml" "${SLS_CFG_DIR}/config.yaml"
    fi
fi
edit_slssteam_config "${SLS_CFG_DIR}/config.yaml"

# CloudRedirect .so (inert until a provider is signed in) + GUI app.
info "Downloading CloudRedirect..."
if curl -fL --progress-bar -o "${TMP_DIR}/cloud_redirect.so" "$CR_URL"; then
    install -m 0755 "${TMP_DIR}/cloud_redirect.so" "${CR_DIR}/cloud_redirect.so"
    ok "Deployed ${CR_DIR}/cloud_redirect.so"
    # cloud_redirect_cli — headcrab fetches this alongside the .so (crinstall).
    if curl -fL -o "${CR_DIR}/cloud_redirect_cli" "$CR_CLI_URL" 2>/dev/null; then
        chmod 0755 "${CR_DIR}/cloud_redirect_cli"
        ok "Deployed ${CR_DIR}/cloud_redirect_cli"
    else
        warn "cloud_redirect_cli download failed (non-fatal)."
    fi
else
    warn "CloudRedirect download failed — continuing without it (cloud saves stay off)."
fi
install_cloudredirect_app

# netsock (required on disk for LumaDeck's per-game online fixes).
info "Downloading netsock..."
mkdir -p "$NETSOCK_DIR"
if curl -fL --progress-bar -o "${TMP_DIR}/netsock.so" "$NETSOCK_SO_URL"; then
    install -m 0755 "${TMP_DIR}/netsock.so" "${NETSOCK_DIR}/netsock.so"
    ok "Deployed ${NETSOCK_DIR}/netsock.so"
else
    warn "netsock download failed — per-game online (netsock) fixes will be unavailable."
fi

# lumalinux.so.
info "Downloading lumalinux..."
curl -fL --progress-bar -o "${TMP_DIR}/${LL_SO_NAME}" "$LL_SO_URL" \
    || die "Failed to download ${LL_SO_NAME} ($LL_SO_URL)."
install -m 0755 "${TMP_DIR}/${LL_SO_NAME}" "${LL_DIR}/${LL_SO_NAME}"
ok "Deployed ${LL_DIR}/${LL_SO_NAME}"

# keys.txt + runtime tools.
if [[ ! -f "${KEYS_DIR}/keys.txt" ]]; then
    cat > "${KEYS_DIR}/keys.txt" <<'EOF'
# lumalinux depot key store
# Extended format (one per line):
#   <depot_id>;<parent_app_id>;<manifest_gid>;<manifest_size>;<64-hex-key>
# Lines starting with # are ignored. Populate with tools/steamidra_lite.py
# or via LumaDeck.
EOF
    ok "Created ${KEYS_DIR}/keys.txt (empty)"
fi
mkdir -p "$TOOLS_DIR"
for tool in "${RUNTIME_TOOLS[@]}"; do
    curl -fsSL -o "${TOOLS_DIR}/${tool}" "${TOOLS_BASE_URL}/${tool}" \
        && chmod 0755 "${TOOLS_DIR}/${tool}" && ok "Deployed ${TOOLS_DIR}/${tool}" \
        || warn "Failed to download ${TOOLS_BASE_URL}/${tool} (continuing; runtime helper, not needed for injection)."
done

# .NET 9 runtime (Steamless) — LumaDeck installs this during deps; headcrab doesn't.
install_dotnet9

# ── write the wrapper (with the crash-loop fail-safe) ──────────────────────
# Single-quoted heredoc: everything expands at Steam-launch time, not now.
info "Writing injection wrapper at $WRAPPER..."
cat > "$WRAPPER" <<'WRAP'
#!/bin/sh
# lumalinux injection wrapper (managed by setup.sh — do not edit).
# Exports the loader env for the unlock stack, then execs the real Steam.
# SLSsteam -> LD_AUDIT (library-inject.so FIRST). CloudRedirect + lumalinux ->
# LD_PRELOAD (NEVER LD_AUDIT: CloudRedirect as an auditor corrupts the client
# heap). Reached via the patched *steam*.desktop Exec= lines and the PATH drop-in.
SLS_DIR="$HOME/.local/share/SLSsteam"
CR_SO="$HOME/.local/share/CloudRedirect/cloud_redirect.so"
LL_SO="$HOME/.local/share/lumalinux/liblumalinux.so"

# Resolve the REAL steam binary, skipping this wrapper. The patched .desktop
# entries pass LUMA_STEAM_BIN=<original launcher> (steam / steam-jupiter-stable /
# bazzite-steam) so we exec the SAME launcher that was wrapped, not always plain
# `steam` — losing that would drop e.g. jupiter/Deck-specific setup.
_self="$HOME/.local/share/SLSsteam/path/steam"
_is_self() { [ "$(readlink -f "$1" 2>/dev/null)" = "$(readlink -f "$_self" 2>/dev/null)" ]; }
STEAM_BIN=""
if [ -n "${LUMA_STEAM_BIN:-}" ]; then
    # Accept a full path as-is, or resolve a bare launcher name via PATH; never
    # accept ourselves (the PATH drop-in puts the wrapper dir first).
    case "$LUMA_STEAM_BIN" in
        */*) _cand="$LUMA_STEAM_BIN" ;;
        *)   _cand="$(command -v "$LUMA_STEAM_BIN" 2>/dev/null || true)" ;;
    esac
    if [ -n "$_cand" ] && [ -x "$_cand" ] && ! _is_self "$_cand"; then STEAM_BIN="$_cand"; fi
fi
if [ -z "$STEAM_BIN" ]; then
    _old_ifs="$IFS"; IFS=:
    for _d in $PATH; do
        _c="$_d/steam"
        [ -x "$_c" ] || continue
        _is_self "$_c" && continue
        STEAM_BIN="$_c"; break
    done
    IFS="$_old_ifs"
    if [ -z "$STEAM_BIN" ]; then
        for _c in /usr/bin/steam /usr/games/steam /usr/lib/steam/steam; do
            [ -x "$_c" ] && { STEAM_BIN="$_c"; break; }
        done
    fi
fi
[ -n "$STEAM_BIN" ] || { echo "lumalinux wrapper: real steam binary not found" >&2; exit 1; }

# Fail-safe (crash-loop protection) + injection env — shared with Game Mode.
. "$SLS_DIR/lumalinux-guard.sh"
luma_guard_run "$@"          # execs vanilla if crash-looping, else sets injection env

# Re-assert .desktop coverage on each launch (best-effort; Desktop path only).
command -v systemctl >/dev/null 2>&1 && systemctl --user start lumalinux-desktop-guardian.service >/dev/null 2>&1 &

exec "$STEAM_BIN" "$@"
WRAP
chmod 0755 "$WRAPPER"
ok "Wrapper written."

# ── fail-safe library + coverage + guardian ────────────────────────────────
write_guard_lib
ok "Fail-safe library written at $GUARD_LIB"
write_ensure_script
ok "Coverage helper written at $ENSURE_SCRIPT"
WRAPPER="$WRAPPER" "$ENSURE_SCRIPT" --user
install_guardian_units

# Game Mode coverage: the systemd drop-in on steam-launcher.service (the PATH
# drop-in does NOT reach Game Mode — verified on-device). Routed through the
# fail-safe so a bad update can't crash-loop Game Mode into a brick.
write_gamemode_launcher
ok "Game Mode launcher written at $GM_LAUNCHER"
install_gamemode_dropin

# Migration safety: with the wrapper + coverage now in place, strip any leftover
# headcrab/old-lumalinux injection from steam.sh so the wrapper is the sole
# injector (no double injection, and the crash-loop vanilla fall-back really is
# vanilla), then sweep the dead headcrab clutter (menu entry, tools, markers,
# backups). Both no-ops on a clean install.
neutralize_steam_sh
sweep_headcrab_leftovers

ok "Done. Restart Steam to load the stack."
info "On startup, look for the toast: 'lumalinux: vX.Y.Z loaded - N/N hooks active'."
info "Desktop mode: patched .desktop entries + guardian."
info "Game Mode: steam-launcher.service drop-in (via the crash-loop fail-safe)."
