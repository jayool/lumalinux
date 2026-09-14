# SteaMidra en Linux (fork drappula/SFF) vs la pila LumaDeck — análisis exhaustivo

*Completo. Qué es y de dónde viene (§1), inventario (§2), la capa Linux y lo
que toca del sistema, con los vectores de "brickeo" (§3), adquisición (§4), la
pregunta del 09-09 con medidas de hoy (§5), puerta a puerta (§6), confianza y
riesgo (§7), veredicto con pasada adversaria (§8) y hallazgos (§9). **Ningún
hallazgo es código.** Es la continuación de `lumacore-findings.md`, que congeló
`Midrags/SFF` en `fa44fc9`; el desarrollo de SteaMidra sigue en este fork.
**Leer §0 primero.***

---

## §0 Alcance, método, evidencia

| Lado | Componentes |
|---|---|
| **Nosotros** | lumalinux (`.so`) + SLSsteam stock vía Headcrab + LumaDeck (Decky) + CloudRedirect |
| **Ellos** | SteaMidra (PyQt6/QtWebEngine, AppImage) + DepotDownloaderMod o descargador nativo + SLSsteam stock vía Headcrab (editado) |

- **Referencia congelada:** `github.com/drappula/SFF`, `main` @ `92d6813`
  ("feat(linux): route slssteam setup through headcrab", 2026-09-13). Es
  `Midrags/SFF` `fa44fc9` (2026-08-21, nuestra referencia en
  `lumacore-findings.md`) + 67 commits. `Midrags/SFF` no se ha movido desde
  entonces: 0 commits nuevos. Autores del fork: mallusrgreat (54), Qiyrax (8,
  los de SteamOS), drappula (3 merges), St0Qx (1), wtfseanscool (1).
- **Método:** `sff/linux/` leído **entero** (13 ficheros, 3.125 líneas) porque
  ahí está lo que toca la Deck; el resto por diffs de los 67 commits y lectura
  dirigida de `sff/lua/{endpoints,provider}.py`, `sff/manifest/downloader.py`,
  `sff/downloads/native_downloader.py`, `sff/network/steam_client.py`. Para la
  comparación se bajaron hoy `headcrab.sh` y `headcrab_native.sh` de Headcrab.
- **Fuera de alcance por decisión:** Windows/LumaCore, la GUI, el kit de DRM
  (Steamless, gbe_fork, unlockers, Crack Files, online-fix), logros, rclone.
- **Sobre el "brickeo":** no hay logs ni reportes con datos, solo mensajes de
  Discord. El código da los vectores; el mecanismo lo tenemos medido en
  nuestra propia pila; la atribución caso a caso no es posible y no se hace.
- **Etiquetas:** [read] leído; [inferred] deducido; [measured] salida real de
  hoy (2026-09-14); [doc] documentación ajena; [not read] no leído.

---

## §1 Qué es

SteaMidra es el gestor de escritorio de Midrag para "instalar" juegos con
LumaCore (Windows) o SLSsteam (Linux): busca el juego, consigue lua + keys +
manifests de proveedores, descarga los depots y da de alta el juego para que
Steam lo muestre. En Linux **nunca deja que Steam descargue ni actualice**: baja
fuera de Steam, escribe el ACF a mano y lo protege por permisos. El fork añade
en tres semanas: cadena de proveedores gratis, descargador nativo, selección de
depots y ficheros, builds antiguos vía SteamDB, parche automático del modo juego
de la Deck, Headcrab como instalador de SLSsteam, y un sistema opt-in de subida
de keys a un worker propio.

---

## §2 Inventario [read]

