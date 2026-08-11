#!/usr/bin/env bash
set -e

# =============================================================================
# post-create del env SteamOS-like. Igual que el del env base (../post-create.sh)
# en display/red/Decky/helpers, mas:
#   - XDG_RUNTIME_DIR real en /run/user/<uid> + session bus en $XDG_RUNTIME_DIR/bus
#     (desktop_handoff.py invoca steamos-session-select con
#     DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/<uid>/bus: ese socket existe)
#   - Emulacion de steamos-session-select (cambio Game Mode <-> Plasma real
#     dentro del display :1) y stub de steamos-readonly
#   - Steam se arranca en gamepadui (~/start-gamemode.sh)
#   - ~/test-handoff.sh: smoke test del round-trip sin LumaDeck
# =============================================================================

# Mesa software stack + ~/.local/bin en el PATH + runtime dir tipo Deck.
cat >> "$HOME/.bashrc" <<'BASHRC'

# SteamOS-like env — Mesa SW + pip user bin + runtime dir como en la Deck
export LIBGL_ALWAYS_SOFTWARE=1
export __GLX_VENDOR_LIBRARY_NAME=mesa
export VK_LOADER_DRIVERS_DISABLE=nvidia*
export PATH="$HOME/.local/bin:$PATH"
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=$XDG_RUNTIME_DIR/bus"
BASHRC

export PATH="$HOME/.local/bin:$PATH"

# noVNC y websockify no estan en los repos oficiales de Arch (movidos a AUR).
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

# =============================================================================
# start-display.sh — igual que el env base + runtime dir /run/user/<uid> +
# session bus en $XDG_RUNTIME_DIR/bus (lo necesitan Plasma, konsole y la
# invocacion de steamos-session-select que hace el backend de LumaDeck).
# Resolucion 1280x800 = la pantalla nativa de la Deck.
# =============================================================================
cat > "$HOME/start-display.sh" <<EOF
#!/usr/bin/env bash
# Xvfb :1 + openbox + x11vnc + websockify -> noVNC en puerto 6080.
set -e

export LIBGL_ALWAYS_SOFTWARE=1
export __GLX_VENDOR_LIBRARY_NAME=mesa
export VK_LOADER_DRIVERS_DISABLE=nvidia*
export PATH="\$HOME/.local/bin:\$PATH"
export XDG_RUNTIME_DIR="/run/user/\$(id -u)"

# Red para el cliente de Steam (D-Bus de sistema + NetworkManager + IPv6 off).
[ -x "\$HOME/start-net.sh" ] && "\$HOME/start-net.sh" || true

sudo mkdir -p /tmp/.X11-unix
sudo chmod 1777 /tmp/.X11-unix

# Runtime dir real (/run/user/<uid>) + session bus. desktop_handoff.py llama a
# steamos-session-select con DBUS_SESSION_BUS_ADDRESS=unix:path=\$XDG_RUNTIME_DIR/bus,
# y Plasma/konsole tambien lo usan — tiene que existir de verdad.
sudo mkdir -p "\$XDG_RUNTIME_DIR"
sudo chown "\$(id -u):\$(id -g)" "\$XDG_RUNTIME_DIR"
chmod 700 "\$XDG_RUNTIME_DIR"
if [ ! -S "\$XDG_RUNTIME_DIR/bus" ]; then
    dbus-daemon --session --address="unix:path=\$XDG_RUNTIME_DIR/bus" --fork
fi

pkill -f "Xvfb :1"            2>/dev/null || true
pkill -f "x11vnc -display :1" 2>/dev/null || true
pkill -f "websockify .* 6080" 2>/dev/null || true
sleep 0.3

# Locks/sockets muertos de un run anterior impiden a Xvfb bindear :1.
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
# start-net.sh — identico al env base: D-Bus de sistema + NetworkManager +
# IPv6 off. Ver ../post-create.sh para la justificacion completa.
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

"$HOME/start-net.sh" || true

