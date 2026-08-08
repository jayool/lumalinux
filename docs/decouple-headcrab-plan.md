# Plan — desacoplar LumaDeck de headcrab (inyección por wrapper)

*Fecha: 2026-08-08. Secuela de `docs/slsteam-moon-findings.md` (M7, M3) y
`docs/method.md` (las 6 gates). Estado: **plan, nada implementado**. Cruza dos
repos: `jayool/lumalinux` (el instalador/wrapper) y `jayool/LumaDeck` (el
backend Decky). Rama de trabajo: `claude/lumalinux-lumadeck-context-qv82xl`.*

---

## 0. Objetivo en una frase

Dejar de **ejecutar** `headcrab.sh` para instalar / quick-install / reparar.
Mover la inyección de las 3 `.so` de un `steam.sh` frágil (SHA-verificado,
re-extraído en cada update de Steam) a un **wrapper propio** alcanzado por
parcheo de `.desktop` + wrap del launcher del sistema (el modelo de moon,
probado en producción). Reducir headcrab a una dependencia de **datos** (el pin
de compatibilidad + `sources.txt`) usada solo en la ruta rara de downgrade de
rescate. Borrar la maquinaria de freeze/pin/handoff.

---

## 1. Diagnóstico (estado actual, verificado en código)

**Toda operación mutante funnelea por headcrab.** Puntos de invocación reales
(`LumaDeck/backend/installer.py` salvo indicado):

| Operación | Entrypoint | Qué corre |
|---|---|---|
| Install completo | `run_full_install` → `install_dependencies` | headcrab parcheado |
| Install core-only | `apply_component("core")` → `install_dependencies` | headcrab parcheado |
| Quick install | `quick_install()` → `install_dependencies` | headcrab parcheado |
| Reparar componente | `apply_component(op="repair")` → `install_dependencies` | headcrab parcheado |
| Reparar SLSsteam | `slssteam_ops.repair_slssteam_headcrab()` | headcrab |
| Downgrade / break-recovery | `desktop_handoff.run_desktop_handoff_quick_install` | `curl -fsSL headcrab.pages.dev \| bash` (raw, sin parchear) |
| Catch-up tras break | re-corre `install_dependencies` | headcrab parcheado |

**headcrab es un monolito 4-en-1** que solo se puede llamar todo-o-nada:

1. **descargar** `SLSsteam.so` + `library-inject.so` + `cloud_redirect.so`
2. **inyectar** en `steam.sh` (LD_AUDIT / LD_PRELOAD)
3. **downgradear** Steam al build fijado
4. **escribir el freeze** (`steam.cfg`, `BootStrapperInhibitAll`)

`_HEADCRAB_PATCHES` + el flag `gamemode` (`installer.py:121-253`) son el aparato
de **neutralizar-por-llamada** las partes (2/3/4) que no queremos de un script
que corremos entero cada vez. La amplitud de invocación es el síntoma.

**La inyección vive en `steam.sh` y es frágil.** `lumalinux` es `LD_PRELOAD`-eado
por `steam.sh` (`lumalinux/src/main.cpp:58`); headcrab hornea el bloque
`LD_PRELOAD=liblumalinux.so`. Dos causas lo borran (finding M7):
(a) headcrab regenera `steam.sh`; (b) **Steam re-extrae `steam.sh` cuando su
tamaño difiere del manifest** → cualquier update de Steam borra la inyección.
LumaDeck ya modela el síntoma (`backend/paths.py` estado `injection_missing`).

---

## 2. Cómo inyecta moon (el modelo que copiamos)

Fichero: **`setup.sh`** de `swwayps/slsteam-moon` (verificado, 1274 líneas).
No es headcrab; es su instalador, que **escribe un wrapper** en
`~/.local/share/SLSsteam/path/steam`. Ese wrapper es lo que inyecta:

```sh
# SLSsteam por LD_AUDIT (library-inject.so PRIMERO — redirige libcurl):
AUDIT="$SLSDIR/library-inject.so:$SLSDIR/SLSsteam.so"

# CloudRedirect por LD_PRELOAD, NUNCA en LD_AUDIT (si va en LD_AUDIT
# corrompe el heap: "realloc(): invalid pointer" en init):
CR_SO="$HOME/.local/share/CloudRedirect/cloud_redirect.so"
[ -f "$CR_SO" ] && export LD_PRELOAD="$CR_SO${LD_PRELOAD:+:$LD_PRELOAD}"

export LD_AUDIT="$AUDIT"          # (o "$AUDIT:$auditor_heredado")
exec "$STEAM_BIN" "$@"
```

