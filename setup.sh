#!/usr/bin/env bash
# lumalinux setup — self-contained installer for the unlock stack.
#
# Unlike install.sh (which requires SLSsteam already installed by Headcrab and
# patches Headcrab's steam.sh), this script installs the WHOLE stack itself and
# injects it via a WRAPPER reached from the Steam launchers — never by patching
# steam.sh. steam.sh is SHA-verified and re-extracted by Steam whenever its size
# differs from the manifest, so a block there does not survive Steam self-updates
# (see docs/slsteam-moon.md §5.2 M22, docs/nosotros.md §2.1).
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
# No root required. Desktop mode → patched .desktop (+ PATH drop-in for terminals);
# Game Mode → systemd drop-in on steam-launcher.service (the PATH drop-in does NOT
# reach Game Mode — verified on-device). Not handled: Steam-as-Flatpak paths.
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/jayool/lumalinux/main/setup.sh | bash
#   curl -fsSL .../setup.sh | bash -s -- --uninstall
#
# Overrides (reversible self-testing):
#   LUMALINUX_SO_URL   install a specific liblumalinux.so (e.g. a -test asset)
#   SLSSTEAM_7Z_URL    install a specific SLSsteam release archive
#   CLOUDREDIRECT_URL  install a specific cloud_redirect.so (skips the sha256 pin)
#   CLOUDREDIRECT_FLATPAK_URL  install a specific CloudRedirect .flatpak bundle
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
# 64-bit twin for Valve's steamrt64 client (Deck beta since 2026-10; stable
# behind the "Steam RT3 Experimental" opt-in). Both go into LD_PRELOAD: each
# client's ld.so loads the one of its own ELF class and rejects the other
# ("wrong ELF class ... ignored") before mapping it — measured 2026-10-23 on
# both channels. Releases before v0.22.5 do not carry it: optional download.
LL64_SO_NAME="liblumalinux-x86_64.so"
LL64_SO_URL="${LUMALINUX64_SO_URL:-https://github.com/${REPO}/releases/latest/download/${LL64_SO_NAME}}"
# SLSsteam's release asset was renamed upstream: the single `SLSsteam-Any.7z`
# became per-variant `SLSsteam-Any-release.7z` / `-debug.7z` (AceSLS 6a06652,
# first published in the 20260819131545 release). We want the plain release
# build. Both names are tried, newest first, so older pinned releases still
# resolve. Filled in by resolve_slssteam_asset() at fetch time.
SLS_7Z_ASSETS=("SLSsteam-Any-release.7z" "SLSsteam-Any.7z")
SLS_7Z_URL=""
SLS_TAG=""
# Two Steam clients coexist for a while (32-bit ubuntu12_32 on stable, 64-bit
# steamrt64 on the beta and the RT3 opt-in) and an SLSsteam.so serves exactly
# one of them. Each archive's SLSsteam.so is placed by its ELF class, never by
# the asset's name: 32-bit at $SLS_DIR/SLSsteam.so (as always), 64-bit at
# $SLS_DIR/x86_64/SLSsteam.so (same file name: SLSsteam itself matches its own
# name, e.g. when it strips itself from LD_AUDIT). The launcher names both in
# LD_AUDIT and each client's ld.so rejects the other ("wrong ELF class").
# A 64-bit archive is never installed over the 32-bit file, whatever upstream
# calls the asset.
#
# Upstream recompiled SLSsteam in 64-bit and dropped 32-bit support (release
# notes of the first x86_64 build, 2026-10-22: "Recompile in 64 bit; dropped
# 32 bit support"). So `latest` describes the 64-bit build from the day that
# build is published, and the 32-bit client's SLSsteam is frozen at the last
# 32-bit release, SLS32_TAG, the way CloudRedirect is frozen at 2.6.6: when
# `latest` is not a 32-bit build, that tag is installed for the 32-bit client
# (skipped when it is already in place). Bump SLS32_TAG only if upstream ever
# publishes another 32-bit build; LumaDeck carries the same tag
# (slssteam_version.LAST_32BIT_TAG) as the latest the 32-bit client is compared
# against. SLSSTEAM32_URL overrides where the 32-bit archive comes from (a .7z
# with a 32-bit bin/SLSsteam.so), SLSSTEAM64_URL where a 64-bit one comes from
# (optional; what `latest` carries once upstream publishes the 64-bit build).
SLS32_TAG="20261014151919"
SLS32_7Z_URL="${SLSSTEAM32_URL:-}"
SLS64_7Z_URL="${SLSSTEAM64_URL:-}"
# CloudRedirect's upstream (Selectively11/CloudRedirect) was deleted on
# 2026-10-16 together with its author's account; nothing is published there any
# more and nobody expects it back (docs/cloudredirect.md §0). We ship the last
# Linux build ourselves, from our own release assets: the 2.6.6 cloud_redirect.so
# archived off a Deck (sha256-pinned below; it still hooks the 2026-10-14 client,
# measured on the Deck) and the sign-in app as a single .flatpak bundle built from
# upstream's last gh-pages publish (2.6.6, commit 83100b8, preserved in
# madoiscool/CloudRedirect; it embeds that same .so and cloud_redirect_cli — see
# deploy_cloudredirect_cli). Both are assets of the v0.22.3 release and both are
# on the `cr-archive` branch (vendor/cloudredirect/, never merged). See
# docs/maintenance.md.
CR_SO_URL="${CLOUDREDIRECT_URL:-https://github.com/${REPO}/releases/download/v0.22.3/cloud_redirect-2.6.6%2B3434d9d.so}"
CR_SO_SHA256="86a40032168da2739a6633bdeae795b95c1464b3414998fbfd703b5bb1795f8b"
NETSOCK_SO_URL="${NETSOCK_SO_URL:-https://github.com/yesyes0649/steamnetsock-patch/releases/latest/download/fix.so}"
NETSOCK_DIR="${SLS_CFG_DIR}/tools/netsock"

