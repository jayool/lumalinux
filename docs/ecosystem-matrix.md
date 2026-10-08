# Matriz del ecosistema — qué hace cada programa, función por función, contra nosotros

Creada 2026-10-08. Un solo sitio para contestar "¿hay algo que ellos hacen y
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
| **nosotros** | **lumalinux**: `liblumalinux.so` (32 bits) entra por `LD_PRELOAD` desde el wrapper `~/.local/share/SLSsteam/path/steam` que escribe `setup.sh` (también funciona como `LD_AUDIT`; el constructor sondea `/proc/self/maps` hasta ver `steamclient.so`). Localiza sus cinco hooks por patrones de bytes + RTTI + xref de strings (nombre del job GMRC, `NotifyLicensesUpdated`), con un feed remoto de RVAs por build (`res/rvas/`) y el gate SafeMode copiado de SLSsteam (`updates.yaml`: hash de `steamclient.so` desconocido → hooks apagados, `status.json` `blocked=hash_unverified`). Solo actúa en el proceso `steam` (`proc_filter`). Crash guard compartido con el launcher: bucle de fallos → se desactiva y LumaDeck muestra "Recovery mode". CI (`probe-steam`, `watch-steam`) rederiva patrones y abre PR. **SLSsteam**: mismo wrapper, su propio SafeMode. **LumaDeck**: salud por componente (`not_loaded`, `not_injected`, `not_supported`), "Fix in Desktop" = downgrade de Steam + reinyección (`desktop_handoff`), pin de compat de Headcrab. | `RESEARCH.md` §4-§8, `rva-feed-design.md`, `maintenance.md`, `update-resilience-comparison.md` §1 |
| SLSsteam | ? | `slssteam-analysis.md` §0-§1, §3 |
| SteaMidra/SFF | ? | `steamidra-linux-analysis.md` |
| LumaCore | ? | `lumacore-findings.md`, `RESEARCH.md` §11 |
| LuaTools + BST | ? | `luatools-app-analysis.md`, `bettersteamtools-findings.md` |
| ASSella | ? | `assella-analysis.md` |
| SLSDeck | ? | `slsdeck-analysis.md` |
| SteamFlipper | ? | `steamflipper-analysis.md` |
| slsteam-moon | ? | `slsteam-moon-findings.md` |
| plugins SLSsteam | ? | `slssteam-plugins-analysis.md` |
| CloudRedirect | `cloud_redirect.so` (32 bits) por `LD_PRELOAD` desde el mismo wrapper (lo instala `setup.sh`). `DoInit` busca `steamclient.so` en `/proc/self/maps` y la vtable del transporte de RPC; si no la encuentra aborta limpio (`Init failed: transport vtable not found`) y lo demás de Steam sigue. 2.6.6: símbolos ocultos (`-fvisibility=hidden`), libcurl y OpenSSL estáticos. Logs: `cr_debug.log` (solo `DoInit`) y `cloud_redirect.log` (todo lo demás). | `cloudredirect.md` (2.6.6, 2026-10-07) |

### 2. Propiedad y licencias

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | **SLSsteam**: spoof de propiedad (`CheckAppOwnership`, `getSubscribedApps`, `AdditionalApps` / `FakeAppIds`), tokens PICS. **lumalinux**: `LoadPackage` mete en el package-0 los ids de depot que hay en `keys.txt` (estilo LumaCore); `license_reconcile` lo reaplica al vuelo cuando Steam emite `LicensesUpdated` y cuando cambia `keys.txt`; los ids que salen de `keys.txt` se **retiran** del package (v0.22.2) y sus claves se siguen sirviendo sin ser licencia. **Juegos que la cuenta posee**: LumaDeck decide desde el `packageinfo.vdf` de Steam (`steam_licenses`), no desde la UI: juego poseído → se añaden solo sus DLC, el juego sigue siendo de Steam (Cloud, logros, updates intactos); un juego comprado después de añadirlo pasa a poseído solo. Depots con licencia real nunca se pinean. **Redists**: sección "SHARED DEPOTS" de los zips, 228980 no se sirve nunca. Family sharing: sin tratamiento propio. | `owned-games-guide.md`, `RESEARCH.md` §4 LoadPackage, §11.2, §21 |
| SLSsteam | ? (sabido: `shouldUnlockDlc`, `AppLicensesChanged_t` solo para apps nuevas en el diff de config; ver §7.14) | `slssteam-analysis.md` |
| SteaMidra/SFF | ? | |
| LumaCore | ? | `RESEARCH.md` §11.2 |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | n/a | |

