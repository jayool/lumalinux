# ASSella (niwia/ASSella) — el gestor de escritorio, leído desde el código

Leído desde el código el 2026-10-09, rama `canary` entera y `beta` por
diferencias. Sustituye a `assella-analysis.md`: sus mediciones con fecha están
en §5.2, lo que afirmaba y el código ya no sostiene en §5.1, y sus decisiones
con el estado de hoy en §4.4. ASSella es un fork de **ACCELA**, cuyo código
original no está publicado; todo lo que aquí se dice es del fork. Cómo
**nosotros** hacemos lo mismo está en `nosotros.md`; SLSsteam, sobre el que
monta, en `slssteam.md`; los plugins Lua que despliega, en
`slssteam-plugins-analysis.md`.

---

## §0 Ficha

| | |
|---|---|
| **Qué es** | Un **gestor de escritorio para Linux** (Python 3.13 + PyQt6, distribuido como AppImage) que añade juegos a Steam sobre **SLSsteam** de serie. Tiene dos descargadores. **Clásico**: baja los depots fuera de Steam con `DepotDownloader.dll` (.NET) y las claves y manifests de Hubcap. **AT0-M** (solo `canary`): escribe en el `config.yaml` de SLSsteam `AdditionalApps`, `AdditionalDepots` y `DecryptionKeys`, siembra `depotcache/` y manda `install\|appid\|lib` por `/tmp/SLSsteam.API`, para que **Steam descargue** con dos plugins Lua de SLSsteam (`download.lua`, que inyecta depots y claves y responde los códigos de manifest, y `spliced-tickets.lua`) que baja de un bucket R2 propio. Además: motor de manifests propio (código de petición de wudrm/ManifestDeX + CDN de Valve, Hubcap de rescate), rollback por build, Workshop, Steamless, Goldberg, proxy EOS, `fix.so` de netsock, editor de logros, tickets de propiedad, "All games online", reparador del `config.yaml` (`assfixer`), cadena de evasión de bloqueos para Hubcap (DoH, Tor, Cloudflare Worker), web UI y CLI. |
| **Repo y commit leídos** | `niwia/ASSella` `canary@73f0471` (2026-10-09 01:34 +0530, versión `3.0.0testing071026003`) entero; `beta@070526c` (2026-10-09, rama por defecto, `2.7.1beta`) por diferencias con `canary`; `main@f582f4f` (2026-09-13, 10 commits, parado) y `arm64@e24372a` (árbol idéntico a `canary`). 460 commits en todas las ramas desde `9a11c5f` (2026-05-20, "Initial ASSella release"). `canary`: 310 ficheros, 170 `.py` fuera de `src/deps` (~90.000 líneas; los mayores `managers/task_manager.py` 3.609, `ui/…/depotselection.py` 3.153, `utils/yaml_config_manager.py` 2.892). |
| **Ramas** | `beta` y `canary` se separan en `1fcd7a8` (2026-09-19): 85 commits solo en `beta`, 144 solo en `canary`, sin merges entre ellas; los arreglos se reaplican a mano (ocho asuntos en las dos, cuatro con el parche idéntico). **AT0-M y `native_steam/` solo existen en `canary`**; en `beta` el motor de manifests se llama `vapor.py` (971 líneas, sin ManifestDeX) y `at0m.py` es un alias, al revés que en `canary`. `arm64` recibe `canary` por un workflow que lo fusiona en cada push (`.github/workflows/sync-arm64.yml`, 20 commits del bot). `beta-6634894975231722385`: un commit vacío de "auditoría sin hallazgos". |
| **Plataforma** | Linux x86_64 (AppImage, paquete de Arch en `gh-pages`, `install.sh`); `arm64` por `scripts/install-armada.sh`. Steam nativo o Flatpak. Requiere .NET para el descargador clásico. |
| **Licencia y quién** | Sin `LICENSE` en el repo. niwia: 427 commits; "You-know-who" (`niwia007@…`) 9 (README, `voices.json`, `broadcast.json`); github-actions[bot] 21; KingCatto 2 (PR #16, 09-15). El repo se desarrolla con agentes (`.agents/AGENTS.md`, `.agents/rules/canary_rules.md`, `plugin_publishing_rules.md`). |
| **Relación con los demás programas** | Monta sobre **SLSsteam** de serie (lo instala ejecutando Headcrab) y escribe su `config.yaml`. Despliega el **`download.lua`** de la familia de `slssteam-plugins-analysis.md` y el **`spliced-tickets.lua`** de Ace, byte a byte el mismo fichero que nuestro `lumadeck-spliced-tickets.lua` en la copia de `beta`. Fuentes compartidas con LumaDeck: **Hubcap** (con la clave del usuario) y los proveedores de códigos **wudrm** y **ManifestDeX**, que también están en la cascada GMRC de lumalinux. **Convive con nuestra pila en la misma máquina**: los dos escriben el mismo `config.yaml`, el mismo `depotcache/` y la misma carpeta de plugins, y ASSella lista como suyos los juegos de LumaDeck (§4.1). |

**Lo que cambia respecto al doc anterior**, en una línea cada uno:

- **ASSella muestra y gestiona los juegos de LumaDeck.** El escaneo da por "AT0-M" todo juego instalado cuyo appid esté en `AdditionalApps` (`managers/game_manager.py:850-860,896-929`). Los mete en su comprobación de updates, "Update" los manda a su handoff y "Move to ASSella" los convierte en juegos de su descargador clásico (§4.1-1).
- **Desinstalar desde ASSella quita del `config.yaml` las entradas del juego sin mirar quién las puso** (`managers/game_manager.py:2046-2064`; con "I bought the game", también `ManifestIds`, `GameTitles`, `CDKeys`…, `ui/dialogs/library/actions.py:659-776`). En los juegos AT0-M el botón está desactivado.
- **Guardar los Ajustes escribe `DisableUpdates` y `Plugins`** (`ui/dialogs/settings.py:983-1014`): la casilla de `DisableUpdates` nace marcada (`ui/dialogs/settings_tabs/at0m_tab.py:213`) y desmarcar AT0-M escribe `Plugins: no`, que apaga también nuestro plugin.
- **El modo nativo ya no retira nada al terminar**: `AdditionalDepots`, `DecryptionKeys` y los plugins se quedan (`core/tasks/native_steam_download_task.py:1145-1172`); los plugins ya no se despliegan por descarga sino una vez, desde Ajustes. El doc anterior describía los plugins como "transitorios".
- **La clave raíz del AppID ya entra en `DecryptionKeys`** (`native_steam_download_task.py:784-790`), lo que cierra el fallo de shaders y Workshop medido el 2026-10-05.
- **El motor de manifests corre wudrm y ManifestDeX en carrera**, wudrm ya por **HTTPS** (`core/at0m.py:127,288,302-337`), y guarda los códigos en SQLite sin caducidad (`:150-185`).
- **Todas las llamadas a Hubcap van sin verificar TLS** (`core/morrenus_api.py:36-66`), con la clave como `Bearer`; en modo `auto` (el de por defecto) pueden acabar en un Cloudflare Worker de un tercero con la clave dentro (`utils/isp_bypass.py:494-498,636-642`).
- **El web server de `canary` escucha en `127.0.0.1` con token** (`utils/web_server.py:21,48-61`); el de `beta`, la rama por defecto, sigue en `0.0.0.0:8765` con CORS `*` y sin autenticación.

---

## §1 Mapa del código

Rutas relativas a `src/` en `canary@73f0471`. F = función de §2.

### 1.1 Arranque, actualización, superficies

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `main.py` | 374 | modo GUI, `--headless`, `--helper`, `accela://`; en GUI, **copia `config.yaml` a `.bak` en cada arranque** (`:251`), fuerza `API: yes`, `LogLevels \|= 0x2` y `Plugins: yes` (`:256` → `utils/yaml_config_manager.py:390-431`) y fusiona la base de FakeAppIds si está activa (`:258`) | 1, 11 |
| `managers/app_update_manager.py` | 368 | updater por canal sobre la lista `/releases` de GitHub; zsync o `appimageupdatetool`, o AppImage entero con su `.sha256` si existe (`:310-339`) | 11 |
| `managers/at0m_update_checker.py` | 560 | comprobación de updates de los juegos "AT0-M": PICS/steamcmd, depots nuevos, preflight gratis de Hubcap `/contents`; `apply_update` baja el zip de Hubcap y añade depots y claves al config (`:496-560`) | 5 |
| `utils/web_server.py` | 689 | web UI en `127.0.0.1:8765` (`0.0.0.0` con `web_ui_allow_lan`), token por sesión; apagada en GUI, encendida en headless/helper | 11 |
| `managers/cli_manager.py`, `managers/helper_manager.py` | 1.085, … | CLI y modo helper (comprobación de updates sin ventana) | 11 |
| `utils/isp_bypass.py` | 652 | transporte de Hubcap: directo → DoH → Tor → Wirecutter (§2.10) | 10, 12 |
| `ui/status_pager.py`, `managers/voices_manager.py` | — | `broadcast.json` (solo texto) y `voices.json` (builds recomendados con `manifest_overrides`) de la rama del repo | 5, 11 |

### 1.2 Juegos, fuentes, SLSsteam

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| `managers/task_manager.py` | 3.609 | cola de trabajos; decide descargador clásico, tarea nativa vigilada o handoff (`:680-870`); vigilante de instalación nativa 30 min (`:1003-1058`) | 4, 9 |
| `core/tasks/native_steam_download_task.py` | 1.224 | AT0-M vigilado: claves, parche del config, licencia, `depotcache`, `install`, vigilancia del `.acf`, limpieza (§2.4) | 4, 9 |
| `core/native_steam/native_steam_handoff.py` | 463 | AT0-M "entregar y volver": lo mismo sin vigilar, `install` opcional | 4, 9 |
| `core/native_steam/steam_manifest_pinning.py` | 218 | bloque `ManifestIds` reconstruido entero; pin por `pin_build`, rollback, `voices.json` | 5 |
| `core/at0m.py` (`core/vapor.py` = alias) | 1.095 | códigos de wudrm/ManifestDeX → CDN de Valve → `DepotManifest` (nombres descifrados, nulos recortados) → Hubcap; Workshop; builds de SteamDB; copia al `depotcache` central (`:963-1003`) | 4 |
| `core/morrenus_api.py` | 595 | API de Hubcap (`hubcapmanifest.com/api/v1`) | 4, 10 |
| `core/tasks/download_depots_task.py`, `process_zip_task.py`, `smart_update_task.py`, `manifest_check_task.py`, `download_workshop_task.py` | 1.155, 1.004, 419, 898, 362 | descargador clásico, zips, updates, Workshop | 4, 5 |
| `utils/manifest_resolver.py`, `core/branch_bundle.py`, `managers/depot_key_manager.py` | 457, 206, … | manifests para el clásico, betas montadas en cliente, claves en SQLite | 4, 5 |
| `utils/yaml_config_manager.py` | 2.892 | **todas** las escrituras del `config.yaml` (§3.2) | 2, 3, 5, 9 |
| `utils/plugin_manager.py`, `utils/plugin_games.py` | 442, 489 | plugins Lua desde R2; registro de juegos AT0-M y sincronía con el config | 1, 9 |
| `utils/slssteam_integration.py`, `core/steam_helpers.py` | 985, 817 | `/tmp/SLSsteam.API`, vida de SLSsteam, watcher muerto, ACF de respaldo, bibliotecas | 1, 9 |
| `utils/assfixer.py` | 1.698 | reparador del `config.yaml` contra la plantilla de SLSsteam | 11 |
| `managers/mrc_config_manager.py` | 143 | `mrc_config.lua/json` para un plugin `download` de prueba | 4 |
| `utils/ticket_manager.py`, `utils/eos_detector.py`, `managers/shsah_reborn.py` | 509, 271, 432 | tickets cifrados, proxy EOS, `UserGameStats_*.bin` | 6, 7 |
| `managers/game_manager.py` | 2.536 | escaneo de bibliotecas, marcadores `.ACCELA`/`.DepotDownloader`, juegos AT0-M | 9 |
| `deps/` | — | binarios sin fuente: `DepotDownloader.dll` y `DepotDownloaderMod.dll` 3.4.0, SteamKit2, protobuf-net, ZstdSharp, QRCoder, `EOSSDK-Win64-Shipping.dll`, Goldberg (`release-2026_08_23`), Steamless, SLScheevo, `schema-grabber` (ELF) | 4, 6, 7 |

---

## §2 Las doce funciones

### 2.1 Engancharse a Steam

**ASSella no se engancha a Steam.** Lo hace SLSsteam, que ASSella instala
ejecutando `curl -fsSL headcrab.pages.dev | bash` en una terminal
(`ui/dialogs/settings_sls.py:229`; `ui/dialogs/canary_welcome_dialog.py:931,952`).
La URL va sin esquema, así que `curl` la pide por `http://`. Del cliente de
Steam solo lee: si hay un proceso `steam` con `SLSsteam.so` en sus `maps`
(`native_steam_download_task.py:368-388`), y el log `.SLSsteam.log`.

En modo AT0-M el enganche lo pone el plugin **`download.lua`**, que corre
dentro de SLSsteam. Engancha `GetPackage`, `CConfigStore::GetBinary` y el sitio
de llamada de `GetManifestRequestCode` por dos patrones de bytes fijos en el
propio Lua. Los dos patrones se midieron únicos en los tres clientes de
septiembre (§5.2). El fichero no está en el repo: se publica en un bucket R2
(`utils/plugin_manager.py:30`) con un `plugins_manifest.json` del mismo bucket
que trae su SHA-256. Ese manifest se guarda la primera vez y **no se vuelve a
pedir nunca** (`:80-85`; el único `force_refresh` está en
`deploy_all_plugins(force_download=True)`, `:414-418`, y nadie lo llama así).
La sincronía al arrancar está apagada a propósito (`:439-442`). Un plugin
nuevo en R2 no llega a quien ya tenía la caché.

**Despliegue de plugins** (`plugin_manager.py:293-411`):

- Siempre fuerza `Plugins: yes`.
- Con `spliced-tickets.lua`, además, `SmartTickets: 0x1` (`:303-304`).
- Si no hay caché, adopta como "bueno" el fichero que ya esté en la carpeta de
  plugins, sin comprobar el hash (`:324-339`).
- Al desplegar un `download*`, borra los demás `download*.lua` (los copia antes
  a `.bak`) (`:367-378`).
- Los nombres de fichero salen del manifest remoto sin validar (`:68,380,419-424`).

El botón "Standard Plugin" ofrece **borrar todos los demás `.lua`** de la
carpeta, salvo `download.lua` y `spliced-tickets.lua`, sin copia
(`at0m_tab.py:584-610`). Con "I'm using custom plugins (Advanced)" se salta la
comprobación de que estén (`plugin_manager.py:207-242`).

**Cuando Valve actualiza**: no hay gate propio; depende de SafeMode de SLSsteam
y de que los patrones del lua sigan siendo únicos. Sin crash guard ni
kill-switch: el remedio publicado es vaciar `plugins/` y quitar
`AdditionalDepots`/`DecryptionKeys` del config (§5.2, 2026-09-29).

### 2.2 Propiedad y licencias

Todo por SLSsteam:

- `AdditionalApps` para el appid. La tarea nativa espera hasta 20 s a ver
  `AppLicensesChanged`/`Unlocked` en el log y sigue aunque no lo vea
  (`native_steam_download_task.py:874-900,231-242`).
- `AdditionalDepots` con los depots que tienen clave, más los redistribuibles
  compartidos con el comentario `# Steamworks Shared`
  (`native_steam_download_task.py:693-769`). El plugin los mete en el paquete
  0, en la lista de **depots** (`+0x48`).
- `FakeAppIds`, `AppTokens` y `DlcData` en el modo clásico.

Reglas de `canary` (`.agents/AGENTS.md` §1):

- Un AppID nunca va a `AdditionalDepots`.
- En modo solo-DLC, el appid base sale de `AdditionalApps` salvo que el usuario
  lo marque.
- Los redistribuibles quedan protegidos: `is_depot_shared_with_other_games`
  impide quitar un depot que otro juego registrado use
  (`yaml_config_manager.py:1637-1749`).

Los juegos que la cuenta posee de verdad se saltan en el escaneo, salvo que
estén en el registro AT0-M o en modo DLC (`game_manager.py:860-862,899-901`).

**"All games online (beta)"** escribe `FakeAppIds: 0: 480`
(`ui/dialogs/settings_sls.py:336-344`). Esa entrada cubre todo juego que la
cuenta no posee y no tiene entrada propia, incluidos los de `DenuvoGames`
(§4.1-4).

Family sharing: nada.

### 2.3 DLC

Hay un modo **DLC Only** para el DLC de un juego que ya tienes:

- Solo los appids de DLC van a `AdditionalApps`.
- Los depots base no entran en `AdditionalDepots`.
- Las claves se limitan a los depots elegidos (`native_steam_download_task.py:647-675,798-825`;
  `utils/dlc_helpers.py`). El appid se quita de `FakeAppIds` y de las opciones
  de lanzamiento (`:867-869`).
- El mapa appid → depots sale de un "pipeline heurístico de 7 niveles" sobre
  comentarios del lua, appinfo y nombres (`depotmapping.md`, `depots_tab.py`).

En el clásico, el DLC de un juego añadido va a `DlcData` cuando tiene 64 o más.
No hay criterio "todo" ni unlock por juego en marcha. Desinstalar un juego
solo-DLC por el clásico **no borra nada**: busca el DLL en
`deps/depot-downloader/DepotDownloader.dll`, que no existe
(`game_manager.py:1842`).

### 2.4 Claves y manifests

**Claves.** Del `.lua` del zip de Hubcap: `addappid(id, 1, "hex")`, más la clave
raíz del appid.

- Se guardan en `cached_luas/<appid>.lua` y en SQLite (`db/depot_keys.db`;
  `DepotKeyManager`), y se reutilizan para siempre.
- La tarea nativa las busca en este orden: `cached_luas` → zip de Hubcap
  (`/manifest/{appid}?force_update=true`) → SQLite → `game_data`
  (`native_steam_download_task.py:451-563`).
- En AT0-M van al **`config.yaml` de SLSsteam**, en una sección
  `DecryptionKeys` que lee el plugin, no SLSsteam (`:771-860`).
- En el clásico van a `/tmp/mistwalker_keys.vdf`, de ruta fija y compartida,
  con los permisos por defecto.

**Manifests, motor propio** (`core/at0m.py`). El orden:

1. Caché de códigos.
2. Carrera wudrm ↔ ManifestDeX (`https://gmrc.wudrm.com/manifest/<gid>` y
   `https://manifest.manifestdex.com/<gid>`, `:285-337`).
3. El otro proveedor.
4. CDN de Valve: lista de `IContentServerDirectoryService` o tres servidores por
   defecto, `GET /depot/<d>/manifest/<gid>/5/<mrc>` (`:399-486`).
5. `DepotManifest`: descifra los nombres con la clave, recorta los nulos y
   serializa sin comprimir (`:494-548`).
6. Hubcap `/generate/manifest`.

Detalles del motor:

- **User-Agent de wudrm.** wudrm se pide con UA `curl/7.88.1` (`:131`), el que
  Cloudflare reta. Si responde algo que no es un número, se repite con
  `curl_cffi` imitando Chrome 120 y, si no, con `curl` del sistema y UA
  `Valve/Steam HTTP Client 1.0` (`:205-254`).
- **Caché sin caducidad.** Los códigos se guardan en memoria y en
  `db/mrc_cache.db` sin caducidad (`:150-185`). Solo se invalida un código que
  el CDN rechaza (`:650-658`).
- **Fallback sin comprobar.** Si el zip no se puede leer, el fallback devuelve
  la primera entrada tal cual, aunque la magia no cuadre (`:537-545`).
- **Hubcap sin comprobar.** Lo que llega de Hubcap no se comprueba contra el
  depot/gid pedido.

**Dónde deja los manifests**:

- **AT0-M**: copia los `.manifest` del zip cacheado
  (`hubcap_manifests/accela_fetch_<appid>*.zip`) a **todos** los `depotcache/`
  que encuentra: raíz de Steam, `.local/share`, `.steam`, Flatpak y la
  biblioteca destino. Sobrescribe si el tamaño difiere
  (`native_steam/native_steam_handoff.py:145-214`). El patrón
  `accela_fetch_<appid>*` también casa con appids que empiezan igual.
- **`fetch_and_install_manifest_for_update`** copia con `shutil.copy2` (siempre
  sobrescribe) a tres `depotcache` centrales (`at0m.py:986-1001`).
- **Clásico**: `/tmp/mistwalker_manifests/<d>_<gid>.manifest` y
  `<juego>/.DepotDownloader/`, y al `depotcache` de la biblioteca.

**Códigos dentro de Steam.** Cuando Steam pide un código, lo responde
`download.lua`:

- Copia leída el 2026-10-01: `curl.downloadString` (fork de `/usr/bin/curl` sin
  `--max-time`) dentro del hook de GMRC con el mutex global de Lua cogido.
- Un solo servidor, `http://gmrc.wudrm.com/manifest/`, con el UA de `curl`.
- Caché en memoria sin caducidad (§5.2).

`managers/mrc_config_manager.py` escribe un `mrc_config.lua/json` (wudrm por
`http://`, ManifestDeX de rescate) para una versión de prueba del plugin; ningún
lua del repo lo lee.

**Descargador clásico** (`core/tasks/download_depots_task.py:1053-1105`):
`dotnet DepotDownloader.dll -app A -depot D -manifest G -manifestfile … -depotkeys
/tmp/mistwalker_keys.vdf -max-downloads 4 -dir … -validate`, anónimo, con
`-use-lancache` por defecto, `-probe-cdn` opcional y `-loginid` aleatorio. Por
cada depot que sigue sin clave **vuelve a bajar el zip entero de Hubcap**
(`:831`). Restaura symlinks desde el manifest uniendo nombres del manifest a la
ruta del juego, sin contención (`utils/manifest_resolver.py:120-151`).

**Si cae cada servicio**:

- **Hubcap**: sin claves no hay juego nuevo en ningún modo.
- **wudrm y ManifestDeX**: los manifests salen de Hubcap (cuota `single`), y
  Steam no puede reinstalar ni actualizar un juego AT0-M sin el manifest en
  `depotcache`.
- **R2**: los plugins desplegados siguen; una instalación nueva no.

### 2.5 Updates de juegos

**Clásico.** `manifest_check_task.py` compara los gids de `depots/<appid>.depot`
con los de Steam (PICS, `api.steamcmd.net`) para la rama elegida. Un cambio de
build sin cambio de gid no cuenta. Se comprueba al arrancar, a mano, con un
temporizador y en modo helper. Actualizar = volver a bajar con
DepotDownloader. Tres fallos trazados en el código:

- Tras un update, el `.acf` conserva el `buildid` viejo
  (`task_manager.py:278-280,1513-1524`).
- Un update con depots saltados se marca al día.
- Reescribir un `.acf` existente aplana claves anidadas
  (`utils/steam_manifest.py:160-168,229-233`).

**AT0-M.** Steam actualiza, si `DisableUpdates` no está a `yes` y si consigue
el código.

- `managers/at0m_update_checker.py` comprueba los juegos AT0-M (los de LumaDeck
  incluidos, §4.1-1) contra PICS y `/contents` de Hubcap.
- `apply_update` baja el zip entero (cuota de 25) y añade depots y claves
  nuevos (`:496-560`).

**Pins y rollback.** `ManifestIds` por `pin_build`, rollback o una
recomendación de `voices.json` (`steam_manifest_pinning.py:163-218`). El bloque
se **reconstruye entero**: cualquier línea que no sea `dígitos: dígitos` se
pierde (`:41-58,93-104`). Los pins de LumaDeck (`depot: gid` sin comillas)
sobreviven. Despinear no borra la entrada (§5.2, 2026-10-01).

**`DisableUpdates`** se escribe con el valor de la casilla en cada guardado de
Ajustes (§4.1-2).

Betas: el bundle de Hubcap solo es la rama pública. Las betas se montan en
cliente con los gids de PICS y `/generate/manifest` (`core/branch_bundle.py`).

### 2.6 Fixes y DRM

En la ficha de cada juego:

- **Steamless** (Python y .NET, `deps/Steamless`).
- **Goldberg** (`deps/Goldberg`, aplicar/quitar, opcional al instalar).
- **Proxy EOS** (`utils/eos_detector.py`):
  - `apply_proxy` borra el original `.yes` si ya hay uno y renombra el `.dll`
    actual sin comprobar si ya es el proxy (`:180-189`). En un árbol con
    carpetas mixtas destruye el original.
  - La aplicación automática tras una descarga nunca corre:
    `task_manager.py:1326` llama a `.get()` sobre un string.
- **`fix.so` de netsock.** Lo baja de
  `yesyes0649/steamnetsock-patch/releases/download/latest/` sin hash
  (`yaml_config_manager.py:2766-2775`) y lo pone en `LaunchOptions` como
  `LD_AUDIT` aunque la descarga falle (`ui/dialogs/game_details/info_tab.py:1618-1621`).
- **Tickets de propiedad** (`utils/ticket_manager.py`):
  - Importa y verifica `encryptedTicket_<appid>.yaml` en
    `~/.config/SLSsteam/Tickets`, ruta fija y no válida para Flatpak (`:16-17`).
  - Importar copia el fichero sin validarlo y añade el appid a `AdditionalApps`
    (`:225-254`).
- **Denuvo**: solo vacía el bloque `DenuvoGames` una vez (`main_window.py:281-288`).
  Lista de estado de Denuvo de un Supabase con clave anónima embebida
  (`core/ratings.py:127-134`).
- **Spliced tickets**: el plugin, más `SmartTickets: 0x1` (§2.1).

### 2.7 Logros, stats y tiempo de juego

- "SHSAH Reborn" (`managers/shsah_reborn.py`) desbloquea logros editando
  `appcache/stats/UserGameStats_<cuenta>_<appid>.bin`. Reinterpreta los enteros
  como 32 bits con signo y pierde los tipos de 64 (`:69-81,345-372`).
- "Generate Achievements" usa SLScheevo y `schema-grabber`, con login de Steam.
- Sin sincronización entre máquinas.
- Lo que conteste `GetUserStats` es cosa de SLSsteam.

### 2.8 Cloud saves

Nada.

### 2.9 Añadir y quitar un juego

**Entradas**: búsqueda en Hubcap, zip arrastrado, web UI, CLI, `accela://`.
Antes de descargar, el selector de depots ("Smart Select" con
`packageinfo.vdf`).

**Qué escribe**:

- **Clásico**:
  - Los ficheros del juego, `.DepotDownloader/` con manifests y `.sha`, y
    `depots/<appid>.depot`.
  - El `.acf` a mano, salvo con `experimental_acf_independent`; con esa opción
    manda `install|appid|0` y Steam escribe el `.acf`. `InstalledDepots` solo se
    rellena en Windows, así que en Linux queda vacío (`task_manager.py:1701`).
  - En el config, `AdditionalApps`, `AppTokens` (sin escapar) y `DlcData`.
- **AT0-M vigilado** (`native_steam_download_task.py:95-366`):
  1. Comprueba que hay un `steam` con SLSsteam.
  2. Borra un `.acf` stub con `buildid 0` o `StateFlags 1026`.
  3. Resuelve las claves.
  4. Hace un respaldo único `config.yaml.native_steam_backup` (solo si no
     existe).
  5. Parchea `Plugins`, `AdditionalApps`, `AdditionalDepots` y
     `DecryptionKeys`; con pin, `ManifestIds`.
  6. Espera la licencia.
  7. Siembra `depotcache`.
  8. Manda `install|appid|lib` hasta 8 veces cada 3 s.
  9. Vigila `content_log.txt` y `StateFlags`.
  10. Al terminar registra el juego en `plugin_library.json` y borra los
      marcadores de ACCELA.

  En fallo o cancelación: **copia el respaldo entero sobre `config.yaml`**,
  borra el `.acf` si no está en estado 4 y manda `uninstall|appid`
  (`:1174-1224`). La vigilancia tiene un tope **absoluto** de 3.600 s
  (`:45,1014`): una descarga que pase de una hora se da por fallida, con esa
  misma limpieza. La pausa no hace nada (`:92-93`).
- **AT0-M handoff** (`native_steam_handoff.py:217-463`; el camino por defecto
  para juegos ya instalados y para "Add to Steam"): lo mismo sin vigilar y sin
  respaldo. El vigilante de `task_manager` mira el `.acf` 30 min
  (`:1003-1058`).

**Quitar**:

- En juegos AT0-M, "Uninstall", "Advanced Uninstall", "Verify" y "Reset Depot
  Selection" están desactivados (`ui/dialogs/library/actions.py:507-543`;
  `ui/dialogs/game_details/info_tab.py:442-490`). Desinstalar desde Steam deja
  `AdditionalApps`, `AdditionalDepots`, `DecryptionKeys` y `AppTokens` (§5.2).
