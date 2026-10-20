# Steam de 64 bits (steamrt3c) — qué cambia para la pila

Doc de medición, 2026-10-16, ampliado el 2026-10-20 (canales de la Deck y el
build de 64 bits). Se fusionará con `nosotros.md`, `slssteam.md` y
`cloudredirect.md` cuando el port tenga forma; hasta entonces vive aquí.

## §0 Ficha

| | |
|---|---|
| **Qué es** | El cliente de Steam para Linux pasa a ser una aplicación de 64 bits que corre dentro de un contenedor de Steam Runtime 3c (la misma maquinaria de pressure-vessel que usan los juegos). Anunciado en las notas de la beta del 2026-10-15 ("The Steam client is now a 64bit application. The Steam client now runs inside a Steam Runtime container."). Existía como opción desde marzo de 2026 ("Steam RT3 Experimental" en Ajustes, marcador `~/.local/share/Steam/.steam-enable-steamrt64-client`); la beta del 15 lo hace el modo por defecto. |
| **Medido en** | codespace SteamOS (`deck@codespaces-67b581`). 2026-10-16 06:40-07:10 UTC: canal `publicbeta` de escritorio, paquete `steam_client_publicbeta_steamrt64` `1792110651` (junto a `steam_client_publicbeta_ubuntu12` `1792108788`). 2026-10-20 08:50-09:10 UTC: canal beta de la Deck, `steamrt64/steamclient.so` 53.559.264 bytes, sha256 `947e5a192e7f6f12…`, con la `liblumalinux.so` de 64 bits del job `build-linux-x86_64` (run 39398060496) cargada dentro. |
| **Dónde está hoy** | En la Deck, `steamdeck_publicbeta` ya publica `steamrt64` **por delante** del de 32 (`1792192922` frente a `1792108788`): Valve itera el cliente de 64 en la beta de la Deck. `steamdeck_stable` publica los dos paquetes con la misma versión (`1791918634`) y la Deck estable sigue arrancando el de 32 (es el paquete del opt-in "RT3 Experimental"). Lo que está en la beta de la Deck llega a estable con la siguiente SteamOS, semanas o un par de meses. |
| **Lo que no sabemos** | Cuándo `steamdeck_stable` arranca el de 64 por defecto. Si Valve conserva el cliente de 32 bits como alternativa. Qué hará Ace con SLSsteam. |
| **Fuentes** | Notas de la beta 2026-10-15; `ValveSoftware/steam-for-linux` `README.beta.md` (requisitos) e issue #13015 (hilo del `reaper` de 32 bits, con el comentario de lostgoat, Valve, del 2026-10-15); hilo de r/linux_gaming del mismo día; manifiestos `client-update.fastly.steamstatic.com/steam_client_<canal>_<ubuntu12|steamrt64>` (§4). |

## §1 Lo medido

Todo en el codespace. §1.1 con el cliente beta de escritorio `1792110651`
(2026-10-16); §1.2 con el de la beta de la Deck (2026-10-20).

### §1.1 El cliente nuevo

**Disposición en disco.** El cliente vive en `steamrt64/` (nuevo), no en
`ubuntu12_64/`; `steamrt32/` existe también con su propio `steamclient.so`
(56.228.716 bytes) que no carga el cliente. Los enlaces de `~/.steam` cambian:
`bin32 → steamrt32`, `bin64 → steamrt64`, `sdk32 → linux32`, `sdk64 → linux64`.
`steamrt64/steamclient.so`: 53.563.360 bytes, sha256 `5759ff1d0b374692…`.
`steamrt64/crashhandler.so` existe y carga.

**Cadena de arranque.** `steam.sh` → `steamrt64/steam` (fase de actualizador:
`ReexecToLauncher: steamrt3 client enabled`, luego `ReexecSteamLinux: received
re-exec request with code: 67`) → `srt-logger` → `srt-client-container` →
`srt-bwrap` → `pv-adverb` → `steamrt64/steam -gamepadui` dentro del contenedor.
El cliente de dentro ya no escribe en `/tmp/steam.log` (ahí solo quedan los
avisos de `ld.so`); su salida va por `srt-logger` a `logs/console-linux.txt`.

