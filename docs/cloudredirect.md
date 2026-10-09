# CloudRedirect (Selectively11/CloudRedirect) — leído desde el código

Leído desde el código el 2026-10-09: `master@00969da` (tag `v2.6.6`) entero en
la parte de Linux (`src/platform/linux/`, `src/common/`, `src/providers/`,
`ui-linux/`, `flatpak/`) y por encima en la de Windows; la rama `gh-pages` (el
repo Flatpak firmado), la rama `ost` y el fork `swwayps/cloudredirect-moon`
(`19da055`) por diferencias. Sustituye al doc anterior del mismo nombre: sus
mediciones con fecha están en §5.2, lo que afirmaba y el código ya no sostiene
en §5.1, y sus decisiones con el estado de hoy en §4.4. Cómo **nosotros** lo
instalamos y lo cargamos (wrapper, orden de `LD_PRELOAD`, salud en LumaDeck)
está en `nosotros.md` §2.1, §2.7 y §2.8; aquí solo entra lo que hace
CloudRedirect.

---

## §0 Ficha

| | |
|---|---|
| **Qué es** | Redirección de **Steam Cloud** para juegos sin licencia. En Linux, una biblioteca de 32 bits (`cloud_redirect.so`) que se carga en el proceso `steam` por `LD_PRELOAD`, engancha el transporte de servicios unificados de `steamclient.so` y **contesta ella** las RPC `Cloud.*` de los juegos de `AdditionalApps` de SLSsteam, guardando las partidas en el almacenamiento del usuario (Google Drive, OneDrive, S3, Cloudflare R2 o una carpeta). Opcionalmente sincroniza **logros, stats y tiempo de juego** con un blob propio por cuenta. Se completa con una **app Qt en Flatpak** (`org.cloudredirect.CloudRedirect`: login OAuth, despliegue del `.so`, copias, borrado, migración) y un **CLI de 32 bits** (`cloud_redirect_cli`). En Windows, una DLL que carga OpenSteamTool, con su UI WPF. |
| **Repo y commits leídos** | `Selectively11/CloudRedirect` `master@00969da` (2026-09-27 autor, 2026-10-03 commit; tag ligero `v2.6.6`). 281 commits en todas las ramas. Desde `v2.6.5` (`bc5e38a`, 2026-08-18): 26 commits, todos rebasados el 2026-10-03. Nada en `master` ni `ost` después. `gh-pages` `83100b8` (2026-10-04, "Publish 2.6.6 to the flatpak repo"). |
| **Release** | v2.6.6 publica `cloud_redirect.so` (7,5 MB, 32 bits, `strings` → `2.6.6+3434d9d`). **`3434d9d` no existe en el repo público**: el binario se compila desde otro árbol (el `.flatpakrepo` apunta a `Selectively11/CloudRedirect-Unified`), con `release.sh`, `deps/` y `tools/` en `.gitignore` (`flatpak/build.sh:2-3`). El CLI no sale en las releases, solo dentro del Flatpak. |
| **Plataforma** | Linux de 32 bits dentro del Steam nativo o Flatpak (SteamOS, escritorio); Windows 64 bits bajo OpenSteamTool. |
| **Licencia y quién** | MIT desde `2c7f89e` (2026-08-18), "Copyright (c) 2026 Selectively11"; antes, sin licencia. Un único autor en `master`; la rama `ost` (7 commits de JanitorialMess, mayo) está parada. Sin CI en `master`. |
| **Relación con los demás programas** | Lee la configuración de **SLSsteam** (`DisableCloud`, `AdditionalApps`) y solo funciona con ella. Lo inyecta **Headcrab** (`LD_PRELOAD` en `steam.sh`) en el flujo de su autor; en el nuestro, nuestro wrapper. Su fork **cloudredirect-moon** lo distribuye **SLSDeck** a la misma ruta que el nuestro (`slsdeck.md` §4.1-4). En Windows lo cargan OpenSteamTool, BetterSteamTools y opensteamtool-cn; LuaTools lo baja. La UI de Windows **bloquea** su instalación si detecta `LumaCore.dll` (`ui/Services/ThirdPartyDetector.cs:9-90`). |

**Lo que cambia respecto al doc anterior**, en una línea cada uno:

- **En cada arranque de Steam borra ficheros de `userdata/*/*/remote/` de todas las cuentas y todos los juegos**, no solo de los suyos: `manifest.dat`, `cn.dat`, `deleted.dat`, `file_tokens.dat`, `root_token.dat`, `Playtime.bin`, `UserGameStats.bin` y `*.cloudredirect` en la raíz de `remote/` (`src/common/legacy_metadata_cleanup.cpp:160-244`, llamado desde `src/platform/linux/init.cpp:248-253`). Una partida con uno de esos nombres se pierde (§4.1-2).
- **Engancha dos funciones más por parche de bytes**, además de los tres slots de vtable: `CCMInterface::Send` (observador de `GamesPlayed` y `StoreUserStats2`) y el escritor de `GetLastPlayedTimes`, solo con `stats_sync_enabled` (`gamesplayed_hook.cpp`, `live_playtime.cpp`). Ninguna es una de las nuestras.
- **El congelamiento al subir un lote grande sigue en la 2.6.6**: en Linux nadie registra el yield hook y todas las esperas "cooperativas" son bloqueos del hilo de Steam (`src/common/coop_yield.cpp:39-63`; `SetYieldHook` solo en `src/platform/win/cloud_intercept.cpp:994-1004`).
- **El inyector de appinfo no arranca en Linux**: `SteamKvInjector::Configure()` solo se llama desde Windows (`platform/win/cloud_intercept.cpp:356`); en Linux `Init()` registra "Configure() not called" y la cuota y las reglas de guardado inyectadas no existen (`src/common/steam_kv_injector.cpp:79-85`).
- **Lee el `config.yaml` de SLSsteam con un parser propio de una sola capa** (`src/platform/linux/yaml_parser.h`): basta `DisableCloud: no` y `AdditionalApps` como lista de bloque, que es como lo escribimos.
- **La app Flatpak reemplaza el `.so` que puso `setup.sh` por el suyo** si las versiones difieren, con un diálogo al abrirla (`ui-linux/src/deployer.cpp:178-196,300-350`, `qml/Main.qml:15-30`), y con nuestro `steam.sh` vanilla dice "h3adcr-b not installed" (`deployer.cpp:155-176,233-238`).
- **El binario publicado no sale del repo público** (`3434d9d`, ver Release).

---

## §1 Mapa del código

Rutas relativas a la raíz del repo en `00969da`. F = función de §2.

