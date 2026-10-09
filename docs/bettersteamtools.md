# BetterSteamTools (madoiscool/BetterSteamTools) — la DLL, leída desde el código

Leído desde el código el 2026-10-09. Sustituye, en lo que toca a
BetterSteamTools y a su antepasado OpenSteamTool, a `bettersteamtools-findings.md`:
sus mediciones con fecha están en §5.2, lo que afirmaba y el código ya no
sostiene en §5.1, y sus decisiones con el estado de hoy en §4.4. El fork chino
(`huanyuejue/OpenSteamTool`) tiene su propio doc, `opensteamtool-cn.md`. La app
LuaTools, que usa BST como motor en Windows, va en `luatools.md`.
Cómo **nosotros** hacemos lo mismo está en `nosotros.md`.

---

## §0 Ficha

| | |
|---|---|
| **Qué es** | Una DLL que se mete **dentro del proceso de Steam en Windows**: `dwmapi.dll` y `xinput1_4.dll` son proxies que Steam carga de su propia carpeta y que cargan `OpenSteamTool.dll`; esta engancha `steamclient64.dll` y `steamui.dll` con Microsoft Detours y trampas `int3`/VEH, reescribe mensajes protobuf del CM y del IPC cliente↔juego, y ejecuta los `.lua` de `config/stplug-in/` en un Lua 5.5 **sin sandbox**. Hace en un solo binario lo que nuestro stack reparte entre SLSsteam, lumalinux y LumaDeck: propiedad y package 0, claves, pin, códigos de manifest, precarga de manifests desde un archivo público, donación de códigos con la sesión del usuario, tickets (forjados, de registro o acuñados por un backend), logros con SteamID de donante, family sharing, ruta Spacewar (480), clave de producto legacy, nube por CloudRedirect e inyección de DLL en juegos. Se actualiza sola. |
| **Repo y commit leídos** | `madoiscool/BetterSteamTools` `main@4747385` (2026-09-21, tag `v1.0.4`); rama `updates@bfa812e` (el canal del auto-update); feed de patrones `madoiscool/steam-monitor` ramas `pattern@6fd7961`, `ipc@c814bde`, `protobuf@729e231` (las tres 2026-10-08). 75 commits en `main` desde `92572e5` (2026-04-24, el "Initial commit" de OpenSteamTool); 172 ficheros; 129 `.cpp`/`.h` en `src/` con 17.713 líneas (`Steam/Enums.h` son 1.781, `Hook/Hooks_NetPacket.cpp` 2.161). |
| **Plataforma** | Windows x64 solo (`README.md`, CMake 3.20+, C++20). El backend del almacén de credenciales es el registro de Windows; "The Linux backend is not implemented yet" (`README.md:66`). |
| **Licencia y quién** | GPL-3.0 (`LICENSE`). OpenSteamTool lo escribió `OpenSteam001` (46 commits, el último `2a08b0b`, 2026-07-06) con siete contribuidores ocasionales. BST lo mantiene `mendy-tools` (= madoiscool, 17 commits desde `2c7af78`, 2026-08-11) más uno de `Tesla697` (`e55c90b`, Denuvo). El operador de BST es el mismo de LuaTools y de `luastools.xyz` / `git.lua.tools`. |
| **Relación con los demás programas** | **OpenSteamTool** (`OpenSteam001`) es su origen, parado desde julio; no tiene doc propio, su historia va aquí (§5.3). **LuaTools** lo instala en Windows como motor ("Mode → BetterSteamTools") y lo resuelve desde la rama `updates`. **`opensteamtool-cn`** (huanyuejue) es un fork hermano que salió de OST antes de BST y lee otro feed. **LumaCore** comparte el antepasado OST. Con nosotros: el archivo de manifests que BST alimenta (`manifest.luastools.xyz`) es el paso 3 de la cadena de manifests de LumaDeck (`nosotros.md` §2.4); el resto de la DLL no convive con nuestra pila (es Windows). |

**Lo que cambia respecto al doc anterior**, en una línea cada uno:

- **El feed de patrones es `madoiscool/steam-monitor`** (raw → jsDelivr → `git.lua.tools/luatools/steam-monitor`), no `OpenSteam001/steam-monitor`; la cadena de cinco espejos de `f721ebe` quedó en tres (`Utils/SteamMetadata/RemoteToml.cpp:19-23`).
- **`madoiscool/steam-monitor` no genera nada**: su `main` solo lleva un workflow que copia cada 15 s, con `push +refs/heads/*`, un repositorio oculto (`secrets.UPSTREAM_URL`); las TOML dicen "published by evil-steam-monitor". Su README promete "only ever fast-forwarded" (§5.2).
- **El RVA manda y la firma no se usa**: con `rva` presente la DLL devuelve `base + rva` sin mirar los bytes (`Utils/SteamMetadata/PatternLoader.cpp:237-243`), y todas las TOML del feed traen `rva` (§5.2). Ningún fichero del feed está firmado ni la DLL lo pediría.
- **La red va antes que la caché**: cada arranque descarga las tres TOML y solo si fallan todos los espejos usa la copia local (`RemoteToml.cpp:111-178`).
- **La precarga de manifests ya no va dentro de `BuildDepotDependency`**: desde `0b776c5` (09-20) se hace en `YldLoadDepotManifest` (5 s) y en un hilo al ver la petición de código (`Hook/Hooks_Manifest.cpp:197-210,224-238`; `Hook/Hooks_NetPacket.cpp:1059-1083`).
- **La donación captura los códigos de *todas* las descargas**, no solo de los juegos del `.lua`: cada `GetManifestRequestCode` saliente se apunta y su respuesta buena se envía al archivo (`Hooks_NetPacket.cpp:1012-1021,1126-1150`), con `[donate] enabled = true` **por defecto en el código**, no solo en el ejemplo (`Utils/Config/Config.h:52`).
- **El ticket de propiedad 858 sí se suplanta**: el comentario de cabecera dice que solo registra el formato hasta confirmar offsets; el código ya mete el ticket del registro o uno acuñado por el backend (`Hooks_NetPacket.cpp:608-678`).
- **El ETicket por 5527 está muerto** (el `case` está comentado, `Hooks_NetPacket.cpp:1972`): pasó al IPC.
- **El Lua no tiene sandbox**: `luaL_openlibs` abre `os` e `io` (`Utils/Config/LuaConfig.cpp:498`), más `http_get`/`http_post`. Cualquier `.lua` en `stplug-in` ejecuta lo que quiera dentro de `steam.exe`.
- **Workshop sí**: la precarga de `YldLoadDepotManifest` cubre los ítems del Workshop (depot = appid), que no pasan por `BuildDepotDependency` (`Hooks_Manifest.cpp:213-222`).

---

## §1 Mapa del código

Rutas relativas a `src/`. F = función de §2. "Muerto" = sin llamadores o con la
instalación comentada.

