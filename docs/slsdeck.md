# SLSDeckUniversal (Kaal31/slsdeck) — el plugin de Decky, leído desde el código

Leído desde el código el 2026-10-09: la rama más nueva, `decky-update`, entera,
y `main` y `core-install-fix` por diferencias. Sustituye a
`slsdeck-analysis.md`: sus mediciones con fecha están en §5.2, lo que afirmaba y
el código ya no sostiene en §5.1, y sus hallazgos y decisiones con el estado de
hoy en §4.4. El motor que este plugin instala, **slsteam-moon**, tiene su propio
doc (`slsteam-moon.md`); aquí solo entra lo que hace el plugin. Cómo
**nosotros** hacemos lo mismo está en `nosotros.md`.

---

## §0 Ficha

| | |
|---|---|
| **Qué es** | Un **plugin de Decky Loader con permisos de root** (`plugin.json`: `"flags": ["debug", "root"]`), backend Python (`main.py` + `py_modules/lt/`, ~36.000 líneas, 384 RPC) y frontend TS (~21.000 líneas). Hace de **mando a distancia** de un ecosistema ajeno: instala **slsteam-moon** (fork de SLSsteam) y **Headcrab**, inyecta Steam reescribiendo `steam.sh`, `path/steam` y el hook de sesión de gamescope, añade juegos con zips de lua+manifests de Hubcap, lua.tools, Ryuu y otros, y suma desbloqueadores de DLC dentro del juego (SmokeAPI, CreamAPI, un proxy compilado por juego), fixes, Workshop por SteamCMD, el **hipervisor anti-Denuvo** (módulo de kernel, Proton propio), **Tokeer** (activación de Denuvo por un servicio de terceros a través del Discord del usuario), CloudRedirect (fork de moon), Zapret como evasión de DPI, colecciones de Nexus y lua.tools y una "ruleta" de la tienda. |
| **Repo y commits leídos** | `Kaal31/slsdeck`. `decky-update@fdce91c` (2026-10-06, 0.9.64) entero. `main@8dd2c0e` (2026-09-27, la release "Latest" de GitHub; `plugin.json` dice 0.9.61, la versión real la estampa el CI). `core-install-fix@a2b85eb` (2026-10-01) por diferencias. 743 commits en 26 ramas desde `6da03a1` (2026-08-20, volcado inicial ya con el disable de lumalinux). |
| **Ramas** | Cadena efectiva: `main` → `vpn-bypass` → `nexus-mods-integration` → `lua-collections-implement` → `beta-branch` (`cc269af`, 09-30) → {`core-install-fix` `a2b85eb` \| `install-safety` `fd2a5a3` → `decky-update` `fdce91c`}. **El arreglo de `core-install-fix` no está en `decky-update`.** `main` entra por promociones aplastadas (`4c15480` = `update-system@6b4a1b1`); `2104fc8` "Reset vpn-bypass to current main" tiene el árbol de `main` con otro padre, de ahí los "62 commits por detrás". Sin actividad desde el 10-06. |
| **Plataforma** | SteamOS (Deck) y, según los comentarios, CachyOS; Steam nativo o Flatpak. |
| **Licencia y quién** | Sin `LICENSE`. `Vibe-coder Jimmy <mashroomgodzillo@gmail.com>` 668 commits, github-actions[bot] 70, `Worker1` 5 (08-20/23). Generado por agente: tres módulos vacíos "porque el montaje impide borrarlos" (`charon.py`, `impersonate.py`, `diagnostics.py`). El README cita a LumaDeck como "inspiración" y dice no incluir código (`README.md:103`, `cc269af`). |
| **Relación con los demás programas** | Instala y mantiene **slsteam-moon** (motor) y **Headcrab** (cliente de Steam fijado). Trae piezas declaradas de ASSella (DepotDownloader), SteaMidra (`depot_history`), luatools-moon (`smart_merge`), ASSfixer, KoriaPolis (CrakFiles, HVAuto), HV-Decky, acidicoala (SmokeAPI, CreamAPI, Uplay), CreamySteamyLinux y Tokeer. Fuentes compartidas con LumaDeck: **Hubcap**, **lua.tools** (login y fixes), **Ryuu** y **`manifest.luastools.xyz`**, al que el config de moon que siembra **dona códigos** por defecto. **Conoce nuestra pila por su nombre y la desactiva** (§4.1). |

**Lo que cambia respecto al doc anterior**, en una línea cada uno:

- **No hace falta pulsar Install para que tome el control de nuestra pila.** Al cargar el plugin, si existe `~/.local/share/SLSsteam/SLSsteam.so` (el nuestro lo es), deja en `exit 0` nuestros `ensure-desktop-coverage.sh` y `desktop-coverage.lib.sh`, devuelve a `/usr/bin/steam` todos los `.desktop` que apuntan a `path/steam` y, con Steam enganchado y `steam.sh` sin parchear, reescribe `steam.sh`, `path/steam` y el hook de gamescope (`py_modules/lt/slssteam.py:497-579,2785-2870`). Un vigilante cada 5 min lo repite (`watchdog.py:29-90`, `audit.py:131-188`).
- **Desinstalar el plugin borra `~/.local/share/SLSsteam` entero** (`slssteam.py:4524-4549`, llamado desde `main.py:585`): nuestro SLSsteam, el wrapper y los scripts de cobertura. Con "full purge" (opción, apagada por defecto) borra además los ficheros de **todo juego de `AdditionalApps`**, `~/.config/SLSsteam` y `stplug-in` (`slssteam.py:3268-3349`).
- **"Quick Install" desactiva lumalinux sin preguntar** (`src/sections/SlsSteamCompact.tsx:85-115`); el aviso solo se pinta cuando no hay SLSsteam instalado (`:226-229`).
- **Sobrescribe `~/.local/share/CloudRedirect/cloud_redirect.so`**, la ruta donde `setup.sh` deja el nuestro, con el fork de moon de `master` y en el sitio (`py_modules/lt/cloudredirect.py:57,1345-1380`).
- **El módulo de kernel** sigue saliendo de los mismos dos repos `glowing-tribble` sin hash, pero el código vivo es el HV-Decky de `lt/hv/`; `hypervisor.py` está muerto.
- **La identidad del `steamclient.so` (sha256 + build-id) y el arreglo del instalador duplicado** solo existen en `core-install-fix`, no en la rama más nueva.
- **`get_user_home` ya no usa una lista de nombres**: puntúa usuarios reales por su `Steam`/`homebrew` (`py_modules/lt/paths.py:53-90`).

---

## §1 Mapa del código

