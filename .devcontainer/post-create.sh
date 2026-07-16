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
sleep 0.3

# Xvfb refuses to bind :1 if a stale lock/socket from a previous run is left
# behind; then xdpyinfo connects to a half-dead server and openbox/x11vnc die
# with "Failed to open the display". Clear them before starting.
sudo rm -f /tmp/.X1-lock "/tmp/.X11-unix/X1" 2>/dev/null || true

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
# Clone LumaDeck (the plugin repo) so it's ready to build/deploy.
# =============================================================================
echo "Clonando LumaDeck..."
if [ -d "$HOME/LumaDeck/.git" ]; then
    git -C "$HOME/LumaDeck" pull --ff-only 2>&1 | tail -1 || true
else
    git clone https://github.com/jayool/LumaDeck.git "$HOME/LumaDeck" 2>&1 | tail -2 || \
        echo "  (clone falló — clónalo a mano si lo necesitas)"
fi

# =============================================================================
# Helper: build lumalinux's liblumalinux.so (32-bit) from the checked-out repo.
# =============================================================================
cat > "$HOME/build-lumalinux.sh" <<'BLL'
#!/usr/bin/env bash
# Build liblumalinux.so from source. Usage: build-lumalinux.sh [repo_dir]
set -e
REPO="${1:-}"
if [ -z "$REPO" ]; then
    for d in /workspaces/lumalinux "$HOME/lumalinux" "$PWD"; do
        if [ -f "$d/CMakeLists.txt" ] && grep -q "project(lumalinux" "$d/CMakeLists.txt" 2>/dev/null; then
            REPO="$d"; break
        fi
    done
fi
[ -n "$REPO" ] || { echo "No encuentro el repo lumalinux. Pásalo: build-lumalinux.sh /ruta"; exit 1; }
cd "$REPO"
echo "== building lumalinux in $REPO =="
[ -x tools/fetch_libmem.sh ] && ./tools/fetch_libmem.sh   # libmem headers (pinned)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
echo "== done. .so: =="
find build -maxdepth 2 -name '*.so' -print
echo
echo "Para DESPLEGARLO hace falta SLSsteam (headcrab) + Steam arrancado una vez,"
echo "y luego ./install.sh (ver ~/README.dev.md). El .so recién compilado va a"
echo "~/.local/share/lumalinux/liblumalinux.so (cópialo ahí si install.sh bajó el de release)."
BLL
chmod +x "$HOME/build-lumalinux.sh"

# =============================================================================
# Helper: build LumaDeck and deploy it into Decky's plugins dir.
# =============================================================================
cat > "$HOME/deploy-lumadeck.sh" <<'DLD'
#!/usr/bin/env bash
# Build LumaDeck and copy it into ~/homebrew/plugins/LumaDeck.
# Usage: deploy-lumadeck.sh [src_dir]   (default ~/LumaDeck)
set -e
SRC="${1:-$HOME/LumaDeck}"
[ -d "$SRC" ] || { echo "No está el repo en $SRC. git clone https://github.com/jayool/LumaDeck.git $SRC"; exit 1; }
cd "$SRC"
git pull --ff-only 2>/dev/null || true
echo "== npm install + build =="
npm install
npm run build
DEST="$HOME/homebrew/plugins/LumaDeck"
echo "== deploy -> $DEST =="
sudo rm -rf "$DEST"
sudo mkdir -p "$DEST"
# A deployed Decky plugin = its runtime files (NOT src/, node_modules/, .git/).
for p in plugin.json main.py package.json dist backend defaults assets LICENSE; do
    [ -e "$p" ] && sudo cp -r "$p" "$DEST"/
done
sudo chown -R "$(id -u):$(id -g)" "$DEST" 2>/dev/null || true
echo "== hecho. Reinicia el plugin_loader de Decky para que lo recoja (ver ~/README.dev.md). =="
DLD
chmod +x "$HOME/deploy-lumadeck.sh"

# =============================================================================
# README
# =============================================================================
cat > "$HOME/README.dev.md" <<'EOF'
# Arch dev codespace — test lumalinux + LumaDeck like SteamOS

Base = Arch Linux (SteamOS también es Arch, así que paths y paquetes coinciden
con la Deck). Sirve para probar lumalinux y LumaDeck sobre un Steam real cuando
no tienes la Deck a mano.

## Pre-instalado por el container

  - Arch + multilib + locales, render por software (Mesa/Vulkan SW, no hay GPU)
  - **Steam** (cliente), bwrap setuid (necesario en Codespaces)
  - Display headless: Xvfb + openbox + x11vnc + noVNC (puerto 6080)
  - Toolchain 32-bit (lumalinux): cmake, ninja, pkgconf, lib32-curl/openssl
  - **nodejs + npm** (build de LumaDeck), unzip
  - Repo LumaDeck clonado en ~/LumaDeck
  - Scripts: ~/start-display.sh, ~/build-lumalinux.sh, ~/deploy-lumadeck.sh

## Orden para montar el entorno

1. **Display**: `~/start-display.sh`, luego abre el puerto **6080** forwardeado
   y entra a `/vnc.html`.
2. **Steam**: `DISPLAY=:1 steam -no-cef-sandbox &`. La primera vez baja su
   runtime y pide login. (Ya está instalado; no hace falta `pacman -S steam`.)
3. **Decky Loader** (manual): instálalo. ⚠ En Codespaces **systemd no es PID 1**,
   así que el instalador oficial (`systemctl --user enable plugin_loader`) puede
   fallar; toca la ruta manual (bajar el binario a ~/homebrew/services y
   arrancar el `plugin_loader` a mano). Este es el paso más frágil.
4. **SLSsteam + CloudRedirect** (headcrab): `curl -fsSL https://headcrab.pages.dev | bash`.
   ⚠ Hazlo con Steam **cerrado / fuera de Game Mode** (Game Mode reinicia Steam a
   media instalación y puede dar OOBE). Arranca Steam una vez después para que
   headcrab bootstrapee `steam.sh` (crea el `INJECT_SLS`).
5. **lumalinux**: `~/build-lumalinux.sh` compila el `.so`. Para desplegarlo,
   desde el repo: `./install.sh` (exige SLSsteam + `steam.sh` del paso 4).
   install.sh baja el `.so` del último release; si quieres el que acabas de
   compilar, cópialo a `~/.local/share/lumalinux/liblumalinux.so`.
6. **LumaDeck**: `~/deploy-lumadeck.sh` (build + copia a ~/homebrew/plugins),
   luego **reinicia el plugin_loader** para que lo recoja.
7. **Reinicia Steam** para cargarlo todo.

## Notas honestas

Los pasos 3 (Decky headless) y 4 (headcrab quiere desktop, Game Mode da OOBE)
son los que dan guerra en Codespaces. Los scripts automatizan lo fiable (deps,
build, deploy de ficheros); esos dos pasos van a mano y pueden requerir toqueteo.
EOF

echo
echo "post-create OK."
echo "  1) ~/start-display.sh  -> abre el puerto 6080 (/vnc.html)"
echo "  2) DISPLAY=:1 steam -no-cef-sandbox &"
echo "  Flujo completo en ~/README.dev.md"
