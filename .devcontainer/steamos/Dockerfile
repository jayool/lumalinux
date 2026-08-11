# SteamOS-like env para Codespaces — testeo de lumalinux + LumaDeck incluyendo
# el desktop hand-off de LumaDeck (Game Mode <-> Plasma).
#
# Diferencias con ../Dockerfile (el env Arch "de escritorio"):
#   - Identidad SteamOS: os-release con ID=steamos + VARIANT_ID=steamdeck.
#     platform_info.py de LumaDeck (y headcrab) leen /etc/os-release para
#     decidir ramas de código; con esto ejercitan las ramas SteamOS reales.
#   - Usuario `deck` (uid 1000) + /home/deck: paths.py de LumaDeck resuelve
#     todo via real_home()/real_user(), asi que con este usuario reproduce
#     EXACTAMENTE las listas de rutas de la consola.
#   - Plasma + konsole: Desktop Mode real para el hand-off. El autostart XDG
#     lo ejecuta Plasma de verdad (misma semantica que la Deck: bit ejecutable
#     del .desktop, no X-KDE-AutostartScript, konsole --hold...).
#   - Steam se arranca en gamepadui (el QAM real donde vive Decky/LumaDeck).
#
# El cambio de sesion (steamos-session-select) y steamos-readonly se emulan
# con scripts que instala post-create.sh — LumaDeck los trata como caja negra
# (solo exec + contrato de args), asi que la emulacion cubre el round-trip.

FROM archlinux:latest

# Multilib: necesario para Steam (32-bit) y para los `lib32-*`.
RUN printf '\n[multilib]\nInclude = /etc/pacman.d/mirrorlist\n' >> /etc/pacman.conf

# pacman -Syu antes del install: mezclar partial-update con install corrompe
# el sistema en Arch.
#
# Mismo set que el env base, mas:
#   - plasma-desktop/plasma-workspace + konsole: Desktop Mode del hand-off
#   - ttf-dejavu + noto-fonts: sin fuentes de sistema Plasma/konsole pintan
#     cuadrados (el CEF de Steam trae las suyas, por eso el env base no las
#     necesitaba)
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
    bubblewrap steam \
    plasma-desktop plasma-workspace konsole \
    ttf-dejavu noto-fonts

# Plasma 6 en Arch: en versiones recientes la sesion X11 se movio al paquete
# `plasma-x11-session` (startplasma-x11 ya no viene en plasma-workspace).
# Instalarlo si existe; si el repo aun no lo tiene, plasma-workspace ya trae
# startplasma-x11 y este RUN es un no-op.
RUN pacman -S --noconfirm plasma-x11-session || true

# bwrap setuid root. Arch moderno distribuye bubblewrap sin setuid porque
# asume `kernel.unprivileged_userns_clone=1`; el kernel de Codespaces lo tiene
# deshabilitado y bwrap petaria con "Failed to make / slave". Ver ../Dockerfile.
RUN chmod u+s /usr/bin/bwrap

# Locale. Arch base no la genera; algunos installers se quejan.
RUN echo "en_US.UTF-8 UTF-8" >> /etc/locale.gen && locale-gen
ENV LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8

# /etc/machine-id. Sin el, dbus no levanta el session bus y el
# steam-runtime-launcher-service entra en crash-loop. Ver ../Dockerfile.
RUN systemd-machine-id-setup \
 && mkdir -p /var/lib/dbus \
 && ln -sf /etc/machine-id /var/lib/dbus/machine-id

# Identidad SteamOS. En Arch /etc/os-release es symlink a /usr/lib/os-release;
# escribimos el fichero real para que ambos caminos (platform_info los mira en
# ese orden) cuenten lo mismo. Valores calcados de una Deck con SteamOS 3.7.
# OJO: una actualizacion del paquete `filesystem` pisaria este fichero — es un
# env de test, no actualices `filesystem` (o reescribelo despues).
RUN printf '%s\n' \
    'NAME="SteamOS"' \
    'PRETTY_NAME="SteamOS"' \
    'VERSION_CODENAME=holo' \
    'ID=steamos' \
    'ID_LIKE=arch' \
    'ANSI_COLOR="1;35"' \
    'HOME_URL="https://www.steampowered.com/"' \
    'DOCUMENTATION_URL="https://support.steampowered.com/"' \
    'SUPPORT_URL="https://support.steampowered.com/"' \
    'BUG_REPORT_URL="https://support.steampowered.com/"' \
    'LOGO=steamos' \
    'VARIANT_ID=steamdeck' \
    'VERSION_ID=3.7.8' \
    'BUILD_ID=20250522.1000' \
    > /usr/lib/os-release

# Usuario `deck` uid 1000, como en la consola (paths.py cae a /home/deck y a
# uid 1000 cuando no puede resolver; con esto ambos caminos coinciden).
# sudo NOPASSWD como el `deck` de SteamOS en modo developer.
RUN useradd -m -u 1000 -s /bin/bash -G wheel deck \
 && echo "deck ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers \
 && echo "%wheel ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

USER deck
WORKDIR /home/deck