`sff/linux/`: `slssteam.py` (699: Headcrab, parches, `steam-jupiter`, hash fix,
migración), `yaml_config.py` (907: `config.yaml` de SLSsteam por regex, escritura
atómica), `linux_download.py` (429: flujo de descarga e instalación),
`steam_process.py` (274: kill/start con `LD_AUDIT`), `desktop_shortcuts.py`
(196), `flat_file_repair.py` (134: repara un bug propio de 6.6.5), `acf_writer.py`
(129), `steamless.py` (127), `slscheevo.py` (96), `permissions.py` (61),
`depot_downloader.py` (35), `dotnet.py` (21).

Cambios del fork fuera de `sff/linux/`: 78 ficheros, 6.806 inserciones; las
2,3 M de líneas borradas son `sff/lua/fallback_depotkeys.json`, que ya no va
embebido (se descarga de KoriaPolis cada 6 h). CI: `.github/workflows/release.yml`
construye el AppImage en `ubuntu-24.04` y publica release. `third_party/`: DDMod,
gbe_fork (+linux, +tools), Steamless, SteamAutoCrack, coldloader, rclone, fzf
como blobs sin fuente.

---

## §3 La capa Linux: qué hace en la Deck y qué toca del sistema

### 3.1 "Linux Setup" desde `92d6813` [read]

`misc_bridge.py:1747` → `slssteam.py::setup_via_headcrab`:

1. Si es SteamOS y `~/.bashrc` tiene `[[ $- != *i* ]] && return` (la guarda
   estándar de bash en Arch y SteamOS), **se niega** y manda al usuario a editar
   el `.bashrc` (`45971f4`, 09-09). No se ha encontrado qué problema real
   arregla; apunta a su propio lanzador de Konsole.
2. Mata Steam con **SIGKILL** (`steam_process.kill_steam`, `proc.kill()`),
   leyendo antes `/proc/<pid>/maps` para saber qué `.so` reinyectar.
3. Baja `headcrab.sh` del raw de GitHub, **le recorta por texto todas las
   funciones y líneas de CloudRedirect** (`_cr_filter`) y ejecuta el resultado.
4. Tras Headcrab, tres parches propios:
   - `patch_slssteam_config`: fuerza `SafeMode: no`, `DisableUpdates: yes`,
     `DisableCloud: yes`, `WarnHashMissmatch: no`. Solo si no existe
     `.headcrabd`; Headcrab lo escribe siempre, así que **tras Headcrab no
     corre** y queda lo de Headcrab: en SteamOS `SafeMode: yes`
     (`headcrab.sh:840-856` [read hoy]). En las versiones anteriores al 13-09,
     que instalaban SLSsteam por su cuenta, sí corría.
   - `patch_steam_sh`: `chmod 644` a `~/.local/share/Steam/steam.sh`, **borra
     toda línea que contenga `LD_AUDIT`** (la `INJECT_SLS=...` de Headcrab),
     inserta `export LD_AUDIT=<dir>/library-inject.so:<dir>/SLSsteam.so` en la
     línea 10 y **no devuelve el bit de ejecución**. Headcrab lo había dejado
     en 555. Funciona porque el `export $INJECT_SLS` de Headcrab exporta vacío y
     el `export` global hace el trabajo. Está también en el Midrags original
     (`fa44fc9:sff/linux/slssteam.py:348`).
   - `create_steam_cfg`: `BootStrapperInhibitAll=enable`, sin pisar si existe.
     Igual que Headcrab.

### 3.2 "Patch Gaming Mode" [read]

Botón separado (`patch_steam_jupiter`): `sudo cp /usr/bin/steam-jupiter
/usr/bin/steam-jupiter.bak`, reescribe el script metiendo el `export LD_AUDIT`
antes del último `exec`, `sudo cp` de vuelta, `chmod 755`. Si el sudo falla, el
mensaje dice `sudo steamos-readonly disable`. Desde `410a3c7` (07-09) también
pone `SafeMode: yes`. Su doc: "SteamOS updates reset the file; re-run after
every update". El nombre del backup cambió el 07-09 de `.steamidra.bak` a `.bak`.

