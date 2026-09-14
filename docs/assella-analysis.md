# ASSella (fork de ACCELA) vs la pila LumaDeck — análisis exhaustivo

*Completo. Qué es (§1), inventario y procedencia (§2), modelo de adquisición
(§3), comparación puerta a puerta (§4), la pregunta del 09-09 (§5), confianza y
riesgo (§6), veredicto con pasada adversaria (§7) y lista de hallazgos (§8).
**Ningún hallazgo es un cambio de código hoy**: uno queda escrito como plan B
con disparador explícito, el resto son notas de documentación o exclusiones
razonadas. **Leer §0 primero**: nada se ejecutó, la UI está fuera de alcance por
decisión, y las etiquetas de evidencia son de carga.*

---

## §0 Alcance, método, reglas de evidencia

### Qué se compara

| Lado | Componentes |
|---|---|
| **Nosotros** | lumalinux (`.so`) + SLSsteam (stock, sin fork) + LumaDeck (plugin Decky) + CloudRedirect |
| **Ellos** | ASSella (app PyQt6 de escritorio, AppImage) + DepotDownloaderMod (.NET 9) + SLSsteam stock vía Headcrab |

ASSella es un **descargador**: sustituye a Steam como cliente de descarga y de
actualización, y usa SLSsteam solo para que Steam crea que el juego es suyo.
Nuestra pila deja que Steam descargue y actualice de forma nativa. Los dos montan
sobre el mismo SLSsteam stock, así que la capa de ownership es común y el resto
es distinto por diseño.

### Fuera de alcance, por decisión

- **UI.** `src/ui/` (28,8k líneas de 61k) no se leyó salvo para seguir llamadas a
  los módulos de adquisición. Decisión del usuario: "LA UI NO ME INTERESA".
- **Ejecución.** Análisis estático. No se instaló ni se ejecutó nada. Toda
  afirmación de comportamiento se etiqueta como tal.
- **Coexistencia** con LumaDeck en la misma Deck. No investigada.

### Referencia congelada

