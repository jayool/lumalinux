# Running lumalinux side-by-side with CloudRedirect

CloudRedirect (Selectively11/CloudRedirect) targets the **cloud-save RPC
path** in `steamclient.so` via vtable swaps; lumalinux targets the
**content / depot path** via inline byte hooks. The two hooked sets are
disjoint, the mechanisms don't overlap, and they're designed to run side
by side.

> **Update (CloudRedirect v2.5.x, 2026-07).** CloudRedirect's Linux surface
> shrank: it dropped its Linux achievement injection, RecvPkt hook, and schema
> fetch (commit `31454e3`; SLSsteam now owns achievement-schema fetching) and
> removed the achievement-sync UI (`1095306`), leaving only its cloud-save and
> playtime hooks plus a native-stats export for SLSsteam. So the disjoint-surface
> premise holds and strengthens. The RecvPkt inline-detour rewrite (`71e0544`) and
> the manifest-endpoint override (`f3d9dfe`) in the same window are **Windows-only**
> and do not touch the Linux side or our GMRC path. Its LD_PRELOAD log fix
> (`0925cc2`: a global `std::mutex`/`std::string` used before C++ static init in an
> LD_PRELOAD constructor) also likely resolves the `cloud_redirect.so ->
> __backtrace -> ld.so` teardown `SIGSEGV` seen in a lumalinux coredump, so
> updating CloudRedirect to v2.5.4+ is worthwhile. lumalinux's own logger is not
> affected by that bug: its `g_logMutex` is a constexpr-constructed `std::mutex`
> (constant-initialized before any constructor) and it builds the log path with a
> local, not global, string.

## How CloudRedirect tracks Steam builds (RVA → signature resolver)

CloudRedirect used to be rigidly tied to a single Steam build, then stopped
being so. This matters for any "is CR compatible with the build I run?" gate:
the answer is **no longer** a lookup in a hardcoded list.

Timeline, traced from the `SC_RVA_GLOBAL_ENGINE` constant in
`src/common/steam_kv_injector.cpp` across release tags:

| Release | `SC_RVA_GLOBAL_ENGINE` | What the release did |
| --- | --- | --- |
| v2.1.8 | `0x17A70E8` | hardcoded offset |
| v2.1.9 | `0x17A70E8` (same) | that build didn't move the function |
| v2.2.1 | `0x17CC738` (**changed**) | new build → **re-hardcode the offset by hand** |
| **v2.2.2** | `0x17CC738` (same) | **added the resolver** (see below) |
| v2.2.3 | `0x17CC738` (same) | "official support for 1782437068" **without touching the RVA** |
| v2.6.2 | *(removed)* | **deleted the hardcoded RVAs entirely** |

- **≤ v2.2.1 — per-build treadmill.** Supporting a new build meant hardcoding
  new RVAs; when Steam moved a function (v2.2.1: `0x17A70E8`→`0x17CC738`) they
  had to ship a release with the new numbers. This is the "requires an update
  for every Steam client update" era.
- **v2.2.2 — the pivot ("CR no longer requires an update for every Steam client
  update").** They added a signature/RTTI scanner (`sig_scanner.cpp`,
  `sc_resolver`) and routed every address through
  `OvResolve(override, base, rva)` / `SC_RESOLVE(field, rva)` = **prefer the
  signature-scanned address; fall back to the hardcoded RVA only if the scan
  fails.** The version gate also became non-blocking (the patcher logs the
  detected build and continues instead of refusing). RVAs stayed as a safety
  net.
- **v2.2.3 — proof it works.** "Official support for the June 26th update
  `1782437068`" shipped with the RVA unchanged (`0x17CC738`) — the scanner
  resolved the new build on its own. The only concrete change was adding the
  build number to the cosmetic `SupportedSteamVersions` array.
- **v2.6.2 — full migration.** Hardcoded RVAs deleted; the resolver is now
  mandatory (`Configure()` / "resolver addresses required"), no fallback.

