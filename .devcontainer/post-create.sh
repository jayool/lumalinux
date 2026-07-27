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

# Steam's current client needs a system D-Bus + NetworkManager to believe there
# is a network (this headless container has neither) or it hangs at "waiting for
# network". start-net.sh brings that up (+ disables the broken IPv6). Idempotent,
# so calling it here on every display start is safe and keeps the 4-command flow.
[ -x "\$HOME/start-net.sh" ] && "\$HOME/start-net.sh" || true

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
# Network bring-up for Steam's current client (start-net.sh).
# The client detects the network via NetworkManager over the system D-Bus. This
# headless container has neither, so the login UI's SystemNetworkStore JS-errors
# (`SteamClient.System.Network.RegisterForDeviceChanges is not a function`) and
# Steam sits at "waiting for network connection" forever — even though IPv4 works
# (connectivity test OK, CM ports 27018/27020 reachable). Fix = provide:
#   - a system D-Bus (socket absent by default),
#   - NetworkManager (reports the interface as connected),
#   - IPv6 OFF: Codespaces blackholes IPv6, so Steam's IPv6 probes/CM attempts
#     time out; /proc/sys is ro but the container's SYS_ADMIN lets us remount rw.
# Idempotent + non-fatal, so start-display.sh calls it before every Steam launch.
# =============================================================================
cat > "$HOME/start-net.sh" <<'NET'
#!/usr/bin/env bash
set +e

# system D-Bus
if [ ! -S /run/dbus/system_bus_socket ]; then
    sudo mkdir -p /run/dbus
    sudo dbus-daemon --system --fork
fi

# NetworkManager (backgrounded so we never block even if it runs foreground)
if ! pgrep -x NetworkManager >/dev/null 2>&1; then
    sudo NetworkManager >/dev/null 2>&1 &
    sleep 3
fi

# Disable the container's broken IPv6 (needs /proc/sys rw; SYS_ADMIN allows the remount)
sudo mount -o remount,rw /proc/sys 2>/dev/null
sudo sysctl -w net.ipv6.conf.all.disable_ipv6=1 net.ipv6.conf.default.disable_ipv6=1 >/dev/null 2>&1

echo "start-net: dbus=$([ -S /run/dbus/system_bus_socket ] && echo up || echo MISSING) NM=$(nmcli -t -f STATE general 2>/dev/null || echo '?') ipv6_disabled=$(cat /proc/sys/net/ipv6/conf/all/disable_ipv6 2>/dev/null)"
exit 0
NET
chmod +x "$HOME/start-net.sh"

# Bring it up now so a freshly-created codespace is ready before the first launch.
"$HOME/start-net.sh" || true

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
# Decky Loader: create ~/homebrew and download the PluginLoader binary.
# El binario se baja aqui (build-time); ARRANCARLO es runtime (~/start-decky.sh)
# y va DESPUES de Steam. Siempre como el usuario codespace, NUNCA root: como
# root Decky rompe el bind del socket (EACCES) y chownea ~/homebrew a root.
# =============================================================================
echo "Preparando Decky Loader..."
mkdir -p "$HOME/homebrew/plugins" "$HOME/homebrew/services" \
         "$HOME/homebrew/data" "$HOME/homebrew/logs" "$HOME/homebrew/themes"
curl -fsSL -o "$HOME/homebrew/services/PluginLoader" \
    https://github.com/SteamDeckHomebrew/decky-loader/releases/latest/download/PluginLoader \
    && chmod +x "$HOME/homebrew/services/PluginLoader" \
    || echo "  (descarga de PluginLoader falló — bájalo a mano si lo necesitas)"

# Decky se inyecta por el puerto de debug CEF de Steam (localhost:8080), que
# viene APAGADO por defecto. Este fichero marcador lo enciende. Creándolo AHORA
# (antes del 1er arranque de Steam) el puerto abre desde el primer launch, sin
# tener que reiniciar Steam luego.
mkdir -p "$HOME/.local/share/Steam"
touch "$HOME/.local/share/Steam/.cef-enable-remote-debugging"

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
BLL
chmod +x "$HOME/build-lumalinux.sh"

# =============================================================================
# Helper: build LumaDeck and deploy it into Decky's plugins dir.
# ~/homebrew es del usuario codespace (Decky corre como codespace), asi que
# NO hace falta sudo.
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
rm -rf "$DEST"
mkdir -p "$DEST"
# A deployed Decky plugin = its runtime files (NOT src/, node_modules/, .git/).
for p in plugin.json main.py package.json dist backend defaults assets LICENSE; do
    [ -e "$p" ] && cp -r "$p" "$DEST"/
done
echo "== hecho. Si Decky ya corre, reinícialo (~/start-decky.sh) para que lo recoja. =="
DLD
chmod +x "$HOME/deploy-lumadeck.sh"