- En los demás, "Uninstall" borra la carpeta y quita del config `AdditionalApps`,
  `DlcData`, `LaunchOptions` y `FakeAppIds` del appid
  (`managers/game_manager.py:2046-2064`).
- La opción "I bought the game" (`_wipe_sls_only`, `actions.py:659-776`) deja los
  ficheros. Quita del config, para el appid, sus DLC y sus depots: `AdditionalApps`,
  `FakeAppIds`, `AppTokens`, `LaunchOptions` y `DlcData`, más `GameTitles`,
  `ManifestIds`, `DepotBlacklist` y `CDKeys` (`:713-745`). También manda
  `uninstall|appid` por el pipe (`:770-776`).
- **"Move to ASSella"** (`info_tab.py:2728-2800`) pasa un juego AT0-M al
  descargador clásico. Quita del config sus depots y claves (deja
  `AdditionalApps`), crea `.ACCELA` en la carpeta y lanza la verificación con
  DepotDownloader sobre ella.

**El canal `/tmp/SLSsteam.API`**:

- Se abre con `open(…, "w")` bloqueante, sin `O_NONBLOCK` ni comprobar que sea
  un FIFO: un FIFO viejo sin lector cuelga el hilo
  (`utils/slssteam_integration.py:416`, `core/steam_helpers.py:634`,
  `native_steam_download_task.py:909`, `native_steam_handoff.py:122`).