# =============================================================================
# Emulacion SteamOS: session-switch + steamos-session-select + steamos-readonly.
# LumaDeck trata steamos-session-select como caja negra (exec + contrato de
# args), asi que emular el binario cubre el round-trip completo del hand-off:
# armado -> switch a Plasma -> autostart XDG -> konsole --hold -> payload ->
# steamos-session-select gamescope -> vuelta a gamepadui.
# =============================================================================
sudo install -d -m 755 /usr/local/lib/lumadev

sudo tee /usr/local/lib/lumadev/session-switch.sh >/dev/null <<'SWITCH'
#!/usr/bin/env bash
# Hace el cambio de "sesion" real dentro del display :1 (emulacion de lo que
# steamos-session-select provoca en la Deck):
#   plasma    -> para Steam y arranca startplasma-x11. Plasma ejecuta los XDG
#                autostart de ~/.config/autostart — ahi se dispara el konsole
#                del hand-off de LumaDeck, igual que en la consola.
#   gamescope -> para Plasma y relanza Steam en gamepadui.
# Corre setsid-eado desde steamos-session-select para sobrevivir a la muerte
# del proceso que lo invoco (p.ej. el konsole del hand-off muere cuando este
# script mata Plasma, y el relanzado de Steam tiene que seguir vivo).
set +e

# Re-exec con entorno LIMPIO. Cuando el switch lo dispara el konsole del
# hand-off, hereda el entorno completo de la sesion Plasma (SESSION_MANAGER,
# KDE_FULL_SESSION, QT_*, ...) que contamina el relanzado de Steam. Un cambio
# de sesion real arranca con entorno fresco; lo reproducimos.
if [ -z "$LUMADEV_CLEAN" ]; then
    exec /usr/bin/env -i \
        LUMADEV_CLEAN=1 \
        HOME="${HOME:-/home/deck}" \
        USER="$(id -un)" \
        LOGNAME="$(id -un)" \
        SHELL=/bin/bash \
        LANG="${LANG:-en_US.UTF-8}" \
        PATH=/usr/local/sbin:/usr/local/bin:/usr/bin:/usr/sbin:/bin:/sbin \
        /bin/bash "$0" "$@"
fi

TARGET="$1"

export DISPLAY=:1
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=$XDG_RUNTIME_DIR/bus"
export LIBGL_ALWAYS_SOFTWARE=1
export __GLX_VENDOR_LIBRARY_NAME=mesa
export VK_LOADER_DRIVERS_DISABLE='nvidia*'
export PATH="$HOME/.local/bin:$PATH"

# stdout va a /tmp/session-select.log (redirigido por steamos-session-select).
log() { echo "$(date '+%T') [switch $$] $*"; }

STEAM_BIN="$HOME/.local/share/Steam/ubuntu12_32/steam"

stop_steam() {
    log "stop_steam"
    [ -x "$STEAM_BIN" ] && "$STEAM_BIN" -shutdown >/dev/null 2>&1
    for _ in $(seq 1 30); do pgrep -x steam >/dev/null 2>&1 || break; sleep 1; done
    pkill -x steam 2>/dev/null
    pkill -f steamwebhelper 2>/dev/null
    sleep 1
    log "stop_steam done (steam=$(pgrep -x steam >/dev/null 2>&1 && echo VIVO || echo muerto))"
}

# Sin logind no hay "cerrar sesion" limpio: matamos los procesos de la sesion
# Plasma. Lista explicita para no llevarnos nada mas por delante (Decky,
# x11vnc y websockify tienen que sobrevivir al switch).
PLASMA_PROCS="plasmashell kwin_x11 startplasma-x11 ksmserver kded6 kded5 \
              kglobalacceld kactivitymanagerd xembedsniproxy kaccess ksplashqml"