# =============================================================================
# Helper: start Decky's PluginLoader (headless). Steam debe estar corriendo.
# =============================================================================
cat > "$HOME/start-decky.sh" <<'SDK'
#!/usr/bin/env bash
# Arranca Decky PluginLoader como el usuario actual (NO root: root rompe el
# socket y chownea ~/homebrew). Steam debe estar corriendo con el debug CEF
# abierto (puerto 8080).
set -e
LOADER="$HOME/homebrew/services/PluginLoader"
[ -x "$LOADER" ] || { echo "No está $LOADER. Bájalo (post-create lo hace) o a mano."; exit 1; }

# Decky se inyecta por el debug CEF de Steam (localhost:8080). Asegura el marcador.
mkdir -p "$HOME/.local/share/Steam"
touch "$HOME/.local/share/Steam/.cef-enable-remote-debugging"

# ¿Responde el 8080? Si no, Steam arrancó sin el marcador -> hay que reiniciarlo.
if ! curl -s http://localhost:8080/json/version >/dev/null 2>&1; then
    echo "⚠ El debug CEF (8080) no responde: Steam arrancó sin el marcador."
    echo "  El marcador ya está puesto; reinicia Steam y vuelve a lanzar esto:"
    echo "    ~/.local/share/Steam/ubuntu12_32/steam -shutdown; sleep 5; DISPLAY=:1 steam -no-cef-sandbox &"
    exit 1
fi

pkill -f 'homebrew/services/PluginLoader' 2>/dev/null || true
sleep 0.3
# Si un arranque previo como root dejó ~/homebrew de root, recupéralo:
sudo chown -R "$(id -un):$(id -gn)" "$HOME/homebrew" 2>/dev/null || true
env UNPRIVILEGED_PATH="$HOME/homebrew" PRIVILEGED_PATH="$HOME/homebrew" \
    LOG_LEVEL=INFO HOME="$HOME" \
    "$LOADER" > /tmp/decky.log 2>&1 &
echo "Decky PluginLoader arrancado (PID $!). Log: /tmp/decky.log"
echo "Abre el QAM en Steam; debería salir el icono de Decky con LumaDeck."
SDK
chmod +x "$HOME/start-decky.sh"

# =============================================================================
# Helper: finish-setup — la cadena funcional que necesita Steam corriendo.
# Guiado (para en los pasos frágiles), NO caja negra.
# =============================================================================
cat > "$HOME/finish-setup.sh" <<'FIN'
#!/usr/bin/env bash
# Monta SLSsteam + lumalinux + LumaDeck + Decky. Ejecutar DESPUÉS de arrancar
# Steam y loguearte. Cada paso frágil se para para que puedas verlo.
set -e
LUMALINUX="${LUMALINUX:-/workspaces/lumalinux}"
[ -d "$LUMALINUX" ] || LUMALINUX="$HOME/lumalinux"
STEAM="$HOME/.local/share/Steam/ubuntu12_32/steam"

restart_steam() {
    "$STEAM" -shutdown 2>/dev/null || true
    for i in $(seq 1 20); do pgrep -x steam >/dev/null 2>&1 || break; sleep 1; done
    ( DISPLAY=:1 steam -no-cef-sandbox >/tmp/steam.log 2>&1 & )
    echo "  Steam relanzándose... dale ~20s."
}

echo "== 1) headcrab: SLSsteam + CloudRedirect =="
echo "   (Steam debe estar corriendo. Aquí no hay Game Mode, va en escritorio.)"
curl -fsSL https://headcrab.pages.dev | bash

echo; echo ">> Reinicio Steam para cargar SLSsteam..."; restart_steam
read -rp ">> Cuando Steam haya vuelto y esté logueado, Enter para seguir con lumalinux... " _

echo "== 2) lumalinux: build + install.sh =="
"$HOME/build-lumalinux.sh" "$LUMALINUX" || echo "   (build falló; install.sh usará el .so del release)"
( cd "$LUMALINUX" && ./install.sh ) || echo "   install.sh falló (¿SLSsteam/steam.sh listos?)"
SO=$(find "$LUMALINUX/build" -maxdepth 2 -name 'liblumalinux.so' 2>/dev/null | head -1)
if [ -n "$SO" ]; then
    mkdir -p "$HOME/.local/share/lumalinux"
    cp "$SO" "$HOME/.local/share/lumalinux/liblumalinux.so"
    echo "   (desplegado TU .so compilado en vez del release)"
fi

echo; echo ">> Reinicio Steam para cargar lumalinux..."; restart_steam
read -rp ">> Cuando Steam haya vuelto, Enter para desplegar LumaDeck + Decky... " _

echo "== 3) LumaDeck: build + deploy =="
"$HOME/deploy-lumadeck.sh"

echo "== 4) Decky =="
"$HOME/start-decky.sh"

echo
echo "Hecho. Abre el QAM -> icono Decky -> LumaDeck."
echo "Si algo no salió, mira: /tmp/decky.log (Decky), ~/.cache/lumalinux/lumalinux.log (lumalinux)."
FIN
chmod +x "$HOME/finish-setup.sh"

