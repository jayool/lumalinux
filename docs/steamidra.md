# SteaMidra (drappula/SFF) — la app, leída desde el código

Leído desde el código el 2026-10-08. Sustituye a
`steamidra-linux-analysis.md` y absorbe de `lumacore-findings.md` lo que habla
de la **app** SteaMidra (lanzador Python, releases, feeds que consume); lo que
ese doc dice del DLL LumaCore queda para `lumacore.md`. Sus mediciones con
fecha están en §5.2, sus vectores de brickeo y decisiones en §4.4 con el
estado de hoy, y su historia por releases en §5.3. `LumaCore/` (el DLL de Windows, hoy repo
aparte `drappula/LumaCore`) **no** se lee aquí: solo cómo SteaMidra lo
descarga, instala y alimenta. Cómo **nosotros** hacemos lo mismo está en
`nosotros.md`.

---

## §0 Ficha

| | |
|---|---|
| **Qué es** | Una **aplicación de escritorio en Python** (CLI `Main.py` y GUI PyQt6 + QtWebEngine `Main_gui.py`, ~70.000 líneas en `sff/`) que **no se engancha a Steam**: prepara ficheros y delega el unlock en otro programa según el SO. En Windows instala y alimenta **LumaCore** (cuatro DLL copiadas a la carpeta de Steam, catálogos de patrones por SHA-256). En Linux instala **SLSsteam de AceSLS** ejecutando el script externo **headcrab** (`Deadboy666/h3adcr-b`, rama `main`, descargado y ejecutado con `bash` tras recortarle CloudRedirect por texto) y después reescribe `steam.sh`, `/usr/bin/steam-jupiter` (botón, `sudo`), `steam.cfg` y `config.yaml`. Alrededor: descarga de `.lua` de cuatro proveedores (Hubcap, Ryuu, DepotBox con clave; "Free Providers" sin clave), base local de claves de depot alimentada por los usuarios, manifests de `manifest.luastools.xyz` → GitHub → ManifestHub2 → Hubcap, **descargador nativo de depots** (sesión Steam anónima, CDN, AES, VZ/VSZTD/ZIP, SHA-1) con DepotDownloaderMod (.NET 9) de respaldo, ACF escrito a mano y dejado en 0444, kit de DRM (Steamless, gbe_fork/Goldberg, ColdClient/ColdLoader, SteamAutoCrack, "Fixes & Bypasses" de CrakFiles por pixeldrain, online-fix.me solo búsqueda), DLC unlockers (SmokeAPI/CreamAPI/Uplay), backup de saves (local, rclone, Google Drive) y, en Linux, SLScheevo para logros. |
| **Repos y commits leídos** | `drappula/SFF` `origin/main@4159300` (2026-10-03, tag `v6.9.0`, `VERSION = "6.9.0"`). 166 commits; raíz `32bd204` (2026-05-25, "Initial clean commit - SteaMidra v6.2.4"), sin git anterior a 6.2.4. Remoto `midrags` = original `Midrags/SFF` (`fa44fc9`, 2026-08-21) = merge-base exacto: 78 commits de upstream, **88 del fork** (mallusrgreat 73, Qiyrax 8, drappula 4, St0Qx 1, michelegoku3 1, wtfseanscool 1); upstream no se ha movido desde el 21-08. 50 tags `v6.2.4`…`v6.9.0`, tres ligeros, `v6.6.7c` apunta a un commit huérfano. `LumaCore/` estuvo in-tree hasta `e4c6b65`/`da3108e` (2026-10-03) y hoy es `drappula/LumaCore`. `sff/` ≈ 70.000 líneas (`gui/` 17.000, `game/` 6.800, `manifest/` 3.900, `lua/` 3.500, `linux/` 3.100, `downloads/` 2.700, `cloud/` 2.300, `network/` 1.650, `dlc_unlockers/` 1.600); `sff/webui/` 2.382 líneas de HTML + 9.000 de JS; `third_party/` 1.806 ficheros (~540 MB de binarios). **Sin tests** (`tests/` está gitignorado). |
| **Plataforma** | Windows 10/11 x64 (instalador NSIS a `%LOCALAPPDATA%` sin UAC, zip portable) y Linux x86_64 (AppImage construido en `ubuntu-24.04`, glibc 2.39, 18 `.so` del sistema copiados; zip con `steamidra_install.sh`). Linux: Steam nativo (`~/.steam/steam`, `~/.local/share/Steam`), Flatpak (detectado por directorio, con prioridad sobre el nativo si existe), snap solo como ruta candidata; SteamOS/Deck (`steam-jupiter`, `sudo`, `steamos-readonly disable`). Requiere .NET 9 en `~/.dotnet` (lo instala solo). Sin macOS. |
| **Licencia y quién** | GPL-3.0 (`LICENSE` "SteaMidra Copyright (C) 2025-2026 Midrag and contributors"; cabeceras "Copyright (c) 2025-2026 Midrag"); el fork no cambia titular. Steamless redistribuido bajo CC BY-NC-ND 4.0; coldloader, CreamAPI/SmokeAPI/Uplay (DLL en `sff/dlc_unlockers/resources/`), SLScheevo, tsf/tml/miniaudio **sin licencia** en `third_party_licenses/`. Upstream: Midrag, 44 releases entre 05-25 y 08-21 y parado. Fork: `drappula`, desarrollo casi íntegro de mallusrgreat (09-04 → 10-03), 7 releases (`v6.6.7`…`v6.6.7d`, `v6.8.0`, `v6.9.0`); `AGENTS.md` declara que se desarrolla con un agente de IA ("opencode") y exige tocar el CHANGELOG en cada commit. Issues cerradas en GitHub; soporte por Discord (`discord.gg/steamidra`). El README y `installer.nsi` siguen enlazando a `github.com/Midrags/SFF` (parado en 6.6.6); el updater interno apunta a `drappula/SFF` desde `d44106a` (09-12). |
| **Relación con los demás programas** | **SLSsteam** (lo instala por headcrab en Linux y escribe su `config.yaml`); **headcrab** (`Deadboy666/h3adcr-b`: instalador, parche de `steam.sh`, downgrade del cliente a `1788652215`); **LumaCore** (`drappula/LumaCore`, V37+; patrones de `michelegoku3/MigoReleases`); **LuaTools** (consume su archivo `manifest.luastools.xyz` como primera fuente de manifests, sin donar); **Hubcap/Ryuu/DepotBox/ManifestHub2** (proveedores con clave); **KoriaPolis** (`Steam-Depot` base de claves, `CrakFiles` cracks); **SLScheevo** (script Python empaquetado); DepotDownloaderMod, gbe_fork, Steamless, SteamAutoCrack, rclone, fzf (blobs en `third_party/`). Nosotros: su `.lua` es lo que `tools/steamidra_lite.py` parsea (`nosotros.md` §2.9); su `steamidra_lite` nació como "equivalente Linux" de su GUI. Compartimos `~/.config/SLSsteam/config.yaml` con lo que ella escribe si una Deck pasa de SteaMidra a nosotros (§4.1). |

**Lo que cambia respecto al doc anterior**, en una línea cada uno:

- **headcrab corre solo, en cada arranque y cada 60 minutos**, no solo desde el botón "Linux Setup": `check_and_notify_update` (`sff/linux/slssteam.py:655-716`) llama a `setup_via_headcrab` cuando `installed != latest`, y `installed` es siempre `"unknown"` porque **nadie escribe `~/.local/share/SteaMidra/SLSsteam/VERSION`** desde `92d6813` (09-13). Solo lo frena que `pidof steam` encuentre a Steam. Resuelve la inconsistencia I1 del doc anterior a favor de §13.2.
- **headcrab no parchea `steam.sh`: lo sustituye entero** (`rm steam.sh; wget -O steam.sh …/h3adcr-b-modul3s/headcrab_native.sh`, `headcrab.sh:876-926`), guarda el de Valve como `client.sh` y deja el suyo en 555. Lo que SFF edita y deja en 644 (`patch_steam_sh`, `slssteam.py:111-133`) es el script de headcrab, no el de Valve.
- **headcrab rebaja el cliente de Steam también al instalar**, no solo en el "hash fix": `CheckHeadcrabCompatibility` compara la versión del `package/*.manifest` con `HeadcrabCompatibleClientVer=1788652215` (`headcrab.sh:5,215`) y si difiere ejecuta `clientdowngrade` (manifest de `Deadboy666/SteamTracking@headcrab`, servidor local `dgsc --port 1666`). Resuelve I8 y S26.
- **`SafeMode: no` ya no lo fuerza SFF tras headcrab**: `patch_slssteam_config` sale si existe `.headcrabd` (`slssteam.py:257-258`), y headcrab lo escribe siempre (`headcrab.sh:841,846,849`); en SteamOS headcrab deja `SafeMode: yes`, en el resto `no`. SFF escribe `no` solo en una config sin marcador (instalación previa a mano/pacman) o cuando crea `config.yaml` desde su plantilla (`yaml_config.py:336-377`). El "hash fix" tampoco lo reescribe (misma función, mismo marcador). Resuelve I6.
- **`InstalledDepots` vacío fue del 09-09 al 09-20** (`46a421d` → `8eaf238`), incluido en v6.8.0; hoy `create_acf` escribe `manifest`, `size` y `dlcappid` por depot (`acf_writer.py:68-82`). Resuelve I4.
- **Los patrones de LumaCore vienen de `michelegoku3/MigoReleases` desde `ffd70db` (09-08)**: `lumacore_setup.py:163` ya lo decía en `8eaf238`.
- **`ManifestIds` de SLSsteam se siguen escribiendo** cuando el usuario elige versión (`ui.py:1041-1046`), y se borran al quitar el juego "full" (`misc_bridge.py:2760-2764`); "`add_manifest_id` no se usa" (M18) es de 6.6.7c y caducó.
- **`PlayNotOwnedGames` no se escribe** (solo un comentario, `download_bridge.py:2319`): entró en 6.6.0 y salió en 6.6.1; SLSsteam eliminó la clave (`slssteam.md` §5.1). El CHANGELOG lo sigue diciendo.
- **Manifests**: copia local → **`manifest.luastools.xyz/m/<depot>/<gid>`** → (con clave Hubcap) `hubcapmanifest.com/api/v1/generate/manifest` → tres espejos planos de GitHub → `api.manifesthub2.filegear-sg.me` (clave de 24 h). Ningún CDN en `ManifestDownloader`; el único código de petición es el de la **sesión anónima** del descargador nativo (`native_downloader.py:302-319`). Consumidor puro del archivo de LuaTools: cero donación.
- **La base de claves no viaja en el build**: `fallback_depotkeys.json` (67 MB) salió del repo en `ed8a2a8` (09-07), los specs la incluyen "si existe" y el CI no la descarga; en runtime se baja de `KoriaPolis/Steam-Depot` (o R2) cada 6 h y se alimenta con cada lua.
- **La contribución de claves está ON en la práctica**: `SettingItem` por defecto `False` (`structs.py:399`), pero la migración de `settings.bin` a `1.1.0` pone `provider_contribute_keys=True` si la clave no existe (`settings.py:223-227`), y corre en cada `load_all_settings` (`:83`): una instalación nueva nace con `_version` ausente → `True`. El doc anterior ("opt-in, default False") y el CHANGELOG 6.6.3 ("ON por defecto") tenían razón a medias cada uno.
- **Steam muere con SIGKILL en cada instalación o borrado de un `.lua` en Linux** (`steam_tools_compat.py:39-65`, desde `a29132f` 09-07) y se relanza con `LD_AUDIT` desde SteaMidra.
- **El camino CLI "Download a game (Linux)" reescribe `config.yaml` entero con `yaml.dump`** (`linux_download.py:160-196`) y mete en `DlcData` ids de **depots** con valor `{}`; los demás caminos usan el editor por regex (`yaml_config.py`).
- **Cadena gratis hoy** (2026-10-08): `fylsdy/ManifestHub/depotkeys.json` sigue en 404; `KoriaPolis/Steam-Depot/fallback_depotkeys.json` responde.
- Código muerto o roto que el doc anterior daba por vivo: `update_check.py` (sin llamadores y con `ValueError`), `manifestor.py` (openlua.cloud), `GseToolUpdater`, `run_gen_emu`, `midra://` (se registra y no se procesa), la capa Ludusavi de cloud saves (`sff/cloud/data/manifest.yaml` no existe; el fichero está en `sff/data/`), música MIDI en Linux (no hay `.so`), el tercer fallback de `start_steam` busca `SLSteam.so` (sin la segunda s).

---

## §1 Mapa del código

Cobertura por fichero con la función de la matriz a la que sirve (números de
§2). "Sin llamadores" = ningún uso en el código Python fuera de
`LumaCore/`.