stop_plasma() {
    log "stop_plasma"
    for p in $PLASMA_PROCS; do pkill -x "$p" 2>/dev/null; done
    pkill -f 'plasma[-_]session' 2>/dev/null
    pkill -f 'polkit-kde-authentication' 2>/dev/null
    # Verificar y escalar a -9: si kwin sobrevive retiene el display y ni
    # openbox ni la siguiente sesion Plasma pueden arrancar bien.
    for _ in 1 2 3 4 5; do
        pgrep -x plasmashell >/dev/null 2>&1 || pgrep -x kwin_x11 >/dev/null 2>&1 || break
        sleep 1
    done
    for p in $PLASMA_PROCS; do pkill -9 -x "$p" 2>/dev/null; done
    pkill -9 -f 'plasma[-_]session' 2>/dev/null
    sleep 1
    log "stop_plasma done (kwin=$(pgrep -x kwin_x11 >/dev/null 2>&1 && echo VIVO || echo muerto) plasmashell=$(pgrep -x plasmashell >/dev/null 2>&1 && echo VIVO || echo muerto))"
}

# Game Mode = el supervisor (emula gamescope-session): relanza Steam si muere.
# El estado de sesion en /tmp/lumadev-session decide si el supervisor sigue.
start_gamemode() {
    echo gamescope > /tmp/lumadev-session
    ( nohup setsid /usr/local/lib/lumadev/gamemode-supervisor.sh </dev/null >>/tmp/session-select.log 2>&1 & )
    sleep 8
    log "start_gamemode done (steam=$(pgrep -x steam >/dev/null 2>&1 && echo arrancado || echo 'aun no — sigue al supervisor en este log'))"
}

log "target=$TARGET"
case "$TARGET" in
plasma)
    # Estado ANTES de matar Steam: el supervisor consulta /tmp/lumadev-session
    # al morir su Steam; si ya dice plasma, se retira en vez de resucitarlo.
    echo plasma > /tmp/lumadev-session
    stop_steam
    stop_plasma   # por si quedo una sesion anterior a medias
    # openbox (el WM que levanta start-display.sh para Steam) impide a kwin
    # coger el display: "kwin: unable to claim manager selection". Fuera antes
    # de arrancar Plasma; al volver a gamescope se relanza.
    pkill -x openbox 2>/dev/null
    sleep 0.5
    if ! command -v startplasma-x11 >/dev/null 2>&1; then
        log "startplasma-x11 no existe (¿falta el paquete plasma-x11-session?)"
        exit 1
    fi
    export XDG_SESSION_TYPE=x11
    export XDG_CURRENT_DESKTOP=KDE
    ( startplasma-x11 >/tmp/plasma.log 2>&1 & )
    log "plasma lanzado (log /tmp/plasma.log)"
    ;;
gamescope)
    stop_plasma
    # kwin murio con Plasma: devolvemos openbox (gamepadui quiere un WM debajo
    # para focus/stacking, igual que en el env base).
    pgrep -x openbox >/dev/null 2>&1 || ( openbox >/dev/null 2>&1 & )
    # Decky (PluginLoader) sigue corriendo; se re-inyecta cuando el CEF vuelve.
    start_gamemode
    ;;
*)
    log "target desconocido: $TARGET"
    exit 1
    ;;
esac
SWITCH
sudo chmod 755 /usr/local/lib/lumadev/session-switch.sh

sudo tee /usr/local/lib/lumadev/gamemode-supervisor.sh >/dev/null <<'SUPER'
#!/usr/bin/env bash
# Emula la supervision de Steam que hace gamescope-session en la Deck: relanza
# Steam cada vez que muere, mientras la "sesion" (/tmp/lumadev-session) siga
# siendo gamescope. LumaDeck cuenta con este comportamiento: su restart_steam()
# hace solo `steam -shutdown` ("Game Mode auto-restarts it"), y el final del
# Quick Install reinicia igual — sin supervisor, cualquier restart desde el QAM
# deja la pantalla en negro.
set +e
STATE=/tmp/lumadev-session
PIDFILE=/tmp/lumadev-supervisor.pid

# Un solo supervisor vivo.
if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE" 2>/dev/null)" 2>/dev/null; then
    exit 0