- `steam_helpers.slssteam_api_send` no mira si existe, así que puede crear un
  fichero normal en esa ruta (`:626-641`). Desde entonces
  `_is_slssteam_available()` da SLSsteam por vivo (`slssteam_integration.py:120-124`).

**Watcher de SLSsteam muerto**: busca `Failed to read from FileWatcher` en los
últimos 64 KB del log, avisa y escribe un `.acf` de respaldo
(`slssteam_integration.py:196-257`).

Varias bibliotecas: elige el índice por `get_library_index`.

### 2.10 Credenciales y proveedores

**Clave de Hubcap** (`morrenus_api_key` en QSettings, en claro):

- Viaja como `Authorization: Bearer` en todas las llamadas
  (`core/morrenus_api.py:72-79`), y también en la query de `/user/stats`
  (`:269`).
- **La sesión de Hubcap no verifica TLS**: `check_hostname=False`, `CERT_NONE`,
  `SECLEVEL=1`, `verify=False` y avisos silenciados (`:36-66`).
- **Transporte `auto`, el de por defecto** (`utils/isp_bypass.py:474-648`):
  1. Directo.
  2. DoH a `1.1.1.1` y `dns.google`, también sin verificar
     (`:438-440`); sustituye `socket.getaddrinfo` en todo el proceso mientras
     dura la petición (`:513-527,589-619`).
  3. Tor: lanza un `tor` del AppImage y da por Tor cualquier cosa que escuche en
     `127.0.0.1:9050/9080` (`:78,154-187`).
  4. **Wirecutter**: Cloudflare Worker `rapid-thunder-fba1wirecutter.7ucking.workers.dev`,
     con la URL ofuscada por XOR y base85 (`:198-209`).

  Tor y el Worker reciben las mismas cabeceras, **clave incluida** (`:622-642`).
  La casilla `isp_bypass_hubcap` de la UI no la lee nadie.

