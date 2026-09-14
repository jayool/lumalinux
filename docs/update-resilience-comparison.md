# Sobrevivir a un update de Steam — nuestra cadena contra el ecosistema

*Completo. Cómo detecta, re-deriva, entrega, degrada, alcanza el proceso y se
protege del brick cada proyecto que engancha `steamclient`, en seis ejes fijos,
contra la cadena de lumalinux + LumaDeck (`watch-steam.yml`, `check_patterns.py`,
Ghidra headless, feed `res/rvas/`, `main.cpp`, `setup.sh`). §0 método, §1 la
nuestra tal como está, §2–§7 un bloque por proyecto, §8 la tabla, §9 veredicto y
pasada adversaria, §10 candidatos. **Ningún candidato es código hasta que se
decida uno a uno.***

---

## §0 Método y evidencia

Seis ejes, los mismos para todos:

1. **Detección**: cómo se enteran de que Valve ha publicado un cliente nuevo.
2. **Re-derivación**: cómo vuelven a encontrar las funciones.
3. **Entrega**: release compilada o datos que el cliente recoge solo.
4. **Build desconocido**: bloquear, avisar y seguir, o probar a ciegas.
5. **Cobertura de lanzamiento**: cómo llegan al proceso en escritorio y modo
   juego, y si sobreviven a que Steam regenere sus ficheros.
6. **Anti-brick**: qué pasa si la inyección tira Steam en bucle.

Referencias congeladas: SLSsteam `71021ad` (09-03); `headcrab.sh` y
`headcrab_native.sh` bajados el 09-14; BST `7243c60`; `madoiscool/steam-monitor`
rama `pattern` (últimos 80 commits, 09-11); LumaCore en `Midrags/SFF` `fa44fc9` y
launcher en `drappula/SFF` `92d6813`; SteamFlipper `7b6e5f7`; slsteam-moon
`cae57d2`; `swwayps/steam-monitor` `main` (09-14); SLSDeck y ASSella por sus
análisis propios; CloudRedirect 2.6.5. Etiquetas: [read], [inferred],
[measured] (salida real del 09-14), [doc].

---

## §1 La nuestra, tal como está el 2026-09-14 [read]

- **Detección.** `watch-steam.yml`, cron diario 07:17 UTC. Nivel 1 gratis:
  `fetch_steamclient.py --version-only` contra el manifest `steamdeck_stable` de
  `media.steampowered.com`, caché de Actions por versión. Nivel 2 solo con versión
  nueva: descarga del paquete VZ, SHA-256 de `ubuntu12_32/steamclient.so`,
  `check_patterns.py` en Python puro. 82 runs, todos verdes hasta el 13-09
  [measured].
- **Validación.** Crítico solo DepotKey. No críticos ShaderDepot y Reconcile
  (`caps`). Diagnósticos GMRC, BuildDep, LoadPackage. Segundas opiniones:
  restricción RTTI (exactamente un slot de la vtable de `CConfigStore`),
  resolución por nombre vía `IClientConfigStoreMap::GetBinary`, xref de GMRC +
  `.eh_frame_hdr`, y las dos anclas del finder (idioma `0xc58`, cola del prólogo
  de GMRC). Desacuerdo = BLOCKING; opinión que no resuelve = aviso.
- **Re-derivación.** Ghidra 11.2.1 headless (`run_ghidra_derive.sh`,
  `derive_patterns.py`) anclado en strings estables, `apply_derived_pattern.py`
  solo sobre UNIQUE, re-validación, y solo en un "pattern moved" real.
  `watch-steam-selftest.yml` corrompe patrones a propósito para probar los
  caminos 2 y 3. `verify-fix.yml` nunca ha estado en verde y lo dice en su
  cabecera.
- **Entrega.** CLEAN: `whitelist_hash.py` + `res/rvas/<sha>.yaml`, PR
  auto-mergeado. Medido en los cinco builds desde julio: PR #22 creado 06:03:32,
  mergeado 06:03:35; #27 12:08:50 → 12:08:53 [measured]. Patrón movido: PR de
  fuente con el feed dentro, el build afectado se autocura antes de la release.
- **Runtime.** Hash consultivo. Por hook: feed RVA por hash (traducido con
  `VaddrXlate`, acotado a `.text`) → patrón compilado → para DepotKey, nombre
  RTTI. Finder por mitades con su propio escaneo de respaldo. `status.json` con
  método y resultado por pieza; LumaDeck lo reduce a cinco estados.
