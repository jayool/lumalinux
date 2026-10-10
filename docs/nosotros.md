# nosotros — lumalinux + LumaDeck, y cómo usamos SLSsteam

Leído desde el código el 2026-10-08. Sustituye a `method.md` y a
`owned-games-guide.md`; sus mediciones con fecha están en §5. `RESEARCH.md`
(diario de ingeniería) y `maintenance.md` (runbook) siguen vivos y se enlazan
donde toca. Los docs de diseño ya ejecutados están en `docs/design/`.

---

## §0 Ficha

| | |
|---|---|
| **Qué es** | Tres piezas que se instalan juntas en un Steam de Linux: **lumalinux**, una biblioteca de 32 bits (`liblumalinux.so`) que se carga dentro del cliente Steam y parchea cinco funciones de `steamclient.so`, más su instalador `setup.sh` y la herramienta `steamidra_lite.py`; **LumaDeck**, un plugin de Decky Loader (backend Python como root + frontend React) que es la interfaz y la lógica de añadir, actualizar y quitar juegos; y **SLSsteam** (de AceSLS), que instalamos tal cual sale de su release y configuramos, y que pone la propiedad falsa de las apps. CloudRedirect (Selectively11) va en la misma instalación para cloud saves y logros; tiene su propio doc. |
| **Repos y commits leídos** | `jayool/lumalinux` `df22459` (release v0.22.2 = `c17536e`, la diferencia es la propia matriz); `jayool/LumaDeck` `c4776ce` (= v0.11.0). SLSsteam: config y fuentes de `AceSLS/SLSsteam` `main@049bbdd` (`config_default.hpp`), release instalada en la Deck `20261001163836`. |
| **Plataforma** | SteamOS (Steam Deck, Game Mode y Desktop). Port a CachyOS en curso (`cachyos-port.md`). Arch, Debian, Void contemplados en `setup.sh`. Steam Flatpak **no** soportado (`setup.sh:31`; LumaDeck solo lo detecta, `platform_info.py:252-278`). |
| **Licencia y quién** | lumalinux: GPL-3.0-only (cabeceras SPDX en `src/`), con `src/update.cpp` tomado de SLSsteam (AGPL-3.0, `update.cpp:1-8`). LumaDeck: "Fork of DeckTools" (`package.json`), autor jayool; sin `LICENSE` en la raíz (leído del código, no de docs). SLSsteam: AGPL-3.0, AceSLS. Releases por tag con CI en los dos repos (`build.yml`, `release.yml`). |
| **Relación con los demás programas** | Base técnica = SLSsteam (propiedad) + lumalinux (claves, package-0, códigos de manifest). Referencia histórica de lumalinux = LumaCore (los hooks se alinearon a él, `main.cpp:9-22`; `RESEARCH.md` §11). `steamidra_lite.py` nació de SteaMidra (parser del lua "verbatim", `steamidra_lite.py:106-144`). LumaDeck nació de DeckTools. |

**Lo que cambia respecto a los docs anteriores**, en una línea cada uno:

- GMRC está **activo por defecto** desde v0.21.0 (`main.cpp:170-174`); `method.md` lo daba como opt-in.
- Steam **no lee** `config/depotcache/` (medido 2026-09-11); `manual-install.md` decía lo contrario.
- El pin de versión vivo es **`ManifestIds` de SLSsteam**, no `keys.txt`+BuildDep (BuildDep está OFF por defecto, `main.cpp:175-186`); `keys.txt` sigue llevando gid y tamaño que hoy nadie lee en runtime.
- El hash SafeMode de lumalinux es **advisory**: un toast, no un bloqueo (`main.cpp:107-126`); `status.json.blocked` es siempre `null` porque `Status::SetBlocked` no se invoca (`main.cpp:270-276`).
- Desde `4f8579c` lumalinux **sí retira** del vector de package-0 los ids que salen de `keys.txt` (`load_package_hook.cpp:199-251`); `owned-games-guide.md` §3.D decía "hoy no".
- El `.so` desktop y el de Deck **no son el mismo binario** desde 2026-09-22 (medido); `cachyos-port.md` lo daba por hecho.

---
## §1 Mapa del código

Cobertura por fichero: cada fichero fuente con la función de la matriz a la que
sirve (números de §2). Ningún fichero quedó sin función. Las librerías
vendorizadas (`include/libmem`, `include/yaml-cpp`) y las dependencias en
`backend/deps/` se anotan sin leer.

### 1.1 lumalinux — la biblioteca (`src/`, `res/`, `CMakeLists.txt`)

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `CMakeLists.txt` | 174 | build `-m32`; versión del tag (`LUMALINUX_VERSION_OVERRIDE`); lee `res/version.txt` como `LUMALINUX_SAFEMODE_VERSION`; opción `LUMA_NO_UPDATE` (ON por defecto fuera de release); `-Wl,--no-undefined`; libmem + yaml-cpp estáticos | 1, 11, 12 |
| `res/version.txt` | 1 | grupo activo de hashes SafeMode (`20260922150000`) | 1, 11 |
| `res/updates.yaml` | 78 | feed de hashes SHA-256 de `steamclient.so` verificados, por grupo | 1, 11 |
| `res/rvas/<sha256>.yaml` | 16-20 | feed de RVAs por build: `hooks` (DepotKey, GMRC, ShaderDepot, Reconcile; BuildDep no se publica, es diagnóstico) y `finder` (`cache_global_disp`, `cache_root_off`, `cache_nodes_off`, `got_rva`) | 1 |
| `src/main.cpp` | 485 | entradas (`la_preinit`/`la_objopen`, constructor), preinit, hash advisory, tabla de hooks, gate crítico (DepotKey), arranque del finder, parches de convivencia, `status.json` | 1, 2, 7, 11 |
| `src/proc_filter.cpp` | 49 | solo actúa en `/proc/self/comm` ∈ {`steam`, `steamwebhelper`}; `LUMA_PROCESS_ANY`; fail-open | 1 |
| `src/log.cpp` | 164 | `~/.cache/lumalinux/lumalinux.log`, nivel `LUMA_LOG_LEVEL`, toast por `notify-send` | 11 |
| `src/status.cpp` | 142 | `$XDG_RUNTIME_DIR/lumalinux/status.json` (una vez) y `gmrc.json` (en cada lookup) | 11, 4, 5 |
| `src/key_store.cpp/.hpp` | 345+119 | `keys.txt` y `retired_keys.txt`: tres formatos de línea, mapa, watcher inotify, `Lookup` con fallback a retiradas | 4, 2, 3, 9 |
| `src/license_reconcile.cpp/.hpp` | 152+68 | resuelve `CUser::NotifyLicensesUpdated` (feed → patrón → anchor), kill-switch, despertar del finder, llamada directa | 2, 9, 1 |
| `src/reconcile_anchor*.{cpp,hpp}` | 63+140+22 | tercer localizador de Reconcile: `push 0x7d; push eax; call` + lectura de `this` + `jle`, único o 0 | 1 |
| `src/patterns.cpp/.hpp` | 288+253 | seis patrones de bytes y el sigscan sobre el r-x de `steamclient.so`; "único o nada" | 1 |
| `src/rtti.cpp/.hpp` | 388+61 | walk RTTI (nombre → type_info → vtable) y slot por nombre de método | 1 |
| `src/rva_feed.cpp/.hpp` | 252+64 | descarga `res/rvas/<sha256>.yaml` de GitHub, caché en `~/.cache/lumalinux/rvas/`, traduce RVAs | 1 |
| `src/vaddr_xlate.cpp/.hpp` | 100+53 | file vaddr → dirección runtime vía PT_LOAD + `/proc/self/maps` | 1 |
| `src/eh_frame.cpp/.hpp` | 250+46 | límites de función desde `.eh_frame_hdr` (fail-closed) | 1 |
| `src/gmrc_xref.cpp`, `gmrc_xref_core.hpp` | 181+179 | localizador por xref de string (nombre del job GMRC; `FindFunctionByString` para cualquier hook) | 1 |
| `src/gmrc_store.hpp` | 474 | cascada de proveedores de request code, ritmo, backoff, caché, verificación en el CDN, sonda de vida | 4, 10 |
| `src/lmhook.cpp/.hpp` | 164+20 | detour con `LM_HookCode`, arreglo de `get_pc_thunk` en el trampolín, relocación de `jmp` encadenado (si SLSsteam ya hookeó) | 1 |
| `src/libcurl_pin.cpp` | 169 | interpone `dlopen()` para que CloudRedirect y lumalinux carguen el libcurl del sistema | 1, 8, 4 |
| `src/sls_achievement_unblock.cpp/.hpp` | 363+39 | repunta dos `call CUser::isSubscribed` en `SLSsteam.so` a un guard `subscribed && !added`; captura el `CUser*` | 7, 2 |
| `src/cr_stats_fix.cpp/.hpp` | 299+64 | stub que interpone `StatsHandlers::HandleGetUserStats` de CloudRedirect y vacía la respuesta "crc 0" | 7, 8 |
| `src/sha256.cpp`, `src/curl.cpp` | 151+138 | SHA-256 propio; libcurl por `dlopen("libcurl.so.4")`, `CURLOPT_NOSIGNAL`, timeouts | 1, 4 |
| `src/update.cpp/.hpp` | 204+22 | SafeMode: descarga `updates.yaml`, caché `.updates.yaml`, compara hash (de SLSsteam, AGPL) | 1, 11 |
| `src/steam_types.hpp`, `globals.*`, `version.hpp.in` | 40+13+16 | layouts `CUtlVector`/`DepotEntry`; módulo steamclient; versión | 2, 5, 11 |
| `src/hooks/depot_key_hook.cpp` | 210 | hook del accessor KeyValues `…\Depots\<id>\DecryptionKey`; sirve la clave de KeyStore | 4, 1 |
| `src/hooks/depot_dependency_hook.cpp` | 198 | hook `BuildDepotDependency`: PATCH del gid en `pDepotInfo` (OFF por defecto) | 5, 2 |
| `src/hooks/load_package_hook.cpp/.hpp` | 289+88 | `InjectDepots`/`RetireDepots` en el `AppIdVec` del package 0; el hook `LoadPackage` solo diagnóstico | 2, 3, 9 |
| `src/hooks/package_zero_finder.cpp/.hpp` | 837+39 | hilo que localiza `CPackageInfoCache`, camina al package 0, inyecta y retira, dispara el reconcile | 2, 3, 9, 1 |
| `src/hooks/gmrc_hook.cpp` | 125 | hook `BYieldingGetManifestRequestCode`: inyecta el código de la cascada | 4, 1 |
| `src/hooks/shader_depot_hook.cpp` | 148 | hook `GetShaderCacheDepot`: 0 para juegos sin clave o sin proveedor vivo | 9, 4 |

### 1.2 lumalinux — instalador, herramientas, CI, devcontainers

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `setup.sh` | 1232 | instala y ordena la pila (SLSsteam por `LD_AUDIT`, CloudRedirect + lumalinux por `LD_PRELOAD`), wrapper, guard anti crash-loop, cobertura Desktop y Game Mode, config de SLSsteam, .NET 9, app de CloudRedirect, `--uninstall` | 1, 2, 6, 8, 11, 12 |
| `downgrade.sh` | — | escape hatch "Fix in Desktop": baja el cliente de Steam al build fijado por headcrab desde su espejo y escribe el pin `steam.cfg` firmado (§2.1 "Cómo se recupera") | 1, 11 |
| `install.sh` | — | instalador **legacy** de la era headcrab (parchea el `steam.sh` de headcrab); sin uso desde el modelo wrapper | — |
| `tools/steamidra_lite.py` | 1462 | "añadir juego": manifests → depotcache, `AdditionalApps`/`AppTokens`/`ManifestIds`, `keys.txt`, `config.vdf`, `.acf`, `stplug-in`; `--pin-installed`/`--unpin`/`--set-pin`/`--pin-status` | 2, 3, 4, 5, 9 |
| `tools/vdf_inject_keys.py` | 240 | vuelca `keys.txt` a `config.vdf > depots` (parser VDF propio) | 4 |
| `tools/pin_set.py` | 37 | edita `ManifestIds` a mano (desarrollo) | 5 |
| `tools/whitelist_hash.py` | 141 | añade un hash a `SafeModeHashes` en `updates.yaml` (`--create-group`) | 1, 11 |
| `tools/check_patterns.py` | 1217 | validador de patrones sin Ghidra: único/no, RTTI, by-name, xref, anclas del finder; `--emit-rvas` | 1 (CI) |
| `tools/fetch_steamclient.py` | 434 | baja el `steamclient.so` actual de un canal de cliente (`media.steampowered.com`) | 1 (CI) |
| `tools/derive_patterns.py`, `derive_*.py`, `apply_derived_pattern.py`, `derive_python_first.sh`, `run_ghidra_derive.sh`, `blocking_constants.py`, `verify_mask.py` | 568+… | rederivación de patrones (Python primero, Ghidra 11.2.1 de respaldo) y aplicación a `patterns.hpp` | 1 (CI) |
| `tools/gmrc_probe.py`, `gmrc_mint.py` | 436+346 | sondas de proveedores de request code y acuñación con sesión real (experimento) | 4 |
| `tools/hubcap_fresh.py`, `smc_fresh.py`, `luatools_find.py`, `snap_app.py` | 213+276+110+96 | sondas: frescura de Hubcap, P-ToyStore, fixes de lua.tools, foto del disco por juego | 4, 6, 9 |
| `tools/experiment_*`, `test_*.py` (16), `find_pic_xrefs.py`, `ghidra_find_gmrc.py`, `scan_signature.py`, `verify_gmrc_anchor.py`, `xlate_vaddr.py`, `zh_grep.py`, `bin_recon.sh`, `fetch_libmem.sh`, `fetch_steamclient.sh` | — | experimentos, tests unitarios (los corre `build.yml`), utilidades de RE, build | 1, 11 |
| `.github/workflows/build.yml` | 127 | build 32-bit, tests, release en tag `v*` con `liblumalinux.so` + `res/version.txt` | 11, 12 |
| `.github/workflows/watch-steam.yml` | 537 | cron diario: versión del cliente → descarga → `check_patterns` → PR de hash (auto-merge) o PR de rederivación o issue | 1, 11 |
| `.github/workflows/probe-steam.yml`, `watch-steam-selftest.yml`, `verify-fix.yml` | 237+187+321 | sonda manual de cualquier canal; autotest de la cadena de rederivación; smoke test en runtime (**nunca ha pasado**, cabecera `:4-13`) | 1, 11 |
| `.devcontainer/` (Arch, `steamos/`, `port-testing/`) | 21+76+393 / 26+98+838 / 21+125+56+110+138 | entornos de prueba: Arch con Steam headless; SteamOS emulado (`deck` uid 1000, `steamos-session-select`, supervisor estilo gamescope); CachyOS real | 1, 11, 12 |

### 1.3 LumaDeck — backend (`main.py`, `backend/`)

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `main.py` | 824 | clase `Plugin` de Decky: arranque (`_main`: update pendiente, verificación de inyección, heal del drop-in de Game Mode, sweep de `.acf`, seed de `api.json`, restore de credenciales, applist, flags de SLSsteam, arranque de `pins`) y un envoltorio por método | todas |
| `backend/downloads.py` | 2056 | añadir juego: descarga del zip por fuentes, `_process_and_install_lua` (archivo, forma owned, orden del lua, depot Linux, `steamidra_lite`, post-check), nombre, notices, pin/unpin de UI, sweep de `.acf` huérfanos, lista de luas | 2, 3, 4, 5, 9 |
| `backend/slssteam_ops.py` | 1453 | `config.yaml` de SLSsteam (FakeAppIds, AdditionalApps, AppTokens, DlcData, DenuvoGames), poke de hot-reload, `config.vdf`, `keys.txt`/`retired_keys.txt`, desinstalación completa, DLC de juego poseído | 2, 3, 4, 9 |
| `backend/fixes.py` | 1304 | fixes de files.luatools.work, extracción con backups, `OnlineFix.ini` → FakeAppId, netsock, launch options, toggle Online, unfix, permisos Linux | 6 |
| `backend/paths.py` | 1281 | rutas (plugin, Steam, SLSsteam, lumalinux, CloudRedirect), cobertura del wrapper, drop-in systemd y su heal, crash guard, salud de los tres componentes | 1, 8, 9, 11, 12 |
| `backend/pins.py` | 1032 | tarea de fondo: pase local (60 s) y de update (30 min); `pins.json`; modelo up/down según `gmrc.json`; pin/unpin con `steamidra_lite`; heal de depotcache; licencias reales; sonda de proveedores | 2, 4, 5 |
| `backend/installer.py` | 807 | corre `setup.sh` como usuario real, flags de SLSsteam (`DisableCloud`/`DisableUpdates`/`SafeMode`/`Plugins`), plugin spliced-tickets, Quick Install, reinject, `apply_component` | 1, 2, 6, 8, 11 |
| `backend/luatools_auth.py` | 730 | cuenta LuaTools (sesión Supabase desde cookies del CEF), refresh, catálogo `/api/denuvo/fixes`, descarga firmada, "versión" de un fix vía pin | 6, 10 |
| `backend/manifests.py` | 642 | un `<depot>_<gid>.manifest`: depotcache → archivo propio → luastools → Hubcap suelto → zip; validación; presupuesto Hubcap; `steamcmd_app_info`; `api_request` | 4, 5 |
| `backend/api_manifest.py` | 600 | `api.json` (seed), credenciales Hubcap/Ryuu/LuaTools con espejo en `credentials.json`, búsqueda por nombre, estado de credenciales (`/user/stats`, caducidad Ryuu, sesión viva) | 4, 9, 10 |
| `backend/steam_utils.py` | 562 | raíz de Steam, VDF, bibliotecas (`libraryfolders.vdf` doble), ruta de instalación, juegos instalados, updates atascadas (`UpdateResult=8`), `is_app_running`, LaunchOptions, `CompatToolMapping` | 5, 6, 9 |
| `backend/ryuu_cookie.py` | 550 | cookie `session` de ryuu.lol por CDP o descifrando el SQLite del CEF (v10/v11); comprobación de login (`data-user-id`) | 10 |
| `backend/steamdb_reader.py`, `versions.py`, `game_versions.py` | 509+359+190 | SteamDB (feed directo; páginas por BrowserView oculto; reto Cloudflare → `needs_user`), traductor build → gids, selector de versión | 5 |
| `backend/steamless.py`, `dotnet.py`, `goldberg.py`, `eos_proxy.py` | 448+204+286+185 | Steamless.CLI con .NET 9, Goldberg (gbe_fork), proxy EOS | 6 |
| `backend/achievements.py` | 439 | esquemas de logros por la Web API de Steam → `UserGameStatsSchema_<appid>.bin` | 7 |
| `backend/cef_cdp.py` | 439 | cliente CDP mínimo (puerto 8080 del CEF): cookies, borrado, `HiddenView` | 5, 10 |
| `backend/http_client.py` | 387 | cliente HTTP sobre urllib; contexto SSL con certifi y ficheros del sistema; **último recurso: sin verificación** (`:218-223`) | 4, 10, 11 |
| `backend/platform_info.py` | 367 | distro, usuario/home/uid real bajo Decky-root, flatpak, familia de sesión, gamescope | 12 |
| `backend/slssteam_schema.py`, `slssteam_version.py` | 369+234 | completa `config.yaml` con claves ausentes del esquema upstream; versión de SLSsteam (`.slssteam.version` o deducida del `.so`) | 1, 2, 11 |
| `backend/desktop_handoff.py`, `quick_install_cli.py` | 384+94 | autostart one-shot en Desktop (`downgrade.sh` + `setup.sh`, o Quick Install), `steamos-session-select` | 1, 11 |
| `backend/components.py`, `update_checks.py`, `self_update.py`, `headcrab_compat.py`, `steam_freeze.py` | 318+263+293+333+189 | estado unificado de componentes (salud + update + crash guard), releases de GitHub con caché 6 h, auto-update del plugin, pin de build de headcrab y soporte por build de lumalinux, `steam.cfg` | 1, 11 |
| `backend/steam_licenses.py` | 146 | parser binario de `appcache/packageinfo.vdf`: `licensed_appids()` / `is_licensed()` | 2, 3, 5, 9 |
| `backend/dev.py`, `config.py`, `utils.py`, `subprocess_env.py` | 171+36+80+43 | overrides de preview; constantes (varias sin uso); helpers; limpieza del entorno de PyInstaller | 11, 4, 9 |
| `backend/deps/Steamless/`, `backend/deps/SplicedTickets/lumadeck-spliced-tickets.lua` | — / 99 | Steamless.CLI (.NET 9) embebido; plugin Lua de SLSsteam (Ace) para tickets de ownership empalmados | 6 |
| `backend/data/` | — | vacío en el repo; en runtime `api.json`, `ryuu_cookie.txt`, `luatools_session.json`, `dev_overrides.json` | — |

`Goldberg/` y `EosProxy/` **no están en el repo**: los baja `release.yml:70-122` al empaquetar; desde fuente son "not bundled" (`goldberg.py:54-72`, `eos_proxy.py:50-52`).

### 1.4 LumaDeck — frontend (`src/`)

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `src/index.tsx` | 174 | rutas (`/lumadeck/game/:appid`, `/lumadeck/settings`, `/lumadeck/library`), parche de `/library/app/:appid` (badge), barra del QAM (Refresh, Settings), contenido del QAM | 9, 11 |
| `src/pages/GameList.tsx` | 982 | el QAM: Quick Install, `SystemStatus`, Add Game (por AppID / por nombre), estado de descarga, My Games | 1, 2, 4, 9, 10, 11 |
| `src/pages/GameDetail.tsx` | 1586 | página por juego: Status, Updates, [Achievements], Fixes & Repairs, Online Fixes, Uninstall | 2, 3, 4, 5, 6, 7, 9, 10 |
| `src/pages/Settings.tsx` | 1417 | API Credentials, [Achievements], [SLSsteam], Components, System, About, Help, Dev | 1, 7, 10, 11, 12 |
| `src/components/SystemStatus.tsx` | 372 | tipos del estado de componentes, `primarySystemAction`, filas de problema/info con confirmación de dos toques | 1, 5, 8, 11 |
| `src/pages/Library.tsx`, `components/GameCard.tsx`, `AppPageButton.tsx` | 130+131+52 | My Games (grid), tile de portada, badge "Added via LumaDeck" | 9, 11 |
| `src/api.ts` | 468 | todas las llamadas al backend (`call()`), `parseResult` | todas |
| `src/i18n.ts`, `pages/Help.tsx` | 919+91 | `en` y `pt-BR`, detección, `useT()`; texto de ayuda | 11, 12 |
| `src/steamOwnership.ts`, `steamDlc.ts`, `browserLogin.ts`, `luatoolsConnection.ts`, `hooks/useLuatoolsConnect.ts`, `features.ts`, `routes.ts`, `refresh.ts`, `components/ActionButton.tsx`, `ScrollAnchor.tsx` | 34+45+20+55+95+26+22+33+44+23 | pista de propiedad real (`rt_purchased_time`), `SetDLCEnabled`, cierre del navegador de login, estado LuaTools, flags (`ACHIEVEMENTS_ENABLED=false`, `SLSSTEAM_TAB_ENABLED=false`), rutas, refresco, widgets | 2, 3, 6, 7, 9, 10, 11 |

---
## §2 Las doce funciones

Cada función: qué hace lumalinux, qué hace SLSsteam de lo que dependemos (y
cómo lo configuramos), qué hace LumaDeck, y la lista de control de
`ecosystem-matrix.md` §1 contestada. Las mediciones con fecha están en §5 y se
citan como "§5 F<n>".

### 2.1 Engancharse a Steam

**Cómo entra.** Modelo **wrapper**, sin tocar `steam.sh` (al contrario: `setup.sh`
lo restaura vanilla si headcrab lo había parcheado, `setup.sh:264-333`). La
inyección no puede vivir en `steam.sh`: Steam lo vuelve a extraer cuando su
tamaño no coincide con el de su manifest, así que cualquier update de Steam la
borraría (y headcrab lo regenera).
`setup.sh` escribe `~/.local/share/SLSsteam/path/steam` (`:1150-1202`), que
resuelve el binario real (`LUMA_STEAM_BIN` → PATH saltándose a sí mismo →
`/usr/bin/steam`, `/usr/games/steam`, `/usr/lib/steam/steam`), pasa por el guard
y hace `exec`. El entorno lo fija `luma_set_injection_env` (`:884-898`):

- `LD_AUDIT=library-inject.so:SLSsteam.so` (o solo `SLSsteam.so` si
  `library-inject.so` está vacío, como upstream lo deja desde 20260903).