**Cuota**: `API x/25 · Single y/1500` en la pantalla principal. No hay
contadores propios, ni backoff, ni `Retry-After`. El zip se pide siempre con
`force_update=true` y se repite sin él ante cualquier error, 429 incluido
(`morrenus_api.py:509-524`).

**Otros**: PICS anónimo con su propio cliente de Steam en Python; Byparr para
SteamDB.

Sin clave de Hubcap: los juegos ya añadidos siguen, y los manifests salen de
wudrm/ManifestDeX si se tienen las claves.

### 2.11 Mantenimiento propio

**Superficies**: ventana Qt, web UI, CLI, helper. **Updater**
(`managers/app_update_manager.py`):

- Recorre la lista de releases filtrada por canal: `canary`/`testing`/`3.`,
  `beta`/`dev`/`rc` o estable.
- Bajada delta (zsync o `appimageupdatetool`) **sin comprobación**
  (`:201-264`). Con `appimageupdatetool` se llama `--self-update <target>`
  (`:193-196`).
- AppImage entero con `.sha256` de la misma release: un desajuste aborta, pero un
  404 se salta la comprobación (`:310-339`). Sin firma.
- `AppRun` exporta `APPIMAGE=1`, así que el destino siempre es
  `~/.local/share/ACCELA/ASSella.AppImage` (`.github/workflows/build-appimage.yml:176,185`).
- El fallo `vv` de la versión ya está corregido (`:52-56`). Uno nuevo: sin
  versión remota conocida pide `/releases/tags/` vacío (`:61,100`).
- Las versiones de canary (`DDMMYY` + secuencia) se comparan como enteros, que
  no es orden cronológico entre meses (`main_window.py:2324-2349`).

**`install.sh`**:

- Usuario, en `~/.local/share/ACCELA`; `sudo` para paquetes de la distro
  (pacman `-Sy` con actualización parcial, paquetes i386 de apt).
- `.sha256` si existe; si falta solo avisa, salvo con `ASSELLA_REQUIRE_SHA256=1`
  (`:388-434`).
- Siempre canal estable, con v2.6.5 fija de reserva (`:163`).
- Registra `accela://`. No instala SLSsteam.

