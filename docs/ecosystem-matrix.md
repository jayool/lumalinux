# Matriz del ecosistema — qué hace cada programa, función por función, contra nosotros

Creada 2026-10-08, vacía a propósito: se rellena programa a programa conforme se releen. Un solo sitio para contestar "¿hay algo que ellos hacen y
nosotros no?" sin leerse los once docs de análisis (unas 19.000 líneas,
organizadas por programa y por fecha, no por función).

**Cómo se usa**

- §1 fija las doce funciones y, dentro de cada una, la lista de control: lo que
  hay que mirar en cada programa para dar la celda por completa.
- §2 es la matriz: una tabla por función, una fila por programa, "nosotros"
  siempre la primera. Cada celda dice **cómo** lo hace en pocas líneas y enlaza
  a la sección del doc del programa donde está el detalle y la medición.
  `n/a` = el programa no juega en esa función (y eso también es un dato).
  `?` = pendiente de relectura (§4).
- §3 es el registro de huecos: lo que ellos tienen y nosotros no, y al revés,
  cada uno con la decisión tomada (hecho, aparcado, descartado) y la fecha.
  Nada entra en §3 sin su fila en §2.
- §4 dice, por programa, qué doc lo cubre, cuándo se barrió por última vez y si
  ya se ha releído **por la matriz** (desde el código, función a función) o
  solo tiene los barridos antiguos.

**Regla de mantenimiento**: cada relectura o barrido rellena primero su columna
aquí y después añade el delta con fecha en el doc del programa. Un doc de
programa sin su columna al día no cuenta como analizado.

**Nosotros** = lumalinux (el `.so` y `setup.sh`) + SLSsteam (de AceSLS, tal como
lo configuramos) + LumaDeck (el plugin de Decky). Cada celda de "nosotros" dice
en cuál de los tres vive la función.

---

## §1 Las doce funciones y su lista de control

| # | Función | Lista de control (qué mirar en cada programa) |
|---|---|---|
| 1 | **Engancharse a Steam** | Cómo entra (LD_PRELOAD, LD_AUDIT, DLL, code cave, wrapper, flatpak). Cómo localiza lo que parchea (patrones, RTTI, RVAs fijos, feed remoto, xref de strings). Qué pasa cuando Steam actualiza (gate de hashes, SafeMode, se rompe, se apaga solo). Cómo se recupera (crash guard, kill switch, downgrade, logs). A qué procesos se limita. |
| 2 | **Propiedad y licencias** | Spoof de propiedad de apps. Inyección de packages o licencias y cuándo se reaplica. Qué hace con juegos que la cuenta posee de verdad. Family sharing. Depots compartidos (redists, 228980). |
| 3 | **DLC** | Criterio de unlock (todo, lista, por juego en marcha). DLC comprado de verdad. DLC sin depot (solo flag). Límites (los 64 de Steam). Cómo se quita. |
| 4 | **Claves y manifests** | De dónde salen las claves y dónde se guardan. De dónde salen los manifests (hubs con clave, generadores con sesión, archivos públicos, propios) y en qué orden. Códigos de petición de manifest: proveedores, cuota, qué pasa si mueren. Qué deja de funcionar si cae cada servicio. |
| 5 | **Updates de juegos** | Pins o congelación. Actualización automática y cómo decide. Cambio de versión a un build antiguo. Qué pasa con una update que Steam ya empezó. |
| 6 | **Fixes y DRM** | Denuvo (fuentes, cuenta). Steamless. Goldberg u otros emuladores. EOS. Cómo se aplican y se quitan. |
| 7 | **Logros, stats y tiempo de juego** | Dónde se guardan. Si sincronizan entre máquinas y por dónde. Convivencia con el unlocker (quién contesta GetUserStats). Esquemas de logros. |
| 8 | **Cloud saves** | Redirección y proveedores. Convivencia con Steam Cloud. |
| 9 | **Añadir y quitar un juego** | Entradas: appid, nombre, página de tienda, zip, lua suelto. Qué escribe al añadir. Qué limpia al quitar y qué deja. Varias bibliotecas y SD. Nombre de carpeta. |
| 10 | **Credenciales y proveedores** | Qué cuentas o claves exige, cómo se obtienen, caducidad y cómo la detecta, qué funciona sin ninguna. |
| 11 | **Mantenimiento propio** | Superficie (QAM, escritorio, CLI, web). Update de sí mismo. Update de componentes y cómo sabe la versión instalada. Salud, estado, logs legibles. |
| 12 | **Proyecto** | Plataformas. Código abierto, licencia, quién mantiene, ritmo. Servicios de terceros de los que depende. Telemetría, privacidad, riesgo de detección. |

