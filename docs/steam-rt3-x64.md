# Steam de 64 bits (steamrt3c) — qué cambia para la pila

Doc de medición, 2026-10-16. Se fusionará con `nosotros.md`, `slssteam.md` y
`cloudredirect.md` cuando el port tenga forma; hasta entonces vive aquí.

## §0 Ficha

| | |
|---|---|
| **Qué es** | El cliente de Steam para Linux pasa a ser una aplicación de 64 bits que corre dentro de un contenedor de Steam Runtime 3c (la misma maquinaria de pressure-vessel que usan los juegos). Anunciado en las notas de la beta del 2026-10-15 ("The Steam client is now a 64bit application. The Steam client now runs inside a Steam Runtime container."). Existía como opción desde marzo de 2026 ("Steam RT3 Experimental" en Ajustes, marcador `~/.local/share/Steam/.steam-enable-steamrt64-client`); la beta del 15 lo hace el modo por defecto. |
| **Medido en** | codespace SteamOS (`deck@codespaces-67b581`), canal `publicbeta` de escritorio, 2026-10-16 06:40-07:10 UTC. Paquete `steam_client_publicbeta_steamrt64` versión `1792110651` (junto a `steam_client_publicbeta_ubuntu12` `1792108788`). |
| **Lo que no sabemos** | Cuándo llega a estable de escritorio y cuándo al canal de SteamOS (`steam_client_steamdeck_*`). Si Valve conserva el cliente de 32 bits como alternativa. Qué harán SLSsteam y CloudRedirect. |
| **Fuentes** | Notas de la beta 2026-10-15; `ValveSoftware/steam-for-linux` `README.beta.md` (requisitos) e issue #13015 (hilo del `reaper` de 32 bits, con el comentario de lostgoat, Valve, del 2026-10-15); hilo de r/linux_gaming del mismo día. |

## §1 Lo medido

Todo en el codespace, cliente beta `1792110651`.

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

## §2 Qué nos afecta

| Pieza | Con el cliente de 64 |
|---|---|
| `liblumalinux.so` | no carga (32 bits). Port a x86-64: build contra glibc 2.31, rederivar los cinco hooks, `-m64` en CMake y `build.yml` |
| Inyección (`LD_PRELOAD`/`LD_AUDIT` desde el wrapper) | funciona; pasa al contenedor por `pv-adverb` |
| Anclas RTTI y de texto | presentes; los resolvedores por nombre valen |
| Patrones, RVAs, calling convention, layouts | a rederivar; `check_patterns.py`, `derive_patterns.py` y `fetch_steamclient.py` en modo x86-64 y paquete `steamrt64` |
| Rutas `ubuntu12_32/steamclient.so` | ahora `steamrt64/steamclient.so`: guard (`g_client_fp`), `headcrab_compat`, `components`, CI, `fetch_steamclient.py` |
| Crash guard | sigue viendo volcados |
| SLSsteam | 32 bits (`Makefile` `-m32`); Ace hizo "future proof for 64 bits" en la parte Lua (`f4213e5`, 2026-08-31), sin rama de 64. Sin SLSsteam de 64 no hay propiedad falsa: **cuello de botella** |
| CloudRedirect | 32 bits; mismo caso, y sin salida: el upstream desapareció el 2026-10-16 y del 2.6.6 solo existe el binario (`cloudredirect.md` §0). La nube redirigida se acaba con el cliente de 64 bits |
| LumaDeck | Python, no le afecta la arquitectura; le afectan las rutas y los estados: hoy mostraría los tres componentes "not loaded" y "Steam build not supported"; "Fix in Desktop" (downgrade) es la salida mientras tanto |
| Codespace como banco de pruebas | sirve con bus de sesión; `start-gamemode.sh` debería lanzarlo |
| El resto de la escena (LumaCore, SteaMidra, ASSella, slsteam-moon) | todos de 32 bits |

## §3 Qué hay que hacer

En orden, cuando se abra el port:

1. **Alarma**: `watch-steam.yml` comprueba a diario si aparece un paquete `steamrt64` en `steamdeck_stable` y `steamdeck_publicbeta`. Es el reloj real para la Deck.
2. **Build de 64 bits contra el runtime**: compilar en el SDK de steamrt3 (sniper, glibc 2.31) y no en el Ubuntu de la CI, para que los símbolos de glibc no superen 2.31.
3. **Rederivar los cinco hooks** sobre `steamrt64/steamclient.so` `1792110651`: anclas iguales, bytes y ABI nuevos.
4. **Rutas** `steamrt64/` en guard, LumaDeck y CI.
5. **SLSsteam**: hablar con Ace; marca la ventana. CloudRedirect ya no tiene con quién hablar: se da por perdido en 64 bits y LumaDeck tendrá que decirlo.
6. **LumaDeck**: mensaje propio para "cliente de 64 bits, aún sin soporte".

## §4 Cómo reproducirlo en el codespace

```bash
# optar a la beta (Steam cerrado)
echo publicbeta > ~/.local/share/Steam/package/beta
# bus de sesión, sin él el cliente nuevo sale con 125
export DBUS_SESSION_BUS_ADDRESS=$(dbus-daemon --session --fork --print-address | head -1)
# arrancar vanilla, sin terminal de por medio
env -u LD_PRELOAD -u LD_AUDIT DISPLAY=:1 setsid ~/.local/share/Steam/steam.sh -gamepadui -no-cef-sandbox </dev/null >/tmp/vanilla.log 2>&1 &
# el cliente de dentro
P=$(pgrep -f 'steamrt64/steam -gamepadui' | tail -1); grep -oE '[^ ]*steamclient\.so' /proc/$P/maps | sort -u
tr '\0' '\n' < /proc/$P/environ | grep -E '^(LD_PRELOAD|LD_AUDIT|PRESSURE_VESSEL_RUNTIME)='
# volver a estable: borrar package/beta y arrancar
```

La prueba de inyección: dos `.so` de 64 bits (constructor y `la_version` que
escriben pid y `comm` en `/tmp`), lanzadas con `LD_PRELOAD` y `LD_AUDIT`, y
mirar `pv-adverb` (`--ld-preload`, `--ld-audit`) y el entorno del pid de dentro.