CR_FLATPAK_ID="org.cloudredirect.CloudRedirect"
CR_FLATPAK_RUNTIME="${CR_FLATPAK_RUNTIME:-org.kde.Platform//6.10}"
CR_FLATPAK_URL="${CLOUDREDIRECT_FLATPAK_URL:-https://github.com/${REPO}/releases/download/v0.22.3/cloudredirect-2.6.6.flatpak}"
CR_FLATPAK_SHA256="bd361482c990ebdd9f05db25846b9b1d0545599d09e269c22be6a04fcb96f52c"
FLATHUB_REPO="https://dl.flathub.org/repo/flathub.flatpakrepo"

RUNTIME_TOOLS=( steamidra_lite.py vdf_inject_keys.py )
# Resolved next to the download (resolve_tools_ref): the tools ride the same
# tag as the .so; TOOLS_BASE_URL is only the fallback.
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

# The runtime tools come from the same git ref as the .so, so a Deck never
# runs a steamidra_lite.py newer (or older) than its liblumalinux.so
# (nosotros.md §3.1). The ref is the tag in the .so URL when it names one
# (releases/download/<tag>/...), else the tag releases/latest redirects to;
# main only when neither can be read.
resolve_tools_ref() {
    local so_url="$1" ref=""
    case "$so_url" in
        */releases/download/*/*)
            ref="${so_url#*/releases/download/}"; ref="${ref%%/*}" ;;
        */releases/latest/download/*)
            ref="$(curl -fsSI -o /dev/null -w '%{redirect_url}' "https://github.com/${REPO}/releases/latest" 2>/dev/null || true)"
            ref="${ref##*/}" ;;
    esac
    if [[ "$ref" =~ ^v[0-9][A-Za-z0-9._-]*$ ]]; then printf '%s' "$ref"; else printf 'main'; fi
}

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

# Reconcile an existing SLSsteam config against the current template. The OUTPUT
# follows the TEMPLATE's key set: keys present in both keep the USER's value
# (AdditionalApps AppIDs, ManifestIds, flag choices, …), template-only keys are
# added with their default, and keys NOT in the current template — options SLSsteam
# has since removed, e.g. PlayNotOwnedGames — are DROPPED. So the config always
# tracks SLSsteam's current schema while preserving user data. $1 = template,
# $2 = existing config.
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