---

## §2 La matriz

Programas (columna de §4 para el doc de cada uno): **nosotros**, SLSsteam,
SteaMidra/SFF, LumaCore, LuaTools + BetterSteamTools, ASSella, SLSDeck,
SteamFlipper, slsteam-moon, plugins de SLSsteam, CloudRedirect.

### 1. Engancharse a Steam

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | wrapper `~/.local/share/SLSsteam/path/steam` escrito por `setup.sh`: `LD_AUDIT` (SLSsteam) + `LD_PRELOAD` (lumalinux, CloudRedirect); cobertura Desktop por `.desktop` sombreados y guardian systemd, Game Mode por drop-in de `steam-launcher.service`. lumalinux localiza cinco funciones de `steamclient.so` por feed de RVAs → patrones únicos → rescates (RTTI, xref, anchor); hash SafeMode solo advisory; CI diaria con PR automático de hashes y RVAs. Recuperación: guard anti crash-loop (vanilla), kill-switches, `setup.sh`, downgrade por Desktop (no ejercitado). LumaDeck: estados por componente. | `nosotros.md` §2.1 |
| SLSsteam | `LD_AUDIT` (rtld-audit), solo en el proceso `steam`. Localiza por 18 patrones de bytes (último match) + RTTI y xref de `TraceIPC` para derivar los índices de vtable + 5 índices fijos; 37 hooks (15 detours, 22 VMT sobre objetos vivos, colocados en el primer `RunInterface`). Al actualizar Steam: gate de hash SafeMode opcional (off por defecto; fail-closed sin feed; `version.txt` congelado desde 09-03), si no aborta limpio o crashea. Recuperación: solo `~/.SLSsteam.log` y toasts; sin kill-switch en caliente ni crash guard. | `slssteam.md` §2.1 |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 2. Propiedad y licencias

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | SLSsteam spoofea por `AdditionalApps`/`FakeAppIds` (los escribimos nosotros). lumalinux inyecta los ids de depot de `keys.txt` en el package-0 en memoria cada 15 s y al cambiar el fichero, retira los que salen (v0.22.2) y dispara `NotifyLicensesUpdated` con el `CUser*` capturado del guard de SLSsteam (sin reiniciar). Juegos poseídos: LumaDeck decide desde `packageinfo.vdf`, añade solo DLC, nunca pinea depots con licencia real. Family sharing: nada. Redists: en `keys.txt`, nunca pin ni `AdditionalApps`. | `nosotros.md` §2.2 |
| SLSsteam | post-hook de `CheckAppOwnership`: `unlockApp` solo para `AdditionalApps` (tras el primer `GetSubscribedApps`), decensor y región para todas; `GetSubscribedApps` añade `AdditionalApps` sin deduplicar. Sin packages ni licencias (solo en la rama `update`, nunca mergeada); `AppTokens` en PICS; `AppLicensesChanged_t` solo en caliente. Poseídos de verdad: `isSubscribed` = trampolín de `CheckAppOwnership` (propiedad real según Steam) protege DLC, logros, cloud, updates, CD keys. Family sharing: descarta 9406 y `NotifyRunningApps`, `GetAppStateInfo` sin `Borrowed`, `owner_id=1`. Redists: nada (solo loguea `sharedDepots`). | `slssteam.md` §2.2 |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 3. DLC

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | unlock: SLSsteam (todo DLC no poseído del juego en marcha). LumaDeck: `DlcData` solo >64 DLC; alta y baja por la casilla de Steam (`SetDLCEnabled`, `DisabledDLC`); DLC comprado se deja a Steam en todos los caminos; DLC sin depot → `AdditionalApps`; quitar un DLC de un juego added exige reinicio. | `nosotros.md` §2.3 |
| SLSsteam | `shouldUnlockDlc`: todo DLC no poseído de verdad y no excluido (`AppIds`/`UseWhitelist`, herencia por `parent`) mientras el pipe sea de un juego; contesta `CheckAppOwnership`, `IsAppDlcInstalled`, `BIsDlcEnabled` (sobre el padre), `IsUserSubscribedAppInTicket`. Comprado: se respeta. Sin depot: flag. Sin límite propio: `DlcData` suple la lista que Steam no entrega (sin llamar al original). Se quita por config (hot reload). | `slssteam.md` §2.3 |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 4. Claves y manifests

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | claves del lua a `keys.txt` (tres formatos) + `config.vdf`, servidas por el hook DepotKey; `retired_keys.txt` para borrar. Manifests: depotcache → archivo propio → luastools → Hubcap suelto (1500/día, 1 intento por depot+gid/día) → zip (Hubcap 25/día, Ryuu; Sushi/Spinoza off). Códigos GMRC: hook de lumalinux con 20770407, manifestdex, wudrm, steam.run (1 req/s, backoff, verificación en el CDN, `gmrc.json`). Sin proveedores: todo congelado al build instalado. | `nosotros.md` §2.4 |
| SLSsteam | sin claves de depot, sin manifests externos, sin códigos de petición (todo eso solo en la rama `update`). `ManifestIds` (pin por depot) y `DepotBlacklist` (con bug de swap) en el post-hook de `BuildDepotDependency`; `CDKeys` legacy (o clave determinista) vía `SetLegacyCDKey`. Servicios: GitHub/jsDelivr (solo SafeMode), tienda de Steam (reseñas para logros). | `slssteam.md` §2.4 |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 5. Updates de juegos

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | pin vivo = `ManifestIds` de SLSsteam (`keys.txt` gid/size dormido, BuildDep OFF). `pins.py`: pase local 60 s (heal, modelo up/down por `gmrc.json`), pase de update 30 min contra `api.steamcmd.net`, pin solo con todos los manifests; congelación por usuario, ficheros (fixes) o proveedores. Versión antigua: SteamDB (10 builds, BrowserView oculto) + `--set-pin` + `StateFlags|=2`; también por LuaTools. Update atascada: `UpdateResult=8` → Fix Update; sin cancelación. | `nosotros.md` §2.5 |
| SLSsteam | `ManifestIds` para fijar o bajar de versión; `DisableUpdates: yes` (default) → `GetUpdateInfo` false para `AdditionalApps` o no poseídas de verdad (hot reload). La automática la decide Steam; no toca `buildId`; sin cancelación de updates en curso. | `slssteam.md` §2.5 |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 6. Fixes y DRM

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | LumaDeck: catálogo lua.tools (cuenta Discord; slot manifest + fix), fixes sin cuenta de files.luatools.work, Steamless (.NET 9, sin deshacer), Goldberg, proxy EOS, toggle Online (480 + netsock + EOS), spliced tickets (plugin Lua de Ace); backups por fichero y log `[FIX]`; cualquier escritura congela el juego. | `nosotros.md` §2.6 |
| SLSsteam | sin fixes (ni Steamless, Goldberg ni EOS). Tickets de propiedad (858) y cifrados (5527) cacheados en `<config>/cache`, reinyectados en `LaunchApp` y si el servidor falla; SteamID que ve el juego: `SteamIdOverride` → spoof de una vez (`SmartTickets`: SteamDRM/Denuvo por entropía en `ConnectPipe`) → ticket cifrado; `DenuvoGames` no finge propiedad de otra cuenta; `FakeAppIds` (AppId real para stats/cloud/appinfo, falso para amigos/matchmaking/presencia); `LaunchOptions` (`%command%`); `ticket-grabber`. | `slssteam.md` §2.6 |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 7. Logros, stats y tiempo de juego

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | SLSsteam consigue el esquema por el borrow con reseñadores (sin almacén propio: guarda Steam) y lo salta si `isSubscribed`, que con nuestra inyección de package-0 es true; lumalinux repunta ese guard a `subscribed && !added` (escritura atómica) para que los juegos añadidos entren. CloudRedirect sincroniza stats y logros por blob en Drive (una descarga por arranque); `cr_stats_fix` sobra desde CR 2.6.6. LumaDeck: generador de esquemas apagado. Pendiente: endpoint de tienda retirado el 2026-10-22, esperar SLSsteam. | `nosotros.md` §2.7 |
| SLSsteam | sin almacén propio (guarda Steam). Borrow de esquemas solo para no poseídas de verdad (guard `isSubscribed`, el que lumalinux repunta): reescribe `GetUserStats` (legacy y `Player.GetUserStats#1`) con SteamIDs de reseñadores de `store.steampowered.com/appreviews` (`MaxSchemaTries`, blacklist, `preferredOwners`, cooldown 10 min; `NoConnection` como fallback); vacía los stats de la respuesta. Borra las `AdditionalApps` de `ClientGetLastPlayedTimes`. Sin sincronización. `schema-grabber` offline. Endpoint de tienda cambia en `dev` (retiro anunciado 2026-10-22). | `slssteam.md` §2.7 |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 8. Cloud saves

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | CloudRedirect (componente core de `setup.sh`; Drive/OneDrive/Dropbox; app flatpak para el login). `DisableCloud: no` en SLSsteam. LumaDeck solo observa (`not_authed`, `disabled`). `libcurl_pin` para nuestras propias descargas. | `nosotros.md` §2.8 |
| SLSsteam | `DisableCloud: yes` (default) → `IsCloudEnabledForApp` false para no poseídas de verdad; sin redirección ni proveedores; el YAML remite a CloudRedirect. | `slssteam.md` §2.8 |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 9. Añadir y quitar un juego

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | entradas: AppID, página abierta (CEF), nombre (tienda de Steam); sin zip local; sin elección de disco. Add: zip por `api.json` → `_process_and_install_lua` (orden del lua, depot Linux) → `steamidra_lite` (depotcache, `AdditionalApps`, `keys.txt`, `config.vdf`, `.acf`, `stplug-in`) → post-check → Proton forzado; sin reinicio (reconcile). Quitar: carpeta, `.acf` en todas las bibliotecas, compatdata/shadercache (raíz + biblioteca), depotcache, config de SLSsteam, claves (retiradas si poseído); deja `config.vdf`, `pins.json`, archivo propio. Bibliotecas: unión de los dos `libraryfolders.vdf` con tres puntos ciegos. | `nosotros.md` §2.9 |
| SLSsteam | entrada: solo AppId en el YAML (a mano, por Lua `setAdditionalApps`, o `install|app|lib`/`uninstall|app` por el fichero de comandos); no escribe nada en disco; no limpia nada (tickets, `cdk_<n>`); `AppLicensesChanged_t` en caliente; bibliotecas por índice (`dumplibraries`); carpeta la decide Steam. | `slssteam.md` §2.9 |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 10. Credenciales y proveedores

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | Hubcap (clave, `/user/stats`, borrable), Ryuu (sesión de Discord por CDP o SQLite del CEF, verificada con `data-user-id`, viva cada hora), LuaTools (Supabase con refresh), Web API de Steam (apagada). Sin ninguna: no se añade; actualizar sigue por depotcache, archivo y luastools. | `nosotros.md` §2.10 |
| SLSsteam | solo una sesión de Steam; opcionales `AppTokens`, `CDKeys`, SteamIDs (`DenuvoGames`, `SteamIdOverride`) y tickets; caducidad no detectada (tickets reutilizados sin mirar antigüedad); `ticket-grabber`/`schema-grabber` piden usuario y contraseña en argv + Steam Guard. | `slssteam.md` §2.10 |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 11. Mantenimiento propio

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | QAM + Settings de Decky (pestaña Dev siempre visible); CLI `setup.sh`/`steamidra_lite.py`. Update propio: zip a `~/Downloads`. Componentes: versión del `.so` en disco (CloudRedirect, lumalinux), tag grabado (SLSsteam), GitHub con caché 6 h, aplicar = `setup.sh` entero. Salud: `status.json`, `gmrc.json`, crash guard, pin de headcrab; logs en lumalinux, SLSsteam y Decky. CI: tests, autotest de rederivación, smoke test nunca verde. | `nosotros.md` §2.11 |
| SLSsteam | YAML con hot reload (inotify, `IN_CLOSE_WRITE|IN_MOVED_TO`; claves faltantes → toast, desconocidas ignoradas); fichero de comandos `/tmp/SLSsteam.API` (7 comandos; `API: no` en el YAML instalado; cualquier proceso del usuario puede escribirlo; roto 09-28→10-01); plugins Lua sin sandbox (`Plugins: no`, 0700 forzado, hot reload sin recrear estado en release); toasts por `notify-send`. Sin auto-update (`update.cpp` solo baja hashes). Versión = `VERSION` embebida (no sube en cada tag) + rama/commit en el log. Releases 7z/zip + PKGBUILD + Nix, sin strip. | `slssteam.md` §2.11 |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 12. Proyecto

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | SteamOS/Deck; CachyOS en curso; Flatpak no. GPL-3 (lumalinux, con `update.cpp` AGPL de SLSsteam), LumaDeck sin LICENSE; releases por tag. Terceros: GitHub, AceSLS, Selectively11, Valve, api.steamcmd.net, SteamDB, Hubcap, Ryuu, luastools, lua.tools, proveedores GMRC (tabla en `nosotros.md` §4.2). Sin telemetría; TLS sin verificar como último recurso; riesgo de detección el de SLSsteam. | `nosotros.md` §2.12 |
| SLSsteam | Linux x86 32-bit en `steam` (nativo, Flatpak, SteamOS/Deck); AGPL-3.0; un mantenedor, ~1 tag cada 4 días, historia reescrita el 07-21, rama `dev` activa; terceros GitHub/jsDelivr, tienda de Steam, `curl` y `notify-send` del sistema, `/proc`; sin telemetría; borra el control parental siempre; detección: detours en `steamclient.so`, paquetes CM modificados y descartados, stats de otros usuarios desde la sesión propia. | `slssteam.md` §2.12 |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