- `LD_PRELOAD=liblumalinux.so:cloud_redirect.so`: lumalinux **antes** que
  CloudRedirect, a propósito, porque `cr_stats_fix` interpone un símbolo de
  CloudRedirect y necesita ir primero (`cr_stats_fix.hpp` "the wrapper puts
  [lumalinux] BEFORE cloud_redirect.so"). CloudRedirect nunca en `LD_AUDIT`:
  "corrompe el heap del cliente" (`setup.sh:1155-1156`). En Wayland se añade
  `libextest.so` al final (`:887-892`).

Cobertura **Desktop**: `ensure-desktop-coverage.sh` reescribe el `Exec=` de todo
`*steam*.desktop` cuyo programa sea `steam|steam-jupiter(-stable)?|bazzite-steam`
a `env LUMA_STEAM_BIN=<prog> <wrapper>` con la marca `X-LumaLinux-Wrapped=1`
(`setup.sh:584-614`); los de `/usr/share/applications` se sombrean en
`~/.local/share/applications`; autostart igual; el icono de `~/Desktop` se
reapunta; PATH drop-in en `.bashrc/.zshrc/.profile`; un guardian systemd
`--user` (`.path` sobre cinco directorios + `.timer` cada 5 min) rehace la
cobertura si Steam regenera su `.desktop` (`:747-806`). Cobertura **Game
Mode**: el PATH drop-in no llega a la sesión gamescope (medido en Deck, §5 F1),
así que un drop-in en `steam-launcher.service` (`ExecStart=` →
`lumalinux-steam-launcher`) carga el mismo guard y hace `exec
/usr/lib/steamos/steam-launcher` (`:960-993`). LumaDeck rehace ese drop-in en
cada carga del plugin y hace `daemon-reload` (`paths.py:631-703`, llamado en
`main.py:93-106`).

Dentro del proceso, `liblumalinux.so` funciona por `LD_PRELOAD` (constructor que
sondea `/proc/self/maps` 300 veces a 1 s hasta ver `steamclient.so`,
`main.cpp:429-469`) y conserva las entradas `LD_AUDIT` (`la_preinit`/`la_objopen`,
`:393-419`) aunque esa ruta rompe el heap (medido, §5 F1). Solo actúa en los
procesos `steam` y `steamwebhelper` (`proc_filter.cpp:25-28`, fail-open si no
se puede leer `/proc`).

**Cómo localiza lo que parchea.** Cinco funciones de `steamclient.so`, cada una
con una cadena de localizadores que solo avanza si el anterior devolvió 0:

| Hook | Función de Steam | Cadena | Qué cambia | Si falla |
|---|---|---|---|---|
| DepotKey | el accessor KeyValues interno (`CConfigStore` slot 6, "GetBinary") al que Steam pide `Software\Valve\Steam\Depots\<id>\DecryptionKey` (`depot_key_hook.cpp:36-38`) | feed RVA → patrón único → RTTI por nombre (`21IClientConfigStoreMap/GetBinary` → `12CConfigStore`, `rtti.cpp:289-386`) | si KeyStore tiene clave, copia 32 bytes y devuelve 32; si no, pasa al original (`:74-106`) | **crítico**: sin él el finder no arranca y nada se inyecta (`main.cpp:192-201, 267-283`) |
| ShaderDepot | `CShaderCacheManager::GetShaderCacheDepot(appinfo)` | feed → patrón → xref del string `shadercachedepot` | devuelve 0 (salta el pre-cache de shaders) para juegos sin clave o cuando ningún proveedor GMRC responde (`shader_depot_hook.cpp:57-96`) | no crítico |
| GMRC | `BYieldingGetManifestRequestCode(this, app, depot, gid, branch, out_code*)` | feed → patrón → xref del nombre del job (`ContentServerDirectory.GetManifestRequestCode#1`) con `.eh_frame_hdr` o walk-back | para depots nuestros pide un código a la cascada (§2.4) y lo escribe en `*out_code` (`gmrc_hook.cpp:43-58`) | no crítico: se pierde la descarga nativa y el pre-cache de shaders; instala desde manifests pre-sembrados |
| BuildDep | `CUserAppManager::BuildDepotDependency(...)` | feed → patrón (primer match, no único, `patterns.cpp:94-118`) → xref | tras el original, PATCH del `ManifestGid` de los depots de `keys.txt` en `pDepotInfo`; nunca inyecta, nunca toca `pSharedDepotInfo` (`depot_dependency_hook.cpp:61-128`) | **OFF por defecto** (`main.cpp:175-186`): SLSsteam hookea la misma función y el pin vive en `ManifestIds`; `LUMA_FORCE_BUILDDEP` lo activa |
| Reconcile | `CUser::NotifyLicensesUpdated(this)` — llamada directa, no detour | feed → patrón → anchor del callback 125 (`push 0x7d; push eax; call` + `this` + `jle`, `reconcile_anchor_core.hpp:101-138`) | hace que Steam reprocese licencias sin reiniciar (§2.2) | `Reconcile: failed` → Add Game necesita reinicio |

Más el **finder de package-0** (`package_zero_finder.cpp`), que no es un hook:
un hilo que localiza el puntero global a `CPackageInfoCache` (GOT por consenso
+ `disp32` del idiom `lea r1,[GOT+X]; mov r2,[r1]; mov r3,[r2+root]`, con dos
layouts conocidos `0xc58/0xc6c` y `0xf90/0xfa4`, `:67-70, 337-437`), camina el
árbol hasta `PackageId==0` y ahí inyecta y retira depots (§2.2). Todo "único o
nada": una ambigüedad nunca se parchea (`patterns.cpp:156-162`,
`package_zero_finder.cpp:410-415`).

El feed de RVAs (`rva_feed.cpp:74-147`) hashea el `steamclient.so` en disco,
descarga `raw.githubusercontent.com/jayool/lumalinux/main/res/rvas/<sha256>.yaml`
(acepta el cuerpo si contiene `hooks:`), lo cachea en
`~/.cache/lumalinux/rvas/` y traduce cada RVA con `VaddrXlate` exigiendo que
caiga en un mapping `r-x` de steamclient. Los patrones y los localizadores de
rescate son el respaldo cuando el feed no tiene el build.

**Por qué estas anclas.** Hay cinco formas de encontrar una función en un binario
sin nombres que Valve recompila cada pocas semanas, de más frágil a más
resistente:

| Ancla | Resiste una recompilación | Resiste que otro parchee antes la función | Quién la usa aquí |
|---|---|---|---|
| prólogo de la función | mal | no: el detour ajeno lo sobrescribe | patrón de DepotKey, ShaderDepot, GMRC, BuildDep |
| instrucción del cuerpo (+ caminar atrás hasta el inicio) | sí | sí | anchor de Reconcile (la secuencia del callback 125 en su cuerpo) |
| sitio de llamada + seguir el salto | sí | sí | nadie todavía (exige convergencia, §4.4) |
| cadena de texto referenciada + entrada por `.eh_frame_hdr` | muy bien | sí | GMRC, ShaderDepot, BuildDep (xref) |
| vtable por nombre (RTTI + mapa de interfaces) | muy bien, también a reordenaciones | sí | rescate de DepotKey |

El prólogo es la peor: casi toda función i386 PIC empieza igual, y para hacerla
única hay que alargar el patrón con lo volátil (tamaño de marco, offsets de
spill, orden de registros). `kDepotKeyFnPattern` mide 46 bytes; las firmas de
cuerpo de SLSsteam, 5 (`slssteam.md` §2.1). Lo compensan el feed (cubre un build
sin depender del patrón) y que cada hook tenga un segundo localizador
independiente. Pese a todo, ningún patrón nuestro ha tenido que rederivarse
desde junio (§5.2 F1): las funciones elegidas son prólogos que Valve no
reordena.

**Qué pasa cuando Steam actualiza.** Dos capas:

1. En el `.so`: el hash SafeMode (`update.cpp`, copiado de SLSsteam) descarga
   `updates.yaml`, comprueba que el SHA-256 de `steamclient.so` esté en el grupo
   `LUMALINUX_SAFEMODE_VERSION` (= `res/version.txt` compilado) y, si no está,
   **solo emite un toast** y sigue con el scan de patrones (`main.cpp:107-126`).
   `Status::SetBlocked` existe pero nunca se llama: `status.json.blocked` es
   siempre `null` (`main.cpp:270-276`). El gate real es por hook: cada uno
   acaba `installed`/`failed`/`disabled`; solo DepotKey es crítico. Toast final
   "loaded — N/M hooks active" o "N/M hooks — X FAILED. Steam update?".
2. En CI (`watch-steam.yml`, diario 07:17 UTC): versión del cliente Deck →
   descarga del `steamclient.so` → `check_patterns.py`. Exit 0/2 → PR de hash
   (`whitelist_hash.py`) + `res/rvas/<sha>.yaml`, **auto-merge sin esperar
   checks** (`:152-166`, "main is unprotected"). Exit 2 → además rederivación
   de las constantes movidas (Python primero, Ghidra 11.2.1 de respaldo) y PR
   con `patterns.hpp`; exit 3 (crítico) → PR que además sube `res/version.txt`
   a un grupo nuevo, o issue. Como el `.so` lee `updates.yaml` y `res/rvas/`
   de `main` en cada arranque, un build nuevo se cubre **sin release** cuando
   los RVAs bastan (`watch-steam.yml:420-437`). `probe-steam.yml` prueba a mano
   cualquier canal (`steam_client_ubuntu12`, betas).

