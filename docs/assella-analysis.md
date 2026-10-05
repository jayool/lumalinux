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
plugin de descarga. Es el segundo sitio público donde aparece el plugin de
Ace (su Discord y ASSella; **no** está en SLSDeck ni en moon, corregido
29-sep: sus "spliced" son otra cosa), y refuerza el punto aparcado del bloque
1 de moon:
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

## §11 Delta — 2026-09-28 (`beta@a0869bd` → `028832e`; `canary@568552c` → `3fb2e32`)

*Barrido el 2026-09-28. `beta`: 18 commits después de `a0869bd`, del 23 al
28, todos de niwia; sin tag (v2.6.5 del 17-sep sigue siendo la release,
marcada pre-release; `src/res/version` en `2.6.6dev`). `canary`: 40 commits
después de `568552c`, del 24 al 28, versión `3.0.0testing280926001`. `main`
sigue en `f582f4f` (13-sep). Leídos como diff: `at0m.py`/`vapor.py`
(`7413be8`, `66750ad`, `3cd01ee`, `2e83813`, `d340ed9`), `download.lua` entero
entre las dos puntas de `canary`, `native_steam_handoff.py` (`ac7fec9`,
`9b3240a`, `4d483cc`, `76af3c2`), `steam_manifest_pinning.py` y el patch del
config (`2b5d9a6`), `task_manager.py` (`eae10e4`), `assfixer.py` (`0e2ae82`);
el resto por mensaje y stat. Issues leídas: #17 y #18 (niwia, 21-sep, sin
cuerpo), #19 (fuente, duplicada). Una semana de 58 commits; el tema es uno.*

### El tema: wudrm bloquea a libcurl, y ASSella lo esquiva por tres sitios

**[read] Lo que pasó.** `gmrc.wudrm.com` está detrás de Cloudflare y su
filtro de huella TLS responde **503** a libcurl "a pelo". Le pega a ASSella en
dos sitios: al `download.lua` de SLSsteam (el hook de `GetManifestRequestCode`
usa el `curl.downloadString` de SLSsteam) y a la ACCELA clásica cuando quiere
un código por su cuenta. La respuesta, en orden cronológico:

1. **`at0-m` (antes "Vapor engine", `7413be8` 24-sep, renombrado `7aacd56`
   25-sep; en `beta` sigue en `vapor.py`): ASSella deja de depender de Hubcap
   para los manifests.** Un módulo de 693 líneas que hace lo que hace nuestro
   `manifests.py`: pide el código a wudrm, descubre los CDN por
   `IContentServerDirectoryService/GetServersForSteamPipe`, baja
   `/depot/<depot>/manifest/<gid>/5/<mrc>`, desempaqueta y descifra nombres
   con la clave del depot, y produce un `.manifest` válido (magic
   `0x71F617D0`). Hubcap (`morrenus_api.generate_single_manifest`) pasa a ser
   el **fallback** cuando el código o el CDN fallan. Es la misma inversión que
   hicimos en 0.21.0: el código de manifest es el camino, el zip de Hubcap el
   rescate.
2. **`WudrmMRCFetcher` en dos niveles (`66750ad`, `3cd01ee`, `2e83813`).**
   Nivel 1, `requests` con UA `curl/7.88.1` (elección suya; ver abajo); si el
   cuerpo no es numérico o llega 503, nivel 2: `curl_cffi` con
   `impersonate="chrome120"` (Client Hello, orden de cifrados y cabeceras de
   Chrome 120), y si `curl_cffi` no está, `curl` del sistema por subprocess.
   Backoff 1-2-4-8 s, cinco intentos, 400/404 son fatales (código inválido,
   no reintentar). Códigos cacheados en memoria y en SQLite
   (`db/mrc_cache.db`) por gid; `d340ed9` invalida la entrada cuando el CDN
   rechaza el código ("self-healing"). Nota suya en el código: reusar la
   sesión de `curl_cffi` da 503 sistemáticos, hay que abrir conexión por
   petición. `2e83813`: wudrm pasa a HTTPS.
3. **`download.lua` (`canary`, `7bb058a`, `26a2427`, `9b3240a`): el mismo
   truco dentro de SLSsteam.** `getManifestRequestCode` deja la recursión por
   un bucle con backoff, y ante cuerpo vacío o no numérico (detecta "503",
   "Service Unavailable", 502, 500 por texto) ejecuta con `io.popen` un
   **binario `curl-impersonate` de 31,7 MB** que ASSella despliega en
   `~/.config/SLSsteam/plugins/bin/` junto al wrapper `curl_chrome120`
   (script bash con la lista de cifrados de Chrome 120, `-4` para resolver
   solo IPv4). Ruta configurable por la clave `ImpersonateBin` del config de
   SLSsteam. Si todo falla devuelve el resultado nativo (`false`) para que
   Steam aborte limpio: *"un éxito silencioso con `pOutMRC=0` sería peor para
   los updates: Steam seguiría con el manifest viejo de depotcache y parecería
   que funciona"*. `7685ad4` **revierte** un fallback a depotcache dentro del
   hook por esa misma razón. Sigue con **un solo proveedor**, wudrm, y con
   el patrón de bytes fijo para localizar la función.
4. **`ac7fec9`: sembrar depotcache antes de la entrega a Steam.** El handoff
   nativo extrae los `.manifest` del zip de Hubcap en caché y los copia a
   todos los `depotcache/` que encuentra (raíz de Steam, `.local/share`,
   `.steam`, flatpak, biblioteca destino), *"para que Steam instale sin
   consultar endpoints MRC que Cloudflare puede bloquear"*. Es nuestro modelo
   0.8 (RESEARCH §19: fichero en depotcache = Steam no pide código), añadido
   como cinturón sobre el hook.

**[read] Lo que sabíamos y ellos no.** Nuestro `gmrc_store.hpp` documenta
desde 0.21.0 que wudrm *"sirve un reto JS de Cloudflare al User-Agent `curl`,
no al nuestro"* (`lumalinux/<versión>`), y que 20770407 también va tras
Cloudflare y exige un UA que no sea el de curl. El nivel 1 de niwia se
identifica precisamente como `curl/7.88.1`, y el `curl.downloadString` de
SLSsteam manda el UA por defecto de libcurl: **lo que Cloudflare filtra es el
UA, no la huella TLS**, o al menos con el UA cambiado nuestro hook no ve el
503 (medido 42/42 el 16-sep, RESEARCH §20.4, y cascada opensteamtool → wudrm
→ steam.run sin fallo por Cloudflare en los logs desde entonces). ASSella ha
resuelto con 31 MB de binario y una impersonación de Chrome lo que se
resuelve con una cabecera. No lo van a leer aquí; queda anotado por si el
filtro cambia de criterio, momento en el que su solución sería la correcta y
la nuestra dejaría de valer.

### `canary`: rollback nativo, separación de modos, y el puente que no fue

- **`2b5d9a6`, `76af3c2`, `652ab67`, `3fb2e32` (28-sep): rollback de build
  por `ManifestIds`.** `steam_manifest_pinning.py` conserva los comentarios de
  cada pin (`# Juego [desc] (appid)`), el patch del config escribe
  `AdditionalDepots` comentados y, en modo solo-DLC, saca el appid base de
  `AdditionalApps` y mete los DLC (si son menos de 64). Para volver a un
  build: pin en `ManifestIds`, manifest sembrado en depotcache, y primero
  `steam://validate/<appid>` para forzar la verificación (`76af3c2`),
  revertido el mismo día: *"rely on Steam manual/boot update"*, el texto de la
  UI pasa a "Steam will apply this build automatically on next launch or
  update". Es exactamente lo que medimos en `pins.py` (Steam relee el pin al
  lanzar, al arrancar y en cada reintento de update, no en reposo) y lo que
  nuestro Change version hace con `StateFlags |= 2` en el ACF. Convergen.
- **`eae10e4` (27-sep): un juego instalado por ACCELA no se "secuestra"
  hacia el handoff nativo al actualizar.** Decide por marcadores (`.accela`,
  `.depotdownloader`) y por el registro de "plugin games"; los nativos siguen
  nativos, los ACCELA siguen con su descargador. Nosotros no tenemos dos
  modos; nada que hacer.
- **`0855aae` → `4d483cc` (24 → 26-sep): `assella_bridge.lua`, nacido y
  muerto en dos días.** Un plugin de SLSsteam que abría un socket Unix
  (`/tmp/assella_ipc.sock`) para avisar a ASSella de eventos y contar depots y
  claves cargados. Retirado *"para evitar un crash"*; con él se va el
  `reloadlua` por `/tmp/SLSsteam.API` y el "touch" del config: `sls_bridge.py`
  ahora confía en el inotify de SLSsteam, y el handoff no reescribe un plugin
  idéntico (`4d483cc`, `63a998c`: sin ventana de aviso por fichero vacío
  durante la escritura atómica). Es la lección de moon D27 y de nuestro
  `key_store`: el watcher del propio `.so` basta.
- **`e8ad701` (27-sep): asistente de bienvenida de 5 pasos** (1.292 líneas +
  un GIF de Skyrim de 2,4 MB) y menos ajustes de SLS. `40b5aa5`: el updater e
  `install.sh` aíslan el canal `canary` del stable/beta. `d206ad4`, `e500221`,
  `114f9a1`, `2aae4ae`: en modo nativo se deshabilitan branch, verify, update
  all y uninstall; badge "Vapor"; Hubcap `get_user_stats` con caché de 60 s y
  espera de 120 s tras un 429 (*"throttle Hubcap stats to fix 429"*: el panel
  llamaba a la API de estadísticas demasiado). `d693baa`: búsqueda del
  marcador sin distinguir mayúsculas.

### `beta`: lo que no es wudrm

