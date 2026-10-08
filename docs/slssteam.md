# SLSsteam — el unlocker de AceSLS, leído desde el código

Relectura completa desde el código, 2026-10-08. Sustituye a
`slssteam-analysis.md` (4319 líneas, barridos por fecha desde 2026-07-06):
sus 223 mediciones con fecha están en §5.2 tal como estaban escritas, sus 84
afirmaciones sin medición están contrastadas en §5.1, y su historia por
releases está condensada en §5.3. `slssteam-plugins-analysis.md` (los plugins
Lua como programas aparte) sigue siendo su propio doc. Cómo **nosotros**
configuramos y parcheamos SLSsteam está en `nosotros.md` (§2 por función y
§4.3); aquí se describe SLSsteam tal como es.

Formato: el de todos los docs de programa (ver `ecosystem-matrix.md`). §0 ficha,
§1 mapa del código, §2 las doce funciones con la lista de control contestada,
§3 superficies externas, §4 lo que nos afecta, §5 historial.

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

- **`isSubscribed` queda cerrado** (la inconsistencia I1 del doc viejo): `CUser::isSubscribed` llama al **trampolín** de `CheckAppOwnership`, es decir, a Steam sin el spoof, y devuelve `ownsLicense && !licenseExpired` (`sdk/CUser.cpp:34-46`). Para un juego solo añadido a `AdditionalApps` es **false**; para uno con la inyección de package-0 de lumalinux es **true** porque el propio Steam lo cree poseído (medido en la Deck, `nosotros.md` §5.2 F7). Las dos posturas del doc viejo eran ciertas en contextos distintos.
- SLSsteam **no tiene almacén de logros**: solo consigue el esquema pidiendo las stats de reseñadores recientes y vacía los valores de la respuesta; quien guarda es Steam (`appcache/stats`). `nosotros.md` §2.7 decía "desde su almacén local"; corregido.
- **No hay límite de 64 DLC en el código**: `DlcData` es la forma de suplir la lista que Steam no entrega; ni `dlc.cpp` ni `config.cpp` imponen ni mencionan 64. El "límite" es de Steam.
- **No inyecta packages ni licencias**: el spoof es por app en `CheckAppOwnership`/`GetSubscribedApps`. La inyección de licencias, depots, claves de depot y el "exploit" de códigos de manifest viven solo en la rama `update`, nunca mergeada (§5.3).
- **`AppLicensesChanged_t` solo en caliente**: en el arranque no se postea nada; solo al añadir (tras la respuesta PICS, lotes de 15) o quitar apps de `AdditionalApps` con Steam en marcha, y solo después de que Steam haya llamado a `GetSubscribedApps`.
- **La API de fichero está apagada por defecto** (`API: no` en el YAML que se instala; el default en código es `true` si falta la clave) y estuvo **rota** entre los tags `20260930144343` y `20261001163836`.
- **`res/version.txt` no ha subido** en los dos últimos tags (sigue en `20260903114323`) y no hay hashes posteriores al cliente del 2026-09-03: con `SafeMode: yes` cualquier `steamclient.so` más nuevo aborta. Nosotros lo llevamos a `no`.
- **La caché offline de SafeMode no funcionó hasta el 2026-09-02** (`eafd3ea`): `path::append("/.updates.yaml")` sustituía la ruta por `/.updates.yaml`.
- **`library-inject.so` es un fichero de 0 bytes** desde `148b9a1` (2026-08-23) y sigue en todos los `LD_AUDIT` upstream; glibc lo ignora con aviso.
- Los hooks son **37** (tabla en §2.1); el doc viejo hablaba de "los `RunIPCFrame` de 7 interfaces", mecanismo muerto desde 2026-07-24.
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

### 4.1 Hallazgos de la relectura (pendientes de decisión)

Aparcados hasta terminar las relecturas, por orden del usuario (2026-10-08).
Cada uno con dónde está en el código y qué nos toca; ninguno se ha actuado.

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
   UI (`nosotros.md` A11 / §5 F2). (`apps.cpp:274-381`, `config.cpp:329-364`)
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

### 5.1 Contraste con el doc anterior

`slssteam-analysis.md` hacía 84 afirmaciones sin medición (S1-S84 en la
extracción) y arrastraba 32 inconsistencias internas (I1-I32). Cada
afirmación, contrastada con el código leído: **confirmada**, **caducada** (ya
no es así), **sin evidencia** (ni código ni medición la sostienen). Las
inconsistencias que el código decide se cierran aquí; las demás eran del doc
y mueren con él.

| # | Afirmación (resumida) | Veredicto | Evidencia |
|---|---|---|---|
| S1 | modifica estructuras y mensajes para no poseídos, family share, DLC, AppId, tickets offline, cosméticos | confirmada | §2.2, §2.3, §2.6, §3.5 |
| S2 | no hace polling: el loader le avisa | confirmada | `main.cpp:245-287` |
| S3 | solo sigue en `steam`; en otros "hace `unload()` y se va" | confirmada con matiz: `unload()` es un no-op, simplemente no hace nada | `main.cpp:74-107` |
| S4 | hosts de 64 bits rechazan auditores de 32 | sin evidencia aquí (no es cosa de SLSsteam) | — |
| S5 | `PrologueUpwards` hasta 0x10000 bytes | confirmada | `memhlp.cpp:181-212` |
| S6 | un detour cerca de un thunk IPC "podría corromperse" | sin evidencia | — |
| S7 | tickets en `~/.config/SLSsteam/cache/*.yaml`, reproducidos offline | confirmada | `ticket.cpp:36-45,124-135` |
| S8 | logros: fuerza `NoConnection` para usar stats offline | caducada: hoy es el borrow con reseñadores; `NoConnection` solo como fallback | `achievements.cpp:186-188,268` |
| S9 | FakeAppId mapea real↔falso por pipe | confirmada | `fakeappid.cpp` |
| S10 | `patchRetn` sobre `FamilyGroupRunningApp`/`StopPlayingBorrowedApp` | caducada: hoy descarta paquetes y reescribe `GetAppStateInfo` | `hooks.cpp:360-380`, `apps.cpp:144-172` |
| S11 | no toca juegos Denuvo de otra cuenta | confirmada | `apps.cpp:102-110` |
| S12 | sin red cae a `.updates.yaml` | confirmada (ruta correcta solo desde `eafd3ea`) | `update.cpp:94-127` |
| S13 | settings en `mtvar`, los hooks leen en caliente | confirmada | `mtvar.hpp`; `.copy()` en cada hook |
| S14 | con ≥ 20260815201341 el rename se detecta solo | confirmada en código (`IN_MOVED_TO`); on-device no medido | `filewatcher.cpp` |
| S15 | SLSsteam "abre `/tmp/SLSsteam.API`" (como si siempre activo) | caducada: el fichero se crea siempre, los comandos solo con `API: yes`; el YAML instalado lleva `no` | `api.cpp:26-29,150-175` |
| S16 | escribe la config solo si no existe; toastea claves faltantes; reinstalar no lo arregla | confirmada | `config.cpp:58-84,309-319`; `setup.sh` no copia config |
| S17 | headcrab completa la config | fuera de alcance (COEX); sin evidencia aquí | — |
| S18 | engancha "los `RunIPCFrame` de 7 interfaces" | caducada: ningún `RunIPCFrame`; 37 hooks (§2.1) | `hooks.cpp` |
| S19 | `AppLicensesChanged_t` fuerza a Steam a re-indexar | sin evidencia (solo se postea; el efecto no está medido) | `apps.cpp:274-321` |
| S20 | el mecanismo `GetAppStateInfo` de 20260705 "funciona para todos" | caducada (mecanismo retirado) | §2.5 |
| S21 | updates: "Update required" sin bajar nada (v1) | caducada: hoy `GetUpdateInfo` false con `DisableUpdates` | `apps.cpp:449-458` |
| S22 | `PlayNotOwnedGames` llena la biblioteca | caducada (clave eliminada `84c3672`) | §3.4 |
| S23 | juegos modernos no llevan `hadthirdpartycdkey` | sin evidencia | — |
| S24 | `CDKeys` cubre added apps porque `isSubscribed` lee propiedad real | confirmada en código; on-device no | `apps.cpp:177-180`, `sdk/CUser.cpp:37-46` |
| S25 | inyectar la clave solo en `Get` no funcionaba | confirmada como comentario del autor | `apps.cpp:226-228` |
| S26 | Steam mergea el esquema prestado con lo local | sin evidencia propia (es lo que el autor pretende al vaciar `stats`) | `achievements.cpp:132-133,218-220` |
| S27 | LumaDeck escribe licencia real → `isSubscribed` true → el guard salta | **confirmada y reconciliada con S24/I1**: `isSubscribed` va por el trampolín (propiedad real según Steam); con la inyección de package-0 de lumalinux Steam la cree poseída (medido `sub=1 added=1`) | `sdk/CUser.cpp:34-46`; `nosotros.md` §5 F7 |
| S28 | sin licencia local → `isSubscribed` false → el borrow corre | confirmada (lógica) | `achievements.cpp:154,236` |
| S29 | un NOP del guard secuestraría la petición real | sin evidencia | — |
| S30 | SteamOS `libssl.3.so` crashea al curlear | confirmada como motivo del autor; causa no verificada | `curl.cpp:7-9` |
| S31 | el decompilador añade segundos al arranque | sin evidencia (nota de release) | — |
| S32 | los métodos del cliente referencian su nombre como string | confirmada (`TraceIPC` en los stubs `*Map`) | `decompiler.cpp:593-648` |
| S33 | leer el `.so` de memoria daría un binario parcheado | sin evidencia (motivo deducido) | — |
| S34 | `DisableUpdates: no` anula el bloqueo en caliente | confirmada en código | `apps.cpp:451` |
| S35 | no reescribe los settings del usuario | confirmada | `config.cpp:58-84` |
| S36 | `isSubscribed` no se inlinea "sin LTO" | caducada como argumento (LTO activo); el hecho lo sostienen M62/M181 | `Makefile:10` |
| S37 | no hay razón para que la forma de bytes derive | sin evidencia | — |
| S38 | se publica sin strippear | confirmada por `Makefile` y `PKGBUILD`; el tarball no inspeccionado | `Makefile`, `PKGBUILD:26-27` |
| S39 | `retf` es no-op defensivo | sin evidencia | — |
| S40 | `.text` nunca es lo último del mapeo | sin evidencia | — |
| S41 | prólogos se mueven "constantemente" | sin evidencia (opinión) | — |
| S42 | interfaces embebidas (`this + offset`) | confirmada | `sdk/CUser.cpp:15-28` |
| S43 | post-relocación los slots son direcciones absolutas | confirmada (por eso parsea antes) | `main.cpp:251-255`, `decompiler.cpp:59` |
| S44 | el spoof solo con ticket cifrado cacheado, poblado del tráfico | parcialmente caducada: hoy hay tres fuentes en orden (§2.6); el poblado del tráfico sí | `hooks.cpp:1113-1159`, `ticket.cpp:301-305` |
| S45 | `sprintf` desborda; `HOME` nulo es UB | caducada: `ostringstream`; sin `HOME`, `setup()` aborta | `config.cpp:22-41`, `main.cpp:109-116` |
| S46 | `0xD6FC3200` puede moverse | caducada para el spoof (VFT hook desde `779178c`) | `hooks.cpp:1092-1160` |
| S47 | SafeMode `no` por defecto, trabajo tirado | confirmada | `config.cpp:181`, `update.cpp:25-30` |
| S48 | dos juegos podían consumir el spoof del otro | sin evidencia | — |
| S49 | fd filtrado por fetch de esquema | caducada (`cbd0cd0`) | §5.3 |
| S50 | "Add Game" emite `AppLicensesChanged_t` | confirmada en código solo con Steam abierto y tras `GetSubscribedApps`; cuál callback refresca la UI, sin medir | `config.cpp:337-359` |
| S51 | índice de `Map` aplicado a la impl "funciona en la práctica" | sin evidencia | — |
| S52 | callbacks al real que el juego no recibía | sin evidencia (internos de Steam) | — |
| S53 | Steam asigna por `Plat_*` | sin evidencia (inferido) | — |
| S54 | `isProtoBuf()` invertido es cosmético | confirmada (tapado por `isValid()`) | `CNetPacket.hpp` |
| S55 | `/proc/<pid>/environ` legible en SteamOS | sin evidencia (solo el comentario del YAML) | `config.yaml:59` |
| S56 | `/proc` inaccesible no crashea | confirmada desde `bbe1e3f`/`e7e54f7` | `process.cpp` |
| S57 | migración de `Notifications` a `0x3F` | sin evidencia; la clave hoy se ignora | §3.4 |
| S58 | cada recarga volcaba la config entera | sin evidencia | — |
| S59 | `PostCallback` vs `PostCallbackToAppId` indeterminable | sin evidencia | — |
| S60 | `setcompat` exige `API: yes`; lo escribe cualquier proceso del usuario | confirmada | `api.cpp:26-29,152` |
| S61 | `CUtlRBTree::find` lee `elements[-1]` con árbol vacío | confirmada como bug latente; sin uso | `sdk/CUtl.hpp:99-128` |
| S62 | con < 20260815201341 la recarga no se dispara al escribir LumaDeck | caducada/contradicha (I4): `IN_CLOSE_WRITE` sobre el directorio desde `5c632dd`; el poke funcionaba | `filewatcher.cpp` |
| S63 | con `SafeMode: yes` y otro build que el 08-04, aborta | confirmada la mecánica; la lista de hashes es otra hoy | `update.cpp:143-152` |
| S64 | la firma mala de `IsUserSubscribedAppInTicket` daba basura; no afecta a instalar | confirmada la corrección (`6546411`); consecuencias sin evidencia | §2.3 |
| S65 | solo reporta claves faltantes | confirmada | `config.cpp` |
| S66 | `SmartTickets` analiza en `ConnectPipe` | confirmada | `hooks.cpp:471-480` |
| S67 | juegos que se atragantan con `NoConnection` | sin evidencia (motivo del autor) | `79a67ac` |
| S68 | corremos con el despacho de excepciones corrompido por `library-inject` | caducada: vacío desde `148b9a1`; nunca se reprodujo | `tools/library-inject/Makefile` |
| S69 | `SLSsteam::initialized` es el único punto sin patrón | confirmada (se dispara tras `Hooks::init`) | `hooks.cpp:1572` |
| S70 | el gate de plugins está en ejecutar, no en montar | confirmada | `lua.cpp:560-563` |
| S71 | `cf63d67` descartaba altas pendientes | sin evidencia (hipótesis) | — |
| S72 | dos callbacks de refresco por coincidencia | sin evidencia (COEX) | — |
| S73 | `f932133` sin relación con nuestro `LD_PRELOAD` | sin evidencia | — |
| S74 | `download.lua` muerto desde el 9-sep | caducada (volvió el 16) | doc de plugins |
| S75 | stable `1788652215` sin cambio (inferencia) | caducada (resuelta en el propio doc, I18) | M201 |
| S76 | `strtok` partía `cmdline` por NUL de casualidad | sin evidencia | — |
| S77 | analiza cada proceso que abre un pipe; un `/proc` raro tiraba Steam | confirmada | `hooks.cpp:471-480`, `e7e54f7` |
| S78 | el endpoint de tienda muere el 2026-10-22 | sin evidencia (dicho del autor en `dev`) | `51724f5` |
| S79 | `42568f0` motivó la segunda release | sin evidencia (plausible) | — |
| S80 | `derive_version_floor` subestima | confirmada (COEX): `version.txt` no sube | §5.3 |
| S81 | el `.so` de release da 403; no reproducible | sin evidencia | — |
| S82 | mapas sin lock alcanzables desde hilos concurrentes → UB | confirmada la ausencia de mutex; la concurrencia sin evidencia (el autor asume hilo único) | `achievements.hpp:17-19`, `hooks.cpp:1473-1476` |
| S83 | `Updater::init()` no descarga ni hashea con ambas off | confirmada | `update.cpp:25-30,131-135` |
| S84 | `ticket-grabber` es el motivo del `!strip` | confirmada como comentario; "puede revertirse" es opinión | `PKGBUILD:26-27` |

