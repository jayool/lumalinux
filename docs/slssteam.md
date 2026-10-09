# SLSsteam — el unlocker de AceSLS, leído desde el código

Leído desde el código el 2026-10-08. Sustituye a `slssteam-analysis.md`;
sus mediciones con fecha están en §5.2 y su historia por releases en §5.3.
`slssteam-plugins.md` (la API de plugins y los plugins que circulan) sigue
siendo su propio doc. Cómo **nosotros**
configuramos y parcheamos SLSsteam está en `nosotros.md` (§2 por función y
§4.3); aquí se describe SLSsteam tal como es.

---

## §0 Ficha

| | |
|---|---|
| **Qué es** | Una biblioteca de 32 bits (`SLSsteam.so`) que se carga como **auditor del enlazador** (`LD_AUDIT`) en el proceso `steam` de Linux y parchea `steamclient.so` en memoria: 37 hooks (15 detours sobre funciones localizadas por patrón o RTTI y 22 sobre vtables de objetos vivos) que fingen la propiedad de las apps listadas en `config.yaml`, desbloquean DLC, reescriben y descartan mensajes del CM, cachean tickets, suplantan el SteamID y el AppId que ven los juegos, y piden esquemas de logros en nombre de otros usuarios. Trae un API de plugins Lua (LuaJIT sin sandbox), un fichero de comandos en `/tmp`, y dos herramientas .NET (`ticket-grabber`, `schema-grabber`). No descarga juegos, no maneja claves de depot ni manifests externos, y no se actualiza a sí mismo. |
| **Repos y commits leídos** | `AceSLS/SLSsteam` `main@049bbdd` (2026-10-06; último tag `20261001163836` = `42568f0`, 2026-10-01; `res/version.txt` = `20260903114323`). `src/` ≈ 12.000 líneas propias sin los protobufs generados. La release instalada en la Deck es `20261001163836` (`nosotros.md` §0). |
| **Plataforma** | Linux x86 de 32 bits dentro del proceso `steam` (`-m32`): Steam nativo, `steam-native`, Flatpak (`flatpak override` + `SHARED_LIBRARY_GUARD=0`), SteamOS/Deck (hashes `steamdeck_stable` en el feed; `SafeMode` recomendado para Game Mode por el propio `config.yaml:112`). Paquetes: 7z/zip "Any", PKGBUILD Arch (`/usr/lib32/libSLSsteam.so`), flake Nix, Docker para compilar contra glibc de Debian bookworm. |
| **Licencia y quién** | AGPL-3.0-only. Autor y mantenedor único en la práctica "Ace! _SL/S" (`.mailmap`, `acesls@protonmail.com`); Parasitic-Hollow como suplente declarado; aportes puntuales de DeveloperMikey (Nix), exefer, skrimix, dankrr, _drazy/Deadboy666 (hashes). Soporte por Discord, no por issues. Ritmo: 22 tags entre 2026-07-05 y 2026-10-01 (≈ uno cada 4 días), ~400 commits en tres meses; historia reescrita el 2026-07-21 (raíz nueva sin padre, §5.3). |
| **Relación con los demás programas** | Es la capa de propiedad de nuestro stack: lumalinux y LumaDeck la instalan tal cual sale de su release y la configuran (`nosotros.md` §2.2, §4.3). lumalinux copió su `update.cpp` (SafeMode) y parchea en memoria dos `call` de `SLSsteam.so` para los logros (`nosotros.md` §2.7). slsteam-moon es un fork; LumaCore, SteaMidra, SLSDeck, ASSella y LuaTools se apoyan en él o lo instalan; los plugins Lua corren dentro de él; CloudRedirect convive con él para cloud y logros. |

**Lo que cambia respecto al doc anterior**, en una línea cada uno:

- **`isSubscribed` queda cerrado**: `CUser::isSubscribed` llama al **trampolín** de `CheckAppOwnership`, es decir, a Steam sin el spoof, y devuelve `ownsLicense && !licenseExpired` (`sdk/CUser.cpp:34-46`). Para un juego solo añadido a `AdditionalApps` es **false**; para uno con la inyección de package-0 de lumalinux es **true** porque el propio Steam lo cree poseído (medido en la Deck, `nosotros.md` §5.2 F7). Las dos posturas que circulaban eran ciertas en contextos distintos.
- SLSsteam **no tiene almacén de logros**: solo consigue el esquema pidiendo las stats de reseñadores recientes y vacía los valores de la respuesta; quien guarda es Steam (`appcache/stats`). `nosotros.md` §2.7 decía "desde su almacén local"; corregido.
- **No hay límite de 64 DLC en el código**: `DlcData` es la forma de suplir la lista que Steam no entrega; ni `dlc.cpp` ni `config.cpp` imponen ni mencionan 64. El "límite" es de Steam.
- **No inyecta packages ni licencias**: el spoof es por app en `CheckAppOwnership`/`GetSubscribedApps`. La inyección de licencias, depots, claves de depot y el "exploit" de códigos de manifest viven solo en la rama `update`, nunca mergeada (§5.3).
- **`AppLicensesChanged_t` solo en caliente**: en el arranque no se postea nada; solo al añadir (tras la respuesta PICS, lotes de 15) o quitar apps de `AdditionalApps` con Steam en marcha, y solo después de que Steam haya llamado a `GetSubscribedApps`.
- **La API de fichero está apagada por defecto** (`API: no` en el YAML que se instala; el default en código es `true` si falta la clave) y estuvo **rota** entre los tags `20260930144343` y `20261001163836`.
- **`res/version.txt` no ha subido** en los dos últimos tags (sigue en `20260903114323`) y no hay hashes posteriores al cliente del 2026-09-03: con `SafeMode: yes` cualquier `steamclient.so` más nuevo aborta. Nosotros lo llevamos a `no`.
- **La caché offline de SafeMode no funcionó hasta el 2026-09-02** (`eafd3ea`): `path::append("/.updates.yaml")` sustituía la ruta por `/.updates.yaml`.
- **`library-inject.so` es un fichero de 0 bytes** desde `148b9a1` (2026-08-23) y sigue en todos los `LD_AUDIT` upstream; glibc lo ignora con aviso.
- Los hooks son **37** (tabla en §2.1); se hablaba de "los `RunIPCFrame` de 7 interfaces", mecanismo muerto desde 2026-07-24.
- El endpoint de reseñas en `main@049bbdd` sigue siendo `store.steampowered.com/appreviews`; el cambio a `api.steampowered.com/IUserReviewsService` está solo en `dev` (`51724f5`, 2026-10-03).

---

## §1 Mapa del código

Cobertura por fichero con la función de la matriz a la que sirve (números de
§2). Las bibliotecas estáticas de `lib/` (libmem 13 MB, protobuf 19 MB,
yaml-cpp, luajit) y `include/` (LuaBridge ~7k líneas) se anotan sin leer; los
`src/sdk/protobufs/*.pb.*` son generados y solo se lista quién los usa.

### 1.1 Núcleo (`src/`)

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `main.cpp` | 292 | entradas rtld-audit `la_version`/`la_objopen`/`la_preinit`; filtra proceso `steam`; crea log; limpia `LD_AUDIT`; init config y Updater; al tener `steamclient.so`+`steamui.so`+`libtier0_s.so` parsea VFTables y lanza `load()` (gate de hash, `Steam::init`, `VFTIndexes`, `Patterns`, `Hooks`, Lua, API, notificación) | 1, 11 |
| `hooks.hpp/.cpp` | 280+1588 | clases `DetourHook`/`VFTHook`/`LuaHook`; los 37 cuerpos `hk*` que despachan a `feats/`, Lua y API; `Hooks::init`, `placeVFTHooks`, `removeAll` (nadie lo llama) | 1, 2, 3, 5, 7, 8, 10 |
| `patterns.hpp/.cpp` | 84+214 | `Pattern_t` (bytes con `?`, modo de seguimiento, prólogo) y las 18 firmas sobre `.text` de `steamclient.so` | 1 |
| `memhlp.hpp/.cpp` | 97+366 | escaneo de `.text`, `call/jmp` relativo, prólogo hacia arriba (0x10000 bytes), `fixPICThunkCall` para trampolines PIC, nombre RTTI, `callVFunc` | 1 |
| `vftableinfo.hpp/.cpp` | 136+406 | 42 `VFTableInfo_t` (5 con índice fijo, el resto por nombre vía los `IClient*Map`); `VFTIndexes::init/dump` | 1 |
| `decompiler.hpp/.cpp` | 75+656 | parser ELF del módulo en disco, strings de `.rodata`, typeinfos y vtables en `.data.rel.ro`, desensamblado (capstone) de los stubs `*Map` para mapear nombre → índice por xref de `TraceIPC` | 1 |
| `config.hpp/.cpp` | 247+428 | `CConfig`: ~35 `MTVariable` con todas las claves YAML; ruta, creación con el default embebido, inotify + hot reload, lista blanca/negra con herencia por `parent`, diffs de `AdditionalApps` | 2, 3, 4, 5, 6, 7, 8, 9, 11 |
| `config_default.hpp` (≡ `res/config.yaml`) | 176 | config por defecto embebida; generado en build por `embed-config.sh`, no está en git | 11 |
| `filewatcher.hpp/.cpp` | 40+199 | hilo + inotify sobre el directorio padre filtrando por nombre; `IN_CLOSE_WRITE\|IN_MOVED_TO`; reintenta en `EINTR` | 11 |
| `update.hpp/.cpp` | 22+169 | SafeMode: descarga `res/updates.yaml` (GitHub raw → jsDelivr) con `curl` externo, caché `<config>/.updates.yaml`, SHA-256 de `steamclient.so` contra `SafeModeHashes[VERSION]` | 1, 11, 12 |
| `api.hpp/.cpp` | 63+316 | fichero de comandos `/tmp/SLSsteam.API` vigilado por inotify; 7 comandos; ops encoladas y ejecutadas en el hilo IPC | 9, 11 |
| `process.hpp/.cpp` | 106+725 | `/proc/<pid>/{exe,cmdline,environ,map_files}`; parsers PE/ELF; detección SteamDRM (`.bind` con entropía ≥ 7.0) y Denuvo (`.xtext/.xcode/.xpdata/.xtls` + entropía > 6.25); `.exe` real bajo `wine-preloader`; `g_processMap` por pipe | 1, 6, 10 |
| `lua.hpp/.cpp` | 83+579 | LuaJIT + LuaBridge: `curl`, `log`, `memhlp`, `VFTableInfo_t`, `LuaHook`, `LuaMutex`, `YAMLNode`, `CConfig`, `CNetPacket`, `CSteamEngine`, `CUser`, `IClient*`, `SLS.*`; carga `<config>/plugins/*.lua`; hot reload; permisos 0700 | 11, 12 |
| `log.hpp/.cpp` | 107+403 | `$HOME/.SLSsteam.log` (truncado en cada arranque), bits `LogLevels`, `Once` deduplica, `notify-send` por `system()`; Debug/Trace fuera en release | 11 |
| `utils.hpp/.cpp` | 52+203 | entropía, `exec` (fork+execve), SHA-256 (OpenSSL), `strsplit` (regex), `tryConvertToNumber` | 1, 11 |
| `curl.hpp/.cpp` | 13+48 | `Curl::downloadString`: binario `curl` externo (`/bin`, `/usr/bin`, `/run/current-system/sw/bin`), `--silent --connect-timeout`; motivo: libssl de SteamOS crasheaba | 11, 12 |
| `globals.hpp/.cpp`, `mtvar.hpp` | 12+8+65 | módulos (`g_modSteamClient/UI/Tier0`), `g_currentSteamId`; `MTVariable<T>` (`shared_ptr` + `shared_mutex`) base del hot reload | 1, 11 |

### 1.2 Features y SDK (`src/feats/`, `src/sdk/`)

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `feats/apps.hpp/.cpp` | 42+632 | `unlockApp` (rellena `AppOwnershipInfo_t`), `checkAppOwnership`, `getSubscribedApps`, `buildDepotDependency` (`DepotBlacklist`, `ManifestIds`), `getAppStateInfo` (family lock), `getLegacyCDKey`, `parseProductInfoFromResponse`/`postAppLicensesChanged`/`runIPCFrame` (altas y bajas en caliente), `spawnGame` (`LaunchOptions`), `shouldDisable{Cloud,CDKey,Updates}`, `sendAndRecvLastPlayedTimes`, `sendGamesPlayed`, `sendPICSInfoRequest`, `setConfigStoreString` (`PrivateApps`) | 2, 4, 5, 7, 8, 9, 12 |
| `feats/dlc.hpp/.cpp` | 20+97 | `shouldUnlockDlc` y las respuestas a `IsAppDlcInstalled`, `BIsDlcEnabled`, `IsUserSubscribedAppInTicket`, `GetDLCCount`, `BGetDLCDataByIndex` | 3 |
| `feats/achievements.hpp/.cpp` | 45+273 | esquema de logros para apps no poseídas: reseñadores de la tienda, reintento de `GetUserStats` con sus SteamIDs, limpieza de stats, `preferredOwners`, blacklist, cooldown 10 min | 7 |
| `feats/fakeappid.hpp/.cpp` | 32+270 | ejecutar un juego "como" otro AppId: mapa por pipe, AppId real desde `/proc`, tabla de interfaces IPC que ven el real, serverbrowser, `GamesPlayed`/`RichPresence` salientes | 6, 2 |
| `feats/misc.hpp/.cpp` | 10+109 | `FakeOffline`; `FakeName`/`FakeEmail`/`FakeWalletBalance` sobre paquetes entrantes; borra `parental_settings` del logon (siempre) | 12, 2 |
| `feats/ticket.hpp/.cpp` | 48+312 | caché en disco de `ClientGetAppOwnershipTicketResponse` (858) y `ClientRequestEncryptedAppTicketResponse` (5527) con su SteamID; recarga en `LaunchApp`; ticket cifrado cacheado si el servidor falla; spoofs "de una vez" del SteamID según `SmartTickets` | 6, 2, 10 |
| `sdk/steam.hpp/.cpp` | 15+38 | `dlopen("libtier0_s.so")` y `Plat_Alloc/Plat_Free/Plat_Realloc` | 1 |
| `sdk/types.hpp` | 319 | `AppId_t`, `GAME_TYPE_SHORTCUT`, enums `EAppOwnershipFlags`, `ELicense*`, `EAppReleaseState`, `EResult`, `CSteamId` (layout y relleno `0x0110000100000000`) | 2, 6 |
| `sdk/CSteamEngine.hpp/.cpp` | 120+191 | motor IPC: `CServerPipe` (layout 0x60), `EIPCCmd`, `EIPCInterface` (0x1..0x3d), `getUser` (vector de "2 punteros por entrada"), `getUtils`, `getServerPipe` por patrón | 1, 6 |
| `sdk/CUser.hpp/.cpp` | 82+64 | `AppOwnershipInfo_t` (0x38 bytes), `AppLicensesChanged_t` (0x114, `apps[64]`), ids de callback `0x7d`/`0xf907c`/`0xf90be`; `checkAppOwnership` (trampolín), **`isSubscribed`**, `postCallback` (patrón), `updateAppOwnershipTicket` | 2, 3, 6 |
| `sdk/CNetPacket.hpp/.cpp` | 159+52 | paquete CM (`body@4,size@8,refs@0xC,originalBody@0x10`), reserialización en arena estática 8×1 MB con mutex | 1, 2, 6, 7, 12 |
| `sdk/CProtoBufMsgBase.hpp` | 45 | `type@0x14, header@0x1C, body@0x20`; incluye los 10 `.pb.h` de mensajes | 7, 2 |
| `sdk/CAPIJob.*`, `sdk/CClientUnifiedServiceTransport.*`, `sdk/CCMInterface.*`, `sdk/CWebSocketConnection.*` | 16+9, 12+9, 20+16, 21+16 | envoltorios de los trampolines (RPC legacy, servicios unificados, recepción CM, envío WebSocket) con sus convenciones de llamada | 1, 2, 7 |
| `sdk/CUtl.hpp/.cpp` | 207+48 | `CUtlMemory/Buffer/RBTree/Map/String/Vector` con layouts de tier1; `CUtlVector::swap` con `>` en vez de `>=` | 1, 4 |
| `sdk/IClientAppManager.hpp/.cpp` | 50+51 | `DepotInfo_t` (`depotId@0, appId@4, manifestId@8`), `installApp/uninstallApp/getAppInstallState/getLibraryFolder*` por índice resuelto en runtime | 4, 9 |
| `sdk/IClientApps.hpp/.cpp` | 58+50 | `AppStateInfo_t` (`ownershipFlags@4, ownerAccountId@0xC, realOwner@0x1C`), `getAppData`, `requestAppInfoUpdate` | 2, 9 |
| `sdk/IClientCompat.*`, `IClientFriends.hpp`, `IClientUser.*`, `IClientUtils.*`, `CSteamMatchmakingServers.hpp`, `CValidator.hpp`, `CSteamClient.hpp` | 21+64, 12, 26+35, 11+22, 26, 31, 66 | Proton por app (API), `GamePlayed_t`, `setLegacyCDKey`/`getSteamId`, pipe actual, `gameserverdetails_t.appId@0x90`, árbol de validación, tabla "función N → `*Map`" | 9, 6, 2, 4 |
| `sdk/protobufs/*.pb.{h,cc}` | generados | `EMsg`, `CMsgProtoBufHeader`, `CMsgClientGamesPlayed`, `CMsgClientPICSProductInfoRequest/Response`, `CMsgClientGetUserStats(+Response)`, `CPlayer_GetUserStats_*`, `CPlayer_GetLastPlayedTimes_Response`, `CMsgClientPersonaState`, `CMsgClientEmailAddrInfo`, `CMsgClientWalletInfoUpdate`, `CMsgClientLogonResponse`, `CMsgClientRichPresenceUpload`, tickets 858/5527 | 2, 6, 7, 12 |