**Cobertura** (cómo se llega al wrapper — cinturón y tirantes):
1. Parchea `Exec=` de todos los `*steam*.desktop` (usuario + sistema + autostart)
   → wrapper, con un tag para poder deshacer.
2. **Envuelve el launcher del sistema** (`/usr/bin/steam` en Arch/Fedora,
   `/usr/games/steam` en Debian). Clave: *"propiedad del gestor de paquetes y
   SIN integrity check (a diferencia de `steam.sh`, SHA-verificado), así que
   envolverlos es seguro y **sobrevive a los self-updates de Steam**"* (setup.sh
   L749-750).
3. Dir `path/` con el wrapper llamado `steam` (estilo PATH-shim).
4. Re-afirma cobertura en cada arranque (`slsteam-desktop-guardian.path` systemd
   o `ensure-desktop-coverage.sh`).
5. **No toca `steam.sh`** (moon lo intentó y revirtió — M7).

Moon inyecta 2 `.so` (SLSsteam + CR). Nosotros necesitamos **3**: añadimos
`lumalinux.so` a la lista `LD_PRELOAD` (es otra preload normal). El conjunto de
env-vars resultante **es idéntico al que headcrab hornea hoy en `steam.sh`**; solo
cambia el *sitio*. Y ya corremos las 3 juntas en producción → sin riesgo nuevo de
coexistencia.

---

## 3. Arquitectura destino

```
lumalinux/install.sh  (equivalente a moon setup.sh — self-contained)
   ├─ fetch:   SLSsteam.so + library-inject.so (AceSLS/SLSsteam releases)
   │           cloud_redirect.so (release CR)
   │           lumalinux.so (jayool/lumalinux releases — ya lo hace)
   ├─ wrapper: ~/.local/share/SLSsteam/path/steam  (inyecta las 3 .so)
   ├─ cobertura: parchea .desktop Exec + wrap launcher sistema + autostart
   ├─ guardian: re-afirma cobertura tras updates de Steam
   └─ uninstall: restaura .desktop/launcher, borra wrapper

LumaDeck/backend/installer.py
   └─ install / quick / repair  →  corre install.sh  (CERO headcrab)

LumaDeck/backend/downgrade.py  (NUEVO, opt-in, break-glass)
   └─ reproduce el downgrade con piezas nuestras, leyendo datos de Deadboy
```

---

## 4. Workstreams

### WS1 — lumalinux: instalador por wrapper self-contained *(núcleo)*

Aquí se muda la inyección. **Decisión de implementación:** en vez de reescribir
`install.sh` (que sigue en uso, depende de headcrab), se crea un script **nuevo
`setup.sh`** self-contained que coexiste con `install.sh`. Así WS1 es aditivo y
testeable sin romper el flujo actual; WS2 apuntará LumaDeck a `setup.sh` y WS3
retirará `install.sh` cuando esté probado.

**Estado:**
- **WS1.1 — HECHO (pendiente de prueba en Deck):** `setup.sh` — fetch de las 4
  `.so` (SLSsteam + library-inject + CloudRedirect + lumalinux + **netsock**) +
  instalación de la **app Flatpak de CloudRedirect** (best-effort, saltable con
  `LUMA_SKIP_CR_APP=1`) + escritura del wrapper + cobertura **Desktop**
  (`.desktop` Exec de `steam`/`steam-jupiter`/`bazzite-steam` + autostart
  override) + cobertura **Game Mode** (PATH drop-in en `.bashrc`/`.zshrc`/
  `.profile`, el modelo de moon) + uninstall (revierte todo, incl. el PATH).
  Sintaxis verificada (`bash -n`, `sh -n` del wrapper); lógica de reescritura de
  `.desktop` probada (reescribe `steam-jupiter`, ignora Exec que solo menciona
  "steam" en args). **No probado**: fetch/extract 7z, install flatpak, y la
  eficacia del PATH drop-in sobre el arranque de Game Mode (requiere Deck — WS5).
- **`7z` es dependencia asumida, no un problema.** Es el único formato `.7z` del
  flujo (el `SLSsteam-Any.7z`); los fixes de luatools son `.zip` (Python `zipfile`,
  `fixes.py`). headcrab ya asume `7z`, así que `setup.sh` lo exige y aborta claro si
  falta. (Opcional a futuro: extraer con `py7zr` desde LumaDeck para el path del
  plugin; no es necesario.)