### 1.1 Arranque, proxies, patrones

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `dllmain.cpp`, `dllmain.h` | 175, 42 | `DllMain` solo trabaja si el host es `steam.exe` (`:139-158`; si no, es `rundll32` sirviendo un `bst://`); hilo de arranque (`:54-128`): `Log::Init`, ruta de Steam = **directorio actual** (`:25`), `LoadLibrary` de `steamclient64.dll` y `steamui.dll`, `opensteamtool.toml`, patrones de `steamui` y `steamclient`, specs IPC, `.lua` de todas las carpetas, watchers, hooks de SteamUI y de steamclient, aviso de funciones que faltan, CloudRedirect, donante, registro de `bst://`, auto-update en otro hilo. `DiversionPath` (`bin\diversion.dll`, `:32`) no lo usa nadie. `kOnlineFixAppId = 480` | 1, 11 |
| `dwmapi/dwmapi.cpp` | 147 | forwarders `#pragma` a `DWMAPI.*` (ordinales incluidos); en `DllMain` (bajo el loader lock) `LoadLibraryA("OpenSteamTool.dll")` por nombre relativo si el proceso es `steam.exe` (`:115-140`); si falla, `DllMain` devuelve `FALSE` | 1 |
| `xinput1_4/xinput1_4.cpp`, `.def` | 141, 15 | envoltorio que carga `System32\xinput1_4.dll` y reenvía (ordinales 100-104 y 108 para la tecla Guide); mismo `OpenSteamToolLoad` (`:123-139`) | 1 |
| `Utils/SteamMetadata/RemoteToml.cpp`, `.h` | 187 | SHA-256 del DLL → espejos en orden (raw `madoiscool/steam-monitor`, jsDelivr, `git.lua.tools`; `[remote] url_template` **sustituye** la lista, `:58-76`) → si alguno da 200, escribe la caché `<Steam>\opensteamtool\<canal>\<componente>\<sha>.toml` y la usa; solo si todos fallan, lee la caché (`:111-178`). Sin firma, sin escritura atómica | 1 |
| `Utils/SteamMetadata/PatternLoader.cpp`, `.h` | 291 | TOML `[0x<fnv1a>] name/rva/sig`; `FindPattern`: **prioridad 1 el RVA, sin comprobar bytes** (`:237-243`), prioridad 2 escaneo de la firma, primera coincidencia (`:78-98,246-256`); módulo sin TOML → aviso y todos sus hooks apagados (`:198-203`); funciones sin entrada → segundo aviso con la lista (`:271-289`) | 1 |
| `Utils/SteamMetadata/IPCLoader.cpp`, `.h` | 215 | canal `ipc/steamclient/<sha>.toml`: interfaces (`vtable_rva`) y métodos (`funcHash`, `fencepost`, `argc`); sin él, aviso y la capa IPC apagada (`:139-166`) | 1 |
| `Utils/SteamMetadata/SteamDiagnostics.cpp` | 106 | SHA-256 de ficheros, avisos en hilo aparte | 1 |
| `Hook/HookMacros.h` | 96 | `HOOK_FUNC`, `INSTALL_HOOK` (= `FindPattern` + `Detour::Attach`), `RESOLVE` | 1 |
| `Utils/HookSupport/VehCommon.cpp`, `.h`; `OSTPlatform/Windows/Trap.cpp` | 89, 171, 161 | trampas `int3` con VEH: capturas de `this` de un solo uso (`ARM_CAPTURE`) y `SpawnProcess` persistente | 1 |
| `Hook/HookManager.cpp` | 49 | orden: SteamUI; steamclient = CallBack, Decryption, IPC, (KeyValues comentado, `:30`), Manifest, Misc, NetPacket, Package | 1 |
| `OSTPlatform/Windows/*` | ~2.300 | Detours, `ByteSearch` (solo lo usa `ProtectionScan`, en fichero), HTTP por WinHTTP (UA `OpenSteamTool/1.0`, TLS validado, `Http.cpp:94,127`), PE, procesos, inyección remota, `DirectoryWatch`, hash, registro | 1, 6 |
| `Steam/NetPacket.h`, `Structs.h`, `Enums.h`, `proto/steam_messages.proto` | 101, 435, 1.781, 519 | layouts de `CNetPacket` (`stable` +0x08, `beta` +0x10, `:42`) detectados en vivo; estructuras del cliente | 1 |

### 1.2 Configuración y Lua

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `Utils/Config/Config.cpp`, `.h` | 310, 99 | `opensteamtool.toml` (§3.4); defaults en código: proveedor `opensteamtool`, `stats` on, `update` on, **`donate` on**, `cloud` off; recarga atómica bajo mutex, salvo `Config::injectDlls`, un `inline` global copiado sin lock (`Config.h:93`, `Config.cpp:60`) | 4, 11 |
| `Utils/Config/LuaConfig.cpp`, `.h` | 969, 68 | un `lua_State` con **`luaL_openlibs`** (`:498`), `_G` con búsqueda sin mayúsculas (`:167-192`), 12 funciones (`:513-526`, §3.4); ejecución línea a línea acumulando hasta que el trozo compila (`:831-852`); tablas `DepotKeySet`, tokens, tickets, overrides por fichero con contador de referencias; `UnloadFile` solo retira depots y pins (`:728-760`); **ningún mutex en todo el fichero** | 2, 4, 9 |
| `Utils/Config/LuaFileWatcher.cpp` | 221 | vigila las carpetas (500 ms de calma), re-parsea, `NotifyLicenseChanged`, `CloudRedirectHost::SyncAppSet` (`:114-119`) | 9 |
| `Utils/Config/ConfigFileWatcher.cpp` | 209 | recarga del `.toml`; `manifest_probe.txt` de diagnóstico: un depot por línea → petición de código originada (`:66-110`) | 4, 11 |

### 1.3 Hooks del cliente

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `Hook/Hooks_Package.cpp`, `.h` | 330, 46 | package 0 (token `10660652434190618804`) capturado por `GetPackageInfo`; `AppIdVec` crece con **todos** los ids del `.lua` si el package está `Available` (`:53-78`); `MarkLicenseAsChanged(0)` + `ProcessPendingLicenseUpdates`; `CheckAppOwnership` (`:193-218`); `DumpOwnedDepots` desde la lista de licencias (`:125-190`); `NotifyLicenseChanged` en caliente (`:262-329`) | 2, 9 |
| `Hook/Hooks_Decryption.cpp` | 83 | `ConfigStoreGetBinary`: `\DecryptionKey` → clave del `.lua` (`:10-36`); lectura de `apptickets\<app>` | 4, 6 |
| `Hook/Hooks_Manifest.cpp`, `.h` | 270, 28 | `BuildDepotDependency`: guarda contra configuración de depots vacía (`:164-172`), registro de los gid de Steam, pin (`:178-195`); `YldLoadDepotManifest`: precarga del archivo (`:224-238`); aviso agrupado "manifests not ready" (`:70-110`) | 4, 5 |
| `Hook/Hooks_NetPacket.cpp`, `.h` | 2.161, 29 | `BBuildAndAsyncSendFrame` + `RecvPkt`: tokens PICS, stats, tickets 858, licencias 780, petición de códigos (originada y respondida), presencia, Spacewar, Cloud, clave legacy, family sharing (§2) | 2, 4, 6, 7, 8 |
| `Hook/Hooks_IPC.cpp`, `Hooks_IPC_ISteamUser.cpp`, `Hooks_IPC_ISteamUtils.cpp`, `PendingAPICalls.cpp` | 183, 203, 106, 34 | `IPCProcessMessage`: post-handlers de `GetSteamID`, `GetAppOwnershipTicketExtendedData`, `RequestEncryptedAppTicket`, `GetEncryptedAppTicket`, `GetAppID`, `GetAPICallResult`; solo si el pipe es de un juego del `.lua` (`Hooks_IPC.cpp:78`) | 6 |
| `Hook/Hooks_Misc.cpp`, `.h` | 218, 39 | `SpawnProcess` (int3): `-onlinefix` → 480 (`:31-50`); `OptedInMask`, `BuildSpawnEnvBlock`; `GetOrAddAppData` **comentado** (`:123`) | 6 |
| `Hook/Hooks_SteamUI.cpp`, `.h` | 154, 23 | `FillInAppOverview` (fecha de compra, descargas activas), `BuildCompleteAppOverviewChange`, `CSteamUIAppControllerRunFrame` (bajas de biblioteca) | 9 |
| `Hook/Hooks_CallBack.cpp` | 29 | `SendCallbackToPipe`: hook instalado que no hace nada (`:6-14`) | — |
| `Hook/Hooks_KeyValues.cpp` | 59 | `ReadAsBinary` sin efecto; instalación comentada: **muerto** | — |