fi
echo $$ > "$PIDFILE"

export DISPLAY=:1
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=$XDG_RUNTIME_DIR/bus"
export LIBGL_ALWAYS_SOFTWARE=1
export __GLX_VENDOR_LIBRARY_NAME=mesa
export VK_LOADER_DRIVERS_DISABLE='nvidia*'
export PATH="$HOME/.local/bin:$PATH"

log() { echo "$(date '+%T') [supervisor $$] $*" >> /tmp/session-select.log; }

log "arranca (session=$(cat "$STATE" 2>/dev/null))"
FAILS=0
while [ "$(cat "$STATE" 2>/dev/null)" = "gamescope" ]; do
    log "lanzando steam gamepadui"
    T0=$SECONDS
    # Ruta REAL del Deck: Game Mode va por steam-launcher.service -> (nuestro
    # systemd drop-in) -> GM_LAUNCHER (guard + wrapper) -> real steam-launcher.
    # El codespace no tiene systemd --user, asi que replicamos ESA cadena: si
    # lumalinux instalo su GM_LAUNCHER (post-migracion), lanzamos por el, de modo
    # que Game Mode pasa por el guard + wrapper e inyecta AUTOMATICO en cada
    # relanzamiento (como en la consola). Si no existe (pre-migracion / stack
    # viejo), steam normal. GM_LAUNCHER hace `exec /usr/lib/steamos/steam-launcher`
    # (stubeado mas abajo) -> steam gamepadui.
    GM_L="$HOME/.local/share/SLSsteam/lumalinux-steam-launcher"
    if [ -x "$GM_L" ]; then
        "$GM_L" >>/tmp/steam.log 2>&1
    else
        steam -gamepadui -no-cef-sandbox >>/tmp/steam.log 2>&1
    fi
    RC=$?
    DUR=$((SECONDS - T0))
    [ "$(cat "$STATE" 2>/dev/null)" != "gamescope" ] && break
    # Guardia anti crash-loop: 5 muertes seguidas en <15s = algo roto de verdad
    # (en la Deck real gamescope-session tambien corta el bucle).
    if [ "$DUR" -lt 15 ]; then
        FAILS=$((FAILS+1))
        if [ "$FAILS" -ge 5 ]; then
            log "steam muere en bucle (rc=$RC, ${DUR}s); paro — mira /tmp/steam.log"
            break
        fi
    else
        FAILS=0
    fi
    log "steam salio (rc=$RC, ${DUR}s); relanzo en 2s (como gamescope-session)"
    sleep 2
done
rm -f "$PIDFILE"
log "termina (session=$(cat "$STATE" 2>/dev/null))"
SUPER
sudo chmod 755 /usr/local/lib/lumadev/gamemode-supervisor.sh

sudo tee /usr/bin/steamos-session-select >/dev/null <<'SEL'
#!/usr/bin/env bash
# Emulacion del steamos-session-select de SteamOS (HoloISO). Misma interfaz:
# acepta plasma / plasma-wayland / gamescope y rechaza lo demas con exit 1,
# como el caso `*)` del script de Valve. LumaDeck depende de ese contrato
# (_desktop_arg_for elige 'plasma' para el linaje steamos precisamente porque
# 'desktop' aqui NO es un arg valido). plasma-wayland se mapea a la sesion
# X11: en este contenedor no hay Wayland.
LOG=/tmp/session-select.log
case "${1:-}" in
    plasma|plasma-wayland) TARGET=plasma ;;
    gamescope)             TARGET=gamescope ;;
    *)
        echo "steamos-session-select: unsupported session '${1:-}'" >&2
        exit 1
        ;;
