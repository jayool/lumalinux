# LuaTools, la app de escritorio (`madoiscool/LuaTools`) — análisis del fuente

La app de escritorio Windows de LuaTools (.NET 8 WPF, `LuaToolsGui`). Hasta
este documento solo teníamos el RE del binario **v1.2.6** (23-jul,
`luatools-desktop-app/README.md`). El fuente se publicó en GitHub el
**2026-08-13** ("Initial commit", 105 ficheros C#) y desde entonces lleva 31
commits y cuatro releases: v1.2.8 (13-ago), v1.3.0 (30-ago), v1.3.1 (9-sep),
v1.3.2 (25-sep). Este es el primer barrido sobre el fuente. Lo que nos
importa de ella es lo que comparte con LumaDeck: la API de lua.tools (auth,
fixes de Denuvo, manifests, DLC), Hubcap, Ryuu y los repos de GitHub de los que
tira. Convenciones: [read] leído en el fuente; [measured] medido por nosotros;
[inferred] deducción.

## 0. Punto de partida: el binario v1.2.6 (23-jul), RE sin fuente

*Lo que había antes de que el fuente se abriera: el análisis del binario
portable v1.2.6, hecho sobre strings, metadatos y el desensamblado IL. Los
artefactos (zip, dll, dumps) siguen en `luatools-desktop-app/`; este era su
README, movido aquí tal cual (en inglés) para que todo LuaTools escritorio viva
en un solo documento.*

The **standalone LuaTools Windows application** from
[`madoiscool/LuaTools`](https://github.com/madoiscool/LuaTools) — a **.NET 8 WPF**
desktop app (namespace `LuaToolsGui`), packaged with **Velopack** (the Squirrel
successor). This is the actual `.exe` app, NOT the Millennium plugin (that lives in
`docs/luatools-windows/`) and NOT the Linux ports.

Release **v1.2.6** (2026-07-23). Kept for **analysis only** — how the app talks to
lua.tools, applies fixes, unlocks DLC, injects the plugin, etc.

### Files

| File | What |
|---|---|
| `LuaTools-1.2.6-win-Portable.zip` | The exact shipped portable app (6.6 MB). The source of truth for a full decompile. |
| `LuaTools.dll` | The app's managed assembly (965 KB) — the **decompile target**. |
| `LuaTools.exe`, `LuaTools.deps.json` | Host + dependency manifest. |
| `LuaTools-1.2.6-types-methods.txt` | Every type (473) + methods, from metadata (dnfile). |
| `LuaTools-1.2.6-strings.txt` | All string literals (1458), incl. every URL/endpoint. |
| `LuaTools-1.2.6-IL-disassembly.txt` | **Full IL disassembly** of all 461 types / 3527 methods (dnfile + dncil), with `ldstr` strings, `call`/`newobj` targets and field accesses resolved. Not C#, but the complete instruction-level logic. |

### Full C# decompile

A proper decompile-to-C# needs the .NET runtime, which this environment's egress
policy **blocks** (`builds.dotnet.microsoft.com` → 403, org policy). So it wasn't
done here. Do it on a normal machine against `LuaTools.dll`:

```
# dnSpy (GUI, Windows) — open LuaTools.dll, or:
dotnet tool install -g ilspycmd
ilspycmd -p -o luatools-src LuaTools.dll
```

The type map + strings below already reveal the behaviour without decompiling.

### What it does (from metadata + strings)

- **.NET 8 WPF** (`Wpf.Ui`, `CommunityToolkit.Mvvm`, `Markdig.Wpf`); updater =
  `Velopack`. Views: Home, Fixes, Manage, Mode, Download, Plugin, Settings,
  Onboarding, DropZone. Services: `LuaToolsApiClient`, `AuthService`,
  `GithubProxy`, `HubcapService`, `SteamDepotInfo`, `HttpServerService`,
  `PluginInstallerService`, `PluginAddService`, `ProtocolService`,
  `ProxiedFileDownloader`, `CoverCache`, `SteamAppListCache`.

- **The fixes flow** (`LuaToolsApiClient`, decompiled to IL — see the IL dump):
  - `GetDenuvoFixesAsync` → `GET /api/denuvo/fixes?appid=<id>` (the catalogue)
  - `GetDenuvoListingsAsync` → `GET /api/denuvo/listings`
  - `DownloadDenuvoAsync` → `/api/denuvo/download?fix=<fix>&slot=<slot>` → returns a URL →
    `DownloadFromUrlAsync` (fails with "The download link was empty — try again.")
  - `DownloadManifestAsync` → `/api/manifest/download?appid=<id>&source=<src>&game_name=<name>.zip`
  - `GenerateDlcAsync` → `/api/dlc/generate?appid=<id>&base=<base>&game_name=<name>.lua`
  - `GetDlcInfoAsync` → `/api/dlc/info?appid=<id>&base=<base>`

- **lua.tools API surface** (base host built at runtime; paths from the strings):
  - `/api/denuvo/fixes?appid=` — fixes catalogue for an appid
  - `/api/denuvo/download?fix=` — download a fix
  - `/api/denuvo/listings` — all listings
  - `/api/dlc/info?appid=`, `/api/dlc/generate?appid=` — DLC unlocks
  - `/api/manifest/download?appid=`, `/api/v1/manifest/` — manifest download
  - `/api/v1/user/stats?api_key=`, `/api/me/supporter-status` — user/supporter
  - `/api/v1/status/`, `/api-list`
  - `/auth/v1/authorize?provider=discord` — **Discord OAuth** (Supabase auth); the
    app also accepts a `/login`-in-Discord code paste.

- **Local HTTP server** `http://127.0.0.1:6767` — backs the "Add via LuaTools"
  button injected into Steam store pages (`GetAddViaLuaToolsStatus`, `HandleAdd`).

- **Plugin bridge**: installs/updates the LuaTools **Millennium plugin**, injecting
  `public/luatools.js` (`Loaded luatools.js ({Length} bytes)`,
  `real.callServerMethod=... ltCall`). Can disable Millennium's own copy so
  LuaLoader injects instead.

- **Hubcap** integration (`HubcapService`, API-key gated) for manifest/key stats;
  regex helpers (`AppIdRegex`, `AddAppIdRegex`, `SetManifestRegex`, …) parse the
  installed `.lua` files.

### Provenance

Downloaded from the upstream release `v1.2.6` in this session and archived here so
it isn't lost. Third-party binary + derived metadata; kept as a reference snapshot.


## Barrido 2026-10-07 — del fuente abierto (13-ago) a v1.3.2 (25-sep)

*Clon `madoiscool/LuaTools` `main` en `9461259`. Autores: mendy-tools (el
grueso), madoiscool, peeblyweeb, gattoooo, Brandon Hernandez. Varios mensajes
de commit citan a una IA ("ty claude") y el estilo de los comentarios lo
confirma.*

### 1. La API de lua.tools que usa la app, comparada con la de LumaDeck [read]

Sin cambios en lo que LumaDeck ya usa (`luatools_auth.py`):

| Endpoint | App v1.3.2 | LumaDeck |
|---|---|---|
| Login por código de Discord: `POST lua.tools/api/auth/code/redeem` → `POST db.lua.tools/auth/v1/verify` (Supabase, cabecera `apikey` anon) | igual | igual |
| Refresco: `POST db.lua.tools/auth/v1/token?grant_type=refresh_token` | igual | igual |
| `GET /api/denuvo/fixes?appid=`, `/api/denuvo/listings`, `/api/denuvo/download?fix=&slot=` con `Authorization: Bearer` | igual | igual (`fixes`, `download`) |
| Cuota: 25 descargas/día (`AppConfig.DailyDownloadLimit`), contadas en vivo por `HEAD db.lua.tools/rest/v1/user_downloads` con `Prefer: count=exact` | | no lo mostramos |

Nuevo en la app, que LumaDeck no usa:

- **`GET /api/givemethemanifestpunk/{depotId}/{manifestId}`**: **un manifest
  suelto por depot y gid, en bytes crudos**, con el mismo Bearer. Según el
  comentario del cliente: *no escribe historial, no consume el tope diario de
  25; límite de 120 peticiones por 10 minutos por usuario, y solo cuentan los
  fallos de caché.* La app lo usa para que la descarga de depots no dependa de
  que Steam tenga el manifest en depotcache. **Candidato para nuestra
  cadena de manifests** (`manifests.py`): tenemos la sesión de lua.tools, es
  el mismo tipo de pieza que el `/generate/manifest` de Hubcap que metimos el
  07-10, y con otra cuota. Sin medir: no sabemos si sirve cualquier gid o solo
  lo que tengan archivado (probablemente el mismo archivo que
  `manifest.luastools.xyz`, que la app no usa por su nombre público).
- `GET /api/me/supporter-status`: si el usuario es "supporter". Cosmético.
- `/api/dlc/info` y `/api/dlc/generate` (DLC unlock lua generado por lua.tools)
  y `/api/manifest/download?appid=&source=` (zip de juego por fuente nombrada)
  ya estaban en 1.2.6.

### 2. Fuentes externas de la app [read]

- **Hubcap** (`HubcapBaseUrl`): zip `/api/v1/manifest/{app}?api_key=`,
  `/api/v1/status/{app}` (gratis, antes de gastar) y `/api/v1/user/stats`.
  Mismo uso que el plugin de Millennium 8.0.4 (`luatools-windows/`).
- **Ryuu por IP** (`ManifestBackendUrl = http://167.235.229.108`, UA
  `secretgoonpoon`): `check_apis?appid=` (qué fuentes tienen el juego) y
  `donatekeys/send`. **`DonateKeysService`**: al arrancar, si "Donate Keys"
  está activado, lee `config/config.vdf` del Steam del usuario, saca todas las
  `DecryptionKey` y las envía al "community key pool" de Ryuu; el backend
  acepta cada depot una vez por IP. La app **no lee** de ese pool: solo dona.
  (LumaDeck no dona claves; lo apuntamos junto a la donación de códigos a
  luastools, RESEARCH §20.6, como decisión de producto.)
- **Sushi** (GitHub) como zip de respaldo; `applist.morrenus.xyz` y
  `api.steampowered.com/ISteamApps/GetAppList` para nombres;
  `api.steamcmd.net/v1/info` para appinfo; `steamdb.info` solo como enlace.
- **Mirrors de GitHub** (`gh.ddlc.top`, `ghfast.top`, `ghproxy.net`,
  `lua.tools/api/gh/`) para usuarios con GitHub bloqueado.
- **Resolución DNS por Cloudflare** (`9461259`, 25-sep, v1.3.2): ajuste
  Auto/Siempre/Nunca; en Auto solo cambia a DoH 1.1.1.1 cuando el DNS del
  sistema falla. Motivo declarado en las cadenas de UI: *"si tu proveedor
  bloquea lua.tools"*. Dato: lua.tools está bloqueado por DNS en algunos ISP.
  LumaDeck no tiene nada equivalente; en la Deck el usuario cambiaría el DNS
  del sistema.

### 3. Lo que la app hace ahora y antes no (v1.2.8 → v1.3.2) [read]

- **Descarga de depots fuera de Steam** (`54fbe9c`, `58d36db`, `0be1bf4`,
  `a08f2a9`, 21 al 30 de agosto; `DepotDownloaderService`, 658 líneas):
  baja `mendy-tools/DepotDownloaderMod` de GitHub (verificando el digest del
  asset), coge las claves del lua o, si el lua no las trae, de `config.vdf`
  del usuario, y el manifest de depotcache o del endpoint `givemethemanifestpunk`.
  Es el modelo de ASSella (DepotDownloader con `-manifestfile`), no el
  nuestro (Steam descarga).
- **SteamAutoCrack** (`SteamAutoCracks/Steam-auto-crack`) y **Steamless**
  (`atom0s/Steamless`) bajados de GitHub e instalados bajo demanda, con el
  runtime .NET 10 Desktop vía Velopack. Windows.
- **`UnlockerService`** (872 líneas): gestiona el "unlocker" de Steam
  instalado, **OpenSteamTool o BetterSteamTools** (o uno propio del usuario),
  resolviendo el build desde `madoiscool/BetterSteamTools` rama `updates`
  (`opensteamtool/latest.toml`), verificando por sha256 e instalando en la raíz
  de Steam. Y se asegura de que el unlocker vigile `config/stplug-in` para que
  los luas se recarguen en caliente. Es decir: la app de escritorio **instala
  el equivalente Windows de SLSsteam+lumalinux** y lo mantiene. Todo Windows.
- **`CefInjectorService`**: inyecta el JS del plugin en el Steam del usuario
  por CDP (`127.0.0.1:{port}/json`), con una conexión reutilizada por llamada.
  Mismo mecanismo que `cef_cdp.py` de LumaDeck.
- **`LuaVault`** (689 líneas): guarda variantes de cada lua con hash SHA-256
  de bytes y nombre, para saber qué variante está "viva" en Steam.
- **Revert de fixes aplicados** (`54e3d56`, 7-sep): registro por fix de qué
  ficheros tocó con hash antes/después (`FileHash`), para deshacer solo si el
  fichero sigue siendo el que el fix escribió. LumaDeck no tiene revert de
  fixes; tiene reinstalación.
- **"secure manifest handling"** (`53f071b`, 8-sep): el nombre de un fix
  (`online-fix:<uuid>`) se sanea antes de usarlo como carpeta, y las rutas de
  un zip no pueden salir del directorio de instalación (zip-slip). Y detección
  de "zip que en realidad es un lua suelto" por magia `PK`, y de manifest por
  magia `0x71F617D0`: las mismas comprobaciones que `manifests.py` hace.
- **`DepotCacheMigrationService`** (`9122c6e`, 9-sep, v1.3.1): *"hasta el
  2026-09-09 esta app escribía cada `.manifest` en `<Steam>\config\depotcache`.
  Steam lee `<Steam>\depotcache`"*. Mueve lo que quedó mal. Coincide con lo
  que medimos en RESEARCH (`config/depotcache/` no se lee). Nota: nuestro
  `steamidra_lite` escribe en las dos por si acaso; es inofensivo.
- **`0485dec` (25-sep, v1.3.2): el formato de respuesta de `appdetails` ha
  cambiado.** El comentario del commit, textual: *"appdetails used to be
  keyed by the appid you asked for. Since ~Sep 2026, any app that HAS child
  apps (a DLC, a soundtrack) comes back keyed by one of those CHILD ids
  instead: appids=500 answers under "1660740" … Measured the same way for
  730→2678630, 620→323180, 220→323140, 243470→293061 and 1091500→2441600.
  Apps with no child apps … and EVERY success:false body are still keyed by
  the requested id. The inner data.steam_appid always echoes the id we asked
  for; it is the only trustworthy anchor left."* Su arreglo: si la clave
  directa no está (o su `data.steam_appid` no es el pedido), recorrer las
  entradas y quedarse con la que tenga `data.steam_appid == appid`; nunca
  "si hay una sola entrada, cógela". **Nos afecta: LumaDeck indexa por
  `str(appid)` en tres sitios** (`downloads.py` `fetch_app_name` y
  `get_game_notices`; `slssteam_ops.py` `_fetch_dlc_list`, el de `DlcData`
  para más de 64 DLC). Si el cambio es real, en juegos con DLC o banda sonora
  esos tres devuelven vacío en silencio. Contradato nuestro: el 07-10 a las
  ~10:50 `appids=1942280` (Brotato, con DLC) respondió bajo `1942280`. Puede
  depender del tipo de hijo o de la región (`cc`). **Medido 2026-10-07
  ~13:40 desde el codespace (red de España)**: `appids=500`, `730`,
  `1091500` y `1942280` responden los cuatro bajo su propia clave, con
  `data.steam_appid` igual al pedido. **No se reproduce.** Temporal, por
  región, o ya revertido por Valve. Dato del Discord de LuaTools, 25-sep 9:11
  (ifuckingHATErafie): *"Steam updated the appdetails api so will need to
  push an app update"*: fue real ese día y forzó la 1.3.2. Nada que cambiar
  hoy; si algún día un juego con DLC se queda sin nombre, sin avisos o con
  "No DLCs found", volver aquí.

### 4. Lo que no cambia

- `/api/denuvo/*` y el login: compatibles con `luatools_auth.py`.
- `files.luatools.work` (fixes gratuitos por índice público) no aparece en la
  app de escritorio: eso es del plugin de Millennium (`luatools-windows/`),
  que es lo que `fixes.py` replica.
- `manifest.luastools.xyz` no aparece en la app: su manifest suelto va por
  `givemethemanifestpunk` autenticado.

### 4b. Segunda pasada, fichero a fichero, en lo que se solapa con nosotros [read]

La primera pasada fue por mensajes de commit, inventario de endpoints y
cabeceras. Esto es lectura de los servicios que hacen lo mismo que piezas
nuestras, para ver dónde coinciden y dónde no.

- **`LuaInstaller.cs` / `LuaFileParser.cs` (lo que hace steamidra_lite).**
  Lua → `config\stplug-in\<appid>.lua`, manifests → `<Steam>\depotcache`
  (comentario textual: *"stplug-in is under config (it is SteamTools'),
  depotcache is not"*). Con el ajuste **"Auto Update Apps" (por defecto
  activado) comenta todas las líneas `setManifestid`** al instalar: el juego
  queda sin pin y Steam lo actualiza solo. Los fixes de Denuvo se instalan con
  `forceLocked` y conservan el pin. Su parser distingue `addappid(id)` a secas
  (DLC, "entitlement") de `addappid(id, 1, "key")` (depot de contenido), y un
  `setManifestid` comentado significa *"Steam keeps this updated"*, no
  "pineado a este manifest". **Es exactamente nuestro modelo 0.9**: nativo sin
  pin por defecto, pin solo para fixes de versión y DLC flag sin clave. Dos
  implementaciones independientes llegan al mismo sitio.
- **`AppInfo/BinaryVdf.cs` + `AppInfoFile.cs` (~450 líneas): lector y
  escritor de `appcache\appinfo.vdf`, v27/28/29.** Layout documentado en el
  fuente: `magic u32 | universe u32 | [v29] offset i64 de la tabla de
  strings`; por app `appid u32 | size u32 | meta (60 bytes en v28+: infoState,
  lastUpdated, picsToken, sha1_text, changeNumber, sha1_binary) | blob`;
  tabla de strings al final (`count u32` + cadenas NUL). Índice
  `appid → offset` en memoria (1,4 MB para 177k apps), lectura bajo demanda.
  Detalles que ellos pisaron: una misma clave puede repetirse en un objeto
  (15 apps de 176.869; un diccionario la colapsa y corrompe el blob), y la
  tabla de strings tiene entradas que no son UTF-8 válido (hay que guardar
  los bytes crudos). **Es la referencia para nuestro parser aparcado**, si
  algún día lo hacemos: solo lectura, y con esos dos detalles.
- **`LaunchOptionsService.cs` / `LaunchModStore.cs`: escriben en
  `appinfo.vdf`** (editan `config.launch` de un juego: opciones de lanzamiento
  mod). Solo con Steam cerrado, porque *"Steam holds the file and rewrites it
  from its own in-memory state"*; copia de seguridad, escritura a temporal y
  swap, y detección de "drift" cuando Steam lo sobreescribe tras una
  actualización de la app. Confirma lo que asumimos: **`appinfo.vdf` nunca se
  escribe con Steam abierto**, y Steam lo regenera de PICS si falta.
- **`DepotDownloaderService.cs`**: sesión **anónima** de SteamKit (nunca
  `-username`), por eso necesita las dos entradas: clave (`-depotkeys`, del
  lua o de `config.vdf` del usuario) y manifest (`-manifestfile`, de
  depotcache o de `givemethemanifestpunk`). Un proceso por depot (el flag de
  manifest es único), `-validate` obligatorio al reanudar (si no, da éxito
  sobre un fichero a medias), serializado porque dos sesiones anónimas
  comparten LoginID y se desconectan. Es el modelo ASSella, con los mismos
  bordes que anotamos en `assella-analysis.md` §3.3.
- **`SteamDepotInfo.cs`**: lista de depots de `api.steamcmd.net` (*"same data
  SteamDB shows"*), con `depotfromapp` para redistribuibles compartidos (en el
  appinfo del juego van como stub sin manifests, la gid vive en la app dueña)
  y `extended.listofdlc` con la nota *"Many have no depot (store-only
  entitlements)"*. Lo mismo que nuestro `steamcmd_app_info` (incluido
  `fromapp`) y nuestra conclusión de DLC flag.