### 1.1 Capa Linux, inyectores y núcleo

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `sff/linux/slssteam.py` | 716 | instalación/actualización de SLSsteam vía **headcrab** (`setup_via_headcrab`, `_cr_filter`, `_cleanup_headcrab_zombies` = `pkill -f dlm\|dgsc\|wget`), `patch_steam_sh` (chmod 644, borra toda línea con `LD_AUDIT`, inserta `export LD_AUDIT=…` en la línea 11), `patch_steam_jupiter` (`sudo cp`, backup `.bak`, línea antes del último `exec`), `create_steam_cfg` (`BootStrapperInhibitAll=enable` + `BootStrapperForceSelfUpdate=disable`, nunca pisa), `patch_slssteam_config` (one-shot por `.headcrabd`), `detect_steam_type` (Flatpak si existe el directorio), `is_steamos`, `bashrc_has_broken_prompt_guard`, `fix_hash_mismatch` (`curl … headcrab.pages.dev/reset \| bash` → Steam 30 s → `curl … headcrab.pages.dev \| bash` → re-parches), `migrate_existing_games` (todo `stplug-in/*.lua` y todo ACF a `AdditionalApps`, one-shot `.sm_migrated`), `get_installed_version` (`VERSION` nunca escrito → `"unknown"`), `check_update_available` (GitHub, sin caché), `check_and_notify_update` | 1, 2, 5, 8, 9, 11 |
| `sff/linux/yaml_config.py` | 907 | editor por **regex** de `config.yaml` de SLSsteam: `AdditionalApps`, `AppTokens`, `ManifestIds`, `DlcData`, `FakeAppIds` (sin llamadores), booleanos (`update_yaml_boolean_value`), plantilla de config nueva (`_init_config_with_app`), campos "requeridos" (`_patch_missing_slssteam_fields`), escritura atómica + `.bak` en cada cambio; dos resolutores de ruta (`get_user_config_path` respeta `XDG_CONFIG_HOME`; `slssteam.get_slssteam_config_dir` no) | 2, 3, 4, 5, 9 |
| `sff/linux/acf_writer.py` | 129 | `create_acf`: `StateFlags 4`, `buildid`/`TargetBuildID` del branch `public`, `AutoUpdateBehavior 0`, `InstalledDepots{manifest,size,dlcappid}`, `LastOwner` = `STEAM32_ID` o `0`, fichero a **0444** | 5, 9 |
| `sff/linux/linux_download.py` | 429 | menú CLI "Download a game (Linux)": lua → `_build_game_data` → biblioteca → `run_download` → `create_acf` → manifests a `<lib>/steamapps/depotcache` → `libraryfolders.vdf` → `_add_to_slssteam` (`yaml.dump`) → `chmod +x` → Steamless sobre todos los exe → SLScheevo; `handle_linux_setup`, `handle_linux_achievements` | 2, 3, 4, 5, 6, 7, 9 |
| `sff/linux/steam_process.py` | 274 | `kill_steam` (psutil, SIGKILL; cachea rutas de `.so` leídas en `/proc/<pid>/maps`), `start_steam` (`Popen(["steam"])` con `LD_AUDIT=library-inject.so:SLSsteam.so` y entorno limpio de `_SYSTEM_TOOL_ENV_DROP`; tercer fallback busca `SLSteam.so`, `:177`) | 1, 9, 11 |
| `sff/linux/desktop_shortcuts.py` | 196 | `.desktop` por juego con `Exec=env LD_AUDIT=… steam steam://rungameid/<id>` (Flatpak sin `LD_AUDIT`); icono de CDN de Steam o SteamGridDB | 1, 9 |
| `sff/linux/flat_file_repair.py`, `permissions.py`, `steamless.py`, `slscheevo.py`, `depot_downloader.py`, `dotnet.py` | 134, 61, 127, 96, 35, 21 | reparación diaria de descargas 6.6.5 con `\` en el nombre; `chmod +x` a `.sh/.x86/.x86_64/.bin` y ELF; `Steamless.CLI.dll` (.NET 9) `-f <exe> --quiet --realign` sobre **todos** los `.exe` candidatos; `SLScheevo.py --max-tries 101`; re-exports de `sff.downloads` | 6, 7, 9, 11 |
| `sff/lumacore/lumacore_setup.py` | 895 | **Windows**: `install_lumacore` (taskkill `steam.exe`, limpia GreenLuma una vez, baja `release.zip\|rar` del último release de `drappula/LumaCore`, extrae `dwmapi.dll`/`xinput1_4.dll`/`LumaCore.dll`/`LumaCorePayload.dll` a `<steam>`), prewarm de `<steam>/lumacore/pattern/<sha256>.toml` desde `michelegoku3/MigoReleases@pattern` (carpetas `steamclient/`, `steamui/`, `steamclientipc/`), versión instalada (setting o `status.json`), check con caché 6 h, `deactivate_lumacore` | 1, 11 |
| `sff/lumacore/auto_setup.py`, `status_banner.py` | 86, 125 | `LumaCoreUpdateChecker` (importa `pywin32_system`, inexistente; **sin llamadores**); `StatusBannerPoller` lee `status.json` cada 2 s | 1, 11 |
| `sff/core/structs.py` | 595 | enums de menú, `Settings` (claves de API, `SLS_CONFIG_LOCATION`, `STEAM32_ID`, `MANIFEST_PRESERVE`, `USE_MANIFEST_PINS`, `PROVIDER_CONTRIBUTE_KEYS` default `False`…), `LuaParsedInfo`, `DLCTypes` | 3, 5, 9, 10, 11 |
| `sff/core/storage/settings.py` | 234 | `settings.bin` (msgpack) junto al ejecutable, secretos cifrados (PyNaCl), **migración 1.1.0 que pone `provider_contribute_keys=True`** y `provider_enrich_steam_metadata=True` si faltan (`:223-227`), ejecutada en cada carga (`:83`) | 10, 11, 12 |
| `sff/core/secret_store.py`, `cache.py`, `integrity.py`, `strings.py`, `utils.py`, `processes.py` | 176, 156, 141, 33, 146, 209 | clave maestra en keyring **y siempre** en `<sff_data>/.sff_encryption_key` 0600; `api_cache.json` 1 h; magic `27 44 56 01` + sha256; `VERSION = "6.9.0"`, `STEAM_WEB_API_KEY` embebida en base64 (`:24-25`, "Public key shared by the free manifest providers"), repos de update `drappula/SFF`; `sff_data_dir` = carpeta del ejecutable/`$APPIMAGE`, `manifests_staging_dir` = `<sff_data>/manifests`; `SteamProcess` (Windows, `taskkill`) | 4, 10, 11, 12 |
| `sff/core/storage/vdf.py`, `acf.py`, `yaml.py`, `named_ids.py` | 173, 139, 48, 93 | `vdf_dump` atómico; `get_steam_libs` (solo `<steam>/config/libraryfolders.vdf`, rutas existentes), `ensure_library_has_app` (`apps/<id>="1"`, crea la biblioteca si falta); `ACFParser` (`needs_update` = bit 2, `MountedDepots`); `names.json` en `saved_lua/` | 5, 9 |
| `sff/manifest_watcher/preserver.py` | 402 | vigila `<steam>/depotcache` y `<steam>/config/depotcache` (inotify `DELETE\|MOVED_FROM`; polling 5 s porque `inotify_simple` no está en `requirements-linux.txt`) y **restaura** desde el staging lo que Steam borre; setting `MANIFEST_PRESERVE` (on) | 4, 5, 9 |
| `sff/app_injector/sls.py` | 345 | `SLSManager`: `get_local_ids` (`AdditionalApps`), `add_ids` (app + todos los `dlcappid` del appinfo + `AppTokens` + `DlcData` si `listofdlc` ≥ 64), `dlc_check` (tabla; auto-añade DLC con clave en `config.vdf` y, previa confirmación, los sin depot), menú "Manage SLSSteam IDs" | 2, 3, 4, 9 |
| `sff/app_injector/lumacore.py`, `base.py` | 134, 36 | `LumaCoreManager` (Windows): stub `addappid(<id>)` en `stplug-in/<id>.lua`; `dlc_check` solo informa | 2, 3 |
| `sff/steam_path.py`, `steam_tools_compat.py`, `registry_access.py` | 139, 172, 124 | rutas de Steam (setting → registro / `~/.steam/steam`, `~/.local/share/Steam`, Flatpak, snap → prompt); `install_lua_to_steam`/`remove_lua_from_steam` (**matan y relanzan Steam** en Linux), `sync_manifest_to_config_depotcache`, `remove_acf_and_manifests`; Windows `winreg`, `set_stats_and_achievements` = stub `False` | 1, 4, 5, 7, 9 |
| `sff/backup.py`, `disk_utils.py`, `single_instance.py`, `library_scanner.py`, `__init__.py` | 172, 158, 93, 270, 31 | backups con retención 5; unidades A-Z (Windows); `QLocalServer "SteaMidra-v1"`; escaneo de ACF (`_get_injection_ids` devuelve `set()`) | 9, 11 |
| `Main.py` | 636 | CLI: `os.chdir(sff_data_dir)`, `SteamClient()`, `check_and_notify_update()` en cada arranque Linux (`:570-575`), menú Linux sin `MANAGE_LUA`, `UPDATE_ALL_MANIFESTS`, `DLC_CHECK`, `INSTALL_MENU`, `CHECK_UPDATES`, `CRACK_GAME`, `REMOVE_DRM`, `STEAM_AUTO`; args `-f/-b/--auto-update/--export-ids/--dry-run/-q`; `prctl(15, "SteaMidra")` | 1, 6, 9, 11 |
| `steamidra_install.sh` | 331 | instala SteaMidra (AppImage/binario/fuente) en `~/.local/share/SteaMidra`, lanzador `~/.local/bin/steamidra` (`QTWEBENGINE_DISABLE_SANDBOX=1`), `.desktop`, icono, **.NET 9** por `dot.net/v1/dotnet-install.sh`, `DOTNET_ROOT` en `.bashrc/.zshrc`; `uninstall` | 11, 12 |

### 1.2 Adquisición: lua, claves, manifests, descarga

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `sff/manifest/downloader.py` | 725 | `ManifestDownloader`: GID por depot (estrategias + pins del lua si `USE_MANIFEST_PINS`), búsqueda local (staging, `depotcache`, `config/depotcache`), **cascada** `download_single_manifest` (`:482-533`: local → LuasTools → [Hubcap generate →] GitHub → ManifestHub2), `_write_manifest_to_depotcache` con validación de magic (solo ruta secuencial), `_cleanup_live_stale_manifests` (borra otros GIDs del mismo depot **antes** de bajar el nuevo), paralelo, workshop; `get_cdn_client` declarado muerto en comentario (`:317-320`) | 4, 5, 9 |
| `sff/manifest/id_resolver.py`, `manifesthub_key.py`, `crypto.py` | 177, 98, 228 | estrategias (appinfo público → `depotfromapp` → appinfo de DLC → manifest local más reciente → manual); clave ManifestHub 24 h (abre el navegador); formato `.manifest`, AES de nombres, `decrypt_and_save_manifest` | 4, 10 |
| `sff/manifest/depot_history.py` | 2101 | historial de versiones: Steam CM → `steamcmd.morrenus.net` → árbol GitHub de `qwe213312/k25FCdfEOoEJ42S6` → `fallback_tokens.json` → SteamDB solo con cookie `cf_clearance`; scraper de 3 capas (curl_cffi/cookie/zendriver-SeleniumBase + Chrome for Testing ~300 MB) **desactivado** (`:1835-1839`); caché `~/.sff/` | 5, 4 |
| `sff/manifest/html_manifest_import.py`, `update_check.py`, `collections.py`, `manifestor.py` | 201, 146, 74, 197 | HTML guardado de SteamDB → grupos; tracker de updates **sin llamadores y roto** (`ValueError`, compara dict con GID); `GetCollectionDetails` con clave Web API; automatización de `openlua.cloud` **sin llamadores** | 5, 4, 9 |
| `sff/downloads/native_downloader.py` | 912 | descargador nativo: `anonymous_login`, servidores por latencia (top-4), token CDN, **request code por `cdn.get_manifest_request_code`** (`:302-319`; "No external request-code mirrors anymore"), manifest `/depot/{d}/manifest/{gid}/5/{code}{token}`, chunks AES-256 + VZ1/VSZTD/ZIP + SHA-1, 8-64 hilos, limitador, filtro de SO por heurística de ruta | 4 |
| `sff/downloads/depot_downloader.py` | 992 | `run_download`: nativo primero, `DepotDownloaderMod.dll` (.NET 9, `-manifestfile` siempre) de respaldo; `_resolve_depot_owners` (`depotfromapp`); `filter_depots_by_os`; `move_manifests_to_depotcache`; `<tmp>/mistwalker_keys.vdf` | 4, 9 |
| `sff/downloads/dotnet_utils.py`, `download_manager.py`, `filetree.py`, `progress.py` | 228, 384, 118, 102 | .NET 9 (`dot.net/v1/dotnet-install.{ps1,sh}`; Windows con `CERT_NONE`); historial de descargas (500); árbol de ficheros por depot | 4, 11 |
| `sff/network/steam_client.py` | 448 | sesión Steam **anónima** única en hilo dedicado; `get_app_info` con caché 7 días y **`api.steamcmd.net` antes del CM** (`:378-413`); `ParsedDLC` | 2, 3, 4, 10 |
| `sff/network/steam_store.py`, `http_utils.py`, `store_browser.py`, `pixeldrain.py` | 142, 278, 469, 317 | `appdetails` y scraping de `<title>`; `get_game_name` (catálogo local → store → `App <id>`); cliente Hubcap `hubcapmanifest.com/api/v1` (Bearer `smm_`); pixeldrain por proxy `pixeldrain-bypass.gamedrive.org` | 3, 4, 6, 9, 10 |
| `sff/lua/endpoints.py` | 942 | proveedores de `.lua`: `get_hubcap` (`/manifest/{app}`, zip lua+manifests, cuota diaria), `get_ryuu` (reseller `auth_code` / premium `X-Auth-Key`), `get_depotbox` (`/api/direct-lua`), `get_freelua` (trionine: `api.steamcmd.net` + `fylsdy/ManifestHub/depotkeys.json` + BD local → `api.luagen.revobd.club/{app}.zip` → `steamtoolsapp/ManifestHub` y `steamtools-games/ManifestHub3`), `_seed_free_manifest` (tres espejos en paralelo, **sin validar magic**), `fetch_build_details` (DepotBox con token embebido ofuscado) | 4, 5, 9, 10 |
| `sff/lua/provider.py` | 770 | BD local `fallback_depotkeys.json` (no en el repo ni en el build), refresco cada 6 h desde `KoriaPolis/Steam-Depot` o R2, ingestión de cada lua, **contribución** a `stea-provider-api.steamidra.workers.dev/submit` cada 3 h (excluye luas con el banner ofuscado "Downloaded using DepotBox") | 4, 10, 12 |
| `sff/lua/choices.py`, `manager.py`, `writer.py`, `generator.py`, `update_pins.py`, `dlc_appid_enricher.py`, `fallback_tokens.json` | 339, 281, 432, 202, 351, 184, datos | flujo CLI (appid o URL de tienda, `GetAppList` con clave Web API, fzf); parser de lua (`addappid`, `setManifestid`, `addtoken`; appid = nombre numérico del fichero) y `write_manifest_pins_to_lua`; `ACFWriter` (no en Windows; `chmod 444` en Linux; `_patch_acf_error_state`) y `ConfigVDFWriter` (`depots/<id>/DecryptionKey`, nunca sobrescribe, `config.vdf.backup`); lua agrupado; comentar/descomentar `setManifestid` + helper `00_LetUpdate_override.lua` (Windows); `addappid(<dlc>)` para DLC sin depot; 6.063 pares depot→gid | 2, 3, 4, 5, 9 |
| `sff/zip.py`, `game_list_fallback.py`, `tools/fetch_store_metadata.py`, `sff/store/*`, `sff/data/manifest.yaml` | 221, 831, 79, 162, 17 MB | zip/7z/rar → lua + manifests a staging y `depotcache`, `safe_extract_*`; catálogo offline (`SteamTools-Team/GameList`, `jsnli/steamappidlist`, `charts/topselling`); seed de CI; `ddmod_launcher.py` **sin llamadores**, `older_version.py` (SteamDB en `QWebEngineView`); manifest de **Ludusavi** (48.716 bloques `steam:`) que solo lee `cloud_save_paths.py` por una ruta que **no existe** | 4, 8, 9, 11 |

### 1.3 Juego, fixes, DLC unlockers, cloud, utilidades, terceros

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `sff/game/fix_game/service.py` | 695 | pipeline "Fix Game" (DRM check por `appdetails` → Goldberg update → `steam_settings/` → Steamless → DLL swap o ColdClient/ColdLoader → launch scripts), `restore_game`; `_close_game_processes` (`taskkill /F /IM` / `pkill -x` de cada `.exe` de la raíz); `generate_emu_config.exe` con login de Steam opcional **guardado en settings**; SteamID64 por defecto `76561198001737783` | 6, 7, 10 |
| `sff/game/fix_game/goldberg_applier.py`, `config_generator.py`, `goldberg_updater.py`, `steamstub_unpacker.py`, `cache.py`, `gse_tool_updater.py` | 752, 791, 470, 380, 205, 368 | aplicación de gbe_fork (regular/Linux `.so`/ColdClient loader/ColdLoader DLL; acceso directo en el Escritorio); `configs.app.ini` con **`unlock_all=1`** + lista de DLC, `achievements.json`/`stats.json` por Web API, identidad global en `GSE Saves/settings/`; releases de `Detanup01/gbe_fork` sin verificación, fallback `third_party/gbe_fork`; Steamless `--exp --realign --recalcchecksum` sobre todos los candidatos; caché; `GseToolUpdater` **sin llamadores** | 6, 3, 7 |
| `sff/game/game_specific.py`, `online_fix.py`, `crack_fix.py`, `steamauto.py`, `launch_options.py` | 800, 324, 205, 376, 272 | caminos legado: `crack_dll` (swap con `OG_<dll>`, sin deshacer), `apply_steamless` (un exe, `.steamlocked.bak`), `manage_dlc_unlockers`, `run_gen_emu` (**sin llamadores**); online-fix.me solo búsqueda (Google/Bing con UA falso) + navegador; "Fixes & Bypasses" (`KoriaPolis/CrakFiles` → pixeldrain, contraseña `cs.rin.ru`, zip **sin backup**); SteamAutoCrack CLI (`.NET 10 win-x86`, lanzado sin `wine` en Linux; `NameError` latente `:323`); `-onlinefix` en `localconfig.vdf` | 6, 3, 9 |
| `sff/game/let_update_override.py`, `update_prompt_override.py`, `auto_update_defaults.py`, `acf_pending_queue.py`, `download_queue.py`, `start_menu.py` | 141, 190, 98, 152, 459, 112 | helpers Lua de LumaCore (`00_LetUpdate_override.lua`, `skipmanifestpin`; Windows); "Linux always returns off"; cola persistente de ediciones de ACF (30 s, 7 días); cola FIFO de descargas; `.lnk`/`.desktop` | 5, 9 |
| `sff/dlc_unlockers/*` | 1.588 + 8 DLL | SmokeAPI (todas las `steam_api*.dll` del árbol, `default_app_status: unlocked`), CreamAPI (`unlockall=false` + lista), UplayR1/R2; DLL **empaquetadas en el repo** usadas antes que la caché y que GitHub; lista de DLC = `extended.listofdlc` | 3 |
| `sff/cloud/cloud_saves.py`, `cloud_save_paths.py`, `google_drive.py` | 1630, 192, 510 | backup/restore de `userdata/<sid>/<app>/remote`, "All Save Locations" (emuladores + Ludusavi + rutas custom), local/rclone/Drive v3 (`drive.file`, token en claro; `client_id` en `sff/core/_gc.py` gitignorado → Drive desactivado en el árbol público); Ludusavi por `sff/cloud/data/manifest.yaml` **inexistente** | 8, 10 |
| `sff/analytics.py`, `updater.py`, `uri_handler.py`, `image_cache.py`, `recent_files.py`, `fzf.py`, `i18n.py`, `quick_tools/steam_updates.py` | 235, 134, 176, 180, 119, 59, 103, 42 | analytics **100 % local** (`analytics.json`); self-update por `api.github.com/repos/drappula/SFF/releases` sin hash ni firma; `midra://` registrado (Windows) y **nunca procesado**; `steam.cfg` `BootStrapperInhibitAll=Enable\|False` (tile "Steam Updates") | 11, 12, 9 |
| `third_party/` | 1.806 ficheros | DDMod 3.4.0 (+copia Linux), SteamAutoCrack CLI 3.5.0.5 (.NET 10, 162 MB, con su propio Goldberg), Steamless 3.1.0.0 (Windows) y `Steamless.CLI.dll` (Linux, dotnet), gbe_fork Windows/Linux (sin versión), `gbe_fork_tools(_linux)`, coldloader (Rust, sin licencia), fzf 0.66.1 (solo `.exe`), rclone 1.74.2 (Windows; Linux **ELF i386 estático**), `linux/slscheevo/SLScheevo.py` (57 KB, no leído) | 6, 7, 8, 4 |

### 1.4 GUI, UI compartida, web UI

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `Main_gui.py` | 714 | entrada GUI: `QTWEBENGINE_DISABLE_SANDBOX=1`, flags Chromium GPU-off en Linux, `debug.log`/`crash.log`, bootstrap .NET 9, ruta de Steam, `SingleInstanceGuard`, `UI`, `SFFMainWindow`, `TrayIcon`, `UriHandler.register()`; **Linux: `check_and_notify_update()` al arrancar y cada 60 min en hilo daemon** (`:520-548`); self-update a los 2 s; prewarm de patrones LumaCore (Windows) a los 8 s | 1, 10, 11 |
| `sff/gui/main_window.py` | 2049 | `QWebEngineView` + `QWebChannel` (objeto `"bridge"`), `LocalContentCanAccessRemoteUrls`, banner LumaCore, arranque del preserver, UI clásica Qt, workers para Steamless/SteamAutoCrack, "save watcher" periódico, barrido de restos del updater | 1, 5, 6, 8, 11 |
| `sff/gui/web_bridge.py` | 3412 | ~170 `@pyqtSlot` que delegan en `bridges/*`; validación de claves de proveedor al arrancar (3 s) y diálogo de clave muerta; cola de descargas (siempre el camino "fastest"); contribución de claves cada hora; refresco de la BD cada 10 min; migración `config/lua` → `stplug-in`; ~800 líneas duplicadas de `store_bridge.py` | 3, 4, 5, 9, 10, 11 |
| `sff/gui/bridges/download_bridge.py` | 3077 | `download_game_with_source` (Windows: handoff a LumaCore sin descargar; Linux: `process_from_store`), `_bridge_run_local_import`, `download_dlc_free` (append de `addappid` al lua del padre + manifest + ficheros), `download_game_version[_native]`, `download_older_version_auto` (DepotBox build-details; en Linux mata Steam), `fetch_depot_filetree`, `download_game_ddmod` (mata Steam si el destino es biblioteca) | 3, 4, 5, 9 |
| `sff/gui/bridges/misc_bridge.py` | 2867 | historial de depots, DLC check, `check_game_update`, LetUpdate, `lure_fix_acf`, restart Steam, LumaCore, `steam.cfg`, online fix, settings, provider, **Linux Setup / Fix Hash / Patch Gaming Mode / Enable SafeMode** (`:1747-1882`; rutas de Steam fijas, ignoran el `steam_path` configurado), bulk import, `delete_game` (`rmtree(game_path)` sin comprobar que sea biblioteca) | 1, 3, 5, 6, 7, 9, 10, 11 |
| `sff/gui/bridges/store_bridge.py`, `game_bridge.py`, `cloudsaves_bridge.py`, `bulk_import.py`, `gui_prompts.py`, pestañas clásicas, `log_window.py`, `themes.py`, `title_bar.py` (muerto), `gen_web_bridge.py` (apunta a fichero inexistente) | 1869, 419, 729, 627, 269, … | búsqueda (catálogo local → `GetAppList` → espejos GitHub + Hubcap `/library`,`/search` + `GetItems`), `fix_game`/`revert_game`, cloud saves, cola de import con dedupe SHA-256, prompts Qt desde hilos (timeout 30 s) | 4, 6, 8, 9, 10 |
| `sff/ui/ui.py` | 1985 | lógica compartida CLI/GUI: `SLSManager` (Linux) o `LumaCoreManager`; `process_lua_full`, `process_lua_minimal`, **`process_from_store`** (`:952-1243`: lua → `sls_man.add_ids` + `dlc_check` + `add_manifest_id` + `patch_slssteam_config` → `config.vdf` → `saved_lua` + pins → `install_lua_to_steam` → manifests → `run_download` → `write_acf_direct`); `remove_game_menu` (`basic`/`full`/`full_keys`); `check_updates` (self-update); `update_all_manifests` | 2, 3, 4, 5, 6, 9, 11 |
| `sff/ui/prompts.py`, `tray_icon.py`, `midi.py`, `notifications.py` | 240, 256, 160, 140 | prompts (Qt o InquirerPy); bandeja; reproductor MIDI (`c/midi_player_lib.{dll,so}`: no hay `.so`); `win10toast` | 11 |
| `sff/webui/index.html` + `js/*.js` (13) + `css/*` | 2382 + ~9.000 | páginas home/store/library/downloads/downgrade/fixgame/cloudsaves/linuxguide/settings; `bridge.js` (`QWebChannel` sobre `qt.webChannelTransport`); guía Linux con los pasos de la Deck (`steamos-readonly disable`, `SafeMode: yes`, `steam-jupiter`); página `tools` huérfana; `cloud-backup-btn` inexistente; 36 temas | todas |
| `c/`, `hooks/`, `sff/locales/` | — | reproductor MIDI (tsf/tml/miniaudio, `.dll` precompilada, `FF5.sf2`, 14 `.mid`); hook PyInstaller `win10toast`; 19 idiomas × 2 ficheros (38 y 144 claves; `webui_zh_CN.json` 916) | 11 |

### 1.5 Build, release, docs upstream

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `.github/workflows/release.yml` | 142 | tag `v*` → `build-linux` (ubuntu-24.04: `fetch_store_metadata.py`, `build_linux_appimage.sh --fresh`, zip + AppImage suelto) + `build-windows` (NSIS + zip; comprueba que no se cuelen `.so`) → `softprops/action-gh-release` con `release-note.md`, etiquetas `[Linux] ZIP` etc. Sin checksums ni firmas | 11, 12 |
| `build_linux_appimage.sh`, `build_sff_linux.spec` | 191, 335 | venv 3.12, `requirements-linux.txt` + `steam==1.4.4 --no-deps` + `seleniumbase --no-deps`, PyInstaller onedir, AppDir con `AppRun` (`QTWEBENGINE_DISABLE_SANDBOX=1`), `appimagetool` **commiteado** (8,5 MB); allowlist de `third_party/`; `fallback_depotkeys.json` solo "si existe" | 11 |
| `build_sff.spec`, `build_sff_gui.spec`, `installer.nsi`, `build_installer.bat`, `build_simple*.bat` | 238, 246, 277, 57, 72 | Windows CLI (no publicado) y GUI; NSIS nivel usuario, zlib sólido, descarga .NET 9/VC++ de `aka.ms`, desinstalador que ofrece borrar las DLL de LumaCore; `hv` en el spec no existe desde 6.5.7; fallback `VERSION "6.6.7"` desfasado | 1, 11 |
| `requirements.txt`, `requirements-linux.txt`, `INSTALL_DEPENDENCIES.md` | 298, 97, 94 | pins (PyQt6 6.10, selenium, gevent, `torpy`+`pysocks` sin uso, zendriver, google-*, py7zr, keyring, pynacl; Linux añade `zstandard`); comentarios de GMRC/Tor obsoletos | 4, 11 |
| `AGENTS.md`, `CHANGELOG.md`, `release-note.md`, `README.md`, `docs/*` (16) | 115, 2271, 23, 213, ~1.400 | método del fork (agente IA, CHANGELOG obligatorio, ritual de release); historia 6.9.0 → 4.5.1; docs desfasadas (LINUX_SETUP sin headcrab, "injection cannot attach in Game Mode", `steamcmd bundled`; SETUP_GUIDE DLLs desde `sff/lumacore/`; CRACK_FILES buzzheavier; USER_GUIDE con Tools/Workshop eliminados); `NOTICE.md` de Midrags pide no archivar | 12 |

---

## §2 Las doce funciones

Cada función contesta la lista de control de `ecosystem-matrix.md` §1, con
`fichero:línea` de `drappula/SFF@4159300` (rutas relativas a `sff/` salvo
`Main*.py`, `steamidra_install.sh` y `headcrab.sh`). Linux primero; Windows
donde cambia.

### 2.1 Engancharse a Steam

**Cómo entra.** SteaMidra no entra: instala a otro.

- *Linux*: **SLSsteam** por `LD_AUDIT`, en tres capas que SFF escribe y una que escribe headcrab:
  1. **headcrab** (`setup_via_headcrab`, `linux/slssteam.py:399-457`): baja `raw.githubusercontent.com/Deadboy666/h3adcr-b/refs/heads/main/headcrab.sh` (rama `main`, sin pin ni hash), lo pasa por `_cr_filter` (`:340-373`: comenta las líneas con `cloudredirect`, las descargas de CloudRedirect, el `flatpak … install` y las llamadas `crinstall`/`wherecr_install`/`crconfigcheck`), lo guarda en `<tmp>/headcrab_install.sh` y lo ejecuta con `bash` (stdin heredado si hay TTY, `DEVNULL` si no, `:385-396`). Lo que hace el script hoy (`headcrab.sh` bajado el 2026-10-08, 954 líneas): instala dependencias con `sudo` (`apt`/`pacman`/`xbps`), `killall steam`, compara la versión del cliente (`package/steam_client_*.manifest`) con **`HeadcrabCompatibleClientVer=1788652215`** y si difiere **rebaja el cliente** (`clientdowngrade`: manifest de `Deadboy666/SteamTracking@headcrab`, `sources.txt`, servidor local `dgsc --port 1666` que sirve el paquete viejo, `:215-226,494-534,706`), baja el último `SLSsteam-Any-release.7z` de AceSLS y `netsock.so` de `yesyes0649/steamnetsock-patch` a `~/.config/SLSsteam/tools/netsock/` (`:739-760`), copia `SLSsteam.so` + `library-inject.so` a `~/.local/share/SLSsteam/` (o la ruta Flatpak), fusiona `res/config.yaml` con el del usuario, lanza Steam una vez con `LD_AUDIT` en el entorno (`export_sls`, `:762-770`), escribe `steam.cfg` si no existe (`:852-862`), edita `config.yaml` (`editconfig`, `:835-850`: SteamOS → `SafeMode: yes`, `NotifyInit: yes`; CachyOS → `LogLevels: 0x3f`, `SafeMode: no`; resto → `SafeMode: no`; **siempre** `echo "config patched" > .headcrabd`) y **sustituye `steam.sh` entero** por `h3adcr-b-modul3s/headcrab_native.sh` (o `headcrab_flatpak.sh`), guardando el de Valve como `client.sh` y dejando el suyo en `555` (`:876-926`). Instala además un `headcrab.desktop` "Headcrab Updater" en el menú (`:99-110`).
  2. **`patch_steam_sh`** (`:111-133`), tras cada headcrab y tras cada hash fix: `os.chmod(steam_sh, 0o644)`, descarta **toda línea que contenga `LD_AUDIT`** (la de headcrab), inserta `export LD_AUDIT=<dir>/library-inject.so:<dir>/SLSsteam.so` en la línea 11 y no devuelve el bit de ejecución. Es decir, SFF reescribe el `steam.sh` de headcrab, no el de Valve.
  3. **`start_steam`** (`linux/steam_process.py:149-274`): `Popen(["steam"], env={…, "LD_AUDIT": "<inject>:<sls>"})` con el entorno limpio de `LD_LIBRARY_PATH`, `LD_PRELOAD`, `PYTHON*`, `QT_*`, `APPIMAGE`…; resuelve el `.so` por la ruta cacheada de `/proc/<pid>/maps`, por defecto nativa/Flatpak, o por un tercer fallback que busca **`SLSteam.so`** (`:177`, nunca encontrará el real). Se recorre en cada `install_lua_to_steam`/`remove_lua_from_steam` si Steam corría (`steam_tools_compat.py:39-65`), en "Restart Steam" y al acabar el Linux Setup. Si no encuentra el `.so`, arranca Steam **sin** inyección y devuelve éxito ("Starting Steam without SLSteam injection", `:204`).
  4. **`.desktop` por juego** (`linux/desktop_shortcuts.py:42-53`): `Exec=env LD_AUDIT="<inject>:<sls>" steam steam://rungameid/<id>`; Flatpak sin `LD_AUDIT`.
  5. **Deck Game Mode** (`patch_steam_jupiter`, `:166-227`, botón "Patch Gaming Mode", visible en todo Linux desde `02976bf` 10-01 pero `is_steamos()` lo exige en el bridge, `misc_bridge.py:1842-1845`): inserta la misma línea `export LD_AUDIT=…` antes del último `exec ` de `/usr/bin/steam-jupiter`, `sudo cp` con backup `steam-jupiter.bak` una vez, `sudo chmod 755`; si falla, "In Konsole: sudo steamos-readonly disable" (`:214`). Docstring: "SteamOS updates reset the file; re-run after every update". Nada lo reaplica solo. El mismo botón escribe **`SafeMode: yes`** (`_bridge_enable_deck_safe_mode`, `misc_bridge.py:1871-1882`).
- *Windows*: **LumaCore** (`lumacore/lumacore_setup.py:495-620`): `taskkill steam.exe`, borra GreenLuma una vez, baja `release.zip\|rar` del último release de `drappula/LumaCore` (`:125-126`; antes `KoriaPolis/LumaCore`, `75829ee` 10-03) sin verificación, extrae `dwmapi.dll`, `xinput1_4.dll`, `LumaCore.dll`, `LumaCorePayload.dll` a `<steam>`.

**Cómo localiza lo que parchea.** Nada propio. Para LumaCore **precalienta** `<steam>/lumacore/pattern/<sha256(steamclient64.dll)>.toml`, `<sha256(steamui.dll)>.toml` y `steamclientipc/<sha256>.toml` desde `raw.githubusercontent.com/michelegoku3/MigoReleases/pattern/{steamclient,steamui,steamclientipc}/` (`:163,298-335`; espejos jsdelivr/gitflic comentados; validación `rva`+`sig` o `interface_id`+`funcHash`) al instalar y en cada arranque de la GUI (`prewarm_pattern_cache_if_missing`, `:371-383`). Para SLSsteam no descarga patrones: los del `.so` van compilados.

**Qué pasa cuando Steam actualiza.** *Linux*: no hay gate propio; el plan es **impedir la actualización**: `steam.cfg` con `BootStrapperInhibitAll=enable` + `BootStrapperForceSelfUpdate=disable` (`create_steam_cfg`, `:230-245`, creado si no existe; headcrab hace lo mismo) y el tile "Steam Updates" que lo pone en `Enable`/`False` (`quick_tools/steam_updates.py:35-42`). SafeMode: lo que deje headcrab (`yes` en SteamOS, `no` en el resto) o el botón (`yes`); `patch_slssteam_config` escribe `SafeMode: no` y `WarnHashMissmatch: no` (`:266-267`) **solo** si no existe `.headcrabd` (`:257-258`), y la plantilla de config nueva también `no` (`yaml_config.py:359-361`). Si el cliente cambia y SLSsteam dice "Unknown steamclient.so hash", el remedio es el botón **"Fix Hash Issue"** (`fix_hash_mismatch`, `:464-571`): `kill_steam` → `bash -c "curl -fsSL https://headcrab.pages.dev/reset \| bash"` (timeout 120 s, **sin filtro** de CloudRedirect) → `start_steam` + `sleep(30)` → `kill_steam` → `curl -fsSL https://headcrab.pages.dev \| bash` → `pkill` → `patch_slssteam_config` (no hace nada por el marcador) + `patch_steam_sh` + `create_steam_cfg`. *Windows*: banner "No cached LumaCore support data for this Steam build" cuando `status.json` no trae TOML (`status_banner.py:104-118`).

**Cómo se recupera.** Sin crash guard, kill-switch ni detección de crash-loop: si SLSsteam rompe Steam, `steam.sh` sigue exportando `LD_AUDIT` hasta que el usuario lo edite o pase el hash fix. Logs: `debug.log` y `crash.log` de SteaMidra; `~/.SLSsteam.log` no se lee. Windows: `deactivate_lumacore` borra las DLL (`:840-895`); el desinstalador NSIS ofrece borrarlas.

**A qué procesos se limita.** `pidof steam` / psutil nombre `== "steam"`; el `LD_AUDIT` va en `steam.sh`, así que afecta a todo lo que `steam.sh` lance (lo filtra el `.so`). Flatpak: `detect_steam_type()` devuelve `flatpak` **si existe el directorio** aunque haya Steam nativo (`:74-78`); si `Popen(["steam"])` con `LD_AUDIT` llega al sandbox no se puede saber desde este código.

**Cuándo se instala.** Tres entradas: menú CLI "Set up Linux tools" (`linux/linux_download.py:294-371`), botón GUI "Set Up Linux Tools" (`misc_bridge.py:1750-1792`: guarda de `~/.bashrc` en SteamOS, `kill_steam`, headcrab, `migrate_existing_games`, `ensure_dotnet_9`) y **automática**: `check_and_notify_update` en cada arranque de la CLI (`Main.py:570-575`) y de la GUI más cada 60 min (`Main_gui.py:520-548`). Lógica (`slssteam.py:655-716`): `latest` = `tag_name` de `api.github.com/repos/AceSLS/SLSsteam/releases/latest` (sin token, sin caché); `installed` = contenido de `~/.local/share/SteaMidra/SLSsteam/VERSION` o `"unknown"` si hay `.so` (`:289-306`); `update_available = latest and installed and installed != latest` (`:330`); `needs_install` y Steam cerrado → `setup_via_headcrab`. Como **nada escribe `VERSION`** desde `92d6813` (la escritura se borró con el instalador propio), `installed` es siempre `"unknown"` y headcrab **se reejecuta en cada arranque y cada hora** mientras Steam esté cerrado: cada pasada vuelve a bajar el script, `pkill -f dlm\|dgsc\|wget` (mata cualquier `wget` del usuario, `:376-382`), reescribe `steam.sh` y rellena la config. El docstring describe otra intención ("Already up to date -> no output"). Solo un 403 de GitHub (60 req/h sin token) lo silencia.

### 2.2 Propiedad y licencias

**Spoof de propiedad.** Delegado: en Linux, la lista **`AdditionalApps`** de `~/.config/SLSsteam/config.yaml`; en Windows, `addappid(<id>)` en `<steam>/config/stplug-in/<id>.lua` que LumaCore lee.

- **Qué entra en `AdditionalApps`** (`app_injector/sls.py:88-160`): el `app_id` del lua, **todos los `dlcappid`** del appinfo del padre ("so they show in Steam properties", `:108-116`), y `AppTokens` con los `addtoken` del lua (`:97-98`, desde `9adac94` 09-08). Escritura por regex tras el último `- ` de la sección (`yaml_config.py:380-399,444-485`), con comprobación de duplicados. `dlc_check(auto_add_depot_dlcs=True)` (`:236-345`, en `process_from_store`/`process_lua_full`/downgrade) añade además los DLC con clave ya en `config.vdf` y, previa confirmación, los sin depot. `migrate_existing_games` (`linux/slssteam.py:574-652`, una vez por instalación) mete **todo** `stplug-in/*.lua` y **todo `appmanifest_*.acf` de todas las bibliotecas**, poseídos o no, con el comentario "migrated from existing install" ("Preserves config from tools like ACCELA when users switch to SteaMidra").
- **Packages / licencias / reaplicación**: nada. SteaMidra no toca packages ni `LicensesUpdated`; lo que SLSsteam haga con `AdditionalApps` está en `slssteam.md` §2.2.
- **Juegos poseídos de verdad**: sin comprobación en ningún camino; `create_acf` escribe `LastOwner` con `STEAM32_ID` o `"0"` (`linux/acf_writer.py:84-92`). El comentario de `writer.py:83-84` explica por qué no escribe ACF en Windows: "LumaCore manages app ownership — ACF writing … can cause Steam to show 'Purchase' on secondary accounts".
- **Family sharing**: solo `DisableFamilyShareLock: yes` escrito en la config de SLSsteam (`:272`) y en la plantilla.
- **Depots compartidos (redists, 228980)**: lista fija `_REDIST_DEPOTS` (`lua/update_pins.py:41-47`: 1004-1006, 228500, 228980-229080) para agruparlos como `-- SHARED DEPOTS` en el lua generado y excluirlos del override de updates; `get_app_depots` excluye `depotfromapp == 228980` (`gui/bridges/store_bridge.py:896-963`). Sin más tratamiento.
- `PlayNotOwnedGames`: **no se escribe** (solo el comentario de `download_bridge.py:2319`; escrito en 6.6.0, retirado en 6.6.1; SLSsteam eliminó la clave).

### 2.3 DLC

- **Criterio**: en Linux, cada `dlcappid` del appinfo del padre va a `AdditionalApps` **siempre** (`sls.py:108-116`); `DlcData` (`<padre>: {<dlc>: "<nombre>"}`) **solo** cuando `extended.listofdlc` del padre tiene ≥ 64 entradas (`_dlc_count`, `:41-43,106`; comentario "SLSsteam only needs the explicit map past that"). DLC sin depot (`DLCTypes.NOT_DEPOT` = "PRE-INSTALLED") → solo `AdditionalApps`, previa confirmación (`:313-320`). En el lua, `dlc_appid_enricher.append_depotless_dlcs` añade `addappid(<dlc>)` por cada `NOT_DEPOT` tras descargar de Hubcap/Ryuu/DepotBox (`lua/dlc_appid_enricher.py:131-184`); Free Providers lo hace inline (`endpoints.py:694-696`). En Windows el lua es todo; `LumaCoreManager.dlc_check` solo muestra una tabla.
- **Camino CLI "Download a game (Linux)"**: `_add_to_slssteam` escribe `DlcData` si hay > 64 **depots seleccionados distintos del appid** (no ids de DLC), con valor `{}` y mediante `yaml.dump` del fichero entero (`linux/linux_download.py:177-196`): pierde comentarios y orden; si SLSsteam acepta `{}` ahí no se puede saber desde aquí.
- **"DLC Check"** (`misc_bridge.py:118-457`): estado por DLC desde `AdditionalApps`, `addappid` del lua del padre, `InstalledDepots`/`MountedDepots` del ACF, claves en `config.vdf`, manifests en `depotcache` y (Windows) registro `HKCU\Software\Valve\Steam\Apps\<dlc>\Installed`; "Unlocked" = cualquiera de ellos. Descarga: `download_dlc_free` (`download_bridge.py:695-1024`) baja el manifest del depot del DLC, **añade al final** de `stplug-in/<padre>.lua` `addappid(<depot>, 1, "<key>")` y `addappid(<dlc>)`, parchea `InstalledDepots` del ACF del padre (`size:"0"`, `dlcappid`) y quita `MountedDepots`, descarga los ficheros con el nativo (`os_filter="windows"`) y en Linux `sls_man.add_ids([dlc])`.
- **DLC comprado de verdad**: sin distinción. **Límite de 64**: solo como umbral para `DlcData`; no hay tope.
- **Cómo se quita**: `remove_additional_app` solo del id elegido (`sls.py:228-232`) o del appid base en `delete_game` (`misc_bridge.py:2759`); **los DLC en `AdditionalApps`, `DlcData` y `AppTokens` no se limpian nunca**. No hay UI para retirar un DLC.
- **Goldberg / unlockers** (fuera de Steam): `configs.app.ini` con `unlock_all=1` + lista (`game/fix_game/config_generator.py:185-264`); SmokeAPI `default_app_status: unlocked` sobre **todas** las `steam_api*.dll` del árbol; CreamAPI `unlockall=false` + lista con nombres `DLC_<id>`; Uplay `blacklist: []`. Lista = `extended.listofdlc` del PICS (`game/game_specific.py:717-728`). Las DLL vienen **empaquetadas en el repo** (`dlc_unlockers/resources/`, sin versión) y se usan antes que la caché y que GitHub (`dlc_unlockers/downloader.py:48-129`).

### 2.4 Claves y manifests

**Claves: de dónde salen.** Del `.lua` del proveedor elegido (`LuaEndpoint`: Free Providers, Hubcap, Ryuu, DepotBox; `lua/endpoints.py`), de un lua/zip del usuario, o de la **base local** `fallback_depotkeys.json` (`lua/provider.py`: candidatos `sff/lua/`, `_internal/sff/lua/`, `<datadir>/provider_cache/`; **ninguno existe en el checkout ni en el build**, `.gitignore:48`, `release.yml` no la descarga; se baja de `raw.githubusercontent.com/KoriaPolis/Steam-Depot/main/fallback_depotkeys.json` o `pub-d3ba7941fdf24c2c84da530b93221e1c.r2.dev` cada 6 h, `:38-41,323-354`, y se alimenta con cada lua descargado, `:357-389`). Orden de "Free Providers" (`get_freelua`, `:633-739`): (1) "trionine": appinfo de `api.steamcmd.net/v1/info/{app}` + claves de `raw.githubusercontent.com/fylsdy/ManifestHub/main/depotkeys.json` (caché 24 h; **404 hoy**) + BD local → construye el lua (`addappid({d},0,"{key}")`, `setManifestid` con el gid público, `addappid({dlc})`) solo si hay al menos una clave; (3) `api.luagen.revobd.club/{app}.zip`; (4-5) `steamtoolsapp/ManifestHub/{app}/{app}.lua` y `steamtools-games/ManifestHub3/{app}/{app}.lua`. Un lua cacheado se reutiliza si tiene claves `,0,` y todos sus pins tienen `.manifest` (`_cached_lua_usable`, `:588-610`; no reconoce `,1,` → los de Hubcap/Ryuu se refetchean siempre).

**Dónde se guardan.** (a) `saved_lua/<app>.lua` (relativo a `Path.cwd()`); (b) `<steam>/config/config.vdf` → `InstallConfigStore/Software/Valve/Steam/depots/<id>/DecryptionKey` por `ConfigVDFWriter` (`lua/writer.py:361-394`; `config.vdf.backup`; **nunca sobrescribe** una clave existente; Linux desde 6.4.9); (c) el lua copiado a `<steam>/config/stplug-in/<app>.lua`; (d) la BD local; (e) `<tmp>/mistwalker_keys.vdf` para DDMod. **Nada** en `config.yaml` de SLSsteam (§3.4 lo lista entero). Borrado: solo `remove_decryption_keys` en el `full_keys` de la CLI (`ui.py:544-572`).

**Contribución** (`provider.py:441-730`): si `PROVIDER_CONTRIBUTE_KEYS` (`SettingItem` default `False`, `core/structs.py:399`; **la migración 1.1.0 lo pone a `True`** en cualquier `settings.bin` sin la clave, `core/storage/settings.py:223-227`, en cada carga `:83`), cada 3 h escanea `saved_lua/*.lua`, `stplug-in/*.lua` y `config/lua/*.lua`, descarta los que lleven el banner ofuscado "Downloaded using DepotBox" (`:66-73,443-454`) y hace `POST https://stea-provider-api.steamidra.workers.dev/submit` con `{tool_version, type:"tool_keys", items:[{id,key,name,kind,parent_appid,parent_name}]}` en lotes ≤ 1000/200 KB. El fork no ha tocado la URL.

**Manifests: de dónde y en qué orden** (`manifest/downloader.py:482-533`, igual para juego y workshop):
1. copia local exacta `<depot>_<gid>.manifest` en staging (`<sff_data>/manifests`), `<steam>/depotcache`, `<steam>/config/depotcache` (`:95-109`);
2. **LuasTools**: `GET https://manifest.luastools.xyz/m/{depot}/{gid}` (`:54,430-447`, 30 s, sin clave; "first-priority for both game and workshop manifests", `:489`; desde `c2c35af` 09-20);
3. si el lua vino de Hubcap o hay clave Hubcap: **Hubcap on-demand** `GET hubcapmanifest.com/api/v1/generate/manifest?depot_id=&manifest_id=` con Bearer (`:339-379`; "Limit: 1500/day") → espejos GitHub → ManifestHub → nada;
4. si no: **tres espejos planos** `raw.githubusercontent.com/{qwe213312\|mejikuhibiniu1\|Sainan}/k25FCdfEOoEJ42S6/main/{depot}_{gid}.manifest` (`:56-60,406-428`) → **ManifestHub2** `GET api.manifesthub2.filegear-sg.me/manifest?apikey=&depotid=&manifestid=` (`:449-480`; clave de 24 h pedida abriendo `manifesthub2.filegear-sg.me`, `manifest/manifesthub_key.py:60-98`) → `None`.
5. **No hay CDN** en `ManifestDownloader`: `get_cdn_client` (`:311-337`) lleva el comentario "Steam now refuses manifest requests from clients that don't own the depot, so an anonymous login just stalls here - this path is dead" y solo lo llama `download_bridge.py:826`.

Antes de descargar, `_cleanup_live_stale_manifests` **borra** de `depotcache/` y `config/depotcache/` los `<d>_*.manifest` del mismo depot con otro GID (`:157-181`), y `_preseed_depotcache` copia los que ya estén en staging. Los ZIP de Hubcap/Ryuu/revobd traen `.manifest` que `zip.read_lua_from_zip` siembra en staging y `depotcache` (`zip.py:61-77`); `_seed_free_manifest` (`endpoints.py:532-585`) pide en paralelo `qwe213312/…`, `steamtoolsapp/ManifestHub/{app}/{d}_{gid}.manifest` y `steamtools-games/ManifestHub3/{app}/…` ("first 200 wins", sin validar magic). El **preserver** (`manifest_watcher/preserver.py`) restaura desde el staging lo que Steam borre de `depotcache`; la descarga CLI deja los manifests en `<lib>/steamapps/depotcache`, que el borrado no mira.

**Códigos de petición de manifest.** Un solo proveedor: la **sesión anónima** del descargador nativo, `cdn_client.get_manifest_request_code(app, depot, gid)` (`downloads/native_downloader.py:302-319`; "No external request-code mirrors anymore: they (steam.run / wudrm / opensteamtool) were retired"), y solo cuando no hay manifest local. Si falla, `RuntimeError("No manifest request code …")` y el depot pasa a DDMod, al que siempre se le da `-manifestfile` "to avoid the 'No manifest request code' error" (`downloads/depot_downloader.py:646-652`). `_REQUEST_CODE_FALLBACKS`, `_fetch_manifest_code_external` y 350 líneas se borraron en `9033611` (09-12); `st-gmrc.kur0.deno.dev` y Tor (`torpy`, `pysocks`) solo sobreviven en `requirements.txt:19-31`. 20770407 y manifestdex: cero referencias.

**Descarga nativa** (`native_downloader.download_depot`, `:444-912`): `anonymous_login`, servidores `cdn.servers` sondeados por latencia (top-4 en round-robin), token CDN, manifest local o por request code, `decode_manifest` (ZIP-wrapped, secciones `0x71F617D0`/`0x1F4812BE`/`0x1B81B817`, AES de nombres), filtros `include_dirs` y `os_filter` por heurística de ruta (sin leer `oslist`), preasignación con `truncate`, **pre-verificación SHA-1** de chunks en disco, 8-64 hilos (`DOWNLOAD_CONCURRENCY`), 3 intentos rotando host, AES-256 + VZ1/VSZTD (`zstandard` o binario `zstd`)/ZIP, SHA-1 por chunk, limitador leaky-bucket. Respaldo **DDMod** (`run_download`, `:412-817`): `dotnet DepotDownloaderMod.dll -app <owner> -depot <d> -depotkeys <tmp>/mistwalker_keys.vdf -manifest <gid> -manifestfile … -validate -dir <dest>/steamapps/common/<installdir> [-os] [-filelist]`, 3 intentos, detección de "exit 0 con 0 bytes".

**Historial de versiones** (`manifest/depot_history.py:1743-1849`): Steam CM anónimo (`depots.branches.<b>.timeupdated`, `manifests.<b>.gid`, DLC) → `steamcmd.morrenus.net/api/{app}` → árbol GitHub de `qwe213312` (API, 60 req/h sin token; `GITHUB_TOKEN` del entorno) → `fallback_tokens.json` (6.063 pares) → SteamDB solo con cookie cacheada. Alternativas: `fetch_build_details` → `depotbox.org/api/depotboxtool/v1/build-details?build_id=` con un token **embebido y ofuscado** (`endpoints.py:828-855`, env `STEAMIDRA_BUILD_TOKEN`); HTML guardado de SteamDB; navegador embebido.

**Qué deja de funcionar si cae cada servicio.** Sin Hubcap/Ryuu/DepotBox: Free Providers. Sin `fylsdy` (ya): trionine solo con la BD local. Sin `KoriaPolis/Steam-Depot` y R2: la BD solo crece con los luas del usuario. Sin LuasTools: GitHub → ManifestHub2 (clave) → "Download failed" por depot. Sin `api.steamcmd.net` y sin CM: `get_app_info` `{}`, sin `buildid` ("0" en el ACF), estrategias de GID solo pins/local/manual, `dlc_check` por tienda. Sin GitHub API: no hay check de SLSsteam (y headcrab no se reejecuta), ni self-update, ni historial por árbol. Sin `headcrab.pages.dev`: no hay hash fix. Sin `dot.net`: no hay DDMod ni Steamless en Linux. No hay circuito: cada depot recorre toda la cascada.

### 2.5 Updates de juegos

- **Pins / congelación (Linux)**: tres capas. (a) `DisableUpdates: yes` en la config de SLSsteam (`slssteam.py:270`, plantilla y campos requeridos). (b) `ManifestIds` (`<depot>: <gid>`) escrito por `add_manifest_id` (`yaml_config.py:591-651`) **solo** cuando el usuario eligió versión (`manifest_override`, `ui.py:1041-1046`); borrado por depot al quitar "full" (`misc_bridge.py:2760-2764`). (c) `setManifestid` en el lua (`write_manifest_pins_to_lua`, `lua/manager.py:102-175`), respetado por `ManifestDownloader` solo con `USE_MANIFEST_PINS` (`downloader.py:286-301`). Más el **ACF** escrito a mano: `StateFlags "4"`, `buildid` y `TargetBuildID` del branch `public` (o `"0"`), `AutoUpdateBehavior "0"`, `ScheduledAutoUpdate "0"`, `InstalledDepots{manifest,size,dlcappid}`, y **0444** (`linux/acf_writer.py:94-124`; `lua/writer.py:147-151`: "Steam cannot flip StateFlags"). Y el cliente congelado por `steam.cfg`.
- **Windows**: helper `00_LetUpdate_override.lua` en `stplug-in` (global, instalado al arrancar con allow-list vacía desde `4e1721f` → juegos nuevos pineados; `game/update_prompt_override.py`), variante por juego `<stplug-in>/<appid>/00_LetUpdate_override.lua` (`let_update_override.py`), `update_pins.apply_selection` comenta/descomenta `setManifestid`; `AUTO_ENABLE_UPDATES_NEW_GAMES` (off); `auto_update_defaults.py:33-36`: "LumaCore is Windows-only. The LetUpdate override has no effect on Linux where SLSsteam handles update blocking through its own config."
- **Actualización automática y cómo decide**: no hay comprobación periódica en la GUI (`UPDATE_CHECK_INTERVAL_MIN` solo es ventana de frescura del badge); `GLOBAL_UPDATE_CHECK` off. Manual: "Update" por juego (`check_game_update`, `misc_bridge.py:464-660`: compara `buildid` del ACF con el CM; si difiere, refetch del lua, `install_lua_to_steam`, manifests, parche del ACF con los `.manifest` obtenidos, pins), "Update All Games" (`ui.update_all_manifests`, `:1552-1768`), "Lure Fix" (`lure_fix_acf`, `:865-989`: ACF con los GIDs `public` y `buildid` actuales **sin descargar nada**, `chmod 0444`). `update_check.py` está muerto. Actualizar = volver a bajar.
- **Cambio de versión**: picker de historial (§2.4) → `process_from_store(manifest_override, build_id_override)` (Linux) o `_native_install_pinned` (Windows); "Automatic" por Build ID → `download_older_version_auto` (`download_bridge.py:1506-1801`: DepotBox build-details, `remove_depots_from_lua` de los depots ausentes, manifests, pins, **mata Steam en Linux**, `write_acf`, `sls_man.add_ids` + `dlc_check`, `run_download`, `_sync_acf_downgrade` con `buildid`/`TargetBuildID`/`InstalledDepots` o cola `acf_pending_queue` cada 30 s hasta 7 días). En Linux la página Downgrade está oculta pero un Build ID corto en la búsqueda del picker llega al mismo camino (`app.js:2405-2414`).
- **Update que Steam ya empezó**: nada específico; `remove_acf_and_manifests` borra el ACF sin mirar `StateFlags`; la cola de ACF espera a `StateFlags & 4`.

### 2.6 Fixes y DRM

Cinco caminos con backups y deshacer incompatibles (`game/`):

| Camino | Qué hace | Backup | Deshacer |
|---|---|---|---|
| A "Fix Game" (`fix_game/service.py:132-313`) | DRM check por `store.steampowered.com/api/appdetails` (`"denuvo" in drm_notice` → **aborta**; `ext_user_account_notice` → aborta; otro DRM → fuerza `coldclient_simple`; red caída → sigue) → gbe_fork de `Detanup01/gbe_fork` (sin hash; fallback bundled) → `steam_settings/` (`configs.app.ini` `unlock_all=1`, `configs.main.ini`, overlay off, `achievements.json`+`stats.json` por Web API, `supported_languages.txt`/`depots.txt` de `steamcmd.morrenus.net`, identidad global en `GSE Saves/settings/` con SteamID64 **`76561198001737783`** por defecto) → Steamless sobre todos los `.exe` candidatos → swap de `steam_api*.dll` (o `libsteam_api.so` si `linux_native`, que la web **no** pasa) / ColdClient loader (+ acceso directo en el Escritorio) / ColdLoader DLL → `Launch*.bat` / `launch*.sh` + `LUTRIS_SETUP.txt` | `<dll>.bak`, `<exe>.steamstub.bak` | `restore_game` (`:315-368`): copia **todo `*.bak`** del árbol, borra `steam_settings/`, loaders, y `version.dll`/`winmm.dll` < 500 KB |
| B "Crack a game (gbe_fork)" (`game_specific.py:283-336`) | swap de una DLL con `third_party/gbe_fork/` | `OG_<dll>` | ninguno |
| C "Remove SteamStub" (`:338-546`) | Steamless sobre un exe elegido por PICS `config/launch` | `<exe>.steamlocked.bak` | ninguno |
| D SteamAutoCrack (`steamauto.py`) | CLI 3.5.0.5 (`.NET 10 win-x86`, su propio Goldberg y `SteamAPICheckBypass.dll`), modos `full` ("Breaks Steam achievements") / `steamless_only`; en Linux lanzado **sin `wine`** | `.steamidra_exe_backups/` (solo `.exe` de la raíz, borrado al acabar) | lo que haga el CLI (`Restore: False`) |
| E "Fixes & Bypasses" (`crack_fix.py`) | índice `raw.githubusercontent.com/KoriaPolis/CrakFiles/main/crackfiles.json` → pixeldrain por `pixeldrain-bypass.gamedrive.org` → extracción sobre el juego con contraseña `cs.rin.ru` | `.bak` solo en la ruta 7z/WinRAR; **zip sin backup** | ninguno propio |

- **Denuvo**: solo detección por `drm_notice` y parada; sin cuentas ni tickets. **EOS**: nada (solo exclusión de carpetas `EOSOverlayRenderer`). **Steamless**: en Linux `Steamless.CLI.dll` con `dotnet` (fallback `wine Steamless.CLI.exe`); la descarga CLI lo pasa **sin preguntar** por todos los `.exe` ≥ 100 KiB del juego (`linux/steamless.py`, `linux_download.py:276-281`). **Online-fix**: solo búsqueda (`online-fix.me`, Google, Bing con UA de Chrome) y navegador (`online_fix.py:287-324`); "LC Online Fix" = `-onlinefix` en `LaunchOptions` de `localconfig.vdf` para LumaCore (`launch_options.py`). En Linux el menú CLI excluye `REMOVE_DRM`, `CRACK_GAME` y `STEAM_AUTO` (`Main.py:210-230`); la web los expone todos.

### 2.7 Logros, stats y tiempo de juego

- **Linux**: **SLScheevo** (`third_party/linux/slscheevo/SLScheevo.py`, 57 KB, no leído; sin autor ni licencia en el repo) lanzado por `linux/slscheevo.py:65-82` con `--noclear --save-dir ~/.local/share/SteaMidra/SLScheevo --silent --max-tries 101 --appid <ids>`, plantilla `UserGameStats_TEMPLATE.bin`; se ofrece al final de la descarga CLI y en el menú `LINUX_ACHIEVEMENTS` (ids a mano o de `AdditionalApps`). Qué genera y dónde lo deja Steam no está en el código de SFF. `MaxSchemaTries: 10` escrito en la config de SLSsteam. `set_stats_and_achievements` es un stub que devuelve `False` (`registry_access.py:76-77`) y se llama igualmente.
- **Windows**: LumaCore (fuera). `run_gen_emu` (`generate_emu_config.exe` con usuario/contraseña de Steam **guardados en `settings.bin`**, copia de `.bin` a `appcache/stats/`) está **sin llamadores**.
- **Goldberg**: `achievements.json`/`stats.json` desde `api.steampowered.com/ISteamUserStats/GetSchemaForGame/v2` con la clave Web API del setting o la **embebida** (`core/strings.py:24-25`); logros en `GSE Saves/<appid>/`; sin sincronización salvo que el usuario incluya `GSE Saves` en el backup de §2.8.
- Dónde se guardan, sincronización entre máquinas, quién contesta `GetUserStats`: nada en SFF. Tiempo de juego: nada.

### 2.8 Cloud saves

- **Sin redirección en tiempo de ejecución**. `DisableCloud: yes` forzado en la config de SLSsteam (`slssteam.py:271`, plantilla, campos requeridos) y **CloudRedirect recortado a propósito** del headcrab que ejecuta (`_cr_filter`); el hash fix (`headcrab.pages.dev`) no pasa por el filtro. headcrab decide el modo CR por `grep "DisableCloud: no" config.yaml` (`headcrab.sh:94-97`), así que con `yes` tampoco lo instalaría.
- **Backup/restore** (`cloud/cloud_saves.py`): copia de `userdata/<steam32>/<app>/remote` (o todo `<app>/`), rutas de emuladores (`C:/Users/Public/Documents/{RUNE,OnlineFix,CODEX,EMPRESS,…}`, `%APPDATA%/GSE Saves`…; en Linux `APPDATA` vacío → rutas relativas al cwd), Ludusavi (**inactivo**: `cloud_save_paths.py:51-52` busca `sff/cloud/data/manifest.yaml`, el fichero está en `sff/data/manifest.yaml` y ningún spec lo copia) y rutas custom (`CLOUD_CUSTOM_SAVE_PATHS`). Destinos: local (`<dest>/SteaMidraAllSaves/<label>/…` + `steamidra_meta.json`), **rclone** (`rclone copy … --update --transfers 9 --checkers 18`, binario `third_party/rclone_linux/rclone` ELF i386 o el del setting, remotos por `rclone config` en un terminal), **Google Drive** API v3 (`drive.file`, token en claro en `gdrive_token.json`, `client_id` en `sff/core/_gc.py` gitignorado → "not available in this build"). Restaurar hace safety backup en `_restore_safety/<ts>/`. Auto backup por `SAVE_WATCHER_INTERVAL` (10 min) con `STEAM32_ID`.
- **Convivencia con Steam Cloud**: no toca `remotecache.vdf` ni la config de cloud del cliente; sobrescribe `remote/` y deja que Steam reconcilie.

### 2.9 Añadir y quitar un juego

**Entradas**: appid o nombre en el Store (catálogo local `store_metadata/` → `GetAppList` con clave Web API → espejos `jsnli/steamappidlist` + Hubcap `/library`,`/search`); appid directo (DDMod); `.lua`/`.zip`/`.rar`/`.7z` locales, recientes, bulk import (carpeta o drop zone, dedupe SHA-256; no `.rar/.7z` en el escaneo de carpeta); `-f <fichero>` y segunda instancia; URL de tienda **solo en la CLI** (`APP_ID_RE`, `lua/choices.py:50`); `midra://` registrado en Windows y **nunca procesado** (`uri_handler.py`: ningún llamador de `check_args_for_uri`). Nombre de carpeta: `installdir` del appinfo → nombre → `App_<id>` (DDMod, `download_bridge.py:2456-2464`); `sanitize_filename(nombre)` sin apóstrofes (`process_from_store`, `ui.py:1128-1131`); nombre de tienda saneado `[<>:"/\|?*]→_` (CLI Linux, `linux_download.py:75-78`).

**Qué escribe al añadir** (Linux, camino `process_from_store`/`process_lua_full`, `ui.py:952-1243`, `:791-950`): `AdditionalApps` (+DLC, `AppTokens`, `DlcData` ≥ 64) → `dlc_check` → `ManifestIds` si hubo versión → `patch_slssteam_config` (no-op con marcador) → `config.vdf` `DecryptionKey` (+ `.backup`) → `saved_lua/<app>.lua` + pins → **`install_lua_to_steam`** (`<steam>/config/stplug-in/<app>.lua`, matando y relanzando Steam) → manifests a staging + `depotcache` + `config/depotcache` → `run_download` a `<lib>/steamapps/common/<installdir>` → `write_acf_direct` (0444) + `patch_workshop_acf` (`NeedsDownload=0`) → `libraryfolders.vdf` `apps/<id>="1"` (creando la biblioteca si falta, `core/storage/vdf.py:151-171`). "Add to Library" en la web (`_bridge_run_linux_fastest`, `download_bridge.py:554-636`) usa el último manifest de `get_depots_for_app` y el mismo `process_from_store`. **Local import** (`:253-368`) solo registra (sin descarga). **DDMod** (`:2070-2990`) mata Steam si el destino es biblioteca, registra sin `install_lua_to_steam` ("LumaCore is Windows-only so the stplug-in copy never runs on Linux") y escribe `create_acf` + `move_manifests_to_depotcache`. Windows (`_bridge_run_windows_fastest`, `:371-551`): lua → `LumaCoreManager.add_ids` → `config.vdf` → `install_lua_to_steam` → sin ACF ("LumaCore owns app state") → `libraryfolders.vdf`; Steam descarga.

**Qué limpia al quitar.** Web `delete_game` (`misc_bridge.py:2710-2841`): `applist` = `remove_lua_from_steam` (lua de `stplug-in` + el stray `config/<id>.lua`; mata/relanza Steam) + `saved_lua/<id>.lua`; `full` = además `remove_additional_app(<appid>)`, `remove_manifest_id` por depot, purga de `<depot>_*.manifest` en staging, `depotcache` y `config/depotcache` ("Staging first, so a watcher can't refill depotcache mid-delete"), el ACF y `shutil.rmtree(game_path)` **sin comprobar** que esté bajo una biblioteca. CLI `remove_game_menu` (`ui.py:456-577`): `basic` / `full` (+ `remove_acf_and_manifests` por `MountedDepots`) / `full_keys` (+ `remove_decryption_keys`, el único camino que borra claves). **Queda**: los DLC en `AdditionalApps`, `DlcData`, `AppTokens`, `apps/<id>` en `libraryfolders.vdf`, claves en `config.vdf` (salvo `full_keys`), `config.vdf.backup`, `config.yaml.bak`, `steam.cfg`, compatdata/shadercache, `<lib>/steamapps/depotcache` (la descarga CLI escribe ahí y nadie lo mira), `00_LetUpdate_override.lua` por juego, entradas de la BD de claves, accesos directos.

**Varias bibliotecas y SD**: solo `<steam>/config/libraryfolders.vdf` con rutas existentes (`vdf.py:95-126`); picker solo en el camino de versión y en DDMod ("Custom folder (outside Steam, no ACF written)"); "Add to Library" siempre `steam_path`. `flat_file_repair` recorre `steamapps/common` de todas una vez al día.

### 2.10 Credenciales y proveedores

| Credencial | Cómo se obtiene | Dónde se guarda | Caducidad / detección | Sin ella |
|---|---|---|---|---|
| **Hubcap** (`smm…`, setting `morrenus_key`) | web de Hubcap ("It's free"; Discord obligatorio según el mensaje de error) | `settings.bin` cifrado (PyNaCl; clave maestra en keyring **y siempre** en `.sff_encryption_key` 0600) | validación al arrancar (3 s) y en cada descarga (`/user/stats`); 401 → `HUBCAP_KEY_DEAD` + diálogo "Remove Key / Open site"; cuota diaria del lua; 1500/día de manifests on-demand | Free Providers; Store sin `/library` |
| **Ryuu** reseller (`auth_code`) / premium (`X-Auth-Key`) | "Contact Ryuu staff" | ídem | sondeo al arrancar con appid 440; 401/403 → `RYUU_KEY_DEAD` | ídem |
| **DepotBox** (+ plan 60/120 req/min) | `depotbox.org` | ídem | `GET /api/direct-lua?appid=440`; 401 → clave borrada + `DEPOTBOX_KEY_DEAD` | ídem |
| **ManifestHub2** (24 h) | navegador a `manifesthub2.filegear-sg.me` + pegar | `MANIFESTHUB_API_KEY` + `MANIFESTHUB_KEY_EXPIRY` | reloj local y 403 | LuasTools / GitHub |
| **Steam Web API key** | opcional | setting, si no la **embebida** `1DD0450A99F573693CD031EBB160907D` (base64 en `strings.py:24-25`) | — | catálogo de GitHub; sin esquema de logros para Goldberg |
| **Cuenta Steam** | **ninguna** para descargar: siempre `anonymous_login` (`network/steam_client.py:148-158`, `native_downloader.py:485-490`) | `STEAM_USER`/`STEAM_PASS` solo para `generate_emu_config` con login (guardados en `settings.bin`) | — | request codes solo para lo que Steam sirva a anónimos |
| **DepotBox build token** | embebido ofuscado (`endpoints.py:828-855`) / `STEAMIDRA_BUILD_TOKEN` | código | — | sin "Automatic" por Build ID |
| **GitHub token** | `GITHUB_TOKEN`/`GH_TOKEN` del entorno | — | `X-RateLimit-Remaining` | 60 req/h |
| **SteamGridDB** | opcional (iconos) | setting | — | icono del CDN |
| **Google OAuth** | navegador | `gdrive_token.json` en claro | refresh; sin `_gc.py` no existe | sin Drive |
| **`sudo`** | el usuario, en Deck | — | — | sin `steam-jupiter`; headcrab falla en sus `apt`/`pacman` |

Auto-selección de fuente: Hubcap > Ryuu > DepotBox si hay clave viva, si no Free Providers (`app.js:768-789`). **Sin ninguna credencial** funciona: Free Providers, LuasTools y los tres espejos, la BD local y su refresco, el historial (CM anónimo, árbol GitHub), el descargador nativo con manifest local, DDMod, headcrab/hash fix, LumaCore y patrones, catálogos, cracks, Goldberg (con la clave embebida), cloud local.

### 2.11 Mantenimiento propio

- **Superficie**: GUI web (QtWebEngine, sandbox de Chromium **desactivado**, `LocalContentCanAccessRemoteUrls`, bridge `QWebChannel` sin autenticación con ~170 slots: borrar carpetas, lanzar ejecutables, abrir terminales, `set_setting` de cualquier clave, `open_url`), GUI Qt clásica, bandeja, CLI `Main.py` (menús InquirerPy, `-f/-b/--dry-run/-q`), instalador `steamidra_install.sh` (`install|uninstall`). Instancia única por `QLocalServer`. Sin QAM ni web remota.
- **Update de sí mismo** (`updater.py`, `ui.py:1259-1550`, `Main_gui.py:580-657`): `api.github.com/repos/drappula/SFF/releases/latest` (+ `/releases`, `/releases/tags/<tag>`), 2 s tras arrancar salvo `AUTO_UPDATE_CHECK=False`; diálogo Download/Skip/Later; descarga el `.zip` con "windows"/"linux" en el nombre **sin hash ni firma**, extrae con `safe_extract_zip`; Windows congelado → `tmp_updater.bat` (`taskkill`, `robocopy`); Linux congelado → abre un terminal con `bash steamidra_install.sh` del zip con entorno limpio; el AppImage no se reemplaza in situ.
- **Update de componentes y versión instalada**: *SLSsteam*: `VERSION` nunca escrito → siempre "update available" → headcrab cada hora (§2.1); `handle_linux_setup` muestra "Installed version: unknown"; no lee el `VERSION`/`version.txt` de SLSsteam ni hashea el `.so`. *LumaCore*: setting `LUMACORE_INSTALLED_VERSION` (tag de GitHub en el momento de instalar) o `lumacore_version` de `status.json`; check cada 6 h; `drappula/LumaCore` V37+ ("Versioning continues above upstream's V36" para que las instalaciones existentes actualicen). *.NET 9*: `ensure_dotnet_9` en el setup, el instalador y 6 s tras arrancar la GUI. *gbe_fork*: `fix_game_cache/goldberg/version.txt`; solo consulta GitHub si falta. *DLC unlockers*: los del repo tienen prioridad. *Steamless, SteamAutoCrack, coldloader, rclone, fzf, DDMod*: fijos en `third_party/`, sin versión comprobada. *headcrab*: siempre `main`. *BD de claves*: cada 6 h; *catálogo*: 24 h.
- **Salud, estado, logs**: `debug.log` (DEBUG, en `sff_data_dir`), `crash.log`, ventana de logs, log en vivo en la web; `analytics.json` local; Linux: `is_installed()` = existe `SLSsteam.so`, sin lectura del log ni del estado de SLSsteam; Windows: banner de `status.json`. Marcadores `.headcrabd`, `.sm_migrated`. Barrido de restos del updater al arrancar; limpieza de memoria horaria; reparación de ficheros planos a los 18 s.
- **Build/CI**: `release.yml` (tag `v*` → AppImage + zip Linux, NSIS + zip Windows; `<ver>` de `strings.py`, nunca del tag; sin checksums ni firma; `release-note.md` como cuerpo). Fork: zlib sólido (−8 min), caché del venv, AppImage suelto desde 6.9.0, `fetch_store_metadata.py` antes de cada build.

### 2.12 Proyecto

- **Plataformas**: Windows 10/11 x64; Linux x86_64 (AppImage glibc 2.39; "Tested on CachyOS, Arch, Debian 12/13, Ubuntu 24.04, Fedora 41, Steam Deck Desktop Mode" según su doc, no verificable aquí); Flatpak Steam parcial (§2.1); sin macOS.
- **Código abierto, licencia, quién, ritmo**: GPL-3.0; §0. Upstream 44 tags en 88 días y parado desde 08-21; fork 88 commits y 7 tags entre 09-04 y 10-03, ninguna versión 6.7.x, `v6.6.7c` huérfano; `AGENTS.md`: desarrollo con agente de IA, CHANGELOG obligatorio. Sin tests en el repo (gitignorados).
- **Servicios de terceros de los que depende** (completo en §3.1): GitHub raw/API (AceSLS/SLSsteam, drappula/LumaCore, drappula/SFF, Deadboy666/h3adcr-b y h3adcr-b-modul3s y SteamTracking (vía headcrab), michelegoku3/MigoReleases, KoriaPolis/Steam-Depot y CrakFiles, fylsdy/ManifestHub, steamtoolsapp/ManifestHub, steamtools-games/ManifestHub3, qwe213312/mejikuhibiniu1/Sainan k25FCdfEOoEJ42S6, Detanup01/gbe_fork, acidicoala/*, SteamTools-Team/GameList, jsnli/steamappidlist, ip7z/7zip), jsDelivr (headcrab), `headcrab.pages.dev`, `manifest.luastools.xyz`, `hubcapmanifest.com`, `generator.ryuu.lol`, `depotbox.org`, `api.manifesthub2.filegear-sg.me`, `api.luagen.revobd.club`, `api.steamcmd.net`, `steamcmd.morrenus.net`, Cloudflare R2, worker `stea-provider-api.steamidra.workers.dev`, Valve (CM anónimo, CDN, store API, Web API con clave compartida, charts, SteamGridDB), SteamDB (desactivado), `pixeldrain-bypass.gamedrive.org`/pixeldrain, `online-fix.me`/Google/Bing, `dot.net`/`aka.ms`, Google OAuth/Drive, `yesyes0649/steamnetsock-patch` (vía headcrab).
- **Telemetría y privacidad**: `analytics.py` es local; la **contribución de claves** sube a un worker del proyecto el contenido de todos los luas del usuario (ids, claves, nombres, `tool_version`) y en la práctica nace activada (§2.4); clave Web API compartida a Valve en cada fix; nombre del juego a Google/Bing/online-fix.me con UA falso; sesiones CM anónimas desde la IP del usuario; UAs `SteaMidra/1.0`…`SteaMidra/6.6` desincronizados; `google-site-verification` en el README; contraseña de Steam en `settings.bin` si se usa `generate_emu_config` con login; token de Google en claro.
- **Ejecución de código remoto**: `bash` de un script de la rama `main` de GitHub filtrado por heurísticas de texto, dos `curl \| bash` desde `headcrab.pages.dev` sin filtro, `sudo` dentro de ambos; descargas de GitHub/pixeldrain/`7zr.exe`/`dotnet-install` sin hash (Windows con `CERT_NONE`); extracción de cracks de terceros sobre el juego.
- **Riesgo de detección**: nada lo trata. Huellas en disco: `LD_AUDIT` en `steam.sh` y `steam-jupiter`, `steam.cfg` inhibiendo el bootstrapper, `client.sh` de headcrab, `DecryptionKey` en `config.vdf`, ACF 0444 con `LastOwner`, `libraryfolders.vdf` editado, `.desktop` con `env LD_AUDIT`, SteamID64 compartido en Goldberg (`76561198001737783`).

---

## §3 Superficies externas

### 3.1 Hosts y URLs (todas las que el código llama; las de headcrab marcadas)

| URL / host | Fichero:línea | Uso |
|---|---|---|
| `https://raw.githubusercontent.com/Deadboy666/h3adcr-b/refs/heads/main/headcrab.sh` | `linux/slssteam.py:337,412` | instalador de SLSsteam (filtrado y ejecutado con `bash`) |
| `https://headcrab.pages.dev/reset`, `https://headcrab.pages.dev` | `linux/slssteam.py:460-461,500-504,544-548` | hash fix (`curl \| bash`, sin filtro) |
| **headcrab**: `cdn.jsdelivr.net/gh/Deadboy666/h3adcr-b-modul3s@{main,cr-test}/{headcrab_native.sh,headcrab_flatpak.sh,stable-sources.txt,headcrab.desktop,headcrab.png}`, `cdn.jsdelivr.net/gh/Deadboy666/SteamTracking@{headcrab/ClientManifest/steam_client_ubuntu12,…steamdeck_stable_ubuntu12,master/ClientExtracted/steam.sh}`, `github.com/Deadboy666/h3adcr-b-modul3s/raw/…/{dgsc,dlm}`, `github.com/AceSLS/SLSsteam/releases/{latest,download/<tag>/SLSsteam-Any-release.7z}`, `cdn.jsdelivr.net/gh/yesyes0649/steamnetsock-patch@builds/fix.so`, `http://localhost:1666/` | `headcrab.sh:21-37,739-760` | sustituto de `steam.sh`, manifest del cliente pineado, downgrade, SLSsteam, netsock |
| `https://api.github.com/repos/AceSLS/SLSsteam/releases/latest` | `linux/slssteam.py:319` | `tag_name` (cada arranque y cada hora en la GUI) |
| `https://api.github.com/repos/drappula/LumaCore/releases/latest` + `browser_download_url` | `lumacore/lumacore_setup.py:126,398,663` | LumaCore (Windows) |
| `https://raw.githubusercontent.com/michelegoku3/MigoReleases/pattern/{steamclient,steamui,steamclientipc}/<sha256>.toml` | `lumacore_setup.py:163,298-311` | patrones de LumaCore |
| `https://api.github.com/repos/drappula/SFF/releases{,/latest,/tags/<v>}`, `https://github.com/drappula/SFF/releases/` | `updater.py:55-56,122`; `core/strings.py:31` | self-update |
| `https://manifest.luastools.xyz/m/{depot}/{gid}` | `manifest/downloader.py:54,430-447` | primera fuente de manifests |
| `https://hubcapmanifest.com/api/v1/{user/stats,library,search,games,status/{id},manifest/{id},lua/{id},generate/manifest,generate/workshopmanifest/{id}}` | `lua/endpoints.py:124,150`; `downloader.py:347-357`; `network/store_browser.py:31,…` | luas, manifests on-demand (1500/día), Store |
| `https://raw.githubusercontent.com/{qwe213312,mejikuhibiniu1,Sainan}/k25FCdfEOoEJ42S6/main/{depot}_{gid}.manifest`; `https://api.github.com/repos/qwe213312/k25FCdfEOoEJ42S6/git/trees/main?recursive=1` y `/commits?path=` | `downloader.py:56-60,381-428`; `depot_history.py:36-38,412,450-458` | espejos de manifests; historial |
| `https://api.manifesthub2.filegear-sg.me/manifest?apikey=&depotid=&manifestid=`; `https://manifesthub2.filegear-sg.me` | `downloader.py:454-459`; `manifest/manifesthub_key.py:30,83` | ManifestHub2 (clave 24 h) |
| `https://generator.ryuu.lol/{secure_download,resellerlua,api/download/{app},requestbranch,resellerrequestupdate,requestupdate,api}` | `endpoints.py:396-399,425`; `gui/bridges/misc_bridge.py:847,1431-1440`; `gui/web_bridge.py:1001-1010,527` | Ryuu |
| `https://depotbox.org/api/direct-lua?appid=`, `/api/depotboxtool/v1/build-details?build_id=`, `https://depotbox.org` | `endpoints.py:780,862`; `web_bridge.py:525,1029` | DepotBox |
| `https://api.steamcmd.net/v1/info/{app}` | `network/steam_client.py:264-290`; `endpoints.py:518-520` | appinfo **antes** del CM |
| `https://raw.githubusercontent.com/fylsdy/ManifestHub/main/depotkeys.json` (404) | `endpoints.py:479,499` | claves "trionine" |
| `https://api.luagen.revobd.club/{app}.zip` | `endpoints.py:708`; `download_bridge.py:1886` | lua + manifests |
| `https://raw.githubusercontent.com/{steamtoolsapp/ManifestHub,steamtools-games/ManifestHub3}/{app}/{app}.lua` y `/{app}/{d}_{gid}.manifest` | `endpoints.py:481-484,540-542,724` | luas y manifests gratis |
| `https://raw.githubusercontent.com/KoriaPolis/Steam-Depot/main/fallback_depotkeys.json`; `https://pub-d3ba7941fdf24c2c84da530b93221e1c.r2.dev/fallback_depotkeys.json` | `lua/provider.py:38-41,344` | BD de claves (6 h) |
| `https://stea-provider-api.steamidra.workers.dev/submit` (POST) | `provider.py:42,708` | contribución de claves (3 h) |
| `https://steamcmd.morrenus.net/api/{app}` | `depot_history.py:555`; `game/fix_game/config_generator.py:68,694,718,746` | ids de depot; idiomas/depots/DLC para Goldberg |
| Steam CM (lib `steam`, anónimo), CDN `{host}/depot/{d}/manifest/{gid}/5/{code}{token}`, `/depot/{d}/chunk/{sha}{token}`, `steampipe.akamaized.net` | `steam_client.py:85,154`; `downloads/native_downloader.py:485-492,529,760,361` | appinfo, request codes, descarga |
| `https://store.steampowered.com/api/appdetails`, `/app/{id}/`, `/charts/topselling`; `https://api.steampowered.com/{IStoreService/GetAppList/v1,IStoreBrowseService/GetItems/v1,ISteamUserStats/GetSchemaForGame/v2,ISteamRemoteStorage/GetCollectionDetails/v0001}`; `shared.steamstatic.com`, `cdn.cloudflare.steamstatic.com`, `cdn.akamai.steamstatic.com` (carátulas) | `network/http_utils.py:162`; `steam_store.py:66,115`; `game_list_fallback.py:704`; `lua/choices.py:51`; `store_bridge.py:466,767,988`; `config_generator.py:69`; `manifest/collections.py:27`; `image_cache.py:38`; `desktop_shortcuts.py:34` | nombres, DRM check, catálogo, esquemas |
| `https://www.steamgriddb.com/api/v2/games/steam/{appid}`, `/icons/game/{id}` | `desktop_shortcuts.py:35,78-121` | icono alternativo |
| `https://raw.githubusercontent.com/SteamTools-Team/GameList/refs/heads/main/games.json`; `…/jsnli/steamappidlist/refs/heads/master/data/{games_appid,software_appid}.json`; `https://cdn.jsdelivr.net/gh/jsnli/steamappidlist@master/data/dlc_appid.json` | `game_list_fallback.py:80-95`; `choices.py:138-142`; `tools/fetch_store_metadata.py:33-46`; `store_bridge.py:495-496` | catálogo offline |
| `https://raw.githubusercontent.com/KoriaPolis/CrakFiles/main/crackfiles.json`; `https://pixeldrain-bypass.gamedrive.org/api/proxy.json`; `https://pixeldrain.com/{api/file/<id>?download,u/<id>}` | `game/crack_fix.py:31`; `web_bridge.py:354`; `network/pixeldrain.py:43,286,297` | cracks |
| `https://api.github.com/repos/{Detanup01/gbe_fork,alex47exe/gse_fork,Detanup01/gbe_fork_tools,acidicoala/{SmokeAPI,CreamAPI,UplayR1Unlocker,UplayR2Unlocker}}/releases/latest`; `https://github.com/ip7z/7zip/releases/latest/download/7zr.exe` | `fix_game/goldberg_updater.py:38,378`; `gse_tool_updater.py:43,46`; `dlc_unlockers/downloader.py:37-42` | emulador, unlockers, 7zr |
| `https://online-fix.me/…`, `https://www.google.com/search?q=site:online-fix.me/games…`, `https://www.bing.com/search?q=…`, `https://discord.gg/ZJx6seG` | `game/online_fix.py:39-40,221-222,235,253` | búsqueda |
| `https://dot.net/v1/dotnet-install.{sh,ps1}`; `aka.ms/dotnet/9.0/…`, `aka.ms/vs/17/release/vc_redist.*` | `downloads/dotnet_utils.py:94,159`; `steamidra_install.sh:170`; `installer.nsi` | .NET 9, VC++ |
| Google OAuth/Drive v3 (`drive.file`), remotos rclone | `cloud/google_drive.py:28-31,43,93`; `cloud/cloud_saves.py:1131-1168` | backups |
| `https://googlechromelabs.github.io/chrome-for-testing/…`; `https://{www.,}steamdb.info/…` | `depot_history.py:979,993,604-1716`; `store/older_version.py:68` | SteamDB (desactivado salvo cookie / navegador embebido) |
| `https://openlua.cloud/` | `manifest/manifestor.py:38` | sin llamadores |
| `https://discord.gg/{steamidra,hubcapsmanifest,depotbox,manifests,hubcap}` | `webui/index.html:81,1133`; `settings.js:535-539`; `endpoints.py:192` | enlaces |

Solo en comentarios o requirements (sin código): `st-gmrc.kur0.deno.dev`, Tor, `steam.run`, `wudrm`, `manifest.opensteamtool.com`, `cdn.jsdelivr.net/gh/KoriaPolis/Steam-Auto-PT@pattern`, buzzheavier.

### 3.2 Ficheros en disco (Linux salvo que se diga Windows)

| Ruta | Quién | Qué |
|---|---|---|
| `<steam>/steam.sh` | headcrab (sustituye, 555) → `patch_steam_sh` (644, línea 11 `export LD_AUDIT=…`) | enganche |
| `<steam>/client.sh` | headcrab | el `steam.sh` de Valve guardado |
| `<steam>/steam.cfg` | `create_steam_cfg`, headcrab, tile "Steam Updates" | `BootStrapperInhibitAll`, `BootStrapperForceSelfUpdate` |
| `<steam>/package/*.manifest` | headcrab | lectura de versión; downgrade |
| `/usr/bin/steam-jupiter`, `.bak` | `patch_steam_jupiter` (`sudo`) | Game Mode |
| `~/.local/share/SLSsteam/{SLSsteam.so,library-inject.so}` (o `~/.var/app/com.valvesoftware.Steam/.local/share/SLSsteam/`) | headcrab | el `.so` (SFF solo comprueba que exista) |
| `~/.config/SLSsteam/config.yaml` (+ `.bak`, `.tmp`), `.headcrabd`, `.sm_migrated`, `tools/netsock/netsock.so` | headcrab (`editconfig`, merge de `res/config.yaml`), `yaml_config.py`, `_add_to_slssteam` (`yaml.dump`), `migrate_existing_games` | §3.4 |
| `~/.local/share/SteaMidra/SLSsteam/VERSION` | **nadie** (solo lectura) | versión "instalada" |
| `<steam>/config/stplug-in/<appid>.lua` (+ stray `<steam>/config/<appid>.lua`), `stplug-in/00_LetUpdate_override.lua`, `stplug-in/<appid>/00_LetUpdate_override.lua` (Windows) | `install_lua_to_steam`, `download_dlc_free` (append), `update_pins`, helpers LetUpdate | fuente que SLSsteam/LumaCore leen |
| `<steam>/config/config.vdf` (+ `.backup`) | `ConfigVDFWriter` | `depots/<id>/DecryptionKey` |
| `<steam>/depotcache/`, `<steam>/config/depotcache/`, `<lib>/steamapps/depotcache/` (CLI Linux) | `ManifestDownloader`, `zip.py`, `_seed_free_manifest`, `move_manifests_to_depotcache`, preserver | manifests |
| `<sff_data>/manifests/` | staging canónico ("IS the backup") | restauración |
| `<lib>/steamapps/appmanifest_<appid>.acf` (0444), `appworkshop_<appid>.acf` | `create_acf`, `ACFWriter`, `lure_fix_acf`, `_sync_acf_downgrade`, `download_dlc_free` | registro |
| `<steam>/config/libraryfolders.vdf`, `<steam>/userdata/<sid3>/config/localconfig.vdf` (`-onlinefix`, Windows), `loginusers.vdf` (lectura), `<steam>/userdata/<sid>/<app>/remote/` | `ensure_library_has_app`, `launch_options.py`, cloud saves | |
| `<steam>/lumacore/{pattern/*.toml,status.json}`, `<steam>/{dwmapi,xinput1_4,LumaCore,LumaCorePayload}.dll`, `bin/lcoverlay.dll` (Windows) | `lumacore_setup.py`, NSIS | LumaCore |
| `<sff_data>/` = carpeta del ejecutable/`$APPIMAGE` (en Linux instalado, `~/.local/share/SteaMidra/`): `settings.bin`, `.sff_encryption_key`, `api_cache.json`, `saved_lua/`, `backups/`, `manifests/`, `provider_cache/{fallback_depotkeys.json,contributor_state.json,trionine_depotkeys.json}`, `fix_game_cache/`, `download_queue.json`, `acf_pending_queue.json`, `download_history.json`, `analytics.json`, `recent_files.json`, `store_metadata/`, `all_games.txt`, `debug.log`, `crash.log`, `sac_runtime/`, `sac_config/`, `gdrive_token.json`, `.bulk_import_drop/`, `webui_custom/`, `update.zip`, `tmp_update/` | varios | datos de la app |
| `~/.sff/{depot_cache_<app>.json,mirror_tree_cache.json,cf_cookie_cache.json,chrome-for-testing/}`, `~/.local/share/SteaMidra/{SLScheevo/,gse_tool/,depots/}`, `~/SteaMidra/image_cache` (Linux, por `APPDATA` vacío), `~/.dotnet/`, `~/.local/bin/steamidra`, `~/.local/share/applications/{steamidra,<juego>}.desktop`, `~/.local/share/icons/hicolor/256x256/apps/`, `~/.bashrc`/`.zshrc` (`DOTNET_ROOT`), `~/.local/share/GSE Saves/`, `~/Desktop/<juego>.desktop`, `<tmp>/{headcrab_install.sh,mistwalker_keys.vdf,mistwalker_manifests/}` | varios | |
| En el juego: `steam_settings/`, `steam_appid.txt`, `*.bak`, `*.steamstub.bak`, `*.steamlocked.bak`, `OG_*`, `*_o.dll`, `SmokeAPI.config.json`, `cream_api.ini`, `ColdClientLoader.ini`, `coldloader.{ini,dll}`, `version.dll`/`winmm.dll`, `steamclient*.dll`, `Launch*.bat`, `launch*.sh`, `LUTRIS_SETUP.txt`, `.steamidra_exe_backups/`, ficheros del crack | `game/` | fixes |

### 3.3 Variables de entorno

`LD_AUDIT` (escrita en `steam.sh`, `steam-jupiter`, `Popen`, `.desktop`); `LD_LIBRARY_PATH`, `LD_PRELOAD`, `PYTHONHOME`, `PYTHONPATH`, `QT_PLUGIN_PATH`, `QML2_IMPORT_PATH`, `APPIMAGE`, `APPDIR`, `OWD`, `ARGV0`, `APPIMAGE_UUID` (**eliminadas** al lanzar `steam`, el instalador y `dotnet-install`; para headcrab solo `LD_LIBRARY_PATH`/`LD_PRELOAD`); `XDG_CONFIG_HOME` (solo `get_user_config_path`), `XDG_DATA_HOME`; `APPIMAGE` (`sff_data_dir`); `DOTNET_ROOT`; `QTWEBENGINE_DISABLE_SANDBOX=1`, `QTWEBENGINE_CHROMIUM_FLAGS`; `GITHUB_TOKEN`/`GH_TOKEN`; `STEAMIDRA_BUILD_TOKEN`; `HTTPS_PROXY`/`HTTP_PROXY`/`ALL_PROXY` (saneadas); `GSE_CFG_USERNAME`/`GSE_CFG_PASSWORD` (para `generate_emu_config`); `SFF_VERBOSE_FILTER`; `SKIPVENV`, `XDG_CURRENT_DESKTOP` (instalador); `APPDATA`, `LOCALAPPDATA`, `WINDIR` (Windows).

### 3.4 Claves de `config.yaml` de SLSsteam que SteaMidra escribe (y sus valores)

| Clave | Valor | Dónde | Cuándo |
|---|---|---|---|
| `SafeMode` | `no` | `linux/slssteam.py:266`; plantilla `yaml_config.py:359` | `patch_slssteam_config` **solo sin `.headcrabd`** (tras headcrab nunca); `_init_config_with_app` si no hay `config.yaml` |
| `SafeMode` | `yes` | `misc_bridge.py:1871-1882` | botón "Patch Gaming Mode" (`update_yaml_boolean_value`) |
| `WarnHashMissmatch` | `no` | `:267`; plantilla | ídem (el CHANGELOG 6.5.8 decía `yes`) |
| `NotifyInit`, `Notifications` | `yes` | `:268-269` | ídem |
| `DisableUpdates`, `DisableCloud`, `DisableFamilyShareLock` | `yes` | `:270-272`; plantilla; campos requeridos | ídem |
| campos "requeridos" si faltan: `FakeName: ""`, `FakeEmail: ""`, `FakeWalletBalance: 0`, `UseWhitelist: no`, `SteamIdOverride:`, `MaxSchemaTries: 10`, `API: no`, `LogLevel: 2`, `DumpClientInterfaces: no`, `ExtendedLogging: no` (+ los anteriores) | añadidos al final | `yaml_config.py:402-441` | `patch_slssteam_config` (sin marcador), `add_additional_app` si falta `FakeName:`, `migrate_existing_games` |
| plantilla completa (sin `config.yaml`): además `AppIds:`, `AdditionalApps:`, `DlcData:`, `AppTokens:`, `FakeOffline:`, `FakeAppIds:`, `ManifestIds:`, `DepotBlacklist:`, `IdleStatus: {AppId: 0, Title: ""}`, `GameTitles:`, `SubscriptionTimestamps:`, `DenuvoGames:` | | `yaml_config.py:336-377` | "official SLSsteam default template" |
| `AdditionalApps` | `  - <appid>   # <comentario>` | `yaml_config.py:380-399,444-510` | `add_ids`, `migrate_existing_games`, `_add_to_slssteam`; borrado por `remove_additional_app` |
| `AppTokens` | `  <appid>: <token>` | `:517-568`; `linux_download.py:160-175` (`yaml.dump`) | `addtoken` del lua |
| `ManifestIds` | `  <depot>: <gid>` | `:591-661` | versión elegida; borrado al quitar "full" |
| `DlcData` | `  <padre>:\n    <dlc>: "<nombre>"` (o `{}` con depots en el camino CLI) | `:693-771`; `linux_download.py:181-194` | ≥ 64 DLC |
| `FakeAppIds`, `API: yes` | — | `:829-907`, `:327-329` | **sin llamadores** |
| `PlayNotOwnedGames` | — | — | no se escribe (6.6.0 → 6.6.1) |
| (headcrab, `editconfig`) `SafeMode`, `NotifyInit`, `LogLevels` | `yes`/`no` por distro; `NotifyInit: yes` (SteamOS); `LogLevels: 0x3f` (CachyOS); merge de `res/config.yaml` del release | `headcrab.sh:835-850,598-705` | en cada ejecución de headcrab |

Settings propias relevantes de `settings.bin` (nombre `key_name`): `morrenus_key` (Hubcap), `ryuu_key`, `ryuu_api_key`, `depotbox_key`, `manifesthub_api_key`, `steam_web_api_key`, `steam_user`, `steam_pass`, `steam32_id`, `steam_path`, `sls_config_loc`, `use_manifest_pins`, `manifest_preserve` (on), `provider_contribute_keys` (on por migración), `provider_enrich_steam_metadata`, `download_concurrency` (32), `download_queue_concurrency` (3), `global_update_check` (off), `auto_enable_updates_new_games` (off), `auto_update_check` (on), `cloud_*`, `save_watcher_interval` (10), `linux_guide_shown`, `lumacore_*`. Aquí solo las que tocan Steam o proveedores.

### 3.5 Comandos externos

`bash <tmp>/headcrab_install.sh`; `bash -c "curl -fsSL https://headcrab.pages.dev{/reset,} \| bash"`; `pkill -f dlm`, `pkill -f dgsc`, `pkill -f wget`; `pidof steam`; `steam` (Popen); `psutil.Process.kill()` (SIGKILL) sobre `steam`; `sudo cp`/`sudo chmod 755` sobre `/usr/bin/steam-jupiter`; `<dotnet> Steamless.CLI.dll -f <exe> --quiet --realign` (y `--exp --realign --recalcchecksum` en Fix Game), `wine Steamless.CLI.exe`; `<dotnet> DepotDownloaderMod.dll …`; `SteamAutoCrack.CLI.exe crack <dir> --appid <id>` (sin `wine`); `<python> SLScheevo.py …`; `generate_emu_config[.exe]`; `rclone copy|copyto|lsd|listremotes|dedupe|config`; terminales (`x-terminal-emulator|gnome-terminal|konsole|xterm|xfce4-terminal|lxterminal`); `7z|7zz|unrar|WinRAR`, `7zr.exe`, `tar xjf`; `zstd`; `pkill -x <exe>` / `taskkill /F /IM` de cada exe del juego; `pkill -f chrome-for-testing|chromedriver`; `powershell` (`.lnk`); `notify-send`, `gtk-update-icon-cache`, `update-desktop-database`, `python3 -m venv`, `pip` (instalador); `libc.prctl(15, "SteaMidra")`. Dentro de headcrab: `sudo apt-get|pacman|xbps-install`, `killall steam`, `wget`, `curl`, `7z x`, `dgsc --port 1666`, `flatpak run`.

---

## §4 Lo que nos afecta

### 4.1 Hallazgos pendientes de decisión

Aparcados el 2026-10-08.
Numeración propia de este doc; no son los F1–F4 del doc anterior.

1. **Una Deck que venga de SteaMidra llega con el cliente rebajado y congelado, `steam.sh` sustituido y `config.yaml` ajeno.** headcrab deja el cliente en `1788652215` (`HeadcrabCompatibleClientVer`) y `steam.cfg` con `BootStrapperInhibitAll=enable` + `BootStrapperForceSelfUpdate=disable` que nadie borra; `steam.sh` es el de `h3adcr-b-modul3s`, en 644 tras SFF, con el de Valve en `client.sh`; `config.yaml` trae `AdditionalApps` con todo lo instalado (`migrate_existing_games`), `DisableUpdates: yes`, `DisableCloud: yes`, `DisableFamilyShareLock: yes`, `SafeMode` según distro/botón, `NotifyInit: yes`, `API: no`, y `.headcrabd`. Nuestro `setup.sh` escribe el wrapper y no toca `steam.sh` (`nosotros.md` §2.1): habría que decidir si detectamos `client.sh`/`steam.cfg`/`.headcrabd` y qué ofrecemos (restaurar `client.sh` como `steam.sh`, borrar `steam.cfg`, limpiar `AdditionalApps`). Hoy no lo hacemos.
2. **headcrab se reejecuta cada hora** en cualquier máquina con SteaMidra abierta y Steam cerrado (§2.1): cada pasada hace `killall steam` (dentro de headcrab), `pkill -f wget`, rebaja el cliente si no es `1788652215`, sustituye `steam.sh` y reaplica `LD_AUDIT`. Si una Deck tiene a la vez SteaMidra y nuestra pila, SteaMidra gana cada hora. Dato para `maintenance.md` (coexistencia) y para el diagnóstico de "Steam vuelve a una versión vieja".
3. **El ACF 0444 con `LastOwner` del setting y `AutoUpdateBehavior 0`**, más `DisableUpdates: yes`, es el modelo de "freeze" de SteaMidra. Nuestro freeze deja a Steam escribir (`nosotros.md` §2.5). Sin cambio, pero es el estado en el que encontraremos los juegos de una Deck migrada: ACF que Steam no puede reescribir (confirmación del P26 anterior, ahora con línea: `acf_writer.py:124`).
4. **Consumidor puro de `manifest.luastools.xyz`** (primera fuente, sin donación, sin clave): cuarto consumidor del archivo (moon, luatools-moon, SLSDeck, SFF) y cero donantes nuevos. Dato para D20 (`nosotros.md`).
5. **La contribución de claves nace activada** por la migración de settings (§2.4) y sube los luas de `stplug-in` enteros al worker `stea-provider-api.steamidra.workers.dev`: si un usuario de SteaMidra tiene nuestros `.lua` en `stplug-in` (no los escribimos ahí; LumaDeck usa `keys.txt`), no pasa nada; si copió luas de Hubcap ahí, los sube. Sin acción; anotado para la pregunta de privacidad de `nosotros.md` §2.12.
6. **La base `KoriaPolis/Steam-Depot`** sigue viva (200 hoy) y es la única fuente de claves "gratis" que SFF tiene operativa (fylsdy 404). Sigue siendo "candidata, no acción" (§4.4 P22); el dato nuevo es que SFF ya no la empaqueta: la descarga en runtime como haríamos nosotros.
7. **Nombre de proceso `SteaMidra`** (`prctl`), `.desktop` `steamidra`, `QLocalServer "SteaMidra-v1"`, `~/.local/share/SteaMidra/`, `~/.sff/`: huellas para detectar SteaMidra en una Deck desde LumaDeck si algún día hace falta un aviso de coexistencia.
8. **SLScheevo** se distribuye como script Python dentro de SteaMidra (`third_party/linux/slscheevo/`) y escribe en `~/.local/share/SteaMidra/SLScheevo/`; no está leído (57 KB). Si queremos saber qué hace con `UserGameStats_*.bin`, hay que leerlo (fuera del alcance de este doc; SteaMidra solo lo invoca).

### 4.2 Dependencias y qué rompe si cambia

| Supuesto de SteaMidra | Dónde | Qué rompe |
|---|---|---|
| `steam.sh` es editable y Steam lo ejecuta; una línea `export LD_AUDIT=…` basta | `linux/slssteam.py:111-133` | si Steam reescribe `steam.sh` (headcrab lo sustituye, SFF lo deja en 644), la inyección se pierde hasta el próximo headcrab (una hora); si `/usr/bin/steam` exigiera `-x`, no arranca |
| `/usr/bin/steam-jupiter` tiene un `exec` final y es editable con `sudo` | `:191-212` | SteamOS lo restaura en cada update; sin reaplicación automática |
| `steam.cfg` sigue siendo respetado por el bootstrapper | `:238`; `headcrab.sh:852-862` | si deja de serlo, el cliente se actualiza y SLSsteam sin SafeMode puede romper Steam; si sigue, el cliente queda en `1788652215` indefinidamente |
| el proceso se llama `steam` y está en PATH; `Popen` con `LD_AUDIT` inyecta | `steam_process.py:108-112,218,265` | Flatpak o lanzadores que limpian el entorno → Steam vanilla con "SUCCESS" |
| `config.yaml` con claves de nivel superior y listas `  - id`; secciones delimitadas por la siguiente línea que empieza por letra | `yaml_config.py:43-52,116-129` | claves nuevas de SLSsteam con otra forma rompen los límites de sección; `yaml.dump` del camino CLI reordena y cambia `yes/no` |
| SLSsteam acepta `DlcData` con `{}` y depots como hijos | `linux_download.py:191` | desconocido |
| `api.github.com` sin token (60 req/h) | `slssteam.py:319`; `updater.py`; `depot_history.py` | con 403 no hay check de SLSsteam (y headcrab no se reejecuta), ni self-update |
| `headcrab.sh` de `main` sigue teniendo la misma forma textual | `_cr_filter` `:340-373` | una reordenación deja pasar CloudRedirect o rompe la sintaxis; el hash fix ya corre sin filtro |
| `HeadcrabCompatibleClientVer` se mueve cuando headcrab lo decida | `headcrab.sh:5` | el día que cambie, todos los usuarios Linux de SteaMidra bajan (o suben) de cliente en la siguiente hora con Steam cerrado |
| `manifest.luastools.xyz` sin clave ni versión como primera fuente | `downloader.py:54` | si cambia de política, GitHub (snapshot) → ManifestHub2 (clave) |
| `fylsdy/ManifestHub/depotkeys.json` | `endpoints.py:479` | ya 404: trionine solo con la BD local |
| `KoriaPolis/Steam-Depot` / R2 | `provider.py:38-41` | la BD solo crece con luas propios |
| `api.steamcmd.net` primero para todo appinfo | `steam_client.py:386-387` | sin él y sin CM: `buildid` "0", sin GIDs automáticos |
| request code solo de sesión anónima | `native_downloader.py:302-319` | depende de qué sirva Steam a anónimos; el resto, DDMod con manifest local |
| .NET 9 en `~/.dotnet` o PATH | `dotnet_utils.py` | sin él, no hay DDMod ni Steamless en Linux (`process_from_store` lo exige, `ui.py:1124-1127`) |
| `inotify_simple` instalado | `preserver.py:285-291` | no está en `requirements-linux.txt`: polling 5 s |
| `drappula/LumaCore` releases y `MigoReleases@pattern` | `lumacore_setup.py:125,163` | Windows: sin DLL / banner "No cached LumaCore support data" |
| AppImage de ubuntu-24.04 (glibc 2.39) | `release.yml` | distros con glibc anterior quedan fuera; no documentado |

### 4.3 Lo que SteaMidra tiene y nosotros no (y al revés)

| Función | Ellos | Nosotros | Notas |
|---|---|---|---|
| 1 | headcrab (instalación de SLSsteam con rebaja y congelación del cliente), parche de `steam.sh`, botón `sudo` sobre `steam-jupiter`, hash fix por `curl \| bash`; sin crash guard | wrapper propio (`.desktop` + drop-in systemd), `steam.sh` vanilla, guard anti crash-loop, feed de RVAs; nunca `/usr` | decisión F1 del doc anterior, vigente (§4.4 P1) |
| 2 | `AdditionalApps` con todo lo instalado (poseído o no) + `AppTokens`; sin packages | package-0 en memoria + `NotifyLicensesUpdated` | `nosotros.md` §2.2 |
| 3 | todos los `dlcappid` a `AdditionalApps`; `DlcData` ≥ 64; nunca se limpian; Goldberg/SmokeAPI/CreamAPI fuera de Steam | `DisabledDLC` de Steam por LumaDeck | |
| 4 | cuatro proveedores de lua, BD de claves pública (KoriaPolis) con contribución, LuasTools → Hubcap/GitHub → ManifestHub2, descargador nativo propio (sesión anónima, CDN), DDMod | `keys.txt` + `config.vdf`, depotcache → archivo propio → luastools → Hubcap → zip; códigos GMRC con 20770407/manifestdex/wudrm/steam.run; Steam descarga | ellos no tienen GMRC externos (`9033611`); nosotros no tenemos descargador propio ni BD pública |
| 5 | ACF a mano 0444 + `DisableUpdates: yes` + `ManifestIds` (si versión) + `steam.cfg`; "Update" = volver a bajar; "Lure Fix" | `pins.py` + `api.steamcmd.net`, Fix Update, Steam escribe el ACF | §4.4 P19, P26 |
| 6 | Fix Game completo (Goldberg/ColdClient/ColdLoader/Steamless/SteamAutoCrack/CrakFiles), DLC unlockers | lua.tools, Steamless, Goldberg, EOS por botón | ellos: Denuvo = parar |
| 7 | SLScheevo (script externo) en Linux; nada propio | nativo (lumalinux) | |
| 8 | backup/restore (local/rclone/Drive); CloudRedirect recortado a propósito | CloudRedirect | |
| 9 | Store con catálogo + Hubcap, bulk import, versiones por historial/DepotBox/HTML de SteamDB; borrado "full" con `rmtree` | QAM/LumaDeck | |
| 10 | claves validadas al arrancar con diálogo "Remove Key"; Web API embebida; DepotBox token embebido | `_HUBCAP_ATTEMPTS` sin diálogo | |
| 11 | self-update por GitHub (sin firma); headcrab cada hora; sin versión real de SLSsteam | LumaDeck estados por componente | |

### 4.4 Decisiones ya tomadas (del doc anterior, con fecha) y estado hoy

Vectores de "brickeo" (§3.3 del doc anterior) con lo que el código y `headcrab.sh` dicen hoy:

| # | Vector | Estado 2026-10-08 |
|---|---|---|
| V1 | crash en bucle en modo juego → `short_session_recover` → OOBE (mecanismo medido en nuestra pila, RESEARCH §17.3) | **vigente**: el fork no fuerza `SafeMode: no` tras headcrab (marcador), pero en cualquier distro que no sea SteamOS headcrab mismo lo deja en `no` (`headcrab.sh:845-848`); en SteamOS queda `yes` salvo config sin marcador o plantilla nueva. Sin crash guard. |
| V2 | `/usr/bin/steam-jupiter` parcheado con `sudo` y raíz en escritura | **vigente, ampliado**: botón visible en todo Linux (`02976bf`), ejecuta solo si `is_steamos()`; sin reaplicación tras update de SteamOS |
| V3 | `steam.sh` en 644 → re-extracción / "Permission denied" | **vigente, precisado**: el fichero en 644 es el **sustituto de headcrab** (`h3adcr-b-modul3s/headcrab_native.sh`), no el de Valve; headcrab lo deja en 555 y SFF lo baja a 644 en cada pasada. Si `bin_steam.sh` lo re-extrae no está medido. |
| V4 | `headcrab.pages.dev/reset` + `steam.cfg`: cliente pineado viejo vs gamescope | **vigente, ampliado**: no solo el reset; el **install** rebaja a `1788652215` si la versión difiere (`CheckHeadcrabCompatibility`), y corre cada hora |
| V5 | SIGKILL a Steam en setup, hash fix y **cada instalación/borrado de lua** | **vigente, ampliado** (`steam_tools_compat.py:39-65`) |

Propuestas anteriores y su estado:

| # | Qué | Estado 2026-10-08 |
|---|---|---|
| P4 | exclusiones por diseño (contributor, SteamDB, rclone, DRM, logros) | **superado**: cubiertos en §2.4, §2.6, §2.7, §2.8 |
| P6 | OpenValve (alternativa anunciada en FMHY, "finales de septiembre", sin repo) | **vigilar**; sin noticias en el repo de SFF ni en sus docs |
| P7 | purga de manifests al borrar (`3befc59`) | **convergido**: `uninstall_game_full` ya lo hacía |
| P8 | request codes externos borrados (`9033611`) | **contexto**: sin vuelta atrás en 6.9.0; el único código es el anónimo de la sesión |
| P9 / P22 | BD pública de claves (`KoriaPolis/Steam-Depot`) | **pendiente condicionado**: "no se implementa hasta ver en un log real el caso DLC nuevo sin clave y Hubcap no la trae" (09-12); hoy la BD responde y SFF la descarga en runtime, no la empaqueta |
| P10 | headcrab rebaja el cliente | **confirmado y ampliado** (install + hourly); riesgo suyo, dato nuestro para coexistencia (§4.1-1, -2) |
| P11 | LuasTools primero sin donar | **contexto** (D20): confirmado en código, `c2c35af` |
| P12 | `InstalledDepots {}` | **cerrado**: del 09-09 al 09-20; hoy escribe depots; nuestra semilla `StateFlags 1` no lo sufre |
| P13 | chunks ZIP, filtro Mono, RAM | **no aplica** |
| P14 | `lumacore-findings.md` cambia de repo a `drappula/LumaCore` | **hecho** |
| P15 | "Patch Gaming Mode" como equivalente de nuestro wrapper para el port a CachyOS | **pendiente condicionado** (`cachyos-port.md`); hoy el botón exige `is_steamos()` en el bridge aunque la UI lo muestre en todo Linux |
| P16 | "flag licenses changed" de `PackagePatch.cpp` | es del DLL: va a `lumacore.md` |
| P17 | veredicto "drappula/SFF es SteaMidra vivo; en Linux es ASSella con peores fuentes; los reportes de Discord son del original" | **parcialmente caducado**: vivo sí (v6.9.0); "peores fuentes" ya no (LuasTools primero, igual que ASSella/moon); la atribución al original sigue sin poder probarse (§5.1 S5) |
| P18 | prewarm de patrones y versión por `status.json` | **rechazados** (08-17), sin cambio |
| P19 | downgrade escribiendo `buildid`/`TargetBuildID` en el ACF | **rechazado** (LumaDeck 0.8.0 `self_heal_acf_build`), sin cambio; SFF lo sigue haciendo (`_sync_acf_downgrade`, cola de 7 días) |
| P20 | steamcmd.net primero, `PosixPath`, ACF 444, migración `config/lua` | **nada**, sin cambio |
| P21 | catálogo `store_metadata/` ante la clave Web API rechazada | **rechazado**, sin cambio; SFF lo regenera en CI |
| P23 | feed `Steam-Auto-PT` muerto → `update-resilience-comparison.md` | **hecho**: medido en `lumacore.md` §5.2 F1 (09-14, 09-23); el doc de comparación se absorbió y se borró el 2026-10-09 |
| P24 | `fix_hash_mismatch` "sujeta el cliente y apaga la puerta del hash" | **contexto, precisado**: sujeta el cliente (reset) y **no** apaga la puerta (el marcador impide reescribir `SafeMode`); lo que apaga la puerta en no-SteamOS es headcrab |
| P25 | fuentes de SFF como candidatas | **rechazado** (09-14), sin cambio: hoy sus fuentes son las nuestras (LuasTools, Hubcap) más dos espejos congelados y una BD pública |
| P26 | ACF 444 como idea para nuestro freeze | **rechazado**, sin cambio |

### 4.5 Bugs y fragilidades propias de SteaMidra (del código)

1. `VERSION` de SLSsteam nunca escrito → headcrab cada hora (`slssteam.py:289-334,655-716`; escritura borrada en `92d6813`).
2. `install_lua_to_steam`/`remove_lua_from_steam` matan Steam (y los juegos abiertos) con SIGKILL sin preguntar (`steam_tools_compat.py:39-65`).
3. `patch_steam_sh` deja `steam.sh` en 644 y borra cualquier línea con `LD_AUDIT`, incluida una futura de Valve (`:123-125`).
4. `_add_to_slssteam` reescribe `config.yaml` con `yaml.dump` y mete depots `{}` en `DlcData` (`linux_download.py:160-196`).
5. `migrate_existing_games` mete juegos poseídos en `AdditionalApps` (`:607-618`).
6. Dos resolutores de `config.yaml` (`XDG_CONFIG_HOME` sí/no); `detect_steam_type` prefiere Flatpak si existe el directorio; `linux_setup_now`/`fix_slssteam_hash` ignoran el `steam_path` configurado (`misc_bridge.py:1747-1833`).
7. `start_steam` tercer fallback `SLSteam.so` (`steam_process.py:177`); sin `.so` arranca vanilla y devuelve éxito.
8. `pkill -f wget|dlm|dgsc` mata procesos ajenos (`slssteam.py:376-382`); `_close_game_processes` mata por nombre de imagen cualquier proceso que coincida con un exe de la raíz del juego (`service.py:51-88`).
9. `_cleanup_live_stale_manifests` borra el manifest viejo **antes** de conseguir el nuevo (`downloader.py:157-181,559-560`); la ruta paralela y `_seed_free_manifest` escriben sin validar magic.
10. `_cached_lua_usable` no reconoce `,1,` → luas de Hubcap/Ryuu/generador se refetchean siempre (`endpoints.py:588-610`).
11. `update_check.py` roto y sin llamadores; `manifestor.py`, `GseToolUpdater`, `run_gen_emu`, `_write_user_config`, `set_active_unlocker`, `LumaCoreUpdateChecker` (importa `pywin32_system`), `FakeAppIds`, `ensure_slssteam_api_enabled`, `title_bar.py`, `ddmod_launcher.py` sin llamadores; `midra://` registrado y no procesado; `get_installed_games` sin `@pyqtSlot`.
12. Ludusavi muerto por ruta (`cloud_save_paths.py:51-52`); `EMU_SAVE_LOCATIONS` con `APPDATA` vacío → rutas relativas al cwd en Linux; `_crack_dll_core` crea `~/AppData/Roaming/GSE Saves/` en Linux; `image_cache` en `~/SteaMidra/`.
13. `fallback_depotkeys.json` ausente del build → `has_depot_key` siempre `False` y `fallback_tokens.json` filtrado a vacío hasta el primer refresco (`depot_history.py:365`).
14. `steamauto.py:323` `NameError` (`logger` sin definir); SteamAutoCrack lanzado sin `wine`; `.unpacked.exe` buscado con `name` en vez de `stem` en ColdClient; `steamstub_x64.dll` referenciado y ausente.
15. Deshacer incompatible entre cinco caminos de fixes (`.bak`, `.steamstub.bak`, `.steamlocked.bak`, `OG_`, `_o.dll`, `.steamidra_exe_backups/`); `restore()` aplica cualquier `*.bak` del árbol y borra `version.dll`/`winmm.dll` < 500 KB.
16. Bridge web sin autenticación con `delete_game` → `rmtree` sin comprobar biblioteca, `launch_game` → `Popen`, `open_url` arbitrario, `set_setting` de cualquier clave; `LocalContentCanAccessRemoteUrls` + `--no-sandbox`; `innerHTML` con nombres de juego escapados a medias (`library.js:296-306`).
17. Secretos: clave maestra siempre en fichero además del keyring; Web API key y token DepotBox embebidos; contraseña de Steam en `settings.bin`; token de Google en claro; `_try_manifesthub` con la clave en la query string.
18. TLS: `dotnet-install.ps1` con `CERT_NONE` (Windows), Chrome for Testing con fallback `CERT_NONE`, `game_list_fallback._get_ssl_ctx` sin verificar como fallback; descargas sin hash.
19. `hv` en `build_sff_gui.spec` y en `Third-party notices.md` no existe; `installer.nsi` con fallback `6.6.7`; README e instalador enlazan a `Midrags/SFF`; `v6.6.7c` tag huérfano; docs desfasadas (§1.5).
20. `rclone_linux/rclone` es ELF i386 estático; `fzf` solo `.exe`; sin música en Linux (`.so` inexistente); `inotify_simple` sin instalar.

---

## §5 Historial

§5.1 recoge lo que se creía de SteaMidra y el código desmiente, además de lo
listado al principio del doc, y lo que sigue sin medir. §5.2 recoge las
pruebas y mediciones con fecha: sondas de red a sus fuentes, lo que sus
propios commits y changelogs dicen haber medido, y lo que cuentan sus
usuarios. §5.3 es la cronología del repositorio.

### 5.1 Lo que se creía y ya no es así

Además de lo listado en la cabecera del doc:

- **Las versiones antiguas salen de SteamDB con la cookie del usuario y no
  hay pin.** El scraper de SteamDB está desactivado
  (`depot_history.py:1835-1839`); el historial sale del CM, de morrenus, del
  árbol de GitHub y de los tokens, y al elegir versión sí escribe
  `ManifestIds` en SLSsteam y `setManifestid` en el lua.
- **En Linux es ASSella con peores fuentes.** Desde `c2c35af` (09-20) el
  archivo de LuasTools va primero, como en ASSella.
- **Su única vía para un manifest fuera de GitHub es el request code con
  licencia o Hubcap con clave.** LuasTools sin clave es la primera vía;
  ManifestHub2 da una clave gratuita de 24 h.
- **La capa Python pre-obtiene el código de manifest para sembrar
  `depotcache/`.** Siembra manifests, no códigos (`zip.py`,
  `_seed_free_manifest`, `_preseed_depotcache`).
- **Los espejos de GitHub son un snapshot de julio de 2025.** Sin medición
  nueva; lo medido el 2026-09-14 es que no tienen ningún gid actual.

**Lo que sigue sin medir:**

- Que `bin_steam.sh` re-extraiga `steam.sh` cuando el tamaño no coincide;
  y el fichero que SFF deja en 644 es el de headcrab, no el de Valve.
- Corrupción de `config.vdf` o `localconfig.vdf` por el SIGKILL a Steam en
  cada instalación; el SIGKILL sí está en el código.
- Que Steam "valide, no pueda reconstruir el estado del depot y limpie el
  contenido" con un ACF a mano; lo que sí consta es `InstalledDepots` vacío
  del 09-09 al 09-20.
- Que el descargador nativo con sesión anónima siga sirviendo en red.
- Que una clave mala aportada por un usuario deje el DLC en error de
  descifrado; la base no valida claves.
- El caso Celeste (504230) que cita `8eaf238`: verificado por ellos, no por
  nosotros.
- Si los informes de Discord se refieren al original o al fork: el hilo no
  trae versión.

### 5.2 Pruebas y mediciones con fecha

#### Función 1 — Engancharse a Steam

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-08 / 09 | Discord (servidor de SLSsteam, capturas transcritas el 09-14; segunda mano) | Seis máquinas rotas tras SteaMidra, causa candidata por el código | Deck que no arranca Steam tras reinstalar con headcrab (`steam.cfg` + `steam-jupiter` editado + raíz desbloqueada); Deck formateada (OOBE o rendición); Bazzite sin updates de Steam, Decky roto, solo offline (`BootStrapperInhibitAll` + entorno del AppImage); ROG Ally con Steam que no arranca (`steam.sh` en 644); "aunque desinstale, todos mis juegos dicen comprar" (`yaml.dump` del `config.yaml` entero o `steam.sh` re-extraído); "Steam borra la carpeta entera del juego" (ACF a mano sin claves ni manifests). Los daños sobreviven a desinstalar SteaMidra; qué parche rompió cada máquina no se confirma | §4.4 |
| 2026-09-08 → 09-14 | Discord (hilo "Remove SteaMidra" en FMHY; segunda mano) | Qué dice la comunidad | "No hace la mayoría de lo que anuncia, o no correctamente", "le dice a un LLM que haga las cosas", "malfunciona en Linux, dejando instalaciones rotas"; el fork no aparece en el hilo | §4.4 |
| 2026-10-08 | red (`curl`) | `headcrab.sh` tal como lo ejecuta SFF | 954 líneas, sha256 `caeca7e4…88c9d4`; `HeadcrabCompatibleClientVer=1788652215`; instala o rebaja el cliente según esa versión; sustituye `steam.sh` entero por `headcrab_native.sh` y guarda el de Valve como `client.sh` | §2.1 |
| 2026-09-14 | `headcrab.sh` bajado ese día | Qué build fija Headcrab | El manifest de Deck pineado es `1788291500` y `HeadcrabCompatibleClientVer` dice `1788652215` (el de escritorio); en una Deck no cambia nada. Hoy (2026-10-08) la variable sigue en `1788652215` | `slsdeck.md` §5.2 F1 (10-01) |

#### Función 2 — Propiedad y licencias

Sin pruebas propias; SteaMidra delega en SLSsteam (`slssteam.md` §5.2).

#### Función 3 — DLC

Sin pruebas propias.

#### Función 4 — Claves y manifests

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-08-17 / 09-12 | repo (`fallback_depotkeys.json`, `KoriaPolis/Steam-Depot`) | La base pública de claves, contada | 369408 entradas; 190465 con clave (190451 en la copia empaquetada, 66,9 MB); 27849 son DLC o depots de DLC; los depots de nuestras pruebas (Balatro, Brotato y sus DLC) están con clave. Desde `ed8a2a8` (09-07) no viaja en el build: se baja cada 6 h | §2.4 |
| 2026-09-12 | changelog v6.8.0 (segunda mano) | Caída de los proveedores de códigos vista por ellos | "The public GMRC mirrors (steam.run, wudrm, opensteamtool) are gone; they now answer 403/404 everywhere"; `9033611` borró toda la familia de request codes externos y Tor | §2.4 |
| 2026-09-14 | red | ¿Sirve la cadena gratis? | `fylsdy/ManifestHub/depotkeys.json` 404. `steamtoolsapp/ManifestHub` y `steamtools-games/ManifestHub3`: las 7 apps probadas existen (lua 200) pero 0 de 6 gids actuales (Lonely Mountains, Rust, Hades, Valheim, Vampire Survivors, Hades II); solo Balatro, sin update en meses; el gid de LMSR que tienen es anterior al build del 08-09. `qwe213312/…` igual. Los mismos 6 gids estaban ese día en P-ToyStore, que el fork no usa | §2.4 |
| 2026-09-15 | commit `f98c8e0` (perfilado de ellos, segunda mano) | Un add por "Free Providers" | 17 s con 2,8 s de red: parseaban dos veces la base local (67 MB, 370k entradas, 13,8 s) y escribían la contribución en línea (7,1 s). Ahora memoizan, escriben en un worker y precalientan a los 12 s | §2.4 |
| 2026-10-08 | red | Fuentes del fork hoy | `headcrab.sh` 206; `fylsdy/ManifestHub/depotkeys.json` 404; `KoriaPolis/Steam-Depot/fallback_depotkeys.json` 206 | §3.1 |

#### Función 5 — Updates de juegos

Sin pruebas propias. Lo que el fork mide de sí mismo está en §5.3 (ACF con
`InstalledDepots` vacío, 09-09 → 09-20).

#### Función 6 — Fixes y DRM

Sin pruebas propias.

#### Función 7 — Logros, stats y tiempo de juego

Sin pruebas propias.

#### Función 8 — Cloud saves

Sin pruebas propias.

#### Función 9 — Añadir y quitar un juego

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-08-21 (6.6.6) | changelog (segunda mano) | Nombres con `\` en Linux | El descargador nativo unía rutas con `\` y creaba ficheros planos `Some\File\Name.exe` (regresión 6.6.5); `_normalize_manifest_path` y `flat_file_repair.py` lo reparan | §2.9 |
| 2026-09-14 | commit `3befc59` (segunda mano) | Por qué borrar "full" purga `depotcache/` | Una reinstalación releía el manifest del gid viejo y "Steam never offered the next update" | §2.9 |
| 2026-09-20 | commit `8eaf238` (verificado por ellos en Celeste 504230) | `InstalledDepots {}` en el ACF | `StateFlags 36`, depots vacíos y Steam marcando "Content still encrypted" aunque los chunks estuvieran bien; vuelven a escribir los manifests. Además su filtro por SO tiraba `.dll`/`.exe` de depots nativos de Linux (Celeste con Mono se quedaba sin `mscorlib.dll`) | §2.9 |

#### Función 10 — Credenciales y proveedores

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-08 (6.6.6) | changelog (segunda mano) | Lista de juegos sin clave de Steam | "Valve rejected the bundled key again": catálogo `store_metadata/` empaquetado (~190k apps) y espejos en GitHub | §2.10 |

#### Función 11 — Mantenimiento propio

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-30 → 10-03 | commit `0bf7b83` (segunda mano) | headcrab en el hilo de la GUI | El chequeo de arranque lanzaba el instalador en el hilo de la GUI con stdin heredado: minutos de ventana congelada, y un `sudo` sin responder la colgaba para siempre. Ahora en un hilo con stdin a `DEVNULL` | §2.11 |

#### Función 12 — Proyecto

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-08-21 → 10-07 | repo | Upstream parado, fork vivo | `Midrags/SFF` no se mueve desde `fa44fc9` (08-21), sin reacción a la caída de proveedores del 09-09. `drappula/SFF`: 88 commits y 7 releases entre 09-04 y 10-03; sin commits desde el 10-03 | §5.3 |

### 5.3 Cronología

**Topología.** `drappula/SFF`: 166 commits en `origin/main`
desde `32bd204` (2026-05-25, "Initial clean commit - SteaMidra v6.2.4",
Midrag); no hay git anterior a 6.2.4, solo `CHANGELOG.md` (hasta v4.5.1).
`midrags/main` (`Midrags/SFF`) = 78 commits, todos de Midrag, hasta `fa44fc9`
("check", 2026-08-21), que es exactamente el merge-base: el fork no ha
tomado nada de upstream desde entonces y upstream no se ha movido. 88 commits
del fork: mallusrgreat 73, Qiyrax 8, drappula 4 (merges), St0Qx 1,
michelegoku3 1, wtfseanscool 1. PRs visibles #1-#4 (St0Qx ← Qiyrax) y #6
(michelegoku3); no hay #5 en `main`. 50 tags; 47 anotados; `v6.6.7`,
`v6.6.7a` y `v6.6.7c` ligeros; `v6.6.7c` apunta a `7e0f2d4`, un commit
huérfano fuera de `main` (el contenido entró por `0b61497`). No existe
6.7.x. `<ver>` de los assets sale de `sff/core/strings.py`, nunca del tag;
`installer.nsi` lleva un fallback `6.6.7` desfasado desde el 09-07.

**Upstream (Midrag), 2026-05-25 → 08-21.** 44 tags en 88 días, releases
aplastadas en un commit ("Release SteaMidra vX"). Hitos para esta lectura:
6.2.2 `patch_slssteam_config` (`PlayNotOwnedGames: yes`, `SafeMode: yes`,
marcador `.headcrabd`) y SLSsteam auto-update al arrancar; 6.2.3 "Stopped
writing ACF on Windows (LumaCore handles ownership; Linux still writes ACF)";
6.2.5 DDMod y Steamless (`Steamless.CLI.dll` + dotnet) en Linux, .NET 9
auto-instalado; 6.2.8 `BootStrapperInhibitAll` como tile y sandbox Lua de
LumaCore; 6.3.2 (06-28) contribución de claves, Lua agrupado, `xinput1_4.dll`
como segundo proxy, Denuvo y EOS en LumaCore, online-fix solo navegador; 6.3.6
tres repos GitHub de manifests y Ludusavi; 6.4.2 `config.yaml` por regex (no
re-dump), `fallback_depotkeys.json` a dir escribible en AppImage; 6.4.3 Ryuu
premium; 6.4.5/6.4.6 **filtro de headcrab** (CloudRedirect y `flatpak install`
comentados) y fallback a DDMod; 6.4.7 ACF read-only "so Steam cannot flip
StateFlags"; 6.4.8 (07-27) **descargador nativo** en Linux, Steam relanzado sin
`APPIMAGE/LD_LIBRARY_PATH`; 6.4.9 claves a `config.vdf` también en Linux con
Steam muerto antes; 6.5.0/6.5.1 DLC en `AdditionalApps` y `DlcData`; 6.5.4
campos requeridos de `config.yaml`; 6.5.5 auto-update "Linux always returns
off"; 6.5.6 no sobrescribir los gids descargados con los últimos del CM; 6.5.7
(07-31) fuera HV, Workshop, Tools tab, buzzheavier → pixeldrain; 6.5.8 (08-01)
**"Fix Hash Issue"** (headcrab reset), `SafeMode: no` + `WarnHashMissmatch: yes`;
6.5.9 DepotBox; 6.6.0 `PlayNotOwnedGames: yes` en cada descarga (retirado en
6.6.1); 6.6.1/6.6.3 reestructuración de `sff/`, `manifesthub2.filegear-sg.me`,
Linux Guide tab, contribución de claves "ON por defecto cada 3 h"; 6.6.5
(08-16) `api.steamcmd.net` primero, cola de descargas, `store_metadata/`,
prewarm de patrones de LumaCore, docs LD_PRELOAD → LD_AUDIT; 6.6.6 (08-21)
downgrade con `buildid`+`TargetBuildID` y cola persistente, ACF read-only solo
Linux, migración `config/lua` → `stplug-in`. Después solo `ca99dd4`/`fddb164`
(README) y `fa44fc9` ("check", Google site verification).

**Fork (drappula), 2026-09-04 → 10-03.**

- **09-04/05**: `cb71021` (wtfseanscool) ABI de `FileExists` en LumaCore; `e93614e`, `4af09f7`, `ebd68c6`, `091bea4`, `61da695` (Linux DDMod, launch options con proceso `steam`, `SAVE_LOCATIONS` por plataforma, **CI sin credenciales de Google Drive**, allowlist de `third_party/` en el spec Linux, sin relanzamiento automático de Steam); `9906a54` **v6.6.7**; `6876f46` Hubcap `validate_api_key` con motivo; `658ab34` appid del lua; `abf624d` Start Menu con `LD_AUDIT` embebido en el `.desktop`.
- **09-07**: `3cc8c41` **v6.6.7a** (un commit, 28 ficheros): versiones antiguas como instalaciones normales, downgrade por Build ID en Linux, CDN nativo top-4, Store "Popular", cancelación, **"Patch Gaming Mode" (`/usr/bin/steam-jupiter`)**, `store_metadata/*.json` al repo; `ed8a2a8` (Qiyrax, PR #1): ACF sin sobre-escape, `LastOwner` de `STEAM32_ID`, `delete_game` limpia `config.yaml`, **borra `fallback_depotkeys.json` (66,9 MB) y lo gitignora**; `8db0304`, `4aecf64` (`depotfromapp` por depot), `d8d8d1c` (pausa/reanudación; appid = nombre numérico del fichero), `99e2604` (rechaza HTML de Akamai como manifest), `a29132f` (Qiyrax): `yaml_config` acotado a su sección e **`install_lua_to_steam` para Steam antes de escribir**; `2b652fe` (no escribir clave falsa del appid en `config.vdf`); `410a3c7` (Qiyrax): **Gaming Mode escribe `SafeMode: yes`**, backup `steam-jupiter.bak`.
- **09-08**: `ffd70db` **patrones de LumaCore desde `michelegoku3/MigoReleases`**; `acb6cf3` **v6.6.7b**; `1cfb20a` (Qiyrax) app id == depot id, sin prompt "pin this version"; `9adac94` (Qiyrax) `create_acf` escribe `InstalledDepots`, **`addtoken()` → `AppTokens`**.
- **09-09**: `6f3153f` depot picker avanzado; `45971f4` (Qiyrax) guarda de `~/.bashrc` en la Deck; `46a421d` (Qiyrax) **`InstalledDepots {}` siempre** ("fixing update issue with newly downloaded games").
- **09-10**: `0b61497` **v6.6.7d**: "Free Providers" (trionine + BD + revobd + `steamtoolsapp/ManifestHub` + `steamtools-games/ManifestHub3`), explorador de ficheros por depot, auto-selección de fuente; `0c02abc` merge PR #4.
- **09-12**: `4cc2559` claves muertas (sondeo al arrancar, diálogo, flag); `4f1999f` **Windows vuelve al handoff a LumaCore**, `store_metadata/` fuera del repo + `tools/fetch_store_metadata.py` en CI; `4e1721f` helper `00_LetUpdate_override.lua` al arrancar (Windows), crea `stplug-in` y `depotcache`; `8d9dab5`/`d06f232` CI zlib + caché del venv; `1b3d3a8` rutas por módulo en MigoReleases (el prewarm daba 404); `1bbe929` límite de velocidad; **`9033611` elimina los espejos GMRC** (`steam.run`, `wudrm`, `manifest.opensteamtool.com`, `_REQUEST_CODE_FALLBACKS`, 350 líneas); `d44106a` **updates y enlaces → `drappula/SFF`**, título "SteaMidra Fork".
- **09-13**: `bd72bcf` luas incompletos refetcheados; **`92d6813` "route slssteam setup through headcrab"**: instalador propio (7z/apt/pacman, escritura de `VERSION`) borrado (−350 líneas), headcrab primario, check en segundo plano no instala con Steam abierto, `create_steam_cfg` no pisa, tile "Steam Updates" en Linux.
- **09-14/15**: `7e34444` sentinel `NOT_FOUND`; `3befc59` borrar "full" purga manifests de depotcache y staging; `b4e77f7` retira "oureveryday/midraeveryday"; `f98c8e0` BD memoizada, espejos en paralelo, prewarm a los 12 s; `3f72cdf` **v6.8.0**.
- **09-20**: `c2c35af` **`manifest.luastools.xyz` primero** (juego y workshop), chunks ZIP, "Remove Key", 1 GB → 650 MB, **`AGENTS.md`**; `8eaf238` proceso `SteaMidra` + `StartupWMClass`, **`InstalledDepots` restaurado** ("Content still encrypted", Celeste), `.dll/.exe` conservados en depots Linux (mscorlib).
- **09-30 / 10-01**: `8aaf3c5` (michelegoku3, PR #6) LumaCore: `xinput1_4.dll` bootstrap primario, `CNetPacket` (+842/−169; va a `lumacore.md`); `02976bf` "Patch Gaming Mode" visible en todo Linux; `1b2cf7f` merge.
- **10-03**: `e4c6b65` `LumaCore/` → submódulo; `0bf7b83` headcrab de arranque en hilo con stdin `DEVNULL`; `da3108e` fuera el submódulo; `75829ee` **LumaCore desde `drappula/LumaCore` (V37+)**; `3452345` docs; `b374b6e` **v6.9.0**; `c853985` check de updates fuera del hilo GUI; `5926f31` AppImage suelto en el release; `4159300` etiquetas de assets (tag `v6.9.0`).

**Ritmo.** Upstream ≈ un tag cada dos días con rachas de tres por día y dos
pausas (05-31 → 06-28, 07-08 → 07-24). Fork: cinco tags en seis días
(09-05 → 09-10), luego v6.8.0 (09-15) y v6.9.0 (10-03); desde el 10-03 sin
commits. El lanzador Python recibe casi todo el trabajo; en
la DLL solo entró el PR #6 y los arreglos de `FileExists`.

**Lo que ya no está en el código y el doc anterior o el CHANGELOG daban por
vivo**: instalador propio de SLSsteam y escritura de `VERSION` (`92d6813`),
espejos GMRC y Tor (`9033611`; `torpy` sigue en requirements), `PlayNotOwnedGames`
(6.6.1), `ManifestIds` "sin uso" (vuelve a escribirse al elegir versión),
`InstalledDepots {}` (`8eaf238`), HV/Workshop/Tools/UserGameStatsSchema (6.5.7),
buzzheavier (6.5.7), scraper de SteamDB (desactivado), `update_check.py`,
`manifestor.py` (openlua.cloud), `midra://`, Ludusavi (ruta rota), música en
Linux, `hv` en el spec.