### 1.4 Servicios, tickets, procesos de juego

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `Utils/SteamMetadata/ManifestClient.cpp` | 161 | código de manifest: `fetch_manifest_code_ex` → `fetch_manifest_code` del `.lua` → **un** proveedor (`opensteamtool` con ruta app/depot/gid, `wudrm` por `http://`, `steamrun`) (`:66-73,138-160`), con un mutex global retenido durante la petición HTTP (`:141`) | 4 |
| `Utils/SteamMetadata/ManifestCache.cpp` | 207 | `manifest.luastools.xyz/m/<depot>/<gid>` → `<Steam>\depotcache\<depot>_<gid>.manifest` (rename atómico); validación **solo por magia de cabecera y de cola** (`:56-57,84-93`); caché negativa de 404 durante 10 min | 4 |
| `Utils/SteamMetadata/ManifestDonor.cpp` | 325 | donante: lista `manifestwanted`, `HEAD` por cada depot poseído, acuñado con la sesión del usuario, `POST /manifestcode/submit` (§2.4) | 4, 12 |
| `Utils/SteamMetadata/StatsClient.cpp` | 74 | `stats.opensteamtool.com/<appid>` → SteamID de donante | 7 |
| `Utils/Tickets/AppTicket.cpp` | 175 | ticket de propiedad del registro o **forjado** con el de la app 7 (`:42-62`); SteamID a suplantar | 6 |
| `Utils/Tickets/EticketClient.cpp` | 243 | acuñado por backend: POST `{app_id, nonce, existing_steam_id}`; URL de `seteticketurl()` o de compilación (vacía en el release) | 6 |
| `Utils/Tickets/LegacyCDKey.cpp` | 52 | clave de producto legacy: del `.lua` o sintética determinista | 6 |
| `Utils/Tokeer/TokeerBridge.cpp`, `TokeerUri.cpp` | 209 | `bst://redeem/<código>` → `luastools.xyz/drm/redeem` → tickets al registro | 6 |
| `Utils/CloudRedirect/CloudRedirectHost.cpp`, `.h` | 221, 52 | carga `cloud_redirect.dll` y le pasa las RPC `Cloud.*` y los logros | 7, 8 |
| `Utils/Update/AppUpdater.cpp` | 181 | auto-update desde la rama `updates` (§2.11) | 11 |
| `Utils/SteamMetadata/Mirror.cpp` | 55 | espejos de `updates`: raw, jsDelivr, `git.lua.tools` | 11 |
| `Pipe/PipeManager.cpp`, `ProcessInspector.cpp`, `Features/DenuvoAuth/*`, `Features/Injection/*` | 157, 94, 715, 85 | handshake de cada pipe → proceso, appid (entorno, pipe con 10×20 ms de reintento, `addprocess`), Denuvo (`ProtectionScan`), inyección `[[inject]]` | 6 |
| `OSTPlatform/Windows/SteamCredentialStore.cpp` | 257 | `HKCU\Software\Valve\Steam\Apps\<app>\{SteamID,AppTicket,ETicket}`, `…\ActiveProcess\{ActiveUser,Universe}` | 6 |
| `tools/extract_tickets/extract_tickets.cpp` | 453 | herramienta aparte: en la máquina del dueño, carga `steamclient64.dll` y escribe `appticket.bin`/`eticket.bin` | 6 |
| `tools/ipc_codegen/ipc_codegen.cpp` | 1.197 | genera `IPCMessages.gen.h` desde las specs | 1 |
| `src/cmake/*.cmake` | — | Detours (`main`), Lua 5.5.0 de `lua.org` **con `TLS_VERIFY OFF`** (`Lua.cmake`), protobuf `v3.15.3`, spdlog `v1.17.0`, toml++ `v3.4.0`; logs compilados solo en Debug (`src/CMakeLists.txt:175-178`) | 12 |

---

## §2 Las doce funciones

Cada función contesta la lista de control de `ecosystem-matrix.md` §1, con
`fichero:línea` de `madoiscool/BetterSteamTools@4747385` (rutas relativas a
`src/`).

### 2.1 Engancharse a Steam

**Cómo entra.** Secuestro de DLL por carpeta: `dwmapi.dll` y `xinput1_4.dll`
junto a `steam.exe`. Los dos hacen `LoadLibraryA("OpenSteamTool.dll")` **dentro
de su `DllMain`**, con nombre relativo y solo si el ejecutable se llama
`steam.exe` (`dwmapi/dwmapi.cpp:115-140`, `xinput1_4/xinput1_4.cpp:123-139`); si
la carga falla, el proxy devuelve `FALSE` y el proceso pierde ese DLL. La DLL
principal repite el filtro (`dllmain.cpp:139-158`) y arranca un hilo. La ruta de
Steam es el **directorio actual** del proceso (`dllmain.cpp:25`), no la del
ejecutable. Engancha el `steamclient64.dll` original, el mismo que Steam carga.

**Cómo localiza lo que parchea.** Una TOML por SHA-256 del DLL en disco y por
canal: `pattern/steamclient`, `pattern/steamui` (funciones por FNV-1a del nombre,
con `name`, `rva`, `sig`) e `ipc/steamclient` (métodos IPC). El orden es **red
primero**: espejo 1, 2 y 3, y solo si los tres fallan, la copia local de ese mismo
SHA (`Utils/SteamMetadata/RemoteToml.cpp:111-178`); cada arranque pide las tres
TOML aunque la caché las tenga. Con la entrada en mano, `FindPattern` usa el
`rva` si existe **sin comparar los bytes** y solo si no hay `rva` escanea la
firma (`Utils/SteamMetadata/PatternLoader.cpp:237-256`). En el feed de hoy todas
las entradas traen `rva` (§5.2), así que la firma no se usa nunca. Ni la TOML ni
el binario del auto-update están firmados. En total: 13 detours vivos (10 en
`steamclient64`, 3 en `steamui`; `GetOrAddAppData` está comentado y
`ReadAsBinary` pertenece al módulo KeyValues, que no se instala), 5 capturas
`int3` de un solo uso, la trampa persistente de `SpawnProcess` y 8 funciones
resueltas sin hook.

**Qué pasa cuando Steam actualiza.** El SHA cambia; si el feed ya tiene la TOML,
se instala sola en ese arranque. Si no, aviso "Unsupported Steam Version" con el
SHA y la ruta donde dejar una TOML a mano, y **todos** los hooks de ese módulo
apagados (`PatternLoader.cpp:147-164,198-203`); sin specs IPC, la capa IPC
apagada (`IPCLoader.cpp:139-166`). Una función ausente de una TOML presente se
apaga sola y se lista en un segundo aviso (`:271-289`). Con un RVA viejo en una
TOML válida el hook se pondría en mitad de otra función: nada lo impide. El
cambio de layout de `CNetPacket` de la beta (`+8` bytes) se resuelve en vivo:
`RecvPkt` no toca ningún campo hasta identificar el layout en un paquete real
(`Hook/Hooks_NetPacket.cpp:2066-2083`, `Steam/NetPacket.h:42`), y mientras tanto
las rutas que contestan en local (Cloud, clave legacy) dejan pasar la petición.

**Cómo se recupera.** No hay kill-switch por hook, ni gate de hash, ni detector
de crash. La salida es quitar los proxies. Los logs solo existen en el build
Debug (`src/CMakeLists.txt:175-178`): el zip Release no escribe nada, solo
muestra avisos.

**A qué procesos se limita.** `steam.exe`. La capa IPC actúa solo sobre pipes
cuyo appid está en el `.lua` (`Hook/Hooks_IPC.cpp:78`). La inyección
`[[inject]]` sí entra en procesos de juego (§2.6).

### 2.2 Propiedad y licencias

