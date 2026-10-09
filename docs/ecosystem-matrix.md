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
SteaMidra/SFF, LumaCore, BetterSteamTools, opensteamtool-cn, LuaTools, ASSella, SLSDeck,
SteamFlipper, slsteam-moon, plugins de SLSsteam, CloudRedirect.

### 1. Engancharse a Steam

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | wrapper `~/.local/share/SLSsteam/path/steam` escrito por `setup.sh`: `LD_AUDIT` (SLSsteam) + `LD_PRELOAD` (lumalinux, CloudRedirect); cobertura Desktop por `.desktop` sombreados y guardian systemd, Game Mode por drop-in de `steam-launcher.service`. lumalinux localiza cinco funciones de `steamclient.so` por feed de RVAs → patrones únicos → rescates (RTTI, xref, anchor); hash SafeMode solo advisory; CI diaria con PR automático de hashes y RVAs. Recuperación: guard anti crash-loop (vanilla), kill-switches, `setup.sh`, downgrade por Desktop (no ejercitado). LumaDeck: estados por componente. | `nosotros.md` §2.1 |
| SLSsteam | `LD_AUDIT` (rtld-audit), solo en el proceso `steam`. Localiza por 18 patrones de bytes (último match) + RTTI y xref de `TraceIPC` para derivar los índices de vtable + 5 índices fijos; 37 hooks (15 detours, 22 VMT sobre objetos vivos, colocados en el primer `RunInterface`). Al actualizar Steam: gate de hash SafeMode opcional (off por defecto; fail-closed sin feed; `version.txt` congelado desde 09-03), si no aborta limpio o crashea. Recuperación: solo `~/.SLSsteam.log` y toasts; sin kill-switch en caliente ni crash guard. | `slssteam.md` §2.1 |
| SteaMidra/SFF | no se engancha: instala a otro. Linux: **SLSsteam** por `LD_AUDIT`, instalado ejecutando **headcrab** (`bash` de `h3adcr-b@main` filtrado por texto; headcrab sustituye `steam.sh` entero, rebaja el cliente a `1788652215`, escribe `steam.cfg` y `config.yaml`) en cada arranque y **cada 60 min** con Steam cerrado (`VERSION` nunca se escribe → siempre "update available"); después `patch_steam_sh` (644, `export LD_AUDIT=…`), `Popen(steam)` con `LD_AUDIT`, `.desktop` por juego, botón `sudo` sobre `/usr/bin/steam-jupiter` (+ `SafeMode: yes`). Sin patrones propios; SafeMode lo decide headcrab (yes en SteamOS, no en el resto). Sin crash guard; "Fix Hash Issue" = `curl \| bash` de `headcrab.pages.dev/reset` + repatch. Windows: LumaCore (DLL de `drappula/LumaCore`, patrones de `MigoReleases` por SHA-256). | `steamidra.md` §2.1 |
| LumaCore | Windows: proxies `xinput1_4.dll` (principal) + `dwmapi.dll` junto a `steam.exe`, que cargan `LumaCore.dll` desde un hilo; engancha el `steamclient64.dll` original (aserción de ruta) con 32 Detours + 2 `int3`/VEH. Localiza por TOML por SHA-256 (`name`/`rva`/`sig`) de `michelegoku3/MigoReleases` (caché primero → raw → jsDelivr → gitflic), **sin comparar la firma** (`ByteSearch` = base+rva) y con firma RSA de módulo ceros (nunca verifica; el feed no publica `.sig`). Al actualizar Steam: sin TOML los hooks faltan limpio, `status.json` + `MessageBoxA`; sin gate, sin kill-switch, sin crash guard. Solo `steam.exe`. Cuatro hooks `_STR` nunca se instalan con el feed de hoy. | `lumacore.md` §2.1 |
| BetterSteamTools | Windows: proxies `dwmapi.dll` + `xinput1_4.dll` que cargan `OpenSteamTool.dll` en su `DllMain` (solo en `steam.exe`); 13 detours vivos + 5 capturas `int3` + trampa de `SpawnProcess` sobre el `steamclient64.dll`/`steamui.dll` originales. Localiza por TOML por SHA-256 de `madoiscool/steam-monitor` (copia cada 15 s de un repo oculto), **red primero** y caché solo si fallan los tres espejos; **RVA sin comparar bytes** (la firma solo si no hay RVA, y el feed siempre trae RVA); sin firma. Al actualizar Steam: sin TOML, aviso y el módulo entero sin hooks; layout de `CNetPacket` detectado en vivo. Sin gate, sin kill-switch, sin crash guard; logs solo en Debug. | `bettersteamtools.md` §2.1 |
| opensteamtool-cn | como BST en la entrada (proxies, Detours, `int3`), pero **engancha una copia** del cliente (`bin\diversion64.dll`) y redirige `LoadModuleWithPath` y `GetModuleHandle*` hacia ella (integridad de Denuvo/RE Engine); TOML por SHA **caché primero**, jsDelivr → GitHub, de `OpenSteam001/steam-monitor`, **parado desde 09-06**: sin hooks en cualquier cliente posterior al estable 1788652215; `CNetPacket` fijo (sin el +8 de la beta). Sin gate, sin crash guard. | `opensteamtool-cn.md` §2.1 |
| LuaTools | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | n/a: el proyecto ya no existe (2026-10-08); su doc se retiró sin releer | — |
| slsteam-moon | `LD_AUDIT` solo desde su wrapper, con cuatro capas de cobertura (shim root en `/usr/bin/steam` con sudo, sombras `.desktop`, guardian systemd `--user`, PATH); sin Flatpak; Deck solo si Game Mode invoca `steam` por PATH. Localiza por caché local → catálogo firmado Ed25519 por SHA del `steamclient.so` (solo aporta RVAs) → 55 firmas con unicidad obligatoria y auto-corrección de raíces IPC; 24 opcionales (feature off); índices de vtable fijos sin verificación. SafeMode inerte (feed de AceSLS); crash guard en el wrapper: latch a vanilla al primer crash si cambió el cliente, borrando `appinfo.vdf`. `library-inject.so` activo. | `slsteam-moon.md` §2.1 |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 2. Propiedad y licencias

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | SLSsteam spoofea por `AdditionalApps`/`FakeAppIds` (los escribimos nosotros). lumalinux inyecta los ids de depot de `keys.txt` en el package-0 en memoria cada 15 s y al cambiar el fichero, retira los que salen (v0.22.2) y dispara `NotifyLicensesUpdated` con el `CUser*` capturado del guard de SLSsteam (sin reiniciar). Juegos poseídos: LumaDeck decide desde `packageinfo.vdf`, añade solo DLC, nunca pinea depots con licencia real. Family sharing: nada. Redists: en `keys.txt`, nunca pin ni `AdditionalApps`. | `nosotros.md` §2.2 |
| SLSsteam | post-hook de `CheckAppOwnership`: `unlockApp` solo para `AdditionalApps` (tras el primer `GetSubscribedApps`), decensor y región para todas; `GetSubscribedApps` añade `AdditionalApps` sin deduplicar. Sin packages ni licencias (solo en la rama `update`, nunca mergeada); `AppTokens` en PICS; `AppLicensesChanged_t` solo en caliente. Poseídos de verdad: `isSubscribed` = trampolín de `CheckAppOwnership` (propiedad real según Steam) protege DLC, logros, cloud, updates, CD keys. Family sharing: descarta 9406 y `NotifyRunningApps`, `GetAppStateInfo` sin `Borrowed`, `owner_id=1`. Redists: nada (solo loguea `sharedDepots`). | `slssteam.md` §2.2 |
| SteaMidra/SFF | delegado: Linux `AdditionalApps` de SLSsteam (app + **todos** los `dlcappid` + `AppTokens`; `migrate_existing_games` mete todo lo instalado, poseído o no); Windows `addappid(<id>)` en `stplug-in/<id>.lua` para LumaCore. Sin packages, sin distinción de poseídos (`LastOwner` del setting o `0`), `DisableFamilyShareLock: yes`; redists solo como lista fija para agrupar el lua. `PlayNotOwnedGames` ya no se escribe (6.6.1). | `steamidra.md` §2.2 |
| LumaCore | package 0 en memoria (`LoadPackage`/`GetPackageInfo` + `CUtlMemoryGrow`, reintento 150×) + `CheckAppOwnership` (dueño para todo id del `.lua`) + `GetSubscribedApps` (solo raíces) + refresco en caliente `MarkLicenseAsChanged`+`ProcessPendingLicenseUpdates` con reconcile completo; `bReloadAll` forzado. Poseídos: Steam dice dueño con >1 paquete → no se toca. Family sharing: vacía `NotifyRunningApps` y `SharedLibraryLockStatus/StopPlaying`. Tokens PICS de `addtoken()` en cada petición. Redists 228980 nunca pineados. | `lumacore.md` §2.2 |
| BetterSteamTools | package 0 (token fijo) con todos los ids del `.lua` + `MarkLicenseAsChanged`/`ProcessPendingLicenseUpdates`; `CheckAppOwnership` dueño para todo id del `.lua` (`bOwnsLicense`, `bFreeLicense=false`); poseído = ya dueño y en >1 package (`MarkOwned`), depots cruzados por el mapa de `BuildDepotDependency`; lista de licencias 780 leída para el donante. Family sharing: vacía `NotifyRunningApps` y 9406 (todas las cuentas con OST). Sin trato de depots compartidos. | `bettersteamtools.md` §2.2 |
| opensteamtool-cn | como BST, más family sharing en los dos sentidos: todo juego prestado (del `.lua` o no) desbloqueado y dado por poseído; hacia Valve, `owner_id` del prestador → 1, `NotifyRunningApps` saneado, 9405 descartado; `m_Size` reescrito tras `Grow`. | `opensteamtool-cn.md` §2.2 |
| LuaTools | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | n/a: el proyecto ya no existe (2026-10-08); su doc se retiró sin releer | — |
| slsteam-moon | fuente `stplug-in/<appid>.lua` + `luaappids.yaml` (`AdditionalApps` legado). Inyección en el package 0 real (`CPackageInfoCache::LoadPackage` + `CUtlMemory::Grow`) con `LicensesUpdated_t` una vez y `MarkLicenseAsChanged`/`ProcessPendingLicenseUpdates` en caliente desde el hilo IPC dueño (`OwnerWork`); `CheckAppOwnership` con gate de tipo y `StatsPolicy::hasNativeLicense` (`isSubscribed` miente: package 0 inyectado); `GetSubscribedApps` sin overflow; PICS saliente sin apps autoritativas y changelist filtrado; family share por choke opcional + `owner_id=1` (sin `GetAppStateInfo`); retirada visual de la biblioteca sin reinicio (`steamui.so`). Redists: nada. | `slsteam-moon.md` §2.2 |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 3. DLC

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | unlock: SLSsteam (todo DLC no poseído del juego en marcha). LumaDeck: `DlcData` solo >64 DLC; alta y baja por la casilla de Steam (`SetDLCEnabled`, `DisabledDLC`); DLC comprado se deja a Steam en todos los caminos; DLC sin depot → `AdditionalApps`; quitar un DLC de un juego added exige reinicio. | `nosotros.md` §2.3 |
| SLSsteam | `shouldUnlockDlc`: todo DLC no poseído de verdad y no excluido (`AppIds`/`UseWhitelist`, herencia por `parent`) mientras el pipe sea de un juego; contesta `CheckAppOwnership`, `IsAppDlcInstalled`, `BIsDlcEnabled` (sobre el padre), `IsUserSubscribedAppInTicket`. Comprado: se respeta. Sin depot: flag. Sin límite propio: `DlcData` suple la lista que Steam no entrega (sin llamar al original). Se quita por config (hot reload). | `slssteam.md` §2.3 |
| SteaMidra/SFF | todos los `dlcappid` del padre a `AdditionalApps` siempre; `DlcData` solo si `listofdlc` ≥ 64 (camino CLI: depots `{}` con `yaml.dump`); DLC sin depot = `addappid(<dlc>)` en el lua + `AdditionalApps` previa confirmación; "DLC Check" + `download_dlc_free` (append al lua del padre, manifest, ficheros). Comprado de verdad: sin distinción. Quitar: nunca se limpian los DLC. Fuera de Steam: Goldberg `unlock_all=1`, SmokeAPI/CreamAPI/Uplay con DLL empaquetadas. | `steamidra.md` §2.3 |
| LumaCore | DLC = ids `addappid` del `.lua` en el package 0, dueño por `CheckAppOwnership`; sin lista ni límite (no usa `DlcData`); comprado de verdad = criterio de poseído (>1 paquete); sin depot = igual; se quita borrando la línea/el `.lua` (refcount + refresco en caliente). | `lumacore.md` §2.3 |
| BetterSteamTools | todo id del `.lua` es dueño, DLC incluido, sin límite; DLC sin depot basta el id; comprado = regla de >1 package; se quita borrando la línea. | `bettersteamtools.md` §2.3 |
| opensteamtool-cn | como BST. | `opensteamtool-cn.md` §2.3 |
| LuaTools | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | n/a: el proyecto ya no existe (2026-10-08); su doc se retiró sin releer | — |
| slsteam-moon | solo DLC del scope de bases gestionados (`listofdlc`, `dlcappid` del appinfo, `DlcData` bajo padre gestionado) + pipe de juego (gate restaurado 09-29) + sin licencia nativa; `BIsDlcEnabled` sobre el DLC; DLC inyectados en package 0 (anunciados solo con contenido o `InjectAllAdvertisedDlc`); metadatos de DLC escritos en `appinfo.vdf`; cuarentena persistente de depots DLC cuya clave no descifra (3 chunks); sin tope propio. | `slsteam-moon.md` §2.3 |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 4. Claves y manifests

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | claves del lua a `keys.txt` (tres formatos) + `config.vdf`, servidas por el hook DepotKey; `retired_keys.txt` para borrar. Manifests: depotcache → archivo propio → luastools → Hubcap suelto (1500/día, 1 intento por depot+gid/día) → zip (Hubcap 25/día, Ryuu; Sushi/Spinoza off). Códigos GMRC: hook de lumalinux con 20770407, manifestdex, wudrm, steam.run (1 req/s, backoff, verificación en el CDN, `gmrc.json`). Sin proveedores: todo congelado al build instalado. | `nosotros.md` §2.4 |
| SLSsteam | sin claves de depot, sin manifests externos, sin códigos de petición (todo eso solo en la rama `update`). `ManifestIds` (pin por depot) y `DepotBlacklist` (con bug de swap) en el post-hook de `BuildDepotDependency`; `CDKeys` legacy (o clave determinista) vía `SetLegacyCDKey`. Servicios: GitHub/jsDelivr (solo SafeMode), tienda de Steam (reseñas para logros). | `slssteam.md` §2.4 |
| SteaMidra/SFF | claves: lua de Hubcap/Ryuu/DepotBox (clave) o "Free Providers" (`api.steamcmd.net` + `fylsdy/ManifestHub` **404** + BD local `KoriaPolis/Steam-Depot` cada 6 h → `revobd` → `steamtoolsapp/ManifestHub`, `steamtools-games/ManifestHub3`) → `saved_lua`, `config.vdf` `DecryptionKey` (nunca sobrescribe), `stplug-in`; contribución de claves al worker `steamidra.workers.dev` (ON por migración de settings). Manifests: local → **`manifest.luastools.xyz`** → (Hubcap on-demand 1500/día) → 3 espejos GitHub `k25FCdfEOoEJ42S6` → ManifestHub2 (clave 24 h). Códigos: solo la sesión **anónima** del descargador nativo; sin GMRC externos (`9033611`). Descarga: nativo propio (CDN, AES, VZ/VSZTD, SHA-1) o DDMod; sin circuito. | `steamidra.md` §2.4 |
| LumaCore | claves solo del `.lua` (64 hex exactos, si no se ignoran) por **tres** caminos: `LoadDepotDecryptionKey` y dos detours de `ConfigStoreGetBinary`; nada en `config.vdf`. Manifests: no los baja (SteaMidra). Códigos: puente **por el cable** (`GetManifestRequestCode#1` retenido) → funciones Lua `fetch_manifest_code(_ex)` → opensteamtool → steam.run → wudrm (los que SteaMidra retiró), **sin comprobar en el CDN**, `trusted_hosts` sin efecto, 12 s para toda la cadena. | `lumacore.md` §2.4 |
| BetterSteamTools | claves del `.lua` (64 hex) por `ConfigStoreGetBinary`; manifests del archivo `manifest.luastools.xyz` precargados en `depotcache` en `YldLoadDepotManifest` (validación solo por magia; también Workshop); códigos: `fetch_manifest_code(_ex)` del `.lua` → **un** proveedor (`opensteamtool`, `wudrm` http, `steamrun`), sin CDN, esperando hasta 12 s en el hilo de recepción. **Donación on por defecto**: acuña con la sesión del usuario los depots poseídos que pide `manifestwanted` y sube los códigos de **todas** sus descargas. | `bettersteamtools.md` §2.4 |
| opensteamtool-cn | claves y pin como BST; **sin** archivo de manifests ni donación; códigos: `.lua` → **seis proveedores en failover** (`20770407`, `SDM`, `manifestdex`, `wudrm`, `steamrun`, `opensteamtool`), enfriamiento 60 s, presupuesto 15 s, en paralelo; sin CDN. | `opensteamtool-cn.md` §2.4 |
| LuaTools | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | n/a: el proyecto ya no existe (2026-10-08); su doc se retiró sin releer | — |
| slsteam-moon | claves del `.lua` (`addappid(d,1,hex)`) y observadas de Steam, en `cache/depotkey_<d>.yaml`, sustituidas en la respuesta 5439 (32 ceros si no hay); depots sin clave podados del appinfo provisionado. Manifests: pin → depotcache → store propio `~/.config/SLSsteam/manifests/` → archivo luastools `manifest.luastools.xyz/m/<depot>/<gid>` (**único** origen; circuito 2 fallos/429) → fallback local; síntesis de `depots`/`launch` para token-locked. Códigos: ninguno para instalar (retirados 09-21); donación de códigos de depots poseídos con la sesión real (`Donate.Enabled: yes` por defecto). | `slsteam-moon.md` §2.4 |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 5. Updates de juegos

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | pin vivo = `ManifestIds` de SLSsteam (`keys.txt` gid/size dormido, BuildDep OFF). `pins.py`: pase local 60 s (heal, modelo up/down por `gmrc.json`), pase de update 30 min contra `api.steamcmd.net`, pin solo con todos los manifests; congelación por usuario, ficheros (fixes) o proveedores. Versión antigua: SteamDB (10 builds, BrowserView oculto) + `--set-pin` + `StateFlags\|=2`; también por LuaTools. Update atascada: `UpdateResult=8` → Fix Update; sin cancelación. | `nosotros.md` §2.5 |
| SLSsteam | `ManifestIds` para fijar o bajar de versión; `DisableUpdates: yes` (default) → `GetUpdateInfo` false para `AdditionalApps` o no poseídas de verdad (hot reload). La automática la decide Steam; no toca `buildId`; sin cancelación de updates en curso. | `slssteam.md` §2.5 |
| SteaMidra/SFF | freeze: ACF a mano 0444 (`StateFlags 4`, `buildid`/`TargetBuildID` public, `AutoUpdateBehavior 0`, `InstalledDepots` reales desde `8eaf238`) + `DisableUpdates: yes` + `ManifestIds` y `setManifestid` si el usuario eligió versión + `steam.cfg` (cliente). Sin comprobación periódica; "Update" = refetch + rebajar; "Lure Fix" = ACF con gids actuales sin descargar. Versión antigua: historial (CM → morrenus → árbol GitHub → tokens; SteamDB off), DepotBox build-details (token embebido), HTML de SteamDB; `_sync_acf_downgrade` o cola de 7 días. Windows: `00_LetUpdate_override.lua`. | `steamidra.md` §2.5 |
| LumaCore | pin por `BuildDepotDependency` (gid del vector, activo) desde `setManifestid` + barrido de texto que re-pinea literales; auto-update por depot con `skipManifestPin`, redists o `_originals` (patrón `00_LetUpdate`); versión antigua = `setManifestid` con gid viejo (lo escribe SteaMidra); nada para updates empezadas. | `lumacore.md` §2.5 |
| BetterSteamTools | pin por `setmanifestid` en `BuildDepotDependency`; sin pase de updates ni rollback; hace fallar `BuildDepotDependency` si la lista de depots de un juego del `.lua` vuelve vacía en un refresco (evita el "instalado sin descargar"). | `bettersteamtools.md` §2.5 |
| opensteamtool-cn | pin por `setmanifestid`, `pinapp` registrado; sin la guarda de depots vacíos de BST. | `opensteamtool-cn.md` §2.5 |
| LuaTools | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | n/a: el proyecto ya no existe (2026-10-08); su doc se retiró sin releer | — |
| slsteam-moon | `ManifestPins` del YAML (`locked`, gid+size) aplicados en el plan (`BuildDepotDependency`) y en el reconcile (`EvaluateConfigChanges`, "one downgrade"); los `setManifestid` del `.lua` no se aplican como gid; `AutoUpdateApps` global; updates suprimidas con proveedores offline; instalación inicial siempre permitida; sin cancelación. | `slsteam-moon.md` §2.5 |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 6. Fixes y DRM

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | LumaDeck: catálogo lua.tools (cuenta Discord; slot manifest + fix), fixes sin cuenta de files.luatools.work, Steamless (.NET 9, sin deshacer), Goldberg, proxy EOS, toggle Online (480 + netsock + EOS), spliced tickets (plugin Lua de Ace); backups por fichero y log `[FIX]`; cualquier escritura congela el juego. | `nosotros.md` §2.6 |
| SLSsteam | sin fixes (ni Steamless, Goldberg ni EOS). Tickets de propiedad (858) y cifrados (5527) cacheados en `<config>/cache`, reinyectados en `LaunchApp` y si el servidor falla; SteamID que ve el juego: `SteamIdOverride` → spoof de una vez (`SmartTickets`: SteamDRM/Denuvo por entropía en `ConnectPipe`) → ticket cifrado; `DenuvoGames` no finge propiedad de otra cuenta; `FakeAppIds` (AppId real para stats/cloud/appinfo, falso para amigos/matchmaking/presencia); `LaunchOptions` (`%command%`); `ticket-grabber`. | `slssteam.md` §2.6 |
| SteaMidra/SFF | Fix Game (DRM check por `appdetails`: Denuvo → **para**; gbe_fork de GitHub sin hash; `steam_settings` con SteamID64 compartido; Steamless `Steamless.CLI.dll`/dotnet sobre todos los exe; swap de DLL / ColdClient / ColdLoader; launch scripts), "Crack a game" (`OG_`), "Remove SteamStub", SteamAutoCrack CLI (sin `wine` en Linux), "Fixes & Bypasses" (`KoriaPolis/CrakFiles` → pixeldrain, contraseña `cs.rin.ru`, zip sin backup), online-fix solo navegador, `-onlinefix` en `localconfig.vdf`. Deshacer incompatible entre caminos. EOS: nada. La descarga CLI Linux pasa Steamless sin preguntar. | `steamidra.md` §2.6 |
| LumaCore | tickets en el registro (`Apps\<id>`) y **forjados** con el ticket firmado de la app 7; `GetSteamID` = registro/ticket/carpeta `userdata`/cuenta activa; ETicket del registro. Denuvo: sin ventana viva (código muerto), `forcedenuvo` sin efecto. SteamStub: sondeo + preflight de ticket en cada lanzamiento, ruta dedicada apagada por defecto. `-onlinefix` → ruta 480 con `LumaCorePayload.dll` (EOS Device ID, sin presencia de lobby) inyectado en el juego suspendido. `RequiresLegacyCDKey` → falso. Sin Goldberg ni Steamless. | `lumacore.md` §2.6 |
| BetterSteamTools | SteamStub: ticket forjado con el de la app 7 (off-by-four). Denuvo: tickets del registro (`setappticket`/`seteticket`, `extract_tickets`, canje `bst://` a `luastools.xyz` sin confirmación), `GetSteamID` suplantado, ventana de autorización, 858 suplantado, ETicket por nonce a un backend (vacío en el release; cacheado por sesión). Clave legacy 730 en local. Ruta 480 con `-onlinefix` (Input/overlay/amigos con el appid real, `-realappid`). `[[inject]]` en juegos. Sin Steamless, Goldberg ni EOS. | `bettersteamtools.md` §2.6 |
| opensteamtool-cn | como OST (SteamStub forjado, tickets del registro, ruta 480, clave legacy, `[inject]` x86/x64); **ventana de Denuvo por plazos** (300 ms al arrancar y tras cada ticket; 2,5 s/3 s con `dauth2`), identidad guardada también para prestados; presencia para amigos como juego no-Steam (primer acceso directo). Sin ETickets por backend ni canje. | `opensteamtool-cn.md` §2.6 |
| LuaTools | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | n/a: el proyecto ya no existe (2026-10-08); su doc se retiró sin releer | — |
| slsteam-moon | tickets con `eresult=OK` estampado y ticket derivado de la app 7 cuando no hay real; Steamless integrado (kit commiteado, opt-in `--steamless` en LaunchOptions, síncrono en `LaunchApp` hasta 180 s, Wine propio; sin opt-in restaura el `.original.exe`); Proton automático por SO (herramienta del usuario o Experimental) en `config.vdf` y en caliente; `RequiresLegacyCDKey` suprimido para base + DLC; `SmartTickets`/`DenuvoGames` (account id); sin Goldberg/EOS; sin `LaunchOptions` de upstream. | `slsteam-moon.md` §2.6 |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 7. Logros, stats y tiempo de juego

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | SLSsteam consigue el esquema por el borrow con reseñadores (sin almacén propio: guarda Steam) y lo salta si `isSubscribed`, que con nuestra inyección de package-0 es true; lumalinux repunta ese guard a `subscribed && !added` (escritura atómica) para que los juegos añadidos entren. CloudRedirect sincroniza stats y logros por blob en Drive (una descarga por arranque); `cr_stats_fix` sobra desde CR 2.6.6. LumaDeck: generador de esquemas apagado. Pendiente: endpoint de tienda retirado el 2026-10-22, esperar SLSsteam. | `nosotros.md` §2.7 |
| SLSsteam | sin almacén propio (guarda Steam). Borrow de esquemas solo para no poseídas de verdad (guard `isSubscribed`, el que lumalinux repunta): reescribe `GetUserStats` (legacy y `Player.GetUserStats#1`) con SteamIDs de reseñadores de `store.steampowered.com/appreviews` (`MaxSchemaTries`, blacklist, `preferredOwners`, cooldown 10 min; `NoConnection` como fallback); vacía los stats de la respuesta. Borra las `AdditionalApps` de `ClientGetLastPlayedTimes`. Sin sincronización. `schema-grabber` offline. Endpoint de tienda cambia en `dev` (retiro anunciado 2026-10-22). | `slssteam.md` §2.7 |
| SteaMidra/SFF | nada propio: Linux lanza **SLScheevo** (script Python empaquetado, no leído) con `--max-tries 101`; `set_stats_and_achievements` es un stub; Windows LumaCore. Goldberg: esquema por Web API con clave **embebida**, logros en `GSE Saves/`. Sin sincronización. | `steamidra.md` §2.7 |
| LumaCore | sin almacén: reescribe `Player.GetUserStats`/`ClientGetUserStats` con SteamID de **donante** (`setStat` o pool de 15 incrustado, rotando) y borra sus stats de la respuesta (solo esquema); `ClientStoreUserStats2` sale al CM tal cual. Sincronización: lo que acepte el CM (sin medir). Tiempo de juego: sin tratamiento (480 en la ruta). | `lumacore.md` §2.7 |
| BetterSteamTools | stats con SteamID de donante (`setstat` → `stats.opensteamtool.com` → fijo `76561198028121353`) reescrito en el cable, consulta HTTP dentro del hook de envío; con CloudRedirect, logros de CloudRedirect en la 819. Sin almacén propio. | `bettersteamtools.md` §2.7 |
| opensteamtool-cn | como OST: SteamID de donante en el cable. | `opensteamtool-cn.md` §2.7 |
| LuaTools | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | n/a: el proyecto ya no existe (2026-10-08); su doc se retiró sin releer | — |
| slsteam-moon | sin almacén; reescribe bytes de `ClientGetUserStats`/`Player.GetUserStats#1` en el WebSocket con un owner fijo (`AchievementOwnerId` 76561198028121353, o `AchievementOwners`) solo para apps que moon inyectó en package 0; descarta `stats`/`crc_stats` del owner; fallo seguro `eresult=2`; sin reseñadores ni cooldown; ABI `slsteam_local_stats_epoch_v1` para CloudRedirect; sin filtrado de playtime; sincronización por cloudredirect-moon. | `slsteam-moon.md` §2.7 |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 8. Cloud saves

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | CloudRedirect (componente core de `setup.sh`; Drive/OneDrive/Dropbox; app flatpak para el login). `DisableCloud: no` en SLSsteam. LumaDeck solo observa (`not_authed`, `disabled`). `libcurl_pin` para nuestras propias descargas. | `nosotros.md` §2.8 |
| SLSsteam | `DisableCloud: yes` (default) → `IsCloudEnabledForApp` false para no poseídas de verdad; sin redirección ni proveedores; el YAML remite a CloudRedirect. | `slssteam.md` §2.8 |
| SteaMidra/SFF | sin redirección: `DisableCloud: yes` en SLSsteam y **CloudRedirect recortado** del headcrab que ejecuta. Backup/restore de `userdata/<id>/<app>/remote`, emuladores, rutas custom (Ludusavi muerto por ruta) a local, rclone (ELF i386) o Google Drive (`_gc.py` gitignorado → off en el árbol público); auto backup cada 10 min; Steam reconcilia. | `steamidra.md` §2.8 |
| LumaCore | sin redirección: **apaga** Steam Cloud para apps del `.lua` no poseídas ni compartidas (`IsCloudEnabledForApp` false, sync = 2, AutoCloud anulado); fuerza la nube en family sharing; en la ruta 480 lee los saves del juego real desde `userdata/<id>/<real>/remote` (hook sin instalar hoy). | `lumacore.md` §2.8 |
| BetterSteamTools | con `[cloud] enabled` (off) carga `cloud_redirect.dll` y le pasa las RPC `Cloud.*` de los juegos del `.lua`, contestadas en local y suprimidas hacia Valve. | `bettersteamtools.md` §2.8 |
| opensteamtool-cn | como OST (CloudRedirect), que recibe la copia del cliente por la redirección de módulos. | `opensteamtool-cn.md` §2.8 |
| LuaTools | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | n/a: el proyecto ya no existe (2026-10-08); su doc se retiró sin releer | — |
| slsteam-moon | `DisableCloud: no` por defecto (CloudRedirect por `LD_PRELOAD` desde el wrapper), pero toda app gestionada va siempre sin cloud; Steam solo se consulta con `PlayNotOwnedGames`. | `slsteam-moon.md` §2.8 |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 9. Añadir y quitar un juego

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | entradas: AppID, página abierta (CEF), nombre (tienda de Steam); sin zip local; sin elección de disco. Add: zip por `api.json` → `_process_and_install_lua` (orden del lua, depot Linux) → `steamidra_lite` (depotcache, `AdditionalApps`, `keys.txt`, `config.vdf`, `.acf`, `stplug-in`) → post-check → Proton forzado; sin reinicio (reconcile). Quitar: carpeta, `.acf` en todas las bibliotecas, compatdata/shadercache (raíz + biblioteca), depotcache, config de SLSsteam, claves (retiradas si poseído); deja `config.vdf`, `pins.json`, archivo propio. Bibliotecas: unión de los dos `libraryfolders.vdf` con tres puntos ciegos. | `nosotros.md` §2.9 |
| SLSsteam | entrada: solo AppId en el YAML (a mano, por Lua `setAdditionalApps`, o `install\|app\|lib`/`uninstall\|app` por el fichero de comandos); no escribe nada en disco; no limpia nada (tickets, `cdk_<n>`); `AppLicensesChanged_t` en caliente; bibliotecas por índice (`dumplibraries`); carpeta la decide Steam. | `slssteam.md` §2.9 |
| SteaMidra/SFF | entradas: appid/nombre (catálogo local → `GetAppList` → espejos → Hubcap), lua/zip/rar/7z, recientes, bulk, `-f`; URL solo en la CLI; `midra://` no se procesa. Escribe: `AdditionalApps` (+DLC) → `ManifestIds` → `config.vdf` → `saved_lua` → `stplug-in` (**matando Steam**) → manifests a depotcache + staging → descarga a `<lib>/steamapps/common/<installdir>` → ACF 0444 → `libraryfolders.vdf`. Quitar: lua (+`saved_lua`), "full" + `AdditionalApps`(base), `ManifestIds`, manifests, ACF, `rmtree`; CLI `full_keys` borra claves. Quedan DLC, `DlcData`, `AppTokens`, `libraryfolders`, `steam.cfg`. Biblioteca: `libraryfolders.vdf`; picker solo en versión/DDMod. | `steamidra.md` §2.9 |
| LumaCore | entrada: `.lua` en `config/stplug-in` (los escribe SteaMidra), ejecutados en Lua 5.5 con sandbox; el nombre numérico registra la app. Añadir: nada en disco, en caliente (watcher → package 0 → licencias → SteamUI). Quitar: borrar el `.lua` → baja del package 0 y de la biblioteca en caliente; quedan tickets del registro y ACF de 480. Bibliotecas e `installdir`: Steam. | `lumacore.md` §2.9 |
| BetterSteamTools | solo `.lua` en `stplug-in` (+ `[lua] paths`), en caliente: alta y baja del package 0 y de la biblioteca (`MarkAppChange`), fecha de compra = `mtime`; no descarga ni escribe ACF; al quitar quedan tokens, tickets del registro y SteamIDs; tablas y `lua_State` sin lock. | `bettersteamtools.md` §2.9 |
| opensteamtool-cn | como BST, en `config\lua` (no `stplug-in`). | `opensteamtool-cn.md` §2.9 |
| LuaTools | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | n/a: el proyecto ya no existe (2026-10-08); su doc se retiró sin releer | — |
| slsteam-moon | entrada `<appid>.lua` por nombre de fichero (escrito por luatools-moon/Lumen), `luaappids.yaml`, API privada `install\|app\|lib`; escribe `appinfo.vdf` (transaccional), pares `picsbuffer_*`, claves, manifests, `config.vdf`, fechas de inclusión; quita renombrando la caché (`.forgotten.*`) y retirando de la UI; deja claves, manifests, entrada en appinfo, contenido y `.acf`; bibliotecas por `libraryfolders.vdf` e índice; carpeta: Steam salvo síntesis. | `slsteam-moon.md` §2.9 |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 10. Credenciales y proveedores

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | Hubcap (clave, `/user/stats`, borrable), Ryuu (sesión de Discord por CDP o SQLite del CEF, verificada con `data-user-id`, viva cada hora), LuaTools (Supabase con refresh), Web API de Steam (apagada). Sin ninguna: no se añade; actualizar sigue por depotcache, archivo y luastools. | `nosotros.md` §2.10 |
| SLSsteam | solo una sesión de Steam; opcionales `AppTokens`, `CDKeys`, SteamIDs (`DenuvoGames`, `SteamIdOverride`) y tickets; caducidad no detectada (tickets reutilizados sin mirar antigüedad); `ticket-grabber`/`schema-grabber` piden usuario y contraseña en argv + Steam Guard. | `slssteam.md` §2.10 |
| SteaMidra/SFF | Hubcap (`smm`, validada al arrancar, diálogo "Remove Key"), Ryuu reseller/premium, DepotBox (+plan), ManifestHub2 (24 h), Web API key opcional (si no, la **embebida**), token DepotBox embebido, Steam **anónimo** siempre, Google OAuth, `sudo` en Deck. Sin ninguna: Free Providers, LuasTools, espejos, BD, nativo con manifest local, DDMod, headcrab, cracks. | `steamidra.md` §2.10 |
| LumaCore | ninguna propia: sesión de Steam del usuario (y su ticket de la app 7), proveedores de códigos sin clave, SteamIDs de donantes incrustados, tickets que traiga el `.lua`; la URL de ETicket no tiene efecto. | `lumacore.md` §2.10 |
| BetterSteamTools | ninguna; usa sin autenticarse feed, archivo, proveedor de códigos y stats; la cuenta del usuario es la que **aporta** (donación). | `bettersteamtools.md` §2.10 |
| opensteamtool-cn | ninguna; seis proveedores sin autenticación. | `opensteamtool-cn.md` §2.10 |
| LuaTools | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | n/a: el proyecto ya no existe (2026-10-08); su doc se retiró sin releer | — |
| slsteam-moon | sin credenciales (CM anónimo propio, archivo sin auth, `api.steamcmd.net`, owner de logros público); usa la sesión real del usuario para donar códigos (opt-out); TTL de appinfo 5 min, `change_number`, circuitos; sin red: pares validados, store y updates suprimidas; `ticket-grabber` sin empaquetar. | `slsteam-moon.md` §2.10 |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 11. Mantenimiento propio

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | QAM + Settings de Decky (pestaña Dev siempre visible); CLI `setup.sh`/`steamidra_lite.py`. Update propio: zip a `~/Downloads`. Componentes: versión del `.so` en disco (CloudRedirect, lumalinux), tag grabado (SLSsteam), GitHub con caché 6 h, aplicar = `setup.sh` entero. Salud: `status.json`, `gmrc.json`, crash guard, pin de headcrab; logs en lumalinux, SLSsteam y Decky. CI: tests, autotest de rederivación, smoke test nunca verde. | `nosotros.md` §2.11 |
| SLSsteam | YAML con hot reload (inotify, `IN_CLOSE_WRITE\|IN_MOVED_TO`; claves faltantes → toast, desconocidas ignoradas); fichero de comandos `/tmp/SLSsteam.API` (7 comandos; `API: no` en el YAML instalado; cualquier proceso del usuario puede escribirlo; roto 09-28→10-01); plugins Lua sin sandbox (`Plugins: no`, 0700 forzado, hot reload sin recrear estado en release); toasts por `notify-send`. Sin auto-update (`update.cpp` solo baja hashes). Versión = `VERSION` embebida (no sube en cada tag) + rama/commit en el log. Releases 7z/zip + PKGBUILD + Nix, sin strip. | `slssteam.md` §2.11 |
| SteaMidra/SFF | GUI web (Chromium sin sandbox, bridge sin autenticación) + Qt + CLI + instalador sh; self-update por GitHub releases sin firma; SLSsteam "versión" = `unknown` → headcrab cada hora; LumaCore por tag con caché 6 h; .NET 9 auto; gbe_fork por `version.txt`; resto fijo en `third_party/`. Logs `debug.log`/`crash.log`; no lee el estado de SLSsteam. CI por tag sin checksums. | `steamidra.md` §2.11 |
| LumaCore | sin UI: `lumacore.toml` (recarga en handshake), `status.json` atómico (dice `V36` en el binario `V37`), popup de diagnóstico; lo instala y actualiza SteaMidra por tag; TOML cacheada nunca se refresca; logs por módulo solo en Debug. | `lumacore.md` §2.11 |
| BetterSteamTools | sin UI: `.toml` en caliente, avisos `MessageBox`, `manifest_probe.txt`; **auto-update on** desde la rama `updates` (raw → jsDelivr → git.lua.tools), versión distinta = update, SHA del mismo puntero (sin firma), reinicio por PowerShell; compilable sin updater. | `bettersteamtools.md` §2.11 |
| opensteamtool-cn | sin UI ni auto-update (lo actualiza Fluent-Steam-Lua); el `.toml` de ejemplo repite `[manifest] url` y no parsea: la DLL sigue con los defaults sin avisar. | `opensteamtool-cn.md` §2.11 |
| LuaTools | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | n/a: el proyecto ya no existe (2026-10-08); su doc se retiró sin releer | — |
| slsteam-moon | YAML autorreparado con hot reload (3 rutas, ráfagas coalescidas); API privada de un comando; toasts EN/PT con throttle + cola JSON para Lumen; CEF a puerto libre salvo Decky; sin Lua; `autofix.sh` reinstala el último release (`-lumen.zip`); catálogos de patrones autoactualizados por `pattern-refresh`; versión embebida fija + build-ids; `~/.SLSsteam.log` en append; 97 tests y sondas en Steam real. | `slsteam-moon.md` §2.11 |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 12. Proyecto

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | SteamOS/Deck; CachyOS en curso; Flatpak no. GPL-3 (lumalinux, con `update.cpp` AGPL de SLSsteam), LumaDeck sin LICENSE; releases por tag. Terceros: GitHub, AceSLS, Selectively11, Valve, api.steamcmd.net, SteamDB, Hubcap, Ryuu, luastools, lua.tools, proveedores GMRC (tabla en `nosotros.md` §4.2). Sin telemetría; TLS sin verificar como último recurso; riesgo de detección el de SLSsteam. | `nosotros.md` §2.12 |
| SLSsteam | Linux x86 32-bit en `steam` (nativo, Flatpak, SteamOS/Deck); AGPL-3.0; un mantenedor, ~1 tag cada 4 días, historia reescrita el 07-21, rama `dev` activa; terceros GitHub/jsDelivr, tienda de Steam, `curl` y `notify-send` del sistema, `/proc`; sin telemetría; borra el control parental siempre; detección: detours en `steamclient.so`, paquetes CM modificados y descartados, stats de otros usuarios desde la sesión propia. | `slssteam.md` §2.12 |
| SteaMidra/SFF | Windows + Linux x86_64 (AppImage glibc 2.39), Flatpak parcial, sin macOS. GPL-3 (Midrag); fork `drappula` (mallusrgreat, 88 commits, 7 releases 09-05 → 10-03; `AGENTS.md`: agente IA); upstream parado 08-21. Depende de ~25 servicios (headcrab/jsDelivr/SteamTracking, luastools, Hubcap, Ryuu, DepotBox, ManifestHub2, KoriaPolis ×2, MigoReleases, GitHub ×12 repos, steamcmd.net, morrenus, revobd, pixeldrain, Google). Telemetría local; contribución de claves ON de facto; `curl \| bash` ×2 + `bash` de `main` filtrado; sin tests. | `steamidra.md` §2.12 |
| LumaCore | Windows x64. GPL-3 (Midrag); fork `drappula` desde 09-04 (wtfseanscool, michelegoku3/Aether, mallusrgreat CI); sin tests; release sin checksums en cada push. Depende de MigoReleases (una persona), gitflic, tres proveedores de códigos; build con `lua.org` sin TLS y Detours sin fijar. Sin telemetría. Riesgo de detección alto: reescribe mensajes del CM, forja tickets, apaga la nube; estado de Lua sin mutex. | `lumacore.md` §2.12 |
| BetterSteamTools | Windows; GPL-3.0; un mantenedor (mendy-tools = operador de LuaTools/luastools), 18 commits sobre OST (08-11 → 09-21), parado desde v1.0.4. Terceros: steam-monitor (origen oculto), jsDelivr, git.lua.tools, luastools.xyz, opensteamtool.com, wudrm, steam.run. Privacidad: SHA del cliente en cada arranque, appids de stats, `HEAD` por depot poseído pedido, códigos de cada descarga; Lua sin sandbox (`os`, `io`). | `bettersteamtools.md` §2.12 |
| opensteamtool-cn | Windows; GPL-3.0; un mantenedor (huanyuejue), ráfagas (25 commits desde 08-11, tres el 10-09); front-end Fluent-Steam-Lua. Terceros: feed `OpenSteam001` (parado), jsDelivr, seis proveedores, `stats.opensteamtool.com`. No dona ni sube códigos; cambia el `owner_id` de los prestados hacia Valve. | `opensteamtool-cn.md` §2.12 |
| LuaTools | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | n/a: el proyecto ya no existe (2026-10-08); su doc se retiró sin releer | — |
| slsteam-moon | Linux 32-bit, sin Flatpak, Deck por PATH; AGPL-3.0 + Steamless CC BY-NC-ND; un mantenedor (`unplausible`), 381 commits desde 05-31, 5 tags, `version.txt` congelado, zip 2.9 resubido cuatro veces; terceros luastools, Valve CM/CDN, steamcmd, steam-monitor, GitHub/jsDelivr, Wine; donación de códigos por defecto; detección: escribe `appinfo.vdf` y `config.vdf`, package 0, paquetes CM mutados, tickets derivados, parche parental. | `slsteam-moon.md` §2.12 |
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
| 9, 11 | SLSsteam | `install\|app\|lib`, `uninstall\|app`, `setcompat` por `/tmp/SLSsteam.API` | descartado por ahora: canal sin autenticación, off por defecto y roto en 20260930144343; Proton lo fijamos en `localconfig.vdf` | 2026-10-08 |
| 9 | slsteam-moon | retirar un juego de la biblioteca sin reiniciar (`LibraryRemoval`: dos detours en `steamui.so`, `ownershipFlags=0` + `MarkAppChange`) | aparcado hasta terminar las relecturas; nosotros solo hacemos aparecer (`nosotros.md` §2.9) | 2026-10-08 |
| 3, 4 | slsteam-moon | cuarentena de depots DLC cuya clave no descifra (3 chunks `Unpack failed`, persistida por FNV de la clave) | hook rechazado 2026-08-18; verificación (`content_log.txt`, si nos ocurre) sigue pendiente | 2026-10-08 |
| 4 | slsteam-moon | claves observadas de respuestas legítimas y sustitución de la respuesta 5439 | hook de mensaje rechazado 2026-06-23 (coexistencia); captura en el passthrough pendiente desde 2026-06-23 | 2026-10-08 |
| 4 | slsteam-moon | síntesis de `depots`/`launch`/`installdir` para apps token-locked | cerrado 2026-07-05: `--token`/`add_game_token` | 2026-10-08 |
| 1 | slsteam-moon | crash guard con latch al primer crash si cambió `steamclient.so` (y borrado de `appinfo.vdf`) | aparcado; el nuestro latchea tras N crashes sin mirar el cliente | 2026-10-08 |
| 1 | slsteam-moon | catálogo de patrones firmado (Ed25519, solo RVAs) y shim root en el PATH | rechazados 2026-08-18 y 2026-09-01 (feed sin firma deliberado; nada de root) | 2026-10-08 |
| 7 | slsteam-moon | esquema de logros con un owner fijo (sin reseñadores) | aparcado; dato para cuando muera el endpoint de reseñas (2026-10-22) | 2026-10-08 |
| 4, 10 | slsteam-moon | donación de códigos de manifest con la sesión real (`Donate`, on por defecto) | rechazado por el usuario 2026-09-28: no se dona | 2026-10-08 |
| 6 | slsteam-moon | Steamless integrado en `LaunchApp` (opt-in por opción de lanzamiento) | sin cambio 2026-09-22: por botón en LumaDeck | 2026-10-08 |
| 11 | slsteam-moon | `autofix.sh` (`curl \| bash` que reinstala el último release) y 97 tests + sondas de hooks en Steam real | anotado; comparar con `probe-steam.yml` cuando se toque el CI | 2026-10-08 |
| 1 | SteaMidra | headcrab como instalador (rebaja y congela el cliente de Steam, sustituye `steam.sh`), parche de `steam.sh` y de `/usr/bin/steam-jupiter` con `sudo`, hash fix por `curl \| bash`, reinstalación cada hora | rechazado 2026-09-14 (F1: nunca `/usr`, nunca `steamos-readonly disable`, nunca SIGKILL, `steam.sh` vanilla); confirmado hoy con `headcrab.sh` | 2026-10-08 |
| 4 | SteaMidra | base pública de claves `KoriaPolis/Steam-Depot` (369k entradas, 190k con clave; descargada cada 6 h) | pendiente condicionado desde 2026-09-12: "no se implementa hasta ver en un log real el caso DLC nuevo sin clave y Hubcap no la trae"; hoy responde | 2026-10-08 |
| 4 | SteaMidra | descargador nativo de depots propio (sesión anónima, CDN, AES, VZ/VSZTD/ZIP, SHA-1) y DDMod | descartado por diseño: Steam descarga (`nosotros.md` §2.4) | 2026-10-08 |
| 5 | SteaMidra | ACF a mano 0444 con `buildid`/`TargetBuildID`, `AutoUpdateBehavior 0` y cola de 7 días | rechazado 2026-09-12 (LumaDeck 0.8.0 `self_heal_acf_build`: Steam escribe el ACF) y 2026-09-14 (P26) | 2026-10-08 |
| 5 | SteaMidra | versiones antiguas por historial (CM/morrenus/árbol GitHub/tokens), DepotBox build-details e importación de HTML de SteamDB | no aplica: nuestro freeze no hace rollback (`nosotros.md` §2.5) | 2026-10-08 |
| 6 | SteaMidra | Fix Game completo (Goldberg/ColdClient/ColdLoader, SteamAutoCrack, CrakFiles, DLC unlockers) | aparcado hasta terminar las relecturas; nuestros fixes (lua.tools, Steamless, Goldberg, EOS) por botón | 2026-10-08 |
| 10 | SteaMidra | validación de claves de proveedor al arrancar con diálogo "Remove Key" | anotado (`_HUBCAP_ATTEMPTS` sin diálogo); aparcado | 2026-10-08 |
| 1, 11 | LumaCore | `status.json` y caché escritos por `.tmp` + rename | pendiente: es el arreglo de `nosotros.md` §4.1-24 (`lumacore.md` §4.1-3) | 2026-10-08 |
| 4, 9 | LumaCore | lectura del `.lua` ejecutándolo: nombres sin distinguir mayúsculas, varias sentencias por línea | pendiente: nuestras regex son más estrechas (`lumacore.md` §4.1-1) | 2026-10-08 |
| 4 | LumaCore | segunda y tercera costura de clave por `ConfigStoreGetBinary` | aparcado desde 2026-08-13: solo si un fallo de clave no lo cubre DepotKey (`lumacore.md` §4.4 D2) | 2026-10-08 |
| 9 | LumaCore | baja de biblioteca en caliente al borrar el `.lua` (`MarkAppChange` en SteamUI) | aparcado con la fila de slsteam-moon de la función 9 | 2026-10-08 |
| 6 | LumaCore | tickets forjados con la app 7, ruta 480 con payload EOS, ruta SteamStub | descartado 2026-08-13: capa de mensajes/IPC de SLSsteam y técnica Windows (`lumacore.md` §4.4 D0) | 2026-10-08 |
| 7 | LumaCore | stats con SteamID de donante reescritos en el cable | descartado 2026-08-13: lo cubre `sls_achievement_unblock` (`lumacore.md` §4.4 D5) | 2026-10-08 |
| 8 | LumaCore | apagar Steam Cloud para juegos no poseídos | descartado 2026-09-12: la nube la lleva CloudRedirect (`lumacore.md` §4.4 D6) | 2026-10-08 |
| 4 | BetterSteamTools | precarga de manifests en `depotcache` en el momento en que Steam los pide (`YldLoadDepotManifest`), Workshop incluido | descartado: la precarga en disco ya es nuestra (LumaDeck) y los gids que no conocemos los cubre GMRC (`bettersteamtools.md` §4.3) | 2026-10-09 |
| 4, 10 | BetterSteamTools | donación de códigos con la sesión del usuario, incluida la captura de todas sus descargas (on por defecto) | rechazado: no se dona (`bettersteamtools.md` §4.4 D7) | 2026-10-09 |
| 5 | BetterSteamTools | fallar `BuildDepotDependency` cuando un refresco de appinfo devuelve cero depots | aparcado: BuildDep apagado; referencia si el síntoma vuelve (`bettersteamtools.md` §4.1-6) | 2026-10-09 |
| 6 | BetterSteamTools | tickets del registro, acuñado por nonce, canje `bst://`, clave legacy en local, `[[inject]]` | descartado: capa de mensajes/IPC de SLSsteam y técnicas Windows (`bettersteamtools.md` §4.4 D0); los tickets van con la fila de SLSsteam | 2026-10-09 |
| 7 | BetterSteamTools | stats con SteamID de donante en el cable | descartado: lo cubre `sls_achievement_unblock` (`bettersteamtools.md` §4.4 D3) | 2026-10-09 |
| 9 | BetterSteamTools | baja de biblioteca en caliente (`MarkAppChange`) y fecha de compra por `mtime` del `.lua` | aparcado con la fila de slsteam-moon de la función 9 | 2026-10-09 |
| 4 | opensteamtool-cn | proveedor `SDM` (`steamapi.993499094.xyz`, mismo código que 20770407) | aparcado desde 2026-09-29: otra puerta al mismo origen (`opensteamtool-cn.md` §4.1-1) | 2026-10-09 |
| 4 | opensteamtool-cn | enfriamiento de 60 s por proveedor tras un fallo | sin acción: referencia si la cascada paga timeouts en serie (`opensteamtool-cn.md` §4.1-2) | 2026-10-09 |
| 1 | opensteamtool-cn | enganchar una copia del cliente y redirigir módulos (integridad de Denuvo/RE Engine) | no aplica: Windows; bajo Proton el juego no ve `steamclient.so` | 2026-10-09 |
| 2, 6 | opensteamtool-cn | family sharing sin bloqueo del prestador; ventana de Denuvo por plazos; presencia como juego no-Steam | descartado: capa de mensajes/IPC de SLSsteam (`bettersteamtools.md` §4.4 D0) | 2026-10-09 |