| Ruta | Líneas | Qué hace | F |
|---|---|---|---|
| `src/platform/linux/init.cpp` | 637 | constructor `OnLoad` (filtro de proceso, `CleanLdPreload`), hilo de init diferido (espera de 10 s a `steamclient.so`), `DoInit`, guardas de señales, `notify-send` | 1, 11 |
| `src/platform/linux/vtable_hook.cpp` | 669 | localiza `steamclient.so` en `/proc/self/maps`, rangos legibles (`[64]`), RTTI → vtable de `CClientUnifiedServiceTransport` y `CUserRemoteStorage`, cambio de slots | 1 |
| `src/platform/linux/cloud_hooks.cpp` | 962 | los hooks de slot 5/7/8 e `IsCloudEnabledForApp`, `EnsureInitialized` (config, proveedor, hilos), stats push/pull del blob de cuenta | 1, 7, 8 |
| `src/platform/linux/cloud_intercept.cpp` | 340 | lista de apps desde `config.yaml` de SLSsteam, vigilancia inotify, ruta de Steam, cuenta | 2, 8 |
| `src/platform/linux/http_server.cpp` | 762 | servidor HTTP en `127.0.0.1:<efímero>` para descargas y subidas de Steam, comprobación del PID del cliente | 8 |
| `src/platform/linux/gamesplayed_hook.cpp` | 285 | parche en `CCMInterface::Send` +15 (observador) | 7 |
| `src/platform/linux/live_playtime.cpp` | 442 | parche en el escritor de `GetLastPlayedTimes`; empuja tiempo de juego al cliente vivo | 7 |
| `src/platform/linux/stats_hooks.cpp` | 94 | `Player.GetUserStats` y `ClientGetLastPlayedTimes` desde el slot 5 | 7 |
| `src/platform/linux/http_transport_linux.cpp` | 320 | libcurl estática, CA del sistema, UA `CloudRedirect/1.0` | 10 |
| `src/platform/linux/token_store_linux.cpp` | 247 | tokens en fichero `0600` (sin libsecret en 32 bits) | 10 |
| `src/platform/linux/yaml_parser.h` | 119 | parser YAML de una capa | 2 |
| `src/common/rpc_handlers.cpp` | 2340 | las RPC `Cloud.*`: changelist, lotes, subida, descarga, borrado, cuota, sesión | 8 |
| `src/common/cloud_storage.cpp` | 3207 | blobs por contenido, publicación, GC, conflictos, `SyncFromCloud` (CLI) | 8 |
| `src/common/app_state.cpp` | 676 | `state.cloudredirect` en la nube, CN, candado de sesión, fetch con tope de 12 s | 8 |
| `src/common/pending_ops_journal.cpp` | — | marcador de subida interrumpida (`UploadInProgress`/`UploadPending`) | 8 |
| `src/common/stats_store.cpp` | 2450 | almacén de logros/stats/tiempo, `SeedApps`, `ExportNativeStats` a `appcache/stats/` | 7 |
| `src/common/stats_handlers.cpp` | — | `HandleGetUserStats` (arreglo `e507ba4`), sesiones de juego | 7 |
| `src/common/legacy_metadata_cleanup.cpp` | — | `PruneSteamUserdata` (§4.1-2) | 8, 11 |
| `src/common/autocloud_scan.cpp` | 1409 | lee las reglas `savefiles` de `appinfo.vdf` y escanea el disco (solo para tokens y cuota) | 8 |
| `src/common/steam_kv_injector.cpp` | 1032 | inyección en appinfo en memoria; inerte en Linux | 8 |
| `src/common/coop_yield.cpp` | — | `PumpUntil`/`LockCooperatively`: sin hook, espera activa de 2 ms | 8 |
| `src/common/cloud_work_queue.cpp` | 764 | 8 workers para subidas asíncronas; se vacía al cerrar | 8 |
| `src/common/cli.cpp` | 1470 | 16 órdenes del CLI | 8, 11 |
| `src/providers/` | — | Google Drive, OneDrive, S3, R2, carpeta local | 10 |
| `ui-linux/src/deployer.cpp` | — | copia el `.so` y el CLI a `~/.local/share/CloudRedirect/`, comprueba SLSsteam y Headcrab | 1, 11 |
| `ui-linux/src/backend.cpp`, `oauthservice.cpp` | — | login OAuth (PKCE), borrado de datos por app, copias, migración, auto-update del Flatpak | 10, 11 |
| `flatpak/org.cloudredirect.CloudRedirect.yml` | — | permisos del Flatpak; consume el `.so` y el CLI ya compilados | 12 |
| `src/platform/win/` | — | DLL de Windows (RVA por resolver, detour en `RecvPkt`, yield hook) | — |

---

## §2 Las doce funciones

### 2.1 Engancharse a Steam

**Carga.** Lo inyecta quien ponga `cloud_redirect.so` en `LD_PRELOAD` de Steam:
Headcrab en el flujo del autor (el README manda a `curl -fsSL
headcrab.pages.dev | bash`, `README.md:56-59`), nuestro wrapper en el nuestro.
Este repo no toca `steam.sh` ni `LD_PRELOAD`.

- **Constructor** `OnLoad` (`init.cpp:580-602`): sale salvo que
  `/proc/self/comm` sea `steam` o `steamclient.so` ya esté mapeado
  (`:521-535,583-585`). Después **quita de `LD_PRELOAD` toda entrada que
  contenga `cloud_redirect`** y deja el resto en su orden (`CleanLdPreload`,
  `:139-163`): los procesos hijos (juegos, `steamwebhelper`) no lo heredan, y
  `liblumalinux.so` sigue en la variable. `LD_AUDIT` no lo toca.
- **Hilo de init diferido** (`:536-578`): espera a `steamclient.so` en
  **20 × 500 ms = 10 s fijos** (`:541-545`); al agotarse ejecuta `DoInit`
  igual, falla con "steamclient.so not found" y **no reintenta**.
- **Interruptor** `~/.config/CloudRedirect/disable` (`XdgConfigHome()`, que en
  Flatpak ignora `XDG_CONFIG_HOME`): se mira **dentro** de `DoInit`
  (`:171-179`), después de la espera. El constructor y el log corren igual.
- **`DoInit`** (`init.cpp:165-262`), en orden:
  1. `FindSteamclient`: rango de `/proc/self/maps` con "steamclient.so",
     base corregida hacia atrás hasta la cabecera ELF
     (`vtable_hook.cpp:27-136`).
  2. Vtable de `30CClientUnifiedServiceTransport` por RTTI: cadena de tipo →
     typeinfo → cabecera `{0, &typeinfo}` en `.data.rel.ro`
     (`vtable_hook.cpp`), con hasta 30 s de espera a que estén las
     reubicaciones (`:237-255`). Sin vtable: "Incompatible Steam client -
     hooks disabled" y nada más.
  3. Cambio de los slots **5** (`BYieldingSendMessageAndGetReply`), **7**
     (`NotificationDirect`) y **8** del vtable (`init.cpp:205-214`).
  4. Slot `IsCloudEnabledForApp` del vtable de `CUserRemoteStorage`
     (`:216-229`).
  5. Escaneo de firmas de los helpers de protobuf de Steam (`:231-234`).
  6. Si `stats_sync_enabled` (lectura ingenua por `strstr` de `config.json`,
     `:83-107`): los dos parches de bytes de §2.7.
  7. `PruneSteamUserdata` (§2.8, §4.1-2).
  8. `SteamKvInjector::Init()`, que en Linux no hace nada (§2.8).
  9. `notify-send` por `fork()`+`exec` (`:109-123`) sin recoger al hijo.

  Todo `DoInit` corre con un manejador de `SIGSEGV`/`SIGBUS` del proceso que
  hace `siglongjmp` al hilo de init (`:264-273,547-568`). Con init correcto
  instala un manejador permanente de `SEGV`/`BUS`/`ABRT` de un solo uso que
  escribe una traza en `cr_debug.log` y re-lanza la señal
  (`:473-519,570-572`): sustituye al que hubiera, sin encadenarlo.

