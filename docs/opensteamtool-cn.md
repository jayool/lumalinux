# opensteamtool-cn (huanyuejue/OpenSteamTool) — el fork chino, leído contra BetterSteamTools

Leído desde el código el 2026-10-09. Es la DLL que instala Fluent-Steam-Lua, el
front-end de la comunidad china. Comparte antepasado con BetterSteamTools
(`bettersteamtools.md`): los dos salen de OpenSteamTool y el punto común es
`c3d89e9` (2026-08-11, la clave de producto legacy de BST, que este fork tomó
como PR #183). Este doc se lee **contra BST**: donde no se dice otra cosa, el
comportamiento es el de `bettersteamtools.md` en la versión de OST anterior a
las adiciones de BST, y aquí solo constan las diferencias, con
`fichero:línea` de `huanyuejue/OpenSteamTool@d2a18f8` (rutas relativas a
`src/`). Sustituye a las secciones del fork chino de
`bettersteamtools-findings.md` (deltas del 09-16 al 10-07). Cómo **nosotros**
hacemos lo mismo está en `nosotros.md`.

---

## §0 Ficha

| | |
|---|---|
| **Qué es** | La misma DLL que OST (proxies `dwmapi.dll`/`xinput1_4.dll` → `OpenSteamTool.dll` dentro de `steam.exe`, Detours + `int3`/VEH, reescritura de mensajes CM e IPC, `.lua` ejecutados en Lua 5.5 sin sandbox), con tres líneas propias: **engancha una copia** de `steamclient64.dll` (`bin\diversion64.dll`) y redirige a Steam hacia ella para que los juegos con comprobación de integridad vean el original limpio; **seis proveedores de códigos con failover y enfriamiento**; y **family sharing** completo (sin bloqueo del prestador). No tiene nada de lo que BST añadió después del 08-11: ni archivo de manifests, ni donación, ni auto-update, ni canje `bst://`, ni acuñado de ETickets, ni detección en vivo del layout de `CNetPacket`. |
| **Repo y commit leídos** | `huanyuejue/OpenSteamTool` `main@d2a18f8` (2026-10-09; tags `1.5.1` → `1.5.2.1`, el último `fa1dcd3` del 10-03). 84 commits; 25 propios desde `c3d89e9` (huanyuejue 19, más 2 merges, 3 de `huanjue` y 1 de `Peron4TheWin`), 1.598 líneas añadidas y 567 quitadas en 27 ficheros. 156 ficheros, 15.020 líneas en `src/`. Feed de patrones: `OpenSteam001/steam-monitor` (`pattern@134c9b1`, 2026-09-06; `ipc@c61b16c` y `protobuf@a6839ec`, 2026-09-26). |
| **Plataforma** | Windows x64 solo; build MSVC con `/utf-8` forzado para que compile en Windows con página de códigos GBK (`src/CMakeLists.txt:24`). |
| **Licencia y quién** | GPL-3.0 (de OST). Un mantenedor, `huanyuejue`; README en chino por defecto desde `fa1dcd3` ("本项目是 OpenSteam001/OpenSteamTool 的优化分支，为适配 Steam 入库工具 huanyuejue/Fluent-Steam-Lua 而开发"). |
| **Relación con los demás programas** | **Fluent-Steam-Lua** (mismo autor, WPF) lo instala y configura; fuera de alcance de momento. **OpenSteamTool** es su origen y su feed; **BetterSteamTools** es el fork hermano. **SteamToolbox** (B-I-A-O) redistribuye una rama propia de este fork; fuera de alcance. Con nosotros: dos de sus proveedores (`20770407`, `manifestdex`) están en la cascada de lumalinux detrás de la comprobación de CDN (`nosotros.md` §2.4); el resto no convive con nuestra pila. |

**Lo que cambia respecto al doc anterior**, en una línea cada uno:

- **Ya no se rinde al primer 404 ni pide primero a la red**: caché local primero, luego jsDelivr, luego GitHub raw, todos los espejos aunque uno dé 404 (`02d7955`, `840477d`; `Utils/SteamMetadata/RemoteToml.cpp:108-131,203-213`).
- **El proveedor por defecto es `20770407`** desde `6d0f1b6` (09-28), con failover a los otros cinco desde `a35262b` (09-29); el doc anterior lo dejó en wudrm (`b754d13`, 09-15).
- **`kProviders` no es un nombre copiado de lumalinux**: la tabla se llama así en OST desde `fb08dce` (2026-06-13). Lo nuevo es el failover.
- **Vuelve la copia del cliente** (`72a06bf`, 10-09): hooks sobre `bin\diversion64.dll` y redirección de `LoadModuleWithPath` y de `GetModuleHandle*` hacia ella, para el crash de arranque de los juegos nuevos de Capcom.
- **Denuvo por "pulsos" de tiempo** (`d2a18f8`, 10-09): la identidad prestada se sirve 300 ms al arrancar y 300 ms tras cada ticket (2,5 s y 3 s con `dauth2(appid)`), no hasta el segundo handshake.
- **El `.toml` de ejemplo no parsea**: `[manifest] url` aparece dos veces (`opensteamtool.example.toml:27,62`, desde `a35262b`); quien lo copie tal cual se queda con los valores por defecto sin saberlo (§2.11).

---

## §1 Mapa del código

Solo lo que difiere de `bettersteamtools.md` §1 (en el estado de OST al
`c3d89e9`). Lo que no aparece aquí es igual.

| Fichero | Líneas | Diferencia | F |
|---|---|---|---|
| `dllmain.cpp`, `dllmain.h` | 233, 49 | `DiversionPath = bin\diversion64.dll` (`:88`); copia del `steamclient64.dll` si tamaño o fecha difieren (`:24-35`), por `.tmp` + `MoveFileExA` con 3 reintentos (`:37-77`); carga de la copia, borrado y segunda copia si falla, si no el original con aviso (`:95-130`); hooks de SteamUI **antes** que los de steamclient; si `LoadModuleWithPath` no tiene patrón, vuelta al original y recarga de patrones (`:170-185`); barrera `g_HooksInstalled`. `.lua` en `config\lua` (`:89`) | 1, 9 |
| `Hook/Hooks_SteamUI.cpp`, `.h` | 322, 20 | `LoadModuleWithPath`: espera la barrera (10 ms × 500) y devuelve la copia cuando Steam carga `steamclient` (`:145-187`); detours de `GetModuleHandleA/W` y `GetModuleHandleExA/W` en todo el proceso para que quien pregunte por `steamclient` reciba la copia (`:63-143`, `:278-283`) | 1 |
| `Utils/SteamMetadata/RemoteToml.cpp` | 215 | `OpenSteam001/steam-monitor` en jsDelivr y GitHub (`:16-21`, `[remote] order`); caché primero (`:108-131`); `.tmp` + rename; distingue "upstream has not published (all N mirrors 404)" de un fallo de red (`:203-213`) | 1 |
| `Utils/SteamMetadata/ManifestClient.cpp`, `.h` | 316, 44 | seis proveedores en orden de failover (`:59-67`); enfriamiento 60 s por proveedor, presupuesto 15 s por código, recortes de timeout al presupuesto (`:213-271`); mutex solo para el tramo Lua (`:277-303`) | 4 |
| `Hook/Hooks_NetPacket.cpp` | 1.624 | espera del código = presupuesto + 1 s (`:525`); `NotifyRunningApps` saneado (deja `family_groupid`, vacía `running_apps`, `:476-499`); 9405 descartado (`:1498`); `owner_id` del prestador → 1 en `GamesPlayed` (`:1056`); presencia para amigos con el `game_id` del primer acceso directo no-Steam (`:947-975`); `[presence] display` | 2, 6 |
| `Hook/Hooks_Package.cpp`, `.h` | 254, 21 | `m_Size` reescrito tras `Grow` (`:75,221`); juegos prestados no cuentan como poseídos (`:121`); a **todo** juego prestado, del `.lua` o no, le quita `bLicenseLocked` y `bBorrowed` y lo da por poseído (`:140-150`); `HasValidLicense` = poseído o prestado | 2 |
| `Pipe/Features/DenuvoAuth/DenuvoAuth.cpp`, `.h` | 301, 19 | ventana por plazos (`:17-32,172-176`), extensión por ticket (`:261-268`), cierre por plazo vencido (`:280-293`); persistencia del SteamID también para prestados | 6 |
| `Hook/Hooks_IPC_ISteamUser.cpp` | — | `GetAppOwnershipTicketExtendedData` extiende el plazo (`:74`) | 6 |
| `Utils/Config/Config.cpp`, `.h`; `LuaConfig.cpp` | 312, 88; — | proveedor `20770407` (`Config.cpp:16`), `failover`, `remote.order`, `presence.display`; timeouts acotados a 1-15.000 ms (`:87-97`); `dauth2(appid)` (`LuaConfig.cpp:447-458`) | 4, 6, 11 |
| `Steam/Structs.h` | — | `CNetPacket` fijo, `m_pubData` justo tras `m_hConnection` (`:209-217`): sin el layout `+8` de la beta | 1 |

No tiene: `ManifestCache`, `ManifestDonor`, `Mirror`, `AppUpdater`,
`TokeerBridge`, `EticketClient`, `Steam/NetPacket.h`, la guarda de depots vacíos
ni `YldLoadDepotManifest`, ni las funciones Lua `addprocess`, `forcedenuvo` y
`seteticketurl`. `lua_pinApp` existe, pero su registro está comentado como en BST (`Utils/Config/LuaConfig.cpp:294,486`): ningún `.lua` puede llamarlo.

---

## §2 Las doce funciones

### 2.1 Engancharse a Steam

Como BST en la entrada (proxies), con tres diferencias. **Engancha una copia**:
al arrancar copia `steamclient64.dll` a `bin\diversion64.dll` si tamaño o fecha
no coinciden, carga la copia, pone todos los hooks en ella y engancha
`LoadModuleWithPath` de `steamui` para devolver la copia cuando Steam carga su
cliente, más `GetModuleHandle*` de todo el proceso para que nadie vea el
original por nombre (`dllmain.cpp:95-130`, `Hook/Hooks_SteamUI.cpp:63-187`). El
motivo, en su comentario: que el original quede intacto para las comprobaciones
de integridad de Denuvo y RE Engine (`72a06bf`, "修复卡普空的新游戏启动崩溃闪退").
Si `LoadModuleWithPath` no se puede enganchar, vuelve al original con aviso
(`dllmain.cpp:170-185`); si la carga de Steam llega antes de que los hooks
estén, espera hasta 5 s y luego devuelve el original. **Caché primero**: el feed
solo se pide si no hay TOML local para ese SHA (`RemoteToml.cpp:108-131`). Y el
feed es **`OpenSteam001/steam-monitor`**, que no publica patrones desde el
2026-09-06 (§5.2): cualquier cliente posterior al estable `1788652215` arranca
sin hooks. El layout de `CNetPacket` está fijo (`Steam/Structs.h:209-217`); el
día que la beta recompilada llegue a estable, harán falta a la vez TOML nueva y
cambio de código. Las funciones que el fork resuelve están todas en la última
TOML del feed salvo las dos del módulo KeyValues, muerto como en BST (§5.2).

### 2.2 Propiedad y licencias

Como BST, más family sharing en las dos direcciones. Como prestatario: a todo
juego prestado, esté o no en el `.lua`, le quita el bloqueo y lo da por poseído
(`Hook/Hooks_Package.cpp:140-150`); los prestados no cuentan como "poseídos de
verdad" para las exclusiones (`:121`). Hacia Valve: el `owner_id` del prestador
se cambia a 1 en cada `GamesPlayed` saliente (`Hook/Hooks_NetPacket.cpp:1056`),
`NotifyRunningApps` llega con la lista vacía y el grupo intacto (`:476-499`) y la
notificación de bloqueo 9405 se descarta (`:1498`), para que el prestador no
pierda su biblioteca mientras el prestatario juega. `m_Size` del `AppIdVec` se
reescribe tras `Grow` (`:75,221`) porque, según su comentario, `Grow` solo
amplía la capacidad; BST no lo hace y su inyección funciona, lo que apunta a que
el `Grow` resuelto sí aumenta la longitud (sin medir).

### 2.3 DLC

Como BST.

### 2.4 Claves y manifests

Claves y pin como BST. **Manifests**: ninguna fuente; Steam los pide al CDN con
el código. **Códigos**: `fetch_manifest_code(_ex)` del `.lua` (con su propio
mutex, `Utils/SteamMetadata/ManifestClient.cpp:277-303`) y después una
**cascada** desde el proveedor configurado, en este orden circular
(`:59-67`): `20770407` (`/<depot>/<gid>`), `SDM`
(`steamapi.993499094.xyz/manifest/<depot>/<gid>`), `manifestdex` (UA
`ManifestDeX/1.0`), `wudrm` (`http://`), `steamrun`, `opensteamtool` (solo por
gid). Un proveedor que falla se salta 60 s; el conjunto tiene 15 s y el hook
espera 16 s en el hilo de recepción (`:213-271`; `Hook/Hooks_NetPacket.cpp:525`).
Los códigos de varios depots se piden en paralelo. Ningún código se comprueba
en el CDN. `[manifest] failover = false` deja solo el configurado.

### 2.5 Updates de juegos

Como OST: pin por `setmanifestid`; `pinapp` no se registra (`Utils/Config/LuaConfig.cpp:486`). Sin la guarda de BST
contra la configuración de depots vacía.

### 2.6 Fixes y DRM

Como OST (SteamStub forjado, tickets del registro, ruta 480, clave legacy,
inyección con el `[inject]` de OST: una DLL x86 y otra x64, `enabled` off por defecto, sin las condiciones por appid o línea de comandos de BST, `Utils/Config/Config.cpp:197-206`). Sin acuñado de ETickets por
backend ni canje. **Ventana de Denuvo por plazos**: el primer pipe de un juego
Denuvo recibe la identidad del ticket durante 300 ms desde el arranque y otros
300 ms cada vez que pide el ticket de propiedad (para el
`memcmp(Ticket->SteamID, GetSteamID())` que hace Denuvo justo después); con
`dauth2(appid)`, 2,5 s y 3 s (`Pipe/Features/DenuvoAuth/DenuvoAuth.cpp:17-32,172-176,261-268`;
`Hook/Hooks_IPC_ISteamUser.cpp:74`). Al vencer el plazo, vuelve la identidad real
y, si la cuenta posee **o tiene prestado** el juego, se guarda su SteamID
(`:280-293`; `3ddcd9f`, Error 54 de los juegos prestados). En la ruta 480, la
presencia para amigos usa el `game_id` del primer acceso directo no-Steam que
encuentre en cualquier `userdata\*\config\shortcuts.vdf`, con el nombre real;
sin accesos directos, 480 (`Hook/Hooks_NetPacket.cpp:947-975`; `d61d98d`,
`[presence] display = "none"` lo apaga).

### 2.7 Logros, stats y tiempo de juego

Como OST: SteamID de donante (`setstat` → `stats.opensteamtool.com` → fijo
`76561198028121353`, `Utils/Config/LuaConfig.cpp:51`).

### 2.8 Cloud saves

Como OST (CloudRedirect). La redirección de `GetModuleHandle*` entrega a
CloudRedirect la copia del cliente, que es la que Steam usa, para que sus hooks
de vtable caigan donde toca (comentario en `Hook/Hooks_SteamUI.cpp:59-63`).

### 2.9 Añadir y quitar un juego

Como BST, pero la carpeta por defecto es `config\lua` (`dllmain.cpp:89`), no
`stplug-in`.

### 2.10 Credenciales y proveedores

Ninguna cuenta. Seis proveedores de códigos sin autenticación (§2.4).

### 2.11 Mantenimiento propio

Sin auto-update: lo actualiza Fluent-Steam-Lua o el usuario. Sin UI. Avisos y,
en Debug, logs. El `opensteamtool.example.toml` tiene `url` dos veces en
`[manifest]` (`:27,62`): no es TOML válido, `Config::Load` lo rechaza y la DLL
sigue con los valores por defecto (que coinciden con los del ejemplo, así que
solo se nota al cambiar algo). Timeouts acotados a 1-15.000 ms
(`Utils/Config/Config.cpp:87-97`).

### 2.12 Proyecto

Windows, GPL-3.0, un mantenedor, ritmo de ráfagas (5 commits el 09-18, 3 el
10-09). Terceros: `OpenSteam001/steam-monitor` (parado), jsDelivr, los seis
proveedores, `stats.opensteamtool.com`. Privacidad: el SHA del cliente solo
sale cuando falta en caché; los appids de stats; los gids de cada descarga a
los proveedores. No dona ni envía códigos. Hacia Valve cambia el `owner_id` de
los juegos prestados (§2.2), una señal que el servidor puede ver.

---

## §3 Superficies externas

Las de BST (`bettersteamtools.md` §3) **menos** `luastools.xyz`,
`manifest.luastools.xyz`, `madoiscool/*`, `git.lua.tools` y el backend de
ETickets, **más**:

| URL / ruta / clave | Fichero:línea | Uso |
|---|---|---|
| `https://cdn.jsdelivr.net/gh/OpenSteam001/steam-monitor@{canal}/…`, `https://raw.githubusercontent.com/OpenSteam001/steam-monitor/…` | `Utils/SteamMetadata/RemoteToml.cpp:16-21` | patrones e IPC |
| `https://20770407.xyz/manifest/<depot>/<gid>`, `https://steamapi.993499094.xyz/manifest/<depot>/<gid>`, `https://manifest.manifestdex.com/<gid>` (UA `ManifestDeX/1.0`), `http://gmrc.wudrm.com/manifest/<gid>`, `https://manifest.steam.run/api/manifest/<gid>`, `https://manifest.opensteamtool.com/<gid>` | `Utils/SteamMetadata/ManifestClient.cpp:59-67` | códigos |
| `<Steam>\bin\diversion64.dll` (+ `.tmp`) | `dllmain.cpp:37-77,88` | copia enganchada del cliente |
| `<Steam>\config\lua\*.lua` | `dllmain.cpp:89` | entrada |
| `<Steam>\userdata\*\config\shortcuts.vdf` | `Hook/Hooks_NetPacket.cpp:947-975` | `game_id` para la presencia |
| `[manifest] url` (`20770407`), `failover` (`true`); `[remote] order` (`jsdelivr-first`); `[presence] display` (`spacewar`) | `Utils/Config/Config.cpp:16-25` | |
| `.lua`: `dauth2(appid)` | `Utils/Config/LuaConfig.cpp:447-458` | |

---

## §4 Lo que nos afecta

### 4.1 Hallazgos pendientes de decisión

Aparcados el 2026-10-09. Numeración propia de este doc.

1. **`SDM` como quinto proveedor.** `steamapi.993499094.xyz` devolvió el mismo
   código que `20770407` en el mismo instante (§5.2): otra puerta al mismo
   origen, no capacidad nueva. Candidato de baja prioridad (una fila y un test
   en `gmrc_store.hpp`); la comprobación de CDN decide. Aparcado desde el
   2026-09-29.
2. **Enfriamiento por proveedor.** Saltar 60 s un proveedor que acaba de
   fallar evita pagar su timeout en cada código de una tanda. Si nuestra
   cascada sufre timeouts en serie con un proveedor caído, esta es la
   referencia. Sin acción.
3. **Caché primero y "no publicado" frente a "sin red".** Las dos cosas ya las
   hacemos (`rva_feed.cpp`, `watch-steam.yml`). Comprobado, sin cambio.
4. **El fork chino depende de un feed parado.** Para cualquier usuario de
   Fluent-Steam-Lua con un cliente posterior al 09-06, la DLL arranca sin
   hooks. No nos afecta; es la tercera prueba de que un feed de terceros puede
   pararse sin aviso (las otras: `Steam-Auto-PT`, `lumacore.md` §5.2, y la de
   las funciones `_STR` de MigoReleases). Va con
   `update-resilience-comparison.md`.

### 4.2 Dependencias y qué rompe si cambia

| Si cambia… | Qué nos pasa |
|---|---|
| `20770407` o `manifestdex` | también están en nuestra cascada; el fork chino los usa como primero y tercero, lo que añade carga a los mismos servidores |
| el resto | nada; Windows |

### 4.3 Lo que tiene y nosotros no (y al revés)

Lo de `bettersteamtools.md` §4.3, salvo archivo de manifests, donación,
auto-update y ETickets por backend, que no tiene, más:

| Función | opensteamtool-cn | Nosotros | Notas |
|---|---|---|---|
| 1 | hooks sobre una copia del cliente y redirección de módulos | hooks sobre `steamclient.so` en memoria | Windows; el motivo (integridad de Denuvo/RE Engine) no aplica bajo Proton |
| 2 | family sharing sin bloqueo del prestador, para todo juego prestado | SLSsteam (`DisableFamilyShareLock`) | otra capa |
| 4 | seis proveedores con failover y enfriamiento, sin CDN | cascada con CDN | §4.1-1, -2 |
| 6 | ventana de Denuvo por plazos; presencia como juego no-Steam | fixes por botón | no portable |

### 4.4 Decisiones ya tomadas (del doc anterior, con fecha) y estado hoy

| # | Decisión | Fecha | Hoy |
|---|---|---|---|
| C1 | `20770407` y `manifestdex` en la cascada de lumalinux detrás de la comprobación de CDN; el campo `headers` por proveedor de PR #200 se volvió nuestro `userAgent` por proveedor | 2026-09-16 | **hecho** (lumalinux v0.21.0, `RESEARCH.md` §20) |
| C2 | La reescritura de `m_Size` tras `Grow`: nuestro `AppendIdsToVec` ya fija `m_Size` | 2026-09-22 | comprobado, sin cambio |
| C3 | Espejo jsDelivr para `res/rvas/` y `updates.yaml` | 2026-09-22 | aparcado (`bettersteamtools.md` §4.4 D8) |
| C4 | `SDM` como quinto proveedor | 2026-09-29 | aparcado (§4.1-1) |
| C5 | Family sharing y presencia del fork: capa de SLSsteam | 2026-09-22 | vigente |

---

## §5 Historial

### 5.1 Lo que se creía y ya no es así

Además de lo listado en la cabecera:

- **"Proveedor por defecto wudrm, cuatro proveedores"** (09-16/09-17). Son seis
  y el primero es `20770407` desde el 09-28.
- **"El fork pudo tomar `kProviders` de lumalinux"** (09-29). OST ya lo llamaba
  así en junio.
- **"Los dos feeds de patrones siguen congelados"** (10-07). Solo el de
  `OpenSteam001`, que es el de este fork; desde el 09-26 tampoco publica `ipc`
  ni `protobuf`.

**Lo que sigue sin medir**:

- Si Steam funciona con normalidad sobre la copia del cliente durante una
  actualización (la copia se rehace en el siguiente arranque).
- Si los seis proveedores responden hoy.
- Si el cambio de `owner_id` en `GamesPlayed` tiene consecuencias para la cuenta.

### 5.2 Pruebas y mediciones con fecha

#### Función 1 — Engancharse a Steam

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-22 | `OpenSteam001/steam-monitor` `pattern@134c9b1` | último TOML | 09-06 (estable 1788652215); faltan las betas del 09-10, 09-11, 09-17, 09-19 y 09-22; `ipc` y `protobuf` sí publicaron esos días | el bot vive, solo su paso de patrones está muerto |
| 2026-09-28 | el mismo | seis días después | sigue en 09-06 (22 días, nueve betas sin TOML) | |
| 2026-10-09 | `OpenSteam001/steam-monitor` | estado | `pattern@134c9b1` (09-06), `ipc@c61b16c` y `protobuf@a6839ec` (09-26): todo el feed parado desde el 09-26; 44 TOML por componente | |
| 2026-10-09 | `OpenSteam001` vs `madoiscool/steam-monitor`, rama `pattern` | ficheros con el mismo SHA | `steamui`: 19 comunes, los 19 idénticos; `steamclient`: 19 comunes, 11 idénticos y 8 con los mismos nombres y RVAs y una firma distinta (comodines `??` en lugar de bytes) | el origen oculto de madoiscool genera lo mismo que el bot de OpenSteam001 |
| 2026-10-09 | última TOML de `OpenSteam001` vs `src/` | nombres que el fork resuelve | presentes los 29, `LoadModuleWithPath` incluido, salvo `FindOrCreateKey` y `ReadAsBinary` (KeyValues, muerto) | cobertura completa para el estable 1788652215 |

#### Función 4 — Claves y manifests

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-16 07:03 UTC | `20770407`, `manifestdex`, `wudrm`, `steam.run` | código para un gid | los cuatro `CODE_VALID` | así se supo que wudrm y steam.run habían vuelto (el fork cambió a wudrm una hora después de que 20770407 diera 502) |
| 2026-09-29 | `SDM` y `20770407` | Spacewar 481, gid `3183503801510301321` | el mismo código, `5726322088222453673`, en el mismo instante | mismo origen o uno cachea al otro |
| 2026-09-29 | `depotcn.caigamer.cn/manifest/<gid>` (proveedor de códigos del lanzador chino SteamToolbox, que redistribuye una rama de este fork) | código para un gid, con nuestro UA, con UA de Chrome + `Accept: application/json` y con el de curl | **403** en los tres; en el mismo minuto `20770407` respondió | no es un filtro de UA: región o una clave que añade la app; inutilizable desde fuera de China, cerrado |

### 5.3 Cronología

**Topología.** Fork de `OpenSteam001/OpenSteamTool` que tomó de BST los commits
`2c7af78` y `c3d89e9` (08-11) por PR #183 y siguió solo.

- **2026-08-11.** `huanjue`: line endings, merge de la clave legacy (#183),
  arreglo de una llave en `ClientGetUserStats`; `2522faf` (Peron4TheWin, de
  junio) presencia para juegos no poseídos por acceso directo.
- **2026-09-13.** `224931d` proveedor `20770407` por depot/gid, por defecto;
  `d834619` `[presence] display`; README con la nota de fork para
  Fluent-Steam-Lua.
- **2026-09-15.** `a730c12` `manifestdex`; `b754d13` por defecto wudrm.
- **2026-09-18.** `47cc7ad` jsDelivr primero; `02d7955` caché primero;
  `3a9a3e2` `m_Size` tras `Grow`; `dc61ad8` family sharing con el prestador
  sin bloqueo; `7109796` códigos en paralelo. Tags `1.5.1.1` (09-15) y
  `1.5.1.2` (09-18).
- **2026-09-25.** `840477d` seguir con el siguiente espejo tras un 404.
- **2026-09-28/29.** `3b7c318` `SDM`; `6d0f1b6` por defecto `20770407`;
  `a35262b` failover entre los seis (tag `1.5.2`).
- **2026-09-30 → 10-03.** `d61d98d` presencia para amigos y máscara de
  `owner_id` separadas; `fa1dcd3` README chino por defecto (tag `1.5.2.1`).
- **2026-10-09.** `72a06bf` copia del cliente (Capcom); `3ddcd9f` identidad de
  los prestados al cerrar la ventana de Denuvo (Error 54); `d2a18f8` ventana por
  plazos para motores multihilo (Error 54).
