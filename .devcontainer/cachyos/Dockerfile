# CachyOS base for Codespaces — reproduce el userspace de un CachyOS Handheld
# para testear lumalinux + SLSsteam/CloudRedirect bajo la MISMA glibc/toolchain
# (repos optimizados) que la distro donde apareció el black-screen (issue del
# reporter en CachyOS Handheld).
#
# Es el gemelo del `.devcontainer/Dockerfile` (Arch), con la unica diferencia
# de la imagen base: `cachyos/cachyos`. El resto (multilib, Steam 32-bit,
# display headless por noVNC, Mesa SW, bwrap setuid, machine-id, usuario
# `codespace`) es identico — CachyOS es Arch con repos optimizados, mismo
# pacman. Comparte el mismo `.devcontainer/post-create.sh`.
#
# NOTA v3/v4: la imagen cachyos/cachyos trae los mirrorlists v3/v4. Si el host
# de Codespaces NO soporta AVX-512, los paquetes v4 dan SIGILL — por eso NO
# forzamos el repo v4. v3 (x86-64-v3) es seguro en las CPUs modernas de
# Codespaces, y es lo que corre un Handheld CachyOS tipico.

FROM cachyos/cachyos:latest

# Keyring: en un primer pacman -Syu sobre la imagen base, una keyring sin
# inicializar es el fallo #1. Idempotente, barato, evita sorpresas.
RUN pacman-key --init 2>/dev/null || true; \
    pacman-key --populate archlinux cachyos 2>/dev/null || true

# Multilib: necesario para Steam (32-bit) y los lib32-*. Guardado para no
# duplicar el bloque si la imagen base ya lo trae habilitado.
RUN grep -q '^\[multilib\]' /etc/pacman.conf \
    || printf '\n[multilib]\nInclude = /etc/pacman.d/mirrorlist\n' >> /etc/pacman.conf

# pacman -Syu antes del install: mezclar partial-update con install corrompe
# el sistema en Arch/CachyOS. Mismos paquetes que el env Arch hermano:
#   - build toolchain 32-bit (lumalinux): cmake ninja pkgconf + lib32-curl/openssl
#   - nodejs + npm: build del plugin LumaDeck. unzip: deploy por zip.
#   - display headless: xorg-server-xvfb x11vnc openbox (noVNC en post-create)
#   - Mesa SW + Vulkan SW + ICD loader: render por software (no hay GPU)
#   - bubblewrap: setuid root en el siguiente RUN (kernel de Codespaces sin
#       unprivileged userns)
#   - networkmanager: el cliente de Steam detecta la red via NM sobre D-Bus
#   - steam: el cliente (multilib). Se instala aqui; se arranca en runtime.
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

# bwrap setuid root. El kernel de Codespaces tiene unprivileged_userns_clone
# deshabilitado y no lo podemos cambiar desde el contenedor; setuid le da los
# caps sin depender del kernel. Igual que en el env Arch.
RUN chmod u+s /usr/bin/bwrap

# Locale.
RUN echo "en_US.UTF-8 UTF-8" >> /etc/locale.gen && locale-gen
ENV LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8

# /etc/machine-id para que dbus levante el session bus (si no,
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