**Consequence for compatibility reasoning.** From v2.2.2 on, CR resolves its
hooks by signature at runtime — the same model SLSsteam and lumalinux already
use. "Does CR version *C* support Steam build *X*?" is therefore a
**signature-resolution** question (retrocompatible across builds; only breaks
if Steam changes the *shape* of the code, which needs a signature update, not a
per-build release), **not** a lookup in `SupportedSteamVersions`. That array is
a non-blocking, cosmetic (and drift-prone — it did not change in v2.6.2) layer;
do not treat its contents as an authoritative supported-builds list.

**The Linux `.so` is even more build-tolerant — it's RTTI-anchored, not
offset-based.** The RVA→resolver timeline above is the *Windows* path
(`src/platform/win/`, `steamclient64.dll` RVAs). The Linux hook
(`src/platform/linux/vtable_hook.cpp`) never used offsets at all: it locates
`steamclient.so` in memory via `/proc/self/maps`, finds the Itanium-ABI RTTI
type-name string for the target class
(`RTTI_NAME = "30CClientUnifiedServiceTransport"`), walks to its `typeinfo`,
then scans for the vtable whose header is `[offset_to_top=0, typeinfo_ptr]` and
swaps the function pointers. There is **no** hardcoded `steamclient` offset, **no**
`SupportedSteamVersions` check, and **no** build comparison anywhere in the Linux
path. Because it resolves the vtable by the C++ **class name** — a semantic
identifier that survives recompilation, refactors, and offset moves — a normal
Steam build update does not affect it. It breaks only on a *structural* change:
Valve renaming the class, altering its vtable layout (add/remove/reorder virtual
methods), or stripping RTTI (they don't). So on Linux the version lag is moot:
the 2.6.1/2.6.2 work was Windows RVA/resolver plumbing that the RTTI-based `.so`
does not need — CloudRedirect at 2.6.0 keeps resolving new builds by class name
regardless.

For a LumaDeck component-update design this means CloudRedirect does **not** fit
the hash-allowlist model the way SLSsteam/lumalinux `updates.yaml` does (CR
publishes no per-build hash list), and it does **not** need to: on Linux the
CloudRedirect Flatpak self-updates via `flatpak update` against its own OSTree
remote (`checkForFlatpakUpdate` / `applyFlatpakUpdate` in `ui-linux/backend.cpp`),
and the `.so` + Steam pin are curated upstream by Headcrab/Selectively11. The
Linux release channel currently tops at **2.6.0**; 2.6.1/2.6.2 are Windows-only.

## Install order

`setup.sh` installs the whole stack in one run and the wrapper loads whichever
`.so`s are present, so there's no manual "CR before lumalinux" ordering to get right
any more.

1. Run the installer, from **desktop mode** (its Steam restarts can trip Game Mode's
   crash-loop detector). It fetches SLSsteam + `library-inject` + **CloudRedirect**
   (`cloud_redirect.so` + `cloud_redirect_cli` + the `org.cloudredirect.CloudRedirect`
   Flatpak) + netsock + `liblumalinux.so`, writes the injection wrapper, and wires
   coverage:

   ```bash
   curl -fsSL https://raw.githubusercontent.com/jayool/lumalinux/main/setup.sh | bash
   ```

2. **CloudRedirect is enabled for you.** `setup.sh` sets `DisableCloud: no` in
   `~/.config/SLSsteam/config.yaml` (CloudRedirect ships as a core component — the
   `.so` download is fatal if it fails, so cloud is never left on without it). No
   manual config edit needed.

3. **Open the CloudRedirect Flatpak app once** to sign into your cloud provider
   (switch to desktop for this). CloudRedirect is inert until a provider signs in.

## How they coexist in the wrapper

Injection now comes from the wrapper at `~/.local/share/SLSsteam/path/steam` (not a
patched `steam.sh`). Before `exec`ing the real Steam, the wrapper builds the env from
whichever `.so`s are on disk (paraphrased):

```sh
# LD_AUDIT: library-inject first, then SLSsteam
export LD_AUDIT="$SLS_DIR/library-inject.so:$SLS_DIR/SLSsteam.so"

# LD_PRELOAD: prepend CloudRedirect then lumalinux (so lumalinux ends up FIRST),
# each only if its .so exists; libextest is appended after, Wayland only.
for _p in "$CR_SO" "$LL_SO"; do
    [ -f "$_p" ] && LD_PRELOAD="$_p${LD_PRELOAD:+:$LD_PRELOAD}"
done
export LD_PRELOAD
exec "$STEAM_BIN" "$@"
```

Final result inside the Steam process (both present):

```
LD_PRELOAD=/home/<user>/.local/share/lumalinux/liblumalinux.so:/home/<user>/.local/share/CloudRedirect/cloud_redirect.so
```

Both `.so`s load. lumalinux comes first in the chain so its symbols shadow anything
CR also provides, but the two hook disjoint functions in `steamclient.so`, with no
symbol-resolution conflicts observed. If CloudRedirect's `.so` isn't on disk the loop
simply skips it and Steam runs with lumalinux only — no `steam.sh` block-insertion
and no LD_PRELOAD-clobber to work around any more.

## Verifying both loaded

After restarting Steam:

- **lumalinux**: `~/.cache/lumalinux/lumalinux.log` ends with
  `3/3 hooks active` (or `4/4` if you set `LUMA_LOADPKG_DEBUG=1`).
- **CloudRedirect**: `~/.config/CloudRedirect/cloud_redirect.log` ends
  with `cloud_redirect.so active in process 'steam' (pid=…)`.

You can also read the live env of the running `steam` process:

```bash
PID=$(pgrep -f 'Steam/ubuntu12_32/steam ' | head -1)
tr '\0' '\n' < /proc/$PID/environ | grep -E '^LD_(PRELOAD|AUDIT)='
```

`LD_PRELOAD` should contain both `liblumalinux.so` and
`cloud_redirect.so`, in that order.

## When only one of the two appears

If lumalinux loads but CloudRedirect doesn't (or vice-versa), the missing `.so`
isn't on disk — the wrapper only adds a preload for a file that exists. Re-run the
installer to re-fetch it; the other component is untouched.

```bash
curl -fsSL https://raw.githubusercontent.com/jayool/lumalinux/main/setup.sh | bash
```

(If *neither* loads — no banner at all — that's the wrapper not being reached, not a
missing `.so`; see `maintenance.md` case B.)

## Stats sync: el arranque en frío deja los juegos a cero logros (fix en lumalinux v0.21.1, 2026-09-20)

### Qué hace CloudRedirect con `stats_sync_enabled`

Para cada juego añadido por lua, CR mantiene una **base propia** de stats y logros
(`~/.config/CloudRedirect/storage/stats/<cuenta>/<app>.json` en disco, y un único
blob por cuenta en Drive). No inventa nada: la llena copiando lo que Steam guarda
en `appcache/stats/` (`UserGameStatsSchema_<app>.bin` = lista de logros,
`UserGameStats_<cuenta>_<app>.bin` = progreso), ficheros que Steam escribe
**después** de recibir el schema del servidor. En Linux, con SLSsteam, ese schema
llega por el truco de SLSsteam: reenvía `Player.GetUserStats#1` con el SteamID de
un reseñador real del juego, Valve devuelve su schema, SLSsteam tira su progreso
y se queda con la lista (`src/feats/achievements.cpp`, `sendAndRecvGetPlayerStats`).

CR engancha esa misma llamada (`platform/linux/cloud_hooks.cpp:596`) y para los
juegos lua **contesta él** desde su base (`StatsHooks::TryHandleGetUserStats` →
`StatsHandlers::HandleGetUserStats`), sin dejar que la petición salga. Compara el
crc que manda el cliente (huella de sus stats locales) con el de su base:
distintos → manda schema + stats; iguales → "al día", sólo el crc, 2 bytes.

### El bug [leído en `common/stats_handlers.cpp:34-60`, probado en Deck]

Un juego que CR nunca ha visto tiene base vacía → crc 0 (`ComputeCrcLocked` sólo
mezcla stats y logros desbloqueados). Un cliente sin stats locales manda crc 0.
0 == 0 → "al día" → 2 bytes (`10 00`: campo 2 varint 0) → **el juego nunca
recibe su schema, la petición nunca llega a SLSsteam ni a Valve**, y sin schema no
puede desbloquear nada. Y la base no se llena porque se llena del blob nativo
que Steam escribe al recibir el schema. Bucle cerrado.

La rama correcta existe: `TryHandleGetUserStats` devuelve `false` (passthrough,
log `store returned empty -> passthrough`) si el cuerpo tiene **0 bytes**. Pero
"al día" siempre tiene 2. Descuido, no decisión: en un juego jugado antes de
instalar CR el blob nativo ya existe, CR lo importa al arrancar, el crc no es 0 y
todo funciona — que es como el autor lo habrá probado.

Datos de una Deck (CR 2.6.5, SLSsteam 2026-09-03, lumalinux 0.21.0), log de CR:

```
[Stats] GetUserStats app=2545360 clientCrc=0
[Stats]   app=2545360 up-to-date (crc=0); sending crc-only no-op
[Stats] GetUserStats app=2545360 handled locally (2 bytes)
```

- Cinco arranques del mismo juego, incluida una sesión de 45 min: siempre eso,
  cero logros. Con `stats_sync_enabled: false` (o CR fuera) los logros saltan.
- Cuatro de cinco juegos gestionados en ese estado (2545360, 2524850, 1942280,
  1875580). 2524850 tenía el **schema en disco** desde dos días antes y CR
  siguió contestando 2 bytes: la condición no es "haberlo jugado", es **tener al
  menos un logro o stat con valor** en local cuando CR arranca.
- El quinto (711540) se jugó con el sync apagado, desbloqueó algo, y al
  encenderlo CR importó el blob: `up-to-date (crc=2292680116)`, y en los siete
  arranques siguientes `Sending schema (97923 bytes)` + `Returning 2..3 stats`
  con crc cambiante. **La ruta "base llena" funciona**; sólo falla la vacía.
- Consecuencia práctica antes del fix: sincronizar logros Deck↔Windows sólo
  funcionaba tras jugar cada juego primero con `sync_achievements: false`.

Un detalle sin cerrar: 2524850 obtuvo su schema con el sync **encendido**. En
Linux CR sólo engancha la llamada unificada; el mensaje antiguo
`ClientGetUserStats` (EMsg 818) lo engancha SLSsteam y CR no
(`HandleLegacyGetUserStats` sólo se usa desde `platform/win/`). Candidato
[inferido], no probado.

### El fix: interposición de símbolo, sin tocar un byte de CR (`src/cr_stats_fix.cpp`)

`cloud_redirect.so` se enlaza con `-s` (sin `.symtab`) pero **sin**
`-fvisibility=hidden`, así que `.dynsym` conserva todas las funciones, y sus
llamadas internas van por la PLT (visto en el desensamblado de la 2.5.2 de
moon; en la 2.6.5 de la Deck, `eu-readelf -r … | grep -c HandleGetUserStats` →
2). Como el wrapper pone `liblumalinux.so` **antes** que `cloud_redirect.so` en
`LD_PRELOAD` (`setup.sh:885`), basta con que lumalinux defina
`_ZN13StatsHandlers18HandleGetUserStatsEjRKSt6vectorIN2PB5FieldESaIS2_EE`: el
enlazador dinámico ata la llamada de CR a la nuestra.

La nuestra es un stub en ensamblador i386 que reenvía los tres slots de pila
(puntero oculto del resultado, `appId`, `fields&`) a la función real (resuelta con
`dlsym(RTLD_NEXT)`), y al volver mira el resultado: si sus primeros 12 bytes son
un `std::vector<uint8_t>` vivo de **exactamente** `10 00`, lo vacía
(`end = begin`, sin realocar). CR ve `Size()==0` y toma **su propia** rama de
passthrough. Cualquier otro cuerpo — "al día" con crc ≠ 0, o schema + stats
sembrados desde Drive en una segunda máquina — se devuelve intacto.

Auto-comprobaciones en vez de lista de versiones (todas en `CrStatsFix::Init`):

1. El símbolo exacto resuelve en `cloud_redirect.so` (el nombre codifica los
   parámetros; si cambian, no se encuentra → `Disabled`).
2. La función real termina en `ret $4` dentro de su tamaño de `.dynsym`
   (`dladdr1` + `RTLD_DL_SYMENT`): sigue devolviendo la estructura por puntero
   oculto que el callee saca de la pila, que es lo que el stub reenvía.
3. `dlsym(RTLD_DEFAULT)` devuelve **nuestro** stub (dirección tomada por un alias
   oculto, no por el nombre interponible, que en PIC resolvería por la GOT a lo que
   el proceso haya atado). Si no, `LD_PRELOAD` está reordenado y se anota `Failed`.

Y por llamada: `process_vm_readv` sobre el propio pid prueba que los 12 bytes y
los 2 del cuerpo son legibles antes de tocarlos (falla con `EFAULT` en vez de
segfault). Kill switch `LUMA_NO_CR_STATS_FIX=1`. Como el símbolo lo define
lumalinux siempre, "apagado" es "reenviar sin mirar", no "no existir".
`status.json` → `CrStatsFix: installed | disabled | failed`.

Self-test de 32 bits con un CR falso de firma idéntica:
`tools/cr_stats_fix_selftest/run.sh` (seis escenarios: sin interposer, armado,
segunda ronda, kill switch, CR delante en `LD_PRELOAD`, log una vez por app).

### Verificar en la Deck

1. `"sync_achievements": true` en `~/.config/CloudRedirect/config.json`, reiniciar Steam.
2. Log de lumalinux: `CR-stats: armed — CloudRedirect 2.6.5, …`. Si sale
   `CR-stats: FAILED — …`, la línea dice qué comprobación falló.
3. Lanzar un juego a cero (schema o no). Log de CR:
   `[Stats] GetUserStats app=N: store returned empty -> passthrough` en vez de
   `crc-only no-op`; log de lumalinux: `CR-stats: app=N — … cleared …` (una vez).
   Desbloquear un logro: tiene que saltar.
4. Lanzar un juego con base llena: sigue `Sending schema … Returning N stats`.

### Parche upstream (propuesto a Selectively11)

`common/stats_handlers.cpp`, `HandleGetUserStats`, antes de construir la
respuesta: si la base del juego no tiene ni stats ni logros
(`stats.crcStats == 0`), devolver `RpcResult()` (cuerpo vacío). `TryHandle…`
ya convierte eso en passthrough. Cuando lo incorpore, el interposer de lumalinux
ve 0 bytes y no hace nada.

## Cloud sync: Steam se congela al subir un lote grande (CR 2.6.5 Linux, incidente 2026-09-16/18)

Incidente real en una Deck (SteamOS 3.8.16, CR 2.6.5, SLSsteam 2026-09-03,
lumalinux 0.21.0, Google Drive). Todo lo de abajo está respaldado por logs de esa
Deck salvo donde se marca [inferido]. Dos días de diagnóstico; no repetirlos.

### Síntomas

Al salir de un juego con muchos ficheros de guardado (Lonely Mountains: Snow
Riders, 2545360, ~100 replays por sesión bajo `Ghosts/`), Steam se queda en
"Sincronizando con la nube": la interfaz se congela, los botones dejan de
responder, la tasa de descarga marca 0 B/s, el apagado desde Game Mode no
responde, y hay que reiniciar a mano. Tras el reinicio, "verificando
instalación". Los demás juegos añadidos tardan más de lo normal en salir (unos
13 s), pero no se cuelgan.