**Los hooks y lo que dejan pasar.** El hook del slot 5
(`cloud_hooks.cpp:572-760`) llama al original para todo método que no sea
`Cloud.*` ni (con stats activo) `Player.GetUserStats` /
`Player.ClientGetLastPlayedTimes`, y para todo `Cloud.*` cuyo AppID no esté en
su lista (`:631-633`). `ContentServerDirectory.*`, licencias, PICS y depots
pasan sin tocar. El de `IsCloudEnabledForApp` devuelve `true` para sus apps y
llama al original para el resto (`:901-918`).

**Frente a lumalinux.** Los conjuntos de funciones son disjuntos. lumalinux
parchea por prólogo `DepotKey`, `ShaderDepot` y `GetManifestRequestCode`
(GMRC), y opcionalmente `BuildDepotDependency` y `LoadPackage`
(`src/main.cpp:170-189`); CloudRedirect cambia punteros de dos vtables y
parchea `CCMInterface::Send` (+15, tras el prólogo PIC) y el escritor de
`GetLastPlayedTimes`. El GMRC de lumalinux, cuando Steam pide el código de
verdad, viaja por el slot 5 de CloudRedirect y sale por el original. Ninguno de
los dos escanea las funciones que parchea el otro.

**Orden en `LD_PRELOAD`.** CloudRedirect no exige posición. Con
`-fvisibility=hidden` (`e16c4bc`) y `--exclude-libs,ALL` (`197def9`) solo
exporta `CR_GetVersion` y pocos más, así que nada de lo que defina lumalinux
delante lo intercepta (§5.2 F7, 2026-10-07).

**Cómo sigue los builds de Steam.** En Linux no hay offsets ni lista de
builds: todo es RTTI (nombre de clase) y firmas con comodines. Se rompe solo
si Valve renombra la clase, cambia el orden del vtable o el prólogo de
`CCMInterface::Send`. Los arreglos por build de las releases 2.6.x son de la
DLL de Windows (`src/platform/win/sc_resolver.h`, `sig_scanner.cpp`), que pasó
de RVAs fijos a resolver por firma en 2.2.2 y quitó los RVAs en 2.6.2.

**Lista de control.** Inyección: `LD_PRELOAD` de otro (Headcrab o wrapper);
se quita a sí mismo de la variable. Localización: RTTI para vtables, firmas
con comodines para los dos parches. Ventana: 10 s a `steamclient.so`, sin
reintento. Recuperación: interruptor por fichero tras la espera; cualquier
fallo deja Steam sin CloudRedirect esa sesión.

### 2.2 Propiedad y licencias

Nada. No da propiedad ni toca licencias; depende de que SLSsteam ya haya puesto
el juego en la biblioteca. **No comprueba la propiedad**: si un AppID poseído
está en `AdditionalApps`, su nube real de Steam se sustituye por la suya
(`cloud_intercept.cpp:49-100`; `cloud_hooks.cpp:631`).

### 2.3 DLC

Nada propio. Los AppID de DLC de `AdditionalApps` entran en su lista, pero las
RPC de nube llevan el AppID del juego base, así que no cambian nada; sí reciben
la exportación de stats al arrancar (§2.7).

### 2.4 Claves y manifests

Nada en Linux. En Windows, un post-call sobre `BuildDepotDependency` y un
override de endpoint de manifests (`src/platform/win/cloud_intercept.cpp:432-437`;
`f3d9dfe`), fuera de este doc.

### 2.5 Updates de juegos

Nada.

### 2.6 Fixes y DRM

Nada.

### 2.7 Logros, stats y tiempo de juego

**Interruptores** (`~/.config/CloudRedirect/config.json`):
`stats_sync_enabled: false` apaga todo y ni instala los parches
(`init.cpp:237-245`); si no, `sync_achievements` y `sync_playtime` por separado
(`cloud_hooks.cpp:315-325`). Desde `7914b02` todo el camino de stats respeta
esos dos.

**Almacén.** Local en `~/.config/CloudRedirect/storage/stats/<cuenta>/<app>.json`
y `schemas/<app>.bin`; en la nube, **un solo blob por cuenta**
`<cuenta>/0/stats.json` (`cloud_hooks.cpp:392-462`).

- **Al arrancar** (`SeedApps`, hilo aparte, `stats_store.cpp:1997-2052`):
  espera la cuenta (≤ 30 s), lee el tiempo de `localconfig.vdf` (solo
  lectura), baja el blob de la nube **una vez**, importa los `.bin` nativos y,
  con `sync_achievements`, **escribe** los `.bin` nativos de **cada** app de la
  lista (`:2035-2043`).
- **`Player.GetUserStats`** (slot 5, `stats_hooks.cpp`,
  `stats_handlers.cpp:34-95`): espera la siembra hasta 10 s; **sin schema en
  su almacén devuelve vacío** y la petición sigue a SLSsteam y Valve
  (`e507ba4`, `:50-51`). Con schema compara crcs: iguales → "al día"; distintos
  → schema + stats + horas de desbloqueo, contestado por él.
- **Observador de `CCMInterface::Send`** (`gamesplayed_hook.cpp`): parche
  `E9` en la función +15 a un trampolín `mmap` RWX; ve los EMsg 742/715/5410
  (`GamesPlayed`, inicio y fin de sesión) y 5466 (`StoreUserStats2`,
  desbloqueos). Solo observa: devuelve siempre 0 y el mensaje sale.
- **Tiempo de juego en vivo** (`live_playtime.cpp`): parche en el escritor de
  `GetLastPlayedTimes` para capturar el `CUser`, y construye una respuesta
  sintética que se aplica en el siguiente envío del slot 5. Lee el tiempo de
  `localconfig.vdf`; nunca lo escribe.
- **`ExportNativeStats`** (`stats_store.cpp:870-961`) escribe
  `<Steam>/appcache/stats/UserGameStats_<cuenta>_<app>.bin` y
  `UserGameStatsSchema_<app>.bin` con `ofstream` truncado (no atómico): al
  arrancar, cuando el blob de la nube avanza y al salir de cada juego.
- **Fusión**: los logros solo suben (OR de bits, hora más temprana,
  `stats_store.cpp:1341-1363`); solo un reset explícito los baja.

**Entre máquinas.** El blob de la nube se baja al arrancar Steam: en la otra
máquina el logro aparece tras reiniciar Steam allí (§5.2 F7). Las partidas
viajan por la nube redirigida y los logros por el blob: dos canales.

**Lista de control.** Almacén: propio (JSON local + blob de cuenta) más los
`.bin` de Steam que reescribe. Esquema: lo pide Steam a SLSsteam/Valve; él lo
guarda y después contesta. Sincronización: una descarga por arranque.
Tiempo: propio, solo lectura de `localconfig.vdf`.