- **`HttpServerService.cs`**: servidor local en `127.0.0.1:6767` para el
  botón "Add via LuaTools" que el plugin inyecta en las páginas de la tienda
  de Steam. Rutas: `/add-status/<id>`, `/has/<id>`, `/open-url`,
  `/restart-steam`, `/check-updates`, `/loaded-apps`, `/api-list`, `/icon`.
  LumaDeck no tiene botón en la tienda; todo va por Decky.
- **`UnlockerService.cs`**: instala y verifica por sha256 `dwmapi.dll`,
  `xinput1_4.dll` y `OpenSteamTool.dll` en la raíz de Steam, desde su
  `OST-Nightly` (upstream compilado de `main`) o desde BetterSteamTools; para
  BST la versión y el hash salen de
  `madoiscool/BetterSteamTools@updates/opensteamtool/latest.toml`, **la misma
  rama `updates` cuyo feed de patrones seguimos en
  `bettersteamtools-findings.md`**. Y registra `config/stplug-in` en el
  `[lua] paths` de `opensteamtool.toml` para que los luas se recarguen en
  caliente.
- **`check_apis` en Ryuu**: sin auth, solo User-Agent fijo
  (`secretgoonpoon`): cualquiera puede preguntar qué fuentes tienen un juego.
  **Hubcap `/status/{app}`** antes de gastar un zip, igual que el plugin
  8.0.4 (nosotros lo descartamos, F2 de `assella-analysis.md`).
- **`DonateKeysService.cs`**: solo validación de pares `(depot, key)` del
  `config.vdf` y `POST` a Ryuu; dedupe permanente por IP en el servidor;
  reintenta lo que no devolvió 200. No lee del pool.

### 5. Accionables

1. ~~Medir el formato de `appdetails`~~ Medido, no se reproduce (§3). Sin
   acción; el ancla por `data.steam_appid` queda como receta si reaparece.
2. **Candidato**: `givemethemanifestpunk` como eslabón de `manifests.py`
   para usuarios con sesión de lua.tools, entre luastools y el suelto de
   Hubcap. Antes, medir qué gids sirve.
3. **Referencia**: si se desaparca el parser de `appinfo.vdf`, partir del
   layout y los dos detalles de `AppInfoFile.cs` (§4b). Solo lectura.
4. Nada en lumalinux.

Próximo barrido desde `9461259` / v1.3.2.