# Flip SLSsteam's config values to what the wrapper stack needs. Every key here
# must exist in the CURRENT SLSsteam default config — the seed/merge above tracks
# the template's key set, so a key upstream has removed is already gone from the
# file and a bare sed for it would be dead code (that is what happened to
# `Notifications`, see below). Deprecated keys (e.g. PlayNotOwnedGames, removed in
# SLSsteam 20260707) need no handling for the same reason. What we flip and why:
#   SafeMode: no        — undo headcrab's update "freno" (also SLSsteam's own default;
#                          lets it hook fresh Steam builds, matches main.cpp advisory hash)
#   DisableCloud: no    — we bundle CloudRedirect, so cloud stays on
#   DisableUpdates: no  — AdditionalApps (unowned) games must be allowed to update
#   NotifyInit: yes     — the "SLSsteam is up" toast
#
# NOT flipped, deliberately: `Notifications`. It was the other half of the toast
# config until SLSsteam removed it in the 20260815 release; that behaviour now
# lives in the LogLevels bitmask (NotifyShort 0x40 / NotifyLong 0x80), whose
# default 0xff already enables both. We leave LogLevels alone.
edit_slssteam_config() {
    local cfg="$1"; [[ -f "$cfg" ]] || return 0

    # Cosmetic toast — flip in place, best-effort (a miss only loses a toast).
    sed -i "s/^NotifyInit:.*/NotifyInit: yes/" "$cfg"

    # Functional keys — flip in place, and if the flip DIDN'T take (the key was
    # absent or not top-level: e.g. the SLSsteam template was missing so the
    # seed/merge above never ran and we're editing a foreign config), APPEND it so
    # the value is GUARANTEED. Relying only on the bare sed matching is what let
    # SafeMode silently stay "yes" (headcrab's update freno) — or DisableUpdates
    # stay "yes" (unowned games can't update) — while we printed "Applied".
    # Append only fires when no top-level key exists, so it never duplicates one.
    _sls_ensure_kv() {  # $1=key $2=value  (edits $cfg via the outer local)
        sed -i "s/^$1:.*/$1: $2/" "$cfg"
        grep -qE "^$1:[[:space:]]*$2([[:space:]]|\$)" "$cfg" || printf '%s: %s\n' "$1" "$2" >> "$cfg"
    }
    _sls_ensure_kv DisableCloud   no
    _sls_ensure_kv SafeMode       no
    _sls_ensure_kv DisableUpdates no
    # Our SLSsteam plugin (lumalinux-cuser.lua, deployed below) needs the
    # plugin system on; LumaDeck sets this too for its spliced-tickets plugin.
    _sls_ensure_kv Plugins        yes

    # Only claim success once the three functional keys are verified. If any is
    # still wrong (e.g. the append couldn't write), warn loudly instead of lying.
    local bad=""
    for _key in SafeMode DisableUpdates DisableCloud; do
        grep -qE "^${_key}:[[:space:]]*no([[:space:]]|\$)" "$cfg" || bad="${bad:+$bad, }${_key}"
    done
    if [[ -z "$bad" ]]; then
        ok "Applied SLSsteam config (SafeMode/DisableCloud/DisableUpdates=no, NotifyInit=yes)."
    else
        warn "SLSsteam config: ${bad} could NOT be set to 'no' in $cfg — SLSsteam's update-freeze/block may still be active. Check the file by hand."
    fi
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
                # Each downloader is wrapped in its own { } so this reads as
                # (curl-block) || (wget-block). Without the inner braces, bash's
                # equal-precedence left-assoc &&/|| parse it as
                # ((curlAvail && curlDL) || wgetAvail) && wgetDL — which forces
                # wget to run and succeed in EVERY case, so a curl-only Deck (no
                # wget, and pacman can't add it on the read-only rootfs) fails the
                # vanilla restore even after curl already fetched it, and falls to
                # the lossy sed strip below.
                if { { command -v curl >/dev/null 2>&1 && curl -fsSL "$vanilla_url" -o "$tmp" 2>/dev/null; } \
                     || { command -v wget >/dev/null 2>&1 && wget -qO "$tmp" "$vanilla_url" 2>/dev/null; } ; } \
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
        "$SLS_DIR/path/steam.bak" \
        "$CR_DIR/cloud_redirect_lib"; do
        # cloud_redirect_lib is headcrab's name for the CloudRedirect CLI; we ship
        # it as cloud_redirect_cli, so its file is never overwritten and lingers as
        # a dead duplicate after migration. Verified orphaned in a real headcrab->
        # wrapper migration. Exact path, only removes headcrab's stale name.
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

# ── SLSsteam release asset + tag ───────────────────────────────────────────
# Resolves which archive to download AND which release it is.
#
# Why not api.github.com: rate-limited for unauthenticated callers, and a 403
# there would leave us unable to install at all. We use the release redirects
# instead — but in two steps, because of a GitHub behaviour that is easy to get
# wrong:
#
#   /releases/latest/download/<name>  ->  302  /releases/download/<TAG>/<name>
#
# GitHub issues that 302 for ANY <name>, whether or not the release publishes an
# asset with that name; the 404 only appears at the SECOND hop. So the redirect
# is a reliable way to learn the tag and a useless way to test the asset.
#
# Step 1 therefore reads the tag off the redirect, and step 2 asks for the pinned
# URL with -L and checks the FINAL status to pick the name this release really
# ships. Both come out of the same resolution, so the version we record can never
# describe a different build than the one we install (the previous code resolved
# the two independently, and the tag lookup could fail on its own — leaving the
# plugin permanently unable to detect SLSsteam updates, silently).
resolve_slssteam_asset() {
    # Explicit override: use it verbatim and record no tag — it need not be an
    # AceSLS release at all, so any tag we invented would be a lie.
    if [[ -n "${SLSSTEAM_7Z_URL:-}" ]]; then
        SLS_7Z_URL="$SLSSTEAM_7Z_URL"
        SLS_TAG=""
        return 0
    fi

    local base="https://github.com/AceSLS/SLSsteam/releases"
    local loc tag asset url code

    # Step 1 — the tag. No -f/-L: we want the 302 itself, not the body.
    loc="$(curl -sS --connect-timeout 15 --max-time 30 -o /dev/null \
           -w '%{redirect_url}' "${base}/latest/download/${SLS_7Z_ASSETS[0]}" \
           2>/dev/null || true)"
    [[ "$loc" == *"/releases/download/"* ]] || return 1
    tag="${loc#*/releases/download/}"
    tag="${tag%%/*}"
    # AceSLS tags releases with a build timestamp (YYYYMMDDHHMMSS). Anything else
    # — the stale rolling `update` tag, an error page — is rejected, so we never
    # record a "version" the plugin's compare cannot order.
    [[ "$tag" =~ ^[0-9]{14}$ ]] || return 1

    # Step 2 — which asset name this release actually publishes.
    url="$(slssteam_release_asset_url "$tag")" || return 1
    SLS_7Z_URL="$url"
    SLS_TAG="$tag"
    return 0
}

# The archive URL of release <tag>: the first of SLS_7Z_ASSETS that the release
# really ships (-L and the FINAL status, see above). Prints it; fails when none.
slssteam_release_asset_url() {
    local tag="$1" base="https://github.com/AceSLS/SLSsteam/releases" asset url code
    for asset in "${SLS_7Z_ASSETS[@]}"; do
        url="${base}/download/${tag}/${asset}"
        code="$(curl -sS -I -L --connect-timeout 15 --max-time 60 -o /dev/null \
                -w '%{http_code}' "$url" 2>/dev/null || true)"
        [[ "$code" == "200" ]] || continue
        printf '%s\n' "$url"
        return 0
    done
    return 1
}

# ELF class byte (offset 4): 1 = 32-bit, 2 = 64-bit. Decides where an
# SLSsteam.so goes.
elf_class() { od -An -tu1 -j4 -N1 "$1" 2>/dev/null | tr -d ' '; }

# Download one SLSsteam archive and install its SLSsteam.so where the client of
# its ELF class looks. install_slssteam_archive <url> <tag> <label> [<class>]:
# <tag> is recorded for that class ("" records nothing: an archive that is not
# an AceSLS release has no tag); <label> names the archive in messages; <class>
# (1 or 2) refuses an archive of the other class instead of installing it.
# Sets SLS_INSTALLED_CLASS to the class installed; the first archive's
# res/config.yaml becomes SLS_CFG_TEMPLATE. Never fatal: the caller decides
# what a missing build means for each client.
SLS_INSTALLED_CLASS=""
SLS_CFG_TEMPLATE=""
install_slssteam_archive() {
    local url="$1" tag="$2" label="$3" want="${4:-}" dir cls
    dir="${TMP_DIR}/sls-$(printf '%s' "$label" | tr -c 'A-Za-z0-9' '_')"
    SLS_INSTALLED_CLASS=""
    info "Downloading SLSsteam (${label})${tag:+, release ${tag}}..."
    if ! curl -fL --progress-bar -o "${dir}.7z" "$url"; then
        warn "SLSsteam (${label}) download failed ($url)."
        return 1
    fi
    if ! "$SEVENZIP" x -aoa -o"$dir" "${dir}.7z" >/dev/null; then
        warn "SLSsteam (${label}) archive could not be extracted."
        return 1
    fi
    if [[ ! -f "$dir/bin/SLSsteam.so" ]]; then
        warn "SLSsteam.so not found in the ${label} archive (upstream layout changed?)."
        return 1
    fi
    cls="$(elf_class "$dir/bin/SLSsteam.so")"
    if [[ -n "$want" && "$cls" != "$want" ]]; then
        warn "SLSsteam (${label}) archive does not carry a $([[ "$want" == 2 ]] && echo 64 || echo 32)-bit SLSsteam.so — ignored."
        return 1
    fi
    case "$cls" in
        1)
            install -m 0755 "$dir/bin/SLSsteam.so" "${SLS_DIR}/SLSsteam.so"
            ok "Deployed ${SLS_DIR}/SLSsteam.so (32-bit client, ${label})"
            # library-inject.so belongs to the 32-bit stack: it ships next to
            # the 32-bit build (an empty placeholder since 20260903, which the
            # wrapper skips; the 64-bit archive carries an empty one too).
            if [[ -f "$dir/bin/library-inject.so" ]]; then
                install -m 0755 "$dir/bin/library-inject.so" "${SLS_DIR}/library-inject.so"
                ok "Deployed ${SLS_DIR}/library-inject.so"
            else
                warn "library-inject.so not in the ${label} archive — the wrapper will load SLSsteam.so alone."
            fi
            [[ -n "$tag" ]] && record_slssteam_version "$tag" ".slssteam.version"
            ;;
        2)
            mkdir -p "${SLS_DIR}/x86_64"
            install -m 0755 "$dir/bin/SLSsteam.so" "${SLS_DIR}/x86_64/SLSsteam.so"
            ok "Deployed ${SLS_DIR}/x86_64/SLSsteam.so (64-bit client, ${label})"
            [[ -n "$tag" ]] && record_slssteam_version "$tag" ".slssteam.version.x86_64"
            ;;
        *)
            warn "SLSsteam.so in the ${label} archive is not an ELF file — ignored."
            return 1
            ;;
    esac
    SLS_INSTALLED_CLASS="$cls"
    [[ -z "$SLS_CFG_TEMPLATE" && -f "$dir/res/config.yaml" ]] && SLS_CFG_TEMPLATE="$dir/res/config.yaml"
    return 0
}