Inconsistencias del doc viejo que el código cierra: **I1** (S27 arriba:
ambas posturas eran ciertas en contextos distintos); **I2** (ni 7 ni 5: cero
`RunIPCFrame`, 37 hooks); **I3** (argumento inválido, hecho sostenido por
medición); **I4** (el poke funcionaba; S62 muere); **I5** (`IN_CLOSE_WRITE`
sobre el directorio sí cubre el rename cuando el editor escribe y cierra en
el directorio; `IN_MOVED_TO` cubre el `rename()` puro); **I20** (API apagada
por defecto en el YAML instalado); **I27** (sin strip por `Makefile`, no solo
por el PKGBUILD); **I29** (los hashes se añaden bajo la clave vigente; cada
bump copia los vigentes). Las demás (I6-I19, I21-I26, I28, I30-I32) eran
notas de redacción o de nuestro lado y no tienen correlato en el código.

### 5.2 Mediciones y pruebas con fecha

Las 223 del doc anterior (M1-M223), tal como estaban escritas (fecha, dónde,
resultado literal), ordenadas por función. "Dónde": **commit/fuente** =
lectura estática del repo a un commit o tag; **binario** = inspección de un
`.so`; **codespace** = corrida en contenedor; **Deck** = on-device; **Discord**
= dicho de segunda mano. Las de la relectura de hoy (2026-10-08) van en una tabla
propia después de las doce funciones.

#### Función 1 — Engancharse a Steam