**Spoof de propiedad.** Dos capas. El package 0 (capturado con el token
`10660652434190618804`) recibe en su `AppIdVec` **todos** los ids del `.lua`
(depots, apps y DLC mezclados) la primera vez que Steam pregunta por una
propiedad y el package está `Available`; después `MarkLicenseAsChanged(0)` +
`ProcessPendingLicenseUpdates` (`Hook/Hooks_Package.cpp:27-92`). Y
`CheckAppOwnership` contesta dueño para todo id con `HasDepot`: `PackageId 0`,
`Released`, `bOwnsLicense = true` ("This forces DLCs on steam family shared
games"), `bFreeLicense = false` para que no se esconda en la biblioteca
(`:193-218`).

**Juegos que la cuenta posee.** Si Steam ya contesta dueño y la app está en más
de un package, se marca como poseída (`MarkOwned`) y se deja la respuesta real
(`:204-207`). Las rutas de código de manifest, tickets y SteamID miran esa marca
para no tocar lo poseído; para los depots, como `MarkOwned` guarda appids, se
cruza con el mapa depot→app que registra `BuildDepotDependency`
(`Hook/Hooks_NetPacket.cpp:1031-1044`, "680 of them on one test machine").

**Licencias.** La lista `CMsgClientLicenseList` (780) se lee sin modificarla
para saber qué depots posee la cuenta (`Hooks_NetPacket.cpp:706-733`;
`Hooks_Package.cpp:95-190`): es la entrada del donante (§2.4).

**Family sharing.** Vacía la respuesta `FamilyGroupsClient.NotifyRunningApps#1`
y el mensaje `SharedLibraryStopPlaying` (9406) (`Hooks_NetPacket.cpp:1928,1981`).
El `README.md:55` exige que **todas** las cuentas de la familia lleven OST.

**Depots compartidos.** Sin trato especial: `HasDepot` decide por id.

### 2.3 DLC

Criterio: todo id que el `.lua` declare con `addappid` es dueño, DLC incluido,
sin límite de cantidad (el `AppIdVec` crece con `CUtlMemoryGrow`). DLC sin depot:
basta el id. DLC comprado: la regla de `ExistInPackageNums > 1` de §2.2. Se
quita borrando la línea o el fichero (§2.9).

### 2.4 Claves y manifests

**Claves.** Solo del `.lua`: `addappid(id, _, "<64 hex>")` (las de otra longitud
se descartan, `Utils/Config/LuaConfig.cpp:213-221`), servidas al pedir Steam
`…\<depot>\DecryptionKey` por `ConfigStoreGetBinary`
(`Hook/Hooks_Decryption.cpp:10-36`). Un `KeyName` nulo o con un segmento no
numérico antes de `\DecryptionKey` llega a `std::string(nullptr)` y a
`std::stoul`, que lanza (`:16,22`); en Debug la clave se escribe entera en el
log (`:25`).

**Pin.** `setmanifestid(depot, gid[, size])` reescribe el gid (y el tamaño si no
es 0) en el vector de depots de `BuildDepotDependency`
(`Hook/Hooks_Manifest.cpp:178-195`).

**Manifests: el archivo.** Antes de que Steam mire su disco
(`YldLoadDepotManifest`, el "acquire" de `CDepotDownloadMgr`), para todo depot
del `.lua` la DLL descarga `https://manifest.luastools.xyz/m/<depot>/<gid>` con
5 s de tope y lo deja en `<Steam>\depotcache\<depot>_<gid>.manifest` por
`.tmp` + rename (`Hooks_Manifest.cpp:224-238`;
`Utils/SteamMetadata/ManifestCache.cpp:95-127,131-205`). Si está, Steam descarga
"on the FIRST attempt with no request code" (`Hooks_Manifest.cpp:213-222`).
Además, al ver salir una petición de código, lanza la misma descarga en un hilo
con el gid exacto (`Hooks_NetPacket.cpp:1059-1083`). La única validación es la
magia de cabecera `D0 17 F6 71` y de cola `AB 15 C4 32`
(`ManifestCache.cpp:56-57,84-93`): ni tamaño, ni depot, ni gid, ni firma. Un 404
queda en caché negativa 10 min, salvo con una descarga activa en curso; los
404 de una descarga real se agrupan en un aviso "Missing N manifest(s) … not
archived yet" (`Hooks_Manifest.cpp:70-110`). El host está escrito en el código,
sin opción (`ManifestCache.cpp:27-30`).

**Manifests: el código de petición.** Para depots del `.lua` no poseídos, la
DLL lanza en paralelo `ManifestClient::FetchManifestRequestCode` y, cuando llega
la respuesta de Valve, **espera hasta 12 s en el hilo de recepción** antes de
reescribirla (`Hooks_NetPacket.cpp:985,1159-1200`). Orden: `fetch_manifest_code_ex`
del `.lua`, `fetch_manifest_code`, y **un solo** proveedor configurado
(`opensteamtool` por defecto, con ruta `/<app>/<depot>/<gid>`; `wudrm` en
`http://` y `steamrun` solo por gid) (`Utils/SteamMetadata/ManifestClient.cpp:66-73,138-160`).
No hay cascada ni comprobación en el CDN: el primer número que vuelva se entrega
a Steam. Un mutex global se retiene durante la petición HTTP (`:141`), así que
los códigos de varios depots se piden en serie. El propio código explica el
cambio de Valve del 2026-09-09: el código "is derived from (depot_id,
manifest_id, time, secret)" y un código de "carrier" gratuito solo vale para
731/571/441 (`ManifestClient.cpp:55-65`).

**Donación de códigos (MRC).** Con `[donate] enabled` (true por defecto en el
código, `Utils/Config/Config.h:52`) un hilo cada 30 s: descarga entera
`manifestwanted` (líneas `app:depot:gid`, hasta 32 MB, cada 300 s, barajada),
para cada entrada **cuyo depot posee la cuenta** hace `HEAD /m/<depot>/<gid>`, y
si falta acuña un código con la sesión del usuario (una petición
`ContentServerDirectory.GetManifestRequestCode#1` originada por la DLL con una
cabecera real copiada y un jobid de rango propio, `Hooks_NetPacket.cpp:759-967`)
y los envía en lote a `POST /manifestcode/submit` como `depot:gid:código`
(`Utils/SteamMetadata/ManifestDonor.cpp:78-259`). Topes: 25 por ciclo, 2 s entre
peticiones, sin tope por sesión. Además, **toda** petición de código que Steam
haga para cualquier depot (no solo del `.lua`) se apunta y, si Valve devuelve un
código bueno, se envía al archivo en un hilo (`Hooks_NetPacket.cpp:1012-1021,1126-1150`;
`ManifestDonor.cpp:285-304`). Nada de esto lleva autenticación.

**Qué deja de funcionar si cae cada servicio.** Sin `manifest.luastools.xyz`:
sin precarga, todo depende del código; sin el proveedor de códigos: sin código
para lo no poseído (salvo un `.lua` con `fetch_manifest_code`); sin ambos, no se
descarga nada no poseído.

### 2.5 Updates de juegos

Pin por `setmanifestid` (§2.4); `pinapp` existe pero no se registra
(`LuaConfig.cpp:520`). No hay pase de updates propio, ni cambio a un build
antiguo, ni congelación por otra vía. Para una update que Steam empieza con un
juego del `.lua`, la DLL **hace fallar** `BuildDepotDependency` si la lista de
depots vuelve vacía durante un refresco de appinfo, para que Steam conserve la
configuración anterior en vez de guardar una vacía y dar el juego por instalado
sin descargar nada (`Hook/Hooks_Manifest.cpp:138-172`; casos 3293260 y 3751260
del 2026-09-20, citados en el comentario).

### 2.6 Fixes y DRM

**SteamStub.** Sin ticket configurado, el ticket de propiedad se **forja** con
el de la app 7 que Steam guarda en ConfigStore, insertando el appid antes de la
firma ("steamdrmp's off-by-four ticket parsing vulnerability",
`Utils/Tickets/AppTicket.cpp:42-62`).

**Denuvo.** Tickets del almacén de credenciales (registro, §3.2), que llegan por
tres caminos: `setappticket`/`seteticket` en el `.lua`, `tools/extract_tickets`
en la máquina de un dueño, o un enlace `bst://redeem/<código>`, que pide los dos
tickets a `luastools.xyz/drm/redeem` y los escribe en el registro para el appid
que diga el servidor, **sin confirmación previa** (`Utils/Tokeer/TokeerBridge.cpp:89-141`;
el esquema se registra en `HKCU\Software\Classes\bst` en cada arranque,
`:183-207`). En el IPC: `GetSteamID` devuelve el SteamID del ticket siempre que
haya uno para esa app (`Hook/Hooks_IPC_ISteamUser.cpp:26-48`); 
`GetAppOwnershipTicketExtendedData` sirve el del registro dentro de la ventana
de autorización (primer pipe del proceso hasta el segundo handshake,
`Pipe/Features/DenuvoAuth/DenuvoAuth.cpp:88-112`) y fuera, registro o forjado
(`Hooks_IPC_ISteamUser.cpp:51-93`); el mensaje de red 858 recibe el mismo ticket
o uno acuñado (`Hooks_NetPacket.cpp:608-678`). Con una URL de backend
(`seteticketurl()` o `-DOST_ETICKET_URL`, vacía en el release:
`Utils/Tickets/EticketClient.cpp:31-33`, valor por defecto vacío, que el workflow de release no sobreescribe), `RequestEncryptedAppTicket` hace un POST
**síncrono en el hilo IPC** con el nonce del juego para acuñar un ETicket
fresco "pinned to the same pool account" (`Hooks_IPC_ISteamUser.cpp:98-148`;
`Utils/Tickets/EticketClient.cpp:123-227`). Detección: secciones heredadas
(`.arch`, `.srdata`, `.xpdata`, `.xdata`, `.xtls`) con la cadena `DENUVO`, patrón
de OEP o entropía de una sección W+X grande; `forcedenuvo(appid)` la salta. Si
la cuenta posee el juego, al cerrar la ventana guarda su SteamID real
(`DenuvoAuth.cpp:118-139`). El `README.md:64` cifra la ventana en 30 min
(`88500005`).

**Clave de producto legacy** (Ubisoft Connect, Rockstar). El mensaje 730 se
contesta en local con la clave de `setlegacycdkey` o una sintética determinista
por (appid, cuenta) y nunca sale a Valve (`Hooks_NetPacket.cpp:1740-1822`;
`Utils/Tickets/LegacyCDKey.cpp:16-51`).