esac
echo "$(date '+%F %T') uid=$(id -u) arg='${1:-}' -> $TARGET" >>"$LOG" 2>/dev/null
# nohup + setsid + </dev/null: el switch tiene que sobrevivir a la muerte del
# proceso que lo llamo. El caso critico es el hand-off: el script que corre en
# konsole es el LIDER DE SESION de su pty y termina justo despues de llamarnos;
# al morir un lider de sesion el kernel manda SIGHUP al grupo foreground de la
# pty, y el hijo sigue en ese grupo hasta completar su setsid() — una carrera
# que lo mataba antes de ejecutar nada. nohup pone SIGHUP a ignorado ANTES del
# exec (y eso sobrevive al exec), asi que la carrera ya no puede matarlo. El
# sleep da ademas tiempo a que el setsid() se complete con el caller aun vivo.
nohup setsid /usr/local/lib/lumadev/session-switch.sh "$TARGET" </dev/null >>"$LOG" 2>&1 &
sleep 0.3
exit 0
SEL
sudo chmod 755 /usr/bin/steamos-session-select

sudo tee /usr/bin/steamos-readonly >/dev/null <<'RO'
#!/usr/bin/env bash
# Stub del steamos-readonly de la Deck. Aqui no hay rootfs inmutable, pero con
# ID=steamos en os-release, scripts externos (headcrab, etc.) pueden llamarlo.
# Interfaz real: enable | disable | status. No-op que siempre "funciona".
case "${1:-}" in
    status) echo "disabled" ;;
    enable|disable) : ;;
    *)
        echo "Usage: steamos-readonly {enable|disable|status}" >&2
        exit 1
        ;;
esac
exit 0
RO
sudo chmod 755 /usr/bin/steamos-readonly

# /usr/lib/steamos/steam-launcher: en la Deck es el binario que gamescope-session
# arranca (via steam-launcher.service) para meter Steam en Game Mode, y sobre el
# que lumalinux instala su systemd drop-in. Aqui no existe, pero GM_LAUNCHER hace
# `exec /usr/lib/steamos/steam-launcher`, asi que lo stubeamos para que lance el
# Steam del codespace en gamepadui. Con esto la cadena post-migracion
# supervisor -> GM_LAUNCHER -> guard -> wrapper -> steam-launcher -> steam queda
# igual que en la consola (ver el supervisor arriba).
sudo install -Dm755 /dev/stdin /usr/lib/steamos/steam-launcher <<'SL'
#!/bin/sh
exec steam -gamepadui -no-cef-sandbox "$@"
SL

# Plasma sin bloqueo de pantalla: en un display headless el lock solo molesta
# (y no hay logind que gestione el unlock).
mkdir -p "$HOME/.config"
cat > "$HOME/.config/kscreenlockerrc" <<'KLOCK'
[Daemon]
Autolock=false
LockOnResume=false
KLOCK

# Plasma 6 arranca la sesion via systemd de usuario por defecto
# (`systemctl --user start plasma-workspace-x11.target`). En el contenedor no
# hay systemd, asi que se queda en el splash para siempre y el autostart (el
# konsole del hand-off) nunca corre. systemdBoot=false fuerza el arranque
# clasico (plasma-session lanza los autostart directamente).
cat > "$HOME/.config/startkderc" <<'KDERC'
[General]
systemdBoot=false
KDERC

# Sin splash: sobre Xvfb solo estorba (y si algo cuelga, tapa lo que hay debajo).
cat > "$HOME/.config/ksplashrc" <<'KSPLASH'
[KSplash]
Theme=None
Engine=none
KSPLASH

# Sin compositor: kwin con GL sobre llvmpipe va lento y no aporta nada aqui.
cat > "$HOME/.config/kwinrc" <<'KWINRC'
[Compositing]
Enabled=false
KWINRC

# =============================================================================
# start-gamemode.sh — Steam en gamepadui (la UI de Game Mode real de la Deck).
# =============================================================================
cat > "$HOME/start-gamemode.sh" <<'GM'
#!/usr/bin/env bash
# Arranca Game Mode: Steam en gamepadui sobre :1 BAJO EL SUPERVISOR que emula
# gamescope-session (si Steam muere o algo hace `steam -shutdown` — el boton
# Restart Steam de LumaDeck, el final del Quick Install — se relanza solo,
# como en la Deck). El QAM donde vive Decky/LumaDeck se abre con el boton de
# acceso rapido de la barra inferior.
set -e
[ -x "$HOME/start-net.sh" ] && "$HOME/start-net.sh" || true