| # | Fecha (como estaba escrita) | Dónde | F | Qué se midió | Resultado (literal) | Sección del doc anterior |
|---|---|---|---|---|---|---|
| M2 | (base ebfb079) | fuente `main.cpp:221-239` | 1 | Mecanismo de inyección | rtld-audit: `la_version()`→`LAV_CURRENT`; `la_preinit()`→`setup()`; `la_objopen()` dispara `load()` cuando `steamclient.so` o `steamui.so` entran en el mapa (`main.cpp:226-234`). Carga vía `LD_AUDIT`, no `LD_PRELOAD` | §0.1 |
| M3 | (base ebfb079) | fuente `main.cpp:111-116`, `95-99` | 1 | Auto-limpieza y filtro de proceso | `cleanEnvVar("LD_AUDIT", ...)` borra sus entradas de `$LD_AUDIT`; sólo continúa si el proceso se llama `steam`, si no `unload()` | §0.1 |
| M4 | (base ebfb079) | fuente `patterns.hpp:11-27`, `memhlp.cpp:35-108`, `11-33` | 1 | Motor de patrones | `Pattern_t` nombre+bytes con `?`+modo+prólogo; `patternScan` enumera segmentos `LM_PROT_XR`; `patternToBytes` → `int16_t` con `-1` comodín | §1.1 |
| M5 | (base ebfb079) | fuente `memhlp.cpp:110-140`, `152-167`, `169-195` | 1 | Modos de resolución | `Relative` (`getJmpTarget`), `None`, `PrologueUpwards` (walk-up hasta 0x10000 bytes buscando prólogo p.ej. `56 57 e5 89 55`) | §1.2 |
| M6 | (base ebfb079) | fuente `memhlp.cpp:197-278` | 1 | `fixPICThunkCall` | Detecta `call`→`mov reg,[retaddr]`→`ret` en el trampolín y reescribe la carga de retaddr desde la ubicación original | §1.3 |
| M7 | (base ebfb079) | fuente `memhlp.cpp:281-288` | 1 | `getTypeName` | vft = `*pClass`; typeInfo = `*(vft - sizeof(ptr))`; name = `*(typeInfo + sizeof(ptr))`. Usado sólo para logging del tipo de cada `CProtoBufMsg` | §1.4 |
| M8 | (base ebfb079) | fuente `hooks.cpp:65-108`, `110-149`, `vftableinfo.hpp` | 1/3 | Tipos de hook e índices | `DetourHook` (`LM_HookCode` + `fixPICThunkCall`); `VFTHook` (`LM_VmtHook(vft, index, hookFn)`); índices `IClientAppManager::BIsDlcEnabled = 11`, `IClientApps::GetDLCCount = 8` | §1.5 |
| M9 | (base ebfb079) | fuente `hooks.cpp:433-455`, `508-531` | 1 | Captura de instancia viva | `hkClientAppManager_RunIPCFrame`: en 1ª llamada `LM_VmtNew(*reinterpret_cast<lm_address_t**>(pObj), vft)`, monta VFTHooks, `RunIPCFrame.remove()`; `hkClientApps_RunIPCFrame` usa `static bool hooked` | §1.6 |
| M10 | 2026-07-24 | commit `8de3384` | 1 | Eliminación de los hooks RunIPCFrame | Eliminados los **cinco** hooks de `RunIPCFrame`; instancias navegadas `CSteamEngine → getUser() → getAppManager()/getClientApps()/getClientUser()` vía offsets por patrón, desde un único `placeVFTHooks()` | §1.6 (aviso SUPERSEDED) |
| M11 | (base ebfb079) | fuente `hooks.cpp:824-930` | 1/2 | Hook naked de `IClientUser::GetSteamID` | `createAndPlaceSteamIdHook`: desensambla, ensambla trampolín (pushad/pushfd, llama hook `stdcall`, popad), `jmp` relativo; autor lo marca "lazy / bad at this" | §1.7 |
| M12 | 2026-07-27 (y "desde 2026-08-05") | commit `c4152f5` | 1/2 | Sustituto del hook naked | Borrado el trampolín naked; `steamId` interceptado en el buffer de retorno IPC dentro de `CSteamEngine::RunInterface` (desde 2026-08-05 `ProcessIPCFrame`), filtro `interfaz == ClientUser && exitCode == Success && fnId == 0xD6FC3200`; el desensamblado queda como comentario | §1.7 (aviso SUPERSEDED) |
| M23 | (base ebfb079) | fuente `update.cpp:22`, `27-34`, `41-55`, `102`; `main.cpp:176-188` | 1/11 | SafeMode | `Curl::getString` baja `raw.githubusercontent.com/AceSLS/SLSsteam/.../res/updates.yaml`; fallback `.updates.yaml`; `SafeModeHashes` mapa versión→{SHA256 de steamclient.so}; `verifySafeModeHash`; en `load()` hash desconocido + `SafeMode` on → `unload()`; off + `WarnHashMissmatch` → aviso | §3 |
| M30 | (base ebfb079) | fuente `patterns.cpp` + `vftableinfo.hpp` | 1 | Disjunción de hooks | SLSsteam **no** toca `LoadDepotDecryptionKey` (CConfigStore slot 6), `BuildDepotDependency`, `GetManifestRequestCode`, `shadercachedepot`, package-0 finder. SLSsteam engancha: `CProtoBufMsgBase::Send/InitFromPacket`, `CUser::CheckAppOwnership/GetSubscribedApps`, `RunIPCFrame` de **7** interfaces, `IClientUtils::GetAppId/GetOfflineMode`, tickets `IClientUser`, DLC `IClientApps`/`IClientAppManager`, matchmaking | §5 |
| M31 | release `5c632dd` | commit | 1/2/5 | Hooks nuevos | `CAppDataCache::BParseResponseMessage` y `IClientAppManager::GetAppStateInfo` (limpia `APPSTATE_UPDATE_*` en sitio); hot-reload postea `AppLicensesChanged_t` | §5, §7 |
| M32 | 2026-09-03 | tag `20260903114323` | 1/4 | Plugins Lua | SLSsteam carga plugins Lua; con `download.lua` **DepotKey** y **GMRC** sí solapan | §5 aviso ALCANCE REDUCIDO |
| M35 | 2026-07-05 | commit `5c632dd` (`20260705132808`) | 1 | Patterns wildcardeados | `cmp eax, <hash>`: `3D 37 9C 88 A6` → `3D ? 9C 88 A6`; reanclados IClientAppManager/IClientUtils RunIPCFrame; wildcards en RequiresLegacyCDKey. Diff `src/` ~22 ficheros, ~450 líneas | §7 |
| M40 | 2026-07-05 | fuente `hooks.cpp` @5c632dd | 1 | Bug de SLSsteam | En `Hooks::remove()` el `CAppDataCache` llama a `.place()` en vez de `.remove()` | §7 observación |
| M55 | 2026-07-22 | commit `2903ef8`, release `20260722152506` | 1 | Decompilador | Nuevos `src/decompiler.{cpp,hpp}`, `src/vftableinfo.{cpp,hpp}`; `collectVFTables` por RTTI; `parseInterfaceMapBase` → `map<string,unsigned>`; nota de release "Steamclient may take a few seconds longer to start now" | §7.5 |
| M56 | 2026-07-22/23 | commits `fa8523f` (`20260722175657`), `4dd592c` (`20260723094105`), `69594b9` (`20260723102618`) | 1/3 | Hotfixes del decompilador | "Workaround SteamOS getting stuck"; "Fix decompiler branching decisions. DLC unlocks should work properly again and crashes should be gone. Use real appId in more interfaces"; "Fix crash for apps that have an empty parent" — tres parches en ~18h | §7.5 |
| M59 | (tag `20260722152506`) | fuente `decompiler.{cpp,hpp}`, `vftableinfo.{cpp,hpp}` | 1 | Pipeline del decompilador | `parseHeader` con `fopen(mod.path,"r")` (disco, no memoria), map `"module::section"`; `collectStrings` `MIN_STRING_SIZE` 5; `collectVFTables` dos pasadas (`30CClientUnifiedServiceTransport`, `14IClientUserMap`); `VFTable::analzye` (typo); `parseInterfaceMapBase` por string de nombre de método; `__parseFunction` via `LM_Disassemble`, `branchesTaken`, `isPICThunk`, `getLeaOffset`; `NO_INDEX` = `0xFFFFFFFF`, caché `tableMap` | §7.5.1 |
| M60 | (tag `20260722152506`) | fuente `vftableinfo.cpp` | 1 | Interfaces/funciones resueltas | 9 interfaces: `CCMInterface`, `CClientUnifiedServiceTransport`, `CSteamMatchmakingServers`, `IClientApps`, `IClientAppManager`, `IClientRemoteStorage`, `IClientUtils`, `IClientUser`, `IClientEngine`; ~27 funciones incl. `RecvPkt`, `SendAndRecv`, `BLoggedOn`, `BUpdateAppOwnershipTicket`, `GetAppOwnershipTicketExtendedData`, `IsUserSubscribedAppInTicket`, `RequiresLegacyCDKey`, `GetUpdateInfo`, `InstallApp`, `UninstallApp`, `GetAppInstallState`, `GetDLCCount`, `BGetDLCDataByIndex`, `BIsDlcEnabled` | §7.5.1 |
| M66 | 07-23 … 08-15 (tabla de hitos) | commits | 1/11 | Cronología de la ventana | 07-23 patternScan ceñido a `.text`; 07-24 elimina 5 `RunIPCFrame`; 07-25 análisis antes de relocaciones; 07-27 borra naked `GetSteamId`; 07-28 release `20260728212859` (`SteamIdOverride`, `FakeName`); 08-04 stutters logros; 08-05 `RunInterface`→`ProcessIPCFrame`; 08-12 `CDKeys`; 08-14 API `setcompat`/`getcompat`/`dumpcompat`/`dumplibraries` + `API.md`; 08-15 release `20260815201341` (`IN_MOVED_TO`, `CUtlRBTree`) | §7.7.0 |
| M68 | 2026-07-23 | commit `69d9623` | 1 | `retf` en parser | `__parseFunction` sólo reconocía `ret`/`retn`; añade `retf` (`CB`/`CA`); comentario suyo: capstone normaliza los `ret` → no-op defensivo | §7.7.1 |
| M69 | 2026-07-23 | commit `f87979f` | 1 | patternScan sólo `.text` | Antes `LM_EnumSegments` sobre todo el proceso filtrando `LM_PROT_XR`; ahora `Decompiler::getSection(".text")` desde cabeceras ELF de disco, `[base+sh_addr, +sh_size)`; `main.cpp` añade `Decompiler::parseHeader(g_modSteamUI)` (`Didn't find .text section` si falta); `parseHeader` = sólo cabeceras, `parseModule` = + strings + vtables. Observación propia: off-by-one (`addr < end` vs guarda `byteAddr > end`); el escaneo devuelve el **último** match con `debug` si `matches > 1` (comentario "Not unique. All matches point to correct function though") | §7.7.1 |
| M70 | 2026-07-25 | commit `3e9dfdd` | 1 | Relocación de `.data.rel.ro` | Mensaje: ".rodata.ro.rel seems to get modified so the offsets turn into actual addresses which broke the decompiler flow"; código `this->functions.emplace_back(offset + moduleBase);` asume offsets relativos; mueve `LM_FindModule` + `parseModule` dentro de `la_objopen` ("Analyse modules before any relocations get applied") | §7.7.1.b, §7.7.3 |
| M71 | 2026-07-25 | commit `807e229` | 1 | Análisis inmediato de vtables | Analiza TODAS las vtables dentro de `la_objopen`; quita `analzye()` diferidos de `hooks.cpp`/`vftableinfo.cpp`; cita "This is wasteful, but we have to analyse right away otherwise the offset get turned into addresses messing up the analysis…" | §7.7.3 |
| M72 | 2026-07-24 | commit `1865890` | 1 | Sub-vtables (herencia múltiple) | `CUser : CBaseUser, IClientUser, IClientMatchmaking, IClientAppDisableUpdates, IClientBilling` → varias sub-vtables con el mismo typeinfo `5CUser`; antes `vftables[name] = vft` pisaba; ahora `subclasses[0..n]` (`subclasses[0]` = `IClientUser`); frontera por heurística `offset & 0xFFFF0000`; `TODO`: cruzar typeinfos "funcionaba en las 4 primeras vftables de CUser y luego empezaba a fallar" | §7.7.2 |
| M73 | 2026-07-24 | commits `8fada22` + `28d59a1` | 1 | Offsets de miembro por patrón | 4 patrones `SigFollowMode::None`: `m_OffsetUserAppInfo` = `"8D 90 ? ? ? ? …"` (`lea edx,[eax+disp32]`, lee en `+2`); `m_OffsetClientUser` = `"2D ? ? ? ? …"` (`sub eax,imm32`, lee en `+1`); interfaces embebidas (`this + offset`); cita "Like the previous commits I picked some patterns that go back about 9 months" | §7.7.2 |
| M74 | 2026-07-24 | commits `69ea49b`, `269e867` | 1 | Víctimas de prueba | `69ea49b` retira patrón de `GetSteamId` (no-único); `269e867` retira `RunIPCFrame` de RemoteStorage; su vtable se relocaliza → `DetourHook` ("the pointers are all wrong and would need manual adjustment which breaks current assumptions by VFTHook<T>"); nueva sobrecarga `DetourHook::setup(name, addr, fn)` | §7.7.2 |
| M75 | 2026-07-24 | commit `8de3384` | 1 | Consolidación | −223/+164 en `hooks.cpp`, 22 ficheros; 4 `RunIPCFrame` restantes fuera; `placeVFTHooks()` one-shot (`static bool hooked` + mutex, "I don't think the IPC layer is multithreaded but better safe than sorry") desde hook de `CSteamEngine::RunInterface`; "first run CUser is null"; mueren `g_pClientApps`/`g_pClientAppManager`/`g_pClientUtils`/`g_pClientUser` → `g_pSteamEngine->getUser()->getClientApps()`. Balance: −6 patrones, +4 offsets | §7.7.2 |
| M76 | 2026-07-24 | commits `bc25bf8`, `3c77e9f`, `99d0af7` | 1 | Fixes SDK | `EInterfaceType` `uint32_t`→`uint8_t` (leía 4 bytes y enmascaraba `& 0xff`); `getPipeIndex() -> uint32_t*` → `getCurrentSteamPipe() -> HSteamPipe` (nombre real en `IClientUtils`); `CUtlBuffer::flags` `uint8_t`@0x1A → `uint32_t`@0x18, total sigue `0x24`, `flags` no se lee en ningún sitio | §7.7.2 |
| M77 | 2026-07-24 | commits `73dba5f`, `b752536`, `ce0bb89` | 1 | Comentarios/limpieza | `b752536` documenta layout del buffer IPC y arregla: `interfaceType` está en offset **fijo 1** (antes usaba cursor `get`); `ce0bb89` formato ~17 ficheros | §7.7.2 |
| M78 | 2026-07-31 (citado en §7.7.2.a) | commit `b8ef92f` | 1 | Índice de Map aplicado a impl | Coge índice de `21IClientConfigStoreMap` y lo aplica a la vtable de `12CConfigStore` (asume correspondencia de orden wrapper↔impl) | §7.7.2.a, §7.7.8 |
| M79 | 2026-07-25 | commit `7aee22c` | 1 | ServerResponded | `ISteamMatchmakingPingResponse::ServerResponded` (patrón en `steamui.so`) → `VFTIndexes::CGameInfoDialog::ServerResponded`; primer patrón retirado en `steamui.so` | §7.7.3 |
| M80 | 2026-07-25 | commit `d2f94c8` | 1 | "Refine patterns" | Prólogo `std::vector<uint8_t>` → `std::vector<int16_t>` (comodines `-1`); migración call-site+`Relative` → ancla corta+`PrologueUpwards`; `CUser::CheckAppOwnership` de 26 bytes a `"0F 94 C2 08 51"` + prólogo `{0x53,0x56,0x57,0xE5,0x89,0x55,-1,-1,-1,-1,0x5,-1,-1,-1,-1,0xE8}`; también `TraceIPC`, `CAPIJob::SendAndRecv`, `CAppDataCache::BParseResponseMessage`, `CWebSocketConnection::BBuildAndAsyncSendFrame`, `CSteamEngine::RunInterface`, `CUser::UpdateAppOwnershipTicket`. Log suyo `"Pattern %s found %i times"` | §7.7.3, §7.7.3.a |
| M81 | 2026-07-25 | commits `ce6509f`, `8effc44` | 1 | Crash con `ExtendedLogging`; nullchecks | Guarda `getUser()` antes de `getUtils()->getAppId()`; mutex de `placeVFTHooks` movido tras el early-return `!usr`; `CSteamEngine::getUtils()` → `if (!getUser()) return nullptr;` | §7.7.3 |
| M83 | 2026-07-25 | commits `4bd33e7`, `9927612`, `526b828`, `dfb8614`, `9f232e6` | 1 | Menores | `CNetPacket+0xC` = `int32_t refs`; `9927612` añade hash de cliente **2026.07.25** a SafeMode; `9f232e6`: buffer IPC `base+2` es `*(this+4)`, id de función en `base+6` | §7.7.3 |
| M90 | 2026-07-27 | commits `d1bbc92`, `1082525`, `2be9d89` | 1 | Vocabulario IPC | `EInterfaceType` → `EIPCInterface` (48 constantes `k_EInterfaceTypeClient*` → `k_EIPCInterfaceClient*`); `EIPCCmd` (`RunInterface = 1`, `SerializeCallbacks = 2`, `ConnectPipe = 9`); `EIPCExitCode` (`Success = 0xb`); args `pBufInterfaceInfo` → `pBufIPCCmd`, `a2` → `pBufReturn` | §7.7.5 |
| M91 | 2026-07-27 | commits `e8e04f1`, `b3b2f98` | 1 | Citas de diseño | "While hooking this function to replace the other hooks might seem attractive we do not do so. Many calls straight up bypass the IPC layer and go straight for the original VFT implementations (IClientAppManager comes to mind)…"; "I really do not like hooking the IClient*Map functions because they are very surface level and get skipped over a lot…" | §7.7.5 |
| M92 | 2026-07-27 | commit `c4152f5` | 1/2 | Intercepción de GetSteamID en IPC | `if (type == k_EIPCInterfaceClientUser && exitCode == EIPCExitCode::Success && fnId == 0xD6FC3200) { if (!g_currentSteamId.accountId) memcpy(&g_currentSteamId, pBufIPCResult->mem.base + 1, sizeof(CSteamId)); … memcpy(pBufIPCResult->mem.base + 1, &newId, sizeof(newId)); }`; comentario "IClientUser::GetSteamID has been optimized to hell and back"; lee `[edx-0x174E]`/`[edx-0x1752]`, escribe `[eax]`/`[eax+4]`; `0xD6FC3200` hardcodeado | §7.7.5 |
| M94 | 2026-07-27 | commit `12a8e2f` | 1/11 | `Updater::isEnabled()` | `return g_config.safeMode.get() \|\| g_config.warnHashMissmatch.get();` al inicio de `init()` y `verifySafeModeHash()`; `SafeMode` viene `no` por defecto | §7.7.5 |
| M104 | 2026-07-30 | commit `e2ac776` | 1 | Centinela en `parseFunction` | `leaOffset = LM_ADDRESS_BAD` (`0xFFFFFFFF`); guarda `if (!leaOffset) continue;` comparaba contra 0 → referencias espurias; fix `if (leaOffset == LM_ADDRESS_BAD) continue;` | §7.7.7 |
| M106 | 2026-07-31 | commit `b8ef92f` | 1 | Hook baja a `IClientConfigStore` | `IClientConfigStore_SetString.setup(VFTIndexes::IClientConfigStoreMap::SetString.getPrintName().c_str(), store.functions[VFTIndexes::IClientConfigStoreMap::SetString.index], hkClientConfigStore_SetString);` resuelto por RTTI `12CConfigStore` | §7.7.8 |
| M107 | 2026-07-31 | commit `00c93c6` | 1 | Sobrecargas en `nombre→índice` | Dos slots con el mismo literal: el segundo pisaba `functionMap[str] = i`; ahora `nombre2`, `nombre3`…; `const auto& str` → `auto str` | §7.7.8 |
| M109 | 2026-07-31 | commit `4bfe8e2` | 1 | Arena de paquetes | `g_packetsArrayIndex` (`uint32_t`) → `g_packetsArrayOffset` (`uintptr_t`); "Biggest message I have observed was around 600kb"; `MAX_PACKET_SIZE` 1 MB × `MAX_PACKETS` 8 = 8 MB estática | §7.7.8 |
| M112 | 2026-08-01 | commit `46b6ff5` | 1 | `VFTIndexes::init()` prueba todos | `bool success = true; for(const auto& fn : functions) if (!fn->init()) success = false; … return success;`; llamante `main.cpp:184` sigue abortando ("Failed to parse VFTables! Aborting…"); incluye renombre `void* a0` → `pClientAppManager` en hook de `BuildDepotDependency` | §7.7.9 |
| M115 | 2026-08-04 | commits `a1b30e8`, `690fb8a` | 1 | SafeMode | Hash de cliente **2026.08.04** en `res/updates.yaml`; merge `main`→`dev` | §7.7.10 |
| M117 | 2026-08-05 | commit `77f5d44` | 1 | `RunInterface` → `ProcessIPCFrame` | `ProcessIPCFrame(this, HSteamPipe pipe, CUtlBuffer* in, CUtlBuffer* out)`; `const EIPCCmd cmd = *reinterpret_cast<EIPCCmd*>(pBufIn->mem.base + 0); if (cmd == EIPCCmd::RunInterface) {…} else { ret = tramp.fn(...); }`; nuevo `EIPCCmd::CreateGlobalUser = 3` ("Also used to connect to global user") | §7.7.11 |
| M122 | 2026-08-06 | commit `a2edc13` | 1 | Asignador de Steam | `dlopen("libtier0_s.so")` + `dlsym` de `Plat_Alloc`, `Plat_Free`, `Plat_Realloc`; tercer módulo en `la_objopen` y precondición de `load()`; descuido `if (!Plat_Alloc \| !Plat_Free \| !Plat_Realloc)` con `\|` bitwise | §7.7.12 |
| M123 | 2026-08-06 | commit `b937ab2` | 1 | Free netpacket | `clearBody()` viejo: `size = body->headerSize + sizeof(CNetPacketBody);` ("Hide body and call original function so steam uses it's own free"); ahora `Steam::Plat_Free(body)` y retorna sin llamar al original | §7.7.12 |
| M124 | 2026-08-06 | commit `0fc9cf9` | 1 | Arena estática muere | Comentario "Freeing pData royally fucks up memory, proly a use after free scenario / So we copy the packet into fresh memory, modify that, etc"; `Plat_Alloc(dataSize)` → `memcpy` → mutar → tramp → `Plat_Free`; borra `g_packetsArray` (8 MB) y `g_packetSerializeMutex` | §7.7.12 |
| M127 | 2026-08-07 | commit `cc8d42a` | 1 | Tamaño de `CNetPacket` | Declarado `0x14` (20 bytes), real `0x20` (32); +12 bytes de padding; campo `__pad0x10` está en `0x14` | §7.7.13 |
| M128 | 2026-08-07 | commits `7366bed`, `6e8e8de` | 1 | Receta `recvPkt`; enviar/falsificar mensajes | Comentario: "Create header with steamId & realm / Create body / Serialize / Set type with ProtoBuf mask / Set refs to 1 (not doing this will debugbreak() in a failed assert)"; hook+tramp para `CCMInterface`, `CWebSocketConnection`, `IClientUser::sendMsg` | §7.7.13 |
| M129 | 2026-08-07 | commits `3d7d3c6`, `a62efeb` | 1 | `has_target_job_name()`; `isValid()` | "Do not modify header by blindly requesting the target_job_name"; `isValid()`: `-return body && size > sizeof(CNetPacketBody) && body->type != INVALID_NETPACKET_TYPE;` → `+return getType() != INVALID_NETPACKET_TYPE && size > sizeof(CNetPacketBody);` (`getType()` guarda `if (!body) return INVALID_NETPACKET_TYPE;`) | §7.7.13 |
| M130 | 2026-08-07 (HEAD del barrido) | fuente `CNetPacket.hpp`/`.cpp:8`, `hooks.cpp:271/275/481` | 1 | Bug `isProtoBuf()` | `constexpr bool isProtoBuf() const { if (getType() == INVALID_NETPACKET_TYPE) { return INVALID_NETPACKET_TYPE; } return getType() & PROTOBUF_TYPE_MASK; }` → `-1` a `bool` = `true`; dos llamantes tapados por `isValid() &&`; tercero `getProtoBufTypeName()` loguea `0x7FFFFFFF` en vez de `"Unknown"` (cosmético) | §7.7.13 |
| M131 | 2026-08-07 | commits `c51c4fa`, `b571526` | 1 | Menores | `la_objopen` `const std::string name = map->l_name`; ya trackea `libtier0_s.so`; logging en `assembleCodeAt` | §7.7.13 |
| M136 | 2026-08-08 | commits `23f3dd7`, `eb4cfac`, `e3ed672`, `6786383`, `b0781a0`, `a05f9ff`, `8375b8e` | 1/11 | Menores | Retira wrappers `Steam::alloc<T>()`/`free()`/`realloc<T>()`; `if (type == 2)` → `k_EWebSocketConnectionSendRaw`; toggle `DEBUG` en Makefile: "I checked using radare, without DEBUG being defined the inlined calls of CLog::once & CLog::debug get fully optimized out"; hook+tramp `CMCInterface`/`CWebSocketConnection` | §7.7.14 |
| M150 | 2026-08-15 | commits `ac6830b`, `9be780a`, `ee0021b` | 1 | `CUtlRBTree` como árbol | `struct Element_t { int32_t leftIndex; //0x0 int32_t rightIndex; //0x4 uint8_t __pad0x0[0x8]; //0x8 T key; //0x10 T2* value; //0x14 }; //0x18`, `int32_t rootNodeIndex; //0x14`, `uint32_t allocated; //0x18`; `find()` devuelve `Element_t*`; `ee0021b`: probado contra `m_MapAppOwnershipTickets` y `m_mapPackages`; grep: `CUtlMap`/`CUtlRBTree` no se usan en ningún sitio (sólo `src/sdk/CUtl.hpp`); bug latente: deref `elements[index]` antes de comprobar `index == -1` (árbol vacío → lectura 0x18 bytes bajo el array) | §7.7.19 |
| M154 | (§7.7.19) | fuente `update.cpp:128`, `res/config.yaml` líneas 98 y 102, `config.cpp:176-177` | 1 | Comportamiento de SafeMode | `verifySafeModeHash()` busca `clientHashMap[VERSION]`; `SafeMode: yes` → `LOG_NOTIFYERROR("Unknown steamclient.so hash! Aborting...")` + `unload()`; `WarnHashMissmatch: yes` → toast "please update"; ambos `no` por defecto (fichero y código `false`) | §7.7.19 |
| M158 | 2026-08-17 | commit `aa27169` | 1/11 | Validación del feed | `- if (res == 0 && data.size() > 0)` → `+ if (res == 0 && data.starts_with("SafeModeHashes:\n"))`; cita "today github decided to do some trolling by returning a wholly different message" | §7.8.3 |
| M171 | 2026-08-23 | commits `148b9a1`, `1f10312` | 1/11 | `library-inject` vaciado; excepciones | "la_objsearch messes up exceptions in a way that they do not get caught anymore"; `main.cpp` comentado entero; `catch (YAML::BadFile&)`/`ParserException`/`BadConversion` → `catch (...)`; Makefile: `#g++ … #Disabled, since having a la_objsearch fucks up exceptions for some reason / -rm "library-inject.so" / touch "library-inject.so"` | §7.9.4 |
| M171b | (§7.9.4) | lumalinux `main.cpp:377-399`, `setup.sh:1044`, `:881`; **medido** `LD_AUDIT` con fichero vacío | 1 | `.so` de 0 bytes | lumalinux no implementa `la_objsearch`; `setup.sh` instala `library-inject.so` y lo pone primero en `LD_AUDIT`; `$ LD_AUDIT="$PWD/empty.so" /bin/true` → `ERROR: ld.so: object '…/empty.so' cannot be loaded as audit interface: file too short; ignored.` `exit=0`; entrada inválida no arrastra a la otra; accionable `-f` → `-s` | §7.9.4 (coexistencia) |
| M173 | 2026-08-24 | commits `0420645`, `a17692e`, `07be566`, `f4a6b9f`, `0168c7d`, `6f7e0d1`, `1e6b96f`, `957ece2` | 1/12 | API de plugins Lua | LuaBridge (~7k líneas en `include/`), plugins en `config/plugins`; hooking desde Lua + vigilancia del directorio; callbacks; decompilador por nombre; `CUser::postCallback` & `MemHlp::hexdump`; callback `SLSsteam::initialized` ("Fired when Steam has finished initializing `CUser`, making it safe to access"); `6f7e0d1` bug: `VFTableInfo_t::init()` tomaba referencia a `Decompiler::vftables[typeName]` y la machacaba con `subClassIndex`; `libluajit.a` reconstruido con GCC 4 (`_GLIBCXX_USE_CXX11_ABI=0`); `957ece2` `SLS.alloc/realloc/free` | §7.9.5 |
| M174 | (§7.9.5) | fuente `src/sdk/CUser.cpp:48-52`, `patterns.cpp:145`, `example.lua` | 1 | `postCallback` es por patrón | `void CUser::postCallback(const ECallbackType type, void* pCallback, const uint32_t callbackSize) { const static auto fn = reinterpret_cast<...>(Patterns::CUser::PostCallback.address); fn(this, type, pCallback, callbackSize, 0); }`; `example.lua`: `memhlp.patternScan("E8 ? ? ? ? 8B 75 ? 89 D8", modSteamClient)`; README: "While hot reloading Luas is possible it's highly advised against doing so. You have been warned." | §7.9.5 |
| M175 | 2026-08-25 | commits `8b9ccf6`, `ac0b16b`, `2b628b2`, `81252c8`, `d62707c`, `3ea3b87`, `0453823`, `7faec87`, `1306feb` | 1/11 | API Lua se llena | `SLSsteam::luaReload`, `SLSsteam::configLoaded` + `rootNode`, `Network::recvPkt`/`Network::sendPkt` con `CNetPacket`/`CNetPacketBody`; crea dir de plugins; sólo `.lua`; orden alfabético (`std::set`); no escribe rutas completas al log | §7.9.6 |
| M176 | 2026-08-26 | commits `c9c0bbb`, `2432a37`, `c0f42f2`, `60a60db`, `3f9c705`, `85dac66`, `ee2620c`, `d648297`, `56bc770`, `7d7a5e6` | 1/11 | Candado de plugins | Desactiva compilación de `library-inject` en Makefile; `Plugins: no` en `config_default.hpp`, `g_config.plugins` gatea `Lua::runLua()`; "SLSsteam plugins can run arbitrary code! So only run plugins you trust"; "the rest of the system stays active to allow for hot reloading"; `watchLoop` gana flag `running` + manejador de señal; `eventMask` configurable; `reloadlua` en API; `Utils::exec` se comía `argv[0]`; `LuaHook` rechaza `LM_ADDRESS_BAD` | §7.9.7 |
| M180 | 2026-08-29 | commits `913a431`, `0a77412`, `f932133`, `7850ab2`, `3131c5f`, `52bbe5e` | 1 | Refactor de `Hooks` | 579 líneas `hooks.cpp` + 196 `hooks.hpp`; `IHook` con `std::unordered_set<IHook*> hooks`; `DetourHook`/`VFTHook`/`LuaHook` : `Hook<T>`; objetos → punteros (`Hooks::CUser_CheckAppOwnership->tramp.fn`); borra comentario "This does not work! GCC sees no path to the hk* functions…"; `LOG_IF_EXISTS(g_pLog)` ("Seems like SLSsteam was trying to preload libc.so.6"); `MTVariable` a `shared_ptr` `.get()`→`.copy()`; `operator=` asignaba puntero | §7.9.10 |
| M182 | 2026-08-30/31 | commits `d900d77`, `32bb863`, `f4213e5`, `2f9af1b`, `3a3ee4a`, `1721312` | 1/12 | API Lua se mueve | Makefile `-rm` + `touch`; LuaBridge moderno (+16k/−4.7k; `Coroutine`, `Expected`, `Enum`, `Overload`), `addConstant` → `addVariable`; `place_lua_hook` export C; elimina `SLS.alloc/realloc/free`, `memhlp.getUserDataPtr`; fusiona `downloadStringWithHeaders` en `downloadString` | §7.9.11 |
| M183 | 2026-09-01 | commits `ac52879`, `3f8e429` | 1 | `unpack_user_data` | `extern void* unpack_user_data(const void* pData) { return reinterpret_cast<const luabridge::detail::Userdata*>(pData)->getPointer(); }`; tercera encarnación en 8 días; doc "Use this to convert `SLS.steamEngine` to the real `CSteamEngine` pointer etc."; `example.lua` engancha `postCallback` escaneando bytes desde Lua, usa `VFTableInfo_t` con subclases, en `SLSsteam::initialized` llama `isSubscribed` y `config:setAdditionalApps()` | §7.9.12 |
| M186 | 2026-09-01..03 | commits `3d45b14`, `5f8ce3a`, `a7c47f5`, `b1fd214`, `6015c05`, `c95b119`, `eafd3ea`, `fc96f26`, `71021ad`, `11f0970`, `f2d6544`, `75acfce`, `94e9341` | 1/11 | `main` hasta release `20260903114323` | `3d45b14` permisos `u=rwx` a dir de plugins y cada `.lua`, si no puede desactiva plugins esa sesión; `eafd3ea` corrige path de `.updates.yaml` (sobraba una barra); hashes de cliente: 2-sep `bc54101b…` (ubuntu12_32 + steamdeck_stable), 3-sep `237495b4…` (sólo ubuntu12_32); release `71021ad` | §7.10 |
| M191 | 2026-09-16 | commit `003f8f0` | 1 | `CNetPacket` vuelve al búfer estático | "Went back to the old method using a buffer + overwriting the body only. Freeing/replacing the originalBody caused issues"; vuelven `g_packetsArray[1 MB × 8]`, `g_packetsArrayOffset`, `g_packetSerializeMutex`; rechaza mensajes ≥ 1 MB; "If I understand correctly Steam cleans up for us, that's why we crash when we free the oldBody ourself. However the body we allocate doesn't get freed, so we just reuse a buffer for it". Moon `2668620` (2-sep) había borrado `PACKETS_ARRAY` | §7.11 |
| M192 | 2026-09-17 (barrido) | feed de SafeMode + tracker moon `swwayps/steam-monitor` | 1 | Hashes | Último hash 3-sep `237495b4…` (`ubuntu12_32`, versión `1788400362`); stable escritorio `1788652215` (5-sep) sin hash (CDN bloqueado en sandbox; inferencia); Deck en `1788291500` (2-sep) con hash en los tres sitios | §7.11 |
| M195 | 2026-09-17 | commit `0cad406` | 1 | Mutex en `serialize` | `lock_guard` sube por encima de la lectura de `g_packetsArrayOffset`; "Doesn't really matter right now, since RecvPkt seems to always get called from the CSteamEngine thread anyway"; `newBdy->type = getType()` | §7.12 |
| M201 | 2026-09-18 (tracker moon `19ca7ee`) | TOML de moon | 1 | Hash del stable 5-sep | `237495b4…` cambió `steam_version` `1788400362` → `1788652215` → el stable del 5-sep lleva el mismo `steamclient.so` que el del 3; betas `1789606022` (18-sep), `1789781627` (19-sep), `1790036264` (22-sep) sin hash; Deck en `1788291500` (`bc54101b…`); nuestro feed sin `237495b4…` | §7.12 |
| M206 | 2026-09-28 | tracker moon | 1 | Hashes | Betas escritorio `1790121765` (23-sep), `1790380355` (26-sep), `1790534246`, `1790545198` (27-sep); stable `1788652215`; Deck `1788291500` (`bc54101b…`) | §7.13 |
| M208 | 2026-09-28 | commit `779178c` | 1/2 | `GetSteamID` a VFT hook | Deja el hash `0xD6FC3200` en `ProcessIPCFrame`; `g_currentSteamId` capturado en el hook; `IClientUser::getSteamId()` en SDK; "Thanks 3vil3vo for explaining what the hell is going on in this function" | §7.14.1 |
| M213 | 2026-10-02 | `dev` commits `07dc57e`, `3d9ee15`, `4f3f2ef`, `b92dd53` | 1 | Offsets por RTTI | `MemHlp::searchOffsetByTypeName(obj, "15CUserAppManager")` recorre hasta 0x10000 bytes; sustituye tres patrones (`CUser::m_OffsetUserAppManager`, `m_OffsetUserAppInfo`, `m_OffsetClientUser`) y `g_pClientCompat` → `CUser::getClientCompat`; `b92dd53` mide y loguea ms | §7.14.2 |
| M216 | 2026-10-07 | tracker moon + feed MigoReleases (`lumacore-findings.md`) | 1 | Hashes | `res/updates.yaml` upstream sin cambio (`main@71021ad`); moon parado desde 27-sep; betas 30-sep `1790721607`, 2-oct `1790904859`, 6-oct `1791249696`; stable `1788652215`; Deck `bc54101b…` | §7.14.3 |