- **Cobertura.** `setup.sh` sin root: wrapper en `~/.local/share/SLSsteam/path/steam`,
  `.desktop` Exec + PATH drop-in + guardián systemd en escritorio, drop-in en
  `steam-launcher.service` en modo juego. `steam.sh` vanilla.
- **Anti-brick.** `guard.sh`: minidumps en los 180 s tras el arranque, tres
  seguidos o uno tras cambio de `steamclient.so` → vanilla latcheado.

---

## §2 SLSsteam + Headcrab

Filosofía contraria: Headcrab **congela Steam** en un build que Ace ha probado
(`HeadcrabCompatibleClientVer` en el script, manifest pineado en su rama de
SteamTracking), le prohíbe actualizarse (`steam.cfg` `BootStrapperInhibitAll`),
y cuando Ace valida uno nuevo el usuario ejecuta el "Headcrab Updater", que
lanza Steam con `-forcesteamupdate -forcepackagedownload -overridepackageurl
http://localhost:1666/` contra `dgsc`, un servidor local que le sirve al
bootstrapper el cliente pineado [read `headcrab.sh`].

| Eje | SLSsteam + Headcrab | Diferencia |
|---|---|---|
| 1 | Nadie vigila. Sin CI. Ace se entera cuando se rompe. | Cron. |
| 2 | A mano. Patrones "que aguantan 9 meses" (`28d59a1`) con `SigFollowMode`; `VFTIndexes` por nombre RTTI con **índice de slot hardcodeado**. Cero re-derivaciones desde julio: los eligió bien. | Ghidra en CI, RTTI como restricción, nombre como paracaídas. |
| 3 | Hash: commit a mano en `res/updates.yaml`, agrupado por versión de SLSsteam (un build puede exigir release aunque solo cambie el hash). Lag en los mismos builds que nosotros: 07-22 mismo día, 07-25 tres días, 07-28, 08-04, 09-02 mismo día [measured]. | PR automático en segundos, feed, patrones compilados. |
| 4 | `SafeMode: yes` (Headcrab lo fuerza en SteamOS): "Unknown steamclient.so hash! Aborting..." y `unload()`. Fail-closed total. | Consultivo, degrada por hook. |
| 5 | `steam.sh` **sustituido** por `headcrab_native.sh`, 555, original en `client.sh`. Modo juego porque `steam-jupiter` acaba en `steam.sh`. Contra la regeneración: no dejar que Steam se actualice. | Fuera del árbol de Steam, tres caminos, guardián. |
| 6 | Solo el SafeMode. Cero detección de crash (grep en `src/`: tres comentarios). | `guard.sh`. |