- **Hueco que queda (respecto a headcrab):** rutas de **Steam-como-Flatpak**
  (`setup.sh` es solo Steam nativo; la Deck es nativo, así que no urge).
- **WS1.2 — HECHO (pendiente de prueba en Deck):** al estilo moon.
  - **Fail-safe anti-brick** dentro del wrapper: cuenta boots que crashean al
    arranque (dump `crash_*.dmp` en los primeros 180s), latchea arranque
    **vanilla** al 3er fallo — o al **1º si el `steamclient.so` cambió** desde el
    último boot limpio (update = causa casi segura). Auto-clear cuando cambia el
    payload (updateas la stack). Probado: inyecta normal / va vanilla latcheado.
  - **Guardian systemd `--user`**: `.path` (vigila los dirs de `.desktop`) +
    `.timer` (reconcilia cada 5min) + `.service` (oneshot → `ensure-desktop-
    coverage.sh --guardian`). El wrapper además lo kickea en cada launch. Cobertura
    extraída a `ensure-desktop-coverage.sh` (compartida install/guardian). Probado:
    genera y habilita los units; apply idempotente; uninstall restaura.
  - Wrap del launcher del sistema (`/usr/bin/steam`): **no** — en SteamOS/Deck
    (inmutable) moon lo salta; Game Mode va por el PATH drop-in. Solo haría falta
    en Linux escritorio mutable (baja prioridad).
- **La única incógnita real a validar en Deck (WS5):** que la sesión gamescope de
  Game Mode herede el PATH del rc y resuelva `steam` por PATH (no por ruta
  absoluta). Es el "open borrow" que marcaban nuestros docs; moon se apoya en esto.

**Fuentes de descarga confirmadas** (verificadas 2026-08-08):
- SLSsteam + `library-inject.so`: `SLSsteam-Any.7z` de
  `AceSLS/SLSsteam/releases/latest/download/` → `bin/{SLSsteam,library-inject}.so`
  + `res/config.yaml`. **Requiere `7z`.**
- CloudRedirect: `Selectively11/h3adcr-b/releases/download/linux-test/cloud_redirect.so`
- lumalinux: `jayool/lumalinux/releases/latest/download/liblumalinux.so`
- Rutas: SLSsteam `~/.local/share/SLSsteam/`, config `~/.config/SLSsteam/config.yaml`,
  CR `~/.local/share/CloudRedirect/`, wrapper `~/.local/share/SLSsteam/path/steam`.

**Caveat de coexistencia (honesto):** en una instalación ya parcheada por headcrab,
el `steam.sh` de headcrab reexporta su propio `LD_AUDIT` y gana sobre el del
wrapper — degrada al flujo actual (inofensivo). El comportamiento limpio del
wrapper se valida en instalación vanilla/secundaria (WS5).

---

#### Diseño de referencia (mantengo la descripción original)

Tareas:
1. **Fetch** de las `.so` a rutas canónicas:
   - `SLSsteam.so` + `library-inject.so` → `~/.local/share/SLSsteam/`
     (de `github.com/AceSLS/SLSsteam/releases/latest` — la fuente oficial que
     headcrab ya usa).
   - `cloud_redirect.so` → `~/.local/share/CloudRedirect/`.
   - `lumalinux.so` → su dir (ya lo hace install.sh).
2. **Escribir el wrapper** `~/.local/share/SLSsteam/path/steam`:
   - `slsm_strip_own_auditors` (quitar nuestros auditores heredados del LD_AUDIT).
   - resolver `STEAM_BIN` real (saltando el propio wrapper).
   - `LD_AUDIT="library-inject.so:SLSsteam.so"`.
   - `LD_PRELOAD="cloud_redirect.so:lumalinux.so:$LD_PRELOAD"`.
   - preload de `libextest.so` solo en Wayland (Steam Input).
   - `exec "$STEAM_BIN" "$@"`.
3. **Cobertura**: parchear `Exec=` de cada `*steam*.desktop` (usuario + sistema
   donde sea escribible + override de autostart), taggeado para undo; envolver el
   launcher del sistema (`/usr/bin/steam` o `/usr/games/steam`). **No `steam.sh`.**
4. **Re-afirmación**: guardian (systemd `--user` path/service) o el propio wrapper
   re-asegura cobertura en cada launch, para que un update de Steam que regenere
   `.desktop` quede re-cubierto.
5. **Uninstall/restore**: deshacer parches `.desktop`, restaurar launcher, borrar
   wrapper.