**Otros**:

- "Install Byparr" ejecuta un `curl | bash` del propio repo fijado a commit,
  que a su vez hace `curl … uv/install.sh | sh` y clona Byparr sin fijar
  (`scripts/setup_byparr.sh:30,47`).
- **`/tmp/byparr_test` está en la ruta de búsqueda de Byparr.** Si no hay un
  Byparr en `~/.local/share/ACCELA/byparr`, en cada arranque de la GUI se ejecuta
  el `assella_runner.py` o el `main.py` que haya ahí, y cualquier usuario local
  puede ponerlo (`core/steamdb_scraper.py:88-96,152-189`;
  `main_window.py:248-251,1530-1555`).
- **Salud**: pestaña Health con binario, versión y proceso de SLSsteam, y el
  reparador `assfixer`:
  - Hoy falla siempre: nombre indefinido `template_url` (`utils/assfixer.py:1299`),
    y la plantilla local de reserva no se distribuye.
  - Si funcionara, reescribiría el fichero entero con solo las claves de la
    plantilla (más `AdditionalDepots`/`DecryptionKeys`), perdiendo valores con
    espacios y aplanando `DlcData` (`:600,521-568,1004-1086,1334`).
- **Logs**: `~/.local/share/ACCELA/logs`, con redacción de secretos salvo en la
  copia de stderr (`utils/logger.py:26-75,290-338`).

### 2.12 Proyecto

- **Mantenimiento**: un mantenedor, desarrollo con agentes de IA y ritmo muy
  alto (22 commits en `canary` del 07 al 09-oct).
- **Licencia**: sin licencia.
- **Build**: AppImage sin firma, construido en CI con `appimagetool`/`appimageupdatetool`
  "continuous" y python-build-standalone sin hash, `requirements.txt` sin fijar
  (`steam.git@master`), acciones fijadas por etiqueta, y un `tor` copiado de
  `/usr/bin/tor` de Ubuntu (`build-appimage.yml:124,205,245-254`). Los fallos
  de la suite previa al release se ignoran (`|| true`, `:429,435`). El repo de
  Arch recomienda `SigLevel = Optional TrustAll` (`:664`).
- **Terceros de los que depende**: Hubcap; wudrm; ManifestDeX; Valve (PICS,
  CDN, Web API); `api.steamcmd.net`; Cloudflare (R2, el Worker); GitHub
  (releases, `broadcast.json`, `voices.json`, `fix.so`); Supabase; Headcrab;
  Byparr.
- **Privacidad**: sin telemetría y sin subir claves, `config.vdf` ni logs.

---

## §3 Superficies externas

### 3.1 Hosts y URLs

| Host | Para qué | Notas |
|---|---|---|
| `hubcapmanifest.com/api/v1` | zips, `/generate/manifest`, `/contents`, `/status`, `/search`, `/user/stats`, `/generate/usage`, `/generate/workshopmanifest` | TLS sin verificar; `Bearer` |
| `rapid-thunder-fba1wirecutter.7ucking.workers.dev` | proxy de Hubcap, cuarto escalón | recibe la clave |
| `1.1.1.1`, `dns.google` | DoH | sin verificar |
| `gmrc.wudrm.com` | códigos (motor propio por HTTPS; `mrc_config` y el plugin por HTTP) | UA `curl/7.88.1`, luego Chrome 120 |
| `manifest.manifestdex.com` | códigos | UA `ManifestDeX/1.0` |
| `api.steampowered.com`, `*.steamcontent.com`, `content1.steampowered.com` | CDN, directorio de servidores, `GetPublishedFileDetails` | UA `Valve/Steam HTTP Client 1.0` |
| `api.steamcmd.net`, `store.steampowered.com` | depots, gids, nombres | caché SQLite de 14 días |
| `pub-19657b4f385d424b91a909253efdb29c.r2.dev` | `plugins_manifest.json` y plugins | hash del mismo origen |
| `github.com/niwia/ASSella`, `raw.githubusercontent.com` | releases, `broadcast.json`, `voices.json`, `src/res/version` | sin firma |
| `github.com/yesyes0649/steamnetsock-patch` | `fix.so` | `latest`, sin hash |
| `headcrab.pages.dev` | instalador de SLSsteam | `curl \| bash`, sin esquema → `http` |
| Supabase de niwia | estado de Denuvo | clave anónima embebida |
| `steamdb.info` vía Byparr (`127.0.0.1:8191`) | historial de builds | |
| ProtonDB, Google Fonts | adorno | |

### 3.2 Ficheros

| Ruta | Qué | Quién más la toca |
|---|---|---|
| `~/.config/SLSsteam/config.yaml` | ASSella escribe `API`, `LogLevels`, `Plugins`, `SmartTickets`, `DisableUpdates`, `AdditionalApps`, `AdditionalDepots`, `DecryptionKeys`, `DlcData`, `FakeAppIds`, `AppTokens`, `LaunchOptions`, `ManifestIds`, `DenuvoGames`. Lo hace por regex sobre el texto (PyYAML solo valida duplicados de primer nivel), en el sitio (`r+`, `truncate`, `fsync`), sin bloqueo (`yaml_config_manager.py:84-105,164-186`). Si falta, lo crea | SLSsteam, LumaDeck |
| `config.yaml.bak`, `config.yaml.native_steam_backup` | copia en cada arranque de la GUI (pisa la anterior); respaldo de la tarea nativa (nunca se borra si nadie lo restaura) | — |
| `~/.config/SLSsteam/plugins/` | `download.lua`, `spliced-tickets.lua`, `.bak` | LumaDeck (`lumadeck-spliced-tickets.lua`) |
| `~/.config/SLSsteam/mrc_config.{lua,json}`, `tools/netsock/`, `Tickets/` | config de códigos de prueba, `fix.so`, tickets | — |
| `/tmp/SLSsteam.API` | `install\|appid\|lib`, `uninstall\|appid` | SLSsteam |
| `<Steam>/depotcache/` (todos los que encuentra) | manifests de Hubcap y del motor propio | Steam, LumaDeck |
| `steamapps/appmanifest_*.acf` | a mano en el clásico; respaldo si el watcher está muerto | Steam |
| `appcache/stats/UserGameStats_*.bin` | editor de logros | Steam |
| `/tmp/mistwalker_keys.vdf`, `/tmp/mistwalker_manifests/` | claves y manifests del clásico | — |
| `/tmp/byparr_test/main.py` | se ejecuta si existe | cualquier usuario local |
| `~/.local/share/ACCELA/` | AppImage, `db/` (claves, códigos, Steam), `cached_luas/`, `hubcap_manifests/`, `plugins/`, `plugin_library.json`, `depots/`, logs | — |
| `~/.config/ACCELA/ACCELA.conf` (QSettings) | ajustes y clave de Hubcap en claro | — |

### 3.3 Puertos

`127.0.0.1:8765` web UI (o `0.0.0.0` con `web_ui_allow_lan`). La autenticación
es un token por arranque (`secrets.token_urlsafe(32)`) que se compara con
`hmac`; llega por la cabecera `X-ASSella-Token` o por `?token=`.

`GET /` no pide token y devuelve el HTML con el token dentro, sin mirar la
cabecera `Host` (`web_server.py:115-119,175-191`). Una página con DNS rebinding
puede leerlo.

`POST /api/update` acepta un `local_path` del cliente (`:385,613-617`).

En `beta`: `0.0.0.0:8765`, CORS `*`, sin token.

Además, Byparr en `127.0.0.1:8191` y Tor en `9050`/`127.0.0.1:9080`.

### 3.4 Ajustes que importan

| Ajuste | Por defecto | Efecto |
|---|---|---|
| `enable_at0m` | `True` en Linux | ofrece el modo nativo |
| `at0m_default_download_action` | `ask` | diálogo clásico/nativo |
| `at0m_disable_updates` | casilla marcada si el config no dice otra cosa | `DisableUpdates: yes` al guardar |
| `isp_bypass_mode` | `auto` | cadena hasta el Worker |
| `all_games_online_beta` | `False` | `FakeAppIds: 0: 480` |
| `custom_plugins_advanced` | `False` | sin comprobar plugins |
| `use_experimental_ddm`, `probe_cdn`, `use_lancache` | `False`, `False`, `True` | descargador clásico |
| `web_ui_allow_lan` | `False` | web en la LAN |

---

## §4 Lo que nos afecta

### 4.1 Hallazgos pendientes de decisión

Aparcados el 2026-10-09. Solo lo que puede pasarle a alguien que tenga ASSella y
nuestra pila en la misma máquina, o que use los mismos servicios. Lo demás de
§2 es de ASSella y no nos toca.