Lo que hacen mejor o igual: patrones más estables que los nuestros; SafeMode
fail-closed más seguro que nuestro "consultivo" si un patrón resuelve UNIQUE
en el sitio equivocado; y su pin sigue siendo dependencia de datos nuestra
(`headcrab_compat.py`, issue #26 abierta). Dato suelto: Headcrab pinea el
manifest de Deck en `1788291500` pero su variable dice `1788652215`, el build de
escritorio; para una Deck no cambia nada.

---

## §3 BetterSteamTools + steam-monitor (Windows)

BST no lleva patrones compilados: hashea `steamclient64.dll` y `steamui.dll` y
baja un TOML por hash (nombre, RVA, firma; 25 entradas) de `steam-monitor`, que es
un espejo sincronizado cada 5 min de "evil-steam-monitor", un bot en la infra de
LuaTools (`git.lua.tools`, secretos `UPSTREAM_URL`/`UPSTREAM_OWNER`). Nuestro
feed está calcado de esto y lo dice `rva-feed-design.md` §2.

| Eje | BST + steam-monitor | Diferencia |
|---|---|---|
| 1 | Bot upstream, estable y beta, Windows. Rama `pattern` desde 28-07 con backfill; 15 commits, 4 estables. Publica en la promoción a canal (estable `1788652215` compilado 05-09, publicado 10-09) [measured]. **Sin Linux**: componentes `steamclient` y `steamui`, DLL. | Somos el único monitor de `steamclient.so`. |
| 2 | "Rule patterns", generador cerrado. Rama `ipc` con vtable RVA, índice y `fencepost`. | Abierto y con selftest; sin fencepost en runtime. |
| 3 | Solo datos, tres mirrors (raw, jsdelivr, `git.lua.tools`), caché por hash. Binario por `latest.toml` en rama `updates`. | Datos + compilado. |
| 4 | Sin TOML: popup "signature file not found", módulo apagado la sesión. Fail-closed por módulo, 100 % dependiente del bot. | Compilado + RTTI + nombre. |
| 5 | Windows, DLL proxy. | n/a |
| 6 | Ninguno. Un TOML con RVA malo tira Steam sin red. | `guard.sh`. |

Lo que hacen mejor: latencia, betas, tres mirrors, `fencepost`. Lo que hacemos
mejor: respaldo compilado, generador abierto, Linux.

---

## §4 LumaCore + SFF (Windows)

LumaCore (fork de OST de Midrag, hijack de `dwmapi.dll`) tampoco lleva patrones
compilados: `<Steam>\lumacore\pattern\<sha>.toml`, formato de steam-monitor,
firma RSA-PSS con **módulo a cero** (`PatternSig.cpp`, placeholder; sin firmar se
acepta con aviso). Su repo de patrones ha sido tres cosas [measured 09-14]:

| Repo | Quién | Estado |
|---|---|---|
| `KoriaPolis/Steam-Auto-PT` `pattern` ("Auto-generated by SteaMidra steam-monitor") | monitor de Midrag, clon del de LuaTools | parado el 19-08; último estable 03-08; ninguno de septiembre |
| gitflic, jsDelivr | mirrors | comentados |
| `michelegoku3/MigoReleases` `pattern` (fork, `ffd70db`) | migo3 a mano | tiene el beta 03-09 (`caba4826`); **no** el estable 10-09 (`ea23997e`) ni el beta 11-09: 1 de 4 |

Es decir, LumaCore en el Steam estable de Windows desde el 10-09 no tiene
patrones: el "Windows tampoco funciona" del hilo de FMHY, medido.

| Eje | LumaCore + SFF | Diferencia |
|---|---|---|
| 1 | El monitor murió con Midrag. SFF solo comprueba al arrancar si hay TOML para el build (`prewarm`, `1b3d3a8`) y pinta banner desde `status.json`. | Cron. |
| 2 | Antes, reglas calcadas de LuaTools; ahora, migo3 a mano sin herramienta pública. | Ghidra abierto. |
| 3 | Solo datos, caché atómica, firma vacía. | Datos + compilado. |
| 4 | `toml_found: false`, hooks no instalados, banner. Fail-closed total. | Degrada por hook. |
| 5 | Windows. | n/a |
| 6 | Ninguno. | `guard.sh`. |

Lo que demuestra su muerte: un monitor que vive en la máquina de una persona
muere con la persona. El nuestro vive en Actions con el generador en el repo.

---

## §5 SteamFlipper (Linux)

Port de OST: un `.so` que sustituye `ubuntu12_64/libXtst.so.6` (el proxy de
Millennium). Consumidor de TOML heredado; productor propio `gen_linux_patterns.py`
que corre **en la máquina del usuario al instalar** y escribe el TOML en la ruta
de caché de `RemoteToml`.

| Eje | SteamFlipper | Diferencia |
|---|---|---|
| 1 | Ninguna. Sin CI, sin cron. El usuario se entera cuando Steam pisa el proxy o el generador se niega. | Cron. |
| 2 | Tres por **VProf scope** (strings de profiling + `.eh_frame`, sin desensamblador), tres por derivación estructural desde el cuerpo de `CheckAppOwnership` (con chequeo de FDE), tres **pineadas a mano** por SHA en un dict Python (`VERIFIED`). Sin RTTI: `CConfigStore::GetBinary` no lo alcanzan y escriben `config.vdf` desde Python. | Tenemos RTTI y nombre; no tenemos VProf (`steamflipper-analysis.md` §5.7, F3 evidence-gated). |
| 3 | Para Linux **no hay canal**: commit, `git pull`, reinstalar. Self-updater compilado fuera. Los cinco mirrors de steam-monitor dan 404 en Linux. | PR + feed + compilado. |
| 4 | Fail-closed razonado: `CUtlMemoryGrow` es load-bearing, sin ella el generador sale con error. Nosotros no la necesitamos (el finder camina la caché). | Degrada por hook. |
| 5 | Proxy **dentro del árbol de Steam**, Valve lo pisa en cada update, slot compartido con Millennium. Recuperación: reinstalar a mano. | Fuera del árbol, guardián. |
| 6 | Ninguno. Que no brickee es porque sin proxy Steam arranca vanilla por accidente. | `guard.sh`. |

Sin reacción al 09-09 (`steamflipper-analysis.md` §10). Nota de portabilidad
propia que salió de ahí: nuestro `.so` exige GLIBCXX 3.4.32 / GLIBC 2.38.

---

## §6 slsteam-moon (Linux)

Fork de SLSsteam de LuaTools. Nuestro `setup.sh`, `ensure-desktop-coverage.sh` y
`guard.sh` descienden de aquí (M3, M7, `decouple-headcrab-plan.md`).

| Eje | slsteam-moon | Diferencia |
|---|---|---|
| 1 | `swwayps/steam-monitor`: catálogos firmados por hash en `linux32/steamclient/`, estable y beta, cada 3-8 días. Hoy Stable `1788652215`, Beta `1789086785`, y **cubre los tres hashes de Deck probados** (`bc54101b`, `d0c0ff6e`, `237495b4`) [measured]. Producción cerrada, en LuaTools. | Nuestro productor está en nuestro repo; ellos tienen betas y más cadencia. |
| 2 | Reglas cerradas + **máscara de bytes de layout** (M1.1: wildcard de offsets de miembro en firmas que solo localizan) + re-derivación estructural en runtime de la raíz de `RunIPCFrame` (M1.2, acepta solo con un candidato en banda). Cuatro commits de patrones a mano en agosto (`d3402a1`, `0fb590f`, `a8029de`, `4c10427`). | Ghidra en CI. La máscara es portable y no la tenemos. |
| 3 | Catálogo Ed25519 verificado por `bin/pattern-refresh`, binario aparte (sin libcrypto en el `.so`), refrescado desde el wrapper antes de Steam, no bloqueante; patrones compilados de base. | Igual sin firma (declinada, §14 del diseño). |
| 4 | **Quitaron el SafeMode** (M2) por nuestro propio incidente; fail-open con guard. `updates.yaml` de Ace se sigue leyendo. | Consultivo con whitelist automática propia. |
| 5 | `.desktop` con etiqueta + autostart + PATH drop-in; modo juego por PATH, que **no llega a gamescope** en Deck (comprobado, por eso WS1.1 divergió a systemd). `steam.sh` vanilla tras revertir el shim (M7). | Systemd para modo juego. |
| 6 | El guard original: tres crashes o uno tras cambio de cliente → vanilla, latch por sesión. | Portado tal cual. |

Lo que hacen mejor: máscara de layout, firma en binario aparte, "runtime hook
attestation" (validan que cada hook quedó donde tocaba; nuestro `status.json` +
cross-checks avisan, no bloquean), cobertura de betas. LuaTools anunció el 09-13
que moon espera su "nuevo sistema" para Linux: re-barrer entonces.