**El contenedor.** `PRESSURE_VESSEL_RUNTIME=steamrt3c_platform_3c.0.20261007.263964`,
`os-release` "Steam Runtime 3c (steamrt3c)", glibc **2.31**
(`Debian GLIBC 2.31-13+deb11u14`, Debian 11), `PRESSURE_VESSEL_ARCHITECTURES=x86_64-linux-gnu:i386-linux-gnu`,
`PRESSURE_VESSEL_COPY_RUNTIME=1`. `$HOME` montado dentro
(`~/.local/share/lumalinux` visible desde el proceso del cliente);
`XDG_RUNTIME_DIR` pasa. En disco hay tres plataformas del runtime (julio,
agosto y octubre de 2026) bajo `steamrt64/pv-runtime/steam-runtime-steamrt/`.

**Requisitos del entorno.** El lanzador del contenedor exige un bus de sesión
de D-Bus: sin él, `steam-runtime-launcher-service: E: Can't find session bus:
Could not connect: Connection refused` → `srt-client-container: Child process
exited with code 69` → `steam.sh` sale con **125**. El supervisor del codespace
lo relanzó en bucle y paró. Con `dbus-daemon --session --fork --print-address`
exportado en `DBUS_SESSION_BUS_ADDRESS`, arranca. Namespaces de usuario sin
privilegios (`unprivileged_userns_clone=1`, `bwrap` setuid) ya estaban. Es la
lista de `README.beta.md`: glibc ≥ 2.31 en el sistema, kernel ≥ 5.6,
userns y mount ns sin privilegios, bus de sesión, X11, PulseAudio o
pipewire-pulse, multilib obligatorio.

**Nuestras `.so` de 32 bits.** El loader de 64 las descarta, tres por arranque:

```
ERROR: ld.so: object '…/SLSsteam/SLSsteam.so' cannot be loaded as audit interface: wrong ELF class: ELFCLASS32; ignored.
ERROR: ld.so: object '…/lumalinux/liblumalinux.so' from LD_PRELOAD cannot be preloaded (wrong ELF class: ELFCLASS32): ignored.
ERROR: ld.so: object '…/CloudRedirect/cloud_redirect.so' from LD_PRELOAD cannot be preloaded (wrong ELF class: ELFCLASS32): ignored.
```

Son avisos: Steam arranca limpio, sin hooks, sin crash. Sin propiedad falsa no
hay juegos añadidos; el guard no interviene porque no hay crash.

**La inyección sigue entrando.** Con dos bibliotecas de 64 bits de prueba
(un constructor y un `la_version` que escriben pid y `comm` en `/tmp`),
lanzadas con `LD_PRELOAD` y `LD_AUDIT` desde fuera: `pv-adverb` recibió
`--ld-preload=…/libpre64.so --ld-audit=…/libaud64.so`, el cliente de dentro
(`steamrt64/steam -gamepadui`, pid distinto del lanzador) tenía las dos
variables en su entorno, y las dos bibliotecas cargaron en él (`comm=steam`).
El mismo camino que usamos hoy vale con el cliente nuevo; solo cambia la
arquitectura de la `.so`.

**Las anclas de nuestros hooks están en el binario de 64**, con los mismos
recuentos que en el de 32:

| Ancla | 64 | 32 | La usa |
|---|---|---|---|
| `21IClientConfigStoreMap` | 1 | 1 | DepotKey por nombre |
| `12CConfigStore` | 2 | 2 | DepotKey por nombre |
| `GetBinary` | 6 | 6 | DepotKey por nombre |
| `Software\Valve\Steam\Depots` | 1 | 1 | DepotKey |
| `GetManifestRequestCode` | 8 | 8 | GMRC (xref de texto) |
| `BuildDepotDependency` | 1 | 1 | BuildDep |
| `shadercachedepot`, `Getting shader depot manifests` | 1, 1 | 1, 1 | ShaderDepot |
| `No package info for packageID` | 1 | 1 | finder de package-0 |
| `17LicensesUpdated_t` | 3 | 3 | Reconcile |
| tipos RTTI (`^\d+[CI]\w+$`) | 2111 | 2108 | — |