- `github.com/niwia/ASSella`, rama `beta`, commit `accb40c`
  ("feat(hubcap): smart missing depot tracking with quota-free /contents
  pre-flight check"), 2026-09-14. Versión `2.6.5dev` en `src/res/version`.
- Los commits del 09-14 (`4dd7375`, `accb40c`) son los últimos leídos. El
  siguiente barrido arranca en `accb40c`.
- Hubcap: la documentación pública de la API (pegada por el usuario el 09-14) y
  dos llamadas gratuitas hechas con la key del usuario desde su codespace
  (`/user/stats`, `/generate/usage`). La key no se reproduce aquí.

### Etiquetas

- **[read]** leído en el código de ASSella o en el nuestro.
- **[inferred]** deducido, no leído ni ejecutado.
- **[measured]** salida real de una llamada o comando del usuario.
- **[doc]** documentación pública de Hubcap.

---

## §1 Qué es

ACCELA es un gestor de escritorio de código cerrado para "instalar" juegos con
SLSsteam: busca el juego en Hubcap, baja el zip con keys y manifests, descarga los
depots con DepotDownloader y da de alta el juego en SLSsteam para que Steam lo
muestre y lo lance. ASSella es el fork que niwia mantiene en público desde
2026-05-12 (commit inicial `9a11c5f`, "20260512+ASSella-1.0"), con un segundo
autor que solo aparece como "You-know-who" en README y `broadcast.json`.

Requisitos: Headcrab (instala SLSsteam stock con sudo desde `headcrab.pages.dev`),
.NET 9 para DepotDownloaderMod, y una API key de Hubcap. Se instala en
`~/.local/share/ACCELA/`, guarda `ACCELA.AppImage.bak` y se autoactualiza desde
las releases de GitHub.

Tiene además un kit de "hacer que arranque": Steamless CLI para quitar SteamStub,
Goldberg gbe_fork para sustituir `steam_api` cuando SLSsteam no basta, un proxy de
EOSSDK, el `fix.so` de netsock como `LD_PRELOAD` por juego, importación de tickets
de SLSsteam, generación de stats de logros con `schema-grabber`, un descargador de
workshop aparte y un scraper de SteamDB con bypass de Turnstile. Nada de eso está
en nuestro alcance (§4, §8-F4).

---

## §2 Inventario y procedencia

64 ficheros Python, 61k líneas, 28,8k de ellas en `src/ui/`. Hay un
`.agents/AGENTS.md` con reglas para agentes de IA que desarrollen el proyecto
(escrituras in-place a `config.yaml` para conservar el inode que vigila el inotify
de SLSsteam; independencia del ACF vía `.DepotDownloader/metadata.json`).

### Módulos leídos enteros [read]

| Fichero | Papel |
|---|---|
| `core/morrenus_api.py` | Cliente de Hubcap (nombre viejo: Morrenus). Todos los endpoints, cabecera `Authorization: Bearer`. |
| `core/steam_api.py` | PICS anónimo (cliente `steam` de solsticegamestudios) + `api.steamcmd.net`, híbrido para gids actuales. |
| `core/tasks/download_depots_task.py` | Prepara keys y manifests en `/tmp` y lanza DepotDownloaderMod. |
| `core/tasks/smart_update_task.py` | Update sin zip: keys cacheadas + `/generate/manifest` por depot. |
| `core/tasks/manifest_check_task.py` | Detección de updates: `.depot` guardado vs gids actuales. |
| `core/tasks/depot_key_migration_task.py` | Migra keys de luas a SQLite. |
| `managers/depot_key_manager.py` | SQLite de keys de depot (`depot_keys`, y desde 09-14 `missing_hubcap_depots`). |
| `managers/helper_manager.py` | Modo headless: descarga y chequeo de updates desde CLI. |
| `utils/slssteam_integration.py` | `config.yaml` in-place, pipe `/tmp/SLSsteam.API`, `install|appid|0`, `uninstall|appid`. |
| `utils/manifest_resolver.py` | Resuelve keys por app (SQLite → lua cacheado → zip) y appid desde depot. |
| `utils/steam_manifest.py` | Escribe el ACF a mano en modo clásico. |
| `utils/isp_bypass.py` | Directo → DoH → Tor → worker Cloudflare "Wirecutter". |
| `utils/yaml_config_manager.py` (1.743 L) | Todas las secciones de `config.yaml`: AdditionalApps, DlcData, AppTokens, FakeAppIds, Denuvo, LaunchOptions. Solo se leyó la lista de funciones y los puntos citados. |

### Vendored sin fuente en el repo [read]

`src/deps/`: `DepotDownloader.dll` + `SteamKit2.dll` (DepotDownloaderMod 3.4.0
sobre SteamKit2 3.2.0; entró en `9a11c5f`, actualizado en `1bc6082`), Steamless
CLI y plugins, `Goldberg/` (gbe_fork release-2026_08_23), `SLScheevo`,
`schema-grabber` (ELF), `EOSSDK-Win64-Shipping.dll`, `QRCoder.dll`. No hay
`.github/workflows`: el AppImage se construye fuera del repo.

### Nota técnica

`ast.parse` de Python 3.11 falla en `managers/task_manager.py:2761`. Es un
f-string multilínea (PEP 701), válido solo en 3.12+. No es un bug: el AppImage
lleva su propio intérprete.

---

## §3 Modelo de adquisición

### 3.1 Una sola fuente: Hubcap [read]

`core/morrenus_api.py` habla solo con `hubcapmanifest.com/api/v1`. No hay
P-ToyStore, ni luastools, ni GitHub, ni base de keys externa. Sin key de Hubcap la
app no adquiere nada. Endpoints usados: `/manifest/{app}` (zip completo),
`/generate/manifest?depot_id&manifest_id` (un manifest suelto), `/search`,
`/status/{app}` y `/manifest/{app}/contents` (los dos últimos desde 09-14).

### 3.2 Keys de depot [read]

Salen del lua que viene en el zip. `DepotKeyManager` las persiste en SQLite y
`manifest_resolver.ensure_depot_keys_for_app` resuelve SQLite → `cached_luas/`
→ zip (gasta cuota). Las keys se cachean para siempre; el smart update solo
funciona en juegos que ya pasaron por el zip una vez.

### 3.3 Descarga: DepotDownloaderMod con `-manifestfile` [read]

`_prepare_downloads` escribe `/tmp/mistwalker_keys.vdf` (`depot;key`) y deja los
manifests en `/tmp/mistwalker_manifests/<depot>_<gid>.manifest`, recuperados del
zip, del `depotcache` de la librería si ya existían, o de
`generate_single_manifest`. Lanza `dotnet DepotDownloader.dll -depot -manifest
-manifestfile ...`. **DepotDownloaderMod nunca pide request code**: tiene el
manifest en fichero y la key, y los chunks son públicos. Steam no participa en la
descarga. Es la razón de que ASSella sobreviva al 09-09 (§5).

### 3.4 Integración con Steam, dos modos [read]

- **Clásico** (por defecto): ASSella escribe el ACF a mano
  (`build_acf_content`: `StateFlags 4`, `buildid` = `TargetBuildID`,
  `AutoUpdateBehavior 0`), copia los manifests a `<juego>/.DepotDownloader/` con
  sidecar `.sha`, y mete el appid en `config.yaml` con escritura in-place.
- **Experimental "ACF-independent"** (`experimental_acf_independent`): copia los
  manifests al `depotcache` central de Steam, escribe el appid, espera a que
  SLSsteam lo loguee, manda `install|appid|0` por `/tmp/SLSsteam.API` y deja que
  Steam cree el ACF. `uninstall|appid` para quitar.

### 3.5 No pinea en SLSsteam [read]

`ManifestIds` solo aparece en `assfixer.py` como clave conocida del YAML. ASSella
nunca la escribe. Su "pin" (`pin_build/<appid>` en QSettings) es un flag local
para que el chequeo ignore el juego. Como SLSsteam bloquea el update nativo de los
AdditionalApps, el juego queda en "Jugar" con la versión que ASSella dejó, y Steam
nunca descarga ni actualiza. El actualizador es ASSella.

### 3.6 Detección de updates [read]

`manifest_check_task.py` compara los gids guardados en `depots/<appid>.depot` con
los actuales (steamcmd.net + PICS). Resultado `update_available` /
`up_to_date` / `cannot_determine`; un gid del mirror más viejo que el instalado se
ignora como "mirror stale". Disparadores: al arrancar la app
(`check_updates_on_boot`, on por defecto), manual, y
`helper_manager.run_headless_update_check` desde fuera. **No hay timer propio, ni
systemd, ni cron.** Si la app no está abierta, no se entera.

### 3.7 Aplicar el update: smart update [read]

`smart_update_task.py`: 1) keys en SQLite, si no, zip completo; 2) PICS para gids
y buildid; 3) Tier 0: zip local ya cacheado; 4) `/generate/manifest` por depot;
fallback zip completo. Sale un `game_data` con `_smart_update: True` que entra por
el mismo pipeline de 3.3: **actualizar = volver a bajar con DepotDownloaderMod**.
Baja solo los chunks que cambian porque DepotDownloader compara el fichero
existente chunk a chunk [inferred, comportamiento del upstream, DLL no leída].