**Spacewar (480).** Con `-onlinefix` en las opciones de lanzamiento, la trampa
de `SpawnProcess` cambia el `CGameID` a 480 (`Hook/Hooks_Misc.cpp:31-50`); Steam
Input y el overlay vuelven al appid real (`:57-86`), `GetAppID` también hasta
que el juego abre `SteamNetworkingSockets`, y desde ahí devuelve 480 para el
certificado P2P salvo con `-realappid` (`:166-178`, Bodycam 2406770); a los
amigos se les envía el nombre real en `game_extra_info`
(`Hooks_NetPacket.cpp:1505-1556`). Un juego a la vez.

**Inyección.** `[[inject]]` mete una DLL en el proceso del juego
(`RemoteProcess::InjectLibrary`) por appid, por subcadena de la línea de
comandos o en todos (`Pipe/Features/Injection/Injection.cpp:51-83`).

Sin Steamless, sin Goldberg, sin EOS.

### 2.7 Logros, stats y tiempo de juego

Para juegos del `.lua`, las peticiones de stats salientes (`Player.GetUserStats#1`
y 818) cambian el SteamID por el de un donante: `setstat(appid, "steamid")`, o
`stats.opensteamtool.com/<appid>` (con `[stats] enable_api`, por defecto), o el
fijo `76561198028121353` (`Utils/Config/LuaConfig.cpp:58,608-615`). La consulta
HTTP se hace dentro del hook de envío (`Hooks_NetPacket.cpp:397,492`), así que la
primera de cada app frena el envío al CM. Las respuestas se marcan `OK` y se
vacían de stats; con CloudRedirect cargado, la 819 recibe los logros que guarda
CloudRedirect (`:505-554`), y cada `StoreUserStats2` se le notifica. Sin
CloudRedirect, los logros se ven con el progreso del donante y no se guarda nada
propio.

### 2.8 Cloud saves

Solo con `[cloud] enabled = true` (off por defecto) y `cloud_redirect.dll` en la
carpeta de Steam: las RPC `Cloud.*` de los juegos del `.lua` se contestan en
local por la DLL de CloudRedirect y **no salen a Valve**; la respuesta se
entrega en el siguiente paquete entrante (`Hooks_NetPacket.cpp:1575-1727,1836-1852`;
`Utils/CloudRedirect/CloudRedirectHost.cpp:88-164`). `SignalAppExitSyncDone` y
`ClientConflictResolution` pasan intactas. Sin CloudRedirect, Steam Cloud se
comporta como para cualquier juego no poseído. El `README.md:110` aún lista la
nube como "Future".

### 2.9 Añadir y quitar un juego

Entrada única: un `.lua` en `<Steam>\config\stplug-in` o en las carpetas de
`[lua] paths` (las configuradas primero, la de por defecto al final, deduplicadas
por ruta canónica, `LuaConfig.cpp:879-911`). La DLL no descarga nada ni escribe
ACF: añade los ids al package 0 en caliente y Steam hace el resto. Al cambiar un
fichero: `UnloadFile` del viejo, ejecución del nuevo, altas y bajas al package 0,
`MarkLicenseAsChanged`, y las bajas a la biblioteca en el hilo de la UI
(`OwnershipFlags = None` + `MarkAppChange`, `Hook/Hooks_SteamUI.cpp:78-108`). La
fecha de compra que muestra la biblioteca es la `mtime` del `.lua`
(`Hooks_SteamUI.cpp:38-48`). Al quitar un fichero solo se retiran depots y pins:
tokens, tickets del registro, SteamIDs de stats y claves legacy siguen hasta
reiniciar Steam, y los tickets del registro para siempre (`LuaConfig.cpp:728-760`).
El watcher modifica las tablas del `.lua` y el `lua_State` mientras los hilos de
Steam las leen y `fetch_manifest_code` usa el mismo estado, sin ningún lock
(§2.12).

### 2.10 Credenciales y proveedores

No pide ninguna cuenta ni clave. Usa, sin autenticarse: el feed de patrones, el
archivo y el canal de donación de `manifest.luastools.xyz`, el proveedor de
códigos, `stats.opensteamtool.com`, y opcionalmente el backend de ETickets y
`luastools.xyz/drm/redeem`. La cuenta Steam del usuario es la credencial que
**aporta**: con ella se acuñan los códigos donados (§2.4). Sin ninguno de los
servicios funciona la propiedad, las claves del `.lua` y los tickets que ya estén
en el registro.

### 2.11 Mantenimiento propio

**Superficie.** Ninguna UI: un `.toml` recargado en caliente, `MessageBox` para
avisos (feed, manifests no archivados, canje) y `manifest_probe.txt` como
diagnóstico.

**Update de sí mismo.** Activo por defecto (`[update] enabled`; se puede quitar
al compilar con `OST_ENABLE_UPDATER=OFF`, `4747385`). En un hilo: descarga
`opensteamtool/latest.toml` de la rama `updates` por raw → jsDelivr →
`git.lua.tools` (`Utils/SteamMetadata/Mirror.cpp:16-20`); si la versión es
**distinta** de la que corre (no compara mayor/menor), baja el DLL de la ruta
que diga el puntero, exige 200 KB-8 MB, `MZ` y que su SHA-256 coincida con el
que trae **el mismo puntero** (`Utils/Update/AppUpdater.cpp:52-117`), renombra el
DLL en uso a `.old` y escribe el nuevo; pregunta si reiniciar y lo hace con un
PowerShell oculto `steam.exe -shutdown` + relanzar (`:157-179`). Quien controle
cualquiera de los tres espejos controla el DLL que se instala.

**Componentes.** CloudRedirect y los DLL de `[[inject]]` los pone el usuario; la
DLL no los versiona.

**Salud.** Solo avisos y, en Debug, logs por módulo en `<Steam>\opensteamtool\`.

### 2.12 Proyecto

Windows. GPL-3.0. Un mantenedor activo (mendy-tools); 18 commits sobre OST entre
el 2026-08-11 y el 2026-09-21, ninguno desde entonces, 17 ramas laterales sin
mover desde mayo-junio salvo `beta-client-support` (= `main`). Releases por
`workflow_dispatch` que publican el DLL en la rama huérfana `updates`, purgan
jsDelivr y crean un tag con zip Release y Debug.

Servicios de terceros: `madoiscool/steam-monitor` (y su repositorio oculto),
jsDelivr, `git.lua.tools`, `manifest.luastools.xyz`, `luastools.xyz`,
`manifest.opensteamtool.com`, `stats.opensteamtool.com`, `gmrc.wudrm.com`,
`manifest.steam.run`.

**Privacidad y riesgo.** Lo que sale de la máquina sin que el usuario haga nada
(con los defaults): el SHA de su `steamclient64.dll` y `steamui.dll` en cada
arranque; el appid de cada juego del `.lua` cuyas stats se piden; con la
donación, un `HEAD` por cada depot **poseído** que esté en la lista de
pendientes (el archivo ve, desde la IP del usuario, qué depots de su biblioteca
coinciden con la lista), un código por cada depot que el usuario descarga
(posea el juego o no) y peticiones de código originadas con su cuenta, fuera de
cualquier descarga, a un ritmo de hasta 25 cada 30 s. El README dice "Only
depots the archive asks for and this account owns are ever mentioned"; la
captura pasiva no pasa por ese filtro. El riesgo de detección de esas peticiones
lo asume la cuenta del usuario.

**Concurrencia.** `LuaConfig.cpp` no tiene ningún mutex: `DepotKeySet`, los
pins, `g_pendingAdditions` y el `lua_State` se modifican desde el watcher
mientras `CheckAppOwnership`, `RecvPkt`, el IPC y el hilo de códigos los leen o
ejecutan funciones Lua. Una recarga de `.lua` con Steam descargando es una
carrera.

---

## §3 Superficies externas

### 3.1 Hosts y URLs

| URL / host | Fichero:línea | Uso |
|---|---|---|
| `https://raw.githubusercontent.com/madoiscool/steam-monitor/{pattern,ipc}/{steamclient,steamui}/<sha>.toml` | `Utils/SteamMetadata/RemoteToml.cpp:20` | patrones y specs IPC |
| `https://cdn.jsdelivr.net/gh/madoiscool/steam-monitor@…`, `https://git.lua.tools/luatools/steam-monitor/raw/branch/…` | `RemoteToml.cpp:21-22` | espejos |
| `[remote] url_template` (lista; sustituye a los tres) | `RemoteToml.cpp:58-76` | espejo de usuario |
| `https://raw.githubusercontent.com/madoiscool/BetterSteamTools/updates/…`, jsDelivr `@updates`, `git.lua.tools/luatools/BetterSteamTools/raw/branch/updates/…` | `Utils/SteamMetadata/Mirror.cpp:16-20` | auto-update |
| `https://manifest.luastools.xyz/m/<depot>/<gid>` (GET y HEAD), `/manifestwanted`, `/manifestcode/submit` (POST `text/plain`) | `ManifestCache.cpp:30,169`; `ManifestDonor.cpp:31,85,116,136` | archivo y donación (`[donate] url` cambia la base del donante, no la del archivo) |
| `https://manifest.opensteamtool.com/<gid>` y `/<app>/<depot>/<gid>`, `http://gmrc.wudrm.com/manifest/<gid>`, `https://manifest.steam.run/api/manifest/<gid>` | `ManifestClient.cpp:66-73` | códigos de manifest |
| `https://stats.opensteamtool.com/<appid>` | `StatsClient.cpp:48` | SteamID de donante |
| `https://luastools.xyz/drm/redeem` (POST JSON) | `Utils/Tokeer/TokeerBridge.cpp:22,92` | canje `bst://` |
| URL de `seteticketurl()` / `OST_ETICKET_URL` (POST JSON) | `Utils/Tickets/EticketClient.cpp:31-39,159-163` | ETickets acuñados |
| `http_get`/`http_post` desde el `.lua` (cualquier URL) | `LuaConfig.cpp:131-162` | `fetch_manifest_code` y lo que quiera el `.lua` |
| Steam CM (mensajes del propio cliente, reescritos u originados) | `Hook/Hooks_NetPacket.cpp` | §2 |
| build: `github.com/microsoft/Detours` (`main`), `www.lua.org/ftp/lua-5.5.0.tar.gz` (`TLS_VERIFY OFF`), protobuf `v3.15.3`, spdlog `v1.17.0`, toml++ `v3.4.0` | `src/cmake/*.cmake` | dependencias |