# Record the installed SLSsteam release so LumaDeck can detect updates. SLSsteam
# embeds its version ONLY inside the compiled .so and writes nothing readable to
# disk — unlike lumalinux, whose injected .so drops a status.json with its
# version — so "which release is installed" has to be remembered at install time,
# the way a package manager does. One file per client: `.slssteam.version` for
# the 32-bit build, `.slssteam.version.x86_64` for the 64-bit one (LumaDeck's
# slssteam_version.py reads the one of the client that runs).
record_slssteam_version() {
    local tag="$1" file="$2"
    mkdir -p "$SLS_CFG_DIR"
    printf '%s\n' "$tag" > "$SLS_CFG_DIR/$file" 2>/dev/null \
        && ok "Recorded SLSsteam version: $tag ($file)" \
        || warn "Could not write $SLS_CFG_DIR/$file (update detection will be skipped)."
}

# ── CloudRedirect .so: ours to ship now ────────────────────────────────────
# Downloads from our pinned asset and verifies it (the pin is skipped only when
# CLOUDREDIRECT_URL overrides the source). Not fatal any more: with upstream gone
# there is no newer release to look for, so a failed download keeps an
# already-installed .so, and with none on disk the stack installs without cloud
# saves — the caller says which. DisableCloud stays `no` either way: SLSsteam only
# applies it to apps it does not see as owned, and the package-0 injection makes
# ours owned, so flipping it would change nothing for our games
# (docs/slssteam.md §2.8).
fetch_cloudredirect_so() {
    local tmp="${TMP_DIR}/cloud_redirect.so" sum
    if ! curl -fL --progress-bar -o "$tmp" "$CR_SO_URL"; then
        warn "CloudRedirect download failed ($CR_SO_URL)."
        return 1
    fi
    if [[ -z "${CLOUDREDIRECT_URL:-}" ]]; then
        sum="$(sha256sum "$tmp" | cut -d' ' -f1)"
        if [[ "$sum" != "$CR_SO_SHA256" ]]; then
            warn "CloudRedirect download does not match the pinned sha256 (got ${sum:0:12}…, expected ${CR_SO_SHA256:0:12}…) — not installing it."
            return 1
        fi
    fi
    install -m 0755 "$tmp" "${CR_DIR}/cloud_redirect.so"
    ok "Deployed ${CR_DIR}/cloud_redirect.so"
}