1. **ASSella trata los juegos de LumaDeck como suyos.** Todo juego instalado
   cuyo appid esté en `AdditionalApps` sale en su biblioteca como "AT0-M"
   (`game_manager.py:850-860`, y una segunda pasada directa por
   `AdditionalApps`, `:896-929`).

   - Entra en su comprobación de updates (`at0m_update_checker.py:94-140`).
   - "Update" lo manda al handoff (`task_manager.py:701-744`). El handoff
     escribe en el config `AdditionalDepots` y `DecryptionKeys` con las claves
     de Hubcap. Si el build está fijado, escribe además `ManifestIds`, encima de
     los pins de `pins.py`, y gasta un zip de la cuota.
   - **"Move to ASSella"** (`info_tab.py:2728-2800`) crea `.ACCELA` en la
     carpeta del juego y lanza DepotDownloader a verificar los ficheros que
     instaló Steam. Desde ese momento el juego es "ACCELA" para ASSella:
     - lo actualiza su descargador;
     - su "Uninstall" queda activo y quita del config `AdditionalApps`,
       `FakeAppIds`, `DlcData` y `LaunchOptions` del appid
       (`game_manager.py:2046-2064`). Con "I bought the game", también
       `ManifestIds` (`actions.py:713-745`).

     `keys.txt` y el archivo de manifests de LumaDeck se quedan, y el juego
     queda a medias en las dos herramientas.

   Desinstalar un juego AT0-M desde ASSella no se puede (botón desactivado).
   Sin medir con las dos instaladas.

2. **Flags de SLSsteam que LumaDeck necesita y ASSella pisa.**

   - **`DisableUpdates`.** Cada "OK" en los Ajustes de ASSella escribe el valor
     de su casilla (`settings.py:1001-1014`), que nace marcada (`at0m_tab.py:213`).
     Resultado: `DisableUpdates: yes`, y los juegos añadidos dejan de
     actualizarse (`slssteam.md` §2.5).
   - **`Plugins`.** Desmarcar AT0-M escribe `Plugins: no` (`settings.py:983-998`),
     que apaga también `lumadeck-spliced-tickets.lua`.

   LumaDeck solo repone `DisableUpdates: no` y `Plugins: yes` al cargar su
   backend (`main.py:151-183` → `installer.ensure_slssteam_flags`). Hasta el
   siguiente arranque de Decky manda lo que haya escrito ASSella. Con el
   `SmartTickets` pasa algo parecido (punto 4).

3. **Escrituras simultáneas en `config.yaml`.** ASSella escribe en el sitio con
   `r+` y `truncate` (`yaml_config_manager.py:164-186`). LumaDeck escribe en un
   temporal y lo renombra (`installer.py:252-255`; `slssteam_ops.py`). Ninguno
   bloquea el fichero. Dos lecturas-modificaciones cruzadas pierden la
   escritura del primero.

   Lo que agranda la ventana es la **tarea nativa vigilada**. Guarda una copia
   al empezar y, si la descarga falla o se cancela, la copia entera sobre
   `config.yaml` (`native_steam_download_task.py:627-632,1183-1192`). Lo que
   LumaDeck haya escrito mientras tanto se pierde: hasta 3.600 s de
   `AdditionalApps`, pins o flags. Y toda descarga que pase de una hora acaba
   en ese camino (`:45,1014`). Lo mismo con el "Restore Backup" de Health, que
   restaura el `.bak` más reciente.

4. **`FakeAppIds: 0: 480` y `SmartTickets` frente a Denuvo.**

   - "All games online" escribe `0: 480` (`settings_sls.py:336-344`).
     SLSsteam aplica la clave `0` a todo juego no poseído sin entrada propia.
     Sin el bit `0x2` de `SmartTickets`, un juego con FakeAppId no recibe su
     ticket cifrado cacheado (`ticket.cpp:188-197`).
   - Desplegar `spliced-tickets.lua` reescribe `SmartTickets` a `0x1` si el
     valor no es literalmente ese (`yaml_config_manager.py:325-366`), y un `0x3`
     pierde el bit de Denuvo.

   Las dos cosas juntas dejan sin activar los juegos de `DenuvoGames`, y el
   comodín se salta la negativa de `fixes.enable_online` de LumaDeck
   (`fixes.py:837`). La ficha de LumaDeck no lo enseña; Ajustes sí.
   Disparador: un Denuvo que deja de activar con ASSella instalado.

5. **`download.lua` encima de lumalinux.** Medido en código el 2026-09-29
   (§4.4 A5): los dos enganchan `GetBinary` y el sitio de llamada de GMRC, y
   `LuaHook::place` de SLSsteam no corrige el `jmp` relativo que deja
   lumalinux.

   - **Plugin cargado con lumalinux ya enganchado**: Steam se cae.
   - **Plugin presente al arrancar Steam**: lumalinux se encadena encima; al
     recargarse el estado Lua, el plugin restaura su prólogo limpio y
     **lumalinux queda mudo** hasta reiniciar Steam.

   Lo que cambia hoy: los plugins ya no van y vienen con cada descarga; se
   despliegan una vez y se quedan (§2.1), así que el caso normal es el segundo.
   Cada despliegue o borrado en la carpeta, y cada `Plugins: no/yes`, recarga
   el estado Lua (`slssteam.md` §2.11).

   El mutex de Lua es uno para todos los plugins. Mientras el GMRC de
   `download.lua` hace su `curl` sin `--max-time`, nuestro hook de tickets
   espera (§5.2, 2026-09-29).

6. **`depotcache` compartido sin validar.**

   - **ASSella** sobrescribe ficheros de `depotcache/` con lo que trae Hubcap
     sin comprobar que el manifest sea del depot/gid del nombre
     (`native_steam_handoff.py:205-206`, `at0m.py:986-1001`, `:537-545`). El
     glob `accela_fetch_<appid>*` copia también los de appids que empiezan
     igual.
   - **LumaDeck** da por bueno cualquier fichero que ya esté ahí
     (`manifests.py:513-514`) y no lo repone.

   Un manifest malo dejado por ASSella se queda (nuestro lado en
   `nosotros.md` §4.1-31).

7. **El botón "Standard Plugin" borra nuestro plugin.** Ofrece borrar todos los
   `.lua` de la carpeta salvo los suyos, sin copia (`at0m_tab.py:584-610`), y
   `lumadeck-spliced-tickets.lua` entra. Pregunta antes, con la lista. LumaDeck
   lo repone al cargar (`installer.py:326-390`).

8. **La misma clave de Hubcap, expuesta y compartida.**

   - **Exposición.** ASSella manda la clave sin verificar TLS (`morrenus_api.py:36-66`)
     y, en `auto`, a Tor y a un Worker de un tercero (`isp_bypass.py:622-642`).
     Si es la misma que el usuario tiene en LumaDeck, cualquiera en ese camino
     puede usarla y gastarle la cuota.
   - **Cuota compartida.** ASSella gasta zips (25/día) en cada descarga nueva,
     en cada depot sin clave del clásico (`download_depots_task.py:831`) y en
     cada `apply_update`. LumaDeck se queda sin zip el mismo día. En cambio,
     la cuota `single` (1.500) sobra para los dos.

9. **Dos `spliced-tickets` a la vez.** El de `beta` es el de Ace, igual al
   nuestro sin la cabecera, con la misma guarda global `SplicedTickets.setup`.
   Con los dos cargados, el segundo no engancha nada: sin daño. La copia de R2
   que usa `canary` está sin leer. Si divergiera de la guarda, habría
   dos hooks sobre la misma función.

### 4.2 Dependencias y qué rompe si cambia

| Si cambia… | Qué nos pasa |
|---|---|
| ASSella pasa AT0-M a `beta` (la rama por defecto) | §4.1-1, -2, -5 y -7 llegan a todos sus usuarios, no solo a los de `canary` |
| el `download.lua` de R2 | nada directo; la caché de ASSella no lo refresca (§2.1). Si quita la guarda de spliced tickets, §4.1-9 |
| Hubcap cierra `/generate/manifest` o recorta cuotas | lo mismo para los dos; ASSella gasta antes la de zips |
| wudrm o ManifestDeX caen | lumalinux tiene 20770407 y steam.run; ASSella se queda en Hubcap |
| ASSella | nada si no está instalado en la misma máquina |

### 4.3 Lo que ASSella tiene y nosotros no (y al revés)