#### Función 2 — Propiedad y licencias

| # | Fecha (como estaba escrita) | Dónde | F | Qué se midió | Resultado (literal) | Sección del doc anterior |
|---|---|---|---|---|---|---|
| M13 | (base ebfb079) | fuente `hooks.cpp:290`, `apps.cpp:52`, `apps.cpp:18` | 2 | Ownership | `CUser::CheckAppOwnership` detour → `Apps::checkAppOwnership`; `unlockApp` muta `CAppOwnershipInfo`: `ownsLicense=true`, owner, `releaseState`, quita low-violence/region-lock | §2 tabla |
| M14 | (base ebfb079) | fuente `hooks.cpp:313`, `apps.cpp:129` | 2 | AppList injection | `CUser::GetSubscribedApps` detour → `Apps::getSubscribedApps` añade `AdditionalApps` | §2 tabla |
| M15 | (base ebfb079) | fuente `hooks.cpp:203`, `apps.cpp:230` | 2/4 | PICS tokens | `CProtoBufMsgBase::Send` detour → `Apps::sendPICSInfoRequest` adjunta `access_token` por-app en `PICSProductInfoRequest` (EMsg 8903) | §2 tabla |
| M17 | (base ebfb079) | fuente `fakeappid.cpp` | 2 | FakeAppId | Hooks `SetAppIdForCurrentPipe`, `GetAppId`, matchmaking, UGC, servers; mapea appId real↔falso por pipe | §2 tabla |
| M18 | (base ebfb079) | fuente `ticket.cpp:253` | 2 | Tickets | `CProtoBufMsgBase::InitFromPacket` → `Ticket::recvMsg`; `BUpdateAppOwnershipTicket`, `GetAppOwnershipTicketExtendedData`, GetSteamId; cache a `~/.config/SLSsteam/cache/*.yaml` (base64), reproducido offline, spoof de `steamId` | §2 tabla |
| M20 | (base ebfb079) | fuente `hooks.cpp:1037-1041`, `813-821` | 2 | Family lock | `patchRetn` escribe `0xC3` al inicio de `FamilyGroupRunningApp` y `StopPlayingBorrowedApp` | §2 tabla, §2.1 |
| M21 | (base ebfb079) | fuente `misc.cpp` | 2 | Cosméticos | `GetOfflineMode`, `BLoggedOn`, `InitFromPacket`: offline por-app, wallet, email verificado | §2 tabla |
| M22 | (base ebfb079) | fuente `apps.cpp:61-69`, `config.cpp:292-309` | 2/6 | Guardas | Denuvo: no modifica juegos Denuvo poseídos por otra cuenta (config `DenuvoGames`); `shouldExcludeAppId`: appId `>= 1e9` nunca se toca + whitelist/blacklist | §2 tabla, §2.2 |
| M33 | (v0.5.6 de lumalinux) | fuente lumalinux `main.cpp:21-22` | 2 | Hook retirado en lumalinux | "Packet/858 hook REMOVED: SLSsteam already covers ownership (CheckAppOwnership) and PICS access tokens. Nothing extra." | §5 |
| M37 | 2026-07-05 | commit `5c632dd` | 2/9 | Add/remove en caliente | Deltas `newApps`/`removedApps` bajo mutex (`config.cpp`); `Apps::runIPCFrame` desde `hkClientUtils_RunIPCFrame` postea `AppLicensesChanged_t` (`postCallback`) + re-pide appinfo; disparador movido de `InitFromPacket` a `RunIPCFrame` (race) | §7 pt 3-4 |
| M46 | 2026-08-12 | fuente `src/sdk/CUser.cpp` "idéntico en ebfb079, 69594b9 y HEAD" | 2 | `isSubscribed` ve propiedad real | `CUser::checkAppOwnership` → `Hooks::CUser_CheckAppOwnership.tramp.fn(this, appId, pInfo)`; `isSubscribed` = `info.ownsLicense && !info.licenseExpired`; para added app devuelve **false** | §7.2 corrección |
| M51 | 2026-07-07 | commit `84c3672` | 2 | Opciones eliminadas | `PlayNotOwnedGames` y `AutomaticFilterList` eliminados (config, parsing, `apps.cpp`); `AdditionalApps` único mecanismo de unlock | §7.4 |
| M82 | 2026-07-25 | commit `3620dd2` | 2/6 | SteamStub Palworld multicuenta | Precedencia invertida en `hkClientUser_GetSteamId`: spoof de un solo uso manda sobre el steamId del ticket cifrado cacheado; cita "One time spoof should take presedence, otherwise SteamStub will fail for games that use encrypted tickets for online auth when you play on multiple accounts" | §7.7.3 |
| M87 | 2026-07-26 | commit `293eb93` | 2/6 | Spoof de SteamId con FakeAppIds | Pros: tickets cifrados reales, AuthSessions; Cons: rompe activaciones Denuvo recientes y online Denuvo vía FakeAppIds. "Never spoof inside the Steamclient" (`utils->getAppId()` == 0 → sin tocar). Mecanismo indirecto: consulta caché con `utils->getAppId()` (falso) en vez de `getRealAppIdForCurrentPipe()`; guarda en `getCachedEncryptedTicket`: `if (realAppId && fakeAppId && appId != realAppId) { g_pLog->once("Returning empty cached encrypted ticket for %u because it's set to %u\n", ...); return ticket; }` | §7.7.4 |
| M87b | (§7.7.4) | LumaDeck `backend/fixes.py`, `GameDetail.tsx:390` | 2/6 | Rutas de FakeAppId en LumaDeck | Automática: sólo desde `[Main]` de `OnlineFix.ini` ("never a hardcoded 480"); manual "Native Online" → `add_fake_app_id(appid, 480)`. LumaDeck no toca la caché de tickets (cero referencias en `backend/`) | §7.7.4 (lado LumaDeck) |
| M88 | 2026-07-26 | commit `7663aef` | 2/12 | Redacción de apps privadas | `sendGamesPlayed`: `else if (!owned \|\| getFakeAppId(gameId))` → `else if (getFakeAppId(gameId))`; hook `SetString` del config store parsea `WebStorage\PrivateApps` (`[730,240,440]`) → escribe `"Redacted"` | §7.7.4 |
| M93 | 2026-07-27 | commits `3250c2c`, `6d1c7fd` | 2 | `g_currentSteamId` a 64 bits | `uint32_t` → `CSteamId`; usos en `apps.cpp` (`familyShared`, `unlockApp`, guard Denuvo), `ticket.cpp` (dos `saveTicketToCache`); ticket-grabber cambia formato; borra aviso de `globals.hpp` | §7.7.5 |
| M98 | 2026-07-28 | commits `3c520bd`, `b8b0b51`, `3b2e0d8`, `8d5c4f9` | 2/6 | Saga ticket cifrado | hook `IClientUser::GetEncryptedAppTicket` spoof one-shot y saca guarda FakeAppIds; `oneTimeSteamIdSpoof` → `unordered_map<AppId_t, CSteamId>`; revert: "Denuvo can be tricked by switching after a variable amount of GetSteamId calls. But it's to unreliable, needs custom amounts per game… So it's axed until I can find a safe timing", `.place()`/`.remove()` comentados; `getCachedTicket` devuelve `SavedTicket*` (`SavedTicket& ticket = ticketMap[appId];`) | §7.7.6 |
| M99 | 2026-07-28 | commits `f7926b5`, `4390e1a`+`a630b7e`/`8da82c2`, `c61df24`+`d942ada` | 2 | Config nueva | `SteamIdOverride` (`appId → steamId64`; `0` = ticket cacheado); `FakeName` (intercepta `CMsgClientPersonaState`, compara `friendid()` con `g_currentSteamId.steamId64`, reescribe `player_name`); `DenuvoGames` `uint32_t` → `uint64_t`/`CSteamId` (antes el guard comparaba mal) | §7.7.6 |
| M105 | 2026-07-30 | commit `146e28f` | 2/9 | `AppLicensesChanged_t` sólo para apps nuevas | "Previously we just invoked it for any appInfo received. Didn't cause any issues but was lazy"; `pendingLicenseChanges` con mutex; `const auto added = g_config.newApps; if (!added.size()) return; … pendingLicenseChanges.emplace(appId);` / `if (!pendingLicenseChanges.contains(app.appid())) continue; set.emplace(app.appid()); pendingLicenseChanges.erase(app.appid());`; diff en `config.cpp:203-212` | §7.7.7 |
| M105b | (§7.7.7.a) | traza de código (no on-device) | 2/9 | Live refresh LumaDeck vs SLSsteam | SLSsteam `AppLicensesChanged_t` vía `Apps::postAppLicensesChanged(set)` por diff de `AdditionalApps`; lumalinux `LicensesUpdated_t` (`ECallbackType` 0x7d) vía `CUser::NotifyLicensesUpdated(user)` por `keys.txt`. "no verificado on-device: no he comprobado cuál de los dos callbacks es el que realmente refresca la UI" | §7.7.7.a (coexistencia) |
| M113 | 2026-08-01 | commit `2281333` | 2 | `AppOwnershipInfo_t::region` | `char region[2]` + `char field7_0x1A[2]` → `char region[4]`; cita "Client copies this like a DWORD…"; tamaño total igual; nadie lee `region` (se usa `regionRestricted`) | §7.7.9 |
| M118 | 2026-08-05 | commit `50c439a` | 2 | Callbacks a FakeAppId | `DetourHook` patrón `CUser::PostCallbackToAppId`: `const AppId_t fakeAppId = FakeAppIds::getFakeAppId(appId); if (fakeAppId) { g_pLog->debug("Rerouting callback from %u to %u\n", appId, fakeAppId); appId = fakeAppId; }` | §7.7.11 |
| M119 | 2026-08-05 | commit `50eed0d` | 2 | Lobbies con FakeAppId | Hook `IClientFriends::GetFriendGamePlayed` (RTTI `12CUserFriends`): `if (fakeAppId && fakeAppId == gamePlayed->appId) gamePlayed->appId = realAppId;`; cita "We do not log this function, it's basically useless since we don't want any SteamIds in the logs" | §7.7.11 |
| M120 | 2026-08-05 | commit `913aca1` (config) | 2 | Aviso FakeAppIds | Línea "Do not run multiple apps under the same AppId simultaneously!" (en HEAD ampliada con "It's possible but will most likely cause undefined behaviour") | §7.7.11 |
| M120b | (§7.7.11) | LumaDeck grep `i18n.ts`, `GameDetail.tsx` | 2 | Aviso en UI | Ningún aviso sobre ejecutar varios juegos con el mismo 480; `slssteam_ops.list_fake_app_ids()` devuelve `{realId: fakeId}` | §7.7.11 (lado LumaDeck) |
| M121 | 2026-08-05 | commits `d566a87`, `f79b238`, `73a710c`, `c727ff1`, `de76936`, `ec239e6`, `6c85f93`, `05d89d9`, `a646d81`, `8d16704` | 2/1 | Refactors | `FakeAppIds::runIPCFrame` comprueba `shouldUseRealAppIdForInterface` él mismo; `EIPCInterface` → `enum class`; `steamId64 \|= 0x0110000100000000`; `IClientFriends.hpp` nuevo; `fakeAppIdMap` `unordered_map<uint32_t, AppId_t>` → `unordered_map<HSteamPipe, AppId_t>` (mismo typedef, cero cambio) | §7.7.11 |
| M132 | 2026-08-08 | commit `9ce2650` | 2 | AppId real desde `/proc` | Antes `AppId_t lastAppLaunched` global; ahora `getRealAppIdFromEnv(HSteamPipe)`: pid del `CServerPipe` → `/proc/<pid>/environ` → regex `SteamAppId=N` → `fakeAppIdMap[pipe] = appId` (cachea también 0); fallback `utils->getAppId()`; logs `"No SteamAppId in /proc/<pid>/environ! Using 0"`, `"Failed to open … to get %p's appId!"` | §7.7.14 |
| M133 | 2026-08-08 | commit `c2a3a2f` (config) | 2/12 | Requisito `/proc` | Comentario "#Requires access to /proc to read processes' real AppId from their environment / #(most distros allow this by default)" | §7.7.14 |
| M134 | 2026-08-12 | commit `d680874` | 2/11 | `/proc/<pid>/comm` en logs | `"Failed to open /proc/1234/environ for MyGame.exe to get 0x3's appId!"`; si `comm` falla: `"ExeName will be unknown in logs"` y `"Unknown"` | §7.7.14, §7.7.17 |
| M135 | 2026-08-08 | commit `14d9e32` | 2 | Alternativa descartada | Comentario "Replacing this call with an injected GetAppOwnershipTicketResponse is possible, but breaks in offline mode so we don't do that" | §7.7.14 |
| M138 | 2026-08-09 | commit `4afaede` | 2 | Strict aliasing | `-fakeAppIdMapPings[*reinterpret_cast<uint64_t*>(&details.address)] = realAppId;` → `+fakeAppIdMapPings[details.ip64] = realAppId;` con `union { servernetadr_t address; uint64_t ip64; }`; Makefile `-O3 -flto=auto -floop-block -fgraphite-identity -floop-parallelize-all -fomit-frame-pointer` | §7.7.15.a |
| M145 | 2026-08-12 (ventana 05-08 → 12-08) | commit `3d7b17f`; `git log res/version.txt` | 2/9 | "forgotten test" en `postCallback` | `-Hooks::CUser_PostCallbackToAppId.tramp.fn(this, 0, static_cast<unsigned int>(type), pCallback, callbackSize);` → `+const static auto fn = reinterpret_cast<void(*)(void*, ECallbackType, void*, uint32_t, uint32_t)>(Patterns::CUser::PostCallback.address); +fn(this, type, pCallback, callbackSize, 0);`; `CUser::postCallback` tiene exactamente dos llamantes (`feats/apps.cpp:271` y `:279`, en `Apps::postAppLicensesChanged`); entre `20260728212859` y `20260815201341` **no hay ninguna release** → nunca publicado | §7.7.17.a |
| M145b | (§7.7.17.a) | LumaDeck `slssteam_ops.py:126` | 2 | Comodín `0:` | LumaDeck escribe `"  {appid}: {fake_id}"`, nunca el comodín `0:` | §7.7.17.a (lado LumaDeck) |
| M163 | 2026-08-21 | commit `d056fda` | 2 | `process.cpp` | ~86 líneas de `/proc/<pid>/comm`+`environ` salen de `fakeappid.cpp` a `process.cpp` con `g_processMap` por pipe; "lo vamos a necesitar también para los tickets" | §7.9.2 |
| M164 | 2026-08-21 | commit `5882bed` | 2 | Ocultar family-share | Hook `IClientAppManager::GetAppStateInfo`: tras el original, `ownerAccountId` = usuario actual, `realOwner` = 0, limpia `k_EAppOwnershipFlagsBorrowed`; gated en `DisableFamilyShareLock` + appId de pipe activo; enum `EAppOwnershipFlags`, struct `AppStateInfo_t` | §7.9.2 |
| M165 | 2026-08-21 | commits `1b9b556`, `91a4336`, `de8d555`, `6dfaf3d`, `861b342` | 2/6 | PE/ELF + `SmartTickets` | Parsers PE/ELF 32 y 64 bits en `process.cpp`; nace `SmartTickets: no` | §7.9.2 |
| M177 | 2026-08-27 | commit `cf63d67` | 2/9 | Batch `setAdditionalApps` | `- auto _newApps = newApps.empty(); - auto _removedApps = removedApps.empty();` → `+ auto _newApps = newApps.get(); + auto _removedApps = removedApps.get();`; `MTVariable::empty()` **vacía** la variable; cada recarga descartaba altas pendientes | §7.9.8 |
| M178b | (§7.9.8) | código | 2/9 | Dos mecanismos de refresco | SLSsteam `AppLicensesChanged_t` (`0xf90be`); lumalinux `LicensesUpdated_t` (`0x7d`) vía `NotifyLicensesUpdated` | §7.9.8 (coexistencia) |
| M179 | 2026-08-28 | commits `f03ad94`, `76d9c7c` | 2 | No trackear antes de `GetSubscribedApps` | `- if (!firstLoad)` → `+ if (!firstLoad && Apps::applistRequested)` (comentario "No need to post a AppLicenseChanged_t callback when GetSubscribedApps hasn't been called yet"); `LuaMutex` RAII | §7.9.9 |
| M198 | 2026-09-20 | commits `ed347d7`, `e7e54f7` | 2/6 | `Process_t` endurecido | Defaults (`pid = -1`, `appId = 0`, `steamDRM = false`…) "so even when parsing fails we have fallback values"; `init` en `try/catch (...)` ("There might be some weirder procfs isolations"); `hkSteamEngine_ProcessIPCFrame` sólo llama `Ticket::connectPipe` si `init` devolvió `true` (antes siempre, también para `steam`) | §7.12 |
| M209 | 2026-09-28 | commit `39822da` | 2/6 | Spoof condicionado | `Ticket::getEncryptedAppTicket` sólo spoofea si `SmartTickets` incluye `k_ESmartTicketsDenuvo` | §7.14.1 |
| M222 | 2026-07-07 .. (§7.4) | LumaDeck `_set_playnotowned_no` | 2/11 | Impacto de eliminar `PlayNotOwnedGames` | LumaDeck devolvía falso "reinstall dependencies" cuando faltaba la línea; corregido 2026-07; headcrab antiguo aún fuerza `yes` | §7.4 |