echo gamescope > /tmp/lumadev-session
( nohup setsid /usr/local/lib/lumadev/gamemode-supervisor.sh </dev/null >>/tmp/session-select.log 2>&1 & )
echo "Steam gamepadui arrancando bajo el supervisor. Mirala en noVNC (:6080)."
echo "Logs: /tmp/steam.log (Steam) y /tmp/session-select.log (supervisor)."
GM
chmod +x "$HOME/start-gamemode.sh"

# =============================================================================
# test-handoff.sh — smoke test del round-trip de sesion SIN LumaDeck: arma un
# autostart one-shot con el MISMO mecanismo que desktop_handoff.py (XDG
# autostart + konsole --hold + auto-borrado) y cambia a Plasma. El payload
# imprime, espera 15s y vuelve a Game Mode. Si esto funciona, el hand-off del
# plugin tiene toda la infraestructura que necesita.
# =============================================================================
cat > "$HOME/test-handoff.sh" <<'TH'
#!/usr/bin/env bash
set -e
AUTOSTART="$HOME/.config/autostart"
SCRIPT="$HOME/.local/share/lumadev-test/handoff-test.sh"
mkdir -p "$AUTOSTART" "$(dirname "$SCRIPT")"

cat > "$SCRIPT" <<EOS
#!/bin/bash
# One-shot: borrar el autostart ANTES de correr, como hace desktop_handoff.py.
rm -f "$AUTOSTART/lumadev-handoff-test.desktop"
echo "=============================================="
echo " LumaDev hand-off test: payload en Desktop"
echo "=============================================="
date
echo
echo "Volviendo a Game Mode en 15s..."
sleep 15
echo ">> llamando a steamos-session-select gamescope"
steamos-session-select gamescope
echo ">> steamos-session-select devolvio rc=\$? — el switch sigue en /tmp/session-select.log"
EOS
chmod +x "$SCRIPT"

cat > "$AUTOSTART/lumadev-handoff-test.desktop" <<EOD
[Desktop Entry]
Type=Application
Name=LumaDev Hand-off Test
Exec=konsole --hold -e $SCRIPT
Terminal=false
EOD
chmod 755 "$AUTOSTART/lumadev-handoff-test.desktop"

echo "Armado. Cambiando a Plasma (miralo en noVNC)..."
steamos-session-select plasma
TH
chmod +x "$HOME/test-handoff.sh"

# =============================================================================
# steamosify-repos.sh — OPCIONAL / EXPERIMENTAL: repos pacman de Valve.
# =============================================================================
cat > "$HOME/steamosify-repos.sh" <<'REPOS'
#!/usr/bin/env bash
# OPCIONAL / EXPERIMENTAL: añade el mirror publico de paquetes de SteamOS para
# poder instalar cosas que solo existen alli (steamos-*, holo-*, versiones
# pineadas de la Deck). Se añaden AL FINAL de pacman.conf (prioridad mas baja):
# NO secuestran las libs del sistema. Para traer algo de Valve usa sintaxis
# explicita repo/paquete:
#     sudo pacman -Sy
#     pacman -Sl jupiter-main | less        # ver que hay
#     sudo pacman -S holo-main/<paquete>
# SigLevel=Never porque el keyring de Valve no esta en el de Arch (env de test).
set -e
if grep -q '\[jupiter-main\]' /etc/pacman.conf; then
    echo "Los repos de Valve ya estan en pacman.conf"
    exit 0
fi
sudo tee -a /etc/pacman.conf >/dev/null <<'EOF'

# Valve SteamOS mirror (añadido por steamosify-repos.sh — experimental)
[jupiter-main]
Server = https://steamdeck-packages.steamos.cloud/archlinux-mirror/$repo/os/$arch
SigLevel = Never