# cloud_redirect_cli — the 32-bit shim the CloudRedirect UI uses to reach the
# 32-bit .so from a 64-bit process. It is not published in the releases, but the
# flatpak bundles it next to the .so from the same build, so we take it from
# there instead of reintroducing a second download source. Best-effort: nothing
# in LumaDeck drives the CLI yet, and CloudRedirect itself works without it.
deploy_cloudredirect_cli() {
    local loc cli
    loc="$(flatpak info --user --show-location "$CR_FLATPAK_ID" 2>/dev/null \
           || flatpak info --show-location "$CR_FLATPAK_ID" 2>/dev/null || true)"
    if [[ -z "$loc" || ! -d "$loc" ]]; then
        info "CloudRedirect app not installed — skipping cloud_redirect_cli."
        return 0
    fi
    cli="${loc}/files/share/cloud_redirect/cloud_redirect_cli"
    if [[ -f "$cli" ]]; then
        install -m 0755 "$cli" "${CR_DIR}/cloud_redirect_cli" \
            && ok "Deployed ${CR_DIR}/cloud_redirect_cli (from the flatpak)" \
            || warn "Could not copy cloud_redirect_cli out of the flatpak (non-fatal)."
    else
        info "cloud_redirect_cli not bundled in this CloudRedirect app build — skipping."
    fi
}