6. *(Opcional, portar moon M3)* **Fail-safe anti-brick**: registrar `size:mtime`
   de `steamclient.so`; si hay crash de arranque **y** el cliente cambió respecto
   al último booteo limpio → `exec` vanilla (sin inyección). Este es el cinturón
   que **sustituye al freeze**: un update malo no brickea, solo corre sin unlock.

Notas de coexistencia: 3 `.so` (moon carga 1). Ya lo hacemos en `steam.sh` hoy
→ probado. `library-inject.so` **primero** en LD_AUDIT. CR + lumalinux en
LD_PRELOAD, **jamás en LD_AUDIT** (heap corruption, doc moon).

### WS2 — LumaDeck: recablear operaciones fuera de headcrab

1. `installer.py`: `install_dependencies()` deja de "descargar+parchear+correr
   headcrab.sh" y pasa a "correr `lumalinux/install.sh` (self-contained)".
   **Mantener el mismo contrato `INSTALL_STATE`/progress** para no tocar la UI.
2. `quick_install` / `apply_component(op="repair")` / catch-up: todos a `install.sh`.
   Reparar = re-correr `install.sh` (idempotente: re-fetch `.so` + re-afirmar
   cobertura). Fin de `_HEADCRAB_PATCHES`.
3. Borrar `_HEADCRAB_PATCHES`, `_patch_headcrab_script`, `_HEADCRAB_RAW_URL`,
   `_SESSION_TRACKER_RESET` de la ruta feliz.
4. `check_dependencies`: la semántica de `injection_missing` cambia a "cobertura
   del wrapper perdida" en vez de "steam.sh perdió su bloque".

### WS3 — LumaDeck: borrar la maquinaria de freeze/pin/handoff

- **Borrar `steam_freeze.py`** (152 L) — sin freeze en el modelo de update libre.
- **Borrar/encoger `desktop_handoff.py`** (363 L) — el enrutado del downgrade se
  va; conservar solo el slice de hand-off a Desktop para el sign-in del proveedor
  de CloudRedirect (que es GUI-only) si sigue haciendo falta.
- **Encoger `headcrab_compat.py`** (307 L) → un `downgrade_pin.py` mínimo que solo
  lee el pin + la lista de sources como **datos** para WS4; quitar el gating de
  "empuja al usuario a Desktop para downgrade" en install/repair (ya no downgradean).
- `update_checks.py` / `components.py`: quitar la lógica "¿este update de Steam
  rompe el pin?"; conservar los update-checks de `.so`/lumalinux.
- Quitar el patch `suppress-freeze` (ya no hay headcrab que parchear).

### WS4 — LumaDeck: downgrade como escape-hatch fino (`downgrade.py`, NUEVO)

Reproducir el mecanismo con piezas nuestras, leyendo datos de Deadboy. El
mecanismo NO es magia de headcrab (verificado leyendo `headcrab.sh`):

1. Leer el pin `HeadcrabCompatibleClientVer` + fetch `sources.txt` de
   `Deadboy666/h3adcr-b-modul3s` (**solo datos**).
2. Descargar los chunks del build desde la **CDN oficial de Valve**
   (`client-update.fastly.steamstatic.com`) con `curl`/`aria2c` a `package/`
   (reemplaza el binario `dlm`).
3. Servirlos en `localhost:1666` con un servidor estático que respete el patrón de
   URLs que Steam pide (reemplaza el binario `dgsc`).
4. Correr el downgrade con el **flag propio de Steam**:
   `steam -forcesteamupdate -forcepackagedownload -overridepackageurl http://localhost:1666/ -exitsteam`.
5. *(Opcional)* escribir `steam.cfg` para fijar el build — **el único sitio donde
   un freeze es legítimo ahora**.
6. **Gate**: ofrecerlo solo cuando la inyección esté rota en un build no
   reconocido **y** lumalinux/SLSsteam no estén listos (cruzar con `updates.yaml`).
   Break-glass, opt-in, en Desktop.

**A verificar antes de comprometer WS4**: el patrón exacto de peticiones que Steam
manda a `-overridepackageurl` (para confirmar que un `http.server` estático basta,
o si `dgsc` hace algo especial). Hasta verificarlo, WS4 puede seguir usando los
binarios `dlm`/`dgsc` de Deadboy como fallback mientras nosotros orquestamos.

### WS5 — Pruebas en la Deck (seguridad)