Los resolvedores por nombre (RTTI) y por ancla de texto sirven tal cual. Lo
que no sirve: los patrones de bytes, las RVAs del feed, las convenciones de
llamada (en 32 bits `thiscall` con registros; en 64 la ABI SysV) y los
layouts de estructuras (`CUser` `0xf90/0xfa4`, `AppIdVec`), que hay que
rederivar sobre `steamrt64/steamclient.so`.

**Volcados.** `crashhandler.so` de 64 carga y escribe en `/tmp/dumps`
(`assert_20261016065738_5.dmp` en el primer arranque). El crash guard seguiría
viendo crashes con el cliente nuevo.

**Lo que el hilo #13015 aporta.** El `reaper` que lanza los juegos sigue siendo
de 32 bits (Steam Runtime 1), y los juegos de 32 necesitan glibc y drivers de
32 del sistema; Valve (lostgoat, 2026-10-15) dice que un sistema solo de 64 no
está soportado. Nada de eso toca el proceso del cliente ni la inyección. Steam
verifica sus ficheros por tamaño y CRC32 (`-noverifyfiles` lo apaga); no nos
afecta porque no sustituimos ficheros de Steam.

### §1.2 Nuestra `.so` de 64 bits, dentro del cliente de 64

**Qué se compila.** Las mismas fuentes con `-DLUMA_X86_64=ON` (`CMakeLists.txt`):
`-m64`, libmem x86_64 (`LUMA_ARCH=x86_64 tools/fetch_libmem.sh`, tarball de
rdbo fijado por sha256), yaml-cpp 0.8.0 compilado desde el fuente estático y
PIC (`tools/build_yamlcpp.sh`, instalado en `lib/x86_64/`, ignorado por git) y
`src/compat_x86_64.cpp`. Tres sitios son solo de 32 bits y quedan fuera del
build de 64: los `static_assert` de layouts en `steam_types.hpp`, el stub en
ensamblador de `cr_stats_fix.cpp` (su `Init()` devuelve `Disabled`) y la
instalación de hooks en `main.cpp`, que en 64 declara `DepotKey`,
`ShaderDepot` y `GMRC` como `FAILED` y el resto `DISABLED`, avisa en el log y
en la notificación ("hooks not ported yet") y escribe el estado. Con eso
LumaDeck diría "Steam build not supported", que es la verdad.

**Dónde se compila y por qué.** El job `build-linux-x86_64` de `build.yml`
corre dentro de la imagen del SDK de Valve
(`registry.gitlab.steamos.cloud/steamrt/sniper/sdk`): el mismo Debian 11 del
contenedor, con su glibc 2.31 y su libstdc++ de GCC 10. En 32 bits nunca
importó dónde compilar porque el cliente usa la glibc del sistema (Arch,
moderna) y la Deck siempre tiene algo igual o más nuevo que el runner; en 64 el
cliente solo ve lo del contenedor, y una `.so` compilada en `ubuntu-latest`
pediría símbolos de glibc que ahí no existen. El job comprueba que el símbolo
`GLIBC_` más alto de la `.so` sea ≤ 2.31 (hoy 2.30) y sube el artefacto
`lumalinux-x86_64`; no se adjunta a las releases mientras no haga nada.

**Los tres fallos de enlace del entorno**, todos por lo mismo (el contenedor es
de 2020): el `libmem.a` precompilado de rdbo llama a
`std::__throw_bad_array_new_length`, que la libstdc++ de GCC 10 no exporta; el
shim `compat_x86_64.cpp` la define (oculta, lanza `bad_array_new_length`). El
`libyaml-cpp.a` de Debian 11 no es PIC y no entra en una `.so`; de ahí
compilarlo nosotros. En glibc 2.31 los pthreads van en `libpthread` aparte;
`Threads::Threads` en CMake (en 32 bits nunca saltó porque las glibc modernas
los integran).

**La carga, medida.** `liblumalinux.so` del artefacto, lanzada con
`LD_PRELOAD` desde fuera (`steam.sh -gamepadui`, sin `LD_AUDIT`): el cliente
de dentro (`steamrt64/steam -gamepadui`, pid 28780) la tiene en `/proc/maps`;
el log escribe "lumalinux loaded (pid=28780)", "Ctor: steamclient.so
detected", el hash `947e5a19…` del `steamclient.so` de 64 y el aviso de hooks
no derivados; `/tmp/steam64.log` sin ningún "cannot be preloaded". Punto 2 de
§3 cumplido: la puerta se abre; lo de dentro es el punto 3.