Rutas relativas a la raíz del repo en `decky-update@fdce91c`. F = función de §2.
`py_modules/lt/slssteam.py` es idéntico en `main`.

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `main.py` | 2.543 | 384 RPC; arranque: parches al importar, calentamiento, guardias de arranque, vigilante de 5 min, Hubcap Updates, re-pins (`:286-504`); `_uninstall` (`:553-676`) | 11 |
| `py_modules/lt/__init__.py` | — | al importar: `live_refresh.patch`, `survival_backup.patch` (restaura juegos desde un zip que sobrevive a la desinstalación), `cloudredirect_reinstall.patch` (reescribe wrappers) | 9, 11 |
| `py_modules/lt/slssteam.py` | 5.070 | instalación de moon y Headcrab, inyección (`steam.sh`, `path/steam`, gamescope), `config.yaml`, API de moon, guardias de arranque, desactivar motores ajenos, desinstalación | 1, 2, 9, 11 |
| `py_modules/lt/steam.py` | 1.380 | bibliotecas, `config.vdf`, "instalaciones fantasma", reinicio de Steam, quitar juegos | 4, 9 |
| `py_modules/lt/audit.py`, `watchdog.py`, `confighealer.py`, `survival_backup.py`, `lifecycle.py`, `install_safety.py` | — | auditoría y autorreparación cada 5 min; reparador del config; copia de supervivencia; lista de riesgos y "recibos" (10-06) | 11 |
| `py_modules/lt/downloads.py`, `smart_merge.py`, `apis.py`, `free_providers.py`, `luatools.py`, `ryuu.py`, `httpc.py` | 1.728, 714, … | añadir juego: fuentes, fusión de lua, manifests, claves; HTTP con DoH/Tor para Hubcap | 4, 9, 10 |
| `py_modules/lt/buildarchive.py`, `buildhistory.py`, `buildpicker.py`, `pinsource.py`, `depot_history.py`, `hubcap_updates.py` | 1.110, … | builds, pins (`ManifestPins`), actualización por Hubcap | 5 |
| `py_modules/lt/depotdl.py`, `assella.py` (+ `defaults/assella/deps`) | 574, 816 | descarga de depots fuera de Steam (DepotDownloader) | 4 |
| `py_modules/lt/dlc*.py`, `smokeapi.py`, `creamysteamy.py` (+ `defaults/creamapi`, `defaults/creamysteamy/proxy.c`) | — | DLC en el cliente y dentro del juego | 3 |
| `py_modules/lt/fixes.py`, `custom_fixes.py`, `crakfiles.py`, `nerai.py`, `steamstub.py`, `netsock.py`, `online_patch.py`, `multiplayer*.py`, `denuvo.py` | 1.720, … | fixes y DRM | 6 |
| `py_modules/lt/hv/*`, `hvauto.py`, `proton.py`, `tokeer.py`, `tokeer_health.py`, `ubisoft_packages.py` (+ `dist/umipcompatd`) | 1.143, 968, … | hipervisor, Proton propio, Tokeer | 6 |
| `py_modules/lt/cloudredirect.py`, `cloudredirect_reinstall.py`, `cloudsave.py` | 1.706, 486, … | CloudRedirect y copias de partidas | 8 |
| `py_modules/lt/plugin_updates.py`, `updates.py`, `ghrel.py` | — | update del plugin por Decky y de dependencias | 11 |
| `py_modules/lt/zapret.py` | — | `nfqws` como root + reglas `iptables` NFQUEUE | 10, 12 |
| `src/patches/StorePatch.ts`, `WorkshopPatch.ts`, `src/lib/*Capture.ts` | 839, … | CDP sobre el CEF de Steam (`localhost:8080`): botones en la tienda, captura de claves de Hubcap/Ryuu/SteamDB, Discord para Tokeer | 9, 10 |
| `.github/workflows/` | 20 | releases "rolling" por rama, builds que suben `dist/` a la rama, parches en CI | 11, 12 |
| `defaults/`, `dist/umipcompatd` | — | binarios sin fuente: `7zz`, CreamAPI, DepotDownloader y deps, `headcrab.sh`, `umipcompatd` (Rust/eBPF, 4,4 MB) | 3, 4, 6 |
| `tests/` | 17 ficheros | DLC, CloudRedirect, compatibilidad, pins, updates…; ningún workflow los ejecuta | 11 |

---

## §2 Las doce funciones

### 2.1 Engancharse a Steam

SLSDeck no se engancha a Steam: instala **slsteam-moon** y lo inyecta.

**Instalación** (`start_install`, `slssteam.py:2139` → `_run_install`, `:1984`):

1. **Limpia conflictos como root**: borra Millennium, `/usr/lib/millennium`
   incluido, y desinstala `slssteam`/`slssteam-git` con `pacman -Rns`
   (`:2528-2602`).
2. **Baja el motor**: la última release de `swwayps/slsteam-moon`, prefiriendo
   el zip "lumen" (`:4618`); si no, el último `SLSsteam-Any.7z` de AceSLS
   (`:1311`). Sin versión fijada y **sin hash**: `_download` acepta un sha256
   (`:1788-1841`) y ninguna de sus cinco llamadas lo pasa (`:1977,2011,3797,4504,4593`).
3. **Copia los ficheros**: copia todo `bin/` sobre `~/.local/share/SLSsteam`
   (`:1905`) sin ejecutar el `setup.sh` de upstream, y escribe un wrapper
   `path/steam` con `LD_AUDIT` y un contador anti-bucle de crashes (`:2729-2782`).
4. **Corre Headcrab**: el `headcrab.sh` de `Deadboy666/h3adcr-b` en
   `refs/heads/main` (`:1313`), con la copia empaquetada de reserva.
   - Lo parchea por texto para que no copie SLSsteam de serie encima de moon
     (`:3819`).
   - Lo ejecuta como el usuario con atajos para `wget`, `sudo`, `pkexec` y
     `7z` (`:3720-3922`).
   - Mientras corre, quita los bloqueos de `steam.cfg` y los repone después
     (`:3872-3913`).
   - Headcrab hace `killall steam`, sirve una descarga del cliente en
     `localhost:1666`, sustituye `steam.sh` y, con `DisableCloud: no`, instala
     CloudRedirect.

**Inyección** (`activate_injection`, `:2974`):

- `~/.config/{gamescope-session-plus,gamescope-session,gamescope}/sessions.d/steam`
  con `STEAMCMD` apuntando a `path/steam` (`:2897-2946`).
- `steam.sh` se copia a `steam.sh.slsorig` y se sustituye por un wrapper
  (`LD_AUDIT`, `LD_PRELOAD` de `cloud_redirect.so`, `source client.sh`) con
  modo `555`.
- `steam.cfg` con `BootStrapperInhibitAll` si falta (`:2458,3010-3078`).

**Cuando Steam se actualiza o revierte `steam.sh`**: `boot_injection_watchdog`
(`:497-579`) y la autorreparación de 5 min lo vuelven a parchear y, con
`autoReinject` (por defecto), reinician Steam (`steam -shutdown; sleep 5; nohup
steam`, `:429-438`).