---
## §3 Registro de huecos

Cada entrada nace de una celda de §2. Estado: **hecho**, **aparcado** (con la
condición para desaparcarlo), **descartado** (con el motivo), **pendiente**.
Se rellena cuando la relectura de un programa termina, no antes.

### 3.1 Ellos sí, nosotros no

| Función | Programa | Qué | Decisión | Fecha |
|---|---|---|---|---|
| 6 | SLSsteam | tickets cacheados y SteamID suplantado (`SmartTickets`, `SteamIdOverride`, `DenuvoGames`, `ticket-grabber`) | aparcado hasta terminar las relecturas: dejamos el default `SmartTickets: 1`, no escribimos tickets ni SteamIDs | 2026-10-08 |
| 6 | SLSsteam | `LaunchOptions` por juego (`%command%`; comodines no poseídos / todos) | aparcado: no lo escribimos; nuestros fixes se aplican en disco | 2026-10-08 |
| 2, 3 | SLSsteam | `AppIds`/`UseWhitelist` para excluir apps o DLC del unlock (con herencia por `parent`) | aparcado: no lo escribimos; LumaDeck gestiona DLC por `DisabledDLC` de Steam | 2026-10-08 |
| 4 | SLSsteam | `CDKeys` (clave legacy inyectada o determinista) | aparcado: con `isSubscribed` true para nuestros juegos no se inyecta (`slssteam.md` §4.1-1); sin caso conocido | 2026-10-08 |
| 9, 11 | SLSsteam | `install|app|lib`, `uninstall|app`, `setcompat` por `/tmp/SLSsteam.API` | descartado por ahora: canal sin autenticación, off por defecto y roto en 20260930144343; Proton lo fijamos en `localconfig.vdf` | 2026-10-08 |