#### Función 3 — DLC

| # | Fecha (como estaba escrita) | Dónde | F | Qué se midió | Resultado (literal) | Sección del doc anterior |
|---|---|---|---|---|---|---|
| M16 | (base ebfb079) | fuente `dlc.cpp`, vftable | 3 | DLC unlock | VFTHooks `IClientApps::GetDLCCount/GetDLCDataByIndex`, `IClientAppManager::BIsDlcEnabled/IsAppDlcInstalled` + `IsUserSubscribedAppInTicket` | §2 tabla |
| M89 | 2026-07-26 | commits `27f4926`, `f34025e`, `32d0ed0`, `a5abb50`, `0684cc3` | 3/11 | Menores | `27f4926` retira override manual de appId en hooks DLC (título dice `IClientAppManager`, diff toca `IClientApps`); `f34025e` `printf`→`g_pLog->debug`; `32d0ed0` `stringstream`→`ostringstream` 6 ficheros; `a5abb50` paddings `char`→`uint8_t`; `0684cc3` "Chocked"→"Choked" | §7.7.4 |
| M157 | 2026-08-20 | commit `6546411`, release `20260820085507` | 3 | Firma de `isUserSubscribedAppInTicket` | `- (IClientUser*, uint32_t steamId, uint32_t a2, uint32_t a3, AppId_t appId)` → `+ (IClientUser*, uint64_t steamId, AppId_t appId)`; notas de release "Fix DLC unlocker in games using AuthSessions" | §7.8.2 |

#### Función 4 — Claves y manifests

| # | Fecha (como estaba escrita) | Dónde | F | Qué se midió | Resultado (literal) | Sección del doc anterior |
|---|---|---|---|---|---|---|
| M187b | 2026-09-12 | plugin `download.lua` (slssteam-plugins-analysis §3.3) | 4 | Request codes | Pide a `gmrc.wudrm.com` en solitario y por HTTP en claro → muerto desde el día 9 (corregido en §7.11: volvió el 16-sep) | §7.10, §7.11 |

#### Función 5 — Updates de juegos