- **"Activo"** = `SLSsteam.so` en algún `/proc/*/maps`, más log escrito desde
  el arranque, más la última marca de carga (`:245-334`).
- **"Es moon"** = cadenas dentro del `.so` (`:4362,4456`).
- **"Cliente compatible"** = versión del manifest de paquetes frente al
  `HeadcrabCompatibleClientVer` del script (`:1457-1540`).
- **Rebaja del cliente**: solo con un fallo de hash explícito en el log
  (`:4045`).
- El sha256 y build-id del `steamclient.so` y el estado `active/incompatible/…`
  solo están en `core-install-fix`.

`headcrab_compatible_client` llama a `ensure_http_client` sin importarlo
(`:1489`). Desde que hay una lectura previa del script cacheado
(`:1479-1486`), solo falla cuando no hay caché.

### 2.2 Propiedad y licencias

Todo lo hace moon, por `AdditionalApps` y `FakeAppIds` en
`~/.config/SLSsteam/config.yaml`, más `PlayNotOwnedGames: yes` forzado
(`slssteam.py:902-966`). La propiedad en caliente la confirma
`live_refresh.py`, que lee el log de moon hasta ver la generación de recarga
procesada (`slsteam-moon.md` §2.2). Juegos poseídos: moon deja fuera lo que la
cuenta ya tiene. Family sharing: nada.

### 2.3 DLC

Tres capas:

- **Cliente.** Moon solo desbloquea el DLC de juegos añadidos
  (`slsteam-moon.md` §2.3). `DlcData` e `InjectAllAdvertisedDlc`, con política
  persistida y reconciliada al arrancar.
- **Dentro del juego.** SmokeAPI, Uplay R1/R2 y CreamAPI (empaquetado). Se
  instalan **por defecto** al añadir un juego que ya está instalado
  (`downloads.py:690-717`).
  - La copia de seguridad `.dll` → `_o.dll` distingue mayúsculas: con un
    `.DLL` en mayúsculas el original se sobrescribe sin copia
    (`smokeapi.py:169`).
  - `creamysteamy.py` compila un proxy por juego con un `zig` 0.13.0 bajado
    en el momento (`:42-44`) y descomprime con `tarfile.extractall` sin
    filtro (`:106`).
- **Contenido.** `dlcdepot.py` y `depotdl.py` bajan con DepotDownloader los
  depots de DLC que Steam no da.

Quitar un juego borra el bloque `DlcData`; los ficheros de desbloqueadores se
quedan.

### 2.4 Claves y manifests

**Fuentes al añadir** (`downloads.py:1057`):

1. Hubcap (`/manifest/<id>?api_key=`).
2. lua.tools si hay sesión (`luatools.py:727`).
3. El resto de `api.json`, ordenado por salud: Ryuu, Sushi en GitHub/jsDelivr,
   TwentyTwo Cloud.
4. "Free providers" (`free_providers.py:171`): steamcmd.net, el
   `depotkeys.json` de `fylsdy`, `qwe213312`, revobd, ManifestHub y
   ManifestHub3.
5. Charon.
6. Si ninguno lo tiene, registra el juego sin manifest.

`api.json` se puede sustituir desde un repo de madoiscool (`apis.py:148`).
`substitute_keys` mete la clave guardada en cualquier `<…key…>` de una URL
(`apis.py:67`).

**Códigos de petición**: el plugin no los pide. Los pide moon, y el config que
siembra **dona** cada 30 s los de los depots que posee la cuenta a
`manifest.luastools.xyz`, sin tope por sesión
(`defaults/slssteam/config.default.yaml:112-121`; el reparador lo añade a
cualquier config que no lo tenga).

**Qué escribe** (`downloads.py:521` → `smart_merge.py:514`):

- Manifests en `~/.config/SLSsteam/manifests` y en `depotcache`, más marcas
  globales `.preferred_<depot>`.
- `stplug-in/<appid>.lua`, sobrescrito con solo las líneas `addappid`.
- La caché de claves privada de moon (`cache/depotkey_<depot>.yaml`), porque
  moon solo importa los lua al arrancar.
- `DecryptionKey` en `config.vdf`, solo con Steam parado.
- `AppTokens`, `AdditionalApps` y `DlcData`.

**Validación**:

- Los zips se extraen por nombre base, sin tamaño máximo.
- Los manifests se comprueban bien: magia, payload, y depot/gid frente al
  nombre (`smart_merge.py:367`).
- `depotdl` y `buildarchive` guardan cualquier respuesta de más de 64 bytes
  sin validar (`depotdl.py:111-129`).

**Al arrancar con Steam parado** (`main.py:310-319` →
`steam.provision_all_added_depots`, `steam.py:859-907`): escribe en
`config.vdf` las claves de **todos** los `stplug-in/*.lua`, copie quien los
copie, y borra los `appmanifest` que considera "fantasma" (`steam.py:562-631`).

Fallos trazados:

- La descarga principal usa `client.stream()`, que en httpx 0.27 se salta el
  `request()` sobrescrito. Se pierden la cabecera `Bearer` de Hubcap, el
  `X-Auth-Key` de Ryuu y el DoH/Tor (`downloads.py:1082`, `httpc.py`).
- Un `addtoken` de cualquier proveedor se escribe en `config.yaml` sin
  comprobar y puede llevar saltos de línea (`downloads.py:650` →
  `slssteam.py:2376`).
- `fetch_manifest_bundle` indexa por depot y puede pasar a DepotDownloader el
  manifest de otra versión.

### 2.5 Updates de juegos

- **Por defecto**: `AutoUpdateApps: yes` de moon. Moon pide el código a Steam
  y a su archivo; lo que haga sin proveedores está en `slsteam-moon.md` §2.5.
- **Pins**: en `ManifestPins` de moon (otra clave y otra forma que
  `ManifestIds`). `pinsource.py` fija el build que pide un fix; al arrancar
  re-fija los juegos con fix y `set_only_update_on_launch`
  (`main.py:479-504`).
- **"Hubcap Updates"** (apagado por defecto): 90 s tras el arranque y cada 2 h,
  `/contents` por juego y, si cambia, un `force_update` del zip (cuota de 25).
- **Historial**: 6 builds por juego. El archivo de builds se reaplica en cada
  arranque (`main.py:395`).

### 2.6 Fixes y DRM

**Fixes**:

- Fuentes: el scraper de Ryuu (bash como root cada 6 h), `files.luatools.work`,
  fixes de lua.tools, Unsteam, CrakFiles, NERAI y perondepot por HTTP
  (`config.py:46`).
- `curl`, `7zz`, `bsdtar` y `unrar` corren como root.
- Rutas comprobadas con `abspath`, no `realpath`, y `copy2` sigue symlinks.
- Las DLL las carga el juego por `WINEDLLOVERRIDES`.
- Steamless automático en `LaunchApp` lo hace moon (`steamstub`); el plugin
  lo activa.

