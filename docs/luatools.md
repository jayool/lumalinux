# LuaTools (madoiscool/LuaTools) — la app de escritorio, leída desde el código

Leído desde el código el 2026-10-09. Sustituye a `luatools-app-analysis.md`:
sus mediciones con fecha están en §5.2, lo que afirmaba y el código ya no
sostiene en §5.1, y sus decisiones con el estado de hoy en §4.4. El motor que
esta app instala, BetterSteamTools, tiene su doc (`bettersteamtools.md`). El
plugin de Millennium de LuaTools es otro programa y no tiene doc; lo único que
nos toca de él está en §5.1. Cómo **nosotros** hacemos lo mismo está en
`nosotros.md`.

---

## §0 Ficha

| | |
|---|---|
| **Qué es** | Un **gestor de escritorio para Windows** (.NET 8 WPF, `LuaToolsGui`) que no se engancha a Steam por sí mismo: instala y mantiene el unlocker (BetterSteamTools, un build nocturno de OpenSteamTool, o el del usuario), escribe los `.lua` y `.manifest` que ese unlocker lee, y mete su propio plugin en las páginas de la tienda de Steam por CDP. Busca juegos, descarga zips de lua+manifests por fuente (lua.tools, Hubcap), genera luas de DLC, descarga "fixes de Denuvo" de lua.tools, cambia de build y fija depots editando el `.lua`, descarga contenido de depots fuera de Steam (DepotDownloaderMod), edita opciones de lanzamiento en `appinfo.vdf` y lanza Steamless, SteamAutoCrack y CloudRedirect. Un `winmm.dll` en la carpeta de Steam la arranca con cada Steam, y en ese arranque se actualiza sola, igual que el plugin. |
| **Repo y commit leídos** | `madoiscool/LuaTools` `main@9461259` (2026-09-25, v1.3.2). 32 commits desde `47b9254` (2026-08-13, "Initial commit"); ramas `main` y `downloads` (fusionada como PR #41 el 08-30); tags `v1.2.7` → `v1.3.2`. 140 ficheros en `src/`, 109 `.cs` (19.903 líneas en `Services/` y `ViewModels/`), 29 idiomas en `.resx`. Relacionados, sin código fuente publicado: `madoiscool/LTSP` (solo un README; las releases traen `plugin.zip` y `winmm.dll`), `madoiscool/OST-Nightly` (último tag `nightly-20260722-2a08b0b`) y `mendy-tools/verynotsusdllsthataredefnotstrelated` (DLL sueltas de OST estable). |
| **Plataforma** | Windows 10/11; Velopack para instalar y actualizar. |
| **Licencia y quién** | MIT (`LICENSE`). Commits: mendy-tools (el grueso), madoiscool, peeblyweeb (README), gattoooo (traducción, estilos), Brandon Hernandez (revert de fixes). Varios mensajes citan a una IA ("ty claude"). El operador es el mismo de BetterSteamTools, `lua.tools`, `luastools.xyz` y `git.lua.tools`. |
| **Relación con los demás programas** | Instala **BetterSteamTools** (o OST nocturno) y le apunta `[lua] paths` a `config/stplug-in`; activa **CloudRedirect** como complemento de OST/BST. Sus fuentes de zips son las mismas que usa LumaDeck: **lua.tools** (con las fuentes Ryuu, Luie y otras detrás) y **Hubcap** con la clave del usuario. El backend de **Ryuu** por IP le dice qué fuentes tienen cada juego y recibe sus claves donadas. Con nosotros: compartimos la API de lua.tools (login, fixes de Denuvo) y las fuentes de zips; no convive con nuestra pila (Windows). |

**Lo que cambia respecto al doc anterior**, en una línea cada uno:

- **"Donate Keys" está activado por defecto** (`Services/SettingsService.cs:98-101`): en cada arranque lee **todas** las `DecryptionKey` de `config/config.vdf` (también las de juegos comprados) y las manda por **HTTP sin cifrar** a `http://167.235.229.108/donatekeys/send` (`Services/DonateKeysService.cs:17,27-65`). El doc anterior decía "si Donate Keys está activado".
- **El servidor local `127.0.0.1:6767` contesta `Access-Control-Allow-Origin: *` sin autenticación** (`Services/HttpServerService.cs:154,604-608`): cualquier página web abierta en cualquier navegador puede borrar el `.lua` de un juego, reiniciar Steam o lanzar una descarga.
- **La app no se engancha a Steam**: lo hacen el unlocker que instala y un `winmm.dll` (loader de LTSP, sin fuente público) que la arranca con `--minimized --tray-locked` en cada Steam y bajo el que se actualizan solos app y plugin (`App.xaml.cs:188-222,480-481`; `Program.cs:61-107`).
- **El CDP de Steam se abre con un truco**: el marcador `.cef-enable-remote-debugging` se crea como junction a una carpeta que no existe (`Services/PluginInstallerService.cs:85-130`), y queda escuchando en `127.0.0.1:8080`.
- **El build OST lo compila madoiscool** (`OST-Nightly`, parado en el 07-22) y el OST estable se reconoce por un espejo de DLL sueltas de mendy-tools (`Services/UnlockerService.cs:35-43,176-203`).
- **Los zips de lua.tools y Hubcap no se validan por dentro**: todo `*.lua` pasa a `<appid>.lua` y todo `*.manifest` a `depotcache` (`Services/LuaInstaller.cs:268-346`). La comprobación por magia `0x71F617D0` que citaba el doc anterior solo está en la descarga de depots (`Services/Downloads/ManifestJobFactory.cs:468,954`).

---

## §1 Mapa del código

Rutas relativas a `src/LuaToolsGui/`. F = función de §2.

### 1.1 Arranque, actualización, plugin

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `Program.cs` | 271 | Velopack, idioma, registro de `luatools://` (HKCU) en cada arranque, instancia única; los arranques del loader (`--minimized`, `--tray-locked`) piden a la instancia viva que se oculte en la bandeja y vuelva a comprobar actualizaciones (`:24-108`) | 11 |
| `App.xaml.cs` | 584 | DI; `OnStartup`: modo DNS, migración de modos, `EnsureLuaPathRegistered` (`:266`), aviso de opciones de lanzamiento sobreescritas (`:112-134`), flujo de actualización **solo** en el contexto del loader: primero la app (Velopack, reinicio), después el plugin (`:188-222,480-481`), donación de claves (`:484`), analítica (`:487`), lista de hardware, migración de `config\depotcache` (`:494`); enrutado de `luatools://` (`:536-583`) | 9, 11 |
| `AppConfig.cs` | 139 | constantes: `db.lua.tools` + clave anon de Supabase, `lua.tools`, Hubcap, callback OAuth `localhost:53789`, tope 25/día, repos de herramientas, backend Ryuu `http://167.235.229.108` con UA `secretgoonpoon`, UA `discord(dot)gg/luatools` para donar, Umami, repos de releases (principal y de reserva), `madoiscool/LTSP`, espejos de GitHub | 10, 12 |
| `Services/UpdateService.cs` | 83 | Velopack sobre `madoiscool/LuaTools` y, si no existe, `mendy-tools/LuaTools`, descargando por los espejos (`:20-25,39-66`) | 11 |
| `Services/PluginInstallerService.cs` | 630 | plugin de `madoiscool/LTSP` (`releases/latest`): `plugin.zip` → `%AppData%\LuaToolsGui\plugin` y `winmm.dll` → raíz de Steam, con el `winmm.dll` de System32 copiado como `winmm_real.dll` (`:54-63,376-383`); comprobación SHA-256 contra el digest del asset (`:333-343`); para cambiar el DLL **mata Steam** y lo reinicia (`:370-400`); desactiva el plugin `luatools` de Millennium (`:393-396,480-...`); junction del marcador CDP en cada consulta de estado (`:274-275`) | 1, 11 |
| `Services/CefInjectorService.cs` | 458 | cada ~1 s lee `http://127.0.0.1:8080/json`, inyecta polyfill + `luatools.js` en las pestañas de `store.steampowered.com` (`:105-181`) y cada 150 ms recoge las llamadas pendientes del shim de Millennium y las reenvía al servidor local (`:183-279`) | 1, 9 |
| `Services/HttpServerService.cs` | 615 | `HttpListener` en `127.0.0.1:6767` (con `netsh urlacl … user=Everyone` elevado si falla, `:99-120`); rutas `/has`, `/add`, `/add-status`, `/add-source`, `/check-sources`, `/download`, `/download-status`, `/cancel`, `/remove`, `/open/fix`, `/open/settings`, `/open-url`, `/restart-steam`, `/check-updates`, `/loaded-apps`, `/api-list`, `/icon` (`:163-189`); CORS `*` sin autenticación | 9 |
| `Services/PluginAddService.cs` | 330 | el flujo "Add via LuaTools" del botón de la tienda, sin ventana | 9 |
| `Services/ProtocolService.cs` | 84 | `luatools://{game,install,manage,fix}/<appid>` y `install/silent/<appid>` (`:31-58`) | 9 |
| `Services/UnlockerService.cs`, `ModeMigration.cs` | 872, 60 | modos `Ost`, `Bst`, `Custom` (§2.1); complemento CloudRedirect (`:542-735`); los cuatro modos antiguos se migran en `Services/ModeMigration.cs` | 1, 8 |

### 1.2 Juegos, fuentes, ficheros

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `Services/LuaToolsApiClient.cs` | 274 | API de lua.tools y de Steam (§3.1) | 4, 6, 10 |
| `Services/AuthService.cs` | 387 | OAuth Discord por Supabase con PKCE y callback local, o código del bot `/login`; tokens con DPAPI en `auth.dat` (`:86-171,324-337`) | 10 |
| `Services/HubcapService.cs` | 82 | clave `smm_<96 hex>`, stats, status y zip con la clave en la URL | 4, 10 |
| `Services/Downloads/ManifestJobFactory.cs` | 971 | trabajos: zip de juego, lua de DLC, fix de Denuvo (manifest o ficheros), depots, SteamAutoCrack | 4, 6 |
| `Services/Downloads/DownloadQueue.cs`, `DownloadItem.cs`, `HttpFileDownloader.cs`, … | 460, 315, 77, … | cola de descargas en `%TEMP%` | 11 |
| `Services/LuaInstaller.cs` | 347 | escritura en `config\stplug-in` y `depotcache`; "Auto Update Apps" comenta los `setManifestid` (`:94-127`) | 4, 5, 9 |
| `Services/LuaFileParser.cs`, `LuaEditor.cs`, `LuaVault.cs` | 209, 76, 689 | lectura del `.lua` (`addappid(id)` = DLC, `addappid(id,1,"key")` = depot), edición de líneas, variantes por build con SHA-256 | 5, 9 |
| `Services/DonateKeysService.cs` | 164 | donación de claves (§2.4) | 4, 12 |
| `Services/DepotDownloaderService.cs` | 658 | DepotDownloaderMod anónimo (§2.4) | 4 |
| `Services/SteamDepotInfo.cs`, `SteamAppInfoCache.cs`, `SteamAppListCache.cs`, `HardwareAppIdService.cs`, `CoverCache.cs` | 233, 643, 102, 70, 124 | depots de `api.steamcmd.net`, `appdetails` anclado por `data.steam_appid`, lista de apps de `applist.morrenus.xyz`, lista de hardware, portadas | 4, 9 |
| `Services/DepotCacheMigrationService.cs` | 170 | mueve lo que las versiones anteriores al 09-09 dejaron en `config\depotcache` | 4 |
| `Services/AppInfo/*` | 233, 215, 178, 156, 236 | lector/escritor de `appcache\appinfo.vdf` v27-29 y opciones de lanzamiento, solo con Steam cerrado, con copia y detección de sobreescritura | 6 |
| `Services/SteamlessService.cs`, `SteamAutoCrackService.cs`, `CloudRedirectService.cs` | 206, 345, 74 | herramientas de GitHub verificadas por digest (si GitHub lo da) | 6, 8 |
| `Services/AppliedFixIndexService.cs`, `FileHash.cs` | 205, 46 | índice de fixes aplicados y revert por hash | 6 |
| `Services/SteamService.cs`, `SteamLibraryService.cs` | 225, 133 | ruta de Steam (registro u override), bibliotecas, `StopSteam` = `Kill(entireProcessTree)` (`:162-170`) | 1, 9 |
| `Services/AppHttp.cs`, `DohResolver.cs`, `GithubProxy.cs`, `ProxiedFileDownloader.cs` | 160, 143, 120, 124 | HTTP común con DNS por `1.1.1.1` (Auto/Siempre/Nunca), espejos de GitHub | 12 |
| `Services/AnalyticsService.cs` | 61 | ping de arranque a Umami | 12 |
| `ViewModels/*` | ~6.900 | páginas Home, Add, Downloads, Manage, Builds (1.639), Mode, Fixes, Plugin, Settings, Onboarding | — |

---

## §2 Las doce funciones

`fichero:línea` de `madoiscool/LuaTools@9461259`, rutas relativas a
`src/LuaToolsGui/`.

### 2.1 Engancharse a Steam

La app no se engancha: **instala quien lo hace**. Tres modos excluyentes
(`Services/UnlockerService.cs:35-64`): `Bst` descarga el zip
`OpenSteamTool-<v>-Release.zip` de las releases de `madoiscool/BetterSteamTools`
con la versión y el SHA-256 de `OpenSteamTool.dll` que dice
`updates/opensteamtool/latest.toml` (`:30-31,226-290`); `Ost` baja la última
release de `madoiscool/OST-Nightly` y compara por el digest del asset; `Custom`
no toca nada. Copia `dwmapi.dll`, `xinput1_4.dll` y `OpenSteamTool.dll` a la
raíz de Steam sin borrar lo del modo anterior (`:293-317`) y, en cada arranque
de la app, se asegura de que `opensteamtool.toml` tenga `config/stplug-in` en
`[lua] paths` con una edición de texto (`:91-96,453-...`). Reconoce un
unlocker ya instalado solo por hash exacto, para no confundir los
`dwmapi.dll`/`xinput1_4.dll` de SteamTools con OST (`:355-408`).

Además pone **su propio loader**, `winmm.dll` de `madoiscool/LTSP`, con el
`winmm.dll` de System32 copiado al lado como `winmm_real.dll`
(`Services/PluginInstallerService.cs:54-63,376-383`). Ese loader no tiene fuente
público; por los comentarios de la app, arranca LuaTools con `--minimized
--tray-locked` cada vez que se abre Steam (`Program.cs:61-107`). Para el plugin
de la tienda habilita el CDP de Steam creando `<Steam>\.cef-enable-remote-debugging`
como junction a `C:\fuckass\folder\that\shall\never\exist\die\millennium`
(`:85-130`), y lo rehace en cada consulta de estado. El CDP queda en
`127.0.0.1:8080` (puerto fijo de Steam), abierto a cualquier proceso local.

### 2.2 Propiedad y licencias

`n/a` en la app: lo hace el unlocker (`bettersteamtools.md` §2.2).

### 2.3 DLC

Un DLC se añade como un `.lua` que genera lua.tools
(`/api/dlc/generate?appid=&base=`, con sesión) y se instala como otro juego
(`Services/LuaToolsApiClient.cs:179-186`;
`Services/Downloads/ManifestJobFactory.cs:57-75`); `/api/dlc/info` dice qué hay.
Desde la página Builds se activa o desactiva cada `addappid` del `.lua` (salvo
el del juego base), comentando la línea. El resto lo decide el unlocker.

### 2.4 Claves y manifests

**De dónde salen.** De zips de lua + manifests que la app pide por fuente
(`ManifestJobFactory.cs:33-54`): con sesión de lua.tools,
`lua.tools/api/manifest/download?appid=&source=<nombre>&game_name=` (cuenta para
el tope de 25 al día, que la app cuenta en vivo con un `HEAD` a
`db.lua.tools/rest/v1/user_downloads`, `LuaToolsApiClient.cs:103-132`); con
clave de Hubcap, `hubcapmanifest.com/api/v1/manifest/<app>?api_key=` directo
(`Services/HubcapService.cs`). Qué fuentes tiene cada juego lo dice
`http://167.235.229.108/check_apis?appid=` (por HTTP, solo User-Agent)
(`LuaToolsApiClient.cs:87-96`). Las claves van dentro del `.lua`.

**Qué escribe.** Todo `*.lua` del zip a `<Steam>\config\stplug-in\<appid>.lua` y
todo `*.manifest` a `<Steam>\depotcache\` (sin pisar uno que ya exista), sin
mirar el contenido de ninguno (`Services/LuaInstaller.cs:268-346`). El `.lua` lo
ejecuta después el unlocker con la biblioteca estándar de Lua completa
(`bettersteamtools.md` §2.12).

**Manifest suelto.** Para la descarga de depots,
`lua.tools/api/givemethemanifestpunk/<depot>/<gid>` con sesión: según el
comentario del cliente, no cuenta para el tope y tiene 120 peticiones cada 10
minutos (`LuaToolsApiClient.cs:173-177`). Se valida por la magia de manifest
antes de usarlo (`ManifestJobFactory.cs:417-487,954`).

**Descarga fuera de Steam.** `DepotDownloaderMod` (re-empaquetado propio en
`mendy-tools/DepotDownloaderMod`, verificado por digest) con **sesión anónima**:
clave del `.lua` o de `config.vdf` (`-depotkeys`) y manifest de `depotcache` o
del endpoint anterior (`-manifestfile`), un proceso por depot y en serie, con
`-validate` al reanudar (`Services/DepotDownloaderService.cs:40-56,216-298,435-457`;
`ManifestJobFactory.cs:207-384`). El fichero de claves se borra al terminar.

**Códigos de manifest.** No: los pide el unlocker.

**Donación de claves.** Activada por defecto (`Services/SettingsService.cs:98-101`):
en cada arranque, todas las parejas `depot:clave` de `config/config.vdf` que no
haya enviado antes, en un `POST text/plain` por **HTTP** a
`http://167.235.229.108/donatekeys/send` con UA `discord(dot)gg/luatools`; solo
recuerda en local qué depots ya mandó (`Services/DonateKeysService.cs:17-65`). No
lee de ese fondo.

**Qué deja de funcionar si cae cada servicio.** Sin lua.tools: sin zips (salvo
Hubcap con clave), sin DLC, sin manifests sueltos ni fixes. Sin Hubcap: sin su
fuente. Sin el backend por IP: sin lista de fuentes por juego.

### 2.5 Updates de juegos

"Auto Update Apps", **activado por defecto** (`Services/SettingsService.cs:91-94`):
al instalar un `.lua` comenta todas sus líneas `setManifestid` y Steam actualiza
el juego como cualquier otro; se respetan los pins si el fichero viene de un
fix (`forceLocked`) o se llama `<algo>_<buildid>` (`Services/LuaInstaller.cs:54,94-127`).
La página Builds guarda variantes del `.lua` por build en `LuaVault` (SHA-256
del contenido) y deja cambiar de una a otra y fijar o soltar cada depot
comentando su `setManifestid` (`ViewModels/BuildsViewModel.cs`). No hay pase de
updates propio.

### 2.6 Fixes y DRM

**Fixes de Denuvo de lua.tools.** Catálogo `/api/denuvo/listings` y
`/api/denuvo/fixes?appid=` sin sesión; descarga `/api/denuvo/download?fix=&slot=`
con sesión, que devuelve una URL firmada (`LuaToolsApiClient.cs:191-223`). Si el
slot es `manifest`, es un `.lua`/zip que se instala con los pins intactos; si
no, el zip se extrae sobre la carpeta del juego, con defensa contra rutas que
salgan de ella, copia `.bak` de lo que pisa y un registro por fix en
`<juego>\.luatools-fix\<fix>.json` con hashes antes y después, para deshacerlo
solo si el fichero sigue siendo el que puso el fix
(`ManifestJobFactory.cs:588-700,732-762`; `Services/AppliedFixIndexService.cs`).

**Herramientas.** Steamless (`atom0s/Steamless`), SteamAutoCrack (solo abre su
GUI, que no admite argumentos, e instala el runtime .NET 10 Desktop si falta) y
CloudRedirect GUI, todos de GitHub con el digest del asset; **si el asset no
trae digest, se acepta sin comprobar** (`Services/AssetHash.cs:35-38`).

**Opciones de lanzamiento.** Edita `config.launch` de un juego en
`appcache\appinfo.vdf`, solo con Steam cerrado (lo cierra matándolo), con copia
de seguridad y escritura por temporal; al arrancar avisa si Steam lo ha
reescrito y ofrece reaplicarlo (`Services/AppInfo/LaunchOptionsService.cs`,
`App.xaml.cs:112-167`).

Online-fix y "Fix Game" no están en la app: son del plugin de Millennium.

### 2.7 Logros, stats y tiempo de juego

`n/a`: los da el unlocker (`bettersteamtools.md` §2.7).

### 2.8 Cloud saves

Complemento de CloudRedirect para los modos OST/BST: descarga `cloud_redirect.dll`
de `Selectively11/CloudRedirect` (`releases/latest`, verificado por digest) y
cambia `[cloud] enabled` en `opensteamtool.toml`; desactivar deja la DLL
(`Services/UnlockerService.cs:542-735`). Botón aparte para abrir el
`CloudRedirect.exe`.

### 2.9 Añadir y quitar un juego

**Entradas.** Búsqueda en la tienda de Steam (sin cuenta), tiras de destacados,
appid, arrastrar un enlace de SteamDB o de la tienda, un `.lua`/`.zip`/`.manifest`
suelto, `luatools://install/<appid>` (y `install/silent/<appid>`, que descarga e
instala sin ventana y **sin la confirmación de sobreescritura**,
`ViewModels/DownloadViewModel.cs:183-225,685`), y el botón "Add via LuaTools" de
la tienda. Con FastFetch elige sola la primera fuente disponible.

**Qué escribe.** `<appid>.lua` y los `.manifest` (§2.4); nada de ACF ni de
biblioteca: el unlocker recarga el `.lua` en caliente y Steam hace el resto.

**Qué quita.** Borrar desde Manage elimina solo el `.lua` (y su `.lua.disabled`);
los `.manifest` de `depotcache` y los ficheros del juego se quedan
(`ViewModels/ManageViewModel.cs:440-520`; `Services/HttpServerService.cs:431-449`).

**Servidor local.** `127.0.0.1:6767` contesta a cualquiera, con CORS `*` y sin
token (`HttpServerService.cs:154,604-608`): una página web cualquiera puede
hacer `POST /remove/<appid>` (borra el `.lua`), `POST /restart-steam` (mata y
reinicia Steam), `POST /download/<appid>` (descarga e instala sin confirmar,
gastando el tope del usuario) o `POST /open-url` (abre una URL `http(s)` en el
navegador).

### 2.10 Credenciales y proveedores

- **lua.tools**: Discord por OAuth de Supabase con PKCE y callback en
  `localhost:53789`, o un código del bot de Discord (`/login`) que se canjea en
  `lua.tools/api/auth/code/redeem` y `db.lua.tools/auth/v1/verify`; refresco
  por `db.lua.tools/auth/v1/token`. Las cuentas del bot (`@bot.lua.tools`) se
  invitan a enlazar la cuenta completa. Tokens cifrados con DPAPI en
  `%AppData%\LuaToolsGui\auth.dat` (`Services/AuthService.cs`). Necesaria para
  zips, DLC, manifest suelto y descarga de fixes.
- **Hubcap**: clave del usuario, en claro en `settings.json`, enviada en la URL
  (`?api_key=`) salvo en `/status`.
- Sin ninguna: buscar, ver fixes y gestionar lo instalado.

### 2.11 Mantenimiento propio

**Superficie.** App de escritorio con bandeja; bajo el loader, el cierre de la
ventana la manda a la bandeja.

**Update de sí misma.** Velopack desde las releases de `madoiscool/LuaTools`
(y, si desapareciera, `mendy-tools/LuaTools`), **solo cuando la arranca el
loader**, sin preguntar, y antes de actualizar el plugin (`App.xaml.cs:188-222`;
`Services/UpdateService.cs`). Un arranque manual no se actualiza; si hay una
versión descargada, se aplica al salir.

**Componentes.** Plugin (`plugin.zip` + `winmm.dll`) en el mismo flujo: si el
DLL cambia, mata Steam, lo sustituye y relanza ("Updating plugin. Steam will
restart."); `.luatools-dll-update-disabled` en la carpeta de Steam lo impide
(`PluginInstallerService.cs:195-197,370-400`). El unlocker se actualiza desde la
página Mode. La versión instalada se sabe por hash de los ficheros.

**Espejos.** Si GitHub falla, la metadata se pide a `lua.tools/api/gh/` (su
proxy) y los ficheros a `ghproxy.net`, `ghfast.top` o `gh.ddlc.top`
(`Services/GithubProxy.cs:23-43`; `AppConfig.cs:129-138`). El `latest.toml` de
BST va por `raw.githubusercontent.com`, que cuenta como descarga: con GitHub
caído, **el mismo espejo de terceros** sirve el hash y el zip que lo cumple. Lo
mismo con los ficheros de Velopack (`Services/ProxiedFileDownloader.cs`).

**Salud.** Logs del plugin (`PluginLog`), avisos y notificaciones.

### 2.12 Proyecto

Windows, MIT, un mantenedor principal (mendy-tools) y ritmo de ráfagas (32
commits del 08-13 al 09-25, nada desde v1.3.2). Terceros: lua.tools,
`db.lua.tools`, `analytics.lua.tools`, Hubcap, el backend de Ryuu por IP, GitHub
y tres espejos públicos, `1.1.1.1` (DoH), la tienda y la API de Steam,
`api.steamcmd.net`, `applist.morrenus.xyz`, `raw.githubusercontent.com/jsnli/…`.

**Privacidad y riesgo.** En cada arranque, sin preguntar: un evento a Umami
(`analytics.lua.tools`, versión de la app, User-Agent de Chrome falso, sin
opción para apagarlo; `Services/AnalyticsService.cs:23-60`) y, con el valor
por defecto, las claves de depot de todo `config.vdf` por HTTP a una IP. Con
GitHub inaccesible, el código que se instala en la carpeta de Steam lo deciden
espejos de terceros. El servidor local y el CDP de Steam quedan abiertos a
páginas web y procesos locales (§2.1, §2.9).

---

## §3 Superficies externas

### 3.1 Hosts y URLs

| URL / host | Fichero:línea | Uso |
|---|---|---|
| `https://lua.tools/api/{manifest/download, dlc/info, dlc/generate, givemethemanifestpunk/<depot>/<gid>, denuvo/listings, denuvo/fixes, denuvo/download, me/supporter-status, auth/code/redeem}` | `Services/LuaToolsApiClient.cs:134-223`; `AuthService.cs:126` | zips, DLC, manifest suelto, fixes, login |
| `https://db.lua.tools/auth/v1/{authorize,token,verify}`, `/rest/v1/user_downloads` (cabecera `apikey` anon) | `AppConfig.cs:9-12`; `AuthService.cs:91,156,216,258`; `LuaToolsApiClient.cs:109` | Supabase |
| `https://analytics.lua.tools/api/send` | `AnalyticsService.cs:42` | Umami |
| `https://lua.tools/api/gh/` | `AppConfig.cs:129-132` | proxy de la API de GitHub |
| `https://hubcapmanifest.com/api/v1/{manifest/<app>,status/<app>,user/stats}` | `HubcapService.cs` | Hubcap |
| `http://167.235.229.108/check_apis?appid=` (UA `secretgoonpoon`), `/donatekeys/send` (UA `discord(dot)gg/luatools`) | `AppConfig.cs:80-85`; `LuaToolsApiClient.cs:91`; `DonateKeysService.cs:17` | fuentes por juego, donación (HTTP) |
| `api.github.com/repos/…/releases` y `github.com/…/releases/download/…` de `madoiscool/{LuaTools,LTSP,BetterSteamTools,OST-Nightly}`, `mendy-tools/{LuaTools,DepotDownloaderMod,verynotsusdllsthataredefnotstrelated}`, `atom0s/Steamless`, `SteamAutoCracks/Steam-auto-crack`, `Selectively11/CloudRedirect` | `AppConfig.cs`; `UnlockerService.cs`; `PluginInstallerService.cs:236` | herramientas y updates |
| `https://raw.githubusercontent.com/madoiscool/BetterSteamTools/refs/heads/updates/opensteamtool/latest.toml` | `UnlockerService.cs:30-31` | versión y hash de BST |
| `https://{ghproxy.net,ghfast.top,gh.ddlc.top}/<url de GitHub>` | `AppConfig.cs:133-138` | espejos |
| `https://1.1.1.1/dns-query` | `Services/DohResolver.cs` | DNS |
| `store.steampowered.com/api/{storesearch,featuredcategories,appdetails}`, `api.steampowered.com/ISteamApps/GetAppList/v2/`, `api.steamcmd.net/v1/info/<app>`, `applist.morrenus.xyz`, `raw.githubusercontent.com/jsnli/steamappidlist/…`, `cdn.cloudflare.steamstatic.com` | `AppConfig.cs:33-42`; `SteamAppInfoCache.cs`; `SteamDepotInfo.cs`; `SteamAppListCache.cs:15` | metadatos |
| `raw.githubusercontent.com/sushi-dev55-alt/…/<appid>.zip`, `http://167.235.229.108/<appid>` | `HttpServerService.cs:88-92` | solo la lista `/api-list` que ve el plugin |
| `http://127.0.0.1:8080/json` (+ WebSocket CDP) | `CefInjectorService.cs:35` | inyección del plugin |

### 3.2 Ficheros y registro

| Ruta | Quién | Para qué |
|---|---|---|
| `<Steam>\{dwmapi,xinput1_4,OpenSteamTool}.dll`, `<Steam>\opensteamtool.toml` (`[lua] paths`, `[cloud] enabled`), `<Steam>\cloud_redirect.dll` | escribe | unlocker |
| `<Steam>\winmm.dll`, `<Steam>\winmm_real.dll` (copia de System32), borrado de `bcrypt.dll`, `psapi.dll`, `dbghelp.dll` antiguos | escribe | loader |
| `<Steam>\.cef-enable-remote-debugging` (junction), `<Steam>\.luatools-dll-update-disabled` (lee) | escribe / lee | CDP, bloqueo de update |
| `<Steam>\millennium\…\config.json`, carpeta del plugin `luatools` de Millennium (renombrada `.disabled-by-luatools`) | escribe | desactivar el plugin de Millennium |
| `<Steam>\config\stplug-in\<appid>.lua` (+ `.lua.disabled`), `<Steam>\depotcache\*.manifest` | escribe y borra | juegos |
| `<Steam>\config\depotcache\*.manifest` | mueve | migración |
| `<Steam>\config\config.vdf` | lee | claves (donación, depots) |
| `<Steam>\appcache\appinfo.vdf` (+ copias) | lee y escribe con Steam cerrado | opciones de lanzamiento |
| `<juego>\.luatools-fix\<fix>.json` y `.bak` | escribe | fixes y revert |
| `%AppData%\LuaToolsGui\{settings.json, auth.dat, plugin\, protocol_url.tmp, cachés}` | escribe | estado |
| `%TEMP%\LuaToolsGui\…`, `%TEMP%\LuaTools\downloads` | escribe | descargas |
| `HKCU\Software\Classes\luatools` | escribe en cada arranque | protocolo |
| `HKCU\…\Valve\Steam\SteamPath`, `HKLM\…\Valve\Steam\InstallPath` | lee | ruta de Steam |
| `netsh http add urlacl url=http://127.0.0.1:6767/ user=Everyone` (elevado) | si el puerto no abre | servidor local |

### 3.3 Variables de entorno

`MILLENNIUM__CONFIG_PATH` (lee, para encontrar la config de Millennium,
`PluginInstallerService.cs:505`). Argumentos: `luatools://…`, `--minimized`,
`--tray-locked`.

### 3.4 Ajustes (`settings.json`)

| Clave | Defecto | Efecto | Fichero:línea |
|---|---|---|---|
| `AutoUpdateApps` | `true` | comenta los `setManifestid` al instalar | `SettingsService.cs:91-94` |
| `DonateKeys` | `true` | donación de claves de `config.vdf` | `SettingsService.cs:98-101` |
| `FastFetch` | — | primera fuente disponible, descarga directa | `SettingsService.cs:52-54` |
| `MinimizeToTray` | — | cerrar = bandeja | `SettingsService.cs:148` |
| `DnsMode` | `Auto` | DoH `1.1.1.1` solo si falla el DNS del sistema / siempre / nunca | `SettingsService.cs:170-173` |
| `HubcapApiKey`, `SteamPathOverride`, `SelectedMode`, `Language` | — | clave, ruta, modo, idioma | `SettingsService.cs` |

---

## §4 Lo que nos afecta

### 4.1 Hallazgos pendientes de decisión

Aparcados el 2026-10-09. Numeración propia de este doc.

1. **La API de lua.tools que compartimos.** LumaDeck usa el mismo login
   (`db.lua.tools`, código del bot) y los mismos `/api/denuvo/fixes` y
   `/api/denuvo/download` (`luatools_auth.py`, `nosotros.md` §2.6). La app
   añade `/api/denuvo/listings` (catálogo completo sin sesión) y el recuento del
   tope diario por `HEAD user_downloads` con `Prefer: count=exact`; LumaDeck no
   muestra el tope. Dato; sin acción.
2. **`appdetails` anclado por `data.steam_appid`.** La app no se fía de la clave
   del JSON (`0485dec`, §5.2); LumaDeck indexa por `str(appid)` en tres sitios
   (`downloads.py` `fetch_app_name` y `get_game_notices`, `slssteam_ops.py`
   `_fetch_dlc_list`). No se reproduce desde el 10-07; si un juego con DLC se
   queda sin nombre, sin avisos o con "No DLCs found", aplicar la misma ancla.
3. **`appinfo.vdf`, referencia para el parser aparcado.** Layout v27-29
   (`magic | universe | [v29] offset de la tabla de strings`; por app
   `appid | size | 60 bytes de meta | blob`; tabla de strings al final), claves
   repetidas en un mismo objeto (15 de 176.869 apps) y cadenas que no son UTF-8
   válido (`Services/AppInfo/AppInfoFile.cs`, `BinaryVdf.cs`). Y confirma que
   no se escribe con Steam abierto. Solo lectura, si se desaparca.
4. **Pins comentados = actualizar con Steam.** "Auto Update Apps" comenta los
   `setManifestid` y deja los de fixes: es nuestro modelo 0.9 (nativo sin pin,
   pin solo para fixes de versión, DLC por flag sin clave). Dos implementaciones
   independientes llegan al mismo sitio. Sin acción.
5. **Revert de fixes por hash.** La app guarda qué tocó cada fix con hash antes
   y después y deshace solo si el fichero sigue siendo el suyo. LumaDeck
   reinstala, no deshace. Referencia si alguna vez hacemos revert.

### 4.2 Dependencias y qué rompe si cambia

| Si cambia… | Qué nos pasa |
|---|---|
| el login o `/api/denuvo/*` de lua.tools | LumaDeck pierde el catálogo y la descarga de fixes de Denuvo (`luatools_auth.py`) |
| las fuentes detrás de lua.tools (Ryuu, Luie) | nada directo: LumaDeck va a Ryuu y Hubcap por su cuenta |
| el formato de los `.lua` que sirve lua.tools | `steamidra_lite.py` puede no verlo (como `lumacore.md` §4.1-1) |
| la app | nada; Windows |

### 4.3 Lo que LuaTools tiene y nosotros no (y al revés)

| Función | LuaTools | Nosotros | Notas |
|---|---|---|---|
| 1 | instala y verifica el unlocker y su loader; plugin en la tienda por CDP | `setup.sh` instala SLSsteam/lumalinux; LumaDeck por CDP | mismo papel de "gestor" |
| 4 | zips por lua.tools/Hubcap; manifest suelto con sesión; depots fuera de Steam; donación de claves | cadena de manifests con validación; Steam descarga; no donamos | §4.4 L3, L4 |
| 5 | Builds: variantes del `.lua` por build | `pins.py`, freeze | |
| 6 | fixes de Denuvo con revert; Steamless, SteamAutoCrack; opciones de lanzamiento en `appinfo.vdf` | fixes por botón (lua.tools, Steamless, Goldberg, EOS) | §4.1-5 |
| 9 | tienda + arrastrar + protocolo + botón en la tienda; borrar = solo el `.lua` | Decky; añadir sin reinicio, quitar con limpieza | |
| 11 | update silencioso al abrir Steam | LumaDeck actualiza componentes con estado | |

### 4.4 Decisiones ya tomadas (del doc anterior, con fecha) y estado hoy

| # | Decisión | Fecha | Hoy |
|---|---|---|---|
| L1 | `appdetails` por clave: medido, no se reproduce; el ancla por `data.steam_appid` queda como receta | 2026-10-07 | vigente (§4.1-2) |
| L2 | `givemethemanifestpunk` como eslabón de `manifests.py`: descartado por el usuario porque exige login en lua.tools | 2026-10-07 | vigente |
| L3 | lua.tools como fuente de zips en `api.json` (solo añadiría Luie/Skyflare y otro camino a Ryuu): apuntado, no propuesto | 2026-10-07 | vigente; Ryuu sigue siendo la fuente de LumaDeck |
| L4 | No donar claves ni códigos | 2026-10-07 | vigente (con `bettersteamtools.md` D7) |
| L5 | Ryuu: guardar solo la sesión cuya home lleva `data-user-id="<id Discord>"`, y comprobarla con el contexto SSL de `http_client` | 2026-10-07 | **hecho** (`ryuu_cookie.py`) |
| L6 | Reordenar el `.lua` de Ryuu para que el `addappid` del juego vaya primero antes de `steamidra_lite` | 2026-10-07 | **hecho** (LumaDeck `fcef4f7`) |
| L7 | Parser de `appinfo.vdf` aparcado; si se hace, partir de `AppInfoFile.cs`, solo lectura | 2026-10-07 | aparcado (§4.1-3) |

---

## §5 Historial

### 5.1 Lo que se creía y ya no es así

Además de lo listado en la cabecera:

- **"La app comparte con LumaDeck el `/api/denuvo/fixes` con Bearer".** El
  catálogo (`/listings`, `/fixes`) va sin sesión; solo la descarga la lleva
  (`LuaToolsApiClient.cs:191-216`).
- **"Mismas comprobaciones que `manifests.py`".** La de magia de manifest solo
  en la descarga de depots; los zips de juego no se validan (cabecera).
- **"Sushi como zip de respaldo".** Sushi y la URL de Ryuu por IP solo aparecen
  en la lista que ve el plugin (`/api-list`); las descargas van por lua.tools
  o Hubcap (`HttpServerService.cs:47-93,572`).
- **"El updater de BST se resuelve desde la rama `updates`".** De ahí sale solo
  la versión y el hash; el zip se baja de las releases del repo
  (`UnlockerService.cs:260-264`).
- Lo que el doc anterior contaba del binario v1.2.6 (puerto, rutas, servicios)
  sigue siendo válido en lo esencial y está recogido en las secciones de arriba.
- **El plugin de Millennium** (8.0.4 y 9.0.1, leído en 2026-07) consulta los
  fixes con un índice público, `https://index.luatools.work/fixes-index.json`
  (`{genericFixes: [...], onlineFixes: [...]}`), y luego baja
  `files.luatools.work/OnlineFix1/<appid>.zip` o `GameBypasses/<appid>.zip`.
  LumaDeck hace un `HEAD` por appid a esas mismas dos URLs
  (`LumaDeck/backend/fixes.py:73-89`); el índice sería una alternativa, no un
  arreglo: el `HEAD` funciona.

**Lo que sigue sin medir**:

- Qué hace el loader `winmm.dll` además de arrancar la app (sin fuente).
- Qué gids sirve `givemethemanifestpunk` (con sesión).
- Si el backend por IP sigue aceptando claves y qué hace con ellas.

### 5.2 Pruebas y mediciones con fecha

#### Función 4 — Claves y manifests

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-10-07 | `http://167.235.229.108/check_apis?appid=1942280`, UA `secretgoonpoon` | qué fuentes tiene Brotato | `{"Luie":"available","Ryuu":"available"}`; no aparecen TwentyTwo Cloud, Sushi ni Skyflare | `Luie` es el generador propio de lua.tools (lo nombra `LuaVaultTests.cs`: "Luie vs Hubcap"), solo servido por `lua.tools/api/manifest/download?source=Luie` con sesión |
| 2026-10-07 | `lua.tools/api/givemethemanifestpunk/…` | sin sesión | 401 | los dos gids de Brotato están también en `manifest.luastools.xyz`; hace falta un gid que no esté allí para saber si es otro archivo |
| 2026-10-07 18:25 | Ryuu `/download?appid=1942280&file_type=manifest` con sesión de Discord | descarga de zip | `200 application/zip`, 42 KB, lua + manifests | el 404 que contaba el Discord de LuaTools no se reproduce; el 403 de la mañana era la cookie anónima previa al login (§4.4 L5) |
| 2026-10-07 19:47 | Ryuu, Balatro 2379780 | orden del `.lua` | `addappid(2379780)` al **final** (Hubcap lo pone primero) | `steamidra_lite` tomaba el depot 2379781 como juego; arreglado (§4.4 L6), reintento 19:56 correcto |

#### Función 9 — Añadir y quitar un juego

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-25 | `0485dec` (comentario del commit) | `appdetails` | desde ~sep-2026, una app con apps hijas (DLC, banda sonora) respondía bajo la clave de un hijo: 500→1660740, 730→2678630, 620→323180, 220→323140, 243470→293061, 1091500→2441600; `data.steam_appid` siempre el pedido | forzó la v1.3.2 ("Steam updated the appdetails api", Discord de LuaTools, 09-25) |
| 2026-10-07 ~13:40 | `appdetails` desde España | 500, 730, 1091500, 1942280 | los cuatro bajo su propia clave, con `data.steam_appid` igual | no se reproduce (§4.4 L1); el 07-10 Brotato también respondió bajo su clave |

### 5.3 Cronología

- **2026-07-23.** v1.2.6, solo binario.
- **2026-08-13/14.** Se publica el fuente (`47b9254`, 105 ficheros C#); v1.2.8
  (`18f6f9c`), estadísticas de Hubcap (`9b4407c`), README.
- **2026-08-21 → 08-30.** Descarga de depots (`54fbe9c`, `58d36db`, `0be1bf4`),
  SteamAutoCrack y arreglos de depots (`a08f2a9`), merge de `downloads` (#41);
  v1.3.0.
- **2026-09-07/08.** Revert de fixes y filtro "Mis juegos" (`54e3d56`); nombres
  de fix saneados y defensa contra zip-slip (`53f071b`).
- **2026-09-09.** `9122c6e`: los `.manifest` pasan de `config\depotcache` a
  `depotcache` y se migran los antiguos; v1.3.1.
- **2026-09-25.** `0485dec` ancla de `appdetails` por `data.steam_appid`;
  `9461259` DNS por Cloudflare ("si tu proveedor bloquea lua.tools"); v1.3.2.
  Nada desde entonces.