**Lo que la prueba dejó ver.** `status.json` no apareció en
`/run/user/<uid>/lumalinux/` del host: dentro del contenedor `XDG_RUNTIME_DIR`
pasa pero no es el mismo árbol de ficheros, así que la ruta del estado (y con
ella la lectura de LumaDeck) es parte del punto 4. Y un Steam lanzado a mano
desde el terminal, sin las variables de render por software que exporta el
supervisor del codespace, arranca pero no pinta la ventana; para verlo hay que
volver a `~/start-gamemode.sh`.

### §1.3 Los hooks en el cliente de 64 (2026-10-20)

**Derivación, sin ejecutar nada.** Sobre `steamrt64/steamclient.so`
`947e5a19…` (beta de la Deck `1792192922`, build-id `2c850bc9…`), con las
relocaciones `R_X86_64_RELATIVE` aplicadas (173.505; en 64 las vtables van
en `.rela.dyn`, el fichero lleva ceros) y `.eh_frame_hdr` (143.381
funciones). Las anclas de 32 valen todas; solo cambia cómo se leen (`lea
reg,[rip+disp32]` en vez de `lea reg,[got+disp32]`, slots de 8 bytes):

| Hook | RVA | Cómo | En 32 |
|---|---|---|---|
| DepotKey (accessor `CConfigStore::GetBinary`) | `0x11e5fd0` | por nombre: "GetBinary" → slot 6 de `IClientConfigStoreMap` → `CConfigStore`; el slot 7 es la sobrecarga | slot 6 |
| GMRC | `0x13a4960` | único `lea` de `ContentServerDirectory.GetManifestRequestCode#1`; un llamador | ancla de texto |
| ShaderDepot (`GetShaderCacheDepot`) | `0x10a0d20` | único `lea` de `shadercachedepot`; su único llamador es el job "Getting shader depot manifests"; lee `[global+0x7c]` (era `+0x44`), `test`, `ret 0` | igual |
| Reconcile (`CUser::NotifyLicensesUpdated`) | `0x18d9e00` | publica el callback 125, lee `[rdi+0x23c4]` (era `0x1b14`), sale si ≤ 0: uno solo entre 55 publicadores | criterios de moon |
| BuildDep | `0x106f840` | único `lea` de `BuildDepotDependency` | apagado por defecto |
| LoadPackage | `0x10521a0` | lee el símbolo global de "appids" que rellena el inicializador `0x1055f20` | solo diagnóstico |

Firmas SysV: DepotKey `(rdi=pObject, esi=foo, rdx=KeyName, rcx=Key,
r8d=KeySize)`, sin cambio de declaración; **GMRC pasa a seis parámetros**,
el manifest viaja entero en `rcx` (`gmrc_hook.cpp` bajo `LUMA_X86_64`);
ShaderDepot `(rdi=appinfo)`; Reconcile `(rdi=CUser*)`; BuildDep seis en
registros y dos en pila.