### 3.8 Los commits del 09-14 [read]

- `4dd7375`: selección de build histórico. Los diálogos permiten elegir un gid
  antiguo en vez del último y guardan metadata de rollback.
- `accb40c`: tabla `missing_hubcap_depots` y pre-flight gratis con `/contents`
  antes de gastar cuota en `/generate/manifest`. Es su reacción al 09-09: Hubcap
  es el único generador que les queda y la cuota es el recurso escaso.

---

## §4 Puerta a puerta

| Puerta | ASSella | Nosotros | Diferencia real |
|---|---|---|---|
| G1 Propiedad / unlock | SLSsteam stock. `AdditionalApps`, `AppTokens`, `FakeAppIds`, `DlcData`, in-place por el inotify. Extra: tickets. | SLSsteam stock vía `slssteam_ops.py`. Sin tickets. | Igual. |
| G2 Depot keys | Lua de Hubcap → SQLite, reusadas en cada update. | Lua de Hubcap → `DecryptionKey` en `config.vdf` (`downloads.py:826`); Steam las reusa. | Mismo origen. Ellos las necesitan para updates porque DepotDownloader no es Steam. Nosotros no. |
| G3 Manifests | Solo Hubcap: zip o suelto. | depotcache → archivo local → P-ToyStore rama → tag → luastools → zip Hubcap (1/día/app). | Cuatro fuentes gratis antes de Hubcap contra Hubcap solo. |
| G4 Request code | No existe. `-manifestfile`. | Steam lo pediría; el pin en `ManifestIds` + manifest pre-sembrado hacen que no llegue a pedirlo. | Ninguno depende del request code. |
| G5 Chunks | DepotDownloaderMod, .NET 9. | Steam nativo. | Ellos: descarga fuera de Steam con `dotnet`. Nosotros: cero binarios. |
| G6 Registro (ACF) | Clásico: a mano. Experimental: `install|appid|0` y Steam lo crea. | Steam lo escribe solo al descargar (`downloads.py:935`). | Su modo experimental se acerca a lo que tenemos gratis. |
| G7 Update: detección | `.depot` vs PICS. Solo al abrir la app o headless externo. | `pins.py` cada 30 min, independiente de la UI. | Nosotros vigilamos siempre. |
| G8 Update: aplicación | Nuevos manifests → volver a bajar con DepotDownloader. Steam nunca actualiza; SLSsteam bloquea el nativo. | Mover pin + pre-sembrar manifest nuevo **y el instalado** → Steam actualiza nativo. Si falta el instalado, Steam se queda en "Actualizar" (`0f4a14e`, sin mergear, lo cura). | Modelo opuesto. Ellos nunca se atascan porque Steam no actualiza; tampoco actualizan si no abres la app. |
| G9 Build histórico | Sí desde 09-14: gid antiguo + rollback + `pin_build`. | Freeze en `pins.json`. Sin rollback. | Ellos tienen rollback. |
| G10 DLC | `DlcData` batch + manifests del mismo zip. | `DlcData` igual. | Igual. |
| G11 Workshop | Script Tkinter aparte, `/generate/workshopmanifest`, ACF a mano. | Nada. | Ellos, como script suelto. |
| G12 DRM / lanzamiento | Steamless, Goldberg, EOSSDK, netsock `fix.so`, ratings Denuvo. | Nada. Fixes por zip. | Fuera de alcance por diseño. |
| G13 Logros | SLScheevo + `schema-grabber`. | Nada; Steam nativo. | Fuera de alcance. |
| G14 Cloud saves | Nada. | CloudRedirect. | Nosotros. |
| G15 Plataforma | AppImage + `install.sh` (548 L) + .NET 9 + Headcrab + Byparr opcional. | Plugin Decky + `.so`. | App de escritorio que también corre en Deck vs solo Deck en modo juego. |