[holo-main]
Server = https://steamdeck-packages.steamos.cloud/archlinux-mirror/$repo/os/$arch
SigLevel = Never
EOF
sudo pacman -Sy
echo "Hecho. Ejemplo: sudo pacman -S holo-main/<paquete>"
REPOS
chmod +x "$HOME/steamosify-repos.sh"

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
# Decky Loader: ~/homebrew + PluginLoader. Igual que el env base: bajar aqui,
# arrancar en runtime (~/start-decky.sh), SIEMPRE como `deck`, nunca root.
# =============================================================================
echo "Preparando Decky Loader..."
mkdir -p "$HOME/homebrew/plugins" "$HOME/homebrew/services" \
         "$HOME/homebrew/data" "$HOME/homebrew/logs" "$HOME/homebrew/themes"
curl -fsSL -o "$HOME/homebrew/services/PluginLoader" \
    https://github.com/SteamDeckHomebrew/decky-loader/releases/latest/download/PluginLoader \
    && chmod +x "$HOME/homebrew/services/PluginLoader" \
    || echo "  (descarga de PluginLoader falló — bájalo a mano si lo necesitas)"

# Debug CEF de Steam (localhost:8080) — necesario para la inyeccion de Decky.
# El marcador se crea ANTES del primer arranque de Steam.
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
    echo "    ~/.local/share/Steam/ubuntu12_32/steam -shutdown; sleep 5; ~/start-gamemode.sh"
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
# Igual que el env base pero los reinicios de Steam van a gamepadui.
# =============================================================================
cat > "$HOME/finish-setup.sh" <<'FIN'
#!/usr/bin/env bash
# Monta SLSsteam + lumalinux + LumaDeck + Decky. Ejecutar DESPUÉS de arrancar
# Steam (~/start-gamemode.sh) y loguearte. Cada paso frágil se para para que
# puedas verlo.
set -e
LUMALINUX="${LUMALINUX:-/workspaces/lumalinux}"
[ -d "$LUMALINUX" ] || LUMALINUX="$HOME/lumalinux"
STEAM="$HOME/.local/share/Steam/ubuntu12_32/steam"

restart_steam() {
    "$STEAM" -shutdown 2>/dev/null || true
    for i in $(seq 1 20); do pgrep -x steam >/dev/null 2>&1 || break; sleep 1; done
    "$HOME/start-gamemode.sh"
    echo "  Steam relanzándose en gamepadui... dale ~20s."
}

echo "== 1) headcrab: SLSsteam + CloudRedirect =="
echo "   (Steam debe estar corriendo y logueado.)"
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
# =============================================================================
echo "Pre-desplegando LumaDeck..."
"$HOME/deploy-lumadeck.sh" 2>&1 | tail -4 || echo "  (pre-deploy falló — hazlo luego con ~/deploy-lumadeck.sh)"

# =============================================================================
# README
# =============================================================================
cat > "$HOME/README.dev.md" <<'EOF'
# SteamOS-like codespace — lumalinux + LumaDeck con hand-off real

Variante del env Arch con identidad SteamOS: `ID=steamos` en os-release,
usuario `deck` (uid 1000) + `/home/deck`, Steam en **gamepadui** (el QAM real
de Game Mode) y **Plasma + konsole** para el desktop hand-off de LumaDeck.
El cambio de sesión (`steamos-session-select`) está emulado y hace switches
reales dentro del display :1.

## Pre-hecho al crear el codespace

  - Identidad SteamOS (os-release), usuario `deck`, rutas de la Deck
  - Arch + multilib + Steam + render por software + Plasma + konsole
  - Display headless (Xvfb 1280x800 = pantalla de la Deck, noVNC en :6080)
  - `/usr/bin/steamos-session-select` (emulado, switches reales) y
    `/usr/bin/steamos-readonly` (stub no-op)
  - Toolchain 32-bit + nodejs/npm; LumaDeck clonado, buildeado y desplegado
    en ~/homebrew/plugins/LumaDeck; Decky PluginLoader bajado
  - Scripts: ~/start-display.sh, ~/start-gamemode.sh, ~/start-decky.sh,
    ~/test-handoff.sh, ~/finish-setup.sh, ~/build-lumalinux.sh,
    ~/deploy-lumadeck.sh, ~/steamosify-repos.sh