**Hipervisor** (`lt/hv/*`; `hypervisor.py` no se llama):

- Módulo `cpuid_fault_emulation-<kernel>.ko` de la release "latest" de
  `PareidoliaDev/glowing-tribble` o `2804u13j200-spec/glowing-tribble`, o de
  cualquier repo que escriba el usuario (`hv/core.py:19-24,339-388`).
- Sin hash ni firma: solo cabecera ELF, más de 4 KB y `vermagic`
  (`hv/operations.py:593-631,1027-1038`).
- `modprobe -r kvm_amd kvm` + `insmod` como root (`:1044-1050`). Rompe todo lo
  que use KVM mientras está cargado.
- "Disable UMIP" mete `clearcpuid=514` en `/etc/default/grub` y ejecuta
  `update-grub`, sin copia (`:498-519`).
- "Install build deps" quita el solo-lectura de SteamOS para `pacman`
  (`:392-485`).
- `dist/umipcompatd` (Rust/eBPF, sin fuente) se lanza como root en su propia
  sesión (`hv/umip.py:67-87`).
- **Proton propio**: GE-Proton11-1-LinUwUx de la release `main-latest` del
  propio repo, subido a mano, sin hash, descomprimido como root con `tar -xf`
  en `compatibilitytools.d` (`proton.py:105-111,248`).
- HVAuto: lista de cracks de `KoriaPolis/HVAuto` (`hvauto.py:37`) con enlaces a
  buzzheavier.

**Tokeer**:

- Runtime de `git.lua.tools`, descomprimido en `~/.tokeer`. Su
  `tokeer_steam_config.py` se ejecuta **dentro del backend de root**
  (`tokeer.py:35-38,524,565`).
- `install_linux.sh` de `main`, sin fijar, ejecutado como el usuario
  (`:629-631`).
- `SERVER_URL=https://luastools.xyz` (`:477`).
- `tokeerDiscordCapture.ts` maneja la **sesión de Discord del usuario** dentro
  de Steam por CDP: abre un ticket, publica el código TLX1 y sube los ficheros
  de petición de Ubisoft/EA.
- El plugin vuelve a firmar el informe del verificador con un secreto fijo en
  el código tras poner a `true` la comprobación de opciones de lanzamiento
  (`tokeer.py:41,750-760`).
- La petición de EA siempre falla: `time` sin importar (`:851`).

### 2.7 Logros, stats y tiempo de juego

Lo hace moon (`Achievements:` en su config, esquema prestado con un
`AchievementOwnerId` fijo en la plantilla, `config.default.yaml:129`). El plugin
solo conmuta. CloudRedirect lleva `sync_achievements`/`sync_playtime`.

### 2.8 Cloud saves

**CloudRedirect, fork de moon**: el `cloud_redirect.so` de
`swwayps/cloudredirect-moon` en `master`, sin fijar ni hash, solo comprobando
que sea ELF de 32 bits (`cloudredirect.py:57,1345-1373`).

- Lo escribe con `open(…, "wb")` en `~/.local/share/CloudRedirect/` (y en la de
  Flatpak).
- Lo carga con `LD_PRELOAD` en sus wrappers.
- Con el `.so` presente, **desinstala la app Flatpak de CloudRedirect**
  aunque la haya instalado otro (`cloudredirect_reinstall.py:154-188`).
- El secreto OAuth se lee del fuente de moon fijado a commit (`:60,708-728`).
- El purgado borra `~/.config/CloudRedirect` con partidas y tokens
  (`:1626-1645`).
- Importar partidas: extracción segura con límites y copia previa
  (`:1118-1246`).

### 2.9 Añadir y quitar un juego

**Entradas**:

- `start_add` (`main.py:909` → `downloads.py:1212`), que se niega si la
  inyección no funciona.
- La tienda de Steam, por un binding `ltInvoke` que `StorePatch.ts` mete en la
  pestaña de `store.steampowered.com` por CDP (`:680-736`).
  - Cualquier script de esa pestaña puede llamar a añadir, quitar, recargar
    Steam o `fixApplyUrl`.
  - `fixApplyUrl` baja como root una URL cualquiera y la extrae en la carpeta
    del juego.
- Workshop (`WorkshopPatch.ts`), Nexus y colecciones de lua.tools.

**Qué escribe**: §2.4. Además mete en `.lua` y `config.yaml` el resultado de la
fusión, y el registro "ever added" se rellena con **todos** los
`stplug-in/*.lua` que ve (`downloads.py:1523`).

**Quitar un juego**:

- Quita el lua de todos los `stplug-in`, `AdditionalApps` de todos los
  `config.yaml`, `DlcData` y `loadedappids`.
- Deja ficheros, ACF, `ManifestPins`, `AppTokens`, claves de `config.vdf`,
  caché de claves de moon y manifests.
- Cancelar una adición borra el lua que hubiera antes (`downloads.py:1257`).
- El camino ASSella, si falla, hace `rmtree` de `steamapps/common/<nombre de
  tienda>` aunque otro juego use esa carpeta (`assella.py:549,623`).
- "Purge" borra todos los lua de `stplug-in`.

**Desinstalar el plugin** (`main.py:553-676`):

- `deactivate_injection`.
- `remove_engine_and_headcrab_livesafe`: **`rmtree` de
  `~/.local/share/SLSsteam`** y `~/.headcrab`, y borrado de `steam.sh.slsorig`,
  `client.sh` y el bloqueo de `steam.cfg` (`slssteam.py:4524-4559`).
- Retira CloudRedirect, Zapret, Tokeer y GE-Proton10-34 (este de todas las
  carpetas de herramientas, aunque lo pusiera el usuario).
- Con `fullPurgeOnUninstall` (por defecto `False`, `settings.py:342-344`):
  `full_uninstall_cleanup` borra el `appmanifest` y los ficheros de **todo
  appid de `AdditionalApps` y del historial**, más `~/.config/SLSsteam`,
  `~/.local/share/SLSsteam`, `~/.headcrab` y `stplug-in` (`slssteam.py:3268-3349`).
- La copia de supervivencia `~/.local/share/slsdeck/build_survival_backup.zip`
  queda y **restaura juegos al volver a instalar** (`survival_backup.py:332-421`).

### 2.10 Credenciales y proveedores

- **Claves**: en el JSON de ajustes con `0600` (`settings.py:100-106`); la
  sesión de lua.tools en `~/.local/share/slsdeck`.
- **Hubcap y Ryuu se capturan del navegador por CDP**.
  - Hubcap: el script entra en la sesión, pulsa "regenerate/reset key" una vez
    y lee `smm_…` de la página (`src/lib/hubcapCapture.ts:25-46`).
  - Ryuu: llama a `POST /api/refresh_my_auth_key` con la cookie de sesión
    (`keyCapture.ts:95-109`).
  - Las dos **rotan la clave**: la que tuviera guardada otra herramienta deja
    de valer.