### 3.2 Nosotros sí, ellos no

| Función | Qué | Quién más lo tiene | Notas |
|---|---|---|---|
| 4 | claves de depot (`keys.txt` + `config.vdf`), manifests por proveedor, códigos GMRC | SteaMidra: claves en `config.vdf` + lua + BD pública, manifests por LuasTools/GitHub/ManifestHub2/Hubcap, **sin** códigos externos (solo sesión anónima); SLSsteam: no (solo en su rama `update`); slsteam-moon: claves del `.lua` + observadas y manifests del archivo luastools (único origen), pero **sin** códigos desde 09-21; resto pendiente; LumaCore: claves del `.lua` por tres caminos y códigos por el cable con tres proveedores **sin** comprobar en el CDN; BetterSteamTools: claves del `.lua`, manifests del archivo luastools (solo magia) y códigos de **un** proveedor sin CDN | `slssteam.md` §2.4, `slsteam-moon.md` §2.4, `lumacore.md` §2.4, `bettersteamtools.md` §2.4 |
| 2 | package-0 en memoria + `NotifyLicensesUpdated` | SteaMidra: no (solo `AdditionalApps` de SLSsteam); SLSsteam: no (spoof por app); slsteam-moon: sí (`LoadPackage` + `Grow`, reconcile `LicensesUpdated_t`, refresco en caliente); LumaCore: sí (`LoadPackage`/`GetPackageInfo` + `Grow`, `MarkLicenseAsChanged` + `ProcessPendingLicenseUpdates`, reconcile completo); BetterSteamTools: sí (`GetPackageInfo` + `Grow`, `MarkLicenseAsChanged` + `ProcessPendingLicenseUpdates`, altas y bajas en caliente) | `slssteam.md` §2.2, `slsteam-moon.md` §2.2, `lumacore.md` §2.2, `bettersteamtools.md` §2.2 |
| 5 | updates por proveedor (`pins.py`, `api.steamcmd.net`, Fix Update) | SteaMidra: ACF 0444 + `DisableUpdates` + pins; "Update" manual = rebajar; SLSsteam: solo `ManifestIds` + `DisableUpdates`; slsteam-moon: `ManifestPins` + `AutoUpdateApps` + supresión con proveedores offline, sin pase de proveedores propio; LumaCore: pin por `BuildDepotDependency`, sin pase de proveedores; BetterSteamTools: igual, más la guarda contra depots vacíos | `slssteam.md` §2.5, `slsteam-moon.md` §2.5, `lumacore.md` §2.5, `bettersteamtools.md` §2.5 |
| 6 | fixes (lua.tools, Steamless, Goldberg, EOS) | SteaMidra: kit completo salvo EOS; SLSsteam: ninguno; slsteam-moon: solo Steamless (integrado); LumaCore: online-fix con payload EOS y tickets en el cliente, sin Steamless ni Goldberg; BetterSteamTools: tickets y ruta 480 en el cliente, sin Steamless, Goldberg ni EOS | `slssteam.md` §2.6, `slsteam-moon.md` §2.6, `lumacore.md` §2.6, `bettersteamtools.md` §2.6 |
| 7 | sincronización de logros entre máquinas (CloudRedirect) | SteaMidra: ninguna (SLScheevo externo); SLSsteam: ninguna; slsteam-moon: cloudredirect-moon (externo); LumaCore: ninguna (donantes, sin almacén); BetterSteamTools: solo si carga CloudRedirect | `slssteam.md` §2.7, `slsteam-moon.md` §2.7, `lumacore.md` §2.7, `bettersteamtools.md` §2.7 |
| 11 | UI (QAM, Settings), update propio y de componentes, estado | SteaMidra: GUI web + CLI, self-update sin firma, versión de SLSsteam desconocida; SLSsteam: YAML + fichero de comandos + Lua; slsteam-moon: Lumen (sidecar CDP) + `autofix.sh`; ninguno comprueba componentes; LumaCore: sin UI, `status.json`, lo actualiza SteaMidra; BetterSteamTools: sin UI, avisos, auto-update sin firma | `slssteam.md` §2.11, `slsteam-moon.md` §2.11, `lumacore.md` §2.11, `bettersteamtools.md` §2.11 |