## Arranque rápido

1. `~/start-display.sh` -> abre el puerto **6080** forwardeado -> `/vnc.html`.
2. `~/start-gamemode.sh` -> Steam en gamepadui; loguéate (la 1ª vez baja su
   runtime y tarda).
3. `~/start-decky.sh` -> abre el QAM (botón de acceso rápido de la barra
   inferior): icono Decky -> LumaDeck.

Stack funcional completo (SLSsteam + lumalinux + descargas): `~/finish-setup.sh`.

## Testear el desktop hand-off

Primero valida la infraestructura sin el plugin:

```
~/test-handoff.sh
```

Deberías ver en noVNC: Steam se cierra -> aparece Plasma -> konsole con el
payload de test -> a los 15s vuelve gamepadui. Si ese round-trip funciona,
el hand-off de LumaDeck tiene todo lo que necesita.

Después, desde LumaDeck (QAM): los botones que arman el hand-off
(`run_desktop_handoff_real` / `run_desktop_handoff_quick_install`) hacen
exactamente ese ciclo con el payload real (headcrab + re-inject de lumalinux,
o Quick Install completo). La rama de fallo deja konsole abierta (`--hold`)
en Plasma, como en la Deck. Diagnóstico: `cat ~/lh.json` ·
`/tmp/session-select.log` · `/tmp/plasma.log`.

## Qué es real y qué está emulado

  - **Real**: identidad de distro (ramas SteamOS de platform_info/headcrab),
    rutas de `deck`, gamepadui/QAM, Plasma ejecutando el autostart XDG,
    konsole --hold, los payloads del hand-off, Decky.
  - **Emulado**: `steamos-session-select` (el switch es process-level sobre
    :1, no sddm/logind), `steamos-readonly` (no-op) y la **supervisión de
    Steam de gamescope-session**: un supervisor relanza Steam cuando muere
    en "sesión" gamescope, así que `steam -shutdown` (el Restart Steam de
    LumaDeck, el final del Quick Install) se comporta como en la Deck. La
    fontanería interna de sesión (gamescope real, logind, timings) NO se
    reproduce.
  - **Imposible aquí**: kernel neptune, GPU/gamescope DRM, mandos físicos,
    inmutabilidad real del rootfs.

## Notas

- **Lanza Steam siempre con los scripts** (`~/start-gamemode.sh`). NO uses
  `dbus-run-session` (pantalla negra por OOM, ver env base).
- **Memoria**: Plasma + Steam + headcrab pide máquina de 8 GB mínimo
  (16 GB cómodo). `mmap() failed` en logs = te quedaste corto.
- El **login de Steam** sigue siendo el único paso manual irreducible.
- `~/steamosify-repos.sh` (opcional/experimental) añade el mirror de paquetes
  de Valve para instalar paquetes `steamos-*`/`holo-*` reales.
- Si actualizas el paquete `filesystem` de Arch, pisa el os-release SteamOS
  (reescríbelo o reconstruye el contenedor).
- Logs: Decky /tmp/decky.log · Steam /tmp/steam.log · Plasma /tmp/plasma.log ·
  session switch /tmp/session-select.log · lumalinux
  ~/.cache/lumalinux/lumalinux.log
EOF

echo
echo "post-create OK."
echo "  1) ~/start-display.sh   -> puerto 6080 (/vnc.html)"
echo "  2) ~/start-gamemode.sh  -> Steam gamepadui (login)"
echo "  3) ~/start-decky.sh     -> QAM -> LumaDeck"
echo "  Hand-off smoke test: ~/test-handoff.sh   ·   Todo en ~/README.dev.md"