### 3. DLC

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | **SLSsteam**: desbloquea cualquier DLC no poseído mientras el juego corre (`shouldUnlockDlc`), sin mirar `AdditionalApps`. **LumaDeck**: lista de DLC por `appdetails`; alta y baja a través de la casilla de DLC de Steam (`DisabledDLC`); en un juego poseído, DLC = `AdditionalApps` + claves de sus depots, y los DLC que la cuenta ya tiene se dejan a Steam (`licensed_appids`); DLC sin depot = `AdditionalApps` a secas. Límite conocido: regeneración de `DlcData` para más de 64 DLC, descartada. Quitar un DLC añadido exige reiniciar Steam (SLSsteam solo emite `AppLicensesChanged_t` para apps nuevas). | `owned-games-guide.md`, `slssteam-analysis.md` §7.14 |
| SLSsteam | ? | |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | n/a | |

### 4. Claves y manifests

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | **Claves**: `~/.config/lumalinux/keys.txt` (formato extendido `depot;app;gid;size;clave` más entradas de presencia `app;`), servidas por el hook `DepotKey`; también a `config.vdf` para Steam. Las escribe `tools/steamidra_lite.py` a partir del lua del zip. **Manifests** (`manifests.py`): `depotcache/` → archivo propio `~/.local/share/lumadeck/manifests/` → `manifest.luastools.xyz` → manifest suelto de Hubcap (`/generate/manifest`, 1500/día, un intento por depot+gid y día) → zip de un hub (`api.json`: Hubcap con clave, Ryuu con sesión, APIs libres; 25 zips/día en Hubcap). **Códigos de petición** (hook GMRC, `gmrc_store.hpp`): proveedores 20770407, manifestdex, wudrm, steam.run, con "skip until" por proveedor y sonda contra el CDN de Valve; sin proveedor vivo, `gmrc.json` pasa a `down` y LumaDeck congela los juegos con pins. **Dependencias**: Hubcap, Ryuu, luastools, `api.steamcmd.net` (PICS: gids actuales, DLC, installdir), SteamDB (versiones), GitHub (updates, feed de RVAs), los cuatro proveedores GMRC. Medido 2026-10-07: `depotkeys.json` de api.993499094.xyz tiene 32/32 de nuestras claves pero no es day-one (aparcado). | `RESEARCH.md` §4, §7, §9, §19; LumaDeck `docs/adding-and-updating-games.md` |
| SLSsteam | n/a (no sirve claves ni manifests) | |
| SteaMidra/SFF | ? | |
| LumaCore | ? | `RESEARCH.md` §11.1, §11.3, §11.5 |
| LuaTools + BST | ? (sabido: lua.tools = cuenta propia, Luie, proxies de Ryuu/TwentyTwo/Sushi/Skyflare; luastools.xyz = archivo público alimentado por BST; ver §4b) | `luatools-app-analysis.md`, `steamflipper-analysis.md` §10.1 |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | n/a | |

### 5. Updates de juegos

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | **Pins**: `setManifestid` del lua → `keys.txt` con gid y tamaño → el hook `BuildDep` parchea gid y tamaño del depot (nunca inyecta). **Pasada** (`pins.py`, al arrancar Decky y cada 30 min): compara cada pin con el build actual de Valve (`api.steamcmd.net`), consigue **todos** los manifests por la cadena de la fila 4 y mueve el pin solo con todos en mano; con `gmrc.json` en `up` los juegos van sin pin y Steam actualiza solo; en `down` quedan congelados hasta que una sonda ve un proveedor vivo. **Versiones**: selector de los últimos 10 builds públicos de SteamDB (`game_versions.py`, `versions.py`). **Update atascada**: aviso y "Fix Update" (re-fetch del zip, re-pin). SLSsteam `DisableUpdates: no`. Steam client: `steam.cfg` congelado durante la recuperación de una rotura. | LumaDeck `docs/adding-and-updating-games.md`, `update-resilience-comparison.md` |
| SLSsteam | ? (`DisableUpdates`) | |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | n/a | |

### 6. Fixes y DRM

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | **LumaDeck**: catálogo de fixes de la comunidad por cuenta LuaTools (`fixes.py`, `luatools_auth.py`; Denuvo), Steamless (`Steamless.CLI` .NET 9, runtime autoinstalado), Goldberg (`gbe_fork`, aplicar y quitar), proxy EOS (`eos_proxy.py`). lumalinux y SLSsteam: nada. | LumaDeck `FIXES_MAP.md`, `docs/managing-a-game.md` |
| SLSsteam | n/a | |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | n/a | |