### 3.2 Ficheros y registro

| Ruta | Quién | Para qué |
|---|---|---|
| `<Steam>\{dwmapi,xinput1_4,OpenSteamTool}.dll`, `OpenSteamTool.dll.old` | el usuario o LuaTools; el updater renombra y escribe | carga, update |
| `<Steam>\opensteamtool.toml`, `<Steam>\manifest_probe.txt` | lee | config, diagnóstico |
| `<Steam>\config\stplug-in\*.lua` (+ `[lua] paths`) | lee, vigila, crea la carpeta | entrada |
| `<Steam>\opensteamtool\{pattern,ipc}\<componente>\<sha>.toml` | escribe (caché) | patrones |
| `<Steam>\opensteamtool\*.log` (Debug) | escribe | logs |
| `<Steam>\depotcache\<depot>_<gid>.manifest` (+ `.<tid>.tmp`) | escribe | precarga |
| `<Steam>\cloud_redirect.dll` (o `[cloud] library`) | carga | nube |
| DLL de `[[inject]]` | inyecta en el juego | inyección |
| `HKCU\Software\Valve\Steam\Apps\<app>\{SteamID,AppTicket,ETicket}` | lee y escribe | tickets e identidad |
| `HKCU\Software\Valve\Steam\ActiveProcess\{ActiveUser,Universe}` | lee | cuenta activa |
| `HKCU\Software\Classes\bst\…` (`rundll32 "<Steam>\OpenSteamTool.dll",TokeerUri "%1"`) | escribe en cada arranque | esquema `bst://` |
| ConfigStore: `…\<depot>\DecryptionKey`, `apptickets\<app>` | lee | claves, ticket de la app 7 |

### 3.3 Variables de entorno

Ninguna propia. Lee del proceso de juego `SteamAppId` y similares para decidir el
appid de un pipe (`Pipe/ProcessInspector.cpp`). Opciones de lanzamiento que
reconoce: `-onlinefix`, `-realappid`, y las de `[[inject]] when_cmdline`.

### 3.4 Claves de configuración (`opensteamtool.toml`) y del `.lua`

| Clave | Defecto | Efecto | Fichero:línea |
|---|---|---|---|
| `[log] level` | `debug` | nivel (solo Debug) | `Config.cpp:16,118-127` |
| `[manifest] url` | `opensteamtool` | proveedor de códigos (uno) | `Config.cpp:14,103-105` |
| `[manifest] timeout_{resolve,connect,send,recv}_ms` | 5000/5000/10000/10000 | tiempos HTTP | `Config.cpp:107-114` |
| `[lua] paths` | `[]` | carpetas extra | `Config.cpp:129-138` |
| `[remote] url_template` | — | sustituye los espejos del feed | `Config.cpp:140-151` |
| `[stats] enable_api` | `true` | consulta a `stats.opensteamtool.com` | `Config.cpp:153-158` |
| `[update] enabled` | `true` | auto-update (si se compiló) | `Config.cpp:160-165` |
| `[donate] enabled`, `url`, `interval_secs`, `max_mints_per_cycle`, `min_mint_interval_ms`, `max_mints_per_session`, `wanted_refresh_secs` | `true`, —, 30, 25, 2000, 0 (sin tope), 300 | donación y captura | `Config.h:51-58`; `Config.cpp:167-189` |
| `[[inject]] path`, `when_cmdline`, `when_appids`, `all_games` | — | inyección en juegos | `Config.cpp:191-217` |
| `[cloud] enabled`, `library` | `false`, `cloud_redirect.dll` | CloudRedirect | `Config.cpp:219-224` |
| `.lua`: `addappid(id[, _, clave64])`, `addtoken(app, "token")`, `setmanifestid(depot, gid[, size])`, `setlegacycdkey(app, "clave")`, `addprocess(app, "exe")`, `forcedenuvo(app)`, `seteticketurl("url")`, `setappticket(app, "hex")`, `seteticket(app, "hex")`, `setstat(app, "steamid")`, `http_get`, `http_post`; funciones globales `fetch_manifest_code(gid)` y `fetch_manifest_code_ex(app, depot, gid)` | — | nombres sin distinguir mayúsculas; `setappticket`/`seteticket` escriben el registro al parsear; `pinapp` no se registra | `LuaConfig.cpp:195-489,513-526,634-725` |

---

## §4 Lo que nos afecta

### 4.1 Hallazgos pendientes de decisión

Aparcados el 2026-10-09. Numeración propia de este doc.

1. **El archivo que usamos lo valida BST solo por la magia.** LumaDeck pide a
   `manifest.luastools.xyz` el paso 3 de su cadena (`nosotros.md` §2.4) y lo
   pasa por `validate_manifest`; BST escribe en `depotcache` todo lo que tenga
   las dos magias (§2.4). Es el motivo para no aflojar nunca nuestra
   validación de identidad. Sin acción.
2. **Nuestros 404 alimentan la lista de pendientes.** Según el wiki de LuaTools
   (§5.2, 2026-09-13) un 404 en `/m/<depot>/<gid>` pone esa versión en
   `manifestwanted`; los donantes de BST la acuñan con sus cuentas. No donamos
   nada (rechazado el 2026-09-28 en `slsteam-moon.md`), pero cada fallo nuestro
   es una petición. Dato, sin acción.
3. **El archivo es un solo operador y un solo host.** `manifest.luastools.xyz`
   está escrito en el código de BST, sin espejo ni opción; si cambia, BST
   necesita release y LumaDeck pierde su paso 3. Sin acción: nuestra cadena
   de manifests tiene más eslabones (`nosotros.md` §2.4).
4. **El feed de patrones copia un repositorio que no se ve.** Si queremos
   usar `madoiscool/steam-monitor` como señal de beta de escritorio (lo es:
   mediana 52 min tras cada build, §5.2), hay que contar con que su origen es
   privado y se copia con `push` forzado. Sin acción; MigoReleases sigue siendo
   la señal anotada (`lumacore.md` §4.1-7).
5. **Códigos sin comprobar en el CDN, otra vez.** Como LumaCore, BST entrega a
   Steam el primer código que devuelva su único proveedor (§2.4). Dato para la
   fila de la función 4.