| Función | ASSella | Nosotros | Notas |
|---|---|---|---|
| 2 | `AdditionalDepots` en la lista de depots del paquete 0 (plugin) | ids de `keys.txt` en la lista de apps (`+0x38`), reconcile sin reiniciar | RESEARCH: `+0x48` probado y descartado |
| 4 | carrera wudrm ↔ ManifestDeX con imitación de Chrome; claves en `DecryptionKeys` del config | cascada de cuatro con UA propio y validación en el CDN; claves en `keys.txt` y `config.vdf` | la imitación no nos hace falta: el filtro es por UA (§5.2) |
| 4 | descarga fuera de Steam (DepotDownloader) | Steam descarga | descartado por diseño |
| 5 | rollback a un build, `voices.json` de builds recomendados | `pins.py`, Change version | |
| 6 | Steamless, Goldberg, EOS, netsock, tickets, editor de logros | fixes por botón | tickets aparcados con la fila de SLSsteam |
| 9 | Workshop (DepotDownloader `-ugc`) | nada | §4.4 A9 |
| 9 | detección del watcher de SLSsteam muerto | nada | §4.4 A4 |
| 11 | reparador del config que reescribe | completado que solo añade | |
| 8 | nada | CloudRedirect | |

### 4.4 Decisiones ya tomadas (del doc anterior, con fecha) y estado hoy

| # | Decisión | Fecha | Hoy |
|---|---|---|---|
| A1 | `/generate/manifest` de Hubcap como eslabón de `manifests.py`, con disparador (P-ToyStore caído o "installed build's manifests are missing") | 2026-09-14 | **hecho** (LumaDeck `47f719a`, 2026-10-07: P-ToyStore dio 404 ese día; un intento por depot+gid al día, cuota `single`) |
| A2 | Preflight gratis `/contents` antes del zip: descartado | 2026-09-14 | vigente |
| A3 | `/tmp/SLSsteam.API` no aplica a nosotros (solo sirve si se descarga fuera de Steam) | 2026-09-14 | vigente (`slssteam.md` §2.11) |
| A4 | Detectar el watcher muerto de SLSsteam en LumaDeck (tail del log, aviso "reinicia Steam"): candidato, sin OK | 2026-09-17 | sin hacer; arreglado upstream en `dev`, sin publicar a la fecha de la nota |
| A5 | `canary` nativo sobre lumalinux (crash u lumalinux mudo): causa en `LuaHook::place` de SLSsteam; **sin acción en código**; opciones apuntadas para el autor: (1) informe a Ace, (2) autocomprobación de prólogos en lumalinux con reinstalación (~60 líneas), (3) aviso en LumaDeck si ve `download.lua`, (4) sus patrones en `check_patterns.py` como línea informativa, (5) preguntar a Steam antes de responder en GMRC | 2026-09-29 | vigente; ninguna opción tomada; agravado por plugins residentes (§4.1-5) |
| A6 | Spliced tickets como **plugin**, no como hook inline (un cuarto punto de choque) | 2026-09-29 | **hecho** (LumaDeck `b70ea44`) |
| A7 | Exclusiones: rollback a build antiguo, Workshop, Steamless/Goldberg/EOS/netsock dentro de la descarga, tickets, logros con `schema-grabber`, scraper de SteamDB | 2026-09-14 | vigente salvo Workshop (A9) |
| A8 | Aviso en la ficha de LumaDeck si `FakeAppIds` tiene `0` (y en Denuvo con `SmartTickets` sin `0x2`); baja prioridad; volcarlo en un futuro *exportar diagnóstico* | 2026-10-04 | sin hacer (§4.1-4) |
| A9 | Workshop: medir el nativo con nuestra pila antes de construir nada (Steam intentó bajar los items solo, con el juego desbloqueado, y falló solo por los códigos) | 2026-10-06 | pendiente de medir |
| A10 | AGPL: si algo de lumalinux/LumaDeck aparece en ASSella, se dice una vez con el enlace; pacto con niwia de avisarse antes de culpar al otro si alguien llega con un crash y las dos herramientas | 2026-10-01 | vigente |
| A11 | Exportar diagnóstico desde el plugin (versión, canal de Steam, `status.json`, hash de `steamclient.so`, `FakeAppIds`, `SmartTickets`) para soporte | 2026-10-01 | sin hacer |
| A12 | Catálogo "Voices" de builds recomendados: idea limpia, sin acción | 2026-09-23 | vigente |
| A13 | Hubcap `/manifest/{app}` solo sirve la rama pública (verificado por KingCatto contra el fuente del servidor): nada que cambiar, no ofrecemos ramas | 2026-09-17 | vigente |
| A14 | Repetir T2/V3 (desinstalar y reinstalar desde Steam con y sin proveedores) con LumaDeck actual | 2026-10-05 | sin hacer |

---

## §5 Historial

### 5.1 Lo que se creía y ya no es así

Además de lo listado en la cabecera:

- **"Una sola fuente: Hubcap; sin proveedores de códigos".** Desde el 24-sep
  (`7413be8`) el motor propio pide códigos y baja del CDN; Hubcap es el
  rescate. Hoy corre dos proveedores en carrera.
- **"Steam nunca actualiza un juego de ASSella".** Cierto para el clásico (ACF a
  mano, `DisableUpdates`). En AT0-M actualiza Steam si `DisableUpdates` lo
  deja.
- **"ASSella no escribe `ManifestIds`".** Lo escribe desde el 28-sep para pins
  y rollback, y reconstruye el bloque entero.
- **"Los plugins se despliegan al empezar cada descarga y se borran al
  acabar".** Ya no: se despliegan desde Ajustes o la bienvenida, una vez, y
  `_deployed_plugins` no se llena nunca, así que la limpieza no borra nada
  (`native_steam_download_task.py:79,1155-1163`).
- **"Si faltan los lua empaquetados, baja `plugins-deps.zip` de
  `ciscosweater/enter-the-wired`".** Ninguna referencia en `canary@73f0471`.
- **"El web server escucha en la LAN sin autenticación".** Cerrado en `canary`
  el 07-oct (`9160eeb`); sigue así en `beta`.
- **"El fallo `vv` del updater sigue en `beta`".** Corregido en los dos.
- **"`assfixer` vacía el fichero antes de rellenarlo" (los "Missing …" espurios).**
  Sigue escribiendo con `"w"` (`assfixer.py:1334`), pero hoy no llega a
  escribir: falla antes por `template_url`.
- **"Las escrituras de `yaml_config_manager.py` de §6.5 sin re-auditar".**
  Auditadas hoy (§3.2): en el sitio, sin bloqueo, sin temporal.
- **"`NameError` en `native_steam_download_task.py:182`".** Ya no está.
- **"P-ToyStore está en nuestra cadena".** Retirado el 2026-10-07 (404); la
  comparación de "cuatro fuentes antes de Hubcap" es hoy depotcache → archivo
  propio → luastools → Hubcap suelto → zip (`manifests.py:1-37`).

**Lo que sigue sin medir**:

- §4.1-1 a -3 con las dos herramientas instaladas.
- El `download.lua` y el `plugins_manifest.json` vivos de R2: la última
  lectura es la del 2026-10-01 (`84d6c23f`).
- Si el `spliced-tickets.lua` de R2 conserva la guarda global.
- El efecto de `DisableUpdates: yes` en un juego de ASSella con build nueva.

### 5.2 Pruebas y mediciones con fecha

