# LumaCore (drappula/LumaCore) — la DLL, leída desde el código

Leído desde el código el 2026-10-08. Sustituye a `lumacore-findings.md`: sus
mediciones con fecha están en §5.2, lo que afirmaba y el código ya no sostiene
en §5.1, y sus decisiones con el estado de hoy en §4.4. Lo que aquel doc decía
de la **app** SteaMidra (lanzador Python, precalentado de patrones, base de
claves de KoriaPolis) ya lo absorbió `steamidra.md`; aquí solo se lee la DLL.
Cómo **nosotros** hacemos lo mismo está en `nosotros.md`; cómo lo hace SLSsteam,
en `slssteam.md`.

---

## §0 Ficha

| | |
|---|---|
| **Qué es** | La **mitad DLL de SteaMidra en Windows**: cuatro ficheros que el lanzador copia junto a `steam.exe`. `xinput1_4.dll` y `dwmapi.dll` son proxies que Steam carga de su propia carpeta y que cargan `LumaCore.dll` desde un hilo; `LumaCore.dll` se engancha **dentro del proceso de Steam** (Microsoft Detours sobre `steamclient64.dll` y `steamui.dll`, dos trampas `int3` con VEH, reescritura de mensajes protobuf del CM y del IPC cliente↔juego) y lee los `.lua` de `config/stplug-in/` **ejecutándolos en un Lua 5.5 con sandbox**; `LumaCorePayload.dll` se inyecta en los procesos de juego lanzados por la ruta online-fix (puente EOS). Hace en un solo binario lo que nuestro stack reparte entre SLSsteam, lumalinux y CloudRedirect: propiedad, package 0, claves, pin de versión, códigos de manifest, tickets forjados, logros con SteamID de donantes, family sharing, ruta Spacewar (480), bloqueo de Steam Cloud. |
| **Repo y commit leídos** | `drappula/LumaCore` `main@33c36e9` (2026-10-03, tag `V37`). Clon completo: 35 commits; raíz `8ec4e88` (2026-05-25, "Initial clean commit - SteaMidra v6.2.4", la historia extraída del subárbol `LumaCore/` de `Midrags/SFF`). 165 ficheros, 24.980 líneas en `source/` (`.cpp` + `.h`; `steam/Enums.h` son 1.902). Patrones: `michelegoku3/MigoReleases` rama `pattern` `@61988bc` (2026-10-08). |
| **Plataforma** | Windows x64 solo (`README.md:30`, `CMakeLists.txt`): MSVC, CMake 3.20+, C++20, runtime estático. Ningún camino Linux ni Proton. |
| **Licencia y quién** | GPL-3.0 (`LICENSE`; cabeceras "GNU General Public License v3 or later", "Copyright (c) 2025-2026 Midrag"). `CREDITS.md`: escrito por Midrag, inspirado en OpenSteamTool (GPL-3.0). Commits: Midrag 26 (05-25 → 08-21, releases de SteaMidra aplastadas), wtfseanscool 1 (09-04), michelegoku3 1 (`c0d0537`, 09-30, el autor de Aether), mallusrgreat 7 (CI, 10-03). Mantenido hoy dentro del fork `drappula`; el original `KoriaPolis/LumaCore` parado en V36. |
| **Relación con los demás programas** | **SteaMidra** lo instala, le escribe los `.lua` y le precalienta la caché de patrones (`steamidra.md` §2.1); **MigoReleases** (michelegoku3) le da los patrones por SHA-256; **Aether** (michelegoku3) es la fuente de la última tanda de cambios; **OpenSteamTool** es su antepasado. Con nosotros: es el **origen de los hooks de lumalinux** (`main.cpp:9-22`, `RESEARCH.md` §11): mismas funciones para claves, pin y package 0; `steamidra_lite.py` imita su lectura del `.lua` (`steamidra_lite.py:111-122,206`). No convive con nosotros en ninguna máquina: es solo Windows. |

**Lo que cambia respecto al doc anterior**, en una línea cada uno:

- **Ya no copia `steamclient64.dll`**: carga el original por ruta absoluta y lo engancha, con aserción de identidad (`core/entry.cpp:132-202`, `steamclient64-direct` / `steamclient64-MISMATCH` en `status.json`). `lcoverlay.dll` desapareció en `c0d0537`; su `README.md` y `docs/LumaCore.md` lo siguen contando.
- **`xinput1_4.dll` es la puerta principal** y `dwmapi.dll` la de reserva (`proxy/LcXInputProxy.cpp:170-213`), no al revés.
- **Ningún hook verifica los bytes antes de engancharse.** Las TOML traen `rva` y `sig`, pero todas las macros de instalación pasan por `ByteSearch`, que devuelve `base + rva` sin comparar la firma (`patterns/ByteScan.cpp:159-167`, `hooks/Macros.h:69-171`); la única ruta que compara (`PatternFetcher::Resolve`) solo la usan dos macros que nadie llama (`runtime/VehUtil.h:41,76`). El doc anterior decía "byte patterns".
- **Los patrones vienen de `michelegoku3/MigoReleases`** (raw → jsDelivr → gitflic `midrags/steam-auto-pt`), no de `KoriaPolis/Steam-Auto-PT` (`patterns/PatternFetcher.cpp:49-64`).
- **El build id no elige firma**: solo se publica en `status.json`; el comentario de `core/entry.h:56` describe un `PatternDb.h` que ya no existe.
- **Denuvo no tiene ventana de autorización viva**: `DenuvoAuthenticator.cpp` no tiene llamadores (solo `Init()`, que escribe una línea de log) y con él muere el escáner con entropía (`IntegrityScanner.cpp`); lo vivo es `AuthWindow` sobre `ProtectionProbe`, que no cambia la respuesta, solo una etiqueta del log.
- **El acuñado de ETickets por HTTP está muerto**: `seteticketurl()` guarda la URL, pero el único que la usa (`IpcHandlers_ISteamUser.cpp`) nunca se registra.
- **Tres caminos de clave de depot, no dos**: `LoadDepotDecryptionKey`, `ConfigStoreGetBinary` con el marcador `\DecryptionKey` y un segundo detour de `ConfigStoreGetBinary` con el prefijo `Software\Valve\Steam\depots\` (`hooks/client/LicenseHooks.cpp:216-244`).
- **Cuatro hooks no se instalan nunca con el feed de hoy**: las siete TOML de `steamclient` de MigoReleases no traen `CConfigStore::FlushToDisk`, `CConfigStore::SetString`, `IClientRemoteStorage::FileExists` ni `IClientRemoteStorage::Dispatch` (medido 2026-10-08, §5.2).
- **`status.json` dice `V36`** en un binario publicado como `V37` (`runtime/HookStatus.cpp:160`).
- `[user] steam_id` de `lumacore.toml` **no existe**: el SteamID sale del registro, del ticket o de la cuenta activa (`hooks/client/CmdUser.cpp:153-170`).

---

## §1 Mapa del código

Cobertura por fichero con la función de la matriz a la que sirve (números de
§2). Líneas = `wc -l`. "Muerto" = sin llamadores en todo `source/` (grep del
nombre fuera de su propio fichero).

### 1.1 Arranque, proxies, patrones

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `core/entry.cpp`, `entry.h` | 426, 71 | `DllMain` fija el módulo y lanza `Bootstrap::Run` en un hilo (`:370-415`); `Run` (`:236-360`): logger, `lumacore.toml`, build id de `steam.exe!GetBootstrapperVersion` (`:210-225`), `PrepareAndLoad` (`LoadLibraryA` del `steamclient64.dll` original + aserción de ruta, `:132-202`; si falla, `return 1` sin hooks), patrones de steamclient, **`PackagePatch` y `SteamCapture` antes que nada** (`:279-280`), patrones de steamui (o reintento 60×500 ms), specs IPC, `BootDiag`, `status.json`, hooks de SteamUI, `ParseDirectory` de los `.lua`, inyección de arranque, `DirWatch`, `IpcHooks`, `DenuvoAuth::Init`, `LumaCore::Attach`. `kOnlineFixAppId = 480` (`entry.h:62`) | 1, 2, 11 |
| `core/Orchestrator.cpp`, `.h` | 55, 22 | orden de instalación: DepotKeys, DecryptionKeyHook, IPCBus, ManifestBind, PacketRouter, OnlineFixInject, LicenseHooks; desinstalación inversa | 1 |
| `proxy/LcXInputProxy.cpp`, `.def` | 238, 15 | `xinput1_4.dll`: reenvía al de System32 (carga perezosa, ordinales 100-104/108); hilo que carga `LumaCore.dll` por ruta absoluta **solo** si el proceso es `steam.exe` y el proxy está a su lado (`:180-213`); "the only bootstrap Steam still loads from its own directory" (Aether `14a6359`) | 1 |
| `proxy/LcDwmProxy.cpp`, `.def` | 177, 105 | `dwmapi.dll`: forwarders `#pragma` a `DWMAPI.*`, mismo hilo y mismas condiciones (`:118-155`); el `.def` no entra en el build | 1 |
| `patterns/PatternFetcher.cpp`, `.h` | 1153, 110 | SHA-256 del fichero del módulo → **caché primero** `<steam>\lumacore\pattern\<sha>.toml` (si parsea, no hay red) → espejo de usuario → `raw.githubusercontent.com/michelegoku3/MigoReleases/pattern/<subdir>/<sha>.toml` → jsDelivr (no si GitHub dio 404) → gitflic (API de blobs, `:542-617`) → escritura atómica `.tmp` + `MoveFileExA` (`:466-496`); cada tramo pide además `<url>.sig` (`:834-871`); TOML `[hash] name/rva/sig`, un `rva` o `sig` malformado tumba el fichero entero (`:385-422`); `Resolve` con comparación de bytes (`:759-806`, sin uso vivo); `LoadCachedSync`, `LoadForSteamUiDeferred`, `Lookup` público: muertos | 1 |
| `patterns/ByteScan.cpp`, `.h` | 207, 32 | `ByteSearch`: busca el nombre (alias `KeyValues_`) y **devuelve `base + rva` sin mirar la firma** (`:135-189`); solo si `rva == 0` escanea con la firma (nunca: `ParseToml` exige `rva`); `PatchMemoryBytes` | 1 |
| `patterns/PatternSig.cpp`, `.h` | 185, 50 | verificación RSA-2048 PSS-SHA256 con BCrypt contra un módulo **todo ceros** (`kPlaceholderModulus`, `:47`) → siempre `KeyUnavailable` | 1, 12 |
| `hooks/Macros.h`, `SigTypes.h` | 191, 9 | `LM_HOOK`, `LM_INSTALL(_STR)`, `LM_BIND(_STR)`, `LM_CAPTURE`, `LM_REMOVE`: todas resuelven por `ByteSearch` contra `diversion_hModule`; las variantes `_STR` usan el `.name` del array como **clave de la TOML**, no como xref de string | 1 |