| commit | qué hace | nos afecta |
|---|---|---|
| `0e2ae82`, `2ab3e84` (23-sep) | `assfixer` 3.2.3 **escanea los `.lua` de `~/.config/SLSsteam/plugins/`** buscando `SLS.config:get*("Clave")` y añade esas claves a las legítimas, para no borrar `AdditionalDepots`/`DecryptionKeys`/`ImpersonateBin` como si fueran errores (issue #18). | No, pero es un dato sobre el config de SLSsteam: **los plugins Lua leen claves que no están en `res/config.yaml`**. Nuestro `slssteam_schema.py` completa claves que faltan, nunca borra, así que no tropieza con ellas. |
| `a134e07` (23-sep) | Workshop: descargador por lotes multi-juego, sincronía con depotcache, `workshop_helpers.py` nuevo (491 líneas) (issue #17). | No (Workshop no es nuestro). |
| `54ca547` (24-sep) | `workshop_keys.txt` a `db/` con migración. | No. |
| `2544d5e`, `52d9ca0`, `589fba4` (24-27 sep) | **Caracteres nulos en nombres de fichero de manifests**: `DepotDownloader.dll` recompilado, y saneado en la extracción del zip, la caché local y el descargador. | Vigilar: si Hubcap está sirviendo manifests con nombres corruptos, nuestro `manifests.py` los pasa a Steam sin tocar. Sin caso visto en los nuestros. |
| `9eef02e` (27-sep) | "Manifest envenenado": si la descarga falla con un manifest de Hubcap, lo regenera por wudrm (`at0-m`) y reintenta. | No. |
| `8424c84`, `028832e` (28-sep) | Modo solo-DLC: purga de ajustes aplicados al desactivar, respeta los depots que eligió el usuario en vez de podar por heurística. | No. |

### Balance del delta

| # | Qué | Prioridad | Estado |
|---|---|---|---|
| 1 | wudrm 503 por Cloudflare; ASSella lo esquiva con impersonación de Chrome | **Vigilar** | A nosotros no nos pasa: el filtro va por User-Agent y el nuestro no es `curl` (`gmrc_store.hpp`). Si un día el 503 nos llega con UA propio, la solución es la suya |
| 2 | `at0-m`: manifest por código + CDN, Hubcap como rescate | — | Nuestro modelo desde 0.21.0; convergen |
| 3 | Sembrar depotcache antes del handoff | — | Nuestro modelo 0.8; convergen |
| 4 | Rollback por `ManifestIds`, sin `validate`, "al siguiente arranque" | — | Lo que mide `pins.py`; convergen |
| 5 | `assfixer` lee las claves que piden los plugins | Dato | Las claves de plugin no están en `res/config.yaml`; nuestro completado no las toca |
| 6 | Nombres de manifest con caracteres nulos desde Hubcap | Vigilar | Sin caso en los nuestros |
| 7 | §6.5 (`yaml_config_manager.py`) sigue sin re-auditar | — | Pendiente |

El siguiente barrido arranca en `beta@028832e` y `canary@3fb2e32`.

## §12 — 2026-09-29: el modo nativo de `canary` es incompatible con lumalinux (causa en SLSsteam; sin acción)

Leído en código, sin ejecutar nada de ASSella: `download.lua` de `canary`
(`d676f04`), `LuaHook::place` / `LuaHook::remove` / `Lua::init` de SLSsteam,
`lmhook.cpp` de lumalinux.

**Qué engancha cada uno** (funciones de `steamclient.so`):

| Función | lumalinux | `download.lua` | Juntos |
|---|---|---|---|
| `CConfigStore::GetBinary` (claves) | DepotKey: feed → RTTI → patrón → por nombre; sirve de `keys.txt` sus depots, el resto a Steam | por nombre (`21IClientConfigStoreMap`→`12CConfigStore`); llama a Steam **primero** y pisa la salida si tiene clave en `DecryptionKeys` del config | misma función, lógica compatible |
| `BYieldingGetManifestRequestCode` (códigos) | GMRC: feed → patrón → xref; responde **antes** de Steam para sus depots, cuatro proveedores, código validado contra el CDN | patrón de sitio de llamada fijo (30 bytes, sin red); llama a Steam primero y sólo actúa si sale 0, sólo wudrm | misma función, lógica compatible |
| Paquete 0 | sin hook: finder recorre la caché y **añade** a `AppIdVec` (`+0x38`), revigila | hook en `GetPackage` (patrón fijo); **sustituye** `DepotIdVec` (`+0x48`) y libera el viejo con `Plat_Free`; lo restaura en `luaReload` | vectores distintos, no se tocan (anotar en RESEARCH: nuestro experimento descartó `+0x48`) |
| Ticket de propiedad | — (decisión: plugin, no hook) | `spliced-tickets.lua` | sin choque |

Los patrones de `download.lua` no se piden a SLSsteam ni se actualizan: van
en el AppImage y el zip remoto de respaldo sólo se usa si el empaquetado
falta. Cuando Valve recompile, su modo nativo muere hasta AppImage nuevo.

**Por qué revienta.** Los dos ponen hooks con libmem (5.1.5 en lumalinux),
que copia el prólogo al trampolín **sin corregir saltos relativos**. Lumalinux
lo sabe: `RelocateChainedJmp` recalcula el `rel32` cuando el prólogo ya era
un `jmp` ajeno (verificado, `lmhook.cpp`). SLSsteam **no**: `LuaHook::place`
hace `LM_HookCode` + `fixPICThunkCall`, que sólo corrige el `call` al thunk
PIC. Con Steam abierto lumalinux ya está enganchado cuando ASSella despliega
sus plugins (los copia al empezar cada descarga nativa, con `Plugins: yes`, y
los borra al acabar), así que:

- **Orden normal (lumalinux → `download.lua` encima):** el trampolín de
  `download.lua` contiene nuestro `jmp` con el desplazamiento viejo desde una
  dirección nueva. Primera llamada a `GetBinary` (Steam la usa para cualquier
  clave de config, constantemente): el hook Lua llama a su trampolín, salto a
  basura, **Steam se cae** segundos después de cargar el plugin. Su limpieza
  de fallo restaura el `config.yaml` de antes y borra `.acf` y plugins.
- **Orden inverso (plugins ya en la carpeta al arrancar Steam, restos de una
  descarga cortada):** `download.lua` engancha primero, lumalinux se encadena
  bien encima. Al borrar ASSella sus plugins, `Lua::init` cierra el estado
  Lua, `~LuaHook` → `LM_UnhookCode` restaura los bytes **que él guardó**: el
  prólogo limpio. Se lleva nuestro `jmp`. **Lumalinux queda mudo** (claves y
  códigos sin servir) hasta reiniciar Steam, sin error alguno.

Mientras la ventana está abierta (horas en un juego grande) **todo lo que
Steam descargue** pasa por la pila, no sólo lo suyo. Fuera de la ventana no
dejan nada en memoria; en disco, `Plugins: yes` y sus `AdditionalApps` (que
luego nadie puede actualizar: sin plugins cargados no hay quien sirva el
código). Otro choque, menor: su restauración de backup del `config.yaml` en
fallo se lleva lo que LumaDeck haya escrito en ese rato. Salvedad única: si la
libmem que compila SLSsteam corrigiera saltos (no consta), el orden normal no
se caería.

**Decisión: sin acción en código.** El defecto es de `LuaHook::place`; lo
sufre cualquier plugin de SLSsteam sobre cualquier hook inline previo, no
sólo nosotros; `canary` es "3.0.0testing" escrito por un agente
(`.agents/AGENTS.md`, `canary_rules.md`); ASSella `main`/`beta` no tocan la
memoria de Steam y conviven sin problema. No se puede bloquear (ASSella
escribe `Plugins: yes` él mismo; borrarle el `.lua` llega tarde y sabotea a
su usuario) y no se ha hecho prueba de convivencia: no es nuestro trabajo que
ASSella funcione encima de lumalinux. Lo único que cambia en lo nuestro:
spliced tickets va como **plugin**, porque un hook inline nuestro en esa
función sería un cuarto punto de choque con el mismo crash
(`slssteam-plugins-analysis.md`, nota del 28/29-sep).

Apuntado, no escrito, a decidir por el autor: (1) informe del bug a Ace, tres
líneas sin parche; (2) autocomprobación periódica de prólogos en lumalinux
(`overridden`/`removed` + módulo en `status.json`) y reinstalación
automática, que cubre el orden inverso, ~60 líneas; (3) aviso en LumaDeck al
ver `download.lua` en la carpeta de plugins; (4) sus dos patrones en
`check_patterns.py` como línea informativa; (5) copiarles el orden de GMRC
(preguntar a Steam antes de responder), cinco líneas.

| # | Qué | Prioridad | Estado |
|---|---|---|---|
| 1 | `canary` nativo sobre lumalinux: crash (orden normal) o lumalinux mudo (orden inverso) | Conocido | Causa en SLSsteam; sin acción; vigilar la promoción a `main` |
| 2 | Spliced tickets: plugin, no hook inline | Hecho | LumaDeck `b70ea44` |
| 3 | Opciones (1)–(5) de arriba | — | Decisión del autor |
| 4 | Confirmación pública (Discord SLSsteam, 29-sep): con la build `v3.0.0270926006` de `canary` "Steam se cierra a intervalos aleatorios"; niwia: "expected, untested waters, plugin testing", "just lab rats for assella"; Ace: "how do you even crash steam from an external tool?" | Dato | Encaja con §12; sin acción |

## §13 — 2026-09-29: `canary` nativo probado en limpio, y el "crash" de Discord leído con logs y commits

Complementa §12. Allí la incompatibilidad con lumalinux se dedujo del código
de SLSsteam. Aquí, dos cosas distintas: (a) qué hace de verdad el modo
nativo de `canary` **sin** lumalinux, medido en un codespace limpio; (b) qué
son los "crashes" que reportan sus usuarios ese mismo día, leído de sus
logs y de los commits de niwia. Ninguna de las dos cambia nada en lo nuestro.

### 13.1 Prueba en limpio [measured]

Codespace SteamOS (`.devcontainer/steamos/`, usuario `deck`), Headcrab +
SLSsteam, **sin lumalinux**, Steam logueado. ASSella `canary@6677a05` en
`.venv` (el `requirements.txt` no resuelve: `steam` git contra `vdf` git;
instalado en tres pasos más `pycryptodomex`, que falta), `--headless` con la
web UI en 8765, clave de Hubcap real en `ACCELA.conf`. Un `watch.sh` vigila
PID de Steam, md5 del `config.yaml`, listado de `plugins/`, mtime de
`/tmp/SLSsteam.API` y las líneas nuevas de `~/.SLSsteam.log`.

| # | Qué | Resultado |
|---|---|---|
| 1 | Arranque de ASSella | Despliega `download.lua` **y** `spliced-tickets.lua` en `plugins/`, y escribe `Plugins: yes` y `API: yes`. Residentes desde el arranque de la app, no sólo durante una descarga. |
| 2 | Balatro (2379780) por la web UI, `at0m_default_download_action=native` | Parchea `AdditionalApps`/`AdditionalDepots`/`DecryptionKeys` (`ManifestIds` vacío: sin pin), espera la licencia, envía `install\|2379780\|lib` por `/tmp/SLSsteam.API`. Steam intenta bajar y muere en el código de manifest: wudrm devuelve el reto de Cloudflare (403/503) desde la IP del codespace con cualquier UA; el fallback por `curl_chrome120` ya no existe en `6677a05`. Steam: "Unknown error" / "Content servers unreachable". Desde la misma IP, manifestdex sí da código. |
| 3 | Matar ASSella | No limpia nada: config, plugins y el `appmanifest` a medias se quedan. |
| 4 | 15 recargas de `config.yaml` en 30 s con `download.lua` cargado, sin descarga en vuelo | `Config reloaded` ×15, cero errores, Steam vivo. |
| 5 | Quitar y reponer `download.lua` con Steam abierto, 5 ciclos con 3 s entre pasos (cada `mv` dispara `Lua::onFileChange` → `Lua::init` → `~LuaHook` → `LM_UnhookCode` y vuelta a colocar) | Steam vivo tras los 10 reinicios del estado Lua; sin errores nuevos en el log. Evidencia indirecta: en release no hay línea de reload (`Ran x.lua` es `Once` y el `notify` está tras el guard), sólo la supervivencia. |

De (4): la hipótesis "`writeDepotIds` hace `Plat_Free` en cada recarga y
revienta" pierde fuerza, con la salvedad de que la reescritura es perezosa
(sólo en la siguiente `GetPackage(0)`) y no se puede contar cuántas veces
corrió: la línea `writeDepotIds into Package` es un `log.debug` de Lua, que
en SLSsteam va a `CLog::__debug` bajo `#ifdef DEBUG` y no existe en una
build release, sea cual sea `LogLevels` (medido: 0 con `0xff`). Los logs de
Discord llenos de `[Debug …]` son de usuarios con build debug.
El defecto sigue ahí de todos modos: el hook guarda `Downloader.Package` la
primera vez y no lo refresca; si Steam reconstruye el paquete 0, la siguiente
reescritura escribe en memoria muerta.

### 13.2 El "crash" de Discord del 29-sep, en cristiano [read: logs de usuario + commits]

Cronología en UTC (Discord se lee en CEST; la versión `3.0.0testing290926001`
del log del usuario y el salto a `…002` en `50fd657` la anclan):

| UTC | Qué |
|---|---|
| 07:40 | niwia publica el AppImage "3.0.3 canary", versión `…001`, con "wudrm bypass fixed". |
| 07:56 en adelante | tranquility (Steam nativo, 4 juegos en modo plugin) reporta: primero "crash", luego "no llega a crashear, se queda pegado", "al cabo de un rato", "sin tocar nada". |
| 08:14 | `cb3a49b` "sanitize env before io.popen to prevent Steam crash". A las 08:16 le pasa ese `download.lua` a mano al usuario. |
| 08:37 | `50fd657`: sync de plugins por SHA al arrancar, versión `…002`. |
| 09:29 | `6677a05`: **revierte** todo, `download.lua` vuelve al original de Ace, sin subproceso. |
| 13:54 | Nuevo AppImage "3.0canary", "this should fix all". |

**El cuelgue.** El "wudrm bypass" de esa build es un `io.popen` de
`curl_chrome120` dentro del hook de `GetManifestRequestCode`. Ese hook corre
con `LuaMutex()` cogido, y `LuaMutex` envuelve `Lua::stateMutex`: un único
`std::recursive_mutex` compartido por todos los hooks de todos los plugins y
por los callbacks de config (`lua.cpp`, `lua.hpp`). Por cada manifest sin
código, con wudrm bloqueado:

| Paso | Máximo |
|---|---|
| `curl.downloadString(url, 5)` | 5 s |
| `io.popen(curl_chrome120 --max-time 6)` | 6 s |
| `ffi.C.sleep` entre intentos (1, 2, 4, 8) | 15 s |
| 5 intentos | **70 s** con el mutex cogido |

Mientras, `GetBinary` (lo llama el hilo de UI de Steam sin parar) pide el
mismo mutex y espera. Steam no muere: se congela. Con 4 juegos sin licencia
en modo plugin, la comprobación periódica de actualizaciones de Steam
encadena esos 70 s depot tras depot. El propio niwia pegó su
`MRC blocked/down (503 Service Temporarily Unavailable)` esa mañana: wudrm
también le bloquea a él. El comentario del fichero dice que el bucle es "para
no bloquear Steam dentro del mutex varios segundos" y justo debajo mete un
`sleep` de 8 s dentro del mutex. El `env -u LD_PRELOAD` de `cb3a49b` arregla
que el `curl` de 64 bits intente cargar el SLSsteam de 32; no arregla el
`fork()` desde un Steam multihilo con el mutex cogido, ni los 70 s. Con el
`download.lua` original de Ace (tras la reversión) quedan 5 × 5 s = 25 s por
manifest, también con el mutex cogido, pero sin subproceso ni `sleep`.

**La carpeta vacía ("0 KB").** Animal Well (813230) por handoff: el log de
ASSella muestra config parcheado a las 08:36:40, "Waiting for Steam license
propagation" durante 16,5 s exactos (= `_poll_license_unlocked` con timeout
de 15 s + `sleep(1.5)`: **el poll no vio la licencia y siguió**), sin línea
"Synced N manifest(s) into depotcache" (cero manifests locales), `install`
por la API a las 08:36:56, y a las 08:36:59 el VaporWatcher lee
`StateFlags=4` y lo marca `up_to_date`; el escaneo siguiente dice "Skipped
empty game folder: Animal Well". La causa está en el log de arranque de
SLSsteam del mismo usuario: `813230` en `AdditionalApps`, pero **ningún
`813231` en `AdditionalDepots`**. `_patch_config` construye
`AdditionalDepots` con las claves de depot del bundle menos la del app
(`native_steam_download_task.py`); el bundle de Hubcap trajo la clave del app
y no la del depot. Steam ve un juego propio sin depots, "instala" en tres
segundos y ASSella lo da por bueno. `DisableUpdates: yes` no interviene: no
hubo depot que actualizar.

**"Pasar el juego a modo ASSella" no es cambiar un flag.** Crea el marcador
`.ACCELA` en la carpeta instalada por Steam y lanza DepotDownloader (.NET 9)
contra esa misma carpeta, validando el juego entero fichero a fichero. La UI
de PyQt se queda sin responder y el proceso sobrevive al cierre de la
ventana ("python shit", niwia).

**Lo que los logs no contienen.** Ninguno de los cuatro logs pegados
contiene el momento del cuelgue: los de ASSella son arranques limpios y los
de SLSsteam están cortados por Discord (`LogLevels: 0x3F`, que ASSella
fuerza, produce ~1 MB por arranque). El mecanismo del cuelgue está
deducido del código y de los commits de niwia, no medido.

### 13.2b Segundo log (tarde del 29-sep): Steam ya no arranca [read: log completo del usuario]

Tras la build de las 13:54 UTC ("this should fix all"), el mismo usuario: "my
steam wont start anymore, crashing on startup". Esta vez el log de SLSsteam
está entero (6495 líneas, build debug) y **termina en el momento del crash**:

```
SendAndRecv(…, ContentServerDirectory.GetManifestRequestCode#1, …)
Curl::getString(http://gmrc.wudrm.com/manifest/9024591918084649408)
[utils.cpp:exec] Created pipe 150 : 151
[utils.cpp:exec] Child PID 344072
[utils.cpp:exec] Exit Status: 0
[lua.cpp:debug] MRC server http://gmrc.wudrm.com/manifest/ returned non-numeric response, skipping
```

Y ahí acaba el fichero. Sin línea de error. Lo que se lee:

- Es el **arranque**: Steam revisa el juego instalado en modo plugin
  (`Using config key for 3832490` 36 líneas antes), Valve le niega el
  código, y el hook de GMRC hace la **primera** petición a wudrm de la
  sesión. Steam muere justo después. Determinista en cada arranque, de ahí
  "ya no arranca".
- El mensaje "MRC server … returned non-numeric response, skipping" no
  existe en ningún `download.lua` publicado (ni el de Ace ni `cb3a49b`): es
  el de la build de las 13:54, que no está en el repo. Un bucle por
  servidores, del que sólo vemos el primero fallar.
- El remedio de niwia: borrar `AdditionalDepots` y `DecryptionKeys` del
  config y vaciar `plugins/`. "Yea that worked". Sin plugin no hay hook, y
  sin depots no hay motivo para pedir código. El usuario se queda sin modo
  nativo.

**Corrección a lo dicho arriba sobre "sin subproceso".** Las tres líneas
`utils.cpp:exec` no son de niwia: son de SLSsteam. `Curl::downloadString`,
lo que Lua ve como `curl.downloadString`, **no usa libcurl: hace `fork` +
`execve` de `/usr/bin/curl`** y lee su salida por un pipe (`curl.cpp`,
comentario de Ace: "SteamOS seems broken. Curling certain URLs will crash
inside libssl.3.so"; es la misma libcurl rota del runtime que nosotros
esquivamos con `libcurl_pin.cpp`). Con `--connect-timeout N` y **sin
`--max-time`**: un servidor que acepta la conexión y no contesta deja al
hijo vivo, al `read` del pipe bloqueado, y al hook con el mutex cogido
**sin límite**. Así que también el `download.lua` original de Ace bifurca
Steam en cada petición de código, y sus "25 s" son 5 × 5 s sólo para
conectar; después no hay techo. El `io.popen` de niwia era un segundo
`fork` encima del primero.

**Candidato al crash, no verificable sin la build.** El hijo salió con 0 y
el hook siguió; lo siguiente que hizo no dejó línea. Los hooks Lua de
SLSsteam son callbacks FFI de LuaJIT llamados directamente por Steam, sin
`pcall` ni `lua_atpanic` (`lua.cpp`, `hooks.cpp`). La documentación de
LuaJIT (`ext_ffi_semantics`, "Callbacks"): lanzar un error a través de un
callback "is allowed but not advisable… only if you know the C function
that called the callback copes with the forced stack unwinding". Steam no
está preparado para eso. Un error de Lua sin capturar en el código nuevo
tras el "skipping" (un `nil` concatenado, un índice fuera de la lista de
servidores) desenrolla la pila de Steam por la fuerza y el proceso muere
sin pasar por el log. Encaja con: log cortado sin error, determinista,
sólo con esa build, sólo cuando wudrm falla.

### 13.3 Qué significa para nosotros [read]

- **Nada que tocar.** LumaDeck no usa `download.lua`. Nuestro
  `lumadeck-spliced-tickets.lua` es el fichero de Ace byte a byte, igual que
  el `spliced-tickets.lua` que despliega ASSella; ambos comparten el guard
  global `SplicedTickets.setup`, así que con los dos instalados el segundo en
  cargar no engancha nada (verificado en el fuente, líneas 2, 7 y 10).
- El hook Lua de tickets y el hook VFT propio de SLSsteam sobre la misma
  dirección (`0xd35ae730` en ese log) es la cadena normal, no un choque.
- El mutex es global a **todos** los plugins: si `download.lua` se cuelga
  70 s con él cogido, nuestro hook de tickets también espera y el juego no
  arranca hasta reiniciar Steam. Sólo afecta a quien tenga ambas
  herramientas y sólo con las builds de esa mañana; no es nuestro fichero.
- Sigue en pie §12 (choque con lumalinux por `jmp` encadenado); esta
  sección no lo contradice, lo acota: los crashes que se reportan en
  público no vienen de lumalinux, vienen de su propio `download.lua`. Y
  con las pruebas 4 y 5, SLSsteam solo (recargas de config y desmontaje de
  hooks en caliente) tampoco tumba a Steam.

| # | Qué | Estado |
|---|---|---|
| 1 | Modo nativo de `canary` en limpio: funciona hasta el código de manifest, muere en wudrm | Medido |
| 2 | Cuelgue de Steam del 29-sep: `io.popen` + `sleep` dentro del hook de GMRC con el mutex global cogido, hasta 70 s por manifest | Deducido de código y commits; revertido por niwia en `6677a05` |
| 2b | Steam no arranca con la build de las 13:54: muere tras la primera petición a wudrm, sin línea de error; remedio de niwia = quitar depots, claves y plugins | Log completo del usuario. **Causa verificada el 30-sep con la build (§14.1)**: `download.lua` l.272 concatena el `uint64_t` `manifestId` en un string; LuaJIT lanza, el callback FFI desenrolla Steam |
| 2c | `curl.downloadString` de SLSsteam es `fork`+`execve` de `/usr/bin/curl` sin `--max-time`: todo `download.lua`, el de Ace incluido, bifurca Steam por cada código y puede esperar sin límite | Fuente de SLSsteam (`curl.cpp`, `utils.cpp`) |
| 3 | Carpeta vacía: bundle sin clave de depot → `AdditionalDepots` sin el depot → Steam "instala" en 3 s | Medido en el log del usuario |
| 4 | Prueba 5 (desmontaje de hooks en caliente) | Medido: Steam sobrevive. El conteo de `writeDepotIds` no es medible en release |

---

## §14 — 2026-09-30: la build rota en la mano, el crash con nombre y línea, y un arreglo probado en el codespaces

*Fuentes: el AppImage `3.0.0testing290926005` (la build "pinned" de Discord,
inventario completo por listado y zip de sus ficheros de texto), el repo
`canary` hasta `e49c967` (`300926001`), LuaJIT 2.1 local, y dos pruebas en el
codespaces SteamOS con SLSsteam stock. Cierra el "candidato" de §13.2b.*

### 14.1 El crash, verificado [measured: build + LuaJIT]

La build fijada en Discord lleva un `download.lua` que **no está en el repo**:
`canary@6677a05` había revertido al lua de Ace, y el AppImage vuelve a meter
el bucle de servidores (`MRC_SERVERS` = wudrm + `http://167.235.229.108/`,
"ryuu"; ambos responden 403 desde el codespaces y desde aquí). El camino
cuando wudrm devuelve HTML:

```lua
-- tryFetchMRC: respuesta no numérica -> log.debug("... skipping"); return nil
-- getManifestRequestCode, l.272:
log.warn("MRC server " .. serverUrl .. " failed for " .. manifestId)
```

`manifestId` llega al hook como `uint64_t` (firma `GetMRC_t`), y LuaJIT no
define `..` para cdata de 64 bits:

```
$ luajit -e 'local ffi=require"ffi"; local m=ffi.new("uint64_t",275796854305563747ULL)
             print(pcall(function() return "failed for " .. m end))'
false   attempt to concatenate 'string' and 'uint64_t'
```

El error salta dentro de un callback FFI sin `pcall` (`lua.cpp`,
`hooks.cpp`), desenrolla la pila de Steam y el proceso muere sin escribir
nada más: exactamente el log de §13.2b, cortado tras "skipping". La l.276
("All MRC servers failed for manifest " .. manifestId) tiene el mismo fallo.
**El lua original de Ace lo tiene también**, en "Failed to download manifest
request code for " .. manifestId, pero sólo se alcanza con respuesta vacía;
con la página de Cloudflare va por "Invalid MRC response " .. codeStr, que es
string. Niwia unificó los dos casos en la rama que revienta.

Alcance: determinista en cada arranque con un juego en modo plugin (Steam
pide el código, wudrm falla, l.272). Sólo se salva si wudrm contesta un
número (el "Animal Well funcionó" de Discord). `SafeMode: 0` en el config de
Shinji: ni siquiera la lista de hashes de Ace estaba activa. El commit
`cfc49ef` (30-sep 03:29, "next build will work better") lleva **el mismo
`download.lua` byte a byte**; la l.272 sigue.

### 14.2 Inventario del AppImage [read: listado + ficheros de texto]

837 MB, 17.702 ficheros. `bin\.venv` 602 MB (venv de Python 3.13 con PyQt6,
numpy, tcl/tk, PyInstaller: la máquina de niwia tal cual). `bin\src` 169 MB
(101 MB de `deps`: Goldberg ×4 de 20 MB, SteamKit2, DepotDownloader,
Steamless, schema-grabber, EOSSDK; 40 MB `steam_headers.db`; 17 MB de gifs y
fuentes). Y dos cosas que no son de ASSella:

- **`bin\decky_loader` + `decky_loader-3.2.6.dist-info`**: el backend de
  Decky Loader desempaquetado, con `direct_url` apuntando al runner de CI de
  Decky (`/home/runner/work/decky-loader/...`). Ninguna referencia desde el
  código de ASSella. Basura del directorio de build. Inofensivo.
- **`bin\tor`**: Tor real (`Tor version %s`, torrc), con `libevent`,
  `libseccomp`, `libcap`, `libsystemd`. Lo lanza `utils/isp_bypass.py` como
  tercer escalón para la API de Hubcap (ver 14.4). No toca descargas.

Los `.pyc` no cuadran con los `.py` (`vapor.py` de 133 bytes con un pyc de
58 KB; `at0m.py` de 41 KB con un pyc 3.14 de 253 bytes): es sólo el renombrado
`vapor` → `at0m`. Nada raro.

### 14.3 Diferencias de la build frente a `canary` [read: diff]

Además del lua: `dispatch_steam_url()` lanza `steam steam://install/<appid>`
como proceso **además** de escribir `install|<appid>|<lib>` en
`/tmp/SLSsteam.API` (dos órdenes por vías distintas); los comandos al pipe
llevan `\n`; el handoff resuelve todos los depots con clave si no se eligen
y registra en `plugin_library.json`. `cfc49ef` lo empeora: bucle de hasta 8
intentos cada 3 s por las dos vías más un hilo de 6 reintentos cada 5 s
(hasta 28 órdenes de instalar), presiembra de manifests en `depotcache`
(correcto, es lo nuestro) y códigos de salida de Steamless.

### 14.4 Lo demás que salió al revisar la build [read]

| Qué | Dónde | Por qué importa |
|---|---|---|
| Clave de Hubcap por un proxy de niwia | `utils/isp_bypass.py`: cadena directo → DoH → Tor → "Wirecutter", un Cloudflare Worker cuya URL va "cifrada" con XOR+base85 (`_WIRECUTTER_SECRET`); descifrada: `rapid-thunder-fba1wirecutter.7ucking.workers.dev` | Todas las llamadas a Hubcap, con `api_key` en la URL, pasan por su servidor cuando los tres escalones anteriores fallan o el usuario elige "Wire". Modo por defecto "auto" |
| Código de terceros en Steam sin fijar versión | `native_steam_download_task.py`: si faltan los lua empaquetados, baja `plugins-deps.zip` del release "latest" de `ciscosweater/enter-the-wired` a la carpeta de plugins | Lo que haya en ese zip ese día corre dentro de Steam |
| Web en la LAN sin auth | `utils/web_server.py`: `0.0.0.0:8765`, CORS abierto, POST que lanzan descargas con la clave del usuario | Apagada por defecto (`enable_remote_web_ui`); al activarla, cualquiera en la wifi |
| AppIDs en `AdditionalDepots` | `native_steam_download_task.py` registra `depot_ids=list(depot_keys.keys())` (incluye la clave del propio AppID) y `plugin_games.register_plugin_game` lo escribe sin filtrar; `sync_all_plugin_games_to_config` lo reescribe en cada arranque | Es lo que Ace avisa en su Discord ("only actual depots in AdditionalDepots, appIds break SLSsteam functionality") culpando a psyche. La captura de Shinji lo muestra: `2483190 # Forza Horizon 6 (2483190)` es el AppID (está en `AppTokens`) |
| "Missing LogLevels… Missing AdditionalDepots" espurios | ASSella escribe el config varias veces por juego (app, cada depot, cada clave), SLSsteam relee en cada `IN_CLOSE_WRITE`; el "repair" (`assfixer.py`) usa `open(...,"w")` que vacía el fichero antes de rellenarlo | Una relectura que cae en el hueco lee vacío: todo "missing" y el "Missing DecryptionKeys!" del lua. Ruido, no daño |
| Mensajes remotos | `status_pager.py` lee `broadcast.json` de la rama `beta` cada poco y lo muestra | Sólo texto |
| Denuvo statuses | `ratings.py`: Supabase con clave anónima, sólo lectura | Nada |

### 14.5 El arreglo, y qué prueba [measured: fork + codespaces]

Rama `fix/mrc-out-of-hook` en `jayool/ASSella` (sobre `canary@e49c967`;
commits `2fd6ecf`, `81213c6`). No es para PR: es la demostración de que el
problema es de arquitectura, no de una línea.

- **`download.lua`**: fuera `MRC_SERVERS`, `tryFetchMRC`,
  `getManifestRequestCode` y los reintentos. `configLoaded` lee una sección
  `ManifestRequestCodes: {gid: mrc}` del config a una tabla, gid y código
  como **strings** (`asString`; nunca por número de Lua). `hkGetMRC`
  convierte el manifest con `snprintf`, busca en la tabla, escribe el código
  con `strtoull` o devuelve el resultado del trampolín. Cero red, cero
  `curl`, cero concatenación de cdata dentro del hook.
- **Python**: `core/native_steam/steam_mrc_prefetch.py` pide los códigos
  **fuera de Steam** con el `WudrmMRCFetcher` que `at0m.py` ya tenía (2
  intentos × 6 s por gid, caché de más de 15 min descartada), también para
  el gid público actual de Valve vía `steamcmd.net`, y los escribe en el
  config. Enganchado en los tres caminos de instalación: tarea nativa,
  handoff, e `install_via_sls` (el de la web, con los gids sacados de los
  nombres de fichero del zip de Hubcap).

Pruebas en el codespaces (SteamOS, SLSsteam stock, `Plugins: yes`, Balatro
2379780, manifest borrado de `depotcache`):

| # | Cómo | Resultado |
|---|---|---|
| A | Instalar desde Steam, sin código y sin manifest: el camino exacto que mataba a Steam | `No prefetched MRC for depot 2379781 manifest 3512319404653808464`; CDN 401; Steam cancela con su error normal ("Unknown error"); **Steam vivo** |
| B | Desde la web de ASSella (camino `install_via_sls`) | `[MRCPrefetch] 2379780: 3/3 MRC(s) resolved`: **wudrm contesta** desde fuera de Steam gracias a la imitación de Chrome de `curl_cffi` (el `curl` pelado del lua recibe la página de bloqueo). Sección escrita en el config. Steam descarga 2379781 (60 MB) + 228989 con esos códigos, sin un solo 401 para ellos. `InstalledDepots` completo, botón Jugar |

Lo que B deja ver de paso: tras instalar, Steam pide el **depot de shaders**
(id = AppID, manifest `8352398215100954353`, el mismo que el lua viejo pedía
a wudrm el 29), sin código → 401 → Steam se rinde y deja el juego instalado.
Es la limitación de todo el ecosistema con `download.lua`; lumalinux la
resuelve con su hook de shaders (apagado, como moon). Con la build de niwia
ese mismo depot era el que disparaba el crash.

### 14.6 Qué significa para nosotros [read]

- Sigue sin haber nada que tocar en lumalinux/LumaDeck. Lo de §13.3 se
  mantiene, ahora con la causa del crash con nombre y línea.
- Sobre los AppIDs en la lista de depots (14.4): nosotros también metemos el
  AppID de `keys.txt` en el paquete 0, **pero en `AppIdVec` (+0x38)**, la
  lista de apps, que es la que Steam consulta para el filtro de licencia
  (RESEARCH.md; `DepotIdVec` +0x48 probado y descartado en 0.16.14). El
  `download.lua` escribe `AdditionalDepots` en la de **depots** (+0x48).
  Es el caso que Ace avisa; no es el nuestro. Experimento pendiente si se
  quiere cerrar del todo: variable de entorno que excluya el AppID de la
  inyección y comparar instalaciones.
- La precarga fuera de Steam con imitación de Chrome **sí saca códigos de
  wudrm hoy** (prueba B). Es un dato para nuestra cascada de GMRC.
  **[Corregido 2026-10-05]** Aquí ponía que la cascada estaba "desactivada
  tras `LUMA_GMRC=1`". Era falso ya al escribirlo: `LUMA_GMRC=1` fue el
  opt-in de v0.20.x, y desde v0.21.0 el hook GMRC va **activo por defecto**
  y se apaga con `LUMA_NO_GMRC` (`src/main.cpp:158-173`; el código actual no
  lee `LUMA_GMRC`). La imitación de Chrome tampoco nos hace falta para
  wudrm: Cloudflare reta al User-Agent `curl` (medido el 5-oct: `curl` 403
  con `Cf-Mitigated: challenge`, UA de navegador 200, misma IP), y
  `gmrc_store.hpp:57-66` ya manda el suyo propio (`lumalinux/<versión>`).

---

## §15 — 2026-10-01: el lua de R2, la caché que no se refresca, beta 2.7.0 y el anuncio

*Fuentes: el `download.lua` servido desde R2 (bajado en el codespaces el
30-sep), `canary` hasta `64a7ab4` (`300926012`), `beta` hasta `03b8933`
(`2.7.0beta`), los hilos del Discord de ASSella del 30-sep y 1-oct, y la
conversación directa con niwia del 29/30-sep. Continúa §14.*

### 15.1 El `download.lua` de R2 [read: fichero]

`sha256 84d6c23f…`, 10.656 bytes, `plugins_manifest.json` con
`updated_at 2026-09-30T11:14:22Z`. Frente al de la build pineada (§14.1):

- La línea del crash está arreglada: la concatenación usa `manifestIdStr`
  (l.301/305), no el `uint64_t`.
- Los hooks van dentro de `pcall`, con el mutex cogido fuera y soltado en las
  dos ramas. Un error de Lua ya no desenrolla Steam.
- Sigue todo lo demás: `curl.downloadString` (fork de curl) **dentro del hook
  de GMRC con el mutex global cogido**, `MAX_MANIFEST_TRIES = 5`, un solo
  servidor (`http://gmrc.wudrm.com/manifest/`), y una `Downloader.MRCCache`
  en memoria **sin caducidad**: un código cacheado se sirve hasta que Steam se
  reinicia, aunque Valve lo haya rotado.

Es la mitad de la pista que se le dio el 29 (§15.5): quitó el síntoma con
nombre y línea, no la causa.

### 15.2 Por qué el arreglo no llega a quien lo necesita [read: `plugin_manager.py`]

Verificado sobre `canary@64a7ab4`, no de memoria:

1. **Los caminos de instalación no despliegan.** `native_steam_download_task.py:212`,
   `native_steam_handoff.py:325`, `info_tab.py:2330` y
   `download_backend_dialog.py` llaman a `are_plugins_present()`, que es un
   `is_file()` por plugin. Si el fichero existe no se toca; si no existe, error
   al usuario ("deploy from Settings"). `_deploy_plugin()` del task y
   `deploy_bundled_plugins()` del handoff siguen en el árbol **sin ningún
   llamador**.
2. **Los únicos despliegues** salen del diálogo de bienvenida de canary (una
   vez: `canary_welcome_seen`) y de los botones de la pestaña AT0-M
   (`deploy_sls_plugin` / `deploy_plugin(force_download=True)`).
3. **Y comparan contra un manifest congelado.** `fetch_plugins_manifest()`
   devuelve la copia local si existe y sólo va a R2 cuando no la hay. Nadie en
   el árbol pasa `force_refresh=True` ni borra esa caché. El `expected_sha256`
   es el del primer arranque del usuario.
4. **Consecuencia:** quien desplegó antes del 30-sep 11:14 tiene el lua viejo
   y la pestaña lo marca válido; si pulsa redesplegar, `download_plugin` baja
   el lua nuevo, lo compara con el sha viejo y lo rechaza (`SHA-256 mismatch`).
   Ni la instalación ni el botón lo arreglan; hay que borrar a mano la
   carpeta de plugins y la caché de ASSella.

Shinji comprobó el 1-oct que su lua era el `84d6c23f` y niwia lo dio por
"working"; sólo demuestra que su primer despliegue fue posterior al cambio de
R2, el caso que no tiene el bug. Pista enviada a niwia el 1-oct 09:25
("the compare runs against the manifest copy that's cached on first run and
never re-fetched"); respuesta "noted".

### 15.3 El Discord del 30-sep y 1-oct [read: hilos]

| Qué | Causa (nuestra lectura) |
|---|---|
| "Missing LogLevels … Missing AdditionalDepots" y "Missing DecryptionKeys" espurios (Shinji) | El "repair" (`assfixer.py:1334`, `open(..., "w")`) vacía el config antes de rellenarlo; SLSsteam relee en el hueco. Ya en §14.4; no es la línea en blanco que sospechaba Shinji |
| Steam muere al instalar "Librarian: Tidy Up the Arcane Library!" (Shinji, build pineada) | Sin línea de error de Lua antes de morir en su `.SLSsteam.log`, hash `237495b4` (escritorio stable). Compatible con el lua viejo sin pcall (§14.1); **no sabemos** si ya tenía el de R2. niwia sigue preguntando "have u got any steam crashes" |
| `UnboundLocalError: QMessageBox` al pulsar actualizar (Bleibeidl) | `from PyQt6.QtWidgets import QMessageBox` dentro de una rama hace el nombre local a toda la función. Arreglado en `64a7ab4` (dos líneas) |
| Byparr: "0 entries, 0 entries, 20 entries" para el historial de builds | `steamdb_scraper.py`: Byparr devuelve el HTML en cuanto pasa el Turnstile, antes de que SteamDB pinte la tabla por JS; el parser busca una tabla con cabecera "Patch Title", no la encuentra, devuelve 0 sin error ni reintento. Las respuestas de 0 son más pequeñas (41-45 KB) que la buena (49 KB). Guarda `cf_clearance` y UA en QSettings |
| "No hay botón para actualizar SLSsteam" (humplydinkle) | niwia: "im not touching that, run headcrab script again". SLSsteam se delega entero en Headcrab, como SLSDeck |
| Despinear no despinea (Shinji) | No borra la entrada de `ManifestIds`; niwia "havent worked on those stuff yet". Reinicio de Steam y "Content Still Encrypted" un minuto |
| Builds `…010` → `…012` (`e4405b9`, `64a7ab4`) | Restyle de diálogos y el fix de arriba. Nada en `plugin_manager`, nada en el lua, nada en el hook |

### 15.4 `beta` 2.7.0 — la rama por defecto, 1-oct [read: commits]

Once commits entre las 14:36 y las 16:01 (+0530):

- `f27a608` `yaml_config_manager.py` (+963/−270): valida el YAML antes de
  escribir; `_atomic_write` pasa a `r+` + `seek(0)` + `write` + `truncate` +
  `fsync`. Sigue sin ser temporal + rename, pero ya no hay ventana de fichero
  vacío. El `assfixer` con `"w"` no se ha mirado.
- `8b24001`: entran `plugin_manager.py` (el de R2), `spliced-tickets.lua` y
  `plugins_manifest.json` **en el repo** (con el sha del `download.lua` de
  R2); updater por canal (`canary|testing`, `beta|dev|rc`, resto stable) en
  `main_window.py` e `install.sh`; limpieza de `.acf`. No entra el modo
  at0m: en `beta`, `at0m.py` es un alias de `vapor.py` y no hay
  `native_steam/` ni pestaña AT0-M.
- `814c040`, `d17fc71`, `8089be4`: CI de AppImage con zsync
  (`gh-releases-zsync|niwia|ASSella|latest`), job de repo de Arch, releases en
  borrador, y `scripts/prerelease_check.py` (947 líneas, Qt offscreen, con el
  venv de su máquina hardcodeado: `/home/aiwin/.local/share/ACCELA/…`).
- `5e4d52d`: versión `2.7.0beta`.
- `e6c6639` y después **cuatro `fix(ci)` en 25 minutos**: faltaban
  `python3-venv` y `git` en el runner; las dependencias `steam`/`vdf`
  pineadas a commit pasan a `@master`/`@v4.0` (builds no reproducibles); un
  f-string multilínea que sólo compila en Python ≥ 3.12 (desarrolla en 3.12,
  el CI y el AppImage usan 3.11); falta `checkout` en el job de Arch.
- `sync_plugins_on_startup()` existe en `yaml_config_manager.py` y
  `sync_plugins_if_enabled()` en `plugin_manager.py`; **ningún llamador**.
  `fetch_plugins_manifest` sigue siendo caché primero. El bug de 15.2 llega a
  `beta` intacto.

Nombres: es un programa con cinco etiquetas. **AT0-M** (descarga por Steam
vía plugins; en `beta` sólo el alias), **Vapor** (el motor de manifests:
MRC de wudrm + CDN de Steam), **Wirecutter** (su Cloudflare Worker, proxy
de Hubcap cuando fallan directo, DoH y Tor), **Byparr** (ajeno: solver de
Cloudflare empaquetado para raspar SteamDB) y **Steamless** (ajeno).
"PLUT-O" no aparece en ninguna rama.

### 15.5 La conversación y el anuncio [read: Discord]

Mensaje directo a niwia el 29-sep con tres pistas: la línea del crash y el
tipo de `manifestId`; sacar la red del hook; probar en un entorno controlado
en vez de en los usuarios. Lo que hizo con cada una: la primera a medias
(15.1), la segunda no, la tercera no ("im just having ppl test this out").
Frases suyas que explican el resto: "i know shit about code", "tbh im not
biggest fan of steam client download … just having option look nice",
"ill check ur git, prob something i can inherit". Sobre lo último:
lumalinux y LumaDeck son AGPL; si algo nuestro aparece en ASSella se le dice
una vez, con el enlace a la licencia. Quedó un pacto útil: si alguien llega
con los dos instalados y un crash, avisarse antes de culpar al otro.

El 30-sep 19:07 lo anunció en el Discord de Steamidra ("testing the client
downloading for Linux … dont join if … not afraid of breaking steam client
often"). Ace: "you been having lots of fun with it don't you :p". La ruta
plugin sigue con bendición upstream; los crashes se los va a comer niwia, no
Ace.

### 15.6 Qué significa para nosotros [read]

- Nada que tocar en lumalinux/LumaDeck. §13.3 y §14.6 se mantienen.
- Vigilar la AGPL en `niwia/ASSella` (15.5).
- El concepto "los juegos bajan por el propio Steam" va a sonar en más sitios
  con la etiqueta AT0-M pegada, incluida gente de Deck a la que no le aplica.
  El 1-oct se abrió un Discord de LumaDeck por invitación (anuncios, guía,
  soporte, feedback; builds siempre en GitHub) y se mandaron por privado los
  cinco pasos a dos usuarios de Reddit que preguntaban. Candidato que sube de
  prioridad: **exportar diagnóstico** desde el plugin (versión, canal de
  Steam, `status.json`, hash del `steamclient.so`) para que un reporte sirva
  sin pedir cinco logs.

### 15.7 Las dos firmas del lua, medidas en CI [measured: probe-steam runs 13–15]

Las firmas de bytes que `download.lua` escanea con `memhlp.patternScan`
(l.67, `GetPackage`: `E8 ? ? ? ? 83 C4 ? 83 78 ? ? 0F 84`; l.68, `GMRC`:
`E8 ? ? ? ? 83 C4 ? 83 F8 ? 0F 85 ? ? ? ? 83 EC ? 6A ? FF 75 ? E8 ? ? ? ? 83 C4 ? 88 45`)
contadas en el `.text` de cada `steamclient.so` que Valve sirve hoy, con el
mismo alcance que SLSsteam (sólo `.text`; `tools/scan_signature.py` vía la
entrada `extra_signatures` del probe):

| Binario | Versión | `lua_GetPackage` | `lua_GMRC` |
|---|---|---|---|
| A, Deck stable `bc54101b` | 1788652215 | UNIQUE (1) @ `0x188ffa1` | UNIQUE (1) @ `0x1371121` |
| B, escritorio stable `237495b4` | 1788652215 | UNIQUE (1) @ `0x188ffa1` | UNIQUE (1) @ `0x1371121` |
| C, beta `a3661f5b` | 1790721607 | UNIQUE (1) @ `0x1a12a41` | UNIQUE (1) @ `0x14f7b01` |

Conclusión, contra lo que 15.5 daba por probable: **las dos anclas de sitio
de llamada sobreviven a la beta recompilada**. El día que C ascienda a stable
y Headcrab suba el pin, el lua carga. at0m no muere por sus patrones en este
ciclo; muere, si muere, por lo de 15.1 y 15.2. Dato lateral: A y B tienen el
mismo tamaño (49.829.436 bytes), el mismo rango de `.text` y las mismas
direcciones para todo lo que medimos; el código es idéntico y la diferencia
está en datos.

## §16 Delta — 2026-10-04 (`canary@64a7ab4` → `22f2759`; `beta@03b8933` → `3cf56bd`)

*Fuentes: `canary` (15 commits, 1-oct 21:22 → 3-oct 15:26 +0530, versión
`3.0.0testing031026001`), `beta` (45 commits, 1-oct 16:06 → 4-oct 22:13,
`2.7.1dev`), el `main` de SLSsteam (`9c829a7`, 1-oct, historial completo) y
LumaDeck en su rama actual. Cada afirmación lleva el commit o el
`fichero:línea` donde se comprobó. Lo que sólo sale del título o del mensaje
de un commit lo pone. **El `plugins_manifest.json` vivo de R2 no se pudo
leer** (el proxy de esta sesión bloquea `*.r2.dev`). Continúa §15.*

### 16.1 "All games online": `FakeAppIds: 0: 480` [read: ASSella, SLSsteam, LumaDeck]

**Qué escribe ASSella.** `a0382f0` (1-oct, sólo en `canary`: `git branch -r
--contains` da `canary`; en `beta` no hay `all_games_online` en el árbol)
añade el checkbox *"All games online (beta)"* en `settings_sls.py:285-304`.
Marcado, llama a `add_fake_app_id(c_path, "0", game_name="All Unowned Apps",
fake_appid="480")`, que escribe `  0: 480  # All Unowned Apps -> Spacewar`
bajo `FakeAppIds:` (`yaml_config_manager.py:2152`), siempre que la gestión
del config de SLS esté activa en ASSella. Desmarcado, borra `0: 480` y
cualquier `0:`.

**Qué hace SLSsteam con eso.** `src/feats/fakeappid.cpp:12-26`:

```cpp
if (fakeAppIds->contains(appId)) return fakeAppIds->at(appId);
else if (fakeAppIds->contains(0) && !g_pSteamEngine->getUser(0)->isSubscribed(appId))
    return fakeAppIds->at(0);
```

`CUser::isSubscribed` (`src/sdk/CUser.cpp:37`) llama a
`Hooks::CUser_CheckAppOwnership->tramp`, la función original de Steam; el
hook (`hooks.cpp:616`) es el que añade la propiedad falsa. O sea, la clave
`0` cubre todo juego que la cuenta no posee de verdad y no tenga entrada
propia en `FakeAppIds`.

**Por qué toca a los juegos Denuvo activados con otra cuenta.**
`Ticket::getCachedEncryptedTicket` (`src/feats/ticket.cpp:188-197`):

```cpp
const AppId_t fakeAppId = FakeAppIds::getFakeAppId(appId);
if (!(smartTickets & CConfig::k_ESmartTicketsDenuvo) && appId && fakeAppId && fakeAppId != appId)
    return nullptr;   // "Returning empty cached encrypted Ticket ... running as ..."
```

Usa `getFakeAppId`, así que el comodín cuenta. Sin el bit `0x2` (Denuvo) en
`SmartTickets`, un juego con FakeAppId no recibe su ticket cifrado cacheado,
y `hkClientUser_GetSteamId` (`hooks.cpp:1092-1160`) sin ticket devuelve el
SteamID real en vez del del propietario. El commit que introdujo la regla,
`293eb93` (26-jul), lo dice en su mensaje: *"Cons: Breaks Denuvo activations
on newer versions"*. Los juegos de `DenuvoGames` no son de la cuenta, así que
el comodín los alcanza. El default de SLSsteam es `SmartTickets: 1`
(`config.cpp:180`, `res/config.yaml:96`), sin el bit `0x2`. **Sin probar en
dispositivo**; es el camino del código más el mensaje de Ace.

**Qué hace LumaDeck hoy.**
- `fixes.enable_online` (`fixes.py:837`) se niega en juegos de
  `DenuvoGames` (`slssteam_ops.is_in_denuvo_games`), justo por esto. El
  comodín de ASSella se salta esa negativa.
- El estado Online de la ficha del juego es el marcador por juego de
  LumaDeck (`fixes.get_online_status`, `fixes.py:916-938`: `"enabled": marker
  is not None`). No mira `FakeAppIds`, así que con el comodín la ficha dice
  Online apagado aunque el juego corra como 480.
- **Ajustes sí lo enseña**: la lista de FakeAppIds pinta la clave `0` como
  "All unowned → 480 ✕" (`Settings.tsx:1040`, cadena `slssFakeAllUnowned` en
  `i18n.ts:275`, en el árbol desde `3ee28e3`, 13-sep) y se puede borrar desde
  ahí.
- Apagar Online en un juego borra sólo la entrada de ese appid, y sólo si la
  puso LumaDeck (`disable_online`, `fixes.py:882-915`). El comodín sigue.

### 16.2 SmartTickets y `spliced-tickets.lua` [read]

Las dos ramas tocan `SmartTickets`, por caminos distintos:

- **`beta`, desde `8b24001` (1-oct, en `tools_tab.py`; `d17fc71` lo mueve a
  la pestaña ASSella; los dos dentro del rango de §15.4, que no lo
  recogió)**: el botón del plugin (`assela_tab.py:588`,
  `handle_spliced_ticket_click`) llama a
  `ensure_smart_tickets_enabled(enable=True)` al activarlo (`:647`) y a
  `ensure_smart_tickets_enabled(enable=False)` al desactivarlo (`:613`).
  Desactivar escribe **`SmartTickets: 0x0`**, que apaga SmartTickets de
  SLSsteam para todo, también el `0x1` (SteamDRM) que trae por defecto, y
  borra `spliced-tickets.lua` de las carpetas de plugins. Ese borrado es por
  nombre: el `lumadeck-spliced-tickets.lua` no se toca.
- **`canary`, `1edf210` (3-oct)**: `deploy_plugin()` llama a
  `ensure_smart_tickets_enabled()` cuando el fichero es `spliced-tickets.lua`
  (`plugin_manager.py:252`, +5 líneas).

La función es la misma en las dos ramas (comparada con `diff`). Escribe
`0x1` (o `0x0`) si el valor actual no es literalmente ese, o lo añade al
final si no existe, salvo que la gestión del config esté apagada. Escribe
con `_atomic_write` (`r+` en sitio, `yaml_config_manager.py:156`). El
mensaje de `1edf210` es sólo el título y no da el motivo. El lua no contiene
la cadena `SmartTickets`.

**Efecto lateral, comprobado sobre su regex:** un `SmartTickets: 0x3`
(SteamDRM + Denuvo) se reescribe a `0x1`, y se pierde el bit Denuvo. Con eso
más el comodín de 16.1, se da exactamente la condición de
`ticket.cpp:193`. `SmartTickets: 1` también se reescribe, a `0x1`, que vale
lo mismo.

**El lua.** En `beta`, `src/res/plugins/spliced-tickets.lua` (añadido en
`8b24001`, 1-oct): 2.187 bytes, sha256 `62f377e3…`, el mismo que lista el
`plugins_manifest.json` del repo. Es **idéntico byte a byte** (`cmp`) al
`lumadeck-spliced-tickets.lua` de LumaDeck sin nuestras 19 líneas de
cabecera. En `canary` el fichero no está en el repo; se baja de R2 y no se
pudo leer (ver cabecera). SLSsteam ejecuta cada plugin con
`luaL_dofile(state, …)` sobre un solo `Lua::state` (`lua.cpp:273`, `:565`),
así que la guarda global `SplicedTickets.setup` hace que la
segunda copia salga sin colocar hook. Esto vale para el lua de `beta`; el
de R2 no está comprobado.

### 16.3 `canary`: el resto [read]

- **El pipe, solo** (`53c6038`, el commit de temas): se retira
  `dispatch_steam_url("steam://install/…")` de `native_steam_handoff.py`,
  `native_steam_download_task.py` y `slssteam_integration.py`, y queda sólo
  `install|appid|lib` por `/tmp/SLSsteam.API`. En el handoff, que falle el
  pipe ya no es error: *"config is written, Steam will pick it up on start"*
  (`native_steam_handoff.py:353`).
- **Conflicto ACCELA / AT0-M** (`d32ce26`): si un juego tiene a la vez el
  marcador de ACCELA y un registro de AT0-M, `game_manager.py` y
  `acf_scanner.py` comparan fechas (`.DepotDownloader/metadata.json` o mtime
  del marcador contra `updated_at` del registro) y gana el más reciente. Al
  registrar un juego en AT0-M (handoff y task) se borran `.ACCELA`,
  `.accela`, `.DepotDownloader`, `.depotdownloader`, el `{appid}.depot` y
  `game_update_status/{appid}`. Aparte, `a0382f0` añade en `task_manager.py`
  que al terminar una descarga por DepotDownloader un juego registrado en
  AT0-M pase a modo ACCELA.
- **"Smart Select"** (`1d36d98`): `steam_package_info.py` parsea el
  `packageinfo.vdf` local, saca los depots de los paquetes que contienen el
  appid y quita macOS, Android, extras de media y 32 bits si hay 64. Hace de
  preselección y además hay un botón "Smart Select (Beta)". Si no hay
  coincidencias, vuelve a la heurística anterior.
- **Depots sin clave** (`f673d57`): `[No Key]` en `depotselection.py` para
  `missing_key`. `process_zip_task.py` sólo añade depots de manifests sueltos
  si hay clave. `download_depots_task.py` aborta si DepotDownloader da
  AccessDenied o 0 bytes.
- **EOSProxy por defecto** (`a0382f0`): `task_manager._finalize_eosproxy`
  aplica el DLL al terminar si `EOSDetector` ve EOS sin proxy. El ajuste
  `enable_eosproxy_default` nace en `False`.
- **Logros** (`8e63e7d`; en `beta` es `b806334`, mismo diff): enteros fuera
  del rango con signo de 32 bits rompían `vdf.binary_dumps` en
  `shsah_reborn.py`. Ahora se enmascaran a 32 bits.
- `c153552`: tres `manifest_overrides` en `voices.json` (depots 3357651,
  3859920, 3859930). La build 22357085 de Pragmata sale sólo del título.
- `53c6038`: temas, `animated_progress_bar.py` (+1.001 líneas) y
  `src/res/halloween/`.
- CI: `477402e`…`22f2759` traen a `canary` workflows de AppImage/Arch y el
  updater (`9a31e81`). Los workflows no son copia de los de `beta`:
  `git diff beta canary -- .github/workflows` da 150+/82−.
- Fuera de 16.2, `plugin_manager.py` no cambia en `canary` en este delta.

### 16.4 `beta` 2.7.1dev [read: commits; código donde se indica]

- **CI.** De los 9 commits del 1 y 2-oct, siete son CI, empaquetado de Arch
  y README (`44746c5`, `3a0f8c1`, `68be86a` con la base de datos de pacman
  `assella.db` y la release rolling, `068dd21`, `e201cd9` con
  python-build-standalone embebido, `bcc1ed6`, `e76f598`). Los otros dos
  son los gemelos de `canary`: logros (`b806334`) y voces (`2855e2f`, mismo
  diff que `c153552`). La madrugada del 3 son **14 commits en 1 h 42
  min** (`1c0bafd` 03:04 → `a7d6124` 04:46): `4c6720c` sólo añade `set -x`
  y un `ls` para ver por qué falla; `80a31f4` sube el builder del AppImage
  de Python 3.11 a 3.13; `66db5fe` abre una rama `ci-test` que no publica.
- **El updater.** El mensaje de `9a31e81` (en `canary`) explica que no podía
  funcionar: las 66 releases son pre-release y `/releases/latest` las
  excluye (404 siempre), el tag se construía como `f"v{remote_version}"` y
  salía `vv2.7.0beta` cuando la versión ya traía `v`, y zsync estaba
  saltado desde 2.5.3 (~290 MB por actualización). En `beta` el arreglo es
  otro: `f0ab6f1` (*"full updater overhaul"*) lo reescribe en
  `main_window.py` y `c8f4164` lo mueve a `managers/app_update_manager.py`. Va por la lista
  `/releases`, filtrada por canal. **Pero el `vv` sigue en `beta`**:
  `app_update_manager.py:208` hace `tag = f"v{self.latest_remote_version}"`
  y `latest_remote_version` viene de `extract_semver(tag_name)`
  (`:153`, `:24-28`), que no quita la `v`. Ese `tag` se usa en el texto del
  diálogo y en logs (`:213`, `:232`, `:316`), y en la URL sólo en el camino
  de respaldo `/releases/tags/{tag}` (`:292`), cuando falla la lista.
- **Instalador** (`c9594ce`, sólo `beta`): descarga a un temporal
  (`mktemp`), comprueba contra el `.sha256` de la release y sólo entonces
  mueve y rota a `.bak`. Antes rotaba el binario bueno a `.bak` y escribía
  encima sin comprobar. Si no hay `.sha256`, avisa y sigue, salvo con
  `ASSELLA_REQUIRE_SHA256=1`.
- **UI, 3-oct tarde/noche** (por título y mensaje): ajustes con barra
  lateral (`f0ab6f1`), ventana compacta y fuente Digital-7 Mono para el log
  (`673bad9`), `MainWindow` partida en módulos (`c8f4164`).
- **Pausar y reanudar** (`6316b13`, 4-oct): estado en
  `{install}/.DepotDownloader/download_state.json`
  (`download_resume_manager.py`), diálogo al parar con "Resume Later",
  "Cancel & Delete" y "Continue Downloading", y los pausados fuera de la
  comprobación de updates. Esa tarde, `df739f2` (pausados en biblioteca y
  recientes, progreso al parar, recuperación del archivo) y `3cf56bd` (daba
  por completos depots a medias al reanudar).
- **Symlinks** (`db24c1e`): `manifest_resolver.restore_depot_symlinks`
  recorre el manifest (flag 512 o `linktarget`) y cambia por enlaces los
  ficheros de relleno que deja DepotDownloader.
- **Actualizar SLSsteam** (`f48b104`): abre una terminal con
  `curl -fsSL headcrab.pages.dev | bash` y cierra ASSella. Es el "run
  headcrab script again" de §15.3 metido en un botón.
- **DLC** (`618b6f0`, `b1ef461`): por título.

**Binario sin fuente (§6.3).** `db24c1e` y `6316b13` cambian
`src/deps/DepotDownloader.dll` (177.152 → 177.664 → 178.176 bytes). El
segundo dice *"Integrate fast buffered chunk hashing in DepotDownloaderMod"*.
En el árbol de `beta` no hay `.cs` ni `.csproj`; sólo el DLL, `deps.json` y
`runtimeconfig.json`.

### 16.5 Lo que no cambió, y una corrección a §15.2 [read]

- **La caché del manifest de plugins sigue congelada en las dos ramas.**
  `fetch_plugins_manifest(force_refresh=False)` devuelve la copia local si
  existe (`canary` `plugin_manager.py:76`, `beta` `:75`). Lo único que pasa
  `force_refresh` es `deploy_all_plugins(force_download=…)` (`canary :324`,
  `beta :331`), y todos sus llamadores pasan `False` o nada.
- **Corrección a §15.2 punto 2**, comprobada en `64a7ab4` y en la punta
  actual: en `canary` sí hay un despliegue automático más, **al arrancar**.
  `main.py:267` y `main_window.py:1402` llaman a `sync_plugins_on_startup()`
  → `sync_plugins_if_enabled()` → `deploy_all_plugins(force_download=False)`,
  si `Plugins` está activo. Usa el manifest cacheado, así que la conclusión
  de §15.2 no cambia: un lua nuevo en R2 no llega a quien ya tenía la
  caché. Los llamadores de `deploy_all_plugins` en `native_steam_handoff.py:136`
  y `native_steam_download_task.py:536` siguen dentro de
  `deploy_bundled_plugins()` y `_deploy_plugin()`, que nadie llama.
- En `beta`, `sync_plugins_on_startup()` sigue sin llamador. El único
  despliegue es el botón de spliced-tickets (`assela_tab.py:651`).
- El `plugins_manifest.json` **del repo** (`beta`) sigue con `updated_at
  2026-09-30T11:14:22Z` y `download.lua` `84d6c23f`. El de R2 no se pudo
  leer.
- `native_steam/` sigue sólo en `canary`. En `beta`, `src/core/` tiene
  `at0m.py` y `vapor.py`, pero no `native_steam/`.

### 16.6 Qué significa para nosotros

- lumalinux: nada.
- LumaDeck, candidato de **baja prioridad**: Ajustes ya enseña la clave
  `0`, pero la ficha del juego no. Si `FakeAppIds` tiene `0`, avisar en la
  ficha de que el juego corre como 480 sin importar el toggle Online, y en
  los juegos de `DenuvoGames` de que eso, con `SmartTickets` sin `0x2`, deja
  sin ticket cifrado la activación (16.1). Disparador: el toggle llega a
  `beta`, o alguien reporta que un Denuvo deja de activar con ASSella
  instalado. Encaja con el **exportar diagnóstico** de §15.6, que debería
  volcar `FakeAppIds` y `SmartTickets`.
- La coexistencia de los dos `spliced-tickets` queda cerrada para el lua de
  `beta`; la copia de R2 que usa `canary` está sin comprobar (16.2).

El siguiente barrido arranca en `beta@3cf56bd` y `canary@22f2759`.

## §17 — 2026-10-05: `canary` en el codespace, medido — instalar, desinstalar y reinstalar

*Codespace SteamOS (`.devcontainer/steamos/`, usuario `deck`), Steam en Game
Mode sobre `:1`/noVNC, Headcrab → SLSsteam `main@9c829a7` (release
`20261001163836`), **sin lumalinux**. ASSella `canary@22f2759`
(`3.0.0testing031026001`) desde el código fuente con Python 3.13 (`uv venv`),
`DISPLAY=:1`, log en `/tmp/assella.log`. Las pruebas las hizo el autor en la
interfaz; los logs (`/tmp/assella.log`, `~/.SLSsteam.log`, `content_log.txt`,
`config.yaml`, `.acf`) se leyeron después de cada paso. Etiquetas: [measured]
sale de esos logs; [read] del código; [inferred] no medido.*

### 17.1 Arrancar ASSella `canary` desde el código [measured]

- Con Python 3.11 no arranca: `SyntaxError` en `task_manager.py:3214`
  (f-string de varias líneas, Python ≥ 3.12). Con 3.13, `requirements.txt` se
  instala de una vez; los tres pasos de §13.1 ya no hacen falta.
- PyQt6 necesita `libEGL` (en el contenedor viene con `mesa`) y, para la
  plataforma `xcb`, `xcb-util-cursor`.
- Al arrancar despliega `download.lua` (sha256 `84d6c23f…`, el de R2 de §15.1)
  y `spliced-tickets.lua` en `plugins/`, y pone `API: yes` y `Plugins: yes`.
  Se quedan cargados en Steam desde ese momento, haya descarga o no.

### 17.2 Brotato (1942280) por AT0-M nativo — funciona [measured]

Ruta `fetchmanifest` → "Native Steam" → "Start steam download", que es el
**handoff** con `auto_install=True` (`fetchmanifest.py:1125-1185`), no la tarea
rota del `NameError` (§16 y más abajo).

| Hora | Qué | Fuente |
|---|---|---|
| 08:24:42 | Baja el zip de Hubcap (`force_update=True`): 4 depots con clave y gid | assella.log |
| 08:24:43 | Parchea `AdditionalApps`, `AdditionalDepots` y `DecryptionKeys` en una escritura; el AppID queda fuera de las dos últimas | config.yaml |
| 08:24:44 | `AppLicensesChanged callback invoked for 1942280` | ambos |
| 08:24:46 | Copia los 4 `.manifest` del zip a `depotcache/` | assella.log |
| 08:24:46 | `install\|1942280\|0` por el pipe → SLSsteam `Installed 1942280 to 0` | SLSsteam |
| 08:24:47-57 | Steam baja 272 MB, `finished update … (BuildID 23429717)`, `Fully Installed` | content_log |
| 08:24:51 | El `.acf` lo escribe Steam (`Steam created ACF manifest … attempt 1`); el ACF de respaldo de 0 bytes (§16) no llega a saltar | assella.log |

Steam no pide código para el juego porque los manifests ya están en
`depotcache/`. El depot de shaders (`1942280`, manifest
`839658903313537823`) sí baja del CDN con código en la URL (`200 OK`): ese
código lo da Valve, no wudrm (Brotato lleva Workshop;
`shader_depot_hook.cpp:36-40`), y el log de SLSsteam no tiene ni una línea de
`MRC`. Tras reiniciar Steam sigue instalado, y el juego arranca.

Detalles del mismo paso: el respaldo `config.yaml.native_steam_backup` se crea
y no se borra nunca (nadie lo restaura: sólo la tarea vigilada restaura la
copia que ella misma crea); `DisableUpdates: yes` (default de SLSsteam,
`config.cpp:189`; ASSella no lo cambia, LumaDeck sí, `installer.py:217`).

### 17.3 Desinstalar [measured + read]

- **Desde ASSella no se puede**: en juegos AT0-M, "Uninstall Game", "Verify
  Game Files" y "Reset Depot Selection" están deshabilitados
  (`actions.py:507-543`).
- **Desde Steam**: el `.acf` desaparece. ASSella no registra nada en su log, y
  `config.yaml` conserva `AdditionalApps`, `AdditionalDepots`,
  `DecryptionKeys` y `AppTokens` de Brotato: el juego queda desbloqueado y "no
  instalado" en la biblioteca.
- **Los 4 manifests ya no están en `depotcache/`**. Es lo que LumaDeck tiene
  documentado: Steam purga `depotcache/` al desinstalar (`pins.py:46`).

### 17.4 Reinstalar desde Steam — "Unknown error" [measured]

1. Licencia y claves bien: `AuthenticateDepotID (1942282) - Success!`,
   `(2868390) - Success!`.
2. Sin manifest local, Steam pide el código. `download.lua` va a su único
   proveedor, cinco veces por manifest: `MRC server http://gmrc.wudrm.com/manifest/
   failed` → `All MRC servers failed` → `Failed to get MRC for 2868390` / `1942282`.
3. El CDN responde 401 en todos los servidores →
   `update canceled : Failed downloading 2 manifests (Unspecified Error)` →
   "Unknown error", `Update Paused`. Steam no lo reintenta.

**Por qué falla wudrm.** `download.lua` l.265 llama
`curl.downloadString(url, 2)` sin cabeceras, y SLSsteam ejecuta
`/usr/bin/curl` tal cual (`curl.cpp:17-41`). Desde el codespace, misma IP y
misma URL (`/manifest/4872816150142449642`):

| User-Agent | Respuesta |
|---|---|
| el de `curl` | **403**, `Server: cloudflare`, `Cf-Mitigated: challenge`, cuerpo "Just a moment" |
| navegador (Chrome 129) | **200** |

Es el filtro por User-Agent que `gmrc_store.hpp:149-150` ya documenta
("wudrm … Serves a Cloudflare JS challenge to the `curl` User-Agent, not to
ours"), y que niwia esquivó con `curl-impersonate` entre el 26 y el 28-sep
(§11) para quitarlo después (`6677a05`, §13.1). §13.1 decía que el 29-sep
wudrm retaba a la IP del codespace "con cualquier UA"; hoy, con un UA de
navegador, no se reproduce.

[inferred] Afecta a cualquier usuario de `download.lua` `84d6c23f`, no sólo al
codespace: lo que cambia la respuesta es el UA, no la IP. Falta medirlo desde
una IP doméstica. En la práctica sólo aparece cuando Steam necesita un código:
reinstalar desde Steam, actualizar (si alguien quita `DisableUpdates`),
cambiar de build o rama, y shaders sin código de Valve. La primera instalación
por ASSella no lo sufre porque lleva los manifests.

### 17.5 Balatro (2379780) por el descargador clásico [measured]

Elegido "ASSella (built-in)" en el diálogo: DepotDownloader baja `2379781`
con las claves en `/tmp/mistwalker_keys.vdf`, y luego el registro "sin ACF
propio" (`experimental_acf_independent`): `Wrote AppID 2379780 to SLS config`
→ `install|2379780|0` → Steam crea el `.acf`.

- Al config sólo va el AppID: ni `2379781` en `AdditionalDepots` ni su clave.
- Steam: `has no changes, 0 active: 0 target` (no ve ningún depot del juego).
- Shaders: `scheduler finished : … (result Missing decryption key)`,
  `Update delayed for 300 secs`. "Content still encrypted" en la interfaz
  durante ese rato [inferred: el texto de la UI no está en el log, la hora
  coincide].
- A los 5 min: `finished update, 0 mounted depots (BuildID 17459173)`,
  `Fully Installed`. El `.acf` queda con `StateFlags 4`, `SizeOnDisk 0` e
  **`InstalledDepots` vacío**.
- El juego arranca y se juega (los ficheros los puso DepotDownloader). Steam no
  sabe qué hay instalado: no podrá verificarlo ni actualizarlo.

### 17.6 Into the Breach (590380) por AT0-M nativo — tira la clave de shaders [measured]

Handoff normal: 3 depots con clave, 3 manifests a `depotcache/`,
`install|590380|0`, `.acf` de Steam.

- `09:22:29 update started : download 0/436713184` →
  `09:22:33 update canceled : Shader Priority (Suspended)` → shaders →
  `09:22:36 … (result Missing decryption key)`, `Update delayed for 300 secs`.
  SLSsteam: `Missing decryptionkey for 590380`. "Content still encrypted" en la
  interfaz.
- El `.lua` del zip **trae la clave de shaders**:
  `addappid(590380, 1, "198dd2b7…")` (l.11). En `DecryptionKeys` no está:
  ASSella excluye el AppID a propósito ("AppIDs MUST NEVER be added",
  `native_steam_handoff.py:289-291`, `native_steam_download_task.py:761-763`).
  El aviso de Ace que recoge §14.4 era sobre AppIDs en `AdditionalDepots`, no
  en `DecryptionKeys`; el depot de shaders tiene el id del AppID y su clave es
  legítima (RESEARCH §13.8).
- A los 5 min: `finished update, 1 mounted depots (BuildID 21601364) :
  590383`, `Fully Installed`, sin shaders.

[inferred] Con `590380` en `DecryptionKeys` habría instalado a la primera y con
shaders, como Brotato. No medido. Pendiente: si Steam sigue reintentando los
shaders cada ~5 min con el juego ya instalado (RESEARCH §13.8 lo vio con
CrossCode y Blasphemous).

### 17.7 Otros datos de la sesión [measured]

- Health "Version: Unknown": `check_slssteam_binary_is_latest`
  (`slssteam_integration.py:265`) devuelve `error` cuando encuentra el `.so`
  pero falla la consulta a `api.github.com`. Los fallos no se guardan en caché.
  Un `curl` a esa API dio 200 en el mismo rato; la causa concreta del fallo
  quedó sin ver.
- ASSella mantiene su propio cliente de Steam en Python
  (`SteamClientWorker … logged on anonymously`) para consultar PICS.
- `appmanifest_1628350.acf` (Steam Linux Runtime, por lanzar un juego) y el
  aviso de `download.lua` `Missing decryptionkey for 1628351`: sin efecto.

### 17.8 LumaDeck en el mismo caso: qué necesita Steam y qué deja LumaDeck [read + measured en septiembre]

Para instalar o reinstalar, Steam necesita (RESEARCH §1-§6):

| Necesita | LumaDeck + lumalinux | ¿Persiste tras desinstalar desde Steam? |
|---|---|---|
| Licencia | SLSsteam `AdditionalApps` | Sí (nadie lo quita) |
| Saber qué depots hay | finder del paquete 0 a partir de `keys.txt` | Sí |
| Clave de cada depot (y de shaders si existe) | hook DepotKey sirviendo `keys.txt` | Sí |
| Manifest de cada depot | **código** por el hook GMRC: 20770407 → manifestdex → wudrm → steamrun, UA propio, validado contra el CDN (`gmrc_store.hpp:154-158`); **o** el `.manifest` en `depotcache/` | El de `depotcache/` no (Steam lo purga). LumaDeck guarda copia en `~/.local/share/lumadeck/manifests/<appid>/` y la repone |

Cómo cubre LumaDeck la reinstalación (`pins.py:1-64`):

- **Proveedor vivo** (`gmrc.json` "up"): Steam pide el código y lumalinux lo
  sirve, como para un juego comprado.
- **Ningún proveedor** ("down"): el pase local (cada 60 s, `LOCAL_INTERVAL`)
  fija cada juego a su build instalada en `ManifestIds` y **repone desde el
  archivo los manifests que falten en `depotcache/`** ("Steam purges depotcache
  on uninstall, after commits and on re-plans", `pins.py:45-46`). Steam
  planifica contra esos gids y no pide código.

Medido en septiembre en este mismo codespace (`docs/update-testing.md`):

- **T1** (15-sep): instalar desde Steam sin ningún manifest local → código por
  manifestdex, `CDN accepted`, instalado.
- **T2** (16-sep): **desinstalar e instalar Lethal Company desde Steam** →
  contenido y shaders con códigos de 20770407, `shadercache/1966720/` lleno,
  sin aviso.
- **V3** (16-sep): proveedores caídos a propósito, **Balatro desinstalado e
  instalado** → `gmrc.json` "down", juego fijado a su build, manifest repuesto
  desde el archivo, **instalado sin ningún código** en el reintento de 30 s de
  Steam.

Es lo que el autor recuerda: con LumaDeck, desinstalar y reinstalar desde Steam
funciona, con o sin proveedores. Las medidas son de hace tres semanas y no se
han repetido hoy. Los dos huecos de ASSella de 17.4 (un proveedor, UA de
`curl`, ningún archivo de manifests) y de 17.6 (clave de shaders tirada) no
existen en lo nuestro: `ShaderDepot` deja correr los shaders si hay clave y
proveedor, y los salta limpio si no (`shader_depot_hook.cpp:64-89`).

### 17.9 Balance

| # | Qué | Fuente | Para nosotros |
|---|---|---|---|
| 1 | AT0-M nativo instala bien la primera vez (manifests copiados) | measured | Mismo modelo que LumaDeck 0.8 |
| 2 | Reinstalar desde Steam → "Unknown error" (wudrm reta al UA `curl`; un solo proveedor; sin archivo de manifests) | measured | Cubierto: T1/T2/V3 |
| 3 | Clave de shaders descartada → "Content still encrypted", 5 min, sin shaders | measured | Cubierto: DepotKey + `ShaderDepot` |
| 4 | Clásico + "ACF independiente": `InstalledDepots` vacío | measured | No aplica |
| 5 | Sin desinstalar desde la app; Steam desinstala y el config queda | measured | — |
| 6 | `DisableUpdates: yes` por defecto | measured | LumaDeck lo pone a `no` |
| 7 | Plugins residentes desde que se abre ASSella | measured | Choque de §12 con quien tenga los dos |
| 8 | `NameError` de la cola (`native_steam_download_task.py:182`) | read + pyflakes | Sin ejecutar: zip por línea de comandos + "Native Steam" + "Start steam download" |

Pendiente: (a) el `curl` a wudrm desde una IP doméstica; (b) reintentos de
shaders de Into the Breach ya instalado; (c) el `NameError` de 8; (d) repetir
T2/V3 con la versión actual de LumaDeck para que 17.8 quede medido hoy.

El siguiente barrido de código arranca en `beta@3cf56bd` y `canary@22f2759`.