Coincidimos en G1, G2 y G10 porque los dos montamos sobre SLSsteam stock.
Divergimos en el resto porque ellos han sustituido a Steam como descargador. Eso
les da rollback, workshop y kit DRM, y les quita vigilancia 24/7, actualización
nativa, cero binarios y cloud. Donde la comparación es directa, G3, vamos por
delante.

---

## §5 La pregunta del 09-09

### 5.1 ¿Les afectó? [read]

No, y no por mérito propio: DepotDownloaderMod nunca pidió request codes. Su
exposición es indirecta: si Hubcap no puede darles un manifest, no instalan.

### 5.2 Lo que el 09-09 le hizo a Hubcap [doc] [measured]

La doc pública de Hubcap dice de `/generate/appmanifest/{app}`: "UNAVAILABLE —
app bundle generation has been retired. Steam patched the method it relied on,
and this endpoint now returns 503. Use /generate/manifest for individual
depots." Es decir: **Hubcap ya no fabrica zips completos de un juego.** Le quedan
dos vías: `/generate/manifest` (un manifest por depot y gid, ida a Steam en vivo)
y `POST /upload-manifest` (cuentas en allowlist suben manifests al archivo
compartido, el mismo modelo que luastools). El zip `/manifest/{app}` sirve lo que
haya en la estantería; `force_update` "triggers a manifest refresh" según la doc,
sin decir si sigue vivo tras el 09-09 [inferred: si refresca, será vía sueltos].

Esto explica el pivote de ASSella del 09-14 a sueltos, y el `broadcast.json`
actual ("Hubcap game updates api is back online").

### 5.3 Cuotas reales de Hubcap [measured 2026-09-14, key del usuario]

| Contador | Límite | Ventana | Endpoints |
|---|---|---|---|
| Descargas (`daily_limit`) | 25 | por día | zip `/manifest/{app}`, `/lua/*` |
| `single` | 1.500 | por día | `/generate/manifest` |
| `bundle` | 100 | por día | `/generate/appmanifest` (retirado, 503) |
| `workshop` | 500 | por día | `/generate/workshopmanifest` |

Key con caducidad a los 7 días (`api_key_expires_at`). `steam_service_ready:
true`. Gratis, con key pero sin contador: `health`, `user/stats`, `library`,
`search`, `status/{app}`, `manifest/{app}/contents`, `depot-keys` (solo IDs),
`generate/usage`.

Corrección a los comentarios de ASSella: el "1,500/day" de sueltos es correcto; el
"55/day" del zip no (25 para este rol). Corrección a este mismo análisis en una
versión anterior: dije que el suelto "no gasta API key"; gasta key y gasta cuota,
de otro contador.

