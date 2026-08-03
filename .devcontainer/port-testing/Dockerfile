# Port-testing env for Codespaces — validate the LumaDeck + lumalinux
# CachyOS / multi-distro port WITHOUT a handheld.
#
# This is a DEDICATED twin of the root Arch env (.devcontainer/Dockerfile),
# selectable from the Codespaces config picker as its own environment so the
# original Arch daily-driver stays untouched. It intentionally uses the SAME
# `archlinux:latest` base (proven, builds reliably) instead of cachyos/cachyos —
# because what the port test validates is base-independent:
#   * the core (hooks/hash) is Valve's steamclient.so, identical on any distro;
#   * the env layer only needs /etc/os-release to say ID=cachyos, which the
#     validator overrides at runtime (temporarily, restored).
# So the CachyOS-optimized userland buys ~nothing here while adding SIGILL/base
# risk. (A real cachyos/cachyos base is only worth a faithful optimized-repo
# build, which this test doesn't exercise. The gamescope crash-loop of issue #31
# is NOT reproducible headless in any base.)
#
# Everything below mirrors the root Arch Dockerfile; the ONLY difference between
# the two environments is the post-create step (see devcontainer.json ->
# port-testing/post-create.sh), which puts LumaDeck on the port branch and
# installs ~/validate-port.sh.

FROM archlinux:latest

# Multilib: necesario para Steam (32-bit) y para los `lib32-*`.
RUN printf '\n[multilib]\nInclude = /etc/pacman.d/mirrorlist\n' >> /etc/pacman.conf

# pacman -Syu antes del install: mezclar partial-update con install corrompe
# el sistema en Arch. Mismos paquetes que el env Arch hermano:
#   - utilidades + build toolchain 32-bit (lumalinux) + nodejs/npm (LumaDeck)
#   - display headless (Xvfb + x11vnc + openbox; noVNC/websockify en post-create)
#   - Mesa SW + Vulkan SW + ICD loader (render por software, no hay GPU)
#   - bubblewrap (setuid en el siguiente RUN), networkmanager, steam
RUN pacman -Syu --noconfirm \
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

# bwrap setuid root: el kernel de Codespaces tiene unprivileged_userns_clone
# deshabilitado; setuid le da los caps sin depender del kernel.
RUN chmod u+s /usr/bin/bwrap

# Locale.
RUN echo "en_US.UTF-8 UTF-8" >> /etc/locale.gen && locale-gen
ENV LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8

# /etc/machine-id para que dbus levante el session bus (si no, el
# steam-runtime-launcher-service entra en crash-loop).
RUN systemd-machine-id-setup \
 && mkdir -p /var/lib/dbus \
 && ln -sf /etc/machine-id /var/lib/dbus/machine-id

# Usuario `codespace` con sudo NOPASSWD (coincide con remoteUser).
RUN useradd -m -s /bin/bash -G wheel codespace \
 && echo "codespace ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers \
 && echo "%wheel ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

USER codespace
WORKDIR /home/codespace