### 2.8 Cloud saves

**Qué apps.** Las de `AdditionalApps` del `config.yaml` de SLSsteam
(`~/.config/SLSsteam/` o la ruta del Steam Flatpak), **solo** con
`DisableCloud` como booleano falso; si falta o es `yes`, no gestiona nada
(`cloud_intercept.cpp:49-100`). Un vigilante inotify sobre el directorio
(`IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE`, `ea671c8`) añade AppID nuevos en
caliente; los quitados siguen hasta reiniciar Steam. Sin exclusión por app en
`config.json`.

- **Parser** (`yaml_parser.h`): una sola capa, sin indentación. Las listas son
  líneas `- valor` tras `clave:` vacía; una lista en línea
  (`AdditionalApps: [1, 2]`) se lee como escalar y deja la lista vacía.
  Booleanos `yes/no/true/false/1/0` en sus mayúsculas comunes.

**Qué RPC contesta** (`rpc_handlers.h:10-24`): `GetAppFileChangelist`,
`ClientBeginFileUpload`, `ClientCommitFileUpload`, `ClientFileDownload`,
`ClientDeleteFile`, `BeginAppUploadBatch`, `CompleteAppUploadBatchBlocking`,
`ClientGetAppQuotaUsage`, `SignalAppLaunchIntent`, `Suspend/ResumeAppSession`;
observa `SignalAppExitSyncDone` y `ClientConflictResolution`; en el slot 7
suprime notificaciones `Cloud.*` de sus apps. Todo **en línea, en el hilo de
Steam** (`cloud_hooks.cpp:671`).

**Datos.**

- **Local**: espejo de las partidas en
  `~/.config/CloudRedirect/storage/<cuenta>/<app>/` con sus metadatos
  `*.cloudredirect` (CN, manifest, tokens, 100 instantáneas de manifest,
  journal) (`local_storage.cpp:96-101`, `manifest_store.cpp:598-638`).
- **Nube**: `<cuenta>/<app>/state.cloudredirect` (CN, candado de sesión,
  cuota, ficheros), `cn.cloudredirect` y blobs por contenido en
  `blobs/<fichero>/<sha1>` (`app_state.cpp:321`, `cloud_storage.cpp:351-357`).
- **Lote**: `BeginBatch` asigna CN = max(local, nube) + 1
  (`rpc_handlers.cpp:1474`); `CompleteBatch` sube **todos** los blobs del lote
  esperando en el hilo de Steam (`cloud_storage.cpp:1747-1825`; S3/R2/carpeta
  uno a uno), avanza el CN local y publica el estado desde un hilo separado con
  4 intentos (`rpc_handlers.cpp:1960-2199`). Tras publicar, el GC borra todo
  blob no referenciado: la nube no guarda historial.
- **Conflictos**: gana el CN mayor. El candado de sesión hace que Steam muestre
  su diálogo de "otra máquina"; "Jugar igualmente" lo pisa. El candado no
  caduca (`app_state.cpp:167-169`).
- **Journal** (`a066345`, `391115e`): `UploadInProgress` al empezar un lote,
  `UploadPending` si falla; mientras exista, `GetChangelist` devuelve delta
  vacío para que la nube vieja no pise las partidas locales
  (`rpc_handlers.cpp:748-756`).
- **Si Steam muere tras `CompleteBatch`**: el hilo de publicación muere, la
  nube queda en el CN viejo y el siguiente lote arrastra el manifest local
  entero (`rpc_handlers.cpp:2093-2101`). No hay republicación al arrancar.

**Descargas.** Steam llama al original de `ClientFileDownload` y CloudRedirect
reescribe la respuesta con una URL de su servidor HTTP en `127.0.0.1:<puerto
efímero>` (`http_server.cpp:556-654`), que solo acepta conexiones cuyo socket
pertenezca a su propio PID (`:152-246`, por `/proc/net/tcp` y
`/proc/*/fd`). En ejecución nunca baja por adelantado: `SyncFromCloud` solo lo
llama el CLI (`cli.cpp:674,735`).

**Bloqueos.** En Linux toda espera es bloqueante (§0): `GetChangelist` hasta
12 s por el estado más descargas de tokens sin tope y un SHA1 de los ficheros
(`rpc_handlers.cpp:643,899,934`); `LaunchIntent` y `BeginBatch` fetch sin tope
(`:1226,1456`); `CompleteBatch` lo que tarde la subida entera. El vigilante de
Steam aserta si `BMainLoop` no avanza en 15 s (§5.2 F8).

**`IsCloudEnabledForApp`** devuelve `true` para sus apps, pero la casilla
"Mantener los archivos guardados en Steam Cloud" del juego se respeta igual:
Steam arranca la sincronización como `Sync Disabled` y no llama a nada (§5.2
F8).

**AutoCloud** (`autocloud_scan.cpp`): lee las reglas `savefiles` de
`appinfo.vdf`, `compat.vdf` y el prefijo de Proton, y hace SHA1 de los
ficheros que encajan; solo sirve para tokens raíz y cuota, no sube nada. La
inyección de esas reglas y de la cuota en el appinfo en memoria es de Windows
(§0).

**Escrituras en ficheros de Steam.**

| Ruta | Cuándo | Código |
|---|---|---|
| `appcache/stats/UserGameStats_<cuenta>_<app>.bin`, `UserGameStatsSchema_<app>.bin` | al arrancar (cada app de la lista), al avanzar el blob, al salir del juego | `stats_store.cpp:934-955` |
| borra en `userdata/*/*/remote/`: `manifest.dat`, `cn.dat`, `deleted.dat`, `file_tokens.dat`, `root_token.dat`, `Playtime.bin`, `UserGameStats.bin`, `*.cloudredirect`, `.cloudredirect/` | cada arranque, todas las cuentas y todas las apps | `legacy_metadata_cleanup.cpp:183-244` |
| `userdata/<cuenta>/<app>` entero (con copia previa) | botón de borrar datos de la app Flatpak | `ui-linux/src/backend.cpp:524-593` |
| `remotecache.vdf`, `localconfig.vdf`, `config.vdf`, `registry.vdf` | nunca | `rpc_handlers.cpp:183,1134` |

**Lista de control.** Redirección: sí, cinco proveedores. Apps:
`AdditionalApps` con `DisableCloud: no`, sin comprobar propiedad. Exclusión:
la casilla de Steam por juego, o quitar el AppID y reiniciar. Bloqueo: sí, en
el hilo de Steam.

### 2.9 Añadir y quitar un juego

Nada: sigue a `AdditionalApps`. Quitar un AppID de la lista no borra nada; la
copia local y la de la nube se quedan. La app Flatpak borra por app
(`userdata/<cuenta>/<app>`, la copia local y la carpeta remota, con copia
previa en `~/.config/CloudRedirect/backups/<cuenta>/`).

### 2.10 Credenciales y proveedores

- **Google Drive** con el cliente OAuth público de `clasp`, ámbito
  `drive.file` (solo lo que crea la app) (`src/providers/google_drive.cpp:24-27`,
  `ui-linux/src/oauthservice.cpp:26-30`).
