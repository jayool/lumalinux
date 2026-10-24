#!/usr/bin/env bash
# Deja el bus de sesión de D-Bus VIVO en $XDG_RUNTIME_DIR/bus, que es donde lo
# busca el cliente de Steam. Un reinicio del codespace deja el socket en disco
# sin demonio detrás; un `[ -S ]` lo da por bueno y el cliente de 64 bits no
# arranca ("steam-runtime-launcher-service: Can't find session bus", el updater
# sale con rc 125 y solo lo cuenta logs/console-linux.txt). Aquí se pregunta al
# bus de verdad; si no contesta, se reemplaza. Un bus vivo no se toca.
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
BUS="$XDG_RUNTIME_DIR/bus"
export DBUS_SESSION_BUS_ADDRESS="unix:path=$BUS"

sudo mkdir -p "$XDG_RUNTIME_DIR"
sudo chown "$(id -u):$(id -g)" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

bus_alive() {
    dbus-send --session --dest=org.freedesktop.DBus --print-reply \
        /org/freedesktop/DBus org.freedesktop.DBus.ListNames >/dev/null 2>&1
}

if bus_alive; then
    exit 0
fi
pkill -f "dbus-daemon --session --address=unix:path=$BUS" 2>/dev/null || true
rm -f "$BUS"
dbus-daemon --session --address="unix:path=$BUS" --fork
for _ in 1 2 3 4 5 6 7 8 9 10; do
    bus_alive && { echo "bus de sesión levantado en $BUS"; exit 0; }
    sleep 0.3
done
echo "no he podido levantar el bus de sesión en $BUS" >&2
exit 1