---

## §7 SLSDeck, ASSella, CloudRedirect

- **SLSDeck.** Hereda SLSsteam + Headcrab en los ejes 1-4. Eje 5: parchea
  `steam.sh` **y** deja que Headcrab lo parchee, con re-parche silencioso cuando
  Steam lo revierte (`slssteam.py:512`); su comentario admite que solo su wrapper
  "aplicado el último" inyecta en modo juego. Eje 6: contador, no latch.
  `slsdeck-analysis.md` §3.5.
- **ASSella.** No inyecta nada propio: Headcrab por `curl | bash`, `SafeMode: yes`
  heredado, sin `.desktop`, sin systemd, sin guard. Añadido en el eje 4: como
  descarga fuera de Steam y bloquea el update nativo, un build desconocido no
  afecta a lo instalado, solo impide instalar. `assella-analysis.md`.
- **CloudRedirect.** Contraste del eje 2: resuelve la vtable de
  `CClientUnifiedServiceTransport` **por nombre de clase RTTI** desde
  `/proc/self/maps`, sin patrones, sin feed, sin whitelist; 2.6.5 sigue
  funcionando sin cambios (`cloudredirect.md:71-92`, campo 09-14). Nuestro
  DepotKey tiene ese camino como paracaídas; CloudRedirect como único.

---

## §8 La tabla

