#!/usr/bin/env bash
# experiment_cold_cache.sh — observador cronometrado para el Accionable A
# (docs/slssteam-plugins-analysis.md §3.4.2).
#
# QUE MIDE
# --------
# RESEARCH.md §18 dice que OpenSteamTool se cuelga en ProcessPendingLicenseUpdates
# porque inyecta ids de depot en AppIdVec que nunca resuelven appinfo, y que
# slsteam-moon evita eso manteniendo AppIdVec solo con appids. lumalinux reune las
# DOS precondiciones (depots en AppIdVec + reconcile por defecto desde v0.16.16) y
# se apoya en "none observed / warm cache".
#
# Este script no decide nada: cronometra la secuencia real para que los brazos
# sean comparables. NO toca nada del sistema -- solo lee logs.
#
# LOS TRES BRAZOS (cada uno es una ejecucion completa)
#   A  cache CALIENTE + reconcile      -> linea base, deberia ir en segundos
#   B  cache FRIA     + reconcile      -> la hipotesis bajo prueba
#   C  cache FRIA     + LUMA_NO_RECONCILE=1  -> control, aisla si el factor es el reconcile
#
# USO
#   Terminal 1:   ./tools/experiment_cold_cache.sh <brazo> [appid]
#   Terminal 2:   arrancar Steam, esperar login, y cuando el script lo pida,
#                 desplegar el juego con steamidra_lite.
#
# Preparacion de cada brazo (a mano, con Steam CERRADO):
#   A:  (nada)
#   B:  mv ~/.local/share/Steam/appcache ~/appcache.bak
#   C:  igual que B, y exportar LUMA_NO_RECONCILE=1 antes de lanzar Steam
#   Restaurar entre brazos:  rm -rf ~/.local/share/Steam/appcache && mv ~/appcache.bak ~/.local/share/Steam/appcache

set -uo pipefail

ARM="${1:-?}"
APPID="${2:-}"
OUT="${HOME}/coldcache-${ARM}-$(date -u +%Y%m%dT%H%M%SZ).log"

LUMALOG="${HOME}/.cache/lumalinux/lumalinux.log"
CONTENTLOG="${HOME}/.local/share/Steam/logs/content_log.txt"

: > /tmp/.cc_pids
START=$(date +%s)
stamp() { printf '[%6ss] %s\n' "$(( $(date +%s) - START ))" "$1"; }

echo "== experimento caché fría — brazo ${ARM} ${APPID:+(appid ${APPID})} =="
echo "   salida: ${OUT}"
echo

# ── Precondiciones ───────────────────────────────────────────────────────────
echo "-- precondiciones --"
[ -f "$HOME/.local/share/lumalinux/liblumalinux.so" ] \
    && echo "   lumalinux .so ......... presente" \
    || echo "   lumalinux .so ......... AUSENTE  <-- el experimento no mide nada"

if [ -d "$HOME/.local/share/Steam/appcache" ]; then
    echo "   appcache .............. PRESENTE (caché caliente -> brazo A)"
else
    echo "   appcache .............. ausente  (caché fría -> brazo B/C)"
fi

if [ -n "${LUMA_NO_RECONCILE:-}" ]; then
    echo "   LUMA_NO_RECONCILE ..... puesto en este shell"
    echo "   OJO: lo que cuenta es el entorno de STEAM, no el de este script."
fi
echo

echo "-- arranca Steam ahora en otra terminal. Observando... --"
echo "   (Ctrl-C para terminar y ver el resumen)"
echo

# ── Observacion ──────────────────────────────────────────────────────────────
# Los dos tail tienen que escribir DENTRO de la tuberia del grep. Si se lanzan
# como subshells sueltos en segundo plano, su salida va directa a stdout y se
# salta el filtro y el cronometro (bug encontrado probando con logs sinteticos).
# tail -F sobrevive a que Steam trunque content_log.txt al arrancar.

TAILS=""
CLEANED=0
cleanup() {
    [ "$CLEANED" = "1" ] && return
    CLEANED=1
    [ -f /tmp/.cc_pids ] && kill $(cat /tmp/.cc_pids) 2>/dev/null
    echo
    echo "-- fin del brazo ${ARM}. Timeline completo en: ${OUT}"
}
trap cleanup EXIT INT TERM

stamp "observador iniciado (brazo ${ARM})" | tee -a "$OUT"

{
    tail -F -n0 "$LUMALOG"    2>/dev/null | sed -u 's/^/LUMA /' &
    echo $! >> /tmp/.cc_pids
    tail -F -n0 "$CONTENTLOG" 2>/dev/null | sed -u 's/^/CONT /' &
    echo $! >> /tmp/.cc_pids
    wait
} \
| grep --line-buffered -E \
    'KeyStore watcher: .* changed|PKG0_FINDER: (HIT|GOT=|cache-access idiom not found)|LoadPackage\[finder\]: (APPENDED|SANITY|realloc)|Reconcile: (broadcast|no CUser|unresolved|no-op)|SLS-ach: scoped|target depots|0 target|mounted depots|added depots|config changed' \
| while IFS= read -r line; do
      stamp "$line"
  done | tee -a "$OUT"