SLSsteam tiene su propio SafeMode; lo ponemos a `no` (`setup.sh:228-241`,
`installer.py:164-213`) para que intente enganchar builds nuevos. LumaDeck
lee las consecuencias: `status.json` (`hooks{…}`, `blocked`) y
`~/.SLSsteam.log` ("Failed to find all patterns! Aborting…", "Unknown
steamclient.so hash! Aborting…", `paths.py:1205-1226`) y los convierte en
estados por componente: `not_installed`, `not_loaded`, `not_injected`,
`not_supported` (causa `version` o `hooks`), `healthy`, y para CloudRedirect
`not_authed`/`disabled` (`paths.py:741-798, 1032-1080, 1229-1262`). El gate de
build (`headcrab_compat.py:273-333`) compara el build de
`package/steam_client_*.manifest` con el pin de headcrab y con el grupo
SafeMode de la **release** de lumalinux (`releases/latest/download/version.txt`).

**Cómo se recupera.**

- Crash guard del launcher (`lumalinux-guard.sh`, `setup.sh:826-948`): cuenta
  `*.dmp` en `/tmp/dumps` más nuevos que `last_launch` dentro de 180 s; al 3º
  seguido, o al 1º si `steamclient.so` cambió desde el último arranque bueno,
  latchea `safe_mode` (fingerprint de los cuatro `.so`), **borra
  `appcache/appinfo.vdf`**, avisa "Steam recovery mode" y arranca vanilla
  (`env -u LD_AUDIT -u LD_PRELOAD`). El latch se limpia solo cuando cambia el
  fingerprint. Sin `/tmp/dumps` el guard es ciego (`:919-932`). LumaDeck lo
  muestra como "Recovery mode" con botón "Re-enable injection"
  (`paths.py:818-888`, `SystemStatus.tsx:201-211`).
- Kill-switches del `.so`: `LUMA_NO_DEPOTKEY`, `LUMA_NO_GMRC`,
  `LUMA_NO_SHADERSKIP`, `LUMA_NO_PKG0_FINDER`, `LUMA_NO_RECONCILE` (o el marker
  `~/.config/lumalinux/no_reconcile`), `LUMA_NO_SLS_ACH_UNBLOCK`,
  `LUMA_NO_CR_STATS_FIX`, `LUMA_NO_LIBCURL_FIX` (tabla en §3.3). `setup.sh
  --uninstall` quita wrapper, guard, cobertura y estado, y deja `.so`, configs
  y `keys.txt` (`:1016-1030`).
- LumaDeck: "Restart Steam" (`steam -shutdown` como usuario, luego SIGTERM,
  `main.py:268-300`), "Repair"/"Reinstall" = `setup.sh` entero
  (`installer.py:486-633`), "Fix in Desktop" = autostart one-shot que ejecuta
  `curl downgrade.sh | bash` + `setup.sh` y vuelve a Game Mode
  (`desktop_handoff.py:83-111`); el pin `steam.cfg` que deja el downgrade se
  levanta al acabar un `install_via_setup` bueno si el objetivo es más nuevo y
  lumalinux lo soporta (`steam_freeze.py:148-189`). **El downgrade nunca se
  ha ejercitado en vivo** (§5 F1).
- `downgrade.sh`, en orden: baja y verifica el manifest del cliente fijado por
  headcrab (`DECK_MANIFEST_URL` / `LINUX_MANIFEST_URL`, de
  `Deadboy666/SteamTracking@headcrab`) antes de tocar nada; respalda
  `package/` y deja el manifest; relanza Steam sin interfaz contra el espejo
  (`-forcesteamupdate -forcepackagedownload -overridepackageurl $DOWNGRADE_URL
  -exitsteam`, por defecto `headcrab.bifrosthub.ru/client-stable`), con SLSsteam
  si está y vanilla si no; comprueba que llegaron paquetes, y **solo entonces**
  escribe el pin `steam.cfg` (`BootStrapperInhibitAll`) con la firma
  `# lumalinux`. Si no llegó nada, restaura `package/` y no pinea: Steam sigue
  arrancando (`downgrade.sh:30-46,155-235`). LumaDeck distingue el pin propio
  (firmado, se levanta con el gate de arriba) de uno ajeno (sin firma, se
  levanta en el acto, `steam_freeze.py:65,148-170`). Se ofrece cuando algún
  componente está `not_supported`, salvo si el único es lumalinux y aún no
  soporta el build fijado (`SystemStatus.tsx:99-121`).
- Logs: `~/.cache/lumalinux/lumalinux.log` con líneas legibles por máquina
  (`Hook install: name=… method=… outcome=installed|miss|hook_install_failed`,
  `Finder resolve: … outcome=resolved|miss`), `~/.local/state/lumalinux/guard.log`,
  `~/.SLSsteam.log`, los logs de Decky. Toasts por `notify-send`.

**Lista de control.** Entrada: wrapper + `LD_AUDIT` (SLSsteam) + `LD_PRELOAD`
(lumalinux, CloudRedirect). Localización: feed de RVAs por build → patrones de
bytes únicos → rescates (RTTI por nombre, xref de string, anchor). Steam
actualiza: hash advisory en el `.so`; CI diaria con PR automático de hashes y
RVAs; rederivación de patrones; estados `not_supported` en LumaDeck. Recuperación:
guard anti crash-loop con vanilla, kill-switches, reinstalación por `setup.sh`,
downgrade por Desktop (no ejercitado). Procesos: solo `steam`/`steamwebhelper`
en el `.so`; el wrapper exporta las variables a todo el árbol de Steam.

### 2.2 Propiedad y licencias

**Spoof de propiedad: SLSsteam.** lumalinux no tiene ningún hook de ownership
(`main.cpp:21-22, 138-139`: `CheckAppOwnership` y los tokens PICS los da
SLSsteam). Lo que hacemos es decirle a SLSsteam **qué apps** poner como
poseídas, por `AdditionalApps` de su `config.yaml`: `steamidra_lite` añade el
AppID del juego (solo el base, "meter depots ahí confunde a Steam",
`steamidra_lite.py:1301-1327`) y LumaDeck lo edita (`slssteam_ops.py:315-400`)
y lo quita al desinstalar. `FakeAppIds` para los fixes online. SLSsteam recarga
`config.yaml` por inotify; LumaDeck fuerza el evento con un append de 0 bytes
tras cada escritura (`_poke_reload`, `slssteam_ops.py:63-104`), salvo
kill-switch o hook `Reconcile` fallido.

Claves de SLSsteam que escribimos (todas, con valor): §3.4. Lo que **no**
tocamos: `AppIds`, `UseWhitelist`, `DisableFamilyShareLock`, `DlcData` salvo
>64 DLC, `AppTokens` salvo `addtoken()` en el lua, `ManifestIds` lo escribe
`steamidra_lite`, `SmartTickets`, `MaxSchemaTries`, `API`, `LaunchOptions`,
`FakeName`… Dos notas: `PlayNotOwnedGames` ya no existe upstream (20260707);
`setup.sh` la elimina al fusionar con la plantilla (`:159-160`) pero
`steamidra_lite.py:377-383` la sigue escribiendo si crea el config desde cero
(§4.1). `NotifyInit: yes` para el toast de arranque.

**Inyección de licencias: lumalinux.** Lo que SLSsteam no hace es que los
**depots** de un juego no poseído pasen el filtro de licencia de
`BuildDepotDependency`. lumalinux mete los **ids de depot** (no el appid) que
hay en `keys.txt` en el `AppIdVec` (+0x38) del `PackageInfo` con
`PackageId==0` (`load_package_hook.hpp:39-45`, `load_package_hook.cpp:183-197`):
sanity del vector (`m_Size ≤ 4096`, entradas 0 o >50M rechazadas), dedup,
`realloc` doblando con el `realloc` de Steam. El finder lo hace cada 2 s hasta
el primer HIT y luego cada 15 s (`package_zero_finder.cpp:139-140, 827`), y al
instante cuando cambia `keys.txt` (watcher inotify, `key_store.cpp:231-262`;
despertar en `license_reconcile.cpp:116-126`). **Retire** (`4f8579c`,
v0.22.2): los ids que lumalinux inyectó y ya no están en `keys.txt` se
compactan fuera del vector (`load_package_hook.cpp:199-251`); nunca toca
entradas de Steam.

**Reconcile sin reiniciar.** Tras inyectar o retirar, si `keys.txt` cambió,
lumalinux llama a `CUser::NotifyLicensesUpdated` con el `CUser*` que captura el
guard de logros de SLSsteam (`sls_achievement_unblock.cpp:64` →
`license_reconcile.cpp:92-98`): Steam reprocesa licencias y el juego aparece
("added depots") sin reinicio. Sin SLSsteam, o antes de que el guard haya
disparado, no hay `CUser*` y se salta ("restart still works",
`:132-135`). Medido en vivo 2026-07-20 y en frío 2026-09-04 (§5 F2). SLSsteam
solo emite `AppLicensesChanged_t` para apps **nuevas** en su diff de config
(`146e28f`): al añadir el juego aparece; al quitarlo sigue en la biblioteca
hasta reiniciar (medido 2026-10-07 09:59, §5 F9).

**Juegos que la cuenta posee de verdad: LumaDeck decide.**
`steam_licenses.licensed_appids()` parsea `appcache/packageinfo.vdf` (el fichero
de Steam, inmune a la inyección en memoria; `steam_licenses.py:82-146`). La
forma del add la fija `downloads.resolve_owned` (`:926-945, 1677-1690`): si ya
lo gestionamos como added sigue added; si `pins.json` dice owned sigue owned;
si no, lo que diga `packageinfo.vdf`. La pista del frontend
(`rt_purchased_time`, `steamOwnership.ts`) solo se loguea. Un juego poseído:

- Add = **solo sus DLC**: `_dlc_depots_of` (PICS `dlcappid`, se niega si
  steamcmd.net no contesta), `_owned_plan`, `_filter_lua_for_owned_base` deja
  solo depots de DLC no poseídos con `addappid(<base>)` primero, borra los
  manifests descartados, y `steamidra_lite --dlc-of-owned` (`downloads.py:948-1002,
  1124-1149`; `steamidra_lite.py:1305-1319`): el base **no** va a
  `AdditionalApps` (rompería family sharing, `:1309-1313`), su `.acf` no se
  toca, se registran los AppIDs de los DLC sin clave.
- Después: `pins.set_owned`; `is_owned` = registro o licencia real
  (`pins.py:276-293`); no se fuerza Proton; `install_game_version` y
  `repair_appmanifest` lo rechazan (`main.py:428, 709`); el pase de update solo
  mira DLC no licenciados (`pins.py:871-878, 909-911`); `ensure_pinned` suelta
  los depots con licencia real (`pins.py:343-361, 675-690`); un juego comprado
  después de añadirlo pasa a owned solo.
- Para lumalinux no hay diferencia: sirve clave a cualquier depot de
  `keys.txt`, poseído o no (`depot_key_hook.cpp:30-34`). Medido 2026-10-05 run
  C: con el base poseído en `AdditionalApps` nada rompió, claves servidas a
  depots poseídos sin crash (§5 F2).

**Family sharing.** Sin tratamiento propio en ningún sitio. Solo
`DisableFamilyShareLock: yes` cuando `steamidra_lite` crea el config desde cero
(`:380`) y como marcador de plantilla válida en `setup.sh:166`.

**Depots compartidos (redists, 228980).** Detección por la cabecera
`-- SHARED DEPOTS` del lua de Hubcap y la lista estática 228981-229033
(`steamidra_lite.py:177-213`). Van a `keys.txt` como cualquier otro ("servir
cualquiera no corrompe desde el hook v1.0") pero **nunca se pinean** ni entran
en `AdditionalApps` en `--dlc-of-owned`. LumaDeck tampoco los fija ni cuenta
(`pins.py:122-126, 537-555, 901`). lumalinux no parchea `pSharedDepotInfo`
(`depot_dependency_hook.cpp:110-128`, corrupción de heap si se tocaba).

**Lista de control.** Spoof: SLSsteam por `AdditionalApps`/`FakeAppIds`, que
escribimos nosotros. Inyección: depots en package-0 en memoria, reaplicada cada
15 s y al cambiar `keys.txt`, retiro de ids que salen; reconcile en vivo con
el `CUser*` de SLSsteam. Poseídos: decidido desde `packageinfo.vdf`, solo DLC,
licencias reales nunca pineadas. Family sharing: nada. Redists: en `keys.txt`,
nunca pin, nunca `AdditionalApps`.

### 2.3 DLC

**Criterio de unlock: SLSsteam.** En su código, `shouldUnlockDlc` desbloquea
cualquier DLC no poseído mientras el juego corre, sin mirar `AdditionalApps`
(`slssteam.md` §2.3, reverificado 2026-10-08). En
lumalinux no hay lógica de DLC: un DLC es "una línea más" de `keys.txt`
(`DepotEntry.DlcAppId` existe en el layout, `steam_types.hpp:33`, y no se lee).

**LumaDeck**:

- Lista de DLC por `appdetails?filters=basic,dlc` y nombres por lotes de 10;
  solo escribe `DlcData` cuando el juego tiene **más de 64 DLC**
  (`slssteam_ops.add_game_dlcs`, `:554-604`), el límite de Steam. Regenerar
  `DlcData` para >64 quedó **descartado** 2026-10-06.
- Alta y baja de DLC por la casilla de Steam: `SetDLCEnabled` desde el
  frontend (`steamDlc.ts`), porque Steam honra `DisabledDLC` al arrancar
  (medido 16:53, §5 F3). Tras el add de un juego poseído instalado, el
  frontend vuelve a marcar los DLC y "instalan la próxima vez que arranque
  Steam" (`GameList.tsx:167-172`); forzar la descarga sin reinicio con un ciclo
  `false→true` funcionó 5/5 con el juego "despierto" y falló al arrancar, y se
  quitó (§5 F3).
- DLC comprado de verdad: fuera del plan owned (`_owned_plan.owned_dlc`), su
  línea keyless se borra del lua, su depot no se pinea ni reinstala, el
  uninstall lo salta (`downloads.py:994-996, 896-898`; `pins.py:343-361`;
  `slssteam_ops.py:1035-1038`).
- DLC sin depot (solo flag): `flag_dlcs` → `AdditionalApps` vía
  `steamidra_lite --dlc-of-owned`, sin descargar nada
  (`downloads.py:999`; `steamidra_lite.py:146-163` acepta un lua sin claves).
  En un juego no poseído los DLC **no** van a `AdditionalApps`: SLSsteam los
  desbloquea solo (`steamidra_lite.py:1282, 1323`).
- Quitar: `remove_game_dlcs` (`slssteam_ops.py:607-640`); para owned, el
  frontend desmarca los DLC en Steam primero (`owned_dlc_to_disable` +
  `disableDlcs`, todo o nada) y el backend se niega a seguir sin eso
  (`:1081-1086`); claves → `retired_keys.txt` para que Steam pueda borrar los
  ficheros (nombres cifrados). Quitar un DLC añadido a un juego added exige
  reiniciar Steam (SLSsteam no emite callback al quitar).

**Lista de control.** Unlock: SLSsteam, todo DLC no poseído del juego en
marcha; `DlcData` solo >64. Comprado: se deja a Steam en todos los caminos.
Sin depot: `AdditionalApps`. 64: umbral de `add_game_dlcs`; regenerar
descartado. Quitar: casilla de Steam + claves retiradas; added → reinicio.

### 2.4 Claves y manifests

**Claves.** Salen del lua del zip (`addappid(depot, 1, "clave")`, regex de
SteaMidra, `steamidra_lite.py:106-109`) y van a dos sitios: (a)
`~/.config/lumalinux/keys.txt` con tres formatos de línea —
`depot;app;gid;size;clave` (extendida, inyectable si app≠0 y gid≠0), `app;clave`
(legacy) y `app;` (presencia, sin clave) — (`key_store.cpp:89-120`;
`steamidra_lite.py:517-534`); (b) `config.vdf > depots > <depot> > DecryptionKey`
para Steam, **filtrando el AppID** porque escribirlo ahí hacía crashear Steam
(`steamidra_lite.py:1411-1419`). lumalinux sirve las de (a) desde el hook
DepotKey; una línea de presencia hace que el hook pase al original y no fabrique
clave cero (`depot_key_hook.cpp:89-98`), y sirve al hook ShaderDepot para
devolver 0. `retired_keys.txt` (v0.22.2): claves de depots quitados, servidas
por `Lookup` pero fuera de `GetAllDepotIds`/`HasDepot`, para que Steam pueda
borrar ficheros con nombre cifrado sin que el depot cuente como nuestro
(`key_store.cpp:271-289`). Steam borra en el shutdown las claves de `config.vdf`
de depots no poseídos (medido, §5 F4), por eso `keys.txt` es la fuente. LumaDeck
no descarga claves de ningún otro sitio; `depotkeys.json` de
api.993499094.xyz se midió (32/32 conocidas, no day-one) y quedó aparcado.

**Manifests.** `steamidra_lite` copia los del zip a `<steam>/depotcache/`
(solo ahí: Steam no lee `config/depotcache/`, medido 2026-09-11) y lee el gid
del nombre y de `setManifestid` (el lua manda). LumaDeck consigue uno suelto
con `manifests.resolve_manifest` (`:501-567`), en este orden:

1. `depotcache/`.
2. Archivo propio `~/.local/share/lumadeck/manifests/<app>/` (todo lo que
   pasa por el plugin se archiva; corrupto se borra).
3. `manifest.luastools.xyz/m/<depot>/<gid>` (sin credencial; archivo público
   alimentado por BetterSteamTools).
4. Hubcap `/generate/manifest?depot_id&manifest_id` (clave Bearer; cuota
   `single` 1500/día; **un intento por depot+gid cada 24 h**,
   `manifests.py:406-423`; medido byte-idéntico 2026-10-07).
5. Zip entero de un hub (`api.json`): Hubcap con clave (25/día; un intento por
   app cada 24 h) → Ryuu con sesión de Discord → Sushi y Spinoza
   **deshabilitados**. Solo si `gid == current_gid`.

Validación: inflado "Pro", magic `d0 17 f6 71`, depot y gid leídos del bloque
de metadatos (`manifests.py:134-213`). Los zips de Hubcap no traen el manifest
del depot de shaders (`<appid>_*.manifest`) (medido, §5 F4).

**Códigos de petición de manifest (GMRC).** Lo pide lumalinux, no LumaDeck.
Cascada en `gmrc_store.hpp:154-159`: `20770407.xyz/manifest/<depot>/<gid>`
(UA `lumalinux/<ver>`), `manifest.manifestdex.com/<gid>` (UA `ManifestDeX/1.0`),
`gmrc.wudrm.com/manifest/<gid>` (HTTP), `manifest.steam.run/api/manifest/<gid>`
(JSON). `GetCode` (`:334-417`): caché por gid 120 s (una denegación se
recuerda); por proveedor hasta 3 intentos a 1 req/s global, timeouts 5/10 s,
clasificación 429/"error code: 1015" → espera 10 s, ≥500 → 5 s, 401/403 →
denegado definitivo, otro → sin código; proveedor caído se salta 60 s. Un
código solo se entrega si el CDN de Valve lo acepta con `Range: 0-0` en
`/depot/<d>/manifest/<gid>/5/<code>` (`:296-323`); un rechazo cuenta como
denegación (medido: un código falso sin este chequeo deja la update en
"Unspecified Error" sin reintento; con él Steam reintenta a los 30 s, §5 F4).
Salud: `gmrc.json` `{"providers": "up"|"down"}` en cada lookup; "up" = alguien
respondió. Sonda `ProvidersReachable` (depot 1/gid 1, 15 s de caché) para el
hook ShaderDepot. `LUMA_GMRC_URL` sustituye la tabla. LumaDeck sondea por su
cuenta 20770407 y manifestdex validando contra el CDN (`pins.probe_providers`,
`:428-485`), solo cuando hay juegos congelados por proveedores. Medido
2026-09-15/16: 42/42 gids válidos por los cuatro, códigos de ≥58 min, el GET
del CDN es anónimo, wudrm/steam.run/manifestdex comparten backend (§5 F4).

**Qué deja de funcionar si cae cada servicio.** Hubcap: sin zips nuevos
(salvo Ryuu), sin manifests sueltos para builds viejos, depots nuevos no se
pueden añadir. Ryuu: solo la fuente 2. luastools: el paso 3 pasa a 404.
`api.steamcmd.net`: sin pase de update ("steamcmd.net unavailable"), add owned
rechazado, sin depot Linux, sin `installdir` de respaldo. Proveedores GMRC:
`gmrc.json` → `down`, todo juego no congelado se fija a su build instalado,
instalaciones nuevas con pin, sin pre-cache de shaders; con manifests en
depotcache Steam instala igual (medido V3, §5 F5). CDN de Valve: nada se
entrega. GitHub raw: SafeMode y feed de RVAs desde caché; sin caché, toast y
patrones. SteamDB: sin selector de versiones. lua.tools: sin catálogo ni
descargas de fixes. GitHub (releases/raw): sin `setup.sh` ni `downgrade.sh`,
sin updates de componentes (caché), esquema de SLSsteam desde snapshot.

**Lista de control.** Claves: del lua a `keys.txt` (+`config.vdf`), retiradas
aparte. Manifests: cinco fuentes en orden, con archivo propio y cuotas.
Códigos: cuatro proveedores con ritmo, backoff y verificación en el CDN; sonda
de vida; `gmrc.json`. Dependencias: tabla en §4.2.

### 2.5 Updates de juegos

**El pin.** Hay dos escrituras y una sola vale. `steamidra_lite` escribe el gid
y el tamaño en `keys.txt` para el hook BuildDep de lumalinux, que está OFF por
defecto; el pin **vivo** es `ManifestIds: {<depot>: <gid>}` en el `config.yaml`
de SLSsteam, que su propio hook de `BuildDepotDependency` lee y recarga por
inotify sin reiniciar Steam (`steamidra_lite.py:988-995, 1363-1402`;
`main.cpp:175-186`). Modos sin zip: `--pin-installed APPID` (lee
`InstalledDepots` del `.acf`, solo en la biblioteca raíz, `:924`),
`--unpin APPID`, `--set-pin APPID DEPOT:GID…` (rechaza gid 0 y depots sin
clave, omite redists), `--pin-status APPID` (`:1055-1161`). Siempre
read-merge-write para no borrar pins de otros juegos. Steam relee `ManifestIds`
al arrancar, al lanzar el juego y en cada reintento de una update pendiente,
no en "Play" ocioso (medido, §5 F5); por eso cambiar un pin va con
`StateFlags |= 2` en el `.acf` (`pins.mark_update_required`, `:207-252`;
medido 2026-09-21: pin + reinicio → nada; pin + flag + reinicio → baja).

**Qué decide LumaDeck.** `pins.json` por juego: `frozen`, `reason`
(`providers`|`files`|None), `fix_id`, `version` (buildid, fecha, label, gids),
`owned`, `depot_apps` (`pins.py:136-329`). Tarea de fondo (`run_forever`,
`:1005-1019`): **pase local cada 60 s** y **pase de update cada 30 min**, ambos
al arrancar. Modelo por `gmrc.json` (`gmrc_state`, `:404-419`): sin fichero
pero hook GMRC `installed` → `up`; `down` con una sonda propia más reciente →
`up`.

- Pase local `_apply_model` (`:813-841`): congelado por el usuario → solo
  `ensure_pinned` (heal); `down` → fija cada juego a su build instalado y marca
  `reason: providers`; `up` → si hay `ManifestIds` nuestros, `--unpin` y
  descongela los `providers`; sin modelo → fija todo (0.8). `ensure_pinned`
  (`:668-799`): suelta depots con licencia real, fija los que faltan al
  `InstalledDepots` del `.acf` o al único manifest que haya, archiva lo
  presente y **restaura desde el archivo** lo pineado que falte, incluido el
  manifest del build instalado cuando el pin apunta a otro (Steam lo necesita
  para diffear; medido 61 s, §5 F5).
- Pase de update `check_update` (`:881-968`): gids actuales de
  `api.steamcmd.net`; depots relevantes = plataforma (`linux` si tenemos clave
  de un depot Linux, si no `windows`), `osarch` ""/64, no redist, no
  `sharedinstall`, no DLC con licencia; poseído → solo DLC no licenciados.
  Depots nuevos sin clave → zip (presupuesto diario), rechazado si sus gids no
  son los actuales, reinstalación completa. Con pin: por cada depot cuyo gid
  cambió, `resolve_all` (archivo → luastools → suelto → zip si es el actual);
  además los manifests del build instalado tienen que existir; **el pin solo
  se mueve con todo en mano** (`:949-968`). Nativo (`up`): "Steam updates it".
  Primera update de producción por pin: 2026-09-12/13, 17 segundos de
  descarga (§5 F5).
- Congelación por ficheros (`reason: files`): cualquier escritura en el
  directorio del juego (fix, Goldberg, EOS, Steamless) congela, porque una
  update de Steam revertiría los ficheros (`pins.freeze_for_files`,
  `:255-273`). Solo `providers` se deshace sola.

**Toggle de usuario.** "Auto-update" en la página del juego = `pin_game` /
`unpin_game` (`downloads.py:221-249`); despinear con `up` hace `--unpin` y
marca el `.acf`; el usuario reinicia. `DisableUpdates: no` en SLSsteam para que
los `AdditionalApps` actualicen (`installer.py:216-259`).

**Cambio de versión.** `game_versions.list_versions`: los últimos 10 builds
públicos del feed RSS de SteamDB (el único endpoint que Cloudflare sirve a un
cliente plano, medido 2026-09-21); `install_version`: página del build
(`const depots = […]`) → gids, depots no listados por el historial del depot
(hasta 8, fila pública a ±3 s), `pins.set_pin` (`--set-pin`), `set_frozen` con
`version`, `mark_update_required`. Las páginas van por un BrowserView oculto
(`cef_cdp.HiddenView`) con reto Cloudflare → `needs_user` y botón "Open
SteamDB" (`steamdb_reader.py:255-340`). Backoff 1 h tras 403/429/503. También
el fix "versión" de LuaTools (`luatools_auth._install_fix_version`,
`:580-673`): fija por `setManifestid` del lua, sustituyendo depots `fromapp`
por el gid actual.

**Update que Steam ya empezó.** `check_stuck_updates` lista juegos con
`UpdateResult=8` que tienen lua y no son poseídos (`steam_utils.py:294-325`) →
fila "can't update" en el QAM y "Fix Update" en la página (= re-descarga del
zip). `steamidra_lite` resetea `UpdateResult`, `Bytes*`, `StagingSize` y
`StateFlags &= ~16` del `.acf` (`:634-641, 780-787`), sin tocar
`ScheduledAutoUpdate` ni `FullValidateAfterNextUpdate` (12 de 13 `.acf` reales
no tienen este último, §5 F9). No hay cancelación de una descarga de Steam.

**Lista de control.** Pin: `ManifestIds` (vivo) + `keys.txt` (dormido);
congelación por usuario, por ficheros o por proveedores caídos. Automática:
pase de 30 min contra `api.steamcmd.net`, cadena de manifests, pin solo con
todo; nativa cuando hay proveedores. Versión antigua: SteamDB + `--set-pin` +
flag del `.acf` + reinicio manual; también por LuaTools. Update empezada:
detección por `UpdateResult=8` y re-descarga; sin cancelación.

### 2.6 Fixes y DRM

Todo en LumaDeck; lumalinux solo instala el runtime .NET 9 (`setup.sh:378-404`)
y descarga `netsock.so` a `~/.config/SLSsteam/tools/netsock/` (`:1109-1117`).

- **Catálogo LuaTools** (`luatools_auth.py`): `lua.tools/api/denuvo/fixes?appid=`
  es público; descargar exige la cuenta (sesión Supabase cosechada de las
  cookies `sb-db-auth-token.N` del CEF tras el login con Discord, refresh con
  `refresh_token` y la `apikey` anon embebida, `:46-53, 304-356`). Un fix
  tiene `slot` `fix` (zip) o `manifest` (fijar el build que el fix exige, §2.5).
  Un fix por juego (`needsReplace` → "Replace fix"). Un juego en `DenuvoGames`
  de SLSsteam bloquea el toggle Online (`fixes.py:837-839`).
- **Aplicar un fix** (`fixes.py:95-453`): también la fuente sin cuenta
  `files.luatools.work/GameBypasses|OnlineFix1/<appid>.zip`; extracción con
  guard de zip-slip, rutas case-insensitive, backup first-write-wins en
  `luatools-backup-<appid>/`, `unsteam.ini` con el appid, `OnlineFix.ini
  [Main] FakeAppId` → `FakeAppIds` de SLSsteam, marcador netsock si hay
  FakeAppId y `netsock.so` y no hay EAC/BattlEye, log `[FIX]` en el directorio,
  `freeze_for_files`. Launch options (`compute_fix_launch_options`,
  `:774-813`): `WINEDLLOVERRIDES=<dll>=n,b;…` de los `.dll` del log, o un
  `*launcher*.exe`, más `LD_AUDIT=netsock.so`; las escribe el frontend con
  `SetAppLaunchOptions`, best-effort y en silencio (`GameDetail.tsx:240-252`).
- **Toggle Online** (`:816-937`): FakeAppId 480 siempre, netsock si está, proxy
  EOS si hay `EOSSDK-Win64-Shipping.dll`; marcador `lumadeck-online-<appid>.json`;
  `disable_online` deshace solo lo nuestro; bloqueado con Denuvo y con juego
  poseído. Aviso fijo "Do not use with anti-cheat games".
- **Steamless** (`steamless.py`): `Steamless.CLI.dll` embebido, `dotnet <cli>
  -f <exe> --quiet --realign`, 10 min por exe, swap `<exe>.unpacked.exe` con
  backup `<name>.original.exe`, congela. **Sin deshacer en la UI** (solo queda
  el `.original.exe`). .NET 9 bajo demanda (`dotnet.py`).
- **Goldberg** (`goldberg.py`): sustituye `steam_api(64).dll`/`libsteam_api.so`
  con backup `.valve`, genera `steam_settings/steam_interfaces.txt`,
  `steam_appid.txt`; `remove_goldberg` restaura. **EOS** (`eos_proxy.py`):
  renombra el SDK a `.yes`, copia el proxy, SHA-256; estados
  `none|inactive|active|stale`; revierte. Ambos dependen de `deps/` que solo
  existe en el zip de release.
- **Spliced tickets** (`installer.py:262-393` + `lumadeck-spliced-tickets.lua`,
  de Ace): `Plugins: yes` y copia del plugin a `~/.config/SLSsteam/plugins/`,
  solo con SLSsteam ≥ `20260903114323`; cubre el error SteamStub 65432 (medido
  2026-09-28, §5 F6).
- **Quitar**: `unfix_game` restaura desde backup, reescribe el log, quita
  FakeAppId y netsock si ningún fix superviviente los declara
  (`fixes.py:988-1133`); "Fix Linux Permissions" (`chown` + 755).

**Lista de control.** Denuvo: catálogo de lua.tools, cuenta Discord, slot
manifest + fix. Steamless: sí, sin deshacer. Goldberg: sí. EOS: sí. Aplicar y
quitar: backups por fichero, log por juego, launch options por el frontend.

### 2.7 Logros, stats y tiempo de juego

**Dónde viven.** El esquema de logros de un juego añadido lo consigue
**SLSsteam** pidiendo las stats de reseñadores recientes en nombre de la
cuenta (no tiene almacén propio: guarda Steam en `appcache/stats`;
`slssteam.md` §2.7), pero su código salta ese camino ("schema borrow") cuando
`CUser::isSubscribed(appid)` es verdadero, y con la inyección de licencias de
lumalinux **lo es** también para los juegos añadidos (medido: trace
`appid=1454400 sub=1 added=1`, §5 F7). Por eso lumalinux parchea SLSsteam:
`sls_achievement_unblock` (`sls_achievement_unblock.cpp`) resuelve en el ELF
de `SLSsteam.so` los símbolos `CUser::isSubscribed`, `CConfig::isAddedAppId`,
`g_config`, `Achievements::sendAndRecvGetUserStats` y `…GetPlayerStats` (por
**prefijo** del nombre mangled, porque SLSsteam 20260728 cambió un parámetro y
el patch quedó mudo seis días, `:19-28`), localiza dentro de las dos funciones
el único sitio `call isSubscribed; add esp,imm8; test al,al; jne` (`:71-91`) y
repunta el `rel32` del `call` a `sls_ach_combined_guard`, que devuelve
`subscribed && !added`: un juego comprado se trata igual que siempre y uno
añadido entra en el borrow (`:49-66`). La escritura es un solo `dword`
alineado con la página aún ejecutable, porque la versión anterior (v0.16.2)
hacía una escritura rasgada y Steam moría al entrar en Game Mode cinco veces
seguidas hasta que gamescope borró `~/.local/share/Steam` (OOBE; coredump en
§5 F7; `:101-140`). Fail-closed: cualquier símbolo o sitio que no resuelva
exacto → no-op y `SlsAchievementUnblock: failed`. El mismo guard captura el
`CUser*` para el reconcile (§2.2). Kill-switch `LUMA_NO_SLS_ACH_UNBLOCK`,
traza `LUMA_SLS_ACH_TRACE`.

**Sincronización entre máquinas: CloudRedirect.** Con `stats_sync_enabled`
(Linux) lleva su propio almacén de stats y un blob por cuenta en el proveedor
de nube; contesta `GetUserStats` desde el almacén y deja pasar la petición si
está vacío. Con 2.6.5 y almacén vacío contestaba "sin cambios" y el juego se
quedaba a cero logros; `cr_stats_fix` de lumalinux interponía
`StatsHandlers::HandleGetUserStats` por `LD_PRELOAD` (por eso lumalinux va
antes en el preload) y vaciaba la respuesta de 2 bytes `10 00` para que
CloudRedirect tomara su propio passthrough (`cr_stats_fix.hpp`). Desde
CloudRedirect 2.6.6 el símbolo está oculto y el bug arreglado arriba; el stub
reenvía sin mirar y el log dice "patch not needed" (v0.22.2). Medido
2026-10-07: logro desbloqueado en la Deck, exportado al blob
(`ExportNativeStats app=2379780 … 1 ach blocks`), visible en el PC tras
reiniciar Steam allí (§5 F7). Dos logs de CloudRedirect: `cr_debug.log` (solo
`DoInit`) y `cloud_redirect.log` (todo lo demás).

**LumaDeck.** `achievements.py` genera esquemas con la Web API de Steam
(`GetSchemaForGame`, clave de 32 hex en `credentials.json`) a
`appcache/stats/UserGameStatsSchema_<appid>.bin` y siembra
`UserGameStats_<acc>_<appid>.bin`; **toda la UI está apagada**
(`ACHIEVEMENTS_ENABLED=false`, `features.ts:14`) y la generación automática al
instalar está comentada como prueba temporal (`downloads.py:1585-1595`); las
llamadas de estado siguen disparándose al abrir páginas (§4.1). Stats y tiempo
de juego: nada propio; CloudRedirect reconcilia el tiempo desde
`localconfig.vdf`.

Pendiente externo: Valve retira el endpoint de tienda de esquemas el
2026-10-22; SLSsteam lleva el arreglo (`51724f5`) sin release.

**Lista de control.** Almacén: Steam (`appcache/stats`) + CloudRedirect. Sincronización:
blob de CloudRedirect en su proveedor (Drive, OneDrive, S3, R2 o carpeta), descargado una vez por
arranque. Convivencia: SLSsteam contesta para añadidos gracias al guard;
CloudRedirect contesta desde su almacén o deja pasar. Esquemas: generador de
LumaDeck apagado; SLSsteam los consigue por reseñadores de la tienda
(`MaxSchemaTries: 10`, por defecto).

### 2.8 Cloud saves

Delegado por completo en **CloudRedirect**: `cloud_redirect.so` por `LD_PRELOAD`
intercepta los RPC de Steam Cloud y los sirve desde Google Drive, OneDrive,
S3, R2 o una carpeta (`cloudredirect.md` §2.10). `setup.sh` lo trata como componente core: sin él aborta ("cloud
enabled sin CloudRedirect es incoherente", `:1091-1103`), resuelve la última
release que traiga `cloud_redirect.so` saltando las Windows-only (`:463-524`),
instala la app flatpak `org.cloudredirect.CloudRedirect` para el login del
proveedor y copia `cloud_redirect_cli`. `DisableCloud: no` en SLSsteam porque
empaquetamos CloudRedirect (`setup.sh:227`, `installer.py:121-161`). LumaDeck
solo observa: estados `not_authed` (sin `tokens_<provider>.json`), `disabled`
(marker `~/.config/CloudRedirect/disable`), `not_loaded`, `not_supported` por
`cr_debug.log` ("Init failed: …") (`paths.py:934-1080`); la fila "Cloud saves
need sign-in / Open the CloudRedirect app in Desktop Mode" y nada más: la UI
no puede iniciar sesión. Versión del `.so` en disco contra la última release
con Linux (`components.py:87-114, 165-181`). lumalinux: `libcurl_pin` hace que
CloudRedirect (y nuestras propias descargas) carguen el libcurl del sistema y
no el del Steam Runtime (`libcurl_pin.cpp:102-168`); con 2.6.6 estático solo
afecta a lo nuestro, y se mantiene (opción A, 2026-10-07). Ninguna medición de
un save sincronizado en nuestros docs; sí de logros (§2.7).

**Lista de control.** Redirección: CloudRedirect, cinco proveedores.
Convivencia con Steam Cloud: `DisableCloud: no`; CloudRedirect contesta los
changelists de las apps de `AdditionalApps` (la inyección de cuota en el
appinfo es solo de Windows, `cloudredirect.md` §2.8).

### 2.9 Añadir y quitar un juego

**Entradas** (QAM, `GameList.tsx`): AppID tecleado; AppID detectado de la
página de tienda o biblioteca abierta (`detect_store_appid`: CEF `/json` +
regex `store.steampowered.com/app/N` o `/library/app/N`, `main.py:746-772`;
también en cada `visibilitychange`); nombre (búsqueda de la tienda de Steam,
sin credencial, no-juegos filtrados, DLC incluidos, `api_manifest.py:401-449`).
No hay entrada de zip local; el lua suelto solo por el camino "versión" de
LuaTools. Tarjeta previa con nombre, desarrollador, tamaño, Metacritic,
ProtonDB, avisos (Denuvo, DRM extra, launcher) y "Owned game: Only its DLC
will be added". Gate `canAddGames`: SLSsteam y lumalinux `healthy` **y** Hubcap
o Ryuu en `ok|soon|unknown` (`GameList.tsx:599-604`); mientras no han cargado,
no bloquea. La ruta de biblioteca se manda siempre vacía: **no hay elección de
disco** (`:377-379`; `downloads.py:1378-1381` la ignora).

**Flujo add** (`downloads._download_zip_for_app`, `:1378-1650`):

1. `resolve_owned` (§2.2).
2. Recorre `api.json` en orden (solo `enabled`): Hubcap mueve `?api_key=` a
   `Authorization: Bearer` y loguea la URL limpia; Ryuu añade `Cookie`,
   `Referer`, `Accept`, `Sec-Fetch-*`. Código `unavailable_code` → siguiente;
   401/403 con credencial real → marca `hubcap_key_expired` o
   `ryuu_session_expired` y sigue. Stream al zip con velocidad; magic `PK`.
3. `_process_and_install_lua(pin=pins.pin_new_installs(), owned)`
   (`:1005-1277`): `archive_zip`; extrae; elige `<appid>.lua` o cualquier lua
   numérico; poseído → plan y filtrado (§2.2); no poseído → `_app_line_first`
   (Ryuu pone `addappid(app)` al final y steamidra toma el primero,
   `:853-867`; medido 2026-10-07) y `_enrich_lua_with_linux_depot` (depot
   `oslist=linux` de steamcmd.net, solo si su manifest ya está en depotcache;
   añade `addappid(linux,1,<token windows>)`, `:733-812`); `steamidra_lite`
   con `--manifests-dir`, `--pin` si toca, `--dlc-of-owned` si poseído
   (detectado por `--help`); LumaDeck ya no pasa `--name` (2026-10-09; el
   script lo sigue aceptando e ignorando, `steamidra_lite.py:1180-1185`); post-check: ≥1 clave extendida con
   `parent == appid` en `keys.txt` si el zip traía manifests, si no aborta
   "phantom install" (`:1214-1250`); `prune_retired_keys`.
4. Lo que escribe `steamidra_lite` (`main`, `:1237-1458`): manifests a
   `depotcache/`; `AdditionalApps`; prune de manifests viejos del mismo depot
   (no-pin) y `keys.txt`; `ManifestIds` (solo con `--pin`); `config.vdf`;
   reset del `.acf` si existe en **cualquier** biblioteca (une
   `steamapps/libraryfolders.vdf` y `config/libraryfolders.vdf`, `:700-749`);
   `stplug-in/<appid>.lua` (interop con SteaMidra/DeckTools y la propia
   detección `has_lua_for_app`). **Nunca crea el `.acf`**: lo escribe Steam al
   pulsar Install en la biblioteca que elija el usuario (`:802-809`; un stub en
   la raíz dejaba el juego como "no instalado" tras instalarlo en la SD,
   §5 F9). Backups `.bak` de yaml, vdf, acf y lua.
5. Después: `set_owned`; `loadedappids.txt`/`appidlogs.txt`; `DlcData` si >64
   y `AppTokens` si el lua trae `addtoken()`; poke a SLSsteam; **Proton
   forzado** (`CompatToolMapping proton_experimental` en todos los
   `localconfig.vdf`) si no es poseído y no tiene depot Linux
   (`:1597-1606`); estado `done`. **No reinicia Steam**: el reconcile hace que
   aparezca en la biblioteca y el usuario pulsa Install (§2.2).

**Nombre de carpeta.** Lo decide Steam; el stub `installdir` se eliminó.

**Quitar** (`slssteam_ops.uninstall_game_full`, `:1048-1310`), con confirmación
de dos toques y lista explícita en la UI: poseído → exige juego cerrado y DLC
desmarcados en Steam (§2.3); no poseído → directorio del juego (`rmtree` por el
`.acf`, o por el `installdir` de appinfo o el nombre exacto si no hay `.acf`,
`:709-801`), `.acf` en todas las bibliotecas, compatdata (opcional; raíz +
biblioteca del juego), shadercache (raíz + biblioteca; añadido 2026-10-07),
manifests de depotcache de los depots del lua, `AdditionalApps` (+ DLC si
poseído), FakeAppId, AppTokens, DlcData, `keys.txt` (poseído → `retired_keys.txt`),
marcador ACCELA, esquema de logros (no poseído), y el `.lua` al final;
poseído → `set_owned(False)`. **Deja**: claves en `config.vdf` (a propósito,
`:1259-1262`), la entrada de `pins.json` (`frozen`, `version`, `depot_apps`),
el archivo propio de zips y manifests, `hubcap_attempts.json`. Tras quitar un
juego added sin reiniciar: carpeta, `.acf`, lua, claves y `AdditionalApps`
fuera; el juego sigue en la biblioteca hasta reiniciar (medido 2026-10-07
09:59, §5 F9). Reinstalar baja todo de nuevo: Steam no reutiliza ficheros
fuera de manifest (medido, 861 MB).

**Varias bibliotecas y SD.** `_library_entries` une los dos
`libraryfolders.vdf`, raíz primero (`steam_utils.py:328-384`); `.acf` buscado
en todas (`pins.find_acf`); My Games marca `hasGameFiles` si el `.acf` existe
en cualquiera; `sweep_orphan_stubs` al arrancar borra stubs de la raíz cuando
hay manifest real en otra biblioteca (`downloads.py:1824-1930`). Límites:
shadercache/compatdata solo raíz + biblioteca del juego;
`get_installed_fixes` y `_find_game_dir_fallback` leen solo
`config/libraryfolders.vdf`; `--pin-installed` solo la raíz. Los cinco defectos
originales (issue #41) están arreglados; sus mediciones en §5.2 F9 y las
decisiones en §4.4.

**Lista de control.** Entradas: appid, página abierta, nombre; sin zip local;
sin elección de disco. Escribe: lo de arriba. Quita: lo de arriba, con lo que
deja. Bibliotecas: unión de los dos `libraryfolders.vdf` con tres puntos
ciegos. Carpeta: la decide Steam.

### 2.10 Credenciales y proveedores

- **Hubcap**: clave en `api.json` (`?api_key=` de la entrada Hubcap) con
  espejo `credentials.json.hubcap_key`; guardar vacío la **borra** y
  deshabilita la entrada (v0.11.0); restore al arrancar si el plugin-dir se
  limpió. Caducidad por `/user/stats` (Bearer; gratis): `api_key_expires_at`,
  `daily_usage`, `daily_limit`; estados `none|unknown|ok|soon(≤5 días)|expired`,
  401 → `expired` (`api_manifest.py:457-524`). Cuotas: 25 zips/día, 1500
  manifests sueltos/día.
- **Ryuu**: sesión de Discord. `connect_ryuu` abre `generator.ryuu.lol` en el
  navegador de Game Mode y sondea cada segundo hasta 180 s la cookie `session`
  por CDP (`Storage.getCookies`) o, sin puerto de depuración, descifrando el
  SQLite `Cookies` del CEF (v10: PBKDF2 `peanuts`/`saltysalt` + AES-128-CBC
  por `openssl`; v11: contraseña de `secret-tool`/`kwallet-query`,
  `ryuu_cookie.py:163-306`). Ryuu pone una cookie **antes** del login: cada
  valor nuevo se verifica con un GET a la home buscando `data-user-id` y solo
  se guarda uno logueado (`:445-478`; medido 2026-10-07). Caducidad: fecha de
  la cookie (30 días, aviso a 1 día) **y** comprobación en vivo una vez por
  hora y cookie (`api_manifest.ryuu_session_live`, `:555-591`); 401/403 en
  descarga la marca muerta al momento.
- **LuaTools**: sesión Supabase (`luatools_session.json` + espejo), refresh
  automático, 4xx → `_lumadeck_rejected`; `disconnect` borra fichero, espejo y
  cookies vivas del CEF. Catálogo sin cuenta; descarga y "required gids" con
  token. Desde v0.7.4 (antes el refresh llamaba a un endpoint inexistente,
  §5 F10).
- **Steam Web API key**: solo para esquemas de logros (UI apagada).
- **Sin credencial**: añadir falla (gate); actualizar sigue por depotcache,
  archivo propio y luastools; búsqueda, notices, SteamDB, catálogo LuaTools,
  Steamless, Goldberg, Online, reparaciones y `setup.sh` funcionan.
- Las credenciales se espejan en el directorio de settings de Decky, que no
  se borra al reinstalar el plugin.

**Lista de control.** Exige: Hubcap o Ryuu para añadir; cuenta LuaTools para
fixes; nada para instalar la pila. Obtención: pegar la clave (web abierta en
el navegador de Steam), login con Discord capturado por CDP. Caducidad:
`/user/stats`, fecha + check vivo, refresh de Supabase. Sin ninguna: lo de
arriba.

### 2.11 Mantenimiento propio

**Superficie.** Plugin de Decky (QAM + páginas: juego, biblioteca, Settings
con API Credentials, Components, System, About, Help y una pestaña **Dev
siempre visible**); Quick Install desde Desktop por `quick_install_cli.py`; sin
app de escritorio ni web. CLI de lumalinux: `setup.sh` (`curl | bash`,
`--uninstall`), `steamidra_lite.py`, `vdf_inject_keys.py`. Idiomas `en` y
`pt-BR` (detección por `localStorage` o `navigator.language`; pt-BR sin las
claves de ayuda).

**Update de sí mismo.** `check_plugin_update` compara `package.json.version`
con la última release de `jayool/LumaDeck`; la UI solo ofrece **bajar el zip a
`~/Downloads`** para instalarlo desde Decky ▸ Developer (`self_update.py`,
`Settings.tsx:679-695`); `update_plugin` in-place existe y no se usa. La
release estampa la versión desde el tag (`release.yml:28-60`). `setup.sh` es
reejecutable (sobrescribe `.so`, wrapper, scripts; fusiona `config.yaml` con
la plantilla de la release, conservando lo del usuario y quitando claves
obsoletas, `:156-192`). Las herramientas de runtime se bajan de `main`, el
`.so` de la release (`:60, 93`): pueden desincronizarse (§4.1).

**Update de componentes.** Versión instalada: CloudRedirect y lumalinux por la
cadena compilada en el `.so` de disco (`version=X.Y.Z`, `lumalinux/vX.Y.Z`),
con `status.json` de respaldo para lumalinux; SLSsteam por el tag que
`setup.sh` grabó en `.slssteam.version` (vacío si se instaló con
`SLSSTEAM_7Z_URL`) o un mínimo deducido buscando en `SLSsteam.so` las VERSION
de su `updates.yaml` (`slssteam_version.py`); plugin por `package.json`.
Última: GitHub Releases con caché 6 h en `~/.cache/lumadeck/releases/`,
CloudRedirect la última que publique `cloud_redirect.so`; stale OK sin red;
"no update" en silencio si no se sabe (`update_checks.py`, `components.py`).
Aplicar = **`setup.sh` entero** para cualquier componente (`reinjectInstalled`
+ reinicio; `installer.apply_component`). Settings y el panel usan las mismas
comprobaciones (v0.11.0). Medido 2026-10-07: CloudRedirect 2.6.5 → 2.6.6 en
la Deck por este camino.

**Salud y diagnóstico.** Estados por componente (§2.1), crash guard, pin de
headcrab y soporte por build, `get_components_status` lo agrega; icono Refresh
fuerza el bypass de las cachés; sin visor de logs en la UI; overrides de la
pestaña Dev persisten en el backend hasta "Reset all to real". Logs: §2.1.
CI de lumalinux: tests `tools/test_*.py` en cada build (desde 2026-09-08),
autotest de la cadena de rederivación, smoke test en runtime que nunca ha
pasado.

**Lista de control.** Superficie: QAM + Settings + CLI. Update propio: zip a
Downloads. Componentes: versión del fichero en disco, tag grabado para
SLSsteam, `setup.sh` para aplicar. Salud: estados, guard, logs en tres sitios.

### 2.12 Proyecto

- **Plataformas**: SteamOS (Deck, Game Mode y Desktop) es el objetivo;
  `setup.sh` contempla Arch/CachyOS, Debian/Ubuntu, Void y los launchers
  `steam`, `steam-jupiter(-stable)`, `bazzite-steam`; LumaDeck detecta
  `cachyos`, `bazzite`, `void`, arch-like, debian-like y la familia de sesión
  (`steamos-session-select plasma|desktop`). Cliente de 32 bits
  (`ubuntu12_32/steamclient.so`); canales validados `steam_client_steamdeck_stable_ubuntu12`
  por defecto y desktop/betas a mano. Steam Flatpak no. CachyOS: lumalinux
  cargó `3/3 hooks` en una instalación real (2026-08-04, §5 F12); el port
  sigue abierto (`cachyos-port.md`).
- **Código y ritmo**: dos repos públicos de jayool, releases por tag con CI;
  lumalinux además con PRs automáticos diarios de hashes. Grupo SafeMode
  actual `20260922150000`. Licencias en §0.
- **Servicios de terceros**: GitHub (releases, raw, API, Actions),
  AceSLS/SLSsteam, Selectively11/CloudRedirect (+ su repo flatpak, Flathub,
  runtime KDE 6.10), yesyes0649/steamnetsock-patch y eos-proxy,
  Detanup01/gbe_fork, SteamDatabase/SteamTracking, Microsoft (`dot.net`),
  Valve (`media.steampowered.com`, `api.steampowered.com`, tienda, CDN
  steampipe), `api.steamcmd.net`, SteamDB, hubcapmanifest.com, generator.ryuu.lol,
  manifest.luastools.xyz, lua.tools / db.lua.tools / files.luatools.work,
  `toolsdb.piqseu.cc`, `applist.morrenus.xyz`, protondb.com, los cuatro
  proveedores GMRC, rdbo/libmem, Ghidra (CI). Qué rompe cada uno: §4.2.
- **Telemetría y privacidad**: ninguna. User-Agent `lumadeck-v0-decky` en
  casi todo, UA de Chrome contra SteamDB y lua.tools, `lumalinux/<ver>` y
  `ManifestDeX/1.0` contra los proveedores. La clave de Hubcap va en
  cabecera para no acabar en logs. Se leen y borran cookies del navegador de
  Steam (ryuu.lol, lua.tools) por CDP; se abre un BrowserView oculto para
  SteamDB. `http_client` **desactiva la verificación TLS** si no encuentra
  ningún CA (último recurso con aviso, `http_client.py:218-223`); `setup.sh`
  descarga binarios sin verificar hash ni arquitectura y ejecuta el instalador
  remoto de .NET.
- **Riesgo de detección**: el de SLSsteam y de la inyección de licencias; sin
  medidas específicas. La UI avisa "Do not use with anti-cheat games" en el
  toggle Online.

---
## §3 Superficies externas

Cada una con su función.

### 3.1 Hosts y URLs

| URL | Quién | Para qué | F |
|---|---|---|---|
| `raw.githubusercontent.com/jayool/lumalinux/main/res/updates.yaml` | `update.cpp:72`; `headcrab_compat.py:46` | feed SafeMode; builds soportados por lumalinux | 1, 11 |
| `raw.githubusercontent.com/jayool/lumalinux/main/res/rvas/<sha256>.yaml` | `rva_feed.cpp:63-64` | feed de RVAs del build cargado (sin override) | 1 |
| `headcrab.bifrosthub.ru/client-stable` | `downgrade.sh:44` | espejo de paquetes del cliente para el downgrade (`DOWNGRADE_URL`) | 1, 11 |
| `raw.githubusercontent.com/Deadboy666/SteamTracking/refs/heads/headcrab/ClientManifest/{steam_client_steamdeck_stable_ubuntu12,steam_client_ubuntu12}` | `downgrade.sh:45-46` | manifest del cliente fijado por headcrab (Deck / escritorio) | 1, 11 |
| `raw.githubusercontent.com/jayool/lumalinux/main/{setup.sh,downgrade.sh,tools/steamidra_lite.py,tools/vdf_inject_keys.py}` | `installer.py:47-50`; `desktop_handoff.py:60-67`; `setup.sh:93,139` | instalador, downgrade, herramientas de runtime (de `main`) | 1, 11 |
| `github.com/jayool/lumalinux/releases/latest/download/{liblumalinux.so,version.txt}` | `setup.sh:60`; `headcrab_compat.py:56` | el `.so` y el grupo SafeMode de la release | 1, 11 |
| `github.com/AceSLS/SLSsteam/releases/…/SLSsteam-Any[-release].7z` | `setup.sh:435-452` | SLSsteam (tag por redirect 302) | 1 |
| `raw.githubusercontent.com/AceSLS/SLSsteam/main/res/{config.yaml,updates.yaml}` | `slssteam_schema.py:50`; `slssteam_version.py:64` | esquema de config; VERSION para deducir la versión instalada | 2, 11 |
| `github.com/Selectively11/CloudRedirect/releases` (+ `api.github.com/...?per_page=10` en slow path) | `setup.sh:80-81,483,498`; `update_checks.py:186` | `cloud_redirect.so` (última con Linux) | 8, 11 |
| `raw.githubusercontent.com/Selectively11/CloudRedirect/refs/heads/gh-pages/cloudredirect.flatpakrepo`, `dl.flathub.org/repo/flathub.flatpakrepo` | `setup.sh:88-90` | app de CloudRedirect y runtime KDE 6.10 | 8 |
| `github.com/yesyes0649/steamnetsock-patch/releases/latest/download/fix.so` | `setup.sh:84` | `netsock.so` para fixes online | 6 |
| `github.com/yesyes0649/eos-proxy/…`, `github.com/Detanup01/gbe_fork/…` | `release.yml:70-122` | proxy EOS y Goldberg, solo al empaquetar | 6 |
| `raw.githubusercontent.com/SteamDatabase/SteamTracking/master/ClientExtracted/steam.sh` | `setup.sh:266` | `steam.sh` vanilla al migrar desde headcrab | 1 |
| `raw.githubusercontent.com/Deadboy666/h3adcr-b/main/headcrab.sh` | `headcrab_compat.py:36` | `HeadcrabCompatibleClientVer` | 1, 11 |
| `dot.net/v1/dotnet-install.sh` | `setup.sh:393`; `dotnet.py:45` | .NET 9 para Steamless | 6 |
| `api.github.com/repos/{jayool/LumaDeck,AceSLS/SLSsteam,jayool/lumalinux,Selectively11/CloudRedirect}/releases[/latest]` | `self_update.py:80`; `update_checks.py:125,186` | updates (caché 6 h) | 11 |
| `20770407.xyz/manifest/<depot>/<gid>`, `manifest.manifestdex.com/<gid>`, `gmrc.wudrm.com/manifest/<gid>` (HTTP), `manifest.steam.run/api/manifest/<gid>` | `gmrc_store.hpp:155-158`; sonda `pins.py:461-462` | códigos de petición de manifest | 4 |
| `steampipe.akamaized.net`, `fastly.cdn.steampipe.steamcontent.com` `/depot/<d>/manifest/<gid>/5/<code>` | `gmrc_store.hpp:292-300`; `pins.py:113-114,474` | validar un código con `Range: 0-0` | 4 |
| `hubcapmanifest.com/api/v1/{manifest/<appid>,generate/manifest,user/stats}` | `api_manifest.py:153,507`; `manifests.py:80,330-353` | zip, manifest suelto, caducidad y cuota | 4, 10 |
| `generator.ryuu.lol/download?appid=<appid>&file_type=manifest`, `generator.ryuu.lol/` | `api_manifest.py:154`; `ryuu_cookie.py:445,467` | zip; login y verificación de sesión | 4, 10 |
| `manifest.luastools.xyz/m/<depot>/<gid>` | `manifests.py:74,314` | archivo público de manifests | 4 |
| `raw.githubusercontent.com/sushi-dev55-alt/…/<appid>.zip`, `github.com/SPIN0ZAi/SB_manifest_DB/archive/refs/heads/<appid>.zip` | `api_manifest.py:155-156` | fuentes de zip **deshabilitadas** | 4 |
| `raw.githubusercontent.com/Star123451/LuaToolsLinux/main/backend/api.json` | `config.py:13` | definida, **nunca usada** | — |
| `api.steamcmd.net/v1/info/<appid>` | `manifests.py:73,603`; `downloads.py:752`; `slssteam_ops.py:722`; sondas de lumalinux | PICS: gids actuales, `oslist`, `dlcappid`, `depotfromapp`, `buildid`, `listofdlc`, `installdir` | 2, 4, 5, 9 |
| `store.steampowered.com/api/{storesearch,appdetails}` | `api_manifest.py:441`; `downloads.py:455,542`; `slssteam_ops.py:523,537` | búsqueda por nombre; nombre, notices, DLC | 3, 9 |
| `api.steampowered.com/ISteamApps/GetAppList/v2/` (+ `applist.morrenus.xyz`), `api.steampowered.com/ISteamUserStats/GetSchemaForGame/v2/` | `config.py:30-31`; `achievements.py:38` | applist; esquemas de logros | 7, 9 |
| `steamdb.info/api/PatchnotesRSS/?appid=`, `/depot/<d>/manifests/`, `/patchnotes/<build>/`, `/app/<app>/[patchnotes/]` | `steamdb_reader.py:54,72-89` | builds públicos, historial de depot, página de build | 5 |
| `lua.tools/api/denuvo/{fixes,download}`, `db.lua.tools/auth/v1/token`, `files.luatools.work/{GameBypasses,OnlineFix1}/<appid>.zip` | `luatools_auth.py:46,392,512,705`; `fixes.py:73,83` | catálogo, descarga firmada, refresh, fixes sin cuenta | 6, 10 |
| `www.protondb.com/api/v1/reports/summaries/<appid>.json`, `toolsdb.piqseu.cc/games.json` | `downloads.py:586,702` | tier ProtonDB; "games DB" | 9 |
| `shared.cloudflare.steamstatic.com/…` | `GameCard.tsx:23-28` | portadas | 9 |
| `localhost:8080/json`, `127.0.0.1:8080/json` + `ws://` | `main.py:752`; `cef_cdp.py:42,144` | CEF de Steam: página abierta, cookies, BrowserView | 5, 9, 10 |
| `media.steampowered.com/client/<manifest>`, `/client/<file>` | `fetch_steamclient.py:55-66` (CI) | `steamclient.so` de cada canal | 1 |
| `github.com/rdbo/libmem/…`, Ghidra 11.2.1, `github.com/novnc/noVNC`, Decky `PluginLoader`, `headcrab.pages.dev`, mirrors de SteamOS y CachyOS | `fetch_libmem.sh`; `run_ghidra_derive.sh`; `.devcontainer/*` | build y entornos de desarrollo | 12 |

### 3.2 Ficheros en disco (los que importan en una Deck)

| Ruta | Quién | Qué |
|---|---|---|
| `~/.local/share/SLSsteam/{SLSsteam.so,library-inject.so,path/steam,ensure-desktop-coverage.sh,lumalinux-guard.sh,lumalinux-steam-launcher}` | `setup.sh` (W) | SLSsteam y el wrapper, cobertura, guard, launcher de Game Mode |
| `~/.config/SLSsteam/config.yaml` (+ `.lumalinux.bak`, `.bak`, `.tmp`) | `setup.sh`, `steamidra_lite.py`, `slssteam_ops.py`, `installer.py`, `slssteam_schema.py` (R/W); `pins.py` (R `ManifestIds`) | configuración de SLSsteam (claves en §3.4) |
| `~/.config/SLSsteam/.slssteam.version`, `plugins/lumadeck-spliced-tickets.lua`, `tools/netsock/netsock.so`, `~/.SLSsteam.log` | `setup.sh` (W); `installer.py` (W); `fixes.py` (R); `paths.py` (R) | tag instalado; plugin Lua; netsock; log de SLSsteam |
| `~/.local/share/CloudRedirect/{cloud_redirect.so,cloud_redirect_cli}`, `~/.config/CloudRedirect/{config.json,tokens_<provider>.json,disable,cr_debug.log,cloud_redirect.log}` | `setup.sh` (W); `paths.py`, `components.py` (R) | CloudRedirect y su estado |
| `~/.local/share/lumalinux/{liblumalinux.so,tools/steamidra_lite.py,tools/vdf_inject_keys.py}` | `setup.sh` (W); `downloads.py`, `pins.py` (ejecución); `components.py` (R versión) | el `.so` y las herramientas |
| `~/.config/lumalinux/keys.txt`, `retired_keys.txt`, `no_reconcile`, `no_acf_sweep` | `steamidra_lite.py`, `slssteam_ops.py` (W); `key_store.cpp` (R + inotify); `pins.py`, `steam_utils.py` (R); `downloads.py:830` (R, con `expanduser`) | claves; claves retiradas; kill-switches |
| `~/.cache/lumalinux/{lumalinux.log,.updates.yaml,rvas/<sha256>.yaml}` | `log.cpp`, `update.cpp`, `rva_feed.cpp` | log, cachés de SafeMode y RVAs |
| `$XDG_RUNTIME_DIR/lumalinux/{status.json,gmrc.json}` (fallbacks `~/.cache/lumalinux/`) | `status.cpp` (W); `paths.py`, `pins.py` (R) | salud por sesión; estado de proveedores |
| `~/.local/state/lumalinux/{last_launch,boot_fail_count,safe_mode,safe_mode_fingerprint,last_client,good_client,guard.log}`, `/tmp/dumps/*.dmp` | guard (`setup.sh:836-870`); `paths.py:818-888` (R/B) | crash guard |
| `~/.config/systemd/user/steam-launcher.service.d/lumalinux.conf`, `lumalinux-desktop-guardian.{service,path,timer}` | `setup.sh` (W); `paths.py:631-703` (heal) | cobertura Game Mode y Desktop |
| `~/.local/share/applications/*steam*.desktop`, `~/.config/autostart/steam*.desktop`, `~/Desktop/*steam*.desktop`, `~/.{bashrc,zshrc,profile}` | `setup.sh` (W); `paths.py:498-518` (R) | cobertura Desktop |
| `<steam>/steam.sh` (+ `.headcrab.bak`, `.lumalinux.pre-wrapper.bak`) | `setup.sh:267-330` | restaurado vanilla |
| `<steam>/depotcache/<depot>_<gid>.manifest` | `steamidra_lite.py` (W/D), `manifests.py`, `pins.py` (R/W), `slssteam_ops.py` (D) | manifests |
| `<steam>/config/config.vdf` (+ `.bak`, `.lumabak`) | `steamidra_lite.py`, `vdf_inject_keys.py`, `slssteam_ops.py` (W) | `depots > <id> > DecryptionKey` |
| `<steam>/config/stplug-in/<appid>.lua[.disabled]` | `steamidra_lite.py` (W); `steam_utils.py`, `slssteam_ops.py` (R); `downloads.py` (D) | el lua del juego (interop) |
| `<lib>/steamapps/appmanifest_<appid>.acf`, `libraryfolders.vdf` (en `steamapps/` y `config/`), `common/<installdir>`, `compatdata/<appid>`, `shadercache/<appid>` | `steamidra_lite.py` (reset), `pins.py` (flag), `slssteam_ops.py` (D), `steam_utils.py` (R) | el juego en disco |
| `<steam>/appcache/{packageinfo.vdf,appinfo.vdf,stats/UserGameStats*.bin}` | `steam_licenses.py` (R); guard (D de `appinfo.vdf` al latchear); `achievements.py` (W) | licencias reales; appinfo; esquemas |
| `<steam>/userdata/<uid>/config/localconfig.vdf`, `<steam>/config/loginusers.vdf`, `<steam>/package/steam_client_*.manifest`, `<steam>/steam.cfg`, `<steam>/config/**/Cookies` | `steam_utils.py` (R/W), `achievements.py`, `headcrab_compat.py`, `steam_freeze.py`, `ryuu_cookie.py` | launch options y Proton; cuenta; build; freeze del cliente; cookies del CEF |
| `~/.config/lumadeck/{pins.json,no_spliced_tickets}`, `~/.local/share/lumadeck/{manifests/<app>/,zips/<app>.zip,handoff.sh}`, `~/.cache/lumadeck/{releases/,steamdb/,luatools/,hubcap_attempts.json,headcrab_target,lumalinux_updates.yaml,lumalinux_release_group,slssteam_config_default.yaml}` | LumaDeck | estado propio |
| `<plugin>/backend/data/{api.json,ryuu_cookie.txt,ryuu_cookie_expiry.txt,luatools_session.json,dev_overrides.json}`, `<plugin>/backend/{loadedappids.txt,appidlogs.txt,temp_dl/}`, `<settings>/credentials.json`, `<settings>/pending_update.zip` | LumaDeck | fuentes, credenciales (con espejo), descargas temporales, update |
| Dentro del juego: `luatools-backup-<appid>/`, `luatools-fix-log-<appid>.log`, `luatools-netsock-<appid>.on`, `lumadeck-online-<appid>.json`, `steam_settings/`, `steam_appid.txt`, `*.valve`, `EOSSDK-Win64-Shipping.yes`, `*.original.exe` | `fixes.py`, `goldberg.py`, `eos_proxy.py`, `steamless.py` | fixes |
| `/proc/self/{comm,maps}`, `/proc/*/{maps,environ,comm}` | `.so`; `paths.py`, `steam_utils.py`, `platform_info.py` | proceso; qué `.so` está mapeado; `SteamAppId` de juegos en marcha |

### 3.3 Variables de entorno

| Variable | Dónde | Efecto |
|---|---|---|
| `LUMA_NO_DEPOTKEY`, `LUMA_NO_SHADERSKIP`, `LUMA_NO_GMRC`, `LUMA_NO_PKG0_FINDER`, `LUMA_NO_RECONCILE`, `LUMA_NO_SLS_ACH_UNBLOCK`, `LUMA_NO_CR_STATS_FIX`, `LUMA_NO_LIBCURL_FIX` | `main.cpp:171-173,284,309`; `license_reconcile.cpp:76`; `cr_stats_fix.cpp:179`; `libcurl_pin.cpp:94` | apagan cada pieza del `.so` |
| `LUMA_FORCE_BUILDDEP`, `LUMA_NO_BUILDDEP`, `LUMA_LOADPKG_DEBUG`, `LUMA_NO_LOADPKG`, `LUMA_LOADPKG_IDX`, `LUMA_PKG0_FINDER=diag`, `LUMA_SLS_ACH_TRACE`, `LUMA_GMRC_URL`, `LUMA_PROCESS_ANY`, `LUMA_LOG_LEVEL`, `LUMA_NO_NOTIFY` | `main.cpp`; `patterns.cpp:264-270`; `package_zero_finder.cpp:589-593`; `gmrc_store.hpp:183`; `proc_filter.cpp:11`; `log.cpp:37-51,150` | diagnóstico y pruebas |
| `LUMALINUX_SO_URL`, `SLSSTEAM_7Z_URL`, `CLOUDREDIRECT_URL`, `NETSOCK_SO_URL`, `CR_FLATPAK_RUNTIME`, `LUMA_SKIP_DOTNET`, `LUMA_SKIP_CR_APP`, `VANILLA_STEAM_SH_URL`, `LUMA_STEAM_BIN`, `LUMA_GUARD_MAX_FAILS` (3), `LUMA_GUARD_STARTUP_SECS` (180), `LUMA_GUARD_DUMPS_DIR` (`/tmp/dumps`) | `setup.sh` | fuentes alternativas, saltos, binario real, parámetros del guard |
| `DOWNGRADE_URL`, `DECK_MANIFEST_URL`, `LINUX_MANIFEST_URL`, `STEAM_DIR_OVERRIDE` | `downgrade.sh:35-46` | espejo y manifests del downgrade, directorio de Steam |
| `LD_AUDIT`, `LD_PRELOAD`, `LD_LIBRARY_PATH` | wrapper (`setup.sh:871,884-897`) | inyección; el guard en vanilla las des-exporta |
| `LUMADECK_SETUP_URL`, `LUMADECK_DOWNGRADE_URL`, `LUMA_NO_ACF_SWEEP`, `LUMA_NO_RECONCILE` | `installer.py:47-50`; `desktop_handoff.py:60-67`; `downloads.py:1794`; `slssteam_ops.py:33` | overrides y kill-switches de LumaDeck |
| `SUDO_USER`, `LOGNAME`, `USER`, `HOME`, `XDG_RUNTIME_DIR`, `DBUS_SESSION_BUS_ADDRESS`, `DOTNET_ROOT`, `PATH` | `platform_info.py:141-164`; `desktop_handoff.py`, `installer.py`, `ryuu_cookie.py`, `paths.py`; `dotnet.py` | usuario y home real bajo Decky-root; entorno al ejecutar como usuario |
| `LUMA_STEAM_MANIFEST`, `LUMA_PATTERNS_HPP`, `GH_TOKEN`/`GITHUB_TOKEN`, `LUMA_NO_UPDATE` (cmake) | CI y herramientas | canal de cliente, Ghidra, PRs, gate de hashes compilado |

### 3.4 Claves de configuración y estado

**`config.yaml` de SLSsteam** (esquema completo con defaults en
`config_default.hpp` de SLSsteam). Lo que escribimos:

| Clave | Valor | Quién | Dónde |
|---|---|---|---|
| `SafeMode` | `no` (sed + append garantizado + verificación) | `setup.sh`; `installer.py` | `setup.sh:228,234-241`; `installer.py:164-213` |
| `DisableCloud` | `no` | `setup.sh`; `installer.py` | `setup.sh:227`; `installer.py:121-161` |
| `DisableUpdates` | `no` | `setup.sh`; `installer.py` | `setup.sh:229`; `installer.py:216-259` |
| `NotifyInit` | `yes` (best-effort) | `setup.sh` | `:214` |
| `Plugins` | `yes` (solo con SLSsteam ≥ 20260903114323) | `installer.py` | `:301-321` |
| `AdditionalApps` | `- <appid>` del juego (added) o de sus DLC sin clave (owned); se quita al desinstalar | `steamidra_lite.py`; `slssteam_ops.py` | `:388-411,1305-1327`; `:315-400,682-706` |
| `ManifestIds` | `<depot>: <gid>` — el pin vivo | `steamidra_lite.py` (`--pin`, `--set-pin`, `--unpin`); `pins.py` (R) | `:1003-1052,1374-1401`; `pins.py:492-513` |
| `AppTokens` | `<appid>: <hex>` si el lua trae `addtoken()` | `steamidra_lite.py --token`; `slssteam_ops.py` | `:414-415`; `:407-513` |
| `FakeAppIds` | `<appid>: <fake>` de los fixes online (480) | `slssteam_ops.py` | `:111-207` |
| `DlcData` | `<appid>: {<dlc>: "nombre"}` solo con >64 DLC | `slssteam_ops.py` | `:554-640` |
| `DenuvoGames` | solo lectura (bloquea Online) | `slssteam_ops.py` | `:267-308` |
| claves ausentes del esquema | añadidas con su default (append) | `slssteam_schema.py` | `:272-369` |
| `PlayNotOwnedGames`, `DisableFamilyShareLock` | `yes` solo si `steamidra_lite` crea el fichero desde cero (`:377-383`); `setup.sh` elimina la primera (obsoleta upstream) | ver §4.1 | |
| resto del merge | el fichero sigue el set de claves de la plantilla de la release; lo del usuario se conserva, lo obsoleto se quita | `setup.sh` | `:156-192` |

No tocamos: `AppIds`, `UseWhitelist`, `AppTokens` fuera del lua, `CDKeys`,
`FakeOffline`, `DepotBlacklist`, `IdleStatus`, `GameTitles`,
`SubscriptionTimestamps`, `SteamIdOverride`, `SmartTickets` (0x1),
`MaxSchemaTries` (10), `LaunchOptions`, `WarnHashMissmatch`, `API`, `FakeName`,
`FakeEmail`, `FakeWalletBalance`, `LogLevels`, `DumpClientInterfaces`,
`ExtendedLogging`.

**Ficheros de lumalinux**: `keys.txt` (tres formatos, §2.4); `retired_keys.txt`
(mismo formato, sin entradas sin clave); `status.json` = `{version, pid,
started_at, blocked (siempre null), hooks{DepotKey|ShaderDepot|GMRC|BuildDep|
LoadPackage|PackageZeroFinder|SlsAchievementUnblock|CrStatsFix|Reconcile:
installed|failed|disabled}}` (`status.cpp:116-133`); `gmrc.json` =
`{providers: up|down, at}`; `updates.yaml` → `SafeModeHashes.<grupo>: [hash…]`
(los comentarios `# steam_version` y `# caps` los lee LumaDeck por texto, no el
`.so`); `rvas/<sha>.yaml` → `hooks.*`, `finder.*` (el `.so` ignora
`steamclient_sha256`, `steam_version`, `depotkey_rtti.*`).

**Ficheros de LumaDeck**: `credentials.json` = `hubcap_key`, `ryuu_cookie`
(`session=…`), `ryuu_cookie_expiry`, `luatools_session`, `steam_webapi_key`;
`api.json` = `{api_list: [{name, url, success_code, unavailable_code,
enabled}]}` con la clave de Hubcap en `?api_key=`; `pins.json` =
`{apps: {<appid>: {frozen, fix_id, reason, version{buildid,date,label,gids},
updated_at, owned, depot_apps}}}`; `hubcap_attempts.json` = `{<appid>: epoch,
"single:<depot>_<gid>": epoch}`; `luatools_session.json` = sesión Supabase +
`_lumadeck_rejected`; marcadores por juego (§3.2). Estado de descarga en
memoria: `status` (queued → checking → downloading → processing → installing →
configuring → done | failed | cancelled), `currentApi`, bytes, `steamidraStep`,
`hasLinuxDepot`, `owned`, `ownedDlc`, `ownedInstalled`, `errorCode`
(`hubcap_key_expired` | `ryuu_session_expired`), `hubcapExpired`, `ryuuExpired`.

---
## §4 Lo que nos afecta

### 4.1 Hallazgos pendientes de decisión

Cosas que el código hace y que no coinciden con lo que se cree o se promete.
Ninguna está decidida; cada una es una fila candidata para `ecosystem-matrix.md`
§3 o un cambio. Orden: primero lo que puede estar fallando en una Deck, luego
lo incoherente, luego lo muerto.

**Puede estar fallando**

1. `downloads.py:830` lee `keys.txt` con `os.path.expanduser("~")` para el
   post-check "phantom install", mientras el resto del backend usa
   `real_home()`. `steamidra_lite` se ejecuta sin `--luma-keys` y sin cambiar
   de usuario, así que escribe donde diga `HOME` del proceso de Decky. Funciona
   mientras Decky corra con `HOME=/home/deck`; si fuera `/root`, cada add
   abortaría. No se ha visto fallar; conviene fijar `real_home()` en los dos
   sitios.
2. `remove_game_dlcs` (`slssteam_ops.py:607-640`) borra la primera línea
   `<appid>:` del fichero **entero**, no solo bajo `DlcData:`; podría llevarse
   una entrada de `FakeAppIds` o `AppTokens` del mismo appid. En el uninstall
   el orden lo evita; en otros caminos no se llama.
3. `update_sls_yaml` de `steamidra_lite` inserta `  - <id>` justo debajo de
   `AdditionalApps:`; si la plantilla de SLSsteam usara el estilo
   `AdditionalApps: []`, el resultado sería YAML inválido (`:393-408`). La
   plantilla actual usa la clave vacía, así que hoy no pasa.
4. Las herramientas de runtime (`steamidra_lite.py`, `vdf_inject_keys.py`) se
   bajan de `main` y el `.so` de la release (`setup.sh:60,93`): una Deck puede
   tener un script más nuevo que su `.so` (por ejemplo, un formato nuevo de
   `keys.txt`).
5. El crash guard es ciego sin `/tmp/dumps` (`setup.sh:919-932`): Steam con
   `-nominidumps` o sin breakpad nunca latchea. No está verificado que en Game
   Mode exista siempre ese directorio.
6. `http_client` desactiva la verificación TLS como último recurso
   (`http_client.py:218-223`). En el Python de Decky `certifi` existe (medido:
   "SSL: loaded certs from certifi"), pero el camino sigue ahí.
7. `--pin-installed` solo mira la biblioteca raíz (`steamidra_lite.py:924`);
   shadercache y compatdata solo se borran en raíz + biblioteca del juego;
   `get_installed_fixes` y `_find_game_dir_fallback` leen solo
   `config/libraryfolders.vdf`. Tres puntos ciegos de multi-biblioteca.
8. "Update" de componentes en el QAM es `reinjectInstalled()`, que re-ejecuta
   `setup.sh` entero; `applyComponent(id, "update")` existe y no se usa. Es lo
   deseado hoy, pero el nombre engaña.

**Incoherente con lo que promete**

9. El toast "ACF repaired — Steam will restart" (`i18n.ts:414`) sin que nadie
   reinicie (`GameDetail.tsx:863-872`); la descripción del botón dice lo
   contrario ("Restart Steam afterwards").
10. El deep-link a una pestaña de Settings nunca se consume
    (`routes.ts:18-22`): "Open Settings" cae siempre en la primera pestaña (que
    hoy es API Credentials, por suerte).
11. El estado de credenciales solo se lee al montar el QAM: guardar una clave
    en Settings no desbloquea "Add game" hasta cerrar y reabrir el QAM
    (`GameList.tsx:268-269`).
12. La pestaña **Dev** está siempre visible (`Settings.tsx:1351`) y sus
    overrides persisten en el backend; un usuario puede dejar la UI mintiendo.
13. `ACHIEVEMENTS_ENABLED=false` pero `checkAchievementsStatus` y `loadAchState`
    siguen llamándose al abrir páginas (`GameDetail.tsx:298`,
    `Settings.tsx:337`); la generación automática al instalar está comentada
    como prueba temporal (`downloads.py:1585-1595`).
14. `PlayNotOwnedGames`: `setup.sh` la elimina (obsoleta upstream desde
    20260707), `steamidra_lite` la escribe `yes` si crea el config desde cero,
    `installer._set_playnotowned_no` existe sin llamador, `verify-fix.yml` la
    pone a `no`.
15. `keys.txt` sigue llevando gid y tamaño "para BuildDep", que está OFF; el
    `.lua` de `stplug-in` no se re-comenta al pinear/despinear
    (`_toggle_stplugin_pin` y `purge_depot_manifests` sin llamadores,
    `steamidra_lite.py:957-985`), así que puede quedar incoherente con
    `ManifestIds`.
16. `status.json.blocked` es siempre `null` (`Status::SetBlocked` sin
    llamador) y LumaDeck lo lee como causa `version` de `not_supported`: ese
    camino no puede dispararse desde lumalinux; solo desde SLSsteam por su log.
17. `verify-fix.yml` nunca ha pasado, usa el tag `linux-test` de `h3adcr-b`
    que `setup.sh` abandonó y un `config.yaml` con claves obsoletas; el
    devcontainer instala SLSsteam vía headcrab y no vía `setup.sh`.
18. pt-BR sin las claves de ayuda ni `showMoreResults`, `sysAlignUpSwitching`,
    `sysAlignUpManual`; cadenas en inglés fuera del diccionario (login de Ryuu
    y LuaTools en `Settings.tsx`, textos de fixes en `GameDetail.tsx`, toasts de
    `useLuatoolsConnect.ts`); la pestaña Dev mezcla castellano e inglés. El
    resultado "DLC added…" de un juego poseído (`ownedDlcNextStart`) sale en
    gris porque el verde solo se aplica a `doneRestartSteam`
    (`GameList.tsx:902`). Comentarios de código que describen el flujo viejo:
    reinicio de Steam al añadir (`downloads.py:1529,1608-1615`), "steam.sh"
    en `GameList.tsx:448-450` y `SystemStatus.tsx:55,91,206`; la fuente "GitHub repo" ya retirada en `luatools_auth.py:684`; headcrab como instalador de netsock en `fixes.py`. La descripción de Steamless en `i18n.ts` dice que guarda un `.exe.bak` y el código guarda `<nombre>.original.exe` (`steamless.py:287`). Lista completa
    de la deriva de la UI frente a sus reglas en LumaDeck `DESIGN_UI.md`.
19. El guard al latchear **borra `appcache/appinfo.vdf`** (`setup.sh:939-941`)
    sin comentario que lo explique.

**Código muerto o constantes sin uso**

20. `api.ts`: `verifySlssteamInjected`, `getInjectionStatus`,
    `checkSlssteamHashStatus`, `importRyuuCookieFromBrowser`,
    `cancelConnectRyuu`, `updatePlugin`, `deleteLuatoolsForApp`,
    `checkFakeAppIdStatus`, `checkForFixes`, `applyGameFix`, `cancelApplyFix`,
    `cancelConnectLuatools`. En particular un login de Discord abandonado no se
    puede cancelar desde la UI.
21. `config.py`: `API_MANIFEST_URL`, `API_MANIFEST_PROXY_URL`,
    `HTTP_PROXY_TIMEOUT_SECONDS`, `DEFAULT_HEADERS`.
    `slssteam_ops.remove_depot_decryption_keys` sin llamador (el uninstall deja
    las claves de `config.vdf` a propósito). `installer._set_playnotowned_no`.
22. `gmrc_probe.py` lleva cinco proveedores (con `manifest.opensteamtool.com`)
    y el runtime cuatro: la sonda no es espejo exacto.
23. Hook `LoadPackage`: solo diagnóstico con `LUMA_LOADPKG_DEBUG`; la
    inyección la hace el finder. El patrón sigue en CI como DIAGNOSTIC.

**Biblioteca de lumalinux (`src/`)**

24. `status.json` y `gmrc.json` se escriben con `fopen("w")` directo, sin
    fichero temporal ni `rename` (`status.cpp:91,108`): LumaDeck puede leer un
    JSON a medias si coincide con la escritura. `gmrc.json` se reescribe en
    cada lookup de código.
25. `curl.cpp` no fija `CAINFO` ni toca `VERIFYPEER`: confía en el CA bundle
    del libcurl que cargue. Por eso existe `libcurl_pin`: si el pin falla y
    entra el libcurl 7.22 del Steam Runtime, las descargas del `.so` (SafeMode,
    RVAs, códigos GMRC) pueden fallar por certificados. Es el aviso de Ace
    sobre "libcurl dentro de Steam".
26. El `.so` ejecuta `system("mkdir -p …")` y `system("notify-send …")` desde
    dentro de Steam (`log.cpp:75-76,157-159`): un shell hijo por arranque y por
    toast.
27. `atexit(_exit(0))` en el constructor (`main.cpp:442`) evita un SIGABRT de
    teardown entre SLSsteam y CloudRedirect, y de paso salta cualquier
    destructor del proceso Steam al salir.
28. Las entradas `LD_AUDIT` (`la_preinit`/`la_objopen`) siguen compiladas
    aunque esa ruta corrompe el heap (medido, §5.2 F1); el wrapper no las usa.
    Y `FindBuildDepotDependencyFunction` usa el primer match, no el único
    (`patterns.cpp:94-118,210-212`): solo importa si alguien fuerza BuildDep.

29. **Nada de lo que instalamos se comprueba por hash.** `setup.sh` baja por
    HTTPS de GitHub, sin digest ni versión fijada, todo lo que acaba dentro de
    Steam: el `.so` de lumalinux (`setup.sh:60,1121`), el `.7z` de SLSsteam
    (`:1048`), `cloud_redirect.so` (`:1100`), el `fix.so` de
    `yesyes0649/steamnetsock-patch` (`:84,1112`, cuenta de un tercero) y el
    `steam.sh` "vanilla" de `SteamDatabase/SteamTracking@master` (`:266`).
    LumaDeck ejecuta ese `setup.sh` de `main` sin fijar (`installer.py:49,
    500-560`) y se actualiza a sí mismo con el `LumaDeck.zip` de la última
    release sin comprobar el digest que GitHub publica (`self_update.py:80,160`).
    Lo que lo hace morder: quien controle cualquiera de esas cuentas, o una
    release rota, mete código en el proceso de Steam del usuario en el
    siguiente `setup.sh`. Es el mismo modelo de confianza que el auto-update de
    BetterSteamTools y LuaTools (`bettersteamtools.md` §2.11, `luatools.md`
    §2.11); LuaTools al menos compara el digest del asset. Opciones: comprobar
    ese digest (barato; protege de una descarga alterada, no de una cuenta
    comprometida) o fijar versión y hash de los binarios de terceros. Sin
    decisión.
30. **Las credenciales se guardan con permisos por defecto.**
    `credentials.json` (clave de Hubcap, cookie de Ryuu, sesión de LuaTools),
    `luatools_session.json` y `api.json` se escriben con `open(…, "w")` desde
    el backend de Decky, que corre como root (`api_manifest.py:43,70-71`;
    `luatools_auth.py:55`; `utils.py:21-23`): quedan con el umask, normalmente
    `0644`, legibles por cualquier usuario o proceso local. En una Deck de un
    solo usuario es poco; LuaTools cifra su sesión con DPAPI
    (`luatools.md` §2.10). Arreglo de una línea (`0600`). Sin decisión.
31. **Un manifest que ya está en `depotcache/` se da por bueno.**
    `resolve_manifest` devuelve el fichero si existe, sin validarlo
    (`manifests.py:513-514`), y `place_in_depotcache` no escribe encima de uno
    que exista (`:300-306`). Todo lo que LumaDeck trae de fuera se valida
    (magia y depot/gid de dentro), pero lo que deja otro programa no. ASSella
    copia a todos los `depotcache/` lo que le da Hubcap sin comprobarlo y
    sobrescribiendo (`assella.md` §4.1-6). Con un fichero malo ahí, Steam falla
    ese depot y LumaDeck no lo repone desde su archivo. Arreglo: pasar
    `validate_manifest` también al fichero existente y reponerlo si no valida.
    Sin decisión.
32. **Los flags de SLSsteam solo se reponen al cargar el backend.**
    `ensure_slssteam_flags` (`DisableUpdates: no`, `Plugins: yes`, el plugin
    de spliced tickets) corre una vez por carga de Decky (`main.py:151-183`). Si
    otro programa escribe `DisableUpdates: yes` o `Plugins: no` (ASSella lo hace
    al guardar sus Ajustes, `assella.md` §4.1-2), los juegos añadidos no se
    actualizan y el plugin no carga hasta el siguiente arranque, sin aviso.
    Opciones: comprobarlos en el pase local de `pins.py` o enseñarlos en el
    panel de estado. Sin decisión.
33. **`_enrich_lua_with_linux_depot` puede pisar una clave buena.** Si el
    manifest del depot Linux ya está en `depotcache/`, añade al final
    `addappid(<linux>,1,"<primera clave del lua>")` sin mirar si el lua ya
    trae ese depot (`downloads.py:781-803`). `steamidra_lite` guarda las claves
    en un `dict` y gana la última línea, así que una clave correcta del depot
    Linux se sustituye por la de otro depot. Que el manifest ya esté en
    `depotcache/` es más probable con otro programa que siembra (ASSella copia
    allí todo lo del zip, `assella.md` §2.4). Sin medir (apuntado el 2026-10-05).
    Arreglo: no añadir la línea si el lua ya trae el depot. Sin decisión.
34. **No vemos cuándo otro gestor nos desmonta.** Ni LumaDeck ni `setup.sh`
    comprueban que el `path/steam` siga siendo el nuestro (con
    `liblumalinux.so`), que `ensure-desktop-coverage.sh` no esté vaciado, que
    las sombras `.desktop` sigan apuntando al wrapper o que `keys.txt` no se
    haya renombrado a `*.slsdeck-disabled`. SLSDeck hace las cuatro cosas, la
    primera y la tercera en cada carga de su plugin (`slsdeck.md` §4.1-1, -2).
    El síntoma es un lumalinux que no carga, sin aviso. Opciones: comprobarlo
    en el panel de componentes de LumaDeck (el estado de lumalinux ya viene de
    `status.json`, que tampoco se escribiría) y nombrar al culpable si se ve su
    marca ("neutralised by SLSDeck" en el script, `.slsdeck-disabled`). Sin
    decisión.
35. **El `.so` de CloudRedirect llega por dos caminos que no se coordinan.**
    `setup.sh` (y LumaDeck al actualizar) baja `cloud_redirect.so` del asset de
    la release de GitHub (`setup.sh:70-80,463-524`); la app Flatpak, que
    instalamos de `gh-pages` y pedimos abrir para el login, trae el suyo y
    ofrece reemplazar el desplegado si la versión difiere
    (`cloudredirect.md` §4.1-6). LumaDeck compara la versión en disco con la
    release (`components.py:87-114,165-181`): si la del Flatpak va por
    delante o por detrás, cada lado ofrece volver a cambiarlo. Opciones: tomar
    el `.so` del Flatpak igual que ya tomamos el CLI, o avisar en LumaDeck
    cuando la versión en disco no sea la de la release. Sin decisión.
36. **La rederivación automática nunca ha corrido sobre un movimiento real.**
    Desde junio Valve no ha movido ningún patrón nuestro (§5.2 F1); la cadena
    solo se ha ensayado con patrones corrompidos a propósito, y el ensayo del
    2026-09-14 destapó tres huecos en un día que 75 runs del cron no habían
    visto. Ghidra headless no deriva DepotKey (solo el camino por nombre) y el
    cron solo mira `steamdeck_stable`: una beta que recompila el cliente es
    invisible hasta que llega a estable (§5.2 F1, 2026-09-19). Lo que pasará
    el día que Valve mueva DepotKey de verdad sigue sin ensayar. Sin decisión.
37. **El hash es consultivo: un patrón que resuelva UNIQUE en el sitio
    equivocado se engancha igual.** La defensa es el guard anti crash-loop, no
    un bloqueo (`main.cpp:107-126`; §4.1-16). `status.json` dice `installed`
    pero no distingue "instalado" de "instalado y sirviendo" (`status.cpp`, sin
    contadores). Candidato anotado: un contador por pieza (§4.4, C5). Sin
    decisión.
38. **El pin de build de LumaDeck sigue saliendo de Headcrab.**
    `HeadcrabCompatibleClientVer` del `headcrab.sh` de `Deadboy666/h3adcr-b`
    (`headcrab_compat.py:36`) alimenta la chapa "compatible" y el desbloqueo
    tras un `downgrade.sh` (`target > current AND lumalinux_ready`). Issue
    #26 abierta desde el 2026-09-14; la variante sin Headcrab está anotada en
    §4.4 (C3). Si Headcrab desaparece o fija un build raro, la chapa miente.
39. **Comentarios y un interruptor del `.so` que no decían lo que hace el
    código — corregido 2026-10-10.** Eran comentarios que describían DepotKey
    con un paso RTTI que ya no existe, ponían a lumalinux el último del
    `LD_PRELOAD`, daban a CloudRedirect un libcurl por `dlopen` (desde 2.6.6 lo
    lleva estático), justificaban el User-Agent con opensteamtool, llamaban
    "crítico" al hook de LoadPackage y "diagnóstico" a GMRC, y citaban docs
    retirados (`slssteam-analysis.md`, `slssteam-plugins-analysis.md`,
    `slsteam-moon-findings.md`, `dev-multi-library.md`); y textos de
    `watch-steam.yml` que decían que SafeMode "keeps blocking" y solo nombraban
    `derive_patterns.py` (Ghidra). Ahora dicen lo que hace el código y citan los
    docs actuales. El interruptor: `LUMA_NO_PKG0_FINDER=0` apagaba el finder en
    `main.cpp` (basta con que exista) mientras el finder trataba `0` como no
    puesta; el finder sigue ahora la misma regla que `main.cpp` y que el resto de
    `LUMA_NO_*` (`package_zero_finder.cpp:583`). Sin cambio de comportamiento:
    con `=0` el finder ya no arrancaba.

**Resuelto al leer** (no es hallazgo): el orden `liblumalinux.so:cloud_redirect.so`
del `LD_PRELOAD` es intencional (`cr_stats_fix` interpone un símbolo de
CloudRedirect; sin efecto desde CloudRedirect 2.6.6, que oculta sus símbolos); `target_library_path` se acepta y se ignora porque Steam elige
la biblioteca al instalar, no es un bug sino una función que no existe.

**Comprobado contra los fallos de BetterSteamTools, opensteamtool-cn y LuaTools**
(2026-10-09, LumaDeck `c4776ce`, lumalinux `0e7b7af`), sin hallazgo: LumaDeck no
abre ningún puerto (el único servidor HTTP está en `tests/`); el CDP de `:8080`
es el de Steam, que no abrimos nosotros; ninguna URL en HTTP salvo
`gmrc.wudrm.com`, cuyo código solo se acepta tras aceptarlo el CDN
(`gmrc_store.hpp:157`); ningún espejo de GitHub; los zips de fixes se extraen
con contención de rutas (`fixes.py:165-173`); los manifests se validan antes de
colocarlos; `headcrab.sh` solo se lee para sacar su versión, no se ejecuta
(`headcrab_compat.py:136-151`); el `api.json` de `Star123451/LuaToolsLinux`
(`config.py:13`) es una constante que ya no se usa, la lista de fuentes es la
nuestra (`api_manifest.py:160-185`).

### 4.2 Dependencias de terceros y qué rompe si cae cada una

| Dependencia | Qué deja de funcionar | Evidencia |
|---|---|---|
| Release de SLSsteam (tag numérico de 14 dígitos, asset `SLSsteam-Any[-release].7z` con `bin/SLSsteam.so` y `res/config.yaml`) | `setup.sh` aborta: no hay instalación ni reinstalación. Un cambio de nombre de asset ya dejó la instalación en 404 ~44 h (2026-08-19) | `setup.sh:426-461,1043-1088` |
| Release de CloudRedirect con `cloud_redirect.so` | `setup.sh` aborta (componente core); `api.github.com` con 403 en el slow path también | `setup.sh:474-524,1097-1103` |
| GitHub raw y releases (`jayool/lumalinux`) | sin `setup.sh`, `downgrade.sh`, herramientas ni `.so`; SafeMode y feed de RVAs desde caché; sin checks de update (caché stale) | `installer.py:502-511`; `update.cpp:86-104`; `rva_feed.cpp:71` |
| Hubcap | sin zips nuevos (salvo Ryuu), sin manifests sueltos para builds viejos, depots nuevos no se añaden; la clave queda `unknown` | `api_manifest.py:153,507`; `manifests.py:330-380` |
| Ryuu | solo la fuente 2 y su verificación | `downloads.py:1446-1476` |
| `manifest.luastools.xyz` | el paso 3 pasa a 404; más carga sobre Hubcap | `manifests.py:309-327` |
| Proveedores GMRC (los cuatro) | `gmrc.json` → `down`; todo se congela al build instalado; sin pre-cache de shaders; sin manifest local nada se instala | `gmrc_store.hpp`; `pins.py:818-827`; `shader_depot_hook.cpp:87` |
| CDN de Valve (`steampipe`) | ningún código se entrega aunque un proveedor lo dé | `gmrc_store.hpp:319-322` |
| `api.steamcmd.net` | sin pase de update, add owned rechazado, sin depot Linux, sin `installdir` de respaldo, sin depots compartidos en LuaTools | `pins.py:892-894`; `downloads.py:752,1124-1130`; `slssteam_ops.py:709-732` |
| SteamDB / Cloudflare | selector de versiones inoperante (`needs_user`, `no_builds`), backoff 1 h por IP | `steamdb_reader.py:235-287` |
| lua.tools / db.lua.tools / files.luatools.work | sin catálogo ni descargas de fixes, sin refresh, sin fixes sin cuenta | `luatools_auth.py:304-356,493-577`; `fixes.py:52-92` |
| tienda y Web API de Steam | sin búsqueda por nombre, notices ni `DlcData`; applist cae a `applist.morrenus.xyz`; sin esquemas | `api_manifest.py:429-449`; `downloads.py:432-470,671-694` |
| `media.steampowered.com` (manifests de cliente) | la CI no detecta updates de Steam | `fetch_steamclient.py`; `watch-steam.yml:49-73` |
| `dot.net`, `yesyes0649/*`, `Detanup01/gbe_fork` | Steamless sin runtime; sin netsock, EOS ni Goldberg (solo al empaquetar) | `dotnet.py`; `setup.sh:1110-1117`; `release.yml:70-122` |
| `/tmp/dumps` (minidumps) | el crash guard no latchea nunca | `setup.sh:919-932` |
| puerto CEF 8080 | sin página de tienda, sin captura CDP (cookies por disco), sin SteamDB por navegador, `disconnect_luatools` no limpia cookies vivas | `main.py:752`; `cef_cdp.py:39-58` |
| `steamidra_lite.py` | sin él no hay add, pin, unpin ni set-pin | `downloads.py:69-104` |
| `systemd --user` | sin guardian de Desktop (el wrapper re-asserta por lanzamiento); el drop-in de Game Mode se escribe igual | `setup.sh:747-806,994-999` |

### 4.3 Lo que SLSsteam tiene y nosotros usamos o no

Usamos: `AdditionalApps`, `ManifestIds`, `AppTokens`, `FakeAppIds`, `DlcData`
(>64), `DenuvoGames` (lectura), `Plugins` (spliced tickets), `SafeMode: no`,
`DisableCloud: no`, `DisableUpdates: no`, `NotifyInit`, la recarga por inotify
de `config.yaml`, el callback `AppLicensesChanged_t` para apps nuevas, su
almacén local de logros (con nuestro parche del guard), su hook de
`BuildDepotDependency` como portador del pin. No usamos: `AppIds` /
`UseWhitelist` (unlock selectivo de DLC), `CDKeys`, `FakeOffline`,
`DepotBlacklist`, `IdleStatus`, `GameTitles`, `SubscriptionTimestamps`,
`SteamIdOverride`, `SmartTickets` (queda en su default 0x1), `MaxSchemaTries`
(default 10), `LaunchOptions`, `API` (`/tmp/SLSsteam.API`), `FakeName`,
`FakeEmail`, `FakeWalletBalance`, `DumpClientInterfaces`, `ExtendedLogging`,
`WarnHashMissmatch`. Qué hace SLSsteam con cada una y si alguna nos vendría
bien: `slssteam.md` §4.3.

### 4.4 Decisiones ya tomadas (2026-09-14 → 10-08)

| Qué | Decisión | Fecha |
|---|---|---|
| `DisabledDLC` tocado tanto al añadir como al quitar un DLC de un juego poseído | se mantiene en los dos sitios | 10-06 |
| Estrategias 3 y 4 (prefijo, substring del appid) para encontrar la carpeta sin `.acf` | eliminadas; solo `installdir` de appinfo y nombre exacto | 10-07 |
| Regenerar `DlcData` para juegos con más de 64 DLC | descartado | 10-06 |
| P-ToyStore como fuente de manifests | retirado (404) | 10-07 |
| Manifest suelto de Hubcap (`/generate/manifest`) | hecho, cuarto eslabón de la cadena | 10-07 |
| `depotkeys.json` de api.993499094.xyz como fuente de claves | aparcado: solo si aparece "zip is stale" en un log real | 10-07 |
| Manifest suelto de lua.tools y generador Luie | descartado: exige login en lua.tools | 10-07 |
| SDM como segundo proveedor GMRC (mismo pool que 20770407) | aparcado | 10-07 |
| Parser propio de `appinfo.vdf` (referencia `AppInfoFile.cs` de LuaTools) | aparcado | 10-07 |
| `libcurl_pin` con CloudRedirect 2.6.6 estático | se mantiene (opción A); preguntar a Ace qué URL revienta Steam | 10-07 |
| Logros tras la retirada del endpoint de tienda (2026-10-22) | esperar la release de SLSsteam con `51724f5` | 10-07 |
| Nunca pinear un depot con licencia real; `is_owned` vivo | hecho (v0.11.0) | 10-07 |
| Ryuu como proveedor completo (login verificado, orden del lua, búsqueda por tienda, sesión viva, borrado de la clave de Hubcap) | hecho (v0.11.0) | 10-07 |
| Versión de componentes desde el `.so` de disco; Settings y panel con la misma comprobación | hecho (v0.11.0) | 10-08 |
| Mensaje de `CR-stats` con CloudRedirect mapeado y símbolo oculto | hecho (v0.22.2) | 10-08 |
| C1 — máscara de bytes de layout en `patterns.hpp` (moon M1.1) | ya hecha donde tiene sentido (disp32 de ShaderDepot; frame, miembro de `CUser` y spill en Reconcile) y refutada donde no (issue #16, 07-06: comodín al frame deja BuildDep con 70 coincidencias y LoadPackage con 222). Único byte sin decidir: el `0x44` de `mov eax,[eax+0x44]` en ShaderDepot; enmascararlo si un día se mueve por él, validando con `verify_mask.py` | 09-14 |
| C2 — segundo job del cron sobre `steamdeck_publicbeta` (`LUMA_STEAM_MANIFEST`, `fetch_steamclient.py:65`) | anotado, no ahora: el usuario no usa beta; `probe-steam.yml` sondea a mano | 09-14 |
| C3 — cerrar #26 sin Headcrab: `stack_target` = build más nuevo con hash en nuestro `updates.yaml` y en el de SLSsteam (~20 líneas en LumaDeck, Headcrab de respaldo) | anotado, no ahora: solo actúa tras una rotura real con downgrade | 09-14 |
| C4 — anclas de texto (VProf) como segunda vía automática | anotado: la técnica ya existe para GMRC (`gmrc_xref`) y como respaldo en runtime de ShaderDepot y BuildDep; #13 y `b599366` midieron que no sirve para DepotKey ni LoadPackage. Reabrir si un patrón con ancla se mueve. De la lista de prioridades de septiembre: el walk-back que falla cerrado y la vtable por nombre (DepotKey) están hechos | 09-14, estado 10-09 |
| Antes de anclar un hook en un **sitio de llamada**, el auditor (`classify_hit_count`) tiene que aceptar N coincidencias que **convergen** al mismo destino; hoy marca AMBIGUOUS cualquier n>1 y el primer hook que se mueva saldría BLOCKING sin estarlo (slsteam-moon tropezó con un localizador de veinte llamadores) | requisito, sin trabajo abierto | 09-08 |
| Lo que no hacemos al instalar o reparar (vectores de SteaMidra que pueden dejar una Deck en OOBE, `steamidra.md` §4.4): nunca escribir en `/usr` ni pedir `steamos-readonly disable` (Game Mode por el drop-in de `steam-launcher.service`); nunca SIGKILL a Steam; no tocar `steam.sh`; mantener el guard (tres crashes → vanilla) por delante de `short_session_recover` | vigente | 09-14 |
| C5a — `fencepost` / comprobación del slot en runtime (BST) | no: la clave por hash y el resolvedor por nombre ya lo cubren | 09-14 |
| C5b — atestación de hooks (contador por pieza en `status.json`, ~30 líneas) | anotado, candidato real para una release futura (§4.1-37) | 09-14 |
| C5c — segundo mirror del feed (jsDelivr) | no mientras `raw.githubusercontent.com` funcione: un mirror ajeno obliga a firmar el feed (`design/rva-feed-design.md` §14) | 09-14 |

#### Decisiones de diseño de LumaDeck (antes en LumaDeck `DESIGN.md`, borrado el 2026-10-09)

Las que siguen vigentes, con la alternativa descartada y el motivo. La 11
(watchdog de `.acf` atascados, #21) la sustituyó el trabajo de fondo de
`pins.py` (§2.5).

| Decisión | Alternativa descartada | Motivo |
|---|---|---|
| Descarga Steam, con los hooks de lumalinux | DepotDownloaderMod (DeckTools) | El disco queda como el de un juego comprado, Steam lo actualiza y muestra el progreso en su biblioteca |
| Partir del backend de DeckTools y cambiar solo el motor de descarga | Reescribir desde cero | Frontend, operaciones de SLSsteam y fixes ya eran lo que se quería |
| Se elige el juego por la tienda (AppID detectado), por nombre o por AppID; luego **Add game** e Install en Steam | Lista externa | Es el flujo natural y no exige saber el AppID |
| Opciones avanzadas por juego (manifest, fixes, versión) | Solo un botón de instalar | Paridad con LuaToolsLinux |
| Fuentes de manifests: Hubcap (clave) y Ryuu (cookie); Sushi y Spinoza en `api.json` pero apagadas | Solo Hubcap | Las espejo gratuitas van por detrás de los lanzamientos (`api_manifest.py:143-156`) |
| Toda la pila con un solo `setup.sh` (modelo wrapper) | Instalación previa; headcrab | Un paso idempotente, sin parchear `steam.sh` ni orden de pasos |
| Menú jerárquico (lista → detalle) | Una sola pantalla; pestañas | Aprovecha el espacio del QAM |
| Reutilizar configs de DeckTools/LuaToolsLinux (`api.json`, cookies) | Pedirlo todo de nuevo | Evita trabajo a quien ya las tiene |
| Solo Game Mode (salvo el hand-off a Desktop que se arma desde ahí) | Game Mode + Desktop | En Desktop ya están SFF y ASSella |
| Pipeline DDL eliminado de `downloads.py` | Dejarlo como código muerto | El flujo nativo está probado; ~1250 líneas sobraban |
| Repair appmanifest **borra** el `.acf` | Reconstruirlo | La reconstrucción lo dejaba en 0444 y Steam no podía actualizarlo |
| Clave de Hubcap en cabecera `Bearer`, nunca en la URL | `?api_key=` | No acaba en los logs (`downloads.py:1420-1440`) |
| Rutas `/lumadeck/*`, nombres y claves i18n de LumaDeck | Mantener los de DeckTools | Sin choques si conviven los dos plugins |
| `steam -shutdown` como el usuario real | Como root | El IPC es por usuario; como root no llega (v0.3.0). Añadir un juego ya no reinicia Steam |
| Añadir un juego no escribe `appmanifest` ni marcas de ACCELA | Stub `.acf` | El stub causaba la #41: Steam veía el juego como no instalado y lo bajaba otra vez (v0.7.4) |
| Toda pregunta de "¿está instalado, dónde?" lee **todas** las bibliotecas por `_library_entries()` | Solo la raíz, con tres parseos distintos | Un juego en la SD o en otra partición salía mal |
| Leer las dos copias de `libraryfolders.vdf` (`steamapps/` y `config/`) y unirlas | Solo la que carga Steam | Ninguna copia puede esconder una biblioteca; las entradas sin `steamapps/` se descartan |
| Los stubs viejos se barren solos al cargar el plugin, solo si hay un `.acf` real en otra biblioteca | Botón manual | Nadie pulsaría un botón para un síntoma que no apunta al stub. Interruptor `LUMA_NO_ACF_SWEEP` o `~/.config/lumalinux/no_acf_sweep` |
| Una sesión de LuaTools rechazada se marca, no se borra, y solo cuenta un 4xx | Borrarla ante cualquier fallo | Un 401 suelto puede ser Cloudflare; borrar tira un refresh token aún válido |
| Borrar una credencial borra también su copia en el directorio de settings | Borrar solo el fichero | La copia se restaura en cada carga y deshacía el borrado (v0.7.5) |

---
## §5 Historial

Lo que se probó y lo que se creía. §5.1 recoge las afirmaciones que circulaban
sobre el stack y que el código o una medición desmienten, para que no vuelvan
a escribirse como hechos, y las preguntas que siguen sin medir. §5.2 recoge
cada prueba hecha con fecha, por función, con su resultado; la columna
"Detalle" apunta a donde está la narración larga cuando sigue existiendo
(`RESEARCH.md`, `maintenance.md`, `docs/design/`, los docs de LumaDeck).

### 5.1 Lo que se creía y ya no es así

- **GMRC es opt-in.** Activo por defecto desde v0.21.0 (`main.cpp:170-174`).
  Lo fue en v0.20.x.
- **ShaderDepot devuelve 0 para todo depot nuestro.** Es condicional: solo
  para juegos sin clave de shader o cuando ningún proveedor responde
  (`shader_depot_hook.cpp:57-96`).
- **lumalinux no retira del package-0 las licencias que salen de
  `keys.txt`.** Lo hace desde `4f8579c` (v0.22.2,
  `load_package_hook.cpp:199-251`), medido el 2026-10-07 (§5.2 F2).
- **Steam lee `config/depotcache/`.** No; solo `depotcache/`, medido el
  2026-09-11 (§5.2 F4). `steamidra_lite` dejó de escribir esa copia en 0.20.1.
- **El `steamclient.so` de escritorio y el de Deck son el mismo binario.**
  Dejó de ser cierto el 2026-09-22 y se reconfirmó el 10-01: misma versión
  `1788652215`, hashes distintos (§5.2 F1). La misma suposición para Bazzite
  cae con ella y no se ha verificado.
- **La cadena de manifests pasa por P-ToyStore.** El repo desapareció
  (404 el 2026-10-07); la cadena es depotcache → archivo propio → luastools →
  manifest suelto de Hubcap → zip (`manifests.py:501-567`).
- **Sin `gmrc.json` LumaDeck aplica el modelo 0.8 (pinear todo).** Desde el
  2026-10-06, ausente más hook GMRC instalado se lee como `up`
  (`pins.py:404-419`).
- **El zip siempre se instala con `--pin`.** Lo decide
  `pins.pin_new_installs()` según el estado de los proveedores
  (`downloads.py:1539-1541`).
- **El self-update sobrescribe el plugin y reinicia Steam.** La UI solo baja
  el zip a `~/Downloads`; el camino in-place existe y no se usa
  (`Settings.tsx:679-695`, `self_update.py:223-257`).
- **La cookie de Ryuu v11 no se puede descifrar.** Sí: con la contraseña de
  `secret-tool` o `kwallet-query` (`ryuu_cookie.py:163-306`).
- **`status.json.blocked` dispara el estado `not_supported` por versión.**
  lumalinux nunca escribe `blocked` (`Status::SetBlocked` sin llamador); esa
  causa solo la dispara el log de SLSsteam (`main.cpp:270-276`,
  `paths.py:1205-1226`).
- **El AppID 2379780 es Mina the Hollower.** Es Balatro; la prueba de
  `method.md` §8 lo atribuía mal.
- **`dev-backend-reference.md` de LumaDeck** describía la cadena con
  P-ToyStore y `--pin` siempre; el doc se borró el 2026-10-09 (el mapa es §1.3).

**Lo que sigue sin medir** (y por tanto no se afirma en §2):

- Que la clave de un depot sea estable entre builds. La misma clave descifró
  deltas en 2026-06 y 2026-09; no hay un caso de rotación observado.
- Que un rescate (RTTI, xref, anchor) haya disparado alguna vez en un
  dispositivo real; `method=rva` del finder y las ramas AMBIGUOUS tampoco.
- Una update real de Valve con el modelo `up` (nativa) y con `down`
  (pin movido por el pase de 30 min) sobre la Deck; solo la de 2026-09-13
  (pin, §5.2 F5).
- El pin sobre un juego poseído: si congela el base al salir un build nuevo.
- `downgrade.sh` en vivo.
- La causa raíz de LumaDeck #31 (CachyOS: hooks en el primer arranque, nada
  en el segundo).
- Si el `OnlineFix.ini` automatiza netsock (Valheim).
- El coste de arranque con el skip de 60 s por proveedor caído, después de
  implementarlo.

### 5.2 Pruebas y mediciones con fecha

Una tabla por función. "Dónde": Deck (la del usuario), PC (Arch nativo),
codespace (contenedor SteamOS o Arch con Steam real), devcontainer, CI,
estático (análisis del binario sin ejecutarlo), red (sondas HTTP).

#### Función 1 — Engancharse a Steam

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-05 | Deck | Binario objetivo | `linux32/steamclient.so`, ELF i386, ~48 MB, BuildID `f92deb5e…`, ~826 símbolos exportados | RESEARCH §8 |
| 2026-05/06 | Deck, codespace | lumalinux en `LD_AUDIT` en vez de `LD_PRELOAD` | Steam muere al arrancar con `realloc(): invalid pointer`; el launcher de 64 bits rechaza el `.so` (`wrong ELF class: ELFCLASS32`). CloudRedirect en `LD_AUDIT` falla igual | RESEARCH §5 |
| 2026-06 | PC | `LD_PRELOAD` como variable de entorno frente al parche de `steam.sh` | La variable no sobrevive al re-exec del cliente: la instalación produce un `.acf` fantasma de 0 bytes. El parche de `steam.sh` sí funcionaba; hoy lo hace el wrapper | RESEARCH §14.3 |
| ≤v0.8 | codespace | Inyectar depots en `pDepotInfo` desde BuildDep | SIGSEGV posterior (llamada virtual sobre puntero nulo en la ruta de appinfo). Por eso BuildDep solo parchea | RESEARCH §6 |
| ≤v0.8 | codespace | Sustituir a mano `m_pMemory` del `AppIdVec` | Crash del hilo de appinfo-cache en `libvstdlib_s.so`. Resuelto usando el `realloc` del propio Steam (único helper hoja entre los cinco llamadores de `realloc@plt`) | RESEARCH §6, §11.2 |
| ≤v0.8 | codespace | Ver GMRC desde un hook en `InitFromPacket` | El hook corrió menos de 513 veces y nunca para GMRC. Callejón sin salida | RESEARCH §6 |
| 2026-06 | codespace Ubuntu 24.04 | Crash con Formula Legends (3194360, redists 228989/228990) | `free(): invalid pointer` al volver del hook DepotKey. Aislado con `LUMA_NO_GMRC`/`LUMA_NO_DEPOTKEY`: sin lumalinux no hay crash; DepotKey apagado → `Missing decryption key`; el crash se correlaciona con servir los redists poseídos. Causa: el hook ≤v0.8 cortocircuitaba el dispatcher externo y dejaba estado a medias; `out_key` es un struct de 128 B, el buffer no era el problema | RESEARCH §12 |
| 2026-06 | codespace (root, `/proc/PID/mem`) | Resolver el accessor interno de claves | `this=*(global)+0xd60; vtable=*this; fn=*(vtable+0x18)` leído del Steam vivo; de ahí `kDepotKeyFnPattern` v1.0, que sirve 228989, 228990 y 3194361 sin un solo volcado (13 GB, `StateFlags=4`) | RESEARCH §12.5-12.6 |
| 2026-06-24 | estático, build `7c4ac73e` | Patrones: GMRC, ShaderDepot, wildcard del frame size | GMRC `E8 ?? ?? ?? ?? 05 … 8B 4D 20` único; `GetShaderCacheDepot` único con un solo llamador (`CGetShaderDepotManifestJob`). Poner comodín al `sub esp,imm32` deja GMRC único pero hace colisionar BuildDep y LoadPackage (6 coincidencias cada uno); los frame sizes sobrevivieron `f92deb5e→7c4ac73e` | RESEARCH §4, §8.1 |
| 2026-06-24 | estático, build `7c4ac73e` (Ghidra) | Camino B: parchear el descifrado del manifest para saltar el shader por juego | Función en `0xfa9050` con dos llamadores; no hay un seam donde devolver "éxito sin manifest". Descartado; se hizo el hook `GetShaderCacheDepot` | RESEARCH §13.9 |
| v0.13.1 | PC (Ghidra Jython) | `Memory.findBytes` con comodines en patrones cortos | Devuelve cero resultados aunque los bytes existan; por eso el validador busca el literal final y comprueba alrededor | RESEARCH §8.1 |
| v0.10.x | codespace | LoadPackage instalado pero nunca disparado tras una update de Steam | `LoadPackage hook: INSTALLED` sin un solo `PackageId=0 hit`; con `LUMA_LOADPKG_DEBUG=1`, Steam no llama a `LoadPackage` para un package 0 ya cacheado. Origen del finder activo | RESEARCH §13.1-13.3 |
| 2026-06 → 2026-09-08 | estático, cinco builds | `disp32` del idiom de acceso a `CPackageInfoCache` | `7c4ac73e→0x3a1bc`, `db0d79c2→0x3967c`, `d0c0ff6e→0x3b314`, `bc54101b→0x3b7d4`, `237495b4→0x3b7d4`; en cada build exactamente un valor distinto, en dos sitios (`0xfdd2dd`, `0x18964e5`) | RESEARCH §13.5; maintenance §E |
| post-2026-05 | Deck | Finder en builds posteriores sin tocar código | Funciona en `7c4ac73e` y `db0d79c2` con los offsets `+0xc58` (raíz) y `+0xc6c` (nodos), nodo de 0x18 B | RESEARCH §13.4 |
| 2026-09-08 | codespace SteamOS, build `237495b4` | Localizador del finder reescrito, en vivo (solo se sustituyó el `.so`) | `GOT UNIQUE — 1 site(s)`, `cache-access idiom UNIQUE — 2 site(s), disp=0x3b7d4`, `outcome=resolved`, `HIT PackageId=0 AppIdVec{size=196}` con `{5,7,8,90}`; `3/3 hooks active`. No ejercitado: `method=rva`, ramas AMBIGUOUS, `outcome=miss` | RESEARCH §13.5.b; maintenance §C |
| 2026-09-22 | estático, beta `1790036264` (`9cf4720f`) | Build que recompila `steamclient.so` (49,8 → 53,5 MB) | Las dos anclas del finder fallan. 26 campos de `CPackageInfoCache` casan uno a uno con el stable, todos `0x338` más arriba: raíz `0xc58→0xf90`, nodos `0xc6c→0xfa4`, `X 0x3b7d4→0x3c7b0`. Reconcile se rompió por fijar `edi`; corregido, único en ambos. Es el layout `beta-0xf90` que el finder conoce | RESEARCH §13.5.c; maintenance §C |
| build `f5eb8bd3` | estático | RTTI aplicada a cada hook | DepotKey es virtual (`CConfigStore`, vtable de 26 slots, slot 6 = `0x1188300`); GMRC, BuildDep, ShaderDepot y LoadPackage no lo son. `check_patterns.py` deriva el slot 6 de forma única en `f5eb8bd3` y `1782861641` | RESEARCH §15.2-15.3 |
| 2026-07-06 | Deck | DepotKey resuelto por RTTI en producción | Enviado en release; `experiment_rtti_depotkey.py` fue la puerta | maintenance §A.2 |
| v0.16.2 → v0.16.7 | Deck (Game Mode) | Escritura rasgada del `rel32` del parche de logros | Steam cayó cinco veces seguidas al entrar en Game Mode; gamescope (`short_session_recover`) borró `~/.local/share/Steam` y la Deck volvió al OOBE. Mismos bytes en escritorio, sin fallo. Coredump: `trap invalid opcode … in liblumalinux.so`, pila en `YAML::Scanner::PushIndentTo`. Arreglo: un solo `dword` alineado, saltando el sitio si cruza línea de caché (~6 % de los casos) | RESEARCH §17.3-17.4; maintenance §D |
| v0.18.0 | Deck | Feed de RVAs en vivo | Fase 3: `method=rva` apunta a la misma dirección que el patrón; fase 4: `RvaFeed: loaded 4 hook RVA(s)` con descarga HTTPS real y caché en disco. `xlate_vaddr.py` validado (`0x118c1f0 → 0xc7bcf1f0`) | design/rva-feed-design.md §12 |
| build `bc54101b` | estático | Copia rancia del patrón DepotKey en `derive_patterns.py` | Daba "UNIQUE" sobre `0x189fca0`, que no es el accessor (`0x11a4500` en ese build). El validador se corrigió | maintenance §A.3 |
| 2026-09-14 / 09-22 | CI (`watch-steam-selftest.yml`) | Cadena de rescate by-name de DepotKey | Patrón corrompido → exit 3 → derivación por nombre → aplicar → revalidar limpio. Ghidra headless nunca resolvió la vcall indirecta de DepotKey en ningún build; desde el 09-22 el script solo valida | maintenance §A.2 |
| todos los builds medidos | CI | Ancla de Reconcile (`push 0x7d; push eax; call` + lee `this` + `jle`) | Exactamente una coincidencia en cada build; Ghidra nunca ha tenido que arrancar | maintenance §A.2 |
| sin fecha | codespace, Deck | `liblumalinux.so` compilado para 64 bits copiado al sitio | Copia limpia y nunca carga (`ELFCLASS32 incorrect`), sin toast: parece un fallo del wrapper y no lo es | maintenance §C |
| 2026-10-06 16:52 | codespace SteamOS | Sustituir `liblumalinux.so` con Steam abierto | Steam vuelca (`assert_…dmp`) al pasar por el hook. El `.so` solo se reemplaza con Steam parado | — |
| sin fecha | PC (`-O2`, Xeon 2,1 GHz) | Coste del sigscan de `patterns.cpp` | Parseo de `/proc/self/maps` 0,06-0,2 ms; `SigScan` sobre 9 MB 18,6-19 ms; cuatro escaneos por arranque (DepotKey, GMRC, ShaderDepot, Reconcile) ≈ 75 ms. SHA-256 de 12 MB ≈ 12 ms | — |
| 2026-08-08 → 08-11 | Deck | `setup.sh` (modelo wrapper) de extremo a extremo | Instalación limpia: cuatro `.so` mapeadas en el cliente de 32 bits, `status.json` con hooks activos. Migración desde headcrab: `steam.sh` vuelve a vanilla, Game Mode arranca por el drop-in de systemd. Fail-safe probado en los dos sentidos (inyecta / latcheado arranca vanilla); el guardian genera y habilita las units, `apply` idempotente, `uninstall` restaura | §2.1 |
| 2026-08 | Deck | ¿Llega el PATH drop-in a la sesión gamescope? | No. De ahí el drop-in en `steam-launcher.service` | §2.1 |
| 2026-08 | codespace Arch limpio + Deck | Validación completa del wrapper | Cargan los cuatro `.so` (SLSsteam por `LD_AUDIT`; CloudRedirect, lumalinux y netsock por `LD_PRELOAD` en el proceso de 32 bits); `status.json` con hooks activos y `blocked=null`; un juego no poseído descarga. Único pendiente: el downgrade nunca ejercitado | §2.1 |
| 2026-08-04 | devcontainer CachyOS real | lumalinux en CachyOS | `3/3 hooks active` en una instalación real (DepotKey, GMRC, ShaderDepot, finder); `distro=cachyos`; `steamos-session-select` tiene `plasma` y no `desktop`; el `short-session-tracker` cuenta hasta 3 y `do_repair()` re-extrae el bootstrap | cachyos-port.md; LumaDeck docs/porting-cachyos.md |
| 2026-08 | devcontainer CachyOS / Arch | Desfase de headcrab | El manifest de cliente Linux (build 1784669098) iba seis días por detrás de `HeadcrabCompatibleClientVer` (1785187029): SLSsteam abortaba por hash desconocido y LumaDeck decía "build not supported". El manifest de Deck sí estaba alineado | LumaDeck docs/porting-cachyos.md |
| sin fecha | campo (CachyOS Handheld, issue #31) | Primer arranque tras instalar | Todos los hooks inyectados en el primer arranque de Steam; en el siguiente nada, Steam colgado, pantalla negra al volver a Game Mode. Sin logs; sin causa raíz | cachyos-port.md |
| sin fecha | CI, devcontainer port-testing | Patrones y hash contra el `steamclient.so` de escritorio del build que headcrab pineaba | Patrones limpios y hash igual al de Deck en el whitelist. Dejó de valer el 2026-09-22 (binarios distintos) | cachyos-port.md |
| 2026-09-02 → 09-16 | CI (`watch-steam.yml`) | Whitelist del Deck stable | `1788291500` (`bc54101b`) whitelisteado el 09-02; los 14 runs diarios siguientes salieron en cache-hit sin nada nuevo | — |
| 2026-09-19 | CI (`probe-steam.yml`) | Una beta que recompila el cliente | Invisible para el monitor diario, que solo mira `steamdeck_stable` | maintenance §A.2 |
| 2026-10-01 | CI (`probe-steam.yml`, `readelf -n`) | Canales de Steam contra nuestros patrones | Deck stable y escritorio stable dicen `1788652215` y sirven binarios distintos (`bc54101b`/`a577b836` frente a `237495b4`/`29734b56`); Deck beta y escritorio beta `1790721607` comparten `a3661f5b`. Los cuatro limpios; la beta cae en el layout `beta-0xf90`. Solo existen `steamdeck_stable` y `steamdeck_publicbeta`; `main`, `preview` y `beta` dan 404 | maintenance §A.2 |
| 2026-10-07 22:16 | Deck | Update de CloudRedirect desde LumaDeck (`setup.sh`) | `.so` y última línea de `cr_debug.log` en `2.6.6+3434d9d`, `DoInit: SUCCESS`, `cloud_redirect.so` mapeado seis veces en `steam`. lumalinux v0.22.1 decía "cloud_redirect.so not loaded" porque el símbolo ya va oculto; texto corregido en v0.22.2 | lumalinux `e92b763` |
| 2026-10-08 | build local | `liblumalinux.so` con el `cr_stats_fix` de v0.22.2 | Compila; `cr_stats_fix_selftest/run.sh` 6/6; las únicas cadenas de versión del binario son `lumalinux v0.22.1 preinit` y `lumalinux/v0.22.1` | `tools/cr_stats_fix_selftest/` |
| 2026-06-11 → 09-14 | CI + historial git | Cuántas veces ha roto Valve un patrón nuestro | **Ninguna rederivación** en 11 builds: un solo grupo SafeMode (`20260611150000`); las huellas de DepotKey, BuildDep, GMRC y LoadPackage son byte a byte las de mayo. Cron desde el 07-01: 75 runs programados, 75 verdes, 5 hash bumps automáticos (07-22, 07-23, 07-28, 08-04, 09-02), 0 "moved". Única máscara preventiva: ShaderDepot el 06-24 (disp32 de global), el día de su creación | en la misma ventana SLSsteam tocó firmas 4 veces y moon 5 (`slssteam.md`, `slsteam-moon.md` §5.2 F1): enganchamos prólogos que Valve no reordena, ellos despachadores IPC |
| 2026-09-14 | CI (`watch-steam-selftest.yml`, build `bc54101b`) | Nueve runs del autotest sobre el binario estable real | #4 (shaderdepot, exit 2) verde en 15 min: Ghidra rederiva BuildDep, GMRC y ShaderDepot UNIQUE; el auto-derive RTTI de Reconcile no resuelve ("type_info not referenced by any function") y no bloquea. #5 rojo: corromper BuildDep+GMRC (diagnósticos) daba CLEAN y nunca llegaba a Ghidra (`dda90dc`). #6 rojo: `blocking_constants.py` no conocía `DepotKey-RTTI:no-slot-matches-pattern` y un DepotKey movido iba a issue (`63a5a0a`). #7 rojo en 20 min: Ghidra no resuelve el `CALL [reg+0x18]` y `derive_patterns.py` validaba una copia rancia. #8 rojo: `KeyError` en un `print` de la herramienta nueva. **#9 verde en 23 s**: exit 3 → `derive_depotkey_byname.py` (GetBinary → slot 6 → `0x11a4500`, 30 bytes UNIQUE) → aplicar → revalidar con RTTI y nombre de acuerdo, sin Ghidra | hasta el #9 el camino del único crítico no era autónomo; la Deck no se habría roto (rescate RTTI desde 07-06) |
| 2026-09-14 | estático | Requisitos de versión del `.so` | `liblumalinux.so` exige GLIBCXX 3.4.32 / GLIBC 2.38 | dato de portabilidad; SteamOS lo cumple |

#### Función 2 — Propiedad y licencias

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| ≤v0.8 | codespace | Qué ids meter en el package 0 | El AppID (2379780) da `0 target`; los ids de depot (228989, 2379781, 2379782) sobreviven el filtro de licencia | RESEARCH §6 |
| 2026-06 | codespace | Detectar la propiedad real desde el hook (tres intentos) | `CheckAppOwnership` no sirve (SLSsteam lo falsea); llamar primero al dispatcher original funciona para 3194361 pero la RPC denegada deja estado que hace caer a Steam; cache-first tiene un efecto lateral en el camino de fallo que también cae, incluso con buffer de prueba. Por eso el hook sirve la clave a cualquier depot de `keys.txt` | RESEARCH §12.4 |
| v0.10.11 | codespace, Brotato 1942280 | Finder de extremo a extremo | `cache-access idiom found 2 time(s), disp=0x3a1bc` → `HIT PackageId=0 AppIdVec{size=192}` → `APPENDED 8 depot id(s)`; BuildDep ve 2868390/1942282; `SERVED local key` para 2868390, 1942282, 1942280; GMRC inyecta el código del shader; `finished update, 2 mounted depots`, `Fully Installed` | RESEARCH §13.6 |
| 2026-06 | PC | Depots con `gid=0` en el package 0 | Siguen inyectados (`all N depot ids already in PackageId=0`); BuildDep no parchea nada; SLSsteam los añade a `AdditionalApps` | RESEARCH §14.2 |
| 2026-06-24 | Deck (v0.13.8, revertido en v0.13.9) | Excluir del package 0 el depot shader sin clave | `excluded 2 presence-only depot(s)` y Steam ejecuta igual el shader update (`Missing decryption key`): el pre-cache lo manda el appinfo, no el filtro de licencia | RESEARCH §13.8 |
| sin fecha | codespace | Ticket de ownership (eMsg 858) con SLSsteam | `failed to update ownership ticket (Access Denied)` en `content_log.txt`; no fatal, la instalación sigue | RESEARCH §3 (paso 1), §6 |
| pre-v0.16.15 | codespace, Balatro | Add Game con Steam abierto, sin reconcile | `has no changes, 0 target` → `0 mounted depots` → "Fully Installed, files missing"; tras reiniciar `config changed: added depots`. `DepotIdVec` (+0x48) como palanca: sin cambio, descartado | RESEARCH §18 |
| 2026-07-20 | codespace (v0.16.15) | Reconcile de licencias en vivo | `keys.txt changed — reloaded (Size=24), reconcile armed` → `APPENDED 5 id(s)` → `NotifyLicensesUpdated found (RVA 0x9f0f10)` → `Reconcile: broadcast LicensesUpdated_t` → `preallocated 6 files (272 MB)`, descarga de 2868390/1942282, `finished update, 2 mounted depots`, `App Running` 36 s después. Sin cuelgue | RESEARCH §18 |
| 2026-09-04 | codespace, cliente `1788400362` | Reconcile con caché fría (`experiment_cold_cache.sh`) | Caliente, fría y segundo disparo consecutivo con ids inexistentes: `APPENDED` y `broadcast` en el mismo segundo, UI normal, `AppIdVec size=196 alloc=202` idéntico. Disparo sintético, un solo build | RESEARCH §18 |
| 2026-10-05 | codespace SteamOS, Darkest Dungeon 262060 (poseído) | Qué concede cada cosa (runs Ref, A, B, C, E) | Sin stack: 4 depots. Stack sin añadir nada: igual. Un DLC a mano en `AdditionalApps` (B): se muestra poseído y **no baja ningún depot**, ni tras Verify. Juego añadido con LumaDeck (C): bajan los cuatro DLC de pago, disponibles en el juego. Base fuera de `AdditionalApps` y DLC dentro (E): los seis DLC poseídos, 8 depots, el juego arranca | RESEARCH §21 |
| 2026-10-05 13:35-13:38 | codespace SteamOS | Run C en detalle | `APPENDED 27 id(s) to PackageId=0`; Steam no replanifica con el juego abierto; al cerrarlo `config changed: added depots 580102,702542,735732,4964111`; `Downloading 169 chunks for depot 702542` con `SERVED local key`; `finished update, 8 mounted depots`. Claves servidas a depots poseídos (445702, 1117862) sin crash | RESEARCH §21.2 |
| 2026-10-06 | codespace SteamOS | `packageinfo.vdf` frente a la inyección en memoria | El fichero no cambia: es la fuente fiable de "poseído de verdad" | — |
| 2026-10-06 (×2) | codespace SteamOS | Licencia inyectada en caliente sobre un juego instalado | Añadirla no baja nada; quitarla no borra nada, ni en caliente ni al reiniciar | — |
| 2026-10-06 10:15 | codespace SteamOS | Reinicio sin licencia + Verify | 8 depots siguen montados: quitar la licencia no quita depots | — |
| 2026-10-06 18:47 | codespace SteamOS | Ciclo de DLC 26 ms después del reconcile, Steam de 2 min | Nada; Steam no procesó el aviso (sin `OnAppLicensesChanged`) | — |
| 2026-10-07 07:20 | codespace SteamOS | `SetDLCEnabled(true)` tras un uninstall sin reiniciar, con el vector append-only | Steam vuelve a bajar los DLC enteros: licencia rancia en el vector, manifests en depotcache, clave retirada | — |
| 2026-10-07 08:10 | codespace SteamOS (`4f8579c`) | Quitar una línea de `keys.txt` → `erase` del vector + reconcile | `- AppIdVec` ×12; un Verify ya no baja nada (`4 target`). "Your stuff" sigue mostrando el juego hasta reiniciar | — |
| 2026-10-07 09:59 | codespace SteamOS, Brotato | Aparecer y desaparecer en caliente | Al añadir aparece (SLSsteam emite `AppLicensesChanged_t` solo para apps nuevas); al quitar, `Config reloaded!` ×6 sin callback y el `LicensesUpdated_t` de lumalinux no suelta la entrada; al reiniciar desaparece | — |

#### Función 3 — DLC

Todas las pruebas del 2026-10-06/07 son en el codespace SteamOS con Darkest
Dungeon (262060, poseído por la cuenta de prueba; cuatro DLC de pago no
poseídos, 861 MB). "Ciclo" = `SetDLCEnabled(false)` seguido de `true` desde
el frontend.

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-13 | devcontainer, Brotato | DLC sin línea de clave en el lua | Invisible para Steam. Con clave y manifest añadidos y un reinicio, Steam lo bajó solo (55 MB, "Update Optional") | RESEARCH §19.3 |
| 2026-09-17 | codespace, Brotato 1942280 | Depot de DLC nuevo simulado (línea de 2868390 quitada, `hubcap_attempts.json` limpio) | Mitad LumaDeck bien: zip de Hubcap (32298 B), `reinstalled from zip: new depots [2868390], build 23429717`, clave de vuelta. La mitad de Steam no corrió porque el `.acf` seguía listando el depot | — (procedimiento de prueba borrado el 2026-10-09) |
| 10-06 09:47 | codespace | Add owned + Install con el juego sin instalar | `added depots` base y DLC, 8 depots; lumalinux sirvió solo las cuatro claves de DLC | — |
| 10-06 10:01 / 10:23 | codespace | Uninstall owned (versión vieja, que borraba los manifests de depotcache) y luego `SetDLCEnabled(false)` | Nada en el momento; al desmarcar, `removed depots` con `0 deleted files`: 1 GB huérfano. Sin el manifest Steam no sabe qué archivos eran del depot | — |
| 10-06 12:12 / 12:20 | codespace | Add owned con el juego instalado y Steam abierto; después reinicio | En caliente nada. Al reiniciar `config changed: added depots`, baja 861 MB sin reutilizar los huérfanos | — |
| 10-06 12:26 | codespace | `SetDLCEnabled(false)` con manifests en depotcache | `4242 deleted files`, 4 depots, 2 s, carpetas vacías | — |
| 10-06 12:31 / 12:48 | codespace | `true` con licencia rancia y marca puesta; ciclo con licencia en caliente | `added depots` al instante; en el segundo caso baja los 861 MB | — |
| 10-06 12:43 | codespace | `SetDLCEnabled(true)` sobre un DLC que no estaba desmarcado | No hace nada: solo planifica si algo cambia | — |
| 10-06 14:51 / 14:58 | codespace | Uninstall owned (primera versión): desmarcar y quitar la clave en el mismo segundo | `removed depots`, luego `Missing decryption key`, "Update required" reintentando cada 5 min, archivos intactos. Al devolver la clave, el reintento borró los 4242 archivos al instante. Origen de `retired_keys.txt` | — |
| 10-06 16:45 | codespace | Ciclo lanzado en el mismo segundo que el reconcile | Marca limpiada, nada añadido: el `true` llegó antes que la licencia | — |
| 10-06 16:53 | codespace | `DisabledDLC` heredado de un uninstall anterior | El plan de arranque lo respeta y no baja nada; por eso el add marca `SetDLCEnabled(true)` una vez | — |
| 10-06 17:03 / 17:19 | codespace | `true` con la licencia asentada (18 min); ciclo 1 s tras el reconcile | `added depots`, 861 MB, en ambos | — |
| 10-06 17:16 | codespace | Uninstall owned (versión final): desmarcar con las claves en `retired_keys.txt` | Steam pide la clave, lumalinux la sirve desde retiradas, borra los archivos, 4 depots | — |
| 10-06 19:07 | codespace | Add tras reiniciar Steam con un `reconcile.json` viejo | Esperó el tope de 20 s; `added depots` a los 20 s y baja | — |
| 10-06 19:25-19:37 | codespace | Uninstall con claves retiradas ×3; add instalado desde la principal, con el menú cerrado y desde la página del juego ×3 | Borra en 2-7 s, 3/3. `added depots` 1 s tras el reconcile, 3/3, con `RequestAppInfoUpdate` + `UpdatesJob: finished OK` en `appinfo_log.txt` ese segundo | — |
| 10-06 20:25 | codespace | Ciclo tras la señal de `appinfo_log.txt` | `added depots` 1 s después | — |
| 10-06 20:29 / 20:36 / 20:53 | codespace | Add 42 s tras arrancar Steam con el juego sin pintar; luego con la página abierta | Sin página: casilla cambiada y Steam sin plan (ni `user config changed`). Con la página del juego abierta, el mismo ciclo baja | — |
| 10-06 20:55-21:04 | codespace | Arranques con el juego en la pantalla de inicio | Steam carga sus stats a los 3-7 s: el juego está "despierto" | — |
| 10-06 (commits `9001216`…`beff30b`) | codespace | Forzar la descarga sin reiniciar con el ciclo `false→true` | 5/5 con el juego despierto; falló cuando el add caía en los primeros segundos tras arrancar Steam y una vez en que Steam ignoró el aviso (18:47). Se quitó: el DLC instala al siguiente arranque | — |
| 10-07 06:10 | codespace | Licencia inyectada y lanzar el juego | `App Running` sin plan; sin DLC | — |
| 10-07 06:39 / 06:42 | codespace | Add owned con el código final (licencia + `true` suelto); reinicio | En caliente, marca limpia y Steam sin tocar nada; al reiniciar `added depots` y 861 MB a los pocos segundos | — |
| 10-07 06:45 | codespace | Uninstall owned con el código final | `removed depots`, borrado con la clave retirada en 5 s, 4 depots | — |
| 10-07 07:36 | codespace | Add owned instalado + Verify desde la página | El Verify planifica con la licencia y baja los DLC | — |
| 10-07 07:47 | codespace | Arranque con la marca puesta, sin licencia y claves retiradas (uninstall "dormido") | `config changed: removed depots`, borra con la clave retirada: el respaldo del uninstall es el siguiente arranque | — |

#### Función 4 — Claves y manifests

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| sin fecha | codespace | Claves en `config.vdf` de depots no poseídos | Steam las borra al cerrar (las entradas `DecryptionKey` pasan de 3 a 1 en un reinicio). Por eso `keys.txt` es la fuente | RESEARCH §6 |
| 2026-10-06 10:11-14:51 | codespace SteamOS | Lo mismo, con Verify | Sin claves en `config.vdf`: `Missing decryption key`. A las 10:14 habían vuelto tras un reinicio; a las 14:51 no estaban ni en disco ni en memoria. LumaDeck ya no edita ese fichero al desinstalar | — |
| sin fecha (build `f92deb5e`) | codespace | Convar `@bClientTryRequestManifestWithoutCode` a 1 | El cliente pide sin código y el CDN lo exige igual ("No connection") | RESEARCH §6 |
| sin fecha | codespace | Variación del código por sesión | El mismo gid recibió `15549905601718457808` y otro día `3261884576850880630`: el código es por petición | RESEARCH §7 |
| 2026-07 | Deck | Cloudflare en `manifest.opensteamtool.com` | Con `User-Agent: OpenSteamTool/1.0` responde; con `curl` a pelo, "Just a moment…" también desde una IP residencial. Es el UA, no JA3 ni IP. Ese mes: wudrm 502, steam.run 404, opensteamtool vivo | RESEARCH §7 |
| sin fecha | codespace, Brotato y Balatro | Clave del depot shader ausente | Brotato (clave real para 1942280) instala; Balatro (`2379780;` sin clave) aborta con `Missing decryption key` y cancela toda la app | RESEARCH §6, §13.7 |
| ≤v0.8.0 | codespace | GMRC gateado solo por `HasManifestGid` | Los manifests del shader pasaban al original → "Access Denied" → "No connection" los primeros minutos; solo visible con un `.acf` en `StateFlags=1`; con `StateFlags=4` Steam salta el pre-cache | RESEARCH §11.5 |
| 2026-06 | PC (lumalinux v0.13.5) | Balatro con zip sin clave de shader | Primer intento `Missing decryption key, state 0x40a`; Steam lo reencoló a los 300 s y el reintento bajó 2379781 (`StateFlags=4`) | RESEARCH §13.7 |
| 2026-06-23 | Deck | ¿Los zips de Hubcap traen la clave del depot shader? | Brotato y Formula Legends sí (limpios); Balatro, CrossCode (368340) y Blasphemous (774361) no → `Missing decryption key`. Ninguno trae `<appid>_*.manifest` (también Silksong, Isaac). Con el juego ya instalado, Steam relanza el shader update cada ~5 min: no es transitorio | RESEARCH §13.8, §13.11 |
| 2026-06-23 | Deck (v0.13.7, revertido) | Clave cero de 32 B para depots sin clave | Arregla el bucle post-instalación pero en instalación fría da `Failed to decrypt cached manifest`, `Invalid content configuration` y `Update Paused` hasta reanudar a mano | RESEARCH §13.8 |
| 2026-06-23 | Deck | Knob de config para el shader por juego | `ShaderCacheManager.App.<appid>` solo admite `ShaderCacheSize`; el toggle de Steam escribe `DisableShaderCache 1` global | RESEARCH §13.8 |
| 2026-06-24 | Deck, v0.14.0 | ShaderDepot en los dos sentidos | `4/4 hooks active`; CrossCode y Blasphemous: `shader depot id … is keyless … returning 0`, cero líneas de shader en los nueve minutos siguientes. Brotato: el hook no dispara, `SERVED local key for depot 1942280`, `shadercache/1942280/: 6 updated`, `finished update` | RESEARCH §13.10 |
| sin fecha (pre-2026-09) | codespace | ¿Cuándo dispara GMRC con manifests pre-sembrados? | Brotato lo disparó solo para el depot shader; Tilt It! Golf, con todos los manifests en depotcache y sin pre-cache, nunca lo disparó e instaló entero | — |
| 2026-09-09 | ecosistema | Caída de los proveedores de códigos | opensteamtool, wudrm y steam.run muertos el mismo día; KeySteam, SteaMidra y LuaTools Windows rompieron con ellos | RESEARCH §19 |
| 2026-09-09 | devcontainer | Login anónimo con `node-steam-user` y `getManifestRequestCode` | Brotato y Lonely Mountains: `AccessDenied`; control con Dota: código y 200 del CDN. `GetCDNAuthToken` sí se emite para depots de pago no poseídos pero no sustituye al código | RESEARCH §19.2b |
| 2026-09-10/11 | devcontainer (`gmrc_mint.py`) | Qué códigos concede Valve anónimamente | Los redists 228980 (depots 228988-229005) y el depot Workshop; el shader de Brotato y RimWorld sí, el de Lethal Company, Silksong y Vampire Survivors no; depots de contenido nunca; un gid inexistente se concede | RESEARCH §19.2 |
| 2026-09-10/11 | devcontainer SteamOS | Qué rompió el 09-09 en lumalinux | El shader pre-cache: el hook devolvía basura de proveedores moribundos → tormenta de 401 en el CDN y toda la cola de descarga atascada. Con fallthrough limpio solo el popup "No internet connection", ~30 s de pausa e instala. Apagar GMRC apagaba el finder: instalaciones fantasma con 0 depots | RESEARCH §19.1 |
| 2026-09-11 | codespace SteamOS | ¿Lee Steam `config/depotcache/`? | No. Manifest solo ahí → pide código y falla; copiado a `depotcache/` → lo coge en el reintento de ~30 s | RESEARCH §19.3 |
| 2026-09 | codespace (`smc_fresh.py`) | `P-ToyStore/SteamManifestCache_Pro` | Valheim: manifest 15 min después de Valve; cabecera de 10 B + deflate; 22/22 chunks verificados en el CDN; sin claves. El 2026-09-14 tenía los 6 gids actuales que la cadena gratis de SteaMidra no tenía. El 2026-10-07 el repo ya no existe (404) | RESEARCH §19.4 |
| 2026-09-14 | red | Cadena "Free Providers" de SteaMidra | `fylsdy/ManifestHub` 404; los tres espejos llevan 0 de los 6 gids actuales | RESEARCH §19.4 |
| 2026-09-15/16 | codespace (`gmrc_probe.py`) | Los proveedores vuelven: dos pools | wudrm, steam.run y manifestdex devuelven el mismo código para el mismo manifest (docenas de coincidencias en 42 depots); 20770407 y wudrm en el mismo segundo difieren. Un código vive ≥ 58 min (comprobado contra el CDN cada 2 min de 07:40 a 08:38). El GET del CDN es anónimo: un código de 20770407 sirvió el manifest desde el codespace (200, 160159 B). 42/42 gids válidos por los cuatro; un gid de 2,5 años antes también | RESEARCH §20.1-20.4 |
| 2026-09-15 | codespace | Hechos de proveedor | 20770407: depot+gid, `Unauthorized` para lo que no puede servir, 10 req/10 s por IP (429 `error code: 1015`), `Service temporarily unavailable` bajo carga, 502 desde las 13:40 UTC el resto del día; volvió el 16. manifestdex: exige `User-Agent: ManifestDeX/1.0`, solo gid, responde un número para cualquier gid, inventado incluido | — (procedimiento de prueba borrado el 2026-10-09) |
| 2026-09-15 | codespace (`LUMA_GMRC_URL` a un servidor local) | Código falso inyectado sin comprobación | Steam cancela con `Unspecified Error`, deja la app en `Update Paused`, la saca del schedule y no reintenta (el fallo de wudrm del 09-10). Con `CdnAcceptsCode`: `CDN REJECTED code (HTTP 401)`, Steam dice `Access Denied`, "No internet connection", `Update delayed for 30 secs` y reintenta | RESEARCH §20.3 |
| 2026-09-15 | codespace | Coste con el primer proveedor caído | Cada depot paga tres intentos (~12 s) antes de pasar al siguiente. De ahí el salto de 60 s por proveedor caído | — (procedimiento de prueba borrado el 2026-10-09) |
| 2026-09-15 | codespace SteamOS, Balatro | Instalación nativa sin manifest local (T1) | `20770407 -> unavailable (HTTP 502)` ×3, `got code (via manifestdex)`, `CDN accepted (HTTP 206)`, `INJECTED`; `manifest request received 200`, 75 chunks, `finished update`; Steam escribió el manifest en depotcache | — (procedimiento de prueba borrado el 2026-10-09) |
| 2026-09-16 | codespace, Lethal Company 1966720 | Depot shader con clave (T2) | Dos manifests de shader pedidos, ambos `via 20770407`, aceptados e inyectados; `shadercache/1966720/` con `fozpipelinesv6`; sin popup. Un Verify no arranca el job de shaders; un install sí | — (procedimiento de prueba borrado el 2026-10-09) |
| 2026-10-07 10:54 | codespace (clave de usuario) | Manifest suelto de Hubcap (`/generate/manifest`) | 200, 271 bytes, idéntico byte a byte al de depotcache. Cuota `single` 1500/día | RESEARCH §19.4 |
| 2026-10-07 | codespace | `api.993499094.xyz/depotkeys.json` | 240103 claves, 18 MB, modificado el mismo día; 32/32 depots de las luas cacheadas con la clave correcta; no es day-one (STAR WARS: Galactic Racer, salido el 10-06, sin sus depots). Aparcado | RESEARCH §19.4 |
| 2026-10-07 19:13 | codespace SteamOS, sin clave de Hubcap | Descarga por `api.json` | `Morrenus status=401` → `Forced Ryu (Cookie) status=200`; añadir queda habilitado solo con Ryuu | log de LumaDeck |

#### Función 5 — Updates de juegos

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-06 | PC (headcrab + SLSsteam + lumalinux v0.13.5), build `7c4ac73e` | Auto-update al despinear (Balatro, Vampire Survivors) | Descarga delta visible. Vampire Survivors: `1794681 6531…→7054…` y `1794685 7577…→7852…` solas; `finished update, 2 mounted depots`. Los dos juegos tenían el `buildid` actual en el `.acf` y aun así actualizaron: Steam compara gids por depot, no el build | RESEARCH §14 |
| estático | SLSsteam `20260705132808` | Ancla del update-block de SLSsteam | `and dword [esi+0x8], 0xFFFFF8E5` (`81 66 08 e5 f8 ff ff`) en `0x1cd7b0`; el inmediato aparece seis veces más como desplazamiento; con el filtro `81 /4` sobrevive una | RESEARCH §16.2 |
| 2026-07-07 | codespace Arch, SLSsteam `20260705132808` | `sls_update_unblock` de extremo a extremo (Balatro) | `patched SLSsteam update-clear` en cada arranque. Pineado al zip viejo: `BuildDep: PATCH 3512… -> 3742…`, baja. Despineado: `Update Required … Update Started`, 2 chunks, `finished update`; `reuse 55000644, delta 18300195`. Con `LUMA_NO_SLS_UNBLOCK=1` se queda en "Update required" (issue #20). El parche se retiró en v0.16.18 cuando SLSsteam cambió el mecanismo | RESEARCH §16.4 |
| 2026-09-10/11 | devcontainer | Cuándo relee Steam `ManifestIds` | Al arrancar, al lanzar el juego y en cada reintento con update pendiente; no en "Play" ocioso; `steam -ifrunning` y `steam.pipe` no lo fuerzan. SLSsteam recarga el fichero al instante | RESEARCH §19.3 |
| 2026-09-12 18:40 → 09-13 08:47 | Deck (LumaDeck 0.8.0, lumalinux 0.20.1) | Primera update de producción por pin (Lonely Mountains: Snow Riders 2545360) | 18:40 pin `2545361 → 6942…`; 19:09 el pase ve el build 25172008, baja el manifest de P-ToyStore, siembra depotcache, `pin moved to build 25172008`; a la mañana `Update Required … Update Started`, 626 chunks, `finished update` en 17 s; `.acf` y `ManifestIds` al nuevo build. Sin código, sin Hubcap | RESEARCH §19.5 |
| 2026-09-13 | codespace, Balatro | Steam necesita el manifest instalado y el objetivo | Con el manifest viejo borrado: `BYldRequestDepotManifest 'Access Denied'`, atascado en "Update"; repuesto, el reintento de 30 s termina | RESEARCH §19.6 |
| 2026-09-14 | codespace SteamOS, Balatro | Heal del manifest del build instalado | Pin a A, `rm` del manifest de B (instalado): 61 s después el fichero está de vuelta (`restored installed-build … from the archive`). No ejercitado: el pase negándose a mover el pin | RESEARCH §19.6 |
| 2026-09-16 | codespace SteamOS (lumalinux 0.21.0, LumaDeck `be91d05`) | Release de pins con proveedores vivos (V1) | El pase deja en paz lo congelado por el usuario y por un fix de LuaTools; Auto-update on → `--unpin` inmediato, el depot sale de `ManifestIds`, el juego en "Play" | — (procedimiento de prueba borrado el 2026-10-09) |
| 2026-09-16 13:33 | codespace (`LUMA_GMRC_URL` a un agujero negro) | Caída de proveedores (V3) y recuperación (V4) | `gmrc.json` → `down`; siguiente pase: siete juegos congelados a su build instalado con `reason: providers`; Balatro instala desde el manifest archivado sin ningún código en el reintento de 30 s. Con el proveedor de vuelta: `provider probe ok`, siete liberados, `ManifestIds` solo con los depots del fix | — (procedimiento de prueba borrado el 2026-10-09) |
| 2026-09-21 | codespace, Balatro | Qué hace que Steam aplique un pin cambiado | Pin + reinicio: nada. Pin + Verify: nada. Pin + `StateFlags 6` + reinicio: SLSsteam sustituye el gid, GMRC da el código, Steam baja el build de diciembre de 2024. De ahí `mark_update_required` | LumaDeck `pins.py` (`mark_update_required`) |
| 2026-09-21 | codespace (SteamDB) | Qué sirve Cloudflare a un cliente plano | El feed RSS sí; cada página, 403 con cuerpo vacío. Una tarde de sondas a `/api/` acabó en 403 y luego retos en todas las páginas, que se levantaron tras ~30 min de silencio. Con login OpenID, el historial de depot lista todos los manifests | LumaDeck `game_versions.py`, `steamdb_reader.py` (docstrings) |
| sin fecha | CI (`tests/fixtures/steamdb/`) | Traductor build → gids | 9 de 10 builds capturados casan al segundo | LumaDeck `tests/test_versions.py` |
| 2026-10-05 | codespace SteamOS | Add de un juego poseído con `pin=True` | Ocurrió porque el codespace fresco no tenía `gmrc.json` (se escribe tras el primer lookup). Corregido en `pins.gmrc_state`. Si ese pin congela un juego poseído al salir un build sigue sin medirse | RESEARCH §21.4 |

#### Función 6 — Fixes y DRM

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| sin fecha | PC (zips abiertos) | Dos fixes reales | Call of Duty 4 (7940): un solo `iw3sp.exe` sin SteamStub. Baldur's Gate 3 (1086940): `steam_api64.dll` + `OnlineFix.ini` (`RealAppId`, `FakeAppId=480`, DLC unlock) | LumaDeck FIXES_MAP.md |
| sin fecha | GitHub | EOS proxy `yesyes0649/eos-proxy` v1.0.0 | Solo existe el build de 64 bits; el asset de 32 es un enlace muerto. `release.yml` comprueba el SHA-256 | LumaDeck FIXES_MAP.md |
| sin fecha | lumalinux (`experiment_ownership_ticket.py`) | Hook nativo del ticket de ownership en vez del plugin Lua | Portable, pero un inline hook se copiaría sin relocar al trampolín de otra copia del plugin y caería; 40 líneas de Lua frente a ~200 de C++. Aparcado | LumaDeck FIXES_MAP.md |
| 2026-08-30 | Discord (Ace, segunda mano) | Cobertura del plugin de tickets empalmados | 19 de 20 juegos; Insurgency Sandstorm la excepción | LumaDeck FIXES_MAP.md |
| 2026-09-28 | codespace, Joe Danger 229890 | Tickets empalmados con control | Plugin activo: arranca; desactivado: error 65432. Nada cambia en el directorio del juego | LumaDeck FIXES_MAP.md |
| 2026-09-29 | codespace | Carrera entre `Plugins: yes` y el fichero del plugin | Un evento de fichero procesado antes de la recarga de config ve `Plugins: no` y no hace nada. De ahí la espera de 1,5 s | LumaDeck FIXES_MAP.md |

#### Función 7 — Logros, stats y tiempo de juego

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-07 | Deck | ¿Por qué un juego añadido no entra en el borrow de SLSsteam? | Traza `appid=1454400 sub=1 added=1`: con la inyección de package-0, `isSubscribed` es true para los añadidos | RESEARCH §17 |
| v0.16.4 → v0.16.7 | Deck | Traza del guard combinado | `appid=1454400 sub=1 added=1 -> skip=0` (el borrow corre, logros saltan en el juego); `22380 sub=1 added=0 -> skip=1`; `2371090 sub=1 added=0 -> skip=1` | RESEARCH §17.5 |
| 2026-07-20 → 07-28 | Deck, codespace | SLSsteam `59f8259` cambia la firma de `sendAndRecvGetUserStats` (`j`→`4EMsg`) | El parche dejó de casar y quedó mudo seis días y dos releases: logros apagados y, con ellos, el reconcile sin `CUser*`. Desde entonces los símbolos se resuelven por prefijo | RESEARCH §17.2 |
| 2026-10-07 22:52-23:06 | Deck + PC (Windows) | Sincronización de logros con CloudRedirect 2.6.6 | Deck: `stats_sync_enabled: True`; al desbloquear, `ExportNativeStats app=2379780: wrote 79 bytes (1 ach blocks)`, `Cloud blob refreshed: 7 app(s)`; al relanzar, `GetUserStats … handled locally` y el logro visible. PC: no aparecía hasta reiniciar Steam; tras reiniciar y abrir Balatro, está. Dos logs: `cr_debug.log` (solo `DoInit`) y `cloud_redirect.log` | cloudredirect.md |

#### Función 8 — Cloud saves

Ningún save sincronizado medido por nosotros; solo logros (F7). La
convivencia con CloudRedirect y el orden del `LD_PRELOAD` están en
`cloudredirect.md`.

#### Función 9 — Añadir y quitar un juego

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-06 | PC | Instalaciones multi-depot en Linux | Vampire Survivors monta dos depots de contenido (1794681 Windows y 1794685 Linux), los dos pineados y los dos auto-actualizados | RESEARCH §14.3 |
| sin fecha | Deck (Proton) | Depots montados en Balatro | Solo 2379781 (64 MB); 2379782 (81 MB) no se descarga | RESEARCH §10 |
| 2026-09-10/11 | devcontainer | Uninstall desde Steam | Borra los manifests del juego en `depotcache/` y el `.acf`, reescribe `config.vdf`, no toca `keys.txt`, el lua ni SLSsteam; reinstalar en la misma sesión falla hasta reponer el manifest | RESEARCH §19.3 |
| 2026-09-16 | codespace, Into the Breach 590380 | Add nativo con proveedores vivos (V2) | `steamidra_lite` sin `--pin`, `ManifestIds` sin cambios, `CDN accepted` para el contenido y el shader, instala | — (procedimiento de prueba borrado el 2026-10-09) |
| sin fecha | devcontainer SteamOS, Brotato y Vampire Survivors | El stub `.acf` en la raíz con el juego instalado en otra biblioteca (D4) | Tras reiniciar, "NOT INSTALLED"; pulsar Install vuelve a bajar el juego entero a la raíz (273 MB duplicados). Borrando solo el stub y reiniciando, "INSTALLED". El stub se reconoce por `StateFlags 1`, sin `InstalledDepots`, `SizeOnDisk 0` | — (informe de la #41, borrado el 2026-10-09) |
| sin fecha | devcontainer SteamOS, A Short Hike y Undertale | ¿Hace falta el stub? | Sin stub: botón Install, sobrevive un reinicio sin instalar, un solo `.acf`, instalado tras reiniciar. Con stub: dos manifests y "NOT INSTALLED". Un build, un entorno | — (informe de la #41, borrado el 2026-10-09) |
| sin fecha (×2) | devcontainer SteamOS | ¿Steam reescribe un `.acf` borrado? | Borrado con Steam abierto y reiniciado: no lo regenera | — (informe de la #41, borrado el 2026-10-09) |
| sin fecha | devcontainer SteamOS | Registrar una segunda biblioteca a mano | Cuatro intentos. `content_log.txt`: `Loaded Steam library folders configuration: …/steamapps/libraryfolders.vdf` (Steam carga `steamapps/`, no `config/`); `pgrep -x steam` no lo ve en Game Mode | — (informe de la #41, borrado el 2026-10-09) |
| sin fecha | test determinista (dos bibliotecas) | El patch de error del `.acf` y `hasGameFiles` con el juego en la biblioteca secundaria (D1, D2) | El patch creaba un stub en la raíz y dejaba intacto el `.acf` real (`UpdateResult=8`); la tarjeta salía en gris. Ambos corregidos leyendo todas las bibliotecas | — (informe de la #41, borrado el 2026-10-09) |
| sin fecha | Deck (9 `.acf`) + Windows (4) | Campos presentes en `.acf` reales (D5) | `StateFlags` y `ScheduledAutoUpdate` en 13/13; `UpdateResult` y `BytesToDownload` en 10/13; `FullValidateAfterNextUpdate` en 1/13. `ScheduledAutoUpdate` distinto de 0 en Proton 11.0 y en Halo (`StateFlags=6`). Por eso el patch no debe exigir campos que no existen | — (informe de la #41, borrado el 2026-10-09) |
| sin fecha | devcontainer SteamOS, Jump King | Add sin stub con el código enviado | `no .acf yet — Steam writes it on Install`; tras instalar, `StateFlags=4` y tamaño real | — (informe de la #41, borrado el 2026-10-09) |
| sin fecha | Discord (segunda mano), app 2111550 | `UpdateResult=8` | "content still encrypted" al lanzar; arreglado editando `StateFlags 36→4` y `UpdateResult 8→0`. Pista fuerte de fallo de descifrado, no prueba | — (informe de la #41, borrado el 2026-10-09) |
| sin fecha | Deck | `appworkshop_*.acf` | No existe ninguno en la Deck del autor; la medida de SteaMidra sobre `NeedsDownload` no aplica | — (informe de la #41, borrado el 2026-10-09) |
| 2026-10-06 | codespace SteamOS | Archivos fuera de todo manifest en una reinstalación | Steam volvió a bajar 861 MB con los archivos ya en disco: no reutiliza huérfanos | — |
| 2026-10-07 09:59 | codespace SteamOS, Brotato | Uninstall de un juego añadido sin reiniciar | Fuera: carpeta, `.acf`, lua, claves, `AdditionalApps`. Quedan: compatdata (si no se marca), shadercache (hoy se borra), el archivo propio de zips y manifests, librarycache. El juego sigue en la biblioteca hasta reiniciar | — |
| 2026-10-07 19:38 | codespace SteamOS | Búsqueda por nombre con la tienda, sin credencial | `storesearch?term=brotato` → 4 items (dos DLC y la banda sonora incluidos); la UI muestra 3 | LumaDeck `5bb8fdb` |
| 2026-10-07 19:47-19:56 | codespace SteamOS, Balatro (zip de Ryuu) | Orden del lua de Ryuu | `addappid(2379780)` al final: `steamidra_lite` tomó 2379781 como juego (`AdditionalApps`, claves y `stplug-in` con el depot como padre) y el post-check abortó. Con la línea del juego puesta primero: `appid principal 2379780`, 3 claves, post-check OK | LumaDeck `fcef4f7` |

#### Función 10 — Credenciales y proveedores

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| hasta v0.7.4 (issue #42) | campo | Renovación del token de LuaTools | Rota desde el día que salió: llamaba a un endpoint inexistente, tragaba el error y enviaba el token muerto; una hora de LuaTools y luego `session_expired` permanente | LumaDeck docs/credentials.md |
| v0.7.5 | campo | Logout de LuaTools | La siguiente carga del plugin restauraba desde el espejo la sesión recién descartada. `_forget_cred` limpia el espejo | LumaDeck docs/credentials.md |
| 2026-10-07 | LumaDeck | Caducidad real de la sesión de Ryuu | Ryuu puede cortar una sesión antes de su fecha; la comprobación horaria y el 401/403 en descarga la marcan al momento | LumaDeck docs/credentials.md |
| 2026-10-07 18:17 | codespace SteamOS | Verificar el login de Ryuu desde el Python de Decky | `CERTIFICATE_VERIFY_FAILED` sin contexto SSL; Decky encuentra los CA por `certifi`. Arreglado pasando el contexto de `http_client` | LumaDeck `8200d4e` |
| 2026-10-07 18:25 | codespace SteamOS | Login de Ryuu rechazando la cookie anónima | "anonymous session; waiting" ×2 y luego "logged-in session cookie captured"; la home logueada lleva `data-user-id`; `GET /download?appid=1942280` → zip de 42082 B con lua y manifests | LumaDeck `0187590` |
| 2026-10-07 | codespace SteamOS | Borrar la clave de Hubcap desde Settings | `credentials.json` sin `hubcap_key`; la entrada Morrenus deshabilitada y sin `api_key=`; Settings "No Hubcap key saved" (las cadenas `credHubcap*` llevaban borradas desde `cf0dedb` y salían en crudo) | LumaDeck `1b5e678` |

#### Función 11 — Mantenimiento propio

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| sin fecha | build local (yaml-cpp) | Cuerpos HTTP 200 inesperados contra `YAML::Load` en SafeMode | Vacío o JSON de error: parsea con 0 entradas, envenena la caché y toastea; HTML, `Guru Meditation` o `Not Found`: lanza y no envenena; feed bueno: 1 entrada. De ahí la validación del cuerpo (`49e4279`, `51297c4`) | — |
| 0.13.6 → 0.15.0 | Deck | SafeMode enlazado con libcurl y OpenSSL | El Steam Runtime quitó `CURL_OPENSSL_4` y el `reaper` de 32 bits no cargaba el `.so`: los juegos rebotaban. Hoy libcurl va por `dlopen` y no hay `NEEDED` de curl ni openssl | `CMakeLists.txt` |
| 2026-08-19/20 | GitHub | Cambio de nombre del asset de SLSsteam (`-Any.7z` → `-release.7z`) | La instalación quedó en 404 ~44 h. CloudRedirect: cinco releases solo traen `.exe`; `cloud_redirect_cli` solo en el flatpak | §3.1 |
| 2026-09-08 | CI | `verify-fix.yml` (smoke test en runtime) | Nunca ha dado verde: sus primeras ejecuciones reales fallaron en el arnés antes de cualquier aserción. El "verde" que se citaba era de otro workflow | maintenance §C |
| sin fecha | código | Regresión de sesión para CachyOS | Una revisión anterior metía CachyOS en la familia ChimeraOS y le enviaba `desktop`, que CachyOS rechaza; corregido (verificado en `CachyOS/gamescope-session`) | LumaDeck docs/porting-cachyos.md |
| 2026-10-07 | Deck | Versiones de componentes que mostraba Settings | CloudRedirect: `.so` en `2.6.5+870afdb-dirty`, caché de releases en `v2.6.6`, pero Settings leía 2.6.3 porque tomaba la primera línea de un `cr_debug.log` acumulativo desde agosto. SLSsteam `20261001163836`, lumalinux v0.22.1. Corregido leyendo la versión del `.so` en disco | LumaDeck `c4776ce`, `be36d92` |
| 2026-10-08 06:51 | CI | Releases lumalinux v0.22.2 y LumaDeck v0.11.0 | `build.yml` run 534 con `liblumalinux.so` (9,5 MB) y `version.txt`; `release.yml` run 143 estampa `0.11.0` del tag | — |
| 2026-07-22 → 09-02 | CI | Latencia del whitelist automático | PR #22 creado 06:03:32 y mergeado 06:03:35; #27 12:08:50 → 12:08:53. SLSsteam en los mismos builds: 07-22 el mismo día, 07-25 tres días después, 07-28, 08-04, 09-02 el mismo día | `slssteam.md` §5.2 F11 |

#### Función 12 — Proyecto

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-06-24 / 07-24 | ecosistema | Valve reordena los virtuals de `IClient*::RunIPCFrame` | SLSsteam tuvo que bumpear y un mes después borró esos hooks (`8de3384`). Límite de la resolución por RTTI: no es inmunidad | RESEARCH §15.4 |
| 2026-09-13 | PC (`strings`, `pefile`) | `ManifestDeXCore.dll` | PDB en `C:\Users\berke\source\repos\OpenSteamTool`, mismos proxies `dwmapi.dll`/`xinput1_4.dll`, mismo hook (eMsg 151/147): OpenSteamTool rebautizado; no reporta códigos ni identidad | RESEARCH §20.2 |
| 2026-08 | Codespace y devcontainer | KVM | `/dev/kvm` no existe en ninguno de los dos, sin `vmx`/`svm`: cualquier VM es TCG; por eso no se puede reproducir Game Mode de CachyOS Handheld en una VM | — (el informe de viabilidad se borró el 2026-10-09) |
