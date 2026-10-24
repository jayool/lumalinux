#!/usr/bin/env bash
# Arranca Game Mode: Steam en gamepadui sobre :1 BAJO EL SUPERVISOR que emula
# gamescope-session (si Steam muere o algo hace `steam -shutdown` — el botón
# Restart Steam de LumaDeck, el final del Quick Install — se relanza solo,
# como en la Deck). El QAM donde vive Decky/LumaDeck se abre con el botón de
# acceso rápido de la barra inferior.
#
# Antes de lanzar deja en pie lo que Steam necesita y que un reinicio del
# codespace rompe en silencio:
#   - el bus de sesión vivo (ensure-session-bus.sh): sin él el cliente de 64
#     bits no arranca;
#   - sin steam.pid / steam.pipe huérfanos: Steam diría "already running" y
#     saldría al instante, y el supervisor lo tomaría por un crash-loop;
#   - un supervisor vivo de verdad: uno colgado sin Steam debajo se reemplaza.
# Sale cuando Steam corre, o con error si no ha arrancado en 60 s.
set -e
[ -x "$HOME/start-net.sh" ] && "$HOME/start-net.sh" || true
"$HOME/ensure-session-bus.sh"

steam_alive() { pgrep -f "steamrt64/steam|ubuntu12_32/steam" >/dev/null 2>&1; }

PIDFILE=/tmp/lumadev-supervisor.pid
SUP="$(cat "$PIDFILE" 2>/dev/null || true)"
if [ -n "$SUP" ] && kill -0 "$SUP" 2>/dev/null; then
    if steam_alive; then
        echo "Steam ya corre bajo el supervisor (pid $SUP). Nada que hacer."
        exit 0
    fi
    echo "supervisor $SUP vivo pero sin Steam debajo: lo reemplazo."
    kill "$SUP" 2>/dev/null || true
    rm -f "$PIDFILE"
fi
if ! steam_alive; then
    rm -f "$HOME/.steam/steam.pid" "$HOME/.steam/steam.pipe"
fi

echo gamescope > /tmp/lumadev-session
( nohup setsid /usr/local/lib/lumadev/gamemode-supervisor.sh </dev/null >>/tmp/session-select.log 2>&1 & )
for _ in $(seq 1 30); do
    steam_alive && break
    sleep 2
done
if steam_alive; then
    echo "Steam gamepadui arrancado bajo el supervisor. Mírala en noVNC (:6080)."
    echo "Logs: /tmp/steam.log (Steam) y /tmp/session-select.log (supervisor)."
else
    echo "Steam no ha arrancado en 60 s. Mira /tmp/steam.log, /tmp/session-select.log y ~/.local/share/Steam/logs/console-linux.txt" >&2
    exit 1
fi
