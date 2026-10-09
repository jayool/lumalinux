# slsteam-moon — el fork de SLSsteam de swwayps, leído desde el código

Leído desde el código el 2026-10-08. Sustituye a `slsteam-moon-findings.md`;
sus mediciones con fecha están en §5.2, sus "portables" con su estado en §4.4
y su historia por releases en §5.3. `slssteam.md` describe el upstream; aquí se describe el
fork y, en cada punto, si es igual que upstream o distinto. Cómo **nosotros**
hacemos lo mismo está en `nosotros.md`.

---

## §0 Ficha

| | |
|---|---|
| **Qué es** | Un fork de SLSsteam (base: el upstream de mayo de 2026, `VERSION = 20260528151547`) que convierte el unlocker en una **pila completa de instalación nativa** dentro del mismo `.so` de 32 bits: lee los `.lua` de LuaTools (`<Steam>/config/stplug-in/<appid>.lua`) como fuente de juegos, inyecta los appIds y depotIds en el **package 0** real de Steam, provisiona el appinfo de apps no poseídas con un **cliente CM anónimo propio** y lo **empalma en `appcache/appinfo.vdf`** antes de que Steam lo abra, toma las claves de depot de los `.lua` (y sustituye la respuesta de Steam cuando falla), baja los manifests del **archivo público de LuaTools** (`manifest.luastools.xyz`) y los guarda en un store propio, aplica pins de manifest en el plan de descarga y en el reconcile, suprime updates según pins y proveedores, pide esquemas de logros con un **SteamID fijo** en vez de reseñadores, **dona** códigos de petición de manifest de los depots que la cuenta posee, integra Steamless (opt-in), desbloquea el control parental (opt-in), retira juegos de la biblioteca sin reiniciar, y recarga todo en caliente en el hilo IPC de Steam. Alrededor: un wrapper de 550 líneas con guard anti crash-loop, cobertura de `.desktop`, shim root del lanzador de la distro, guardian systemd, un helper nativo que baja **catálogos de patrones firmados** (Ed25519) por SHA-256 del `steamclient.so`, y tres satélites externos (Lumen, luatools-moon, cloudredirect-moon). 43 objetos de hook en `hooks.cpp` (32 detours, 11 VMT) más los que instalan las feats (package 0, UI de `steamui.so`, pipeline de depots, cuarentena, reconcile, parental, caché de appinfo). No tiene decompilador RTTI ni Lua de plugins: los índices de vtable son constantes. |
| **Repos y commits leídos** | `swwayps/slsteam-moon` `origin/slsteam-moon@f50f28e` (2026-09-29, "revert(dlc): restore active-app gate on dlc checks"). Tags `v2.6` (06-30), `v2.6-millennium`, `v2.7` (07-02), `v2.8` (07-21), `v2.9` (09-19); 22 commits sin tag después de v2.9. 381 commits desde `84450ef` (2026-05-31, "chore: import upstream tree"); sin ancestro común con upstream (77 commits de AceSLS cherry-pickeados). `src/` ≈ 47.000 líneas propias (15.100 núcleo, 30.500 `feats/`, 1.500 `sdk/` sin protobufs, 3.000 `utils/`), `tools/` 21.700 (97 tests unitarios), `setup.sh` 1433, `scripts/` 4.900. Ramas `beta` (09-04) y `millennium` (07-22) son **viejas**, no adelantadas. |
| **Plataforma** | Linux x86 de 32 bits dentro de `steam` (`ubuntu12_32`); helper `pattern-refresh` nativo x86_64. Distros nombradas en código: Debian/Ubuntu/Mint (`/usr/games/steam`), Arch/Fedora/Nobara (`bin_steam.sh`), openSUSE, Pop!_OS, SteamOS/Bazzite/Silverblue (inmutables), NixOS (bwrap), ChimeraOS. **Sin Flatpak**. Deck/Game Mode: solo si Game Mode invoca `steam` por PATH o por el shim; no hay drop-in de `steam-launcher.service`. Build "portable" en `ubuntu:22.04` (glibc ≤ 2.34). |
| **Licencia y quién** | AGPL-3.0-only (ficheros nuevos con cabecera SPDX). Steamless redistribuido verbatim bajo CC BY-NC-ND 4.0. Autor principal `unplausible` (296 de 381 commits), organización `swwayps`; PRs de FATEx0 (pins, reconcile) y dankrr (presencia de shortcuts); 77 cherry-picks de Ace SLS. Ritmo ≈ 3,1 commits/día entre 05-31 y 09-29; 5 tags lightweight; `res/version.txt` **nunca cambia** (todo build embebe `20260528151547`). Soporte por issues de GitHub (#6 y #7 sin respuesta). |
| **Relación con los demás programas** | Fork de **SLSsteam** (§5.3). Pareja de **luatools-moon** (plugin que escribe los `.lua`, `luaappids.yaml` y `ManifestPins`), **Lumen** (sidecar que inyecta el frontend de LuaTools por el puerto CDP de CEF que moon le publica), **cloudredirect-moon** (CloudRedirect por `LD_PRELOAD` desde el wrapper), **steam-monitor** (catálogos de patrones firmados) y el espejo **jsdelivr** (releases). Manifests del **archivo de LuaTools**; el README acredita a Midrags/SFF/**LumaCore** ("Windows reference port for the LoadPackage patch logic") y a OpenSteamTool (ticket derivado de la app 7). **SLSDeck carga moon** y solo moon. Nosotros: el modelo de wrapper de `setup.sh` viene de moon; portamos su reconcile de licencias (v0.16.15) y `-fno-reorder-blocks-and-partition` (v0.16.9); rechazamos su SafeMode-off, su feed firmado, su donación de códigos y su shim root (§4.4). |

**Lo que cambia respecto al doc anterior**, en una línea cada uno:

- **Moon ya no inyecta códigos de petición de manifest** (`c1b5e15`, 09-21): el único origen de manifests es `https://manifest.luastools.xyz/m/<depot>/<gid>`; `gmrc.wudrm.com` y `manifest.steam.run` no existen en el código (solo en comentarios viejos). Las secciones del doc anterior sobre "GMRC en moon" describen código borrado.
- **`isSubscribed` ya no significa "poseído de verdad" en moon**: como inyecta en package 0, el `CheckAppOwnership` original devuelve `ownsLicense=true, subId=0` para las apps gestionadas; moon usa `StatsPolicy::hasNativeLicense` (Steam observó `subId > 0`) para distinguir. El mismo fenómeno que medimos con nuestra inyección (`nosotros.md` §2.7).
- **DLC ya no es "todo DLC no poseído"**: solo DLC descubiertos en el appinfo de bases gestionados (`listofdlc`, `dlcappid`) o declarados en `DlcData` bajo padre gestionado, con el gate de app activa **restaurado** en `f50f28e`.
- **Logros con una cuenta fija** (`AchievementOwnerId` = `76561198028121353`), reescribiendo bytes de la petición en el borde del WebSocket; sin reseñadores, sin cooldown, y solo para apps que moon inyectó.
- **Los pins de los `.lua` (`setManifestid`) no se aplican como gid**: solo marcan el depot como "en scope"; los pins reales son `ManifestPins` del YAML (que escribe Lumen/luatools-moon). Nadie en `src/` copia unos a otros.
- **SafeMode inerte** (forzado a `false`), feed de hashes apuntando al repo de **AceSLS**; lo sustituye el guard del wrapper: latch a vanilla al primer crash si `steamclient.so` cambió, borrando `appinfo.vdf`.
- **`library-inject.so` sigue activo** (`la_objsearch` de libcurl); upstream lo vació en agosto.
- **Quitar un juego sin reiniciar sí existe** (`LibraryRemoval`: detours en `steamui.so` que ponen `ownershipFlags = 0` y llaman a `MarkAppChange`); nosotros solo hacemos aparecer, no retirar (`nosotros.md` §2.9).
- **El catálogo firmado no aporta firmas nuevas**: solo RVAs para localizadores compilados; una política distinta descarta el catálogo entero. El doc anterior lo corrigió el 10-01; las secciones de agosto y septiembre no.
- **Ticket derivado de la app 7** en `main` (el "exploit de OpenSteam001" que upstream dejó en su rama `update`).
- **`Donate.Enabled: yes` por defecto**: con la sesión real del usuario pide y sube códigos de depots que posee. Decisión nuestra: no se dona (2026-09-28).
- **Moon comparte `~/.config/SLSsteam/` y `cache/` con SLSsteam vanilla**: una Deck que pase de SLSDeck a nosotros hereda su `config.yaml` (claves `Donate`, `AchievementOwnerId`, `AutoUpdateApps`…) y sus `depotkey_*.yaml`.
- API de fichero en `$XDG_RUNTIME_DIR/SLSsteam/api` (0600) con un solo comando; sin Lua de plugins; sin decompilador: índices de vtable fijos.

---

## §1 Mapa del código

Cobertura por fichero con la función de la matriz a la que sirve (números de
§2). `lib/` (libmem, protobuf-lite, yaml-cpp; **sin** luajit) e `include/` se
anotan sin leer; `src/sdk/protobufs/` son generados.

### 1.1 Núcleo (`src/`, `src/utils/`, `src/sdk/`)

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `main.cpp` | 1125 | entradas rtld-audit `la_version`/`la_preinit`/`la_objopen` y **`la_symbind32`** (nuevo); `setup()`: filtro `steam`, log en append, lock por namespace, política de audit, `OwnerWork`, limpieza `LD_AUDIT`, config, `DisableShaderCache`, deps runtime, puerto CEF, **provisión de appinfo en preinit** y splice de `appinfo.vdf`; `load()` one-shot con flock, build-ids, gate de hash (inerte), `Steam::init`, `Patterns`, `Hooks`, API, SteamStub, imports Lua, DLC ids, `HotReload`; wrappers de `exec*`/`posix_spawn*` que reescriben `--remote-debugging-port` | 1, 2, 4, 6, 9, 11 |
| `hooks.hpp/.cpp` | 199+1729 | 43 objetos de hook (32 detours + 11 VMT); VMT hooks colocados desde los detours de `IClient*::RunIPCFrame`; `Hooks::setup/place/remove`; arranque de PackagePatch, LibraryRemoval, ManifestBind, DepotQuarantine, ReconcilePin, Parental, AppInfoState | 1, 2, 3, 5, 6, 7, 8, 10, 11 |
| `patterns.hpp/.cpp` | 231+1657 | 55 patrones con `symbol` estable, `optional`, módulo; carga de caché local y catálogo firmado; `autoResolveIpcFrameRoots` (auto-corrección del byte raíz de los `RunIPCFrame`); comentarios de deriva por build | 1 |
| `pattern_catalog.hpp/.cpp`, `pattern_cache.hpp/.cpp`, `pattern_scan.hpp` | 85+414, 111+563, 171 | parser del catálogo TOML firmado y `validatePolicy`; caché local por build-id+size+mtime con revalidación byte a byte; escaneo con `memchr` que cuenta todas las coincidencias y `convergedTarget` | 1, 11 |
| `memhlp.hpp/.cpp` | 194+399 | `patternScan` solo sobre segmentos `r-x`, **unicidad obligatoria** o convergencia, `getJmpTarget` sin `stoul`, `findPrologue` acotado, `fixPICThunkCall` | 1 |
| `vftableinfo.hpp` | 57 | **índices de vtable fijos** (`GetDLCCount=8`, `LaunchApp=2`, `GetEncryptedAppTicket=121`, `SpecifyCompatTool=4`…); herencia del upstream de mayo | 1 |
| `config.hpp/.cpp` | 300+915 | ~45 `MTVariable`; fuentes de apps gestionadas (`stplug-in/*.lua`, `luaappids.yaml`, `AdditionalApps` legado, instaladas/Accela), `ManifestPins`, `Donate`, `Achievements*`, `MaxManagedApps`; parseo con **reparación** de YAML; watcher de 3 rutas; `onFileChange` → `HotReload` | 2, 3, 4, 5, 7, 8, 9, 11 |
| `config_discovery.hpp`, `config_path.hpp`, `confload.hpp`, `confnormalize.hpp` | 445, 19, 80, 154 | selección de fuentes con cap 4096, appId por nombre de fichero, `libraryfolders.vdf`, detección de instaladas y marcador `.DepotDownloader`; ruta XDG; `parseWithRepair`; `repairSeqIndent` (listas rotas por el escritor de LuaTools) | 2, 9, 11 |
| `config_default.hpp`, `version.hpp` | 197, 4 | generados en build pero **commiteados** (`res/config.yaml` envuelto; `VERSION`) | 11 |
| `filewatcher.hpp/.cpp`, `filewatcher_burst.hpp` | 37+237, 56 | inotify con `poll` de 500 ms, máscara por tipo de ruta, re-arme tras rename, join en `atexit`; drenado de ráfagas (250 ms, máx 5 s) | 9, 11 |
| `update.hpp/.cpp`, `update_cache.hpp` | 27+302, 119 | feed `updates.yaml` (solo GitHub raw **de AceSLS**), caché TTL 24 h en hilo de fondo, `verifySafeModeHash` salta el SHA si nadie lo consume | 1, 11, 12 |
| `api.hpp/.cpp` | 21+171 | fichero de comandos en `$XDG_RUNTIME_DIR/SLSsteam/api` (0600), un solo comando `install\|app\|lib` vía `OwnerWork` | 9, 11 |
| `process.hpp/.cpp` | 125+843 | parsers PE/ELF con límites; `Process_t` por pipe; SteamDRM/Denuvo por entropía; barrido de `map_files` solo con el bit Denuvo | 1, 6, 10 |
| `log.hpp/.cpp`, `notify.hpp`, `usermsg.hpp` | 217+136, 311, 289 | log en **append**, `LogLevel` numérico; único camino de popup `notifyUser` con catálogo EN/PT de 20 mensajes, throttle 60 s por categoría, escape de shell, cola JSON para Lumen | 11 |
| `curl.hpp/.cpp`, `cainfo.hpp` | 9+68, 163 | libcurl por `dlopen("libcurl.so.4")`, CA pinning por distro, `NOSIGNAL`, timeouts 10/5 s | 11, 12 |
| `audit_log.hpp`, `audit_policy.hpp`, `audit_symbols.hpp` | 75, 67, 67 | `write(2)` async-signal-safe; política `LA_FLG_BINDFROM/BINDTO` (bind-all por defecto, estrecha opt-in); clasificador de símbolos interceptados (`execv*`, `posix_spawn*`, `slsteam_local_stats_epoch_v1`) | 1, 11, 7 |
| `runtime_attestation.hpp`, `runtime_dependencies.hpp/.cpp`, `runtimedir.hpp`, `bootprof.hpp`, `thread_start.hpp`, `afftrace.hpp`, `ascii.hpp`, `mtvar.hpp`, `globals.*`, `utils.*`, `manifest_index.hpp` | 288, 37+65, 139, 63, 181, 747, 36, 45, 20, 49+373, 49 | evidencia JSONL opt-in; comprobación de `unzip`/`gzip`/`timeout`; directorio privado `$XDG_RUNTIME_DIR/SLSsteam`; marcas de arranque; hilos sin excepciones escapadas; traza de afinidad; ctype sin locale; `MTVariable` sin `shared_ptr`; SHA-256 propio y `getBuildId` | 1, 11 |
| `ownerwork.hpp/.cpp`, `utils/ownerqueue.hpp` | 224+473, 745 | cola acotada (32) de trabajo "Steam-owned" (package 0, reconcile, install, compat) hacia el hilo IPC dueño latchado en `IClientUtils::RunIPCFrame`; escape por staleness 30 s | 2, 9 |
| `utils/ManifestFetch.hpp/.cpp` | 287+881 | descarga de manifests del archivo luastools, `ProviderCircuit`, `BoundedExecutor(8,64)`, presupuesto 60 s, fichero `offline`, `archiveRequest` para el donante | 4, 10 |
| `utils/atomic_file.hpp`, `boundedexecutor.hpp`, `process_lock.hpp`, `contentserverdirectory.*`, `manifest_zip.*` | 385, 115, 120, 41+110, 16+271 | escritura atómica con `renameat2`; pool fijo; `FileLock` con `heldByAnother()`; URL SteamPipe (**sin llamadores**); ZIP de una entrada (**sin llamadores**) | 4, 9, 11 |
| `sdk/steam.*`, `CSteamEngine.*`, `CUser.*`, `CNetPacket.*`, `CAppOwnershipInfo.hpp`, `CPackageInfo.hpp`, `CProtoBufMsgBase.hpp`, `CUtl.hpp`, `CWebSocketFrame.hpp`, `IClient{AppManager,Apps,Compat,Utils}.*`, `IClientFriends.hpp`, `CSteamMatchmakingServers.hpp`, `CSteamID.hpp`, `EReleaseState.hpp`, `EResult.hpp` | 1525 en total | enums IPC y allocators de `libtier0_s.so`; `getUser/getUtils` por offset de patrón y `getLocalClientCompat` (valida la vtable); `isSubscribed`, `postCallback`, **`notifyLicensesUpdated`** por patrón; `CNetPacket` con **detección de layout** (shift 0/+8); `PackageInfo` (`AppIdVec +0x38`, `DepotIdVec +0x48`); EMsgs usados; `IClient*` por índices fijos | 1, 2, 4, 6, 9 |

Ficheros de upstream que **no existen** en moon: `decompiler.*`, `lua.*`, `vftableinfo.cpp`.

### 1.2 Features (`src/feats/`, 111 ficheros, 30.500 líneas)

| Grupo | Ficheros (líneas) | Qué hace | F |
|---|---|---|---|
| Propiedad y biblioteca | `apps.hpp/.cpp` (61+642), `stats_policy.*` (149+75), `clouddecision.hpp` (44), `librarydates.*` (28+157), `libraryremoval.*` + `_policy.hpp` (26+323+300), `familyshare.hpp` (25) | `checkAppOwnership` con gate de tipo y `hasNativeLicense`; `getSubscribedApps` sin overflow; `shouldDisable{Cloud,CDKey,Updates}`; filtros de red salientes; fechas de inclusión (`library-added-times.txt`); retirada visual en `steamui.so`; regla de family share | 2, 3, 5, 8, 9, 12 |
| Hot reload | `hotreload.*` (61+752), `hotreload_{types,capabilities,inputs,package,publish_policy,state}.hpp/.cpp` (25, 35, 138+220, 261, 205, 325), `license_refresh_policy.hpp` (36) | coordinador de generaciones, snapshot de package 0 (techos 16384 apps / 131072 depots), `readyBaseIds`, matriz de capacidades, gate del refresco de licencias | 2, 9 |
| Package 0, PICS, appinfo | `packagepatch.*` (91+1210), `pics.*` (188+863), `appinfo_vdf.*` (126+1379), `appinfostate.*` + `_policy.hpp` + `appinforeload.hpp` + `appdata_layout.hpp` (582+810+35+91+167), `synthmark.hpp` (560) | detour de `CPackageInfoCache::LoadPackage` (inyección en `AppIdVec`/`DepotIdVec` con `CUtlMemory::Grow`), reconcile y refresh de licencias; filtro del changelist PICS; lector/escritor de `appinfo.vdf` v41 transaccional; detour manual de `CAppInfoCache::GetOrAddAppData` (byte "skip"); marker sintético y cuarentena de artefactos | 2, 3, 4, 9 |
| Provisión | `appinfo_provision.hpp/.cpp` (372+4221), `provision_{cache,refresh,network,pass,result,schedule,terminal}.*` (430, 270, 197, 43, 113, 136, 59+225), `cache_pair.hpp` (181), `retry.hpp` (51, sin uso), `usabledepot.hpp` (64), `emptydepot.hpp` (59), `pending_proton.hpp` (123), `compattool.hpp` (92), `compatlive.hpp` (79) | cadena CM anónimo → `api.steamcmd.net`; validación de pares `picsbuffer_<id>.{bin,yaml}`; poda de depots sin clave; síntesis de `depots`/`launch`/`installdir`; `neutralizeLegacyCdKey`; terminales; `CompatToolMapping` en `config.vdf` o Proton pendiente; Proton en caliente | 2, 4, 5, 6, 9, 10 |
| CM | `cmclient.*` (69+782), `cmwire.hpp` (160), `cm_budget.hpp` (23), `cmlist_cache.hpp` (50), `cmrecv.*` (56+169) | websocket manual a los CM de Valve (logon anónimo 5514, PICS 8903/8904, `gzip` por `popen`); latch de transporte `InitFromPacket` vs `RecvPkt` y dispatcher de eMsgs | 1, 2, 4, 10 |
| Claves y cuarentena | `depotkey.*` (117+627), `depotkey_{scope,recv_policy,import,index}.hpp` (68, 44, 65, 105), `depotquarantine.*` + `_store.hpp` (142+428+152), `manageddepotfilter.hpp` (61) | catálogo `depotkey_<d>.yaml` (flag `managed`), importador de `addappid(d,1,"hex64")`, sustitución en 5439, `DisableShaderCache`; cuarentena por 3 chunks indescifrables (`dlcquarantine.txt`); filtro de depots de tamaño 0 | 3, 4, 5, 9, 11 |
| Manifests | `manifestid.*` (164+336), `manifestpins.hpp` (150), `manifestselection.hpp` (106), `manifeststore.*` + `_io.hpp` (91+332+260), `manifestsynth.hpp` (502), `manifestbind.*` (45+974), `manifestcode.*` (30+850), `manifestdonor.*` + `_policy.hpp` (18+726+129), `prewarm.*` (467+357), `reconcilepin.*` (130+424), `ipcframe.hpp` (258) | pins Lua y YAML; selección exacto/preferido/legacy; store `~/.config/SLSsteam/manifests/`; síntesis; tres detours en `CDepotDownloadMgr`; hooks del WebSocket (donante, stats); donante; prewarm legacy; parche del reconcile; resolutor de `RunIPCFrame` | 1, 4, 5, 7, 10, 12 |
| DLC | `dlc.*` (21+125), `dlc_metadata.*` (93+386), `dlcids.hpp` (457), `managed_dlc_scope.*` (48+27) | criterio de unlock por scope; sidecar de metadatos; extracción de ids (`listofdlc`, `dlcappid`); scope descubierto ∪ configurado | 3 |
| Tickets y DRM | `ticket.*` (103+378), `appticket.hpp` (165), `steamstub.*` (44+831), `steamstub_launchopt.hpp` (103), `steamstub_warmup.hpp` (69), `fakeappid.*` (36+282) | caché de tickets con mutex y tombstones, estampado `eresult=OK`; ticket derivado de la app 7; Steamless opt-in por `--steamless`; FakeAppIds (= upstream) | 2, 6, 10 |
| Logros | `achievements.*` (34+171), `playerstats.hpp` (213) | reescritura de bytes de `ClientGetUserStats`/`Player.GetUserStats#1` por jobid; parser protobuf a mano | 7 |
| Cosméticos y privacidad | `misc.*` (17+60), `parental.*` (190+239), `cefport.hpp` (380) | `FakeOffline`, `FakeEmail`, `FakeWalletBalance`; detour del receptor parental + parche `75→EB`; reescritura del puerto CEF y contrato para Lumen | 11, 12 |

### 1.3 Build, instalación, herramientas, tests y docs upstream

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `Makefile` | 592 | `g++ -m32 -O3 -flto=auto -std=c++20 -fno-reorder-blocks-and-partition`; `LDFLAGS = -shared -Wl,--no-undefined -lpthread -ldl` (**sin** libcurl ni openssl); `bin/library-inject.so` **compilado**; `bin/pattern-refresh` nativo x86_64 con clave Ed25519 embebida; 95 targets `test-*`; sin `DEBUG`, sin strip | 1, 11, 12 |
| `setup.sh` | 1433 | instalador: copia `bin/*` + 3 libs shell + CLI a `~/.local/share/SLSsteam`; genera el **wrapper** `path/steam` (550 líneas); PATH en rc files; sombras `.desktop` (guardian systemd `--user`); shim root del lanzador de distro (sudo); Steamless; `uninstall` | 1, 6, 8, 11 |
| `autofix.sh` | 203 | `curl \| bash`: baja el último release `*-lumen.zip` (API de GitHub → espejo jsDelivr con sha256), `setup.sh install`, relanza Steam | 1, 11 |
| `ensure-desktop-coverage.sh`, `tools/desktop-coverage.lib.sh`, `launcher-shim.lib.sh`, `desktop-guardian-units.lib.sh` | 92, 1619, 521, 416 | motor de cobertura de `.desktop` (clasificación, `Exec=` reescrito, backups centrales, política `launcher\|desktop`, huella); shim `/usr/bin/steam` → wrapper con trust check; unidades systemd `--user` y drop-ins de autostart | 1, 12 |
| `embed-config.sh`, `embed-version.sh` | 47, 34 | generan los headers; rechazan terminador `)SLSCFG"` y CR; `VERSION` solo dígitos | 11 |
| `scripts/` (24) | 4.902 | `Dockerfile` ubuntu:22.04 i386; `build.sh --host\|--portable`; `package.sh` (zip + sidecar `.sha256`, gates ABI ELF32/GLIBC ≤ 2.34, exige Steamless commiteado); `release.sh`; `check-compat.sh`; 15 `test-*.sh` de shell (cobertura, guardian, shim, wrapper guard, embed guards, steamless) | 1, 6, 11 |
| `tools/pattern-refresh/` | 50+207+1384 | helper nativo prelanzamiento: identidad de `steamclient.so`/`steamui.so` (sha256, build-id, segmentos X), baja `<sha256>.toml` + `.sig` de `raw.githubusercontent.com/swwayps/steam-monitor` o jsDelivr, verifica **Ed25519**, valida RVAs en segmento ejecutable, activa en `~/.config/SLSsteam/patterns/` | 1 |
| `tools/steamstub-bypass/` + `tools/steamless-bin/` | 118+345+186 + 11 ficheros | `run-steamless.sh` (Wine/Proton de Steam, `pkill -9 -f wineserver`, `--prewarm`), `install-steamless.sh`, `scan-all.sh` (lee `AdditionalApps` de `config.yaml`, no `stplug-in`); kit Steamless commiteado (`Steamless.CLI.exe` + plugins) | 6, 12 |
| `tools/library-inject/` | 89+6 | `la_objsearch` que redirige `libcurl.so*` a `/usr/lib32` etc.; **activo** | 1 |
| `tools/ticket-grabber/`, `appinfo_inject_test/`, `appinfo_roundtrip/`, `manifestid-test/`, `decode_vdf.py`, `popos-isolation/`, `protoc-3.15/` | 395, 328, 166, 180, 30, 88, 85 | ticket-grabber sin `EncryptedAppTicket` y **sin empaquetar**; herramientas de desarrollo de `appinfo.vdf` v41; harness del crash de Pop!_OS (`SLS_PASSIVE`, Millennium por `libXtst.so.6`); `protoc` fijado | 2, 4, 9, 10 |
| `tools/test_*.cpp` (97) | 15.897 | tests unitarios standalone (`CHECK`), varios "source-contract" (`grep` sobre `src/`); inventario en §2 y §5.3 | — |
| `res/config.yaml` | 195 | 36 claves con comentarios (§3.4) | 1-11 |
| `res/updates.yaml`, `res/version.txt`, `res/pattern-public-key.hex`, `res/runtime-probes.toml` | 21, 1, 1, 61 | 6 claves de hashes (truncado; última `20260528151547`); versión congelada; clave Ed25519 `79dab6cc…`; 7 sondas de `RunIPCFrame` para validación en Steam real | 1, 11 |
| `res/SLSsteam*.desktop`, `pkg/*/PKGBUILD` | 282+283, 48+57 | = upstream byte a byte; los PKGBUILD empaquetarían **upstream** (`source=` AceSLS) | 12 |
| `README.md`, `docs/BUILDING.md`, `docs/DIAGNOSTICS.md`, `docs/LICENSE/*` | 33, 155, 195, 661+… | portada mínima ("A fork of SLSsteam with extra protocol handlers, SteamStub support, and a Lua manifest importer"), instalación por luatools-moon; build (omite `pattern-refresh` y las libs del zip); variables `SLSSTEAM_AFFTRACE`/`SLSSTEAM_OWNER_QUEUE*`; licencias + `steamless` (CC BY-NC-ND), sin luajit/LuaBridge | 11, 12 |

---

## §2 Las doce funciones

Cada función: qué hace moon, con `fichero:línea` sobre `f50f28e`, marcando
**= upstream** (igual que `AceSLS/SLSsteam@049bbdd`, descrito en
`slssteam.md`), **heredado** (del upstream de mayo que 049bbdd ya no tiene) o
**moon** (nuevo o cambiado), y la lista de control de `ecosystem-matrix.md` §1
contestada. Mediciones con fecha en §5.2 ("§5 F<n>").

### 2.1 Engancharse a Steam

**Cómo entra.** `LD_AUDIT` como upstream, pero **solo** desde el wrapper
`~/.local/share/SLSsteam/path/steam` (`setup.sh:333-881`): exporta
`LD_AUDIT="$SLSDIR/library-inject.so:$SLSDIR/SLSsteam.so"`, `LD_PRELOAD` de
`~/.local/share/CloudRedirect/cloud_redirect.so` (si existe; "loading it as
an LD_AUDIT auditor corrupts the client heap") y `libextest.so` en Wayland,
lanza el sidecar Lumen, ejecuta `pattern-refresh --cache-only` y hace `exec`
del binario real. `library-inject.so` **no está vacío** (moon): su
`la_objsearch` redirige `libcurl.so*` a copias de 32 bits del sistema
(`tools/library-inject/main.cpp:42-80`); el comentario de `main.cpp:420-422`
explica el `LD_LIBRARY_PATH` por eso. `main.cpp:781-808` razona por qué
`LD_AUDIT` y no `LD_PRELOAD`: "Steam binds to OUR protobuf/std copies, and the
resulting ABI mismatch aborts Steam during store load (reproduced on
Pop!_OS)". Cuatro capas para que Steam pase por el wrapper (`setup.sh`): (1)
**shim root** en `/usr/bin|/usr/games|/usr/local/bin/steam` (con `sudo`, no en
inmutables, solo con Steam bootstrapped; el original a
`system-launcher-backup/<ruta>.orig` + alias `steam` porque `bin_steam.sh`
aborta con otro argv[0]; el shim exige que el wrapper sea fichero regular,
del EUID y sin escritura de grupo/otros, `launcher-shim.lib.sh:254-269`); (2)
**sombras `.desktop`** same-ID en `~/.local/share/applications`, autostart y
`~/Desktop` (siempre); (3) **guardian systemd `--user`** (`.path` + `.timer`
5 min + drop-ins `app-<id>@autostart.service.d`); (4) PATH en
`.bashrc/.zshrc/.profile`. **Sin Flatpak**; Deck/Game Mode solo si invoca
`steam` por PATH o shim (el comentario `setup.sh:558-561` lo da por hecho; no
hay drop-in de `steam-launcher.service`). Nada de ptrace ni parche en disco.

**Filtro de proceso.** = upstream (`main.cpp:174-179`), más dos locks:
`setup()` toma `flock` sobre `$XDG_RUNTIME_DIR/SLSsteam/setup.<pid>` porque
glibc instancia el auditor "once per link-map namespace" y "two SLSsteam.so
mappings in the minidump" (`main.cpp:213-231,451-457`); `load()` repite con
`load.<pid>` (`:481-524`). Si el lock no es "usable" se **continúa** (el de
`/tmp` de upstream "was enough for any local process to switch injection off
entirely", `runtimedir.hpp:2-18`). `unload()` ya no es un no-op: para
donante, `ManifestCode::resetSession`, `HotReload::shutdown` y
`Hooks::remove()` (`main.cpp:100-120`). **`la_symbind32`** corre en todo
objeto del proceso `steam` (bind-all por defecto; `SLSSTEAM_AUDIT_NARROW=1`
lo estrecha): intercepta `execv*`/`posix_spawn*` para reescribir
`--remote-debugging-port=8080` del webhelper (§2.11) y redirige
`slsteam_local_stats_epoch_v1` (§2.7) (`main.cpp:1052-1094`).

**Secuencia.** `la_preinit` → `setup()` (`main.cpp:165-433`): log en append,
política de audit, lock, `AffTrace`, `OwnerWork`, limpieza de `LD_AUDIT`,
`g_config.init()`, `DepotKey::disableShaderCache()` (escribe
`DisableShaderCache=1` en `config.vdf`), deps runtime, puerto CEF, **provisión
de appinfo en preinit** (precalienta libcurl porque cargarla desde un
callback PICS "can crash the 32-bit Steam client", aplica Proton pendientes,
provisiona las apps sin par válido, splicea buffers y metadatos de DLC en
`appcache/appinfo.vdf` antes de que Steam lo abra, `main.cpp:318-417`),
`LD_LIBRARY_PATH`, `Updater::init()`. `la_objopen` llama a `load()` al ver
`steamclient.so` o `steamui.so` (ya no espera a `libtier0_s.so`: `Steam::init`
lo abre con `dlopen`). `load()` (`:435-774`) bajo `try/catch`: attestation
opcional, build-ids al log, gate de hash (inerte), `Steam::init`,
`Patterns::init`, `Hooks::setup`, `SLSAPI::init`, `SteamStub::setup`,
`DepotKey::onStartup` + `ManifestId::importLuaScripts`,
`Apps::setDiscoveredAppDlcIds`, `HotReload::initialize()`, notificación. Los
VMT hooks se colocan desde los detours de cada `IClient*::RunIPCFrame` en su
primera llamada (`hooks.cpp:760-792,890-927,951-974,1029-1059,1350-1375`);
`IClientAppManager_RunIPCFrame` se des-hookea a sí mismo tras la primera.

**Cómo localiza lo que parchea.** Tres capas por prioridad
(`Pattern_t::find`, `patterns.cpp:577-662`): (1) **caché local**
(`PatternCache: yes`): `<config>/patterns/local/<component>/<buildid>-<size>-<mtime>.cache`
con `target_rva`/`match_rva`/firma/modo por localizador; antes de aceptarlo se
recomprueban los bytes en `match_rva`, la ejecutabilidad del destino y que
re-seguir la firma dé el mismo RVA; una discrepancia descarta el catálogo
entero y marca `dirty` (`:132-242`); se escribe solo si **todos** los
requeridos resolvieron. (2) **Catálogo firmado**
`<config>/patterns/<component>/<sha256(steamclient.so)>.toml` (`:294-376`):
cabecera `schema=1`, `platform=linux32`, identidad del módulo, `revision`,
`source ∈ {deterministic, model-validated}` y `[[locators]]` con
`target_rva`; `validatePolicy` exige que símbolo, firma, modo y `required`
coincidan con el registro compilado (`pattern_catalog.cpp:268-297`): **un
catálogo no puede introducir localizadores ni cambiar firmas, solo aportar
RVAs**. La firma Ed25519 la verifica el helper `pattern-refresh` fuera del
proceso (clave `res/pattern-public-key.hex`; URLs
`raw.githubusercontent.com/swwayps/steam-monitor/main/linux32/<component>/<sha256>.toml[.sig]`
y espejo jsDelivr; ETag; 2 s/4 s; revisión monotónica); el `.so` confía en el
`.toml` por SHA-256 del módulo. (3) **55 firmas embebidas**
(`patterns.cpp:893-1647`): mismo motor que upstream (`None`/`Relative`/`PrologueUpwards`)
pero `patternScan` recorre solo segmentos `r-x` y **exige coincidencia única**;
varias solo si el modo es `Relative` y todas convergen al mismo destino
(`convergedTarget`; upstream se queda con la última); el destino se valida
como ejecutable. `autoResolveIpcFrameRoots` (`:664-805`) reescribe en caliente
el id raíz del árbol de despacho de las seis firmas `IClient*::RunIPCFrame`
("drifts when Steam adds/removes interface methods (the 2026-06-23 update
broke all four this way)") con `IpcFrame::scan` y pivots de 24 bits para
RemoteStorage/UserStats; sin candidato único mantiene la semilla. **24 patrones
opcionales** (`:825-848`: `CAPIJob::GetPlayerStats`, `CUser::{MarkLicenseAsChanged,
ProcessPendingLicenseUpdates, NotifyLicensesUpdated, Offset_CompatManager}`,
`CCMInterface::RecvPkt`, `CAppInfoCache::*`, `CDepotDownloadMgr::*` (6),
`Parental*`, `SteamUI::*` (5)): si falta uno, su feature se apaga con `warn`;
si falta un requerido, `Patterns::init` devuelve false con el nombre y
`load()` aborta con `UserMsg::InitializationFailed` ("Leave the Steam beta
channel and run the install command again"). Sin decompilador: los índices de
vtable son **constantes** (`vftableinfo.hpp:27-55`: `GetDLCCount=8`,
`GetDLCDataByIndex=9`, `GetAppData=0`, `LaunchApp=2`, `IsAppDlcInstalled=9`,
`BIsDlcEnabled=11`, `GetUpdateInfo=20`, `IsCloudEnabledForApp=24`,
`GetEncryptedAppTicket=121`, `GetOfflineMode=17`, `GetAppId=19`, `InstallApp=0`,
`GetAppInstallState=4`, `SpecifyCompatTool=4`, `GetCompatToolName=7`), sin
verificación salvo `getLocalClientCompat` (slots 4 y 7 ejecutables).

**Mecánica de hook.** `DetourHook` = `LM_HookCode` + `fixPICThunkCall` (=
upstream) + attestation; `VFTHook` = `LM_VmtHook` sobre el `this` capturado.
Hooks propios de feats fuera de `hooks.cpp`: `PackagePatch`
(`CPackageInfoCache::LoadPackage`), `LibraryRemoval` (dos detours en
`steamui.so`), `ManifestBind` (tres en `CDepotDownloadMgr`), `DepotQuarantine`
(`OnChunkUnpacked`, cdecl y `regparm(3)`), `ReconcilePin`
(`EvaluateConfigChanges` `regparm(1)` + un `call rel32` parcheado en
`+0x183 ±0x40`), `Parental` (detour + byte `75→EB`), `AppInfoState` (detour
manual de 5 bytes con trampolín verificado y máquina de estados
`Applied/Retained/Restored/Catastrophic`). `patchRetn` existe sin llamadores.

**Los hooks de `hooks.cpp`** (43; "=" igual que 049bbdd, "M" del upstream de
mayo, "N" nuevo):

| Hook | Función de Steam | Tipo | Qué hace | vs upstream |
|---|---|---|---|---|
| `TraceIPC` (255-271) | traza IPC | detour | log con `ExtendedLogging` | = |
| `CAPIJob_GetPlayerStats` (273-286) | RPC legacy de stats | detour **opcional** | solo log | M |
| `CProtoBufMsgBase_InitFromPacket` (288-322) | deserialización CM clásica | detour | si el transporte clásico reclama el latch: `Achievements::recvMessage` (751/757/780), `ManifestDonor::onLicenseList/onLoggedOff`, `DepotKey::recvMsg`, `Misc::recvMsg`, `PICS::recvMsg`, `Ticket::recvMsg` | M |
| `CCMInterface_RecvPkt` (324-370) | cada paquete del CM | detour **opcional** | `CNetPacket::detectLayout`; choke de 9406 y `FamilyGroupsClient.NotifyRunningApps#1` (`FamilyShare::shouldBlock`); `CmRecv::dispatchNetPacket` (claves, tickets, PICS, wallet/email, stats, donante) | = (choke) + N |
| `CProtoBufMsgBase_Send` (374-382) | envío CM clásico | detour | `DepotKey::sendMsg` | M |
| `CSteamEngine_ProcessIPCFrame` (384-493) | despachador IPC | detour | FakeAppIds antes/después; `User`/`0xD6FC3200` → `g_currentSteamId`, `StatsPolicy::setAccount`, `hkClientUser_GetSteamId`; `ConnectPipe` → `Process_t::init` + `Ticket::connectPipe`; `ClosePipe` | = / N |
| `CSteamEngine_Init` (495-501) | | detour | captura `g_pSteamEngine` | M |
| `CSteamEngine_SetAppIdForCurrentPipe` (503-521) | | detour | FakeAppIds | = |
| `CSteamMatchmakingServers_GetServerDetails` / `_RequestInternetServerList` (523-568) | serverbrowser | detour por prólogo (upstream: índices fijos) | FakeAppIds | = |
| `CUser_CheckAppOwnership` (570-612) | **propiedad** | detour | `g_pLocalUser`; `StatsPolicy::observe`; orig.; `PackagePatch::tryReconcileLicenses()` una vez; `Apps::checkAppOwnership` o `DLC::checkAppOwnership` | = + N |
| `CUser_GetSubscribedApps` (614-633) | | detour | `Apps::getSubscribedApps` | = |
| `CUser_PostCallbackToAppId` (635-660) | | detour | FakeAppIds | = |
| `IClientAppManager_BCanRemotePlayTogether` (662-675) | | detour (upstream: VMT) | siempre true | = |
| `IClient{Apps,AppManager,RemoteStorage,UGC,Utils,User,UserStats}_RunIPCFrame` (760-792, 890-927, 951-974, 976-985, 1029-1059, 1350-1375, 1377-1386) | despachadores IPC por interfaz | detour | 1.ª vez: captura `this` y coloca los VMT hooks; `OwnerWork::drainOnOwnerFrame`; Utils latchea el hilo dueño | M/N |
| `IClientFriends_GetFriendGamePlayed` (872-888) | | detour por prólogo | FakeAppIds | = |
| `IClientUser_BLoggedOn` (1061-1071) | | detour (upstream: VMT) | `FakeOffline` | = |
| `IClientUser_BUpdateAppOwnershipTicket` (1073-1097) | | detour | `staleOnly=false` si suscrita sin ticket | = |
| `IClientUser_GetAppOwnershipTicketExtendedData` (1135-1205) | | detour | orig.; si 0 y app gestionada: ticket explícito cacheado o **derivado de la app 7** (`AppTicket`) al buffer | = + N |
| `IClientUser_IsUserSubscribedAppInTicket` (1231-1250) | | detour | 0 si `DLC::userSubscribedInTicket` | = |
| `IClientUser_RequiresLegacyCDKey` (1319-1348) | | detour | `*a2=0`, false si `Apps::shouldDisableCDKey` ("matches LumaCore's RequiresLegacyCDKey suppression") | N (upstream inyecta `CDKeys`) |
| `ISteamMatchmakingPingResponse_ServerResponded` (1388-1392) | `steamui.so` | detour por prólogo | FakeAppIds | = |
| `CWebSocketConnection_BBuildAndAsyncSendFrame` | envío al CM | detour (cuerpo en `manifestcode.cpp:554-640`) | copia del frame; `Apps::sendMsg`, `FakeAppIds::sendMsg`; captura el header autenticado para el donante; `dispatchSend`: reescribe `ClientGetUserStats`/`Player.GetUserStats#1` y observa `GetManifestRequestCode` | = + N |
| `CRemoteClientManager_RecvPkt`, `CJobMgr_BRouteMsgToJob` | recepción CM "remote client", enrutado a jobs | detour (`manifestcode.cpp:646-757`) | respuestas de códigos del donante y de stats; comprobaciones de puntero plausible | N |
| `CDepotDownloadMgr_BYldRequestDepotManifest` | petición de manifest | detour (`manifestcode.cpp:759-848`) | asegura el `.manifest` en depotcache (fetch síncrono del archivo) antes del original | N |
| VMT `IClientAppManager::{BIsDlcEnabled(11), GetUpdateInfo(20), LaunchApp(2), IsAppDlcInstalled(9)}` (677-758) | | VFT | DLC (recibe base **y** dlc), `shouldDisableUpdates`, `Ticket::launchApp` + `SteamStub::onLaunchApp`, DLC | = + N |
| VMT `IClientApps::{GetDLCCount(8), GetDLCDataByIndex(9), GetAppData(0)}` (794-870) | | VFT | `DlcData` + `makeDlcAvailable`; `GetAppData` solo log | = / N |
| VMT `IClientRemoteStorage::IsCloudEnabledForApp(24)` (929-949) | | VFT | `shouldDisableCloud` | = |
| VMT `IClientUser::GetEncryptedAppTicket(121)` (1207-1229) | | VFT | ticket cifrado cacheado | = |
| VMT `IClientUtils::{GetAppId(19), GetOfflineMode(17)}` (987-1020) | | VFT | FakeAppIds, `FakeOffline` | = |

Hooks de 049bbdd que moon **no** tiene: `CAPIJob_SendAndRecv`,
`CAppDataCache_BParseResponseFromMessage`, `CClientUnifiedServiceMethod_SendAndRecvMsg`,
`CUser_SpawnGameId`, `CUserAppManager_BuildDepotDependency` (moon lo hookea
desde feats), `IClientCompat_BIsCompatLayerEnabled`, `IClientConfigStore_SetString`,
`IClientAppManager_GetAppStateInfo`, `IClientUser_GetLegacyCDKey`,
`IClientUser_GetSteamId` (VMT; moon lo reescribe en el buffer IPC).

**Qué pasa cuando Steam actualiza.** (a) **Gate de hash inerte**: `SafeMode`
forzado a `false` (`config.cpp:463-472`: "its hash whitelist cannot be kept
current… sourced from an upstream mirror we do not control"); el feed sigue
siendo `raw.githubusercontent.com/AceSLS/SLSsteam/…/updates.yaml` (sin
espejo), con caché TTL 24 h en hilo de fondo; `verifySafeModeHash` ni calcula
el SHA si nadie lo consume ("0.22 s of blocking boot time"); con
`WarnHashMissmatch` solo avisa. (b) **Patrones**: caché local → catálogo
firmado por SHA exacto → firmas embebidas con unicidad y auto-corrección de
raíces IPC; requerido ausente → aborta limpio con popup; opcional → feature
off. (c) **Crash guard externo** (`setup.sh:537-761`): estado en
`$XDG_STATE_HOME/slsteam-moon/`; un "fallo" = `crash_*.dmp` (no `assert_*`)
en `/tmp/dumps` dentro de los 180 s siguientes a un lanzamiento con display;
latch a vanilla si fallos ≥ 3 **o al primero si `steamclient.so` (size:mtime)
cambió** desde el último arranque limpio; al latchar guarda la huella del
payload, **borra `appcache/appinfo.vdf`** de los tres roots ("the abandoned
splice… is what stalls the engine"), `notify-send "Steam recovery mode"` y
`exec` sin `LD_AUDIT`/`LD_PRELOAD`; se limpia si cambia la sesión de kernel
(`boot_id`) o el payload. (d) Diagnóstico: build-ids de todos los módulos al
log, attestation JSONL (`runtime-probes.toml` para CI), `BOOTPROF`, `AffTrace`,
`guard.log`. Sin downgrade ni auto-update del `.so`; `autofix.sh` reinstala el
último release.

**A qué procesos se limita.** `setup()` solo en `steam`; `la_symbind32` en
todo objeto del proceso; los hijos no heredan el auditor (`cleanEnvVar`). Por
juego vía pipe + `/proc` (= upstream, parseo endurecido).

**Lista de control.** Entra: `LD_AUDIT` desde un wrapper con cuatro capas de
cobertura (shim root, `.desktop`, guardian systemd, PATH). Localiza: caché
local, catálogo firmado (solo RVAs), 55 firmas con unicidad; índices de
vtable fijos. Al actualizar: sin gate de hash; aborta limpio o feature off;
crash guard en el wrapper con latch al primer crash tras cambio de cliente.
Recupera: `autofix.sh`, `uninstall`, logs y toasts localizados. Procesos:
`steam` (+ `la_symbind32`).

### 2.2 Propiedad y licencias

**Fuente de apps.** `managedAppIds` = stems numéricos de
`<Steam>/config/stplug-in/*.lua` ∪ `AdditionalApps:` de
`<config>/luaappids.yaml` (`config.cpp:103-217,537-626`); `addedAppIds` suma
las instaladas con marcador Accela y las `AdditionalApps` de `config.yaml`
**solo si están instaladas** ("legado"; el YAML dice "DO NOT ADD APPIDS HERE").
`MaxManagedApps` (4096) acota con prioridad luaappids → instaladas → activas
previas → resto; `recoverRacyScanDrops` no toma un scan corto como borrado.
El directorio `stplug-in` se crea si falta y se vigila (`100d39e`).

**Spoof de propiedad.** `Apps::checkAppOwnership` (`apps.cpp:253-321`),
post-hook de `CheckAppOwnership`: guardas = upstream; **Denuvo** con clave de
**account id de 32 bits** (moon: "keeps DenuvoGames keyed by the 32-bit
account id"; upstream SteamID64); `shouldExcludeAppId` = upstream **sin**
herencia por `parent`; **nuevo**: si `StatsPolicy::hasNativeLicense(appId)`
(Steam observó `subId > 0`) no toca nada (`:273-276`); **gate de tipo**
`ownershipOverrideAllowed` (`apps.hpp:20-39`): entra si está en `AdditionalApps`,
o si `PlayNotOwnedGames` y Steam dice que no la posee y `GetAppType` es
conocido, no es DLC y (con `AutoFilterList`) es `GAME`/`APPLICATION` ("never
DLC… fail closed until Steam's app type is available") — con el default
`PlayNotOwnedGames: no` el decensor/región **ya no se aplica** a apps no
gestionadas (upstream sí); `SubscriptionTimestamps` = upstream, y si no hay,
`purchaseTime = LibraryDates` (fecha de inclusión propia: "Package 0
contributes its license date to every appended app… breaks "recently added"");
`unlockApp` = upstream campo a campo (`familyShared` siempre false).
`getSubscribedApps` (`:323-348`) = upstream pero **sin desbordar el buffer**
("a game added (hot-reload) between the size probe and this fill —
corrupting adjacent client heap"); sigue sin deduplicar.

**`isSubscribed` y "poseído de verdad".** `CUser::isSubscribed`
(`sdk/CUser.cpp:16-25`) = upstream (trampolín + `ownsLicense && !licenseExpired`).
Pero como `PackagePatch` inyecta en package 0, el original ya devuelve
`ownsLicense=true, subId=0` para las gestionadas; moon lo reconoce
(`apps.cpp:358-365`: "Managed apps are injected into package 0 so Steam
treats them as owned — which means isSubscribed() returns true for them") y
usa `StatsPolicy` (`stats_policy.hpp`): `observe` en cada `CheckAppOwnership`
(`native` = `subId > 0`; `local` = `subId == 0 && owns && !expired`),
`hasNativeLicense`, set de apps con package nativo rellenado por
`ManifestDonor::onLicenseList`/`observePackage` desde `ClientLicenseList` y los
packages cargados, y `localEpoch` que exige `PackagePatch::isInjectedAppId`
("Package 0 may contain real free games too. Membership must have been
appended by us").

**Inyección de packages — moon.** `PackagePatch` (`packagepatch.cpp`): detour
de `CPackageInfoCache::LoadPackage(PackageInfo*, sha1, cn, ctx)` (`:736-900`);
Steam carga primero; si `PackageId == 0` y `Status == Available` inyecta en
`AppIdVec` (+0x38) los ids gestionados ∪ DLC descubiertos y en `DepotIdVec`
(+0x48) los depots de `depotkey_*.yaml` cuyo `appId` esté en el conjunto,
menos los cuarentenados, creciendo con `CUtlMemory::Grow` (`:616-731`);
packages ≠ 0 solo se observan para el donante. Dos rutas: sin snapshot del
coordinador (`injectFullSetLocked`) o con él (`applySnapshotLocked` →
`buildVectorPlan`/`compactInjected`, que quita los ids que este proceso
añadió y ya no se desean) seguido de `processRuntimeRefresh` →
`CUser::MarkLicenseAsChanged(user, 0, true)` + `ProcessPendingLicenseUpdates`
(`:526-614`, patrones **opcionales**: sin ellos "adding a game needs a Steam
restart again"). `reconcileLicensesOnce` (`:128-174`) emite **una vez**
`LicensesUpdated_t` por `CUser::notifyLicensesUpdated` (patrón
`CUser::NotifyLicensesUpdated`; "without it, injecting appids into package 0's
AppIdVec on a COLD cache leaves Steam re-requesting PICS product-info for
those apps forever ('Loading user data' hang)"), disparado también desde
`CheckAppOwnership` (`hooks.cpp:597-604`). El README acredita este diseño a
LumaCore/SFF. `AppLicensesChanged_t` **ya no se postea** (upstream sí).

**Cuándo se reaplica: `HotReload`.** `HotReload::initialize()` tras los hooks
(`hotreload.cpp:298-327`) y `HotReload::publish` en cada evento inotify
(`config.cpp:323`): `publishLocked` (`:96-288`) calcula añadidos/quitados y
huellas de contenido local, construye el `PackageSnapshot`
(`HotReloadInputs::buildFromCaches`; techos 16384/131072, fail-closed),
publica primero la autoridad de appinfo ("a newly guarded record must never
be observable with a real SHA during the gap"), entrega el snapshot al hilo
dueño (`OwnerWork::submitManagedState`) y encola `LibraryRemoval` para los
quitados. Un base **nuevo no entra en propiedad hasta que su appinfo local
está "live"** (`readyBaseIds`, `publishPreparedBase` tras provisionar y tras
`CompatLive::step`); cada rechazo se loguea con motivo. `OwnerWork`
(`ownerwork.cpp`) ejecuta el trabajo "Steam-owned" en el hilo IPC latchado
en `IClientUtils::RunIPCFrame` (drenado en cada `RunIPCFrame`) o inline tras
30 s de staleness (`SLSSTEAM_OWNER_QUEUE_MAX_STALE_MS`).

**PICS.** `AppTokens` = upstream (`apps.cpp:607-615`). **Moon elimina de la
petición saliente** `ClientPICSProductInfoRequest` las apps "localmente
autoritativas" no instaladas del todo (`SynthMark::stripIndices`,
`permitSynthStrip`, `:580-595`; "Their account access token may be denied
even when our anonymous CM fetch returned complete metadata; the resulting
empty refresh clobbers the startup splice") y **nunca añade** `AdditionalApps`
a ella ("This mirrors the upstream LumaCore design: ownership is established
purely by the package-0 AppIdVec injection"). En recepción, `PICS::recvChangesSinceResponse`
borra del `ClientPICSChangesSinceResponse` (8902) las apps autoritativas y
programa un refresco propio (`pics.cpp:767-845`). `AppInfoState` marca el
byte "skip" del `CAppData` en cada `GetOrAddAppData` de una app gestionada sin
SHA (`appinfostate.hpp:507-550`).

**Juegos que la cuenta posee de verdad.** Protegidos por `hasNativeLicense`
en `checkAppOwnership`, `shouldUnlockDlc`, `DepotKey::manifestInManagedScope`
(claves solo observadas, `managed=false`, no entran en el circuito propio) y
`DepotQuarantine`; `shouldDisableCloud` solo consulta a Steam con
`PlayNotOwnedGames` ("a false "not owned" there silently turns cloud saves off
for a genuinely owned game"). Huecos: `DepotIdVec` recibe cualquier
`depotkey_*.yaml` cuyo `appId` sea gestionado sin mirar `managed`
(`packagepatch.cpp:195-242`); un DLC comprado de un base gestionado se
desbloquea igual si Steam no lo observó con `subId > 0` (§2.3).

**Family sharing.** `DisableFamilyShareLock` = upstream en regla
(`FamilyShare::shouldBlock`: 9406 y `NotifyRunningApps#1`) y `owner_id = 1`,
pero el hook `CCMInterface::RecvPkt` es **opcional** (si su patrón no
resuelve: "Family Share receive hook unavailable; message filtering
disabled"); historia: choke de CM puesto (`50a5959`, 08-30), **revertido**
(`52c03f5`, 09-01: "the unverified CM packet hook… gates client
initialization") y vuelto a poner como opcional (`2668620`, 09-02). **No hay**
`GetAppStateInfo` (upstream oculta el flag `Borrowed` a los juegos).

**Depots compartidos (228980).** Nada específico; `DepotIdVec` solo recibe
depots del catálogo de claves; `sharedDepots` no se tocan (el plan lo filtra
`ManifestBind`). Steam Linux Runtime (1070560/1391110) queda fuera del scope
por ser clave "solo observada" (`test_depotkey_scope`).

**Lista de control.** Spoof: post-hook de `CheckAppOwnership` con gate de tipo
y `hasNativeLicense`; `GetSubscribedApps`. Packages: **sí**, package 0 en
memoria (`LoadPackage` + `Grow`) con reconcile `LicensesUpdated_t` una vez y
`MarkLicenseAsChanged`/`ProcessPendingLicenseUpdates` en caliente, en el hilo
dueño. Poseídos de verdad: `hasNativeLicense` (no `isSubscribed`). Family
sharing: choke opcional + `owner_id`; sin `GetAppStateInfo`. Redists: nada.

### 2.3 DLC

**Criterio: `DLC::shouldUnlockDlc`** (`dlc.cpp:12-43`): (a) pipe de un juego
(`g_pClientUtils->getAppId() != 0`; gate **restaurado** en `f50f28e`: "The
runtime package refresh already surfaces DLC for games added while Steam
runs; answering on the client pipe only duplicated it and logged every managed
DLC at login"); (b) **moon**: `Apps::isAddedAppDlcId(appId)`: el DLC está en el
**scope gestionado** = `discovered_` (ids extraídos del appinfo de bases
gestionados: `extended.listofdlc` y `depots.<id>.dlcappid`, `dlcids.hpp`;
publicados en arranque, tras cada publicación de caché y tras cada
provisión completa) ∪ `configured_` (`DlcData` solo bajo padre gestionado,
vaciado en cada recarga); (c) no excluido; (d) **moon**: `!hasNativeLicense`.
Comentario del autor: "The upstream DLC hook applied the global AppIds
blacklist/whitelist to every DLC queried while any game was active… made DLC
from ordinary, genuinely-owned games eligible… Scope the override to DLC ids
discovered from LuaTools-managed base apps… This deliberately does not depend
on ownership of the base app: adding an already-owned game through LuaTools
must still enable its managed DLC set."

**Qué contesta.** `CheckAppOwnership` del DLC → `unlockApp` (= upstream);
`IsAppDlcInstalled` → `shouldUnlockDlc(dlcId)` (=); `BIsDlcEnabled(base, dlc)`
→ sobre el **DLC** (upstream: sobre el padre); `IsUserSubscribedAppInTicket`
→ 0 (=). Además moon **inyecta los DLC en `AppIdVec` de package 0** ("Steam's
depot-install planner only schedules a depot tagged with `dlcappid` when that
DLC's appid is present in package 0's AppIdVec… DLC appids were never
injected and their depots were filtered out", `dlcids.hpp:5-10`): un DLC
anunciado solo entra si el base tiene `hasdepotsindlc`, o hay un manifest
válido para su depot, o `InjectAllAdvertisedDlc: yes`. Escribe **metadatos de
DLC** hijos (`dlcmetadata_<base>.yaml`, validados como `type=dlc` hijo del
base) en `appinfo.vdf` solo si no existen ("Never downgrade an entry supplied
by Steam").

**DLC comprado de verdad.** Respetado si `hasNativeLicense` (observado
`subId > 0` **y** en `AdditionalApps`, o en el set de packages nativos). Si
ninguna de las dos, un DLC comprado de un base gestionado recibe `unlockApp`.

**Sin depot / límites / `DlcData`.** Flag como upstream; `DlcData` solo para
`managedAppIds` (`:77-80,93-97`); `available` = `isAddedAppDlcId &&
!shouldExcludeAppId` (upstream: true fijo); `makeDlcAvailable` solo para DLC
del scope. Sin tope 64 en código (= upstream). **Cuarentena** (moon,
`depotquarantine.cpp`): 3 chunks distintos con `Unpack failed` en un depot
gestionado con `DlcAppId != 0` → fuera del plan y de package 0, persistido en
`cache/dlcquarantine.txt` con FNV de la clave; cambiar la clave lo libera.
`pruneUnsupportedDepots` quita de `listofdlc` los DLC cuyos depots no tienen
clave ("Content still encrypted" deja de inyectarse como owned).

**Cómo se quita.** Borrar el `.lua` del base → `HotReload` → snapshot sin el
base → package 0 resincronizado y `LibraryRemoval`; `discovered_` se recalcula
en el siguiente `setDiscoveredAppDlcIds` completo.

**Lista de control.** Criterio: DLC del scope de bases gestionados + pipe de
juego + sin licencia nativa. Comprado: solo si Steam lo observó con package.
Sin depot: flag + inyección condicionada en package 0. Límites: ninguno
propio; cuarentena por clave que no descifra. Quitar: por el base.

### 2.4 Claves y manifests

**Claves de depot — moon** (`depotkey.cpp`; upstream no tiene). Dos fuentes:
(1) `<Steam>/config/stplug-in/<appid>.lua`, regex
`addappid(<depot>, <n>, "<64 hex>")` quitando comentarios `--`, solo para
scripts cuyo stem esté en `managedAppIds`, guardadas con `managed=true`
(`:306-384`); (2) respuestas legítimas `ClientGetDepotDecryptionKeyResponse`
(5439) con `eresult OK` → `managed=false` (`:549-554`). Guardadas en
`<config>/cache/depotkey_<depot>.yaml` (`appId`, `depotId`, `key` base64,
`managed`; escritura **no atómica**; `managed` pegajoso). Importación en
preinit, en `load()` (`onStartup`, que además copia `config/depotcache/*.manifest`
→ `depotcache/`) y en cada evento del watcher. **Sustitución en caliente**
(`recvDepotKey`, `:510-590`, por ambos transportes): si Steam responde error y
hay clave → `eresult=OK` + clave; si no hay clave pero el `depot_id` es un
AdditionalApp → **32 ceros** (`SynthZero`); si OK → cachea como observada. La
correlación depot→app sale de la petición saliente (`sendDepotKey`). Depots
sin clave se **podan del appinfo provisionado** (`pruneUnsupportedDepots`,
`appinfo_provision.cpp:450-639`), y si no queda depot Linux la app se marca
para Proton. `DisableShaderCache=1` se escribe en `config.vdf` siempre que
haya apps añadidas. Nada borra claves salvo que el usuario lo haga.

**Manifests — moon.** **Fuente única por red**: `https://manifest.luastools.xyz/m/<depotId>/<gid>`
(`ManifestFetch.cpp:497-498`; "it is the only source. When it does not hold
this gid yet there is nowhere else to fetch it"), bytes de manifest crudos
validados por magic; no hay descarga del CDN con request code
(`contentserverdirectory.cpp` **sin llamadores**; `manifest_zip.cpp` idem).
Orden en el plan (`ManifestBind::redirectGid`, `manifestbind.cpp:472-650`,
para depots "en scope"): pin (asegura el manifest pinado pero devuelve el gid
original: "the pin is TARGET-ONLY") → gid congelado del plan → gid público ya
en `depotcache/` → **store** `~/.config/SLSsteam/manifests/<depot>_<gid>.manifest`
(persistente, "survives Steam's depotcache purges"; usa `$HOME` literal, no
XDG) → archivo luastools con el resto del presupuesto de 12 s del plan →
fallback local (`.preferred_<depot>`, más reciente por mtime, alternativo en
depotcache), escribiendo `<config>/offline_<app>` si instala un gid distinto.
`hkBuildDepot` prelanza los manifests del plan real (8 hilos, cola 64, 60 s
por job, caché de 404 de 10 min). `BYldRequestDepotManifest` hace el fetch
síncrono antes de que Steam pida el request code. `ProviderCircuit`: 2 fallos
de transporte o un 429 abren el circuito 30 s, 404 cuenta como alcanzable;
abierto → `<config>/offline` y **updates suprimidas**. **Síntesis**
(`synthesizeDepotsFromStore`, `:659-734`): para apps cuyo product-info anónimo
llega sin `depots` (token denegado; ejemplo del autor: Risk of Rain 2 632360),
reconstruye `depots` con los depots gestionados y su mejor gid archivado,
`oslist` por nombres de fichero del manifest, tamaños, `installdir` desde
`common.name` y `launch` por SO prefiriendo `steamshim`; marca el par como
sintético (autoritativo, fuera del PICS saliente). Modos legacy:
`SLSSTEAM_LEGACY_MANIFEST_STAGING=1` (staging síncrono en PICS + `Prewarm`),
`SLSSTEAM_MANIFEST_FALLBACK=0`.

**Códigos de petición de manifest — moon.** Para instalar, **ya no se
generan ni se inyectan** (`c1b5e15`, 09-21): `handleSend_GetManifestRequestCode`
solo observa la petición y lanza el staging ("We no longer answer that
handshake ourselves (the archive is the manifest source)"); `hkBRouteMsgToJob`
deja pasar la respuesta. Los comentarios con `gmrc.wudrm.com`/`manifest.steam.run`
(`manifestbind.hpp:13`, `pics.cpp:584`) son restos: **ningún host de códigos
existe en el código**. **Donante** (`manifestdonor.cpp`; `Donate.Enabled`
**`yes` por defecto**, `Url` `https://manifest.luastools.xyz`): con la
**sesión real del usuario** (header `steamid` + `client_sessionid` capturado
del primer `ServiceMethodCallFromClient`, `manifestcode.cpp:585-619`) pide
`ContentServerDirectory.GetManifestRequestCode#1` por el WebSocket de Steam
para depots que la cuenta **posee** (licencias → packages → depots) y que el
archivo lista en `GET /manifestwanted` (`app:depot:gid`), comprueba
`HEAD /m/<depot>/<gid>` y sube `POST /manifestcode/submit` (`depot:gid:code`);
cuotas `IntervalSecs 30`, `WantedRefreshSecs 300`, `MaxMintsPerCycle 25`,
`MinMintIntervalMs 2000`, `MaxMintsPerSession 0`; además captura
**pasivamente** los códigos que Steam pide por sí mismo y los sube si están en
la lista. Portado de `madoiscool/BetterSteamTools@4a97d9d`. Si el archivo
cae, el donante solo loguea.

**Pins** (sustituyen a `ManifestIds` de upstream): dos catálogos
**desconectados**: `ManifestPins: {app: {locked, build_id, depots: {depot: "gid"}}}`
en `config.yaml` (`config.cpp:749-819`; `flattenDepots` descarta ids a cero,
`purgeOrphans` quita apps no activas) y `setManifestid(d,"gid")` de los `.lua`
→ `manifestid_<depot>.yaml` (`manifestid.cpp:251-334`). **Solo el primero se
aplica como gid**; el segundo se usa únicamente como booleano `depotHasPin`
de scope (`manifestcode.cpp:263,765`). Aplicación: `hkBuildDepot` reescribe
`ManifestGid`+`ManifestSize` como par (`resolvePinnedPair`), `ReconcilePin`
hace lo mismo en el vector TARGET del `EvaluateConfigChanges`; **nunca** en el
appinfo provisionado ni en el buffer PICS vivo ("Steam validates the
product-info buffer against the SHA-1… 'Corrupt data in text buffer'").
`build_id` se parsea y **nadie lo consume**.

**Qué deja de funcionar si cae cada servicio.** Archivo luastools: no hay
manifests nuevos (solo store/depotcache), donación parada, updates
suprimidas. CM anónimo + `api.steamcmd.net`: sin appinfo para apps nuevas
(`UserMsg::GameMetadataUnavailable`). `steam-monitor`: firmas embebidas.
`AceSLS` raw: nada (inerte). `unzip`/`gzip`/`timeout`: `RuntimeDependencyMissing`.

**Lista de control.** Claves: del `.lua` y de respuestas observadas, en
`depotkey_<d>.yaml`, sustituidas en la respuesta CM. Manifests: store propio
+ depotcache + archivo luastools (único); síntesis para token-locked. Códigos:
ninguno para instalar; donación con la cuenta real (opt-out). Servicios:
luastools, CM/steamcmd, steam-monitor.

### 2.5 Updates de juegos

**Decisión: `Apps::shouldDisableUpdates`** (`apps.cpp:422-484`; upstream:
`DisableUpdates && (added || !isSubscribed)`; moon **no lee** `DisableUpdates`):
app no gestionada → `!isSubscribed`; gestionada no `FULLY_INSTALLED` →
**permitir** ("Returning false from GetUpdateInfo unconditionally made Steam
think a not-yet-installed AddedApp had "nothing to download", so the install
hung in "Reconfiguring""); gestionada instalada y no `locked` → suprimir si
`ManifestFetch::areProvidersOffline()` o `AutoUpdateApps: no` ("mirrors
LuaTools' toggle"; default `yes`); `locked` → suprimir **solo** cuando los
depots pinados instalados ya están en su gid (`appAtPinnedGids` parsea
`appmanifest_*.acf` de todas las bibliotecas en cada llamada: "A locked,
pinned app must let Steam run the update ONCE to install the pinned build,
then freeze… the scheduled/unscheduled "Update Required" loop").

**Pins / congelación / build antiguo.** `ManifestPins` (§2.4) + `locked`;
`ReconcilePin` (`reconcilepin.cpp`) parchea el lado TARGET de
`EvaluateConfigChanges` (`flags & 0x8`: elimina depots gestionados de tamaño
0 y reescribe gid+size a los pins) y redirige el `call` del constructor del
vector local en `+0x183` ("the ONE downgrade"; `SLSSTEAM_RECONCILE_PIN=0` lo
apaga); el pin de build en appinfo se **retiró** (`b1da6b6`, 07-10: "made
metadata lag the live build, stalled the update job, hung the client at
startup, and contaminated the download planner"). Primer import pinado
aplicado en el plan inicial (PRs #3/#5 de FATEx0); downgrade preservando el
manifest instalado (`50e1752`). Los refrescos de Steam para apps
autoritativas se bloquean (§2.2), así que la "update" que ve Steam depende
del product-info que moon reprovisiona por CM anónimo al ver un
`change_number` nuevo y splicea en caliente (`publishRuntimeAppInfo` +
`ThreadedReadFromDisk`).

**Update ya empezada.** Sin cancelación; `hkBuildDepot` recorta el plan en
cada construcción; la cuarentena omite el depot en el **siguiente** plan;
`isAnyManagedDownloadActive` lee `StateFlags` del `.acf`.

**Lista de control.** Pins: `ManifestPins` (YAML) con `locked`, aplicados en
plan y reconcile. Automática: `AutoUpdateApps` global + proveedores offline.
Build antiguo: pin + "one downgrade". En curso: nada.

### 2.6 Fixes y DRM

**Detección** = upstream (`process.cpp`), salvo que el barrido de `map_files`
solo corre con `SmartTickets & 0x2` ("trips Steam's >15s BMainLoop watchdog
and fatally exits the client", `:751-784`).

**Tickets.** Caché YAML = upstream + `try/catch` ("a truncated or corrupt
ticket_<appid>.yaml… Uncaught on this IPC path the throw aborts the whole
client"), mutex y tombstones por app quitada. **Moon**: `recvAppTicket`
**estampa `eresult=OK`** en la respuesta 858 para apps y DLC gestionados
("Steam's downloader treats that as a hard fault: it won't even progress to
GetManifestRequestCode… We intentionally do NOT touch the `ticket` string
field — … rewriting it has corrupted the heap"). **Ticket sintético**
(`appticket.hpp`, `hooks.cpp:1135-1205`): si `GetAppOwnershipTicketExtendedData`
devuelve 0 para una app gestionada, sirve el `ticket_<app>.yaml` si su
`appId@16` coincide, o **deriva** uno del ticket de la **app 7** (obtenido
del propio cliente o de `ticket_7.yaml`): copia hasta la firma, inserta los 4
bytes del AppId y la firma de 128 bytes, manteniendo el `totalSize` original
("The client-side parser consumes one field beyond the reported logical
boundary"). Es el "Exploit from OpenSteam001" que upstream dejó en su rama
`update`. Encrypted tickets = upstream, pero `getEncryptedAppTicket` **perdió
el gate del bit Denuvo** (programa el spoof de una vez siempre que haya
ticket cifrado cacheado); `GetSteamID` mantiene la cascada de upstream
(`SteamIdOverride` → spoof de una vez → cifrado solo con Denuvo detectado),
reescribiendo el buffer IPC `0xD6FC3200` en vez de un VMT hook.

**Steamless — moon** (`steamstub.cpp`): `SteamStub::setup(root)` busca
`tools/steamstub-bypass/run-steamless.sh` y `tools/steamless-bin/Steamless.CLI.exe`
(commiteados, CC BY-NC-ND) bajo la instalación; sin ellos "DRM removal
DISABLED — games with Steam DRM will fail with "Application load error 6"".
`onLaunchApp` (pre-hook de `LaunchApp`, solo `isAddedAppId`): **opt-in por
app** con el token `-steamless`/`--steamless` en `LaunchOptions` de
`localconfig.vdf` (desde `d3a424e`, 09-20; antes automático); sin opt-in,
**restaura** `<exe>.original.exe` → `<exe>` ("An unmodified on-disk exe is what
SteamStub-verifying titles hash, and the stub itself runs under Proton once
ownership is satisfied"); con opt-in recorre `.exe` hasta profundidad 3,
detecta SteamStub (`VLV\0`@0x40 o sección `.bind`), espera al prewarm del
prefijo Wine propio (`~/.local/share/SLSsteam/steamless-prefix`, Wine/Proton
del propio Steam) y ejecuta `bash run-steamless.sh <exe>` **síncrono dentro
de `LaunchApp`** con timeout 180 s; `SIGCHLD=SIG_IGN` en Steam obliga a
juzgar el éxito por la desaparición del stub; el helper hace
`pkill -9 -f wineserver`. Fallo → `DrmRemovalFailed`. `scan-all.sh` offline.

**Denuvo / cuenta.** `DenuvoGames` (account id de 32 bits) salta el spoof;
`SmartTickets` y `SteamIdOverride` = upstream (cherry-pick). Sin fuentes de
tokens. **Goldberg / EOS**: no.

**FakeAppIds** = upstream función por función (null-checks añadidos).
**Proton**: `requiresProtonMapping` (`pending_proton.hpp`: Proton si el
conjunto efectivo de SO no contiene `linux`; "publishers frequently mis-tag a
Windows-only title's content depot with "windows,linux""), `CompatToolMapping`
en `config.vdf` con la herramienta por defecto del usuario o
`proton_experimental` (solo en preinit; si no, `cache/proton-mappings.pending`)
y `CompatLive::step` en caliente (`specifyCompatTool(app, tool, "", 250)` desde
el hilo dueño, hasta 20 intentos; "the difference between the title appearing
in ~1-2s and it staying at "0 B" until the next launch"). `neutralizeLegacyCdKey`
pone `extended.hadthirdpartycdkey="0"` en el appinfo provisionado y
`RequiresLegacyCDKey` devuelve false para base + DLC del scope ("a single
owned DLC whose appinfo still carries hadthirdpartycdkey (e.g. Far Cry 4's
season-pass DLC…) re-arms GettingLegacyKey"). **Sin `LaunchOptions`** de
upstream (no hookea `SpawnGameId`).

**Cómo se aplican y quitan.** Config, caché de tickets, opción de
lanzamiento `--steamless`; `restoreOriginals` al lanzar sin opt-in; no hay
helper de restore manual.

**Lista de control.** Denuvo: detección + tickets (incl. derivado de la app 7)
+ `DenuvoGames`. Steamless: integrado, opt-in por juego, síncrono en
`LaunchApp`. Goldberg/EOS: no. Proton: automático por SO con la herramienta
del usuario.

### 2.7 Logros, stats y tiempo de juego

**Dónde se guardan.** En Steam (= upstream). En memoria: `pending` (jobid →
app/cuenta/epoch, máx 4096) y `StatsPolicy::Store`.

**Quién contesta `GetUserStats` — moon, diseño nuevo.** Sin los hooks de RPC
de upstream, sin reseñadores, sin `MaxSchemaTries` ni cooldown. Se reescriben
**bytes** en el borde del WebSocket (`manifestcode.cpp:208-224,295-303,385-407`
→ `achievements.cpp`): saliente `Player.GetUserStats#1` (151) y legacy
`ClientGetUserStats` (818) → `rewriteRequest` (`:21-85`): si `Achievements: yes`,
tamaños acotados, `jobid_source` válido, `header.steamid` = yo, petición bien
formada, `steamid` objetivo = yo o 0, **`StatsPolicy::localEpoch(app, cuenta,
refresh=true) != 0`** (app en `AdditionalApps`, no excluida, **inyectada por
moon** en package 0 y observada `subId==0 && owns`) y owner ≠ yo, construye una
petición nueva con `steamid = owner` y sin `sha_schema`/`crc_stats` ("forces
the server to return a full, fresh schema"; protobuf editado a mano en
`playerstats.hpp` porque "The player proto isn't compiled into the fork") y
registra el job (jobs sin respuesta se retienen como tombstones: "Expiring by
time would allow a late owner reply through with its achievement progress
intact"). **Owner** = `AchievementOwners[app]` o `AchievementOwnerId`, default
hardcodeado **`76561198028121353`** (`config.cpp:507`; YAML: "Only the public
schema is read; the account is never signed in to"). Entrante (819 o
`ServiceMethodResponse`) → `rewriteResponse` (`:87-145`): correlación por
`jobid_target` ("never app ID alone"), re-valida `localEpoch`, y si algo falla
devuelve **fallo** con `eresult=2` ("A rewritten request must NEVER leak
another user's progress"); si OK copia todo **menos `crc_stats` y `stats`**
(modern) o limpia `stats/achievement_blocks/crc_stats` (legacy). Invalidación
en 751/757/780. `localEpoch(refresh=true)` llama al `CheckAppOwnership`
original desde el hilo del WebSocket, contra el propio comentario de
`stats_policy.hpp:142-143`. ABI C `slsteam_local_stats_epoch_v1` exportada y
re-enlazada por `la_symbind32` para otros módulos ("Share eligibility with
CloudRedirect", `ce71798`).

**Esquemas.** Llegan por el canal normal de Steam en nombre del owner
configurado; si esa cuenta no posee el juego o no es pública, `eresult=2`,
sin reintento ni blacklist.

**Tiempo de juego.** Desaparece `sendAndRecvLastPlayedTimes` (upstream borra
el playtime de servidor de `AdditionalApps`); nada en moon. `GamesPlayed` =
upstream. Sincronización: ninguna (cloudredirect-moon).

**Lista de control.** Almacén: Steam. Sincronización: no. `GetUserStats`:
moon para apps que inyectó, con un owner fijo. Esquemas: del owner.

### 2.8 Cloud saves

`DisableCloud` default **`no`** en el YAML (código `true` si falta la clave)
porque "CloudRedirect (shipped with this stack) can sync saves"
(`config.yaml:116-118`). Decisión pura `Apps::cloudDisableDecision`
(`clouddecision.hpp:24-41`): `!enabled` → no; **gestionada → siempre sin
cloud** ("Valve's cloud backend validates ownership server-side and rejects
the upload with "Access Denied""); no gestionada → solo con
`PlayNotOwnedGames` se consulta `isSubscribed` ("That answer flips to "not
owned" for genuinely owned apps while the client rebuilds its license set").
Upstream: `DisableCloud && !isSubscribed` para todo. CloudRedirect va por
`LD_PRELOAD` desde el wrapper (build "2.1.5 with the steamclient.so wait
extended 10s → 120s"); cloudredirect-moon arrastra el bug del store vacío
que nosotros parcheábamos (`cr_stats_fix`), según el doc anterior.

**Lista de control.** Redirección: no (CloudRedirect externo). Convivencia:
cloud off para gestionadas aunque `DisableCloud: no`.

### 2.9 Añadir y quitar un juego

**Entradas.** `<Steam>/config/stplug-in/<appid>.lua` (el **nombre** da el
appId; el cuerpo da claves y `setManifestid`; lo escribe luatools-moon/Lumen
"while Steam runs"), `<config>/luaappids.yaml` (`AdditionalApps:`),
`AdditionalApps` de `config.yaml` (legado, solo instaladas) y `install|<app>|<lib>`
en `$XDG_RUNTIME_DIR/SLSsteam/api` (→ `IClientAppManager::installApp` vía
`OwnerWork`). Sin nombre, tienda ni zip en `src/` (el zip lo extrae el plugin
externo). Hot add por inotify en 3 rutas con drenado de ráfagas ("199 config
reloads in one directory copy") y re-arme tras rename.

**Qué escribe al añadir.** `cache/depotkey_<d>.yaml`, `cache/manifestid_<d>.yaml`,
`cache/picsbuffer_<app>.{bin,yaml}` (+ `synthetic_<app>`, `terminal_<app>.state`,
`dlcmetadata_<app>.yaml`), entrada en `appcache/appinfo.vdf` (+
`.slssteam-previous`, lock `.slssteam.lock`, cuarentena `.slssteam-corrupt.*`),
`config/config.vdf` (`CompatToolMapping`, `DisableShaderCache`) o
`cache/proton-mappings.pending`, `depotcache/*.manifest` y
`~/.config/SLSsteam/manifests/*`, `cache/cmlist.txt`, `library-added-times.txt`;
borra `userdata/**/<app>_pbuf`; en memoria package 0. Flujo en frío
(`main.cpp:318-417`): `importLuaScripts` → `provisionColdStartApps` →
`injectAllCached` → `injectValidatedMetadataApps`; en caliente: watcher →
`loadSettings` → `reloadLuaScripts` → `HotReload` → `refreshInBackground` →
`provisionRequestedApps` → `publishRuntimeAppInfo` (`injectCachedApps` +
`ThreadedReadFromDisk`) → `publishPreparedBase` / `submitCompatReadiness` →
package 0 + mark/process de licencias. Steam escribe después el `.acf`;
`installdir` solo se sintetiza para token-locked.

**Qué limpia al quitar** (`forgetAppImpl`, `appinfo_provision.cpp:4102-4187`):
**renombra** (nunca borra) `picsbuffer_*`, `terminal_*`, `dlcmetadata_*`,
`synthetic_*`, `ticket_*`, `encryptedTicket_*` y los `manifestid_*` de depots
exclusivos a `*.forgotten.<ts>.<pid>`; tombstone de tickets en memoria;
`ManifestPins::purgeOrphans`; `LibraryRemoval::queue` (§2.2: un trabajo por
`RunFrame` de `steamui.so`, `ownershipFlags = 0` + `MarkAppChange(…, 0x0002)`;
`restore` si se re-añade). **Deja**: `depotkey_*.yaml`, manifests del store y
de depotcache (`purgeDepots` sin llamadores), `dlcquarantine.txt`, la entrada
en `appinfo.vdf`, el `CompatToolMapping`, el contenido instalado y el `.acf`;
"a removed app loses its inclusion date". `restoreQuarantined` sin llamadores:
la restauración es manual. En arranque, los pares de apps no activas se
cuarentenan como `*.orphaned.*`.

**Varias bibliotecas y SD.** `libraryfolders.vdf` (ambas ubicaciones) para
detectar instaladas, leer `.acf` y Steamless; `install|app|lib` por índice;
sin `dumplibraries`. Carpeta: Steam (`installdir`), salvo síntesis.

**Lista de control.** Entradas: `.lua` por nombre, `luaappids.yaml`, API.
Escribe: appinfo.vdf, pares de caché, claves, manifests, `config.vdf`.
Limpia: cuarentena por renombrado + retirada visual; deja claves, manifests,
appinfo, contenido. Bibliotecas: por `libraryfolders.vdf` e índice.

### 2.10 Credenciales y proveedores

Sin cuenta ni clave externa: CM anónimo (`kAnonSteamId`, `protocol_version
65580`, `client_package_version 1561159470` fijos), archivo luastools sin
auth, `api.steamcmd.net` sin auth, catálogos de GitHub/jsDelivr, owner de
logros público. **Lo que sí usa la cuenta del usuario**: el donante (§2.4)
con la sesión autenticada real, por defecto. `ticket-grabber` existe pero no
se compila ni distribuye. Caducidad: TTL del par de appinfo 5 min
(`SLSSTEAM_PROVISION_TTL`), `change_number` observado, terminales ligados a
change number + fingerprint, cmlist 24 h, catálogos por SHA del módulo, miss
del archivo 10 min, circuito 30 s; tickets sin comprobar edad (= upstream).
Sin red: pares validados como `FallbackCache`, manifests del store, updates
suprimidas, spoof/DLC/tickets locales.

**Lista de control.** Exige: sesión de Steam. Opcionales: `AppTokens`,
SteamIDs, `AchievementOwners`. Usa sin pedir: la sesión real para donar.
Caducidad: TTLs y circuitos. Sin ninguna: unlock sí; instalar nuevo no.

### 2.11 Mantenimiento propio

**Superficie.** (1) `config.yaml` + `luaappids.yaml` + `stplug-in` con hot
reload; parseo **sin excepciones** (`as<T>(defVal)`: con `-O3 -flto` el `throw`
caía en `.cold` y "even catch (...) is bypassed", "bricked Steam on every
launch" con un `FakeWalletBalance` enorme → de ahí `-fno-reorder-blocks-and-partition`)
y **reparación** de la indentación de listas rotas por el escritor de
LuaTools, reescrita atómicamente (`ConfigRepaired`); claves faltantes solo al
log, ya no popup. (2) **API**: `$XDG_RUNTIME_DIR/SLSsteam/api` (0600 en dir
0700, revalidado en cada reapertura), un solo comando `install|app|lib`;
sustituye a `/tmp/SLSsteam.API` ("turned a lock file into an injection kill
switch"). (3) **Sin plugins Lua**. (4) Popups solo por `CLog::notifyUser` con
catálogo EN/PT de 20 mensajes (`LoadSuccess`, `SteamVersionUnsupported`,
`InitializationFailed`, `ContentServersUnavailable`, `DownloadTimedOut`,
`ManifestNotReady`, `GamePreparationFailed`, `DrmRemovalFailed`,
`LocalStorageError`, `RuntimeDependencyMissing`, `ConfigRepaired`,
`GameMetadataUnavailable`…), throttle 60 s por categoría, escape de shell, y
cola JSON `~/.local/share/Lumen/notifications/*.json` para la UI de gamepad.
(5) **CEF**: reescribe `--remote-debugging-port=8080` del webhelper a un
puerto libre y publica `~/.local/share/Lumen/cef_port` (`<port>\nowner <pid>
<startTicks>`); **si existe `~/homebrew/services/PluginLoader` (Decky) se
mantiene 8080** y se borra el contrato ("Decky's injector is hard-coded to
localhost:8080… multiple CDP clients coexist on one CEF target"). (6) Proton
en caliente (`CompatLive`). (7) Fuera del `.so`: wrapper con guard,
`autofix.sh`, guardian systemd, `ensure-desktop-coverage.sh`.

**Update de sí mismo.** No en el `.so`. `autofix.sh`: `api.github.com/repos/swwayps/slsteam-moon/releases`
→ primer asset `slsteam-moon-linux-*-lumen.zip` (no verificado) o espejo
`cdn.jsdelivr.net/gh/swwayps/jsdelivr@main/manifest.json` (URL fijada a
commit + sha256 verificado), `setup.sh install`, relanza. El asset `-lumen`
no lo produce `package.sh` (que genera `slsteam-moon-linux-<ver>.zip` +
sidecar `.sha256`, sin firma); lo construye luatools-moon. **Catálogos de
patrones sí se actualizan solos** (`pattern-refresh` en cada lanzamiento).
Versión instalada: `VERSION` fija `20260528151547` + build-ids en el log;
huella `size:mtime` del payload en el guard.

**Salud / logs.** `~/.SLSsteam.log` en append (`[Nivel]` sin fichero:línea;
`LogLevel` umbral, Debug compilado en release), `~/.SLSsteam.afftrace.log`
opt-in, attestation JSONL opt-in, `$XDG_STATE_HOME/slsteam-moon/{guard.log,guardian.log,coverage.policy,…}`,
`coverage-policy.effective`, `<config>/offline`.

**Lista de control.** Superficie: YAML autorreparado, API privada de un
comando, toasts localizados + cola JSON, CEF para Lumen; sin QAM propio
(Lumen). Update propio: `autofix.sh`/luatools-moon. Componentes: n/a.
Versión: fija + build-ids. Salud: logs, guard, attestation.

### 2.12 Proyecto

**Plataformas.** Linux x86 32 bits en `steam`; helper x86_64; glibc ≥ 2.34;
sin Flatpak; Deck solo por PATH/shim; Decky coexiste (CEF 8080).

**Código abierto, licencia, quién, ritmo.** AGPL-3.0-only; Steamless
CC BY-NC-ND redistribuido; un mantenedor (`unplausible`), ~3,1 commits/día,
5 tags lightweight, `version.txt` congelado, el zip "2.9" **resubido cuatro
veces** bajo el mismo tag (§5.2 F11); historia desde el 2026-05-31
(`84450ef`, "chore: import upstream tree"); 77 cherry-picks de AceSLS con fecha original; README de 33
líneas que remite a luatools-moon; issues #6 ("Upstreaming the moon
branches") cerrada sin respuesta, #7 ("User controllable coverage") abierta.

**Servicios de terceros.** `manifest.luastools.xyz` (manifests y donación),
CMs de Valve (`api.steampowered.com/ISteamDirectory/GetCMListForConnect`,
`wss://<cm>/cmsocket/`), `api.steamcmd.net`, `raw.githubusercontent.com/swwayps/steam-monitor`
+ jsDelivr (catálogos), `raw.githubusercontent.com/AceSLS` (inerte),
`api.github.com` + `cdn.jsdelivr.net/gh/swwayps/jsdelivr` (autofix),
`api.github.com/repos/atom0s/Steamless` (solo sin bundle); libcurl, zlib,
libcrypto (`SHA1` por `dlsym`; si falta, SHA = 0 y nada valida) y libtier0
por `dlopen`; `gzip` por `popen`, `unzip`, `timeout`, `notify-send`, Wine.

**Telemetría y privacidad.** Sin telemetría explícita. Sale: GET anónimos al
archivo con depot+gid (User-Agents propios `SLSsteam-ManifestFetch/0.1`,
`SLSsteam-CmClient/0.1`, `SLSsteam-AppInfoProvision/0.1`); **POST de códigos
de petición de depots poseídos con la identidad del usuario** (donación por
defecto); peticiones `GetUserStats` al CM con el SteamID de otro usuario;
sesión CM anónima extra desde la IP del usuario; catálogos nombrados por el
SHA del `steamclient.so` (revela el build a GitHub). AffTrace y log evitan
SteamIDs.

**Riesgo de detección.** Mayor que upstream: además de detours y vtables,
**modifica `appcache/appinfo.vdf` y `config.vdf` en disco**, inyecta en
package 0 y dispara `LicensesUpdated_t`, reescribe paquetes CM salientes
(PICS sin apps autoritativas, stats, códigos del donante) y entrantes (claves
de depot, tickets `eresult=OK`, changelist), sirve tickets cuyo AppId no está
firmado, parchea un byte del check parental, reescribe argv del webhelper.
Sin ofuscación. El guard reduce el riesgo de brick, no el de detección.

**Lista de control.** Plataformas: Linux 32-bit, sin Flatpak. Abierto: AGPL,
un mantenedor, versión congelada, releases resubidos. Terceros: luastools,
Valve CM/CDN, steamcmd, steam-monitor, GitHub/jsDelivr, Wine. Telemetría:
donación por defecto. Detección: la mayor del ecosistema por escribir en los
ficheros de Steam.

---

## §3 Superficies externas

### 3.1 Hosts y URLs

| URL | Dónde | Para qué | ¿Upstream? |
|---|---|---|---|
| `https://manifest.luastools.xyz/m/<depot>/<gid>` (GET, HEAD) | `ManifestFetch.cpp:497`, `manifestdonor.cpp:279` | único origen de manifests; comprobación del donante | no |
| `https://manifest.luastools.xyz/manifestwanted` (GET), `/manifestcode/submit` (POST `depot:gid:code`) | `manifestdonor.cpp:122-199` | lista "wanted" y donación de códigos (`Donate.Url`, solo `https://` o loopback) | no |
| `https://api.steampowered.com/ISteamDirectory/GetCMListForConnect/v1/?cellid=0&cmtype=websockets&format=json` y `wss://<cm>:<port>/cmsocket/` | `cmclient.cpp:299-301,442-448` | CM anónimo propio (logon 5514, PICS 8903/8904); caché `cache/cmlist.txt` 24 h | no |
| `https://api.steamcmd.net/v1/info/{appid}` | `appinfo_provision.cpp:301` (override `SLSSTEAM_APPINFO_PROVIDER`) | respaldo de appinfo; 12 s/intento, 3 intentos, 15 s por pase | no |
| `https://raw.githubusercontent.com/AceSLS/SLSsteam/refs/heads/main/res/updates.yaml` | `update.cpp:37-38` | feed de hashes (inerte; sin espejo) | sí (upstream con jsDelivr) |
| `https://raw.githubusercontent.com/swwayps/steam-monitor/main/linux32/<component>/<sha256>.toml[.sig]`, espejo `https://cdn.jsdelivr.net/gh/swwayps/steam-monitor@main/…` | `tools/pattern-refresh/catalog.cpp:768-798` | catálogos de patrones firmados (helper, no el `.so`) | no |
| `https://api.github.com/repos/swwayps/slsteam-moon/releases`, `https://cdn.jsdelivr.net/gh/swwayps/jsdelivr@main/manifest.json` | `autofix.sh` | reinstalación | no |
| `https://api.github.com/repos/atom0s/Steamless/releases/latest` | `install-steamless.sh:55` | solo sin bundle | no |
| `http://localhost:<port>` (CEF) | `cefport.hpp` | contrato para Lumen | no |

Sin `store.steampowered.com/appreviews` (upstream). Todo el `.so` descarga con
libcurl por `dlopen` (upstream: binario `curl`).

### 3.2 Ficheros en disco

| Ruta | Dónde | Uso |
|---|---|---|
| `<config>` = `$XDG_CONFIG_HOME/SLSsteam` o `~/.config/SLSsteam` (XDG vacío → `~/.config`) | `config_path.hpp` | **compartido con SLSsteam vanilla** |
| `<config>/config.yaml` (reescrito si se repara), `luaappids.yaml` | `config.cpp` | config y segunda fuente de apps |
| `<Steam>/config/stplug-in/<appid>.lua` | `config.cpp:103-180`, `depotkey.cpp:306-384`, `manifestid.cpp:251-334` | fuente primaria de apps, claves y pins Lua; dir creado y vigilado |
| `<config>/cache/{depotkey_<d>,manifestid_<d>,picsbuffer_<app>.{bin,yaml},synthetic_<app>,terminal_<app>.state,dlcmetadata_<app>.yaml,proton-mappings.pending,cmlist.txt,dlcquarantine.txt,ticket_<app>.yaml,encryptedTicket_<app>.yaml,.picsbuffer.lock}`; cuarentenas `*.forgotten.*`, `*.orphaned.*` | feats | estado por app |
| `<config>/manifests/<depot>_<gid>.manifest`, `.preferred_<depot>` (**`$HOME` literal**) | `manifeststore.cpp:100-105` | store persistente de manifests |
| `<config>/patterns/<component>/<sha256>.{toml,sig}`, `patterns/.state/`, `patterns/local/<component>/<buildid>-<size>-<mtime>.cache` | `patterns.cpp`, `pattern-refresh` | catálogos y caché local |
| `<config>/.updates.yaml`, `<config>/offline`, `offline_<app>`, `library-added-times.txt` (+`.lock`), `manifest_probe.txt` | `update.cpp`, `ManifestFetch.cpp`, `librarydates.cpp`, `manifestdonor.cpp` | caché del feed; proveedores offline; fechas de inclusión; sonda manual |
| `<Steam>/appcache/appinfo.vdf` (+ `.slssteam-previous`, `.slssteam.lock`, `.slssteam-corrupt.<ts>.<pid>`, `.slssteam-validate.*`) | `appinfo_vdf.cpp:845-1010` | **escrito** (v41 solo); borrado por el guard al latchar |
| `<Steam>/config/config.vdf` (`CompatToolMapping`, `DisableShaderCache`) | `appinfo_provision.cpp:1321-1419`, `depotkey.cpp:432-498` | escrito por texto |
| `<Steam>/depotcache/*.manifest` (+ `.slsteam_tmp.*`, `.archive.<pid>`), `<Steam>/config/depotcache/*` (copiado) | `ManifestFetch.cpp`, `depotkey.cpp:387-430` | manifests de trabajo |
| `<Steam>/steamapps/appmanifest_<app>.acf`, `libraryfolders.vdf`, `workshop/appworkshop_<app>.acf`, `common/<installdir>/**/*.exe` (+ `.original.exe`, `.steamless_done`), `userdata/*/config/localconfig.vdf`, `userdata/**/<app>_pbuf` (borrado) | `apps.cpp`, `steamstub.cpp`, `prewarm.cpp`, `pics.cpp:236-267` | lectura de estado; Steamless; limpieza de shader cache |
| `~/.SLSsteam.log` (append), `~/.SLSsteam.afftrace.log`, `$SLSSTEAM_ATTESTATION_FILE` | `log.cpp`, `afftrace.hpp`, `runtime_attestation.hpp` | logs |
| `$XDG_RUNTIME_DIR/SLSsteam/{setup.<pid>,load.<pid>,api}` (o `~/.cache/SLSsteam/`) | `runtimedir.hpp`, `api.cpp` | locks y API privados |
| `~/.local/share/Lumen/{cef_port,notifications/*.json,lumen}`, `~/homebrew/services/PluginLoader` (marcador Decky), `~/.steam/steam/.cef-enable-remote-debugging` | `cefport.hpp`, `notify.hpp`, `setup.sh` | contrato CEF, cola de avisos, sidecar |
| instalación: `~/.local/share/SLSsteam/{SLSsteam.so,library-inject.so,pattern-refresh,*.lib.sh,ensure-desktop-coverage.sh,path/steam,launcher-alias/,backup/,system-launcher-backup/,steamstub-bypass/,steamless-bin/,steamless-prefix/,coverage-policy.effective}`, `~/.local/share/applications/<id>.desktop`, `~/.config/autostart/steam.desktop`, `~/Desktop/steam.desktop`, `/usr/bin\|/usr/games\|/usr/local/bin/steam` (shim), `~/.config/systemd/user/slsteam-desktop-guardian.{service,path,timer}` + `app-*@autostart.service.d/`, `~/.bashrc/.zshrc/.profile`, `$XDG_STATE_HOME/slsteam-moon/{coverage.policy,coverage.fingerprint.*,guardian.log,guard.log,last_launch,boot_fail_count,safe_mode,session_id,last_client,good_client}`, `/tmp/dumps/crash_*.dmp` (leído), `~/.local/share/CloudRedirect/cloud_redirect.so` | `setup.sh`, libs shell | |

### 3.3 Variables de entorno

Runtime del `.so`: `LD_AUDIT` (limpiado), `LD_LIBRARY_PATH` (`+:/usr/lib:/usr/lib32`,
tolera `nullptr`), `HOME`, `XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR`, `PATH`,
`LC_ALL`/`LC_MESSAGES`/`LANG`/`LANGUAGE` (popups EN/PT), `SteamAppId` (del
juego), `SLSSTEAM_AUDIT_NARROW`/`_BINDALL`, `SLSSTEAM_AFFTRACE`,
`SLSSTEAM_OWNER_QUEUE` (on/off), `SLSSTEAM_OWNER_QUEUE_MAX_STALE_MS`
(def. 30000), `SLSSTEAM_ATTESTATION_FILE`/`_SESSION`, `SLSSTEAM_UPDATES_TTL`,
`SLSSTEAM_PATTERN_CACHE`, `SLSSTEAM_BOOTPROF`, `SLSSTEAM_CA_BUNDLE`/`CURL_CA_BUNDLE`/`SSL_CERT_FILE`/`SSL_CERT_DIR`,
`SLSSTEAM_APPINFO_PROVIDER`, `SLSSTEAM_DISABLE_CM`, `SLSSTEAM_PROVISION_TTL`
(def. 300), `SLSSTEAM_CMLIST_TTL` (def. 86400), `SLSSTEAM_ASYNC_PROVISION`,
`SLSSTEAM_LEGACY_MANIFEST_STAGING`, `SLSSTEAM_MANIFEST_FALLBACK`,
`SLSSTEAM_PLAN_TRACE`, `SLSSTEAM_RECONCILE_PIN`, `SLSSTEAM_RECONCILE_TRACE`.
Wrapper (`setup.sh`): `SLSM_STEAM_BIN`, `SLSM_GUARD_MAX_FAILS` (3),
`SLSM_GUARD_STARTUP_SECS` (180), `SLSM_GUARD_DUMPS_DIR` (`/tmp/dumps`),
`SLSM_IMMUTABLE`, `SLSM_COVERAGE_POLICY`, `SLSM_SUDO_*`, `XDG_STATE_HOME`,
`DISPLAY`/`WAYLAND_DISPLAY`/`XDG_SESSION_TYPE`, `LD_PRELOAD` (CloudRedirect,
libextest), `LUMEN_BACKEND_DIR`/`LUMEN_LUA_DIR`. Steamless: `STEAMLESS_HOME`,
`QUIET`, `WINE_BIN`, `WINESERVER`, `WINEPREFIX`. Build: `NATIVE`.
Upstream tiene `TRACE`/`DEBUG` y `SHARED_LIBRARY_GUARD` (Flatpak); moon no.

### 3.4 Claves de `config.yaml` (todas; `res/config.yaml` ≡ `config_default.hpp`)

Leídas en `CConfig::loadSettings` (`config.cpp:403-836`) al arrancar y en
cada evento inotify. "Up" = existe en upstream 049bbdd.

| Clave | Default (yaml / código) | Qué cambia | Up |
|---|---|---|---|
| `DisableFamilyShareLock` | `yes` | choke opcional de 9406/`NotifyRunningApps` + `owner_id=1` | sí |
| `DisableParentalRestrictions` | `no` | detour del receptor parental + parche de 1 byte; requiere reinicio | **no** (upstream lo hace siempre, sin clave) |
| `UseWhitelist` | `no` | `AppIds` como lista blanca (sin herencia por `parent`) | sí |
| `AutoFilterList` | `yes` | con `PlayNotOwnedGames`, solo `GAME`/`APPLICATION` | heredada (eliminada en upstream 07-07) |
| `AppIds` | vacía | lista negra/blanca | sí |
| `PlayNotOwnedGames` | `no` | unlock genérico de no poseídas (nunca DLC) | heredada (eliminada en upstream 07-07) |
| `AdditionalApps` | vacía | **legado**: solo cuenta si está instalada; "DO NOT ADD APPIDS HERE" | sí (semántica distinta) |
| `DlcData` | vacío | solo bajo padres gestionados | sí (más restrictivo) |
| `AppTokens` | vacío | `access_token` en PICS | sí |
| `FakeOffline`, `FakeAppIds`, `IdleStatus`, `GameTitles`, `SubscriptionTimestamps`, `SteamIdOverride`, `SmartTickets` (`0x1`), `FakeEmail`, `FakeWalletBalance`, `ExtendedLogging`, `WarnHashMissmatch`, `NotifyInit` | = upstream | = upstream | sí |
| `DenuvoGames` | vacío | **account id de 32 bits** → apps (upstream SteamID64) | sí (tipo distinto) |
| `SafeMode` | `no` / **forzado false** | inerte | sí (activa en upstream) |
| `Notifications` | `yes` | habilita popups | heredada (upstream la metió en `LogLevels`) |
| `API` | `no` / `true` | `install\|app\|lib` en el runtime dir | sí (ruta y comandos distintos) |
| `DisableCloud` | **`no`** / `true` | gestionadas siempre sin cloud; resto solo con `PlayNotOwnedGames` | sí (default `yes`) |
| `AutoUpdateApps` | `yes` | `no` congela las gestionadas instaladas no pineadas | **no** |
| `Donate` `{Enabled, Url, IntervalSecs, WantedRefreshSecs, MaxMintsPerCycle, MinMintIntervalMs, MaxMintsPerSession}` | `yes`, `https://manifest.luastools.xyz`, 30, 300, 25, 2000, 0 | donación de códigos con la cuenta real | **no** |
| `InjectAllAdvertisedDlc` | `no` | todo DLC anunciado en package 0 | **no** |
| `Achievements`, `AchievementOwnerId`, `AchievementOwners` | `yes`, `76561198028121353`, vacío | esquema de logros del owner | **no** (upstream `MaxSchemaTries`) |
| `PatternCache` | `yes` | caché local de direcciones | **no** |
| `AsyncProvision` | `yes` | refresco de appinfo fuera de preinit | **no** |
| `MaxManagedApps` | `4096` | tope de scripts activos | **no** |
| `ManifestPins` `{app: {locked, build_id, depots: {depot: "gid"}}}` | (no está en el YAML por defecto) | pins reales por app | **no** |
| `LogLevel` | `2` | umbral numérico (0 Once … 6 None) | heredada (upstream `LogLevels` bits) |

Ausentes respecto a upstream: `CDKeys`, `ManifestIds`, `DepotBlacklist`,
`MaxSchemaTries`, `LaunchOptions`, `Plugins`, `DisableUpdates`, `FakeName`,
`LogLevels`, `DumpClientInterfaces`.

### 3.5 Mensajes de red y callbacks que toca

Sesión propia (`cmclient.cpp`): salientes `ClientLogon` 5514 y
`ClientPICSProductInfoRequest` 8903; entrantes `Multi` 1 (`gzip`),
`ClientLogOnResponse` 751, `ClientPICSProductInfoResponse` 8904. Sobre la
sesión de Steam, entrantes: 751, `ClientLoggedOff` 757, `ClientLicenseList`
780 (observación para `StatsPolicy`/donante), 858 (**mutado**: `eresult=OK`),
`ClientGetDepotDecryptionKeyResponse` 5439 (**mutado**), 5527 (sustitución),
5528, 5456, 8904 (observado), `ClientPICSChangesSinceResponse` 8902
(**mutado**: `app_changes` filtrado), `ServiceMethodResponse` 147 y
`ClientGetUserStatsResponse` 819 (**mutados**), 9406 y `ServiceMethod` 146
`NotifyRunningApps` (descartados); salientes: `ClientGetDepotDecryptionKey`
5438 (observado), 8903 (**mutado**: apps autoritativas fuera, tokens),
`ServiceMethodCallFromClient` 151 (`GetManifestRequestCode` observado e
**inyectado** por el donante; `Player.GetUserStats#1` **mutado**),
`ClientGetUserStats` 818 (**mutado**), `GamesPlayed*` 715/742/5410,
`ClientRichPresenceUpload` 7501 (= upstream). Ya no se tocan:
`ClientPersonaState` 766, `Player.ClientGetLastPlayedTimes#1`. Callbacks y
llamadas internas: `LicensesUpdated_t` (`0x7d`, una vez), `MarkLicenseAsChanged`
+ `ProcessPendingLicenseUpdates`, `AppOwnershipTicketReceived_t`,
`RequestAppInfoUpdate`, `ThreadedReadFromDisk`, `CUtlMemory::Grow`,
`MarkAppChange` (steamui). `AppLicensesChanged_t` ya no.

---

## §4 Lo que nos afecta

### 4.1 Hallazgos pendientes de decisión

Aparcados el 2026-10-08.
Los que el doc anterior ya decidió están en §4.4 y no se repiten.

1. **Retirar un juego de la biblioteca sin reiniciar**: moon lo hace con dos
   detours en `steamui.so` (`CUpdateManager::MarkAppChange`,
   `CSteamUIAppController::RunFrame`) y un offset derivado de una instrucción
   (`8B 41|42 disp8`). Nosotros solo hacemos aparecer (`nosotros.md` §2.9). Patrones opcionales en su código; coste: dos localizadores más en
   `steamui.so` y escribir en un objeto de la UI. (`libraryremoval.cpp:88-161`)
2. **Cuarentena de depots por clave que no descifra**: 3 chunks con
   `Unpack failed` → el depot DLC sale del plan y de package 0, persistido con
   el FNV de la clave; una clave nueva lo libera. El doc anterior dejó abierto
   comprobar si `content_log.txt` ya trae el dato y si nos ocurre (P29).
   (`depotquarantine.cpp`, `depotquarantine_store.hpp`)
3. **Claves observadas de respuestas legítimas** (`managed=false`) y
   **sustitución de la respuesta 5439** cuando Steam da error: nuestro
   DepotKey sirve claves por el accesor local y no captura las reales (P3
   abierto). La sustitución en la capa de mensajes la rechazamos por
   coexistencia (P4). (`depotkey.cpp:510-590`)
4. **Síntesis de `depots`/`launch`/`installdir` para apps token-locked** y
   poda de depots sin clave del appinfo: nosotros dependemos de `AppTokens`
   (`--token`, `add_game_token`; P18 cerrado) y no provisionamos appinfo.
   Sigue cerrado salvo que aparezca un caso sin token obtenible.
   (`appinfo_provision.cpp:450-734`)
5. **`isSubscribed` miente también en moon** y lo resuelve con
   `StatsPolicy::hasNativeLicense` (`subId > 0` observado en el
   `CheckAppOwnership` original). Nuestro `sls_achievement_unblock` repunta
   el guard a `subscribed && !added`; el equivalente de moon es más fino (un
   juego comprado **y** añadido sigue siendo nativo). Dato para cuando se
   revise el parche. (`stats_policy.hpp:55-70`, `apps.cpp:273-276`)
6. **Logros con owner fijo**: moon pide el esquema a `76561198028121353`
   para toda app gestionada y descarta el progreso del owner; upstream usa
   reseñadores. Si el endpoint de reseñas muere el 2026-10-22 (`slssteam.md`
   §4.1-2), el modelo de moon es el que no depende de la tienda, pero sí de
   una cuenta ajena. (`achievements.cpp:21-85`, `config.cpp:507`)
7. **Crash guard con latch al primer crash tras cambio de cliente** y
   **borrado de `appinfo.vdf`**: nuestro guard (vanilla tras N crashes,
   `nosotros.md` §2.1) no distingue "cambió `steamclient.so`". P16 lo dio por
   contexto; la parte de "primer crash si cambió el cliente" es barata.
   (`setup.sh:537-761`)
8. **Pins Lua desconectados de `ManifestPins`**: un `setManifestid` en el
   `.lua` no pina nada en moon; los pins reales los escribe luatools-moon en
   el YAML. Nosotros sí aplicamos el gid del lua (`ManifestIds`). Relevante
   solo si alguna vez importamos `.lua` escritos para moon.
   (`manifestid.cpp`, `manifestcode.cpp:263,765`)
9. **`~/.config/SLSsteam` compartido**: una Deck que venga de SLSDeck (que
   carga moon) trae `config.yaml` con `Donate`, `AchievementOwnerId`,
   `AutoUpdateApps`, `ManifestPins`, `AdditionalApps` legado, y `cache/` con
   `depotkey_*.yaml`, `picsbuffer_*`, `manifests/`, `patterns/`; SLSsteam
   vanilla ignora las claves desconocidas, pero `luaappids.yaml` y
   `stplug-in/*.lua` quedan. Hay que decidir qué limpia `setup.sh` al migrar.
   (`config_path.hpp`, §3.2)
10. **`AutoUpdateApps` global + `locked` por app + proveedores offline** es
    el modelo de updates de moon; el nuestro es `pins.py` + `DisableUpdates:
    no`. La supresión con proveedores offline la tenemos como "pin por
    defecto + resiembra" (P39). Sin acción.
11. **Steamless síncrono dentro de `LaunchApp`** (hasta 180 s): nosotros lo
    aplicamos por botón en LumaDeck (P45). Sin acción; dato de riesgo.
12. **Donación de códigos con la sesión real, por defecto**: decidido no
    donar (P43). Vigilar si SLSDeck/luatools-moon lo dejan activo en Decks
    que compartan cuenta con nuestros usuarios.
13. **Catálogo firmado que solo aporta RVAs**: nuestro feed de RVAs
    (`rva_feed.cpp`) hace lo mismo sin firma (P30 rechazado). La regla de
    moon "el catálogo no puede cambiar firmas ni opcionalidad" es la misma
    que aplica nuestro `verify_mask.py`.
14. **Hilo dueño (`OwnerWork`)**: moon serializa todo lo "Steam-owned" al
    hilo IPC latchado; nosotros disparamos el reconcile desde inotify con
    `CUser*` capturado (`nosotros.md` §2.2). Se daba por
    "ya lo hacemos"; el `NotifyLicensesUpdated` nuestro corre en el
    hilo del watcher. Verificar en qué hilo entra realmente.
15. **`library-inject.so` activo + `-fno-reorder-blocks-and-partition`**:
    moon mantiene el `la_objsearch` de libcurl (upstream lo vació por romper
    excepciones) confiando en el flag. Nosotros no implementamos
    `la_objsearch` y ya llevamos el flag. Sin acción.
16. **Tests contra Steam real** (`test-optional-locators`,
    `test-manifestpin-patterns`, `test_autorepair_cmpfingerprint`,
    `test-cmclient-live`) y `runtime-probes.toml` + attestation: moon valida
    hooks en CI con un cliente real; nuestro `probe-steam.yml` resuelve
    patrones pero no valida hooks colocados. Candidato a comparar cuando se
    toque el CI.

### 4.2 Dependencias y qué rompe si cambia

| Dependencia | Dónde | Qué se rompe |
|---|---|---|
| 55 firmas de bytes (24 opcionales) sobre `steamclient.so`/`steamui.so` | `patterns.cpp:893-1647` | requerida ausente → aborta limpio con popup; opcional → feature off; coincidencia múltiple no convergente → se rehúsa; firma que casa en otro sitio ejecutable → hook erróneo |
| **índices de vtable fijos** (15) sin verificación | `vftableinfo.hpp:27-55` | un virtual nuevo → función equivocada, corrupción silenciosa |
| hash IPC `0xD6FC3200` y layout del buffer de salida de `GetSteamID` | `hooks.cpp:434-453` | sin spoof de SteamID o basura |
| layouts: `CServerPipe`, `CNetPacket` (shift 0/+8 detectado), `CProtoBufMsgBase`, `CAppOwnershipInfo` (`subId==0`/`-1` como señales), `PackageInfo` (+0x38/+0x48), `CUtlVector`, `DepotEntry` 0x20, ctx de `EvaluateConfigChanges` (`flags@4`, `-0x90(ebp)`, `call +0x183`), `OnChunkUnpacked` (`chunk+0x0c`), `CJobMgr::BRouteMsgToJob` (`pJob+0x10`), `CRemoteClientPacket`, `CAppData` (offsets derivados), ticket (`steamId@8`, `appId@16`, firma 128), `gameserverdetails_t.appId@0x90` | `sdk/`, feats | gids en campos equivocados, planes corruptos, tickets inválidos; algunas comprobaciones de "puntero plausible" degradan en vez de crashear |
| convenciones: `regparm(1)` (`EvaluateConfigChanges`), `regparm(3)`/cdecl (`OnChunkUnpacked`), `LoadPackage(PackageInfo*, sha1, cn, ctx)`, `MarkLicenseAsChanged(user, 0, true)`, `GetAppByID(ctrl, app, bool)`, `MarkAppChange(src, app, flags)`, `SettingsReceived(7 args)` | feats | crash o pila corrupta |
| protobuf por número de campo: `ParentalSettings` (9/10/13/15/16, máscara `0x7fff`), `CPlayer_GetUserStats_*` (1-4) | `parental.hpp`, `playerstats.hpp` | renumeración → silencio o fuga de stats ajenas |
| formatos de Valve parseados a mano: `appinfo.vdf` **solo v41** (`0x07564429`), `config.vdf` por búsqueda de texto, `appmanifest_*.acf` (`"manifest"` primero), `libraryfolders.vdf`, `localconfig.vdf`, wire-text PICS, JSON de steamcmd y de CM list | feats | otro magic → no se toca; otro orden de claves → pins/estado erróneos |
| CM: `protocol_version 65580`, `client_package_version 1561159470`, `GetCMListForConnect`, `gzip` en PATH; `ContentServerDirectory.GetManifestRequestCode#1`, `Player.GetUserStats#1` por nombre | `cmwire.hpp`, `cmclient.cpp`, `manifestcode.cpp` | Valve rechaza versiones viejas → steamcmd; `#2` → feature muda |
| transporte CM dual (`InitFromPacket` vs `RecvPkt`, latch una vez por proceso) | `cmrecv.cpp:55-67` | nuevo transporte → toda la recepción muere sin aviso |
| `manifest.luastools.xyz` (único origen), `api.steamcmd.net`, CMs de Valve, `steam-monitor`, `api.github.com`/jsDelivr | `ManifestFetch.cpp`, `appinfo_provision.cpp`, `cmclient.cpp`, `catalog.cpp`, `autofix.sh` | sin manifests nuevos / sin appinfo / firmas embebidas / sin autofix |
| `libcurl.so.4`, `libz`, `libcrypto` (`SHA1` por `dlsym`: si falta, **SHA = 0 y ningún par valida**), `libtier0_s.so`, CA bundle | `curl.cpp`, `manifest_zip.cpp`, `appinfo_provision.cpp:1196-1216`, `cainfo.hpp` | descargas/validación rotas |
| `unzip`, `gzip`, `timeout`, `notify-send`, Wine/Proton, `/proc`, inotify, `$XDG_RUNTIME_DIR`/`$HOME` | `runtime_dependencies.cpp`, `steamstub.cpp`, `process.cpp`, `filewatcher.cpp`, `runtimedir.hpp` | popups de dependencia; sin Steamless; FakeAppIds off; sin hot reload; locks "no usables" → continúa |
| que `steam` lance el webhelper con `--remote-debugging-port=8080` por `exec*`/`posix_spawn*` | `main.cpp:927-1016` | Lumen cae a 8080 |
| `appcache/appinfo.vdf` legible y parseable **antes** de que Steam lo abra | `main.cpp:345-416` | "managed games cannot be prepared this boot"; splice a medias → "stalls the engine" (el guard lo borra) |
| hilo IPC dueño latchado en `IClientUtils::RunIPCFrame` | `ownerwork.cpp:351-401` | tras 30 s corre inline con warn; `Hooks::remove` tras placement cierra la cola para toda la sesión |
| wrapper como único punto de entrada; `/tmp/dumps/crash_*.dmp` para el guard; `boot_id` | `setup.sh` | `steam.sh` directo → sin inyección ni guard; sin dumps → el guard no ve crashes |
| dos namespaces de auditor | `main.cpp:189-231` | sin runtime dir usable ambas copias ejecutan `setup()` |
| `AchievementOwnerId` público y dueño del juego | `config.cpp:507` | `eresult=2` para todas las apps sin `AchievementOwners` |
| glibc ≥ 2.34 (build portable), host x86_64 para `pattern-refresh` | `scripts/Dockerfile`, `Makefile:153-169` | sin helper el wrapper sigue sin catálogos |

### 4.3 Lo que moon tiene y nosotros no (y al revés)

Está en la matriz (`ecosystem-matrix.md` §2 y §3). En una línea: moon tiene
cliente CM anónimo y splice de `appinfo.vdf`, package 0 por `LoadPackage`,
claves observadas y sustituidas en el mensaje, archivo único de manifests
con store propio, cuarentena, pins en el reconcile, `AutoUpdateApps`,
donación, logros por owner fijo, ticket derivado, Steamless integrado,
parental, retirada visual, hot reload en hilo dueño, catálogos firmados,
crash guard con fast-path, shim root, guardian systemd, autofix, 97 tests y
sondas en Steam real. Nosotros tenemos proveedores de manifests y códigos
(Hubcap, Ryuu, luastools, GMRC por hook), pins por job (`pins.py`), fixes
(lua.tools, Goldberg, EOS), UI (QAM), Flatpak detectado, soporte de Deck/Game
Mode explícito, CloudRedirect con `cr_stats_fix`, y no escribimos en
`appinfo.vdf` ni en `config.vdf` salvo `DecryptionKeys`/`CompatToolMapping`.

### 4.4 Decisiones ya tomadas (del doc anterior, con fecha)

| Qué | Decisión | Cuándo |
|---|---|---|
| Leer el `.lua` directamente desde lumalinux (dejar `keys.txt`) | pendiente, nunca cerrado ("Real but bounded") | 2026-06-23 |
| Dejar de escribir `DecryptionKeys` en `config.vdf` | pendiente ("Needs an empirical test") | 2026-06-23 |
| Capturar claves reales en el passthrough de `depot_key_hook` | pendiente ("robustness nicety") | 2026-06-23 |
| Mover DepotKey a la capa de mensajes | **rechazado** (coexistencia con vanilla) | 2026-06-23 |
| Appinfo anónimo + splice de `appinfo.vdf`; hook de la respuesta PICS | cerrado sin acción / rechazado | 2026-06-23, 07-05, 07-22, 09-12 |
| Package 0 por `LoadPackage` | rechazado: nuestro finder va por delante (7 offsets, 2026-09-08) | 2026-06-23 |
| Reconcile de licencias (`LicensesUpdated_t` sobre `CUser`) | **portado** v0.16.15, default v0.16.16, medido 2026-07-20 | 2026-07-20 |
| `manifestid` / `manifestbind` | rechazado (no portable / bajo valor) | 2026-06-23 |
| Segundo endpoint GMRC `manifest.steam.run` | **portado** (cascada de 3) | 2026-07-22 |
| Hooks de red de moon y `BYldRequestDepotManifest` | rechazado (colisión / innecesario) | 2026-06-23 |
| Enmascarar bytes de layout en patrones | parcial (`verify_mask.py`); auditoría nunca cerrada | 2026-07-05 → 09-01 |
| Re-derivación estructural de raíces `RunIPCFrame` | rechazado suave | 2026-07-05 |
| Quitar SafeMode como moon | **rechazado** ("note the trade-off") | 2026-07-05 |
| Crash guard con huella `size:mtime` | contexto, sin trabajo | 2026-07-05 |
| Circuit breaker GMRC + re-probe | pendiente; la cascada "ya cubre la indisponibilidad por otra vía"; moon abandonó los códigos | 2026-07-22 → 09-22 |
| Síntesis de appinfo para token-locked | cerrado: `--token`/`add_game_token` | 2026-07-05 |
| CEF: no mover de 8080 si hay Decky | contexto para un futuro cliente CDP propio | 2026-07-05 |
| Inyección por `.desktop`/wrapper en vez de `steam.sh` | **hecho** (modelo de wrapper) | 2026-09-01 |
| `-fno-reorder-blocks-and-partition` | **portado** v0.16.9 | 2026-07-10 |
| Indentación de los writers de `config.yaml` | rechazado (no autoinfligido) | 2026-07-10 |
| No anunciar DLC cuyo depot no tiene clave | pendiente: "test on Deck" nunca cerrado | 2026-07-22 |
| Parental, staging, backups `.desktop` | no aplica | 2026-07-22 |
| Mensaje "incomplete install → re-add" | idea pendiente | 2026-07-22 |
| Cuarentena de depots (hook `OnChunkUnpacked`) | hook rechazado; verificación pendiente | 2026-08-18 |
| Feed de patrones firmado | **rechazado** (`design/rva-feed-design.md` §14/§15) | 2026-08-18 |
| Unicidad/convergencia en runtime | anotado baja; regla "ambiguo == no resuelto" en finder y CI | 2026-09-01, 09-08 |
| Shim root en el PATH | **rechazado por diseño** ("no ponemos nada de root") | 2026-09-01 |
| Hook no crítico como condición de carga | ya resuelto (solo DepotKey crítico desde v0.20.0) | 2026-09-12 |
| Estado "install readiness" en UI | **rechazado**: pin por defecto + resiembra + job | 2026-09-11 |
| Espejo de releases fijado a commit + hash | anotado baja | 2026-09-12 |
| Donar códigos al archivo | **rechazado por el usuario**: no se dona, ni por defecto ni como opción | 2026-09-28 |
| Steamless opt-in | sin cambio: por botón | 2026-09-22 |
| Pasar nuestros patrones por la beta de escritorio en CI | vigilancia (`probe-steam.yml`, beta CLEAN 2026-10-01) | 2026-09-22 → 10-01 |
| DLC en caliente | sin acción (lo cubre SLSsteam por pipe) | 2026-09-28 |
| Crear el directorio antes del watch | ya hecho (`key_store.cpp:170-189`) | 2026-09-28 |
| steam-monitor como fuente | rechazado (parado desde 09-27; no sigue la Deck) | 2026-10-01, 10-07 |
| "Done!" en vez de "Restart Steam" | hecho (independiente) | 2026-07-22 |

### 4.5 Bugs y fragilidades propias de moon (del código)

- `saveKeyToCache`, `savePin`: `ofstream trunc` no atómico (`depotkey.cpp:208-227`, `manifestid.cpp`).
- `DepotIdVec` de package 0 se llena con cualquier `depotkey_*.yaml` de app gestionada sin mirar `managed` (`packagepatch.cpp:195-242`).
- Dos catálogos de pins desconectados; `AppPins::build_id` sin consumidor; `manifests/` con `$HOME` literal.
- `forgetApp` no retira `depotkey_*.yaml` ni manifests; `purgeDepots`, `restoreQuarantined`, `StripBudget`, `retry.hpp`, `ManifestZip`, `ContentServerDirectory`, `appIdOwnerOverride`, `hasDepotsForApp` sin llamadores.
- `pruneUnsupportedDepots` poda depots sin clave también para apps que Steam posee si llegan a provisionarse.
- `injectProtonMappings` busca `"<appid>"` hasta el final de `config.vdf`, no solo en el bloque.
- `gzipInflate` escribe el blob en `/tmp` y lo pasa por `popen` (shell); `cleanShaderHitCache` recorre todo `userdata/`.
- `getSubscribedApps` sin deduplicar; `getDlcDataByIndex` sin comprobar rango (= upstream).
- `shouldUnlockDlc` depende de `g_pClientUtils` (primer `RunIPCFrame` de Utils): antes devuelve false.
- `checkAppOwnership` llama a `GetAppType` en cada consulta; con `g_pClientApps` nulo el decensor deja de aplicarse a no gestionadas.
- `appAtPinnedGids` relee y parsea el `.acf` en cada `GetUpdateInfo` de una app bloqueada; `onLaunchApp` hace I/O recursiva y puede bloquear 180 s dentro de `LaunchApp`; el iterador recursivo sin `error_code` lanza en el hook.
- `localEpoch(refresh=true)` desde el hilo del WebSocket contra su propio comentario.
- `LibraryRemoval` escribe 0 en un offset derivado de una sola instrucción; `parental.cpp` parchea código y si `LM_HookCode` falla después solo loguea.
- `cefport::readPortFile` acepta cualquier puerto 1024-65535 del contrato.
- `dlcids.hpp::nextQuotedValueFor` busca `listofdlc`/`dlcappid` sin respetar jerarquía.
- Comentarios desactualizados: `manifestid.hpp:26-29` (reescritura PICS inexistente), `manifestbind.hpp:12-13` y `pics.cpp:584` (wudrm/steam.run), `pics.hpp:16-21` (`meta_data_only` flip inexistente), `config.yaml:122` (`/tmp/SLSsteam.API`).
- `setup.sh`: sugiere `./build-docker.sh` (no existe); `check-compat.sh` idem; `scan-all.sh` lee `AdditionalApps` de `config.yaml` (vacío en una instalación normal); PKGBUILDs empaquetarían upstream; BUILDING.md omite `pattern-refresh` y afirma flags "upstream" que no lo son; `ticket-grabber` huérfano; `res/updates.yaml` truncado a mayo.
- Asset de release `-lumen.zip` sin verificar desde GitHub (solo el espejo); el `.so` embebe siempre `VERSION = 20260528151547`.

---

## §5 Historial

§5.1 recoge lo que se creía de slsteam-moon y el código desmiente, además
de lo listado al principio del doc, y lo que sigue sin medir. §5.2 recoge las
pruebas y mediciones con fecha: lo que moon mide de sí mismo en sus commits,
lo que se vio en sus satélites y lo que se sondeó en red. Las pruebas de lo
que portamos de moon (el reconcile de licencias, el wrapper) están en
`nosotros.md` §5.2. §5.3 es la cronología del repositorio.

### 5.1 Lo que se creía y ya no es así

Además de lo listado en la cabecera del doc:

- **El package 0 por hook tiene un timing frágil y sin constantes de
  build.** Hoy hay snapshot, reconcile e hilo dueño; sigue dependiendo de
  `PackageInfo` +0x38/+0x48 (`packagepatch.cpp`, `sdk/CPackageInfo.hpp`).
- **Los proveedores de códigos están caídos para siempre.** Volvieron el
  2026-09-15/16; moon los abandonó igualmente el 09-21 (`c1b5e15`).
- **Un usuario de escritorio con el cliente del 5-sep depende del
  localizador estructural.** `steam-monitor` publicó el TOML el 09-18; el
  catálogo no aporta firmas, solo RVAs (§2.1).
- **La derivación de DLC corre antes de la UI en frío, no en caliente.**
  `f50f28e` (09-29) restaura el gate de app activa: "the runtime package
  refresh already surfaces DLC for games added while Steam runs"
  (`dlc.cpp:12-20`).
- **Moon no retira juegos de la biblioteca en caliente.** Sí:
  `LibraryRemoval` (dos detours en `steamui.so`, `ownershipFlags = 0` +
  `MarkAppChange`).
- **La ingesta del `.lua` es solo en `onStartup`.** Es en caliente, por
  inotify sobre `stplug-in/` (con el directorio creado antes de vigilarlo
  desde `100d39e`).
- **Hay un solo circuito global de proveedores.** Hay un `ProviderCircuit`
  global y además un miss por gid de 10 min (`ManifestFetch.cpp:97-98,159`).

**Lo que sigue sin medir:**

- Que Steam re-extraiga `steam.sh` cuando cambia el tamaño; el revert del
  shim (`dc89501`) está, la causa no.
- Que el crash de ctype sin locale en un módulo `LD_AUDIT` aplique a
  SLSsteam vanilla; la causa en moon sí (`ascii.hpp`,
  `test_audit_policy`).
- Que el zip "2.9" del espejo lleve el revert de DLC sin subir versión: el
  asset se resubió cuatro veces bajo el mismo tag y `version.txt` no cambia,
  pero el contenido no se comparó.
- Qué hace el archivo de LuasTools con un código donado (bajar el manifest
  del CDN al momento): comportamiento del archivo, no de moon.
- Que `cloudredirect-moon` arrastre el bug del almacén vacío de CloudRedirect
  2.6.5; no hay commit al respecto.
- Si la cuarentena de depots responde a un problema de inyectar desde
  `.lua` propios o a claves malas de los proveedores.

### 5.2 Pruebas y mediciones con fecha

"Dónde": commit (lo que moon mide o afirma de sí mismo en el mensaje o el
código del commit; segunda mano), binario, satélite (sus repos auxiliares),
red, usuario (un log ajeno).

#### Función 1 — Engancharse a Steam

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-06-23 → 07-05 | commit `9f419a5` + binario (dos `steamclient.so`) | Self-heal estructural de firmas tras el cliente del 06-23 | `CUser::NotifyLicensesUpdated` lleva un `mov edi,[eax+0x1bXX]` que derivó `0x1b18 → 0x1b14`; `RequiresLegacyCDKey` un `sub eax,<off>`. Las firmas nuevas casan exactamente una vez en el cliente anterior y en el posterior | §2.1 |
| 2026-06-30 | commit `643a92c` | Por qué moon apaga SafeMode | "Its hash whitelist cannot be kept current: it goes stale on every Steam client update and would disable an otherwise-working client"; el guard del wrapper cubre el brickeo de Game Mode. Es nuestro mismo incidente de producción | §2.1 |
| 2026-06-30 → 07-05 | commits `d829fb2` → `dc89501` | Shim de `steam.sh` probado y revertido | Cubrir todos los lanzadores desde `steam.sh` falló por la misma razón que a luatools; la cobertura pasó a los `.desktop`. Origen del modelo de wrapper que `setup.sh` adoptó | §2.1 |
| 2026-07-22 | commit `9d4380f` | Cliente del 07-21 | `RequestInternetServerList` cambió una constante de tamaño (`0x350→0x354`, enmascarada), el patrón de `IClientAppManager::RunIPCFrame` se sustituyó y la mediana del árbol de despacho de `IClientRemoteStorage` se movió ~1,9 M fuera de la banda de tolerancia | §2.1 |
| 2026-08-23 | commits `d3402a1`, `a8029de`, `0fb590f` | Unicidad de patrones | `patternScan` paraba en la primera coincidencia y solo contaba duplicados con `extendedLogging`; pasa a barrer el módulo y exigir una. El mismo día, un comodín de más en el desplazamiento del GOT resolvió al campo vecino ("the neighbouring field shares the emitter shape") y se revirtió a medias | §2.1 |
| 2026-08-26 | commit `a2cef85` | Convergencia | Un localizador alcanzado desde veinte sitios de llamada tenía veinte coincidencias legítimas y exigir una sola abortaba el arranque; si hay varias se sigue cada una y se exige que todas resuelvan al mismo destino (`kMaxConvergenceCandidates = 256`) | §2.1 |
| 2026-08-27 | commits `74b6b1b`, `821e8cb` | Escalada por el shim root | "This script is root-owned and sits on the system PATH, but the wrapper it delegates to lives in a user's home directory and is writable by that user": el shim exige fichero normal, del UID efectivo y sin escritura de grupo u otros | §2.1 |
| 2026-09-01 | commit `52c03f5` | Hook de `CCMInterface::RecvPkt` (family share) revertido | "Remove the unverified CM packet hook so Family Share filtering no longer gates client initialization": el patrón falló en algún cliente y se llevó la inicialización; vuelve el 09-02 como hook opcional con prólogo completo | §2.2 |
| 2026-09-17 | satélite `steam-monitor` | Qué mide y qué publica | Mide solo el manifest de escritorio: stable `1788652215` desde el 5-sep, betas `1789086785` (09-11) y `1789606022` (09-17); el último TOML es el del 3-sep (`237495b4`); el del 5-sep sale el 09-18 cambiando solo la versión. Tiene `bc54101b` etiquetado `1788291500` porque el 2-sep ese binario era también el stable de escritorio | §2.1 |
| 2026-09-18 → 21 | commits `016a536`, `3bd8bc7`, `22eee28`, `b0bb093`, `edfc7ea` | Beta de escritorio que recompila el cliente | `CNetPacket` gana 8 bytes al principio; moon detecta el layout en el primer paquete vivo y accede por accessors. Los mensajes CM llegan a `CCMInterface::RecvPkt` como `CNetPacket` en vez de por `InitFromPacket` | §2.1 |
| 2026-09-27 → 10-07 | satélite `steam-monitor` (`58c784a`) frente al feed de MigoReleases | El tracker, parado | README del 27-sep: stable `1788652215`, beta `1790545198`. MigoReleases registra betas de escritorio el 30-sep, 2-oct y 6-oct que steam-monitor no tiene: parado desde el 27-sep, no Steam | §2.12 |
| 2026-09-30 | usuario (log de SLSDeck, `~/.SLSsteam.log`) | Orden de resolución de un patrón | Caché local primero; "Local pattern cache for steamclient changed compiled locator policy; ignoring" cuando la política compilada cambió; luego catálogo firmado; luego patrón embebido | §2.1 |
| 2026-06-10 → 08-23 | historial git | Commits que tocan firmas | 06-10 RemoteClientManager (byte de struct pineado, 1 comodín); **06-24 seis firmas derivaron en el cliente del 23-06** (`1cbb4f59`): cuatro `RunIPCFrame`, `RequiresLegacyCDKey`, `NotifyLicensesUpdated` (+4 de miembro), con el self-heal estructural el mismo día; 07-22 "adapt locators to the latest client" (3); 08-09 y 08-23 RemoteStorage y "drifted optional locators" (4) | nuestras huellas pasaron `1cbb4f59` sin cambios (`ada282ad`, whitelisteado) |
| 2026-09-14 | red (`swwayps/steam-monitor`) | Cobertura del catálogo firmado | Stable `1788652215`, Beta `1789086785`, y los tres hashes de Deck probados (`bc54101b`, `d0c0ff6e`, `237495b4`); catálogos cada 3-8 días | productor cerrado, en LuaTools |

#### Función 2 — Propiedad y licencias

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-06 (v2.6) | commit + `content_log.txt` | Quién abre la puerta del PICS | No es el `AppToken`: el spoof de propiedad más la inyección de package 0 es lo que hace que Steam pida y trate el appinfo como poseído (el `failed to update ownership ticket` del log es inofensivo). Mismo diseño que LumaCore | §2.2 |
| 2026-06 (v2.6) | commit | Inyectar en package 0 con la caché fría | Cuelga el cliente (Steam re-planifica contra un árbol a medias); de ahí el snapshot y el hilo dueño | §2.2 |
| 2026-07-02 | commit `998ff6f` | Apps bloqueadas por token (Risk of Rain 2, 632360) | Mostraban 0 B y "invalid configuration"; moon reconstruye el bloque `depots` con lo que tiene en disco. Endurecido en v2.8: al menos un depot de contenido instalable con gid ≠ 0, y las apps sintéticas fuera del changelist PICS | §2.2 |
| 2026-07-05 | fuente de SLSsteam `ebfb079` | `AppTokens` en vanilla | Un borrador anterior decía que vanilla "no tiene nada" para apps con token; `Apps::sendPICSInfoRequest` adjunta el `access_token` del YAML. Corregido | `slssteam.md` §2.2 |

#### Función 3 — DLC

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-23 → 09-29 | commits `43f5b6d`, `f50f28e` | DLC de un juego añadido en caliente | El 23 se quitó el gate de app activa porque en el pipe del cliente `getAppId()` es 0 y la biblioteca nunca recibía el unlock; el 29 se restauró: el refresco del package ya hace visibles los DLC de juegos añadidos con Steam abierto, y el test vuelve a afirmar "no DLC is unlocked outside an active app context" | §2.3 |

#### Función 4 — Claves y manifests

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-07-10 | commit `b1da6b6` | Pin de gid y buildid en el appinfo | Hacía que los metadatos fueran por detrás del build vivo, paraba el job de update, colgaba el cliente al arrancar y contaminaba el planificador (activo == objetivo == pin da delta cero y el contenido real no baja). Revertido; el pin se aplica en el reconcile | §2.5 |
| 2026-07-10 | commit `362476d` | Depots gestionados con tamaño 0 | Steam aborta con manifests degenerados; se descartan en `hkBuildDepot`. El circuito pasa a ser por gid (404) en vez de global | §2.4 |
| 2026-07-27 | commit `1022c9f` | Cuarentena de depots cuya clave no descifra | Hook de `OnChunkUnpacked` en dos convenciones (stack y `regparm(3)`); tres chunks fallidos (SHA de 20 bytes en `chunk+0x0c`) ponen el depot en cuarentena, persistida por FNV de la clave. Nosotros no hemos visto el caso (`nosotros.md` §5.1) | §2.4 |
| 2026-08-31 | commit `07d6d17` | "Install readiness" retirado | Era un heartbeat JSON en `~/.config/SLSsteam/install-readiness.json` con una regla `shouldBlock` que, con proveedores caídos, bloqueaba cualquier app con objetivos pendientes; se retira a favor de reintentos vivos y fallo en abierto | §2.4 |
| 2026-09-17 | commit `f40d35b` (port de BetterSteamTools `4a97d9d`) | Donación de códigos | Cada cliente con `Donate.Enabled: yes` (por defecto) pide cada 30 s la lista `GET /manifestwanted` del archivo de LuasTools, acuña códigos con su sesión para los depots que su cuenta posee y los sube a `POST /manifestcode/submit`; 25 por ciclo, 2 s entre ellos. Cuatro días antes de que upstream `dev` hiciera lo mismo | §2.4 |
| 2026-09-21 | commit `c1b5e15` | Moon deja de inyectar códigos | Se va la reescritura de `BRouteMsgToJob` y la cascada wudrm / steam.run ("superseded by the archive and no longer reachable"); el manifest se pone en `depotcache/` antes de que Steam lo pida | §2.4 |

#### Función 5 — Updates de juegos

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-08-30 → 09-03 | commit `168c478` | Pins a cero | `flattenDepots` salta `appId == 0` y `depotId == 0 \|\| gid == 0`; un app solo cuenta como `locked` con al menos un depot válido; tests para las cuatro formas malformadas. Lo que SLSsteam vanilla hace con un `depot: 0` en `ManifestIds` sigue sin verificar | §2.5 |
| 2026-09-19 | commit `668a646` (v2.9); Lumen `f087882` | `AutoUpdateApps` | Con `no`, todo juego gestionado instalado se queda en su build ("mirrors LuaTools' toggle"); los pins por juego mandan; Lumen lo expone en su menú | §2.5 |

#### Función 6 — Fixes y DRM

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09 | commits `d3a424e`, `2f66eaf` | Steamless pasa a opt-in | Por defecto no toca el exe; si lo había tocado, restaura `<exe>.original.exe` al lanzar (comprobando la cabecera `MZ`). Solo corre si las opciones de lanzamiento lo piden | §2.6 |

#### Función 7 — Logros, stats y tiempo de juego

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-08-15 → 29 | commit `ce71798` | Enrutado de logros con CloudRedirect | El manejo local de stats se condiciona a evidencia de licencia nativa y las respuestas se correlacionan por petición; el código dice "Share eligibility with CloudRedirect": se coordinan con cloudredirect-moon | §2.7 |

#### Función 8 — Cloud saves

Sin pruebas propias.

#### Función 9 — Añadir y quitar un juego

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-07-21 | commit `069c031` | Mensaje de instalación incompleta | De "restart Steam" a "re-add the game via LuaTools": reiniciar no arregla datos de instalación a medio escribir | §2.9 |
| 2026-08-15 → 29 | commits `b26737e`, `efa77b8`, `29097d8`, `1729c6a`, `348eed5` | Rendimiento y topes del hot reload | Escaneos cuadráticos al buscar depots; un tope `MaxManagedApps` porque soltar decenas de miles de scripts "stalled the client for minutes"; 199 recargas de config en una copia de directorio, agrupadas en una ventana de silencio; 133 notificaciones en una sesión. La descompresión del zip deja de ir en un hijo "para evitar carreras al recoger procesos hijos dentro de Steam" | §2.9 |
| 2026-09-07 → 22 | commits `1f22f01`, `5aef42e`, `2176c6b`, `5d4e52a`, `7e69d4c` | Hot reload: carreras | Memoización por app para que añadir un juego no rescanee la biblioteca (~2,8 s en 155 apps); dos carreras en las que un escaneo de `steamapps/` coincidiendo con una escritura de Steam daba por desinstalado un juego | §2.9 |
| 2026-09-23 | commit `100d39e` | Watcher de `stplug-in/` en una instalación limpia | El directorio no existe hasta el primer juego, inotify no vigila un path inexistente y nadie volvía a registrar el watch: el primer Add no producía evento. Ahora crea el directorio antes de `addFile` | §2.9 |

#### Función 10 — Credenciales y proveedores

Sin pruebas propias.

#### Función 11 — Mantenimiento propio

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-06-30 → 07-05 | commits `a880c9c`, `ee99e74` (Lumen) | Coexistencia con Decky | Lumen mueve el puerto CDP de Steam fuera de 8080; Decky lo tiene fijo en `localhost:8080`; moon detecta Decky (`$HOME/homebrew/services/`) y deja el puerto. "Verified on Bazzite" es de moon | §2.11 |
| 2026-07-10 | commit `30605f4` | `config.yaml` malformado abortaba el cliente | yaml-cpp lanzaba y, a `-O2` o más, la partición de bloques dejaba la landing pad en `.cold`, saltándose incluso `catch(...)`; arreglado con `-fno-reorder-blocks-and-partition`. lumalinux lo adoptó en v0.16.9 | `nosotros.md` §2.1 |
| 2026-07-14 | commits (sin hash) | Backups de `.desktop` | Un backup con sufijo junto al original hacía que el generador de autostart de KDE lo tratara como un segundo lanzador; pasan a un directorio espejo | §2.11 |
| 2026-08-10 → 10-01 | satélite `jsdelivr` | Lo que publica el espejo | `manifest.json` fija tres componentes con sha256 y URL a un commit de 40 hex; el asset `slsteam-moon-linux-2.9-lumen.zip` se resubió bajo el mismo tag el 21, 22, 24 y 29 de septiembre (5021178 → 5020684 → 5021186 bytes, sha distinto cada vez); el tag `v2.9` sigue en `668a646` | §2.11 |
| 2026-09-28 | satélite `luatools-moon` (`2c739b5`, PR #14) | Instalador | `install.sh` y `uninstall.sh` ganan traducciones, desinstalación, `--keep-millennium` y variables `LUATOOLS_MOON_*` | §2.11 |

#### Función 12 — Proyecto

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-02 → 09-17 | repo | Silencio tras la caída de proveedores | Ni un commit entre el 09-04 y el 09-17 en ninguna rama; el 17 o después se empujan 106 commits de golpe sobre `cae57d2` (64 cherry-picks de upstream) hasta el 22, y el tag v2.9 el 19 | §5.3 |
| 2026-08-17 / 09-07 | GitHub (issues) | Issues | #6 "Upstreaming the moon branches" (ItszFinn, 17-ago) cerrada el 7-sep sin respuesta visible; #7 "User controllable coverage" (yofukashino, 24-ago) abierta. Ninguna sobre proveedores | §5.3 |

### 5.3 Cronología (desde 2026-05-31)

**Topología.** El fork nace el 2026-05-31 con `84450ef` ("chore: import
upstream tree", 714 ficheros de golpe), seguido el mismo día de los
primeros cambios propios (protocol handlers, exewrapper, pins de Lua);
`res/version.txt` y `res/updates.yaml` no cambian en toda la historia. Sin ancestro con upstream
(`git merge-base origin/slsteam-moon 049bbdd` vacío): 77 commits de Ace SLS
están **cherry-pickeados** con su fecha de autor (por eso aparecen fechas de
julio entre agosto y septiembre). Autores: `unplausible` 296, Ace SLS 77,
FATEx0 4, swwayps 3, dankrr 1. 381 commits en `slsteam-moon` (9 may,
129 jun, 87 jul, 99 ago, 57 sep). `beta` (46 propios, último 09-04) y
`millennium` (60 propios, último 07-22) van **por detrás** de main (229 y 323
commits); lo único exclusivo de `beta` es `feat(themes): preload themes
before SteamUI starts` y artefactos de release commiteados; `millennium` (UI
por symlink `libXtst.so.6`) no tiene ficheros exclusivos y le faltan
`autofix.sh`, `pattern-refresh`, el shim, el guardian y las claves nuevas.

**Tags** (todos lightweight; todos con `VERSION = 20260528151547`):

| Tag | Fecha | Commit | Qué cierra |
|---|---|---|---|
| `v2.6` | 06-30 | `3597eb6` | cobertura de `.desktop`, guard anti crash-loop, Lumen en main, cliente CM nativo, pins, Steamless bundle |
| `v2.6-millennium` | 06-30 | `e906775` | mismo estado en la rama Millennium |
| `v2.7` | 07-02 | `b3a9de7` | circuit breaker de red, restore de manifests desde el store |
| `v2.8` | 07-21 | `a25e1d1` | autofix, parental, config malformada, autostart, staging acotado |
| `v2.9` | 09-19 | `668a646` | catálogos firmados, shim del lanzador, guardian, hot reload de biblioteca, tickets locales, donación de MRC, `AutoUpdateApps` |

22 commits sin tag tras `v2.9` (09-19 → 09-29). El asset publicado se llama
`slsteam-moon-linux-<ver>-lumen.zip` (lo construye luatools-moon; `package.sh`
produce `slsteam-moon-linux-<ver>.zip`) y el de 2.9 se **resubió** el 21, 22,
24 y 29 de septiembre bajo el mismo tag (§5.2 F11).

**Cambios de comportamiento, por mes** (hash, fecha):

- *Junio (106)*: `6ccbcb9` 06-04 cloud off para AdditionalApps; `fa6815e`
  SteamStub v3; `f3cbd0a` 06-05 pre-warm de manifests; `d707a5a`→`2e7401e`
  06-06 CloudRedirect por `LD_PRELOAD` (LD_AUDIT probado y revertido);
  **`4b36b05` `DisableCloud` default `no`**; `66ec1f2`…`84920de` **cliente CM
  anónimo nativo**; `18c2762` CM primero, steamcmd fallback; `d68fd05` 06-07
  feed de hashes fuera del boot path; **`5426ac5` 06-09 el wrapper lanza
  Lumen**; `c29a93a` 06-10 SHA-256 propio; **`f28e1e1` 06-11 Lumen a main,
  Millennium a su rama**; `cf493a2`/`d2514cf` 06-15 `load()` una vez por
  proceso; **`633ae9d` Steamless bundle en el release**; **`f51378d` 06-16
  puerto CEF fuera de 8080**; `87499e8` mensajes localizados; **`d1bd5bb`
  06-17 manifestbind con store persistente**; `200ce2b`/`9aa89a4` 06-18
  **esquemas de logros** (nativo + `Player.GetUserStats`); `61d754a`…`a15d7f6`
  06-19 **pins por depot**; `1320677`…`fb361cc` 06-21 **pins por app** (gid de
  appinfo, build number, install plan; "freeze updates only once installed
  at the pinned build"); `818ec98` 06-22 Proton por defecto para solo-Windows;
  `860571c` depots vacíos (crash); `66b5ee5`/`9f419a5` 06-24 patrones para el
  cliente del 06-23 + **auto-reparación de firmas**; `cdfd116`…`d62a8d2`
  **guard anti boot-loop**; `b103474` 06-25 latch al primer crash tras cambio
  de cliente; **`643a92c` SafeMode inerte**; `013b5a9`/`c90bd47` 06-27 config
  sin excepciones; `d829fb2`→`dc89501` 06-28 shim sobre `steam.sh` probado y
  revertido; `a9ea362`…`d1720d9` **desktop-coverage.lib.sh**; `ee99e74`/`a880c9c`
  06-30 contrato CEF, **8080 si hay Decky**; `998ff6f` síntesis de
  depots/launch; `3597eb6` v2.6.
- *Julio (87)*: `3acab45` 07-01 sin gate de CD key legado; `516eeb4`/`d263ba4`/`b3a9de7`
  07-02 **circuit breaker** + restore desde store (v2.7); **`bbae0fc` 07-05
  `autofix.sh`**; `30605f4` 07-07 config malformada recuperada
  (`-fno-reorder-blocks-and-partition`); `362476d` 07-10 depots size-0;
  **`b1da6b6` se retira el pin de build en appinfo** (colgaba el arranque);
  `ab8758b` 07-14 backups fuera de XDG; `bcc7909`/`bdcf1fa` 07-16 staging
  acotado bajo demanda; `a72deb9` DLC sin clave fuera; **`c34fcea` parental**;
  `9abd0de`/`625053e` 07-17 updates suprimidas con proveedores offline;
  `2198a57` apps sintéticas fuera del changelist; `803cd75`/`a25e1d1` 07-21
  gids concretos obligatorios (v2.8); `069c031` aviso "incomplete install
  data"; `9d4380f` 07-22 patrones para el cliente del 07-21;
  **`9d062c6`/`14a66d5`/`f4ab34b` 07-23 `stplug-in` + `luaappids.yaml`,
  `AdditionalApps` legado**; **`557ec42` 07-24 guardian systemd**; `f256cbe`
  07-26 entradas de compatibilidad; **`e757293` alertas a la UI de gamepad**;
  `1022c9f` 07-27 add-ons con clave que no descifra omitidos; `04d8e56` 07-28
  launchers `sh -c`; **`857c009` tickets derivados localmente**; **`774acc2`
  07-29 llamadas del watcher al hilo dueño**; `669c076` sin SHA del cliente
  si nadie lo lee; `643a163`/`9e0f3b6` 07-30 **attestation + locator probe**.
  Cherry-picks de upstream con fecha de julio: `13dbce5`, `c494838`/`5a9b17a`
  (family share por choking), `162cae6` (`RecvPkt`), `4f67fab`, `42b91a9`
  (SteamID 64), `a8e171f`…`a35fc53` (tickets), **`26bed0e` `SteamIdOverride`**.
- *Agosto (99)*: **`85ccd0c`/`be35ab5` 08-01 catálogos RVA locales +
  `pattern-refresh` firmado**; `05e8bb4` 08-04 `appinfo.vdf` endurecido;
  **`5eb9711` shim del launcher de sistema**; `13e9b6f` 08-06 pins con scope +
  DLC virtual; **`2984163` 08-07 política `launcher|desktop`**; `43c6c2c` PR #4
  (dankrr); `ef10b79`…`1668847` 08-09 refresco no bloqueante desde el wrapper;
  **`84e8176` `InjectAllAdvertisedDlc`, `PatternCache`**; **`a4c5703`
  `AsyncProvision`**; `53b2fc2` 08-10 no reprovisionar toda la flota desde PICS;
  `b735708` 08-11 latch por sesión; **`d4efa0f` 08-12 sincronización de
  juegos gestionados en runtime** (añadir/quitar sin reiniciar); `e400331`
  08-13 DLC solo para gestionados; `50e1752` (FATEx0) downgrade preservando el
  manifest instalado; `34ab4fe`/`f7c0c93` PR #5; `0cec89d`/`7ba753c` PR #3;
  `a138c0b` 08-21 appinfo tras updates de package; `a8029de`/`078cad7`/`0fb590f`/`d3402a1`
  08-23 locators opcionales re-resueltos, `.toml.sig`, comodín que resolvía
  al vecino revertido a medias, **`patternScan` exige unicidad**; `a2cef85`
  08-26 convergencia; `b43b317` instalaciones protegidas en outage;
  **`efa77b8` 08-27 `MaxManagedApps`**; `29097d8` coalescencia (199 recargas en
  una copia); `1729c6a` avisos agregados (133 → 1); **`74b6b1b`/`821e8cb`
  RuntimeDir fuera de `/tmp`, shim con trust boundary**; `e459276` 08-28
  fechas de inclusión; `ce71798` enrutado de logros ("Share eligibility with
  CloudRedirect"); `348eed5` unzip en proceso; `08c5af8` 08-29 `DlcData` para
  gestionados; `465a420` README recortado; `305d028` 08-30 lobbies FakeAppId;
  `50a5959` choke de CM para family share; `168c478`/`07d6d17` 08-31 pins a
  cero inertes, fuera el "install readiness". Cherry-picks upstream con fecha
  de agosto: FakeAppIds callbacks/lobbies, `7f9d0ea` AppId desde `/proc`,
  `dd72747`/`b5c4cad` PE/ELF, **`088de32` `SmartTickets`**, Denuvo por entropía.
- *Septiembre (57)*: **`52c03f5` 09-01 revert del choke de CM** ("so Family
  Share filtering no longer gates client initialization"); `9f5d8fc` 09-02
  autofix con espejo jsDelivr; `0e7d207` 09-03 "harden live library updates"
  (`ascii.hpp` sin locale, autoridad local ampliada, Proton por
  `common.oslist`, `chmod 0755`); `2668620`…`cae57d2` 09-02 family share
  **opcional** y `CNetPacket` acotado; `7009f71` 09-05 prólogos con comodín;
  `994716c` 09-06 detour websocket alineado con upstream; `5a883e4` 09-09;
  `1c9ed60`…**`ec56bae` 09-11 tickets cifrados por vtable**; `b101ef6`/`1fac75a`/`2fa7e67`
  09-17 análisis DRM acotado, caché de tickets corrupta tolerada; **`f40d35b`
  09-17 donación de MRC (`Donate`)**, port de BetterSteamTools `4a97d9d`;
  `7e69d4c`…`edfc7ea` 09-18 Proton publicado, pre-seed solo pins, layout de
  paquete detectado en runtime (+8 bytes en la beta), hook opcional no tumba
  la inyección; **`668a646` 09-19 `AutoUpdateApps`** (v2.9); `5aef42e`/`2176c6b`
  scans racy; `b0bb093` reconcilepin con drift; `72ccfca` 09-20 `steamshim`
  preferido; **`d3a424e` Steamless opt-in por `--steamless`**; `2f66eaf`
  `SIGCHLD`; **`c1b5e15` 09-21 "drop dead request-code provider, gate offline
  on archive"**; `22eee28` mensajes CM por `RecvPkt`; `9b67364` join del
  watcher; `1052d4b`/`837e0dd` 09-22 lista de apps suscritas acotada,
  plataforma autoritativa para compat; `16e58e9`/`447ef71`/`013550d`/`43f5b6d`/`100d39e`/`44436f3`
  09-23 locks con tres estados, autoridad publicada antes del splice, **DLC
  sin gate de app activa** (`43f5b6d`), watcher crea `stplug-in`, motivos de
  rechazo en el log; **`f50f28e` 09-29 vuelve el gate de app activa** ("The
  runtime package refresh already surfaces DLC for games added while Steam
  runs; answering on the client pipe only duplicated it and logged every
  managed DLC at login").

**Claves de config cambiadas en el periodo.** Añadidas (ninguna en
upstream): `DisableParentalRestrictions` (07-16), `InjectAllAdvertisedDlc` y
`PatternCache` (08-09), `AsyncProvision` (08-09), `MaxManagedApps` (08-27),
`Donate` (09-17), `AutoUpdateApps`, `Achievements`, `AchievementOwnerId`,
`AchievementOwners` (09-19); por cherry-pick de upstream: `SteamIdOverride`
(07-28), `SmartTickets` (08-21, bits 08-22). Default cambiado: `DisableCloud`
yes→no (06-06). Semántica cambiada: `AdditionalApps` legado desde 07-23.
Conservadas del upstream de mayo: `AutoFilterList`, `PlayNotOwnedGames`,
`Notifications`, `LogLevel`. Nunca portadas: `CDKeys`, `ManifestIds`,
`DepotBlacklist`, `MaxSchemaTries`, `LaunchOptions`, `Plugins`,
`DisableUpdates`, `FakeName`, `LogLevels`, `DumpClientInterfaces`.

**Satélites:** `luatools-moon` 2.9 ya no pide
reiniciar Steam al añadir; **Lumen** (106 commits desde junio) inyecta el
frontend de LuaTools por el puerto CDP; `cloudredirect-moon` reconstruyó su
`.so` el 19-sep; `jsdelivr` fija tres componentes con sha; `steam-monitor`
(catálogos) mide solo los canales de escritorio y está **parado desde el
27-sep**: no sigue a la Deck (etiquetó `bc54101b…` como `1788291500` el 2-sep
y no vio que el 3-sep escritorio pasó a `237495b4…` mientras la Deck se
quedó), y no tiene las betas del 30-sep, 2-oct ni 6-oct que el feed de
MigoReleases sí registra. Issues: #6 "Upstreaming the moon branches" (ItszFinn,
17-ago) cerrada sin respuesta el 7-sep; #7 "User controllable coverage"
(yofukashino, 24-ago) abierta.
