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

---

## §9 Delta — 2026-09-17 (`accb40c` → `b099de7`, rama `beta`)

*Barrido el 2026-09-17 sobre clon fresco (deepen a 60; comprobado `.git/shallow`
antes de contar). `beta`: **33 commits** después de `accb40c`, del 14 al 17 de
septiembre, autor niwia salvo el PR #16 de KingCatto. `main` en `f582f4f`
(13-sep, README). Versión `src/res/version` = `2.6.5`, sin tag: la última release
sigue siendo v2.6.4 (9-sep). Leídos como diff `d3cbdbc`, `d9cf09b`, `fd651e9`,
`57bcb60`, `9c68932`, `edc8fa8`; el resto por mensaje y stat. Volumen: 73
ficheros, +18.652/−12.897, de los cuales ~10.000 son la modularización de
`gamelibrary.py`, `gamelibrary_v2.py` y `settings.py` en subpaquetes.*

**Lo primero.** Siguen sin ningún provider de request codes (grep de
`20770407|manifestdex|wudrm|steam.run|gmrc` en `src/`: cero). Hubcap sigue siendo
su único generador, y todo lo sustantivo de estos cuatro días es gastar menos
cuota en él o sacarle más:

| commit | qué hace | nos afecta |
|---|---|---|
| `fd651e9` (15-sep, KingCatto) | **Hecho verificado por ellos contra el fuente del servidor de Hubcap (`SolusManifestManager`):** `GET /api/v1/manifest/{app}` sólo sirve `{app}/public/{app}.zip`, ignora `?branch=`; `/contents` también es sólo public; `/generate/appmanifest?branch=` existe pero está desactivado (503); **el único camino a un manifest no-public es `/generate/manifest?depot_id=&manifest_id=` por gid.** Desde `d9cf09b` ASSella creía bajar betas y guardaba el bundle public con etiqueta beta. Ahora `branch_bundle.py` (206 L) monta la beta en cliente: claves y lua del bundle public, gids de la rama por PICS, cada manifest distinto por `/generate/manifest`, `setManifestid` reescritos. | **Dato útil para nuestra cascada**: el zip de Hubcap en `api.json` es public y sólo public. Nada que cambiar (no ofrecemos ramas), pero cierra la duda de si `?branch=` haría algo. |
| `57bcb60` (15-sep, KingCatto) | Verify / Smart Update / "Queue selected" / Web UI / CLI perdían la rama y caían a public, algunos persistiendo `selected_branch=public`. Un juego en beta se "actualizaba" al último public. | No. No gestionamos ramas. |
| `d9cf09b` (15-sep) | `download_manifest` pide siempre `?force_update=true` (refresco en servidor antes de servir) y cae a la llamada normal si falla; `generate_bundle_manifest` marcado deprecated ("the upstream bundle endpoint has been decommissioned"); comprobación de build en vivo por PICS antes de decidir si hay update. | Cosmético para nosotros; `force_update` es una forma de que Hubcap regenere el zip antes de servirlo. No medido si cuenta contra la cuota de §5.3. |
| `9c68932` (17-sep) | Frescura de caché: un zip cacheado de 0 bytes o sin manifests se tira y se rebaja; comparación con el buildid vivo de PICS; **actualización dirigida por manifest único** (`/generate/manifest` por gid) en vez de bajar el zip entero, reescribiendo el zip cacheado; `/contents` una vez para saber qué depots faltantes son recuperables antes de gastar `generate`. | No. Es la continuación de `accb40c` (§3.8). |
| `d3cbdbc` (14-sep) | **Detecta el watcher muerto de SLSsteam**: lee los últimos 64 KB de `~/.SLSsteam.log` buscando `Failed to read from FileWatcher` (el log se trunca en cada arranque, así que una aparición = muerto en esta sesión); visor amarillo "Steam: Restart", aviso persistente, y si está muerto ASSella escribe ella misma el ACF de fallback en vez de esperar a que SLSsteam lo genere. | **Ver abajo.** Es la issue #158 de SLSsteam (EINTR), arreglada en `dev` el 12-sep y **sin publicar** (`slssteam-analysis.md` §7.11). |
| `2f9ff1d` (16-sep) | "SHSAH Reborn": gestor de logros que lee y escribe los `UserGameStats` binarios de Steam (nativo y Flatpak), con esquema e iconos del CDN. Desbloqueo local de logros. | No. Nuestro camino de logros es otro (parche de lumalinux + SLSsteam); esto es un editor de ficheros. |
| `b76adeb` (17-sep) | Robustez del cliente CM en Python: `select`+`MSG_PEEK` antes de cada query, abort en `disconnected`, timeout 25 → 12 s, `auto_access_tokens=False`, fallback a la REST de SteamCMD. | No. No hablamos con CM. |
| `edc8fa8` (15-sep) | Uninstall avanzado: DLC ids de `DlcData` y del `.depot`, limpieza de config SLS, restaurar binarios EOS y backups de Goldberg, borrar `.DepotDownloader`/`.ACCELA`, `.depot`, claves QSettings. | Convergido en lo que nos aplica (`uninstall_game_full`: lua, keys, AdditionalApps, depotcache, compatdata opcional). |
| `58aefd7` (14-sep) | "Fix installation" repara manifests que faltan en vez de abortar; ACF de fallback offline; **`yaml_config_manager.py` reescrito (918 líneas cambiadas)**: límites de sección y borrado de claves acotado. | La escritura de YAML que §6.5 criticaba ha cambiado de arriba abajo; **no re-auditada**. Pendiente si volvemos a §6.5. |
| `e3c4847`, `663cc3a`, `af81e9b`, `8d02cc1` | Update check al despinear un build; toggle de depots ocultos; modal de aviso de DLC con 3 s de bloqueo; navegador de historial de builds. | No. |
| resto (~20) | Modularización (library/, settings_tabs/, game_details/), spinners, visor de SteamDB, pestaña Workshop condicional, botón de Discord de Hubcap. | No. |