#### Función 1 — Engancharse a Steam

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-29 | SteamOS + SLSsteam de serie, sin lumalinux; ASSella `canary@6677a05` | 15 recargas de `config.yaml` en 30 s con `download.lua` cargado | Steam vivo, 15 `Config reloaded`, cero errores | la reescritura de `writeDepotIds` es perezosa; su conteo no es medible en release (`log.debug` solo en debug) |
| 2026-09-29 | ídem | quitar y reponer `download.lua` 5 ciclos con Steam abierto | Steam vivo tras 10 reinicios del estado Lua | SLSsteam solo no tumba a Steam al desmontar hooks |
| 2026-09-29 | logs de un usuario y commits de la mañana (`cb3a49b` → `6677a05`) | cuelgue de Steam con 4 juegos en modo plugin | `io.popen` de `curl_chrome120` y `sleep` dentro del hook de GMRC con el mutex global de Lua cogido: hasta 70 s por manifest (5 intentos × (5 + 6 s) + 15 s de esperas) con `GetBinary` esperando el mismo mutex; Steam se congela | deducido de código y commits; revertido en `6677a05` |
| 2026-09-29 | fuente de SLSsteam (`curl.cpp`, `utils.cpp`) | qué es `curl.downloadString` | `fork` + `execve` de `/usr/bin/curl` con `--connect-timeout` y **sin `--max-time`**: todo `download.lua`, el de Ace incluido, bifurca Steam por cada código y puede esperar sin límite con el mutex | vale para nuestro hook de tickets en el mismo mutex (§4.1-5) |
| 2026-09-29 | log completo de un usuario (build `canary` del día, 6.495 líneas) | Steam no arranca | muere tras la primera petición a wudrm, sin línea de error | causa en la fila siguiente |
| 2026-09-30 | AppImage `3.0.0testing290926005` + LuaJIT 2.1 | la línea del crash | `download.lua` l.272 concatena el `uint64_t` `manifestId`: `attempt to concatenate 'string' and 'uint64_t'`; el error cruza un callback FFI sin `pcall` y Steam muere | el lua original de Ace lo tiene en otra rama, menos alcanzable |
| 2026-09-30 | ídem, inventario | AppImage | 837 MB, 17.702 ficheros; `.venv` 602 MB; `deps` 101 MB; `decky_loader` 3.2.6 de sobra; `tor` | |
| 2026-09-30 | fork `jayool/ASSella` rama `fix/mrc-out-of-hook` (`2fd6ecf`, `81213c6`), Balatro 2379780 | (A) instalar desde Steam sin código ni manifest con el GMRC fuera del hook | Steam vivo, "Unknown error" normal | demostración: el problema es de arquitectura (red dentro del hook) |
| 2026-09-30 | ídem | (B) instalar por la web con códigos precargados fuera de Steam (`curl_cffi`) | 3/3 códigos de wudrm, Steam baja 60 MB + 228989, `InstalledDepots` completo | después Steam pide el depot de shaders sin código → 401 y se rinde |
| 2026-10-01 | `download.lua` de R2, sha256 `84d6c23f…`, 10.656 B | lectura | crash de l.272 arreglado, hooks en `pcall`; sigue `curl` dentro del hook con el mutex, un servidor, caché sin caducidad | |
| 2026-10-01 | CI de lumalinux (`probe-steam`, runs 13-15) | patrones de `download.lua` en `.text` | `GetPackage` y `GMRC` únicos en Deck estable `bc54101b`, escritorio estable `237495b4` (1788652215) y beta `a3661f5b` (1790721607) | A y B mismo código, diferencia en datos |

#### Función 4 — Claves y manifests

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-14 | `/generate/usage` con la clave del usuario | cuotas de Hubcap | zip y `/lua/*` 25/día; `single` 1.500/día; `bundle` 100 (retirado, 503); `workshop` 500; clave caduca a los 7 días; gratis: `health`, `user/stats`, `library`, `search`, `status`, `contents`, `depot-keys`, `usage` | el "55/day" del código de ASSella no es el de este rol |
| 2026-09-29 | SteamOS + SLSsteam, ASSella `canary@6677a05`, Balatro por web, nativo | instalación AT0-M | wudrm devuelve el reto de Cloudflare (403/503); Steam "Unknown error"; ManifestDeX sí daba código desde la misma IP | |
| 2026-09-29 | log de un usuario, Animal Well 813230 por handoff | carpeta vacía | `AdditionalApps` con 813230 pero sin 813231 en `AdditionalDepots`: el bundle trajo la clave del app y no la del depot; Steam "instaló" en 3 s | la espera de licencia (15 s + 1,5) no vio el evento y siguió |
| 2026-10-05 | SteamOS + SLSsteam `main@9c829a7`, sin lumalinux; ASSella `canary@22f2759`; Brotato 1942280 por handoff | instalar | funciona: 4 manifests a `depotcache`, `install\|1942280\|0`, 272 MB, `.acf` de Steam; shaders con código de Valve | `config.yaml.native_steam_backup` queda para siempre |
| 2026-10-05 | ídem | desinstalar desde Steam y reinstalar | "Unknown error": `download.lua` pide a wudrm con el UA de `curl` → 403 `Cf-Mitigated: challenge`; con UA de navegador 200, misma IP | el filtro es por User-Agent |
| 2026-10-05 | ídem, Balatro 2379780 por el clásico con "ACF independiente" | instalar | juego jugable; `.acf` con `StateFlags 4`, `SizeOnDisk 0`, `InstalledDepots` vacío; shaders "Missing decryption key", 300 s | Steam no sabe qué hay instalado |
| 2026-10-05 | ídem, Into the Breach 590380 por handoff | instalar | sin shaders: la clave de `590380` estaba en el lua y ASSella la excluyó de `DecryptionKeys`; sin bucle de reintentos | arreglado en `canary` `dae9d9f` (06-oct) |
| 2026-10-05 | hilo de soporte (usuario en casa, Ravenfield 636480) | Workshop por nativo | `MRC server http://gmrc.wudrm.com/manifest/ failed` ×7 con el UA de `curl`; Steam intentó bajar los items por sí mismo | por el clásico funcionó; §4.4 A9 |
| 2026-09-24/27 | commits `2544d5e`, `52d9ca0`, `589fba4` de `beta` | nombres de fichero con caracteres nulos en manifests de Hubcap | DepotDownloader recompilado y saneado en la extracción; el motor propio recorta los nulos (`at0m.py:518-530`) | sin caso visto en los nuestros; `manifests.py` los pasaría tal cual |
| 2026-10-06 | `download.lua 1.4.0-spacetest` (distribuido a mano) | lectura | timeouts en ms pasados como segundos a `--connect-timeout`; autoridad wudrm sin UA; caché de la autoridad para siempre; premisa falsa ("un código es una decisión de enrutado") | si lo promocionan, el primero es el que estalla |

#### Función 9 — Añadir y quitar un juego

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-29 | SteamOS + SLSsteam, ASSella `canary@6677a05` | arrancar ASSella | despliega `download.lua` y `spliced-tickets.lua`, escribe `Plugins: yes` y `API: yes` | hoy el despliegue va desde Ajustes (§5.1) |
| 2026-09-29 | ídem | matar ASSella a mitad | config, plugins y `.acf` a medias se quedan | |
| 2026-10-05 | ídem `canary@22f2759` | desinstalar un juego AT0-M | desde ASSella desactivado; desde Steam se va el `.acf` y los manifests de `depotcache`, el config conserva `AdditionalApps`, `AdditionalDepots`, `DecryptionKeys`, `AppTokens` | lo que LumaDeck documenta en `pins.py:46` |
| 2026-10-05 | ídem | arrancar desde el fuente | Python 3.11 no arranca (f-string multilínea, `task_manager.py`); con 3.13 sí | |

### 5.3 Cronología

- **2026-05-20.** `9a11c5f`, "Initial ASSella release" (v1.0). Unas 55 tags
  hasta v2.5.4 en una línea que luego se reescribió.
- **2026-08-03/09.** `81b8d0b` 2.5.5-beta; `beta` sale de `main`. `0898f9f`
  (08-05): integración con el ACF de SLSsteam.
- **2026-09-14.** `4dd7375` builds históricos; `accb40c` `missing_hubcap_depots`
  y preflight `/contents`. `d3cbdbc` detección del watcher muerto.
- **2026-09-15/17.** PR #16 de KingCatto: Hubcap `/manifest` solo pública
  (`fd651e9`), betas montadas en cliente; v2.6.5 (09-17, última release común).
- **2026-09-19.** Separación `beta`/`canary` (`1fcd7a8`); `78dbda7`, primer
  backend nativo con pins y handoff; catálogo "Voices".
- **2026-09-24/26.** `7413be8` motor de manifests propio (Vapor → AT0-M, 09-25);
  `assella_bridge.lua` nace y muere; `curl-impersonate` dentro del lua;
  `2aae4ae` nativo por defecto.
- **2026-09-28/29.** Rollback por `ManifestIds` (`2b5d9a6`); `ac7fec9` siembra
  de `depotcache`; builds rotas del 29 (cuelgue de 70 s con el mutex, crash
  de l.272); `6677a05` vuelta al lua de Ace; `6be2400` fuerza `Plugins: yes`.
- **2026-09-30.** `ace06bd` plugins desde R2 con manifest de hashes.
- **2026-10-01/04.** `beta` 2.7.0 (updater por canal, `_atomic_write` en el
  sitio, CI de AppImage y Arch); `a0382f0` "All games online"; `1edf210`
  `SmartTickets`; `beta` 2.7.1 (instalador con `.sha256`, pausa/reanudar).
- **2026-10-06.** `dae9d9f` clave raíz en `DecryptionKeys`; `a4a38ad` carrera
  de proveedores y sincronía de plugins apagada; `b9c31c9` "custom plugins".
- **2026-10-07.** `9160eeb` web server a `127.0.0.1` con token; `8ddd248`
  `mrc_config`; `a090c63` DepotDownloaderMod 3.4.0 y `-probe-cdn`;
  `d13376f` rama `arm64` con fusión automática.
- **2026-10-08/09.** `42294dd` comprobación de plugins antes de escribir el
  config, `lua_parsing.py`, regex tolerantes a claves entre comillas;
  `e9ad493` ningún AppID en `AdditionalDepots`; `c9ceace` comprobador de
  updates AT0-M; `beta` `070526c`.