- **OneDrive** con el cliente público de rclone, `Files.ReadWrite
  offline_access` (todo OneDrive) (`onedrive.cpp:16-18`,
  `oauthservice.cpp:32-37`). El código inicial se canjea en `/common` y el
  refresco va a `/consumers` (`onedrive_provider.h:34-35`): cuentas
  personales.
- **S3** con endpoint propio, SigV4, opciones `allow_insecure_http`,
  `allow_insecure_tls` y `ca_cert_path` (`s3_provider.cpp:89-135`). **R2**
  con `account_id` validado (`r2_provider.cpp:7-15`). **Carpeta** con
  `sync_path` (`2b732d8`, #193).
- **Login** solo en la app: escucha en `localhost:53682` (OneDrive, la URI
  registrada de rclone) o `:53692` (Drive), PKCE S256 y `state`
  (`oauthservice.cpp:169-336`). El `.so` solo refresca.
- **Tokens** en `~/.config/CloudRedirect/tokens_<proveedor>.json`,
  `r2_credentials.json`, `s3_credentials.json`, escritos con `0600` por
  temporal + rename (`token_store_linux.cpp:184-196`). La app guarda además
  copia en Secret Service; el `.so` de 32 bits no lo usa (`:52-55`).
- **Red**: refresco 60 s antes de caducar, 30 s de espera tras fallo, 60 ms
  entre peticiones por proveedor, 5 intentos con backoff en 429/5xx/0; **un
  fallo TLS no se reintenta** (`43c0b19`, `cloud_provider_base.cpp:55-276`).
- **Qué sale**: el ID de cuenta de Steam (SteamID3), los AppID, nombres,
  contenido, tamaños y fechas de las partidas, el blob de stats. La app pide a
  `api.steampowered.com` los nombres de los juegos e imágenes de
  `shared.steamstatic.com`. Sin telemetría.

**Lista de control.** Proveedores: Drive, OneDrive, S3, R2, carpeta.
Secretos: clientes OAuth públicos de otros proyectos, embebidos. Tokens:
fichero `0600` en claro.

### 2.11 Mantenimiento propio

- **El `.so`**: la app Flatpak lo copia de `/app/share/cloud_redirect/` a
  `~/.local/share/CloudRedirect/` (`deployer.cpp:135-296`) y compara la
  versión embebida (`X.Y.Z+<sha7>`) o el SHA-256 con lo desplegado; si
  difieren, abre la pestaña de Setup y ofrece "Update" (`:178-196`,
  `qml/Main.qml:15-30`). `update()` borra y copia aunque Steam esté abierto
  (`deployer.cpp:300-350`); `deploy()` exige SLSsteam y un `steam.sh` con
  `LD_AUDIT` y `SLSsteam.so` (`:155-176,226-238`). "Purge" borra
  `~/.config/CloudRedirect` y `~/.local/share/CloudRedirect` (`:368-387`).
- **La app**: con `/.flatpak-info` y clave GPG, ofrece añadir el remoto
  `selectively11.github.io/CloudRedirect/cloudredirect.flatpakrepo` y
  actualizarse con `flatpak-spawn --host flatpak update`
  (`backend.cpp:1939-2090`). La DLL de Windows se actualiza desde las
  releases de GitHub validando solo tamaño y cabecera `MZ`
  (`ui/Services/AppUpdater.cs:194-243`).
- **CLI** (`cli.cpp:1290-1460`): `auth-status`, `list-remote-apps`,
  `list-remote-app-ids`, `list-remote-app-files`, `delete-remote-app`,
  `list-blobs`, `download-blob`, `list-all-stats`, `delete-blobs`,
  `sync-remote-app`, `sync-all-remote-apps`, `prune-local-legacy-metadata`,
  `publish-full-manifest`, `gc-blobs`, `scan-all`, `migrate`. La app lo lanza
  en el host con `flatpak-spawn --host ~/.local/share/CloudRedirect/cloud_redirect_cli`
  (`backend.cpp:2172-2229`).
- **Logs**: `~/.config/CloudRedirect/cr_debug.log` (solo `DoInit`, versión y
  trazas de crash) y `cloud_redirect.log` (todo lo demás).
- **Salud**: un toast por `notify-send` al cargar o fallar; nada que otro
  programa pueda leer salvo esos logs y `CR_GetVersion`.

**Lista de control.** Update del `.so`: por la app (Flatpak) o por quien lo
instale; sin hash publicado salvo el OSTree firmado. Kill switch: fichero
`disable`. Salud: logs.

### 2.12 Proyecto

- Un autor; 26 commits entre el 27-sep y el 3-oct tras seis semanas parado;
  issues sin responder (#197 cerrada sin respuesta, #203 y #193 cerradas por
  commit). Sin CI en `master`.
- Releases compiladas en local desde un árbol que no es el público; las
  versiones de libcurl y OpenSSL estáticas no constan en el repo (el binario
  dice `OpenSSL 3.3.2`).
- Repo Flatpak en `gh-pages` firmado con GPG (`546A 829D D01F 06B9 E73D 05E7
  AF6B A12B F507 BE28`, 2026-05-13); runtime `org.kde.Platform//6.10`.
  Permisos: `--filesystem=home`, red y **`--talk-name=org.freedesktop.Flatpak`**
  (ejecución en el host) (`flatpak/org.cloudredirect.CloudRedirect.yml:10-27`).
- **cloudredirect-moon** (`19da055`, 2026-09-19, base `bc5e38a`): rangos de
  mapa sin tope (`2eee675`), espera de 120 s cancelable, apps desde
  `stplug-in/*.lua` y `luaappids.yaml`, y un puente
  `slsteam_local_stats_epoch_v1` que solo responde slsteam-moon (sin él no
  sirve logros nunca). No ha adoptado nada de la 2.6.6.

---

## §3 Superficies externas

### 3.1 Hosts y URLs

| Host | Para qué | Quién |
|---|---|---|
| `www.googleapis.com`, `oauth2.googleapis.com`, `accounts.google.com` | Drive y su OAuth | `.so`, app |
| `graph.microsoft.com`, `login.microsoftonline.com` (`/common`, `/consumers`) | OneDrive y su OAuth | `.so`, app |
| `<account_id>.r2.cloudflarestorage.com`, endpoint S3 configurable | R2, S3 | `.so`, CLI |
| `api.steampowered.com` (`IStoreBrowseService/GetItems`), `shared.steamstatic.com` | nombres e imágenes de juegos | app |
| `selectively11.github.io/CloudRedirect/` (`gh-pages`) | repo Flatpak | app |
| `api.github.com/repos/Selectively11/CloudRedirect/releases` | update de la UI de Windows | Windows |
| `headcrab.pages.dev` | instalación de Headcrab (README) | usuario |

### 3.2 Ficheros

| Ruta | Qué | Escribe |
|---|---|---|
| `~/.local/share/CloudRedirect/cloud_redirect.so`, `cloud_redirect_cli` | binarios | app (deploy/update), nosotros (`setup.sh`), SLSDeck |
| `~/.config/CloudRedirect/config.json` | proveedor, `stats_sync_enabled`, `sync_*`, `sync_path`, `upload_inflight_mb`, `notifications_enabled`, `token_paths` | app |
| `~/.config/CloudRedirect/tokens_<prov>.json`, `r2_credentials.json`, `s3_credentials.json` | credenciales `0600` | app, `.so` (refresco) |
| `~/.config/CloudRedirect/disable` | interruptor | usuario (LumaDeck solo lo lee) |
| `~/.config/CloudRedirect/storage/<cuenta>/<app>/`, `storage/stats/` | espejo local, metadatos, journal, stats | `.so` |
| `~/.config/CloudRedirect/conflicts/`, `backups/` | copias desplazadas (30 días), copias previas a borrar | `.so`, app |
| `~/.config/CloudRedirect/cr_debug.log`, `cloud_redirect.log` | logs | `.so` |
| `~/.config/SLSsteam/config.yaml` | `DisableCloud`, `AdditionalApps` | solo lee |
| `<Steam>/steam.sh` | busca `LD_AUDIT` + `SLSsteam.so` | solo lee (app) |
| `<Steam>/appcache/appinfo.vdf`, `config/loginusers.vdf`, `userdata/*/config/compat.vdf`, `localconfig.vdf` | reglas de guardado, cuenta, Proton, tiempo | solo lee |
| `<Steam>/appcache/stats/UserGameStats_*.bin`, `UserGameStatsSchema_*.bin` | stats nativos | `.so` |
| `<Steam>/userdata/*/*/remote/` (nombres de §2.8) | borrado en cada arranque | `.so` |
| `<Steam>/userdata/<cuenta>/<app>/` | borrado por app | app |

### 3.3 Puertos

| Puerto | Qué |
|---|---|
| `127.0.0.1:<efímero>` | servidor HTTP del `.so` para Steam; solo su propio PID |
| `localhost:53682`, `localhost:53692` | callback OAuth de la app (OneDrive, Drive) |

---

## §4 Lo que nos afecta

### 4.1 Hallazgos pendientes de decisión

Aparcados el 2026-10-09. Solo lo que le pasa a nuestra pila o a un usuario de
LumaDeck, que lleva CloudRedirect instalado por `setup.sh`.

1. **Steam se congela al salir de un juego con muchos ficheros nuevos.**
   `CompleteAppUploadBatchBlocking` espera en el hilo de Steam a que se suba
   el lote entero, y en Linux esa espera no cede nunca (§2.8). Con un lote de
   100 ficheros en Drive son 85-90 s; el vigilante de Steam aserta a los 15 s
   y la interfaz queda muerta hasta reiniciar (§5.2 F8, 2026-09-16/18). Sigue
   igual en la 2.6.6. Lo sufre cualquier juego de LumaDeck que guarde muchos
   ficheros por sesión; los demás tardan unos segundos más en salir.
   Mitigación probada: desmarcar Steam Cloud en las propiedades del juego, o
   el fichero `disable`.
2. **Borra ficheros de partidas reales en cada arranque de Steam.** El barrido
   de `PruneSteamUserdata` recorre `userdata/<cuenta>/<app>/remote/` de
   **todas** las cuentas y **todos** los AppID, poseídos o no, gestionados o
   no, y borra de la raíz los nombres de §2.8 (`legacy_metadata_cleanup.cpp:160-244`).
   Un juego cuyo guardado de Steam Cloud se llame `manifest.dat`, `cn.dat`,
   `deleted.dat`, `Playtime.bin` o `UserGameStats.bin` en la raíz de
   `remote/` lo pierde en local en cada arranque, y Steam puede propagar la
   ausencia a la nube de Valve. Sin caso conocido; los nombres son genéricos.
3. **Reescribe los `.bin` de stats de Steam de cada juego de la lista.** Con
   `sync_achievements`, al arrancar, al avanzar el blob y al salir de cada
   juego, sobrescribe `appcache/stats/UserGameStats_<cuenta>_<app>.bin` y el
   schema, de forma no atómica (`stats_store.cpp:934-955`). Lo que escriba
   otro (Steam en su propio volcado, SLSsteam en su fallback, un gestor de
   logros) se pisa: los re-bloqueos vuelven a desbloquearse por la fusión
   hacia arriba, los stats no enteros se reescriben como enteros y las claves
   que no conoce se pierden.
4. **El blob de stats de la cuenta puede perder entradas.** Al subir,
   `DownloadCloudMetadataWithLegacyFallback` no distingue "no existe" de "error
   transitorio" y, ante un error, fusiona sobre un objeto vacío y sube
   (`cloud_hooks.cpp:428-461`): se pierden las entradas de las apps que esta
   máquina no tenga en memoria. Lo nota otra máquina del mismo usuario
   (Deck ↔ PC).
5. **Una subida puede quedarse sin publicar sin que nada lo marque.**
   `SignalAppExitSyncDone` borra el journal antes de que acabe la publicación
   diferida; si los 4 intentos fallan, no queda `UploadPending`
   (`pending_ops_journal.cpp:236-260,335-345`). Y si la nube ya tiene un CN
   igual o mayor, el lote se descarta sin copia de conflicto
   (`rpc_handlers.cpp:2076-2080`). La partida sigue en el espejo local; la
   otra máquina no la ve.
6. **La app Flatpak cambia el `.so` que pusimos.** Al abrirla (que es lo que
   pedimos para el login, `nosotros.md` §2.8) compara su `.so` empaquetado con
   el desplegado y, si la versión difiere, salta a Setup con "Update"; pulsarlo
   lo reemplaza aunque Steam esté abierto (`deployer.cpp:178-196,300-350`).
   `setup.sh` baja el `.so` de la release y el Flatpak sale de `gh-pages`: si
   uno va por delante del otro, LumaDeck y la app se lo cambian mutuamente.
   Además, con nuestro `steam.sh` vanilla la app dice "h3adcr-b not installed.
   Did you run h3adcr-b first?" aunque CloudRedirect esté cargado
   (`:155-176,233-238`) [sin medir en la Deck].
7. **Un AppID poseído en `AdditionalApps` pierde su nube de Valve.** No hay
   comprobación de propiedad (§2.2). LumaDeck no pone juegos poseídos en la
   lista (`resolve_owned`, LumaDeck `backend/downloads.py:911-950`), pero lo
   hace cualquier herramienta que escriba ahí, y el usuario no ve el cambio.
8. **La espera de 10 s a `steamclient.so`.** Si el arranque tarda más (update
   de Steam, disco lento, CachyOS según moon), CloudRedirect no se engancha esa
   sesión, sin reintento ni aviso más allá del toast (`init.cpp:541-545`). Es
   requisito del port a CachyOS (`cachyos-port.md`).
9. **Los 64 rangos de memoria.** `g_readableRanges[64]` con ventana de
   ±16 MiB y truncado silencioso (`vtable_hook.cpp:17-20,101-127`): si el mapa
   está más fragmentado, no encuentra el vtable y se apaga esa sesión.
   Teórico para nuestra pila (22 rangos medidos, §5.2 F1).
10. **El manejador de crash permanente.** Tras un init correcto sustituye el
    manejador de `SEGV`/`BUS`/`ABRT` que hubiera, sin encadenarlo
    (`init.cpp:510-519`); re-lanza la señal, así que el proceso muere igual y
    nuestro guard anti crash-loop lo sigue viendo. Y durante `DoInit` un
    `SIGSEGV` de **cualquier** hilo salta al hilo de init de CloudRedirect
    (`:264-273`): si coincide con un fallo de otro hilo de Steam, el resultado
    es indefinido. Sin caso conocido.

### 4.2 Dependencias y qué rompe si cambia

| Si cambia… | Qué nos pasa |
|---|---|
| Valve renombra `CClientUnifiedServiceTransport` o reordena su vtable | CloudRedirect se apaga esa sesión ("Incompatible Steam client"); lumalinux sigue |
| el prólogo de `CCMInterface::Send` o del escritor de `GetLastPlayedTimes` | sin tiempo de juego ni captura de desbloqueos; nube y lumalinux siguen |
| el formato de `config.yaml` de SLSsteam (lista en línea, `DisableCloud` anidado) | CloudRedirect deja de gestionar los juegos, en silencio |
| `DisableCloud: yes` (lo escriba quien lo escriba) | sin nube para ningún juego añadido |
| una release del `.so` sin publicar el asset | `setup.sh` aborta: CloudRedirect es componente core (`nosotros.md` §2.8) |
| SLSDeck en la misma Deck | `cloud_redirect.so` sustituido por el de moon, que sin slsteam-moon no sirve logros (`slsdeck.md` §4.1-4) |
| la retirada del endpoint de tienda que usa SLSsteam para los schemas (2026-10-22) | los juegos nuevos no consiguen schema hasta la release de SLSsteam con el arreglo; CloudRedirect solo guarda lo que Steam reciba |

### 4.3 Lo que CloudRedirect tiene y nosotros no (y al revés)

| Función | CloudRedirect | Nosotros | Notas |
|---|---|---|---|
| 7 | almacén propio de logros/stats/tiempo y blob por cuenta | nada propio; usamos el suyo | `nosotros.md` §2.7 |
| 8 | redirección completa de Steam Cloud | delegada en él | `nosotros.md` §2.8 |
| 1 | se quita de `LD_PRELOAD` para los hijos | `liblumalinux.so` se hereda y filtra por proceso (`proc_filter.cpp`) | |
| 11 | toast de escritorio al cargar o fallar | `status.json` legible por LumaDeck | |
| 1 | — | `libcurl_pin`, `cr_stats_fix` para él; inertes desde 2.6.6 | §4.4 C8, C12 |

### 4.4 Decisiones ya tomadas (del doc anterior, con fecha) y estado hoy

| # | Decisión | Fecha | Hoy |
|---|---|---|---|
| C1 | Actualizar a v2.5.4+ por el `SIGSEGV` de cierre (`0925cc2`) | 2026-07 | **superado**: 2.6.6 en la Deck desde el 2026-10-07 |
| C2 | Arreglar en lumalinux el arranque en frío de stats (`cr_stats_fix`, interposición de `HandleGetUserStats`) | 2026-09-20 | **superado**: arreglado arriba en `e507ba4`; con símbolos ocultos el stub reenvía sin mirar y dice "patch not needed" (`cr_stats_fix.cpp:219`) |
| C3 | Proponer a Selectively11 el parche de una línea de stats | 2026-09-20 | **superado** por `e507ba4` |
| C4 | Congelamiento al subir: mitigar por juego (casilla de Steam Cloud) o con `disable`; nada en LumaDeck (aviso, toggle por juego, tocar `config.json`) porque el arreglo es de CloudRedirect | 2026-09-18 | vigente; 2.6.6 sigue sin yield hook (§4.1-1); app 2545360 con la casilla desmarcada |
| C5 | LumaDeck `_account_id()` sin la cascada `MostRecent > AutoLogin > Timestamp` (`9d9260d`) | 2026-08-18 | **abierto, prioridad baja**: sigue tomando el primer bloque sin `MostRecent` (LumaDeck `backend/achievements.py:138-146`) |
| C6 | `ea671c8` (vigilar directorio): no aplicable, lumalinux ya lo hace | 2026-08-18 | cerrado |
| C7 | La detección de `LumaCore.dll` (`eec2f2f`) no afecta: solo UI de Windows | 2026-08-18 | cerrado |
| C8 | `curl failed: 60`: sin acción, `libcurl_pin` lo corrige | 2026-09-29 | **superado**: libcurl estática desde `25df01a`; el pin queda para nuestras descargas; su comentario de cabecera ("Neither CloudRedirect nor lumalinux links libcurl", `libcurl_pin.cpp:8`) sigue sin actualizar |
| C9 | No enviar el informe largo a Selectively11 (yield hook, publicación que sobreviva, exclusión por app, stats, `CAINFO`) | 2026-09-23 | vigente |
| C10 | Defecto de los 64 rangos: teórico; camino A (detectar en LumaDeck) descartado | 2026-09-28 | vigente (§4.1-9) |
| C11 | No proponer parches que no hemos escrito ni probado (camino C); build propio (B) solo si lo exige CachyOS | 2026-09-28 | vigente |
| C12 | Actualizar a 2.6.6 | 2026-10-07 | **hecho** en la Deck 22:16 (`2.6.6+3434d9d`, `DoInit: SUCCESS`) |
| C13 | Espera de 10 s como requisito del port a CachyOS | 2026-09-28 | vigente: sigue en `init.cpp:541-545` (`cachyos-port.md`) |

---

## §5 Historial

### 5.1 Lo que se creía y ya no es así

- **"Los dos hookean conjuntos disjuntos por mecanismos que no se solapan:
  CloudRedirect solo cambia punteros de vtable."** Los conjuntos siguen
  siendo disjuntos, pero CloudRedirect también parchea bytes de dos
  funciones (§2.7).
- **"`IsCloudEnabledForApp` devuelve `true` e ignora la casilla del juego."**
  El hook devuelve `true`, pero Steam respeta la casilla antes de llegar a él
  (§5.2 F8).
- **"La premisa de superficies disjuntas cubre todo lo que nos afecta."**
  CloudRedirect escribe y borra en `appcache/stats/` y `userdata/*/remote/`
  de juegos que no son suyos (§4.1-2, -3).
- **"El canal de Linux tope en 2.6.0; 2.6.1/2.6.2 solo Windows."** La 2.6.6
  publica `.so` y es la que llevamos.
- **"Inyecta reglas de guardado y cuota en el appinfo."** Solo en Windows.
- **"El orden de `LD_PRELOAD` importa porque lumalinux interpone un símbolo de
  CloudRedirect."** Desde 2.6.6 no hay símbolo que interponer.

**Lo que sigue sin medir**:

- §4.1-6: qué dice la app Flatpak en la Deck con nuestro `steam.sh`, y si las
  versiones del `.so` de la release y del Flatpak coinciden.
- §4.1-2: si algún juego de los que añade LumaDeck guarda con esos nombres.
- El tiempo real de arranque de `steamclient.so` en CachyOS.

### 5.2 Pruebas y mediciones con fecha

#### Función 1 — Engancharse a Steam

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-28 | codespace (SLSsteam de Ace + lumalinux, Steam en marcha) | `grep -c steamclient.so /proc/<steam>/maps` | **22** rangos con nombre | la ventana de ±16 MiB y el scan temprano suman más; lejos de 64 |
| 2026-10-07 | binario publicado v2.6.6 | `readelf -d`, `.dynsym`, `strings` | sin `NEEDED libcurl`; 691 funciones, **0 `HandleGetUserStats`** (la 2.6.5 tenía 2 referencias); `CR_GetVersion` exportado; `OpenSSL 3.3.2 3 Sep 2024`; `version=2.6.6+3434d9d` | 7,5 MB frente a ~2 MB |
| 2026-10-07 22:16 | Deck | update a 2.6.6 desde LumaDeck (`setup.sh`) | `.so` y `cr_debug.log` en `2.6.6+3434d9d`, `DoInit: SUCCESS`; lumalinux `CR-stats: … nothing to interpose`; `.so` mapeado 6 veces | |

#### Función 7 — Logros, stats y tiempo de juego

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-18/20 | Deck (CR 2.6.5, SLSsteam 2026-09-03, lumalinux 0.21.0) | cinco arranques de 2545360, uno de 45 min | siempre `up-to-date (crc=0); sending crc-only no-op` → `handled locally (2 bytes)`; cero logros; con `stats_sync_enabled: false` saltan | cuatro de cinco juegos así (2545360, 2524850, 1942280, 1875580); 2524850 tenía el schema en disco: la condición es no tener ningún stat o logro con valor |
| 2026-09-18/20 | Deck, app 711540 | jugado con sync apagado, encendido después | `up-to-date (crc=2292680116)`, luego `Sending schema (97923 bytes)` + `Returning 2..3 stats` en siete arranques | la ruta "almacén lleno" funcionaba |
| 2026-09-20 | `tools/cr_stats_fix_selftest/run.sh` (lumalinux) | interposer contra un CR falso de 32 bits | seis escenarios pasan | ya inerte desde 2.6.6 |
| 2026-10-07 22:52-23:06 | Deck y PC, CR 2.6.6, Drive, sync en los dos | Balatro (2379780) a cero, un logro | Deck: `ExportNativeStats app=2379780: wrote 79 bytes (1 stats, 1 ach blocks)`, `Cloud blob refreshed: 7 app(s)`; al relanzar `handled locally (5 bytes)` y logro visible. PC: no aparece hasta reiniciar Steam (`SeedApps`) | el bug de 2.6.5 no se reproduce |

#### Función 8 — Cloud saves

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-08-16 | Deck (versiones anteriores) | historial de `UploadBatch` | el mismo bloqueo de ~90 s | no era nuevo de septiembre |
| 2026-09-16/18 | Deck (SteamOS 3.8.16, CR 2.6.5, Drive), Lonely Mountains: Snow Riders 2545360 (~100 replays por sesión en `Ghosts/`) | salir del juego | "Sincronizando con la nube", UI congelada, 0 B/s, apagado sin respuesta; `BMainLoop stalled > 15 s` en `journalctl`; tras reiniciar, "verificando instalación" | lote de 100: CAS 13,5-15,8 s (~7 ficheros/s), subida 85-90 s (~1,1/s); salida de sesión de cualquier juego ~12 llamadas a Drive en serie, ~13 s |
| 2026-09-16 | misma Deck | estado tras el cuelgue de las 21:53 | nube en CN 8, local subiendo hasta 15; conflicto en `BestTimes.json` en cada arranque; 13 líneas "another session active" | eran sesiones muertas de la propia Deck (un clientId por arranque) |
| 2026-09-16/18 | misma Deck | `LUMA_NO_GMRC=1` | se cuelga igual; el log de lumalinux no muestra GMRC en la ventana | descartado lumalinux |
| 2026-09-16/18 | misma Deck | `LUMA_NO_LIBCURL_FIX=1` | `curl failed: 60` con la libcurl del runtime | el pin era necesario en 2.6.5 |
| 2026-09-16/18 | misma Deck | casilla de Steam Cloud desmarcada en 2545360 | `cloud_log.txt`: `Starting sync (AC Exit,Sync Disabled,)`; no sube nada; el juego arranca y cierra normal | mitigación por juego |
| 2026-09-29 | issues #197, #203; Discord de SLSsteam | "CloudRedirect en Linux roto desde hace semanas" | `[R2] curl failed: 60` en bucle, `FetchCloudStateBounded … exceeded 12000ms` | la libcurl del runtime de Steam tras vaciarse `library-inject` en SLSsteam 20260903; usuarios de LumaDeck cubiertos por el pin; cerrado arriba en 2.6.6 |

### 5.3 Cronología

- **≤ v2.2.1.** DLL de Windows con RVAs fijos por build. El `.so` de Linux ya
  iba por RTTI.
- **v2.2.2 / v2.2.3.** Resolver por firma con los RVAs como respaldo; la
  2.2.3 soporta el build 1782437068 sin tocar RVAs.
- **2026-05-13/17.** Repo Flatpak en `gh-pages` con su clave GPG; rama `ost`
  de JanitorialMess.
- **v2.5.x (2026-07).** Linux deja la inyección de logros, el hook de
  `RecvPkt` y la descarga de schemas (`31454e3`); se quita la UI de logros
  (`1095306`); arreglo del log antes de la inicialización estática
  (`0925cc2`).
- **v2.6.2.** Fuera los RVAs fijos en Windows.
- **2026-07-22 → 08-18.** v2.6.3-2.6.5: tiempo de juego y last-played, S3/R2,
  retirada de SteamTools (`bc5e38a`, −6041 líneas), detección de `LumaCore.dll`
  (`eec2f2f`), vigilancia del directorio de SLSsteam (`ea671c8`), cascada de
  `loginusers.vdf` (`9d9260d`, PR de ciscosweater), MIT (`2c7f89e`).
- **2026-08-26.** SLSsteam desactiva `library-inject` (vacío en 20260903): la
  libcurl del runtime gana dentro de Steam.
- **2026-09-07.** `libcurl_pin` en lumalinux (`f0fab5a`).
- **2026-09-16/18.** Incidente de congelamiento en la Deck (§5.2 F8).
- **2026-09-19.** cloudredirect-moon `19da055`, su último commit.
- **2026-09-20.** `cr_stats_fix` en lumalinux v0.21.1.
- **2026-09-23.** Decisión de no enviar el informe (C9).
- **2026-09-27 → 10-03.** 26 commits: journal de subidas interrumpidas,
  libcurl estática (`25df01a`), sin reintento TLS, símbolos ocultos,
  `e507ba4` (stats), puertas de `sync_*`, carpeta en Linux (#193).
- **2026-10-03/04.** Tag `v2.6.6`; Flatpak 2.6.6 publicado (`83100b8`).
- **2026-10-07.** 2.6.6 en la Deck; logros Deck ↔ PC medidos.