**El watcher de SLSsteam (`d3cbdbc`), mirado desde nuestro lado.** El bug es
real y está en la release que instalamos (`20260903114323`): una señal
interrumpe el `read()` del `CFileWatcher` de `config.yaml` y el hilo muere hasta
reiniciar Steam; Ace tiene "2 reports". Con el watcher muerto, un Add Game de
LumaDeck escribe `AdditionalApps` y SLSsteam no lo relee: el juego no aparece
como propio hasta reiniciar. Nuestro watcher de `keys.txt` (`key_store.cpp`) no
tiene el bug, pero la mitad de SLSsteam sí. ASSella lo detecta por el log y
avisa. **Decisión pendiente, no tomada aquí:** LumaDeck podría hacer la misma
comprobación barata (tail de `~/.SLSsteam.log` buscando esa cadena) y mostrar
"reinicia Steam" en lugar de un Add Game que parece no hacer nada. Es un
diagnóstico, no un arreglo; el arreglo llegará con la siguiente release de
SLSsteam.

### Balance del delta

| # | Qué | Prioridad | Estado |
|---|---|---|---|
| 1 | Hubcap `/manifest` es sólo public (`fd651e9`) | — | Dato registrado; nuestra cascada no pide ramas |
| 2 | Detección del watcher muerto de SLSsteam (`d3cbdbc`) | Baja | Candidata a diagnóstico en LumaDeck; sin OK |
| 3 | `yaml_config_manager.py` reescrito | — | §6.5 desactualizado; re-auditar si se vuelve a esa sección |
| 4 | Todo lo demás | — | Nada |

El siguiente barrido arranca en `beta@b099de7`.

## §10 Delta — 2026-09-23 (`beta@b099de7` → `a0869bd`; rama nueva `canary`)

*Barrido el 2026-09-23. `beta`: 13 commits después de `b099de7`, del 17 al
20 de septiembre, niwia salvo tres de "You-know-who" (README y `voices.json`);
tag **v2.6.5** el 17-sep, `src/res/version` ya en `2.6.6dev`. `main` sigue en
`f582f4f` (13-sep). **Rama nueva `canary`** (`568552c`, 20-sep, versión
`3.0.0testing200926002`): 14 commits sobre `beta` en dos días, +2.873/−194 en
28 ficheros. Leídos como diff `185e201`, `a0869bd`, `78dbda7` (sus dos
plugins Lua enteros), `native_steam_handoff.py`, `steam_manifest_pinning.py`
y `native_steam_download_task.py`; el resto por mensaje y stat. Sigue sin
ningún provider de request codes en `src/` (grep: cero); el que hay está en
el plugin Lua, ver abajo.*