6. **Guarda contra configuración de depots vacía.** BST hace fallar
   `BuildDepotDependency` cuando la lista de depots de un juego del `.lua` vuelve
   vacía durante un refresco de appinfo (§2.5). Nosotros tenemos BuildDep
   apagado y el síntoma "Fully Installed sin ficheros" lo cerramos por el
   reconcile (`RESEARCH.md` §18). Si alguna vez reaparece con otra causa, esta
   es la referencia.

### 4.2 Dependencias y qué rompe si cambia

| Si cambia… | Qué nos pasa |
|---|---|
| `manifest.luastools.xyz` (cae, cambia de host, exige credencial) | el paso 3 de `manifests.py` pasa a 404; más carga sobre Hubcap (`nosotros.md` §4.2) |
| la base de donantes de BST (menos usuarios, donación apagada por defecto) | menos cobertura del archivo para builds posteriores al 09-09 |
| el formato del `.lua` que BST acepta (funciones nuevas, grafías) | los `.lua` de LuaTools pueden traer cosas que `steamidra_lite.py` no ve (mismo caso que `lumacore.md` §4.1-1) |
| el resto de la DLL | nada; Windows |

### 4.3 Lo que BST tiene y nosotros no (y al revés)

| Función | BST | Nosotros | Notas |
|---|---|---|---|
| 1 | proxies DLL; TOML por SHA de un feed de terceros, red primero, RVA sin comprobar; layout de `CNetPacket` en vivo; sin gate ni crash guard | wrapper; feed RVA propio + patrones + rescates; guard anti crash-loop | Windows ↔ Linux |
| 2 | package 0 + `CheckAppOwnership` + `MarkLicenseAsChanged`; family sharing por mensajes vaciados | SLSsteam + finder del package 0 + `NotifyLicensesUpdated` | misma idea, otra capa |
| 3 | DLC = ids del `.lua` | `DisabledDLC` por LumaDeck | |
| 4 | claves del `.lua` por ConfigStore; manifests del archivo luastools (solo magia) en `YldLoadDepotManifest`; códigos de un proveedor sin CDN; **donación** | DepotKey; cadena de manifests con validación; GMRC con CDN | §4.1-1, -5; donación rechazada |
| 5 | pin por `BuildDepotDependency`; guarda contra depots vacíos | `ManifestIds` de SLSsteam; `pins.py` | §4.1-6 |
| 6 | SteamStub forjado, tickets del registro, acuñado por backend, canje `bst://`, ruta 480, clave legacy, inyección | fixes por botón en LumaDeck | no portable (§4.4 D0) |
| 7 | SteamID de donante por el cable; logros de CloudRedirect si está | SLSsteam + `sls_achievement_unblock` + CloudRedirect | |
| 8 | CloudRedirect en proceso, RPC contestadas en local | CloudRedirect | |
| 9 | `.lua` en caliente, alta y baja de biblioteca, fecha de compra por `mtime` | `steamidra_lite` + reconcile; solo alta en caliente | baja en caliente aparcada en la matriz |
| 11 | auto-update por defecto, sin firma | LumaDeck actualiza componentes; lumalinux no se reemplaza solo | |

### 4.4 Decisiones ya tomadas (del doc anterior, con fecha) y estado hoy

| # | Decisión | Fecha | Hoy |
|---|---|---|---|
| D0 | Lo que OST/BST hace en la capa de mensajes o IPC (cable, `IPCProcessMessage`, WebSocket) no se porta a lumalinux: SLSsteam es dueño de esa capa | 2026-07-03 | vigente |
| D1 | Feed de patrones por hash del `steamclient` (Finding 1): explorado, implementado y **revertido**; queda solo el gate de hash | 2026-07 | vigente; hoy lumalinux tiene feed de RVAs propio (`nosotros.md` §2.1), sin firma por decisión |
| D2 | Leer el `.lua` en el `.so` para dejar de generar `keys.txt` (Finding 2): rechazado | 2026-07 | vigente |
| D3 | Logros por SteamID de donante: paridad de resultado, nos quedamos con lo nuestro (Finding 3) | 2026-07-03 | vigente (hoy por `sls_achievement_unblock`, `lumacore.md` D5) |
| D4 | Importar ETickets de terceros a la caché de SLSsteam (Finding 4): barato, condicionado a la ventana de Denuvo | 2026-07-03 | aparcado con la fila de SLSsteam de la función 6 en la matriz; la ventana sigue sin medir por nosotros (BST la cifra en 30 min y acuña por nonce, §2.6) |
| D5 | Ruta 480: paridad por `FakeAppIds` de SLSsteam (Finding 5) | 2026-07-03 | vigente; BST además devuelve el appid real a Steam Input, overlay y amigos (§2.6) |
| D6 | `manifest.luastools.xyz/m/<depot>/<gid>` como paso de la cadena de manifests de LumaDeck, antes de Hubcap | 2026-09-12 | **hecho** (LumaDeck `4638a97`; `nosotros.md` §2.4 paso 3) |
| D7 | No donar códigos con nuestras cuentas | 2026-09-12 | vigente (una pila solo-`.lua` no posee licencias; rechazado de nuevo el 2026-09-28 para moon) |
| D8 | espejo jsDelivr para `res/rvas/` y `updates.yaml` (regiones sin `raw.githubusercontent.com`) | 2026-09-22 | aparcado |

---

## §5 Historial

§5.1 recoge lo que se creía de OST/BST y el código desmiente, además de lo
listado al principio, y lo que sigue sin medir. §5.2, las mediciones con fecha.
§5.3, la cronología.

### 5.1 Lo que se creía y ya no es así

Además de lo listado en la cabecera:

- **"Los `.lua` en `<Steam>/config/lua/`".** Desde `88539a9` (08-12) la carpeta
  por defecto es `config/stplug-in`.
- **"Precarga síncrona dentro de `BuildDepotDependency` con 15 s de
  presupuesto".** Así era en `4a97d9d` (09-12); `dec29b7` (09-17) la limitó a
  depots con pin y `0b776c5` (09-20) la quitó.
- **"OST mitiga el feed con el SHA del DLL y jsDelivr como espejo de solo
  lectura".** El SHA solo elige el fichero; nada comprueba lo que trae, el RVA
  se usa sin mirar los bytes y el origen real del feed se copia con `push`
  forzado (§5.2).
- **"Workshop: none".** BST precarga ítems del Workshop en `YldLoadDepotManifest`
  desde `dec29b7`.
- **"Los dos feeds de patrones siguen congelados" (delta del 10-07).** El de
  madoiscool publicó el 09-30, el 10-02 y el 10-06 (y el 10-08); solo el de
  `OpenSteam001` estaba parado.
- **"El wiki: los códigos no te identifican y nunca se guardan".** El código no
  identifica, pero los `HEAD` del donante y la captura pasiva dicen al archivo,
  desde la IP del usuario, qué depots posee y cuáles descarga (§2.12).
- **"`OST_TOKEER_URL` vacío por defecto"** (comentario de `src/CMakeLists.txt`).
  El código pone `https://luastools.xyz` si no se define (`TokeerBridge.cpp:18-23`).
- **"Steam Cloud: Future"** (`README.md:110`). La integración con CloudRedirect
  existe desde PR #153 (julio).
- **"El 858 solo registra el formato".** Ya lo suplanta (cabecera).
- **"ETicket 'fresco' por nonce".** El acuñado se cachea por app para toda la
  sesión de Steam y el nonce solo se usa en el primero (`EticketClient.cpp:117-146`);
  además `g_freshEticket` nunca se borra (`Hooks_IPC_ISteamUser.cpp:23-24,127`).
  Un segundo lanzamiento en la misma sesión recibe el ticket del primer nonce.
- **"Un proveedor, sin failover, sin caché" (2026-07).** Sigue siendo un
  proveedor sin failover ni caché de códigos; lo nuevo es el archivo de
  manifests, que en muchos casos evita pedir código.