- **Transporte de Hubcap**: DoH (Cloudflare, Google) conectando a la IP con el
  SNI correcto, sin desactivar la verificación, y un Tor local si existe
  (`httpc.py:98-202`).
- **Zapret**: `nfqws` como root más reglas `iptables`/`ip6tables` mangle
  OUTPUT de TCP 80/443 a NFQUEUE 210 para 67 dominios; se reanuda al arrancar.
- **Cuota**: sin contador de Hubcap; hasta 3 zips por adición.
- **Sin cuenta**: siguen funcionando moon y los "free providers".

### 2.11 Mantenimiento propio

**Superficie**: QAM, página Advanced, parches de biblioteca y tienda.

**Update del plugin** (`plugin_updates.py`):

- Repo y canal fijos (`Kaal31/slsdeck`, `update-system`, `:24-25`). Lista de
  releases con fallback a Atom y a caché (`:276-317`).
- Instala **Decky** con `utilities/install_plugin` y **hash vacío**
  (`src/sections/Updates.tsx:128-133`, `src/index.tsx:471-476`), desde releases
  "rolling" que el CI pisa con `--clobber`.
- En `decky-update` no se empaqueta `build.json` (`release-decky-update.yml:53`):
  el canal sale "unknown" y el plugin cae a la última release inmutable de
  `update-system`. Como 0.9.68 es mayor que 0.9.64, ofrecería "actualizar" a
  código del 09-26 (`:65-71,320-342`; deducido, sin comprobar en GitHub).

**Dependencias** (`updates.py`, `ghrel.py`): `releases/latest` de SmokeAPI,
uc-online2, eos-proxy, Uplay, Zapret, moon y Headcrab, aplicadas solas al
arrancar con auto-update.

**Salud**:

- `audit.py` puntúa y `watchdog.py` repara cada 5 min. Para tras 3 intentos
  sin efecto.
- Un "Deactivate" o "Restore Steam startup" del usuario se deshace solo en 5
  min mientras exista `SLSsteam.so` (`audit.py:52-113`).
- `install_safety.py` (10-06) añade una lista de riesgos y un registro JSONL;
  no hace instantáneas.

**Fallos de arranque**:

- Decky arranca antes que Steam: "parcheado pero no vivo" reinicia Steam y
  puede lanzar un segundo Steam en Game Mode (`slssteam.py:537-545`).
- Con `autoClientRepin` (por defecto) el vigilante no reinyecta hasta el
  tercer arranque (`:556-563`).
- Instalaciones duplicadas por carrera en `start_install` (`:2140-2155`);
  arreglado solo en `core-install-fix`.

**Tests**: 17 ficheros, ningún workflow los ejecuta (el CI hace
`compileall` y greps).

### 2.12 Proyecto

- **Mantenimiento**: un autor y un agente; ráfagas de commits (hasta 125 al día
  en agosto, 12 en septiembre) y pausa desde el 10-06. Ramas con releases
  "rolling" y `contents: write`; acciones fijadas por etiqueta.
- **Binarios sin fuente**: `defaults/bin/7zz` (7-Zip 23.01), CreamAPI,
  DepotDownloader y deps, `headcrab.sh`, `dist/umipcompatd`, todos del volcado
  inicial salvo el último. `dist/index.js` se sube al repo (92 commits).
  `dist/container/Dockerfile` con `SigLevel … TrustAll`, "de un usuario de
  Discord".
- **Terceros**: GitHub (moon, Headcrab, `SteamTracking`, `h3adcr-b-modul3s`,
  `glowing-tribble`, HVAuto, cloudredirect-moon, netsock, SmokeAPI…), Hubcap,
  lua.tools, `luastools.xyz`, Ryuu, steamcmd.net, Discord, buzzheavier,
  perondepot, Supabase de lua.tools, Flathub, Valve.
- **Privacidad**: sin telemetría propia. La donación de códigos de moon va
  encendida por defecto, y Tokeer pasa por el Discord del usuario.

---

## §3 Superficies externas

### 3.1 Hosts y URLs

| Host | Para qué | Notas |
|---|---|---|
| `api.github.com`, `github.com`, `raw.githubusercontent.com` | moon, Headcrab (`refs/heads/main`), `SteamTracking`, `h3adcr-b-modul3s`, `glowing-tribble`, HVAuto, `cloudredirect-moon@master`, netsock, SmokeAPI, Uplay, uc-online2, eos-proxy, Zapret, el propio repo (Proton, updates) | sin hash en nada de lo que se ejecuta |
| `hubcapmanifest.com` | zips, `/contents`, Workshop | `?api_key=` o `Bearer` según el camino |
| `lua.tools`, `db.lua.tools`, `files.luatools.work`, `git.lua.tools`, `luastools.xyz`, `manifest.luastools.xyz` | login, fixes, colecciones, Tokeer, donación de códigos | |
| `generator.ryuu.lol` y espejos | zips, fixes | |
| `api.steamcmd.net`, `steamdb.info`, Valve | metadatos, builds | |
| `discord.com`, `cdn.discordapp.com`, `media.discordapp.net` | Tokeer | sesión del usuario |
| `buzzheavier.com`, `pixeldrain.com`, `api.perondepot.xyz` (HTTP) | cracks, fixes | |
| `ziglang.org`, `dl.flathub.org`, `steamdeck-packages.steamos.cloud` | toolchain, CloudRedirect, paquetes | |
| `localhost:8080`/`8081` | CDP del CEF de Steam | |
| `1.1.1.1`, `dns.google` | DoH para Hubcap | con verificación |

### 3.2 Ficheros