### 3.2 Nosotros sí, ellos no

| Función | Qué | Quién más lo tiene | Notas |
|---|---|---|---|
| 4 | claves de depot (`keys.txt` + `config.vdf`), manifests por proveedor, códigos GMRC | SLSsteam: no (solo en su rama `update`, nunca mergeada); resto pendiente de relectura | `slssteam.md` §2.4 |
| 2 | package-0 en memoria + `NotifyLicensesUpdated` | SLSsteam: no (spoof por app en `CheckAppOwnership`) | `slssteam.md` §2.2 |
| 5 | updates por proveedor (`pins.py`, `api.steamcmd.net`, Fix Update) | SLSsteam: solo `ManifestIds` + `DisableUpdates` | `slssteam.md` §2.5 |
| 6 | fixes (lua.tools, Steamless, Goldberg, EOS) | SLSsteam: ninguno | `slssteam.md` §2.6 |
| 7 | sincronización de logros entre máquinas (CloudRedirect) | SLSsteam: ninguna | `slssteam.md` §2.7 |
| 11 | UI (QAM, Settings), update propio y de componentes, estado | SLSsteam: YAML + fichero de comandos + Lua; sin auto-update | `slssteam.md` §2.11 |

### 3.3 Fuentes retiradas

| Función | Qué | Cuándo | Motivo |
|---|---|---|---|