- Lo que el doc anterior decía de SLSsteam (`ticket.cpp`, `FakeAppIds`,
  `SubscriptionTimestamps`), de LumaDeck y del fork de aitronz no es de este
  programa: SLSsteam está en `slssteam.md`, nosotros en `nosotros.md`; el fork
  de aitronz se absorbió en OST/BST (PR #153, Denuvo de Tesla697) y no tiene
  rama viva propia que releer.

**Lo que sigue sin medir**:

- Si los tres proveedores de códigos responden hoy.
- Si Steam comprueba la firma de un manifest de `depotcache` que la DLL dejó
  sin validar.
- Qué pasa con un hook puesto en un RVA viejo de una TOML válida.
- Si la carrera del `lua_State` (§2.12) rompe algo en una recarga real.
- Cuántos usuarios donan (el archivo no publica estadísticas).

### 5.2 Pruebas y mediciones con fecha

#### Función 1 — Engancharse a Steam

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-22 | `madoiscool/steam-monitor` `pattern@7bea3a6` vs `OpenSteam001/steam-monitor` `pattern@134c9b1` | los dos feeds | madoiscool al día (betas 1789606022, 1789781627 con `d2d085e7…`, 1790036264; estable 1788652215); OpenSteam001 sin TOML desde 09-06 aunque sus ramas `ipc` y `protobuf` seguían | el fork chino lee OpenSteam001 (`opensteamtool-cn.md`) |
| 2026-09-28 | los mismos | seis días después | madoiscool: 1790121765, 1790380355, 1790534246, 1790545198; OpenSteam001 `pattern` aún en 09-06 (22 días) | |
| 2026-10-09 | `madoiscool/steam-monitor` (`main@59882a1`, `pattern@6fd7961`, `ipc@c814bde`, `protobuf@729e231`) | qué es el repo | `main`: un commit (08-04) con README y `mirror.yml`, que cada 15 s hace `git ls-remote` de `secrets.UPSTREAM_URL` y `push '+refs/heads/*'` (y borra ramas que desaparezcan arriba); el README de `pattern` dice "published by evil-steam-monitor" | el generador no es público |
| 2026-10-09 | `pattern@6fd7961` | contenido | 27 commits (07-28 → 10-08), 32 TOML de `steamclient` y 32 de `steamui`; la última de `steamclient`, 26 entradas, la de `steamui`, 10, **todas con `rva` y `sig`** | la firma nunca se usa (`PatternLoader.cpp:237`) |
| 2026-10-09 | `pattern@6fd7961` vs `src/` | nombres que la DLL resuelve frente a la última TOML | presentes los 29 de `INSTALL_HOOK`/`RESOLVE`/`ARM_*` salvo `FindOrCreateKey` y `ReadAsBinary` (los del módulo KeyValues, muerto); `YldLoadDepotManifest` aparece por primera vez en la TOML del 09-03 | cobertura completa de lo vivo |
| 2026-10-09 | `pattern` (26 commits sin el de relleno) | latencia build de Steam → commit | mínima 7 min, mediana 52 min, máxima 4.337 min (~3 días) | estable 1788652215 (09-05), beta 1791415817 (10-07 23:30 UTC, publicada 10-08 01:22) |
| 2026-05-17 → 09-14 | historial git | Patrones de BST | 05-17 tres commits a mano en `Patterns.h` (estable `1778281814`, beta `1778803745`; 18 + 12 + 3 líneas); 05-25 pasan al feed remoto y **no hay más commits de patrones** | desde entonces todo depende del feed |
| 2026-09-11/14 | red (`madoiscool/steam-monitor`, rama `pattern`) | Cadencia del feed | rama desde el 07-28 con backfill; 15 commits, 4 estables; el estable `1788652215` compilado el 05-09 se publicó el 10-09 (al promocionarse a canal) | sin Linux: solo `steamclient64.dll` y `steamui.dll` |

#### Función 4 — Claves y manifests

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-12 | `manifest.luastools.xyz/m/<depot>/<gid>` | gids públicos del momento de 7 juegos (Vampire Survivors, Backpack Battles, Brotato con DLC 2868390, Balatro, Silksong, R.E.P.O., Black Myth) | **17/17** con 200, sin cuenta ni clave, caché de Cloudflare, `application/octet-stream`; `manifestwanted` vacío | Balatro 4.083 B, idéntico a P-ToyStore; Black Myth 6.288.801 B |
| 2026-09-12 | el mismo | builds publicados tras el 09-09 (40 juegos vía `api.steamcmd.net`) | **8 de 15** depots: Valheim 892970 404/200/200, Rust 252490 200×3, No Man's Sky 275850 200/404, Phasmophobia 739630 200, Borderlands 4 1285190 404×4, Hunt: Showdown 594650 404/404/200/404 | solo pueden venir de códigos acuñados tras el 09-09 por dueños |
| 2026-09-13 | el mismo | re-prueba, 12 juegos / 32 depots, gids actuales | luastools **31/32** (todos los huecos del día anterior cubiertos; el que falta, un `522` de Cloudflare en NMS 275852), P-ToyStore 32/32 | los donantes cerraron ocho huecos en menos de 24 h |
| 2026-09-13 | rama `updates@e4e1152` | DLL v1.0.3 (1.675.264 B) | `strings` con `/manifestwanted`, `/manifestcode/submit`, `{}/m/{}/{}`, `SubmitCapturedCode` y `manifest.opensteamtool.com/%u/%u/%llu` | lo publicado es el código de `4a97d9d` |
| 2026-09-13 | wiki de LuaTools (`mrc-system`) | lo que dice el operador | un MRC "is tied to one exact depot and one exact version", "expires within about 5 minutes", "only an account that actually owns the game can generate one"; un fallo de archivo pone la versión en la lista de pendientes; "This code is the same for everyone so this does not identify you"; opt-out `[donate] enabled = false` ("don't be a leech") | §5.1 sobre la privacidad |

#### Función 11 — Mantenimiento propio

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-12 | rama `updates` | v1.0.2 | publicada a las 04:57 UTC y borrada a las 05:11 (`fd8b476`, `6aa1a82`) | |
| 2026-10-09 | rama `updates@bfa812e` | DLL v1.0.4 | 1.675.264 B, SHA-256 `fe849c7e2b532e4e…` = el de `latest.toml`; sin URL de ETickets incrustada; incluye el updater (`powershell`, `-shutdown`); URLs: las de §3.1 | el release no acuña ETickets salvo `seteticketurl()` en un `.lua` |

### 5.3 Cronología

**OpenSteamTool (`OpenSteam001`).** 57 commits del 2026-04-24 (`92572e5`) al
2026-07-06 (`2a08b0b`, merge del Cloud-RPC de PR #153). Barrido del 2026-07-03
(el primer doc): proxies DLL, package 0, claves por ConfigStore, pin, GMRC por
el cable con un proveedor, stats con donante, tickets (app 7 y replay), ruta
480, CloudRedirect y el feed `OpenSteam001/steam-monitor`. En julio, el fork de
mantenimiento de aitronz (`v1.4.9`, 2026-07-03, 39 commits por delante) añadió el
acuñado de ETickets por nonce, la detección estructural de Denuvo y el
enrutado del appid real en la ruta 480; parte de eso entró en OST y en BST.
`OpenSteam001/steam-monitor` dejó de publicar patrones el 2026-09-06.

**BetterSteamTools (`madoiscool`), 18 commits sobre `2a08b0b`.**

- **2026-08-11 → 08-13.** `2c7af78` renombre a BST; `c3d89e9` clave de
  producto legacy (730/785); `f721ebe` cadena de espejos del feed; `d03f4e4` +
  `49a2d11` auto-update y acción de borrar versión; `88539a9` carpeta
  `stplug-in` por defecto; `c5b2c07`/`5b754d3`; `7936adc` inyección por
  condiciones, OnlineFix aunque se posea el juego, flip a 480 solo con P2P
  (#146); `e55c90b` (Tesla697) Denuvo estructural, seguimiento sin variables de
  entorno, ETickets por backend; `f7b7caf` canje `bst://`. v1.0.0 en `updates`
  (08-12, borrada y republicada el 08-13).
- **2026-09-05.** `c7b435f` `-realappid`. v1.0.1.
- **2026-09-12.** `4a97d9d` donación de códigos, archivo y precarga (1.697
  líneas); `7243c60` `[donate]` en el ejemplo. v1.0.2 publicada y retirada en 14
  minutos; v1.0.3 el 09-13. Ese día LuaTools anuncia BST como su motor.
- **2026-09-17.** `dec29b7`: precarga síncrona solo para depots con pin,
  `YldLoadDepotManifest` nuevo, `[donate]` en el README.
- **2026-09-20.** `0b776c5` guarda contra depots vacíos, sin precarga en
  `BuildDepotDependency`, `CNetPacket` +8 de la beta; `1d15f39` layout de
  `CNetPacket` detectado en vivo con tabla `stable`/`beta`.
- **2026-09-21.** `4747385` opción `OST_ENABLE_UPDATER`; tag y release v1.0.4
  (`updates@bfa812e`). Sin commits desde entonces (barridos del 09-28 y 10-07).