### Causa (leída en el código de CR 2.6.5 y confirmada con los tiempos del log)

CR intercepta el RPC `CompleteAppUploadBatchBlocking` de Steam
(`common/rpc_handlers.cpp:1948-1966`): lanza la promoción del lote en un hilo y
**espera en el hilo de Steam** con `CoopYield::PumpUntil`. Ese `PumpUntil`
(`common/coop_yield.cpp:38-49`) sólo cede el turno al bucle principal de Steam si
hay un "yield hook" registrado; si no, es un bucle de `sleep 2 ms` hasta que el
hilo termina. El propio comentario del autor: *"Degrades to a plain spin off
Steam"*. Y el hook (`SetYieldHook(&CooperativeYieldCurrentJob)`, que resuelve
`CJob::BYieldIfTimeSlice` por RVA) **sólo existe en `platform/win/cloud_intercept.cpp:1001`**.
En Linux no hay equivalente, así que `PumpUntil` y `LockCooperatively` son spins
puros en el hilo de Steam.

Steam tiene un vigilante: si `BMainLoop` no avanza en 15 s, aserta
(`BMainLoop stalled > 15 s` en `journalctl -b -N`, visto en el arranque del
16-09). A partir de ahí la interfaz está muerta aunque el proceso siga vivo.

La promoción del lote son llamadas a Drive **por fichero**: comprobación de
existencia (~7 ficheros/s) y subida (~1.1 ficheros/s), en lotes de 100, que es
el máximo de Steam. Tiempos del log de CR de esa Deck:

| Fase | Lote de 100 replays | Umbral |
|---|---|---|
| Comprobación (CAS) | 13.5 – 15.8 s | 15 s → ya roza el vigilante |
| Subida | 85 – 90 s | 15 s → cuelgue seguro |
| Cadena de salida de sesión (cualquier juego) | ~12 llamadas a Drive en serie, ~13 s | por debajo, pero se nota |

El mismo bloqueo de 90 s aparece en el historial de `UploadBatch` del 16 de
agosto, con versiones anteriores de todo el stack: **no es nada nuevo de
lumalinux 0.21.0 ni de las releases de septiembre.**

Efecto secundario que convierte el cuelgue en bucle: CR difiere la publicación
del lote a la liberación de sesión ("barrier at session release"). Si Steam
muere a mitad, esa publicación no llega. Tras el cuelgue del 16-09 a las 21:53 la
nube se quedó en changelist 8 y el local siguió subiendo hasta 15, con conflicto
en `BestTimes.json` en cada arranque, y cada arranque volvía a intentar el lote
de 100. Las 13 líneas de "another session active" son las propias sesiones
muertas de la Deck (un clientId por arranque de Steam), no otra máquina.

### Lo que se descartó, con la prueba

- **GMRC / lumalinux**: el log de lumalinux no muestra actividad de GMRC en la
  ventana del cuelgue, y con `LUMA_NO_GMRC=1` se cuelga igual.