# CloudRedirect GUI app (provider sign-in; also where cloud_redirect_cli comes
# from) from our .flatpak bundle. The KDE runtime still comes from Flathub. The
# `cloudredirect` remote earlier setups added points at upstream's deleted Pages
# and makes every flatpak command print a summary error, so it is dropped.
# Best-effort, skippable, never fatal.
install_cloudredirect_app() {
    [[ "${LUMA_SKIP_CR_APP:-0}" == "1" ]] && { info "Skipping CloudRedirect app (LUMA_SKIP_CR_APP=1)."; return 0; }
    if ! command -v flatpak &>/dev/null; then
        warn "flatpak not found — skipping CloudRedirect app (sign-in GUI). Cloud saves stay off until it is installed."
        return 0
    fi
    local bundle="${TMP_DIR}/cloudredirect.flatpak" sum
    info "Installing CloudRedirect app (flatpak bundle; may pull the KDE runtime)..."
    flatpak remote-delete --user --force cloudredirect 2>/dev/null || true
    if ! curl -fL --progress-bar -o "$bundle" "$CR_FLATPAK_URL"; then
        warn "CloudRedirect app download failed ($CR_FLATPAK_URL) — the .so still works; sign-in GUI unavailable until installed."
        return 0
    fi
    if [[ -z "${CLOUDREDIRECT_FLATPAK_URL:-}" ]]; then
        sum="$(sha256sum "$bundle" | cut -d' ' -f1)"
        if [[ "$sum" != "$CR_FLATPAK_SHA256" ]]; then
            warn "CloudRedirect app bundle does not match the pinned sha256 (got ${sum:0:12}…) — not installing it."
            return 0
        fi
    fi
    flatpak remote-add --user --if-not-exists flathub "$FLATHUB_REPO" 2>/dev/null || true
    if flatpak install --user -y --noninteractive flathub "$CR_FLATPAK_RUNTIME" 2>/dev/null \
       && flatpak install --user -y --noninteractive --reinstall "$bundle" 2>/dev/null; then
        command -v update-desktop-database &>/dev/null \
            && update-desktop-database "${XDG_DATA_HOME:-$HOME/.local/share}/applications" 2>/dev/null || true
        ok "CloudRedirect app installed (sign into your provider from Desktop)."
    else
        warn "CloudRedirect app install failed (runtime/bundle) — the .so still works; sign-in GUI unavailable until installed."
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
LL64_SO="$HOME/.local/share/lumalinux/liblumalinux-x86_64.so"
SLS64_SO="$SLS_DIR/x86_64/SLSsteam.so"
GDIR="${XDG_STATE_HOME:-$HOME/.local/state}/lumalinux"
G_LAST="$GDIR/last_launch"; G_COUNT="$GDIR/boot_fail_count"
G_SAFE="$GDIR/safe_mode"; G_FP="$GDIR/safe_mode_fingerprint"
G_CLIENT_LAST="$GDIR/last_client"; G_CLIENT_GOOD="$GDIR/good_client"; G_LOG="$GDIR/guard.log"
: "${LUMA_GUARD_MAX_FAILS:=3}"; : "${LUMA_GUARD_STARTUP_SECS:=180}"; : "${LUMA_GUARD_DUMPS_DIR:=/tmp/dumps}"
: "${LUMA_GUARD_NO_COREDUMP:=0}"

g_log()    { printf '%s %s\n' "$(date '+%F %T' 2>/dev/null)" "$1" >> "$G_LOG" 2>/dev/null || true; }
g_notify() { command -v notify-send >/dev/null 2>&1 && notify-send -u normal -t 10000 "Steam recovery mode" "$1" >/dev/null 2>&1 || true; }
g_int()    { _v="$(cat "$1" 2>/dev/null)"; case "$_v" in ''|*[!0-9]*) printf 0 ;; *) printf '%s' "$_v" ;; esac; }
g_client_fp() {
    # Both clients ship in one package since 2026-10-21 (ubuntu12_32 boots on
    # stable, steamrt64 on beta / opt-in): fingerprint whichever exist, joined.
    for _r in "$HOME/.steam/steam" "$HOME/.steam/debian-installation" "$HOME/.local/share/Steam"; do
        _fp=''
        for _c in "$_r/ubuntu12_32/steamclient.so" "$_r/steamrt64/steamclient.so"; do
            [ -e "$_c" ] && _fp="$_fp$(stat -c '%s:%Y' "$_c" 2>/dev/null || stat -f '%z:%m' "$_c" 2>/dev/null || printf '?')/"
        done
        [ -n "$_fp" ] && { printf '%s' "$_fp"; return 0; }
    done; printf ''
}
g_fingerprint() {
    for _f in "$SLS_DIR/SLSsteam.so" "$SLS64_SO" "$CR_SO" "$LL_SO" "$LL64_SO" "$HOME/.config/SLSsteam/tools/netsock/netsock.so"; do
        if [ -e "$_f" ]; then stat -c '%s:%Y' "$_f" 2>/dev/null || stat -f '%z:%m' "$_f" 2>/dev/null || printf '?'; else printf -- '-'; fi
        printf '|'
    done
}
# Two sources of crash evidence, OR'ed:
#  * Steam's own minidumps (*.dmp in /tmp/dumps, written by crashhandler.so).
#    SLSsteam 20261014151919 chokes crashhandler.so from loading, so on a Deck
#    with that release Steam creates /tmp/dumps and never writes into it.
#  * systemd-coredump: without crashhandler.so a client crash goes to the
#    kernel, SteamOS hands it to systemd-coredump (through steamos_log_submitter's
#    core_pattern hook) and `coredumpctl list` shows it with time and exe. Only
#    the client itself counts (ubuntu12_32/steam, or steamrt64/steam on the
#    64-bit client); steamwebhelper and games crash on their own.
# "Can assess the previous boot" = either source exists; with neither we are
# blind and leave the counters alone (see luma_guard_run).
g_coredump_available() { [ "$LUMA_GUARD_NO_COREDUMP" = 0 ] && command -v coredumpctl >/dev/null 2>&1; }
g_coredump_crash() {  # $1 = launch epoch; a client core between launch and launch+window
    g_coredump_available || return 1
    _until=$(( $1 + LUMA_GUARD_STARTUP_SECS ))
    timeout 5 coredumpctl list --no-pager --no-legend --since="@$1" --until="@$_until" 2>/dev/null \
        | grep -q -E '[[:space:]]/[^[:space:]]*/(ubuntu12_32|steamrt64)/steam([[:space:]]|$)'
}
g_startup_crash() {
    case "$2" in ''|*[!0-9]*) return 1 ;; esac
    [ "$2" -gt 0 ] || return 1
    g_coredump_crash "$2" && return 0
    [ -d "$LUMA_GUARD_DUMPS_DIR" ] || return 1
    _ref="$GDIR/.crash_win_ref"
    touch -d "@$(( $2 + LUMA_GUARD_STARTUP_SECS ))" "$_ref" 2>/dev/null || { rm -f "$_ref" 2>/dev/null; return 1; }
    # Match ANY minidump, not just crash_*.dmp: Steam also writes assert_*.dmp for
    # assertion-failure crashes (and the time window already scopes this to dumps
    # created right after a launch), so a bad-inject crash-loop via an assertion is
    # caught too — otherwise the anti-brick fail-safe would miss it.
    _hit="$(find "$LUMA_GUARD_DUMPS_DIR" -maxdepth 1 -name '*.dmp' -newer "$1" ! -newer "$_ref" 2>/dev/null | head -n1)"
    rm -f "$_ref" 2>/dev/null
    [ -n "$_hit" ]
}
luma_exec_vanilla() { exec env -u LD_AUDIT -u LD_PRELOAD -u LD_LIBRARY_PATH "$STEAM_BIN" "$@"; }
# NOTE ON THE -s TESTS BELOW. They used to be -f (exists). SLSsteam has shipped
# bin/library-inject.so as a ZERO-BYTE file since its 20260903 release — AceSLS
# disabled that component on 2026-08-26 and its Makefile now literally `touch`es
# an empty file — and -f is true for an empty file, so we were naming a 0-byte
# object as the FIRST entry of LD_AUDIT. ld.so then prints
#   object '...library-inject.so' cannot be loaded as audit interface:
#   file too short; ignored
# into every single process Steam spawns. It is only noise (the loader skips it
# and goes on to SLSsteam.so), but it is noise we generate, in every game, and it
# buries real loader errors. -s means "exists and is non-empty", which is what we
# actually meant for the three .so files WE install and name. The libextest test
# below stays -f on purpose: that one is the distro's, not ours to second-guess.
luma_set_injection_env() {
    # LL_SO (i386) and LL64_SO (x86_64) both named: each process's ld.so keeps
    # the one of its ELF class and skips the other with a one-line warning.
    for _p in "$CR_SO" "$LL_SO" "$LL64_SO"; do [ -s "$_p" ] && LD_PRELOAD="$_p${LD_PRELOAD:+:$LD_PRELOAD}"; done
    [ -n "${LD_PRELOAD:-}" ] && export LD_PRELOAD
    if [ "${XDG_SESSION_TYPE:-}" = "wayland" ]; then
        for _e in /usr/lib/extest/libextest.so /usr/lib64/extest/libextest.so \
                  /usr/lib/x86_64-linux-gnu/extest/libextest.so; do
            [ -f "$_e" ] && { export LD_PRELOAD="${LD_PRELOAD:+$LD_PRELOAD:}$_e"; break; }
        done
    fi
    # library-inject.so first (when non-empty), then the 32-bit SLSsteam.so, then
    # the 64-bit one under x86_64/ (when installed): each client's ld.so keeps
    # the auditor of its own ELF class and skips the other with a warning.
    _aud=""
    for _p in "$SLS_DIR/library-inject.so" "$SLS_DIR/SLSsteam.so" "$SLS64_SO"; do
        [ -s "$_p" ] && _aud="${_aud:+$_aud:}$_p"
    done
    [ -n "$_aud" ] && export LD_AUDIT="$_aud"
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
        elif [ -d "$LUMA_GUARD_DUMPS_DIR" ] || g_coredump_available; then
            # Only treat "no crash seen" as a clean boot when a source of evidence
            # EXISTS (the dumps dir or coredumpctl), so the absence of a crash is
            # real evidence. With neither we are blind — a non-dumping crash
            # (OOM/hang/breakpad-off) must NOT be read as success: do nothing
            # (leave the counter, and crucially do NOT promote a possibly-bad
            # steamclient.so to the known-good baseline, which would defeat the
            # "client changed -> latch on first crash" path).
            [ "$_fails" -ne 0 ] && g_log "previous boot ok -> reset fail count"
            _fails=0
            _prev="$(cat "$G_CLIENT_LAST" 2>/dev/null || true)"
            [ -n "$_prev" ] && printf '%s' "$_prev" > "$G_CLIENT_GOOD" 2>/dev/null || true
        else
            g_log "no dumps dir and no coredumpctl -> cannot assess previous boot; leaving fail/good state unchanged"
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
    # ALWAYS write the drop-in file — it is just a file, and systemd reads unit
    # drop-ins fresh on the next Game Mode start. Only the LIVE apply
    # (daemon-reload) needs a running user bus. Skipping the whole thing when the
    # bus isn't reachable RIGHT NOW was a real bug: the LumaDeck Desktop hand-off
    # runs setup.sh from an autostart konsole before the user bus is up, so
    # `have_user_systemd` returned false, the drop-in was never written, and Game
    # Mode launched steam-launcher.service vanilla (uninjected) while Desktop —
    # which uses the .desktop/PATH path, not systemd — worked. The file must land
    # regardless; it takes effect on the next Game Mode boot. (write_gamemode_launcher
    # runs just before this and is systemd-independent, so the target launcher
    # always exists when this drop-in points at it.)
    mkdir -p "$GM_DROPIN_DIR"
    cat > "$GM_DROPIN" <<EOF
# lumalinux Game Mode injection (managed by setup.sh — safe to delete)
[Service]
ExecStart=
ExecStart=%h/.local/share/SLSsteam/lumalinux-steam-launcher
EOF
    if have_user_systemd; then
        systemctl --user daemon-reload 2>/dev/null || true
        ok "Game Mode injection installed (steam-launcher.service drop-in, via fail-safe)."
    else
        warn "Game Mode drop-in written but not live-applied (no user bus right now) — it takes effect on the next Game Mode start."
    fi
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
[[ -n "$SEVENZIP" ]] || die "7z is required to extract the SLSsteam archive (install p7zip / 7zip)."

# ── fetch ──────────────────────────────────────────────────────────────────
info "Installing lumalinux unlock stack (self-contained, wrapper injection)..."
TMP_DIR="$(mktemp -d)"
mkdir -p "$SLS_DIR" "$SLS_CFG_DIR" "$CR_DIR" "$LL_DIR" "$KEYS_DIR" "$WRAPPER_DIR"

# SLSsteam (+ library-inject, bundled in the 32-bit release archive under
# bin/). `latest` first; then the 32-bit client's build when `latest` is not
# one (SLS32_TAG, see the top); then an optional 64-bit archive by URL.
latest_class=""
if resolve_slssteam_asset; then
    [[ -n "$SLS_TAG" ]] && info "SLSsteam release: ${SLS_TAG}"
    install_slssteam_archive "$SLS_7Z_URL" "$SLS_TAG" "latest" && latest_class="$SLS_INSTALLED_CLASS"
else
    warn "Could not resolve the latest SLSsteam release asset (tried: ${SLS_7Z_ASSETS[*]}). Upstream may have renamed it again — see SLS_7Z_ASSETS. Falling back to the last 32-bit release (${SLS32_TAG}); the 64-bit client gets no new SLSsteam."
fi
if [[ "$latest_class" != "1" ]]; then
    if [[ -n "$SLS32_7Z_URL" ]]; then
        install_slssteam_archive "$SLS32_7Z_URL" "" "32-bit, SLSSTEAM32_URL" 1 || true
    elif [[ -s "${SLS_DIR}/SLSsteam.so" && "$(cat "$SLS_CFG_DIR/.slssteam.version" 2>/dev/null)" == "$SLS32_TAG" ]]; then
        ok "32-bit SLSsteam ${SLS32_TAG} already installed (the last 32-bit release)."
    else
        [[ "$latest_class" == "2" ]] && info "The latest SLSsteam release is 64-bit only: installing the last 32-bit release (${SLS32_TAG}) for the 32-bit client."
        if url32="$(slssteam_release_asset_url "$SLS32_TAG")"; then
            install_slssteam_archive "$url32" "$SLS32_TAG" "32-bit" 1 || true
        else
            warn "Release ${SLS32_TAG} has no archive named ${SLS_7Z_ASSETS[*]} (see SLS32_TAG / SLS_7Z_ASSETS)."
        fi
    fi
    if [[ ! -s "${SLS_DIR}/SLSsteam.so" ]]; then
        warn "No 32-bit SLSsteam on this machine: the 32-bit client gets no SLSsteam."
    fi
fi
if [[ -n "$SLS64_7Z_URL" ]]; then
    install_slssteam_archive "$SLS64_7Z_URL" "" "64-bit, SLSSTEAM64_URL" 2 || true
fi
[[ -s "${SLS_DIR}/SLSsteam.so" || -s "${SLS_DIR}/x86_64/SLSsteam.so" ]] \
    || die "No SLSsteam.so installed for either client."

# Config: seed from the template if absent, else merge (preserving your values);
# then apply headcrab's functional settings (minus SafeMode — the freno).
if [[ -n "$SLS_CFG_TEMPLATE" ]]; then
    if [[ ! -f "${SLS_CFG_DIR}/config.yaml" ]]; then
        install -m 0644 "$SLS_CFG_TEMPLATE" "${SLS_CFG_DIR}/config.yaml"
        ok "Seeded ${SLS_CFG_DIR}/config.yaml (default)"
    else
        merge_slssteam_config "$SLS_CFG_TEMPLATE" "${SLS_CFG_DIR}/config.yaml"
    fi
fi
edit_slssteam_config "${SLS_CFG_DIR}/config.yaml"

# CloudRedirect .so. Used to be fatal ("cloud enabled without CloudRedirect is
# incoherent"); since upstream vanished a failed download must not take the
# whole install with it. Keep what is there, or install without cloud and say so.
info "Downloading CloudRedirect..."
if ! fetch_cloudredirect_so; then
    if [[ -s "${CR_DIR}/cloud_redirect.so" ]]; then
        warn "Keeping the CloudRedirect already installed at ${CR_DIR}/cloud_redirect.so."
    else
        warn "No CloudRedirect on this machine: added games will show Steam's cloud sync error until it is installed (re-run setup.sh, or set CLOUDREDIRECT_URL)."
    fi
fi
# The app first: it is where cloud_redirect_cli comes from, so the copy below
# needs it in place.
install_cloudredirect_app
deploy_cloudredirect_cli

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
if curl -fsL -o "${TMP_DIR}/${LL64_SO_NAME}" "$LL64_SO_URL" && [[ -s "${TMP_DIR}/${LL64_SO_NAME}" ]]; then
    install -m 0755 "${TMP_DIR}/${LL64_SO_NAME}" "${LL_DIR}/${LL64_SO_NAME}"
    ok "Deployed ${LL_DIR}/${LL64_SO_NAME} (64-bit client)"
elif [[ -s "${LL_DIR}/${LL64_SO_NAME}" ]]; then
    warn "${LL64_SO_NAME} download failed — keeping the installed one."
else
    warn "${LL64_SO_NAME} not in this release — the 64-bit (steamrt64) client gets no lumalinux hooks until a release ships it."
fi

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
TOOLS_REF="$(resolve_tools_ref "$LL_SO_URL")"
TOOLS_REF_URL="https://raw.githubusercontent.com/${REPO}/${TOOLS_REF}/tools"
info "Runtime tools from ${TOOLS_REF} (same ref as the .so)"
for tool in "${RUNTIME_TOOLS[@]}"; do
    if curl -fsSL -o "${TOOLS_DIR}/${tool}" "${TOOLS_REF_URL}/${tool}"; then
        chmod 0755 "${TOOLS_DIR}/${tool}" && ok "Deployed ${TOOLS_DIR}/${tool} (${TOOLS_REF})"
    elif [[ "$TOOLS_REF" != "main" ]] && curl -fsSL -o "${TOOLS_DIR}/${tool}" "${TOOLS_BASE_URL}/${tool}"; then
        chmod 0755 "${TOOLS_DIR}/${tool}" && warn "Deployed ${TOOLS_DIR}/${tool} from main (${TOOLS_REF} has no ${tool})"
    else
        warn "Failed to download ${tool} from ${TOOLS_REF_URL} (continuing; runtime helper, not needed for injection)."
    fi
done

# Our SLSsteam plugin: hands Steam's CUser to lumalinux's license reconcile
# (res/plugins/lumalinux-cuser.lua; the file says why). Same ref as the .so and
# the tools. SLSsteam runs every .lua in its plugins dir and wants them owner-only.
SLS_PLUGIN="lumalinux-cuser.lua"
mkdir -p "${SLS_CFG_DIR}/plugins"
PLUGIN_REF_URL="https://raw.githubusercontent.com/${REPO}/${TOOLS_REF}/res/plugins"
if curl -fsSL -o "${SLS_CFG_DIR}/plugins/${SLS_PLUGIN}.tmp" "${PLUGIN_REF_URL}/${SLS_PLUGIN}" \
   || { [[ "$TOOLS_REF" != "main" ]] && curl -fsSL -o "${SLS_CFG_DIR}/plugins/${SLS_PLUGIN}.tmp" "https://raw.githubusercontent.com/${REPO}/main/res/plugins/${SLS_PLUGIN}"; }; then
    chmod 0600 "${SLS_CFG_DIR}/plugins/${SLS_PLUGIN}.tmp"
    mv -f "${SLS_CFG_DIR}/plugins/${SLS_PLUGIN}.tmp" "${SLS_CFG_DIR}/plugins/${SLS_PLUGIN}"
    ok "Deployed SLSsteam plugin ${SLS_CFG_DIR}/plugins/${SLS_PLUGIN}"
else
    rm -f "${SLS_CFG_DIR}/plugins/${SLS_PLUGIN}.tmp"
    warn "Failed to download ${SLS_PLUGIN} (continuing; on the 64-bit client a game added while Steam runs then needs a Steam restart before it installs)."
fi

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