### 5.4 Qué significa para nuestra cadena [read]

Nuestro último eslabón (`manifests.py` `resolve_manifest`) es el zip, solo para
el gid actual, una vez al día por app. Post-09-09 el zip es un archivo estático:
para un build recién salido que ni P-ToyStore ni luastools tienen todavía, el zip
casi seguro tampoco lo tiene. La llamada se gasta a ciegas (1 de 25) y vuelve con
el build viejo. El único mostrador de Hubcap que va a Steam es el suelto, y no lo
usamos. Ver §8-F1.

### 5.5 Cuánto nos importa hoy [measured, del sondeo del 09-13]

P-ToyStore 32/32 gids actuales, luastools 31/32, lag de P-ToyStore 6 min–11 h. El
hueco donde el suelto cambiaría algo es esa ventana de lag, y en ella el juego no
se rompe: el pin no se mueve, se sigue jugando la versión vieja, y el siguiente
pase de 30 min vuelve a mirar. Lo que cambiaría es que el update llegue en 30 min
en vez de en horas. El otro caso, manifest instalado perdido, exige cuatro fallos
simultáneos (depotcache, archivo local, P-ToyStore, luastools).

El día que muera P-ToyStore (org de GitHub con 114k ramas de manifests; el
patrón ManifestHub) la cadena queda en luastools + zip estático, y el suelto pasa
a ser lo único que sigue trayendo builds nuevos.

---

## §6 Confianza y riesgo

### 6.1 Código remoto y autoupdate [read]

- `broadcast.json` se lee de `raw.githubusercontent.com/niwia/ASSella/beta/`
  en cada arranque (`ui/status_pager.py:52`). Solo se muestra (`message`,
  `level`, `duration`). No ejecuta nada.
- Autoupdate (`main_window.py:3159`): lee `src/res/version` de la rama, baja el
  AppImage de la última release y sustituye el instalado. **Sin checksum ni
  firma.**
- `install.sh` y Settings ejecutan `curl -fsSL headcrab.pages.dev | bash`
  (`settings_sls.py:227`): SLSsteam con sudo desde un dominio ajeno a GitHub.
  Confianza transitiva en AceSLS, la misma que ya asumimos nosotros vía Headcrab.

### 6.2 Tráfico a terceros [read]

- `utils/isp_bypass.py`: modo por defecto `auto` = directo → DoH
  (Cloudflare/Google) → Tor local → "Wirecutter", un Cloudflare Worker de niwia
  con URL ofuscada (XOR + base85) y fallback en claro
  `rapid-thunder-fba1wirecutter.7ucking.workers.dev`. Reescribe la URL de Hubcap
  para pasar por el worker; la cabecera `Authorization` viaja intacta, así que el
  worker ve la API key [inferred: tiene que verla para reenviar]. Solo si fallan
  los tres anteriores.
- `core/ratings.py:127`: Supabase de niwia con token anon hardcoded, solo lectura
  (ratings de Denuvo). Telemetría pasiva de appids consultados [inferred].
- `yaml_config_manager.py:1608`: baja `fix.so` de
  `yesyes0649/steamnetsock-patch/releases/download/latest/` sin pin de versión
  ni hash y lo carga como `LD_PRELOAD` en el proceso del juego. Opt-in por juego.
- `core/steamdb_scraper.py`: Byparr/Camoufox contra Turnstile de SteamDB. Opcional.

### 6.3 Binarios sin fuente

Ver §2. Todo es "confía en niwia". No vamos a ejecutar nada de esto.

### 6.4 Secretos

API key de Hubcap en QSettings en claro. Igual que la nuestra en
`~/.config/lumadeck`. Empate.

### 6.5 Lo que escribe en Steam

`config.yaml` in-place por secciones, ACF a mano en clásico, `depotcache` en
experimental, `.DepotDownloader/` en cada juego. No toca `config.vdf`. Con
LumaDeck en la misma Deck los dos escribirían `config.yaml` por secciones y
sobrevivirían [inferred]; no probado, fuera de alcance.

### 6.6 Lo que no hay

Ni telemetría propia, ni cuenta de usuario, ni credenciales de Steam (PICS
anónimo). Sucio de arquitectura, no malicioso a la lectura.

---

## §7 Veredicto y pasada adversaria

### 7.1 Veredicto

