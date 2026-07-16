#!/usr/bin/env bash
set -e

# Mesa software stack + ~/.local/bin en el PATH para los binarios de
# `pip install --user` (websockify).
cat >> "$HOME/.bashrc" <<'BASHRC'

# Steam deps env — Mesa software stack + pip user bin path
export LIBGL_ALWAYS_SOFTWARE=1
export __GLX_VENDOR_LIBRARY_NAME=mesa
export VK_LOADER_DRIVERS_DISABLE=nvidia*
export PATH="$HOME/.local/bin:$PATH"
BASHRC

export PATH="$HOME/.local/bin:$PATH"

# noVNC y websockify no estan en los repos oficiales de Arch (movidos a
# AUR). Los traemos por canales independientes de la distro:
#   - websockify: pip install --user (binario en ~/.local/bin)
#   - noVNC: clone --depth 1 de su repo oficial a ~/.local/share/novnc
NOVNC_TAG="v1.5.0"

echo "Instalando websockify via pip..."
pip install --user --break-system-packages websockify 2>&1 | tail -3 \
  || pip install --user websockify 2>&1 | tail -3

echo "Clonando noVNC $NOVNC_TAG..."
rm -rf "$HOME/.local/share/novnc"
git clone --depth 1 --branch "$NOVNC_TAG" \
    https://github.com/novnc/noVNC.git "$HOME/.local/share/novnc"

NOVNC_WEB="$HOME/.local/share/novnc"
if [ ! -f "$NOVNC_WEB/vnc.html" ]; then
    echo "ERROR: clone de noVNC no produjo vnc.html en $NOVNC_WEB"
    exit 1
fi

# Script para arrancar el display headless. Lo genera el post-create para
# que el path absoluto de noVNC quede embebido y no haya que detectarlo
# cada vez. Lo dejamos en ~/start-display.sh.
#
# Ojo: openbox se lanza DESPUÉS de esperar a que Xvfb responda en :1, no
# tras un sleep ciego — el sleep 1 anterior a veces no llegaba y openbox
# moría con "Failed to open the display", llevándose el `set -e` por
# delante y dejando el script a medias (sin x11vnc ni websockify).
cat > "$HOME/start-display.sh" <<EOF
#!/usr/bin/env bash
# Xvfb :1 + openbox + x11vnc + websockify -> noVNC en puerto 6080.
set -e

export LIBGL_ALWAYS_SOFTWARE=1
export __GLX_VENDOR_LIBRARY_NAME=mesa
export VK_LOADER_DRIVERS_DISABLE=nvidia*
export PATH="\$HOME/.local/bin:\$PATH"

sudo mkdir -p /tmp/.X11-unix
sudo chmod 1777 /tmp/.X11-unix

mkdir -p "/tmp/runtime-\$(id -u)"
chmod 700 "/tmp/runtime-\$(id -u)"

pkill -f "Xvfb :1"            2>/dev/null || true
pkill -f "x11vnc -display :1" 2>/dev/null || true
pkill -f "websockify .* 6080" 2>/dev/null || true

Xvfb :1 -screen 0 1280x800x24 -nolisten tcp &
for i in 1 2 3 4 5 6 7 8 9 10; do
    DISPLAY=:1 xdpyinfo >/dev/null 2>&1 && break
    sleep 0.5
done
DISPLAY=:1 openbox &
sleep 0.5
x11vnc -display :1 -nopw -listen 127.0.0.1 -xkb -forever -shared -bg -o /tmp/x11vnc.log
websockify --web=$NOVNC_WEB -D 6080 127.0.0.1:5900

echo
echo "Display listo. noVNC web root = $NOVNC_WEB"
echo "Abre el puerto 6080 forwardeado y entra a /vnc.html"
EOF
chmod +x "$HOME/start-display.sh"

# =============================================================================
# README
# =============================================================================
cat > "$HOME/README.dev.md" <<'EOF'
# Arch dev codespace

Container base = Arch Linux con todas las dependencias para instalar y
ejecutar Steam, pero SIN Steam instalado. SteamOS también es Arch, así que
paths y nombres de paquete coinciden con la Deck.

## Qué provee el container (pre-instalado en cada rebuild)

  - Arch + multilib + locales en_US.UTF-8
  - Display headless: Xvfb + openbox + x11vnc + noVNC (puerto 6080)
  - Mesa SW + Vulkan SW + ICD loader (no hay GPU)
  - bwrap setuid root (necesario para Steam en Codespaces)
  - Build toolchain 32-bit: cmake, ninja, pkgconf, lib32-curl, lib32-openssl

NO instala Steam, lumalinux ni SLSsteam — solo deja el sistema listo.

## Arrancar el display

```
~/start-display.sh
```

Abre el puerto 6080 forwardeado por Codespaces y entra a `/vnc.html`.

## Instalar Steam (manual)

```
sudo pacman -S --noconfirm steam
DISPLAY=:1 steam -no-cef-sandbox &
```
EOF

echo
echo "post-create OK. Arranca el display con ~/start-display.sh"