### 7. Logros, stats y tiempo de juego

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | **SLSsteam** guarda stats locales; **lumalinux** `sls_achievement_unblock` le quita el bloqueo para que los logros de juegos añadidos se sirvan (por símbolos de SLSsteam.so, se degrada a no-op si cambian). **CloudRedirect** con `stats_sync_enabled` lleva su propio almacén y un blob por cuenta en Drive: los logros viajan entre máquinas por ahí (medido 2026-10-07, Deck → PC tras reiniciar Steam). `cr_stats_fix` de lumalinux (vacío de CR → passthrough) sobra desde CR 2.6.6. **LumaDeck** `achievements.py`: esquemas por la Web API de Steam (UI oculta tras `ACHIEVEMENTS_ENABLED`). Pendiente: Valve retira el endpoint de tienda el 2026-10-22; SLSsteam lleva el arreglo (`51724f5`) sin release. | `cloudredirect.md` §Stats, `slssteam-analysis.md` §7.14 |
| SLSsteam | ? | |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | Almacén propio de stats y logros; `GetUserStats` contestado desde el almacén (`handled locally`) o passthrough si está vacío (fix `e507ba4`, 2.6.6); blob por cuenta en el proveedor, descargado una vez al arrancar Steam (`SeedApps`); tiempo de juego reconciliado desde `localconfig.vdf`. Interruptores: Linux `stats_sync_enabled` (maestro) + `sync_achievements`/`sync_playtime`; Windows solo `sync_achievements`. | `cloudredirect.md` |

### 8. Cloud saves

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | Delegado en **CloudRedirect** (lo instala `setup.sh`; LumaDeck avisa si falta el login). lumalinux le pinea el libcurl del sistema (`libcurl_pin`, opción A mantenida 2026-10-07; con CR 2.6.6 estático ya solo lo usan nuestras propias descargas). | LumaDeck `docs/cloud-saves.md`, `cloudredirect.md` |
| CloudRedirect | Intercepta los RPC de Steam Cloud y los sirve desde Google Drive, OneDrive o Dropbox; inyección de cuota y changelists por app; partidas y logros por canales distintos. | `cloudredirect.md` |
| los demás | ? / n/a por confirmar | |

