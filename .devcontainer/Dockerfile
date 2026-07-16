# Arch base for Codespaces — dependencies to install & run Steam.
#
# Provides: Arch + multilib + display headless por noVNC + Mesa SW +
# bwrap setuid + build toolchain 32-bit. NO instala Steam: solo deja el
# sistema listo para instalarlo/ejecutarlo.

FROM archlinux:latest

# Multilib: necesario para Steam (32-bit) y para los `lib32-*`. Append
# literal del bloque en vez de sed sobre el comentado, mas robusto frente
# a cambios en la imagen base.
RUN printf '\n[multilib]\nInclude = /etc/pacman.d/mirrorlist\n' >> /etc/pacman.conf

# pacman -Syu antes del install: mezclar partial-update con install corrompe
# el sistema en Arch.
#
# Paquetes (dependencias para instalar/ejecutar Steam — Steam NO se instala):
#   - utilidades: base-devel git curl wget sudo nano vim less which
#   - build toolchain 32-bit: cmake ninja pkgconf + lib32-curl lib32-openssl
#   - display headless: xorg-server-xvfb x11vnc openbox
#       (noVNC y websockify se instalan en post-create — no estan en repos
#       oficiales de Arch)
#   - python + pip: necesarios para `pip install websockify`
#   - Mesa SW + Vulkan SW + ICD loader: render por software (no hay GPU)
#   - bubblewrap: setuid root en el siguiente RUN, ver justificacion abajo
RUN pacman -Syu --noconfirm \
    base-devel git curl wget sudo nano vim less which \
    cmake ninja pkgconf \
    xorg-server-xvfb x11vnc openbox \
    python python-pip \
    mesa lib32-mesa vulkan-swrast lib32-vulkan-swrast vulkan-mesa-layers \
    vulkan-icd-loader lib32-vulkan-icd-loader \
    lib32-curl lib32-openssl \
    bubblewrap

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

# Usuario `codespace` con sudo NOPASSWD. Nombre coincide con la convencion
# de Codespaces y con el `remoteUser` del devcontainer.json.
RUN useradd -m -s /bin/bash -G wheel codespace \
 && echo "codespace ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers \
 && echo "%wheel ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

USER codespace
WORKDIR /home/codespace