### `beta`: catálogo "Voices" y menudencias

| commit | qué hace | nos afecta |
|---|---|---|
| `185e201`, `711bc37`, `db1bdd9` (19-sep) | **"Voices"**: un `voices.json` en el repo (rama `beta`, sincronizado al arrancar con caché) con builds "probados y recomendados" por juego: `recommended_build_id`, `reason`, `manifest_overrides` (depot → gid) y alias de búsqueda; instrucciones de contribución por PR. Al buscar un juego que está en la lista sale un diálogo "Voices version available" que pinea ese build y sus gids; si `manifest_overrides` está vacío, resuelve los gids del build por SteamDB. Entradas: Persona 3 Reload, Black Myth: Wukong, Dead Space… ("Known stable and compatible build"). | **No, pero es una idea limpia**: una lista curada de "el build que funciona con el crack/bypass actual", mantenida por PR. Es lo que LumaDeck hace implícitamente con los fixes de lua.tools (el fix trae su build y LumaDeck lo pinea, `pinsource.py`); ellos lo separan del fix. Apuntado como referencia, sin acción. |
| `4428bec` (19-sep) | Comprobación previa: aborta limpio si no puede encontrar o generar los manifests de un depot, en vez de petar. | No. |
| `878b966`, `bc34aa2` (19-sep) | Workshop: sincronía de ACF, tamaño, enlace de mods locales (Ravenfield), y luego quitan el symlink porque cargaba el contenido dos veces. | No (Workshop no es nuestro). |
| `1fcd7a8` (19-sep) | Carrera "worker deleted" en su `TaskRunner` de Qt. | No. |
| `a0869bd` (20-sep) | `assfixer` (su reparador de `config.yaml`) reconoce `AdditionalDepots` y `DecryptionKeys` como claves legítimas "del plugin" y las conserva al sincronizar con la plantilla upstream; lee la plantilla del `res/config.yaml` de SLSsteam con fallback al `config_default.hpp` embebido. Motivo: usuarios de `beta` con `download.lua` veían avisos falsos. | Dato: confirma que **`download.lua` escribe esas dos claves en `config.yaml`** y que ASSella ya lo da por instalado en `beta`. |

### `canary`: "Vapor", descarga nativa por Steam con los plugins Lua

**[read] Qué es.** Un modo nuevo, pestaña "Vapor Beta" ("Enable Vapor (Native
Steam Integration)", "Always Native Steam"), que sustituye su DepotDownloader
por **que Steam descargue el juego**. `native_steam_download_task.py`, en su
propia cabecera:

1. claves y gids del bundle de Hubcap (o caché);
2. escribe en `config.yaml` de SLSsteam `Plugins: yes`, `AdditionalApps`,
   `AdditionalDepots`, `DecryptionKeys` (atómico, fsync) y, si el build va
   pineado, `ManifestIds` (`steam_manifest_pinning.py`);
3. despliega **`download.lua` y `spliced-tickets.lua`** en
   `~/.config/SLSsteam/plugins/` desde copias que **ahora viajan dentro de
   ASSella** (`src/res/plugins/`, 328 y 79 líneas);
4. espera a ver `AppLicensesChanged`/`Unlocked` para el appid en
   `.SLSsteam.log`, más 1,5 s "para que Steam propague en memoria";
5. limpia un ACF stub de un intento anterior;
6. manda `install|<appid>|<lib>` por **`/tmp/SLSsteam.API`**;
7. vigila `content_log.txt` y el `.acf` hasta `StateFlags 4`;
8. **al terminar con éxito borra `AdditionalDepots`, `DecryptionKeys` y los
   plugins** ("transient"), conservando solo `AdditionalApps`; en fallo
   restaura el backup del yaml, borra el ACF y manda `uninstall|<appid>`.

"Vapor library scan" (`2651bd1`, `48f11fe`, `568552c`) recorre la biblioteca
de Steam para descubrir juegos añadidos, con un watcher de refresco, y filtra
los que la cuenta posee de verdad.