ASSella es un gestor de escritorio que sustituye a Steam como descargador y
actualizador, montado sobre SLSsteam stock. Funciona tras el 09-09 porque nunca
pidió request codes, y depende al 100 % de Hubcap con key. Su update es peor que
el nuestro en vigilancia (solo con la app abierta) y mejor en garantía (Steam
nunca se queda en "Actualizar" porque nunca actualiza). Tiene rollback, workshop
y kit DRM, ninguno en nuestro alcance. En adquisición de manifests, la única
puerta comparable, estamos por delante con cuatro fuentes gratis antes de Hubcap.
Lo que sí nos enseña es que Hubcap ha cambiado de naturaleza (§5.2) y que su
único generador vivo es el que no usamos (§8-F1).

### 7.2 Pasada adversaria

- *"Ellos nunca se atascan en Actualizar."* Cierto, a cambio de no actualizar
  hasta que el usuario abre ASSella. Nuestro atasco exige que falten cuatro
  fuentes a la vez; tras `0f4a14e` no se mueve el pin y el juego sigue jugable.
- *"Hubcap solo es frágil."* Cierto, y es también nuestro último eslabón. La
  diferencia es que nosotros llegamos a él 1 de cada N veces y ellos siempre.
- *"Su SQLite de keys es más robusto."* Para ellos. Steam ya guarda las keys en
  `config.vdf`; su SQLite existe porque DepotDownloader no es Steam.
- *"El modo experimental con `install|appid|0` es lo que deberíamos hacer."* No:
  nunca escribimos ACF, Steam lo hace al descargar. Resuelve un problema que no
  tenemos (§8-F3).
- *"Descargar fuera de Steam es más rápido en la Deck."* Posible, y exige .NET 9
  y un AppImage de escritorio. Contra la premisa "Steam nativo sin tocar nada".
- *"El zip diario de Hubcap es un desperdicio con 25/día."* Solo si más de 25
  juegos tuvieran update el mismo día y todos fuera de P-ToyStore y luastools.
  No ocurre. Por eso §8-F2 es no.
- *"Hay lógica de adquisición escondida en la UI que no leí."* Mitigación: se
  buscó `morrenus_api`, `DepotDownloader`, `SLSsteam.API` y `depotcache` en todo
  `src/` incluido `ui/`; todas las llamadas van a los módulos de `core/` y
  `utils/` leídos enteros. Residual: bajo.
- *"Dije que el suelto era gratis."* Lo fue en una versión de este análisis y
  era falso. Corregido en §5.3 con la salida real de `/generate/usage`.

---

## §8 Hallazgos

Cuatro. **Ninguno es código hoy.**

- **F1 — plan B, `LumaDeck/backend/manifests.py`, NO AHORA.** Añadir
  `/generate/manifest?depot_id&manifest_id` como último eslabón de
  `resolve_manifest`, después de `fetch_luastools_manifest` y antes del zip,
  para cualquier gid (actual o instalado). Key: reutilizar la entrada Hubcap de
  `api.json` vía `api_request()` (ya mueve la key a la cabecera). Cuota: contador
  `single` (1.500/día), no toca las 25 descargas. Guardarraíl obligatorio: un
  intento por depot+gid al día (Hubcap puede no generar algunos gids; ASSella
  lleva `missing_hubcap_depots` por eso). Tamaño: ~30 líneas, sin UI, sin tocar
  `pins.py` (`resolve_all` ya lo llama). **Disparador para implementarlo**:
  P-ToyStore devolviendo 404 en el log de la Deck (org caída), o el mensaje
  `installed build's manifests are missing` visto en un dispositivo real. Por qué
  no ahora: §5.5.
- **F2 — descartado.** Pre-flight gratis `/manifest/{app}/contents` antes del
  zip. Solo ahorraría descargas en el caso 2 de §5.4, ya limitado a 1/día/app, y
  desaparece si entra F1. Con 25/día no aporta.
- **F3 — documentación, `slssteam-analysis.md` §4.2.** El pipe
  `/tmp/SLSsteam.API` (`install|appid|library`) queda cerrado como "no aplica":
  solo sirve cuando se descarga fuera de Steam, que es exactamente el modo
  experimental de ASSella. Nota añadida en esa sección.
- **F4 — exclusiones explícitas.** Rollback a build antiguo, workshop, Steamless
  / Goldberg / EOSSDK / netsock, tickets, logros con `schema-grabber`, scraper de
  SteamDB. ASSella puede hacerlos porque controla la descarga. Nosotros no, y no
  queremos: Steam nativo sin tocar nada. Anotado para no volver a mirarlo.

Pendiente de barrido: `niwia/ASSella` desde `accb40c`.