### 1.3 Build, instalación, herramientas y docs upstream

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `Makefile` | 161 | `g++ -m32 -O3 -flto=auto -std=c++20 -fopenmp …` (clang "crashes on some hooks"); `-Wl,--no-undefined` + openssl + libcurl (enlazada, no usada); **sin `strip` ni `-fvisibility`**; `DEBUG=1`/`TRACE=1`/`NATIVE=1`; objetos cacheados por SHA de flags; `embed-version.sh` y `embed-config.sh`; targets `audit-libs`, `tools`, `zips`, `install/uninstall` (→ `setup.sh`) | 1, 11, 12 |
| `embed-config.sh`, `embed-version.sh` | 17+24 | generan `src/config_default.hpp` (raw string de `res/config.yaml`) y `src/version.hpp` (`BUILD_BRANCH`, `LAST_COMMIT_HASH`, `VERSION` = `res/version.txt`) | 11 |
| `make-releases.sh` | 57 | construye en Docker (`docker/build.sh zips`) dos variantes, `DEBUG=1` → "debug" y "release", renombra a `SLSsteam-Any-{debug,release}.{7z,zip}`; opcional `makepkg` → `SLSsteam-Arch-*` | 11, 12 |
| `docker/Dockerfile`, `docker/build.sh` | 12+31 | `debian:bookworm` multiarch i386 + dotnet-sdk-9.0; monta el repo en `/src` y ejecuta `make $@` | 1, 12 |
| `setup.sh` | 203 | instalador de usuario: copia `bin/*` a `~/.local/share/SLSsteam`; wrappers en PATH **solo para fish**; `.desktop` parcheados con `env LD_AUDIT=…` (solo `Exec=/…` absolutos); `flatpak-install` con `flatpak override --env=LD_AUDIT=… --env=SHARED_LIBRARY_GUARD=0`; `uninstall` borra una ruta de fish distinta de la que escribe | 1, 11 |
| `res/config.yaml` | 166 | config por defecto (33 claves con comentarios); idéntica al `config.yaml` que el `.so` escribe al usuario | 1-11 |
| `res/updates.yaml` | 59 | `SafeModeHashes: {<VERSION>: [sha256 de steamclient.so #rama - fecha]}`; 13 claves 20251226083318 … 20260903114323; último hash: cliente 2026-09-03 | 1, 11 |
| `res/version.txt` | 1 | `20260903114323` | 11 |
| `res/SLSsteam.desktop`, `res/SLSsteam-native.desktop` | 283+282 | `.desktop` de Arch con `Exec=/usr/bin/env LD_AUDIT="/usr/lib32/libSLS-library-inject.so:/usr/lib32/libSLSsteam.so" /usr/bin/steam %U`; las acciones sin `LD_AUDIT` | 1 |
| `pkg/slssteam/PKGBUILD`, `pkg/slssteam-git/PKGBUILD` | 49+58 | tarball del tag `pkgver=20261001163836` (siempre un tag por detrás); `options=(!strip)` "to not mess up disturb ticket-grabber"; instala `.so`, `.desktop`, icono y `sls-{ticket,schema}-grabber`; `-git` compila la rama `dev` | 1, 7, 10, 12 |
| `flake.nix`, `nix-modules/*` | 53+47+31+220+… | derivación i686, wrapper `SLSsteam` = `steam` con `LD_AUDIT`, módulo home-manager con esquema de config **desfasado** (`AutoFilterList`, `PlayNotOwnedGames`, `Notifications`, `LogLevel`), NuGet lock (SteamKit2 3.3.1) | 1, 11, 12 |
| `tools/library-inject/` | 89+5 | fuente íntegramente comentado (antiguo `la_objsearch` que redirigía `libcurl.so` a rutas de 32 bits); el Makefile hace `rm` + `touch` → **fichero vacío** | 1 |
| `tools/schema-grabber/Program.cs` | 437 | CLI .NET/SteamKit2: login usuario+contraseña (+Steam Guard), pide `ClientGetUserStats` con `steam_id_for_user` de 10 reseñadores y escribe `appcache/stats/UserGameStatsSchema_<app>.bin` + `UserGameStats_<acc>_<app>.bin` vacíos; `./Sentries/<user>.sentry` | 7, 10 |
| `tools/ticket-grabber/Program.cs` | 398 | CLI .NET/SteamKit2: `AppOwnershipTicket` + `EncryptedAppTicket` de un app (o batch de todas las licencias) → `./Tickets/{ticket,encryptedTicket}_<app>.yaml` con `steamId:` + base64 | 2, 6, 10 |
| `docs/API/README.md`, `docs/Lua/README.md`, `docs/Lua/example.lua` | 25+201+126 | única spec del fichero de comandos; spec del API Lua (con huecos: `IClientApps/User/AppManager` sin métodos, typo `configLoaing`); ejemplo que hookea `PostCallback` por patrón y añade 14 appIds a `AdditionalApps` | 11, 9, 2 |
| `README.md`, `LICENSE`, `docs/LICENSE/*` | 60+661+… | portada (sin lista de features: remite a la wiki), créditos, "Hall of Shame", Discord, proyectos relacionados (h3adcr-b, steamnetsock-patch, eos-proxy, CloudRedirect, SteamDB App Parser); AGPL-3.0 + licencias de libmem, yaml-cpp, protobuf, LuaJIT, LuaBridge (plantilla sin rellenar), base64 | 12 |

---

## §2 Las doce funciones

Cada función: qué hace SLSsteam, con `fichero:línea` sobre `main@049bbdd`, y la
lista de control de `ecosystem-matrix.md` §1 contestada. Las mediciones con
fecha están en §5.2 y se citan como "§5 F<n>". Cómo nos afecta y qué usamos
de cada cosa: `nosotros.md` §2.

### 2.1 Engancharse a Steam