- Probar el install por wrapper en una instalación **secundaria/desechable** antes
  de tocar la instalación viva del usuario.
- Verificar: cargan las 3 `.so` (SLSsteam por LD_AUDIT; CR + lumalinux mapeadas en
  el proceso); Game Mode + Desktop + autostart pasan todos por el wrapper; un update
  de Steam simulado (regenerar `.desktop`) → el guardian re-cubre.
- Verificar que un juego no-owned sigue descargando (las 6 gates siguen abriendo).
- Verificar que uninstall restaura limpio.
- Solo tras verde en secundaria: migrar la instalación viva.

---

## 5. Secuenciación (orden siempre-seguro)

1. **WS1** (wrapper en `lumalinux/install.sh`) — aditivo; no rompe el install por
   headcrab actual, pueden coexistir.
2. **WS5** — probar WS1 en secundaria.
3. **WS2** (recablear LumaDeck a `install.sh`), manteniendo la ruta headcrab como
   fallback tras un flag durante una release.
4. **WS3** (borrar maquinaria) una vez WS2 probado.
5. **WS4** (downgrade escape-hatch) — independiente, cuando sea; menor urgencia
   (rara vez dispara).

---

## 6. Riesgos y mitigaciones

| Riesgo | Impacto | Mitigación |
|---|---|---|
| El wrapper no cubre alguna vía de launch | Steam corre vanilla (sin unlock) — **falla seguro, no brickea** | cubrir `.desktop` + launcher sistema + autostart + PATH; re-afirmar con guardian |
| Coexistencia de las 3 `.so` | crash en init | ya probado en `steam.sh` hoy; mismo orden (library-inject 1º; CR/lumalinux en LD_PRELOAD) |
| Perder la red de seguridad del freeze en un update malo | brick / crash-loop | portar el crash-guard de moon (WS1.6): cliente-cambiado + crash → vanilla |
| El servidor estático de downgrade no casa con lo que Steam pide | downgrade falla | mantener `dgsc` de Deadboy como fallback hasta verificar el patrón |
| Drift de las URLs de release (AceSLS/CR) | fetch falla | fijar a `releases/latest`, fail-closed con error claro (como hace `headcrab_compat` hoy) |

---

## 7. Dependencia de headcrab que queda tras todo esto

- **Ruta feliz** (install / quick / repair): **CERO**.
- **Downgrade escape-hatch**: **dos ficheros de datos** (pin de compat +
  `sources.txt`) de los repos de Deadboy, leídos por HTTP, en una ruta opt-in poco
  frecuente. El mecanismo es nuestro. Promovible a 100%-propio más tarde (generar
  `sources.txt` desde `updates.yaml`) si se quiere.

---

## 8. Ficheros tocados (referencia)

**lumalinux**
- `install.sh` — mayor: instalador por wrapper (fetch + wrapper + cobertura + uninstall)
- *(nuevos)* helper de cobertura (`scripts/ensure-desktop-coverage.sh`), plantilla
  del wrapper, unit del guardian (`slsteam-desktop-guardian.{path,service}`)

**LumaDeck** (`backend/`)
- `installer.py` — reescribir `install_dependencies`, quitar patches headcrab
- `steam_freeze.py` — **BORRAR**
- `desktop_handoff.py` — **BORRAR o encoger** (conservar solo sign-in CR si aplica)
- `headcrab_compat.py` — encoger → `downgrade_pin.py` (solo lee pin + sources)
- `downgrade.py` — **NUEVO** (escape-hatch)
- `components.py`, `update_checks.py`, `paths.py`, `slssteam_ops.py`, `main.py`,
  `quick_install_cli.py` — ajustar referencias

---

## 9. Referencias

- Modelo de inyección: `swwayps/slsteam-moon` `setup.sh` (wrapper + cobertura
  `.desktop`/launcher). Findings propios: `docs/slsteam-moon-findings.md` M7, M3.
- Mecanismo de downgrade: `Deadboy666/h3adcr-b` `headcrab.sh`
  (`clientdowngrade` = `prepdowngrade` + `overideupdate`; `dlm`/`dgsc`/`sources.txt`
  de `h3adcr-b-modul3s`; chunks desde `client-update.fastly.steamstatic.com`).
- Las 6 gates que deben seguir abriendo: `docs/method.md`.
- Inyección actual (a sustituir): `lumalinux/src/main.cpp:58`; `LumaDeck/backend/
  installer.py:121-253` (`_HEADCRAB_PATCHES`), `backend/paths.py` (`injection_missing`).