---

## §4 Estado de relectura por programa

"Barrido" = delta por fechas (lo que había hasta ahora). "Relectura por la
matriz" = desde el código actual, las doce funciones, y contraste con el doc
viejo: lo que el doc afirma y el código no sostiene se marca o se borra.
Una celda solo se rellena cuando la relectura la ha sacado del código y lo
dice en su columna Fuente; hasta entonces es `?`.

| Programa | Doc | Último barrido | Relectura por la matriz | Orden propuesto |
|---|---|---|---|---|
| SLSsteam | `slssteam.md` | 2026-10-07 | **hecha 2026-10-08** desde el código (`main@049bbdd`, release `20261001163836`); `slssteam-analysis.md` absorbido (mediciones en §5.2, afirmaciones en §5.1) | 1 |
| SteaMidra/SFF (drappula) | `steamidra-linux-analysis.md` | 2026-10-07 | pendiente | 2 |
| LumaCore (drappula) | `lumacore-findings.md` | 2026-10-07 | pendiente | 3 |
| LuaTools + BetterSteamTools (+ OpenSteamTool) | `luatools-app-analysis.md`, `bettersteamtools-findings.md` | 2026-10-07 | pendiente | 4 |
| ASSella | `assella-analysis.md` | 2026-10-07 | pendiente | 5 |
| SLSDeck | `slsdeck-analysis.md` | 2026-10-05 | pendiente | 6 |
| SteamFlipper | `steamflipper-analysis.md` | 2026-09-28 | pendiente | 7 |
| slsteam-moon | `slsteam-moon-findings.md` | 2026-10-07 | pendiente | 8 |
| plugins de SLSsteam | `slssteam-plugins-analysis.md` | 2026-09-29 | pendiente | 9 |
| CloudRedirect | `cloudredirect.md` | 2026-10-07 (2.6.6) | pendiente | 10 |
| nosotros | `nosotros.md` (+ `RESEARCH.md`, `maintenance.md`, docs de LumaDeck) | — | **hecha 2026-10-08** desde el código (lumalinux `df22459`, LumaDeck `c4776ce`); `method.md` y `owned-games-guide.md` absorbidos | 0 |