| Ruta | Qué | Quién más la toca |
|---|---|---|
| `~/.local/share/SLSsteam/` | moon, `path/steam`, **`ensure-desktop-coverage.sh` y `desktop-coverage.lib.sh` puestos a `exit 0`**; borrado entero al desinstalar | **lumalinux/LumaDeck** (SLSsteam, wrapper, cobertura) |
| `<Steam>/steam.sh`, `steam.sh.slsorig`, `client.sh` | wrapper `555`; último recurso de la restauración: borrar `steam.sh` (`slssteam.py:3095-3153`) | Steam, Headcrab |
| `~/.config/gamescope-session*/sessions.d/steam` | hook de Game Mode (con `.bak.<ts>`) | — |
| `*steam*.desktop` en `~/.local/share/applications`, `/usr/share/applications`, `~/.config/autostart`, `/etc/xdg/autostart` | `Exec` de `path/steam` a `/usr/bin/steam`; los de sistema quedan del usuario (`chown`) | **lumalinux** (sombras `.desktop`) |
| `~/.config/SLSsteam/config.yaml` | escritura atómica (temporal + `fsync` + `replace` + `chown -R`), línea a línea, conserva claves ajenas salvo `set_blacklist` y el reparador (`slssteam.py:623-671`) | SLSsteam, LumaDeck |
| `~/.config/SLSsteam/cache/`, `manifests/` | caché de claves y archivo de moon | moon |
| `<Steam>/config/stplug-in/*.lua` | lua de cada juego; los trata todos como suyos | **LumaDeck** (copia de cada `.lua`) |
| `<Steam>/config/config.vdf` | `DecryptionKey` de todos los lua con Steam parado | Steam, LumaDeck |
| `<Steam>/depotcache/`, `steamapps/appmanifest_*.acf` | manifests; borra los ACF "fantasma" | Steam, LumaDeck |
| `~/.local/share/CloudRedirect/cloud_redirect.so` | fork de moon, escrito en el sitio | **lumalinux** (`setup.sh:834,1102`) |
| `~/.config/lumalinux`, `~/.local/share/lumalinux`, `keys.txt` | renombrados a `*.slsdeck-disabled` | **lumalinux** |
| `<Steam>/compatibilitytools.d/` | GE-Proton propio, GE-Proton10-34 | Steam, otros |
| `/etc/default/grub`, módulos de kernel, `iptables` | hipervisor, Zapret | sistema |
| `/run/user/<uid>/SLSsteam/api`, `~/.cache/SLSsteam/api`, `/tmp/SLSsteam.API` | `install\|appid\|lib` como root (`slssteam.py:358-387`) | moon |
| `~/.local/share/slsdeck/` | sesión de lua.tools, copia de supervivencia, recibos | — |

### 3.3 Puertos

`127.0.0.1:6767` (callback OAuth de lua.tools, `luatools.py:287`),
`127.0.0.1:53682/53692` (OAuth de CloudRedirect, `cloudredirect.py:750`) y el
`localhost:1666` de Headcrab durante la rebaja. Sin servidor en `0.0.0.0`.

---

## §4 Lo que nos afecta

### 4.1 Hallazgos pendientes de decisión

Aparcados el 2026-10-09. Solo lo que le pasa a alguien con SLSDeck y nuestra
pila en la misma Deck.

1. **Con el plugin cargado, desmonta nuestra pila en cada arranque, sin pulsar
   nada.** Las guardias de arranque y el vigilante de 5 min solo miran si
   existe `~/.local/share/SLSsteam/SLSsteam.so` (`slssteam.py:509,2791`;
   `audit.py:27-59`), y el nuestro está ahí. En cada carga del plugin, en este
   orden:
   - **Desactiva nuestro guardián.** Sobrescribe con `exit 0` nuestros
     `ensure-desktop-coverage.sh` y `desktop-coverage.lib.sh`
     (`slssteam.py:2803-2823`). El guardián systemd que llama al primero
     (`setup.sh:765`) sigue corriendo, pero ya no hace nada.
   - **Quita la cobertura de Desktop.** Reescribe a `/usr/bin/steam` todo
     `*steam*.desktop` que apunte a `path/steam`, de usuario o de sistema
     (`:2826-2870`). Steam en Desktop arranca vanilla.
   - **Reemplaza la inyección.** Con Steam enganchado (nuestro SLSsteam
     cuenta como "vivo") y `steam.sh` vanilla (el nuestro lo es), llama a
     `activate_injection()` (`:528-533`). Eso reescribe `steam.sh` con su
     wrapper (`555`), `path/steam` y el hook de gamescope.
     - El `path/steam` nuevo es el suyo: `LD_AUDIT` y el `LD_PRELOAD` de
       `cloud_redirect.so`, **sin `liblumalinux.so`**.
     - Si nuestro `steam.sh` no tiene sus marcas, lo guarda como "original" en
       `client.sh` y lo sigue cargando dentro del suyo.

   Tras el siguiente reinicio de Steam, lumalinux no se carga. Las claves, los
   códigos, el paquete 0 y ShaderDepot desaparecen sin error: los juegos
   añadidos fallan al descargar o actualizar. Volver a ejecutar `setup.sh` lo
   repara hasta la siguiente carga del plugin o los 5 min del vigilante.