### 1.2 Configuración y Lua

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `config/Settings.cpp`, `.h` | 288, 127 | `<steam>\lumacore.toml` (§3.4), valores por defecto (`Settings.h:21-123`), recarga por `mtime` en cada handshake de pipe (`ReloadIfChanged`, `:253-285`, llamada desde `PipeWatch.cpp:533,551`); `trusted_hosts` y `[stats] enable_api` se leen y **nadie los usa**; `luaHttpAllowlistExtra` se usa y **nadie lo rellena** (no se parsea `http_allowlist`) | 4, 11 |
| `config/LuaState.cpp` | 310 | `lua_State` con solo `base`, `table`, `string`, `math` (`:184-189`), sin `dofile/loadfile/load/loadstring/require/collectgarbage` (`:198-205`); 16 bindings en minúsculas (`:125-146`; `pinapp` sin registrar) + resolvedor case-fold en `_G.__index` (`addAppId`, `AddAppId`, `ADDAPPID`…) + tabla `_originals` para que un `.lua` envuelva un binding (el patrón `00_LetUpdate`); pool de 15 SteamIDs de donantes para logros (`:64-80`) | 4, 5, 7, 9 |
| `config/LuaBindings.cpp` | 578 | `addappid(depot[, _, key])`: clave **solo si mide exactamente 64 hex** (`:309`), si no se ignora en silencio; limpia "poseído"/"compartido" del depot. `addtoken`, `setManifestid(depot, gid[, size])` (size ignorado), `setAppticket`/`setEticket` (hex → registro), `setStat`, `getCachedAppTicket`, `getDecryptionKey`, `seteticketurl`, `forcedenuvo` (sin efecto: `IsForcedDenuvo` no tiene llamadores), `skipManifestPin`, `addprocess`, `fetchManifestCode(Ex)`; `lcHttpGet/Post` con lista de hosts fija (`:54-60`), fuera de ella `403` sin tocar la red | 4, 5, 6, 7, 9 |
| `config/LuaQuery.cpp` | 582 | consultas (`HasDepot` = en el `.lua` y no poseído ni compartido, `:43-48`); `ParseFile` (`:305-467`): descarga el fichero previo, **el nombre numérico del `.lua` registra la app** como depot, raíz de biblioteca y raíz de stats (`:340-372`), `luaL_loadbuffer` + `pcall`, y luego un barrido de texto que vuelve a pinear todo `setManifestid("…")` literal no comentado salvo los redistribuibles de 228980 (+220211, `:409-414`), los ya pineados por Lua y los de `skipManifestPin` (`:401-463`); `UnloadFile` por refcount (`:208-278`); `ParseDirectory` crea la carpeta si falta | 2, 3, 5, 9 |
| `config/LuaLoader.h`, `LuaLoaderInternal.h` | 90, 158 | API pública y estado compartido (conjuntos de depots, raíces, stats, tokens, overrides, pendientes); `GetLuaMtime` (el `mtime` se guarda para "Date Added" y nadie lo lee), `QueueStartupInjection`: muertos | 2, 9 |