| # | Fecha (como estaba escrita) | Dónde | F | Qué se midió | Resultado (literal) | Sección del doc anterior |
|---|---|---|---|---|---|---|
| M36 | 2026-07-05 | commit `5c632dd` | 5 | Update-blocking v1 | Retirado VFThook `GetUpdateInfo` (devolvía `false`) por detour `GetAppStateInfo` que hace `&= ~APPSTATE_UPDATE_RUNNING`… | §7 pt 2 |
| M41 | 2026-07 (20260705) | binario `SLSsteam.so` 20260705132808 | 5 | Codegen del clear de flags | `-flto=auto -O3` colapsa seis `&=` en `and dword [reg+disp], 0xFFFFF8E5` (`0xFFFFF8E5 = ~0x71A = ~(REQUIRED\|QUEUED\|OPTIONAL\|RUNNING\|PAUSED\|STARTED)`); ancla `E5 F8 FF FF` con prefijo `81 /4`, exactamente 1 hit | §7.1 |
| M42 | (20260705) | fuente `apps.cpp` | 5 | Criterio de bloqueo | `shouldDisableUpdates(appId) = isAddedAppId(appId) \|\| !isSubscribed(appId)`; sin toggle de config; `UseWhitelist: yes` es global (caso thecatantirat/Cuphead) | §7.1 |
| M43 | **2026-07-07** | **codespace limpio**, SLSsteam `20260705132808` | 5 | Test end-to-end de `sls_update_unblock` | Balatro (2379780) en `AdditionalApps`, pinneado a manifest `3742336026811834465` vía `steamidra_lite --pin`, luego `--unpin`. Log `SLS-unblock: patched … -> and reg,0xFFFFFFFF`; al reiniciar: `Update Required → Update Queued → Update Running`, bajó gid `3512319404653808464` | §7.1 |
| M44 | 2026-07-14 | release `20260714131044` | 5 | Revert del bloqueo | Vuelta al hook de `GetUpdateInfo` gated por `DisableUpdates`; parche quedó código muerto, retirado en v0.16.18 (2026-07-23) | §7.1 aviso |
| M57 | 2026-07-14 | commit `c69d502`, release `20260714131044` | 5/4/2 | Config nueva | `ManifestIds` (versiones viejas + bloquear updates en OWNED), `DepotBlacklist`, rework de `AppIds` con parent AppId, revert del bloqueo de updates (solo unowned), fixes shortcuts non-Steam y playtime de refunds | §7.5 |
| M61 | 2026-07-23 | clon `AceSLS/SLSsteam@20260723102618` | 5 | Mecanismo nuevo de bloqueo | Hook vtable `IClientAppManager::GetUpdateInfo` → `return false` si `shouldDisableUpdates`; `shouldDisableUpdates`: `if (!g_config.disableUpdates.get()) return false; return isAddedAppId \|\| !isSubscribed`; `DisableUpdates` **yes por defecto** (`config_default.hpp`); grep de `0xFFFFF8E5` → cero | §7.6 |
| M219 | 2026-07 (issue #20) | LumaDeck issue | 5 | Síntoma del bloqueo de updates v1 | "Update required" sin bajar nada; `UseWhitelist: yes` global rompe unlock/DLC (caso thecatantirat / Cuphead) — segunda mano | §7.1 |

#### Función 6 — Fixes y DRM

| # | Fecha (como estaba escrita) | Dónde | F | Qué se midió | Resultado (literal) | Sección del doc anterior |
|---|---|---|---|---|---|---|
| M45 | 2026-07-01 | moon `3acab45` vs vanilla `ebfb079` | 6/2 | CD-key legacy | vanilla: `shouldDisableCDKey = !isSubscribed`; `shouldDisableUpdates = isAddedAppId \|\| !isSubscribed`; moon añade `isAddedAppId \|\| isAddedAppDlcId`, zerea out-param, `neutralizeLegacyCdKey` sobre `extended/hadthirdpartycdkey` | §7.2 |
| M47 | 2026-08-12 | commit `79905b0` | 6/2 | `CDKeys` + `GetLegacyCDKey` | `hkClientUser_GetLegacyCDKey` llama `Apps::getLegacyCDKey(appId)` antes del original; sale si `isSubscribed`; clave de `CDKeys` o determinista `srand(accountId + appId)`, 4×4 `A-Z0-9` `XXXX-XXXX-XXXX-XXXX`, fijada con `clientUser->setLegacyCDKey()`; se borra quitando `cdk_[n]` de `localconfig.vdf`; flag `silent` en `getSetting`/`getList`/`getMap`; log `"Added CDKey for %u"`. Cita del commit: antes desactivaban CD keys "porque no teníamos decompilador y la VFT de IClientUser cambiaba mucho" | §7.2 actualización |
| M149b | 2026-08-17 (rebajado) | LumaDeck `backend/steam_utils.py:440`, `downloads.py:1403`, commit `cda38b7` (2026-07-29), `GameDetail.tsx:188-196` | 6/9 | `set_compat_tool_for_app` | Escribe `CompatToolMapping` en `localconfig.vdf` con Steam vivo; docstring "Safe to call while Steam is not running"; no verificado on-device; rebajado: Steam Play global ON por defecto en Deck, pin redundante | §7.7.18.a (lado LumaDeck) |
| M166 | 2026-08-22 | commits `0fec99d`, `c1a958f`, `9571cdc`, `4aa94a7`, `79a45a7`, `cd10f2c`, `95d56fd` | 6 | Detección DRM | Denuvo: secciones características **y** entropía de `.text` > 7.0; escanea ficheros abiertos; `checkMagic()` en vez de extensión; `Elf_Sheader` → `Section_t`; `IExecutableFile::load()` recibe `LogLevelFlags_t` | §7.9.3 |
| M167 | 2026-08-22 | commit `2febc71` (`config_default.hpp`, `hooks.cpp:499-506`) | 6/2 | `SmartTickets` a flags | `- smartTickets = getSetting<bool>(rootNode, "SmartTickets", false);` → `+ smartTickets = getSetting<SmartTicketsFlags>(rootNode, "SmartTickets", 1);`; `0x1` = SteamDRM, `0x2` = Denuvo; medición de Ace: "less than 10ms for SteamDRM, up to 20-2000ms for Denuvo on my SSD"; análisis en `ConnectPipe` | §7.9.3 |
| M168 | 2026-08-22 | commit `f20ef6c` | 6 | Retira conteo de pipes Denuvo | "It only works on new games. I'd rather have consistent & predictable behaviour than something that sometimes works" | §7.9.3 |
| M197 | 2026-09-20 | commit `3ca4b73` | 6 | Parser PE/ELF | `strncpy` en búfer de 8 bytes sin terminador → 9 a cero; ELF `.at()`; `parseSections` devuelve resultado real | §7.12 |
| M200 | 2026-09-20 | commits `1298aa8`, `4a24b69` | 6 | `getOpenFiles`, `analyse` | Resuelve symlinks de `/proc/<pid>/map_files/*` y descarta no-regulares; no re-analiza el exe principal si aparece mapeado ("only affects wine games") | §7.12 |

#### Función 7 — Logros, stats y tiempo de juego

| # | Fecha (como estaba escrita) | Dónde | F | Qué se midió | Resultado (literal) | Sección del doc anterior |
|---|---|---|---|---|---|---|
| M19 | (base ebfb079) | fuente `achievements.cpp` | 7 | Achievements offline | `CAPIJob::GetPlayerStats` detour + `InitFromPacket` fuerzan `ERESULT_NO_CONNECTION` | §2 tabla |
| M39 | 2026-07-05 | commit `5c632dd` (`tools/`) | 7 | schema-grabber | Reemplaza SLScheevo (dejó de funcionar); batch lee `loginusers.vdf`/`libraryfolders.vdf` | §7 pt 6 |
| M48 | (julio 2026) | fuente `feats/achievements.cpp` | 7 | Guard del borrow | `if (g_pSteamEngine->getUser(0)->isSubscribed(appId)) return;` en `sendAndRecvGetUserStats` (`CMsgClientGetUserStats`) y `sendAndRecvGetPlayerStats` (`Player.GetUserStats#1`); `getReviewersForGame` → `store.steampowered.com/appreviews/<appId>?…&num_per_page=<MaxSchemaTries>`; `set_steam_id_for_user(id)` sobre el protobuf; reenvío por trampolín; limpia `achievement_blocks`/`crc_stats`/`stats`; `ownerBlacklist` `unordered_map` sin lock; `fork`+`curl` bloqueante | §7.3 |
| M49 | 2026-08-04 | commit `3b97ac2` | 7 | `preferredOwners` | Recuerda el dueño que funcionó (`preferredOwners[appId]`), salta el fetch de reviews; segundo `unordered_map` sin lock con `contains` → `at` → `erase`; "Comprobado a HEAD: cero mutex en todo `achievements.cpp`" | §7.3 nota |
| M50 | (v0.16.x) | Deck (OOBE) | 7 | Postmortem del parche `sls_achievement_unblock` | Escritura no atómica del `rel32` → hilo de Steam leyó destino a medias → `SIGILL` → wipe de gamescope; arreglado con `WriteRel32` atómico (detalle en RESEARCH §17) | §7.3 |
| M53 | 2026-07-11 (HEAD `d35a697`, VERSION `20260710192125`) | commits `4f3e607`, `0d6b3a8`, `4ab84f7`, `c063fb1`, `6e8d357`, `c12234f`, `aa316c4` | 7 | Evolución del borrow | protobufs en vez de scrapear HTML; blacklist por AppId; `MaxSchemaTries` configurable (default 10; `0` = sólo caché offline); auto-update de schema; refactor | §7.4 |
| M54 | 2026-07-11 | **Deck (on-device)** contra `d35a697`/20260711 | 7 | Validación `sls_achievement_unblock` | Anclajes (símbolos + patrón del guard) casan | §7.4 |
| M62 | 2026-07-23 | fuente @`20260723102618` (`.symtab`) | 7 | Símbolos del parche de logros | `_ZN5CUser12isSubscribedEj`, `_ZN7CConfig12isAddedAppIdEj`, `g_config`, `Achievements::sendAndRecvGetUserStats`, `...GetPlayerStats`; guard `call isSubscribed; add esp,imm; test al,al; jne` único por función (L105/L153); otros branches `85 C0` | §7.6 |
| M63 | 2026-08-17 (corrección) | commit `59f8259` (2026-07-20) + tabla release a release | 7 | Cambio de firma | Último parámetro de `sendAndRecvGetUserStats`: `const uint32_t targetType` → `const EMsg targetType`; mangled `…S3_j` → `…S3_4EMsg`. Tabla: `20260710192125` uint32_t; `20260714131044` uint32_t; **`20260722152506` EMsg (primera afectada)**; `20260723102618` EMsg; `20260728212859` EMsg | §7.6 corrección |
| M64 | (2026-07-23) | binario release | 7 | `.so` del release | Da 403 en el proxy; compilarlo no es reproducible → forma de bytes no confirmada | §7.6 |
| M96 | 2026-07-28 | commit `f624777` | 7 | Mangling | `CUtl*` `struct`→`class` con `public:` **no altera mangled names** | §7.7.6 |
| M103 | 2026-07-30 | commit `a673d04` | 7 | Buffer de lectura | 128 → 8192 bytes (64× menos syscalls) | §7.7.7 |
| M114 | 2026-08-02 | commits `03a4a96`, `fdefbe5`, `ae9428a` | 7/11 | Tools .NET | `OnDisconnected`/`OnLoggedOff` imprimían "Disconnected from Steam! Exiting..." sin `finished = true` → cuelgue en `schema-grabber`/`ticket-grabber`; borra `build.sh`; 4 espacios → tab en dos `Program.cs` (812 líneas) | §7.7.9 |
| M116 | 2026-08-04 | commit `3b97ac2` | 7 | Fix stutters | Cita: "Some games just ruthlessly spam GetUserStats, which in turn just spams getReviewersForGame which causes massive stuttering."; código `if (preferredOwners.contains(appId)) { const uint32_t res = tryGetPlayerStats(..., preferredOwners.at(appId)); if (res == k_EResultOK) return res; preferredOwners.erase(send->appid()); }`; `preferredOwners[appId]` fijado en `tryGetPlayerStats`/`tryGetUserStats` sólo con `k_EResultOK`; sólo `k_EResultFailure` quema un dueño, no `NoConnection`; escrituras en líneas 113 y 200, lecturas 142-150 y 221-230; "Comprobado a HEAD: cero mutex en todo `achievements.cpp`" | §7.7.10 |
| M169 | 2026-08-22 | commit `79a67ac` | 7 | Cooldown de logros | `Achievements::setCooldown()` marca el appId **10 minutos** sin schema; `getReviewersForGame()` sale temprano; llamadas dentro de `sendAndRecvGetPlayerStats`/`sendAndRecvGetUserStats` | §7.9.3 |
| M181 | 2026-08-29 (verificado "sobre el fuente, no sobre el binario") | `dev@3f8e429` | 7 | Supervivencia de `sls_achievement_unblock` | `_ZN5CUser12isSubscribedE` sigue (`sdk/CUser.cpp:37`, ni inline ni virtual); `_ZN7CConfig12isAddedAppIdE` (`config.cpp:324`); `g_config`; `_ZN12Achievements23sendAndRecvGetUserStatsE`; `_ZN12Achievements25sendAndRecvGetPlayerStatsE`; exactamente un `call isSubscribed` por función (`achievements.cpp:154` y `:236`); guard `call`+`test`+`jne` intacto | §7.9.10 |
| M214 | 2026-10-03 | `dev` commit `51724f5` | 7 | Endpoint de reseñas | `getReviewUrl`: `store.steampowered.com/appreviews/<app>?json=1…` → `api.steampowered.com/IUserReviewsService/GetAppReviews/v1/?appid=…&filter=1&languages[0]=all&review_type=0&purchase_type=1…`; "Store endpoint gets deprecated on 2026.10.22" (no verificado por nosotros); release instalada `20261001163836` sin el fix | §7.14.2 |
| M218 | (sin fecha; "cuando se root-causeó") | **Deck** | 7 | Release presente en el Deck al diagnosticar la rotura de `sls_achievement_unblock` | `20260728212859` era la instalada; la rotura se **detectó** ahí, se introdujo en `59f8259` (07-20) | §7.7.6 |
| M220 | (§7.6) | lumalinux log | 7 | Señales on-device del parche de logros | `SLS-ach: scoped the native-achievement guard…` vs `guard pattern not found exactly once`; `LUMA_SLS_ACH_TRACE=1`; `SLS-ach: could not resolve` | §7.6, §7.7.1 |

#### Función 8 — Cloud saves

(ninguna medición en el doc anterior)


#### Función 9 — Añadir y quitar un juego

| # | Fecha (como estaba escrita) | Dónde | F | Qué se midió | Resultado (literal) | Sección del doc anterior |
|---|---|---|---|---|---|---|
| M170 | 2026-08-22 | commit `1bbcbc8` (`apps.cpp:396-410`) | 9/6 | `LaunchOptions` | Por juego con `%command%`, en `Apps::spawnGame`; comodines `UINT32_MAX-1` (no poseídos) y `UINT32_MAX` (todos; el primero gana) | §7.9.3 |

#### Función 10 — Credenciales y proveedores

(ninguna medición en el doc anterior)


#### Función 11 — Mantenimiento propio

| # | Fecha (como estaba escrita) | Dónde | F | Qué se midió | Resultado (literal) | Sección del doc anterior |
|---|---|---|---|---|---|---|
| M24 | (base ebfb079) | fuente `filewatcher.cpp`, `config.cpp:75-86` | 11 | Hot-reload | `CFileWatcher` inotify en hilo `watchLoop`; `loadSettings()` recarga sin reiniciar; settings en `mtvar` | §4.1 |
| M25 | release `5c632dd` (2026-07-05) | commit | 11 | Fix del filewatcher | Variante `inotify_add_watch(fd, fichero, IN_MODIFY)` rota con rename atómico (sólo dispara una vez); nueva: directorio padre + `IN_CLOSE_WRITE` + filtro `event->name` + leer buffer entero (antes `sizeof(inotify_event)`) | §4.1 |
| M26 | 2026-08-15 | commit `1444fa5` | 11 | `IN_MOVED_TO` | `constexpr static int WATCH_MASK = IN_CLOSE_WRITE \| IN_MOVED_TO;` (`filewatcher.hpp`), en el `inotify_add_watch` y en el filtro del loop | §4.1 |
| M27 | (base ebfb079) | fuente `api.cpp` | 11/9 | API local | Fichero `/tmp/SLSsteam.API` observado con el filewatcher; `echo "install\|<appid>\|<library>" > /tmp/SLSsteam.API` → `IClientAppManager::installApp` | §4.2 |
| M28 | 2026-09-14 | (cierre, lectura ASSella) | 11 | Uso real del pipe | Único uso en el ecosistema: modo experimental de ASSella (`install\|appid\|0` tras DepotDownloaderMod) | §4.2 |
| M29 | 2026-08 | fuente SLSsteam (`createFile()`), headcrab `fe3f6ba`, Discord | 11 | Completado del config | `createFile()` es create-if-missing; al cargar toastea `"Missing key(s)"`; headcrab `updateSLSsteamConfig` (commit `fe3f6ba`, "headcrab updates configs now" en el Discord de SLSsteam) reescribe según `res/config.yaml` del release, backup `config.yaml.headcrab-<fecha>`, valida con `DisableFamilyShareLock:` | §4.3 |
| M34 | 2026-07-05 | commit `da97d11` (`20260705144737`) | 11 | Release pequeña | Cero código: bump `VERSION` 20260624075231 → 20260705144737 en `version.hpp` + `res/updates.yaml`; en la grande olvidaron subir la versión → el gate habría rechazado su propio build | §7 |
| M38 | 2026-07-05 | commit `5c632dd` | 11 | API fixes | Recursión infinita en la API arreglada; fix creación fichero API; comando `uninstall\|appid` nuevo | §7 pt 5 |
| M52 | 2026-07-11 | commit `f62d97c` | 11/12 | curl externo | libcurl in-process → `fork`+`execve("/bin/curl")` porque en SteamOS `libssl.3.so` crashea al curlear ciertas URLs | §7.4 |
| M58 | 2026-07-15 | commit `7d62c68`, release `20260715200441` | 11 | Fixes | Recursión infinita en exclusiones (parent↔hijo), mirror jsdelivr para `res/updates.yaml`, `curl --connect-timeout 15` | §7.5 |
| M67 | 2026-07-23 | commit `6725ff1` (`pkg/slssteam/PKGBUILD`) | 11/7 | PKGBUILD sin strip | `pkgver` → `20260723102618`; `#Disable stripping to not mess up disturb ticket-grabber` / `options=(!strip)` → `.symtab` intacta; `sls_achievement_unblock` resuelve `_ZN12Achievements25sendAndRecvGetPlayerStatsE*` y `_ZN12Achievements23sendAndRecvGetUserStatsE*` (no exportadas, sólo `.symtab`). Diagnóstico sugerido: `nm -a SLSsteam.so \| wc -l` | §7.7.1 |
| M84 | 2026-07-26 | commit `1250950` | 11 | Errores de config enumerados | Antes `enum ELoadError { None, MissingKey, ParsingException }` con severidad (`if (__loadErrors.get() > err) return;`), toast `"Issues during config loading encountered! Missing key(s)"`; ahora `__loadErrors` `std::string` acumulado, `setError(name)`, formato `Config loading errors:\nMissing DlcData\nMissing DenuvoGames\nFailed to parse IdleStatus`; emitido con `g_pLog->notify(errors.c_str())` → `system("notify-send … \"<msg>\"")` (format-string observación) | §7.7.4 |
| M85 | (escrito ~2026-08, §7.7.4) | HTTP a GitHub | 11 | URL de `config_default.hpp` usada por LumaDeck | Apunta a rama `master`, que **no existe** en `AceSLS/SLSsteam` (ramas `main` y `dev`); GitHub redirige y devuelve **200 idéntico a `main`** incl. `CDKeys`, `LogLevels`, `SteamIdOverride`, `FakeName` | §7.7.4 |
| M85b | (§7.7.4) | LumaDeck `_BUNDLED_YAML` vs upstream | 11 | Snapshot desfasado | 29 claves; faltan `CDKeys` (08-12) y `LogLevels` (08-09); arrastra `LogLevel` singular (upstream renombró a plural). El flag `silent` no suprime `setError` | §7.7.4 (lado LumaDeck) |
| M86 | 2026-07-26 | commit `62afc2e` | 11 | C→C++ en ruta de config | Antes `char pathBuf[255]` + `sprintf` desde `$XDG_CONFIG_HOME`/`$HOME`; ahora `ostringstream`; `getenv("HOME")` sigue sin comprobar; `createFile()` pasa de `fopen(path,"w")` a `std::ofstream(path, std::ios::app \| std::ios::out)` (revertido 07-27 `4e79bcd`) | §7.7.4 |
| M95 | 2026-07-27 | commits `4e79bcd`, `42a23ff`, `871f065`, `f91ceaf`, `9d6a05d`, `a9b5135`, `30acee8` | 11/2 | Higiene | `lock_guard` en `log.hpp`/`mtvar.hpp` + revierte `std::ios::app`; `SavedTicket::isValid()`; `map`→`unordered_map` tickets; Makefiles propios para `library-inject`, `schema-grabber`, `ticket-grabber`; nix `audit-libs`; easter egg 22 de febrero en `notifyInit` | §7.7.5 |
| M97 | 2026-07-28 | commits `a8c468d`, `f391f19` (PRs #146, #147) | 11/12 | Comunidad | `_drazy` / Deadboy666 actualizan `res/updates.yaml` | §7.7.6 |
| M100 | 2026-07-28 | commits `3f56397`, `e7eb27a`, `bfc3458` | 11/2 | Robustez | Crash al loguear en apagado (`ofstream.is_open()`); `getTicketOwnershipExtendedData` sólo si la original devolvió tamaño; `/run/current-system/sw/bin/curl` para NixOS en la cascada de `execve` | §7.7.6 |
| M101 | 2026-07-28 | commits `22b49e7`, `0c6d8ec`/`1906382`, `0919403`, `3f2bc38`, `81f4e8e`/`1a06744`/`f828ba5` | 11 | Release | Bump a `20260728212859`; PKGBUILDs; `updates.yaml`; borra `CSteamID.hpp`; estilo (typo `VFTable::analyze`) | §7.7.6 |
| M102 | 2026-07-30 | commit `cbd0cd0` | 11/7 | Fuga de fds en `Curl::getString` | `fork()`+`execve("curl")`+`pipe()`: si `fork()` falla no cierra el pipe; extremo de lectura nunca se cerraba; usada por app en `getReviewersForGame` | §7.7.7 |
| M108 | 2026-07-31 | commit `284115b` | 11 | Abort si falla la config | `CConfig::init()` retorna `false` si `createFile()` falla; cita "the tickets assume the config directory was created successfully"; `Ticket::getTicketDir()` = `<configDir>/cache` | §7.7.8 |
| M111 | 2026-07-31 | commits `e15dcce`, `57b80e3`, `5f946ec` | 11 | Menores | Renombre `hkBUpdateOwnershipTicket`→`hkBUpdateAppOwnershipTicket`; `if (!name.size())` → `if (name.size() < 1)` (idénticos) | §7.7.8 |
| M111b | (§7.7.8.b) | LumaDeck `main.py`, `backend/slssteam_config.py:88` | 11 | `set_sls_value` destructivo | `_read_yaml` hace `line.strip()` → pierde anidados; `_write_yaml` reescribe plano; cero llamadas desde `api.ts`/`pages/*.tsx`/`components/*.tsx`; backend corre root (`flags: ["_root"]`, `paths.py:561`), config queda root:644 (no verificado on-device) | §7.7.8.b (lado LumaDeck) |
| M125 | 2026-08-06 | commits `da69f54`, `38b3af1`, `556d2a6`, `dd85a69`, `e2cfdb2`/`4d4eab8` | 11 | Menores | `EIPCCmd_ToString`/`EIPCInterface_ToString`; log de `hkTraceIPC` antes de la llamada; `malloc`→`std::string` en `memhlp` | §7.7.12 |
| M137 | 2026-08-09 | commits `b556408`, `652a3b6`, `ebf28b4`, `9a54629`, `0f73549`, `dfed8e2`, `e578e5a`, `9aad713`, `9e51989`, `3b1be49` | 11 | Reescritura del logging | `__FILE__`/`__FUNCTION__`/`__LINE__` en `LOG_*`; niveles como flags `k_ELogLevelX = 1 << n`; off-by-one (empezaban en `1 << 1`) → `LogLevel: 4` pasa a `LogLevel: 3`; `traceOnce`; quita `NotifyWarn`/`NotifyError`, `ELogLevelCount = 8`; "we get actual format checks" + fix de printf malformados ("Using pedantic settings…") 21 min después; cita `e578e5a` "tracing will come in very handy when hunting down crashes" | §7.7.15 |
| M139 | 2026-08-11 (corrección de fecha) | `git log -S"LogLevels:" -- res/config.yaml` | 11 | Fecha de `LogLevels` | Clave `LogLevels` (plural) aparece en `b5b1315`, 2026-08-11 ("refactor(log): Make logging smarter"); tres pasos: 09-08 flags, 10-08 `82afc5d` test de máscara, 11-08 renombre; LumaDeck `a061b00` citó "b556408/ebf28b4, 2026-08-09" (impreciso); `_BUNDLED_YAML` verificado byte-idéntico a la URL; default `LogLevels: 0xff` | §7.7.15.b |
| M140 | 2026-08-09 | commits `8f450e8`, `cfc3bfb`, `3882c46` | 11/4 | Menores | Script Debug+Release; `3882c46` quita arg `appId` sin usar de `Apps::buildDepotDependency` (helper interno, no la firma del hook `CUserAppManager::BuildDepotDependency`); su handler de BuildDep no filtra por app | §7.7.15 |
| M141 | 2026-08-10 | commits `82afc5d`, `8097864` | 11 | Máscara de log | `-if (flags < getMinLevel())` → `+if (!(g_config.logLevel.get() & flags))`; desaparece `getMinLevel()` ("dirty workaround for not being able to access g_config from __log"); `ELogLevelCount` 8 → 9, bucle `i = ELogLevelCount - 1` (sin cambio neto) | §7.7.16 |
| M142 | 2026-08-11 | commit `d5a17c7` | 11 | `Notifications` eliminada | `-if (shouldNotify() && notification.size() > 0)` → `+if (notification.size() > 0)`; fuera `notifications`, `CLog::shouldNotify()`; control vía `LogLevels` bits `k_ELogLevelNotifyShort` = `0x40`, `k_ELogLevelNotifyLong` = `0x80` (default `0xff`); migración `0x3F` | §7.7.16 |
| M143 | 2026-08-11 | commits `b5b1315`, `49cc2dc`, `37a29f7`, `cd43f3b`, `7059664` | 11 | Logging asentado | `LogLevel` → `LogLevels`; `LOG_CUSTOM(Info \| Once, …)` para volcado de config ("Prevents the logfile bloating to hell and back with big configs"), dedupe por `msgHist`; `LogLevels` se carga primero ("Otherwise on first load settings won't get logged"); `DlcData` vía `getMap` | §7.7.16 |
| M143b | (§7.7.16) | grep LumaDeck | 11 | `Notifications` | LumaDeck no escribe `Notifications` en ningún sitio | §7.7.16 (lado LumaDeck) |
| M144 | 2026-08-12 | commit `2b4b411` | 11 | Info en notify | Añade `k_ELogLevelInfo` a `notify`/`notifyLong` "So it still gets logged when users turn of notifications" | §7.7.16, §7.7.17 |
| M146 | 2026-08-12 | commits `f0dab0e`, `df64d1d`, `ce8ce33`, `9d88160`, `dbb3a09`, `4d99883`, `25e1a2f`, `ffc66d6` | 11/1 | Tipado | `void*` → tipos reales en hooks; `sdk.hpp`; `CServerPipe` ampliado; `ffc66d6` `uint32_t type` → `EWebSocketConnectionSendType` (`: uint32_t`, cero ABI) | §7.7.17 |
| M148 | 2026-08-14 | commits `aebac96`, `b7407e4`, `89c70c8`, `1dc0fdd`, `75ba3eb`, `4eecaa4`, `6e8951e`, `4135d51`, `aa35638`, `ac6830b`, `06603a0`, `9c350c1` | 11/9 | API local a 6 comandos + `API.md` | `Utils::tryConvertToNumber`; `InstallOp_t`/`LibraryOp_t`; `setcompat`/`getcompat`/`dumpcompat`/`dumplibraries` vía `IClientCompat` (`src/sdk/IClientCompat.hpp`); `API.md`: `dumplibraries`, `install\|appId\|libraryIndex`, `uninstall\|appId`, `dumpcompat\|appId`, `getcompat\|appId`, `setcompat\|appId[\|tool-name]`; `k_ELogLevelAPI`; `CUtlString`, `CUtlMap`, `CUtlRBTree` | §7.7.18 |
| M149 | (§7.7.18.a) | fuente `res/config.yaml` línea 110 vs `getSetting<bool>(node, "API", true)` | 11 | Default de `API` | Fichero enviado dice `API: no`; default en código es `true` | §7.7.18.a |
| M151 | 2026-08-15 | commit `665fc8c` | 11 | API: primer comando ejecutado dos veces | `fstream.close(); fstream.open(path, std::fstream::in);` sobre stream abierto; `isEnabled()` usaba `is_open()`; `echo >` dispara 2 eventos inotify; fix: `bool initialized`, abre-lee-cierra con `goto done`; `strcmp` → `==`. Presente en versiones < `20260815201341` | §7.7.19 |
| M152 | 2026-08-15 | commit `182fdf0` | 11 | Makefile flag-aware | `FLAGSSHA := sha256sum` de `CXXFLAGS + LDFLAGS`; `obj/$(FLAGSSHA)/`, `bin/SLSsteam-$(FLAGSSHA).so`, `link-bins` con `ln -f`; `embed-config.sh`/`embed-version.sh` sólo reescriben si cambió | §7.7.19 |
| M153 | 2026-08-15 | commits `c73e40b`, `505b2c5`, `97364a2` | 11/1 | Release `20260815201341` | `res/version.txt` y `src/version.hpp` de `20260728212859` a `20260815201341`; PKGBUILDs venían de `20260801163409`; SafeMode: `20260815201341: #tag 20260815201341 / - d0c0ff6e...a6b3df8900 #ubuntu32_32 & steamdeck_stable - 20260804` (**un solo hash**, el anterior listaba dos) | §7.7.19 |
| M159 | 2026-08-15 release (verificado "contra el upstream de hoy", §7.8.4) | `config_default.hpp` | 11 | `Notifications` desaparecida | `NotifyInit` y `LogLevels` siguen; `Notifications` no; SLSsteam sólo reporta claves faltantes (`ELoadError::MissingKey`, `config.cpp:121`); `setup.sh:210` `sed -i "s/^Notifications:.*/Notifications: yes/"` muerto; mensaje línea 234 "NotifyInit/Notifications=yes" | §7.8.4 |
| M162 | 2026-08-20 | commits `7186151`, `5d01066` | 11 | API multilínea | Antes leía **una** línea de 128 bytes; ahora fichero entero línea a línea; `goto done` → `return`; `lock_guard` al inicio de `parseCmd()` | §7.9.1 |
| M178 | 2026-08-27 | commits `9c9de32`, `407b572`, `f497d6d`, `14034de`+`5d22865`, `8397c56`/`df92dc5`/`9a4f779`, `f801adb` | 11/1 | Fixes | `eventMask` configurable se ignoraba (`inotify_add_watch` con `WATCH_MASK`); full reload de Lua sólo en DEBUG; recarga silenciosa de config (flag `silent`); `stateMutex`; `YAMLNode` | §7.9.8 |
| M184 | (§7.9.13) | fuente `config.cpp:127` | 11 | Claves nuevas | `SmartTickets`, `LaunchOptions`, `Plugins`; `ELoadError::MissingKey`; LumaDeck `_CONFIG_DEFAULT_URL = "https://raw.githubusercontent.com/AceSLS/SLSsteam/main/src/config_default.hpp"` | §7.9.13 |
| M187 | 2026-09-03 y 08 | `dev` commits `faeaf9b`, `a456396`, `bbe1e3f` | 11/2 | `dev` sin publicar | newlines en logs; bypass de controles parentales en `ClientLogOnResponse`; crash en `Process_t::getRealExe` con `/proc` inaccesible | §7.10 |
| M189 | 2026-09-12 | commits `6956e2b`, `e6082d0`; issue **#158** (Zyggarg, 11-sep) | 11 | Filewatcher `running` / EINTR | `running = true` antes de crear el hilo; `read()` `-1` con `errno == EINTR` → `continue`; cita issue: "System signals (like OS performance profile changes or thread teardowns) trigger errno == EINTR … break treats it like one and dies"; Ace: "pretty ghetto and shouldn't happen in the first place. But I got 2 reports of it randomly happening" | §7.11 |
| M190 | 2026-09-14 | commit `cf79c27` | 11 | `tryConvertToNumber` | Gana `int64_t`/`uint64_t` (`stoll`/`stoull`) | §7.11 |
| M196 | 2026-09-17..19 | commits `2c021bd`, `65d4071`, `ae4d4c7` | 11 | Menores | Typo README Lua; comentarios `CServerPipe`; log de `RecvPkt` con `refs`, `body`, `originalBody` ("Ref counted pointer") | §7.12 |
| M199 | 2026-09-20 | commit `2a538fd` | 11 | `strsplit` por regex | `strtok` → `std::regex` + `sregex_token_iterator`; firma `(const std::string&, const char*)`; `process.cpp:698` `strsplit(readFile("cmdline"), "\0")` → delimitador = cadena vacía → `cmdLine` troceado por caracteres; `cmdLine` no se lee (`git grep`) | §7.12 |
| M203 | 2026-09-22 | commits `ae5cbf1`, `32d6d96` | 11 | Log de arranque + `config_default.hpp` fuera de git | `SLSsteam (<rama> -> <commit>) loading in <proceso>` a INFO; `embed-version.sh` genera `BUILD_BRANCH` y `LAST_COMMIT_HASH`; `src/config_default.hpp` borrado (168 líneas), en `.gitignore`; `res/config.yaml` idéntico byte a byte al YAML del `.hpp` (verificado con diff) | §7.13 |
| M204 | 2026-09-22 / 24 / 25 | commits `d659a66`, `6d9ad4e`, `1c2cc92` | 11/2 | Fixes | `Config::logLevels` arranca en `0xff`; `hkUser_CheckAppOwnership` devuelve `bool` (antes `uint32_t`, funcionaba porque i386 lee `al`); `strsplit` toma `const std::string&`, `cmdline` partido con `std::string("\0", 1)` (cierra nota de §7.12) | §7.13 |
| M210 | 2026-10-01 | commit `42568f0` | 11 | API rota en `20260930144343` | `SLSAPI::parseCmd`: `strsplit(cmd, "\\|")`; desde `2a538fd` el `"\|"` era alternancia vacía → comandos no se partían; arreglado en `20261001163836` | §7.14.1 |
| M212 | 2026-10-07 | `git show 20261001163836:res/version.txt` | 11/1 | Versión sin subir | Ambas releases llevan `res/version.txt = 20260903114323`; `verifySafeModeHash` usa la entrada `20260903114323` (`bc54101b…`, `237495b4…`); LumaDeck `SafeMode: no` (`installer.py:164`); `derive_version_floor` subestima; `.slssteam.version` = `20261001163836`; `~/.SLSsteam.log` arranca con `SLSsteam (main -> 42568f0) loading in <proceso>` | §7.14.1 |
| M215 | 2026-10-02/03 | `dev` commits `71e3eb3`, `f64b2ab`, `b10181d`, `10b5cf1`, `0f21f31`, `993f691` | 11/1 | Menores | "pattern found N times", "unable to find signature", "failed to disassemble" pasan de DEBUG a WARN/ERROR; `__attribute__((hot))` en `hkSteamEngine_ProcessIPCFrame`; merges | §7.14.2 |
| M217 | 2026-10-07 | LumaDeck | 11 | Accionable de §7.13 hecho | `slssteam_schema.py` ya lee `res/config.yaml`; `src/config_default.hpp` da 404 en `main` desde el merge | §7.14.4 (lado LumaDeck) |
| M221 | (§7.8.1, sin fecha ni commit) | release assets | 11 | Renombrado de assets de release | "Nos rompió el instalador 44 h; ya arreglado" | §7.8.1 |
| M223 | (§7.7.4) | SLSsteam config | 11 | Claves ignoradas | "una clave desconocida se ignora en silencio; sólo toasta lo que SLSsteam espera y no encuentra" (coherente con `config.cpp:121/127` `MissingKey`) | §7.7.4, §7.8.4, §7.9.13 |

#### Función 12 — Proyecto

| # | Fecha (como estaba escrita) | Dónde | F | Qué se midió | Resultado (literal) | Sección del doc anterior |
|---|---|---|---|---|---|---|
| M1 | 2026-07-06 (fecha de investigación) | commit `ebfb079` | 12 | Base del análisis | `VERSION = 20260624075231`, i386, C++20/CMake, libmem. Re-verificado contra `20260705132808` (`5c632dd`) y `20260705144737` (`da97d11`) | Cabecera |
| M65 | 2026-08-17 (cierre) | clon completo `69594b9`→`01a3b1e` | 12 | Tamaño de la ventana | 205 commits, 2026-07-23 → 2026-08-15, releases `20260728212859` y `20260815201341`, +43.7k/−25.1k líneas (~38k = regeneración de protobufs del 08-07, lite→completo) | §7.7 |
| M110 | 2026-07-31 | commit `ac12369` (README) | 12 | Hall of shame | "OnetapBeta & Hammer Decky by Hammer Steam: Resells Steamless & SLSsteam for a bogus price, completely breaking licensing agreements and leeching off the communities hard work while putting in 0 effort themself." | §7.7.8, §7.7.8.a |
| M126 | 2026-08-07 | commit `4b29945` | 12 | ProtoBuf lite → completo | +38k/−22k líneas; cita "We're gonna need it from now on. Mostly for dumping messages" | §7.7.13 |
| M147 | 2026-08-13 | commits `0227a09`, `1bdf1a6` (DeveloperMikey) | 12 | Contribución externa | Fix build nix; segundo aporte externo del rango | §7.7.18 |
| M155 | 2026-08-15 | commits `5d76d5b`, `143dcba`, `a70939b`, `01a3b1e` | 12 | Markdown de `API.md` | "Fix docs again I hate github's markdown. Why is it so weird..."; `01a3b1e` = HEAD del rango | §7.7.19 |
| M156 | 2026-08-15 … 2026-08-20 | tags `20260819120840` (sin release), `20260819131545`, `20260820085507` | 12 | Ventana §7.8 | 34 commits en 5 días; 9 SDK (16 y 19-ago), 1 firma, 1 feed, 3 logging, 1 renombrado de assets ("nos rompió el instalador 44 h"), 19 chore | §7.8, §7.8.1 |
| M161 | 2026-08-20 → 2026-09-01 | clon `65b6ee1` (= tag `20260820085507`) → `dev@3f8e429` | 12 | Ventana §7.9 | 112 commits, +22k/−1.2k (~16k LuaBridge 31-ago); cero releases/tags; `main` clavado en `65b6ee1` trece días; 34/112 commits son Lua | §7.9 |
| M172 | 2026-08-23 | commits `82a9e85`, `49186e7`, `5f9d9b4`, `78789fb` | 12/11 | Menores | `lib/libluajit.a` al repo sin usar; `CFileWatcher*` a smart pointers; `MTVariable` acelerado | §7.9.4 |
| M185 | 2026-09-12 (barrido) | clon `main`/`dev` | 12 | Ventana §7.10 | Nada después del 9-sep en `main` ni `dev` tras el 8; 14 commits de `main` 1→3 sep; 3 de `dev` (3 y 8 sep) | §7.10 |
| M188 | 2026-09-17 (barrido) | clon fresco; `main@71021ad`; `dev` 4 commits 12–16 sep | 12 | Ventana §7.11 | Sin commit/tag/release en `main` | §7.11 |
| M193 | 2026-09-17 | issues GitHub | 12 | Issues | #157 (HANDZCZ, crash con SamRewritten, bisecado a `d056fda`) cerrada 8-sep, arreglado por `bbe1e3f`; #136 y #124 cerradas el mismo día; #158 abierta; #155 (cliente 1-sep) cerrada 4-sep | §7.11 |
| M194 | 2026-09-22 (barrido; issues 403) | `dev` 10 commits 17–20 sep | 12 | Ventana §7.12 | `main@71021ad` sin cambios | §7.12 |
| M202 | 2026-09-28 (barrido; issues leídas) | `dev` 5 commits 22–25 sep | 12 | Ventana §7.13 | `main@71021ad` sin cambios desde 3-sep | §7.13 |
| M205 | 2026-09-28 | issues GitHub | 12/7/2 | Issues | #159 (kaunkrishna, 27-sep): horas/logros de SLScheevo al comprar, Steam Cloud; #158 abierta; #156 (Ace, 4-sep, "LuaHook issues") abierta; #46 (Ace, nov-2025, "Problems with FakeAppIds") lista *tickets* y *achievements* como rotos al cambiar AppId | §7.13 |
| M207 | 2026-10-07 (barrido; issues 403) | `main` `71021ad` → `049bbdd`; `dev` fusionado; `dev` 11 commits 2–6 oct | 12 | Ventana §7.14 | Releases **`20260930144343`** (tag `39822da`, 28-sep) y **`20261001163836`** (`42568f0`, 1-oct; la de `.slssteam.version` en el codespace) | §7.14 |
| M211 | 2026-09-28..10-06 | commits `5774941`, `9c829a7`, `cd83d69`, `9e19510` (DeveloperMikey), `e84bbb8`, `049bbdd` | 12 | Menores + README | Nix; Docker `git`; README quita "ADS": "They asked for collab, yet didn't add proper support. They also supposedly collect HWIDs of their users which is a huge nogo" | §7.14.1 |

#### Mediciones del lado lumalinux / LumaDeck que vivían en el doc anterior

Son de nuestro stack, no de SLSsteam; se conservan aquí para no perderlas (la relectura de `nosotros.md` no las tomó de este doc). Van a `nosotros.md` §5.2 cuando se consolide.

| # | Fecha | Dónde | Qué se midió | Resultado (literal) | Sección del doc anterior |
|---|---|---|---|---|---|
| M69b | (sin fecha, §7.7.1.c) | **lumalinux**, `-O2`, Xeon @ 2.1 GHz, maps sintéticos 1.5k/4k líneas, `.text` 9 MB | Coste de `patterns.cpp` de lumalinux | parseo maps 0.064 / 0.219 ms; `SigScan` 9 MB 18.6 / 19.0 ms; bucle unique 20.1 / 19.9 ms; 8× parseos 0.51 / 1.75 ms; 4× escaneo 74.5 / 76.1 ms; ratio 145× / 43×. 4 escaneos por arranque en Deck (DepotKey, GMRC, ShaderDepot, Reconcile) | §7.7.1.c (lado lumalinux) |
| M78b | (RESEARCH §15.2, build f5eb8bd3) | lumalinux | Virtualidad de los 5 targets de lumalinux | DepotKey SÍ (`CConfigStore` slot 6); GMRC no; BuildDep no; ShaderDepot no; LoadPackage no | §7.7.2.a (lado lumalinux) |
| M80b | (§7.7.3.a) | lumalinux `src/patterns.hpp` | Anclas de los 6 patrones de lumalinux | `kDepotKeyFnPattern` `55 57 56 53 E8 ??…` (45 bytes, ~37 fijos); `kBuildDepotDependencyPattern` `55 89 E5 57 56 E8`; `kLoadPackagePattern` `55 89 E5 57 E8`; `kGmrcFunctionPattern` `E8 ?? ?? ?? ?? 05 ?? … 55 89 E5`; `kShaderCacheDepotPattern` `57 56 53 E8 ?? … 81 C3`; `kNotifyLicensesUpdatedPattern` `55 89 E5 57 56 53 E8`; deriva documentada 0x1b18→0x1b14 | §7.7.3.a (lado lumalinux) |
| M94b | (§7.7.5.a) | lumalinux | Hash doble, timeouts | SHA-256 12 MB ~12 ms OpenSSL; duplicado 25-50 ms; `Curl::getString` defaults `connectTimeoutSec = 15`, `totalTimeoutSec = 30`; peor caso ~60 s; incidente `CURL_OPENSSL_4` en `reaper` (0.13.6 → 0.15.0) | §7.7.5.a (lado lumalinux) |
| M114b | (§7.7.9) | lumalinux `src/main.cpp:218-241`, grep | Reporte de hooks en lumalinux | `InstallHooks()` recorre todos, `Status::RecordHook` INSTALLED/DISABLED/FAILED, `X/Y hooks active`; cero referencias a `AppOwnershipInfo` en `src/` | §7.7.9 (lado lumalinux) |
| M125b | (§7.7.12.a) | lumalinux `load_package_hook.cpp::AppendIdsToVec`, `package_zero_finder.cpp:348`, `main.cpp:5`, `:19` | `std::realloc` sobre memoria de Steam | `void* new_mem = std::realloc(vec->m_pMemory, new_alloc * sizeof(uint32_t));` en ruta viva por defecto (finder ON, `LUMA_NO_PKG0_FINDER` lo apaga); comentario "CUtlMemory is malloc-backed on Steam Linux i386"; historial v0.3 "allocator issues — abandoned"; v0.5.6 dice "In-place append only (no risky manual realloc)" (incoherente) | §7.7.12.a (lado lumalinux) |
| M131b | (§7.7.13.a) | grep lumalinux `src/` | Capa de mensajes | Cero `#include` de protobuf, cero `CNetPacket`/`EMsg`/`CMsgClient*` (salvo comentario en `sls_achievement_unblock.cpp`) | §7.7.13.a (coexistencia) |
| M138b | (§7.7.15.a) | lumalinux flags | Aliasing en lumalinux | `-m32 -D_GLIBCXX_USE_CXX11_ABI=0`, `-Wall -Wextra -Wpedantic`, `-fno-reorder-blocks-and-partition`; ningún type-punning de miembro tipado | §7.7.15.a (lado lumalinux) |
| M158b | (§7.8.3) | **medido compilando contra yaml-cpp** (lumalinux `src/update.cpp:39-51`, `:80`) | Cuerpos HTTP 200 vs `YAML::Load` | Vacío: parsea, 0 entradas, envenena caché, toast; JSON de error: parsea, 0, envenena, toast; HTML: lanza, no envenena, toast; `Guru Meditation:`: lanza; `Not Found`: lanza; feed bueno: 1 entrada. `setup.sh:224` `_sls_ensure_kv SafeMode no`; `main.cpp:145` "Mirrors SLSsteam's SafeMode=no" | §7.8.3 (lado lumalinux) |
| M160 | 2026-08-20 | commits `49e4279`, `51297c4` (lumalinux) | Cierre de §7.8 | Validación del cuerpo del feed y `sed` muerto cerrados el 20-ago | §7.9 preámbulo (lado lumalinux) |
| M189b | (§7.11) | lumalinux `src/key_store.cpp:194-199` | EINTR en el watcher de `keys.txt` | Ya hace `if (n < 0 && errno == EINTR) continue;` | §7.11 (lado lumalinux) |

#### Verificaciones de la relectura (2026-10-08, `main@049bbdd`, lectura estática)

| Fecha | Dónde | F | Qué se midió | Resultado |
|---|---|---|---|---|
| 2026-10-08 | fuente `sdk/CUser.cpp:33-46` | 2/7 | `CUser::isSubscribed` | `checkAppOwnership` = `Hooks::CUser_CheckAppOwnership->tramp.fn(this, appId, pInfo)`; `isSubscribed` = `info.ownsLicense && !info.licenseExpired` tras llamar al trampolín. Propiedad real según Steam, sin el spoof |
| 2026-10-08 | fuente `feats/achievements.cpp:154,236` | 7 | guard del borrow | `if (g_pSteamEngine->getUser(0)->isSubscribed(send->appid()))` / `(sendBdy->game_id())` → return; `:21` URL `store.steampowered.com/appreviews/`; `:188`/`:268` `k_EResultNoConnection`; `:96` `setCooldown` |
| 2026-10-08 | `grep -rn 64 src/feats/dlc.cpp src/config.cpp` (sin `uint64/int64/steamId64/base64`) | 3 | límite de 64 DLC | **cero** coincidencias; `res/config.yaml:40-42` "Only needed when the App you're playing is hit by Steams 64 DLC limit" |
| 2026-10-08 | fuente `feats/apps.cpp:93-142` | 2 | `Apps::checkAppOwnership` | guarda `!applistRequested \|\| !pInfo \|\| !g_currentSteamId.isSet()` → false; Denuvo de otro → false; `shouldExcludeAppId` → false; decensor + región + timestamps para todas; `unlockApp` solo si `isAddedAppId` |
| 2026-10-08 | fuente `feats/apps.cpp:24-56` | 2 | `unlockApp` | `owner = ownerId.accountId()`, `realOwner = 0`, `familyShared = owner != cuenta`, `licensePermanent = !familyShared`, `ownsLicense = true`, `releaseState = Released`, `freeLicense = familyShared`; sobrecarga de 2 args usa `g_currentSteamId` |
| 2026-10-08 | fuente `feats/apps.cpp:236-252` | 2 | `getSubscribedApps` | "`//TODO: Maybe Add check if AppId already in list before blindly appending`"; `applistRequested = true` tras rellenar |
| 2026-10-08 | fuente `config.cpp:329-364` | 2/9 | `setAdditionalApps` | diffs solo `if (!firstLoad && Apps::applistRequested)`; comentario "No need to post a AppLicenseChanged_t callback when GetSubscribedApps hasn't been called yet" |
| 2026-10-08 | fuente `feats/apps.cpp:434-458` | 5/8 | `shouldDisableCloud` / `shouldDisableUpdates` | cloud: `!disableCloud → false; return !isSubscribed`; updates: `!disableUpdates → false; return isAddedAppId \|\| !isSubscribed` ("Using AdditionalApps here aswell so users can manually block updates"); `shouldDisableCDKey = !isSubscribed` sin llamadores |
| 2026-10-08 | fuente `feats/dlc.cpp:8-27` | 3 | `shouldUnlockDlc` | `!getUtils()->getAppId() → false; isSubscribed → false; shouldExcludeAppId → false; true` |
| 2026-10-08 | fuente `config.cpp:162,177-189` | 11 | defaults en código | `API` **true**, `DisableFamilyShareLock` true, `UseWhitelist` false, `MaxSchemaTries` 10, `SmartTickets` 1, `SafeMode` false, `WarnHashMissmatch` false, `NotifyInit` true, `Plugins` false, `DisableCloud` true, `DisableUpdates` true |
| 2026-10-08 | fuente `hooks.cpp:1495-1516` | 1 | hooks duplicados | los seis `new VFTHook(vft, VFTIndexes::IClientAppManager::…)` aparecen dos veces, literalmente iguales; `Hooks::removeAll` definido en `:1575` y solo referenciado en `hooks.hpp:279` |
| 2026-10-08 | `git tag --sort=creatordate \| tail -3`; `cat res/version.txt` | 11 | versión | tags `20260903114323`, `20260930144343`, `20261001163836`; `res/version.txt` = `20260903114323` |
| 2026-10-08 | fuente `feats/ticket.cpp:20-46` | 6 | ruta de tickets | `getTicketDir` = `g_config.getDir()/cache` (creado si falta); `ticket_<app>.yaml` |
| 2026-10-08 | `cat src/*.cpp src/*.hpp src/feats/* src/sdk/*.cpp src/sdk/*.hpp \| wc -l` | 12 | tamaño | 11 972 líneas sin protobufs |

### 5.3 Cronología (desde 2026-07-05)

**Topología.** `origin/main` empieza en un commit raíz **sin padre**
(`22f4880`, 2026-07-21, "initial function parser": snapshot de 739 ficheros).
`git merge-base 20260715200441 origin/main` está vacío: los tags
`20250510134347` … `20260715200441` y el tag `update` son huérfanos y siguen
en el remoto. Para cubrir desde 07-05 se leyeron tres rangos: la historia
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

**Lo que vio el doc anterior y conviene recordar** (detalle en §5.2):
`5c632dd` 07-05 reescribió el filewatcher (directorio padre +
`IN_CLOSE_WRITE`), wildcardeó patrones y añadió `uninstall` a la API (M25,
M35, M38); el parche `sls_update_unblock` de lumalinux se probó end-to-end el
07-07 contra el bloqueo v1 y murió con `20260714131044` (M43, M44); el cambio
de firma `uint32_t → EMsg` en `sendAndRecvGetUserStats` (`59f8259`, 07-20)
dejó mudo seis días a `sls_achievement_unblock` y se detectó en
`20260728212859` (M63, M218); la escritura rasgada del `rel32` del mismo
parche tiró Steam en Game Mode hasta el OOBE (M50); issues #157 (crash con
SamRewritten, bisecado a `d056fda`, arreglado por `bbe1e3f`), #158 (EINTR),
#159 (horas y logros al comprar), #156 (LuaHook), #46 (FakeAppIds rompe
tickets y logros) (M193, M205).