El Midrags original no lo automatizaba: en su webui (`fa44fc9:sff/webui/index.html:1508-1554`)
mandaba al usuario hacer `steamos-readonly disable`, backup, `sudo kate
/usr/bin/steam-jupiter`, y ya tenía la entrada **"Gaming Mode black screen / boot
loop — boot to Desktop Mode, restore steam-jupiter from backup"**. El fork lo
convirtió en botón el 07-09.

**Headcrab no toca `/usr`** (`headcrab.sh` [read hoy]) y nuestra Deck funciona
en modo juego solo con `steam.sh` de usuario y el drop-in de systemd
(`RESEARCH.md:212-220`, `decouple-headcrab-plan.md` WS1.1). El parche de
`steam-jupiter` es innecesario para inyectar en modo juego.

### 3.3 Vectores de "brickeo", del más al menos probable

**Los reportes de Discord se refieren al SteaMidra original (Midrags, hasta
v6.6.6 del 21-08), no a este fork.** [user report] El usuario probó el original
en su Deck: sin crash, pero inusable (UI incomprensible).

**La fuente [external, Discord]:** hilo "Remove SteaMidra" del servidor de
FMHY (canal `1546879201565610075`), abierto el **2026-09-08** por el
desarrollador de SLSsteam y pegado entero por el usuario el 09-14. Lo que dice:

- Apertura (08-09): "no hace la mayoría de lo que anuncia, o no correctamente";
  el desarrollador "le dice a un LLM que haga las cosas, incluso las
  decisiones"; el lado Linux "es muy improbable que lo prueben", "cada supuesto
  fix rompe otras cosas", "malfunciona en Linux, potencialmente dejando las
  instalaciones de los usuarios rotas sin forma de deshacerlo". Ocho capturas
  adjuntas de "los problemas más terroríficos", **no incluidas en el volcado**.
- Otros usuarios (08/09-09): fallos al instalar juegos en CachyOS con Steam
  limpio; "siempre me ha destrozado las instalaciones de Steam en Linux"; en
  Windows tampoco funcionaba ese día: hacía falta bajar de versión Steam o
  copiar ficheros que reparte un moderador en su Discord ("el fix de migo3",
  los patrones de MigoReleases que el fork adoptó en `ffd70db`).
- El administrador de FMHY: se probó en dos hilos enteros y funcionaba
  entonces; "que algo se rompa con el tiempo no es que añadamos cosas rotas";
  lo retira en la siguiente actualización de la wiki.
- El caso de la Deck (10-09): "no era mi amigo, alguien de nuestros canales de
  soporte, pero sí, fue muy real", y "por cada reporte hay más gente con el
  mismo problema que no lo dice". Sin versión, sin log, sin síntoma. Se remite
  a un "previous topic" que no está en el volcado.
- Midrag desaparecido "algo más de dos semanas" el 08-09, "tres" el 10-09;
  único programador. FMHY recomienda ACCELA en Linux y LuaTools en Windows.