### 9. Añadir y quitar un juego

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | **Añadir** (QAM): por AppID, por nombre (búsqueda de la tienda de Steam, sin credencial, DLC incluidos), desde la página de tienda abierta (detección por CDP del CEF de Steam). Descarga el zip por `api.json`, `_process_and_install_lua` normaliza el lua (línea del juego primero), enriquece con depot Linux si existe, y `steamidra_lite` escribe claves, manifests, `config.yaml`, `config.vdf` y `stplug-in/`. Juego poseído → solo DLC (fila 2). **Quitar**: `.acf`, carpeta (por `.acf`, si no por `installdir` de appinfo o nombre exacto), `depotcache`, `shadercache`, claves retiradas, `AdditionalApps`, casilla `DisabledDLC`; juego poseído → solo sus DLC. **Varias bibliotecas**: análisis de cinco defectos (issue #41), pins por biblioteca. | LumaDeck `docs/adding-and-updating-games.md`, `docs/managing-a-game.md`, `docs/dev-multi-library.md` |
| SLSsteam | n/a (config a mano o por su API local) | |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | n/a | |

### 10. Credenciales y proveedores

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | Hubcap (clave, caducidad por `/user/stats`, se puede borrar), Ryuu (sesión de Discord capturada del navegador de Steam, verificada antes de guardar y en vivo cada hora), cuenta LuaTools (sesión que se renueva sola, solo para fixes), APIs libres de `api.json` (Sushi, Spinoza: estado por confirmar), clave de la Web API de Steam para esquemas de logros. Sin ninguna credencial: añadir falla; actualizar sigue por depotcache, archivo propio y luastools. | LumaDeck `docs/credentials.md` |
| los demás | ? | |

### 11. Mantenimiento propio

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | Plugin de Decky (QAM + Settings) y CLI de Quick Install; sin app de escritorio ni web. Update del plugin desde releases de GitHub; componentes por `setup.sh` entero (SLSsteam, CloudRedirect, lumalinux a la última); versión instalada leída del `.so` en disco (CloudRedirect, lumalinux) o del tag grabado (SLSsteam); cache de 6 h. Salud: `status.json` de lumalinux, crash guard, estados por componente; logs en `~/.cache/lumalinux/lumalinux.log` y los de Decky. | LumaDeck `docs/components-and-health.md`, `docs/updating-lumadeck.md` |
| CloudRedirect | App de escritorio propia (login, interruptores), `auto_update_dll` en Windows, `cloud_redirect_cli` en Linux. | `cloudredirect.md` |
| los demás | ? | |

### 12. Proyecto

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | Linux: SteamOS/Deck, port a CachyOS en curso. lumalinux GPL-3 con `update.cpp` tomado de SLSsteam (AGPL); LumaDeck y lumalinux de jayool, releases por tag con CI. Dependencias de terceros: fila 4 más SLSsteam (AceSLS) y CloudRedirect (Selectively11). Sin telemetría. Riesgo de detección: el de SLSsteam. | |
| CloudRedirect | Windows, Linux (Deck) y flatpak; código en GitHub (Selectively11), releases frecuentes; depende del proveedor de nube del usuario. | `cloudredirect.md` |
| los demás | ? | |

---

## §3 Registro de huecos

Cada entrada nace de una celda de §2. Estado: **hecho**, **aparcado** (con la
condición para desaparcarlo), **descartado** (con el motivo), **pendiente**.

### 3.1 Ellos sí, nosotros no

| Función | Programa | Qué | Decisión | Fecha |
|---|---|---|---|---|
| 4 | BST / OpenSteamTool | SDM como segundo proveedor de códigos GMRC (mismo pool que 20770407) | aparcado: dos líneas en `gmrc_store.hpp`, solo si 20770407 falla de verdad | 2026-10-07 |
| 4 | LuaTools | parser propio de `appinfo.vdf` (`AppInfoFile.cs`) en vez de `api.steamcmd.net` | aparcado: referencia guardada en `luatools-app-analysis.md` §4b | 2026-10-07 |
| 4 | OpenSteamTool | `depotkeys.json` (api.993499094.xyz) como fuente de claves | aparcado: 32/32 conocidas, no day-one; solo si aparece "zip is stale" en un log real | 2026-10-07 |
| 4 | lua.tools | manifest suelto `givemethemanifestpunk` y generador Luie | descartado: exige login en lua.tools | 2026-10-07 |
| 4 | Hubcap | manifest suelto `/generate/manifest` | hecho (`manifests.py`) | 2026-10-07 |
| 3 | SLSsteam/Steam | regenerar `DlcData` para juegos con más de 64 DLC | descartado | 2026-10-06 |
| 7 | SLSsteam `51724f5` | logros tras la retirada del endpoint de tienda (2026-10-22) | pendiente: esperar release de Ace | 2026-10-07 |
| 8 | CloudRedirect 2.6.6 | libcurl propio estático vs nuestro pin | hecho: se mantiene el pin para nuestras descargas (opción A) | 2026-10-07 |

### 3.2 Nosotros sí, ellos no

| Función | Qué | Quién más lo tiene | Notas |
|---|---|---|---|
| 2 | juegos poseídos tratados como de Steam (solo DLC, licencias reales intactas, depots con licencia nunca pineados) | por confirmar en la relectura | 2026-10-07 |
| 5 | pasada de updates con cadena de manifests y congelación por proveedores caídos (`gmrc.json`) | por confirmar | |
| 1 | feed de RVAs por build + rederivación automática de patrones en CI | por confirmar | |

### 3.3 Fuentes retiradas

| Función | Qué | Cuándo | Motivo |
|---|---|---|---|
| 4 | P-ToyStore (espejo de manifests en GitHub) | 2026-10-07 | 404 |
| 4 | TwentyTwo Cloud, Sushi (como fuentes de zip) | 2026-10-07 | muertos según `check_apis` de Ryuu; Sushi sigue en `api.json` deshabilitado |

---

## §4 Estado de relectura por programa

"Barrido" = delta por fechas (lo que había hasta ahora). "Relectura por la
matriz" = desde el código actual, las doce funciones, y contraste con el doc
viejo: lo que el doc afirma y el código no sostiene se marca o se borra.

| Programa | Doc | Último barrido | Relectura por la matriz | Orden propuesto |
|---|---|---|---|---|
| SLSsteam | `slssteam-analysis.md` | 2026-10-07 | pendiente | 1 |
| SteaMidra/SFF (drappula) | `steamidra-linux-analysis.md` | 2026-10-07 | pendiente | 2 |
| LumaCore (drappula) | `lumacore-findings.md` | 2026-10-07 | pendiente | 3 |
| LuaTools + BetterSteamTools (+ OpenSteamTool) | `luatools-app-analysis.md`, `bettersteamtools-findings.md` | 2026-10-07 | pendiente | 4 |
| ASSella | `assella-analysis.md` | 2026-10-07 | pendiente | 5 |
| SLSDeck | `slsdeck-analysis.md` | 2026-10-05 | pendiente | 6 |
| SteamFlipper | `steamflipper-analysis.md` | 2026-09-28 | pendiente | 7 |
| slsteam-moon | `slsteam-moon-findings.md` | 2026-10-07 | pendiente | 8 |
| plugins de SLSsteam | `slssteam-plugins-analysis.md` | 2026-09-29 | pendiente | 9 |
| CloudRedirect | `cloudredirect.md` | 2026-10-07 (2.6.6) | hecha 2026-10-07 (filas 1, 7, 8, 11, 12; el resto n/a) | — |
| nosotros | `RESEARCH.md`, `owned-games-guide.md`, docs de LumaDeck | — | columna rellena 2026-10-08 desde el código | — |