**[read] Los dos plugins.** `download.lua` es el que analizamos en
`slssteam-plugins-analysis.md` (mismos nombres `getPackageHook` /
`getBinaryHook` / `getMRCHook`, mismo `place_lua_hook`): inyecta depots en el
paquete 0 (`GetPackage`), sirve claves (`GetBinary`) y engancha
`GetManifestRequestCode` con **un solo proveedor, `http://gmrc.wudrm.com`, en
HTTP claro**, con `manifest.opensteamtool.com` comentado; localiza
`GetManifestRequestCode` por un patrón de bytes fijo
(`E8 ? ? ? ? 83 C4 ? 83 F8 ? 0F 85 …`) sin derivación. `spliced-tickets.lua`
es el de Ace (§ nota del 22-sep en `slssteam-plugins-analysis.md`): ticket de
la app 7 con el appid empalmado, para que SteamStub se desempaquete sin
Steamless.

**[inferred] Lo que esto significa.** ASSella ha llegado, dos días de commits
mediante, a **nuestra arquitectura**: Steam descarga en nativo, el motor
inyecta depots y claves en el paquete 0, y el código de manifest sale de un
hook de `GetManifestRequestCode`. La diferencia está en cómo:

| | ASSella "Vapor" | LumaDeck + lumalinux |
|---|---|---|
| Inyección de depots/claves | plugin Lua de SLSsteam (`download.lua`), desplegado por descarga y **retirado al acabar** | `.so` propio, finder del paquete 0 + `keys.txt`, permanente |
| Request codes | wudrm en solitario, HTTP, patrón fijo en Lua | cascada opensteamtool → wudrm → steam.run, HTTPS, validación contra el CDN, hook GMRC con ficha + patrón + rescate por cadena |
| Después de instalar | claves y depots fuera de `config.yaml`; el juego queda **pineado** a los gids de Hubcap (`ManifestIds`) y sin update nativo | claves siempre cargadas; Steam actualiza en nativo mientras haya proveedor, pin solo si caen |
| "El juego ya es tuyo" | espera `AppLicensesChanged` en el log | reconcile (`NotifyLicensesUpdated`) sin reiniciar |
| Canal de órdenes | `/tmp/SLSsteam.API` (mundo-escribible; moon lo ha retirado por inseguro, `slsdeck-analysis.md` §18.2) | fichero de claves propio + inotify |
| Cuando Valve recompila | el patrón Lua de GMRC se rompe como cualquier patrón fijo; ASSella no tiene monitor ni derivación | watch-steam + derivación Python + tres capas en el `.so` |

Dos cosas para nosotros. (1) **Nada que adoptar**: es nuestro modelo con menos
capas. (2) **Spliced tickets ya no es teoría**: ASSella lo distribuye
empaquetado en su AppImage y lo instala en cada descarga nativa, junto con el
plugin de descarga. Es el tercer sitio donde aparece el plugin de Ace (Discord,
SLSDeck vía moon, ASSella), y refuerza el punto aparcado del bloque 1 de moon:
decidir si lo llevamos como plugin de SLSsteam o dentro de lumalinux. Sin
cambio de prioridad; el dato queda.

**Riesgo suyo, anotado.** `canary` es "3.0.0testing" con `AGENTS.md` que
restringe las builds de desarrollo a `ASSella.AppImage.dev`; se desarrolla con
un agente de IA, como SFF y SteamFlipper esta misma semana.

### Balance del delta

| # | Qué | Prioridad | Estado |
|---|---|---|---|
| 1 | "Vapor": descarga nativa por Steam con `download.lua` + `spliced-tickets.lua` empaquetados | — | Convergen a nuestro modelo; nada que adoptar |
| 2 | Spliced tickets distribuido a usuarios finales por un tercer programa | Dato | Refuerza el punto aparcado (plugin o lumalinux) |
| 3 | Catálogo "Voices" de builds recomendados | Referencia | Idea limpia; sin acción |
| 4 | `assfixer` conserva `AdditionalDepots`/`DecryptionKeys` | — | Confirma qué escribe `download.lua` |
| 5 | §6.5 (`yaml_config_manager.py`) sigue sin re-auditar | — | Pendiente si se vuelve a esa sección |

El siguiente barrido arranca en `beta@a0869bd` y `canary@568552c`.