- **El pin de libcurl** (`src/libcurl_pin.cpp`): con `LUMA_NO_LIBCURL_FIX=1` CR
  da `curl failed: 60` (certificado) con la libcurl del runtime de Steam y va
  peor. El pin es necesario, no culpable.
- **Que fuera nuevo**: el bloqueo de 90 s ya estaba el 16 de agosto.
- **Que el checkbox de Steam Cloud del juego no sirviera**: CR devuelve `true`
  en `IsCloudEnabledForApp` para los juegos lua, pero la casilla por juego de
  Steam la respeta igual: `cloud_log.txt` dice
  `Starting sync (AC Exit,Sync Disabled,)` y no sube nada.

### Qué hacer hoy

- **Por juego**: desmarcar "Mantener los archivos guardados en Steam Cloud" en
  las propiedades del juego. Probado: la sincronización se salta y el juego
  arranca y cierra normal. CR no tiene exclusión por app en su `config.json`.
- **Global**: el fichero `~/.config/CloudRedirect/disable` apaga CR entero.
- Las mitigaciones desde nuestro lado (aviso en LumaDeck, toggle de cloud por
  juego, tocar `config.json`) se consideraron y se descartaron como parches
  sobre un bug ajeno. El arreglo es de CR.

### Lo que hay que pedirle a Selectively11

