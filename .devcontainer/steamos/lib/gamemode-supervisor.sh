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
    # Lo que el cliente necesita y un reinicio del codespace rompe: el bus de
    # sesion vivo (el de 64 bits no arranca sin el) y ningun steam.pid/pipe
    # huerfano (Steam saldria al instante con "already running" y este bucle
    # lo tomaria por un crash-loop). Aqui Steam acaba de salir, asi que no hay
    # proceso que respetar.
    "$HOME/ensure-session-bus.sh" >>/tmp/session-select.log 2>&1 || log "bus de sesion no disponible; lanzo igual"
    pgrep -f "steamrt64/steam|ubuntu12_32/steam" >/dev/null 2>&1 || rm -f "$HOME/.steam/steam.pid" "$HOME/.steam/steam.pipe"
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
