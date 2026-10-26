# Steam smoke image for CI (.github/workflows/steam-smoke.yml): the SteamOS
# devcontainer's base without Plasma, so a GitHub runner builds it in minutes.
# Arch rolling (glibc 2.44 today; SteamOS 3.7/3.8 carry 2.41 — see the
# workflow's `base` input for the Debian variant), the `steam` package, the
# 32-bit userland Steam needs, Xvfb for a headless display, bwrap setuid for
# the 64-bit client's pressure-vessel on a kernel without unprivileged userns,
# a SteamOS identity in os-release and the `deck` user, as in the devcontainer.
FROM archlinux:latest
RUN printf '\n[multilib]\nInclude = /etc/pacman.d/mirrorlist\n' >> /etc/pacman.conf
RUN pacman -Syu --noconfirm \
    base-devel git curl wget sudo which procps-ng \
    python python-pip jq p7zip unzip \
    xorg-server-xvfb xorg-xdpyinfo \
    mesa lib32-mesa vulkan-swrast lib32-vulkan-swrast vulkan-mesa-layers \
    vulkan-icd-loader lib32-vulkan-icd-loader \
    lib32-curl lib32-openssl \
    networkmanager dbus \
    bubblewrap steam \
    ttf-dejavu \
 && pacman -Scc --noconfirm
RUN chmod u+s /usr/bin/bwrap
RUN echo "en_US.UTF-8 UTF-8" >> /etc/locale.gen && locale-gen
ENV LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8
RUN systemd-machine-id-setup \
 && mkdir -p /var/lib/dbus \
 && ln -sf /etc/machine-id /var/lib/dbus/machine-id
RUN printf '%s\n' \
    'NAME="SteamOS"' 'PRETTY_NAME="SteamOS"' 'VERSION_CODENAME=holo' \
    'ID=steamos' 'ID_LIKE=arch' 'VARIANT_ID=steamdeck' \
    'VERSION_ID=3.7.8' 'BUILD_ID=20250522.1000' \
    > /usr/lib/os-release
RUN useradd -m -u 1000 -s /bin/bash -G wheel deck \
 && echo "deck ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers
USER deck
WORKDIR /home/deck
