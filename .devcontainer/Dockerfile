# Arch base for Codespaces — a SteamOS-like env to test lumalinux + LumaDeck.
#
# Provides: Arch + multilib + Steam + display headless por noVNC + Mesa SW +
# bwrap setuid + build toolchain 32-bit (lumalinux) + nodejs/npm (LumaDeck).
# Steam se instala pero NO se arranca ni configura aqui (eso es runtime, via
# los scripts que genera post-create.sh). Decky, SLSsteam/CloudRedirect
# (headcrab) y el deploy de lumalinux siguen siendo manuales: ver README.dev.md.

FROM archlinux:latest

# Multilib: necesario para Steam (32-bit) y para los `lib32-*`. Append
# literal del bloque en vez de sed sobre el comentado, mas robusto frente
# a cambios en la imagen base.
RUN printf '\n[multilib]\nInclude = /etc/pacman.d/mirrorlist\n' >> /etc/pacman.conf

# pacman -Syu antes del install: mezclar partial-update con install corrompe
# el sistema en Arch.
#
# Paquetes:
#   - utilidades: base-devel git curl wget sudo nano vim less which
#   - build toolchain 32-bit (lumalinux): cmake ninja pkgconf + lib32-curl lib32-openssl
#   - nodejs + npm: build del plugin LumaDeck (rollup). unzip: deploy por zip.
#   - display headless: xorg-server-xvfb x11vnc openbox
#       (noVNC y websockify se instalan en post-create — no estan en repos
#       oficiales de Arch)
#   - python + pip: necesarios para `pip install websockify`
#   - Mesa SW + Vulkan SW + ICD loader: render por software (no hay GPU)
#   - bubblewrap: setuid root en el siguiente RUN, ver justificacion abajo
#   - networkmanager: el cliente de Steam ACTUAL detecta la red vía NetworkManager
#       sobre el D-Bus del sistema. Sin NM el `SystemNetworkStore` de la UI de
#       login peta (`RegisterForDeviceChanges is not a function`) y Steam se queda
#       en "waiting for network connection" aunque IPv4 funcione. Se arranca en
#       runtime (start-net.sh, generado por post-create) porque es un daemon.
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

# bwrap setuid root. Arch moderno distribuye bubblewrap sin setuid porque
# asume el kernel tiene `kernel.unprivileged_userns_clone=1`. El kernel de
# Codespaces tiene ese sysctl deshabilitado y no podemos cambiarlo desde el
# contenedor, asi que bwrap petaria con:
#     bwrap: Failed to make / slave: Permission denied
# Setuid root le da los caps necesarios sin depender del kernel. Es el
# modelo que usa Microsoft's universal:2-linux (donde Steam funciona).
RUN chmod u+s /usr/bin/bwrap

# Locale. Arch base no la genera; algunos installers se quejan.
RUN echo "en_US.UTF-8 UTF-8" >> /etc/locale.gen && locale-gen
ENV LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8

# /etc/machine-id. Sin el, dbus no levanta el session bus y el
# steam-runtime-launcher-service entra en crash-loop (Steam lo desactiva y
# sigue, pero se pierden notificaciones / IPC). systemd-machine-id-setup
# escribe un id aleatorio (no necesita systemd corriendo). Steam mira tanto
# /etc/machine-id como /var/lib/dbus/machine-id, asi que enlazamos el segundo.
RUN systemd-machine-id-setup \
 && mkdir -p /var/lib/dbus \
 && ln -sf /etc/machine-id /var/lib/dbus/machine-id

# Usuario `codespace` con sudo NOPASSWD. Nombre coincide con la convencion
# de Codespaces y con el `remoteUser` del devcontainer.json.
RUN useradd -m -s /bin/bash -G wheel codespace \
 && echo "codespace ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers \
 && echo "%wheel ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

USER codespace
WORKDIR /home/codespace