| Eje | Nosotros | SLSsteam+Headcrab | BST | LumaCore/SFF | SteamFlipper | moon | SLSDeck / ASSella | CloudRedirect |
|---|---|---|---|---|---|---|---|---|
| 1 Detección | cron diario propio, Deck estable | ninguna | bot LuaTools, Win, estable+beta | monitor muerto; migo3 a mano | ninguna | bot LuaTools, Linux, estable+beta, 3-8 días | heredan | no necesita |
| 2 Re-derivación | Ghidra CI + RTTI + nombre, abierta, selftest | a mano; patrones estables + slots fijos | reglas cerradas | a mano | VProf + estructural + 3 a mano | reglas cerradas + máscara + estructural runtime | heredan | RTTI por nombre |
| 3 Entrega | PR auto en segundos + feed + compilado | commit a mano + release por grupo | solo datos | solo datos | commit + pull + reinstalar | datos firmados + compilado | heredan | nada |
| 4 Desconocido | consultivo, por hook | fail-closed total | módulo apagado | apagado | fail-closed razonado | fail-open + guard | heredan; ASSella no afecta instalados | funciona |
| 5 Cobertura | wrapper, `.desktop`+PATH+systemd, guardián, `steam.sh` vanilla | `steam.sh` sustituido + cliente congelado | n/a | n/a | proxy en árbol de Steam | `.desktop`+PATH (no gamescope) | doble parche / Headcrab | en nuestro wrapper |
| 6 Anti-brick | guard 3/1 → vanilla | solo SafeMode | ninguno | ninguno | ninguno | guard original | contador / ninguno | n/a |

---

## §9 Veredicto y pasada adversaria

**Veredicto.** Somos los únicos con la cadena entera automatizada y abierta:
detectar, validar, re-derivar, entregar, degradar y recuperar, sin depender de
una persona ni de una infraestructura ajena. LuaTools nos gana en canales
cubiertos y velocidad de publicación con una máquina cerrada que alimenta a BST
y a moon; si se para, los dos se quedan sin nada nuevo y nosotros tenemos los
patrones compilados. Ace nos gana en patrones que no se mueven; le ganamos en no
tener que estar él. SteamFlipper tiene la técnica de derivación que no tenemos y
el peor punto de inyección. CloudRedirect demuestra que la resolución por nombre
es la más robusta, y por eso es nuestro paracaídas.

**Adversaria.**
- *"Vuestros patrones se mueven más que los de Ace; la automatización tapa una
  fragilidad."* Cierto. La respuesta correcta es §10-C1, no negarlo.
- *"moon cubre la Deck igual de bien, firmado y con betas."* Medido, sí. La
  diferencia es de propiedad y de respaldo: su productor no es suyo ni público y
  sin él no hay compilado que valga para builds nuevos.
- *"El hash consultivo permite enganchar en el sitio equivocado."* La defensa es
  el guard, no el gate, y §5.8 de SteamFlipper ya lo reconoce. Cerrarlo del todo
  es la atestación en runtime (§10-C5).
- *"`verify-fix.yml` nunca ha pasado: nada en CI ejecuta el `.so`."* Cierto y
  documentado en su cabecera; la validación en dispositivo sigue siendo manual
  (`update-testing.md` Parte 2).
- *"El pin de Headcrab sigue en LumaDeck."* Cierto, issue #26.

---

## §10 Candidatos (a decidir uno a uno; ninguno es código todavía)

- **C1 — Máscara de bytes de layout en `patterns.hpp`** (moon M1.1). **Ya
  hecho donde tiene sentido y refutado donde no** (issue #16, cerrada 07-06 con
  medida en el build `fa20a21d`): wildcardear el tamaño de frame deja BuildDep
  con 70 coincidencias y LoadPackage con 222; en DepotKey y GMRC es seguro pero
  inútil (el frame no ha derivado en 11 builds, DepotKey tiene RTTI y nombre, y
  los bytes de frame de GMRC son el ancla de la GOT del finder). Lo que sí se
  enmascara ya: el disp32 de la global en ShaderDepot (`8B 83 ?? ?? ?? ??`) y en
  Reconcile el frame, el offset de miembro de `CUser` y el spill, calcados de
  moon. Único byte de layout sin decidir: el `0x44` de `mov eax,[eax+0x44]` en
  ShaderDepot (no crítico). Decisión: **nada ahora**; si ShaderDepot se mueve
  alguna vez por ese byte, enmascararlo entonces y validarlo con
  `verify_mask.py` en el runner, que es donde está el binario.
- **C2 — Lookahead de beta en el cron**: derivar contra `steamdeck_publicbeta`
  sin whitelistear, para llegar cubiertos al día de promoción
  (`slsdeck-analysis.md` §8).
- **C3 — Cerrar issue #26**: derivar el "target" seguro de Steam de nuestro
  propio cron en vez del pin de Headcrab.
- **C4 — VProf como segunda vía automática** (SteamFlipper F3, evidence-gated en
  su §5.9).
- **C5 — `fencepost` de vtable y atestación de hooks en runtime** (BST `ipc`,
  moon), y un segundo mirror del feed.
