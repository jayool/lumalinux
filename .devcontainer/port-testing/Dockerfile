# CachyOS userland env for Codespaces — test the LumaDeck + lumalinux port on
# the REAL distro, not an Arch stand-in.
#
# This is the port-testing twin of the root Arch env (.devcontainer/Dockerfile),
# selectable from the Codespaces config picker as its own environment so the
# original Arch daily-driver stays untouched. It boots the SAME way — Steam +
# headless display (Xvfb/x11vnc/noVNC) + Mesa SW + 32-bit toolchain + Decky, all
# wired by the shared ../post-create.sh — but on a genuine CachyOS userland:
#   * CachyOS optimized repositories (cachyos-repo.sh, CPU-auto-detected v3/v4),
#   * ID=cachyos in /etc/os-release (platform_info detects cachyos for REAL, no
#     runtime os-release faking),
#   * the REAL CachyOS session scripts (gamescope-session-cachyos ->
#     steamos-session-select + steam-short-session-tracker) so the issue #31
#     session logic can be exercised headless.
#
# What it still CANNOT do (no GPU / no seat in a container): run the actual
# gamescope Game Mode compositor session. But the desktop-mode client downgrade
# + steam.sh behaviour — #31's PRIMARY symptom — IS reproducible here on real
# CachyOS userland, which an Arch base could not give us.

FROM archlinux:latest

# ---------------------------------------------------------------------------
# 1) Bootstrap Arch's keyring + the tools cachyos-repo.sh needs, and enable
#    multilib (Steam is 32-bit). pacman-key --init/--populate is REQUIRED before
#    any signed install and before cachyos-repo.sh can lsign the CachyOS key.
# ---------------------------------------------------------------------------
RUN pacman-key --init \
 && pacman-key --populate archlinux \
 && printf '\n[multilib]\nInclude = /etc/pacman.d/mirrorlist\n' >> /etc/pacman.conf \
 && pacman -Sy --noconfirm --needed archlinux-keyring curl tar gawk

# ---------------------------------------------------------------------------
# 2) Add the CachyOS optimized repositories. `cachyos-repo.sh --install` is
#    NON-interactive (verified against CachyOS/cachyos-repo-add-script: no
#    read/tty prompts, requires root, --install is the default action): it
#    detects the ISA level via /lib/ld-linux-x86-64.so.2, recv+lsigns the CachyOS
#    key from keyserver.ubuntu.com, `pacman -U`s the keyring/mirrorlists/patched
#    pacman, and prepends the cachyos* repos above Arch's in /etc/pacman.conf.
#    Needs open internet incl. the keyserver — Codespaces has it. If it fails,
#    it's almost always a transient keyserver/mirror hiccup: rebuild, or try a
#    mirror (https://mirror.cachyos.org is canonical; see the wiki for alternates).
# ---------------------------------------------------------------------------
RUN curl -fsSL -O https://mirror.cachyos.org/cachyos-repo.tar.xz \
 && tar xf cachyos-repo.tar.xz \
 && ( cd cachyos-repo && ./cachyos-repo.sh --install ) \
 && rm -rf cachyos-repo cachyos-repo.tar.xz

# ---------------------------------------------------------------------------
# 3) The Steam + headless + toolchain stack (same set the Arch env installs,
#    now pulled from the CachyOS-optimized repos where available). -Syu so the
#    newly-added repos are synced and the system stays coherent (partial upgrades
#    corrupt Arch/CachyOS).
#      - utilities + 32-bit build toolchain (lumalinux) + nodejs/npm (LumaDeck)
#      - headless display (Xvfb + x11vnc + openbox; noVNC/websockify in post-create)
#      - Mesa SW + Vulkan SW + ICD loader (software render, no GPU)
#      - bubblewrap (setuid below), networkmanager, steam
# ---------------------------------------------------------------------------
RUN pacman -Syu --noconfirm --needed \
    base-devel git curl wget sudo nano vim less which \
    cmake ninja pkgconf \
    nodejs npm unzip \
    xorg-server-xvfb x11vnc openbox \
    python python-pip \
    mesa lib32-mesa vulkan-swrast lib32-vulkan-swrast vulkan-mesa-layers \
    vulkan-icd-loader lib32-vulkan-icd-loader \
    lib32-curl lib32-openssl \
    networkmanager \
    bubblewrap steam

# ---------------------------------------------------------------------------
# 4) The REAL CachyOS session scripts (steamos-session-select,
#    steam-short-session-tracker) via gamescope-session-cachyos. BEST-EFFORT: if
#    CachyOS packaging pulls a handheld/firmware dep that won't install in a
#    container, we DON'T deny the whole (working) Steam+headless env — the WARN
#    just means the session-logic scripts aren't present; everything else works,
#    and the package can be installed by hand later.
# ---------------------------------------------------------------------------
RUN pacman -S --noconfirm --needed gamescope-session-cachyos \
 || echo "WARN: gamescope-session-cachyos not installed (container-incompatible dep?). Steam+headless env is fine; the #31 session-script tests will need this package — install it by hand or adjust the dep set."

# ---------------------------------------------------------------------------
# 5) Make the identity genuinely CachyOS so platform_info resolves ID=cachyos
#    with no runtime faking. Written LAST so no package install clobbers it.
# ---------------------------------------------------------------------------
RUN printf '%s\n' \
    'NAME="CachyOS Linux"' \
    'PRETTY_NAME="CachyOS"' \
    'ID=cachyos' \
    'ID_LIKE=arch' \
    'BUILD_ID=rolling' \
    'ANSI_COLOR="38;2;23;147;209"' \
    'HOME_URL="https://cachyos.org/"' \
    'DOCUMENTATION_URL="https://wiki.cachyos.org/"' \
    'LOGO=cachyos' \
    > /etc/os-release

# ---------------------------------------------------------------------------
# 6) Same runtime prerequisites as the Arch env.
#    bwrap setuid: the Codespaces kernel disables unprivileged_userns_clone, so
#    setuid gives bwrap the caps without depending on the kernel.
# ---------------------------------------------------------------------------
RUN chmod u+s /usr/bin/bwrap

# Locale.
RUN echo "en_US.UTF-8 UTF-8" >> /etc/locale.gen && locale-gen
ENV LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8

# /etc/machine-id so dbus brings up the session bus (else the
# steam-runtime-launcher-service crash-loops).
RUN systemd-machine-id-setup \
 && mkdir -p /var/lib/dbus \
 && ln -sf /etc/machine-id /var/lib/dbus/machine-id

# `codespace` user with sudo NOPASSWD (matches remoteUser).
RUN useradd -m -s /bin/bash -G wheel codespace \
 && echo "codespace ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers \
 && echo "%wheel ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

USER codespace
WORKDIR /home/codespace