1. Un yield hook en Linux equivalente al de Windows (localizar
   `CJob::BYieldIfTimeSlice` en `steamclient.so`, por RTTI o patrón, como hace
   en Windows por RVA), o, si no, hacer la promoción **fuera** del RPC bloqueante
   y contestar a Steam antes de los 15 s.
2. Que la publicación diferida sobreviva a una sesión muerta (hoy queda en
   changelist vieja y cada arranque reintenta el lote entero).
3. Exclusión por app en `config.json`.
4. Y el bug de stats sync de la sección anterior, con su parche de una línea.

## Re-barrido 2026-08-18 — v2.6.2 → HEAD (`bc5e38a`)

*26 commits, del 2026-07-22 a hoy: releases v2.6.3, v2.6.4 y v2.6.5. La tabla de
`SC_RVA_GLOBAL_ENGINE` de arriba ya cubría hasta v2.6.2, así que esa es la
frontera real, no la ventana de julio del bloque "Update".*

**La premisa de superficies disjuntas sigue intacta.** Nada de esta ventana toca
el camino de contenido/depots. El grueso son playtime y last-played (`4c531ff`,
`5c0c36f`, `531072f`, `c680fd5`, `784a506`), almacenamiento S3/R2, y la retirada
progresiva de features de SteamTools (`75b87cf`, `bc5e38a`).

