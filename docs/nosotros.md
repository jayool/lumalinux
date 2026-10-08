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
| `res/rvas/<sha256>.yaml` | 16-20 | feed de RVAs por build: `hooks` (DepotKey, GMRC, BuildDep, ShaderDepot, Reconcile) y `finder` (`cache_global_disp`, `cache_root_off`, `cache_nodes_off`, `got_rva`) | 1 |
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
lo restaura vanilla si headcrab lo había parcheado, `setup.sh:264-333`).
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
  ha ejercitado en vivo** (§5 F1, `decouple-headcrab-plan` WS5).
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
blob de CloudRedirect en Drive/OneDrive/Dropbox, descargado una vez por
arranque. Convivencia: SLSsteam contesta para añadidos gracias al guard;
CloudRedirect contesta desde su almacén o deja pasar. Esquemas: generador de
LumaDeck apagado; SLSsteam los consigue por reseñadores de la tienda
(`MaxSchemaTries: 10`, por defecto).

### 2.8 Cloud saves

Delegado por completo en **CloudRedirect**: `cloud_redirect.so` por `LD_PRELOAD`
intercepta los RPC de Steam Cloud y los sirve desde Google Drive, OneDrive o
Dropbox. `setup.sh` lo trata como componente core: sin él aborta ("cloud
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

**Lista de control.** Redirección: CloudRedirect, tres proveedores.
Convivencia con Steam Cloud: `DisableCloud: no`; CloudRedirect inyecta cuota y
changelists por app (su doc).

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
   (detectado por `--help`), `--name` (ignorado por el script moderno,
   `steamidra_lite.py:1180-1185`); post-check: ≥1 clave extendida con
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
`config/libraryfolders.vdf`; `--pin-installed` solo la raíz. Análisis de los
cinco defectos originales en `LumaDeck/docs/dev-multi-library.md` (issue #41).

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

Sacadas con grep del código entero, no de memoria. Cada una con su función.

### 3.1 Hosts y URLs

| URL | Quién | Para qué | F |
|---|---|---|---|
| `raw.githubusercontent.com/jayool/lumalinux/main/res/updates.yaml` | `update.cpp:72`; `headcrab_compat.py:46` | feed SafeMode; builds soportados por lumalinux | 1, 11 |
| `raw.githubusercontent.com/jayool/lumalinux/main/res/rvas/<sha256>.yaml` | `rva_feed.cpp:63-64` | feed de RVAs del build cargado (sin override) | 1 |
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
18. pt-BR sin las claves de ayuda ni `showMoreResults`; cadenas en inglés
    fuera del diccionario; la pestaña Dev mezcla castellano e inglés.
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

**Resuelto al leer** (no es hallazgo): el orden `liblumalinux.so:cloud_redirect.so`
del `LD_PRELOAD` es intencional (`cr_stats_fix` interpone un símbolo de
CloudRedirect); `target_library_path` se acepta y se ignora porque Steam elige
la biblioteca al instalar, no es un bug sino una función que no existe.

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

### 4.4 Decisiones ya tomadas (2026-10-05 → 10-08)

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

---
## §5 Historial

### 5.1 Contraste con los docs absorbidos

`method.md` y `owned-games-guide.md` hacían cien afirmaciones sobre nuestro
stack sin medición con fecha (la extracción las numeró A1-A100). Cada una,
contrastada con el código leído: **confirmada** (el código lo hace, con
evidencia), **caducada** (ya no es así), **sin evidencia** (ni código ni
medición la sostienen; se mantiene como supuesto o se tira). Las caducadas y
las sin evidencia son las que no deben volver a escribirse como hechos.

| # | Afirmación (resumida) | Veredicto | Evidencia |
|---|---|---|---|
| A1 | GMRC sigue haciendo falta porque Steam re-pide códigos de manifests que revalida | sin evidencia para manifests de contenido; **confirmada** para el pre-cache de shaders (medido, §5.2 F4) | `gmrc_hook.cpp:51-54` |
| A2 | `AppTokens` no es portante; el flujo no escribe tokens | confirmada: solo con `--token` o `addtoken()` en el lua | `steamidra_lite.py:414-415`; `slssteam_ops.py:407-472` |
| A3 | la clave de depot es estable entre versiones | sin evidencia nueva; la misma clave descifró deltas en 2026-06 | §5.2 F5 |
| A4 | Steam hace staging y solo commitea en éxito | sin evidencia propia | — |
| A5 | pase de Valve cada 30 min; heal cada 60 s | confirmada | `pins.py:101-102` |
| A6 | `--set-pin` rechaza depots sin clave | confirmada | `steamidra_lite.py:1105-1149` |
| A7 | GMRC es opt-in (`LUMA_GMRC=1`) | **caducada**: activo por defecto desde v0.21.0 | `main.cpp:170-174` |
| A8 | ShaderDepot devuelve 0 para todo depot nuestro | **caducada**: condicional (clave + GMRC + proveedor vivo) | `shader_depot_hook.cpp:57-96` |
| A9 | el finder rehúsa lo ambiguo y no reintenta | confirmada en código; no ejercitado en vivo | `package_zero_finder.cpp:410-415,745-754` |
| A10 | `DisableUpdates: no` en instalación y en cada arranque del plugin | confirmada | `installer.py:216-259`; `main.py:151-183` |
| A11 | callback `0x7d`, `CUser*` del guard, único o no-op | confirmada | `reconcile_anchor_core.hpp:101-138`; `sls_achievement_unblock.cpp:64` |
| A12 | si el patrón de Reconcile falla, Add Game vuelve a necesitar reinicio | confirmada en código; nunca observado | `license_reconcile.cpp:132-135` |
| A13 | Steam nunca limpia manifests viejos | sin evidencia; `steamidra_lite` sí los borra en no-pin | `steamidra_lite.py:352-371` |
| A14 | `retired_keys.txt` solo lo lee `Lookup`, se recarga con cualquiera de los dos | confirmada | `key_store.cpp:143-171,254,271-289` |
| A15 | `is_owned` vivo (registro o licencia real) | confirmada; sin compra real medida | `pins.py:276-293` |
| A16 | `ensure_pinned` salta licenciados; depot desconocido cuenta como base | confirmada | `pins.py:343-361,675-690` |
| A17 | flujos G y H coherentes | sin medición; el código los cubre | `pins.py:343-361`; `slssteam_ops.py:1035-1038` |
| A18 | update de un juego owned filtra por `dlcappid` | confirmada; no medida | `pins.py:871-878,909-911` |
| A19 | tabla de qué toca cada operación | descripción; sustituida por §2.9 | — |
| A20 | `resolve_owned`: added > pins > packageinfo | confirmada | `downloads.py:926-945` |
| A21 | lumalinux "hoy no" quita la licencia del vector en caliente | **caducada**: retira desde `4f8579c` | `load_package_hook.cpp:199-251` |
| A22 | `DlcData` solo >64; DLC flag lo lleva SLSsteam solo | confirmada | `slssteam_ops.py:565-566`; `steamidra_lite.py:1323` |
| A23 | race finder ↔ Steam teórico, sin mitigar | parcial: cruce de `PackageId` desde 2026-09-08 | `package_zero_finder.cpp:558-564` |
| A24 | sonda 3/5 s, caché 15 s | confirmada | `gmrc_store.hpp:433-472` |
| A25 | cascada v0.21.0 (1 req/s, 3 intentos, 60 s skip, 120 s caché) | confirmada | `gmrc_store.hpp:73-97,334-417` |
| A26 | chunks retenidos ≥14 meses | sin evidencia propia | — |
| A27 | `gmrc.json` tras cada lookup; down solo si nadie contesta | confirmada | `status.cpp:87-96`; `gmrc_store.hpp:384,411` |
| A28 | el scan corre una vez; con feed no corre | confirmada; `method=rva` del finder no ejercitado | `rva_feed.cpp:229-250` |
| A29 | hash advisory; `updates.yaml` y `res/rvas/` de `main` cada arranque | confirmada | `main.cpp:107-126`; `update.cpp:72`; `rva_feed.cpp:63` |
| A30 | cero `NEEDED` de curl/openssl | confirmada (dlopen) | `curl.cpp:73` |
| A31 | ningún rescate ha disparado en dispositivo | sigue sin medir | — |
| A32 | fail-safe: 3 dumps o 1 si cambió el cliente | confirmada | `setup.sh:826-948` |
| A33 | `check_patterns` bloquea con cualquier cosa que no sea UNIQUE en las anclas | confirmada; nunca ha bloqueado un build real | `check_patterns.py:1103-1123` |
| A34 | el feed lleva `cache_root_off`/`cache_nodes_off` y el `.so` los usa | confirmada la lectura; el walk sin re-scan no ejercitado | `rva_feed.cpp:102-113` |
| A35 | `sls_achievement_unblock` fail-closed | confirmada | `sls_achievement_unblock.cpp:38-68` (ResolveSymbols) |
| A36 | la Parte 1 de update-testing exige `LUMA_FORCE_BUILDDEP=1` | confirmada; nunca ejecutada así | `main.cpp:183-186` |
| A37 | el skip de 60 s elimina el coste de 12 s | implementado; coste no medido después | `gmrc_store.hpp:85` |
| A38 | no ejercitada una update real de Valve en `up` ni en `down` | sigue | — |
| A39 | Steam lee `config/depotcache/` | **caducada** (medido 2026-09-11) | `steamidra_lite.py:294-297` |
| A40 | `config.vdf`: no-op sin bloque `depots`, AppID filtrado, `--no-vdf` | confirmada | `steamidra_lite.py:538-597,1409-1428` |
| A41 | la mayoría de juegos no necesita token | opinión del código; sin medición | — |
| A42 | reset del `.acf` deja `ScheduledAutoUpdate` y `FullValidateAfterNextUpdate`; `.bak` solo si cambia | confirmada | `steamidra_lite.py:620-641,780-794` |
| A43 | `--name` aceptado e ignorado; soporte cacheado por sesión | confirmada | `steamidra_lite.py:1180-1185`; `downloads.py:141-155` |
| A44 | `--pin` ya no congela por sí solo | confirmada: el pin es `ManifestIds` | `steamidra_lite.py:1364-1366`; `main.cpp:175-186` |
| A45 | `~/.steam/steam` es symlink en CachyOS | sin verificar | — |
| A46 | mismo `steamclient.so` desktop y Deck | **caducada** (2026-09-22, 10-01) | §5.2 F1 |
| A47 | Bazzite: mismo binario, mismo veredicto | **caducada** por A46; sin verificar | — |
| A48 | la caché de RVAs se relee sin revalidar | confirmada | `rva_feed.cpp:71` |
| A49 | el feed es más rápido que el scan | sin medición | — |
| A50 | un RVA falso no es RCE: debe caer en r-x de steamclient | confirmada | `rva_feed.cpp:242` |
| A51 | `downgrade.sh` nunca ejercitado en vivo | sigue | — |
| A52 | `steam_freeze` levanta cualquier freeze sin firma | confirmada | `steam_freeze.py:148-189` |
| A53 | Steam re-extrae `steam.sh` si el tamaño difiere | de moon; sin medir | — |
| A54 | el juego aparece sin reiniciar; algunos builds no | mecanismo confirmado (reconcile); builds no medidos | §2.2 |
| A55 | la búsqueda filtra bandas sonoras, demos y tools | confirmada | `api_manifest.py:401-425` |
| A56 | en un minuto fija todos los juegos sin proveedor | confirmada (pase local 60 s) | `pins.py:101,813-841` |
| A57 | cadena luastools → suelto → zip, un intento/día; sonda cada 30 min | confirmada | `manifests.py:501-567`; `pins.py:971-983` |
| A58 | un DLC comprado se salta en el uninstall owned | confirmada | `slssteam_ops.py:1035-1038` |
| A59 | Fix Update re-descarga el zip y re-pinea | confirmada | `GameDetail.tsx:1262-1274`; `downloads.py` |
| A60 | Ryuu: 30 días, 180 s, v10 sí / v11 no | parcial: **v11 también** se descifra con `secret-tool`/`kwallet-query` | `ryuu_cookie.py:163-306` |
| A61 | LuaTools: cookie en memoria 15 s, token 1 h, refresh single-use | parcial: el refresh existe; los tiempos no se miden aquí | `luatools_auth.py:304-356` |
| A62 | Hubcap por `/user/stats`; Ryuu vivo cada hora | confirmada | `api_manifest.py:457-591` |
| A63 | credenciales espejadas en el settings dir | confirmada | `api_manifest.py:41-96` |
| A64 | los logros se desbloquean y persisten como en un juego comprado | medido el guard y "popped in-game"; sincronización medida 2026-10-07 por CloudRedirect | §5.2 F7 |
| A65 | los esquemas generados aparecen tras reiniciar | sin medir; UI apagada | — |
| A66 | CloudRedirect lee `tokens_<provider>.json` en claro | del código de CloudRedirect; LumaDeck lo lee igual | `paths.py:934-955` |
| A67 | `setup.sh` pone `DisableCloud: no` e instala el flatpak | confirmada | `setup.sh:227,549-567` |
| A68 | el drop-in de Game Mode se rehace en cada carga | confirmada | `main.py:93-106`; `paths.py:631-703` |
| A69 | `not_supported` causa `version` = hash no reconocido → downgrade | parcial: lumalinux nunca escribe `blocked`; solo el log de SLSsteam dispara esa causa; el downgrade nunca ejercitado | `main.cpp:270-276`; `paths.py:1205-1226` |
| A70 | de headcrab solo se lee el pin de compat | confirmada | `headcrab_compat.py:36` |
| A71 | la cadena incluye P-ToyStore | **caducada** (404) | `manifests.py:501-567` |
| A72 | `gmrc.json` ausente → modelo 0.8 | **caducada**: ausente + hook GMRC instalado → `up` | `pins.py:404-419` |
| A73 | el zip siempre se instala con `--pin` | **caducada**: `pin_new_installs()` | `downloads.py:1539-1541` |
| A74 | TTLs de SteamDB 1 h / 6 h / 24 h; backoff 1 h | confirmada | `steamdb_reader.py` |
| A75 | versiones desde el `.so`; SLSsteam por tag o floor; caché 6 h | confirmada | `components.py`; `slssteam_version.py` |
| A76 | `HiddenView` es la única ruta que pasa Cloudflare | confirmada en código | `cef_cdp.py:290-439` |
| A77 | 10 builds, fija los depots que cambian, marca el `.acf` | confirmada | `game_versions.py:100-190` |
| A78 | no correr dos juegos con FakeAppId a la vez; el token arregla "invalid configuration" | la primera es de la plantilla de SLSsteam; la segunda sin medición | `config_default.hpp` |
| A79 | un Denuvo no poseído baja pero no arranca; el ticket en `~/.config/SLSsteam/cache` | sin medición propia | — |
| A80 | Steamless informa si no hay DRM | confirmada (rc 1 clasificado por texto) | `steamless.py` |
| A81 | sweep de `.acf` huérfanos en cada carga | confirmada; ventana de arranque no probada | `downloads.py:1824-1930` |
| A82 | uninstall rechazado con el juego en marcha; repair borra el `.acf` en todas las bibliotecas | confirmada | `slssteam_ops.py:1048-1086`; `downloads.py:1343-1356` |
| A83 | el reintento de 30 s cierra el bucle | el reintento sí (medido); el cierre e2e no | §5.2 F4 |
| A84 | el self-update sobrescribe el plugin y reinicia Steam | **caducada** en la práctica: la UI solo baja el zip | `Settings.tsx:679-695`; `self_update.py:223-257` |
| A85 | netsock falla con "pattern not found" en juegos sin SNS | del README de netsock | — |
| A86 | Steamless 10 min por exe | confirmada | `steamless.py` |
| A87 | dos copias del plugin no doble-hookean | lectura del lua | `lumadeck-spliced-tickets.lua` |
| A88 | si el ini automatiza netsock: pendiente (Valheim) | sigue | — |
| A89 | toda escritura en el directorio congela; la reversión por update no observada | confirmada la congelación | `pins.py:255-273` |
| A90 | EOS rechazado con Denuvo | confirmada | `fixes.py:837-839` |
| A91 | `_merge_launch_options` conserva wrappers del usuario | confirmada | `fixes.py:710-771` |
| A92 | #31 sin causa raíz | sigue | — |
| A93 | `inhibit-short-session-tracker` no se escribe | confirmada (no aparece) | — |
| A94 | LoadPackage ~2 candidatos, índice 0 | confirmada | `patterns.cpp:231-276` |
| A95 | finder: 2 s hasta el primer HIT, luego 15 s | confirmada | `package_zero_finder.cpp:139-140,827` |
| A96 | sanity del `AppIdVec` (0, >50M, `m_Size>4096`) | confirmada | `load_package_hook.cpp:84-109` |
| A97 | `manifest_size` de `keys.txt` dormido | confirmada | `depot_dependency_hook.cpp:61-82` |
| A98 | sin badge de update propio; marcador ACCELA ya no se escribe | confirmada | `steamidra_lite.py:819-823` |
| A99 | cuotas de Hubcap (25 zips, 1500 sueltos) | citadas por Hubcap; límites no medidos | `manifests.py` |
| A100 | hilo donante de moon; nosotros solo leemos | lectura upstream | — |

Inconsistencias que arrastraban los docs anteriores, ya resueltas en este:
`method.md` §8 atribuía 2379780 a Mina the Hollower (es Balatro); `method.md`
daba GMRC como opt-in; `manual-install.md` y RESEARCH §9 decían que Steam lee
`config/depotcache/`; `cachyos-port.md` daba por común el binario desktop/Deck;
`dev-backend-reference.md` de LumaDeck describía la cadena con P-ToyStore y
`--pin` siempre; `owned-games-guide.md` §3.D se contradecía con su §1.2 sobre
el retiro en caliente.

### 5.2 Mediciones y pruebas con fecha

Todas las que había en `method.md`, `owned-games-guide.md`, `RESEARCH.md`,
`maintenance.md`, `update-testing.md`, `manual-install.md`, `cachyos-port.md`,
`rva-feed-design.md`, `decouple-headcrab-plan.md`, los docs de LumaDeck y
`FIXES_MAP.md`, tal como estaban escritas (fecha, dónde, resultado literal),
ordenadas por función. Las de hoy (2026-10-07/08) que aún no estaban en ningún
doc van al final de cada función. La fuente original sigue siendo el sitio
donde está el detalle largo; `method.md` y `owned-games-guide.md` ya no
existen, y sus mediciones están aquí.

#### Función 1

| Fecha | Dónde | F | Qué se midió | Resultado | Fuente original |
|---|---|---|---|---|---|
| 2026-06 | PC Arch (mismo run) | 1 | Inyección por env var vs parche `steam.sh` | `LD_PRELOAD` como env var "does **not** survive Steam's client re-exec… the install silently produces a 0-byte 'phantom' `.acf`"; el parche de `steam.sh` sí funciona | RESEARCH §14.3 "The injection vehicle matters" |
| 2026-10-06 16:52 | codespace SteamOS | 1/11 | `liblumalinux.so` copiado encima del que Steam tenía cargado | "Steam vuelca (`assert_…dmp`) al pasar por el hook; la medida de ese minuto no vale. El `.so` solo se reemplaza con Steam parado" | owned-games-guide.md §4 |
| ~May 2026 | Deck (binario de investigación) | 1 | Binario objetivo de la RE | `~/.local/share/Steam/linux32/steamclient.so`, ELF 32-bit i386, ~48 MB, BuildID `f92deb5ee064a2cf28977bd86a6ed43f420cfcba`; ~826 símbolos exportados | RESEARCH cabecera; §8 paso 1 |
| sin fecha (post-May-2026) | Deck | 1 | Finder package-0 en builds posteriores sin cambios de código | "verified on the post-May-2026 builds `7c4ac73e` and `db0d79c2` without code changes"; offsets `cache+0xc58` root, `+0xc6c` node array, nodo 0x18 B | RESEARCH cabecera; §13.4 |
| build `f92deb5e` (estático, Ghidra) | PC análisis | 1 | GMRC `…BYieldingGetManifestRequestCode` | Ghidra `FUN_012d3bd0`, file vaddr `0x12c3bd0`; patrón `E8 ?? ?? ?? ?? 05 ?? ?? ?? ?? 55 89 E5 57 56 53 81 EC 10 01 00 00 8B 7D 08 8B 4D 20` "Verified **unique** in this build"; `out_code` en `[ebp+0x20]` | RESEARCH §4 GMRC |
| build `7c4ac73e` (estático) | PC análisis | 1 | `GetShaderCacheDepot` | file vaddr `0xfd9ca0`; patrón `kShaderCacheDepotPattern` único; único caller `CGetShaderDepotManifestJob::BYieldingRunClientJob` (VA `0x1725a00`), un solo xref `E8` en `0x1725a99` | RESEARCH §4 ShaderDepot; §13.10 |
| sin fecha | Deck/codespace | 1 | lumalinux en `LD_AUDIT` en vez de `LD_PRELOAD` | Steam muere al arrancar con `realloc(): invalid pointer`; a nivel launcher 64-bit `wrong ELF class: ELFCLASS32` | RESEARCH §5; decouple-headcrab-plan.md §2 (moon: CR en LD_AUDIT idem) |
| sin fecha | codespace | 1/2 | Inyectar depots en `pDepotInfo` (BuildDep) | SIGSEGV posterior ("virtual call on a null/bad pointer in the appinfo path") | RESEARCH §6 |
| ≤v0.7 / v0.8 | codespace | 1 | Swap manual de `m_pMemory` en `AppIdVec` | crash del hilo appinfo-cache (null deref en `libvstdlib_s.so`); resuelto al desensamblar "the only 5 `realloc@plt` callers", leaf helper `+0x5c79d30` = `realloc(ptr, count*elem_size)` sin wrapper tier0 | RESEARCH §6; §11.2 |
| sin fecha | codespace | 1 | Hook en `InitFromPacket` para ver GMRC | "a heartbeat showed our hook ran <513 times total and never on GMRC" | RESEARCH §6 |
| v0.13.1 | PC (Ghidra Jython) | 1 | `Memory.findBytes` con wildcards en patrones cortos | "returned zero hits even when the bytes existed" → el chequeo de anclas busca el literal final y verifica alrededor | RESEARCH §8.1 "Jython quirk" |
| 2026-06-24 (build `7c4ac73e`, estático) | PC | 1 | Wildcard del frame size (`sub esp,imm32`) | "wildcarding it leaves GMRC unique but makes **BuildDep collide (6 matches) and LoadPackage (6)**"; frame sizes sobrevivieron `f92deb5e→7c4ac73e`; cache global movió `0x3a1bc→0x3967c`; BuildDep/GMRC/DepotKey "re-derive byte-identical and still UNIQUE"; todos los patrones reconfirmados UNIQUE o expected-multi (LoadPackage) en `7c4ac73e` | RESEARCH §8.1 "Which immediates to wildcard" / "Robustness ranking" |
| 2026-09-08 | código | 1 | Cuarta mitigación del race finder: cruce `PackageId` nodo vs objeto | "makes a walk that landed on the wrong object end in a refusal plus a log line instead of a write"; no cubre un PackageInfo rancio del paquete 0 real (borderline: cambio de código, no medición) | RESEARCH §10 "Update 2026-09-08" |
| sin fecha (codespace Ubuntu 24.04, Xvfb+noVNC) | codespace | 1/4 | Crash DepotKey Formula Legends (3194360, redists 228989/228990 de 228980) | `free(): invalid pointer` → abort, heap corruption tras volver el hook. Aislamiento con `LUMA_NO_GMRC`/`LUMA_NO_DEPOTKEY`: sin lumalinux no crash ("content still encrypted"); DepotKey off → `Missing decryption key` 3194361; crash correlado con servir 228989/228990 (owned); servir 3194361 no crasheaba | RESEARCH §12.1–12.2 |
| sin fecha | codespace | 1 | Causa raíz y `out_key` | hook ≤v0.8 en el dispatcher externo y cortocircuito; `out_key` es struct de 128 B (`KeySize=128`), el memcpy de 32 cabía → "the crash was about **state**, not about the buffer" | RESEARCH §12.3 |
| sin fecha | codespace (root, `/proc/PID/mem`) | 1 | Resolución del accessor interno | `this=*(global)+0xd60; vtable=*this; fn=*(vtable+0x18)` leído del Steam vivo → `kDepotKeyFnPattern` v1.0 | RESEARCH §12.5 |
| v0.10.x (tras update de Steam) | codespace | 1/2 | LoadPackage instalado pero nunca disparado | log `LoadPackage hook: INSTALLED` y nunca `LoadPackage: PackageId=0 hit`; v0.10.7 "Steam did NOT refactor it"; v0.10.8 `LUMA_LOADPKG_DEBUG=1` mostró que `LoadPackage` no se llama para PackageId=0 cacheado | RESEARCH §13.1–13.3 |
| cinco builds (hasta 2026-09-08) | Deck/codespace + estático | 1 | `disp32` del idiom cache-access por build | `7c4ac73e`→`0x3a1bc`; `db0d79c2`→`0x3967c`; `d0c0ff6e`→`0x3b314`; `bc54101b29`→`0x3b7d4`; `237495b4` (2026-09-08)→`0x3b7d4`. "Every one of them yields **exactly one** distinct `disp32`" | RESEARCH §13.5 tabla; maintenance.md §E |
| build actual (2026-09) | estático | 1 | Sitios del idiom | dos sitios en rva `0xfdd2dd` y `0x18964e5` con el mismo X, registros `esi` y `eax`; span `r-x` un solo `LOAD R E` de `0x1ffe574` B ≈ 32 MB | RESEARCH §13.5 |
| 2026-09-22 (beta `1790036264`, `9cf4720f`) | estático (`experiment_cache_idiom_free.py`) | 1 | Cambio de layout de `CPackageInfoCache` | 49.8 → 53.5 MB; ambas anclas NOT_FOUND. Tabla bc54101b vs 9cf4720f: loads 598/607; root `0xc58`→`0xf90`; node array `0xc6c`→`0xfa4`; 2º árbol `0xc98`→`0xfd0`; su array `0xcac`→`0xfe4`; campo más leído `0xa70`→`0xda8`; GMRC `sub esp` `0x110`→`0x120`; X `0x3b7d4`→`0x3c7b0`. "26 fields matched one-to-one… every one of them exactly `0x338` higher". SDK `linux32` misma versión: `X = 0x3dcdc`. Reconcile roto por pin de `edi`; ahora UNIQUE en ambos (`0x188c950` stable, `0x1a0d010` beta, coincide con steam-monitor) | RESEARCH §13.5.b; maintenance.md §C Fix paso 1 y 4 |
| 2026-09-08 | código + CI (auditoría) | 1 | Auditoría del finder (9 funciones) | 20 hallazgos: 13 arreglados, 6 en `KNOWN LIMITS`, 1 abierto (doble papel de `0xc58`); `grep -rn cache_global_disp src/` daba cero (borderline: auditoría, no medición) | RESEARCH §13.5.a |
| 2026-09-08 | codespace SteamOS, `steamclient.so` `237495b4…` | 1/2 | Validación en vivo del localizador reescrito (solo `liblumalinux.so` sustituido) | `PKG0_FINDER: GOT UNIQUE — 1 site(s), got=0x…` / `cache-access idiom UNIQUE — 2 site(s), disp=0x3b7d4` / `Finder resolve: name=PKG0Finder method=… outcome=resolved` / `HIT pkg=… PackageId=0 AppIdVec{size=196}`, appids `{5,7,8,90}`; `3/3 hooks active`. No ejercitado: `method=rva`, ramas AMBIGUOUS, fin del hilo, `outcome=miss` | RESEARCH §13.5.b; maintenance.md §C "Manual on-device validation" |
| 2026-06-24 (build `7c4ac73e`, estático) | PC (Ghidra) | 1/4 | Path B: función que loguea `Failed to decrypt cached manifest %d_%llu` | string VA `0xdb0538`; función VA `0xfa9050` (`applicationmanager.cpp`), callers `0xfe0940`, `0xfe0c95`; `lea eax,[esi-0x20e74cc]` en `0xfa9386`, `esi=0x2e97a04`; lookup `call 0xfa8b40`; scheduler `0x2a5d060`; decrypt `call 0x29b5f10` (flags `[arg+0xbc/0xbd/0xbe]`, iter `[arg+0xa8]`/`[arg+0xb0]`); validación `0x24fc030`; devuelve `[entry+4]` en éxito y fallo. Conclusión: no hay seam; no se envía. Binario en rama `tmp/steamclient-bin`, borrada | RESEARCH §13.9 |
| build `f5eb8bd3` (estático) | PC | 1 | RTTI aplicada a cada hook | DepotKey `0x1188300` (virtual, `CConfigStore` slot 6); GMRC `0x1351890`; BuildDep `0xfe1bf0`; ShaderDepot `0x102d9f0`; LoadPackage `0xfce960`. `12CConfigStore` @`0xb892e8` → typeinfo @`0x2e65f0c` → vtable @`0x2e65e8c` (26 slots), slot 6 = `0x1188300`; `30CClientUnifiedServiceTransport` @`0xb7bf20` → vtable @`0x2e63cd0` (11 slots) | RESEARCH §15.2 |
| builds `f5eb8bd3` / `1782861641` | CI (`check_patterns.py`) | 1 | Derivación del slot DepotKey por firma | "derives slot 6 (`0x1188300`) uniquely on builds f5eb8bd3 / 1782861641" | RESEARCH §15.3 |
| 2026-07-06 | Deck | 1 | Migración DepotKey a RTTI | "that migration **shipped** (2026-07-06, RESEARCH §15)"; `experiment_rtti_depotkey.py` fue el gate | maintenance.md §A.2, §E |
| v0.16.2 → v0.16.7 | Deck (game mode) | 1/7 | OOBE por escritura rasgada | v0.16.2 "hard-crashed Steam on the switch to game mode, five fast exits in a row → gamescope's `short_session_recover` wiped `~/.local/share/Steam` → OOBE"; mismos bytes en desktop limpios (v0.16.4). Coredump: `trap invalid opcode ip:f59da5e0 … in liblumalinux.so`, `#0 YAML::Scanner::PushIndentTo (liblumalinux.so + 0x3175e0)`, `#1 0x00000013`. Segundo coredump `cloud_redirect.so → __backtrace → ld.so` durante `sigreturn` = race aparte | RESEARCH §17.3; maintenance.md §D |
| v0.16.7 | código | 1 | Straddle de línea de caché del rel32 | "If the operand would straddle a cache line (rare, ~6%), skip that site" | RESEARCH §17.4 |
| 2026-09-15 | codespace | 1 | Cómo inyectar `LUMA_GMRC=1` en el codespace | va en `LUMA_GMRC=1 ~/start-gamemode.sh`; hay que `pkill -f gamemode-supervisor` antes; comprobar con `tr '\0' '\n' < /proc/$(pgrep -o -x steam)/environ \| grep ^LUMA` | update-testing.md Part 3 cabecera |
| 2026-09-19 | CI (`probe-steam.yml`) | 1 | Beta que recompila `steamclient.so` | "49.8 → 53.5 MB" invisible para el monitor diario (solo `steamdeck_stable`) | maintenance.md §A.2 "Ahead of time" |
| 2026-10-01 | CI (`probe-steam.yml`, `readelf -n`) | 1 | Versión de Steam vs binario; manifests Deck servidos | "Deck stable and desktop stable both said `1788652215` and shipped different `steamclient.so` (`bc54101b`/`a577b836` vs `237495b4`/`29734b56`)"; solo existen `steamdeck_stable` y `steamdeck_publicbeta` (`steamdeck_main`, `steamdeck_preview`, `steamdeck_beta` → 404) | maintenance.md §A.2 "Ahead of time" |
| 2026-09-14 (selftest run #7) | CI | 1 | Ghidra headless y la vcall indirecta de DepotKey | "headless Ghidra never resolved that indirect call on any build tried" → desde 2026-09-22 el script solo valida | maintenance.md §A.2 DepotKey |
| 2026-09-14 (build `bc54101b`) / 2026-09-22 | CI (`watch-steam-selftest.yml` target=criticals) | 1/11 | Cadena by-name de DepotKey | "pattern corrupted → exit 3 → by-name derive → apply → re-validate CLEAN" | maintenance.md §A.2 DepotKey |
| todos los builds medidos | CI | 1 | Ancla Reconcile `push 0x7d ; push eax ; call` + lee `this` y sale en `<= 0` | "exactly one on every build measured"; "On every build measured so far Ghidra never has to start" | maintenance.md §A.2 Reconcile; box "Automated first" |
| sin fecha (build `bc54101b`) | estático | 1 | Copia rancia del patrón DepotKey en `derive_patterns.py` | validaba "UNIQUE — keep it" en RVA `0x189fca0`, "a function that is not DepotKey (the accessor is `0x11a4500` on `bc54101b`)" | maintenance.md §A.3 |
| sin fecha | codespace/Deck | 1/11 | Build 64-bit de host copiado como `liblumalinux.so` | "copies over cleanly and then simply never loads (`ELF file class ELFCLASS32 incorrect`… no lumalinux banner)" — parece un problema de wrapper (§B) y no lo es | maintenance.md §C paso 2; update-testing.md Layer 4 |
| sin fecha | Deck/codespace | 1 | Toast de hooks | `3/3 hooks active` con defaults actuales (DepotKey, ShaderDepot, GMRC); GMRC opt-in en v0.20.x, on por defecto desde v0.21.0; `4/4` en v0.14.0 (con BuildDep) | maintenance.md "Look at the log first"; RESEARCH §13.10 |
| sin fecha (validado "live") | Deck | 1 | `tools/xlate_vaddr.py` file-vaddr → runtime | `0x118c1f0 -> 0xc7bcf1f0` | rva-feed-design.md §3 |
| sin fecha (feed de ejemplo) | CI | 1 | Fichero de feed `res/rvas/dd3ca9…af19.yaml` | `steam_version: 1785187029`; DepotKey `0x118c1f0`, GMRC `0x4d9f00`, ShaderDepot `0x1b3e90`, Reconcile `0x5a11c0`; `depotkey_rtti` slot 6 rva `0x118c1f0`; `finder.cache_global_disp: "0x3967c"` | rva-feed-design.md §5 |
| v0.18.0 (fases 3–4) | Deck | 1 | RVA-first en vivo | fase 3: "`method=rva target=…` matches the pattern target"; fase 4: "`RvaFeed: loaded 4 hook RVA(s)` + `method=rva`" con fetch HTTPS real y caché de disco | rva-feed-design.md §12 |
| 2026-08-08 → 2026-08-11 (WS1.1) | Deck real | 1/11 | `setup.sh` (wrapper) e2e | instalación limpia: "4 `.so` mapeadas en el cliente 32-bit, `status.json` con hooks activos"; migración desde headcrab: `steam.sh` vanilla, Game Mode arranca por el drop-in de systemd | decouple-headcrab-plan.md WS1.1; WS5 |
| WS1.2 | Deck | 1/11 | Fail-safe anti-brick y guardian | "Probado: inyecta normal / va vanilla latcheado"; guardian: "genera y habilita los units; apply idempotente; uninstall restaura" | decouple-headcrab-plan.md WS1.2 |
| WS5 (verificado en Deck) | Deck | 1 | ¿Llega el PATH drop-in a la sesión gamescope? | "**no** — el PATH drop-in NO llega a Game Mode (verificado en Deck)" → drop-in de systemd en `steam-launcher.service` | decouple-headcrab-plan.md WS1.2 / WS5 |
| WS5 | codespace Arch limpio + Deck real | 1/9 | Validación completa | cargan 4 `.so` (SLSsteam LD_AUDIT; CR + lumalinux + netsock en el proceso 32-bit); `status.json` hooks activos y `blocked=null`; "un no-owned descarga (las 6 gates abren); el race de descarga (#38) se auto-cura vía re-check de Steam". Pendiente único: downgrade/mirror nunca ejercitado | decouple-headcrab-plan.md WS5 |
| sin fecha (cold-check) | CI / devcontainer port-testing | 1/12 | `check_patterns.py` + hash gate contra el `steamclient.so` desktop (`steam_client_ubuntu12`) del build pineado por Headcrab | "patterns match (CLEAN) and the hash equals the whitelisted Deck hash" | cachyos-port.md item 1; porting-cachyos.md §6 |
| 2026-08-04 | devcontainer CachyOS real (headless Steam) | 1/12 | lumalinux en CachyOS | "lumalinux loaded `3/3 hooks active` LIVE on a genuine CachyOS install" (DepotKey, GMRC, ShaderDepot, finder); `platform_info.summary()` → `distro=cachyos`; `/usr/bin/steamos-session-select` tiene caso `plasma` y no `desktop`; `/usr/lib/steamos/steam-short-session-tracker` usa `/tmp/steamos-short-session-tracker`, `count_before_reset=3`, `do_repair()` re-extrae el bootstrap; flujo desktop E2E (headcrab → SLSsteam → lumalinux → LumaDeck → Decky) | cachyos-port.md cabecera; porting-cachyos.md §0 "Live validation" |
| issue #31 (campo) | CachyOS Handheld de un usuario | 1/11 | Primer boot tras instalar | "the plugin reported **all hooks injected on first Steam boot**"; siguiente reinicio nada inyectado, Steam colgado, pantalla negra al volver a Game Mode; sin logs | cachyos-port.md "Why the core doesn't move"; porting-cachyos.md §2 "Field evidence" |
| 2026-09-22 | CI | 1/12 | "mismo binario desktop y Deck" dejó de cumplirse | "Deck stable 1788652215 / `bc54101b…` vs a different desktop stable" → nota quitada de `fetch_steamclient.py` | cachyos-port.md item 2 |

#### Función 2

| Fecha | Dónde | F | Qué se midió | Resultado | Fuente original |
|---|---|---|---|---|---|
| 2026-06 | PC Arch (mismo run) | 2 | Depots con `gid=0` siguen inyectados en package 0 | log `PKG0_FINDER: HIT PackageId=0` + `all N depot ids already in PackageId=0`; BuildDep: `app <appid> … none required patching`, sin PATCH; SLSsteam `Added <appid> to AdditionalApps` + `PlayNotOwnedGames: 1` | RESEARCH §14.2 |
| sin fecha (pre-v0.16.15) | codespace | 2/9 | Add Game con Steam abierto, sin reconcile | content_log `has no changes, 0 target` → `0 mounted depots` → "Fully Installed, files missing"; tras reinicio `config changed: added depots …`. "Verified in both Mina the Hollower (2379780) and Balatro" (nota: 2379780 es el AppID de Balatro; el doc lo atribuye a Mina) | method.md §8; RESEARCH §18 "Problem" |
| v0.16.14 | codespace | 2 | `DepotIdVec` (+0x48) como palanca no-restart (`LUMA_DEPOT_IDVEC`) | "**no change** — still `0 target depots` without a restart"; descartado, eliminado en v0.16.15 | method.md §8; RESEARCH §6 último bullet; §18 "Dead end" |
| 2026-07-20 | codespace (v0.16.15, marker `no_restart`) | 2 | Reconcile de licencias en vivo (juego añadido mid-session) | `22:02:28 KeyStore watcher: keys.txt changed — reloaded (Size=24), reconcile armed` / `22:02:33 LoadPackage[finder]: APPENDED 5 id(s)` / `22:02:33 Patterns: NotifyLicensesUpdated found at 0x… (RVA 0x9f0f10)` / `22:02:33 Reconcile: broadcast LicensesUpdated_t` / content_log `22:02:36 preallocated 6 files (272 MB)`, `Downloading … depot 2868390 / 1942282`, `22:02:50 finished update, 2 mounted depots`, `22:03:04 App Running`. "Hang: none" | method.md §8; RESEARCH §18 "Verified live (2026-07-20…)" |
| 2026-09-04 | codespace, cliente `1788400362` | 2 | Estrés caché fría del reconcile (`tools/experiment_cold_cache.sh`): caliente+reconcile, fría+reconcile, segundo disparo consecutivo, con ids de depot inexistentes | "En los tres, `APPENDED` y `Reconcile: broadcast` caen en el mismo segundo y la UI responde con normalidad"; `AppIdVec` `size=196 alloc=202` idéntico en frío y caliente. Límites: disparo sintético, un build, no prueba que `ProcessPendingLicenseUpdates` termine | RESEARCH §18 "[AMPLIADO 2026-09-04…]" |
| 2026-10-06 (dos veces) | codespace SteamOS | 2/3 | Licencia inyectada en caliente sobre juego instalado | "añadir licencia en caliente no baja nada (medido dos veces) y quitarla no borra nada (medido, ni en caliente ni al reiniciar)" | owned-games-guide.md §1.1 "El cálculo de depots" |
| 2026-10-07 (`4f8579c`) / 08:10 | codespace SteamOS | 2 | Quitar línea de keys.txt → `erase` del vector package 0 + reconcile | "medido: un verify tras el uninstall ya no baja nada; antes volvía a bajar los DLC enteros"; `- AppIdVec` ×12, verify `4 target`, nada baja; "Your stuff" sigue hasta reiniciar | owned-games-guide.md §1.2 keys.txt; §3.D; §4 fila 2026-10-07 08:10 |
| 2026-10-06 10:15 | codespace SteamOS | 2/3 | Reinicio sin licencia + verify | "8 depots: quitar licencia no quita depots" | owned-games-guide.md §4 |
| 2026-10-06 18:47 | codespace SteamOS | 2/3 | Ciclo 26 ms tras el reconcile, Steam de 2 min | "nada; Steam no procesó el aviso (sin `OnAppLicensesChanged`)" | owned-games-guide.md §3.C; §4 |
| 2026-10-07 07:20 | codespace SteamOS | 2/3 | `true` tras un uninstall, sin reiniciar, vector append-only | "Steam vuelve a bajar los DLC enteros (licencia rancia en el vector, manifests en depotcache, clave retirada)" | owned-games-guide.md §4 |
| medido 06-10 | codespace SteamOS | 2 | `packageinfo.vdf` frente a la inyección en memoria | "packageinfo.vdf es el fichero de Steam, inmune a la inyección en memoria de lumalinux (medido 06-10)" | owned-games-guide.md §3.F |
| sin fecha (SLSsteam `146e28f`) | codespace SteamOS | 2/9 | Aparición/desaparición en biblioteca en caliente | Al añadir aparece porque SLSsteam emite `AppLicensesChanged_t` solo para apps nuevas; al quitar no emite nada y el `LicensesUpdated_t` de lumalinux no suelta la entrada; al reiniciar desaparece (medido) | owned-games-guide.md §3.E |
| sin fecha | codespace | 2 | Ticket de ownership (eMsg 858) con SLSsteam | content_log imprime `failed to update ownership ticket (Access Denied)`, "non-fatal; the install proceeds" | RESEARCH §3.1; §19.5 |
| sin fecha | codespace | 2 | Qué ids inyectar en PackageId=0 | app id 2379780 → "0 target"; depot ids 228989, 2379781, 2379782 → sobreviven el filtro | RESEARCH §6 |
| sin fecha | codespace | 2/4 | Detección dinámica de ownership (tres intentos) | `CheckAppOwnership` inútil (SLSsteam lo falsea); llamar al dispatcher original primero → OK para 3194361 por config.vdf y "the **denied RPC leaves state that crashes**"; cache-first → "its *miss* path has an internal side effect that crashes anyway — verified by probing even with a scratch buffer" | RESEARCH §12.4 |
| v0.10.11 (sin fecha) | codespace, Brotato 1942280 | 2/4/9 | E2E con finder | `PKG0_FINDER: cache-access idiom found 2 time(s), disp=0x3a1bc` / `GOT=0xd4dbea04 disp=0x3a1bc cache_global=0xd4df8bc0` / `HIT pkg=0xd7c59380 PackageId=0 AppIdVec{mem=… size=192 alloc=202}` / `AppIdVec[0..4) = {5, 7, 8, 90}` / `APPENDED 8 depot id(s) … (size 192 -> 200)` `+ depot 1942280 1942281 1942282 1942283 2379780 2379781 2379782 2868390`; BuildDep surface 2868390/1942282; `SERVED local key for depot 2868390 / 1942282 / 1942280`; GMRC `got code … INJECTED … (depot 1942280)`; content_log `steamapps/common/Brotato : 6 updated`, `finished update, 2 mounted depots : 2868390, 1942282`, `Fully Installed` | RESEARCH §13.6 |
| 2026-06-24 | Deck (v0.13.8, revertido en v0.13.9) | 2/4 | Excluir el depot shader keyless del package 0 | `excluded 2 presence-only depot(s)` pero Steam "**still** ran the shader update for 368340 (`Missing decryption key`)" → el pre-cache lo manda el appinfo, no el filtro de licencia | RESEARCH §13.8 "Attempt 2" |

#### Función 3

| Fecha | Dónde | F | Qué se midió | Resultado | Fuente original |
|---|---|---|---|---|---|
| 2026-10-06 (día entero) | codespace SteamOS, Darkest Dungeon 262060 | 3/2 | Todo lo afirmado en la guía owned "está medido en el codespace SteamOS el 2026-10-06 con Darkest Dungeon (262060), salvo donde se dice 'no medido'" | (ver filas por hora abajo) | owned-games-guide.md cabecera |
| 2026-10-06 14:51 | codespace SteamOS | 3/4 | Borrar DLC necesita la clave | Steam desmarcó el DLC, fue a borrar, la clave ya no estaba → "Update required" reintentando cada 5 minutos; al volver la clave "borró los 4242 archivos al instante" | owned-games-guide.md §1.1 "La clave hace falta para borrar"; §4 filas 14:51/14:58 |
| 2026-10-06 12:43 | codespace SteamOS | 3 | `SetDLCEnabled(true)` sobre DLC no desmarcado | "no hace nada (medido a las 12:43)" — solo planifica si cambia algo | owned-games-guide.md §1.1 "SetDLCEnabled"; §4 fila 12:43 |
| 2026-10-06 09:47 | codespace SteamOS | 3/4 | Add owned + Install (juego no instalado) | `added depots` base+DLC; 8 depots; "lumalinux sirvió solo las 4 claves de DLC" | owned-games-guide.md §3.B; §4 fila 09:47 |
| 2026-10-06 10:01 | codespace SteamOS | 3/9 | Uninstall owned versión vieja (borró manifests de depotcache) | "nada; depots siguen montados"; dejó "1 GB huérfano" tras el `SetDLCEnabled(false)` de 10:23 | owned-games-guide.md §3.D; §4 filas 10:01, 10:23 |
| 2026-10-06 10:23 | codespace SteamOS | 3/9 | `SetDLCEnabled(false)` sin manifests en depotcache | `removed depots`, `0 deleted files`: 1 GB huérfano | owned-games-guide.md §4 |
| 2026-10-06 12:12 | codespace SteamOS | 3 | Add owned, juego instalado, Steam abierto | nada | owned-games-guide.md §3.C; §4 |
| 2026-10-06 12:20 | codespace SteamOS | 3/5 | Reinicio de Steam con licencia inyectada | `config changed: added depots`, baja 861 MB, no reutiliza huérfanos | owned-games-guide.md §3.C; §4 |
| 2026-10-06 12:26 | codespace SteamOS | 3/9 | `SetDLCEnabled(false)` con manifests | `4242 deleted files`, 4 depots, en 2 s, carpetas vacías | owned-games-guide.md §3.D paso 3; §4 |
| 2026-10-06 12:31 | codespace SteamOS | 3 | `SetDLCEnabled(true)` con licencia inyectada rancia y marca puesta | `added depots` al instante | owned-games-guide.md §4 |
| 2026-10-06 12:48 | codespace SteamOS | 3 | `false`+`true` con licencia en caliente | `added depots`, baja 861 MB | owned-games-guide.md §4 |
| 2026-10-06 14:51 | codespace SteamOS | 3/4 | Uninstall owned (primera versión): `false` y clave quitada en el mismo segundo | `removed depots`, luego `Missing decryption key`, "Update required", reintento cada 5 min, archivos intactos | owned-games-guide.md §4 |
| 2026-10-06 14:58 | codespace SteamOS | 3/4 | Clave de vuelta (re-add) | "el reintento borra 4242 archivos al instante" | owned-games-guide.md §4 |
| 2026-10-06 16:45 | codespace SteamOS | 3 | Add owned instalado, ciclo lanzado en el mismo segundo que el reconcile | "marca limpiada, nada añadido: el `true` llegó antes que la licencia" | owned-games-guide.md §4 |
| 2026-10-06 16:53 | codespace SteamOS | 3 | `DisabledDLC` heredado de un uninstall anterior | el plan de arranque lo respeta y no baja nada → por eso el add marca `SetDLCEnabled(true)` una vez | owned-games-guide.md §3.C |
| 2026-10-06 17:03 | codespace SteamOS | 3 | `true` con la licencia asentada (18 min) | `added depots`, baja 861 MB | owned-games-guide.md §4 |
| 2026-10-06 17:16 | codespace SteamOS | 3/4 | Uninstall owned (versión final): `false`, claves retiradas a `retired_keys.txt` | "Steam pide la clave, lumalinux la sirve desde retired_keys.txt, borra los archivos, 4 depots" | owned-games-guide.md §4 |
| 2026-10-06 17:19 | codespace SteamOS | 3 | Add owned instalado: ciclo 1 s tras el reconcile | `added depots`, baja 861 MB | owned-games-guide.md §4 |
| 2026-10-06 19:07 | codespace SteamOS | 3 | Add tras reiniciar Steam; `reconcile.json` viejo | esperó "los 20 s de tope"; `added depots` a los 20 s, baja | owned-games-guide.md §4 |
| 2026-10-06 19:25, 19:32, 19:37 | codespace SteamOS | 3/9 | Uninstall owned (claves retiradas) ×3 | "Steam borra en 2-7 s, 3/3" | owned-games-guide.md §4 |
| 2026-10-06 19:28, 19:32, 19:37 | codespace SteamOS | 3 | Add owned instalado desde la principal / menú cerrado / página del juego | `added depots` 1 s tras el reconcile, 3/3; `RequestAppInfoUpdate` + `UpdatesJob: finished OK` en `appinfo_log.txt` ese segundo | owned-games-guide.md §3.C; §4 |
| 2026-10-06 20:25 | codespace SteamOS | 3 | Add owned instalado, ciclo tras la señal de `appinfo_log` | `added depots` 1 s después | owned-games-guide.md §4 |
| 2026-10-06 20:29 / 20:36 | codespace SteamOS | 3 | Add 42 s tras arrancar Steam, juego sin pintar en el inicio | "casilla cambiada, Steam sin plan" (ni `user config changed`); a las 20:36 tras abrir la página el mismo ciclo baja | owned-games-guide.md §3.C; §4 |
| 2026-10-06 20:53 | codespace SteamOS | 3 | Steam de 5 min, add sin tocar el juego, luego abrir página y ciclar | "sin página: nada; con página: `added depots`" | owned-games-guide.md §4 |
| 2026-10-06 20:55, 21:00, 21:04 | codespace SteamOS | 3 | Arranques con el juego en el inicio | "Steam carga sus stats a los 3-7 s: 'despierto'" | owned-games-guide.md §4 |
| 2026-10-06 (commits `9001216`…`beff30b`) | codespace SteamOS | 3 | Forzar descarga sin reiniciar con ciclo `false`→`true` | "Funcionó 5 de 5 cuando el juego estaba 'despierto'… y falló cuando el add caía en los primeros segundos tras arrancar Steam… y una vez en que Steam ignoró el aviso (18:47)". Se quitó | owned-games-guide.md §3.C "Lo que se probó y se quitó" |
| 2026-10-07 06:10 | codespace SteamOS | 3 | Licencia inyectada, lanzar el juego | `App Running` sin plan; sin DLC | owned-games-guide.md §3.C; §4 |
| 2026-10-07 06:39 | codespace SteamOS | 3 | Add owned en juego instalado, código final (licencia + `true` suelto) | "marca limpia, Steam sin tocar nada" | owned-games-guide.md §4 |
| 2026-10-07 06:42 | codespace SteamOS | 3/5 | Reinicio de Steam | `config changed: added depots`, 861 MB "a los pocos segundos del arranque" | owned-games-guide.md §4 |
| 2026-10-07 06:45 | codespace SteamOS | 3/9 | Uninstall owned, código final | `removed depots`, "borrado con la clave retirada en 5 s, 4 depots" | owned-games-guide.md §4 |
| 2026-10-07 07:36 | codespace SteamOS | 3 | Add owned en juego instalado + verify desde la página | "el verify planifica con la licencia y baja los DLC" | owned-games-guide.md §3.C; §4 |
| 2026-10-07 07:47 | codespace SteamOS | 3/9 | Arranque con marca puesta, sin licencia, claves retiradas (uninstall "dormido" simulado) | `config changed: removed depots`, borra con la clave retirada: "el fallback del uninstall es el siguiente arranque" | owned-games-guide.md §4 |
| 2026-09-13 | devcontainer, Brotato | 3/4 | DLC sin línea de clave → invisible; añadir clave+manifest y reiniciar | "made it download the DLC by itself (Brotato, 55 MB, 'Update Optional')" | RESEARCH §19.3; update-testing.md V5 |
| 2026-09-17 | codespace, Brotato 1942280 | 3/4/5 | V5 depot DLC nuevo simulado (quitar línea de 2868390, limpiar `hubcap_attempts.json`) | **pass (mitad LumaDeck)**: `zip for 1942280 from 'Morrenus' (32298 bytes)`, `update check 1942280: reinstalled from zip: new depots [2868390], build 23429717`, línea de clave de vuelta. Mitad Steam no corrió (el `.acf` seguía listando 2868390). Claves nuevas solo del zip (Hubcap/Ryu); si no, `no source returned one` a diario | update-testing.md Part 4 V5 |
| 2026-10-05 | codespace SteamOS (`.devcontainer/steamos/`, user `deck`), SLSsteam `main@9c829a7`, Darkest Dungeon 262060 | 3/2 | V6 / runs Ref, A, B, C, E | Ref: 4 depots 262065, 262066, 445702, 1117862. A: igual, nada para 702540/702542. B (`702540` a mano en `AdditionalApps`, `Config reloaded!`): Shieldbreaker mostrado owned, **ningún** depot, ni tras Verify (`finished update, 4 mounted depots`). C (LumaDeck add): Shieldbreaker no owned, **los cuatro DLC de pago** bajan, todo disponible en juego. E (262060 fuera, DLC dentro, reinicio): seis DLC owned, "Install" marcado, 8 depots, sin `added`/`removed depots`, juego arranca | RESEARCH §21.2 tabla; update-testing.md V6 |
| 2026-10-05 13:35–13:38 | codespace SteamOS | 3/2/4 | Run C en detalle | `13:35:29 LoadPackage[finder]: APPENDED 27 id(s) to PackageId=0 AppIdVec … + AppIdVec 702540 / 702541 / 702542 …` / `13:37:28 AppID 262060 config changed : added depots 580102,702542,735732,4964111` (al salir del juego) / `13:38:17 Downloading 169 chunks for depot 702542 (4258374143576351227)` / `13:38:17 LoadDepotKey: SERVED local key for depot 702542` / `13:38:55 AppID 262060 finished update, 8 mounted depots`. keys.txt `702542;262060;4258374143576351227;40024569;4ede…`; `AdditionalApps` solo `262060`; `add_game_dlcs` no escribió (6 ≤ 64). `dlc/` con 580100_crimson_court, 702540_shieldbreaker, 735730_color_of_madness, 4964110_fires_edge | RESEARCH §21.2 |

#### Función 4

| Fecha | Dónde | F | Qué se midió | Resultado | Fuente original |
|---|---|---|---|---|---|
| 2026-09-09 | codespace / ecosistema | 4 | Los tres proveedores de request code (`opensteamtool`, `wudrm`, `steamrun`) | "all three providers are dead"; KeySteam, SteaMidra y LuaTools Windows rompieron el mismo día; hilos 3a.lol / caigamer sin reemplazo | method.md §1 (status box); §5 cabecera; RESEARCH §19 |
| 2026-09-11 | codespace SteamOS | 4/9 | ¿Lee Steam `config/depotcache/`? | No: "Steam does **not** read `config/depotcache/`"; manifest solo ahí → Steam pide código y falla; copiado a `depotcache/` → lo coge en el reintento de ~30 s. steamidra dejó de escribir esa copia en 0.20.1 | method.md §3 Phase 0 paso 2; RESEARCH §19.3; dev-multi-library.md §5 Q3 |
| sin fecha (pre-2026-09) | codespace | 4 | ¿Cuándo dispara GMRC con manifests pre-sembrados? | "Brotato fired GMRC for its shader depot (1942280); Tilt It! Golf, whose content manifests were all pre-seeded and which ran no shader pre-cache, never fired GMRC and still installed end to end" | method.md §6 "Where GMRC stays load-bearing" |
| 2026-10-06 10:14 / 14:51 | codespace SteamOS | 4 | Claves de depot en `config/config.vdf` | "a las 10:14 las claves volvieron tras un reinicio; a las 14:51 no estaban ni en disco ni en memoria". Desde 2026-10-06 LumaDeck no edita ese archivo al desinstalar | owned-games-guide.md §1.1 "Claves de depot en config/config.vdf" |
| 2026-10-06 10:11 | codespace SteamOS | 4 | Verify sin claves en config.vdf | `Missing decryption key` | owned-games-guide.md §4 |
| sin fecha | codespace | 4 | Claves en `config.vdf` para depots no owned | "Steam **deletes** the entries for unowned depots on shutdown (`grep -c DecryptionKey` dropped from 3 to 1 across a restart)" | RESEARCH §6 primer bullet |
| sin fecha (build f92deb5e) | codespace | 4 | Convar `@bClientTryRequestManifestWithoutCode` (objeto RVA `0x2e7b1a0`, `entry.init216`) puesto a 1 | el cliente intenta sin código "but **the CDN still requires the code** → 'No connection'" | RESEARCH §6 |
| sin fecha (Brotato vs Balatro) | codespace | 4 | Clave del depot shader ausente | Brotato (clave real para 1942280) instala; Balatro (`2379780;` sin clave) aborta con `Missing decryption key` cancelando toda la app | RESEARCH §6; §13.7 |
| 2026-07 (on-device) | Deck | 4 | Cloudflare / User-Agent en `manifest.opensteamtool.com` | `curl -A "OpenSteamTool/1.0" …/{gid}` devuelve el código; `curl` a pelo recibe el "Just a moment…"; "a residential Steam Deck gets the same challenge with bare curl" → no es JA3/IP | RESEARCH §7 "Cloudflare / User-Agent gate" |
| 2026-07 | Deck | 4 | Estado de endpoints | "**wudrm 502** (flaky/down), **steam.run 404**, **opensteamtool works** (with a non-curl UA)"; wudrm único proveedor + 502 = causa del popup Silksong depot 1030300 | RESEARCH §7 |
| sin fecha | codespace | 4 | Variación del código por sesión | gid `3512319404653808464` → `15549905601718457808`, luego `3261884576850880630` otro día | RESEARCH §7 |
| sin fecha | codespace | 4 | Valores que funcionaron (Balatro) | depot 2379781 gid 3512319404653808464; depot 2379782 gid 1898957422191678575; depot 228989 gid 3514306556860204959 | RESEARCH §7 "Confirmed working values" |
| sin fecha | codespace | 4/9 | Verificación e2e Formula Legends con hook v1.0 | sirve 228989, 228990 y 3194361 (`KeySize=128`) "**without a single crash dump**", 13 GB descargados, `appmanifest_3194360.acf` `StateFlags=4`; residuo benigno: auto-update con `Missing decryption key` / `UpdateResult=8` sin cambiar StateFlags | RESEARCH §12.6; dev-multi-library.md §4b |
| ≤v0.8.0 → v0.8.1 | codespace | 4 | Gating GMRC solo por `HasManifestGid` | manifests del shader pre-cache pasaban al original → "Access Denied" → "No connection" los primeros minutos; "only visible with an `.acf` of `StateFlags=1`; with `StateFlags=4` Steam skipped the shader pre-cache entirely" | RESEARCH §11.5 History |
| 2026-06 | PC Arch (Headcrab + lumalinux v0.13.5) | 4 | Balatro con zip sin clave de shader | primer intento `Missing decryption key, state 0x40a`; Steam "re-queued it (300 s delay) and the retry succeeded": bajó 2379781, `StateFlags=4` | RESEARCH §13.7 "Update (2026-06)"; update-testing.md Step 2 nota |
| 2026-06-23 | Deck | 4 | Zips Hubcap de cinco juegos: ¿llevan clave del depot shader? | Brotato `addappid(1942280, 1, "<key>")` sí → limpio; Formula Legends sí → limpio; Balatro `addappid(2379780)` no; CrossCode (368340) no; Blasphemous (774361) no → `Missing decryption key`. Snapshots: Brotato May 2026, CrossCode Sep 2025, Blasphemous Oct 2025 | RESEARCH §13.8 tabla |
| 2026-06-23 | Deck | 4 | Bucle del shader con juego Fully Installed | CrossCode y Blasphemous `state 0xc`: "Steam re-ran the **Shader update** every ~5 min (`Shader update changed: Running Update … → Missing decryption key`)" — corrige la afirmación "transitorio" de §13.7 | RESEARCH §13.8 "Correction to the §13.7" |
| 2026-06-23 | Deck (v0.13.7, revertido en v0.13.8) | 4 | Clave cero de 32 B para depots presence-only | para el bucle post-install; pero en instalación fría `Failed to decrypt cached manifest 368340_…`, `Invalid content configuration`, `Update Paused` hasta "resume" manual: "CrossCode reinstall paused mid-download" | RESEARCH §13.8 "Attempt 1" |
| sin fecha | Deck | 4 | Knob de config para shader por juego | `ShaderCacheManager.App.<appid>` solo `ShaderCacheSize`; `"DisableShaderCache" "1"` global es lo que escribe el toggle "Shader Pre-Caching" de Steam ("verified on Deck") | RESEARCH §13.8 "No per-app config knob" |
| 2026-06-24 | Deck, v0.14.0 release | 4/1 | ShaderDepot e2e en ambas direcciones | `4/4 hooks active`, `ShaderDepot hook: INSTALLED … rva=0x1a5ca0`; CrossCode/Blasphemous: `ShaderDepot: shader depot id <id> is keyless … returning 0`; última línea shader de 368340 `… 09:20:27 … scheduler finished: removed from schedule (result Missing decryption key, state 0x41a)` (antes del hook a 10:45); de 10:48 a 10:57 "**zero** new shader lines". Brotato: hook nunca disparó, `SERVED local key for depot 1942280 (KeySize=128)`, `Shader update changed: Running Update,Preallocating,Downloading,Committing` → `shadercache/1942280/ : 6 updated` → `Shader update changed: None` → `finished update, 2 mounted depots : 2868390, 1942282` | RESEARCH §13.10 "End-to-end verification" |
| sin fecha | Deck | 4 | Zips Hubcap y manifest del depot shader | "confirmed across Silksong/Brotato/Formula Legends/Binding-of-Isaac zips — none ship a `<appid>_*.manifest`"; síntoma Silksong: wudrm caído → "No internet connection" aunque el contenido instaló | RESEARCH §13.11 |
| 2026-09-10 → 2026-09-11 | devcontainer SteamOS | 4 | Qué rompió el 09-09 en lumalinux | el shader pre-cache: el hook devolvía basura de proveedores moribundos → "401 storm on the CDN and stuck the whole download queue"; con fallthrough limpio solo popup "No internet connection", ~30 s de stall y luego instala; quitar GMRC apagaba el finder → "0-target-depot phantom installs during testing" | RESEARCH §19.1 |
| 2026-09-10/11 | devcontainer (`tools/gmrc_mint.py`) | 4 | Qué códigos concede Valve anónimamente | públicos: redists 228980 (depots 228988–229005) y el depot Workshop (id = appid = depot shader). Brotato, RimWorld obtienen el manifest shader; Lethal Company, Silksong, Vampire Survivors no; depots de contenido siempre denegados; gid nunca visto (p.ej. 1) se concede | RESEARCH §19.2 |
| 2026-09-09 (rama de prueba, borrada) | devcontainer | 4 | Login anónimo `node-steam-user` `getManifestRequestCode` | Brotato, Lonely Mountains: **AccessDenied**; control Dota: código y 200 del CDN | RESEARCH §19.2b |
| 2026-09-09 (build `1788652215`, Ghidra + hook in-process) | devcontainer | 4 | `GetCDNAuthToken(this, app_id, depot_id, char* host, CUtlString* out)` | Valve lo emite (`rc=1`) también para depot de pago no owned → no gateado por ownership; irrelevante (no sustituye al request code) | RESEARCH §19.2b |
| sin fecha (2026-09) | codespace (`tools/smc_fresh.py`) | 4 | `P-ToyStore/SteamManifestCache_Pro` | Valheim: manifest 15 min tras Valve; ficheros = cabecera 10 B + raw deflate; "verifies a chunk on the CDN: 22/22 OK"; sin claves | RESEARCH §19.4 |
| 2026-10-07 | codespace | 4 | `api.993499094.xyz/depotkeys.json` | 240,103 claves (221,727 al verlo por primera vez), 18 MB, `last-modified` el mismo día, ids hasta 5.3 M (218 >5 M, 3,136 en 4.5-5 M); 32/32 depots de las luas Hubcap cacheadas presentes con la clave correcta (dos en mayúsculas); NO day-one: STAR WARS: Galactic Racer 4078430 (salió 2026-10-06) sin 4078431 ni 4554460 | RESEARCH §19.4 |
| 2026-10-07 | GitHub API | 4 | `P-ToyStore/SteamManifestCache_Pro` | "gone (GitHub API 404 for the repo)"; cadena ahora depotcache → archivo propio → luastools → Hubcap `/generate/manifest` → zip Hubcap | RESEARCH §19.4 box 2026-10-07 |
| 2026-10-07 10:54 | codespace (clave de usuario) | 4/10 | Hubcap `/generate/manifest` (cuota `single` 1,500/día, un intento por depot+gid/día) | `GET /generate/manifest?depot_id=1942280&manifest_id=2046527723717816735` → 200, `application/octet-stream`, 271 bytes, "byte-identical to the file Steam had in depotcache/" | RESEARCH §19.4 box 2026-10-07 |
| 2026-09-14 | codespace | 4 | Cadena "Free Providers" de `drappula/SFF` (añadida 09-10) | `fylsdy/ManifestHub` 404; mirrors `steamtoolsapp/ManifestHub`, `steamtools-games/ManifestHub3`, `qwe213312/k25FCdfEOoEJ42S6` "carry 0 of the 6 current gids that P-ToyStore had the same day" | RESEARCH §19.4 box 2026-09-14 |
| 2026-09-15/16 | codespace (`tools/gmrc_probe.py`, 1 req/s, cada código al CDN) | 4 | Un código de 20770407 usado desde otra máquina | `steampipe.akamaized.net/depot/2545361/manifest/…` → 200, 160159 B desde el codespace: el GET del CDN es anónimo | RESEARCH §20.1 |
| 2026-07-21 / 2026-09-13 | ecosistema | 4 | Historia de 20770407 | 07-21 `20770407.xyz/manifest/<gid>` (OpenSteamTool #164); 09-13 fork huanyuejue `224931d` → `/manifest/<depot>/<gid>`; "It adapted in four days" | RESEARCH §20.1 |
| 09-15 / 09-16 | codespace | 4 | Disponibilidad de los dos pools | 20770407: "healthy → 31 % failures for an hour → Cloudflare 502 for ~18 h (09-15) → back (09-16)"; manifestdex/wudrm/steam.run "dead 09-09 → 09-16, then all three back the same morning"; 20770407 ~70k usuarios, ~170k req/día, 10 req/10 s por IP | RESEARCH §20.2 tabla |
| 2026-09-15/16 | codespace | 4 | Backend compartido y vida del código | wudrm, steam.run y manifestdex devuelven el **mismo** código (3 coincidencias en el primer run, docenas en el de 42 depots); 20770407 y wudrm en el mismo segundo difieren; manifestdex sirvió un código 5+ min; "Codes live **≥ 58 min** (one checked against the CDN every 2 min from 07:40 to 08:38, still 206)" → "15 minutos" era falso | RESEARCH §20.2 |
| 09-15 | codespace (`LUMA_GMRC_URL` a servidor local con `123456789`) | 4 | Código falso inyectado sin chequeo CDN | Steam cancela con `Unspecified Error`, `Update Paused`, fuera del schedule, no reintenta (= fallo wudrm del 09-10). Con `CdnAcceptsCode` (`af93594`): `CDN REJECTED code 123456789 (HTTP 401) — not handing it to Steam`, Steam: `Failed to get manifest request code, 'Access Denied'`, "No internet connection", `Update delayed for 30 secs`, reintenta | RESEARCH §20.3; update-testing.md Part 3 T4 |
| 2026-09-15 | codespace | 4 | Coste con el primer proveedor caído | "every depot pays the three attempts (~12 s) before the next provider answers" (T3: tres intentos pausados, 12 s) | update-testing.md Part 3 T3 y "Cost noted"; RESEARCH §20.3 |
| 2026-09-15/16 | codespace | 4 | Cobertura de los cuatro proveedores | catálogo de 42 depots de 7 apps (30+ DLC): "42/42 `CODE_VALID` from all four"; Balatro gid de 3 días antes y de **2.5 años** antes (`1435140510430378530`) concedidos por los cuatro | RESEARCH §20.4 |
| 2026-09-15 (`af93594`, `LUMA_GMRC=1`) | codespace SteamOS | 4/9 | T1 instalación nativa sin manifest local (Balatro 2379780/2379781) | **pass**: `20770407 -> unavailable (HTTP 502)` ×3 con esperas de 5 s, `got code … (via manifestdex)`, `CDN accepted … (HTTP 206)`, `INJECTED`; content_log `manifest request received 200`, 75 chunks, `finished update`, `StateFlags 4`; Steam escribió `2379781_<gid>.manifest` en depotcache | update-testing.md Part 3 T1; RESEARCH §20.4 |
| 2026-09-16 | codespace, Lethal Company 1966720 | 4 | T2 depot shader con clave | **pass**: contenido 1966721 inyectado; shader 1966720 pidió dos manifests (uno por variante), ambos `via 20770407`, `CDN accepted (HTTP 206)`, `INJECTED`; `Shader update changed : Running Update,Downloading,Staging` … `starting commit … 1 updated` … `None`; `shadercache/1966720/` con `fozpipelinesv6` y `transcoded_video.foz`; sin popup. "verify integrity" NO arranca el job shader, un install sí. 2875150 el día anterior: hook deja pasar pero sin petición (no tiene manifest shader) | update-testing.md Part 3 T2; RESEARCH §20.4 |
| 2026-09-15 | codespace | 4/10 | Hechos de proveedor | 20770407.xyz: depot+gid, `Unauthorized` para gid que no puede servir, 10 req/10 s por IP (Cloudflare 429, body `error code: 1015`), `Service temporarily unavailable - please retry` bajo carga, 502 desde ~13:40 UTC el resto del día. manifestdex: `User-Agent: ManifestDeX/1.0`, solo gid, "answers a number for ANY gid (a made-up one included)" | update-testing.md Part 3 "Provider facts" |
| 2026-10-05 | codespace SteamOS | 4/9 | Notas laterales run B/C | Verify en B re-bajó 1 KB (`Validation: read 1 files missing` en 262066 = `app.log` del juego); con 262060 (owned) en `AdditionalApps` nada rompió; claves servidas para depots owned (445702, 1117862) sin crash; release build (sin líneas `Unlocked`) | RESEARCH §21.2 "Side notes" |

#### Función 5

| Fecha | Dónde | F | Qué se midió | Resultado | Fuente original |
|---|---|---|---|---|---|
| 2026-06 | PC Arch nativo (stack canónico: Headcrab + SLSsteam + lumalinux v0.13.5 release vía `install.sh`), build `7c4ac73e` | 5 | Auto-update por unpin (Balatro y Vampire Survivors) | Validado e2e con "a visible delta download". Vampire Survivors: `1794681 6531…→7054…` y `1794685 7577…→7852…` solas tras despinear; content_log `finished update, 2 mounted depots : 1794681 (7054…), 1794685 (7852…)` | method.md §6 "Auto-update by unpinning (validated 2026-06)"; RESEARCH §14 |
| 2026-06 | PC Arch (mismo run) | 5 | Cómo decide Steam que hay update: buildid vs gid por depot | "both validation games carried Steam's *current* `buildid` in the `.acf`… yet unpinning still triggered an update because the per-depot manifest GID differed" | method.md §6 "How an available update is detected"; RESEARCH §14.3 |
| build SLSsteam `20260705132808` (estático) | PC | 5 | Ancla del update-block de SLSsteam (`5c632dd`, 2026-07-05) | `and dword [esi+0x8], 0xFFFFF8E5` (`81 66 08 e5 f8 ff ff`) en file offset `0x1cd7b0`; `0xFFFFF8E5 = ~0x71A`; el inmediato aparece 6 veces más como desplazamiento; con el filtro `81 /4` "exactly one match survives" | RESEARCH §16.2 |
| 2026-07-07 | codespace Arch limpio, SLSsteam `20260705132808`, lumalinux main vía `install.sh` | 5 | `sls_update_unblock` e2e (Balatro) | `SLS-unblock: patched SLSsteam update-clear … -> and reg,0xFFFFFFFF` cada arranque (`insn=0xf57a77b0`). Fase A: zip viejo gid `3742336026811834465`, `.acf` `InstalledDepots 2379781 → 3742336026811834465`, `StateFlags 4`, `BuildDep: PATCH … 3512319404653808464 -> 3742336026811834465`, `LoadDepotKey: SERVED … depot 2379781`. Fase B (`--unpin`): `state changed : Update Required,Fully Installed,Update Queued,Update Running,Update Started` / `Downloading 2 chunks for depot 2379781 (3512319404653808464)` / `finished update, 1 mounted depots`; `reuse 55000644, delta 18300195`. Con `LUMA_NO_SLS_UNBLOCK=1` se queda en "Update required" (observación Mina, issue #20) | RESEARCH §16.4 |
| v0.16.18 / 2026-07-23 | código | 5 | `sls_update_unblock` eliminado | SLSsteam `20260714131044` volvió al vtable hook `GetUpdateInfo` gateado por `DisableUpdates`; LumaDeck escribe `DisableUpdates: no` | RESEARCH §16 cabecera; maintenance.md §D; method.md §6 box |
| 2026-09-10/11 | devcontainer | 5 | Cuándo relee Steam `ManifestIds` | al arrancar, al lanzar el juego, en cada reintento con update pendiente; NO en "Play" ocioso; `steam -ifrunning steam://…` y `~/.steam/steam.pipe` no lo fuerzan; SLSsteam recarga el fichero al instante ("Config reloaded!") | RESEARCH §19.3 |
| 2026-09-12 18:40 → 2026-09-13 08:47 | Deck del usuario, LumaDeck 0.8.0 + lumalinux 0.20.1 | 5 | Primera update de producción por pin (Lonely Mountains: Snow Riders 2545360) | 09-12 18:40 `ensure_pinned` pin `2545361 → 6942462877456514386`; 19:09 pasada de 30 min ve build 25172008, baja `2545361_9107064576136045598.manifest` de P-ToyStore (branch), siembra depotcache (modo 0600), `update check 2545360: pin moved to build 25172008`; 09-13 08:46:50 `Update Required … Update Started`, `Downloading 626 chunks for depot 2545361 (9107064576136045598)`, commit 08:47:07, `finished update, 1 mounted depots (BuildID 25172008)`: **17 segundos**; `.acf` `buildid 25172008`, `StateFlags 4`; `config.yaml` `ManifestIds 2545361: 9107064576136045598`. Sin request code, sin Hubcap | RESEARCH §19.5 |
| 2026-09-13 | codespace, Balatro | 5/4 | Steam necesita el manifest instalado Y el objetivo | old manifest borrado → `BYldRequestDepotManifest … 'Access Denied'`, atascado en "Update"; repuesto → el reintento de 30 s termina | RESEARCH §19.6 |
| 2026-09-14 | codespace SteamOS, Balatro 2379780/2379781, rama sobre 0.8.1 (commit `0f4a14e`, merge `3e4a155`) | 5 | Heal del manifest del build instalado | instalado B=`3512319404653808464`, archivo con B y A=`3742336026811834465`; `--set-pin 2379780 2379781:A`; `rm depotcache/2379781_B.manifest` → "61 s later the file is back", log `restored installed-build 2379781_3512319404653808464.manifest for 2379780 from the archive (Steam needs it to compute the update)`; control: el heal del pin ya existente restauró el mismo fichero dos minutos después. No ejercitado: la pasada negándose a mover el pin | RESEARCH §19.6 |
| 2026-09-16 | codespace SteamOS, lumalinux 0.21.0 (`a632d1b`), LumaDeck `be91d05` | 5 | V1 release de pins | **pass**: la pasada dejó en paz Balatro (frozen por usuario, `fix_id: null`) y 2215260 (fix LuaTools); Auto-update on → `steamidra_lite … --unpin 2379780` inmediato, `2379781` fuera de `ManifestIds`, juego en "Play" | update-testing.md Part 4 V1 |
| 2026-09-16 13:33:11 | codespace (`LUMA_GMRC_URL="https://127.0.0.1:9/…"`) | 5/4 | V3 outage | **pass**: `gmrc.json` → `down` a las 13:33:11; siguiente pasada: `no provider answers — <app> frozen to its installed build …` ×7 juegos, `reason: "providers"` en pins.json; Balatro congelado a `3512319404653808464` con manifest archivado "**finished installing without any code** on Steam's 30 s retry" | update-testing.md Part 4 V3 |
| 2026-09-16 | codespace | 5/4 | V4 recovery | **pass**: `provider probe ok (20770407.xyz, depot 590381)`, `provider up — <app> released to native updates` ×7; `ManifestIds` solo con los dos depots del fix; sin `reason: "providers"` | update-testing.md Part 4 V4 |
| 2026-10-05 | codespace SteamOS | 5 | Add owned corrió con `pin=True` | porque `pins.pin_new_installs()` devuelve `gmrc_state() != "up"` y el codespace fresco no tenía `gmrc.json` (solo se escribe tras el primer lookup, `status.cpp:87`); "Whether that pin freezes an owned game… is **not measured**" | RESEARCH §21.4 |
| 2026-09-21 | codespace (SteamDB) | 5 | Feed RSS de SteamDB sin navegador | "Cloudflare serves the RSS to a plain client, measured 2026-09-21"; cada página responde 403 con body vacío a un cliente plano | dev-backend-reference.md `game_versions.py`, `steamdb_reader.py` |
| 2026-09-21 | codespace (SteamDB) | 5 | Rate limit de Cloudflare por IP | "an afternoon of probes ended in 403s and then challenges on every page"; "403 on `/api/`, then challenges on every page, cleared after ~30 quiet minutes" | dev-backend-reference.md `game_versions.py`, `steamdb_reader.py` |
| sin fecha | codespace (SteamDB con login OpenID) | 5 | Historial de depot con login | "the depot history page lists every manifest, so versions older than the feed's 10 are reachable by date — measured, not built" | dev-backend-reference.md `game_versions.py` |
| sin fecha | CI (fixtures `tests/fixtures/steamdb/`) | 5 | Traductor `versions.py` | "9/10 captured builds match to the second" | dev-backend-reference.md `versions.py` |
| 2026-09-21 | codespace, Balatro | 5 | Qué hace que Steam aplique un pin cambiado | "pin + restart → nothing; pin + verify → nothing; pin + StateFlags 6 + restart → SLSsteam substitutes the gid, GMRC gets the code, Steam downloads the Dec-2024 build" → `mark_update_required` (`StateFlags \|= 2`) | dev-backend-reference.md `pins.py` |

#### Función 6

| Fecha | Dónde | F | Qué se midió | Resultado | Fuente original |
|---|---|---|---|---|---|
| 2026-09-28 | codespace, Joe Danger 229890 | 6 | Spliced tickets (plugin Lua de Ace) con control | "plugin on → launches, plugin off → 65432"; nada cambia en el directorio del juego | FIXES_MAP.md "SteamStub, error 6:0000065432" |
| 2026-08-30 (Ace, segunda mano) | Discord SLSsteam | 6 | Cobertura del plugin según Ace | "19 of 20 games (Insurgency Sandstorm the exception)" | FIXES_MAP.md idem |
| 2026-09-29 | codespace | 6/11 | Race entre flag `Plugins: yes` y fichero del plugin | "a file event processed before the config reload lands sees `Plugins: no` and silently does nothing (seen in the codespace, 2026-09-29)" → espera de 1.5 s | FIXES_MAP.md idem |
| sin fecha | GitHub | 6 | Copia bundled del plugin | sha256 empieza por `62f377e3`; gateado a SLSsteam `>= 20260903114323` (primera release con sistema de plugins) | FIXES_MAP.md idem |
| sin fecha (verificado abriendo los zips) | PC | 6 | Dos fixes reales | Call of Duty 4 (7940): un solo `iw3sp.exe` (sin SteamStub, `cl_cdkey`); Baldur's Gate 3 (1086940): `steam_api64.dll` + `OnlineFix.ini` (`RealAppId=1086940`, `FakeAppId=480`, DLC unlock) | FIXES_MAP.md "Two real fix examples" |
| sin fecha | GitHub | 6 | EOS proxy `yesyes0649/eos-proxy` v1.0.0 | "only a 64-bit build exists; the 32-bit asset is a dead link"; `release.yml` comprueba SHA-256 | FIXES_MAP.md "Online multiplayer" |
| sin fecha | lumalinux `tools/experiment_ownership_ticket.py` | 6 | Hook nativo del ticket de ownership (investigado y aparcado) | "the port is feasible" pero un inline hook se copiaría sin relocar al trampolín de otra copia del plugin → crash; 40 líneas vs ~200 | FIXES_MAP.md "Why a plugin and not a native lumalinux hook" |

#### Función 7

| Fecha | Dónde | F | Qué se midió | Resultado | Fuente original |
|---|---|---|---|---|---|
| sin fecha (on-device) | Deck | 7 | Guard de SLSsteam con juego LumaDeck | trace `appid=1454400 sub=1 added=1` — la inyección profunda hace que `isSubscribed` sea true | RESEARCH §17 "Resolving the apparent paradox" |
| SLSsteam `59f8259` (2026-07-20) / release `20260722152506` / detectado en `20260728212859` | codespace SteamOS (espejo Deck) | 7/2 | Cambio de firma `sendAndRecvGetUserStats` (`j`→`4EMsg`) | `…S3_j` dejó de casar con `…S3_4EMsg`; `Apply()` no-op silencioso en todo Deck actualizado → cheevos OFF y "the **no-restart Add path died with it**"; ventana "six days and two releases wider than first recorded"; símbolos aún `GLOBAL DEFAULT`; LumaDeck #38 | RESEARCH §17.2 nota |
| v0.16.4 armado / v0.16.7 default-on | Deck | 7 | Trace del guard | `appid=1454400 sub=1 added=1 -> skip=0` (borrow corre, cheevos "popped in-game"); `appid=22380 sub=1 added=0 -> skip=1`; `appid=2371090 sub=1 added=0 -> skip=1` | RESEARCH §17.5 |

#### Función 8

| Fecha | Dónde | F | Qué se midió | Resultado | Fuente original |
|---|---|---|---|---|---|
| 2026-09-28 | lectura de código upstream | 8/12 | CloudRedirect 2.6.5 espera 10 s a `steamclient.so` | `init.cpp:542` espera ≤10 s y desiste; moon lo subió a 120 s (`2eee675`) porque en Arch/CachyOS lo vio mapearse pasados los 10 s (medición de moon, no nuestra) | cachyos-port.md "Añadido 2026-09-28" |

#### Función 9

| Fecha | Dónde | F | Qué se midió | Resultado | Fuente original |
|---|---|---|---|---|---|
| 2026-06 | PC Arch (mismo run) | 9 | Instalaciones multi-depot en Linux | Vampire Survivors montó **dos** depots de contenido (1794681 Windows + 1794685 Linux), ambos pineados y ambos auto-actualizados | RESEARCH §14.3 "Multi-depot installs do happen" |
| 2026-10-06 (sin hora) | codespace SteamOS | 9 | Borrado de depot sin manifest en depotcache | "Sin ese manifest Steam no sabe qué archivos eran de ese depot y no borra nada (medido: `0 deleted files`)" | owned-games-guide.md §1.1 "`depotcache/…`" |
| 2026-10-06 (sin hora) | codespace SteamOS | 9 | Archivos fuera de todo manifest en reinstalación | "volvió a bajar 861 MB con los archivos ya en disco" (no reutiliza huérfanos) | owned-games-guide.md §1.1 "Los archivos del juego"; §4 fila 12:20 |
| 2026-10-07 09:59 | codespace SteamOS, Brotato | 9 | Uninstall added sin reiniciar | lumalinux: watcher, `- AppIdVec` ×5, `Reconcile: broadcast`; SLSsteam: "Config reloaded!" ×6 sin callback. Fuera: carpeta, `.acf`, lua, claves, AdditionalApps. Quedan: compatdata (sin marcar), shadercache (ahora se borra), archivo y zip propios, librarycache. "El juego sigue en la biblioteca; tras reiniciar Steam, desaparece" | owned-games-guide.md §3.E; §4 |
| sin fecha | Deck (Proton) | 9 | Depots montados en Balatro | "Only depot 2379781 mounted (64 MB)… Depot 2379782 (81 MB) wasn't downloaded" | RESEARCH §10 |
| 2026-09-10/11 | devcontainer | 9 | Uninstall de Steam | borra los manifests de `depotcache/` del juego y el `.acf`, reescribe `config.vdf`, no toca `keys.txt`/lua/SLSsteam; reinstall misma sesión falla hasta reponer el manifest | RESEARCH §19.3 |
| 2026-09-16 | codespace | 9/4 | V2 add nativo (Into the Breach 590380) con `up` | **pass**: steamidra sin `--pin`, `ManifestIds` sin cambios, `CDN accepted … depot 590380` (también el shader), instaló | update-testing.md Part 4 V2 |
| sin fecha (Test A) | test determinista (árbol falso, dos bibliotecas) | 9 | D1: `write_or_patch_acf` con juego en biblioteca secundaria | `[root] CREATED StateFlags=1 SizeOnDisk=0 InstalledDepots=no` / `[lib2] UNTOUCHED UpdateResult=8 StateFlags=22` ← falla en ambos; controles OK (root: `UpdateResult 8->0, StateFlags 22->6`) | dev-multi-library.md §6 Test A |
| sin fecha (Test B) | test contra `get_installed_lua_scripts()` | 9 | D2: `hasGameFiles` solo en root | `.acf in the secondary library -> hasGameFiles=False -> CARD GREYED OUT`; fix verificado "sensitive" (revertir solo `downloads.py` lo vuelve rojo) | dev-multi-library.md §6 Test B |
| sin fecha | devcontainer SteamOS (gamepadui, noVNC 6080), root + `/tmp/steamlib` ext4 | 9 | Registrar una segunda biblioteca a mano | 4 intentos; content_log `Loaded Steam library folders configuration: .../steamapps/libraryfolders.vdf` (Steam carga `steamapps/`, no `config/`); "`pgrep -x steam` does not detect it in Game Mode"; el supervisor relanza Steam (`echo stop > /tmp/lumadev-session`) | dev-multi-library.md §6 "Field run — D4" |
| sin fecha (Run 1) | devcontainer SteamOS, Brotato 1942280 | 9 | D4 reproducido | add: root `StateFlags 1, SizeOnDisk 0`; install a lib2: lib2 `StateFlags 4, 286 MB`; restart → NOT INSTALLED; press Install → "re-downloads the whole game into the ROOT (273 MB duplicate)" (no "can't move storage") | dev-multi-library.md §3 D4; §6 Run 1 |
| sin fecha (Run 2) | devcontainer SteamOS, Vampire Survivors 1794680 | 9 | D4 + cura | lib2 `StateFlags 4, 1.2 GB`; restart → NOT INSTALLED; `rm` solo el stub root → restart → INSTALLED; el stub casaba el fingerprint (`StateFlags 1`, sin `InstalledDepots`, `SizeOnDisk 0`) y el de Run 1 (sobrescrito por Steam) no | dev-multi-library.md §6 Run 2 |
| sin fecha | devcontainer SteamOS, A Short Hike 1055540 (root), Undertale 391540 (lib2) | 9 | ¿Hace falta el stub `.acf`? (seed sustituido por literal; log `appmanifest_1055540.acf: SKIPPED`) | sin stub: botón Install; sobrevive reinicio sin instalar; una sola `.acf`; instalado tras reiniciar. Con stub: dos manifests, NOT INSTALLED. "One Steam build, one environment" | dev-multi-library.md §6 "is the stub needed?" |
| sin fecha (dos veces) | devcontainer SteamOS | 9 | Q2: ¿Steam reescribe un `.acf` borrado? | "Deleted with Steam running, restarted, not regenerated — observed twice" | dev-multi-library.md §5 Q2 |
| sin fecha | Deck SteamOS del autor (9 manifests) + Windows (4) | 9 | D5: campos presentes en `.acf` reales | `StateFlags` 9/9, 4/4; `ScheduledAutoUpdate` 9/9, 4/4; `UpdateResult` 6/9, 4/4; `BytesToDownload` 6/9, 4/4; `FullValidateAfterNextUpdate` **0/9**, **1/4**. Non-zero `ScheduledAutoUpdate`: `4628710` (Proton 11.0) y `2806050` (Halo: Campaign Evolved, `StateFlags=6`, valor `1788144179`); `FullValidateAfterNextUpdate=1` en `228980`. 12 de 13 carecen del campo → el patch nunca devolvía "clean". Sin segunda biblioteca → sin stubs huérfanos | dev-multi-library.md §3 D5; §6 "Field measurements — D5" |
| sin fecha (lumalinux `6dc36a0`) | CI (`tools/test_acf_error_patch.py`) | 9 | Sensibilidad del test D5 | "stashing only `steamidra_lite.py` fails 6 of its 11 checks, while the 5 covering legitimate residue-clearing pass either way" | dev-multi-library.md §3 D5 |
| sin fecha (lumalinux `8260b12`) | devcontainer SteamOS, Jump King | 9 | P6 (seed eliminado) con el código shipped | log `none (no .acf yet — Steam writes it on Install)`, no escribe nada, tras instalar `StateFlags=4` y tamaño real | dev-multi-library.md §7 "P6" |
| sin fecha | CI | 9/11 | Tests tras P1/P2 | P1: "110 tests pass, no subprocess per refresh"; P2: "112 tests pass"; P5 `c96e2db`; P4 `2c2cc7c`; P3 `6dc36a0` | dev-multi-library.md §7 |
| sin fecha (Discord, segunda mano) | PC de un tercero, app 2111550 | 9 | `UpdateResult=8` = fallo de descifrado | "content still encrypted" al lanzar, arreglado editando `StateFlags 36 → 4` y `UpdateResult 8 → 0` — "a strong lead, not proof" | dev-multi-library.md §4b |
| sin fecha | rig del autor | 9 | `appworkshop_*.acf` | "No `steamapps/workshop/appworkshop_*.acf` exists on the author's rig" → la medida de SFF (NeedsDownload) no aplica | dev-multi-library.md §4b |

#### Función 10

| Fecha | Dónde | F | Qué se midió | Resultado | Fuente original |
|---|---|---|---|---|---|
| 2026-10-07 | LumaDeck (Ryuu) | 10 | Caducidad real de la sesión Ryuu | "Ryuu can drop a session before its date (measured 2026-10-07)" → el live check horario (GET a la home) marca expirada; 401/403 en descarga la marca muerta al momento | credentials.md "Expiry warnings" |
| hasta v0.7.4 (issue #42) | campo | 10 | Renovación del token LuaTools | "broken from the day the feature shipped until **v0.7.4**: the call it made didn't exist, the error was swallowed, and the dead token was sent anyway — so everyone got one hour of LuaTools and then a permanent `session_expired`" | credentials.md "It renews itself"; troubleshooting.md |
| release que añadió LuaTools al restore | campo | 10 | Logout no borraba el mirror | "the next plugin load would restore the very session you just discarded, which is what happened in the release that first added LuaTools to the restore list" | credentials.md "Where the values live" |

#### Función 11

| Fecha | Dónde | F | Qué se midió | Resultado | Fuente original |
|---|---|---|---|---|---|
| 2026-09-08 | CI | 11 | `verify-fix.yml` ("Runtime smoke test") | "**it has never produced a green run** — its first real executions (2026-09-08) all failed in the harness, before any assertion"; antes "en verde" porque el run citado era de otro workflow | maintenance.md "Look at the log first"; update-testing.md Part 2; RESEARCH §13.5.b |
| 2026-09-08 | CI (tests sintéticos) | 11 | Mutación de los tests del idiom | "alterando los predicados uno a uno, los tests fallan (8/2/1 fallos según la mutación)"; los cuatro escaneos coinciden contra un ELF sintético | RESEARCH §13.5.b "Cómo se verificó el propio arnés" |
| hasta 2026-09-08 | CI | 11 | Ejecución de `tools/test_*.py` | "until 2026-09-08 *nothing* invoked `tools/test_*.py`"; ahora `build.yml` los corre todos | maintenance.md §E último; update-testing.md Layer 2 |
| 2026-07-23 / 2026-08-11 / 2026-08-17 | CI | 11 | Cronología del feed | `watch-steam.yml` 2026-07-23; feed RVA 2026-08-11 (19 días después); 2026-08-17 ambas rutas de re-derivación publican al feed y se quitan `LUMA_RVAS_DIR`/`LUMA_RVAS_URL` ("grepped": nada más los referenciaba) | rva-feed-design.md §15–16 |
| 2026-08-20 (revisión) | GitHub | 11/12 | Asset de release de SLSsteam | AceSLS pasó de `SLSsteam-Any.7z` a `-release.7z`/`-debug.7z` (`6a06652`, release `20260819131545`): "Dejó la instalación en 404 durante ~44 h". CloudRedirect: releases v2.1.9, v2.2.4, v2.5.3, v2.6.1, v2.6.2 solo traen `.exe`; `cloud_redirect_cli` solo en el flatpak | decouple-headcrab-plan.md "Fuentes de descarga confirmadas" |
| 2026-08 (live) | devcontainer CachyOS-Desktop / Arch fresco | 11/12 | Caveat upstream Headcrab | `LinuxClientManifest` build 1784669098 (2026-07-21) "lags its own `HeadcrabCompatibleClientVer` (1785187029, 2026-07-27) by ~6 days"; SLSsteam aborta "Unknown steamclient.so hash", LumaDeck "Steam build not supported"; Handheld usa el manifest Deck (alineado) | porting-cachyos.md §0 "Upstream caveat" |
| sin fecha | CI (`tests/test_installer_crashloop.py`) | 11 | Mecanismo `do_repair()` del tracker reproducido en test | "DONE" (approach B) | cachyos-vm-testing.md tabla; porting-cachyos.md §0 Phase 2 |
| sin fecha | CI | 11 | Tests de caracterización SteamOS del env layer | "65 tests, including golden-value characterization that SteamOS resolves bit-for-bit to `deck` / `/home/deck`"; live run con uid≠1000 | porting-cachyos.md §0, §10 |
| sin fecha | código | 11 | Regresión `desktop` para CachyOS | "an earlier revision put CachyOS in the ChimeraOS family and sent it `desktop` — a regression (CachyOS rejects `desktop`); now fixed" (verificado en fuente `CachyOS/gamescope-session@cachyos`) | porting-cachyos.md §0, §2 corrección |

#### Función 12

| Fecha | Dónde | F | Qué se midió | Resultado | Fuente original |
|---|---|---|---|---|---|
| 2026-06-24 (ecosistema) | SLSsteam upstream | 12 | Valve reordenó los virtuals `IClient*::RunIPCFrame`; SLSsteam tuvo que bumpear; 2026-07-24 SLSsteam borró esos hooks (`8de3384`) | (contexto para la caveat RTTI) | RESEARCH §15.4 |
| build 2026-09-13 | PC (`strings`/`pefile`) | 12 | `ManifestDeXCore.dll` | PDB `C:\Users\berke\source\repos\OpenSteamTool`, mismos proxies `dwmapi.dll`/`xinput1_4.dll`, mismo hook (eMsg 151/147); no reporta códigos ni identidad | RESEARCH §20.2 |
| 2026-08 (CONFIRMED) | Codespace del mantenedor | 12 | KVM en el Codespace | `ls -l /dev/kvm` → "No such file or directory"; el devcontainer tampoco (`ls /dev/kvm` not found, sin `vmx`/`svm`) → cualquier VM es TCG | cachyos-vm-testing.md §1 |

#### Mediciones de 2026-10-07/08 (codespace, Deck y PC) que no estaban en ningún doc

| Fecha | Dónde | F | Qué se midió | Resultado | Fuente |
|---|---|---|---|---|---|
| 2026-10-07 18:17 | codespace SteamOS | 10 | Verificación de login de Ryuu desde el Python de Decky | `CERTIFICATE_VERIFY_FAILED` (sin contexto SSL); Decky encuentra los CA por `certifi` ("SSL: loaded certs from certifi"); arreglado pasando el contexto de `http_client` (LumaDeck `8200d4e`) | LumaDeck `8200d4e` |
| 2026-10-07 18:25 | codespace SteamOS | 10 | Login de Ryuu con la cookie anónima rechazada | log "anonymous session; waiting" ×2 y luego "logged-in session cookie captured"; la home logueada lleva `data-user-id="1698…"`; `GET /download?appid=1942280` → `200 application/zip` 42082 B con `1942280.lua` y manifests | LumaDeck `0187590` |
| 2026-10-07 19:13 | codespace SteamOS, sin clave de Hubcap | 4/10 | Descarga por `api.json` | `Morrenus status=401` → `Forced Ryu (Cookie) status=200`; añadir habilitado solo con Ryuu | log de LumaDeck (Decky) |
| 2026-10-07 19:47 | codespace SteamOS, Balatro 2379780 (zip de Ryuu) | 9 | Orden del lua de Ryuu | `addappid(2379780)` al final; steamidra tomó 2379781 como juego (`AdditionalApps` 2379781, claves con padre 2379781, `stplug-in/2379781.lua`); post-check abortó. Tras `_app_line_first` (LumaDeck `fcef4f7`), 19:56: `appid principal 2379780`, `3 keys nuevas`, `post-check OK — 2 depot key(s)`, `keys.txt` `2379780;` + dos extendidas con padre 2379780 | LumaDeck `fcef4f7` |
| 2026-10-07 ~19:38 | codespace SteamOS | 9 | Búsqueda por nombre con la tienda de Steam, sin clave | `storesearch?term=brotato` → 4 items (`type: app` incluidos los dos DLC y la banda sonora); la UI muestra 3 (banda sonora filtrada) | LumaDeck `5bb8fdb` |
| 2026-10-07 | codespace SteamOS | 10 | Borrado de la clave de Hubcap desde Settings | `credentials.json` sin `hubcap_key`; Morrenus `enabled: False` y sin `api_key=`; Settings "No Hubcap key saved" (las cadenas `credHubcap*` estaban borradas desde `cf0dedb` y salían en crudo; restauradas) | LumaDeck `1b5e678` |
| 2026-10-07 | Deck | 11 | Comprobaciones de update de componentes | `.so` de CloudRedirect `2.6.5+870afdb-dirty`; caché `Selectively11__CloudRedirect__cloud_redirect.so.json` = `v2.6.6`; `cr_debug.log` acumulativo desde agosto (primera línea `2.6.3`), por lo que Settings leía 2.6.3; `.slssteam.version` `20261001163836`; lumalinux `v0.22.1` | LumaDeck `c4776ce`, `be36d92` |
| 2026-10-07 22:16 | Deck | 1/8/11 | Update de CloudRedirect desde LumaDeck (`setup.sh`) | `.so` y última línea de `cr_debug.log` `2.6.6+3434d9d`, `DoInit: SUCCESS`, `cloud_redirect.so` mapeado 6 veces en `steam`; lumalinux v0.22.1 "CR-stats: cloud_redirect.so not loaded (or it no longer defines HandleGetUserStats)" (texto corregido en v0.22.2) | lumalinux `e92b763` |
| 2026-10-07 22:52-23:06 | Deck + PC (Windows) | 7 | Sync de logros con CloudRedirect 2.6.6 | Deck: `config.json` `stats_sync_enabled: True`; `ExportNativeStats app=2379780: wrote 79 bytes (1 stats, 1 ach blocks)`, `Cloud blob refreshed: 7 app(s)`; al relanzar Balatro `GetUserStats app=2379780 … handled locally (5 bytes)` y el logro visible. PC: no aparecía hasta reiniciar Steam; tras reiniciar y abrir Balatro, el logro está (`SyncFromCloud`/changelists en `<Steam>\cloud_redirect.log`). Dos logs de CloudRedirect: `cr_debug.log` y `cloud_redirect.log` | `cloudredirect.md` (2.6.6) |
| 2026-10-08 06:51-06:52 | CI | 11/12 | Releases lumalinux v0.22.2 y LumaDeck v0.11.0 | `build.yml` run 534 success con `liblumalinux.so` (9,5 MB) y `version.txt`; `release.yml` run 143 estampa `0.11.0` del tag (tags creados desde la web de GitHub) | `build.yml` run 534; `release.yml` run 143 |
| 2026-10-08 | build local (`cmake --build`) | 1 | `liblumalinux.so` recién compilado (`cr_stats_fix` v0.22.2) | compila, self-test `cr_stats_fix_selftest/run.sh` 6/6; las únicas cadenas de versión en el binario son `lumalinux v0.22.1 preinit` y `lumalinux/v0.22.1` | `tools/cr_stats_fix_selftest/run.sh` |