2. **"Quick Install" desactiva lumalinux sin preguntar.** Con un motor ajeno
   detectado (`~/.local/share/lumalinux`, `~/.config/lumalinux`,
   `~/.steam/steam/keys.txt` o la cadena "lumalinux" en `steam.sh`;
   `slssteam.py:4365-4386`), llama a `disable_foreign_engines`
   (`SlsSteamCompact.tsx:85-115`). Eso renombra a `*.slsdeck-disabled` los dos
   directorios de lumalinux y los dos `keys.txt` (`slssteam.py:4417-4450`).

   Después instala moon encima de nuestro SLSsteam, aplica la rebaja de
   Headcrab y reinicia Steam. El aviso "Install will disable it" solo se
   muestra si no hay SLSsteam instalado (`:226-229`), y con nuestra pila sí lo
   hay.

   Nada en ningún lado devuelve los ficheros renombrados, ni el plugin ni
   LumaDeck. El modelo que tiene de nosotros ("un motor que se engancha por
   `steam.sh`") es antiguo.

3. **Desinstalarlo borra nuestro SLSsteam.** `_uninstall` hace `rmtree` de
   `~/.local/share/SLSsteam` (`slssteam.py:4534-4545`). Ahí están nuestro
   `SLSsteam.so`, el wrapper `path/steam` y los scripts de cobertura. Además
   quita el bloqueo de `steam.cfg`, que LumaDeck gestiona con `steam_freeze.py`.

   Con "Full purge on uninstall" (apagado por defecto), además borra los
   ficheros de **todo juego de `AdditionalApps`**, que incluye los de LumaDeck,
   y `~/.config/SLSsteam` (config y plugins) y `stplug-in` (`:3268-3349`).

   La copia de supervivencia restaura sus juegos al reinstalar
   (`survival_backup.py:332-421`, al importar el paquete), los nuestros
   incluidos si pasaron por su registro.

4. **`cloud_redirect.so` compartido.** `setup.sh` instala el de Selectively11 en
   `~/.local/share/CloudRedirect/cloud_redirect.so` (`setup.sh:834,1100-1102`).
   SLSDeck escribe ahí el de `swwayps/cloudredirect-moon@master`
   (`cloudredirect.py:1362-1373`). Es un fork hecho para moon que, según su
   comentario, necesita el `steamclient` de moon (`:50-56`), y nuestro wrapper
   lo cargaría con SLSsteam de serie.

   Lo escribe con `open(…, "wb")` en el sitio. Si Steam lo tiene mapeado, el
   truncado puede tumbar el proceso; nuestro `install -m` crea un fichero
   nuevo. Y cada reejecución de `setup.sh` vuelve a poner el nuestro.

5. **Los `.lua` de LumaDeck en `stplug-in` le valen como juegos suyos.**

   - Al arrancar con Steam parado escribe en `config.vdf` las claves de todos
     y borra los `appmanifest` que ve como "fantasma" (`steam.py:562-631,859-907`).
   - Su historial "ever added" se rellena con todos (`downloads.py:1523`).
   - "Purge" los borra.
   - "Quitar" un juego quita el lua y `AdditionalApps`.

   Un juego de LumaDeck entra así en las operaciones de SLSDeck sin que el
   usuario lo haya añadido allí. Sin medir qué considera "fantasma" un ACF de
   un juego de LumaDeck a medio instalar.

6. **Rota la clave de Hubcap y la de Ryuu.** La captura por CDP pulsa
   "regenerate key" en la página de Hubcap y pide una clave nueva a Ryuu
   (`hubcapCapture.ts:25-46`, `keyCapture.ts:95-109`). La clave que tenga
   LumaDeck en `api.json` deja de valer: Hubcap y Ryuu responden 401/403 hasta
   volver a pegarla.

7. **`config.yaml` compartido, con esquemas que divergen.** Fuerza
   `API: yes` y `PlayNotOwnedGames: yes` en cada pase (`slssteam.py:931-966`).
   Siembra `Donate` encendido, y el reparador lo añade a cualquier config.

   Los pins van en `ManifestPins` (moon) y no en `ManifestIds` (SLSsteam de
   serie, LumaDeck). Con moon instalado encima, los pins de `pins.py` no los
   lee nadie.

   Sus escrituras son atómicas y conservan lo ajeno. El reparador
   (`confighealer.py`) convierte `AdditionalApps: [..]` en una lista anidada
   (`:218-245`).

8. **Se apropia de rutas del sistema.** Reescribe `/usr/share/applications` y
   `/etc/xdg/autostart` y les hace `chown` al usuario (`slssteam.py:2833-2864`).
   También desinstala como root paquetes `slssteam` de pacman y borra
   Millennium (`:2528-2602`).

   En una Deck con nuestra pila, los `.desktop` de sistema no son nuestros,
   pero sí lo son las sombras de `~/.local/share/applications` que el punto 1
   reescribe.

### 4.2 Dependencias y qué rompe si cambia

| Si cambia… | Qué nos pasa |
|---|---|
| SLSDeck se instala en una Deck con nuestra pila | §4.1-1 en la primera carga del plugin; -2 si se pulsa Quick Install |
| SLSDeck se desinstala | §4.1-3: nuestro SLSsteam y el wrapper desaparecen |
| `cloudredirect-moon@master` | lo que cargue el Steam del usuario si §4.1-4 pasó |
| Hubcap, lua.tools, Ryuu, `manifest.luastools.xyz` | lo mismo para los dos; SLSDeck además dona códigos a ese archivo |
| el plugin, sin estar instalado | nada |

### 4.3 Lo que SLSDeck tiene y nosotros no (y al revés)

| Función | SLSDeck | Nosotros | Notas |
|---|---|---|---|
| 1 | motor ajeno (moon) + Headcrab + `steam.sh` parcheado y re-parcheado | SLSsteam de serie + lumalinux + wrapper sin tocar `steam.sh` | modelo `steam.sh` = línea roja (§4.4 S16) |
| 3 | desbloqueo dentro del juego (SmokeAPI, CreamAPI, proxy por juego) | SLSsteam de serie ya responde las comprobaciones del juego | `slsteam-moon.md` §2.3: moon lo quitó |
| 4 | "free providers", Charon, DepotDownloader | cascada de manifests validada; Steam descarga | §4.4 S9, S20 |
| 6 | hipervisor, Tokeer, Proton propio, HVAuto | fixes por botón | líneas rojas |
| 9 | botones en la tienda, Workshop, Nexus, colecciones | Decky + CDP solo para Ryuu | |
| 11 | update por Decky desde dentro del plugin | zip en `~/Downloads` + instalar desde zip | §4.4 S19 |
| 11 | vigilante de 5 min que repara | guardián systemd de cobertura | el suyo deshace el nuestro (§4.1-1) |

### 4.4 Decisiones ya tomadas (del doc anterior, con fecha) y estado hoy

| # | Decisión | Fecha | Hoy |
|---|---|---|---|
| S1 | ¿Lee Steam `config/depotcache`? Pregunta previa a todo lo demás | 2026-08-26 | **contestado**: no lo lee (medido 2026-09-11, `nosotros.md` §0); el archivo propio de LumaDeck repone `depotcache` |
| S2 | `downloads.py` `extractall()` de un zip de terceros como root | 2026-08-26 | **no es hallazgo**: `zipfile` de Python quita las rutas absolutas y los `..` al extraer y no crea symlinks; sigue en `downloads.py:1083` y `self_update.py:127` |
| S3 | Detectar los `*.slsdeck-disabled` y ofrecer la vuelta | 2026-08-26 | sin hacer (`nosotros.md` §4.1-34) |
| S4 | `setup.sh` sin checksums | 2026-08-26 | sin decisión (`nosotros.md` §4.1-29) |
| S5 | Credenciales con permisos por defecto (`0600`) | 2026-08-26 | sin decisión (`nosotros.md` §4.1-30) |
| S6 | `self_update.py` crea `~/Downloads` como root sin `chown` | 2026-08-26 | sin cambio (`self_update.py:207-220`) |
| S7 | Depots compartidos detectados por cabecera y lista, no por señal viva | 2026-08-26 | **superado**: desde el hook v1.0 servir cualquiera no corrompe; los redists van a `keys.txt` y nunca se pinean (`nosotros.md` §2.2) |
| S8 | DLC capas 2 y 3 | 2026-08-26 | corregido 2026-10-05: la capa 2 la da SLSsteam de serie, la 3 la tenemos (medido); la 1 es `AdditionalApps` por DLC (RESEARCH §21) |
| S9 | Descarga directa como respaldo (builds viejos, DLC sin dueño) | 2026-08-26 | descartado por diseño: Steam descarga |
| S10 | Provisión anónima de appinfo (puerta 2) | 2026-08-26 | aparcado |
| S11 | Detección de clave mala (puerta 5b) | 2026-08-26 | aparcado |
| S12 | Firmar y ampliar el feed de RVAs | 2026-08-26 | rechazado 2026-08-18/09-01 (feed sin firma deliberado; matriz §3.1) |
| S13 | Recuperar un prólogo movido sin rederivar a mano | 2026-08-26 | aparcado (`maintenance.md`) |
| S14 | Vigilar la beta de Steam como alerta temprana | 2026-08-26 | **en parte**: `probe-steam.yml` sondea las betas a mano (medido 2026-10-01) |
| S15 | Steamless automático al lanzar | 2026-08-26 | sin cambio 2026-09-22: por botón |
| S16 | Líneas rojas: Tokeer, módulo de kernel sin firma, interceptor de credenciales por CDP, modelo `steam.sh` | 2026-08-26 | vigentes |
| S17 | Destino de fixes sin distinguir mayúsculas | 2026-09-12 | **hecho** (LumaDeck `f67b4ec`; `fixes.py:193`) |
| S18 | Subir `@decky/ui` para la beta de Steam de septiembre | 2026-09-17 | abierto: sigue 4.11.1 |
| S19 | Actualizar por Decky desde dentro del plugin | 2026-09-28 | sin hacer |
| S20 | No adoptar: "keyless providers", OAuth de CloudRedirect con secretos raspados, Hubcap Updates, Zapret, Nexus | 2026-09-12 → 09-28 | vigente |

---

## §5 Historial

### 5.1 Lo que se creía y ya no es así

- **"La desactivación de lumalinux pasa al pulsar Install."** Quick Install la
  hace sin confirmación, y el desmontaje del wrapper y la cobertura ocurre en
  cada carga del plugin sin pulsar nada (§4.1-1, -2).
- **"Cuatro sitios de extracción sin guarda, el `.so` del motor incluido."**
  Siguen `extractall` en `slssteam.py:1896`, `tokeer.py:442`,
  `ubisoft_packages.py:169`, `zapret.py:137`, `hv/core.py:550` y
  `hv/operations.py:851,880` (los `zipfile` sin `..` por la propia
  biblioteca). `creamysteamy.py:106` es `tarfile` sin filtro: ese sí sigue
  `..` y symlinks.
- **"`get_user_home` con una lista de nombres y `/home/deck` fijo".** Ya
  puntúa usuarios reales (`paths.py:53-90`).
- **"Dos sistemas de nube vivos (CloudRedirect y OpenSave)".** OpenSave se
  retiró el 09-13.
- **"`hypervisor.py` carga el módulo".** Lo hace `lt/hv/`; `hypervisor.py` no
  tiene llamadores.
- **"Sin tests".** Hay 17 ficheros; ningún CI los ejecuta.
- **"El NameError de `headcrab_compatible_client` falla siempre".** Ahora lee
  antes el script cacheado; solo falla sin caché.
- **"`/tmp/SLSsteam.API` como único canal".** Prueba primero
  `/run/user/<uid>/SLSsteam/api` y `~/.cache/SLSsteam/api`.

**Lo que sigue sin medir**:

- §4.1-1 en una Deck con nuestra pila: qué queda cargado tras el primer
  reinicio de Steam.
- Si moon con SLSsteam de serie en la misma carpeta convive o se pisan los
  `.so`.
- Qué hace el `cloud_redirect.so` de moon con SLSsteam de serie.
- Si existe aún la release `update-system-v0.9.68` que el banner ofrecería.

### 5.2 Pruebas y mediciones con fecha

#### Función 1 — Engancharse a Steam

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-08-26 | moon `d3402a1`, contenedor x86-64 sin Steam | sus 51 `make test-*` | 42 pasan, 1 falla (`test-thread-start`: "join failure does not reopen state before worker exit"), 6 sin OpenSSL/libcurl de 32 bits, 2 piden un `steamclient.so` real | tests propios: evidencia débil (circular) |
| 2026-08-26 | SLSDeck `c243cf3` | importar los 59 módulos con `decky` simulado; `pyflakes` | importan todos; 36 avisos, 1 nombre indefinido (`ensure_http_client`) | LumaDeck: 11 avisos, 0; `pytest` 108 pasan |
| 2026-10-01 | logs de dos usuarios (mj, huh) | tres pasadas del instalador en una tarde | cada pasada quita `BootStrapperInhibitAll` con Steam vivo; en una el updater de Valve ganó la carrera: Deck medio actualizada, OOBE de SteamOS, moon abortando ("Failed to find all patterns"), `steamclient.so` build-id `d9f8d233…` | `core-install-fix` reduce pasadas, no cierra la ventana |
| 2026-10-01 | `probe-steam.yml` de lumalinux (runs 3-7) | qué sirve Valve | Deck estable 1788652215 `bc54101b` (build-id `a577b836`); escritorio estable 1788652215 `237495b4` (`29734b56`); betas 1790721607 `a3661f5b` (`e6de8467`); `steam_client_steamdeck_{main,preview,beta}` 404 | `d9f8d233` no es ninguno: probablemente una beta de Deck ya sustituida |
| 2026-10-01 | `headcrab.sh` vivo | qué fija | `HeadcrabCompatibleClientVer=1788652215`; Deck → `bc54101b`, escritorio → `237495b4` | en una Deck baja 170 MB para dejar lo mismo |

#### Función 4 — Claves y manifests

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-12 | los "keyless providers" (`6e36758`) | si sirven builds actuales | uno muerto, dos son una foto de 2025-07-26, uno no comprobable | no adoptados (§4.4 S20) |

#### Función 9 — Añadir y quitar un juego

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-08-26 | normalización de los dos backends (comentarios, docstrings, imports) | ¿hay código copiado de LumaDeck? | no: lo común viene de DeckTools, el ancestro de los dos | |
| sin fecha | informes públicos de usuarios | "Could not back up steam.sh"; juego con etiqueta SLS y sin biblioteca; instalación instantánea con 0 B; `path/steam` renombrado a `.bak` y Steam sin arrancar en Desktop | cada síntoma casa con un camino del código (`steam.sh` disputado, clave cero de moon con Steam abierto, renombrados sin reparación) | sin verificar; un revisor nombró LumaDeck al desarrollador dos veces |

### 5.3 Cronología

- **2026-07-22.** Se analiza `Kaal31/slsdeck` v0.01 (otro repo, desaparecido).
- **2026-08-20.** `6da03a1`, volcado de SLSDeckUniversal 0.9.59: ya trae la
  detección y desactivación de lumalinux. `65232aa` transporte "estilo Luma"
  (docstring que citaba LumaDeck, borrado después). Rama `lumainject`.
- **2026-08-20 → 26.** 369 commits en 7 días (125 el 08-22); `c243cf3` 0.9.61.
- **2026-09-09 → 13.** Reacción al cierre de códigos: "keyless providers";
  Headcrab y moon; OpenSave retirado; Workshop; purga.
- **2026-09-15/17.** Decky 3.2.9; Hubcap Workshop; Tokeer en `git.lua.tools`;
  Hubcap Updates (`458271a`).
- **2026-09-22.** `dlc-fix` 0.9.64: política de DLC persistida, moon sin
  `DisableUpdates` (`AutoUpdateApps`, `Donate`).
- **2026-09-26/27.** `4c15480` sistema de updates por Decky a `main`;
  `vpn-bypass` (Zapret, DoH/Tor para Hubcap).
- **2026-09-28/30.** Nexus, colecciones de lua.tools, EA Tokeer, créditos
  (`cc269af`).
- **2026-10-01.** `core-install-fix` `a2b85eb`.
- **2026-10-06.** `install-safety` `fd2a5a3`; `decky-update` `fdce91c`
  (0.9.64, selector de carpeta para Decky 3.2.10). Nada después.