**Cache de paquetes** (de `CPackageInfoCache::GetPackage`, la función de "No
package info for packageID"): el puntero al objeto está en un slot fijo de
`.bss` (`0x331f210`, lo leen 2.235 funciones: es el singleton del cliente)
que se lee relativo a `rip`, sin GOT ni desplazamiento; raíz del árbol en
`+0x14b0`, array de nodos en `+0x14c8`, nodo de `0x20` bytes (`shl rax,5`):
`+0x00` izquierda, `+0x04` derecha, `+0x10` packageId, `+0x18` `PackageInfo*`.
`PackageInfo` (de las escrituras de `LoadPackage`): `+0x04` ChangeNumber,
`+0x1c` SHA-1, dos punteros en `+0x30`/`+0x38`, **`AppIdVec` en `+0x40`**
(`CUtlVector` de 24 bytes: `m_pMemory` +0, alloc +8, grow +0xc, `m_Size`
+0x10), `DepotIdVec` en `+0x58`.

**Lo que corre en el runtime de 64** (todo bajo `LUMA_X86_64`, el de 32 sin
tocar): resolución **solo por feed**. `res/rvas/947e5a19….yaml` lleva los
cuatro RVAs y, para el finder, `cache_global_rva` + `cache_root_off` /
`cache_nodes_off`; `vaddr_xlate` lee cabeceras ELF64 y coloca una dirección
de `.bss` por el sesgo de carga (`ToRuntimeData`); el finder usa el layout
de 64 y no escanea; los patrones de bytes y los tres rescates (xref de
cadena, ancla del callback, RTTI por nombre) devuelven 0 (son formas de
código i386), así que **sin fichero en el feed no se instala nada y se
reporta FAILED**. `proc_filter` solo admite `steam`: la `.so` de 64 entraba
en cada subproceso de `steamwebhelper` y lo hacía morir y renacer cada 10 s
(13 asserts de CEF en 120 s contra 1 vanilla; con el filtro, 1).

**libmem y el trampolín.** `LM_HookCode` copia los bytes recolocados tal
cual. El prólogo de ShaderDepot empieza por `mov rax,[rip+disp32]`: en el
trampolín leía otra dirección y el primer paso por la función original
habría caído (reproducido offline con la misma `liblibmem.a`: SIGSEGV).
`src/rip_fixup_core.hpp` recalcula cada `disp32` relativo a `rip` de los
bytes recolocados; lo que no puede recomponer (inmediato detrás del
operando, salto relativo, destino a más de 2 GB) deshace el hook con
`LM_UnhookCode` y lo reporta fallido. `tools/rip_fixup_selftest.cpp` prueba
los dos caminos contra el libmem real y corre en el job del SDK. DepotKey
(5 bytes recolocados) y GMRC (7) no llevan nada relativo a `rip` en el
prólogo.

**Medido en el codespace** (beta de la Deck, `.so` del job `build-linux-x86_64`
en `cc402b9`, feed copiado a `~/.cache/lumalinux/rvas/`, lanzado a mano con
`LD_PRELOAD`): `RvaFeed: loaded 4 hook RVA(s)`; `LmHook: rebased
rip-relative displacement(s)` para ShaderDepot; `Hook install:
name=DepotKey|ShaderDepot|GMRC method=rva … outcome=installed`, "lumalinux
active (3/3 hooks)"; `Finder resolve: … method=rva … outcome=resolved` y
`PKG0_FINDER: HIT PackageId=0 AppIdVec{size=196 alloc=202}` seguido de la
inyección de nuestros depots en el package 0; Reconcile resuelto; Steam
vivo a los dos minutos, un assert de CEF en 120 s (igual que vanilla),
cuatro procesos cargan la `.so` (la cadena `steam`). **No probado**: que un
hook se dispare con una descarga real (sin SLSsteam de 64 no hay propiedad
falsa, Steam no pide claves ni códigos), ni ShaderDepot delegando en la
original dentro de Steam (solo offline).

**Abierto.** El puntero `CUser` de Reconcile: en 32 lo da el hook de
`isSubscribed` de SLSsteam (`sls_achievement_unblock.cpp`); sin SLSsteam en
64 no hay de dónde sacarlo, y los llamadores de `0x18d9e00` lo reciben como
`this`, sin global a la vista. Reconcile se resuelve pero no se llama.

## §2 Qué nos afecta

| Pieza | Con el cliente de 64 |
|---|---|
| `liblumalinux.so` | la de 32 no carga. La de 64 (`LUMA_X86_64`, job `build-linux-x86_64` en el SDK de sniper) carga dentro del contenedor e instala DepotKey, ShaderDepot y GMRC por feed, con el finder inyectando en el package 0 (§1.3). Sin patrones ni rescates: un build sin fichero en `res/rvas/` instala nada y reporta FAILED |
| Inyección (`LD_PRELOAD`/`LD_AUDIT` desde el wrapper) | funciona; pasa al contenedor por `pv-adverb` |
| Anclas RTTI y de texto | presentes; derivadas en 64 offline (§1.3) |
| Patrones, RVAs, calling convention, layouts | RVAs y layouts derivados y en el feed; los rescates del runtime (`gmrc_xref`, `reconcile_anchor`, `rtti` por nombre) siguen siendo de 32: en 64 devuelven 0. `fetch_steamclient.py` baja ya el paquete `steamrt64`; `check_patterns.py` / `derive_*` no saben x86-64 |
| Rutas `ubuntu12_32/steamclient.so` | ahora `steamrt64/steamclient.so`: guard (`g_client_fp`), `headcrab_compat`, `components`, CI, `fetch_steamclient.py` |
| Crash guard | sigue viendo volcados |
| SLSsteam | 32 bits (`Makefile` `-m32`); Ace hizo "future proof for 64 bits" en la parte Lua (`f4213e5`, 2026-08-31), sin rama de 64. Sin SLSsteam de 64 no hay propiedad falsa: **cuello de botella** |
| CloudRedirect | 32 bits; el upstream desapareció el 2026-10-16, pero el código de `v2.6.6` está en `jayool/CloudRedirect` (`cloudredirect.md` §0): se puede compilar en 64 bits, con el mismo entorno del SDK que lumalinux. Es trabajo nuestro, sin upstream que lo haga |
| LumaDeck | Python, no le afecta la arquitectura; le afectan las rutas y los estados: hoy mostraría los tres componentes "not loaded" y "Steam build not supported"; "Fix in Desktop" (downgrade) es la salida mientras tanto |
| Codespace como banco de pruebas | sirve: en la beta de la Deck arranca el cliente de 64 bajo el supervisor (`start-display.sh` deja el bus de sesión en `/run/user/<uid>/bus`); la prueba con nuestra `.so` en §4 |
| El resto de la escena (LumaCore, SteaMidra, ASSella, slsteam-moon) | todos de 32 bits |

## §3 Qué hay que hacer

En orden. El port está abierto desde el 2026-10-20.

1. **Alarma**: descartada (2026-10-20). El paquete `steamrt64` ya está en los dos canales de la Deck y se sabe dónde está; los manifiestos se miran a mano con el comando de §4 cuando haga falta. `watch-steam.yml` sigue vigilando solo el cliente de 32.
2. **Build de 64 bits contra el runtime**: hecho (§1.2). `LUMA_X86_64` en CMake, dependencias de 64 por script, job en el SDK de sniper con la comprobación de glibc, artefacto `lumalinux-x86_64`.
3. **Hooks en 64**: hecho para `947e5a19…` (§1.3). (a) La derivación está en herramienta: `tools/check_patterns_x64.py` deriva y valida el feed de un cliente de 64 (DepotKey por nombre, GMRC/ShaderDepot/BuildDep por xref de cadena, Reconcile por el ancla del callback, el cache de paquetes y el layout de `PackageInfo` leyendo `GetPackage` y `LoadPackage` con capstone) y `fetch-steamclient.yml` lo corre sobre cada paquete `steamrt64` que baja, junto con `tools/x64_rescues_selftest.cpp`; el fichero generado viaja en la rama y en el artefacto, publicarlo en `res/rvas/` de main es decisión humana. Salida 3 = BLOCKING cuando un crítico, el finder o un layout que el runtime lleva fijo (stride de nodo, `PackageInfo*`, `AppIdVec`) no cuadra. (b) Los rescates del runtime existen en 64: xref de cadena por `rip` + `.eh_frame_hdr` (sin walk-back: sin tabla no hay rescate), ancla de Reconcile en forma SysV, RTTI por nombre con sitios `rip`; `x64_rescues_selftest.cpp` los reproduce sobre `947e5a19…`. Queda (c) el `CUser` de Reconcile.
4. **Rutas y estado** `steamrt64/` en guard (`g_client_fp`), LumaDeck (salud, hash del cliente, downgrade), CI y feeds (`updates.yaml` y `res/rvas/` con grupos por arquitectura); `setup.sh` elige la `.so` según el cliente instalado; ruta de `status.json` que LumaDeck pueda leer desde fuera del contenedor. Dos binarios por release durante la transición.
5. **CloudRedirect en 64**, desde `jayool/CloudRedirect`, con el mismo entorno de build.
6. **SLSsteam**: es de Ace y de 32 bits; sin SLSsteam de 64 no hay propiedad falsa y el resto no sirve de nada. Decisión pendiente: esperar, o fork propio. No se le escribe.
7. **LumaDeck**: mensaje propio para "cliente de 64 bits, aún sin soporte".

## §4 Cómo reproducirlo en el codespace

**Qué publica cada canal de la Deck** (lo que llega a la Deck, no a escritorio):

```bash
for c in steamdeck_stable steamdeck_publicbeta; do echo "== $c"; for a in ubuntu12 steamrt64; do printf '  %-10s ' "$a"; curl -s "https://client-update.fastly.steamstatic.com/steam_client_${c}_${a}" | grep -m1 -oE '"version"\s+"[0-9]+"'; done; done
```

**Arrancar el cliente de 64** (2026-10-20, bajo el supervisor del codespace,
que ya deja el bus de sesión en `/run/user/<uid>/bus`):

```bash
# optar a la beta (Steam cerrado); el supervisor relanza y Steam baja el paquete steamrt64
echo publicbeta > ~/.local/share/Steam/package/beta
# comprobar: el cliente de dentro, su steamclient.so y el hash
pgrep -af 'steamrt64/steam' | head -3 | cut -c1-120
P=$(pgrep -f 'steamrt64/steam -gamepadui' | tail -1); grep -oE '[^ ]*steamclient\.so' /proc/$P/maps | sort -u
sha256sum ~/.local/share/Steam/steamrt64/steamclient.so | cut -c1-16
# volver a estable: borrar package/beta y relanzar (baja de nuevo el de 32)
```

**Probar nuestra `.so` de 64** (artefacto `lumalinux-x86_64` del job, con el
`GITHUB_TOKEN` del codespace; el Steam lanzado así no pinta la ventana, ver
§1.2). Mientras el feed de ese build no esté en main, la `.so` lo toma de la
caché local: copiar `res/rvas/<hash>.yaml` a `~/.cache/lumalinux/rvas/`
antes de lanzar. Lo que debe salir en el log: `RvaFeed: loaded 4 hook
RVA(s)`, `LmHook: rebased rip-relative` (ShaderDepot), tres `Hook install: …
outcome=installed`, `Finder resolve: … outcome=resolved`, `PKG0_FINDER: HIT`;
y, contra un arranque vanilla de la misma duración, el mismo número de
`assert_*.dmp` en `/tmp/dumps` (los asserts de `CefCrashReportingEnabled()`
son del webhelper sin GPU, uno por arranque; más de eso es nuestro):

```bash
cd /tmp && rm -rf ll64 && mkdir ll64 && cd ll64
curl -sL -H "Authorization: Bearer $GITHUB_TOKEN" -o a.zip https://api.github.com/repos/jayool/lumalinux/actions/artifacts/<id>/zip && python3 -c "import zipfile; zipfile.ZipFile('a.zip').extractall('.')"
file liblumalinux.so; objdump -T liblumalinux.so | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -1
echo desktop > /tmp/lumadev-session; steam -shutdown; while pgrep -x steam >/dev/null; do sleep 1; done
: > ~/.cache/lumalinux/lumalinux.log
export DISPLAY=:1 XDG_RUNTIME_DIR=/run/user/$(id -u) DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$(id -u)/bus
env -u LD_AUDIT LD_PRELOAD=/tmp/ll64/liblumalinux.so setsid ~/.local/share/Steam/steam.sh -gamepadui -no-cef-sandbox </dev/null >/tmp/steam64.log 2>&1 &
sleep 90; P=$(pgrep -f 'steamrt64/steam ' | head -1); grep -c liblumalinux /proc/$P/maps
grep -E 'lumalinux loaded|detected|x86_64|hash is' ~/.cache/lumalinux/lumalinux.log | head
grep -i 'cannot be preloaded' /tmp/steam64.log | sort -u
# devolverlo al supervisor
pkill -f 'steamrt64/steam'; sleep 3; ~/start-gamemode.sh
```

Receta del 2026-10-16 (beta de escritorio, sin supervisor): `echo publicbeta >
package/beta`, bus propio con `dbus-daemon --session --fork --print-address`
en `DBUS_SESSION_BUS_ADDRESS` (sin él el cliente sale con 125), y
`env -u LD_PRELOAD -u LD_AUDIT DISPLAY=:1 setsid steam.sh -gamepadui -no-cef-sandbox`.

La prueba de inyección: dos `.so` de 64 bits (constructor y `la_version` que
escriben pid y `comm` en `/tmp`), lanzadas con `LD_PRELOAD` y `LD_AUDIT`, y
mirar `pv-adverb` (`--ld-preload`, `--ld-audit`) y el entorno del pid de dentro.