**Cómo entra.** `LD_AUDIT` (rtld-audit), nunca `LD_PRELOAD`: `SLSsteam.so`
exporta `la_version` (`main.cpp:240-243`), `la_preinit` (`:289-292`, llama a
`setup()` antes de que el ejecutable empiece) y `la_objopen` (`:245-287`, una
llamada por objeto que el loader mapea). Todos los caminos de instalación
ponen la misma variable, `LD_AUDIT="<dir>/library-inject.so:<dir>/SLSsteam.so"`
(`setup.sh:6`, `res/SLSsteam.desktop:30`, `nix-modules/wrapped.nix:60-61`;
Flatpak antepone `/app/links/$LIB/libshared-library-guard.so` y fija
`SHARED_LIBRARY_GUARD=0`, `setup.sh:10,163`). `library-inject.so` va primero
pero es un fichero de **0 bytes** desde `148b9a1` (`tools/library-inject/Makefile:1-5`:
`la_objsearch` "messes up exceptions in a way that they do not get caught
anymore"); su fuente, que redirigía `libcurl.so*` a copias de 32 bits del
sistema, está íntegramente comentado. No hay ptrace, code cave ni parche de
binario en disco.

**Filtro de proceso.** `setup()` sale si `LM_GetProcess().name != "steam"`
(`main.cpp:95-107`): el auditor se carga también en steamwebhelper, reaper y
cada juego, pero no hace nada allí. Además `cleanEnvVar("LD_AUDIT", …)`
(`:34-70,123-128`) quita del entorno del propio `steam` las entradas que
terminan en `SLSsteam.so`, `library-inject.so`, `libSLSsteam.so` o
`libSLS-library-inject.so`, para que los hijos no lo hereden. `unload()` es un
no-op (todo comentado: desmapear el módulo crasheaba, `:74-88`).

**Secuencia.** `la_preinit` → `setup()`: log en `$HOME/.SLSsteam.log` (sin
`$HOME` no hay log y `setup()` aborta, `:109-116`), manejador SIGINT del
filewatcher, limpieza de `LD_AUDIT`, `g_config.init()` (crea o lee
`config.yaml` y arranca inotify), `LD_LIBRARY_PATH += /usr/lib:/usr/lib32`
(`:141-143`; sin `:` de separación y con `std::string(nullptr)` si la variable
no existe), `Updater::init()` (solo descarga el feed si `SafeMode` o
`WarnHashMissmatch`, `update.cpp:25-30`). Luego `la_objopen`: al ver
`…/steamclient.so` o `…/steamui.so` **parsea el ELF del fichero en disco y
todas sus vtables antes de que se apliquen las relocaciones** (comentario
`main.cpp:251-255`; las vtables aún contienen offsets relativos al módulo,
`decompiler.cpp:59`), y al ver `…/libtier0_s.so` guarda el módulo; `load()`
solo trabaja cuando los tres están (`:157-160`). Dentro de `load()`
(`:180-237`): gate de hash → `Steam::init()` (`dlopen` de `libtier0_s.so` y
`dlsym` de `Plat_Alloc/Plat_Free/Plat_Realloc`, `sdk/steam.cpp:15-37`) →
`VFTIndexes::init()` → `Patterns::init()` → `Hooks::init()` → `Lua::init(true)`
→ `SLSAPI::init()` → notificación "Loaded successfully" (o "Happy birthday
SLSsteam!" el 22 de febrero, `:223-237`). Los hooks por vtable se colocan más
tarde, en el primer `RunInterface` del IPC, cuando ya existe el `CUser`
(`placeVFTHooks`, `hooks.cpp:1471-1573`, llamado desde
`hkSteamEngine_ProcessIPCFrame` `:413-416`).

**Cómo localiza lo que parchea.** Tres mecanismos:

1. **18 patrones de bytes** sobre `.text` de `steamclient.so`
   (`patterns.cpp:51-213`): `TraceIPC`, `CAPIJob::SendAndRecv`,
   `CAppDataCache::BParseResponseMessage`, `CWebSocketConnection::BBuildAndAsyncSendFrame`,
   `CSteamEngine::{GetServerPipe, SetAppIdForCurrentPipe, ProcessIPCFrame, m_ClientUtils, m_pUser}`,
   `CUser::{CheckAppOwnership, GetSubscribedApps, PostCallback, PostCallbackToAppId, SpawnGameId, UpdateAppOwnershipTicket, m_ClientUser, m_UserAppInfo, m_UserAppmanager}`,
   `CUserAppManager::BuildDepotDependency`, `IClientUtils::m_PipeIndex`.
   `patternScan` (`memhlp.cpp:64-120`) recorre `.text` byte a byte usando los
   límites del ELF parseado y se queda con la **última** coincidencia (log si
   hay más de una). Tres modos (`memhlp.hpp:21-26`): `None` (la dirección es
   un operando: los patrones `m_*` leen el inmediato en `+1`/`+2`,
   `sdk/CUser.cpp:15-28`, `sdk/CSteamEngine.cpp:159,180`), `Relative`
   (desensambla el `call`/`jmp` y toma el destino) y `PrologueUpwards`
   (retrocede hasta 0x10000 bytes buscando el prólogo dado, `memhlp.cpp:181-212`).
   Un patrón ausente → "Failed to find all patterns! Aborting..." y `load()`
   termina sin colocar ningún hook (`main.cpp:206-210`).
2. **RTTI + decompilación de los `IClient*Map`** (`decompiler.cpp`,
   `vftableinfo.cpp`). `Decompiler::parseModule` lee los section headers del
   fichero, recoge las cadenas ≥ 5 bytes de `.rodata`/`.rodata.str`, y en
   `.data.rel.ro` localiza typeinfos (puntero a cadena conocida en
   `typeinfo+4`) y vtables (puntero a typeinfo conocido en `vft+4`; los
   duplicados son `subclasses` para la herencia múltiple de `CUser`,
   `decompiler.cpp:343-413`). Las vtables se indexan por nombre mangled
   (`14IClientUserMap`, `20IClientAppManagerMap`, `14IClientAppsMap`,
   `23IClientRemoteStorageMap`, `15IClientUtilsMap`, `16IClientCompatMap`,
   `21IClientConfigStoreMap`, `17IClientFriendsMap`, `12CCMInterface`,
   `30CClientUnifiedServiceTransport`, `15CGameInfoDialog`,
   `24CSteamMatchMakingServers`, `14CCompatManager`, `12CConfigStore`,
   `12CUserFriends`, `18CUserRemoteStorage`; `vftableinfo.cpp:89-361`). Para
   cada `VFTableInfo_t` sin índice, `parseInterfaceMapBase` (`decompiler.cpp:593-648`)
   desensambla cada función de la vtable del `*Map` (los stubs IPC de Steam
   llaman a `TraceIPC("IClientUser", "GetSteamID")`), sigue saltos, resuelve
   el thunk PIC y los `lea` a cadenas conocidas, y mapea nombre de función →
   índice (sobrecargas con sufijo `2`, `3`…). Solo cinco entradas llevan
   índice fijo porque esas clases no son `*Map`: `CCMInterface::RecvPkt`=3,
   `CClientUnifiedServiceTransport::SendAndRecv`=5,
   `CGameInfoDialog::ServerResponded`=5 (en `steamui.so`),
   `CSteamMatchMakingServers::GetServerDetails`=7 y
   `RequestInternetServerList`=0 (`vftableinfo.cpp:89-132`).
   `DumpClientInterfaces: yes` vuelca el mapa al log.
3. **Nombre RTTI en runtime** (`memhlp.cpp:359-366`) solo para loguear tipos.

**Mecánica de hook.** `DetourHook<T>` = `LM_HookCode` (parche inline de ≥ 5
bytes con trampolín) + `MemHlp::fixPICThunkCall` (`memhlp.cpp:214-306`): si
entre los bytes desplazados hay un `call __x86.get_pc_thunk.*` lo sustituye
por `mov reg, <retorno original>` para que el código PIC siga calculando el
GOT. `VFTHook<T>` = `LM_VmtNew`/`LM_VmtHook` sobre la vtable de un **objeto
vivo** (`hooks.cpp:139-161`). Las vtables de `CCompatManager`, `CConfigStore`,
`CUserFriends` y `CUserRemoteStorage` se detour-hookean "porque se relocalizan"
(`:1407-1409`). `Hooks::removeAll` existe (`:1575-1588`) pero **nadie lo
llama**: no hay kill-switch en caliente, solo no cargar o SafeMode. Los seis
hooks de `IClientAppManager` se crean **dos veces** (`:1502-1507` y
`:1509-1514`, copia literal): la segunda instancia lee como "original" el
puntero ya parcheado y envuelve a la primera; funciona porque los cuerpos son
idempotentes, pero cada llamada pasa dos veces por `hk*`.

**Los 37 hooks** (`hooks.cpp`; "orig." = llama al original; pre/post = dónde actúa):

| Hook | Función de Steam | Tipo | Qué hace | F |
|---|---|---|---|---|
| `TraceIPC` (238-254) | traza IPC `(iface, fn)` | detour/patrón | solo log con `ExtendedLogging` | — |
| `CAPIJob_SendAndRecv` (256-281) | RPC síncrono CM | detour/patrón | pre `Achievements::sendAndRecvGetUserStats`; si contesta, **no** llama al original | 7 |
| `CAppDataCache_BParseResponseFromMessage` (283-300) | parseo de `PICSProductInfoResponse` | detour/patrón | post `Apps::parseProductInfoFromResponse` | 2, 9 |
| `CClientUnifiedServiceMethod_SendAndRecvMsg` (302-334) | servicios unificados | detour/índice 5 | pre logros (`Player.GetUserStats#1`); post playtime (`Player.ClientGetLastPlayedTimes#1`) | 7 |
| `CCMInterface_RecvPkt` (336-390) | cada paquete del CM | detour/índice 3 | captura `g_pCMInterface`; descarta 9406 y `FamilyGroupsClient.NotifyRunningApps#1`; Lua `recvPkt`; `Misc::recvMsg`; `Ticket::recvMsg` | 2, 6, 12 |
| `CSteamEngine_ProcessIPCFrame` (392-495) | despachador IPC | detour/patrón | `placeVFTHooks`; FakeAppIds antes/después; `Apps::runIPCFrame`; `SLSAPI::runIPCFrame`; `ConnectPipe` → `g_processMap[pipe].init` + `Ticket::connectPipe`; `ClosePipe` → borra | 1, 2, 6, 9, 11 |
| `CSteamEngine_SetAppIdForCurrentPipe` (497-516) | appId del pipe | detour/patrón | pre FakeAppIds | 6 |
| `CWebSocketConnection_BBuildAndAsyncSendFrame` (518-564) | envío al CM | detour/patrón | copia del frame; Lua `sendPkt`; `Apps::sendMsg` (GamesPlayed, PICS tokens); `FakeAppIds::sendMsg` | 2, 6, 12 |
| `CSteamMatchmakingServers_GetServerDetails` (566-588), `_RequestInternetServerList` (590-613) | serverbrowser | detour/índices 7, 0 | FakeAppIds | 6 |
| `CUser_CheckAppOwnership` (615-638) | **la** función de propiedad | detour/patrón | orig.; post `Apps::checkAppOwnership` o `DLC::checkAppOwnership` → true | 2, 3 |
| `CUser_GetSubscribedApps` (640-660) | lista de apps con licencia | detour/patrón | post añade `AdditionalApps` | 2 |
| `CUser_PostCallbackToAppId` (662-688) | callbacks a un juego | detour/patrón | pre reencamina al fake | 6 |
| `CUser_SpawnGameId` (690-728) | lanzar el proceso | detour/patrón | pre `LaunchOptions` | 6, 9 |
| `CUserAppManager_BuildDepotDependency` (730-757) | depots a instalar | detour/patrón | post `DepotBlacklist`, `ManifestIds` | 4, 5 |
| `IClientCompat_BIsCompatLayerEnabled` (1241-1250) | `CCompatManager` | detour/RTTI | captura `g_pClientCompat` | 11 |
| `IClientConfigStore_SetString` (1252-1275) | `CConfigStore` | detour/RTTI | post `WebStorage\PrivateApps` | 12 |
| `IClientFriends_GetFriendGamePlayed` (936-953) | `CUserFriends` | detour/RTTI | post deshace el fake | 6 |
| `IClientRemoteStorage_IsCloudEnabledForApp` (955-976) | `CUserRemoteStorage` | detour/RTTI | post false si `DisableCloud` y no poseído | 8 |
| `IClientAppManager_BCanRemotePlayTogether` (759-774) | VMT | VFT | siempre true | — |
| `IClientAppManager_LaunchApp` (776-798) | VMT | VFT | pre `Ticket::launchApp` | 6 |
| `IClientAppManager_IsAppDlcInstalled` (800-822), `_BIsDlcEnabled` (824-848) | VMT | VFT | post true si `shouldUnlockDlc` (`BIsDlcEnabled` evalúa el **padre**) | 3 |
| `IClientAppManager_GetAppUpdateInfo` (850-872) | VMT `GetUpdateInfo` | VFT | post false si `shouldDisableUpdates` | 5 |
| `IClientAppManager_GetAppStateInfo` (874-884) | VMT | VFT | post family lock | 2 |
| `IClientApps_GetDLCCount` (886-908), `_GetDLCDataByIndex` (910-934) | VMT de `CUserAppInfo` | VFT | `DlcData` | 3 |
| `IClientUser_BLoggedOn` (978-997), `IClientUtils_GetOfflineMode` (1211-1222) | VMT | VFT | `FakeOffline` | 12 |
| `IClientUser_BUpdateAppOwnershipTicket` (999-1023), `_GetAppOwnershipTicketExtendedData` (1025-1058), `_GetEncryptedAppTicket` (1060-1083) | VMT | VFT | tickets | 6 |
| `IClientUser_GetLegacyCDKey` (1085-1090) | VMT | VFT | pre `CDKeys` | 6 |
| `IClientUser_GetSteamId` (1092-1160) | VMT `GetSteamID` | VFT | guarda `g_currentSteamId`; `SteamIdOverride` → spoof de una vez → ticket cifrado (Denuvo) | 6 |
| `IClientUser_IsUserSubscribedAppInTicket` (1162-1185) | VMT | VFT | post 0 ("suscrito") si `shouldUnlockDlc` | 3 |
| `IClientUtils_GetAppId` (1187-1209) | VMT | VFT | post appId real del pipe | 6 |
| `CGameInfoDialog_ServerResponded` (1224-1239) | `steamui.so` | detour/índice 5 | pre FakeAppIds | 6 |

**Qué pasa cuando Steam actualiza.** Dos capas. (a) El **gate de hash**
(`Updater::verifySafeModeHash`, `update.cpp:129-164`): SHA-256 del fichero
`steamclient.so` contra `updates.yaml → SafeModeHashes → <VERSION compilada>`
(solo esa clave). Solo se evalúa con `SafeMode` o `WarnHashMissmatch` a `yes`;
con `SafeMode: yes` y hash desconocido → "Unknown steamclient.so hash!
Aborting..." y **no se engancha nada** (`main.cpp:180-187`); con
`WarnHashMissmatch` solo avisa "Please update :)". Con ambos a `no` (default)
**se engancha siempre**. El feed se descarga de `main` por `curl` externo
(`raw.githubusercontent.com` → `cdn.jsdelivr.net`, `update.cpp:19-23`), se
exige que empiece por `"SafeModeHashes:\n"` (`:42`), y se cachea en
`<config>/.updates.yaml` (ruta correcta solo desde `eafd3ea`, 2026-09-02);
sin red ni caché, `Updater::init` falla y con SafeMode no se engancha
(fail-closed). Un hash nuevo bajo la clave vigente **habilita a los usuarios
sin nueva release**. Hoy el último hash es el cliente del 2026-09-03 y los
tags `20260930144343`/`20261001163836` no subieron `version.txt`: con
`SafeMode: yes` cualquier cliente posterior aborta. (b) La **robustez de
patrones y RTTI**: patrón ausente o vtable/índice sin resolver → aborta limpio
con notificación y Steam arranca sin hooks; patrón encontrado en otro sitio
(se toma el último) o índice que cambie de semántica → hook mal colocado y
crash. Los `chore(patterns): Update` del log marcan cuándo Valve rompió
patrones.

**Cómo se recupera.** No hay crash guard, ni downgrade, ni telemetría de
fallos: diagnóstico por `$HOME/.SLSsteam.log` (truncado en cada arranque;
desde `ae5cbf1` la primera línea es `SLSsteam (<rama> -> <commit>) loading in
<proceso>`) y notificaciones `notify-send`. `setup.sh uninstall` borra
`~/.local/share/SLSsteam` y los `.desktop`, pero no `~/.config/SLSsteam` ni el
fichero de PATH de fish (escribe `~/.config/fish/conf.d/SLSsteam.fish` y borra
`~/.config/fish/SLSsteam.fish`). El downgrader externo es h3adcr-b.

**A qué procesos se limita.** Solo `steam`. Dentro, los efectos "por juego"
se resuelven por pipe IPC: en `ConnectPipe`, `g_processMap[pipe]` lee
`/proc/<pid>/exe` (resolviendo `wine-preloader` al `.exe` abierto),
`cmdline`, `environ` y `SteamAppId=` (`process.cpp:593-627,685-723`); sin
`SteamAppId` (el propio steam, steamwebhelper) la entrada queda con `appId=0`
y no se spoofea nada para ella.

**Lista de control.** Entra: `LD_AUDIT`. Localiza: patrones (18, último match),
RTTI + xref de `TraceIPC` (índices derivados), 5 índices fijos. Al actualizar
Steam: gate de hash opcional (off por defecto; fail-closed sin feed), si no se
engancha o crashea. Recupera: log y toast; sin kill-switch en caliente; sin
auto-update (§2.11). Procesos: solo `steam`; por juego vía pipe + `/proc`.

### 2.2 Propiedad y licencias

**Spoof de propiedad.** `hkUser_CheckAppOwnership` (`hooks.cpp:615-638`)
llama primero al original y después a `Apps::checkAppOwnership(appId, info)`
(`apps.cpp:93-142`):

1. No hace nada hasta que Steam haya llamado una vez a `GetSubscribedApps`
   (`applistRequested`, `:97`; el comentario: "let Steam request and populate
   legit data first"), ni si `g_currentSteamId` aún no se ha capturado (primer
   `GetSteamID`).
2. Si `DenuvoGames` ata la app a un SteamID distinto del actual, sale sin
   tocar (`:102-110`).
3. Si `shouldExcludeAppId` (lista `AppIds` + `UseWhitelist`, con herencia del
   `parent`, `config.cpp:366-412`; ≥ 10⁹ siempre excluido), sale.
4. Para **cualquier** app no excluida, poseída o no: borra `lowViolence` y
   `regionRestricted` ("Decensoring", "Bypassing region restriction",
   `:117-126`) y aplica `SubscriptionTimestamps` a `purchaseTime` (`:128-132`).
5. Solo si la app está en `AdditionalApps` (`isAddedAppId`): `unlockApp`
   (`:24-51`) rellena el `AppOwnershipInfo_t` que Steam acaba de devolver:
   `owner = cuenta actual`, `realOwner = 0`, `familyShared = false` en la
   práctica (la sobrecarga con `ownerId` ajeno no tiene llamadores),
   `licensePermanent = true`, `ownsLicense = true`, `releaseState = Released`,
   `licenseExpired/Pending/Locked/retail/autoGrant/fromFreeWeekend/freeLicense/siteLicense = false`,
   `trialTime = 0`. Comentario del autor: "Changing the purchased field is
   enough, but just for nicety in the Steamclient UI we change the owner too".

`hkUser_GetSubscribedApps` (`hooks.cpp:640-660` → `apps.cpp:235-253`): Steam
llama dos veces (primero con tamaño 0 para contar); SLSsteam suma y luego
**añade todos los `AdditionalApps` al final sin comprobar duplicados** (TODO
en `:246`) y marca `applistRequested`. Se reaplica en cada llamada: no hay
caché del lado de SLSsteam.

**Lo que Steam cree de verdad: `CUser::isSubscribed`.** Es la consulta propia
de SLSsteam (`sdk/CUser.cpp:37-46`): llama al **trampolín** de
`CheckAppOwnership`, o sea a Steam sin el spoof, y devuelve
`ownsLicense && !licenseExpired`. Es la definición de "lo posee de verdad"
que usan DLC (`dlc.cpp:16`), logros (`achievements.cpp:154,236`), cloud
(`apps.cpp:442`), CD keys (`:177,446`), updates (`:457`), el comodín de
`FakeAppIds` (`fakeappid.cpp:20`), `LaunchOptions` (`apps.cpp:403`) y la
re-petición de tickets (`hooks.cpp:1002`). Para una app que solo está en
`AdditionalApps` es false; para una app que Steam cree poseída por otra vía
(una licencia real, o la inyección de package-0 de lumalinux) es true, y
entonces todas esas features la tratan como comprada (§4.1).

**Inyección de packages o licencias.** No existe en `main`: no se toca
`ClientLicenseList` (780) ni `CMsgClientGetDepotDecryptionKeyResponse`; la
única "inyección" es por app en los dos hooks de arriba. Lo que sí hay:
`AppTokens` sustituye `access_token` en `ClientPICSProductInfoRequest` (8903)
saliente (`apps.cpp:565-581`) para que PICS devuelva appinfo de apps que lo
exigen; los tokens los pone el usuario en el YAML. La appinfo de apps
inyectadas se parsea al llegar (`parseProductInfoFromResponse`, `:255-272`).

**Cuándo se reaplica / `AppLicensesChanged_t`.** `CConfig::setAdditionalApps`
(`config.cpp:329-364`) calcula `newApps`/`removedApps` en cada recarga del
YAML, solo si no es la primera carga **y** `Apps::applistRequested` ya es
true. `Apps::runIPCFrame` (`apps.cpp:323-381`, en cada `RunInterface`) postea
`AppLicensesChanged_t` (`0xf90be`, lotes de 64, `appsAdded` como máscara,
`:274-321`) **inmediatamente para las eliminadas**, y para las añadidas pide
`RequestAppInfoUpdate` en lotes de 15 (con más "no todas reciben respuesta")
y las deja en `pendingLicenseChanges`; cuando llega la
`PICSProductInfoResponse`, `parseProductInfoFromResponse` postea el callback
solo para las pendientes que aparecen en la respuesta. **En el arranque no se
postea nada**: las apps simplemente aparecen en `GetSubscribedApps`. Desde
`146e28f` (2026-07-30) el callback ya no se dispara "por cualquier appinfo
recibida".

**Juegos que la cuenta posee de verdad.** `checkAppOwnership` no distingue:
una app poseída que también esté en `AdditionalApps` recibe `unlockApp` igual
(sobrescribe owner y flags; el comentario de `config.yaml:34-38` avisa: con
apps compartidas "Overrides OwnerIds… This breaks downloads"); si no está,
solo decensor/región/timestamp. El resto de features consultan `isSubscribed`
para **no** tocar apps legítimas. `hkClientUser_BUpdateAppOwnershipTicket`
(`hooks.cpp:999-1023`) fuerza `staleOnly=false` para apps suscritas de verdad
sin ticket cacheado: cachea también tickets de juegos propios.

**Family sharing.** `DisableFamilyShareLock` (default `yes`,
`config.cpp:177`), tres piezas: (a) en `RecvPkt` descarta
`ClientSharedLibraryStopPlaying` (9406) y el `ServiceMethod` (146) con
`target_job_name == "FamilyGroupsClient.NotifyRunningApps#1"`
(`hooks.cpp:360-380`): este cliente ni deja de jugar cuando el dueño arranca
ni avisa a los demás; (b) `Apps::getAppStateInfo` (`apps.cpp:144-172`, post
de `IClientAppManager::GetAppStateInfo`) pone `ownerAccountId = cuenta
actual`, `realOwner = 0` y quita `k_EAppOwnershipFlagsBorrowed` para el juego
del pipe actual, también para juegos prestados de verdad (no mira
`isSubscribed`): los juegos no ven que son "borrowed"; (c) `owner_id = 1` en
cada entrada de `ClientGamesPlayed*` saliente (`:509,556`). No es spoof de
ownership: es supresión de mensajes y de flags.

**Depots compartidos (redists, 228980).** `hkUserAppManager_BuildDepotDependency`
recibe `depots` y `sharedDepots` (`hooks.cpp:730-757`);
`Apps::buildDepotDependency` (`apps.cpp:58-91`) solo **loguea** los
`sharedDepots`; no hay lógica para 228980 ni para ningún depot compartido.

**Lista de control.** Spoof: por app, post-hook de `CheckAppOwnership` +
`GetSubscribedApps`, solo `AdditionalApps`, tras el primer `GetSubscribedApps`.
Packages/licencias: no; `AppTokens` en PICS; `AppLicensesChanged_t` solo en
caliente. Poseídos de verdad: `isSubscribed` (trampolín) los protege en todas
las features salvo en `checkAppOwnership` (decensor/región) y en `unlockApp` si
se listan. Family sharing: paquetes descartados + `GetAppStateInfo` + `owner_id`.
Redists: nada.

### 2.3 DLC

**Criterio de unlock: `DLC::shouldUnlockDlc(appId)`** (`dlc.cpp:8-27`): true
si (a) el pipe actual es de un juego (`IClientUtils::GetAppId() != 0`; nunca
dentro del propio cliente), (b) la cuenta **no** posee el AppId consultado
(`isSubscribed`) y (c) no está excluido por `AppIds`/`UseWhitelist`. Es decir,
**todo DLC no poseído, de cualquier juego, consultado desde un proceso de
juego**, salvo exclusiones; no mira `AdditionalApps`, no hay lista de DLC ni
criterio "por juego en marcha" distinto del pipe. (Reverificado 2026-10-08;
`nosotros.md` §2.3 lo daba como pendiente de reverificar.)

**Qué contesta.** `CheckAppOwnership` del DLC → `DLC::checkAppOwnership`
(`:29-39`) → `Apps::unlockApp` (mismos flags que una app), evaluado solo si
`Apps::checkAppOwnership` devolvió false (`hooks.cpp:632`) y **sin** la guarda
`applistRequested`. `IClientAppManager::IsAppDlcInstalled(app, dlc)` → true si
`shouldUnlockDlc(dlcId)` (`:46-49`): un DLC sin depot queda "instalado" solo
por flag. `BIsDlcEnabled(app, dlc)` → true si `shouldUnlockDlc(appId)`: se
evalúa sobre el **padre**, no sobre el DLC (`:41-44`, `hooks.cpp:842`).
`IClientUser::IsUserSubscribedAppInTicket(steamId, app)` → 0 ("suscrito") si
`shouldUnlockDlc(appId)` (`:51-56`; firma corregida en `6546411`, antes el
appId que llegaba era basura, "Fix DLC unlocker in games using AuthSessions").

**DLC comprado de verdad.** `isSubscribed` true → `shouldUnlockDlc` false → la
respuesta original de Steam se respeta.

**Límites y `DlcData`.** El número 64 no aparece en `dlc.cpp` ni en el parseo
de `DlcData` (`config.cpp:238-269`); el comentario del YAML dice "Only needed
when the App you're playing is hit by Steams 64 DLC limit" (`res/config.yaml:40-42`).
Con entrada en `DlcData`, `GetDLCCount` devuelve `dlcData[app].size()` y
`BGetDLCDataByIndex` devuelve el i-ésimo par `(dlcId, nombre)` de un
`unordered_map` (orden no determinista) **sin llamar al original**
(`dlc.cpp:58-97`, `hooks.cpp:901-915`): si el YAML tiene menos DLC que Steam,
los que falten desaparecen de la lista; no comprueba `index < size`. Sin
entrada, llama al original y fuerza `available = true` para el DLC devuelto
si no está excluido. El único `0x40` del código es
`AppLicensesChanged_t::MAX_APPS_PER_CALLBACK`, que es otra cosa.

**Cómo se quita.** No hay persistencia: sacar la app de `AdditionalApps` o
excluirla en `AppIds` y guardar; el hot reload hace que la siguiente consulta
devuelva lo original. Los DLC no se añaden a `GetSubscribedApps`. Lo único
persistente son los tickets en `<config>/cache/` y, para CD keys, `cdk_<n>` en
`localconfig.vdf` (`config.yaml:49`).

**Lista de control.** Criterio: todo DLC no poseído y no excluido mientras
el pipe sea de un juego. Comprado: se respeta. Sin depot: flag en
`IsAppDlcInstalled`/`BIsDlcEnabled`/ticket. Límites: ninguno propio; `DlcData`
suple la lista. Quitar: config.

### 2.4 Claves y manifests

**Claves de depot.** No existe ningún código que maneje claves de descifrado
de depots: ni config, ni descarga, ni almacenamiento, ni hook sobre
`LoadDepotDecryptionKey`/`GetDepotDecryptionKey`. SLSsteam depende de que
Steam las obtenga por sus cauces con la licencia que Steam crea tener; el
propio YAML avisa de que inyectar apps "breaks downloads" (`config.yaml:34`).
La clave `DepotKeys` (inyección en `CMsgClientGetDepotDecryptionKeyResponse`,
5439) existió solo en la rama `update` y fue retirada allí mismo (§5.3).

**Manifests.** Solo `ManifestIds` (`depotId: manifestId`, `config.cpp:198`),
aplicado en el post-hook de `BuildDepotDependency` sobre `depot->manifestId`
(`apps.cpp:76-80`), y `DepotBlacklist` (`:69-73`), que elimina depots del
vector haciendo swap con el último y `size--` **sin** re-inspeccionar el
elemento movido ni hacer `continue` (dos depots en lista negra consecutivos
al final pueden quedarse; el override de manifest se aplica al recién movido).
No hay hubs, generadores, archivos, ni "manifest request codes": nada relativo
a `GetManifestRequestCode` ni al CDN. El "exploit" de códigos (sustituir
`depotId` por 1 en la petición de manifest) existió solo en la rama `update`.

**CD keys legacy** (`CDKeys`, `config.cpp:200`): `Apps::getLegacyCDKey`
(`apps.cpp:174-233`, pre-hook de `IClientUser::GetLegacyCDKey`): si la app es
poseída de verdad, nada; si hay clave en `CDKeys` la usa; si no, genera una
determinista `XXXX-XXXX-XXXX-XXXX` (A-Z0-9, `srand(accountId + appId)`) y la
fija con `IClientUser::SetLegacyCDKey` para que la lectura original la
devuelva (inyectarla solo en `Get` "no funcionaba", `:226-228`); queda en
`localconfig.vdf` como `cdk_<n>`. `shouldDisableCDKey` existe sin llamadores.

**Qué deja de funcionar si cae cada servicio.** Sin GitHub/jsDelivr: solo el
feed de SafeMode, y solo si está activado (cae a la caché; sin caché,
fail-closed). Sin `store.steampowered.com/appreviews`: los esquemas de logros
de apps no poseídas (§2.7). Sin el CM de Steam: todo, porque todo cuelga del
cliente logueado.

**Lista de control.** Claves: ninguna. Manifests: solo pin por `ManifestIds`
y lista negra de depots; sin fuentes. Códigos: ninguno. Servicios: GitHub
(SafeMode), tienda (logros).

### 2.5 Updates de juegos

**Pins o congelación.** `ManifestIds` por depot (§2.4) sirve tanto para bajar
una versión antigua como para fijarla (comentario `config.yaml:62-64`). Para
apps **no poseídas de verdad** además `DisableUpdates: yes` (default,
`config.cpp:189`): `Apps::shouldDisableUpdates` (`apps.cpp:449-458`) devuelve
true si la opción está activa y (`isAddedAppId` **o** `!isSubscribed`;
comentario: "Using AdditionalApps here aswell so users can manually block
updates"), y entonces `hkClientAppManager_GetUpdateInfo` devuelve `false`
(`hooks.cpp:865-869`): el cliente no ve información de update. El comentario
del YAML (`:133-135`) explica que solo vale para no poseídas "since those do
not get any depots from CUserAppManager::BuildDepotDependency"; para
poseídas, `ManifestIds`.

**Historia del mecanismo** (importa porque lumalinux tuvo un parche que
dependía de él, §5 F5): 2026-07-05 `GetAppStateInfo` limpiando
`APPSTATE_UPDATE_*` (sin toggle, para todo `AdditionalApps`); 07-12
`CClientConfigStore::GetInt` devolviendo `DisableUpdatesUntil = now+20 min`
(bloqueaba también el Workshop); 07-12/13 vuelta a `GetUpdateInfo → false`
con la clave `DisableUpdates` y sin el límite "solo instalados".

**Actualización automática y cómo decide.** La decide Steam; SLSsteam solo
niega `GetUpdateInfo` o reescribe la lista de depots. No hay planificación,
no se toca `pBuildId` (`hooks.cpp:738`), no hay selección de build.

**Update que Steam ya empezó.** Nada: no hay código que cancele ni intercepte
descargas en curso; solo afecta a la siguiente evaluación.

**Lista de control.** Pins: `ManifestIds` (+ `DisableUpdates` para no
poseídas). Automática: Steam. Build antiguo: `ManifestIds`. En curso: nada.

### 2.6 Fixes y DRM

SLSsteam no distribuye ni aplica fixes (ni Steamless, ni Goldberg, ni EOS).
Lo que tiene es **tickets** y **SteamID**, y una forma de ejecutar un juego
"como" otro AppId.

**Detección de DRM** (`process.cpp`): en `ConnectPipe` se analiza el ejecutable
del proceso (y sus ficheros abiertos, `/proc/<pid>/map_files`) solo si
`SmartTickets != 0`: `steamDRM` = última sección `.bind` con entropía ≥ 7.0
(`:53-88`); `denuvo` = secciones `.xtext/.xcode/.xpdata/.xtls` o entropía de
código > 6.25 (`:90-169`). El autor midió "<10 ms SteamDRM, 20-2000 ms Denuvo"
(`config.yaml:89-96`).

**Tickets de propiedad** (858, `ClientGetAppOwnershipTicketResponse`,
`ticket.cpp`): al recibir uno con `eresult == OK` lo guarda en
`<config>/cache/ticket_<app>.yaml` (`steamId: <64 bits>`, `ticket: <base64>`);
al lanzar (`LaunchApp`) lo recarga con `CUser::UpdateAppOwnershipTicket` (y
postea `AppOwnershipTicketReceived_t`). Si la respuesta no es OK **no** inyecta
nada en la capa de red ("rompería el modo offline", `:294`). Los YAML se
cargan sin `try/catch`. **Tickets cifrados** (5527): se guarda el mensaje
entero serializado en `encryptedTicket_<app>.yaml`; si el servidor **falla**,
se sustituye el paquete entrante por el cacheado y el juego recibe el ticket
viejo (`:273-280`). Fuente alternativa: `tools/ticket-grabber`, desde una
cuenta que sí posee el juego. Los tickets de 32 bits anteriores a `6d1c7fd`
(2026-07-27) no coinciden con el formato actual.

**SteamID que ve el juego** (`hkClientUser_GetSteamId`, `hooks.cpp:1092-1160`;
nunca para el propio steamclient ni con FakeAppIds activo): en orden,
`SteamIdOverride[app]` si ≠ 0, o el SteamID del `ticket_<app>.yaml` si es 0;
luego un `oneTimeSteamIdSpoof[app]` pendiente, que se consume; luego el
SteamID del `encryptedTicket_<app>.yaml`, y con el bit Denuvo de
`SmartTickets` solo si el proceso tiene Denuvo detectado. `SmartTickets`
(default `0x1` = SteamDRM; `0x2` = Denuvo): con SteamDRM, `Ticket::connectPipe`
programa el spoof de una vez **antes** de que el juego pida nada (para que el
wrapper SteamStub valide con el SteamID del ticket); sin ese bit lo programa
`GetAppOwnershipTicketExtendedData`; con Denuvo, `GetEncryptedAppTicket`
(desde `39822da`, 2026-09-28). El hook de `GetSteamID` fue naked (trampolín a
mano), luego intercepción del buffer IPC (`fnId 0xD6FC3200`), y desde
`779178c` (09-28) un VFT hook real. El contador de pipes para Denuvo y el
cambio tras N `GetSteamId` se retiraron ("too unreliable").

**Denuvo (cuenta).** `DenuvoGames{steamId64: [appIds]}`: `checkAppOwnership`
**no** finge propiedad de esas apps si el dueño no es la cuenta actual
(`apps.cpp:102-110`): se asume que se juega con los tickets de la otra cuenta.
Sin fuentes de tokens: solo lo que pasó por la red o lo que trae
`ticket-grabber`.

**FakeAppIds** (ejecutar con otro AppId, `fakeappid.cpp`): `FakeAppIds[app]`,
o `FakeAppIds[0]` como comodín para no poseídas. El AppId real sale de
`SteamAppId=` del environ del proceso (`g_processMap`). `SetAppIdForCurrentPipe`
pone el falso; antes y después de cada `RunInterface` se conmuta: la lista
`shouldUseRealAppIdForInterface` (`:60-118`: Utils, Billing, Apps, UserStats,
RemoteStorage, AppManager, ConfigStore, HTTP, Screenshots, UnifiedMessages…
Timeline) ve el **real**; User, Friends, Matchmaking, Networking,
GameCoordinator, GameServer* ven el **falso**. Stats, cloud y appinfo operan
sobre el juego real; presencia, amigos y matchmaking sobre el falso.
`PostCallbackToAppId` reencamina callbacks; `GetFriendGamePlayed` deshace;
serverbrowser por handle e IP; `ClientGamesPlayed*` y `routing_appid` de
`ClientRichPresenceUpload` salen con el falso. Una interfaz nueva no listada
vería el falso por defecto. Aviso del YAML: no correr varios juegos bajo el
mismo AppId.

**`LaunchOptions`** (`apps.cpp:384-432`, pre de `SpawnGameId`): por `gameId`,
luego `4294967294` (solo si **no** es poseído), luego `4294967295` (todos);
`%command%` se sustituye por la línea original, si no la reemplaza entera;
ignora shortcuts no-Steam. Permite envolver el lanzamiento (p.ej. un fix
externo), pero SLSsteam no distribuye ninguno.

**Cómo se aplican y quitan.** Solo config (`SmartTickets`, `SteamIdOverride`,
`DenuvoGames`, `FakeAppIds`, `LaunchOptions`) y caché de tickets; borrar
`<config>/cache/ticket_<app>.yaml` elimina el spoof automático. SLSsteam nunca
borra la caché.

**Lista de control.** Denuvo: detección + `DenuvoGames` + tickets cifrados
cacheados; sin fuentes. Steamless/Goldberg/EOS: no. Aplicar/quitar: config y
caché.

### 2.7 Logros, stats y tiempo de juego

**Dónde se guardan.** En Steam (`appcache/stats`), no en SLSsteam. SLSsteam
no persiste logros ni stats; en memoria solo `preferredOwners`,
`ownerBlacklist` y `fetchCooldowns` (`achievements.hpp:17-19`, sin mutex).

**Quién contesta `GetUserStats`: el "schema borrow".** Dos rutas, ambas solo
para apps **no poseídas de verdad** (`isSubscribed` guard,
`achievements.cpp:154` y `:236`; es este `call` el que lumalinux repunta,
`nosotros.md` §2.7):

1. Unificada `Player.GetUserStats#1` por `CClientUnifiedServiceTransport::SendAndRecvMsg`
   (`hooks.cpp:302-334`): `sendAndRecvGetPlayerStats` (`:140-189`) limpia
   `crc_stats`, prueba primero `preferredOwners[app]`, luego cada reseñador;
   `tryGetPlayerStats` (`:107-138`) pone el `steamid` del reseñador, envía por
   el trampolín, en `k_EResultFailure` lo manda a `ownerBlacklist`, y si OK
   **borra `crc_stats` y `stats`** de la respuesta (deja solo el esquema) y
   memoriza el owner.
2. Legacy `CMsgClientGetUserStats` (818/819) por `CAPIJob::SendAndRecv`
   (`hooks.cpp:256-281`): `sendAndRecvGetUserStats` (`:227-273`), mismo
   esquema con `steam_id_for_user`, borra `achievement_blocks`, `crc_stats` y
   `stats`.

Si nadie responde: cooldown de 10 minutos por app (`COOLDOWN_MINUTES`,
`achievements.hpp:15`) y `k_EResultNoConnection` (`:186-188`, `:268`) para que
el cliente caiga a su caché offline. Con respuesta, **no se llama al
original**. Con `MaxSchemaTries: 0` no se descarga nada y se usa lo que haya
en `appcache/stats` (p.ej. lo que puso `tools/schema-grabber`).

**Esquemas.** `getReviewersForGame` (`:28-94`): descarga con `curl` externo
`https://store.steampowered.com/appreviews/<app>?json=1&filter=recent&language=all&purchase_type=all&num_per_page=<MaxSchemaTries>`
(`:21-23`) y extrae con regex todos los `"steamid":"<n>"`, saltando la
blacklist. `MaxSchemaTries` (default 10) = cuántos reseñadores recientes se
piden y se prueban como "dueños". Depende de que haya reseñas recientes de
usuarios con stats públicas. En `dev` (`51724f5`, 2026-10-03) el endpoint pasa
a `api.steampowered.com/IUserReviewsService/GetAppReviews/v1/` con el
comentario "Store endpoint gets deprecated on 2026.10.22"; `main@049bbdd` y la
release `20261001163836` siguen con el de la tienda. `tools/schema-grabber`
hace lo mismo desde fuera (login propio, 10 reseñadores) y escribe
`UserGameStatsSchema_<app>.bin` + `UserGameStats_<acc>_<app>.bin` vacíos.

**Tiempo de juego.** `Apps::sendAndRecvLastPlayedTimes` (`apps.cpp:460-479`,
post de `Player.ClientGetLastPlayedTimes#1`) **borra de la respuesta** las
entradas de juegos en `AdditionalApps` ("Removed serverside PlayTime",
aportación de exefer para que el tiempo de refunds no se pise). No hay
sincronización propia entre máquinas; el README remite a CloudRedirect.

**Convivencia con el unlocker.** SLSsteam es el unlocker; para apps poseídas
no interviene. Pide stats **de otros usuarios** desde la sesión propia,
posiblemente decenas de veces (riesgo de detección, §2.12).

**Lista de control.** Almacén: Steam. Sincronización: ninguna. `GetUserStats`:
SLSsteam para no poseídas (borrow), Steam para el resto. Esquemas: reseñadores
de la tienda (`MaxSchemaTries`), o `schema-grabber` offline.

### 2.8 Cloud saves

`DisableCloud: yes` (default, `config.cpp:188`): `Apps::shouldDisableCloud`
(`apps.cpp:434-442`) → true si la opción está activa y la app **no** es poseída
de verdad; el post-hook de `IClientRemoteStorage::IsCloudEnabledForApp`
devuelve false (`hooks.cpp:955-976`). El YAML sugiere `no` si se usa
"CloudRedirect or similar" (`:129-130`); `SteamIdOverride` "can be used to
workaround locked saves" (`:85`). No hay redirección ni proveedores. Con cloud
activo para una app no poseída, Steam intentaría sincronizar con la cuenta
real (resultado no evaluado en el código). `FakeAppIds` pone el AppId real
para la interfaz `RemoteStorage`, así que el cloud opera sobre el juego real.

**Lista de control.** Redirección: no. Convivencia: apaga Steam Cloud para no
poseídas por defecto; el resto es de Steam.

### 2.9 Añadir y quitar un juego

**Entradas.** Únicamente el **AppId** en `config.yaml` (`AdditionalApps` para
inyectar, `AppIds` para in/excluir), a mano, por plugin Lua
(`SLS.config:setAdditionalApps`, `lua.cpp:455`) o, para instalar, por el
fichero de comandos (`install|<app>|<libIdx>`, `api.cpp:289-303` →
`IClientAppManager::InstallApp`). No hay nombre, URL de tienda, zip ni "lua
suelto" (los `.lua` son plugins de código). `dumplibraries` enumera
bibliotecas (índice, etiqueta, ruta) y `uninstall|<app>` → `UninstallApp`.

**Qué escribe al añadir.** Nada en disco por parte de SLSsteam (la config la
escribe el usuario; la caché de tickets se escribe al recibir tickets, no al
añadir). En memoria: `addedAppIds`, `newApps` → `RequestAppInfoUpdate` →
`AppLicensesChanged_t` (§2.2). Steam escribe después appinfo y `appmanifest`
por sus cauces; el nombre de carpeta lo decide Steam.

**Qué limpia al quitar.** Nada: ni tickets, ni caché, ni `cdk_<n>` de
`localconfig.vdf` ("delete cdk_[number] from your localconfig.vdf",
`config.yaml:49`). Quitar de `AdditionalApps` genera `removedApps` →
`AppLicensesChanged_t` para que la biblioteca lo oculte.

**Lista de control.** Entradas: AppId (YAML, Lua, API). Escribe: nada. Limpia:
nada. Bibliotecas: por índice vía API. Carpeta: Steam.

### 2.10 Credenciales y proveedores

SLSsteam solo exige una sesión de Steam iniciada (todo ocurre dentro del
steamclient logueado). No pide claves API, cuentas externas ni tokens propios.
Opcionales en el YAML: `AppTokens` (tokens PICS que consigue el usuario),
`CDKeys`, `DenuvoGames`/`SteamIdOverride` (SteamIDs), y los tickets cacheados
(automáticos desde el tráfico, o importados con `ticket-grabber`). Las dos
herramientas .NET sí piden **usuario y contraseña en `argv`** y Steam Guard
por consola, y guardan `./Sentries/<user>.sentry` relativo al cwd;
`ticket-grabber` necesita una cuenta que posea el juego.

**Caducidad.** No se detecta: los tickets cacheados se reutilizan sin mirar
antigüedad (`BUpdateAppOwnershipTicket` solo fuerza refresco si no hay caché;
el ticket cifrado cacheado se sirve si el servidor falla). Reseñadores: blacklist
en memoria por fallo y cooldown de 10 min.

**Sin ninguna.** El unlock básico (propiedad + DLC), `FakeOffline`, tickets
cacheados: sí. Esquemas de logros de no poseídas sin red: caché offline de
Steam.

**Lista de control.** Exige: sesión de Steam. Opcionales: tokens, claves,
SteamIDs, tickets. Caducidad: no detectada. Sin ninguna: unlock sí.

### 2.11 Mantenimiento propio

**Superficie.** (1) `config.yaml` con hot reload: inotify sobre el directorio
padre con `IN_CLOSE_WRITE\|IN_MOVED_TO` filtrando por nombre
(`filewatcher.cpp:14-78,115-149`; desde `1444fa5` también renames atómicos),
`loadSettings()` y toast "Config reloaded!"; cada valor en un `MTVariable` que
los hooks leen en cada llamada, salvo `SafeMode`/`WarnHashMissmatch`
(arranque) y `DumpClientInterfaces` (init). Clave ausente → default +
notificación agrupada "Config loading errors:\nMissing X…"; clave desconocida
→ ignorada en silencio (pero legible desde Lua). El fichero se crea con el
default embebido solo si no existe (`createFile`, `config.cpp:58-84`); nunca
se reescribe. (2) **Fichero de comandos** `/tmp/SLSsteam.API` (`api.cpp`):
creado y truncado en cada arranque con la umask del usuario, vigilado por
inotify en `/tmp`; cada escritura relee el fichero entero y parsea cada
línea `cmd|arg|arg` (`:31-57`); comandos `dumpcompat|app`, `getcompat|app`,
`setcompat|app[|tool]` (Proton por app vía `IClientCompat`), `dumplibraries`,
`install\|app\|lib`, `uninstall\|app`, `reloadlua` (`:59-148`); salida solo al
log con nivel `API`; ops de compat y biblioteca encoladas bajo `cmdMutex` y
ejecutadas en el hilo IPC en el siguiente `RunInterface`. Activación: `API:
yes` (el YAML instalado lleva `no`; si la clave falta, el código asume `true`,
`config.cpp:162`). Cualquier proceso del mismo usuario (un juego incluido)
puede escribirlo; estuvo roto entre `20260930144343` y `20261001163836`
(`strsplit` por regex partía `"|"` en cada carácter, `42568f0`). (3)
**Plugins Lua** (`lua.cpp`): LuaJIT 2.1 con `luaL_openlibs` completo → **sin
sandbox** (`os`, `io`, `ffi`; el YAML avisa "plugins can run arbitrary
code"). `<config>/plugins/*.lua` en orden alfabético, solo si `Plugins: yes`
(default `no`); permisos forzados a 0700 en directorio y ficheros (si falla,
plugins desactivados esa sesión); tras cargar, `loadSettings(false, true)`
para que los plugins vean claves nuevas; hot reload por inotify (en release
**sin** recrear el estado: callbacks y globales persisten y los ficheros se
re-ejecutan encima; en DEBUG se recrea). Callbacks: `SLSsteam::configLoading`,
`configLoaded`, `initialized` (tras los hooks, `hooks.cpp:1572`), `luaReload`,
`Network::recvPkt` (antes de Misc/Ticket), `Network::sendPkt` (antes de
Apps/FakeAppIds), bajo `stateMutex` recursivo. Expuesto:
`curl.downloadString`, `log.*`, `memhlp.{getModule,getJmpTarget,hexdump,findPrologue,patternScan}`,
`VFTableInfo_t` (resuelve contra `Decompiler::vftables`, que por eso no se
libera tras init), `LuaHook(name, ptr)` con `place()/remove()` y el export C
`place_lua_hook`, `unpack_user_data`, `LuaMutex`, `YAMLNode`, `CConfig`
(getters tipados, `get/setAdditionalApps`, `get/setNode`), `CNetPacket`,
`CSteamEngine.getUser/getUtils`, `CUser.{getClientApps,getClientUser,getAppManager,isSubscribed,postCallback}`,
`IClientApps.getAppData/getAppType`, `IClientUser.loggedOn`,
`IClientUtils.getAppId/getCurrentSteamPipe`, `SLS.{config,steamEngine,registerCallback}`.
En resumen un plugin puede hookear cualquier dirección, leer y escribir
memoria por FFI, descargar de internet, postear callbacks a los juegos y
cambiar la config en memoria. El API cambió nombres el 2026-08-31 sin versión
ni garantía de estabilidad. (4) Notificaciones `notify-send` por `system()`
con el texto interpolado sin escapar entre comillas dobles (`log.cpp:122,145`).
No hay QAM, GUI ni CLI propia.

**Update de sí mismo.** No existe. `update.cpp` **no actualiza SLSsteam**:
solo baja la lista de hashes para SafeMode/Warn y, con `WarnHashMissmatch`,
dice "Please update :)". La versión instalada se conoce por `VERSION`
embebida (`res/version.txt`, que no sube en cada tag) y por rama + commit en
la primera línea del log. Actualizar = reemplazar el `.so` con `setup.sh` (o
el paquete). Release = 7z/zip con `bin/SLSsteam.so`, `bin/library-inject.so`
(0 bytes), `setup.sh`, `res/config.yaml`, `docs/LICENSE/`, y los dos
binarios .NET (`Makefile:110-131`); variantes `debug` (`-D DEBUG`: niveles
Debug/Once/Trace y recreación del `lua_State`) y `release`, **ninguna
strippeada**.

**Salud, estado, logs.** `$HOME/.SLSsteam.log`, truncado en cada arranque,
prefijo `[Nivel in fichero:función:línea]`, bits `LogLevels` (default `0xff`;
NotifyShort `0x40`, NotifyLong `0x80`; API `0x100` automático con `API: yes`).
Notificaciones: "Loaded successfully", "Config reloaded!", fallos de
patrones/VFT, hash mismatch, errores de config. No hay fichero de estado.

**Lista de control.** Superficie: YAML + fichero de comandos + plugins Lua +
toasts. Update propio: no. Componentes: n/a. Versión instalada: `VERSION`
embebida + commit en el log. Salud: log y toasts.

### 2.12 Proyecto

**Plataformas.** Linux x86 32 bits dentro del proceso `steam`; nativo, Arch,
Flatpak, SteamOS/Deck (`steam-jupiter` no pasa por PATH: el flujo Deck real
depende de instaladores externos como h3adcr-b o el nuestro). Build en Docker
bookworm (glibc antigua) con g++ (clang "crashes on some hooks").

**Código abierto, licencia, quién, ritmo.** AGPL-3.0-only; un mantenedor;
tags por fecha (`YYYYMMDDHHMMSS`), 22 entre 07-05 y 10-01, con parejas el
mismo día (hotfix) y huecos de hasta 25 días; `pkgver` del PKGBUILD siempre un
tag por detrás; rama `dev` activa (`pkg/slssteam-git` compila `dev`). Historia
reescrita el 2026-07-21: `origin/main` empieza en un commit raíz sin padre y
los tags anteriores quedaron huérfanos (§5.3). Hay un "Hall of Shame" en el
README ("OnetapBeta & Hammer Decky… Resells Steamless & SLSsteam") y el último
commit (`049bbdd`) quita a "ADS" de los créditos por "supposedly collect HWIDs
of their users".

**Servicios de terceros.** `raw.githubusercontent.com` y `cdn.jsdelivr.net`
(feed de hashes), `store.steampowered.com` (reseñas), binario `curl` y
`notify-send` del sistema, `/proc`, inotify, glibc rtld-audit. Estáticas:
libmem (hooking + capstone/keystone), luajit, protobuf, yaml-cpp. Dinámicas:
openssl (SHA-256), libcurl (enlazada, sin uso), libgomp. Herramientas:
.NET 9 + SteamKit2 3.3.1.

**Telemetría y privacidad.** Ninguna telemetría; las peticiones propias son
GET sin identificadores ni cabeceras. El código evita loguear SteamIDs; el log
sí lleva appIds, rutas y nombres de ejecutables. Spoofs locales permanentes:
borra `parental_settings` y su firma del logon **siempre, sin opción**
(`misc.cpp:88-104`, desde `a456396`).

**Riesgo de detección (hechos del código).** Vive en el espacio de direcciones
de `steam` y parchea código de `steamclient.so` (detours inline) y vtables de
objetos vivos; modifica paquetes CM salientes (`GamesPlayed*` con `owner_id=1`
y títulos; `PICSProductInfoRequest` con tokens; `RichPresenceUpload` con otro
`routing_appid`) y descarta entrantes (family share); pide stats de **otros
usuarios** desde la sesión propia; suplanta SteamID y AppId a los juegos. Sin
ofuscación ni anti-detección; ninguna respuesta de VAC se evalúa.

**Lista de control.** Plataformas: Linux 32-bit en `steam`. Abierto: AGPL,
un mantenedor, ~4 días por tag. Terceros: GitHub/jsDelivr, tienda de Steam,
curl, notify-send. Telemetría: ninguna. Detección: la propia del mecanismo.

---

## §3 Superficies externas

### 3.1 Hosts y URLs

| URL | Dónde | Para qué |
|---|---|---|
| `https://raw.githubusercontent.com/AceSLS/SLSsteam/refs/heads/main/res/updates.yaml` | `update.cpp:21` | feed de hashes SafeMode (primer intento); solo si `SafeMode` o `WarnHashMissmatch` |
| `https://cdn.jsdelivr.net/gh/AceSLS/SLSsteam/res/updates.yaml` | `update.cpp:22` | espejo |
| `https://store.steampowered.com/appreviews/<app>?json=1&filter=recent&language=all&purchase_type=all&num_per_page=<MaxSchemaTries>` | `achievements.cpp:21-23` | SteamIDs de reseñadores para el borrow de esquemas (`dev` ya usa `api.steampowered.com/IUserReviewsService/GetAppReviews/v1/`) |
| cualquiera | `lua.cpp:98-116` | `curl.downloadString` desde plugins |

Todo por el binario `curl` externo con `--silent --connect-timeout 15`
(`curl.cpp`), `PATH='/usr/bin:/bin'` fijo (con comillas literales,
`utils.cpp:55-59`). Sin User-Agent ni datos del usuario.

### 3.2 Ficheros en disco

| Ruta | Dónde | Uso |
|---|---|---|
| `$XDG_CONFIG_HOME/SLSsteam/` o `$HOME/.config/SLSsteam/` | `config.cpp:22-41` | base; se crea; si no puede, `CConfig::init` falla y nada se engancha (`284115b`) |
| `<config>/config.yaml` | `config.cpp:44-84` | creado con el default si falta; leído con `YAML::LoadFile`; vigilado |
| `<config>/.updates.yaml` | `update.cpp:94-127` | caché del feed de hashes |
| `<config>/cache/ticket_<app>.yaml`, `encryptedTicket_<app>.yaml` | `ticket.cpp:23-45,177-186` | tickets cacheados (`steamId` + base64) |
| `<config>/plugins/*.lua` | `lua.cpp:287-322` | plugins; 0700 forzado; vigilado |
| `$HOME/.SLSsteam.log` | `log.cpp:389-401` | log, truncado en cada arranque |
| `/tmp/SLSsteam.API` | `api.cpp:150-175` | fichero de comandos, creado siempre |
| `/proc/<pid>/{exe,cmdline,environ,map_files/*}` | `process.cpp:523-627` | proceso que conecta un pipe |
| ruta de `steamclient.so`, `steamui.so` | `update.cpp:137-141`, `decompiler.cpp:418-434`, `main.cpp:267` | SHA-256 y parseo ELF desde disco |
| `/bin/curl`, `/usr/bin/curl`, `/run/current-system/sw/bin/curl` | `curl.cpp:20-25` | descargas |
| `localconfig.vdf` (`cdk_<n>`) | vía `SetLegacyCDKey` | CD keys inyectadas (lo escribe Steam) |
| instalación: `~/.local/share/SLSsteam/{SLSsteam.so,library-inject.so,path/*}`, `~/.local/share/applications/steam{,-native}.desktop`, `~/.config/fish/conf.d/SLSsteam.fish`; Flatpak `~/.var/app/com.valvesoftware.Steam/.local/share/SLSsteam/`; Arch `/usr/lib32/libSLSsteam.so` | `setup.sh`, `PKGBUILD` | |

### 3.3 Variables de entorno

| Variable | Dónde | Uso |
|---|---|---|
| `LD_AUDIT` | `main.cpp:123-128` | carga; se limpia de las entradas propias en `steam` |
| `LD_LIBRARY_PATH` | `main.cpp:141-143` | se le añade `/usr/lib:/usr/lib32` (sin `:`; UB si no existe) |
| `XDG_CONFIG_HOME`, `HOME` | `config.cpp:26-36`, `log.cpp:391-399` | config y log; sin `HOME` aborta |
| `SteamAppId` (del environ del **juego**) | `process.cpp:545-563` | AppId real del pipe |
| `SHARED_LIBRARY_GUARD=0` | `setup.sh:163` | solo Flatpak |
| `TRACE`, `DEBUG`, `NATIVE` | `Makefile:17-27` | build |

### 3.4 Claves de `config.yaml` (todas; `res/config.yaml` ≡ `config_default.hpp`)

Todas se leen en `CConfig::loadSettings` (`config.cpp:142-322`) al arrancar,
en cada escritura del fichero y tras cada `Lua::init`. Default = el del YAML
instalado (entre paréntesis el del código si difiere y la clave falta).

| Clave | Tipo | Default | Qué cambia |
|---|---|---|---|
| `DisableFamilyShareLock` | bool | `yes` | §2.2 family sharing |
| `UseWhitelist` | bool | `no` | `AppIds` como lista blanca en vez de negra |
| `AppIds` | lista | vacía | in/exclusión para DLC, decensor, family lock…; herencia por `parent`; ≥ 10⁹ excluido |
| `AdditionalApps` | lista | vacía | apps inyectadas como poseídas; diffs en caliente |
| `DlcData` | `{app: {dlc: nombre}}` | vacío | lista sintética de DLC (§2.3) |
| `AppTokens` | `{app: uint64}` | vacío | `access_token` en PICS |
| `CDKeys` | `{app: str}` | vacío (silent) | clave legacy; si falta, generada |
| `FakeOffline` | lista | vacía | `BLoggedOn` false / `GetOfflineMode` true para ese juego |
| `FakeAppIds` | `{app: app}` | vacío | §2.6; clave `0` = todas las no poseídas |
| `ManifestIds` | `{depot: uint64}` | vacío | pin de manifest por depot |
| `DepotBlacklist` | lista | vacía | depots que no se descargan |
| `IdleStatus` | `{AppId, Title}` | `{0, ""}` | "jugando a" falso sin juego |
| `GameTitles` | `{app: str}` | vacío | título en `GamesPlayed` (solo poseídas) |
| `SubscriptionTimestamps` | `{app: uint32}` | vacío | `purchaseTime` falso |
| `DenuvoGames` | `{steamId64: [apps]}` | vacío | no fingir propiedad si el dueño es otro |
| `SteamIdOverride` | `{app: steamId64 \| 0}` | vacío | SteamID que ve el juego (0 = del ticket cacheado) |
| `SmartTickets` | bits | `1` (SteamDRM) | análisis del exe y spoofs de una vez; `2` = Denuvo |
| `MaxSchemaTries` | uint | `10` | reseñadores a probar; 0 = sin descarga |
| `LaunchOptions` | `{gameId: cmd}` | vacío | `%command%`; `4294967294` no poseídos, `4294967295` todos |
| `SafeMode` | bool | `no` | abortar con hash desconocido; se lee al arrancar |
| `WarnHashMissmatch` | bool | `no` | solo avisar |
| `NotifyInit` | bool | `yes` | toast "Loaded successfully" |
| `API` | bool | `no` (`true`) | procesar `/tmp/SLSsteam.API` |
| `Plugins` | bool | `no` | ejecutar `plugins/*.lua` |
| `DisableCloud` | bool | `yes` | cloud off para no poseídas |
| `DisableUpdates` | bool | `yes` | `GetUpdateInfo` false para `AdditionalApps` o no poseídas |
| `FakeName`, `FakeEmail` | str | `""` | spoof clientside en `PersonaState` / `EmailAddrInfo` |
| `FakeWalletBalance` | int32 | `0` | spoof clientside en `WalletInfoUpdate` |
| `LogLevels` | bits | `0xff` | Trace 1, Once 2, Debug 4, Warn 8, Error 0x10, Info 0x20, NotifyShort 0x40, NotifyLong 0x80 (+API 0x100 automático) |
| `DumpClientInterfaces` | bool | `no` | volcar mapa nombre → índice al log (init) |
| `ExtendedLogging` | bool | `no` | trazas IPC y secciones (solo visible en DEBUG) |

Claves que han **desaparecido** y hoy se ignoran en silencio: `PlayNotOwnedGames`,
`AutoFilterList` (07-07), `GrabSchemas` (07-07 → `MaxSchemaTries`),
`Notifications` (08-11 → bits de `LogLevels`), `LogLevel` (08-11 → `LogLevels`).
`nix-modules/home.nix` sigue con el esquema viejo.

### 3.5 Mensajes de red y callbacks que toca

Salientes (`BBuildAndAsyncSendFrame`): `ClientPICSProductInfoRequest` 8903
(tokens), `ClientGamesPlayedNoDataBlob` 715 / `ClientGamesPlayed` 742 /
`ClientGamesPlayedWithDataBlob` 5410 (`owner_id=1`, títulos, IdleStatus,
AppId falso), `ClientRichPresenceUpload` 7501 (`routing_appid`). Entrantes
(`RecvPkt`): `ClientLogOnResponse` 751 (parental), `ClientPersonaState` 766,
`ClientEmailAddrInfo` 5456, `ClientWalletInfoUpdate` 5528,
`ClientGetAppOwnershipTicketResponse` 858, `ClientRequestEncryptedAppTicketResponse`
5527, `ClientSharedLibraryStopPlaying` 9406 (descartado), `ServiceMethod` 146
con `FamilyGroupsClient.NotifyRunningApps#1` (descartado), `ClientLogon` 5514
(identifica el socket). Jobs y servicios: `ClientGetUserStats` 818/819,
`Player.GetUserStats#1`, `Player.ClientGetLastPlayedTimes#1`,
`ClientPICSProductInfoResponse` 8904. Callbacks posteados: `AppLicensesChanged_t`
`0xf90be`, `AppOwnershipTicketReceived_t` `0xf907c`. `LicensesUpdate_t`
`0x7d` solo en la rama `update`. La reserialización usa una arena estática de
8×1 MB con rotación (`CNetPacket.hpp:16-21,97-103`); mensajes ≥ 1 MB se
rechazan.

---

## §4 Lo que nos afecta

### 4.1 Hallazgos pendientes de decisión

Aparcados el 2026-10-08. Cada uno con dónde está en el código y qué nos toca;
ninguno se ha actuado.

1. **`isSubscribed` es true para nuestros juegos**, así que todas las guardas
   de SLSsteam los tratan como comprados: no solo el borrow de logros (que
   parcheamos), también `DisableCloud`, `DisableUpdates` (solo vía
   `isAddedAppId`, que sí los coge), `CDKeys` (no se inyecta clave para
   ellos), el comodín `FakeAppIds[0]` y `LaunchOptions[4294967294]` (no les
   aplican), y `shouldUnlockDlc` (**sus DLC no se desbloquean por esta vía**
   salvo que el DLC mismo no esté en el package-0 inyectado). Hay que
   contrastar con `nosotros.md` §2.3 qué DLC entran por `keys.txt` y cuáles
   por `AdditionalApps`. (`sdk/CUser.cpp:37-46`, `dlc.cpp:16`, `apps.cpp:177,403,442,457`, `fakeappid.cpp:20`)
2. **Borrow de esquemas y nosotros**: el guard que repunta lumalinux es
   exactamente `achievements.cpp:154` y `:236`; el endpoint de reseñas cambia
   en `dev` y el autor dice que el de la tienda muere el 2026-10-22. Si
   SLSsteam publica, nuestro parche sigue válido (misma forma del `call`);
   si no publica y el endpoint muere, los juegos nuevos no tendrán esquema
   hasta que haya release. Esperar a SLSsteam (ya decidido en `nosotros.md`
   §4.4). (`achievements.cpp:21-23`; `dev` `51724f5`)
3. **`AppLicensesChanged_t` nunca en arranque y solo tras `GetSubscribedApps`**:
   nuestro reconcile dispara `LicensesUpdate_t` (`0x7d`); el de SLSsteam
   `AppLicensesChanged_t` (`0xf90be`) solo cuando LumaDeck añade con Steam
   abierto. Son dos callbacks distintos y nadie ha medido cuál refresca la
   UI (`nosotros.md` §5.1). (`apps.cpp:274-381`, `config.cpp:329-364`)
4. **`GetSubscribedApps` no deduplica**: una app poseída (o inyectada por
   package-0) que además esté en `AdditionalApps` aparece dos veces en la
   lista. Nosotros escribimos `AdditionalApps` para todo lo que añadimos.
   (`apps.cpp:246-250`)
5. **`DisableUpdates` default `yes` bloquea `GetUpdateInfo` para todo
   `AdditionalApps`**: nosotros lo ponemos a `no`; confirmado que basta
   (`.copy()` en cada llamada, hot reload). (`apps.cpp:449-458`)
6. **`DepotBlacklist` tiene el bug del swap**; no lo usamos. `ManifestIds`
   se aplica después del original y antes de que Steam use la lista: es el
   pin vivo que usamos. (`apps.cpp:58-91`)
7. **`SafeMode` fail-closed y `version.txt` congelado**: con `SafeMode: yes`
   el feed debe contener `VERSION` compilada y el hash del cliente; los dos
   últimos tags validan contra `20260903114323` y no hay hashes desde
   09-03. Nosotros forzamos `no` (`nosotros.md` §3.4). Nuestro
   `derive_version_floor` subestima por diseño (`nosotros.md` §4.1).
8. **`library-inject.so` vacío**: upstream lo deja en `LD_AUDIT`; nuestro
   wrapper ya lo omite si está vacío. Nada que hacer; vigilar si upstream lo
   resucita. (`tools/library-inject/Makefile`)
9. **El fichero de comandos** lo puede escribir cualquier proceso del mismo
   usuario, incluidos los juegos, y permite `install`/`uninstall`/`setcompat`/
   `reloadlua`. Lo dejamos en `no` (no lo escribimos). Si algún día lo
   usamos para Proton por app, es un canal sin autenticación. (`api.cpp:150-175`)
10. **Plugins Lua sin sandbox** y hot reload sin recrear el estado: nuestro
    `Plugins: yes` (≥ 20260903114323) solo tiene sentido con plugins que
    controlemos; el directorio se fuerza a 0700. (`lua.cpp:358-359,541-554`)
11. **Notificaciones por `system()` sin escapar**: un valor de config o un
    plugin con `"` o `$(…)` en un mensaje ejecutaría shell. Nosotros no
    escribimos texto libre en claves que se notifiquen; vigilar
    `GameTitles`/`IdleStatus.Title` si alguna vez los escribimos. (`log.cpp:122,145`)
12. **`parental_settings` se borra siempre**: cualquier cuenta con control
    parental queda sin él mientras SLSsteam esté cargado; no es configurable.
    Dato para el usuario, no para nosotros. (`misc.cpp:88-104`)
13. **Hooks de `IClientAppManager` duplicados**: cada `GetUpdateInfo`,
    `BIsDlcEnabled`, etc. pasa dos veces por el hook; sin efecto funcional,
    pero cualquier medición de tiempos en esas rutas lo verá doble.
    (`hooks.cpp:1502-1514`)
14. **Sin kill-switch en caliente** (`removeAll` sin llamadores) y sin crash
    guard: el único guard del stack es el nuestro (vanilla tras N crashes).
15. **Arena de paquetes de 8×1 MB con rotación**: si Steam retuviera un
    paquete reserializado más de 8 reserializaciones, se sobreescribe. No
    tocamos la capa CM; solo importa si un plugin nuestro lo hiciera.
16. **Símbolos**: ni release ni debug van strippeados y no hay `-fvisibility`;
    nuestro parche de logros resuelve `CUser::isSubscribed`,
    `CConfig::isAddedAppId`, `g_config` y `Achievements::sendAndRecv*` por
    `.symtab`. Si el autor estrippease (el motivo del `!strip` es
    `ticket-grabber`), el parche quedaría mudo (fail-closed). (`Makefile`, `PKGBUILD:26-27`)

### 4.2 Dependencias y qué rompe si cambia

| Dependencia | Dónde | Qué se rompe |
|---|---|---|
| layout de `.text` de `steamclient.so` (18 patrones; prólogos con `__x86.get_pc_thunk`) | `patterns.cpp:51-213` | patrón ausente → aborta limpio; patrón en otro sitio (último match) → hook erróneo, crash |
| nombres RTTI mangled de 16 clases | `vftableinfo.cpp:89-361` | renombrar o compilar sin RTTI → "Failed to get … VFTable", aborta |
| índices fijos: `RecvPkt`=3, `SendAndRecv`=5, `ServerResponded`=5, `GetServerDetails`=7, `RequestInternetServerList`=0 | `vftableinfo.cpp:89-132` | un virtual nuevo delante → otra función con la misma firma, crash |
| heurística de fin de vtable (`& 0xFFFF0000`) y de typeinfo | `decompiler.cpp:53-57,343-372` | índices mal resueltos |
| cadenas `TraceIPC("IClientX","Func")` en los stubs `*Map` | `decompiler.cpp:593-648` | sin ellas no se resuelve ningún índice por nombre |
| layout IPC (`base+0` cmd, `+1` interfaz, `+2` hSteamUser, `+6` función; `EIPCCmd`, `EIPCInterface`) | `hooks.cpp:403-431`, `sdk/CSteamEngine.hpp:13-82` | FakeAppIds, VFT hooks y procesos por pipe fallan; interfaz nueva no listada ve el AppId falso |
| offsets de miembros por patrón (`m_pUser`, `m_ClientUtils`, `m_ClientUser`, `m_UserAppInfo`, `m_UserAppmanager`, `m_PipeIndex`) y "2 punteros por usuario" | `sdk/CUser.cpp`, `sdk/CSteamEngine.cpp` | punteros erróneos al colocar VMT hooks |
| structs hardcodeados: `AppOwnershipInfo_t` 0x38, `AppLicensesChanged_t` 0x114, `AppStateInfo_t`, `DepotInfo_t` 0x20, `CNetPacket`, `CProtoBufMsgBase`, `CServerPipe`, `CUtl*`, `gameserverdetails_t.appId@0x90`, `CSteamId` | `sdk/*.hpp` | datos corruptos sin crash inmediato: spoof inefectivo, licencias "expiradas", filtrado de red roto |
| convenciones de llamada (`PostCallback(…, 0)`, `SetAppIdForCurrentPipe(…, 0)`, `SendAndRecv(…, 1, …)`, `InstallApp(…, 0)`) | `sdk/*.cpp` | crash o pila corrupta |
| `libtier0_s.so` exportando `Plat_Alloc/Free/Realloc` | `sdk/steam.cpp` | "Failed to find steam exports", aborta |
| section headers presentes en `steamclient.so`/`steamui.so`/`libtier0_s.so` | `decompiler.cpp:441-465`, `memhlp.cpp:68-72` | sin `.text` no hay escaneo |
| mensajes CM (9406, 146 + `FamilyGroupsClient.NotifyRunningApps#1`, 751, 5514, 8904) y servicios por nombre (`#1`) | `hooks.cpp`, `achievements.hpp`, `apps.cpp:462` | family lock vuelve; punteros globales sin capturar; una versión `#2` desactiva la feature sin error |
| `EResult`: `Failure`=2 como "owner inválido", `NoConnection`=3 como "usa caché" | `achievements.cpp:122,188,207,268` | blacklist no se llena; reintentos hasta el cooldown |
| `store.steampowered.com/appreviews` y su JSON | `achievements.cpp:21-23,63` | sin esquema para no poseídas (caché offline) |
| GitHub raw / jsDelivr | `update.cpp:21-22` | solo con SafeMode/Warn; sin caché, fail-closed |
| binario `curl`, `notify-send`, `/proc` legible, inotify, `$HOME` | `curl.cpp`, `log.cpp`, `process.cpp`, `filewatcher.cpp` | sin descargas; sin toasts; FakeAppIds/SmartTickets con `appId=0`; sin hot reload, API ni plugins; sin log y sin hooks |
| glibc rtld-audit y que el lanzador respete `LD_AUDIT` (Flatpak: `SHARED_LIBRARY_GUARD=0`) | `main.cpp:240-292` | no se carga |
| hilo único del steamclient (sin locks en hooks; mapas globales sin mutex) | `hooks.cpp:1473-1476`, `achievements.hpp` | carreras si Valve paraleliza el IPC |
| `VERSION` presente en `updates.yaml` | `update.cpp:143-146` | builds propias nunca pasan SafeMode |

### 4.3 Lo que SLSsteam tiene y nosotros usamos o no

La tabla completa de claves que escribimos y su valor está en `nosotros.md`
§3.4 y §4.3. En resumen: usamos `AdditionalApps`, `ManifestIds`, `AppTokens`,
`FakeAppIds`, `DlcData` (> 64), `DisableCloud: no`, `DisableUpdates: no`,
`SafeMode: no`, `NotifyInit: yes`, `Plugins: yes` (≥ 20260903114323); leemos
`DenuvoGames`. No usamos `AppIds`/`UseWhitelist`, `CDKeys`, `DepotBlacklist`,
`LaunchOptions`, `SubscriptionTimestamps`, `SteamIdOverride`, `SmartTickets`
(queda en su default `1`), `FakeOffline`, `IdleStatus`, `GameTitles`,
`FakeName/Email/WalletBalance`, el fichero de comandos ni las herramientas
.NET. Lo que SLSsteam no tiene y cubrimos nosotros: claves de depot,
manifests, códigos de petición de manifest, package-0, updates por proveedor,
fixes, cloud (CloudRedirect), UI.

### 4.4 Bugs y fragilidades propias de SLSsteam (del código, no de la convivencia)

- `buildDepotDependency`: swap sin re-inspección ni `continue` (`apps.cpp:69-80`).
- `CUtlVector::swap` comprueba `index > size` en vez de `>=` (`sdk/CUtl.hpp:196`).
- `getSubscribedApps` sin deduplicar (`apps.cpp:246-250`).
- `getDlcDataByIndex` sin comprobar `index < size` (`dlc.cpp:80`); con
  `DlcData` incompleto, los DLC reales que falten desaparecen.
- `setConfigStoreString` hace `std::stoul` tras avisar de que no es número
  (`apps.cpp:623-628`): excepción posible.
- `getCachedTicket`/`getCachedEncryptedTicket` cargan YAML sin `try/catch`
  (`ticket.cpp:66-70,216-226`): un YAML corrupto en `cache/` lanza dentro del
  hook.
- `std::string(getenv("LD_LIBRARY_PATH"))` con `nullptr` (`main.cpp:141`).
- `load()` vuelve a ejecutarse entero si un módulo llegara tras una pasada
  completa (hooks duplicados); en la práctica el tercer módulo dispara una
  sola vez.
- Seis hooks de `IClientAppManager` registrados dos veces (`hooks.cpp:1502-1514`).
- `isProtoBuf()` devuelve `-1` convertido a `true` para paquetes inválidos;
  tapado por `isValid()` en los llamadores reales (`CNetPacket.hpp`).
- `CUtlRBTree::find` deref antes de comprobar `-1` (no se usa).
- `setup.sh`: `uninstall` borra otra ruta de fish; `$@` sin comillas en los
  wrappers; `make install` lo invoca con `sh` aunque usa `[[ ]]`.
- `make-releases.sh`: el paquete "Arch-debug" se compila sin que el PKGBUILD
  propague `DEBUG`.
- `docs/Lua/README.md`: typo `SLSsteam::configLoaing`; `IClientApps/User/AppManager`
  sin documentar.
- `PKGBUILD`: `optdepends` con el nombre erróneo "sls-ticket-generator";
  `validpgpkeys` sin `.sig` en `source`.

---

## §5 Historial

§5.1 recoge lo que se creía de SLSsteam y el código desmiente, además de lo
ya listado al principio del doc, y lo que sigue sin medir. §5.2 recoge las
pruebas y mediciones con fecha sobre SLSsteam mismo; las pruebas del stack
completo (SLSsteam con lumalinux y LumaDeck) están en `nosotros.md` §5.2.
§5.3 es la cronología del repositorio desde 2026-07-05.

### 5.1 Lo que se creía y ya no es así

Además de lo listado en la cabecera del doc:

- **Los logros se consiguen forzando `NoConnection` para usar las stats
  offline.** Hoy es el borrow con reseñadores; `NoConnection` queda como
  respaldo (`achievements.cpp:186-188,268`).
- **El family-share lock se salta con `patchRetn` sobre
  `FamilyGroupRunningApp` y `StopPlayingBorrowedApp`.** Hoy descarta los
  paquetes y reescribe `GetAppStateInfo` (`hooks.cpp:360-380`,
  `apps.cpp:144-172`).
- **El bloqueo de updates deja "Update required" sin bajar nada.** Era la
  v1 de 2026-07-05 (`GetAppStateInfo` limpiando flags); desde
  `20260714131044` es `GetUpdateInfo` devolviendo false bajo `DisableUpdates`
  (`apps.cpp:449-458`). lumalinux retiró su contraparche en v0.16.18.
- **`PlayNotOwnedGames` llena la biblioteca.** Clave eliminada en `84c3672`
  (2026-07-07); `AdditionalApps` es el único mecanismo.
- **`isSubscribed` no se inlinea porque no hay LTO.** LTO está activo
  (`Makefile:10`); el hecho se sostiene porque el símbolo está en `.symtab`
  de cada release (§5.2 F7).
- **El spoof de SteamID solo usa el ticket cifrado cacheado.** Hoy hay tres
  fuentes en orden (§2.6); el poblado desde el tráfico sigue.
- **`sprintf` desborda la ruta de config y `HOME` nulo es UB.** Es
  `ostringstream` desde `62afc2e`; sin `HOME`, `setup()` aborta
  (`config.cpp:22-41`, `main.cpp:109-116`).
- **El hash `0xD6FC3200` de `GetSteamID` puede moverse.** Desde `779178c`
  el spoof es un hook de vtable; el hash queda solo en `ProcessIPCFrame`.
- **La recarga no se dispara cuando LumaDeck escribe `config.yaml`.** Sí:
  `IN_CLOSE_WRITE` sobre el directorio desde `5c632dd`; el poke de 0 bytes
  funcionaba. `IN_MOVED_TO` (desde `1444fa5`) cubre además el `rename()`.
- **`library-inject.so` corrompe el despacho de excepciones y corremos
  así.** Está vacío desde `148b9a1` (2026-08-23); nunca se reprodujo.
- **La fuga de descriptores en `Curl::getString` sigue.** Cerrada en
  `cbd0cd0` (2026-07-30).
- **El plugin `download.lua` murió con wudrm el 2026-09-09.** wudrm volvió
  el 09-16 y con él el plugin.

**Lo que sigue sin medir:**

- Que la detección del `rename()` (`IN_MOVED_TO`) funcione en una Deck; en
  código sí.
- Qué efecto tiene `AppLicensesChanged_t` sobre la interfaz, y cuál de los
  dos callbacks (el de SLSsteam al añadir, el `LicensesUpdated_t` de
  lumalinux) es el que la refresca.
- `CDKeys` con una app añadida en dispositivo; en código no se inyecta
  porque `isSubscribed` es true para las nuestras.
- Que Steam fusione el esquema prestado con lo local, que es lo que el autor
  pretende al vaciar `stats` de la respuesta.
- La causa del crash de `libssl.3.so` en SteamOS que motivó el curl externo.
- La retirada del endpoint de reseñas de la tienda el 2026-10-22: dicho del
  autor en `dev`.
- La concurrencia sobre los mapas sin mutex de `achievements.cpp`; el autor
  asume un solo hilo.
- La forma de bytes del `.so` de release: compilarlo no es reproducible, así
  que las anclas del parche de logros se verifican sobre `.symtab` y la
  fuente, no sobre el binario publicado.

### 5.2 Pruebas y mediciones con fecha

"Dónde": binario (inspección de un `.so` publicado), repo (historia del
repositorio), Deck, codespace, red,
Discord (segunda mano).

#### Función 1 — Engancharse a Steam

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-08-23 | shell | `LD_AUDIT` con un `.so` de 0 bytes, como queda `library-inject.so` | `ld.so: object '…/empty.so' cannot be loaded as audit interface: file too short; ignored`, salida 0: la entrada se ignora y el resto del `LD_AUDIT` carga | `nosotros.md` §2.1 |
| 2026-09-17 → 10-07 | red (feed `res/updates.yaml`, tracker `swwayps/steam-monitor`) | Qué clientes tienen hash en el feed | Último hash el 3-sep (`237495b4`, versión `1788400362`); el stable de escritorio del 5-sep (`1788652215`) lleva el mismo `steamclient.so` (el tracker cambió solo la versión el 09-18); Deck en `1788291500` (`bc54101b`) desde el 2-sep. Ninguna beta posterior (18-sep a 6-oct) con hash. `res/version.txt` sigue en `20260903114323` | §2.1 |
| 2025-12-20 → 2026-07-25 | historial git | Commits que tocan patrones | 20-12-2025 "Fix for latest stable Steam client version" (12 líneas + hooks); 10-03 (6, grupo `20260310103750`); 28-05 (3, `20260528151547`); 30-06 "Add more wildcards" (2); 22-07 (5, `20260722152506`, build `dd3ca9f5`); 25-07 "Refine patterns" + `VFTIndexes` (9). Nada después hasta el 09-14 | lumalinux, en la misma ventana: 0 (`nosotros.md` §5.2 F1) |

#### Función 2 — Propiedad y licencias

Las pruebas en vivo son del stack completo (`isSubscribed` true para los
juegos añadidos, qué concede `AdditionalApps` frente al package-0, aparición
y retirada en caliente): `nosotros.md` §5.2 F2.

#### Función 3 — DLC

Sin pruebas propias; las de DLC de juegos poseídos están en `nosotros.md`
§5.2 F3.

#### Función 4 — Claves y manifests

SLSsteam no interviene. El plugin `download.lua` (códigos por wudrm, en
claro) se mide en `slssteam-plugins.md`.

#### Función 5 — Updates de juegos

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-07 | binario `SLSsteam.so` `20260705132808` | Codegen del bloqueo de updates v1 | `-flto=auto -O3` colapsa seis `&=` en un solo `and dword [reg+disp], 0xFFFFF8E5` (`~0x71A`); con el prefijo `81 /4` el ancla `E5 F8 FF FF` casa exactamente una vez. Base del contraparche de lumalinux (probado el 07-07, `nosotros.md` §5.2 F5) | lumalinux `RESEARCH.md` §16.2 |
| 2026-07 | Discord (issue #20 de LumaDeck, segunda mano) | Síntoma del bloqueo v1 | "Update required" sin bajar nada; `UseWhitelist: yes` global rompía el unlock y los DLC (caso Cuphead) | §2.5 |
| 2026-07-23 | repo (`20260723102618`) | ¿Queda el ancla v1? | `0xFFFFF8E5` ya no aparece en la fuente; el bloqueo es el hook de `GetUpdateInfo` con `DisableUpdates: yes` por defecto | §2.5 |

#### Función 6 — Fixes y DRM

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-08-22 | Ace (commit `2febc71`, segunda mano) | Coste del análisis de ejecutables en `ConnectPipe` | Menos de 10 ms para SteamDRM, de 20 a 2000 ms para Denuvo en su SSD | §2.6 |

#### Función 7 — Logros, stats y tiempo de juego

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-07-11 | Deck, SLSsteam `d35a697` (`20260710192125`) | Anclas del parche de logros de lumalinux | Símbolos y patrón del guard casan | `nosotros.md` §2.7 |
| 2026-07-23 | binario (`.symtab` de `20260723102618`) | Símbolos que necesita el parche | `_ZN5CUser12isSubscribedEj`, `_ZN7CConfig12isAddedAppIdEj`, `g_config`, `Achievements::sendAndRecvGetUserStats` y `…GetPlayerStats` presentes; en cada una de las dos funciones un solo `call isSubscribed; add esp,imm; test al,al; jne`. Las releases no van strippeadas (`Makefile`, `PKGBUILD` `!strip`) | §2.7 |
| 2026-07-20 → 08-17 | binario, release a release | Cambio de firma de `sendAndRecvGetUserStats` (`uint32_t` → `EMsg`, `59f8259`) | `20260710192125` y `20260714131044` con `…S3_j`; `20260722152506` es la primera con `…S3_4EMsg`; `20260723102618` y `20260728212859` también. El parche de lumalinux quedó mudo desde la primera; se detectó con `20260728212859` instalada en la Deck | `nosotros.md` §5.2 F7 |
| 2026-08-29 | repo (`dev@3f8e429`, fuente) | Supervivencia de las anclas | Los cinco símbolos siguen (ni inline ni virtual); un solo `call isSubscribed` por función | §2.7 |
| 2026-08-04 | Ace (commit `3b97ac2`, segunda mano) | Tartamudeo con el borrow | Algunos juegos llaman a `GetUserStats` sin parar y cada llamada pedía reseñadores; `preferredOwners` recuerda el dueño que funcionó | §2.7 |

#### Función 8 — Cloud saves

Sin pruebas propias.

#### Función 9 — Añadir y quitar un juego

Sin pruebas propias; aparición y retirada en caliente en `nosotros.md` §5.2
F2 y F9.

#### Función 10 — Credenciales y proveedores

Sin pruebas propias.

#### Función 11 — Mantenimiento propio

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-08 | red | La URL de `config_default.hpp` en rama `master`, que no existe | GitHub redirige y devuelve 200 con el mismo contenido que `main` | §2.11 |
| 2026-08-11 | repo (`git log -S"LogLevels:"`) | Cuándo nace `LogLevels` (plural) | `b5b1315`, 2026-08-11, tras los flags del 08-09 y el test de máscara del 08-10 | §2.11 |
| 2026-08-15 | repo (commit `665fc8c`) | API local: el primer comando se ejecutaba dos veces | `echo >` dispara dos eventos inotify y el stream se reabría sobre uno abierto; corregido; afecta a versiones anteriores a `20260815201341` | §2.11 |
| 2026-09-14 | ecosistema | Quién usa el fichero de comandos | Único uso encontrado: el modo experimental de ASSella (`install\|appid\|0` tras DepotDownloaderMod) | §2.11 |
| 2026-09-22 | repo (`diff`) | `res/config.yaml` frente al YAML embebido en `config_default.hpp` | Idénticos byte a byte; el `.hpp` sale del repo ese día y LumaDeck pasa a leer `res/config.yaml` | §2.11 |
| 2026-10-07 | repo (`git show <tag>:res/version.txt`), Deck | Versión embebida de las dos últimas releases | `20260930144343` y `20261001163836` llevan `20260903114323`: el gate de SafeMode usa esa entrada (hashes `bc54101b`, `237495b4`). En la Deck, `.slssteam.version` = `20261001163836` y el log arranca con `SLSsteam (main -> 42568f0)` | §2.11 |
| 2026-10-08 | repo | Tamaño | 11 972 líneas en `src/` sin los protobufs generados | §0 |
| 2026-07-22 → 09-02 | historial git | Retraso del hash en `res/updates.yaml` frente a nuestro whitelist | 07-22 el mismo día, 07-25 tres días, 07-28, 08-04 y 09-02 el mismo día; commit a mano agrupado por versión de SLSsteam | nuestro PR automático tarda segundos (`nosotros.md` §5.2 F11) |

#### Función 12 — Proyecto

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-17 / 09-28 | GitHub (issues) | Issues abiertas y cerradas | #157 (crash con SamRewritten, bisecado a `d056fda`, arreglado por `bbe1e3f`) cerrada el 8-sep con #136 y #124; #155 (cliente del 1-sep) cerrada el 4-sep; #158 (EINTR en el filewatcher) abierta y arreglada en `6956e2b`; #159 (horas y logros al comprar) y #156 (LuaHook) abiertas; #46 (nov-2025, FakeAppIds rompe tickets y logros) sigue | §5.3 |

### 5.3 Cronología (desde 2026-07-05)

**Topología.** `origin/main` empieza en un commit raíz **sin padre**
(`22f4880`, 2026-07-21, "initial function parser": snapshot de 739 ficheros).
`git merge-base 20260715200441 origin/main` está vacío: los tags
`20250510134347` … `20260715200441` y el tag `update` son huérfanos y siguen
en el remoto. La historia desde 07-05 está en tres rangos: la historia
vieja `20260705132808..20260715200441` (70 commits), `origin/main` completo
(397 commits, 07-21 → 10-06: 99 en julio, 250 en agosto, 44 en septiembre, 4
en octubre) y la rama `update` (`22f4880..update`, 18 commits, 17 patches que
no están en `main`). `git diff --stat 20260715200441 22f4880`: 63 ficheros,
+10 095/−922 (la raíz nueva = la vieja + decompilador + protobufs completos).

**Tags** (`YYYYMMDDHHMMSS`; en negrita los que suben `res/version.txt`, es
decir, las claves de `updates.yaml`):

| Tag | Fecha | Commit | Rama | `version.txt` |
|---|---|---|---|---|
| 20260705132808 | 07-05 | `5c632dd` | huérfana | 20260624075231 |
| **20260705144737** | 07-05 | `da97d11` | huérfana | 20260705144737 |
| **20260710192125** | 07-10 | `9bc0abd` | huérfana | 20260710192125 |
| 20260711162202 | 07-11 | `f62d97c` | huérfana | = |
| **20260714131044** | 07-14 | `c69d502` | huérfana | 20260714131044 |
| 20260715200441 | 07-15 | `7d62c68` | huérfana | = |
| **20260722152506** | 07-22 | `2903ef8` | main | 20260722152506 |
| 20260722175657 | 07-22 | `fa8523f` | main | = |
| `update` | 07-22 | `09aaee0` | experimental | = |
| 20260723094105, 20260723102618 | 07-23 | `4dd592c`, `69594b9` | main | = |
| **20260728212859** | 07-28 | `22b49e7` | main | 20260728212859 |
| 20260728235649, 20260730132828, 20260801163409 | 07-28 … 08-01 | `8da82c2`, `a673d04`, `2281333` | main | = |
| **20260815201341** | 08-15 | `c73e40b` | main | 20260815201341 |
| 20260819120840, 20260819131545, 20260820085507 | 08-19/20 | `a7ac6ed`, `d754e65`, `6546411` | main | = |
| **20260903114323** | 09-03 | `11f0970` | main | 20260903114323 |
| 20260930144343 | 09-28 (el nombre dice 09-30) | `39822da` | main | = (**no sube**) |
| 20261001163836 | 10-01 | `42568f0` | main | = (**no sube**) |

22 tags en 89 días; 5 parejas el mismo día (hotfix); huecos máximos 14 d y
25 d. El `pkgver` del PKGBUILD va siempre un tag por detrás (20 commits
"chore(PKGBUILDs): Update"). Hashes de cliente añadidos sin release: 07-25,
07-27/28 (PRs #146/#147), 08-04, 09-02, 09-03.

**Cambios de comportamiento, por mes** (hash, fecha):

- *Julio, historia vieja*: `6e8d357` 07-06 nace el borrow de esquemas
  (`GrabSchemas`, scrapeando perfiles); `4ab84f7` 07-07 → `MaxSchemaTries`,
  `NoConnection` como fallback; `84c3672` 07-07 **elimina `PlayNotOwnedGames` y
  `AutoFilterList`**; `4f3e607` 07-07 hook `CAPIJob::SendAndRecv` y
  reseñadores por protobuf; `0d6b3a8` 07-08 `ownerBlacklist`; `e2b9abf` 07-08
  `checkAppOwnership` solo `isAddedAppId`; `f62d97c` 07-11 **curl externo**
  (SteamOS crasheaba en libssl); `544bb9e` 07-12 bloqueo de updates vía
  `DisableUpdatesUntil` → `9f3cb72` clave `DisableUpdates` → `94e7ab9` hook
  de `BuildDepotDependency` (quita el de ConfigStore: bloqueaba el Workshop)
  → `ce38634` **`ManifestIds`** → `6906335` 07-13 **`GetUpdateInfo → false`**
  ("GetAppState is bugged"); `c64b4fe` 07-12 `AppIds` con herencia por
  `parent`; `4728a2e` 07-13 (exefer) **filtra `ClientGetLastPlayedTimes`**;
  `afddf3e` 07-13 **`DepotBlacklist`**; `1ab3438`/`e54afaa`/`ace2505` 07-15
  SafeMode con jsDelivr, cuerpo vacío rechazado, `--connect-timeout 15`;
  `7d62c68` 07-15 recursión infinita `parent`↔hijo.
- *Julio, historia nueva*: `22f4880`/`9dc673d` 07-21 decompilador con xrefs;
  `a2fdadc` 07-22 `DumpClientInterfaces`; `d0888d0`…`2683e94` 07-22 los
  `RunIPCFrame` de interfaces sustituidos por `CSteamEngine::RunInterface`;
  `2903ef8` 07-22 primer tag nuevo; `fa8523f` SteamOS colgado en el hook naked
  de `GetSteamID`; `8de3384` 07-24 **fuera los cinco `RunIPCFrame`**,
  `placeVFTHooks` único desde `RunInterface`, instancias navegadas por
  offsets de patrón; `3e9dfdd`/`807e229` 07-25 análisis de vtables **antes de
  relocaciones**; `d2f94c8` 07-25 patrones cortos + `PrologueUpwards`;
  `3620dd2` 07-25 el spoof de una vez manda sobre el ticket cifrado
  (SteamStub multicuenta); `293eb93` 07-26 nunca spoofear en pipe 0 ni con
  FakeAppIds; `7663aef` 07-26 "Redacted" para `PrivateApps`; `c4152f5` 07-27
  `GetSteamID` interceptado en IPC (`0xD6FC3200`); `3250c2c`/`6d1c7fd` 07-27
  **SteamID de 64 bits** y nuevo formato de tickets; `12a8e2f` 07-27 feed
  solo con SafeMode/Warn; `3c520bd`…`3b2e0d8` 07-28 saga del ticket cifrado
  (hook `GetEncryptedAppTicket` sin colocar: "too unreliable"); `f7926b5`
  **`SteamIdOverride`**, `4390e1a` **`FakeName`**, `c61df24` `DenuvoGames` a
  64 bits; `cbd0cd0`/`a673d04` 07-30 fds de curl y buffer 8192; `146e28f`
  07-30 **`AppLicensesChanged_t` solo para apps nuevas**; `284115b` 07-31
  aborta si no puede crear el directorio de config; `b8ef92f` hook en
  `CConfigStore`.
- *Agosto (250 commits)*: `2281333` 08-01 `region[4]`; `46b6ff5` VFT prueba
  todas antes de abortar; `3b97ac2` 08-04 `preferredOwners` (stutter por spam
  de `GetUserStats`); `77f5d44` 08-05 `RunInterface` → `ProcessIPCFrame`;
  `50c439a`/`50eed0d` callbacks y lobbies con FakeAppId; `a2edc13` 08-06
  allocators de `libtier0_s.so`; `6e8e8de`/`4b29945` 08-07 el SDK puede enviar
  y falsificar mensajes, protobuf completo; `9ce2650` 08-08 **AppId real de
  `/proc/<pid>/environ`**; `e3ed672` `DEBUG=1` (sin él `once/debug` se
  eliminan); `ebf28b4`…`b5b1315` 08-09→11 **`LogLevels` bitflags**,
  `Notifications` eliminada; `79905b0` 08-12 **`CDKeys`** (antes solo
  deshabilitaba CD keys; antes de eso "no teníamos decompilador y la VFT de
  IClientUser cambiaba mucho"); `3d7b17f` 08-12 `postCallback` por patrón
  (un "forgotten test" nunca publicado); 08-14 **API**: `setcompat`,
  `getcompat`, `dumpcompat`, `dumplibraries`, `docs/API/README.md`, nivel
  `API`; `665fc8c` 08-15 la API no ejecuta dos veces el primer comando;
  `182fdf0` Makefile por hash de flags; `1444fa5` **`IN_MOVED_TO`**;
  `aa27169` 08-17 el feed debe empezar por `SafeModeHashes:`; `6546411`
  08-20 **firma correcta de `IsUserSubscribedAppInTicket`**; `7186151` API
  multilínea; `5882bed` 08-21 **oculta "family shared" a los juegos**
  (`GetAppStateInfo`); `d056fda`…`de8d555` 08-21 parsers PE/ELF y
  **`SmartTickets`**; `0fec99d`…`2febc71` 08-22 **Denuvo por entropía**,
  `SmartTickets` a bits (default `0x1`); `79a67ac` **cooldown de 10 min**;
  `1bbcbc8` **`LaunchOptions`**; `148b9a1` 08-23 **`library-inject.so`
  vacío**; `1f10312` no crashea con config malformada; 08-24→28 **API de
  plugins Lua** (`0420645` LuaHook/memhlp/log, `07be566` callbacks,
  `f4a6b9f` `VFTableInfo_t`, `a17692e` watch de `plugins/`, `2432a37`
  **`Plugins: no`**, `85dac66` `reloadlua`, `f801adb`/`f497d6d` la config se
  recarga en cada reload Lua, `5d504ee` `configLoading`, `407b572` estado
  recreado solo en DEBUG, `f03ad94` no trackear altas antes de
  `GetSubscribedApps`); `913a431` 08-29 refactor de `Hooks` (herencia,
  `place/remove`); `32bb863`…`3a3ee4a` 08-31 LuaBridge moderno **con roturas
  de API** (`SLS.alloc/realloc/free` y `getUserDataPtr` fuera,
  `downloadStringWithHeaders` fusionado).
- *Septiembre (44)*: `ac52879` 09-01 `unpack_user_data`; `eafd3ea` 09-02
  **ruta de `.updates.yaml` corregida**; `3d45b14` 09-03 **0700 forzado en
  plugins**; `11f0970`/`71021ad` 09-03 tag + hash 09-03; `a456396` 09-03
  **bypass de controles parentales** (siempre); `bbe1e3f` 09-08 `/proc`
  inaccesible no crashea; `6956e2b`/`e6082d0` 09-12 filewatcher con `EINTR`
  (issue #158); `003f8f0`/`0cad406` 09-16/17 `CNetPacket` vuelve a la arena
  estática ("freeing the oldBody" crasheaba); 09-20 `Process::init`
  endurecido (`3ca4b73`, `ed347d7`, `e7e54f7`, `1298aa8`, `4a24b69`) y
  **`strsplit` por regex** (`2a538fd`, rompe la API); `ae5cbf1` 09-22 rama y
  commit en el log, `version.hpp`/`config_default.hpp` fuera de git;
  `6d9ad4e` 09-24 `CheckAppOwnership` devuelve `bool`; `779178c` 09-28
  **`GetSteamID` como VFT hook real** (3vil3vo); `39822da` spoof tras
  `GetEncryptedAppTicket` solo con `SmartTickets & Denuvo`.
- *Octubre (4)*: `e84bbb8` 10-01 `git` en el Dockerfile; `42568f0` 10-01
  **arregla la API** (`strsplit(cmd, "\\|")`): rota entre `20260930144343` y
  `20261001163836`; `9c829a7` PKGBUILDs; `049bbdd` 10-06 README sin "ADS".
  En `dev` tras el merge (no publicado): `07dc57e`…`b92dd53` 10-02 offsets por
  RTTI (`searchOffsetByTypeName`) en vez de tres patrones; `51724f5` 10-03
  **endpoint de reseñas** → `api.steampowered.com/IUserReviewsService/GetAppReviews/v1/`
  ("Store endpoint gets deprecated on 2026.10.22"); mensajes de patrón a
  WARN/ERROR; `__attribute__((hot))` en `ProcessIPCFrame`.

**Claves del YAML cambiadas en el periodo.** Añadidas: `MaxSchemaTries`
(07-07), `DisableUpdates` y `ManifestIds` (07-12), `DepotBlacklist` (07-13),
`DumpClientInterfaces` (07-22), `SteamIdOverride` y `FakeName` (07-28),
`LogLevels` (08-11), `CDKeys` (08-12), `SmartTickets` (08-21; bits 08-22),
`LaunchOptions` (08-22), `Plugins` (08-26). Eliminadas: `PlayNotOwnedGames`,
`AutoFilterList`, `GrabSchemas` (07-07), `Notifications`, `LogLevel` (08-11).
Tipos: `DenuvoGames` a SteamID64 (07-28), `SmartTickets` bool → bits (08-22).

**La rama `update` (no mergeada), qué prometía.** `7949cbc` **`AdditionalPackages`**
(licencias inyectadas en `CMsgClientLicenseList` 780 con `owner_id = cuenta`,
`license_type = SinglePurchase`) y **`DepotKeys`** (inyectados en
`CMsgClientGetDepotDecryptionKeyResponse` 5439; retirado en `6f53256`);
`c5584c3`/`4b50593` **"manifest request code exploit"** (hook de `GetManifest`
que pone `depotId = 1` en la petición cuando el app no es poseído o está en
`AdditionalApps`); `b3bc81e` **`AdditionalDepots`** (en `CPackageInfo` del
package 0 vía `CPackageInfoCache::GetPackageInfo`); `6b06a31` hot reload de
packages con `CUser::ProcessLicenseList` + `LicensesUpdate_t`;
`53ba171`…`1c80127` **`InventoryItems`**; `fc09662` tickets falsos para
SteamStub ("Exploit from OpenSteam001": ticket extendido del app 7 con el
appId objetivo empalmado delante de la firma). Nada de esto está en `main`
ni en los releases: el consumidor **no** puede contar con inyección de
licencias, depots o códigos de manifest en SLSsteam. Si siguen vivos en `dev`
no se ha comprobado.

**Hitos que tocan a nuestro stack** (detalle en `nosotros.md` §5.2):
`5c632dd` (07-05) reescribió el filewatcher (directorio padre +
`IN_CLOSE_WRITE`), puso comodines en los patrones y añadió `uninstall` a la
API; el contraparche `sls_update_unblock` de lumalinux se probó de extremo a
extremo el 07-07 contra el bloqueo v1 y murió con `20260714131044`; el cambio
de firma `uint32_t → EMsg` en `sendAndRecvGetUserStats` (`59f8259`, 07-20)
dejó mudo seis días al parche de logros y se detectó con `20260728212859`; la
escritura rasgada del `rel32` de ese mismo parche tiró Steam en Game Mode
hasta el OOBE; issue #157 (crash con SamRewritten, bisecado a `d056fda`,
arreglado por `bbe1e3f`), #158 (EINTR), #159 (horas y logros al comprar),
#156 (LuaHook), #46 (FakeAppIds rompe tickets y logros).