- **OpenValve**: alternativa open source anunciada por un miembro de FMHY con
  rol [CODE] ("it's mine", "release by the end of this month", "NoUI early
  access, 6 testers", "un fichero de config que abres en un editor", "OST pero
  con esteroides y crack"). No es del autor de OpenSteamTool; ese mismo usuario
  llama a OST "vibecoded slop". Sin repo conocido.
- Un usuario pregunta por una alternativa Linux "que no sea ACCELA" porque
  "me gusta bajar el juego desde el propio cliente de Steam"; le responden
  LuaTools ("lo añades y lo bajas por Steam"). Es exactamente nuestro modelo.
- El fork `drappula/SFF` no aparece en ningún momento: para la comunidad
  SteaMidra está muerto. La disputa de créditos (LumaCore como derivado de
  OpenSteamTool, "tried to hide the evidence") ya está en `lumacore-findings.md`.

Lo que **no** hay en todo el hilo: una versión, un log, un síntoma técnico, un
mecanismo. Las capturas del 08-09 serían lo más cercano y no se han visto.

**Los síntomas [external, capturas del hilo, transcritas por el usuario el
09-14].** Las ocho capturas son mensajes de usuarios pidiendo ayuda en el
servidor de SLSsteam, todos con el original (anteriores al 08-09). Sin versión
ni log, pero con síntoma. Cruce con el código, cada fila [inferred]:

| Síntoma reportado | Qué del código lo produce | Confianza |
|---|---|---|
| Deck: "tenía ACCELA funcionando, abrí SteaMidra por error y me rompió Steam; desinstalé y reinstalé con Headcrab y ahora dice `slssteam loaded successfully` pero Steam no abre nunca" | Headcrab repone `steam.sh` y `config.yaml`, pero no toca lo que SteaMidra dejó fuera: `steam.cfg` bloqueando el cliente, un `steam-jupiter` editado a mano si siguió la guía, y la raíz desbloqueada. SLSsteam se inyecta (el toast) y Steam muere o se queda después. Compatible con los vectores 1 y 2; sin log no se distingue. | media |
| Deck: "formateé la Deck ayer porque nada funcionaba tras probar SteaMidra; ahora CachyOS" | El resultado final que describe §3.3 vector 1 (OOBE) o el usuario rindiéndose. No distinguible. | baja |
| Bazzite: "rompió el sistema entero: Steam ya no recibe updates, rompió los plugins de Decky, y solo puedo jugar sin internet" | "Sin updates" = `steam.cfg` `BootStrapperInhibitAll=enable` (`create_steam_cfg`), que SteaMidra crea y su desinstalación no quita. "Decky roto" y "solo offline" encajan con Steam lanzado desde el propio SteaMidra (`start_steam`: `env LD_AUDIT=... steam` con el entorno del AppImage limpiado), fuera de la sesión que Decky y gamescope esperan, o con la inyección perdida: online Steam pide comprar, offline arranca lo que tiene en caché. | media |
| ROG Ally X, Bazzite: "funcionaba en el escritorio, en la Ally Steam no arranca en absoluto; ¿cómo lo quito del todo?" | `patch_steam_sh`: `steam.sh` en 644 y reescrito. Si el launcher hace `exec` directo, "Permission denied" y Steam no arranca (vector 3). No hay desinstalador de esos parches. | media |
| "Me ha jodido ACCELA y SLS enteros; aunque desinstale, todos mis juegos dicen comprar" | Dos caminos. (a) `_add_to_slssteam` reescribe `config.yaml` entero con `yaml.dump` y `patch_slssteam_config` lo regexea: SLSsteam carga un config que no reconoce (o sin sus `AdditionalApps`) y no desbloquea nada. (b) Inyección perdida: Headcrab deja `steam.sh` en **555**, y Steam re-extrae `steam.sh` cuando su tamaño no coincide con el manifest (`slsteam-moon-findings.md` M7); el 555 de Headcrab es lo que impide esa sobreescritura, y el `chmod 644` de SteaMidra la vuelve a permitir. En el siguiente update del cliente, `steam.sh` vuelve a ser el de Valve, sin SLSsteam, y todo dice "comprar". | media-alta |
| Linux: "cuando le doy a jugar, Steam borra la carpeta entera del juego" | ACF escrito a mano (`acf_writer.py`), sin keys en Steam y sin manifests que Steam pueda usar para reconciliar. Al lanzar, Steam valida, no puede reconstruir el estado del depot y limpia el contenido como si fuera una instalación inválida. El disparador exacto no está en el código de SteaMidra sino en cómo Steam trata un ACF que no escribió él; el `InstalledDepots` vacío de `46a421d` (09-09, "fixing update issue with newly downloaded games") es su propio intento de arreglar algo en esa zona. | baja-media |

Lo que estas capturas confirman sin duda: **los daños son en Deck, Bazzite y
CachyOS, en Steam y en SLSsteam/ACCELA, y sobreviven a desinstalar SteaMidra**,
porque los parches a `steam.sh`, `steam.cfg`, `config.yaml` y `/usr` no tienen
desinstalador. Lo que no confirman: qué parche concreto rompió cada máquina.

Atribución por versión, con el original `fa44fc9` delante:

- **Original:** el flujo de terminal (`linux_download.py:341`) instalaba
  SLSsteam **sin Headcrab** (`install_from_github`: 7z, apt/pacman, limpieza del
  paquete pacman), y la GUI probaba Headcrab con **fallback silencioso** al
  instalador propio. Sin `.headcrabd`, `patch_slssteam_config` corría y forzaba
  **`SafeMode: no`**. El parche de `steam-jupiter` no estaba automatizado: la
  webui mandaba al usuario hacerlo con `kate` como root tras `steamos-readonly
  disable`. Vectores 1 y 2 de la lista, en su forma manual.
- **Fork:** botón automático de `steam-jupiter` desde el 07-09 (`3cc8c41`,
  `410a3c7` añade `SafeMode: yes` al pulsarlo) y Headcrab sin fallback desde el
  13-09 (`92d6813`), que en SteamOS deja `SafeMode: yes`. En el vector 1 el fork
  es más seguro que el original; en el 2 es el mismo vector con botón.


1. **Steam en bucle de crash en modo juego → gamescope `short_session_recover`
   → borrado de `~/.local/share/Steam` → OOBE.** [measured en nuestra pila,
   `RESEARCH.md` §17.3, v0.16.2]. La Deck aparece "de fábrica", sin juegos: eso
   es lo que la gente llama brickeada. El original lo podía disparar por
   `SafeMode: no` forzado (instalación propia sin Headcrab) con un cliente
   actualizado y SLSsteam viejo; el fork, desde el 13-09, ya no fuerza
   `SafeMode: no` tras Headcrab. Cualquier parche mal aplicado en modo juego
   también lo dispara. [inferred en la atribución]
2. **`/usr/bin/steam-jupiter` parcheado con sudo y raíz en escritura.** Un
   `exec` movido, un backup con otro nombre (cambió el 07-09), o una
   actualización de SteamOS a medias con la raíz desbloqueada. Pantalla negra
   en modo juego. [read el vector; inferred el resultado]
3. **`steam.sh` en modo 644.** Steam re-extrae `steam.sh` del bootstrap cuando
   su tamaño no coincide con el manifest (`slsteam-moon-findings.md` M7).
   Headcrab lo deja en **555** y eso es, con toda probabilidad, lo que impide la
   sobreescritura; el `chmod 644` de SteaMidra la vuelve a permitir. Dos
   resultados posibles: Steam repone el original limpio y la inyección
   desaparece ("todos mis juegos dicen comprar"), o, si el launcher hace `exec`
   directo antes de ese chequeo, "Permission denied" y Steam no arranca.
   [inferred; `bin_steam.sh` de Valve no disponible desde aquí]
4. **`headcrab.pages.dev/reset` + `steam.cfg`** en el "hash fix": baja el
   bootstrap del cliente y bloquea sus updates. En SteamOS nuevo con cliente
   pineado viejo, gamescope y cliente pueden desencajar. [inferred]
5. **SIGKILL a Steam** en cada setup y cada hash fix, con `config.vdf` y
   `localconfig.vdf` a medio escribir. Corrupción, no brick. [read]

### 3.4 El resto de `sff/linux/` [read]

- `acf_writer.py`: ACF a mano, **`InstalledDepots` siempre vacío** (`46a421d`,
  09-09, "fixing update issue") y **modo 444**. Es su forma de que Steam no
  toque el juego. Steam no puede escribir estado tras un lanzamiento.
- `linux_download.py`: lua del proveedor → depots con key → `run_download`
  (DDMod o nativo) → ACF → manifests a `<librería>/steamapps/depotcache` →
  `AdditionalApps`, `AppTokens` y `DlcData` (>64 DLC) con `yaml.dump` del
  fichero entero (pierde comentarios y orden). **Nunca escribe keys en Steam**.
  `add_manifest_id` existe pero `1cfb20a` (08-09) "drop version pinning": no
  se usa.
- `yaml_config.py`: escrituras atómicas (`os.replace`), que el inotify viejo de
  SLSsteam no veía (`slssteam-analysis.md` §5, ya corregido en SLSsteam).
- `steam_process.py`: arranque `env LD_AUDIT=... steam`; accesos directos
  `.desktop` con el mismo `env`.
- `flat_file_repair.py`: repara los ficheros con barras invertidas en el nombre
  que dejó el descargador nativo de 6.6.5. Corre una vez al día.

---

## §4 Adquisición

### 4.1 Luas y keys, en orden [read `endpoints.py::get_freelua`, `provider.py`]

"Free Providers" (6.6.7d, 10-09): construye el lua él mismo con los gids
actuales de `api.steamcmd.net` y las keys de `fylsdy/ManifestHub/main/depotkeys.json`
("trionine"), rellenando con `KoriaPolis/Steam-Depot/fallback_depotkeys.json`
(o su mirror r2.dev); si no, `api.luagen.revobd.club/<app>.zip`; si no, los luas
de `steamtoolsapp/ManifestHub/<app>/<app>.lua` y `steamtools-games/ManifestHub3`.
Con key: Hubcap, Ryuu, DepotBox, ManifestHub2 (`manifesthub2.filegear-sg.me`,
keys de 24 h).

### 4.2 Manifests [read `manifest/downloader.py`, `native_downloader.py`]

depotcache local → **request code anónimo** con el cliente `steam` de Python
(`native_downloader.py:300-527`; muerto desde el 09-09 salvo 731/571/441; su
changelog lo reconoce: "Steam stopped serving manifests to anonymous clients")
→ Hubcap `/generate/manifest` (mensaje "1500/day") → repos planos
`qwe213312|mejikuhibiniu1|Sainan/k25FCdfEOoEJ42S6/main/<depot>_<gid>.manifest`
→ ManifestHub2 con key. Los mirrors GMRC (steam.run, wudrm, opensteamtool) se
retiraron el 12-09 por muertos (`9033611`).

### 4.3 Descarga, registro, updates [read]

DDMod o descargador nativo (chunks por CDN con login anónimo, que sigue vivo).
ACF a mano y 444. `DisableUpdates: yes`. `update_check.py` compara lo guardado
con PICS y solo avisa; actualizar es volver a bajar. El "manifest-pin helper"
del changelog (`00_letupdate_override.lua`) es de LumaCore/Windows.

### 4.4 El "contributor" [read `provider.py:427-740`]

Escanea los luas de `config/stplug-in`, extrae `addappid(depot,0,"key")`, los
enriquece con nombres (PICS, opcional) y los sube por POST a
`stea-provider-api.steamidra.workers.dev/submit` cada 3 h. **Opt-in, default
`False`** (`structs.py:399`). Excluye, por una cadena ofuscada en cinco trozos
que decodificada es "Downloaded using DepotBox" (`provider.py:413-426`), los
luas de DepotBox. Crowdsourcing de keys hacia su propia base.

---

## §5 La pregunta del 09-09 [measured 2026-09-14]

| Fuente del fork | Estado hoy |
|---|---|
| `fylsdy/ManifestHub/main/depotkeys.json` (keys, primer eslabón gratis) | **404**. |
| `steamtoolsapp/ManifestHub/<app>/` y `steamtools-games/ManifestHub3/<app>/` | Las 7 apps probadas existen (lua 200), pero **0 de 6 gids actuales** (2545360 LMSR, 252490 Rust, 1794680 Hades, 275850 Valheim, 739630 Vampire Survivors, 892970 Hades II). Solo Balatro (2379780), sin update desde hace meses. El gid de LMSR que tienen (`5818028561835039765`) es anterior al build instalado del 08-09. Snapshot congelado. |
| `qwe213312/k25FCdfEOoEJ42S6/main/` (plano) | Igual: Balatro sí, nada reciente. |
| `api.luagen.revobd.club` | Bloqueado por el proxy del sandbox; no medido. |
| Request code anónimo | Muerto desde el 09-09. |
| Hubcap `/generate/manifest` | Único generador vivo, con key (`assella-analysis.md` §5.3). |

Los mismos 6 gids estaban en P-ToyStore (nuestros clones `pts_<appid>`), que el
fork no usa. Tampoco usa luastools. **La cadena gratis del fork no sirve hoy
para ningún juego actualizado desde agosto**; para esos acaba en Hubcap o
ManifestHub2 con key, como ASSella. No hay ninguna fuente nueva que adoptar.

---

## §6 Puerta a puerta

| Puerta | SteaMidra (fork, Linux) | Nosotros | Diferencia |
|---|---|---|---|
| G1 Propiedad | SLSsteam stock vía Headcrab editado al vuelo + 3 parches encima. | SLSsteam stock vía Headcrab, sin tocar. | Misma capa; ellos la desmontan y remontan. |
| G1b Inyección modo juego | `steam.sh` de usuario + botón sudo sobre `/usr/bin/steam-jupiter`. | `steam.sh` de usuario + drop-in systemd. Nunca `/usr`. | Vector 2 de §3.3. |
| G2 Keys | Solo para DDMod/nativo. Nunca a Steam. | `config.vdf`; Steam las reusa. | Steam no descarga en su modelo. |
| G3 Manifests | request code (muerto) → Hubcap → 3 repos planos (snapshot) → ManifestHub2. | depotcache → archivo → P-ToyStore → luastools → Hubcap zip. | 0/6 contra 6/6 hoy. |
| G4 Request code | Lo piden primero y falla. | No se pide. | Un intento muerto por depot. |
| G5 Chunks | Nativo Python o DDMod (.NET 9 en `~/.dotnet`). | Steam. | Como ASSella. |
| G6 Registro | ACF a mano, `InstalledDepots` vacío, 444. | Steam escribe el ACF. | Bloqueo por permisos. |
| G7/G8 Updates | Aviso; actualizar = rebajar. `DisableUpdates: yes`. | 30 min + update nativo. | Como ASSella. |
| G9 Build antiguo | SteamDB con cookie del usuario + DDMod. Sin pin. | Freeze, sin rollback. | Ellos, a costa de SteamDB. |
| G10 DLC | `DlcData` con `yaml.dump` completo. | Por secciones. | Su escritura destroza el formato. |
| G12 DRM / G13 Logros | Kit completo / SLScheevo. | Nada / nativo. | Fuera de alcance. |
| G14 Cloud | rclone manual a un remote del usuario; CloudRedirect eliminado de Headcrab. | CloudRedirect. | Nosotros. |
| G15 Plataforma | AppImage de CI, `~/.local`, sudo solo en el botón. | Decky + `.so`. | Su CI es mejor que el de ASSella. |

---

## §7 Confianza y riesgo [read]

- **Provenance.** AppImage desde `release.yml` en GitHub Actions; `third_party/`
  siguen siendo blobs.
- **Privilegios.** Solo `patch_steam_jupiter` (sudo, `steamos-readonly disable`).
- **Hacia fuera.** Contributor opt-in (§4.4). `analytics.py` es local. Keys de
  proveedores validadas al arrancar. Scraping de SteamDB con la cookie de sesión
  del usuario para builds antiguos.
- **`curl | bash` reescrito.** Headcrab filtrado por texto antes de ejecutar;
  un cambio de Headcrab puede romper el filtro en silencio. El hash fix ejecuta
  `headcrab.pages.dev/reset` y `headcrab.pages.dev` sin filtrar.
- **Autoupdate de la app.** [not read] No localizado en `sff/`; los updaters
  encontrados son de SLSsteam, LumaCore, gbe_fork y gse (GitHub releases, sin
  firma).
- **Estado que deja.** `steam.sh` 644 reescrito; ACFs 444; `steam.cfg`
  bloqueando el cliente; `.bashrc` señalado como "roto"; opcionalmente `/usr`
  modificado y raíz en escritura.

---

## §8 Veredicto y pasada adversaria

### 8.1 Veredicto

drappula/SFF es SteaMidra vivo. En Linux es ASSella con peores fuentes:
descarga fuera de Steam, ACF a mano bloqueado por permisos, `DisableUpdates:
yes`, sin pin. Su cadena gratis del 10-09 está hoy muerta (keys 404) o congelada
(mirrors sin builds recientes), así que acaba en Hubcap o ManifestHub2 con key.
El 13-09 delegaron en Headcrab y lo primero que hacen después es deshacerle el
`steam.sh`. Sobre el brickeo: los reportes son del original, no del fork. Hay un
mecanismo conocido y medido por nosotros (§3.3 vector 1) y el original tenía dos
formas claras de dispararlo (`SafeMode: no` forzado y la edición manual de
`steam-jupiter` con la raíz desbloqueada); el fork quita la primera y automatiza
la segunda. Compatible con los mensajes de Discord sin poder atribuir ningún caso.

### 8.2 Adversaria

- *"Sin reportes es especulación."* Vectores [read]; mecanismo [measured] en
  la nuestra; atribución [inferred] y así etiquetada.
- *"Headcrab también toca `steam.sh` y bloquea el cliente."* Cierto, y no cuenta
  como diferencia. La diferencia es `/usr`, el 644, el SIGKILL y `SafeMode: no`.
- *"Sus mirrors son lo mismo que P-ToyStore."* Mismo formato, otra alimentación:
  snapshot sin bot contra bot con lag de minutos a horas. 0/6 contra 6/6.
- *"El ACF 444 sirve para nuestro freeze."* No: nuestro freeze deja a Steam
  escribir su estado; un ACF de solo lectura lo rompe tras cada lanzamiento.
- *"`download_bridge.py` no leído."* Se comprobó que el camino Linux acaba en
  `run_download` y `acf_writer`. Residual bajo.
- *"Windows no importa."* Correcto para nosotros; por eso no se analiza.

---

## §9 Hallazgos

Cuatro. **Ninguno es código.**

- **F1 — documentación, `decouple-headcrab-plan.md`.** Los vectores de §3.3
  como lista de "lo que no hacemos y por qué": nunca `/usr`, nunca
  `steamos-readonly disable`, nunca SIGKILL a Steam, SafeMode como lo deje
  Headcrab, y el OOBE de `RESEARCH.md` §17.3 como el coste real de un crash en
  bucle en modo juego. Una nota cruzada, no un cambio de plan.
- **F2 — evidencia, `RESEARCH.md` §19.4.** La cadena "Free Providers" del fork
  no aporta ninguna fuente viva (§5). P-ToyStore y luastools siguen siendo las
  únicas gratis que funcionan hoy. Una línea con la medida.
- **F3 — documentación, `lumacore-findings.md`.** Una línea remitiendo aquí:
  `Midrags/SFF` está parado desde `fa44fc9`; el barrido siguiente arranca en
  `drappula/SFF` `92d6813`.
- **F4 — exclusiones explícitas.** Contributor de keys, scraping de SteamDB,
  rclone, kit de DRM, logros: vistos, fuera, por diseño.

Pendiente de barrido: `drappula/SFF` desde `92d6813`. **A vigilar:** "OpenValve",
la alternativa open source anunciada en el hilo de FMHY (§3.3) para finales de
septiembre, sin UI, por fichero de config; sin repo conocido a 2026-09-14.