# =============================================================================
# Pre-deploy LumaDeck (build-time) so el plugin ya está puesto al arrancar.
# No-fatal: si falla, se hace luego con ~/deploy-lumadeck.sh.
# =============================================================================
echo "Pre-desplegando LumaDeck..."
"$HOME/deploy-lumadeck.sh" 2>&1 | tail -4 || echo "  (pre-deploy falló — hazlo luego con ~/deploy-lumadeck.sh)"

# =============================================================================
# README
# =============================================================================
cat > "$HOME/README.dev.md" <<'EOF'
# Arch dev codespace — test lumalinux + LumaDeck like SteamOS

Base = Arch Linux (SteamOS también es Arch, así que paths y paquetes coinciden
con la Deck). Sirve para probar lumalinux y LumaDeck sobre un Steam real cuando
no tienes la Deck a mano.

## Pre-hecho al crear el codespace

  - Arch + multilib + Steam + render por software (Mesa/Vulkan SW)
  - Display headless (Xvfb + x11vnc + noVNC en :6080), machine-id, bwrap setuid
  - Toolchain 32-bit (lumalinux) + nodejs/npm (LumaDeck) + unzip
  - Repo LumaDeck clonado en ~/LumaDeck y **ya buildeado + desplegado** en
    ~/homebrew/plugins/LumaDeck
  - **Decky PluginLoader** bajado en ~/homebrew/services (falta arrancarlo)
  - Scripts: ~/start-display.sh, ~/start-decky.sh, ~/finish-setup.sh,
    ~/build-lumalinux.sh, ~/deploy-lumadeck.sh

## Arranque rápido (revisar la UI de LumaDeck)

1. `~/start-display.sh` -> abre el puerto **6080** forwardeado -> `/vnc.html`.
2. `DISPLAY=:1 steam -no-cef-sandbox &` -> loguéate (la 1ª vez baja su runtime).
   **Lánzalo así, simple. NO uses `dbus-run-session`** (ver Notas).
3. `~/start-decky.sh` -> abre el QAM en Steam: icono Decky -> LumaDeck.
   (El debug CEF de Steam ya viene activado por el marcador que crea post-create,
   así que en un codespace nuevo no hace falta reiniciar Steam. Si start-decky
   avisa de que el 8080 no responde, reinicia Steam una vez y relánzalo.)

La UI se ve aunque los componentes salgan "not installed"; para revisar estados
usa la pestaña **Dev** de LumaDeck (fuerza health/credenciales).

## Stack funcional completo (descargas de verdad)

Para SLSsteam + lumalinux + descargas reales, tras loguearte en Steam:

```
~/finish-setup.sh
```

Hace, en orden y parándose en los pasos frágiles: headcrab (SLSsteam +
CloudRedirect) -> reinicio Steam -> build + install.sh de lumalinux -> reinicio
-> deploy LumaDeck -> arranca Decky.

## Lecciones aprendidas (léelas)

- **Lanza Steam SIMPLE** (`DISPLAY=:1 steam -no-cef-sandbox &`). **NO lo envuelvas
  en `dbus-run-session`**: levanta xdg-desktop-portal / a11y / launcher-service
  que se comen la memoria, y el proceso peta con `mmap() failed: Cannot allocate
  memory` a mansalva -> **pantalla negra**. Los warnings de dbus del arranque
  simple (session bus, zenity) son **cosméticos**; déjalos.
- **Decky necesita el debug CEF de Steam (localhost:8080)**, apagado por defecto.
  Lo enciende el fichero `~/.local/share/Steam/.cef-enable-remote-debugging`
  (post-create ya lo crea) + un arranque de Steam con él puesto. Arrancar el
  PluginLoader SIN ese puerto abierto = Decky no aparece.
- **Decky se arranca como el usuario `codespace`, NUNCA como root** (root rompe
  el bind del socket y chownea ~/homebrew). ~/start-decky.sh ya lo hace bien.

## Otras notas

- El **login de Steam** es el único paso irreducible manual.
- **headcrab** aquí es más benigno que en la Deck (no hay Game Mode / OOBE),
  pero sigue necesitando Steam corriendo.
- Si ves `mmap() failed` incluso con el arranque simple, es el límite de memoria
  del Codespace; usa un tipo de máquina más grande.
- Logs: Decky /tmp/decky.log · Steam /tmp/steam.log · lumalinux
  ~/.cache/lumalinux/lumalinux.log
EOF

echo
echo "post-create OK."
echo "  1) ~/start-display.sh   -> puerto 6080 (/vnc.html)"
echo "  2) DISPLAY=:1 steam -no-cef-sandbox &   (login)"
echo "  3) ~/start-decky.sh     -> QAM -> LumaDeck"
echo "  Stack funcional completo: ~/finish-setup.sh   ·   Todo en ~/README.dev.md"