### 1.3 Runtime

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `runtime/DirWatch.cpp`, `.h` | 343, 14 | `ReadDirectoryChangesW` sobre `stplug-in` (+ `[lua] paths`), solo `*.lua`, rebote de 500 ms, tres reaperturas si el búfer desborda (sin re-escaneo de lo perdido), al cerrar el lote `ParseFile`/`UnloadFile` y `SteamCapture::NotifyLicenseChanged` (`:211-308`) | 9 |
| `runtime/ManifestFetch.cpp`, `.h` | 299, 57 | puente de **códigos de petición de manifest**: futuro por `jobid`, primero las funciones Lua `fetch_manifest_code(_ex)` del script, después `urls` en orden (dos intentos si 429, UA `OpenSteamTool/1.0` solo para opensteamtool, `:144-224`), cuerpo decimal o JSON `manifest_request_code`/`content`/`code`; caché por (gid, app, depot) que se vacía en cada `ParseFile`; la espera es `timeout_sec` **en total** (`:276-288`) | 4 |
| `runtime/RuntimeHttp.cpp`, `.h` | 292, 42 | WinHTTP GET/POST, 12 s, 8 MiB, proxy del sistema, validación TLS por defecto (`:22-23`) | 4, 10 |
| `runtime/Ticket.cpp`, `.h` | 1022, 140 | tickets en `HKCU\Software\Valve\Steam\Apps\<id>` (`SteamID`, `AppTicket`, `ETicket`); cuenta activa (`ActiveProcess\ActiveUser` → carpeta `userdata` más reciente → caché `Software\Valve\Steam\lumacore\LastActiveSteamId`, `:542-654`); **forja**: copia el ticket firmado de la app 7 (el cliente de Steam, que toda cuenta tiene) e inserta el appid objetivo antes de los 128 bytes de firma (`:916-947`), si no hay app 7 usa el ticket firmado de menor appid del registro; preflight de lanzamiento (`EnsureRegistryTicketsForApp`, `:736-914`); ticket mínimo sin firmar, v4, +30 días (`:700-730`); SteamStub conocidos: Teardown, DOOM Eternal, Spore, Mirror's Edge (`:665-675`) | 2, 6 |
| `runtime/TicketProvider.cpp`, `.h`, `CredentialStore.cpp`, `.h` | 156, 35, 188, 38 | envoltorio de lo anterior sobre el mismo registro | 2, 6, 10 |
| `runtime/EticketFetcher.cpp`, `.h` | 142, 30 | POST JSON `{app_id, nonce}` a la URL de `seteticketurl()`, **sin la lista de hosts** que sí tiene `lcHttpGet`, caché por app sin mirar el nonce (`:67-112`); su único llamador está muerto | 6, 10 |
| `runtime/HookStatus.cpp`, `.h` | 1089, 121 | `<steam>\lumacore\status.json`: build id, rutas y SHA, TOML por módulo, hooks instalados/fallidos, estado del package 0, nube, stats, SteamStub, online-fix, `diagnostic_reason` (`:706-735`); escritura atómica `.tmp` + `MoveFileExA` con reintento y 5 s de enfriamiento (`:602-665`), reescrito en cada cambio tras el arranque; `kLumaCoreVersion = "V36"` (`:160`) | 11 |
| `runtime/Logger.cpp`, `.h`, `ModuleLog.h` | 103, 175, 39 | spdlog **solo en Debug**: un `.log` por módulo en `<steam>\lumacore\`, 50 MB × 3, volcado en cada línea, `verbose = true` → trace (`:41-47`); en Release todos los `LOG_*` son `((void)0)` | 11 |
| `runtime/BootDiag.cpp`, `.h` | 115, 27 | `MessageBoxA` con build id y SHA si no hay specs IPC (`:91`); `diagnostic_popup = true` por defecto (el comentario dice false) | 11 |
| `runtime/Diagnostics.cpp`, `.h` | 172, 74 | anillo de 64 eventos de logros, volcado a `%APPDATA%\SteaMidra\lumacore_diag.txt` al descargar la DLL (también en Release) | 7, 11 |
| `runtime/ProtectionProbe.cpp`, `.h` | 825, 28 | detección de SteamStub/Denuvo por fichero: `.bind` ejecutable con la entrada dentro (= SteamStub, el único método que "acepta ruta", `:298-317`), secciones `.arch/.srdata/.xpdata/.xdata/.xtls`, `DODENUVODE` en la entrada, `DENUVO` en sección ejecutable, o "hay ETicket en el registro" (`:682-689`); antes de lanzar, hasta 128 candidatos bajo la carpeta del juego; tope 96 MiB por PE | 6 |
| `runtime/IntegrityScanner.cpp`, `.h` | 331, 32 | escáner Denuvo con entropía; solo lo llama `DenuvoAuthenticator`: **muerto** | 6 |
| `runtime/ProcessInspect.cpp`, `.h`, `RemoteTools.cpp`, `.h` | 193, 60, 427, 41 | entorno del proceso de juego (`SteamAppId`, `SteamGameId`, `SteamOverlayGameId`), bitness, módulos, inyección remota (`VirtualAllocEx` + `WriteProcessMemory` + `CreateRemoteThread(LoadLibraryW)`, `:267-390`) | 6, 9 |
| `runtime/IpcSpecLoader.cpp`, `.h` | 504, 56 | `steamclientipc/<sha>.toml` (hash de cada método IPC) de MigoReleases raw → jsDelivr → gitflic, caché `<steam>\lumacore\pattern\steamclientipc\`, sin firma ni espejo de usuario (`:109,347-397`) | 1 |
| `runtime/LibraryInjector.cpp`, `.h` | 164, 26 | inyector genérico: **muerto** (lo hace `ProcessExtension`) | 6 |
| `runtime/VehUtil.cpp`, `.h`, `ClockMark.h`, `LcFnvHash.h` | 20, 117, 25, 18 | `int3` de un byte; macros `ARM_CAPTURE_D`/`_STR_D` (las únicas con `Resolve` y xref real): **muertas**; cronómetro; FNV-1a | 1 |

### 1.4 Hooks del cliente

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `hooks/client/DepotKeys.cpp` | 56 | `LoadDepotDecryptionKey`: `…\<depot>\DecryptionKey` → clave del `.lua` (`:12-41`) | 4 |
| `hooks/client/DecryptionKeyHook.cpp` | 286 | `ConfigStoreGetBinary` con el mismo marcador (`:30-65`); captura el `ConfigStore` UserLocal; `GetCachedAppTicket` = `apptickets\<id>` del ConfigStore o el registro (`:139-167`); `CConfigStore::FlushToDisk` reaplica el idioma al ACF de 480 (`:75-84`; no se instala, §5.2); `SetString`; lee/escribe `appmanifest_480.acf` (lo crea si falta, `:254-284`); bibliotecas solo con letra de unidad | 4, 6 |
| `hooks/client/ManifestBind.cpp` | 124 | `BuildDepotDependency`: tras el original, cambia `ManifestGid` (no el tamaño) de todo depot con override de `setManifestid` (`:54-107`), sin mirar si la app es poseída | 5 |
| `hooks/client/PackagePatch.cpp` | 437 | `LoadPackage` (package 0 ← todos los ids del `.lua` con `CUtlMemoryGrow`, `:194-217`), `CheckAppOwnership` (poseído si Steam dice que sí con más de un paquete y `Released`; si no, `bOwnsLicense`/`Released`/`PackageId 0`, `:219-288`), `GetSubscribedApps` (añade solo las raíces de biblioteca, `:290-331`), `SendCallbackToPipe` (`AppLicensesChanged` con `bReloadAll` forzado; logros duplicados a 480 en la ruta, `:333-357`) | 2, 3, 7 |
| `hooks/client/LicenseHooks.cpp` | 307 | `OptedInMask` (mando: 480 → real), **bloqueo de Steam Cloud** para apps del `.lua` no poseídas ni compartidas (`IsCloudEnabledForApp` false, `Evaluate/GetRemoteStorageSyncState` = 2, `RunAutoCloudOnAppLaunch/Exit` = 0, `:78,134-212`), family sharing con nube forzada a sí; `RequiresLegacyCDKey` → false; segundo `ConfigStoreGetBinary` (`depots\<id>`, `:216-244`) | 4, 6, 8 |
| `hooks/client/IPCBus.cpp`, `.h` | 338, 59 | `IPCProcessMessage` (IPC cliente↔juego): tabla de manejadores por interfaz + hash (los de `IpcSpecLoader` si cargó; si no, constantes `IPCBus.h:32-39`), solo si la app tiene `HasDepot` (`:285`); handshakes a `PipeWatch`; en la ruta 480, `FileExists`/`GetFileSize`/`FileRead` de RemoteStorage servidos desde `<steam>/userdata/<cuenta>/<real>/remote/` (nombre = la cadena ASCII más larga de la petición, `:170-271`); hooks `IClientRemoteStorage_FileExists/Dispatch` (no se instalan, §5.2) | 6, 8 |
| `hooks/client/CmdUser.cpp`, `.h` | 588, 13 | `GetSteamID` (`:194-223`): SteamID del registro o del ticket → cuenta con carpeta `userdata\<cuenta>\<appid>` → cuenta activa (y la guarda); `GetAppOwnershipTicketExtendedData` (registro o forjado, con offsets, `:268-340`); `Request/GetEncryptedAppTicket` (ETicket del registro, callback forzado a OK); `GetAppID` (real, no 480); `GetAPICallResult` (gameid 480↔real en callbacks de stats) | 2, 6, 7 |
| `hooks/client/IpcDispatch.cpp`, `.h`, `IpcHooks.cpp`, `.h`, `IpcHandlers_ISteamUtils.cpp` | 102, 41, 37, 13, 124 | capa pre/post encima de IPCBus; solo registra `IClientUtils::GetAppID` (duplica la de CmdUser: con `std::map::emplace` gana la primera en registrarse, y `IpcHooks::Install` va antes que `LumaCore::Attach`) | 6 |
| `hooks/client/IpcHandlers_ISteamUser.cpp` | 167 | duplicado de CmdUser + acuñado de ETicket por HTTP: **muerto** (su `Register` nunca se llama) | 6 |
| `hooks/client/IpcMethodLoader.cpp`, `.h` | 309, 33 | segundo lector del mismo `steamclientipc/<sha>.toml` (raw → jsDelivr → gitflic, caché no atómica) | 1 |
| `hooks/client/CmdUtils.cpp`, `.h`, `PendingCallMap.cpp`, `.h` | 13, 10, 52, 17 | **muertos** | — |
| `hooks/client/AsyncTicketMap.cpp`, `.h` | 45, 17 | `hCall` → app para la respuesta del ETicket | 6 |
| `hooks/client/PipeWatch.cpp`, `.h` | 608, 62 | por cada handshake: PID, entorno, módulos (`steamclient`, `steam_api`, `EOSSDK-Win64-Shipping`), caché por pipe; inyección de reserva del payload si el juego trae EOS; `AuthWindow`; `ProcessExtension`; appid por entorno o por `addprocess()` (`:531-583`) | 6, 9 |
| `hooks/client/AuthWindow.cpp`, `.h` | 177, 16 | ventana de 2 handshakes para juegos con protección detectada; al cerrar, si el juego **es** de la cuenta, guarda el SteamID activo (`:56-110`); en la respuesta solo cambia la etiqueta del log | 6 |
| `hooks/client/DenuvoAuthenticator.cpp`, `.h` | 180, 26 | máquina de estados Denuvo: `OnHandshake` e `IsAuthorizedPipe` **sin llamadores** (`:106-170`) | 6 |
| `hooks/client/SteamStubAuto.cpp`, `.h`, `SteamStubTicket.cpp`, `.h` | 116, 22, 218, 20 | ruta SteamStub (480 para Steam, real para el juego) con ticket forjado de la app 7; **apagada por defecto** (`[steamstub] auto_enabled = false`, `SteamStubAuto.cpp:41-50`) | 6 |
| `hooks/client/OnlineFixInject.cpp`, `.h` | 365, 20 | Detours sobre `kernel32!CreateProcessW/AsUserW` (por `GetProcAddress`, no por TOML): el exe en cola arranca suspendido, se le inyecta `LumaCorePayload.dll` y se reanuda (`:149-280`); `[onlinefix] inject_enabled` | 6 |
| `hooks/client/ProcessExtension.cpp`, `.h` | 250, 14 | inyecta una DLL configurada (`[process_extension]`, apagado) en procesos de juego del `.lua`, solo de 64 bits | 6 |
| `hooks/client/PacketRouter.cpp`, `.h`, `NetPacket.cpp`, `.h` | 17, 11, 510, 100 | `BBuildAndAsyncSendFrame` (salida) y `RecvPkt` (entrada) del CM: despacho por `EMsg` y por `target_job_name` en FNV (`:69-188`); layout de `CNetPacket` detectado en vivo (stable `+0x08` / beta `+0x10`, dos paquetes seguidos, 512 intentos y se apaga, `:200-366`) | 2, 4, 6, 7 |
| `hooks/client/NetPacket_Manifest.cpp`, `.h` | 90, 15 | salida `GetManifestRequestCode` de un depot del `.lua` → `ManifestFetch::Submit`; entrada → espera el código, `eresult = OK`, cuerpo nuevo (`:43-82`) | 4 |
| `hooks/client/NetPacket_AccessToken.cpp`, `.h` | 68, 12 | `ClientPICSProductInfoRequest`: pone el `access_token` de `addtoken()` (`:41`) | 2 |
| `hooks/client/NetPacket_FamilySharing.cpp`, `.h` | 17, 10 | vacía el cuerpo de `NotifyRunningApps` (respuesta) y de `SharedLibraryLockStatus/StopPlaying` | 2 |
| `hooks/client/NetPacket_UserStats.cpp`, `.h` | 564, 17 | logros con **SteamID de donante** (§2.7) | 7 |
| `hooks/client/NetPacket_OnlineFix.cpp`, `NetPacket_SteamStub.cpp`, `NetPacket_RichPresence.h`, `RichPresence.cpp`, `.h` | 80, 77, 20, 364, 35 | `GamesPlayed` en la ruta 480 (nombre real como extra info, o real → 480 en SteamStub); `PersonaState` de amigos 480 → app real | 6 |
| `hooks/client/NetPacket_ETicket.cpp`, `.h` | 51, 12 | parche de la respuesta de ETicket: **muerto** (no está en el despacho) | — |
| `hooks/client/StringFind.cpp`, `.h` | 126, 24 | xref de strings por `LEA` RIP-relativo + `.pdata`; solo desde `ARM_CAPTURE_STR_D`: **muerto** | — |

### 1.5 Captura, SteamUI, payload, tipos, build

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `hooks/capture/RuntimeCapture.cpp`, `.h`, `SteamCapture.h` | 853, 96, 7 | `int3` + VEH solo en `GetAppDataFromAppInfo` (nombres) y `SpawnProcess` (con re-armado por single-step; byte original `0x48` fijo, `:409-415`); Detours en `GetAppIDForCurrentPipe`, `MarkLicenseAsChanged`, `GetPackageInfo`, `BuildSpawnEnvBlock` (`:577-582`); inyección de arranque con hilo de 150 reintentos (`:303-362`); **lanzamiento**: `-onlinefix` → ruta 480, SteamStub, preflight de tickets (`:393-526`); idioma a Spacewar (`:138-161`); `NotifyLicenseChanged` en caliente (`:736-852`) | 2, 6, 9 |
| `hooks/ui/SteamUI.cpp`, `.h` | 560, 39 | `steamui.dll`: `LoadModuleWithPath` (lanza la TOML de steamui), `GetAppByID`, `GetTopManager`, `MarkAppChange`, `FillInAppOverview`, `BuildCompleteAppOverviewChange`, `CSteamUIAppControllerRunFrame` (drena altas y bajas de biblioteca con `MarkAppChange(…, 2)`, `:230-271`) | 9 |
| `hooks/ui/StatusWriter.cpp`, `.h` | 172, 53 | segundo escritor de `status.json`: **muerto** | — |
| `payload/LumaCorePayload/*` (6 `.cpp`/`.h` + `RemoteDllDeploy.h`, `EpicOnlineTypes.h`) | 146, 12, 81, 48, 21, 80, 76, 12, 33 | DLL de juego: se propaga a los hijos (`CreateProcessW/AsUserW`), espera `EOSSDK-Win64-Shipping.dll` (`LdrRegisterDllNotification`) y le cambia `EOS_Connect_Login` por login de Device ID con el nombre de persona de Steam, responde `Success` a `IntegratedPlatformOptionsContainer_Add` y quita la presencia de los lobbies; log por PID solo en Debug | 6 |
| `proto/steam_messages.proto` | 356 | 20 mensajes (cabecera, ETicket, PICS, NotifyRunningApps, SharedLibrary, GetUserStats ×2, GetManifestRequestCode, GamesPlayed, StoreUserStats2, PersonaState, RichPresenceUpload, `CAppOverview_Change`); protobuf **3.15.3** fijado por ABI de Steam | 1 |
| `steam/Types.h`, `Enums.h`, `Structs.h`, `Callback.h`, `NetPacketLayout.h` | 39, 1902, 284, 107, 111 | tipos y layouts de Steam (`PackageInfo`, `AppOwnership`, `DepotEntry`, `CUtlVector`, `CSteamPipeClient`, 1.423 `EMsg`) | 1 |
| `source/CMakeLists.txt`, `cmake/*.cmake` (7) | 216, 351 | dependencias por FetchContent: **Lua 5.5.0** de `lua.org` con `TLS_VERIFY OFF` (`LcLua.cmake`), Detours `GIT_TAG main` sin fijar, protobuf v3.15.3, spdlog v1.17.0 (solo Debug), toml++ v3.4.0; `LUMACORE_LOGGING_ENABLED` solo en Debug, `LUMACORE_DIAGNOSTICS_ENABLED` siempre | 11, 12 |
| `.github/workflows/release.yml`, `build.bat` | 82, 229 | CI en cada push a `main`: Windows, MSVC, CMake 3.31.6 + Ninja, Release y Debug, `Release.zip`/`Debug.zip`, release `V<n+1>` ("stay above upstream V36"), sin checksums ni firmas; `build.bat` local con `--clean` por defecto | 11, 12 |
| `README.md`, `docs/LumaCore.md`, `CREDITS.md`, `lumacore.toml.example`, `.gitignore`, `LICENSE` | 49, 457, 19, 67, 51, 674 | docs de upstream (la de funciones es de `8321f69`, 07-04, y cuenta la copia `lcoverlay`, la entropía, `[user] steam_id`, el ETicket por GET y "Lua 5.4"); ejemplo de config; `.gitignore` en italiano | 12 |

---

## §2 Las doce funciones

Cada función contesta la lista de control de `ecosystem-matrix.md` §1, con
`fichero:línea` de `drappula/LumaCore@33c36e9` (rutas relativas a `source/`).

### 2.1 Engancharse a Steam

**Cómo entra.** Secuestro de DLL por carpeta: `xinput1_4.dll` (principal) y
`dwmapi.dll` (reserva) junto a `steam.exe`. Ninguno carga nada en `DllMain`;
un hilo comprueba que el proceso es `steam.exe` y que el proxy está a su lado,
y carga `LumaCore.dll` por ruta absoluta si nadie lo cargó ya
(`proxy/LcXInputProxy.cpp:180-213`, `proxy/LcDwmProxy.cpp:139-155`). `LumaCore.dll`
hace lo mismo en su `DllMain` (`core/entry.cpp:370-391`) y el trabajo real va en
`Bootstrap::Run` (`:236-360`). El objetivo es el `steamclient64.dll`
**original**, cargado otra vez por ruta para tener su `HMODULE`, con
comprobación de que la ruta del módulo mapeado coincide con la pedida
(`:132-202`); si no coincide, `status.json` lo dice (`steamclient64-MISMATCH`)
y los hooks se ponen igual sobre un módulo que nadie llama.

**Cómo localiza lo que parchea.** Un fichero TOML por SHA-256 del DLL en disco
(`steamclient64.dll`, `steamui.dll`, y `steamclientipc/` para los hashes de
métodos IPC), con `name`, `rva` y `sig` por función. Orden
(`patterns/PatternFetcher.cpp:1009-1104`): **caché local primero**
(`<steam>\lumacore\pattern\<sha>.toml`; si existe y parsea, no se toca la red
nunca más para ese binario), espejo de usuario, GitHub raw de MigoReleases,
jsDelivr, gitflic. Con la entrada en mano, **todas** las instalaciones van por
`ByteSearch`, que devuelve `base + rva` sin comparar la firma
(`patterns/ByteScan.cpp:159-167`): la firma de la TOML se valida solo como
forma (`:365-382`) y la comparación real (`PatternFetcher::Resolve`,
`:759-806`) no tiene uso vivo. Tampoco hay firma criptográfica operativa: cada
tramo pide `<url>.sig`, pero la clave pública incrustada es un módulo de ceros
(`patterns/PatternSig.cpp:47`) y con `require_signed = false` (por defecto) todo
se acepta "UNSIGNED" (`PatternFetcher.cpp:834-871`). MigoReleases no publica
ningún `.sig` (§5.2). Fuera de la TOML: `CreateProcessW/AsUserW` por
`GetProcAddress` (`hooks/client/OnlineFixInject.cpp:254-264`). En total, 32
detours en `LumaCore.dll` (25 instalaciones en `steamclient64`, 5 en `steamui`,
2 en `kernel32`) y 2 trampas `int3`.

**Qué pasa cuando Steam actualiza.** El SHA cambia, no hay TOML en caché y se
busca en la red. Si MigoReleases ya la tiene, se instala sola en el siguiente
arranque; si no, cada hook falta limpio (`RecordMissed`), Steam corre sin
modificar en esas funciones y `status.json` dice `pattern-missing-no-cache`
(`runtime/HookStatus.cpp:706-735`). No hay gate de hash ni SafeMode: el build
id (`GetBootstrapperVersion`, `core/entry.cpp:210-225`) solo se publica. Sin
specs IPC, `MessageBoxA` con build id y SHA para que el usuario lo reporte
(`runtime/BootDiag.cpp:91`, activo por defecto). Con un RVA viejo en una TOML
válida el hook se pondría en mitad de otra función: nada lo impide.

**Cómo se recupera.** No hay kill-switch por hook, ni variables de entorno, ni
detector de crash. La salida es quitar las DLL (lo hace SteaMidra,
`steamidra.md` §2.1) o borrar la TOML. Logs por módulo solo en el build Debug
(`runtime/Logger.cpp:41-47`); en Release, `status.json` y el anillo de logros
(`%APPDATA%\SteaMidra\lumacore_diag.txt`).

**A qué procesos se limita.** `steam.exe` (proxies). Dentro de Steam, los
pipes internos (`m_hSteamPipe & 0xFFFF <= 2`) pasan sin tocar
(`hooks/client/IPCBus.cpp:56`). El payload va a procesos de juego,
y desde ahí a sus hijos.

### 2.2 Propiedad y licencias

**Spoof de propiedad.** Dos capas: el package 0 contiene todos los ids del
`.lua` (depots, apps y DLC) y `CheckAppOwnership` devuelve dueño para toda app
con `HasDepot` (`hooks/client/PackagePatch.cpp:219-288`: `PackageId 0`,
`Released`, `bFreeLicense = false`, `bOwnsLicense = true`). La biblioteca solo
recibe las **raíces** (los `.lua` con nombre numérico) por `GetSubscribedApps`
(`:290-331`), para que no salgan tarjetas de DLC o depots.

**Inyección de packages y cuándo se reaplica.** `LoadPackage` (`:194-217`) y
`GetPackageInfo` (`hooks/capture/RuntimeCapture.cpp:264-276`) meten en el
vector del package 0 los ids que falten (`EnsurePackageContains`, idempotente,
`PackagePatch.cpp:47-170`). Si el package 0 se cargó antes que los `.lua`, un
hilo reintenta 150 veces (40×250 ms + 1 s) (`RuntimeCapture.cpp:340-362`). En
caliente, al cambiar un `.lua`: quita las bajas, **reconcilia con el conjunto
completo** (no solo el delta: "Aether parity", `:773-784`), llama a
`MarkLicenseAsChanged(pCUser, 0, true)` + `ProcessPendingLicenseUpdates`
(`:801-805`) y encola los cambios de biblioteca para el hilo de SteamUI.
`SendCallbackToPipe` fuerza `bReloadAll` en cada `AppLicensesChanged`
(`PackagePatch.cpp:333-340`).

**Juegos poseídos de verdad.** Si Steam ya dice "dueño" con más de un paquete
y `Released`, se marca `Owned` (o `FamilyShared` si `bBorrowed`/`bFamilyShared`)
y no se toca (`PackagePatch.cpp:227-247`); `HasDepot` pasa a falso para esa app
y todo lo demás (tickets, nube, stats) la deja en paz. La marca se borra al
re-parsear el `.lua` (`config/LuaBindings.cpp:319-320`,
`config/LuaQuery.cpp:354-359`) y se vuelve a evaluar en la siguiente consulta.

**Family sharing.** Vacía la respuesta de `FamilyGroupsClient.NotifyRunningApps`
y los mensajes `ClientSharedLibraryLockStatus`/`StopPlaying`
(`hooks/client/NetPacket.cpp:145-187`, `NetPacket_FamilySharing.cpp`): el
prestatario no ve el bloqueo del dueño. Juegos compartidos de verdad se
reconocen y se dejan.

**Depots compartidos.** Los redistribuibles de 228980 (y 220211) no se pinean
nunca (`config/LuaQuery.cpp:409-414`); como cualquier id del `.lua`, entran en
el package 0 si el `.lua` los lista. Tokens PICS: `addtoken()` se inyecta en
cada `ClientPICSProductInfoRequest` (`NetPacket_AccessToken.cpp:41`).

### 2.3 DLC

**Criterio de unlock.** Lo que liste el `.lua`: cada `addappid(<dlc>)` entra en
el package 0 y `CheckAppOwnership` lo da por poseído. No hay lista blanca ni
unlock "todo"; no hay límite de 64 (no usa `DlcData` ni `listofdlc`).
**DLC comprado de verdad**: el mismo criterio de §2.2 (más de un paquete →
`Owned`). **DLC sin depot**: `addappid(<id>)` sin clave hace lo mismo.
**Cómo se quita**: borrando la línea o el `.lua`; `UnloadFile` lo quita del
package 0 si ningún otro `.lua` lo aporta (refcount, `config/LuaQuery.cpp:208-278`)
y `NotifyLicenseChanged` refresca.

### 2.4 Claves y manifests

**Claves.** Solo las del `.lua` (`addappid(depot, _, "<64 hex>")`); una clave
de otra longitud se descarta sin aviso (`config/LuaBindings.cpp:307-316`). Se
sirven por **tres** caminos: `LoadDepotDecryptionKey`
(`hooks/client/DepotKeys.cpp:12-41`), `ConfigStoreGetBinary` con
`\DecryptionKey` (`DecryptionKeyHook.cpp:30-65`) y un segundo detour de
`ConfigStoreGetBinary` con prefijo `Software\Valve\Steam\depots\`
(`LicenseHooks.cpp:216-244`). No escribe `config.vdf` ni guarda claves fuera de
memoria.

**Manifests.** LumaCore no descarga ni siembra manifests: eso lo hace
SteaMidra (`steamidra.md` §2.4). Lo que sí hace es el **código de petición**:
cuando Steam pide `ContentServerDirectory.GetManifestRequestCode#1` para un
depot del `.lua`, lanza la búsqueda en paralelo
(`NetPacket_Manifest.cpp:43`) y retiene la respuesta del CM hasta tenerla
(`:60-82`). Orden (`runtime/ManifestFetch.cpp:144-224`): funciones Lua
`fetch_manifest_code_ex` / `fetch_manifest_code` si el `.lua` las define →
`https://manifest.opensteamtool.com/{gid}` (UA `OpenSteamTool/1.0`) →
`https://manifest.steam.run/api/manifest/{gid}` → `http://gmrc.wudrm.com/manifest/{gid}`
(`config/Settings.h:86-90`). Dos intentos si 429; caché por (gid, app, depot)
hasta el siguiente `ParseFile`. **Sin comprobación contra el CDN**: el primer
número que parsee va a Steam. `trusted_hosts` se lee y nadie lo mira
(`Settings.h:97`). La espera total es `timeout_sec` (12 s) para toda la cadena
(`ManifestFetch.cpp:276-288`), y cada GET puede tardar 12 s
(`RuntimeHttp.cpp:23`): con el primero lento, los otros no llegan.

**Qué deja de funcionar si cae cada servicio.** Sin proveedores (SteaMidra los
dio por muertos el 2026-09-12, `ecosystem-matrix.md` §3.3) la respuesta del CM
pasa tal cual y Steam solo instala si el manifest ya está en disco. Sin
MigoReleases, un cliente nuevo no tiene hooks. Las claves no dependen de red.

### 2.5 Updates de juegos

**Pin.** `setManifestid(depot, "gid")` crea un override que
`BuildDepotDependency` aplica al vector de depots que Steam calcula
(`hooks/client/ManifestBind.cpp:54-107`): Steam ve como destino el gid pineado,
para cualquier app que tenga ese depot. Después de ejecutar el `.lua`, un
barrido de texto vuelve a pinear todo `setManifestid` literal no comentado
(`config/LuaQuery.cpp:401-463`).

**Actualización automática.** Por depot: `skipManifestPin(depot)`, los
redistribuibles, o un `.lua` que envuelva `setManifestid` con `_originals` y
no lo llame (el patrón `00_LetUpdate_override.lua` de SteaMidra,
`config/LuaState.cpp:250-285`; ojo, sin `skipManifestPin` el barrido de texto
lo vuelve a pinear). Sin pin, Steam actualiza como con un juego propio, con los
códigos de §2.4.

**Versión antigua.** `setManifestid` con el gid viejo (SteaMidra lo escribe,
`steamidra.md` §2.5). **Update ya empezada por Steam**: nada específico.

### 2.6 Fixes y DRM

**Denuvo.** Sin ventana de autorización viva (§0). Lo que queda: `GetSteamID`
devuelve, para una app del `.lua`, el SteamID del registro
(`Apps\<id>\SteamID`), el del `AppTicket` guardado, el de una cuenta que tenga
carpeta `userdata\<cuenta>\<appid>`, o la cuenta activa
(`hooks/client/CmdUser.cpp:153-170`); `Request/GetEncryptedAppTicket` sirven el
`ETicket` del registro (`:343-388`). Es decir: si el `.lua` trae
`setAppticket`/`setEticket` del dueño, el juego ve al dueño. `forcedenuvo()` no
hace nada; `addprocess()` sí (appid por nombre de exe).

**SteamStub.** Antes de cada lanzamiento de una app del `.lua` no poseída,
escanea el exe y hasta 128 ficheros del juego (`runtime/ProtectionProbe.cpp`)
y prepara el `AppTicket` del registro: conserva uno válido, si no forja uno con
el ticket de la app 7, y si tampoco escribe uno mínimo sin firmar
(`runtime/Ticket.cpp:736-914`, desde `RuntimeCapture.cpp:454`). La ruta
dedicada (Steam ve 480, el juego su appid) está **apagada por defecto**; con un
SteamStub conocido, el log recomienda "Remove SteamStub from SteaMidra"
(`PackagePatch.cpp:269-283`). Steamless lo aplica SteaMidra, no la DLL.

**Goldberg u otros emuladores.** No.

**EOS y online-fix.** Lanzar con `-onlinefix` en la línea de comandos activa la
ruta 480 (`RuntimeCapture.cpp:420,471-491`): el `CGameID` del lanzamiento pasa
a 480, el overlay al juego real, el idioma del juego se copia al ACF y al
ConfigStore de Spacewar, el exe se lanza suspendido y recibe
`LumaCorePayload.dll` (puente EOS, §1.5), `GamesPlayed` lleva el nombre real,
los amigos ven el juego real, los saves de RemoteStorage se leen de la carpeta
del juego real (si el hook se instala; hoy no, §5.2) y los callbacks de logros
se duplican con gameid 480.

**Otros.** `RequiresLegacyCDKey` → falso para apps del `.lua`
(`LicenseHooks.cpp:246-255`). Inyector genérico de DLL por config, apagado
(`hooks/client/ProcessExtension.cpp:174`).

**Cómo se aplican y se quitan.** Todo en memoria salvo el registro (tickets,
SteamID) y el ACF de 480; no hay deshacer para lo escrito en el registro.

### 2.7 Logros, stats y tiempo de juego

**Dónde se guardan.** LumaCore no tiene almacén propio. Para apps "de stats"
(los `.lua` con nombre numérico o `setStat`), reescribe las peticiones
`Player.GetUserStats` y `ClientGetUserStats` con el SteamID de un **donante**
(el de `setStat(app, "sid")` o uno de un pool de 15 incrustado,
`config/LuaState.cpp:64-80`, rotando hasta que uno devuelve datos y
recordándolo, `hooks/client/NetPacket_UserStats.cpp:79-98`), quita `sha_schema`
y pide el esquema entero; en la respuesta pone `eresult = OK` y **borra** los
stats y bloques de logros del donante (`:230,354`): el juego recibe el esquema
vacío. `ClientStoreUserStats2` sale al CM tal cual (solo 480 → real) y marca la
app para que la siguiente lectura no se reescriba (`:514-563`).

**Sincronización.** La que haga Steam con lo que el CM acepte para una app que
la cuenta no tiene; no hay medición. **Quién contesta `GetUserStats`**: el CM,
con la petición reescrita. **Tiempo de juego**: sin tratamiento; en la ruta
480, Steam registra Spacewar.

### 2.8 Cloud saves

**Redirección.** No. **Convivencia con Steam Cloud**: la **apaga** para toda
app del `.lua` que no sea poseída ni compartida (`IsCloudEnabledForApp` falso,
estados de sincronización a "sincronizado", AutoCloud al lanzar/salir anulado,
`hooks/client/LicenseHooks.cpp:78,134-212`); fuerza la nube a sí para juegos
compartidos por family sharing. En la ruta 480 sirve lecturas de RemoteStorage
desde `userdata/<cuenta>/<real>/remote/` (`IPCBus.cpp:170-271`).

### 2.9 Añadir y quitar un juego

**Entradas.** Ficheros `.lua` en `<steam>\config\stplug-in\` (y `[lua] paths`),
ejecutados en el Lua con sandbox; los escribe SteaMidra. El nombre numérico del
fichero registra la app aunque el cuerpo solo traiga depots
(`config/LuaQuery.cpp:340-372`).

**Qué escribe al añadir.** Nada en disco: todo es memoria (package 0, sets de
Lua) y refresco en caliente sin reiniciar Steam (`DirWatch` → `ParseFile` →
`NotifyLicenseChanged` → `MarkAppChange` en SteamUI,
`hooks/ui/SteamUI.cpp:230-271`). En el registro y en el ACF de 480 solo escribe
al lanzar.

**Qué limpia al quitar.** Borrar el `.lua` → `UnloadFile` → bajas en el package
0 y en la biblioteca (`QueueLibraryRemoval`). Quedan en el registro los
`SteamID`/`AppTicket`/`ETicket` escritos y el ACF de 480. Un desbordamiento del
watcher pierde eventos sin re-escaneo (`runtime/DirWatch.cpp:113-136`).

**Varias bibliotecas, nombre de carpeta.** Cosa de Steam (instala él);
`FindAcfPath` solo lee bibliotecas con letra de unidad
(`DecryptionKeyHook.cpp:198-226`).

### 2.10 Credenciales y proveedores

Ninguna propia. Usa la sesión de Steam del usuario (y su ticket de la app 7
para forjar). Proveedores de códigos sin clave. SteamIDs de donantes
incrustados. Tickets/ETickets que traiga el `.lua`. La URL de ETicket
(`seteticketurl`) está sin efecto. **Sin ninguna**: funciona todo menos los
códigos de manifest (si los proveedores no responden) y las TOML de un cliente
nuevo.

### 2.11 Mantenimiento propio

**Superficie.** Ninguna propia: config `lumacore.toml` (recarga en caliente al
siguiente handshake), `status.json`, `MessageBoxA` de diagnóstico. **Update de
sí mismo**: no; lo instala y actualiza SteaMidra comparando tags
(`steamidra.md` §2.11), y `status.json` declara `V36` en el binario `V37`
(`runtime/HookStatus.cpp:160`). **Componentes**: patrones por caché + red (§2.1);
una TOML cacheada no se vuelve a pedir. **Salud y logs**: `status.json` con
`hooks_installed`, `hooks_missed`, `toml_found`, `diagnostic_reason`; logs por
módulo solo en Debug, que la CI publica como `Debug.zip`.

### 2.12 Proyecto

**Plataformas.** Windows x64. **Código abierto**: GPL-3.0, sin tests; CI que
compila y publica en cada push a `main` sin checksums ni firmas
(`.github/workflows/release.yml`). **Quién y ritmo**: Midrag hasta 08-21; desde
entonces 9 commits de terceros (un arreglo, un PR grande de michelegoku3 y la
CI). **Servicios de terceros**: MigoReleases (una persona, un bot), su espejo
jsDelivr, gitflic `midrags/steam-auto-pt`, tres proveedores de códigos, y en
build `lua.org` sin verificar TLS y Detours sin fijar. **Telemetría**: ninguna
(no hay más red que la de §3.1). **Riesgo de detección**: es el más alto del
ecosistema que hemos leído: reescribe mensajes del CM (stats con SteamID ajeno,
tokens PICS, family sharing), forja tickets con la firma de la app 7 y los
sirve al juego, y apaga Steam Cloud por app. **Concurrencia**: el estado de
Lua (el `lua_State` y los conjuntos) no tiene mutex y lo tocan el hilo del
watcher, los hilos de `ManifestFetch` (que llaman a `fetch_manifest_code` del
script) y los hooks de Steam.

---

## §3 Superficies externas

### 3.1 Hosts y URLs

| URL / host | Fichero:línea | Uso |
|---|---|---|
| `https://raw.githubusercontent.com/michelegoku3/MigoReleases/pattern/{steamclient,steamui}/<sha>.toml` (+ `.sig`) | `patterns/PatternFetcher.cpp:49-51,520-529,839` | patrones |
| `https://cdn.jsdelivr.net/gh/michelegoku3/MigoReleases@pattern/…` | `PatternFetcher.cpp:50-52,531-540` | espejo |
| `https://gitflic.ru/api/project/midrags/steam-auto-pt/blob?branch=pattern&file=…` | `PatternFetcher.cpp:63-64,542-554` | espejo "para regiones bloqueadas" (`gitflic_enabled`) |
| los tres anteriores con `steamclientipc/<sha>.toml` | `runtime/IpcSpecLoader.cpp:352-358`; `hooks/client/IpcMethodLoader.cpp:164-169` | hashes de métodos IPC |
| `[pattern_fetch] mirror` (plantilla `{subdir}`, `{sha}`; acepta `file://`) | `PatternFetcher.cpp:886-913,240-256` | espejo de usuario |
| `https://manifest.opensteamtool.com/{gid}` (UA `OpenSteamTool/1.0`), `https://manifest.steam.run/api/manifest/{gid}`, `http://gmrc.wudrm.com/manifest/{gid}` | `config/Settings.h:86-90`; `runtime/ManifestFetch.cpp:140-142,185` | códigos de manifest |
| lista de `lcHttpGet/Post`: `manifesthub1.filegear-sg.me`, `raw.githubusercontent.com`, `cdn.jsdelivr.net`, `gitflic.ru`, `api.github.com` | `config/LuaBindings.cpp:54-60` | HTTP desde el `.lua` |
| URL de `seteticketurl()` (POST JSON) | `runtime/EticketFetcher.cpp:96` | muerto |
| Steam CM (mensajes reescritos por el propio cliente) | `hooks/client/NetPacket.cpp` | stats, PICS, family sharing, códigos |
| build: `github.com/microsoft/Detours` (`main`), `www.lua.org/ftp/lua-5.5.0.tar.gz` (TLS sin verificar), `github.com/protocolbuffers/protobuf` (`v3.15.3`), `gabime/spdlog` (`v1.17.0`), `marzer/tomlplusplus` (`v3.4.0`) | `source/cmake/Lc*.cmake` | dependencias |

### 3.2 Ficheros y registro

| Ruta | Quién | Para qué |
|---|---|---|
| `<steam>\{xinput1_4,dwmapi,LumaCore,LumaCorePayload}.dll`, `<steam>\lumacore.toml` | SteaMidra los pone; la DLL los lee | carga y config |
| `<steam>\config\stplug-in\*.lua` (+ `[lua] paths`) | lee y vigila; crea la carpeta si falta | entrada |
| `<steam>\lumacore\pattern\<sha>.toml` (+ `.tmp`), `…\pattern\steamclientipc\<sha>.toml` | escribe (caché) | patrones |
| `<steam>\lumacore\status.json` (+ `.tmp`) | escribe, atómico | estado |
| `<steam>\lumacore\<módulo>.log` (Debug) | escribe | logs |
| `%APPDATA%\SteaMidra\lumacore_diag.txt` | añade al descargar | anillo de logros |
| `<steam>\steamapps\appmanifest_480.acf` (lo crea si falta), `appmanifest_<real>.acf`, `libraryfolders.vdf` | escribe idioma / lee | ruta 480 |
| `<steam>\userdata\<cuenta>\<app>\remote\<fichero>`, `userdata\<cuenta>\<appid>` | lee | saves en ruta 480; SteamID por carpeta |
| `<dir del payload>\lumacore\payload\<pid>.log` (Debug) | escribe | payload |
| `HKCU\Software\Valve\Steam\Apps\<id>\{SteamID,AppTicket,ETicket}` | lee y escribe | tickets e identidad |
| `HKCU\Software\Valve\Steam\ActiveProcess\{ActiveUser,Universe}`, `…\Steam\SteamPath` | lee | cuenta activa |
| `HKCU\Software\Valve\Steam\lumacore\LastActiveSteamId` | escribe | caché de cuenta |
| ConfigStore: `…\<depot>\DecryptionKey`, `Software\Valve\Steam\depots\<id>…`, `apptickets\<id>`, `UserAppConfig\480` | lee / escribe el último | claves, tickets, idioma |

### 3.3 Variables de entorno

Ninguna propia (no hay `getenv`). Lee las del proceso de juego: `SteamAppId`,
`SteamGameId`, `SteamOverlayGameId` (`hooks/client/PipeWatch.cpp:396-406`).

### 3.4 Claves de configuración (`lumacore.toml`) y del `.lua`

| Clave | Defecto | Efecto | Fichero:línea |
|---|---|---|---|
| `[log] level`, `verbose` | `debug`, `true` | nivel de spdlog (solo Debug) | `config/Settings.h:21,28` |
| `[lua] paths` | `[]` | carpetas extra de `.lua` | `Settings.cpp:122-130` |
| `[lua] http_allowlist` | — | **no se lee** (`luaHttpAllowlistExtra` vacío siempre) | `Settings.h:45` |
| `[pattern_fetch] mirror`, `gitflic_enabled`, `require_signed` | `""`, `true`, `false` | espejo, gitflic, firma obligatoria | `Settings.h:51-69` |
| `[manifest_fetch] urls` / `url`, `timeout_sec`, `trusted_hosts` | 3 proveedores, `12`, 3 hosts | cadena de códigos; `trusted_hosts` **sin efecto** | `Settings.h:86-97` |
| `[stats] enable_api` | `true` | **sin efecto** | `Settings.h:98` |
| `[process_extension] enabled`, `x86`, `x64` | `false` | inyector de DLL en juegos | `Settings.h:99-101` |
| `[onlinefix] inject_enabled` | `true` | payload en la ruta 480 | `Settings.h:107` |
| `[steamstub] auto_enabled` | `false` | ruta SteamStub automática | `Settings.h:114` |
| `[boot] diagnostic_popup` | `true` | `MessageBoxA` sin specs | `Settings.h:123` |
| `.lua`: `addappid`, `addtoken`, `setManifestid`, `setAppticket`, `setEticket`, `setStat`, `skipManifestPin`, `addprocess`, `forcedenuvo`, `seteticketurl`, `getCachedAppTicket`, `getDecryptionKey`, `fetchManifestCode(Ex)`, `lcHttpGet/Post`; globales `fetch_manifest_code(_ex)` que define el script; `_originals` | — | §2 | `config/LuaState.cpp:125-146`, `LuaBindings.cpp` |

---

## §4 Lo que nos afecta

### 4.1 Hallazgos pendientes de decisión

Aparcados el 2026-10-08. Numeración propia de este doc.

1. **`steamidra_lite.py` lee el `.lua` de forma más estrecha que LumaCore.**
   LumaCore ejecuta el fichero con nombres de función sin distinguir
   mayúsculas (`addAppId`, `AddAppId`, `SetManifestId`…,
   `config/LuaState.cpp:152-173`), varias sentencias por línea y la clave en
   cualquier segundo argumento; nuestras regex exigen `addappid` y
   `setManifestid` exactos al principio de línea y un dígito como segundo
   argumento (`steamidra_lite.py:103-122`). Un `.lua` que LumaCore acepta puede
   darnos cero depots sin error. A la inversa, LumaCore descarta claves que no
   midan 64 hex y nosotros no. Ningún `.lua` de Hubcap de los que tenemos usa
   otra grafía; sin decisión. De paso: el comentario de
   `steamidra_lite.py:116` cita `LuaLoader.cpp:187`, fichero que ya no existe
   (hoy `config/LuaBindings.cpp:361-392`).
2. **Verificación de bytes tras el RVA.** LumaCore publica `rva` y `sig` y no
   compara la `sig` (§2.1): un RVA desplazado cae en mitad de otra función.
   Nuestro feed hace lo contrario (RVA, comprobación de que cae en el `r-x` de
   `steamclient.so` y patrón de respaldo, `nosotros.md` §2.1). Es el argumento
   para no aflojar esa comprobación nunca. Sin acción.
3. **`status.json` atómico.** LumaCore escribe `status.json` y las TOML por
   `.tmp` + rename (`runtime/HookStatus.cpp:602-665`); el nuestro hace
   `fopen("w")` directo (`nosotros.md` §4.1-24). Es la referencia para
   arreglar ese punto si se decide.
4. **Códigos de manifest sin comprobar en el CDN.** LumaCore entrega a Steam
   el primer número que un proveedor devuelva; nosotros solo tras aceptarlo el
   CDN (`nosotros.md` §2.4). Sus tres proveedores por defecto son los que
   SteaMidra retiró el 09-12 por 403/404. Dato para la fila de la función 4.
5. **Tercer camino de clave.** El candidato "segunda costura de clave por
   `ConfigStore`" (§4.4 D2) tiene ahora tres caminos en LumaCore. Sigue
   condicionado a ver un fallo de clave que DepotKey no cubra.
6. **Feed de patrones sin las claves de los hooks `_STR`.** Cuatro hooks de
   LumaCore no se instalan con ninguna TOML de MigoReleases (§5.2). No nos
   afecta (Windows); es la segunda prueba de que un feed de terceros puede
   cubrir el SHA y no las funciones (la primera, `Steam-Auto-PT` parado,
   §5.2). Va con el balance de `update-resilience-comparison.md`.
7. **MigoReleases como señal de beta de escritorio.** Publica TOML para cada
   beta de Windows en horas (cuatro betas entre 09-29 y 10-08, §5.2). Sigue
   siendo la mejor señal externa de "ha salido una beta"; no la consumimos.

### 4.2 Dependencias y qué rompe si cambia

| Si cambia… | Qué nos pasa |
|---|---|
| el formato del `.lua` que LumaCore acepta (bindings nuevos, grafías) | los `.lua` de SteaMidra/Hubcap pueden traer cosas que `steamidra_lite.py` no ve (§4.1-1) |
| las funciones que LumaCore engancha (claves, pin, package 0) | nada directo; es la referencia con la que alineamos los hooks (`RESEARCH.md` §11) y conviene releerla en cada versión |
| MigoReleases | nada; no lo consumimos |
| LumaCore deja de mantenerse | nada para nuestra pila; SteaMidra en Windows pierde su motor |

### 4.3 Lo que LumaCore tiene y nosotros no (y al revés)

| Función | LumaCore | Nosotros | Notas |
|---|---|---|---|
| 1 | proxies DLL en la carpeta de Steam; TOML por SHA (RVA, sig sin comprobar) de un feed de terceros; sin gate, sin crash guard | wrapper `LD_AUDIT`/`LD_PRELOAD`; feed RVA propio + patrones + rescates; guard anti crash-loop | Windows ↔ Linux; mismo concepto de feed por hash |
| 2 | package 0 (`LoadPackage`/`GetPackageInfo`) + `CheckAppOwnership` + `GetSubscribedApps` + refresco con `MarkLicenseAsChanged`; family sharing por mensajes vaciados | SLSsteam (propiedad) + finder del package 0 + `NotifyLicensesUpdated` | misma idea, otra capa; el reconcile completo convergió el 10-07 |
| 3 | DLC = ids del `.lua`, sin límite | `DisabledDLC` por LumaDeck | |
| 4 | claves por tres caminos; códigos por el cable con proveedores sin comprobar | DepotKey; GMRC por función con CDN | §4.1-4, -5 |
| 5 | pin por `BuildDepotDependency` (activo) | pin por `ManifestIds` de SLSsteam; BuildDep apagado | |
| 6 | tickets forjados (app 7), ruta 480 con payload EOS, SteamStub (apagado), `RequiresLegacyCDKey` | fixes por botón en LumaDeck (lua.tools, Steamless, Goldberg, EOS) | no portable: capa de mensajes de SLSsteam (§4.4 D0) |
| 7 | stats con SteamID de donante por el cable | SLSsteam + parche `sls_achievement_unblock` + CloudRedirect | |
| 8 | apaga Steam Cloud para no poseídos | CloudRedirect | |
| 9 | `.lua` en `stplug-in`, en caliente, alta y baja de biblioteca | `steamidra_lite` + reconcile; solo alta en caliente | la baja en caliente está aparcada en la matriz (moon, función 9) |

### 4.4 Decisiones ya tomadas (del doc anterior, con fecha) y estado hoy

| # | Decisión | Fecha | Hoy |
|---|---|---|---|
| D0 | Lo que LumaCore hace en la capa de mensajes o IPC (cable, `IPCProcessMessage`, VEH) no se porta a lumalinux: SLSsteam ya es dueño de esa capa y un segundo envoltorio choca; lumalinux usa costuras de función | 2026-08-13 | vigente |
| D1 | Corregir `RESEARCH.md` §11.5 y `nosotros.md`: LumaCore sí tiene puente GMRC (por el cable) | 2026-08-13 | **hecho** (`RESEARCH.md` §11.5; `nosotros.md` reescrito) |
| D2 | Segunda costura de clave por `ConfigStore`: candidata condicionada; prueba de aceptación = instrumentar sin servir y ver si alguna instalación lee la clave por ahí | 2026-08-13 | aparcada; ahora son tres caminos (§4.1-5) |
| D3 | Nota de frontera en `FIXES_MAP.md` (por qué no hay DenuvoAuth ni payload online-fix en lumalinux) | 2026-08-13 | **no hecho** |
| D4 | Una línea en `design/rva-feed-design.md`: el antepasado por patrones es más frágil y ninguno firma el feed | 2026-08-13 | **no hecho**; el matiz de hoy: LumaCore también publica RVA, pero no comprueba la firma (§4.1-2) |
| D5 | El parche de logros (`sls_achievement_unblock`) cubre lo que LumaCore hace con donantes; nada que portar | 2026-08-13 | vigente |
| D6 | No tocar el estado de sincronización de Steam Cloud (lo lleva CloudRedirect) | 2026-09-12 | vigente |
| D7 | MigoReleases es la mejor señal de beta de escritorio; no se consume | 2026-10-07 | vigente (§4.1-7) |

---

## §5 Historial

§5.1 recoge lo que se creía de LumaCore y el código desmiente, además de lo
listado al principio, y lo que sigue sin medir. §5.2, las mediciones con fecha.
§5.3, la cronología del repositorio.

### 5.1 Lo que se creía y ya no es así

Además de lo listado en la cabecera:

- **"40+ hooks Detours".** Son 32 detours en `LumaCore.dll` (25 en
  `steamclient64`, 5 en `steamui`, 2 en `kernel32`) más 2 trampas `int3`; el
  `40+` sale de su `README.md`.
- **"Las cuatro capturas VEH".** Solo quedan `GetAppDataFromAppInfo` y
  `SpawnProcess`; las de `GetAppIDForCurrentPipe`, `MarkLicenseAsChanged` y
  `GetPackageInfo` son Detours (`RuntimeCapture.cpp:577-582`). El doc anterior
  ya lo había visto; su `docs/LumaCore.md` no.
- **"Denuvo: OEP / entropía W+X 7.0+ / cadena de sección, ventana de dos
  handshakes que sirve el SteamID del dueño".** La entropía está en código
  muerto; la ventana viva no cambia la respuesta (§2.6).
- **"El acuñado de ETicket por HTTP GET con `{appid}`".** Es un POST JSON sin
  sustitución, y está muerto.
- **"`NotifyRunningApps` reescribe la lista saliente".** Vacía la respuesta
  entrante.
- **"ManifestBind verifica que el manifest se cifró con la clave".** Reescribe
  el gid del vector de depots.
- **"El payload redirige los lobbies".** Quita la presencia de las opciones de
  lobby.
- **"Lua 5.4".** Lua 5.5.0, descargado sin verificar TLS.
- **"LumaCore engancha la copia para no parchear código en uso".** Engancha el
  original (desde `c0d0537`).
- **Del lado nuestro, el doc anterior daba como paridad** "proveedores
  opensteamtool → wudrm → steamrun" y "pin por `depot_dependency_hook`"; hoy
  nuestros proveedores son 20770407 → manifestdex → wudrm → steam.run y el pin
  va por `ManifestIds` (`nosotros.md` §2.4, §2.5).
- Lo que el doc anterior decía de SLSsteam (`ticket.cpp`, `apps.cpp`,
  `DenuvoGames`) y de LumaDeck (esquema de logros offline) no es de este
  programa: está en `slssteam.md` y `nosotros.md`.

**Lo que sigue sin medir** (Windows, no hay banco):

- Si el CM acepta `ClientStoreUserStats2` de una app no poseída y los logros
  persisten entre máquinas.
- Si los tres proveedores de códigos responden hoy (no medidos).
- Qué hace Steam con un hook puesto en un RVA viejo de una TOML válida.
- Si las carreras del estado de Lua (§2.12) llegan a romper algo en un
  hot-reload.

### 5.2 Pruebas y mediciones con fecha

#### Función 1 — Engancharse a Steam

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-23 | `KoriaPolis/Steam-Auto-PT` rama `pattern` | último commit del feed original | `654dab1`, 2026-08-19 ("stable 1785799196, beta 1787097529"); 28 TOML de `steamclient` frente a 24 del feed de madoiscool, 15 comunes, 9 solo en madoiscool (todos posteriores al 19-ago, entre ellos el estable Windows `1788652215`) | desde el estable del 5/8-sep, LumaCore sin patrones por red hasta que SteaMidra empezó a precalentar desde MigoReleases (`ffd70db`, 09-08) |
| 2026-09-28 | MigoReleases | contenido | dos `steamclient` (`caba4826…` = estable 1788652215, `3f864358…` = 1782533657), dos `steamui`, dos `steamclientipc`; prueba de beta el 23-sep revertida el 24 | TOML de "SteamPatternForge", cabecera "Made by migo3" |
| 2026-10-07 | MigoReleases | refresco | 27 ficheros, `state/stable.txt = 1788652215`, `state/beta.txt = 1791249696`; betas 1790545198 (09-29), 1790721607 (09-30), 1790904859 (10-02), 1791249696 (10-06) | el tracker de slsteam-moon estaba parado, no Steam (corregido en `slssteam.md` y `slsteam-moon.md`) |
| 2026-10-08 | MigoReleases `@61988bc` | refresco y contenido | 31 ficheros (7 `steamclient`, 7 `steamui`, 7 `steamclientipc`, 8 `bundle`, 2 `state`); beta nueva 1791415817 (10-08 01:03); **0 ficheros `.sig`**; cada TOML de `steamclient`, 40 nombres (54 secciones en seis de ellas, con alias) | firma: imposible aunque la DLL la pidiera |
| 2026-10-08 | MigoReleases `@61988bc` vs `source/` | nombres que la DLL busca frente a los de la TOML estable `caba4826…` | presentes todos los de `LM_INSTALL`/`LM_BIND`/`LM_CAPTURE`/`SpawnProcess` salvo `CConfigStore::FlushToDisk`, `CConfigStore::SetString`, `IClientRemoteStorage::FileExists`, `IClientRemoteStorage::Dispatch`; los cuatro faltan en **las siete** TOML | esos hooks/binds no se instalan nunca; `status.json` queda en `hooks-missed` |

#### Función 2 — Propiedad y licencias

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-10-07 | `c0d0537` (michelegoku3) | reconcile del package 0 en caliente | contra el conjunto completo de `.lua`, no el delta ("Aether parity") | el mismo día y por el mismo motivo que lumalinux `4f8579c` (`RetireDepots` + `InjectDepots` en cada pasada del finder) |

#### Función 8 — Cloud saves

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-12 | `0b7e846` (SteaMidra 6.6.6) | `GetRemoteStorageSyncState` al bloquear la nube | de 1 a 2 ("synchronized, not syncing"); con 1 Steam mostraba "sincronizando" sin fin | sigue en 2 (`LicenseHooks.cpp:206-212`) |

#### Función 11 — Mantenimiento propio

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-12 | `0b7e846` | `kLumaCoreVersion` | V35 → V36 | hoy sigue en V36 con el tag V37 |
| 2026-09-28 | subárbol `LumaCore/` de `drappula/SFF` | actividad | commits por mes: 13 en julio, 5 en agosto, 1 en septiembre; ninguno de Midrag desde el 21-ago | "en mantenimiento, no muerto": SteaMidra 6.8.0 lo volvió a poner por defecto en Windows |

### 5.3 Cronología

**Topología.** `drappula/LumaCore`: 35 commits desde `8ec4e88` (2026-05-25), la
historia del subárbol `LumaCore/` de SteaMidra extraída el 3-oct (`e4c6b65`,
`da3108e` en SFF, `steamidra.md` §5.3). Un tag, `V37`.

- **2026-05-25 → 2026-08-21, Midrag (26 commits).** Releases de SteaMidra
  aplastadas: 6.2.4 inicial, sandbox de Lua (6.2.8, `b0ec680`), "LumaCore
  security" (6.3.1, `a55c4c7`), docs de LumaCore (`8321f69`, 07-04, la última
  vez que se tocó `docs/LumaCore.md`), puente GMRC por el cable en 6.6.4
  (`1a43bda`, 08-11), 6.6.6 (`82df85d`, 08-21: estado de sincronización 2,
  V36). Barrido 2026-08-17 sobre 6.6.5: el subárbol no cambió.
- **2026-09-04, wtfseanscool (`5884159`).** ABI de
  `IClientRemoteStorage::FileExists`: de `(this, pchFile)` con escritura en
  `this+0x38` a `(this, appId, fileRoot, pchFile)` cambiando el argumento.
- **2026-09-30, michelegoku3 (`c0d0537`).** Una tanda portada de Aether: fin de
  la copia `lcoverlay` con aserción de identidad, proxies con hilo y
  comprobación de host (Aether `14a6359`), `xinput1_4` como puerta principal,
  layout de `CNetPacket` detectado en vivo (la beta movió `m_pubData`/`m_cubData`
  de +0x08/+0x10 a +0x10/+0x18), `AppLicensesChanged` con recarga forzada,
  reconcile completo del package 0, feed de patrones a MigoReleases en raw y
  jsDelivr, `LoadModuleWithPath` comparando por nombre base.
- **2026-10-03, mallusrgreat (7 commits).** CI de releases; la DLL precompilada
  sale de git (`source/*.dll` en `.gitignore`). Primera release: `V37`.