### 3.3 Fuentes retiradas

| Función | Qué | Cuándo | Motivo |
|---|---|---|---|
| 4 | `gmrc.wudrm.com` y `manifest.steam.run` como proveedores de códigos de manifest en slsteam-moon | 2026-09-21 (`c1b5e15`) | "superseded by the archive and no longer reachable": el archivo luastools sirve el manifest entero; moon dejó de generar e inyectar códigos | 
| 4 | `steam.run`, `wudrm`, `manifest.opensteamtool.com` (y su variante XOR) y toda la familia de request codes externos en SteaMidra | 2026-09-12 (`9033611`) | "the public GMRC mirrors … now answer 403/404 everywhere"; queda solo el código de la sesión anónima del descargador nativo | 
| 4 | `fylsdy/ManifestHub/main/depotkeys.json` (claves "trionine" de Free Providers) | 404 desde 2026-09-14 (medido; sigue 404 el 2026-10-08) | el código lo sigue pidiendo; la cadena gratis depende de `KoriaPolis/Steam-Depot` | 
| 1 | `KoriaPolis/Steam-Auto-PT` (rama `pattern`) como feed de patrones de LumaCore | 2026-09-30 (`c0d0537`) | parado desde el 2026-08-19; la DLL pasa a `michelegoku3/MigoReleases` | 
| 1 | `OpenSteam001/steam-monitor` (rama `pattern`) como feed de patrones de OpenSteamTool | 2026-08-11 en BST (`f721ebe`, hoy fuera de la cadena) | sin TOML desde el 2026-09-06; BST lee `madoiscool/steam-monitor` | 

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
| SteaMidra/SFF (drappula) | `steamidra.md` | 2026-10-07 | **hecha 2026-10-08** desde el código (`origin/main@4159300`, v6.9.0) y `headcrab.sh` de hoy; `steamidra-linux-analysis.md` absorbido (mediciones en §5.2, afirmaciones en §5.1, vectores y hallazgos en §4.4); las partes SFF-app de `lumacore-findings.md` también | 2 |
| LumaCore (drappula) | `lumacore.md` | 2026-10-07 | **hecha 2026-10-08** desde el código (`drappula/LumaCore@33c36e9`, V37) y el feed `MigoReleases@61988bc`; `lumacore-findings.md` absorbido (mediciones en §5.2, afirmaciones en §5.1, decisiones en §4.4) | 3 |
| BetterSteamTools (+ OpenSteamTool) | `bettersteamtools.md` | 2026-10-07 | **hecha 2026-10-09** desde el código (`main@4747385`, v1.0.4), la rama `updates@bfa812e` y el feed `madoiscool/steam-monitor` (`pattern@6fd7961`); `bettersteamtools-findings.md` absorbido y borrado (mediciones en §5.2, afirmaciones en §5.1, decisiones en §4.4; Fluent-Steam-Lua y SteamToolbox quedan fuera de momento) | 4 |
| opensteamtool-cn (huanyuejue) | `opensteamtool-cn.md` | 2026-10-07 | **hecha 2026-10-09** contra BST desde el código (`main@d2a18f8`, 25 commits propios desde `c3d89e9`) y el feed `OpenSteam001/steam-monitor`; sus deltas de `bettersteamtools-findings.md` absorbidos | 4b |
| LuaTools | `luatools-app-analysis.md` | 2026-10-07 | pendiente | 4c |
| ASSella | `assella-analysis.md` | 2026-10-07 | pendiente | 5 |
| SLSDeck | `slsdeck-analysis.md` | 2026-10-05 | pendiente | 6 |
| SteamFlipper | (ninguno: `steamflipper-analysis.md` borrado el 2026-10-08) | 2026-09-28 | **retirado**: el proyecto ya no existe | — |
| slsteam-moon | `slsteam-moon.md` | 2026-10-07 | **hecha 2026-10-08** desde el código (`origin/slsteam-moon@f50f28e`); `slsteam-moon-findings.md` absorbido (mediciones en §5.2, portables en §4.4) | 8 |
| plugins de SLSsteam | `slssteam-plugins-analysis.md` | 2026-09-29 | pendiente | 9 |
| CloudRedirect | `cloudredirect.md` | 2026-10-07 (2.6.6) | pendiente | 10 |
| nosotros | `nosotros.md` (+ `RESEARCH.md`, `maintenance.md`, docs de LumaDeck) | — | **hecha 2026-10-08** desde el código (lumalinux `df22459`, LumaDeck `c4776ce`); `method.md` y `owned-games-guide.md` absorbidos | 0 |