### CloudRedirect pasa a MIT — antes no tenía licencia

`2c7f89e` (2026-08-18) añade `LICENSE` con el texto MIT: **+21 líneas, cero
borrados.** Es decir, hasta hoy el repo estaba **sin licencia**, que por defecto
significa "todos los derechos reservados" — no era legalmente reutilizable. A
partir de v2.6.5 sí lo es, con atribución. Relevante si alguna vez se quiere
tomar algo prestado de ahí.

### Accionable en LumaDeck: `_account_id()` elige mal el usuario sin `MostRecent`

`9d9260d` (PR externo de `ciscosweater`) cambia el parseo de `loginusers.vdf` de
"busca `MostRecent "1"`" a una cascada **`MostRecent > AutoLogin > Timestamp`**.
Que venga de un PR de fuera dice que alguien se lo encontró en una máquina real.

`LumaDeck backend/achievements.py:139-146` tiene el mismo patrón sin la cascada:

```python
if best is None:
    best = sid                      # fallback: el PRIMERO del fichero
if re.search(r'"MostRecent"\s*"1"', body):
    best = sid; break
```

Sin `MostRecent "1"` se queda con el primer bloque del VDF, que es orden
arbitrario, no recencia. Como `_account_id()` alimenta el nombre del fichero
`UserGameStats`, elegir mal escribe los logros en el fichero de otra cuenta y no
aparecen. **Prioridad baja** — en una Deck lo normal es una cuenta y `MostRecent`
suele estar presente; es un arreglo para multi-cuenta. No aplicado.

### Verificado y NO aplicable

- **`ea671c8` "Watch SLSSteam config in a smarter way"** — CR pasa de
  `inotify_add_watch(configPath, IN_MODIFY)` a vigilar el **directorio** con
  `IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE`. Es el mismo fallo que SLSsteam
  arregló en `1444fa5` (ver `slssteam-analysis.md` §4.1): vigilar el fichero con
  `IN_MODIFY` no ve una escritura por rename atómico. Tercer proyecto en
  tropezar con ello. **lumalinux ya lo hace bien** — `key_store.cpp:181` vigila
  el directorio con esa máscara exacta, y lo documenta. Nada que hacer.
- **`eec2f2f` detección de "unlock solution"** — detecta `LumaCore.dll` y
  recomienda HubcapTools, pero vive entera en `ui/` (el WPF de Windows); no
  aparece en `src/platform/linux/` ni `src/common/`. El `.so` de Linux no gatea
  nada, así que no hay riesgo para la coexistencia.
- **`2251593` ruta `~/.steam/debian-installation/`** — LumaDeck sólo contempla
  `.local/share/Steam` y `.steam/steam`. Esa ruta es de escritorios
  Debian/Ubuntu; irrelevante en SteamOS salvo que algún día importe el Decky de
  escritorio.
