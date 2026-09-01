# Análisis — SLSsteam (vanilla) para lumalinux

*Fecha de investigación: 2026-07-06. Base del análisis: [`AceSLS/SLSsteam`](https://github.com/AceSLS/SLSsteam)
@ `ebfb079` (`VERSION = 20260624075231`), i386, C++20/CMake, libmem.
Re-verificado contra las releases `20260705132808` (`5c632dd`) y `20260705144737`
(`da97d11`) — ver §7 para los deltas. **Barrido de la ventana `20260723102618` →
`20260815201341` (205 commits) en §7.7, en curso día a día.** Compañero de
[`slsteam-moon-findings.md`](slsteam-moon-findings.md) (el mod) y
[`opensteamtool-findings.md`](opensteamtool-findings.md).*

SLSsteam **no compite** con lumalinux: es el fork base, la capa de propiedad /
tickets / mensajes sobre la que lumalinux coexiste. Este doc no es una tabla
"SLSsteam vs lumalinux" fila a fila, porque no hacen lo mismo. El eje es:
**qué hace SLSsteam, cómo lo implementa, y qué me aporta a lumalinux** — y como
las dos piezas funcionan *en conjunto*, nada se descarta. Cada hallazgo cae en
uno de tres cubos:

- **[YA]** — lumalinux ya hace lo mismo con el mismo mecanismo; sirve de confirmación.
- **[PRESTABLE]** — técnica que podría endurecer lumalinux; candidata a issue.
- **[FRONTERA]** — capa que SLSsteam posee y lumalinux delega en él por diseño.
  Aquí lo útil es *documentar el límite de coexistencia*, no copiar: es justo lo
  que evita que os piséis.

---

## 0. Contexto — qué es SLSsteam y cómo se carga

SLSsteam es una lib i386 in-process dentro del cliente `steam`. Modifica las
estructuras internas y el flujo de mensajes de Steam para: jugar juegos no
poseídos / family-share sin bloqueo, desbloquear DLC, spoofear appId, cachear y
reproducir tickets de propiedad (incl. offline), y cosméticos (wallet, email,
idle status). Es el cimiento sobre el que lumalinux añade su capa de
depot-keys / manifests / shader-depot.

### 0.1 Inyección: rtld-audit (`LD_AUDIT`), **no** `LD_PRELOAD` — [FRONTERA, clave]

Esto es lo más importante para la coexistencia y no estaba escrito en ningún
sitio nuestro. SLSsteam implementa la interfaz **rtld-audit** del loader
(`main.cpp:221-239`):

- `la_version()` → `LAV_CURRENT` (se anuncia como auditor del enlazador dinámico).
- `la_preinit()` → llama a `setup()` (una sola vez, antes de que corra nada).
- `la_objopen()` → dispara `load()` **exactamente cuando `steamclient.so` o
  `steamui.so` entran en el mapa** (`main.cpp:226-234`).

O sea, SLSsteam se carga vía `LD_AUDIT=...SLSsteam.so`, no `LD_PRELOAD`. Dos
consecuencias directas para lumalinux:

1. **No compiten por el hueco de inyección — y la división es principiada por
   ambos lados.** lumalinux es en realidad **dual-load**: implementa la interfaz
   de audit (`la_preinit`/`la_objopen`, `main.cpp:277-303`) *y* un
   `__attribute__((constructor))` para `LD_PRELOAD`. Pero **en la práctica corre
   por `LD_PRELOAD` a propósito**: el propio código lo dice — "NOT LD_AUDIT; the
   audit namespace corrupts the heap → realloc abort" (`main.cpp:162`), y además
   los hosts de 64-bit rechazan libs de audit de 32-bit, con lo que en la mayoría
   de launchers `LD_PRELOAD` es el único camino que corre (`main.cpp:11-15`).
   Resultado: **SLSsteam se queda con `LD_AUDIT`, lumalinux elige `LD_PRELOAD`.**
   No es casualidad de dos mecanismos distintos; es una repartición donde cada
   uno usa el que le funciona bien. `steam.sh` (parcheado por headcrab, marcador
   `INJECT_SLS`) pone **ambos** y el loader los carga por caminos separados. Esa
   es la base física de que convivan sin chocar en el arranque.
2. **`la_objopen` es un "espera-hasta-que-el-target-esté-mapeado" limpio y
   gratuito.** SLSsteam no hace polling ni relee `/proc/self/maps` esperando a
   steamclient: el loader le *avisa*. lumalinux resuelve el mismo problema por
   otros medios (patterns.cpp escanea los mapeos r-x cuando instala). No es
   portable (lumalinux no es auditor), pero conviene tenerlo anotado como el
   mecanismo "correcto" del lado de SLSsteam.

`setup()` además **se auto-limpia del entorno**: `cleanEnvVar("LD_AUDIT", ...)`
borra las entradas de SLSsteam de `$LD_AUDIT` (`main.cpp:111-116`) para que los
procesos hijos no la hereden, y sólo continúa si el proceso se llama `steam`
(`main.cpp:95-99`) — en cualquier otro proceso hace `unload()` y se va. Es la
misma disciplina "sólo actúo en el proceso correcto" que lumalinux necesita al
convivir bajo un `LD_PRELOAD` global.

---

## 1. Motor de localización y hooking (lo más comparable a lumalinux)

### 1.1 Pattern_t + patternScan — mismo linaje que lumalinux [YA]

`Pattern_t` (`patterns.hpp:11-27`) es el mismo tipo del que desciende el de
lumalinux: nombre + string de bytes con wildcards `?` + modo de resolución +
prólogo opcional. `MemHlp::patternScan` (`memhlp.cpp:35-108`) enumera los
segmentos `LM_PROT_XR` (ejecutables) del módulo y escanea byte a byte — igual
que el `SigScan` de lumalinux sobre los mapeos r-x. `patternToBytes`
(`memhlp.cpp:11-33`) convierte `"E8 ? ? ? ?"` a `int16_t` con `-1` como
comodín. Nada nuevo aquí: es la misma familia. Confirma que la byte-pattern es
el suelo común de todo el ecosistema.

### 1.2 Tres modos de seguimiento — uno que lumalinux no explota [PRESTABLE]

`searchSignature` (`memhlp.cpp:110-140`) resuelve el match según el modo:

- **`Relative`** — desensambla el `call`/`jmp` del match y sigue el destino
  (`getJmpTarget`, `memhlp.cpp:152-167`). Igual que anclar en un call-site y
  seguir al target.
- **`None`** — usa la dirección tal cual (para offsets/campos, no funciones).
- **`PrologueUpwards`** — **el interesante**: ancla en una firma *dentro* de la
  función y escanea **hacia arriba** hasta 0x10000 bytes buscando la secuencia
  de bytes del prólogo (`findPrologue`, `memhlp.cpp:169-195`), p.ej.
  `56 57 e5 89 55` (`push esi; push edi; ...`). Devuelve el inicio real de la
  función.

lumalinux ancla sus patterns fragiles en strings de `.rodata` y deriva la
función con Ghidra offline. `PrologueUpwards` es una **técnica alternativa de
anclaje**: firma en el cuerpo (más estable que el prólogo, que cambia en cada
recompilación) + walk-up al prólogo. Candidata para los patterns que hoy
dependen de bytes de prólogo (BuildDep es string-anchored, pero si algún día un
pattern se ancla en prólogo, esto lo hace más robusto sin Ghidra).

### 1.3 `fixPICThunkCall` — reparación de thunks PIC en el trampolín [PRESTABLE, investigar]

`memhlp.cpp:197-278`. En i386-PIC, muchas funciones acceden a globales vía un
`call __x86.get_pc_thunk.*` que devuelve el EIP en un registro. Cuando copias
los primeros bytes de la función a un trampolín (para un detour inline), ese
`call` relativo **apunta a un sitio equivocado** desde la nueva ubicación —
y peor, algunos son "thunks IPC" que Steam usa para resolver la dirección de
retorno. SLSsteam desensambla el trampolín, detecta el patrón `call` → `mov reg,
[retaddr]` → `ret`, y **reescribe la instrucción para cargar la dirección de
retorno correcta calculada desde la ubicación original** de la función.

Esto importa a lumalinux directamente: **si `lmhook` no maneja los thunks PIC al
construir sus trampolines, un detour de una función que empiece cerca de un
`get_pc_thunk`/thunk-IPC podría corromperse silenciosamente.** lumalinux hookea
funciones reales de steamclient (DepotKey, BuildDep, GMRC) con detours inline —
exactamente el escenario donde esto muerde. **Acción:** verificar si `lmhook` ya
cubre la reubicación de calls relativos / thunks PIC; si no, esto es un
endurecimiento real, no cosmético.

### 1.4 `getTypeName` — RTTI puro, la misma primitiva que acabamos de implementar [YA / confirma §15]

`memhlp.cpp:281-288`:

```cpp
const lm_address_t vft      = *reinterpret_cast<lm_address_t*>(pClass);
const lm_address_t typeInfo = *reinterpret_cast<lm_address_t*>(vft - sizeof(lm_address_t));
const char* name            = *reinterpret_cast<const char**>(typeInfo + sizeof(lm_address_t));
```

Es **exactamente** el camino vtable → type_info → name-string que `src/rtti.cpp`
recorre para resolver `12CConfigStore` slot 6 (RESEARCH §15). Diferencia de uso:
SLSsteam lo usa **sólo para logging** (imprime el tipo de cada `CProtoBufMsg`
recibido), partiendo de un objeto **vivo** (`pClass`). lumalinux lo usa al revés
— partiendo del **string** en `.rodata`, hacia atrás hasta la vtable **estática**
en `.data.rel.ro`, para **resolver una dirección**. Confirmación importante: la
técnica RTTI que introdujimos no es exótica; el fork base ya la tiene en su
toolbox, sólo que no la había aplicado a la resolución de funciones. Valida el
enfoque de §15.

### 1.5 Dos tipos de hook: DetourHook (pattern) + VFTHook (slot de vtable) [PRESTABLE — la dirección de §15.4]

`hooks.cpp` monta hooks de dos maneras:

- **`DetourHook`** (`hooks.cpp:65-108`): detour inline por dirección de pattern
  (`LM_HookCode` + `fixPICThunkCall`). Es lo que hace lumalinux con `lmhook`.
- **`VFTHook`** (`hooks.cpp:110-149`): **swap de un slot de vtable por índice**
  (`LM_VmtHook(vft, index, hookFn)`). Los índices están en `vftableinfo.hpp`
  (p.ej. `IClientAppManager::BIsDlcEnabled = 11`, `IClientApps::GetDLCCount = 8`).

El VFTHook es la versión "activa" de lo que lumalinux hace "pasivo" con RTTI:
SLSsteam **cambia el slot**, lumalinux **resuelve la dirección del slot y hace un
detour ahí** (no toca la vtable — `rtti.hpp` lo dice explícito: read-only). Los
dos keyean sobre `{interfaz, índice de slot}`, no sobre bytes de prólogo — que es
justo la robustez que perseguimos en §15. **Esto es evidencia de que la
migración de más hooks a resolución por slot (BuildDep/GMRC si tuvieran vtable)
es la dirección correcta**, y de que hay dos sabores válidos (swap vs
detour-en-slot-resuelto); lumalinux eligió el no-intrusivo, que es el correcto
bajo coexistencia (no queremos reescribir vtables que SLSsteam podría estar
leyendo).

### 1.6 Captura de instancia viva + self-heal vía RunIPCFrame [SUPERSEDED upstream — ver §7.7]

> ⚠️ **SUPERSEDED (2026-07-24, `8de3384`).** SLSsteam **eliminó los cinco hooks de
> `RunIPCFrame`** y con ellos esta técnica entera. Las instancias vivas ya no se
> capturan al vuelo: se navegan estructuralmente (`CSteamEngine → getUser() →
> getAppManager()/getClientApps()/getClientUser()`) vía offsets de miembro
> localizados por patrón, desde un único `placeVFTHooks()`. Esta sección se
> conserva como registro del mecanismo antiguo; **como técnica "PRESTABLE" está
> obsoleta** — lo vigente es §7.7.

Problema: para hacer un VFTHook necesitas el **puntero de la vtable de una
instancia viva** de la interfaz (`IClientAppManager`, etc.), y ese puntero no
existe estáticamente. Solución de SLSsteam (`hooks.cpp:433-455`,
`hkClientAppManager_RunIPCFrame`):

1. Hookea `RunIPCFrame` de la interfaz por pattern (detour).
2. En la **primera** llamada, `pClientAppManager` **es** una instancia viva →
   `LM_VmtNew(*reinterpret_cast<lm_address_t**>(pObj), vft)` extrae su vtable.
3. Monta los VFTHooks por índice sobre esa vtable.
4. **Se auto-elimina** (`RunIPCFrame.remove()`) y llama al original — el hook de
   RunIPCFrame era sólo el disparador para capturar la instancia.

Otros interfaces usan un `static bool hooked` en vez de auto-removerse
(`hkClientApps_RunIPCFrame`, `hooks.cpp:508-531`), mismo efecto: hookear la
vtable una sola vez, en el primer frame, cuando hay un objeto real.

Contraste con lumalinux: `rtti.cpp` encuentra la vtable **estática** en
`.data.rel.ro` (esperando a que las relocaciones acaben, cap 30s) — no necesita
una instancia viva. Ambos válidos. **Anotar como técnica de respaldo:** si algún
día lumalinux necesita una interfaz cuyo vtable *no* aparece estáticamente (sólo
existe tras construir el objeto), el patrón "hookea RunIPCFrame → captura `this`
→ resuelve vtable → deshookea" es la vía. Es el mismo self-heal de RunIPCFrame
que ya anotamos para slsteam-moon (§13/#18), aquí en su forma original.

### 1.7 Hook "naked" ensamblado a mano (GetSteamId) [SUPERSEDED upstream — ver §7.7]

> ⚠️ **SUPERSEDED (2026-07-27, `c4152f5`).** SLSsteam **borró el trampolín naked**.
> Ahora intercepta el `steamId` en el **buffer de retorno de la capa IPC**, dentro de
> `CSteamEngine::RunInterface` (desde 2026-08-05, `ProcessIPCFrame`), filtrando por
> `interfaz == ClientUser && exitCode == Success && fnId == 0xD6FC3200`. El
> desensamblado de la función se conserva como comentario en su código. La lección de
> esta sección ("lo que cuesta hookear sin punto de anclaje limpio") se mantiene, pero
> el mecanismo descrito ya no existe. Ver §7.7.

`createAndPlaceSteamIdHook` (`hooks.cpp:824-930`) es un caso especial:
`IClientUser::GetSteamID` tiene múltiples matches y una convención rara, así que
SLSsteam **desensambla la función, ensambla un trampolín a mano** (pushad/pushfd,
llama a su hook `stdcall`, popad, ejecuta las instrucciones sobreescritas) y
coloca un `jmp` relativo. Es artesanía frágil que el propio autor marca como
"lazy / bad at this". No es algo que lumalinux quiera imitar — se documenta como
recordatorio de lo que cuesta hookear una función sin un punto de anclaje limpio,
que es justo el problema que la resolución por RTTI/slot elimina.

---

## 2. Inventario de features + frontera de coexistencia

Cada feature con su punto de enganche y su cubo. La columna **frontera** es la
que importa para no pisar a SLSsteam.

| Feature | Dónde engancha | Qué hace | Cubo |
|---|---|---|---|
| **Play not-owned / family-share** | `CUser::CheckAppOwnership` detour (`hooks.cpp:290`) → `Apps::checkAppOwnership` (`apps.cpp:52`) | Muta `CAppOwnershipInfo` en sitio (`unlockApp`, `apps.cpp:18`): `ownsLicense=true`, owner, `releaseState`, quita low-violence/region-lock | **[FRONTERA]** SLSsteam posee la propiedad. lumalinux **no** toca ownership. |
| **AppList injection** | `CUser::GetSubscribedApps` detour (`hooks.cpp:313`) → `Apps::getSubscribedApps` (`apps.cpp:129`) | Añade `AdditionalApps` del config a la lista de apps suscritas | **[FRONTERA]** |
| **PICS tokens** | `CProtoBufMsgBase::Send` detour (`hooks.cpp:203`) → `Apps::sendPICSInfoRequest` (`apps.cpp:230`) | Adjunta `access_token` por-app en el `PICSProductInfoRequest` saliente (EMsg 8903) para des-strippear appinfo | **[FRONTERA]** Ya cableado en nuestro stack vía `steamidra_lite --token`. |
| **DLC unlock** | VFTHooks `IClientApps::GetDLCCount/GetDLCDataByIndex`, `IClientAppManager::BIsDlcEnabled/IsAppDlcInstalled` + `IsUserSubscribedAppInTicket` | Reporta DLC como poseído/instalado; inyecta DLC del config (`dlc.cpp`) | **[FRONTERA]** |
| **FakeAppId** | `SetAppIdForCurrentPipe`, `GetAppId`, matchmaking, UGC, servers (`fakeappid.cpp`) | Mapea appId real↔falso por pipe para que un juego se presente como otro (multiplayer/servers) | **[FRONTERA]** Relevante a LumaDeck, no a la capa de depots de lumalinux. |
| **Tickets (ownership + encrypted)** | `CProtoBufMsgBase::InitFromPacket` → `Ticket::recvMsg` (`ticket.cpp:253`); `BUpdateAppOwnershipTicket`, `GetAppOwnershipTicketExtendedData`, GetSteamId | Cachea tickets a disco (`~/.config/SLSsteam/cache/*.yaml`, base64), los **reproduce en offline**, spoofea `steamId` del ticket | **[FRONTERA]** Solapa territorio de LumaDeck (online-fix/tickets). |
| **Achievements offline** | `CAPIJob::GetPlayerStats` detour + `InitFromPacket` (`achievements.cpp`) | Fuerza `ERESULT_NO_CONNECTION` para que Steam use stats locales/offline | **[FRONTERA]** LumaDeck-ish. |
| **Family lock disable** | `patchRetn` sobre `FamilyGroupRunningApp` y `StopPlayingBorrowedApp` (`hooks.cpp:1037-1041`) | Escribe `0xC3` (ret) al inicio → neutraliza la función | **[PRESTABLE]** ver §2.1 |
| **Fake offline / wallet / email** | `GetOfflineMode`, `BLoggedOn`, `InitFromPacket` (`misc.cpp`) | Cosméticos: modo offline por-app, balance de wallet, email verificado | **[FRONTERA]** cosmético. |
| **Denuvo safety** | `Apps::checkAppOwnership` (`apps.cpp:61-69`), config `DenuvoGames` | **No** modifica juegos Denuvo poseídos por otra cuenta (evita bans/rompimientos) | **[PRESTABLE]** ver §2.2 |

### 2.1 `patchRetn` — primitiva "neutraliza función" [PRESTABLE, menor]

`hooks.cpp:813-821`: proteger la página `XRW`, escribir un solo byte `0xC3`
(`ret`) en la entrada de la función, restaurar protección. Es la forma más
barata de desactivar una función entera sin trampolín. lumalinux ya usa la ruta
de skip propia de Steam para el shader-depot (más limpia), pero `patchRetn` es
una herramienta útil de tener en el cinturón para "haz que esta función no haga
nada" sin los riesgos de un detour completo.

### 2.2 Guardas de seguridad — el patrón "do no harm" [PRESTABLE, principio]

Dos guardas de SLSsteam valen como principio para lumalinux:

- **Denuvo/other-owner** (`apps.cpp:61-69`): no tocar lo que podría causar un ban
  o romper DRM ajeno.
- **`shouldExcludeAppId`** (`config.cpp:292-309`): appId `>= 1e9` es rango
  interno de Steam → nunca tocar; más el filtro whitelist/blacklist.

lumalinux sirve keys de `KeyStore` incondicionalmente (por diseño — sólo tiene
keys que le pasan). No hay riesgo Denuvo ahí, pero el principio "identifica los
rangos/casos donde intervenir hace daño y exclúyelos explícitamente" es sano y
barato de adoptar si alguna vez lumalinux amplía qué sirve.

---

## 3. SafeMode / Updater — el gate que lumalinux **copió**, con RTTI encima

`update.cpp`. SLSsteam mantiene su seguridad-ante-updates así:

1. Al arrancar, `Curl::getString` baja
   `raw.githubusercontent.com/AceSLS/SLSsteam/.../res/updates.yaml`
   (`update.cpp:22`). Si falla la red, **cae al cache en disco**
   (`.updates.yaml`, `update.cpp:27-34`).
2. Parsea `SafeModeHashes`: un mapa `versión-de-SLSsteam → {SHA256 de
   steamclient.so conocidos-buenos}` (`update.cpp:41-55`).
3. `verifySafeModeHash` (`update.cpp:102`) saca el SHA256 del steamclient.so
   cargado y comprueba si está en el set de `VERSION`.
4. En `load()` (`main.cpp:176-188`): si el hash es desconocido **y** `SafeMode`
   está on → **aborta** (`unload()`). Si está off pero `WarnHashMissmatch` on →
   sólo avisa.

Esto es el mismo concepto de SafeMode que ya tratamos alrededor del pin de
headcrab — y aquí hay que corregir una simplificación fácil de asumir. **No es
"SLSsteam usa allowlist, lumalinux usa RTTI".** lumalinux tiene **las dos cosas**:

- **lumalinux también tiene un hash-gate "estilo SLSsteam"** (`main.cpp:134-152`,
  el comentario literal dice "Hash gate, SLSsteam-style"). Lee su **propio**
  `res/updates.yaml` bajo el grupo `LUMALINUX_SAFEMODE_VERSION`, hashea el
  steamclient.so cargado, y si no está en la lista **aborta fail-closed** (sin
  override, sin config — la única salida es publicar release nueva o añadir el
  hash a `main`). Escribe `status.json` con `hash_unverified` para que LumaDeck
  distinga "bloqueado por hash" de "Steam no arrancó". Es un calco directo de
  `update.cpp` de SLSsteam. (Se puede compilar fuera con `LUMA_NO_UPDATE` — sólo
  para builds de validación/codespace, nunca distribución.)
- **RTTI/slot (§15) va _encima_ del gate, no en su lugar.** El gate decide si
  lumalinux instala; el RTTI hace que la *resolución* de la función sobreviva
  cambios de build sin re-derivar bytes. Son capas ortogonales: el gate protege
  contra "build no validado"; el RTTI reduce cuántos builds hay que re-validar a
  mano.

Entonces la diferencia real de filosofía es más fina: **ambos gatean por hash**
(SLSsteam sobre su lista, lumalinux sobre la suya — dos listas independientes),
y **ambos deben pasar** bajo coexistencia. lumalinux *añade* resolución
resiliente para que su lado del gate necesite tocarse menos. Consecuencia
operativa: **el stack sólo arranca si pasan los DOS SafeModes** — el de SLSsteam
(o headcrab te downgradea a un build pinneado) y el de lumalinux (o instala en
modo bloqueado, juegos ya instalados siguen, descargas nuevas off). Detalle no
obvio: hay **dos gates de versión en serie**, no uno.

---

## 4. Config hot-reload + API local — ergonomía [PRESTABLE, menor]

### 4.1 Hot-reload vía inotify — **con la trampa que SLSsteam ya pisó**

`CFileWatcher` (`filewatcher.cpp`) crea un `inotify_init`, observa un fichero en
un hilo (`watchLoop`), y llama a un callback al cambiar. `config.cpp:75-86` lo
cablea: al editar `config.yaml`, `loadSettings()` recarga **sin reiniciar
Steam**. Los settings viven en `mtvar` (variables thread-safe), así que los hooks
leen el valor nuevo en caliente.

**Aviso importante (corregido tras leer la release `5c632dd`):** la versión
"obvia" de esto —`inotify_add_watch(fd, fichero, IN_MODIFY)` sobre el fichero
directo— **está rota** y SLSsteam la tuvo que arreglar. Los editores que guardan
por rename atómico (nvim, y muchos otros) reemplazan el inode: el watch queda
colgado del fichero viejo y **sólo dispara una vez**. La forma correcta, la que
SLSsteam usa ahora:

- vigilar el **directorio padre** con `IN_CLOSE_WRITE` (no el fichero, no
  `IN_MODIFY`),
- **filtrar los eventos por nombre de fichero** (`event->name`),
- leer el **buffer entero** de inotify de una (el código viejo leía sólo
  `sizeof(inotify_event)` y se comía el nombre y los eventos encolados).

Si lumalinux adopta hot-reload de su `KeyStore` / config algún día, **el patrón a
copiar es el de directorio + `IN_CLOSE_WRITE` + filtro por nombre**, ~60 líneas,
no el de vigilar el fichero (que documenté aquí antes como prestable y resultó
ser justo la variante buggy). Candidato de baja prioridad, pero copiar la versión
correcta desde el principio.

**Actualización 2026-08-15 (`1444fa5`) — el watcher ahora sí ve los renames.**
SLSsteam añadió `IN_MOVED_TO` a la máscara, en el `inotify_add_watch` **y** en el
filtro del loop:

```cpp
constexpr static int WATCH_MASK = IN_CLOSE_WRITE | IN_MOVED_TO;   // filewatcher.hpp
```

Esto cierra el agujero que quedaba: **`IN_CLOSE_WRITE` no dispara cuando el fichero
llega por rename** (`rename(2)` / `os.replace`), sólo cuando alguien escribe y cierra
el fichero en su sitio.

**Impacto directo en LumaDeck.** `backend/slssteam_ops.py::_commit_config()` escribe
el `config.yaml` de forma atómica (`os.replace`) y **luego hace un "poke"**
(reabrir+cerrar el fichero) cuyo único propósito es emitir un `IN_CLOSE_WRITE` que el
watcher sí escuchaba — el comentario de esa función documenta exactamente esta
carencia. Con SLSsteam ≥ `20260815201341` **el poke es redundante**: el rename ya se
detecta solo, y desaparece la ventana entre el `replace` y el poke.

No retirar el poke sin gatear por versión: LumaDeck ya lee la versión instalada de
SLSsteam (`backend/components.py`, `backend/slssteam_config.py` → `.slssteam.version`),
así que la condición es trivial. Y el poke es inofensivo en builds nuevos (un
`IN_CLOSE_WRITE` extra = una recarga idempotente), así que **no hay urgencia**: es
deuda a retirar cuando el mínimo soportado suba, no un bug.

### 4.2 API local (canal de control)

`api.cpp`: SLSsteam abre `/tmp/SLSsteam.API`, lo observa con el mismo filewatcher,
y ejecuta comandos escritos ahí (`echo "install|<appid>|<library>" > /tmp/SLSsteam.API`
→ `IClientAppManager::installApp`). Es un canal de control fichero-basado,
hot-reloadable. lumalinux no expone control en runtime; si hiciera falta (p.ej.
"recarga keys ahora"), este es un patrón mínimo y sin dependencias.

### 4.3 Completar `config.yaml` — headcrab **y** LumaDeck (coexistencia) [2026-08]

Contexto del problema: SLSsteam escribe su config completo **solo cuando el
fichero NO existe** (`createFile()` es create-if-missing, no migra un fichero ya
existente), pero **valida al cargar** y toastea `"Missing key(s)"` cuando al
config en disco le faltan claves que la versión en curso espera. Un config que
precede a claves nuevas de SLSsteam se queda incompleto para siempre y avisa en
cada arranque; reinstalar no lo arregla (el fichero existe → SLSsteam no lo
reescribe).

Desde 2026-08 hay **dos** actores rellenando ese config, y conviene saber que
**coexisten sin pisarse**:

- **headcrab** (`updateSLSsteamConfig`, commit `fe3f6ba` — el "headcrab updates
  configs now" del Discord de SLSsteam). Toma como referencia el
  `res/config.yaml` que viene **dentro de la descarga de SLSsteam** que acaba de
  extraer, y **reescribe** el fichero en el orden de esa plantilla: conserva **tus
  valores** para claves existentes, añade defaults para las nuevas, valida con
  `DisableFamilyShareLock:` y deja backup `config.yaml.headcrab-<fecha>`. Corre
  **solo en su flujo de install/update** (incluye el "Fix in Desktop" de LumaDeck
  y el install de lumalinux).

- **LumaDeck** (`slssteam_schema.complete_slssteam_config`). Referencia el
  **`config_default.hpp`** de SLSsteam (la autoridad real contra la que valida),
  **fetch en vivo** de master + snapshot bundleado de fallback. Es **append-only**:
  conserva el fichero **byte a byte** y solo **añade al final** las claves que
  faltan. Corre en el **reconcile de arranque** + operaciones de dependencias.

Frontera / por qué se mantienen los dos:

1. **Timing distinto.** headcrab completa solo al instalar/actualizar; LumaDeck en
   cada arranque → cubre el hueco en que el config se queda incompleto sin volver
   a pasar headcrab (SLSsteam se autoactualiza, aparece una clave upstream nueva).
2. **Referencia distinta.** LumaDeck usa `config_default.hpp` (lo que SLSsteam
   valida), así que ve claves nuevas antes de que estén en un `res/config.yaml`
   publicado; headcrab usa el config del release que baja.
3. **Destructividad distinta — el punto clave.** headcrab **reescribe** iterando
   su plantilla: cualquier clave top-level que tengas y que **no** esté en su
   plantilla **se pierde** (p.ej. una que LumaDeck añadió desde un
   `config_default.hpp` más nuevo que el release de headcrab). El append-only de
   LumaDeck **nunca pierde nada** y **la reañade** en el siguiente arranque. O
   sea, juntos son **auto-curativos**.

Efectos cosméticos (inofensivos): el layout **oscila** según quién corrió último
(headcrab reordena al orden canónico y tira el header "keys added by LumaDeck";
LumaDeck reañade al final bajo ese header), y los backups `config.yaml.headcrab-*`
se acumulan. Los **valores** quedan intactos en ambos sentidos.

Veredicto: **no es redundante**. La completación de LumaDeck es la red de
seguridad de arranque, con la referencia más precisa y la única de las dos que no
puede perder claves. Se quedan las dos.

---

## 5. Mapa de coexistencia — los conjuntos de hooks son **disjuntos** (la sección que importa)

Lo más valioso del análisis: **verificar que SLSsteam y lumalinux no tocan
ninguna de las mismas funciones.** Recorriendo `patterns.cpp` + `vftableinfo.hpp`
de SLSsteam contra los cinco puntos de lumalinux:

| Punto de lumalinux | ¿Lo toca SLSsteam? |
|---|---|
| `LoadDepotDecryptionKey` (CConfigStore slot 6 / DepotKey) | **No.** SLSsteam no hookea CConfigStore ni ninguna ruta de decryption keys. |
| `BuildDepotDependency` (pin de manifest) | **No.** |
| `GetManifestRequestCode` (GMRC) | **No.** |
| `shadercachedepot` (ShaderDepot) | **No.** |
| package-0 finder (memory-walk, no es hook) | **No** — y además es un walk pasivo, no un hook, así que ni siquiera comparte superficie. |

Y al revés, SLSsteam engancha en: `CProtoBufMsgBase::Send/InitFromPacket` (capa
de mensajes), `CUser::CheckAppOwnership/GetSubscribedApps`, los `RunIPCFrame` de
7 interfaces, `IClientUtils::GetAppId/GetOfflineMode`, tickets de `IClientUser`,
DLC de `IClientApps`/`IClientAppManager`, matchmaking. **lumalinux no toca
ninguno de estos.** Los dos conjuntos son **disjuntos por diseño**, y ahora está
verificado leyendo el código, no asumido.

Esto es la prueba concreta de la regla que ya aparecía en los otros dos docs:
lumalinux **no puede hookear la capa de mensajes/dispatch** (SLSsteam la posee
vía `CProtoBufMsgBase`), así que usa **seams de capa-de-función** (los cinco
puntos de arriba) que SLSsteam deliberadamente no pisa. Aquí está el porqué, con
nombres de función a ambos lados.

Y no es teoría: lumalinux **ya retiró** un hook por esta razón. El changelog de
`main.cpp:21-22` (v0.5.6) lo dice literal: *"Packet/858 hook REMOVED: SLSsteam
already covers ownership (CheckAppOwnership) and PICS access tokens. Nothing
extra."* Es decir, lumalinux tuvo en algún momento un hook de ownership/PICS,
detectó que solapaba con lo que SLSsteam ya hace (`CheckAppOwnership` +
`sendPICSInfoRequest`, justo las funciones de la tabla de features §2), y lo
**quitó a propósito para no double-wrappear**. La frontera de coexistencia no es
un ideal de diseño: es una decisión ya tomada y registrada en el código.

**Re-verificado contra `20260705132808` (`5c632dd`).** Esa release añade dos
hooks nuevos —`CAppDataCache::BParseResponseMessage` (recoge appids de las
respuestas PICS) y `IClientAppManager::GetAppStateInfo` (nuevo mecanismo de
bloqueo de updates, limpia los flags `APPSTATE_UPDATE_*` en sitio)— y reescribe
el hot-reload para añadir/quitar apps en vivo posteando callbacks
`AppLicensesChanged_t` (su equivalente al `MarkLicenseAsChanged` de OST,
`opensteamtool-findings.md`). **Los dos hooks nuevos siguen en la capa
appdata/ownership de SLSsteam; ninguno roza tus cinco puntos.** La disjunción de
la tabla de arriba se mantiene 11 días y una release después. Detalle §7.

**Coexistencia a tres niveles, resumida:**

1. **Loader** — SLSsteam por `LD_AUDIT`, lumalinux por `LD_PRELOAD` (§0.1). No
   compiten por el mecanismo de carga.
2. **Superficie de hooks** — conjuntos disjuntos (tabla de arriba). No
   double-wrappean ni reordenan la misma vtable.
3. **Gate de versión** — el SafeMode de SLSsteam decide si el stack arranca
   (§3); lumalinux vive dentro de esa ventana saneada.

---

## 6. Qué es realmente prestable — lista accionable

Ordenado por valor para lumalinux (la capa de depots), no por vistosidad:

1. **`fixPICThunkCall` / reubicación de thunks PIC** (§1.3) — **el único con
   riesgo de corrección real.** Verificar si `lmhook` ya repara los calls
   relativos / thunks `get_pc_thunk` al construir trampolines de detours inline.
   Si no, es un bug latente en DepotKey/BuildDep/GMRC. **Candidato a issue de
   investigación.**
2. **Captura de instancia viva vía RunIPCFrame** (§1.6) — técnica de respaldo
   documentada para resolver una vtable que no aparezca estáticamente en
   `.data.rel.ro`. Complementa el RTTI estático de §15. No urge; anotar.
3. **VFTHook por índice de slot** (§1.5) — confirma que la dirección de §15.4
   (más hooks a resolución por slot) es sólida, y que el sabor no-intrusivo
   (detour-en-slot-resuelto, no swap de vtable) es el correcto bajo coexistencia.
4. **`PrologueUpwards`** (§1.2) — anclaje alternativo (firma en cuerpo + walk-up
   al prólogo) para cualquier pattern que dependa de bytes de prólogo.
5. **Guardas do-no-harm** (§2.2) — principio de excluir explícitamente rangos/casos
   donde intervenir hace daño. Barato, sano.
6. **Hot-reload inotify + API local** (§4) — ergonomía, baja prioridad.

Nada de esto es urgente ni cambia la arquitectura. El item 1 es el único que
podría ser un bug; el resto son confirmaciones de rumbo (RTTI/slot) y
herramientas de cinturón. El entregable real de este análisis es la **§5**: la
frontera de coexistencia, ahora verificada función por función.

---

## 7. Deltas — releases 20260705 (`5c632dd` / `da97d11`)

El análisis se hizo sobre `ebfb079` (2026-06-24). Once días después salieron dos
releases; el diff de `src/` es modesto (~22 ficheros, ~450 líneas) y **no cambia
ninguna conclusión** — la frontera sigue disjunta. Aquí los cambios, con su
lectura para lumalinux.

**`20260705144737` (`da97d11`) — la pequeña.** Cero código: sólo bumpea
`VERSION` (20260624075231 → 20260705144737) en `version.hpp` y actualiza
`res/updates.yaml`. En la release grande se olvidaron de subir la versión que usa
el SafeMode, así que el gate de hash habría rechazado su propio build; esto lo
arregla. Confirma en vivo el punto §3: **el SafeMode es un gate de versión, y un
mismatch te bloquea el build** — incluso a ellos mismos.

**`20260705132808` (`5c632dd`) — la de features:**

1. **Patterns wildcardeados** (`patterns.cpp`) — cambiaron un byte del hash de
   interfaz en los `cmp eax, <hash>` de varios `RunIPCFrame`
   (`3D 37 9C 88 A6` → `3D ? 9C 88 A6`), reanclaron dos patterns enteros
   (IClientAppManager / IClientUtils RunIPCFrame) y añadieron wildcards a
   RequiresLegacyCDKey. → **La cinta de correr del byte-pattern en directo:** 11
   días y ya reparando firmas por un update de Steam. Es justo el coste que el
   RTTI de §15 elimina para DepotKey. Munición para el rumbo RTTI (§6).
2. **Update-blocking reescrito** (`GetAppStateInfo`) — retiraron el VFThook de
   `GetUpdateInfo` (devolvía `false`, una sola ruta) por un detour de
   `IClientAppManager::GetAppStateInfo` que **limpia los flags `APPSTATE_UPDATE_*`
   en la struct** (`&= ~APPSTATE_UPDATE_RUNNING`…). Mutar estado en sitio en vez
   de mentir en el retorno → funciona para todos los juegos. Técnica anotada,
   capa de SLSsteam. **[FRONTERA]**
3. **Add/remove en caliente de AdditionalApps** — antes sólo añadías; ahora
   añades y quitas sin reiniciar. La config calcula deltas `newApps`/`removedApps`
   bajo mutex (`config.cpp`), y en cada frame IPC (`Apps::runIPCFrame`, disparado
   desde `hkClientUtils_RunIPCFrame`) **postea callbacks `AppLicensesChanged_t`**
   al objeto user (`postCallback`) + re-pide appinfo. Fuerza a Steam a
   **re-indexar propiedad en vivo**. Es la versión propia de SLSsteam del
   `MarkLicenseAsChanged` de OST. Habilitado por el hook nuevo
   `CAppDataCache::BParseResponseMessage`. **[FRONTERA]** — capa de ownership,
   disjunta de lumalinux.
4. **Fix de race condition** — el disparador del re-index pasó de
   `CProtoBufMsg::InitFromPacket` (a veces demasiado pronto) al tick estable de
   `RunIPCFrame`. Sólo timing.
5. **File-watching arreglado** — corrige §4.1: la variante "vigila el fichero con
   `IN_MODIFY`" (que documenté como prestable) **está rota** con editores de
   rename atómico; el fix es **directorio padre + `IN_CLOSE_WRITE` + filtro por
   nombre + leer el buffer entero**. Más: recursión infinita en la API arreglada
   (reabrir el stream disparaba su propio evento), fix de creación del fichero de
   API, y comando `uninstall|appid` nuevo. **[PRESTABLE]** — copiar la versión
   correcta.
6. **schema-grabber** (`tools/`) — herramienta nueva que reemplaza a SLScheevo
   (dejó de funcionar). Saca schemas de stats para **logros offline**; modo batch
   que lee `loginusers.vdf` / `libraryfolders.vdf` para todos los usuarios/apps.
   → Territorio de **LumaDeck** (achievements), no de la capa de depots de
   lumalinux.

**Observación de paso (bug suyo):** en `Hooks::remove()` el `CAppDataCache` llama
a `.place()` en vez de `.remove()` (`hooks.cpp`, copy-paste). Inofensivo en la
práctica —`remove()` casi no se usa— pero es un bug real de SLSsteam, no nuestro.

**Neto:** disjunción intacta, §4.1 corregido, y una confirmación más de que el
byte-pattern es una cinta de correr (punto 1) que el RTTI nos ahorra.

### 7.1 El bloqueo de updates, resuelto — `sls_update_unblock` (2026-07)

> **⚠️ SUPERSEDED por §7.6 (v0.16.18, 2026-07-23).** Esta sección describe el parche
> `sls_update_unblock` contra el mecanismo de bloqueo del **20260705**. SLSsteam lo
> **revirtió** en `20260714131044` (vuelta al hook de `GetUpdateInfo` gated por
> `DisableUpdates`), el parche quedó código muerto y se **retiró**. La contramedida
> actual es el flag de config `DisableUpdates: no` que escribe LumaDeck. Ver §7.6.
> Lo de abajo se conserva como registro histórico.

El punto 2 de arriba (el detour de `GetAppStateInfo` que limpia los flags
`APPSTATE_UPDATE_*`) rompe una feature concreta de LumaDeck: los juegos que
gestiona van a `AdditionalApps`, y `shouldDisableUpdates(appId) =
isAddedAppId(appId) || !isSubscribed(appId)` es **permanentemente true** para
cualquier appid de `AdditionalApps` (primera cláusula). Resultado: Steam deja de
auto-actualizar esos juegos — muestra "Update required" pero no baja nada solo.
No hay toggle de config para desactivarlo, y todos los workarounds de config
tienen un efecto secundario peor (`UseWhitelist: yes` es **global** y rompe
unlock/DLC de todo lo no-whitelisteado — el caso thecatantirat de Cuphead;
`PlayNotOwnedGames` en bloque llena la library y ni siquiera saca el juego del
trigger). Ver issue #20.

**La solución no toca el config: lumalinux parchea en caliente esa única
instrucción de SLSsteam.** SLSsteam se compila `-flto=auto -O3`, así que los seis
`state &= ~APPSTATE_UPDATE_*` se colapsan en un solo
`and dword [reg+disp], 0xFFFFF8E5` (`0xFFFFF8E5 = ~0x71A =
~(REQUIRED|QUEUED|OPTIONAL|RUNNING|PAUSED|STARTED)`). El módulo
`src/sls_update_unblock.cpp` localiza esa instrucción en el `SLSsteam.so`
mapeado y voltea el inmediato a `0xFFFFFFFF`: `AND` con todo-unos es un no-op →
los flags sobreviven → Steam auto-actualiza como con un juego poseído. Ancla en
el inmediato `E5 F8 FF FF` (ABI-estable: los valores del enum los fijan los
juegos, así que la constante sobrevive recompilaciones de SLSsteam — esquiva la
cinta de correr del punto 1 anclando en un valor que la ABI congela); exige el
prefijo `81 /4` para distinguir la instrucción de los seis desplazamientos de
jump/call que coinciden por casualidad; fail-safe: exige **exactamente 1** hit o
no parchea. Nada más de SLSsteam se toca: ownership, DLC, keys, tokens, todo
igual; la única conducta retirada es el borrado de flags.

Es la **primera vez que lumalinux cruza la frontera** de §5 y modifica a SLSsteam
en vez de sólo coexistir — de forma quirúrgica (4 bytes, reversible) y con
degradación segura: si el ancla no aparece exactamente una vez, no parchea y el
auto-update cae a manual, nunca crashea.

**Validado end-to-end (2026-07-07, codespace limpio, SLSsteam `20260705132808`).**
Balatro (2379780) en `AdditionalApps`, instalado pinneado a un manifest viejo
(`3742336026811834465`) vía `steamidra_lite --pin`, luego `--unpin`. Con el parche
puesto (`SLS-unblock: patched … -> and reg,0xFFFFFFFF`), al reiniciar Steam el
juego pasó **solo** `Update Required → Update Queued → Update Running` y bajó el
gid actual (`3512319404653808464`) sin tocar nada — justo lo que el bloqueo
mataba. Coexistió con SLSsteam sin pisarse (hooks disjuntos, §5). Receta en
`docs/update-testing.md`; detalle técnico del ancla y de los fail-safes en
`RESEARCH.md` §16.

### 7.2 Gap conocido de vanilla — CD-key legacy en added apps (visto vía moon `3acab45`)

Anotado desde el análisis de los commits de julio de `swwayps/slsteam-moon`
(fork de SLSsteam). moon `3acab45` (2026-07-01) arregla un agujero que **vanilla
sigue teniendo** (ebfb079). Asimetría en `apps.cpp`:

```cpp
bool Apps::shouldDisableCDKey(uint32_t appId) {
    return !isSubscribed(appId);                       // vanilla: SOLO !isSubscribed
}
bool Apps::shouldDisableUpdates(uint32_t appId) {
    return isAddedAppId(appId) || !isSubscribed(appId);   // este sí mira isAddedAppId
}
```

Un added app, por el hook de ownership, **lee como subscribed=true**, así que
`!isSubscribed=false` → `shouldDisableCDKey` devuelve false → vanilla **no** le
quita el gate de CD-key. moon lo corrige añadiendo `isAddedAppId(appId) ||
isAddedAppDlcId(appId)` a `shouldDisableCDKey`, zereando el out-param, y
neutralizando el campo `extended/hadthirdpartycdkey` del appinfo offline
(`neutralizeLegacyCdKey`) para saltar el paso "GettingLegacyKey" del arranque.

- **Síntoma si nos pasa:** un juego **añadido vía LumaDeck** (AdditionalApps) que
  lleve **CD-key de terceros legacy** (campo `hadthirdpartycdkey`, DRM de juegos
  viejos) se cuelga al lanzar pidiendo la clave. Los juegos modernos no lo llevan
  → riesgo real pero de nicho.
- **Capa:** SLSsteam (hook `RequiresLegacyCDKey`), no lumalinux. No lo arreglamos
  desde la capa de depots. Si algún día muerde, la opción sería el mismo
  runtime-patch que ya usamos para el update-clear (§7.1) — pero sería
  sobre-ingeniería preventiva mientras no veamos el síntoma.
- **Estado:** solo anotado, no accionado. Si aparece, la causa y el fix de
  referencia (moon `3acab45`) ya están aquí.

> ⚠️ **CORRECCIÓN + ACTUALIZACIÓN (2026-08-12, `79905b0`).** Dos cosas, y la primera
> es un error de esta sección.
>
> **1. La premisa de arriba es falsa.** Escribí que "un added app, por el hook de
> ownership, lee como `subscribed=true`". **No es así**, y no lo ha sido nunca en la
> ventana analizada. El helper interno de SLSsteam llama al **trampolín**, no a su
> propio hook:
>
> ```cpp
> // src/sdk/CUser.cpp — idéntico en ebfb079, 69594b9 y HEAD
> bool CUser::checkAppOwnership(const AppId_t appId, AppOwnershipInfo_t* pInfo) {
>     return Hooks::CUser_CheckAppOwnership.tramp.fn(this, appId, pInfo);   // ← tramp
> }
> bool CUser::isSubscribed(const AppId_t appId) {
>     AppOwnershipInfo_t info {};
>     if (!checkAppOwnership(appId, &info)) return false;
>     return info.ownsLicense && !info.licenseExpired;
> }
> ```
>
> O sea: `isSubscribed()` **ve la propiedad REAL**, deliberadamente, porque va por el
> trampolín y se salta el falseo de `hkUser_CheckAppOwnership`. Para un added app
> (juego de LumaDeck, no poseído de verdad) devuelve **false**. Por tanto
> `shouldDisableCDKey() → !isSubscribed → true` → **vanilla sí desactivaba el gate**.
> La asimetría `shouldDisableCDKey` vs `shouldDisableUpdates` es real a nivel de
> código, pero **no producía el síntoma que le atribuí**. Lo que moon `3acab45`
> aportaba de verdad era la otra mitad: zerear el out-param y **neutralizar
> `extended/hadthirdpartycdkey`** en el appinfo offline para saltarse el paso
> "GettingLegacyKey".
>
> *(Este es un patrón a recordar al leer SLSsteam: `CUser::*` en `src/sdk/` son
> wrappers que llaman al **tramp**, no a la versión hookeada. Cualquier razonamiento
> del tipo "aquí SLSsteam se ve su propio falseo" es sospechoso por defecto.)*
>
> **2. Vanilla ahora ataca el problema por el lado opuesto: suministra la clave.**
> Nueva opción de config `CDKeys` + hook nuevo sobre `IClientUser::GetLegacyCDKey`:
>
> ```cpp
> static bool hkClientUser_GetLegacyCDKey(IClientUser* p, AppId_t appId, char* k, uint32_t n) {
>     Apps::getLegacyCDKey(appId);                       // inyecta ANTES de la original
>     return Hooks::IClientUser_GetLegacyCDKey.originalFn.fn(p, appId, k, n);
> }
> ```
>
> `Apps::getLegacyCDKey` sale temprano si `isSubscribed` (juegos poseídos intactos), y
> si no: coge la clave de `CDKeys` en el config o **genera una determinista** — 4
> segmentos de 4 chars `A-Z0-9`, `srand(accountId + appId)`, formato `XXXX-XXXX-XXXX-XXXX`
> — y la fija con `clientUser->setLegacyCDKey()`. Comentario suyo: inyectarla sólo en
> `GetLegacyCDKey` no funcionaba bien, hay que **setearla**. Se borra quitando
> `cdk_[n]` del `localconfig.vdf`. El config no loguea las claves (flag `silent` nuevo
> en `getSetting`/`getList`/`getMap`; sólo `"Added CDKey for %u"`).
>
> Razón que da en el commit: *"antes SLSsteam simplemente desactivaba las CD keys
> porque era la mejor opción teniendo en cuenta que no teníamos decompilador y la VFT
> de `IClientUser` cambiaba mucho"* — o sea, **el trabajo de julio (§7.7) es lo que
> hizo viable la feature**.
>
> **Neto para LumaDeck:** los juegos con CD-key legacy de terceros pasan de "gate
> desactivado" a "clave suministrada", lo cual cubre además los que **exigen** una
> clave con forma válida en vez de conformarse con que no se les pida. Cubre los added
> apps porque `isSubscribed` lee propiedad real (punto 1). **Sin trabajo por nuestra
> parte.** Sigue siendo nicho (DRM de juegos viejos), y sigue sin verificarse
> on-device — pero ya no es un "gap conocido de vanilla", es una feature de vanilla.

### 7.3 Logros nativos — el "borrow" de schema y su scoping (`sls_achievement_unblock`)

Las releases de julio de SLSsteam añadieron **soporte de logros nativos**: cuando
un juego pide sus stats a Steam y la cuenta **no** posee el app, SLSsteam toma
prestado un *schema* de logros de un dueño real que localiza vía las **reviews
recientes** del juego. Dos entradas lo implementan —
`Achievements::sendAndRecvGetUserStats` (legacy `CMsgClientGetUserStats`) y
`Achievements::sendAndRecvGetPlayerStats` (unificada `Player.GetUserStats#1`) — y
ambas abren con el mismo guard:

    if (g_pSteamEngine->getUser(0)->isSubscribed(appId)) return;   // "lo posees → no tomes prestado"

Flujo del borrow (`feats/achievements.cpp`): `getReviewersForGame(appId)` hace un
`Curl::getString` a `store.steampowered.com/appreviews/<appId>?…&num_per_page=
<MaxSchemaTries>`, saca los `steamid` de los reviewers; para cada uno hace
`set_steam_id_for_user(id)` sobre el protobuf de la petición, reenvía por el
**trampolín** (el original, no el hook), obtiene el schema, **limpia**
`achievement_blocks`/`crc_stats`/`stats` y devuelve un schema con progreso a cero
(Steam lo mergea con lo local). Un `ownerBlacklist` global cachea ids que fallan.
Dos detalles clave para nosotros:

- `set_steam_id_for_user` es un **setter de campo del protobuf** de esa petición
  concreta — **no** cambia el SteamID de sesión ni ningún estado global de
  steamclient. No hay swap de identidad ni recursión (el reenvío usa el trampolín).
- El borrow mete un `fork`+`curl` externo **bloqueante** + hasta `MaxSchemaTries`
  RPCs de red secuenciales en el hilo de la petición, y el `ownerBlacklist` es un
  `unordered_map` **sin lock** compartido por las dos entradas. Son hazards de
  SLSsteam que sólo importan cuando el borrow **corre** — y nuestro parche
  ensancha la población de apps que lo alcanzan.

  > **Actualización 2026-08-04 (`3b97ac2`), ver §7.7.10.** Este hazard se ha movido en
  > las dos direcciones. **A mejor:** el `fork`+`curl` ya no corre por cada petición —
  > SLSsteam recuerda el dueño que funcionó (`preferredOwners[appId]`) y se salta el
  > fetch de reviews entero, lo que era exactamente la causa de los stutters en juegos
  > que espamean `GetUserStats`. Como nuestro parche es lo que mete a los juegos de
  > LumaDeck en esta ruta, la mejora nos beneficia de forma desproporcionada.
  > **A peor:** el arreglo introduce un **segundo `unordered_map` sin lock**
  > (`preferredOwners`), alcanzable desde las dos entradas igual que `ownerBlacklist`, y
  > con patrones read-modify-write (`contains` → `at` → `erase`). Comprobado a HEAD:
  > **cero mutex en todo `achievements.cpp`**. O sea: baja la *frecuencia* con la que se
  > pisa el hazard, no lo sincroniza. La nota de arriba debe leerse ahora como **dos**
  > mapas sin lock, no uno.

**Por qué los juegos de LumaDeck no tenían logros por defecto.** LumaDeck instala
vía descarga **nativa** de Steam, que escribe una **licencia local real**
(`config.vdf` DecryptionKeys/AppTokens + `.acf`). Así `isSubscribed(appId)`
devuelve **true** para ellos → el guard salta el borrow → sin logros. (Una
herramienta tipo DepotDownloader que se auto-descarga sin licencia local →
`isSubscribed=false` → el borrow corre → logros. El diferencial de lumalinux es
justo la vía nativa, y por eso pierde el borrow.)

**Nuestro parche (scoping, no NOP).** Un NOP a secas del `jne` correría el borrow
también para juegos **realmente poseídos** — secuestrando tu petición real con la
de un reviewer y **borrando tus logros desbloqueados**. Así que el guard debe
seguir para las compras reales y sólo levantarse para juegos LumaDeck. El
distintivo: los juegos LumaDeck son los que SLSsteam tiene en `AdditionalApps`
(`isAddedAppId == true`). `sls_achievement_unblock` repunta en caliente las dos
llamadas `call isSubscribed` a un guard propio que devuelve
`isSubscribed && !isAddedAppId`:

- `subscribed && !added` → skip borrow (compra real, intacta)
- `subscribed &&  added` → run borrow  (juego LumaDeck → logros nativos)
- `!subscribed`          → run borrow  (igual que vanilla)

Es la **segunda vez que lumalinux cruza la frontera** de §5 y parchea SLSsteam en
memoria (tras §7.1), con la misma disciplina fail-closed. **El detalle técnico del
parche (ABI cdecl, resolución de símbolos, los fail-safes) y el postmortem del
OOBE que provocó — una escritura no atómica del `rel32` que dejó a un hilo de Steam
leer un destino a medias → `SIGILL` → wipe de gamescope → arreglado con un
`WriteRel32` atómico — están en `RESEARCH.md` §17.**

### 7.4 Delta 2026-07-11 (VERSION `20260710192125`, HEAD `d35a697`)

Repaso semanal de `AceSLS/SLSsteam` desde `da97d11` (20260705, base de §7). Cambios que nos tocan:

- **`PlayNotOwnedGames` y `AutomaticFilterList` eliminados** (`84c3672`, 07-07). SLSsteam quitó ambas opciones por completo (config, parsing y uso en `apps.cpp`): `AdditionalApps` es ya el único mecanismo de unlock. Impacto en LumaDeck: tenía lógica de `_set_playnotowned_no` que devolvía un falso "reinstall dependencies" cuando la línea faltaba. Corregido (2026-07): "línea ausente" se trata como no-op OK, y se mantiene el flip para usuarios en SLSsteam antiguo (donde Headcrab aún fuerza `yes`). El default que siembra LumaDeck todavía escribe la clave (inerte, SLSsteam la ignora).
- **curl in-process reemplazado por `curl` externo** (`f62d97c`, 07-11). SLSsteam pasó de libcurl in-process a `fork`+`execve("/bin/curl")` porque en SteamOS `libssl.3.so` **crashea** al curlear ciertas URLs (certs rotos, causa poco clara). **Riesgo de la misma clase para lumalinux:** usamos libcurl in-process (`curl.cpp` dlopen, `gmrc_store`, `update.cpp`) contra HTTPS (`raw.githubusercontent.com/.../updates.yaml` al arrancar, `manifest.opensteamtool.com`, `manifest.steam.run`). No observado nunca con nuestras URLs, y el fix de SLSsteam (curl externo) es pesado, así que **solo vigilado, no accionado**. Si aparece un crash de arranque correlado con el fetch de `updates.yaml`, esta es la causa.
- **Evolución del borrow de logros** (refina §7.3): optimizado con **protobufs** en vez de scrapear el HTML del perfil (`4f3e607`), **blacklist de perfiles fallidos por AppId** para acelerar cargas posteriores (`0d6b3a8`), `MaxSchemaTries` configurable (default 10; `0` = solo caché offline) (`4ab84f7`, `c063fb1`), opción de auto-update de schema (`6e8d357`), y refactor + checks en `achievements.cpp` (`c12234f`, `aa316c4`), que es nuestro objetivo de hook. Validamos `sls_achievement_unblock` on-device contra `d35a697`/20260711, así que los anclajes (símbolos + patrón del guard) **casan hoy**; un refactor futuro podría moverlos y el fail-safe lo cubre.
- **SafeMode**: bumps de `res/updates.yaml` / `res/version.txt` (nuevos hashes de `steamclient.so`). Sin impacto salvo el co-gating de §3.

### 7.5 Delta 2026-07-23 (VERSION `20260723102618`, HEAD `69594b9`) — el decompilador

Cambio arquitectónico **gordo** desde §7.4. Verificado en el fuente (`raw.githubusercontent`
@ `20260722152506`), no solo en las notas de release.

- **★★ Añaden un DECOMPILADOR que reemplaza patterns y VFT-indexes hardcodeados**
  (`2903ef8`, release `20260722152506`, 07-22). *"Add decompiler to replace lots of
  patterns and hardcoded VFT Indexes, and hooks (Steamclient may take a few seconds
  longer to start now). Huge refactors."* Nuevos ficheros `src/decompiler.{cpp,hpp}`
  + `src/vftableinfo.{cpp,hpp}`. Qué hace (leído en `decompiler.hpp`/`.cpp`):
  - `collectVFTables` **localiza las VFTables por el string RTTI del `typeInfo`**
    (recorre `.data.rel.ro`, cruza typeinfos→vtables) — **la misma primitiva que el
    `.so` de CloudRedirect en Linux y que nuestro `getTypeName` de §1.4/§15**.
  - `parseInterfaceMapBase(interface)` → `map<string,unsigned>` que **resuelve los
    índices de slot del vtable dinámicamente**, en vez de la tabla de VFT-indexes
    hardcodeada que documentamos en §1.5 (`VFTHook`).
  - `parseFunction`/`__parseFunction` caminan instrucciones (PIC thunks, `lea`
    offsets, branches) vía `libmem` — análisis de código ligero, no pattern-match.

  **Esto es exactamente la dirección que marcamos en §1.5 ("VFTHook + DetourHook…
  la dirección de §15.4"), ahora automatizada por SLSsteam.** Y es la **tercera
  convergencia** al mismo modelo: CloudRedirect (RVA→resolver), lumalinux (RTTI
  puro §1.4/§15) y ahora SLSsteam (decompilador). El ecosistema entero está
  abandonando offsets/índices/patterns hardcodeados por resolución dinámica RTTI.

- **El rollout fue accidentado** (como el de CR 2.6.2): el refactor del
  decompilador rompió cosas y hubo tres parches rápidos en ~18h:
  - `fa8523f` (`20260722175657`): *"Workaround SteamOS getting stuck when loading
    latest SLSsteam"* — el arranque del decompilador colgaba SteamOS.
  - `4dd592c` (`20260723094105`): *"Fix decompiler branching decisions. DLC unlocks
    should work properly again and crashes should be gone. Use real appId in more
    interfaces than before for FakeAppIds"* — el seguimiento de branches del
    decompilador rompió los unlocks de DLC y causaba crashes.
  - `69594b9` (`20260723102618`, LATEST): *"Fix crash for apps that have an empty
    parent."*

- **Config nueva (07-14, `c69d502`, release `20260714131044`)** — relevante para el
  eje de updates:
  - **`ManifestIds`**: descargar versiones viejas + **bloquear updates en juegos
    OWNED** (el `update-unblock` de §7.1 solo cubría unowned; esto cierra el hueco
    de owned). Es el equivalente al `--pin` de moon (method.md §6).
  - **`DepotBlacklist`**: bloquear que Steam baje ciertos depots (arregla p.ej. el
    download de Saints Row IV en Proton).
  - **Rework de `AppIds`**: ahora tiene en cuenta el **parent AppId** → puedes
    black/whitelistear un juego entero por su AppId.
  - Revierten el bloqueo de updates al método previo (solo unowned; owned →
    `ManifestIds`). Fixes de shortcuts non-Steam y playtime de refunds.
- **07-15 (`7d62c68`, `20260715200441`)**: fix de recursión infinita en exclusiones
  (parent que referencia a su propio hijo como parent), **mirror jsdelivr para
  `res/updates.yaml`** (segunda fuente del fichero que LumaDeck text-scanea), y
  `curl --connect-timeout 15`.

**Qué significa para nosotros:**
1. **Valida nuestra dirección RTTI** (§1.4/§15) y la de §1.5 — SLSsteam llegó al
   mismo sitio.
2. **SLSsteam se vuelve más robusto a builds de Steam** (resuelve dinámicamente),
   igual que CR. Su `SafeModeHashes`/`version.txt` **sigue existiendo** (el gate de
   "puedo hookear esta `steamclient.so`" + lo que lee su updater — `update.cpp`
   depende de `version.txt`, Makefile), pero el *hooking* de debajo ya no depende de
   VFT-indexes fijos. Para el gate de LumaDeck **no cambia nada**: seguimos
   consumiendo el `updates.yaml` de AceSLS como veredicto (decisión del eje-1), y el
   group-id bumpeó a `20260722152506` con este refactor (co-gating §3).
3. **Vigilancia**: el decompilador es nuevo y su rollout fue buggy (SteamOS colgado,
   DLC roto, crashes). Si un usuario en SLSsteam muy reciente reporta cuelgue de
   arranque o DLC que dejó de desbloquear, esta migración es la causa candidata.

#### 7.5.1 El decompilador por dentro — desglose verificado en el fuente

Leído completo en `src/decompiler.{cpp,hpp}` y `src/vftableinfo.{cpp,hpp}` @ tag
`20260722152506`. No es un "decompilador" en el sentido clásico: es un **mini-motor
de análisis estático que corre en runtime, al cargar**, cuyo único trabajo es
responder una pregunta que antes estaba hardcodeada — *"¿en qué slot del vtable está
`BLoggedOn`?"*. Antes ese número vivía fijo en el código (`VFTHook(idx=42)`); ahora
lo **deriva solo en cada arranque**. Eso es lo que añade los "few seconds longer to
start" de las notas.

**El pipeline, 6 etapas:**

1. **`parseHeader`** — abre el `steamclient.so` **desde disco con `fopen(mod.path,"r")`,
   no desde memoria**. Deliberado: leer de memoria daría un binario ya
   parcheado/hookeado. Parsea el ELF header (`Elf32_Ehdr`), los section headers en
   `e_shoff`, y guarda cada sección en un map estático clave `"module::section"`
   (`.rodata`, `.data.rel.ro`, string table).

2. **`collectStrings`** — recorre `.rodata`/`.rodata.str` byte a byte con `getString`
   (`std::isprint` hasta el null), guarda las strings imprimibles de ≥`MIN_STRING_SIZE`
   (5) chars indexadas por dirección. Son las **etiquetas** que se usan en el paso 5.

3. **`collectVFTables` — localización por RTTI, dos pasadas** *(la convergencia con
   nosotros)*:
   - **Pasada 1** sobre `.data.rel.ro` alineado a puntero: un puntero que apunta a un
     string conocido = un `TypeInfo`. El nombre mangled es literal
     (`30CClientUnifiedServiceTransport`, `14IClientUserMap`, …). El `TypeInfo`
     queda en `addr - sizeof(ptr)`.
   - **Pasada 2**: un puntero que apunta a un `TypeInfo` conocido = un vtable. Layout
     Itanium C++ ABI: `[offset-to-top][typeinfo][fn0][fn1]…`; el vtable está en
     `addr - sizeof(ptr)`. `VFTable::init` guarda dirección + puntero a typeInfo.
   - Es **exactamente** `getTypeName` de lumalinux (§1.4/§15) y el `sc_resolver`
     de CloudRedirect en Linux: RTTI typeinfo string → vtable.

4. **`VFTable::analzye`** *(sic, typo del autor)* — lee punteros a función desde
   `address + sizeof(lm_address_t)*2` (salta offset-to-top y typeinfo) hasta un
   puntero null; cada uno se guarda como `offset + moduleBase`.

5. **`parseInterfaceMapBase(interface)` — el truco de verdad**. Localizar el vtable
   no basta: hace falta **qué slot es qué función**. Aquí está lo que SLSsteam hace y
   nosotros no: por cada slot desensambla la función (`parseFunction`) y mira **qué
   string conocido referencia**. Los métodos del client de Steam **referencian su
   propio nombre como string** (logging/asserts). Así, si el slot `i` referencia
   `"BLoggedOn"` → slot `i` = `BLoggedOn`. Devuelve `map<functionName, slotIndex>`.

6. **`__parseFunction` — el walker de instrucciones** (vía `libmem` `LM_Disassemble`):
   - Sigue saltos: `jmp` → continúa en el target; condicional → recursión; trackea
     `branchesTaken` (`unordered_set`) para no colgarse en loops; para en `ret`.
   - Maneja **PIC thunks** (x86 32-bit): para leer strings PIC el compilador emite
     `call __x86.get_pc_thunk.bx; add ebx, off`. `isPICThunk` detecta el patrón
     `call`→(`mov reg,[esp]`;`ret`), `getLeaOffset` reconstruye la base del GOT, y un
     `lea reg,[ebx+off]` posterior se resuelve a un string conocido (cuenta la
     referencia en el map `references`). — Es el mismo problema de thunks PIC que
     documentamos en §1.3 (`fixPICThunkCall`), aquí resuelto para *leer* código en vez
     de para *reparar* el trampolín.

**Lo que resuelve así** — `vftableinfo.cpp` pide un índice solo cuando vale
`NO_INDEX` (`0xFFFFFFFF`), cacheando por typeName en `tableMap`:
- **9 interfaces**: `CCMInterface`, `CClientUnifiedServiceTransport`,
  `CSteamMatchmakingServers`, `IClientApps`, `IClientAppManager`,
  `IClientRemoteStorage`, `IClientUtils`, `IClientUser`, `IClientEngine`.
- **~27 funciones** localizadas por nombre en vez de por índice fijo, entre ellas las
  que nos tocan de cerca:
  - *Transport*: `RecvPkt`, `SendAndRecv` (el mismo hook
    `CClientUnifiedServiceTransport` que ataca CloudRedirect en Linux).
  - *Ownership/licensing*: `BLoggedOn`, `BUpdateAppOwnershipTicket`,
    `GetAppOwnershipTicketExtendedData`, `IsUserSubscribedAppInTicket`,
    `RequiresLegacyCDKey`.
  - *App manager (update-blocking)*: `GetUpdateInfo`, `InstallApp`, `UninstallApp`,
    `GetAppInstallState`.
  - *DLC*: `GetDLCCount`, `BGetDLCDataByIndex`, `BIsDlcEnabled`.

**Por qué esto explica los tres hotfixes.** El punto frágil es el paso 6.
`20260723094105` fue literalmente *"fix decompiler branching decisions"*: si el
walker sigue mal una rama, o pierde el string del nombre (→ slot equivocado o
`NO_INDEX`) o lee basura. De ahí DLC roto, crashes, y el cuelgue de SteamOS al cargar
— todo es este análisis corriendo **en la máquina de cada usuario, en cada arranque**.

**La diferencia de reparto de riesgo con lumalinux.** Resolvemos el vtable por RTTI
**igual**, pero el **índice de slot lo llevamos precomputado en `patterns.hpp`**, y el
**CI lo verifica contra el `steamclient.so` real antes de anunciar la build** en
`updates.yaml` (co-gating §3, blindaje del fleco). SLSsteam lo deriva **en vivo, sin
gate de CI** → cualquier fallo del walker le llega directo al usuario. Misma dirección
técnica (RTTI), reparto de riesgo opuesto: ellos migraron de golpe y pagaron 3
hotfixes en 24h; nosotros lo hicimos incremental y con verificación previa.

### 7.6 El bloqueo de updates cambió de mecanismo — SLS-unblock retirado (v0.16.18)

**Hallazgo (2026-07-23, verificado clonando `AceSLS/SLSsteam@20260723102618`).**
El release `20260714131044` incluye *"Revert back to the previous method for
blocking updates. Only works for unowned games, for owned ones use the ManifestIds
config option"*. Eso **cambia el mecanismo que atacaba nuestro `sls_update_unblock`**.

**Mecanismo viejo (20260705, el que documenta RESEARCH §16):** un clear inline de
flags — `and dword [reg+off], 0xFFFFF8E5` (`~0x71A`) — que borraba los bits
`APPSTATE_UPDATE_*`. Nuestro `SlsUpdateUnblock::Apply()` lo parcheaba a
`0xFFFFFFFF` (no-op) en memoria.

**Mecanismo nuevo (≥20260714):** un **hook de vtable sobre
`IClientAppManager::GetUpdateInfo`** que devuelve `false` cuando
`Apps::shouldDisableUpdates(appId)` es true (`src/hooks.cpp`, `src/feats/apps.cpp`):

```cpp
// hooks.cpp
if (Apps::shouldDisableUpdates(appId)) return false;   // -> Steam ve "sin update"

// apps.cpp
bool Apps::shouldDisableUpdates(const AppId_t appId) {
    if (!g_config.disableUpdates.get()) return false;              // <-- el config corta de raíz
    return g_config.isAddedAppId(appId) || !g_pSteamEngine->getUser(0)->isSubscribed(appId);
}
```

- `DisableUpdates` está **`yes` por defecto** (`config_default.hpp`, `config.cpp` default `true`).
- Aplica a `isAddedAppId(appId) || !isSubscribed(appId)` → **exactamente los juegos que
  gestiona LumaDeck** (AdditionalApps / unowned).
- El `and …,0xFFFFF8E5` viejo **ya no existe en el binario** (grep de la máscara → cero).

**Consecuencia y decisión:**
1. `sls_update_unblock` quedó **código muerto** en cualquier SLSsteam ≥20260714: su
   ancla desapareció, `Apply()` no-op, y lumalinux reportaba `SlsUpdateUnblock:
   DISABLED` a perpetuidad (señal engañosa). **Retirado en v0.16.18** (borrados
   `src/sls_update_unblock.{cpp,hpp}`, su include, su bloque en `main.cpp` y la
   entrada en `CMakeLists.txt`). RESEARCH §16 se conserva como registro histórico de
   la ingeniería inversa, marcado como SUPERSEDED.
2. **La contramedida real ya existía y vive en LumaDeck, a nivel de config**:
   `ensure_slssteam_flags()` escribe `DisableUpdates: no` en el `config.yaml` de
   SLSsteam. Como el hook nuevo cortocircuita en `if (!disableUpdates) return false`,
   el flag **anula el bloqueo entero** — es ABI-independiente, sobrevive a cada
   recompilación de SLSsteam (incluido el refactor del decompiler §7.5), y SLSsteam
   hot-reloadea el config. Se aplica en dos puntos:
   - post-install (`installer.py`, en el mismo flujo que corre headcrab), y
   - cada arranque del plugin (`main.py`, poll ~60s hasta que SLSsteam crea el config).
3. **No hay ventana de regresión sin re-aplicar**: el único camino que regenera el
   `config.yaml` es una reinstalación (que re-corre headcrab **y** `ensure_slssteam_flags`
   en el mismo paso). SLSsteam no reescribe los settings del usuario una vez creados.

**Lección de método:** el parche binario ataba nuestra funcionalidad a un patrón de
codegen concreto de SLSsteam; el config flag ataca la **superficie soportada y
estable**. Donde exista un knob de config equivalente, preferirlo al parcheo en
memoria — sobrevive a refactors como el del decompiler sin tocar nada.

**El OTRO parche en memoria, `sls_achievement_unblock` (§17), SÍ sigue intacto —
verificado al fuente contra `20260723102618`.** A diferencia del update-unblock (que
murió por un revert intencional del mecanismo), el decompiler tocó cómo SLSsteam
resuelve vtables de *steamclient*, no su lógica propia de achievements:
- Los 5 símbolos que resuelve por `.symtab` persisten con nombres mangled idénticos:
  `_ZN5CUser12isSubscribedEj`, `_ZN7CConfig12isAddedAppIdEj`, `g_config`,
  `Achievements::sendAndRecvGetUserStats`, `...GetPlayerStats` (firmas sin cambio en
  `src/feats/achievements.{cpp,hpp}`, `src/sdk/CUser.hpp`, `src/config.hpp`).
- El guard que ancla `FindGuardCall` (`call isSubscribed; add esp,imm; test al,al;
  jne`) sigue siendo **único por función** (una sola `isSubscribed(...)` early-return
  en cada entry point, L105/L153). Los demás branches usan `test eax,eax` (`85 C0`) o
  `cmp` contra constante, así que no crean un segundo match del `84 C0`.
- `isSubscribed` es método no-inline (definido en su `.cpp`), sin LTO no se inlinea →
  el `call` sobrevive y el símbolo se exporta.

> 🔴 **ESTA VERIFICACIÓN ERA FALSA. Corregido 2026-08-17 (ver §7.7.6).**
> El punto de arriba —*"firmas sin cambio en `src/feats/achievements.{cpp,hpp}`"*— **no era
> cierto cuando se escribió**, y esa falsa verificación es la razón más probable de que la
> rotura pasara desapercibida varios días.
>
> `59f8259` (**2026-07-20**, *"refactor(sdk): Replace handmade EMsgType enum with protoc
> generated EMsg"*) cambió el último parámetro de `Achievements::sendAndRecvGetUserStats`
> de `const uint32_t targetType` a `const EMsg targetType`. Mangled: `…S3_j` → `…S3_4EMsg`.
> Comprobado release a release:
>
> | VERSION | fecha | último parámetro |
> |---|---|---|
> | `20260710192125` | 07-10 | `const uint32_t targetType` |
> | `20260714131044` | 07-14 | `const uint32_t targetType` |
> | **`20260722152506`** | **07-22** | **`const EMsg targetType`** ← primera afectada |
> | `20260723102618` | 07-23 | `const EMsg targetType` ← la que este §7.6 declaró "intacta" |
> | `20260728212859` | 07-28 | `const EMsg targetType` |
>
> **Consecuencias:**
> 1. La ventana de rotura empieza en **`20260722152506`**, no en `20260728212859`. Seis días
>    y **dos releases** más de lo documentado. Si alguna vez se correlacionan reportes de
>    "los logros dejaron de funcionar", el rango es más ancho.
> 2. `RESEARCH.md` §17 y el mensaje de `e3dc918` atribuyen el cambio a "SLSsteam 20260728".
>    Es la release donde se **detectó**, no donde se introdujo.
> 3. No afecta al fix: el matching por prefijo cubre toda la ventana igual.
>
> **Lección de método:** verificar "firmas sin cambio" leyendo el fuente **en el HEAD
> actual** no dice nada sobre si cambiaron *desde la última verificación*. Lo que había que
> comparar es la firma **entre releases**, que es lo que hace la tabla de arriba. Para los
> símbolos que se resuelven por nombre mangled, el chequeo correcto es
> `git log -S"<firma>" -- <fichero>`, no un grep del estado presente.

Lo único no confirmado es la forma de bytes compilada exacta (el `.so` del release da
403 en el proxy; compilarlo no es reproducible), pero no hay razón estructural para
que derive y el patrón deja el `imm8` del `add esp` libre. Confirmación práctica
on-device: el log del parche (`SLS-ach: scoped the native-achievement guard…` vs
`guard pattern not found exactly once`) y `LUMA_SLS_ACH_TRACE=1`.

---

### 7.7 Ventana `20260723102618` → `20260815201341` — barrido día a día

*Método: clon completo de `AceSLS/SLSsteam`, lectura de diffs commit a commit desde
`69594b9` (donde acaba §7.5/§7.6) hasta `01a3b1e`. **205 commits, 2026-07-23 →
2026-08-15**, dos releases (`20260728212859` y `20260815201341`), +43.7k/−25.1k líneas
—de las que ~38k son la regeneración de protobufs del 08-07 (paso de ProtoBuf lite a
completo)—. Los internals del decompilador ya están desglosados en §7.5.1; esta
sección NO los repite, documenta qué se construyó **encima**.*

**Estado: COMPLETO — cerrado el 2026-08-17.** Los 22 días del rango están desglosados a
fondo en §7.7.1 … §7.7.19. El cierre, con el balance de accionables, está en §7.7.20.

#### 7.7.0 Panorama de la ventana — el hilo conductor

Un solo tema explica casi todo: **Ace desmantela el hooking por patrones de bytes y
apoya el proyecto entero sobre el decompilador** (§7.5.1). Consecuencias en cascada,
por orden cronológico:

| Fecha | Hito | Efecto en el doc |
|---|---|---|
| 07-23 | Endurece decompilador + `patternScan` ceñido a `.text` | §7.7.1 |
| 07-24 | **Elimina los 5 hooks `RunIPCFrame`**; navegación estructural `CSteamEngine→CUser→interfaces` | **§1.6 SUPERSEDED** |
| 07-25 | Análisis movido a **antes de las relocaciones**; patrones cortos + prólogo con comodines | pendiente |
| 07-27 | **Borra el trampolín naked de `GetSteamId`**; intercepta el buffer IPC | **§1.7 SUPERSEDED** |
| 07-28 | Release `20260728212859`. `SteamIdOverride`, `FakeName`, refactor masivo de tipos SDK | rotura de `sls_achievement_unblock` → fix por prefijo |
| 08-04 | Arregla stutters del fetch de reviews en logros nativos | mejora §7.3 |
| 08-05 | `RunInterface` → `ProcessIPCFrame` (un nivel más profundo en la capa IPC) | pendiente |
| 08-12 | **`CDKeys`**: suministra la clave en vez de saltar el paso | **§7.2 corregido** |
| 08-14 | API local: `setcompat`/`getcompat`/`dumpcompat`/`dumplibraries` + `API.md` | amplía §4.2 |
| 08-15 | Release `20260815201341`. **`IN_MOVED_TO`** en el filewatcher; `CUtlRBTree` como árbol real | **§4.1 actualizado**, §7.7.19 |

**Regalos gratis para LumaDeck en esta ventana** (nada que implementar): CD-keys legacy
suministradas (§7.2), stutters de logros arreglados, callbacks y lobbies de FakeAppIds
corregidos, fuga de descriptores en curl cerrada, crash del logger durante el apagado
de Steam arreglado.

#### 7.7.1 Día 2026-07-23 — 3 commits (mantenimiento con intención)

**`6725ff1` `chore(PKGBUILDs): Update`** — sube `pkgver` a `20260723102618` en
`pkg/slssteam/PKGBUILD` y `pkg/slssteam-git/PKGBUILD` + nuevo `sha256sums`. Release
plumbing; es literalmente "publicar la versión donde acaba §7.5".

> **[FRONTERA — dependencia oculta de `sls_achievement_unblock`]**
> Ese PKGBUILD lleva:
> ```bash
> #Disable stripping to not mess up disturb ticket-grabber
> options=(!strip)
> ```
> SLSsteam se publica **sin strippear**, con `.symtab` intacta. Y de eso depende
> nuestro §17: `sls_achievement_unblock.cpp` resuelve
> `_ZN12Achievements25sendAndRecvGetPlayerStatsE*` y
> `_ZN12Achievements23sendAndRecvGetUserStatsE*`, que son funciones **internas, no
> exportadas** — no están en `.dynsym`, sólo en `.symtab`. Si SLSsteam se strippeara,
> el parche caería al `no-op` de fail-safe y los logros nativos se apagarían en
> silencio (bueno: degrada limpio, ver §17).
> **Lo relevante es el motivo:** el `!strip` está ahí por el `ticket-grabber`, **no por
> nosotros**. Nuestra feature va montada sobre una decisión ajena que puede revertirse
> sin que nadie piense en LumaDeck. No accionable hoy; sí anotable como riesgo. Si
> aparece `SLS-ach: could not resolve` en un build nuevo, **lo primero a comprobar es
> `nm -a SLSsteam.so | wc -l`** antes de asumir drift de mangling.

**`69d9623` `fix(decompiler): Add retf to abort instructions in function parser`** —
`__parseFunction` desensambla instrucción a instrucción y para al ver un retorno. Sólo
reconocía `ret`/`retn`; añade `retf` (*far return*, `CB`/`CA`, reliquia del modo
segmentado). Si el parser no reconoce el retorno **no para**: se desborda al cuerpo de
la función siguiente y sigue anotando referencias a strings ajenas → el mapa
`nombre→índice de slot` de `parseInterfaceMapBase` sale contaminado → hook en el slot
equivocado. Su propio comentario dice que capstone normaliza todos los `ret`, o sea que
en la práctica es **un no-op defensivo**: coste cero, elimina una clase de bug
diagnosticable sólo a base de sufrimiento. Sin efecto para nosotros; señal de que
estaba blindando los cimientos justo antes de cargar peso encima (07-24).

**`f87979f` `refactor(memhlp): Optimize patternscan by just scanning the .text
section`** — el importante del día.

- **Antes:** `LM_EnumSegments` sobre **todo el proceso**, se queda con los segmentos
  `LM_PROT_XR`, y luego filtra por solape con el módulo objetivo. Tres defectos: (a)
  enumera todo para tirar casi todo; (b) el filtro descarta sólo lo que queda
  *totalmente* fuera, así que un segmento con solape **parcial** se escanea entero,
  incluidos trozos de otra `.so`; (c) el segmento r-x arrastra `.init`/`.plt`/`.fini`
  además de `.text`.
- **Ahora:** pide `.text` a `Decompiler::getSection()` —que ya leyó las cabeceras ELF
  **de disco**— y escanea `[base+sh_addr, +sh_size)`. Un bucle, sin mapa intermedio.
- **Cambio acoplado en `main.cpp`:** `Decompiler::parseHeader(g_modSteamUI)`. SLSsteam
  también escanea patrones en `steamui.so`; si `patternScan` ahora exige el mapa de
  secciones, hay que parsear sus cabeceras o los escaneos ahí fallan en silencio
  (`Didn't find .text section`). Nótese la distinción deliberada: **`parseHeader`** =
  sólo cabeceras ELF (baratísimo, un `fopen` + tres `fread`); **`parseModule`** = eso +
  recorrer `.rodata` entera recogiendo strings + `.data.rel.ro` buscando vtables
  (caro). De `steamui` sólo quiere las secciones, y lo documenta en el comentario para
  que nadie lo "arregle" luego.
- **Observaciones propias (no son suyas):** sobrevive un off-by-one — el bucle va
  `addr < end` y la guarda interna es `byteAddr > end` (debería ser `>=`, y el bucle
  parar en `end - bytes.size()`), así que la última iteración puede leer hasta
  `bytes.size()` bytes más allá de `.text`. Inocuo en la práctica (`.text` nunca es lo
  último del mapeo) pero latente. Y el escaneo **no sale al primer match**: cuenta
  todos y devuelve **el último**, con un `debug` cuando `matches > 1` — intencionado
  (quiere saber cuándo un patrón es ambiguo, de ahí sus comentarios *"Not unique. All
  matches point to correct function though"*), pero significa que cada patrón recorre
  `.text` completa siempre.

##### 7.7.1.a Veredicto sobre "escanear sólo `.text`" en lumalinux [PRESTABLE — prioridad BAJA]

Anotado tras comparar contra `src/patterns.cpp`. **El valor de este commit NO se
traslada, porque lumalinux nunca tuvo el problema que él arregló.** Su ganancia venía
de eliminar `LM_EnumSegments` sobre el proceso entero + un filtro de solape chapucero;
`FindModuleRangeFromMaps()` ya filtra por nombre de módulo en `/proc/self/maps` y ya se
restringe a `r-x`. El delta que queda es sólo **mapeo r-x vs sección `.text`**, y con
`-z separate-code` (default en binutils moderno) ese mapeo es `.init + .plt + .text +
.fini`: **todo código, sin `.rodata`**. Así que "quita falsos positivos por datos" es
**falso** en nuestro caso, y la aceleración es marginal (`.text` es la aplastante
mayoría del rango). Los patrones de lumalinux son de 20+ bytes, con lo que un falso
positivo en los stubs repetitivos de `.plt` es improbable.

Lo que sí salió de la comparación, por orden de valor real:

1. ~~**`/proc/self/maps` se re-lee y re-parsea en CADA búsqueda**~~ → **DESCARTADO, medido.**
   Ver §7.7.1.c: el parseo de maps cuesta **0.06–0.22 ms**, y el total cacheable de un
   arranque es **~0.5–1.8 ms**. Irrelevante. El coste real está en el propio escaneo de
   patrones (~19 ms cada uno), que es **43×–145×** el total de los parseos. No cachear
   por rendimiento.
2. **Inconsistencia substring vs sufijo.** `main.cpp::IsSteamclient()` es estricto
   (`ends_with("/steamclient.so")`), pero `FindModuleRangeFromMaps()` usa
   `line.find("steamclient.so")` — substring. Si en el proceso coexistieran dos rutas
   que contengan esa cadena (p.ej. `linux32/` y `linux64/`), el `[min_start, max_end]`
   agregado abarcaría **desde una hasta la otra**, incluyendo lo que haya en medio →
   rango enorme, posibles huecos no mapeados (riesgo de SIGSEGV al escanear) y matches
   cruzados entre módulos. **No verificado que ocurra** —lumalinux es i386 y sólo carga
   en procesos de 32 bits, donde en principio sólo está `linux32/steamclient.so`— pero
   alinear el filtro con `IsSteamclient()` cuesta una línea y cierra el caso.
3. Los dos off-by-one equivalentes en `SigScan` y en los bucles de
   `FindUniqueInSteamclient`/`FindLoadPackageFunction` ya están correctos aquí
   (`i + patLen <= r.size`), a diferencia del de SLSsteam. **[YA]**

**Conclusión: NO abrir issue por el `.text`, ni por el cacheo.** Lo único que sobrevive
de esta línea es (2), y es un one-liner de robustez, no de rendimiento. El aprendizaje
transferible de verdad de este commit es otro: **leer las secciones del ELF de disco**
como primitiva, que es lo que desbloquea el pipeline RTTI/vtable — ver §7.7.1.b.

##### 7.7.1.c Medición — dónde está de verdad el tiempo en `patterns.cpp` [YA / cerrado]

Motivo de la medición: en §7.7.1.a afirmé que cachear el `ModuleRange` "vale bastante
más que ceñirse a `.text`". **Falso, y por un factor de dos órdenes de magnitud.**
Medido replicando los bucles exactos de `patterns.cpp` (`FindModuleRangeFromMaps`,
`SigScan`, y el bucle unique de `FindUniqueInSteamclient`) sobre `/proc/self/maps`
sintéticos de 1.5k y 4k líneas y un `.text` de 9 MB de ruido (peor caso: patrón de 20
bytes que no existe, recorre todo). `-O2`, Xeon @ 2.1 GHz:

| Operación | 1.5k líneas | 4k líneas |
|---|---|---|
| 1× parseo de `/proc/self/maps` | **0.064 ms** | **0.219 ms** |
| 1× `SigScan` sobre 9 MB (first-match) | **18.6 ms** | 19.0 ms |
| 1× bucle unique sobre 9 MB | **20.1 ms** | 19.9 ms |
| ~8× parseos de maps (todo lo cacheable) | 0.51 ms | 1.75 ms |
| 4× escaneo (el trabajo real) | **74.5 ms** | 76.1 ms |
| **ratio escaneo / maps** | **145×** | **43×** |

Escaneos reales por arranque en la config por defecto de un Deck: **4** — DepotKey,
GMRC, ShaderDepot y Reconcile. (`BuildDep` está off por defecto y `LoadPackage` es
opt-in → 0. Los dos call-sites que parecían duplicar escaneo, `depot_key_hook.cpp:123`
vs `:131` y `license_reconcile.cpp:43` vs `:48`, son **ramas mutuamente excluyentes**;
y `ResolveAddr()` ya cachea en un `static`. **[YA]**)

Conclusiones:

1. **El cacheo del `ModuleRange` no merece la pena.** ~1 ms sobre un arranque. Y el
   poll del ctor (1 parseo/segundo) es 0.2 ms/s: ruido.
2. **El coste está en el escaneo**, y aun así son ~76 ms **en un hilo detached durante
   el arranque de Steam** — invisible para el usuario. Tampoco merece optimizarse.
3. Dato de interés si algún día importa: los dos escaneos `FindUniqueInSteamclient`
   (ShaderDepot y Reconcile) **nunca salen temprano** — tienen que recorrer los 9 MB
   completos para *demostrar* unicidad, incluso si el match está en el primer byte. Es
   el precio correcto a pagar por no hookear la función equivocada (§ comentario en
   `patterns.cpp:121-127`), no un defecto.
4. **La preocupación de consistencia entre llamadas tampoco se sostiene**: temí que el
   `mprotect` de la colocación de un hook sacara páginas del filtro `r-x` y encogiera el
   rango para búsquedas posteriores. No ocurre: `lmhook.cpp` restaura la protección
   anterior tras cada escritura (`LM_ProtMemory(..., oldProt, nullptr)` en las líneas
   75 y 115), y `LM_HookCode` hace lo mismo internamente.

**Veredicto global: `patterns.cpp` no necesita tocarse por rendimiento.** Cerrado.

##### 7.7.1.b Corrección — el pipeline RTTI/vtable SÍ es portable a `LD_PRELOAD`

Rectifico una conclusión precipitada: dije que la resolución de vtables por RTTI de
SLSsteam "no se puede portar a lumalinux" porque exige analizar antes de las
relocaciones, y eso sólo lo da `LD_AUDIT`. **La premisa es correcta pero la conclusión
es errónea, y la dirección del problema es la contraria.**

Lo que SLSsteam arregló el 07-25 (`3e9dfdd`) fue: *".rodata.ro.rel seems to get
modified so the offsets turn into actual addresses which broke the decompiler flow"*. Su
código asume valores **relativos al módulo** y les suma la base:

```cpp
const lm_address_t offset = *(reinterpret_cast<lm_address_t*>(start) + i);
this->functions.emplace_back(offset + moduleBase);   // ← asume offset relativo
```

O sea: **pre-relocación** los slots de la vtable contienen offsets (hay que sumar base);
**post-relocación** ya contienen **direcciones absolutas listas para usar**. Un
consumidor `LD_PRELOAD`, que por definición llega después, no tiene un problema: tiene
un caso **más sencillo** — lee el puntero del slot y lo usa tal cual, sin aritmética de
base. La validación natural es comprobar que el puntero cae dentro del rango r-x del
módulo.

Lo que sigue haciendo falta y no es gratis es el resto del pipeline: leer las secciones
del ELF de disco (`.rodata`/`.rodata.str` para strings, `.data.rel.ro` para vtables),
localizar los `typeInfo` por nombre RTTI mangled, y —si se quiere el descubrimiento
automático de índices— desensamblar las funciones de los `IClient*Map` para cruzar el
literal del nombre con el número de slot. Eso es el grueso de `decompiler.cpp` +
`vftableinfo.cpp`. lumalinux ya tiene la primitiva RTTI (§1.4/§15, `src/rtti.cpp`) y ya
tiene traducción de direcciones (`src/vaddr_xlate.cpp`), así que la distancia es menor
de lo que parece.

**Estado: sigue siendo [PRESTABLE], no [DESCARTADO].** Coste real: medio-alto (el
desensamblado por slot es lo caro). Beneficio: los hooks dejarían de depender de
patrones de bytes, que es nuestra causa nº1 de rotura por update de Steam (§A de
`maintenance.md`). Candidato serio para cuando `patterns.cpp` vuelva a romperse, no
antes — y con la ruta gradual obvia: empezar por RTTI+vtable para los hooks
**no críticos** (ShaderDepot, `NotifyLicensesUpdated`), que ya degradan a no-op limpio,
y dejar DepotKey/GMRC en patrones hasta que la ruta esté probada en campo.

#### 7.7.2 Día 2026-07-24 — 12 commits (mueren los 5 hooks `RunIPCFrame`)

**Resumen: Ace no eliminó el "arranque por hook desechable", consolidó cinco en uno.**

*El problema de fondo.* Un `VFTHook` parchea un slot de la vtable de un **objeto vivo**,
y Steam no te da el puntero de sus `IClientAppManager` / `IClientApps` / `IClientUtils` /
`IClientUser`. La solución vieja (la de §1.6) era hookear por patrón una función que
recibiera el objeto como `this` — `RunIPCFrame`, llamada pronto y a menudo —, robarle la
vtable, colocar los hooks reales y **auto-desinstalarse**. Coste: **5 patrones de bytes
que existen sólo para arrancar**, sin aportar ninguna feature, y uno más por cada
interfaz nueva.

**Los dos habilitadores:**

1. **`1865890` — vtables secundarias (= herencia múltiple).** `CUser : CBaseUser,
   IClientUser, IClientMatchmaking, IClientAppDisableUpdates, IClientBilling` no tiene
   una vtable, tiene varias sub-vtables contiguas, **todas apuntando al MISMO typeinfo
   `5CUser`**. El código viejo hacía `vftables[name] = vft` y cada hallazgo pisaba al
   anterior. Ahora los encadena en `subclasses[0..n]` por orden de memoria, y el
   comentario del commit siguiente documenta el mapa de herencia deducido a mano
   (`subclasses[0]` = `IClientUser`, porque la primaria es `CBaseUser`).
   Detección de frontera entre sub-vtables: **heurística**, `offset & 0xFFFF0000` (el
   `offset-to-top` del siguiente sub-vtable es negativo). Con un `TODO` admitiendo que
   la vía correcta —cruzar typeinfos— *"no le salió bien: funcionaba en las 4 primeras
   vftables de CUser y luego empezaba a fallar"*. **Fragilidad declarada en el corazón
   del sistema nuevo.**
2. **`8fada22` + `28d59a1` — offsets de miembro.** Cuatro patrones de un tipo distinto:
   localizan **la instrucción que calcula la dirección de un miembro**, con
   `SigFollowMode::None`, y le extraen el desplazamiento. `m_OffsetUserAppInfo` =
   `"8D 90 ? ? ? ? …"` (`lea edx,[eax+disp32]`, opcode de 2 bytes → lee en `+2`);
   `m_OffsetClientUser` = `"2D ? ? ? ? …"` (`sub eax,imm32`, opcode de 1 byte → lee en
   `+1`). Las interfaces están **embebidas**, no son punteros (`this + offset`, sin
   desreferenciar), y el puntero a vtable sale del offset 0 del sub-objeto.
   Nota de método suya: *"Like the previous commits I picked some patterns that go back
   about 9 months"* — **eligió patrones verificados contra ~9 meses de builds**.

**Las dos víctimas de prueba:** `69ea49b` retira el patrón de `GetSteamId` (que su
propio comentario admitía no-único) y `269e867` el `RunIPCFrame` de RemoteStorage. Este
segundo enseña algo: la vtable de RemoteStorage **se relocaliza**, así que un `VFTHook`
no sirve (*"the pointers are all wrong and would need manual adjustment which breaks
current assumptions by VFTHook<T>"*) → usa un `DetourHook`, y para ello añade la
sobrecarga `DetourHook::setup(name, addr, fn)` que **desacopla "cómo encuentro la
función" de "cómo la hookeo"**.

**La ejecución, `8de3384`** (−223/+164 en `hooks.cpp`, 22 ficheros). Dos mitades:
- Los 4 `RunIPCFrame` restantes fuera; en su lugar un `placeVFTHooks()` one-shot
  (`static bool hooked` + mutex, *"I don't think the IPC layer is multithreaded but
  better safe than sorry"*) llamado desde el hook de `CSteamEngine::RunInterface`. No
  puede hacerse al cargar: *"first run CUser is null"* — sigue necesitando **un latido
  recurrente** que reintente. `RunInterface` es ese latido. **De 5 hooks-reloj a 1.**
- Mueren los globales `g_pClientApps` / `g_pClientAppManager` / `g_pClientUtils` /
  `g_pClientUser`, sustituidos por navegación (`g_pSteamEngine->getUser()->getClientApps()`)
  en `api.cpp`, `config.cpp` y `feats/*`. La ganancia es de diseño: un global se rellena
  *si y cuando* alguien pasó por el bootstrap; una navegación **funciona siempre que el
  engine exista**. Estado mutable global con orden de init implícito → consulta
  idempotente.

Balance de patrones del día: **−6 frágiles (5 bootstrap + 1 localizador), +4 offsets de
miembro.** Y el cambio cualitativo pesa más que el recuento: un offset de miembro se
mueve si Valve **reordena los campos de la clase**; un prólogo se mueve cuando Valve
**recompila la función**. Lo segundo pasa constantemente.

**Los tres arreglos del SDK** (`bc25bf8`, `3c77e9f`, `99d0af7`) parecen trivia y son
método — su SDK es un espejo a mano de structs de Steam, y un espejo que miente
envenena todo lo que se lea a través de él:
- `EInterfaceType` de `uint32_t` a `uint8_t`: el campo es **1 byte**; leía 4 y
  enmascaraba con `& 0xff`, o sea **leía 3 bytes de campos vecinos**. Regla:
  *si necesitas una máscara para leer un campo, tu tipo está mal.*
- `getPipeIndex() -> uint32_t*` a `getCurrentSteamPipe() -> HSteamPipe`: devolver valor
  en vez de puntero mata la clase de bug que su propio comentario borrado ese día
  describía (*"Don't assign a pointer to IClientUser::GetSteamID! …it's lifetime is very
  short"*). Y el renombre no es cosmético: `GetCurrentSteamPipe` es el nombre **real** en
  la `IClientUtils` de Steam → el nombre pasa de etiqueta a **clave de búsqueda**.
- `CUtlBuffer::flags` de `uint8_t`@0x1A a `uint32_t`@0x18. **Impacto conductual: cero**
  — el total sigue siendo `0x24`, los campos anteriores (`mem`/`get`/`put`/`offset`) no
  se mueven, y `flags` **no se lee en ningún sitio**, ni entonces ni hoy. Es fidelidad
  preventiva del espejo. (Los comentarios `//0x1A`/`//0x1B` quedan obsoletos: la
  aritmética cuadra, los comentarios mienten.)

**Comentarios/limpieza:** `73dba5f` documenta el mutex y su propia incertidumbre;
`b752536` documenta el layout del buffer IPC por primera vez **y esconde un arreglo de
correctitud** (usaba el cursor `get` del buffer como offset del `interfaceType`, que
está en offset **fijo** 1); `ce0bb89` imports/formato en ~17 ficheros.

##### 7.7.2.a Veredicto sobre lo "prestable" del 24-07 — 1 de 3 sobrevive

Anotado tras verificar las tres candidatas contra el código de lumalinux. **Dos eran
malas y una es sólo confirmación.** Se registra para que nadie las re-derive.

**(1) "Localizar un objeto raíz y navegar desde él, aplica tal cual" → FALSO.**
La técnica exige que los targets sean **métodos virtuales de objetos alcanzables desde
una raíz**. `RESEARCH.md` §15.2 ya tiene la tabla, verificada por RVA en el build
f5eb8bd3, y dice que **3 de 5 targets de lumalinux no son virtuales**:

| Hook | ¿Virtual? | Ruta update-proof |
|---|---|---|
| DepotKey | **SÍ** — vtable de `CConfigStore`, slot 6 | RTTI → slot DERIVADO por firma — **✅ ya implementado** |
| GMRC | no (pero es un RPC con nombre) | RTTI del transport + dispatch por nombre de RPC (coste: protobuf) |
| BuildDep | no | ninguna — se queda en patrón |
| ShaderDepot | no | ninguna — se queda en patrón (no crítico) |
| LoadPackage | no | ninguna (sólo diagnóstico) |

Para esos tres **no hay raíz que navegar**: son lógica local sobre appinfo/KeyValues, no
métodos despachados por vtable. Y el único que sí lo es (DepotKey) **ya usa RTTI** desde
§15.3. O sea: la "lección de arquitectura" era re-derivar peor un análisis que ya
existía. **No accionable.**

**(2) "Desacoplar localización de hooking" → [YA], y uniforme.** Verificado: los cuatro
hooks siguen la misma forma — `RvaFeed::Resolve(name)` primero, si falla el
patrón/locator/RTTI, y luego `LmHook::Install(target, …)` con la dirección ya resuelta
(`depot_key_hook.cpp:121-152`, `gmrc_hook.cpp:65-73`, `shader_depot_hook.cpp:95-97`,
`depot_dependency_hook.cpp:154`). Lo que Ace añadió el 24-07 con
`DetourHook::setup(name, addr, fn)` lumalinux ya lo tenía por diseño. Confirmación
buena, nada que hacer.

**(3) "La regla de los tipos, mirable en `steam_types.hpp`" → VACÍA, y al revés.**
- `grep` de lecturas con máscara en `src/`: **cero** (fuera de `sha256.cpp`, donde son
  aritmética legítima). No hay nada que arreglar.
- Y la comparación va en la otra dirección: `steam_types.hpp` **pinza los layouts con
  `static_assert`** (`sizeof(CUtlVector)==16`, `sizeof(DepotEntry)==0x20`) → el build
  falla si un supuesto se rompe. SLSsteam tiene **0 `static_assert` en todo su `sdk/`**
  y depende de comentarios a mano `//0x1A` que, como se vio arriba, se quedan obsoletos.

**Y lo que creí que era la pregunta nueva del día TAMBIÉN estaba ya contestada.**
La diferencia real entre los dos proyectos es cómo derivan el slot: SLSsteam **por
NOMBRE** (`parseInterfaceMapBase` lee el literal dentro de los wrappers `IClient*Map` y
construye `nombre → índice`), lumalinux **por FIRMA DE BYTES**
(`ResolveVtableSlotBySignature`) — o sea que la ruta RTTI de lumalinux sigue arrastrando
una dependencia de patrón. Parecía una mejora obvia a proponer. Pero `RESEARCH.md` §15.4
ya lo tiene descartado, con el motivo:

> *(A hook whose target self-names could instead derive the slot by that name, à la
> SLSsteam's decompiler; DepotKey's accessor is a generic KeyValues call with no
> distinctive string, so the prologue signature is the discriminant.)*

Es decir: el accesor de DepotKey es un **getter genérico de KeyValues sin string
distintivo**, así que el nombre no identificaría un slot único. La firma del prólogo *es*
el discriminante, a propósito. (Dato a favor de que la mecánica en sí funcionaría: Ace da
ese salto para ConfigStore — coge el índice de `21IClientConfigStoreMap` y lo aplica a la
vtable de `12CConfigStore`, `b8ef92f`, asumiendo correspondencia de orden entre wrapper e
implementación. Pero eso resuelve el *cómo*, no el problema de que el nombre no sea
único.)

Lo único que quedaría por hacer aquí es **de bajo valor**: §15.4 *afirma* que no hay
string distintivo, pero no lo respalda con un volcado. Un `nombre → índice` de
`21IClientConfigStoreMap` (desensamblando sus slots y cruzando literales de `.rodata`,
como `parseInterfaceMapBase`) confirmaría la afirmación con evidencia y cerraría el tema
para siempre; `tools/check_patterns.py` ya camina vtables estáticamente, así que sería
una extensión pequeña. **Opcional, aserción→evidencia, no un cambio de conclusión.**

**Neto del 24-07 para lumalinux: cero accionables.** El día es valiosísimo para
*entender* SLSsteam (y para saber que §1.6 está muerta), pero `RESEARCH.md` §15.2–15.4 ya
había anticipado todas sus lecciones aplicables, con más precisión y con RVAs verificados.
Conviene recordarlo antes de volver a "descubrir" esto en un barrido futuro.

#### 7.7.3 Día 2026-07-25 — 13 commits (el bug de "analizar demasiado tarde")

**`3e9dfdd` + `807e229` — la relocación de `.data.rel.ro`.** Los dos commits que
sostienen la corrección de §7.7.1.b, y el hallazgo conceptual del día. Mensaje textual:
*".rodata.ro.rel seems to get modified so the offsets turn into actual addresses which
broke the decompiler flow"*.

Qué pasa: `VFTable::analzye()` lee cada slot y le **suma la base del módulo**
(`functions.emplace_back(offset + moduleBase)`), o sea asume valores **relativos**. Pero
`.data.rel.ro` es, por definición, *data con relocaciones*: el linker dinámico convierte
esos offsets en **direcciones absolutas** al cargar. Si analizas después, sumas la base
a algo que ya es absoluto → basura.

Arreglo, en dos pasos:
1. `3e9dfdd` mueve `LM_FindModule` + `parseModule` **dentro de `la_objopen`**, por módulo
   y por separado (`steamclient.so` y `steamui.so`), con el comentario *"Analyse modules
   before any relocations get applied"*. Y elimina los `LM_FindModule` de `load()`.
2. `807e229` va más lejos: **analiza TODAS las vtables inmediatamente** dentro de
   `la_objopen`, y quita los `analzye()` diferidos de `hooks.cpp` y `vftableinfo.cpp`.
   Reconoce el coste: *"This is wasteful, but we have to analyse right away otherwise the
   offset get turned into addresses messing up the analysis. We could workaround it by
   only loading after a late module has been loaded"*.

**Por qué importa para nosotros:** esto es lo que hace que su pipeline de vtables sea
`LD_AUDIT`-only, y **es exactamente la razón por la que §7.7.1.b concluye que para
`LD_PRELOAD` el caso es más FÁCIL, no imposible**: post-relocación los slots ya son
direcciones absolutas, así que se leen tal cual, sin aritmética de base. Su bug es
nuestro caso trivial.

**`7aee22c` — `ServerResponded` de patrón a índice de vtable.**
`ISteamMatchmakingPingResponse::ServerResponded` (patrón en `steamui.so`) →
`CGameInfoDialog::ServerResponded` (`VFTIndexes::CGameInfoDialog::ServerResponded`).
Un patrón menos, y el primero que retira en `steamui.so` — que es el módulo cuyas
cabeceras empezó a parsear el 23-07 (`f87979f`).

**`d2f94c8` — "Refine patterns": la migración de estilo.** Dos cambios acoplados:
- El vector de prólogo pasa de `std::vector<uint8_t>` a **`std::vector<int16_t>`**, para
  poder poner **comodines (`-1`) dentro del prólogo**. `findPrologue()` los salta.
- Y migra los patrones del estilo **"firma larga en el CALL-SITE + `SigFollowMode::Relative`"**
  (localiza la llamada `E8` y la sigue hasta la función) al estilo **"ancla corta DENTRO
  de la función + `PrologueUpwards`"** (busca hacia atrás el prólogo). Ejemplo:
  `CUser::CheckAppOwnership` pasa de 26 bytes en el call-site a
  `"0F 94 C2 08 51"` + prólogo `{0x53,0x56,0x57,0xE5,0x89,0x55,-1,-1,-1,-1,0x5,-1,-1,-1,-1,0xE8}`.
  Igual `TraceIPC`, `CAPIJob::SendAndRecv`, `CAppDataCache::BParseResponseMessage`,
  `CWebSocketConnection::BBuildAndAsyncSendFrame`, `CSteamEngine::RunInterface`,
  `CUser::UpdateAppOwnershipTicket`.

##### 7.7.3.a Veredicto sobre la "migración de patrones" — RETRACTACIÓN + trade-off real

**Retracto lo que escribí en el primer barrido:** dije que *"tus patrones de
`patterns.cpp` son del tipo largo y frágil que él acaba de abandonar"*. **Falso.** Lo que
Ace abandonó el 25-07 son **patrones de call-site con `SigFollowMode::Relative`**.
Verificado en `src/patterns.hpp`: **los seis patrones de lumalinux están anclados en el
PRÓLOGO de su función objetivo**, ninguno en un call-site, y no existe nada equivalente a
`SigFollowMode::Relative`:

| Patrón | Ancla |
|---|---|
| `kDepotKeyFnPattern` | `55 57 56 53 E8 ?? …` — prólogo |
| `kBuildDepotDependencyPattern` | `55 89 E5 57 56 E8 ?? …` — prólogo |
| `kLoadPackagePattern` | `55 89 E5 57 E8 ?? …` — prólogo |
| `kGmrcFunctionPattern` | `E8 ?? ?? ?? ?? 05 ?? … 55 89 E5 …` — thunk PIC + prólogo |
| `kShaderCacheDepotPattern` | `57 56 53 E8 ?? … 81 C3 ?? …` — prólogo |
| `kNotifyLicensesUpdatedPattern` | `55 89 E5 57 56 53 E8 ?? …` — prólogo |

O sea: **lumalinux ya partía del destino de su migración.** Y va más allá en el
comodinado, que hace con más criterio que él: wildcardea el rel32 del `get_pc_thunk`, el
imm32 del GOT, el tamaño de frame (`81 EC ?? ?? ?? ??`), el disp32 de globales
GOT-relativas (`8B 83 ?? ?? ?? ??`, con el comentario explicando *por qué*: "shifts
between Steam builds… verified still unique wildcarded"), y hasta **los dos bytes bajos
de un offset de miembro dejando los altos como restricción** (`8B B8 ?? ?? 00 00`, con la
deriva documentada 0x1b18→0x1b14). Eso es más fino que los comodines todo-o-nada de
SLSsteam.

**Lo que SÍ es genuinamente distinto, y es un TRADE-OFF, no una mejora:** el `PrologueUpwards`
reduce la **superficie de bytes que debe permanecer estable**. lumalinux alcanza
unicidad haciendo el patrón **largo y contiguo** desde el inicio de la función (DepotKey:
45 bytes, ~37 fijos) — si cambia *cualquier* byte de esa ventana, rompe. SLSsteam la
alcanza con **ancla corta + prólogo corto en dos sitios separados** (CheckAppOwnership:
~11 bytes fijos en total), así que un cambio en medio de la función no le afecta.

Pero el precio es real y él lo paga: **anclas cortas son mucho más propensas a
multi-match**. `"0F 94 C2 08 51"` son 5 bytes; el prólogo hace de segundo filtro pero no
garantiza unicidad — de ahí su log `"Pattern %s found %i times"` y sus comentarios
*"Not unique. All matches point to correct function though"*. Para lumalinux eso es peor
que para él: `FindUniqueInSteamclient` **exige unicidad** y falla cerrado, así que más
ambigüedad = ShaderDepot y Reconcile desactivados. Y el patrón largo de lumalinux tiene
una virtud compensatoria: **codifica la secuencia de carga de argumentos**, o sea es
semánticamente auto-validante, no una coincidencia de bytes.

**Veredicto: no migrar.** Es un trade-off con contrapartida concreta, sobre un esquema
que ya está en el lado bueno del cambio que Ace hizo. Si alguna vez un patrón de
lumalinux rompe *por una recompilación que sólo tocó el medio de la función* (síntoma:
el prólogo sigue ahí pero el patrón no matchea), entonces `PrologueUpwards` es la
herramienta — y entonces sí merece implementarse. Anotado como **[PRESTABLE
condicional]**, con el disparador escrito.

**El resto del día:**
- **`ce6509f` — crash al arrancar con `ExtendedLogging`.** Guarda `g_pSteamEngine->getUser()`
  antes de tocar `getUtils()->getAppId()` en la línea de log. Y de paso **mueve el mutex
  de `placeVFTHooks` DESPUÉS del early-return de `!usr`**: antes, cada llamada a
  `RunInterface` previa a que existiera `CUser` serializaba en el mutex para nada.
- **`8effc44` — nullchecks de `CUser::getClient*`.** Mete la guarda **dentro** de
  `CSteamEngine::getUtils()` (`if (!getUser()) return nullptr;`) y retira las guardas
  redundantes de los llamantes. Éste y el anterior son la factura del `8de3384` de ayer,
  que navegaba estructuralmente sin proteger los eslabones intermedios.
- **`3620dd2` — SteamStub roto en Palworld multicuenta.** Invierte la precedencia en
  `hkClientUser_GetSteamId`: el spoof de un solo uso pasa a mandar **sobre** el steamId
  del ticket cifrado cacheado. *"One time spoof should take presedence, otherwise
  SteamStub will fail for games that use encrypted tickets for online auth when you play
  on multiple accounts"*. Nicho, pero es el tipo de bug que sólo aparece con varias
  cuentas.
- **`4bd33e7`** identifica el padding de `CNetPacket+0xC` como `int32_t refs` (contador de
  referencias) — preparación para el `b937ab2` del 08-06 ("liberar el netpacket en vez de
  esconderlo"). **`9927612`** añade el hash de cliente 2026.07.25 a SafeMode. **`526b828`,
  `dfb8614`, `9f232e6`** estilo; el último corrige el mapa del buffer IPC documentado
  ayer (`base+2` es `*(this+4)`, el id de función está en `base+6`).

**Neto del 25-07 para lumalinux: cero accionables**, más una retractación. El único
candidato (`PrologueUpwards`) queda como condicional con disparador definido.

#### 7.7.4 Día 2026-07-26 — 9 commits (el trade-off de FakeAppIds + privacidad)

**`1250950` — los errores de config pasan de genéricos a enumerados.** Antes había un
`enum ELoadError { None, MissingKey, ParsingException }` con **orden de severidad** (sólo
se guardaba el peor: `if (__loadErrors.get() > err) return;`) y el toast era literal:
`"Issues during config loading encountered! Missing key(s)"`. Ahora `__loadErrors` es un
`std::string` que **acumula**, y `setError()` recibe el nombre de la clave:

```
Config loading errors:
Missing DlcData
Missing DenuvoGames
Failed to parse IdleStatus
```

Los tres templates (`getSetting`/`getList`/`getMap`) pasan su `name` a `setError`, así que
sale gratis para todas las claves.

**Relevancia para §4.3 (completado de `config.yaml`), verificada:**
- **Ganancia de diagnóstico real:** el toast ya dice *qué* clave falta. Un usuario con un
  config incompleto puede arreglarlo a mano sin adivinar.
- **`backend/slssteam_schema.py` de LumaDeck sigue funcionando bien.** Trae
  `config_default.hpp` **en vivo** desde upstream, así que las claves nuevas del rango
  (`CDKeys`, `LogLevels`, `SteamIdOverride`, `FakeName`) se completan solas.
  Comprobado por HTTP: la URL apunta a la rama `master`, que **no existe** en
  `AceSLS/SLSsteam` (sus ramas son `main` y `dev`) — pero GitHub mantiene el redirect
  post-renombrado y devuelve **200 con contenido idéntico a `main`**, incluidas las cuatro
  claves nuevas. **No es un bug**, sólo una dependencia implícita de una cortesía de
  GitHub.
- **Lo que sí está desactualizado (bajo impacto):** el snapshot `_BUNDLED_YAML` tiene 29
  claves y está desfasado en **tres** puntos frente a upstream: le falta `CDKeys` (08-12),
  le falta `LogLevels` (08-09), y arrastra el `LogLevel` **singular** que upstream renombró
  a plural. Sólo se usa si el fetch en vivo falla (primer arranque sin red, GitHub caído,
  proxy). En ese caso el completado omite las dos claves nuevas y SLSsteam las enumera en
  el toast — confirmado que sí cuentan: el flag `silent` que Ace añadió a
  `getSetting`/`getMap` sólo suprime el **log del valor**, no la llamada a `setError`. La
  `LogLevel` obsoleta es inocua (una clave desconocida se ignora en silencio; sólo toasta
  lo que SLSsteam *espera* y no encuentra). Su propio docstring ya lo prevé (*"Updated when
  we bump supported SLSsteam"*) → es mantenimiento pendiente, no defecto.
- **Y el docstring cita el texto viejo del toast** (`"Issues during config loading
  encountered! Missing key(s)"`), que este commit cambió. Cosmético, pero es el documento
  que explica el *por qué* del módulo.

**`62afc2e` — "Replace C code with C++", y es más que cosmético.** El código viejo:

```cpp
char pathBuf[255];
const char* configDir = getenv("XDG_CONFIG_HOME");
if (configDir != NULL) sprintf(pathBuf, "%s/SLSsteam", configDir);
else { const char* home = getenv("HOME"); sprintf(pathBuf, "%s/.config/SLSsteam", home); }
```

`sprintf` sin límite a un buffer fijo de 255 bytes **desde una variable de entorno**.
Un `$XDG_CONFIG_HOME` largo desborda la pila. La versión con `ostringstream` no tiene
tope, así que ese riesgo desaparece. **Pero `getenv("HOME")` sigue sin comprobarse**: el
código nuevo hace `path << home << "/.config"` y meter un `const char*` nulo en un
`ostream` es UB igual. Arregla (a), no (b).

Detalle latente del mismo commit: `createFile()` pasa de `fopen(path, "w")` (trunca) a
`std::ofstream(path, std::ios::app | std::ios::out)` (**añade**). Es inocuo porque
`createFile` está guardado por un check de existencia (es create-if-missing, §4.3), pero
si alguna vez se llamara sobre un fichero existente, el comportamiento cambió de
"sobreescribe" a "duplica el config por defecto al final".

**`293eb93` — desactiva el spoofing de SteamId cuando se usa FakeAppIds.** El commit más
interesante del día porque **él mismo enumera el trade-off** en el mensaje:
- *Pros:* arregla el uso de tickets de propiedad cifrados **reales**, arregla las
  AuthSessions.
- *Cons:* rompe las activaciones de Denuvo en versiones recientes, rompe el multijugador
  online en juegos Denuvo vía FakeAppIds.

Añade además **"Never spoof inside the Steamclient"**: si `utils->getAppId()` es 0 (o sea,
la llamada viene del propio cliente y no de un juego), devuelve el steamId sin tocar.
Elige la corrección del caso común sobre el caso Denuvo. Ojo: lo revierte parcialmente el
28-07 (`3b2e0d8`), al concluir que engañar a Denuvo por *timing* es demasiado
inconsistente.

**Mecanismo exacto (el "disable" es indirecto).** El commit no añade ningún
`if (fakeAppIds) return`. Lo que hace es **cambiar el appId con el que se consulta la
caché de tickets**: de `FakeAppIds::getRealAppIdForCurrentPipe()` (el appId REAL) a
`utils->getAppId()` (el que reporta el pipe, que **con FakeAppIds activo es el FALSO**).
Y `getCachedEncryptedTicket` ya llevaba dentro esta guarda:

```cpp
const AppId_t realAppId = FakeAppIds::getRealAppIdForCurrentPipe();
const AppId_t fakeAppId = FakeAppIds::getFakeAppId(realAppId);
if (realAppId && fakeAppId && appId != realAppId) {
    g_pLog->once("Returning empty cached encrypted ticket for %u because it's set to %u\n", ...);
    return ticket;   // ← VACÍO
}
```

Con el appId del pipe (falso) != realAppId, la guarda dispara → ticket vacío →
`ticket.steamId == 0` → **no hay steamId con el que spoofear**. El spoofing se desactiva
por *inanición de datos*, no por una rama explícita. (Más los dos guards nuevos: `!utils`
y `getAppId() == 0` → "never spoof inside the Steamclient".)

**¿Nos aplican los "cons"? Verificado: la ruta automática NO, la manual SÍ podría.**
LumaDeck fija FakeAppIds por dos caminos distintos:
1. **Automático, durante la aplicación de un fix** (`backend/fixes.py`): el valor sale del
   `[Main]` del `OnlineFix.ini` **que trae el propio fix**, nunca de un 480 hardcodeado
   (`_parse_onlinefix_ini`: *"Returns {} if no FakeAppId is found so callers only ever act
   on what the fix explicitly declares (never a hardcoded 480)"*). Y el comentario de
   `fixes.py:335-346` lo dice explícito: *"Denuvo / single-player / generic cracks have no
   such .ini, so they get neither 480 nor netsock"*. O sea: **por construcción, LumaDeck
   nunca pone un FakeAppId en un juego Denuvo** → los dos "cons" del commit son
   estructuralmente imposibles por esta ruta, y los "pros" (AuthSessions, tickets cifrados
   reales) caen justo encima del flujo de online-fix.
2. **Manual, el toggle "Native Online"** (`GameDetail.tsx:390` → `enable_native_online` →
   `add_fake_app_id(appid, 480)`): aquí el 480 **sí es fijo** y es decisión del usuario.
   Nada impide activarlo sobre un juego Denuvo, y en ese caso sí se entra en el caso que
   Ace marcó como roto. Candidato a nota en la UI si alguna vez se reporta.

**Precondición que acota el impacto real:** el spoof sólo actuaba si existía un **ticket
cifrado cacheado** para el juego, y esa caché la puebla SLSsteam observando el tráfico
(`~/.config/SLSsteam/`). LumaDeck **no la toca en absoluto** (comprobado: ninguna
referencia a tickets en `backend/`, sólo comentarios del schema). Para un usuario sin
tickets cacheados de ese juego, este commit no cambia nada.

**`7663aef` — redactar el título de apps privadas.** Dos cambios en uno:
1. **Estrecha** la condición de `sendGamesPlayed`: de `else if (!owned || getFakeAppId(gameId))`
   a `else if (getFakeAppId(gameId))`, y borra el tracking de `owned`. Correcto: rellenar
   `game_extra_info` sólo hace falta cuando el appId reportado es **falso** (los amigos
   verían "Spacewar" en vez del juego); para un juego simplemente no-poseído el appId es
   real y Steam ya resuelve el nombre solo.
2. **Redacta**: hookea `SetString` del config store, parsea `WebStorage\PrivateApps`
   (formato `[730,240,440]`) a un `unordered_set`, y si el juego está ahí escribe
   `"Redacted"` en lugar del nombre real.

**Relevancia para LumaDeck:** aplica exactamente al flujo del 480. LumaDeck fija
FakeAppIds (`backend/slssteam_ops.py::add_fake_app_id`, default 480 — ver
`FIXES_MAP.md` §"Online multiplayer") para habilitar multijugador. Con esto, un usuario que
además tenga el juego marcado como privado en su perfil **deja de filtrar el título real**
a la lista de amigos. Mejora de privacidad gratis, pequeña y real.

**Menores:** `27f4926` retira el override manual de appId en los hooks de DLC (el título
dice `IClientAppManager` pero el diff toca `IClientApps` — commit mal etiquetado);
`f34025e` sustituye un `printf` perdido en `memhlp` por `g_pLog->debug` (escribía al stdout
de Steam); `32d0ed0` `stringstream`→`ostringstream` en 6 ficheros (el bidireccional no
hacía falta); `a5abb50` paddings `char`→`uint8_t` (un `char` es signed y su signo puede
sorprender al leer bytes crudos); `0684cc3` arregla el typo *"Chocked"*→*"Choked"* **y** de
paso sustituye dos mensajes hardcodeados por el nombre real del paquete/job.

**Observación menor sobre `1250950`:** el toast acumulado se emite con
`g_pLog->notify(errors.c_str())`, o sea **una cadena construida en runtime como format
string** de un `__log` estilo printf, que además acaba en
`system("notify-send … \"<msg>\"")`. Hoy es inofensivo (los nombres de clave son literales
de compilación, sin `%` ni `"` ni `$`), pero es el patrón que muerde en cuanto un nombre
venga de datos. No afecta a lumalinux; anotado por completitud.

**Neto del 26-07 para lumalinux: cero.** Para LumaDeck: la redacción de apps privadas
(gratis) y **dos items de mantenimiento de prioridad baja** en `slssteam_schema.py` —
refrescar `_BUNDLED_YAML` con `CDKeys`/`LogLevels`, y actualizar el docstring que cita el
texto viejo del toast.

#### 7.7.5 Día 2026-07-27 — 16 commits (la capa IPC nombrada, y muere el último hook artesanal)

**Bloque A — la capa IPC deja de ser anónima.** `d1bbc92` + `1082525` renombran
`EInterfaceType` → **`EIPCInterface`** (48 constantes `k_EInterfaceTypeClient*` →
`k_EIPCInterfaceClient*`) y añaden **`EIPCCmd`** (`RunInterface = 1`,
`SerializeCallbacks = 2`, `ConnectPipe = 9`) y **`EIPCExitCode`** (`Success = 0xb`).
`2be9d89` renombra los argumentos del hook (`pBufInterfaceInfo` → `pBufIPCCmd`,
`a2` → `pBufReturn`). Puro nombrado, pero es el paso que convierte "un buffer con bytes"
en "un protocolo con vocabulario" — y sin ese vocabulario el commit siguiente no se puede
escribir.

`e8e04f1` documenta el layout **y deja escrito por qué NO reemplaza todos sus hooks por
este único punto**, que es la pregunta obvia al ver la capa IPC:

> *"While hooking this function to replace the other hooks might seem attractive we do not
> do so. Many calls straight up bypass the IPC layer and go straight for the original VFT
> implementations (IClientAppManager comes to mind). Although it's a great spot to quickly
> test things"*

`b3b2f98` añade la otra confesión del día, sobre el hook de `SetString`:
> *"I really do not like hooking the IClient*Map functions because they are very surface
> level and get skipped over a lot. But for the current purpose it's enough"*

(Lo cumple el 31-07 con `b8ef92f`, bajando el hook a `IClientConfigStore`.)

**Bloque B — `c4152f5`: muere el trampolín naked de `GetSteamID` (§1.7).** El hook artesanal
(pushad/pushfd, `call` a un `stdcall`, popad, instrucciones sobreescritas, `jmp` relativo
ensamblado a mano con `MemHlp::assembleCodeAt`) se sustituye por **interceptar el buffer de
retorno del IPC**:

```cpp
if (type == k_EIPCInterfaceClientUser && exitCode == EIPCExitCode::Success && fnId == 0xD6FC3200)
{
    if (!g_currentSteamId.accountId)
        memcpy(&g_currentSteamId, pBufIPCResult->mem.base + 1, sizeof(CSteamId));
    const CSteamId newId = hkClientUser_GetSteamId(g_currentSteamId);
    memcpy(pBufIPCResult->mem.base + 1, &newId, sizeof(newId));
}
```

Tres claves identifican la llamada: **interfaz** + **exit code** + **id de función**
(`0xD6FC3200`). Deja el desensamblado en comentario explicando por qué era artesanal
(*"IClientUser::GetSteamID has been optimized to hell and back"*: el steamId se lee de
`[edx-0x174E]`/`[edx-0x1752]` y se escribe en `[eax]`/`[eax+4]`, sin prólogo estándar donde
anclar). **Cambia el ancla de "la forma de la función" a "la identidad del mensaje"** —
mucho más estable, y sólo posible tras el bloque A.

Ojo al `fnId = 0xD6FC3200`: es un id **hardcodeado** que no se resuelve por nombre. Es una
constante nueva que puede moverse si Valve reordena su tabla de funciones IPC. Cambia una
fragilidad por otra, más pequeña.

**Bloque C — `3250c2c` + `6d1c7fd`: `g_currentSteamId` de 32 a 64 bits.** Pasa de
`uint32_t` (accountId) a **`CSteamId`** completo (steamId64), y todos los usos se vuelven
explícitos (`g_currentSteamId.accountId` donde antes valía el entero pelado): `apps.cpp`
(`familyShared`, `unlockApp`, el guard de Denuvo), `ticket.cpp` (los dos
`saveTicketToCache`). El ticket-grabber cambia su formato en consecuencia. También borra el
comentario-aviso de `globals.hpp` (*"Don't assign a pointer to IClientUser::GetSteamID!
…it's lifetime is very short"*), que ya no aplica porque ahora se copia por valor del
buffer IPC. Habilitador del `FakeName` del 28-07, que necesita comparar `friendid()`
(64 bits) contra el steamId propio.

**Bloque D — `12a8e2f`: `Updater::isEnabled()`.** Añade una guarda al principio de
`init()` y de `verifySafeModeHash()`:

```cpp
bool Updater::isEnabled() { return g_config.safeMode.get() || g_config.warnHashMissmatch.get(); }
```

Si ninguna de las dos opciones está activa, **ni descarga `updates.yaml` ni calcula el
SHA**. Lógico: en SLSsteam ambas son opciones de config y `SafeMode` viene `no` por
defecto, así que para el usuario típico ese trabajo era íntegramente tirado.

##### 7.7.5.a Veredicto sobre `Updater::isEnabled()` en lumalinux — NO transfiere

En el primer barrido escribí que *"el `Updater::isEnabled()` es un patrón que lumalinux
podría copiar: tu `verifySafeModeHash()` hashea el `steamclient.so` en cada arranque aunque
el resultado sea solo advisory"*. **La premisa es correcta, la conclusión no.**

Correcto: en un build de release (`-DLUMA_NO_UPDATE=OFF`, que es lo que pasa el workflow)
`main.cpp:149-156` llama a `Updater::init()` + `verifySafeModeHash()` en cada arranque, y
su único consumidor es un `Log::Notify` advisory — el comentario del propio código lo dice
en mayúsculas (*"ADVISORY, not a gate"*). No hay kill-switch en runtime: `LUMA_NO_UPDATE`
es `#ifndef`, de compilación.

**Pero la optimización no se puede aplicar, porque el hash NO es prescindible.**
`src/rva_feed.cpp:68` llama a `Utils::getFileSHA256(path)` para keyear
`res/rvas/<hash>.yaml`, y el RVA feed es la ruta **primaria** de resolución de DepotKey,
GMRC, ShaderDepot y Reconcile. El `CMakeLists.txt:126-130` lo deja explícito: `sha256.cpp`
se compila **incondicionalmente** justo por eso. O sea: SLSsteam puede saltarse el hash
porque sin SafeMode nadie lo usa; lumalinux lo necesita igual.

**Y el riesgo de red que motivaría saltárselo ya está resuelto, mejor que en SLSsteam.**
Hubo un incidente real: el `NEEDED` de libcurl/libcrypto se resolvía en *cada* hijo de
`steam.sh` incluido el `reaper` de 32 bits; cuando el runtime de Steam quitó la versión de
símbolo `CURL_OPENSSL_4`, `liblumalinux.so` dejó de cargar ahí y **los juegos rebotaban**.
El apaño de urgencia (0.13.6) fue compilar SafeMode fuera; **0.15.0 eliminó la causa**:
SHA-256 propio sin libcrypto y `dlopen` perezoso de libcurl, o sea cero dependencias de
enlace. **[YA]**, y por delante de él.

**Lo que sí queda, y es pequeño: el hash se calcula DOS veces.** `update.cpp:128` y
`rva_feed.cpp:68` llaman cada uno a `Utils::getFileSHA256` sobre el mismo
`steamclient.so`, sin caché. Medido: SHA-256 de 12 MB son ~12 ms con el de OpenSSL; el de
`src/sha256.cpp` es propio y sin optimizar, así que la pasada duplicada estará en el orden
de **25-50 ms**. Corre en el hilo detached del constructor, igual que los ~76 ms de
escaneo de patrones (§7.7.1.c), o sea **no es visible para el usuario**. Un `static` con
caché por path en `getFileSHA256` lo elimina; **no merece un cambio por sí solo**, sí
merece incluirse si alguna vez se toca esa zona.

**Observación NO verificada, con números (merece una mirada, no una acción).** En un build
de release la instalación de hooks encadena **dos fetches HTTPS secuenciales** antes de
terminar: `update.cpp:39` (`updates.yaml`) y `rva_feed.cpp:55` (`res/rvas/<hash>.yaml`).
Ambos usan los **defaults** de `Curl::getString` — `connectTimeoutSec = 15`,
`totalTimeoutSec = 30`. Con una red *colgada* (no ausente: un portal cautivo o un firewall
que traga SYN), el peor caso son ~60 s antes de que `InstallHooks()` acabe — y con él el
arranque del package-0 finder y la escritura de `status.json`, que es lo que LumaDeck lee
para el badge de salud. Con red ausente curl falla rápido, y ambos tienen caché en disco
como fallback (pero sólo *después* del timeout). El comentario de `curl.hpp` demuestra que
el tema está pensado para GMRC (*"so a hung endpoint can never block the calling
thread"*), y la sonda de ShaderDepot ya pasa valores cortos; la ruta de arranque se quedó
con los generosos. **No verificado on-device y puede ser perfectamente aceptable** — el
usuario tarda mucho más de 60 s en llegar a pulsar Install. Se anota como pregunta.

**Bloque E — higiene.** `4e79bcd` cambia `unique_lock` por `lock_guard` en `log.hpp` y
`mtvar.hpp` (ninguno necesitaba unlock manual ni movibilidad) **y de paso revierte el
`std::ios::app` de ayer** (`62afc2e`) a un `std::ofstream(path)` normal — o sea, el cambio
latente truncar→añadir de `createFile` que anotamos en §7.7.4 lo corrigió él al día
siguiente. `42a23ff` añade `SavedTicket::isValid()` y sustituye los `!ticket.ticket.size()`
/ `!steamId.isSet()` dispersos. `871f065` `map`→`unordered_map` para los tickets (no hacía
falta orden). `f91ceaf` da Makefile propio a `library-inject`, `schema-grabber` y
`ticket-grabber`, con rebuild de los subproyectos .NET cuando cambian; `9d6a05d` limpia un
resto del ticket-grabber en el schema-grabber; `a9b5135` apunta el build de nix a
`audit-libs` con todos los cores. `30acee8` arregla el formato de un **easter egg**: si el
día es **22 de febrero**, `notifyInit` saca un mensaje especial.

**Neto del 27-07 para lumalinux: cero accionables.** Una retractación más
(`Updater::isEnabled()`), un [YA] a favor de lumalinux (la ruta libcurl/libcrypto), un
hallazgo real pero por debajo del umbral de acción (doble SHA-256, ~25-50 ms en hilo
detached), y una pregunta abierta con números (los dos fetches secuenciales con timeouts de
30 s en la ruta de instalación de hooks).

#### 7.7.6 Día 2026-07-28 — 28 commits, release `20260728212859` (el día más cargado)

**Lo primero: la rotura de `sls_achievement_unblock` NO empezó aquí.** Ver la caja roja de
§7.6. `20260728212859` es la release donde se **detectó** (fue la que había en el Deck
cuando se root-causeó), no donde se introdujo: la firma cambió en `59f8259` el **20-07** y
la primera release afectada es **`20260722152506`** (22-07). Dos releases y seis días antes.
Y el refactor de tipos de ESTE día (`f624777`, `CUtl*` de `struct` a `class` con `public:`)
**no altera ningún mangled name** — `struct` y `class` manglean idéntico —, así que la
atribución vaga que hice en el primer barrido ("coherente con la reescritura masiva de tipos
del 28-07") también era falsa.

**Aportes de la comunidad.** `a8c468d` + `f391f19` de **`_drazy` / Deadboy666** actualizan
`res/updates.yaml` (PRs #146 y #147), mergeados de madrugada. Es el mismo Deadboy666 de
Headcrab, que ya figura en los créditos de LumaDeck — el ecosistema se realimenta.

**La saga del ticket cifrado — cuatro commits que se pisan entre sí en un día.** Es la
historia más instructiva del rango porque se ve el método de Ace en vivo:
1. `3c520bd` **"Only spoof steamId when necessary"**: añade un hook a
   `IClientUser::GetEncryptedAppTicket` para armar un spoof de un solo uso, y **saca** de
   `getCachedEncryptedTicket` la guarda de FakeAppIds que había puesto el 26-07.
2. `b8b0b51` **"Improve threading & timing"**: `oneTimeSteamIdSpoof` pasa de variable única
   a **`unordered_map<AppId_t, CSteamId>`**. Bug real: con dos juegos a la vez, uno podía
   consumir el spoof del otro.
3. `3b2e0d8` **"Restore old encrypted Ticket behaviour"** — la marcha atrás, con el
   razonamiento escrito: *"Denuvo can be tricked by switching after a variable amount of
   GetSteamId calls. But it's to unreliable, needs custom amounts per game and would add a
   lot of clutter to the config. So it's axed until I can find a safe timing"*. Restaura la
   guarda y **comenta** (no borra) el `.place()`/`.remove()` del hook que acababa de añadir
   en el paso 1. El hook queda instalado-pero-no-colocado, listo para reactivar.
4. `8d5c4f9` **"Optimize code by using pointers & skip copy"**: `getCachedTicket` pasa de
   devolver `SavedTicket` por valor a `SavedTicket*`, construyendo **dentro** del mapa
   (`SavedTicket& ticket = ticketMap[appId];`) en vez de construir-y-copiar. `nullptr` en vez
   de un ticket vacío como señal de "no hay".

   El patrón que se repite: **prueba una idea, la mide contra la realidad, y la retira
   dejando el andamio comentado y el motivo escrito.** No borra el trabajo, lo desactiva.

**Config nueva.** `f7926b5` **`SteamIdOverride`** (mapa `appId → steamId64`; `0` = usar el
del ticket cacheado) para juegos que llaman a `GetSteamId` **antes** de pedir su ticket, y
como workaround de partidas guardadas bloqueadas. `4390e1a` + `a630b7e`/`8da82c2`
**`FakeName`**: intercepta `CMsgClientPersonaState`, busca tu propio `friendid()` y reescribe
`player_name` — habilitado por el paso a 64 bits del 27-07, porque hay que comparar contra
`g_currentSteamId.steamId64`. `c61df24` + `d942ada` corrigen el tipo de `DenuvoGames` de
`uint32_t` a `uint64_t`/`CSteamId`: la clave es un **steamId completo**, y con 32 bits el
guard anti-Denuvo comparaba mal.

**Robustez.** `3f56397` arregla un crash cuando se loguea **durante el apagado de Steam**
(comprueba `ofstream.is_open()` antes de escribir; las operaciones encoladas llegaban con el
stream ya cerrado). `e7eb27a` sólo llama a `getTicketOwnershipExtendedData` **si la original
devolvió tamaño** — antes lo hacía siempre, incluso cuando no había ticket. `bfc3458` añade
`/run/current-system/sw/bin/curl` a la cascada de `execve` **para NixOS** (nota: en esta
época `Curl::getString` todavía lanzaba `curl` por `execve`, no libcurl).

**Release y limpieza.** `22b49e7` bumpea a `20260728212859`, `0c6d8ec`/`1906382` los
PKGBUILDs, `0919403` `res/updates.yaml`. `f624777` convierte `CUtl*` de `struct` a `class`
con `public:` (cosmético, sin efecto en mangling ni en layout); `3f2bc38` borra el
`CSteamID.hpp` viejo ya sin usar; `81f4e8e`/`1a06744`/`f828ba5` estilo (ordenar hooks
alfabéticamente, quitar `&` redundantes delante de los `hk*`, un typo en `VFTable::analyze`).

##### 7.7.6.a Correcciones aplicadas a la documentación de lumalinux

Este día produjo **el primer hallazgo del barrido que corrige código-adyacente y no sólo
prosa**, aunque sigue siendo documentación:

1. **§7.6 de este doc** declaraba `sls_achievement_unblock` *"verificado al fuente contra
   `20260723102618`"* con *"firmas sin cambio en `src/feats/achievements.{cpp,hpp}`"*. Era
   **falso**: en esa misma release la firma ya era `4EMsg` y el parche ya no-opeaba en
   silencio. Corregido con la tabla release-a-release.
2. **`RESEARCH.md` §17** atribuía el cambio a "SLSsteam `20260728`". Corregido a `59f8259`
   (20-07), primera release `20260722152506`, con la nota de que `20260728212859` es donde
   se detectó.
3. **Lección de método, anotada en §7.6:** verificar "firmas sin cambio" grepeando el
   **HEAD actual** no dice nada sobre si cambiaron *desde la verificación anterior*. Para
   símbolos resueltos por nombre mangled el chequeo correcto es
   `git log -S"<firma>" -- <fichero>` entre releases, no un grep del presente. Es
   exactamente el error que dejó la rotura invisible seis días.

**Neto del 28-07 para lumalinux: cero cambios de código** (el fix por prefijo ya cubre toda
la ventana, incluida la parte que no estaba documentada), **dos correcciones de doc** y una
lección de método que vale para cualquier futuro parche anclado en símbolos.

#### 7.7.7 Día 2026-07-30 — 6 commits (fugas de descriptores y el callback afinado)

**`cbd0cd0` — fuga de descriptores en `Curl::getString`.** En esta época su curl todavía era
`fork()` + `execve("curl")` + `pipe()`. Dos fugas, ambas reales:
- Si `fork()` falla, se retornaba **sin cerrar ninguno de los dos extremos** del pipe.
- Tras el bucle de lectura, **el extremo de lectura nunca se cerraba**.

Severidad: `Curl::getString` no es una llamada de arranque aislada — `getReviewersForGame`
la usa **por app** en la ruta de logros nativos (`https://store.steampowered.com/appreviews/…`),
con reintentos. O sea **un fd filtrado por cada fetch de schema**, acumulándose durante toda
la sesión de Steam. Encaja con el `3b97ac2` del 04-08, que reduce drásticamente cuántas
veces se llama a esa función: los dos atacan la misma presión desde lados distintos.

**Para lumalinux: no aplica. [YA]** — `src/curl.cpp` es una **reescritura**, no un port: usa
libcurl con `dlopen` y un `CURLOPT_WRITEFUNCTION` que hace `data->append(...)`. Cero
`fork`, cero `pipe`, cero `execve` (comprobado: 0 ocurrencias). La clase de bug no existe
ahí. La reescritura se hizo por el incidente del `reaper` (§7.7.5.a), y de rebote inmunizó
contra esto.

**`a673d04` — buffer de lectura de 128 a 8192 bytes.** El JSON de reviews es de decenas de
KB (`num_per_page` = `maxSchemaTries`), así que leerlo en trozos de 128 bytes eran cientos de
`read()` y otros tantos append al `ostringstream`. 64× menos syscalls por fetch.

**`e2ac776` — el centinela equivocado en el parser de funciones.** `parseFunction` inicializa
`lm_address_t leaOffset = LM_ADDRESS_BAD` (que es `(lm_address_t)-1`, o sea `0xFFFFFFFF`), y
la guarda decía:

```cpp
if (!leaOffset) continue;      // "salta si no está puesto"  ← compara contra 0
```

Pero el centinela de "no puesto" es `LM_ADDRESS_BAD`, no `0`. Así que **el caso que la guarda
pretendía saltar era justo el que no saltaba**: antes de encontrar el thunk PIC, `leaOffset`
valía `0xFFFFFFFF`, `!leaOffset` era falso, y el código seguía a la lógica de `lea`
calculando `targetAddr = 0xFFFFFFFF ± offset` y buscándolo en el mapa de strings. Casi
siempre un fallo inocuo, pero podía producir una **referencia a string espuria**. Arreglado a
`if (leaOffset == LM_ADDRESS_BAD) continue;`.

Es **la misma clase de bug que el `retf` del 23-07** (§7.7.1): el parser de funciones
divagando donde no debe y contaminando el mapa `nombre → índice de slot` del que ahora
depende todo el hooking. Dos endurecimientos del mismo punto crítico en una semana.

**`146e28f` — `AppLicensesChanged_t` sólo para el appList que cambió.** Su mensaje: *"Previously
we just invoked it for any appInfo received. Didn't cause any issues but was lazy"*. Añade
`pendingLicenseChanges` (con mutex) y un early-return si no hay apps nuevas:

```cpp
// runIPCFrame: sólo pide appinfo para lo nuevo, y lo marca pendiente
const auto added = g_config.newApps;
if (!added.size()) return;
...
pendingLicenseChanges.emplace(appId);

// parseProductInfoFromResponse: sólo emite callback para lo que pidió
if (!pendingLicenseChanges.contains(app.appid())) continue;
set.emplace(app.appid());
pendingLicenseChanges.erase(app.appid());
```

##### 7.7.7.a ¿Afecta al live refresh de LumaDeck? Trazado: NO, y explica por qué

Dije en el primer barrido que esto *"toca directamente el live refresh de LumaDeck"*.
Trazadas ambas rutas, **no lo toca** — y el porqué merece quedar escrito, porque revela que
los dos mecanismos de refresco **no son redundantes sino complementarios**.

**Son dos señales distintas, con disparadores distintos:**

| | SLSsteam | lumalinux |
|---|---|---|
| Callback | `AppLicensesChanged_t` | `LicensesUpdated_t` (`ECallbackType` 0x7d) |
| Cómo | `Apps::postAppLicensesChanged(set)` | `CUser::NotifyLicensesUpdated(user)` |
| Disparador | recarga de `config.yaml` → diff de `AdditionalApps` | cambio en **`keys.txt`** (watcher) |

**El diff de SLSsteam** (`config.cpp:203-212`): al recargar, `newApps` = los appIds de
`AdditionalApps` que **no estaban** en la carga anterior.

**Traza del flujo de "Add Game" de LumaDeck:** añade el appid a `AdditionalApps` → `os.replace`
+ poke → SLSsteam recarga → `newApps` **contiene** el appid → `runIPCFrame` pide su appinfo y
lo marca pendiente → llega la respuesta PICS → el callback se emite **para él**. **Funciona**,
y el estrechamiento está exactamente alineado con el caso que LumaDeck crea.

**Dónde el estrechamiento sí cambia algo — y por qué tampoco importa:** en un
**re-deploy** sobre un juego **ya añadido** (el flujo *"Fix Update"*), el appid **no es nuevo**
en el diff → `newApps` vacío → `runIPCFrame` sale temprano → nada en `pendingLicenseChanges`
→ si más tarde llega appinfo de ese juego, **antes** el callback se emitía y **ahora no**.
Pero ese flujo reescribe `keys.txt`, que es lo que dispara el watcher de lumalinux →
`Reconcile()` → `LicensesUpdated_t`. **La cobertura viene por la otra ruta**, no por ésta.

**Conclusión: cero impacto**, y una nota de arquitectura para el futuro: si alguna vez se
plantea retirar el reconcile de lumalinux por "ya lo hace SLSsteam", **no es cierto** — el de
SLSsteam sólo cubre apps *nuevas en el diff de config*, y desde el 30-07 lo hace de forma
deliberadamente estrecha. *(Análisis de rutas de código, no verificado on-device: no he
comprobado cuál de los dos callbacks es el que realmente refresca la UI de Steam.)*

**Menores:** `2b4a039` merge de `main` a `dev`; `b3da36b` PKGBUILDs.

**Neto del 30-07 para lumalinux: cero accionables.** Un [YA] a favor (la fuga de fds no
existe en la reescritura de `curl.cpp`) y una pregunta cerrada con traza (el callback afinado
no afecta al live refresh, y los dos mecanismos son complementarios).

#### 7.7.8 Día 2026-07-31 — 8 commits (cumple su propia queja, y endurece la config)

**`b8ef92f` — `IClientConfigStoreMap` → `IClientConfigStore`.** Cumple, cuatro días después,
la queja que se había dejado escrita el 27-07 (§7.7.5): *"I really do not like hooking the
IClient\*Map functions because they are very surface level and get skipped over a lot"*. Baja
el hook del wrapper del mapa a la **clase real**, resuelta por RTTI (`12CConfigStore`).

Y aquí se ve el truco que sostiene todo su esquema: coge el **índice** de
`21IClientConfigStoreMap` (que es donde viven los literales de nombre que su decompilador
lee) y lo aplica a la **vtable de `12CConfigStore`**:

```cpp
IClientConfigStore_SetString.setup(
    VFTIndexes::IClientConfigStoreMap::SetString.getPrintName().c_str(),
    store.functions[VFTIndexes::IClientConfigStoreMap::SetString.index],   // ← índice del Map, vtable de la impl
    hkClientConfigStore_SetString);
```

O sea **asume correspondencia de orden de slots entre el wrapper `*Map` y la implementación**.
Es lo que permite nombrar slots de clases que no llevan los literales encima. Supuesto no
declarado y no verificado por él, pero que funciona en la práctica. *(Es también el mecanismo
que §7.7.2.a evaluó para DepotKey y descartó: allí el problema no es la correspondencia sino
que el accesor no tiene un nombre distintivo.)*

**`00c93c6` — nombres de funciones sobrecargadas.** Bug real del mapa `nombre → índice`: si
dos slots referencian **el mismo literal** (funciones sobrecargadas, p.ej. dos `SetString`
con firmas distintas), el segundo **sobreescribía** al primero en `functionMap[str] = i`.
Ahora, si la clave ya existe, va probando `nombre2`, `nombre3`… hasta encontrar hueco.
Tercer arreglo del mismo punto crítico en nueve días (tras el `retf` del 23-07 y el
centinela `leaOffset` del 30-07): el mapa nombre→slot del que depende su hooking entero.
Nótese el cambio de `const auto& str` a `auto str` — necesita copia porque ahora lo modifica.

**`284115b` — abortar si falla la creación de la config.** `CConfig::init()` pasa de
"si `createFile()` va bien, monta el watcher; en cualquier caso `loadSettings`" a **retornar
`false` y no seguir**. Motivo suyo: *"the tickets assume the config directory was created
successfully"* — `Ticket::getTicketDir()` construye `<configDir>/cache` y hace
`create_directories`. Antes, un fallo de permisos en el directorio dejaba SLSsteam corriendo
con defaults y los tickets escribiendo a un sitio inexistente; ahora aborta limpio.

**`4bfe8e2` — el buffer de paquetes.** `g_packetsArrayIndex` (`uint32_t`, índice) pasa a
`g_packetsArrayOffset` (`uintptr_t`, offset en bytes), y documenta los límites: *"Biggest
message I have observed was around 600kb"* → `MAX_PACKET_SIZE` 1 MB × `MAX_PACKETS` 8 = **8 MB
de arena estática** para serializar paquetes. Es la arena que el 06-08 (`0fc9cf9`) sustituirá
por el asignador propio de Steam.

**Menores.** `ac12369` actualiza el *"hall of shame"* del README (ver nota abajo). `e15dcce`
renombra `hkBUpdateOwnershipTicket`→`hkBUpdateAppOwnershipTicket`; `57b80e3` un newline doble.
`5f946ec` cambia `if (!name.size())` por `if (name.size() < 1)` — el commit dice *"something
more logical"*, pero **son semánticamente idénticos** (`size()` es unsigned); es preferencia
de estilo, no un arreglo.

##### 7.7.8.a Contexto de ecosistema — el "hall of shame" y por qué nos concierne

`ac12369` reescribe la entrada de un proyecto y **le añade un plugin de Decky**:

> *"OnetapBeta **& Hammer Decky** by Hammer Steam: Resells Steamless & SLSsteam for a bogus
> price, completely breaking licensing agreements and leeching off the communities hard work
> while putting in 0 effort themself."*

Relevante porque **LumaDeck es también un plugin de Decky que instala SLSsteam**, o sea juega
en el mismo terreno que Ace está señalando públicamente. La diferencia es de postura, y
LumaDeck está en el lado correcto por construcción, no por suerte:
- **Gratis y MIT**, sin reventa.
- **Acredita a AceSLS/SLSsteam de forma prominente** en la tabla de créditos del README.
- **No re-empaqueta SLSsteam**: lo descarga de upstream vía el `setup.sh` de lumalinux, así
  que el usuario recibe el binario de Ace, no una copia renombrada.
- Y lumalinux **nunca forkea** SLSsteam (§0, §5): coexiste y aplica un parche reversible en
  memoria, con los ficheros portados marcados AGPL-3.0 y atribuidos fichero a fichero.

No hay nada que cambiar. Se anota porque conviene saber que el upstream vigila activamente
este espacio, y porque la postura de atribución que ya seguimos **es** lo que nos distingue.

##### 7.7.8.b Hallazgo colateral en LumaDeck — `set_sls_value` es destructivo [MINA, no bug activo]

Encontrado tirando del hilo de `284115b` (permisos y creación de config), no de un cambio de
SLSsteam. **No es un bug activo: es una mina.**

`main.py` expone tres métodos de config de SLSsteam:

```python
async def read_sls_config(self) -> str:   # inofensivo (lectura)
async def get_sls_value(self, key) -> str: # inofensivo (lectura)
async def set_sls_value(self, key, value) -> str:   # ← DESTRUCTIVO
    from slssteam_config import set_value
    set_value(key, value)
```

`set_value` (`backend/slssteam_config.py:88`) hace `read_config()` → mutar dict →
`write_config()`. Y ese par **no preserva el fichero**:
- `_read_yaml` hace `line.strip()` **antes** de parsear, o sea **descarta la indentación**.
  Un `  - 480` bajo `AdditionalApps:` queda en `- 480`, sin `:`, y se **salta**. Un
  `  1234: 5678` bajo `FakeAppIds:` se parsea como clave **de primer nivel** `1234`.
- `_write_yaml` reescribe **el fichero entero** como líneas planas `key: value`.

Efecto de **una sola** llamada: se pierden los comentarios y, sobre todo, **todo el contenido
anidado** — `AdditionalApps` (¡los juegos!), `DlcData`, `FakeAppIds`, `ManifestIds`,
`CDKeys`, `DenuvoGames`. `AdditionalApps` quedaría como `AdditionalApps: ` vacío → SLSsteam
deja de fingir propiedad → **los juegos de LumaDeck dejan de funcionar**, en silencio.

**Por qué NO está pasando hoy:** comprobado que **ningún** wrapper de `src/api.ts` ni ninguna
página lo invoca (grep sobre `api.ts`, `pages/*.tsx`, `components/*.tsx`: cero). Está expuesto
como RPC pero muerto desde la UI. Sólo se dispara si alguien añade el wrapper o lo llama desde
la consola de Decky.

**Por qué merece cerrarse:** el resto del backend ya hace lo correcto —`slssteam_ops.py` edita
**por líneas y append-only**, y §4.3/`slssteam_schema.py` insisten en no perder un byte—. Este
par es la única vía que contradice ese invariante, y su nombre (`set_sls_value`) invita a
usarlo. Opciones: borrar los tres métodos, o reimplementar `set_value` sobre el editor por
líneas de `slssteam_ops.py`. **Pendiente de decisión del dev; no tocado.**

*(Aparte y menor, sin verificar on-device: el backend de Decky corre como **root** en Game
Mode —`plugin.json` lleva `flags: ["_root"]`, y `paths.py:561` lo dice— y `_commit_config`
escribe el `.tmp` con `open()` normal antes del `os.replace`, así que el `config.yaml`
resultante queda **root:644**. Legible por SLSsteam, o sea sin rotura funcional; pero el
usuario ya no puede editarlo a mano sin `sudo`, y SLSsteam está diseñado alrededor de un
config editable a mano.)*

**Neto del 31-07: cero accionables de SLSsteam**, y un hallazgo colateral en LumaDeck que es
la primera cosa del barrido que apunta a código propio y no a documentación.

#### 7.7.9 Días 2026-08-01 y 2026-08-02 — 6 commits (jornadas flojas)

Dos días de poco volumen y ningún cambio de mecanismo. Se documentan por completitud del
barrido; **no hay nada accionable ni prestable en ninguno de los dos**.

**01-08 (3 commits).**

`46b6ff5` **"Search all functions before aborting"** — `VFTIndexes::init()` pasa de abortar en
el primer `VFTableInfo_t` que no resuelve, a intentarlos **todos** y devolver `false` si alguno
falló:

```cpp
bool success = true;
for(const auto& fn : functions)
    if (!fn->init()) success = false;   // antes: return false
...
return success;
```

Comprobado el llamante (`main.cpp:184`): sigue abortando igual (*"Failed to parse VFTables!
Aborting…"*). O sea **el cambio es puramente de diagnóstico**: si Valve reordena una clase, el
log muestra de una vez *todos* los índices afectados en lugar de obligar a arreglarlos de uno
en uno, arranque a arranque. El bloque que hay tras el bucle es sólo el `dump` de
`DumpClientInterfaces`, no lógica de control.

*(Hazard latente, no de hoy: `VFTableInfo_t::init()` tiene efecto secundario — fija `index` y
`address`. Con el early-return, un fallo dejaba la tabla intacta; ahora deja **algunas
entradas resueltas y otras no**. Es inocuo porque el llamante aborta, pero si alguien
degradara ese abort a un warning, tendría una tabla a medias.)*

El mismo commit incluye, sin relación con su título, un renombrado en el hook de
`BuildDepotDependency` (`void* a0` → `pClientAppManager`). Commit mezclado.

**Para lumalinux: [YA], y por delante.** `InstallHooks()` (`src/main.cpp:218-241`) ya recorre
**todos** los hooks, registra el resultado de cada uno (`Status::RecordHook` →
INSTALLED/DISABLED/FAILED), acumula la lista de fallidos y reporta `X/Y hooks active` en el
toast y en `status.json`. El mismo principio, y además **expuesto a LumaDeck** para el badge de
salud, que es un paso más de lo que hace él.

`2281333` **`AppOwnershipInfo_t::region`** — `char region[2]` + `char field7_0x1A[2]` se
fusionan en `char region[4]`, con el motivo: *"Client copies this like a DWORD, so even though
CountryCodes are only 2 bytes 4 seems to be correct"*. Ojo con no dramatizar esto: **el tamaño
total no cambia** (2+2 = 4), así que **todos los offsets posteriores son idénticos** — es una
*fusión de campos*, no un cambio de layout. Y comprobado que **nadie lee `region`** (lo que se
usa es `regionRestricted`, otro campo). Impacto conductual: **cero**. Es documentación del
struct, del mismo estilo que el `CUtlBuffer::flags` del 24-07.

**Frontera:** `AppOwnershipInfo_t` es la capa de propiedad de SLSsteam (§5) y lumalinux no la
conoce — comprobado, cero referencias a `AppOwnershipInfo` en `src/`. Nada que mirar.

`7cf6636` PKGBUILDs.

**02-08 (3 commits).**

`03a4a96` **"Fix exiting when connection to Steam was lost"** — bug real en los dos tools de
.NET. `OnDisconnected` y `OnLoggedOff` imprimían `"Disconnected from Steam! Exiting..."` pero
**nunca ponían `finished = true`**, así que el bucle de callbacks seguía girando: el mensaje
mentía y el proceso **se quedaba colgado para siempre**. Dos líneas, un cuelgue menos en
`schema-grabber` y `ticket-grabber`.

`fdefbe5` borra una declaración muerta en `CNetPacket.hpp` y el `tools/ticket-grabber/build.sh`
(6 líneas) — resto de la migración a Makefiles del 27-07 (`f91ceaf`), que dejó los `build.sh`
sin uso.

`ae9428a` cambia 4 espacios por tabulador en los dos `Program.cs` (812 líneas en cada
dirección). **Es lo que explica el diffstat abultado del día** (~816/819) — nada de fondo. Y
tiene gracia el orden: en `03a4a96` añadió los dos `finished = true;` **con tabulador** en el
ticket-grabber mientras el fichero usaba espacios (se ve en el propio diff, `+\t\t\tfinished`
frente al `+            finished` del schema-grabber), y el commit siguiente normaliza los
ficheros enteros a tabulador. La inconsistencia se la provocó él mismo el commit anterior.

**Neto de los dos días para lumalinux: cero accionables, un [YA]** (la recolección de fallos
de todos los hooks, que lumalinux ya hace y además publica en `status.json`).

#### 7.7.10 Día 2026-08-04 — 3 commits (los stutters de los logros nativos)

**`a1b30e8` + `690fb8a`** — hash de cliente 2026.08.04 en `res/updates.yaml`, y merge de
`main` a `dev`. Rutina.

**`3b97ac2` — "Fix stutters caused by review fetching".** El commit con más impacto real
para LumaDeck de toda la segunda mitad del rango, porque ataca un hazard que **nuestro propio
parche amplifica**.

**El problema, en sus palabras:** *"Some games just ruthlessly spam GetUserStats, which in turn
just spams getReviewersForGame which causes massive stuttering."*

Traducido al flujo de §7.3: cada petición de stats de un juego no-poseído disparaba
`getReviewersForGame(appId)` → un `Curl::getString` **bloqueante** (en esta época todavía
`fork`+`execve("curl")`) a `store.steampowered.com/appreviews/…`, **en el hilo de la petición
del juego**. Un juego que pide stats en bucle producía una petición HTTP por vuelta. De ahí los
tirones.

**El arreglo:** memoriza el dueño que funcionó y **sáltate el fetch entero**.

```cpp
//Prefer last successfull owner to skip review fetch. Fixes stutters caused
//by review fetching in games that spam stat requests
if (preferredOwners.contains(appId)) {
    const uint32_t res = tryGetPlayerStats(..., preferredOwners.at(appId));
    if (res == k_EResultOK) return res;
    preferredOwners.erase(send->appid());     // falló -> se repite el proceso entero
}
const auto reviewers = getReviewersForGame(send->appid());   // sólo si no hay preferido
```

`preferredOwners[appId] = steamId` se fija dentro de los helpers nuevos (`tryGetPlayerStats` /
`tryGetUserStats`, extraídos en este mismo commit) **sólo** cuando el resultado es
`k_EResultOK`. Y conserva la disciplina que ya tenía en el blacklist: sólo se apunta como
fallido un `k_EResultFailure` confirmado, **no** un `NoConnection` — un corte de red no quema
un dueño válido.

Efecto en régimen estacionario: de *una petición HTTP por llamada a stats* a **una por juego y
sesión** (mientras el dueño preferido siga sirviendo).

##### 7.7.10.a Por qué esto nos toca más que a un usuario de SLSsteam vanilla

Porque **nosotros somos los que ponemos a los juegos de LumaDeck en esa ruta**. Recordando
§7.3: la descarga nativa escribe una licencia local real, así que `isSubscribed(appId)` devuelve
**true** para un juego de LumaDeck y el guard salta el borrow — de ahí que
`sls_achievement_unblock` reescriba el guard a `isSubscribed && !isAddedAppId` para que los
juegos añadidos **sí** lo alcancen.

Consecuencia directa, que §7.3 ya anticipaba (*"nuestro parche ensancha la población de apps que
lo alcanzan"*): un usuario de LumaDeck con logros nativos activos entra en el camino del borrow
para **todos** sus juegos añadidos, mientras un usuario de SLSsteam vanilla sólo lo hace para
los realmente no-poseídos. Los stutters, por tanto, nos pegaban más. Y el arreglo nos beneficia
más. **Argumento concreto para recomendar SLSsteam ≥ `20260815201341` a los usuarios de
LumaDeck**, junto con el `IN_MOVED_TO` de §4.1 y los `CDKeys` de §7.2.

**Y el matiz que hay que anotar, no celebrar:** el arreglo **añade un segundo contenedor
compartido sin sincronizar**. `preferredOwners` es un `std::unordered_map` global, escrito
(`[appId] = steamId`, líneas 113 y 200) y leído/borrado (`contains` → `at` → `erase`, líneas
142-150 y 221-230) **desde las dos entradas**, igual que `ownerBlacklist`. Comprobado a HEAD:
**cero mutex en todo `achievements.cpp`**. Como esas entradas cuelgan de los hilos de job/RPC
de Steam, dos peticiones concurrentes sobre distintos appIds pueden hacer insert/erase a la vez
sobre el mismo mapa → UB (los `unordered_map` de la stdlib no son thread-safe ni para claves
distintas).

O sea: **baja la frecuencia con la que se pisa el hazard, no lo sincroniza.** En la práctica
la ventana se estrecha mucho (menos llamadas, y el camino rápido sale antes), así que el neto
es claramente positivo — pero la nota de §7.3 pasa de "un mapa sin lock" a **dos**. No es
accionable por nuestra parte (es código de SLSsteam, y parchearlo cruzaría la frontera de §5
por una tercera vez sin necesidad); se anota como riesgo conocido de la ruta que habilitamos.

**Neto del 04-08: cero cambios de código, una mejora gratis que nos toca de lleno, y una
actualización del hazard de §7.3.**

#### 7.7.11 Día 2026-08-05 — 14 commits (el hook IPC baja un nivel, y dos arreglos de FakeAppIds)

**`77f5d44` — `CSteamEngine::RunInterface` → `CSteamEngine::ProcessIPCFrame`.** Sube un
escalón en la pila: `ProcessIPCFrame(this, HSteamPipe pipe, CUtlBuffer* in, CUtlBuffer* out)`
recibe **todos** los comandos IPC, y él despacha según `EIPCCmd`:

```cpp
const EIPCCmd cmd = *reinterpret_cast<EIPCCmd*>(pBufIn->mem.base + 0);
if (cmd == EIPCCmd::RunInterface) { /* todo lo de antes */ }
else { ret = tramp.fn(pSteamEngine, pipe, pBufIn, pBufOut); }   // pasa de largo
```

`RunInterface` era **el comando 1**; ahora ve la superficie entera. Gana además el
**`HSteamPipe`** explícito en la firma (antes había que pedírselo a
`utils->getCurrentSteamPipe()`), y de paso identifica un comando nuevo:
`EIPCCmd::CreateGlobalUser = 3` (*"Also used to connect to global user"*). El comentario de
07-27 explicando por qué **no** reemplaza los demás hooks desde aquí se conserva íntegro — el
motivo sigue vigente: muchas llamadas se saltan la capa IPC.

**Los dos arreglos de FakeAppIds — el bloque que nos toca.**

`50c439a` **"Fix some callbacks not reaching fakeAppId enabled games"**. Hook nuevo
(`DetourHook`, patrón `CUser::PostCallbackToAppId`) que **reencamina el destinatario**:

```cpp
const AppId_t fakeAppId = FakeAppIds::getFakeAppId(appId);
if (fakeAppId) { g_pLog->debug("Rerouting callback from %u to %u\n", appId, fakeAppId); appId = fakeAppId; }
```

El problema: Steam publica callbacks **dirigidos al appId real**, pero el juego corre
registrado como el **falso** — así que nunca los recibía. Es un agujero silencioso: no
crashea, simplemente falta funcionalidad que depende de callbacks.

`50eed0d` **"Fix lobbies getting hidden due to fake & real appId missmatch"**. Hook sobre
`IClientFriends::GetFriendGamePlayed` (RTTI `12CUserFriends`), reescribiendo el appId reportado
de un amigo **del falso al real**:

```cpp
if (fakeAppId && fakeAppId == gamePlayed->appId) gamePlayed->appId = realAppId;
```

O sea: cuando el amigo aparece jugando **tu mismo appId falso**, se traduce a tu appId real, de
forma que el juego —que pregunta "¿está mi amigo jugando a *mi* juego?" usando el real— lo
reconozca y no oculte el lobby / el "Unirse a partida". Detalle de privacidad suyo: *"We do not
log this function, it's basically useless since we don't want any SteamIds in the logs"*.

**Relevancia directa para LumaDeck: alta, y `50eed0d` describe literalmente nuestro caso de
uso.** LumaDeck fija FakeAppIds para habilitar multijugador (`fixes.py`, valor tomado del
`OnlineFix.ini`; y el toggle manual "Native Online" con 480 fijo — ver §7.7.4). El escenario
**dos usuarios de LumaDeck con FakeAppId 480 en el mismo juego queriendo verse los lobbies** es
exactamente lo que este commit arregla. Y `50c439a` arregla callbacks perdidos en cualquier
juego con FakeAppId. **Cuarta razón acumulada** para recomendar SLSsteam ≥ `20260815201341`
(tras `IN_MOVED_TO` §4.1, `CDKeys` §7.2 y los stutters §7.7.10).

**`913aca1` — aviso nuevo en la config**, y merece nota: añade a `FakeAppIds` la línea
*"Do not run multiple apps under the same AppId simultaneously!"* (que en HEAD se amplía con
*"It's possible but will most likely cause undefined behaviour"*).

Nos aplica en teoría: LumaDeck **puede** acabar con varios juegos mapeados al mismo 480 (es lo
normal — es el appId que usa OnlineFix), y comprobado que **no hay ningún aviso en la UI**
sobre ejecutarlos a la vez (grep en `i18n.ts` y `GameDetail.tsx`: nada). Pero la exposición
real en un Deck es **baja**: el Game Mode ejecuta un juego a la vez, así que llegar al caso
requiere Desktop mode o un atajo no-Steam en paralelo. **No accionable hoy.** Si alguna vez se
reporta, el dato ya está a mano: `slssteam_ops.list_fake_app_ids()` devuelve el mapa
`{realId: fakeId}` completo, así que detectar "N juegos comparten el 480" y avisar es UI de una
línea.

**Refactors y SDK.** `d566a87` mueve la política dentro de la feature: `FakeAppIds::runIPCFrame`
pasa a recibir la interfaz y comprobar `shouldUseRealAppIdForInterface` **él mismo**, así que el
hook deja de duplicar la condición pre/post. Comportamiento idéntico, menos acoplamiento.
`f79b238` convierte `EIPCInterface` en `enum class` (ida y vuelta: el 27-07 lo había hecho
`enum : uint8_t`). `73a710c` sustituye el relleno campo-a-campo del `CSteamId` de 32→64 bits por
la constante canónica: `steamId64 |= 0x0110000100000000` (universo público + cuenta individual),
dejando el código viejo comentado. `c727ff1` y `de76936` amplían `EIPCCmd` y `CUtlBuffer`;
`ec239e6` añade `IClientFriends.hpp` (soporte del hook de arriba). `6c85f93` y `05d89d9`
logging y comentarios de `ProcessIPCFrame`; `a646d81` renombra en `vftableinfo`.

`8d16704` **"Fix type of fakeAppIdMap"** — `unordered_map<uint32_t, AppId_t>` →
`unordered_map<HSteamPipe, AppId_t>`. Ojo, es **puramente descriptivo**: `HSteamPipe`, `AppId_t`
y `uint32_t` son el mismo tipo (`typedef uint32_t`), y el inicializador incluso decía
`unordered_map<AppId_t, AppId_t>()`. Eran tres nombres para lo mismo; ahora es el correcto.
**Cero cambio de comportamiento** — pero es el tipo de arreglo que evita que el siguiente lector
crea que la clave es un appId cuando es un handle de pipe.

**Neto del 05-08: cero accionables**, dos mejoras gratis que caen de lleno en el flujo de
multijugador de LumaDeck, y una nota de baja prioridad sobre el aviso de FakeAppIds
simultáneos.

#### 7.7.12 Día 2026-08-06 — 9 commits (el asignador de Steam) ★ el día con el hallazgo más relevante del rango

**`a2edc13` — localiza el asignador de Steam, y lo hace de la forma más limpia posible.**
`dlopen("libtier0_s.so")` + `dlsym` de tres símbolos **C exportados**:

```cpp
Plat_Alloc   = reinterpret_cast<Plat_Alloc_t>(dlsym(tier0, "Plat_Alloc"));
Plat_Free    = reinterpret_cast<Plat_Free_t>(dlsym(tier0, "Plat_Free"));
Plat_Realloc = reinterpret_cast<Plat_Realloc_t>(dlsym(tier0, "Plat_Realloc"));
```

**Ni patrones, ni RTTI, ni índices de vtable** — son símbolos exportados con nombre estable,
o sea la resolución más robusta de todo el proyecto. Añade `libtier0_s.so` como tercer módulo
en `la_objopen` y a la precondición de `load()` (ahora espera steamclient + steamui + tier0).
*(Descuido menor: `if (!Plat_Alloc | !Plat_Free | !Plat_Realloc)` usa `|` bitwise donde quería
`||`; funciona porque los operandos no tienen efectos secundarios.)*

**`b937ab2` — "Free netpacket instead of hiding".** El `clearBody()` viejo no liberaba nada,
**mentía sobre el tamaño** para que Steam viera un cuerpo vacío y lo liberase él:

```cpp
//Hide body and call original function so steam uses it's own free
size = body->headerSize + sizeof(CNetPacketBody);
```

Ahora libera de verdad (`Steam::Plat_Free(body)`, y limpia `size`/`body`/`originalBody`) y
**retorna sin llamar al original**. Cambio semántico real: antes el paquete seguía pasando por
el handler de Steam con el cuerpo vacío; ahora se descarta **antes**. Para "ahogar" un mensaje
es más correcto — no llega al parser.

**`0fc9cf9` — la arena estática muere, y con ella el mutex.** El comentario es la mejor parte:

```cpp
//Freeing pData royally fucks up memory, proly a use after free scenario
//So we copy the packet into fresh memory, modify that, etc
```

Antes: apuntaba `packet.body` al `pData` de Steam, mutaba **en sitio**, y reasignaba
`pData = packet.body` con el comentario *"Do not free ourself since Steam does so. We reuse our
CNetPacket buffer"*, apoyándose en la **arena estática de 8 MB** (`g_packetsArray`, §7.7.8).
Ahora: `Plat_Alloc(dataSize)` → `memcpy` → mutar → llamar al trampolín con el buffer nuevo →
`Plat_Free`. Y borra la arena entera **más `g_packetSerializeMutex`**.

Eso último es lo más elegante del día: la arena necesitaba mutex porque dos serializaciones
concurrentes competían por el offset. Con asignación por llamada **no hay estado compartido, así
que no hace falta candado**. Elimina el recurso compartido en vez de protegerlo.

Contraste que vale anotar: **la misma semana, el mismo dev, dos instintos opuestos.** Aquí quita
estado mutable compartido; en la ruta de logros del 04-08 (§7.7.10) **añadió** un mapa
compartido sin sincronizar.

**Menores.** `da69f54` añade `EIPCCmd_ToString`/`EIPCInterface_ToString` (legibilidad de logs).
`38b3af1` mueve el log de `hkTraceIPC` **antes** de la llamada real — si la llamada revienta, la
línea ya está escrita; es una decisión de instrumentación para postmortems. `556d2a6` pule
`CNetPacket`; `dd85a69` sustituye `malloc` por `std::string` en `memhlp`; `e2cfdb2`/`4d4eab8`
formato y comentarios.

##### 7.7.12.a ★ Hallazgo: la ruta de inyección por defecto de lumalinux hace `realloc` de libc sobre memoria de Steam

**Este es el primer [PRESTABLE] técnico real de todo el barrido, y no es hipotético.**

`src/hooks/load_package_hook.cpp::AppendIdsToVec` hace crecer el `AppIdVec` del paquete con
**`std::realloc` de libc**, sobre un puntero que asignó Steam:

```cpp
// Grow if needed (raw libc realloc — CUtlMemory is malloc-backed on Steam
// Linux i386; growth doubles capacity, min first alloc 32).
void* new_mem = std::realloc(vec->m_pMemory, new_alloc * sizeof(uint32_t));
```

**Y es un camino VIVO y por defecto**, no código muerto. Traza comprobada:
`package_zero_finder.cpp:348` → `Hooks::LoadPackage::InjectDepots(pkg, "finder")` →
`AppendIdsToVec` → `std::realloc`. El package-0 finder está **ON por defecto**
(`LUMA_NO_PKG0_FINDER` lo apaga) y es **el único inyector** — o sea el corazón de que un
install funcione. *(El hook `LoadPackage` en sí ya no inyecta: sólo registra el avistamiento. La
ruta viva es la del finder.)*

**El supuesto está escrito y es load-bearing:** *"CUtlMemory is malloc-backed on Steam Linux
i386"*. Y `a2edc13` acaba de demostrar que **Steam asigna a través de `Plat_Alloc`/`Plat_Free`/
`Plat_Realloc` de `libtier0_s.so`**, no de libc directamente. Si esos wrappers reenvían a libc
—lo habitual en el tier0 de Valve en Linux— el supuesto se sostiene y hoy no hay bug. Si algún
día usan un heap propio, un `realloc()` de libc sobre un puntero de `Plat_Alloc` es **UB**, y
el síntoma sería justo el que lumalinux ya conoce: `free(): invalid pointer` / corrupción de
heap. El historial de `main.cpp:5` lo dice sin adornos: *"v0.3: BuildDepotDependency injection
(**allocator issues** — abandoned)"*.

**Atenuantes honestos:** el `realloc` sólo se ejecuta si el vector **necesita crecer**
(`!m_pMemory || m_nAllocationCount < total`); si el `AppIdVec` del paquete 0 ya tiene capacidad,
es escritura en sitio y no se toca el asignador. Y no hay ningún crash reportado atribuido a
esto. **No es un bug conocido: es un supuesto no verificado en la ruta crítica.**

**Lo que SLSsteam nos regala aquí son las dos mitades:**
1. **Cómo verificarlo, barato:** `dlsym("Plat_Realloc")` en `libtier0_s.so` y comparar el
   puntero contra el `realloc` de libc (`dlsym(RTLD_NEXT, "realloc")`). Si coinciden —o si
   `Plat_*` es un trampolín de una instrucción a libc—, el supuesto queda **confirmado con
   evidencia** en lugar de asumido, y se puede anotar en el comentario. Se puede hacer en un
   log de diagnóstico sin cambiar comportamiento.
2. **Cómo arreglarlo si no coinciden:** usar `Plat_Realloc` (y `Plat_Free`) exactamente como él
   — `dlopen`/`dlsym` de símbolos exportados, sin patrones, con fallback a libc si tier0 no
   está. Coste bajo y sin fragilidad nueva.

**Y una incoherencia de documentación detectada de paso:** el historial de versiones de
`main.cpp` (entrada v0.5.6, línea 19) afirma **"In-place append only (no risky manual
realloc)"**. La ruta viva **sí** hace un realloc manual. Es una nota histórica, no una promesa
actual, pero quien lea esa cabecera se llevará la impresión contraria a lo que hace el código.

**Prioridad: media.** No hay síntoma, pero está en la ruta crítica por defecto y el coste de
cerrarlo (un log de diagnóstico primero, `Plat_Realloc` después sólo si hace falta) es bajo.
Es, con diferencia, lo más accionable que ha salido del barrido.

**Neto del 06-08: un [PRESTABLE] de prioridad media con traza completa** (el primero del
rango), más una incoherencia menor de documentación en `main.cpp`.

#### 7.7.13 Día 2026-08-07 — 8 commits (cimientos para inyectar mensajes)

**`4b29945` — ProtoBuf lite → ProtoBuf completo.** El commit que explica el +38k/−22k del día
(regeneración de todos los `.pb.cpp`/`.pb.h`). Su motivo: *"We're gonna need it from now on.
Mostly for dumping messages"*.

La diferencia técnica importa: **protobuf-lite no lleva reflexión ni descriptores**, así que no
existe `DebugString()` ni formato texto — para imprimir un mensaje tienes que escribir el
printer a mano, campo a campo. Con protobuf completo puedes volcar **cualquier** mensaje sin
tocar código. El precio es tamaño de binario y memoria. Cambia peso por capacidad de
introspección, y lo hace justo antes de necesitarla.

**Los cimientos para inyectar mensajes — tres commits que son una sola historia.**

`cc8d42a` **"Fix missing pad in CNetPacket"** — el struct estaba declarado como **`0x14`
(20 bytes)** y en realidad mide **`0x20` (32)**. Añade 12 bytes de padding y corrige el
comentario de tamaño. **Es el prerrequisito de todo lo demás**: `CCMInterface::recvPkt(CNetPacket*)`
recibe un puntero, y si le pasas un `CNetPacket` construido en pila que mide 12 bytes menos de
lo que Steam espera, Steam lee y escribe **más allá del objeto** → corrupción de pila. No se
puede fabricar un paquete para dárselo a Steam hasta tener el tamaño bien.
*(Desliz de nombres: el campo se llama `__pad0x10` pero está en `0x14`.)*

`7366bed` añade la **receta** para usar `recvPkt`, que es oro documental:

```cpp
//Before using make sure to:
//Create header with steamId & realm
//Create body
//Serialize
//Set type with ProtoBuf mask
//Set refs to 1 (not doing this will debugbreak() in a failed assert)
```

`6e8e8de` **"Add ability to send messages & spoof incoming ones"** — versiones hook+tramp para
`CCMInterface` y `CWebSocketConnection`, más `IClientUser::sendMsg`.

**Y aquí está lo interesante: la cadena lleva tres semanas montándose.**

| Fecha | Commit | Pieza |
|---|---|---|
| 07-25 | `4bd33e7` | identifica el padding de `+0xC` como `int32_t refs` (contador de referencias) |
| 08-06 | `b937ab2` | libera el paquete de verdad con `Plat_Free` en vez de mentir sobre el tamaño |
| 08-06 | `0fc9cf9` | asigna con `Plat_Alloc` en vez de la arena estática |
| 08-07 | `cc8d42a` | corrige el tamaño real del struct (0x14 → 0x20) |
| 08-07 | `7366bed` | documenta que hay que poner `refs = 1` o salta un assert |
| 08-07 | `6e8e8de` | y ya se pueden enviar/falsificar mensajes |

Ese `refs = 1` del último paso **es** el campo que identificó el 25-07. Tres semanas de
cimientos para una capacidad que todavía no es una feature — no hay ninguna opción de config que
la use. Es infraestructura por adelantado.

**`3d7d3c6` — `has_target_job_name()` antes de usarlo**, con su motivo: *"Do not modify header by
blindly requesting the target_job_name"*. Nota honesta: en proto2 un getter **const** de un campo
opcional no debería mutar el mensaje (devuelve el valor o el default), así que esto se lee más
como código defensivo que como un bug arreglado — y antes ya era seguro porque un campo sin
poner devuelve `""`, que nunca iguala al nombre buscado. Lo que sí gana es **intención
explícita**: dice "sólo si el campo existe" en lugar de apoyarse en que comparar contra vacío
falle.

**`a62efeb` — simplifica `isValid()`**, y lo comprobé porque parecía una regresión:

```cpp
-return body && size > sizeof(CNetPacketBody) && body->type != INVALID_NETPACKET_TYPE;
+return getType() != INVALID_NETPACKET_TYPE && size > sizeof(CNetPacketBody);
```

Desaparece el `body &&`. **No es regresión:** `getType()` tiene su propia guarda
(`if (!body) return INVALID_NETPACKET_TYPE;`), así que el nullcheck se movió, no se perdió.
Correcto.

**Pero al lado hay un bug de verdad, de los suyos** (no accionable por nosotros, se anota como
observación del barrido):

```cpp
constexpr bool isProtoBuf() const {
    if (getType() == INVALID_NETPACKET_TYPE) {
        return INVALID_NETPACKET_TYPE;      // ← ENetPacket(-1) desde una función bool
    }
    return getType() & PROTOBUF_TYPE_MASK;
}
```

`INVALID_NETPACKET_TYPE` es `-1`, y `-1` convertido a `bool` es **`true`**. O sea:
`isProtoBuf()` devuelve **"sí, es protobuf"** justo cuando quiere decir "no lo es". La intención
está invertida.

Rastreados los tres llamantes: dos (`hooks.cpp:275` y `:481`) van precedidos de `isValid() &&`,
que ya filtra el caso, así que están tapados. El tercero **no**: `CNetPacket.cpp:8`, dentro de
`getProtoBufTypeName()`, que a su vez se llama en `hooks.cpp:271` — **una línea antes** del
`isValid()` de la 275. Efecto real: para un paquete con `body == nullptr`, la línea de log
imprime un nombre de mensaje inventado (`-1 & ~0x80000000` = `0x7FFFFFFF`) en lugar de
`"Unknown"`. **Cosmético, sólo afecta al log.** Ni crash ni cambio de comportamiento.

**Menores.** `c51c4fa` limpia `la_objopen` (`const std::string name = map->l_name` en vez de
construir el string tres veces) y deja ver que ya trackea `libtier0_s.so` del día anterior;
`b571526` arregla el logging dentro de `assembleCodeAt`.

##### 7.7.13.a Frontera (§5): SLSsteam crece hacia la capa de mensajes, nosotros no estamos ahí

Vale la pena anotarlo en términos del mapa de coexistencia, porque este día lo hace nítido.

**SLSsteam** lleva un mes profundizando en la **capa de red/mensajes**: `CNetPacket`,
`CCMInterface::recvPkt`, `CWebSocketConnection`, protobuf completo, y ahora capacidad de
**enviar y falsificar** mensajes.

**lumalinux no tiene nada de eso.** Comprobado: **cero** `#include` de protobuf en todo `src/`,
cero referencias a `CNetPacket`, `EMsg` o `CMsgClient*` (la única coincidencia del grep es un
comentario en `sls_achievement_unblock.cpp` que menciona `EMsg` al explicar el cambio de
mangling). lumalinux opera en la **capa de llamada a función**: hookea `LoadDepotDecryptionKey`,
`GetManifestRequestCode`, `GetShaderCacheDepot`, `BuildDepotDependency` — argumentos y valores
de retorno, no paquetes.

**Consecuencia para §5:** los conjuntos no sólo son disjuntos hoy, sino que **están creciendo en
direcciones que no colisionan**. SLSsteam se hunde hacia la red; lumalinux se queda en la
frontera del install-path. No hay ninguna señal de que vaya a haber solape, y eso es una buena
noticia estructural para la estrategia de coexistencia.

**Neto del 07-08: cero accionables.** Un bug cosmético de SLSsteam anotado, una confirmación de
que `a62efeb` no rompe nada, y una observación de frontera que refuerza §5.

#### 7.7.14 Día 2026-08-08 — 10 commits (el AppId real sale de `/proc`)

**`9ce2650` — "Read real AppId from process environment".** Cambia la forma de saber **qué juego
hay al otro lado de un pipe**, que es el dato del que depende todo FakeAppIds.

Antes: un `AppId_t lastAppLaunched` global que se actualizaba en `launchApp`, y
`setAppIdForCurrentPipe` asociaba ese "último lanzado" al pipe actual. Frágil por construcción —
si dos juegos arrancan cerca, o si el orden no es el esperado, la asociación se cruza.

Ahora, `getRealAppIdFromEnv(HSteamPipe)`:
1. saca el `pid` del `CServerPipe` correspondiente al pipe,
2. abre **`/proc/<pid>/environ`**,
3. extrae `SteamAppId=N` por regex,
4. y **cachea el resultado por pipe** (`fakeAppIdMap[pipe] = appId`).

Es una fuente de verdad mucho mejor: el `SteamAppId` del entorno lo pone Steam al lanzar el
proceso, así que es el appId real por definición, sin heurística de orden.

Detalle de diseño acertado: **cachea también el 0**. El pipe del propio cliente de Steam no
tiene `SteamAppId` en su entorno, así que se resuelve a 0 una vez y **no se vuelve a leer
`/proc`** en cada llamada. La caché sirve tanto para acertar como para no reintentar.

`c2a3a2f` documenta la consecuencia en la config:

```
#Requires access to /proc to read processes' real AppId from their environment
#(most distros allow this by default)
```

##### 7.7.14.a Requisito nuevo de runtime: acceso a `/proc` — qué implica para LumaDeck

**LumaDeck conduce FakeAppIds** (§7.7.4, §7.7.11), así que este requisito es nuestro también.
Desglosado:

- **`/proc/<pid>/environ` es modo `0400`, propiedad del UID del proceso.** Leerlo exige **mismo
  UID** o root. SLSsteam corre dentro del cliente de Steam (uid `deck`) y los juegos también
  corren como `deck` → **legible**. En SteamOS funciona.
- **Degrada, no rompe.** Comprobada la ruta: si no se puede abrir el fichero o no hay
  `SteamAppId`, `getRealAppIdFromEnv` devuelve 0, y `getRealAppIdForCurrentPipe` cae a
  `utils->getAppId()` — que es el fallback que **ya existía antes** de este commit. O sea, un
  `/proc` inaccesible deja FakeAppIds funcionando *mal* (devolvería el appId **falso** donde
  debería devolver el real, así que la traducción falso↔real se confunde), pero **sin crash**.
- **Riesgos teóricos, no verificados on-device:**
  - `hidepid=1/2` montado en `/proc` ocultaría los procesos ajenos. **No es el default en
    SteamOS.**
  - **Namespaces de PID**: los juegos bajo Proton corren dentro del contenedor del Steam Linux
    Runtime (pressure-vessel / bubblewrap). Si ese contenedor no comparte el namespace de PIDs,
    el `pid` que SLSsteam lee del `CServerPipe` podría no resolverse en su vista de `/proc`.
    **No lo he verificado** — el pid que registra Steam debería ser el visible desde el host,
    porque la conexión IPC la acepta el Steam del host, pero no lo doy por seguro.

**Valor práctico para nosotros: diagnóstico.** No hay nada que cambiar. Pero si algún día un
usuario reporta *"el online fix / los lobbies dejaron de funcionar después de actualizar
SLSsteam"*, **`/proc` es una causa candidata que no existía antes del 08-08**. El síntoma sería
FakeAppIds resolviendo mal sin ningún error visible, y el log de SLSsteam lo delata:
`"No SteamAppId in /proc/<pid>/environ! Using 0"` o `"Failed to open … to get %p's appId!"`.
Merece la pena tenerlo en la lista mental de sospechosos de `troubleshooting.md`.

> **Mejora del 12-08 (`d680874`) — el log ya dice QUÉ proceso falló.** Añade una lectura de
> `/proc/<pid>/comm` para sacar el nombre del ejecutable y lo mete en las líneas de error:
> `"Failed to open /proc/1234/environ for MyGame.exe to get 0x3's appId!"`. Sube bastante el valor
> diagnóstico de esta ruta: con el nombre del proceso se distingue de un tirón si el fallo es en
> un juego concreto, en el propio cliente de Steam, o en un proceso auxiliar. Si el `comm` tampoco
> se puede leer, avisa (`"ExeName will be unknown in logs"`) y sigue con `"Unknown"` — degrada en
> vez de callarse.

**`14d9e32` — el comentario más valioso del día**, y hay que leerlo junto al 07-08:

```cpp
//Replacing this call with an injected GetAppOwnershipTicketResponse is possible, but breaks
//in offline mode so we don't do that
```

**El día después** de construir la capacidad de inyectar mensajes (§7.7.13), documenta un sitio
donde **decidió no usarla**, y por qué. Es una alternativa descartada dejada por escrito — el
tipo de nota que evita que el siguiente lector (o él mismo en tres meses) reintente un camino
muerto. Nótese además que el motivo es **modo offline**, o sea la clase de caso que sólo se
descubre probando.

**`23f3dd7` — retira su propia abstracción, dos días después de crearla.** Los wrappers
`Steam::alloc<T>()` / `Steam::free()` / `Steam::realloc<T>()` que añadió el 06-08 (§7.7.12)
desaparecen; se llama a `Plat_Alloc`/`Plat_Free`/`Plat_Realloc` directamente. No aportaban nada
sobre los punteros de función. Buena señal: **borra la indirección cuando no se gana el sitio**,
en vez de dejarla porque ya está escrita.

**`eb4cfac`** sustituye el literal `if (type == 2)` por `k_EWebSocketConnectionSendRaw` — el
mismo movimiento de "el nombre pasa de etiqueta a información" que ya se vio con
`getCurrentSteamPipe` (§7.7.2).

**`e3ed672` — toggle `DEBUG` en el Makefile.** Sin `-D DEBUG`, las llamadas inline a
`CLog::once` y `CLog::debug` **se optimizan fuera por completo** — y lo verificó:
*"I checked using radare, without DEBUG being defined the inlined calls of CLog::once &
CLog::debug get fully optimized out"*. Comprobar el binario en vez de asumir que el compilador
hizo lo esperado es el detalle que separa una suposición de un hecho.

**Menores.** `6786383` versiones hook+tramp para `CMCInterface` y `CWebSocketConnection`
(continúa los cimientos del 07-08); `b0781a0` limpieza de headers; `a05f9ff` targets `.PHONY`
que faltaban; `8375b8e` un newline.

**Neto del 08-08: cero accionables**, un requisito de runtime nuevo (`/proc`) que en SteamOS se
cumple y que degrada sin romper, anotado como **candidato de diagnóstico** para
`troubleshooting.md` si alguna vez fallan los lobbies tras una actualización de SLSsteam.

#### 7.7.15 Día 2026-08-09 — 14 commits (reescritura del logging)

Doce de los catorce commits son el sistema de logs. Parece autocomplaciente para un mod, pero
tiene una justificación que él escribe: *"tracing will come in very handy when hunting down
crashes"* (`e578e5a`). Viene de un mes arreglando cuelgues y corrupciones de heap, y está
construyendo el instrumental para el siguiente.

**El arco del día, en orden:**

`b556408` amplía los niveles y mete `__FILE__`/`__FUNCTION__`/`__LINE__` en cada macro `LOG_*`.
`652a3b6` los aplica por todo el proyecto. `ebf28b4` convierte los niveles en **flags bitwise**
combinables (`k_ELogLevelX = 1 << n`), de modo que se puede pedir "sólo errores y notificaciones
largas" en vez de un umbral lineal.

`9a54629` **readmite** los wrappers de `__log` que había quitado, con el razonamiento a la vista:
*"otherwise notifications will still print logging info to the log. Of course we could just not
print them but **optimizing them out fully just feels better**"*. O sea, elige la variante que el
compilador puede eliminar entera antes que la que simplemente no imprime.

`0f73549` **arregla un off-by-one en la propia definición de los flags**: empezaban en `1 << 1`,
desperdiciando el bit 0 y desplazando toda la numeración documentada. Ojo al detalle: en este
punto el valor del config es **el índice del bit**, no la máscara — por eso `LogLevel: 4` pasa a
`LogLevel: 3`.

`dfed8e2` itera los niveles al revés en `ELogLevel_ToString` *"Makes the higher levels show up
first. Makes more sense because they are more important"*. `e578e5a` añade `traceOnce`.

`9aad713` **quita** `NotifyWarn` y `NotifyError` porque son *"easily replicated by mixing flags"*
(p.ej. `NotifyShort | Warn`) y añade `ELogLevelCount = 8`. Es un cambio visible en el config:
quien tuviera `LogLevel: 8` o `9` apunta ahora a otra cosa. Transitorio, porque el 11-08 cambia
todo otra vez.

**El par que más me gusta del día, y son 21 minutos:** `9e51989` convierte las plantillas de
logging en funciones print de verdad, *"It's kind of a mess, but this way **we get actual format
checks**"*. Y el commit siguiente, `3b1be49`, **arregla las llamadas malformadas a `printf` que
esos checks acaban de encontrar** (*"Using pedantic settings we had to explicitly convert some
stuff"*). Habilitar una comprobación del compilador y que se pague a sí misma en el mismo rato.

##### 7.7.15.a `4afaede` — strict aliasing, y qué pasa con lumalinux

El único commit del día que no es logging y tiene fondo real:

```cpp
-fakeAppIdMapPings[*reinterpret_cast<uint64_t*>(&details.address)] = realAppId;
+fakeAppIdMapPings[details.ip64] = realAppId;
```

Estaba leyendo un miembro **declarado** como `servernetadr_t` (dos `uint16_t` + un `uint32_t`)
a través de un `uint64_t*`. Eso viola **strict aliasing**: el compilador tiene derecho a asumir
que dos punteros de tipos incompatibles no apuntan al mismo sitio, y con eso reordena o cachea
lecturas. Es la clase de bug que **funciona a `-O0` y revienta a `-O2/-O3`**. Lo arregla con la
herramienta correcta, una `union { servernetadr_t address; uint64_t ip64; }`.

Y le importaba especialmente porque su `Makefile` compila con
`-O3 -flto=auto -floop-block -fgraphite-identity -floop-parallelize-all -fomit-frame-pointer`:
con LTO y ese nivel de agresividad, el compilador explota los supuestos de aliasing mucho más.

**¿Nos aplica?** Fui a comprobarlo en serio, y la respuesta es **no, y conviene entender por
qué** (para no añadirlo a la lista de préstamos falsos):

- **Ninguno de los dos proyectos usa `-fno-strict-aliasing`.** lumalinux compila con
  `-m32 -D_GLIBCXX_USE_CXX11_ABI=0`, `-Wall -Wextra -Wpedantic`, y —dato interesante—
  **`-fno-reorder-blocks-and-partition`**, una mitigación de optimización muy específica que ya
  encontraron necesaria y documentaron. O sea que ya piensan en estos términos.
- **Pero el patrón que le explotó a él no aparece en lumalinux.** Su bug era leer un **miembro
  con tipo declarado** a través de otro tipo. Los `reinterpret_cast` de lumalinux van de
  **memoria cruda** (un `void*`/`uintptr_t` sacado de un puntero de Steam) a **un único tipo** por
  acceso: `CUtlVector<uint32_t>*` en el LoadPackage, `void**` para las vtables en `rtti.cpp`,
  `lm_byte_t*` en el escaneo de patrones — y los tipos `char`/`unsigned char` están **exentos**
  de strict aliasing por norma. No hay ningún sitio donde se lean los mismos bytes como dos tipos
  distintos.
- Y donde se leen offsets distintos del mismo objeto (`PkgId(pInfo)` como `uint32_t*` y
  `AppIdVec(pInfo)` como `CUtlVector*`), **no solapan**, así que no hay conflicto.

**Conclusión: nada que hacer.** `-fno-strict-aliasing` sería seguro-a-cambio-de-nada como
cinturón extra, pero **no lo respalda ninguna violación encontrada**, y lo apunto explícitamente
como tal para que no se lea como un hallazgo. Lo que sí queda es la observación: el patrón
peligroso es *punear un miembro tipado*, y eso lumalinux no lo hace.

##### 7.7.15.b Corrección a mi propio commit en LumaDeck: la fecha de `LogLevels`

Rastreado con `git log -S"LogLevels:" -- res/config.yaml`: la clave de config **`LogLevels`**
(plural) aparece en **`b5b1315`, el 2026-08-11** (*"refactor(log): Make logging smarter"*). Lo del
09-08 son los **flags internos**, mientras el config seguía siendo `LogLevel` con un **índice de
bit**.

*(Precisado en §7.7.16: el cambio va en tres pasos, no dos. **09-08** define los flags como
`1 << n` pero `__log` sigue comparando `flags < getMinLevel()`, o sea los usa como **magnitud**;
**10-08** (`82afc5d`) cambia esa comparación a un **test de máscara**, y ahí es donde el valor del
config pasa de índice a máscara; **11-08** (`b5b1315`) renombra la clave a plural para que el
nombre cuadre con la semántica.)*

En el mensaje del commit `a061b00` de LumaDeck escribí que faltaba *"`LogLevels`
(b556408/ebf28b4, 2026-08-09)"*. **La atribución es imprecisa**: esos dos commits introdujeron
los flags, no la clave de config. La fecha correcta es **2026-08-11 (`b5b1315`)**.

**No afecta al arreglo**: el `_BUNDLED_YAML` se refrescó copiando el `config_default.hpp` de
upstream y se verificó **byte-idéntico** al que sirve la URL, así que la clave y su default
(`LogLevels: 0xff`) son los correctos. Sólo estaba mal la fecha citada en la prosa del mensaje.
No reescribo el commit ya publicado por un dato de atribución; queda corregido aquí, que es el
registro vivo.

**Menores.** `8f450e8` ajusta el comentario de `LogLevel`; `cfc3bfb` añade un script que genera
las versiones Debug y Release (automatiza los paquetes "Any" y "Release"). `3882c46` quita el
argumento `appId` **sin usar** de `Apps::buildDepotDependency` — es su helper interno, **no** la
firma del hook `CUserAppManager::BuildDepotDependency`, así que **no hay impacto en la nota de
coexistencia de BuildDep** de `main.cpp`. De paso revela algo: su handler de BuildDep no
filtraba por app, opera sobre los vectores de depots enteros.

**Neto del 09-08: cero accionables**, una comprobación de aliasing que sale limpia, y una
corrección de fecha en mi propio commit de LumaDeck.

#### 7.7.16 Días 2026-08-10 y 2026-08-11 — 8 commits (el logging se asienta, y `Notifications` desaparece de verdad)

**10-08 (2 commits).**

`82afc5d` **"Add ability to turn off individual loglevels"** — es el cambio semántico que faltaba
para que los flags del 09-08 sirvieran de algo:

```cpp
-if (flags < getMinLevel())              // umbral: "sólo lo que pese más que X"
+if (!(g_config.logLevel.get() & flags)) // máscara: "sólo los bits que yo encienda"
```

**Aquí es donde el valor del config deja de ser un índice y pasa a ser una máscara.** Hasta este
commit los niveles estaban *definidos* como `1 << n` pero se *usaban* como magnitud, que es por
lo que el config listaba índices (§7.7.15). Ahora se pueden apagar niveles **individuales**, no
sólo cortar por debajo de un umbral. Y de paso desaparece `getMinLevel()`, el *"dirty workaround
for not being able to access g_config from __log"*.

`8097864` **"Set LogLevel count to actual num"** — `ELogLevelCount` 8 → 9 y el bucle de
`ELogLevel_ToString` de `i = ELogLevelCount` a `i = ELogLevelCount - 1`. Ojo: los dos cambios se
compensan, así que **no hay cambio de comportamiento** — el bucle ya recorría los 9 bits
correctos. Lo que arregla es el **significado de la constante**: pasa de valer "índice del bit más
alto" a valer "número de niveles", que es lo que su nombre dice.

**11-08 (6 commits).**

`d5a17c7` **"Properly remove Notifications"** — y "properly" es la palabra clave, porque **no
sólo borra la opción de config: borra el interruptor**:

```cpp
-if (shouldNotify() && notification.size() > 0)
+if (notification.size() > 0)
```

Fuera la variable `notifications`, fuera su línea de log, y fuera `CLog::shouldNotify()`.

**Consecuencia visible para el usuario: `Notifications: no` ya no silencia nada.** La forma de
callar los toasts pasa a ser **apagar los bits de notificación en `LogLevels`**
(`k_ELogLevelNotifyShort` = `0x40`, `k_ELogLevelNotifyLong` = `0x80`; el default `0xff` los lleva
encendidos). Lo confirma el `2b4b411` del 12-08, que añade `k_ELogLevelInfo` a `notify`/`notifyLong`
*"So it still gets logged when users turn of notifications"* — o sea, apagas el toast pero el
mensaje sigue cayendo al fichero.

**Qué implica para LumaDeck** (comprobado): LumaDeck **no** escribe `Notifications` en ningún
sitio, así que no hay nada que arreglar por nuestra parte. Dos notas:
1. **Un usuario que lo hubiera puesto a mano** para silenciar los toasts de SLSsteam **volverá a
   verlos** tras actualizar. Migración: quitar `0x40|0x80` de `LogLevels` (p.ej. `0x3F`).
2. **El refresco del `_BUNDLED_YAML` que hicimos (`a061b00`) cuadra con esto.** Quitar
   `Notifications` del referente es correcto porque upstream ya la ignora; y como el completado es
   **append-only**, quien ya tenga la clave en su config la conserva como texto inerte (SLSsteam
   ignora en silencio las claves que no espera, §7.7.4). Consistente en los dos sentidos.

`b5b1315` **"Make logging smarter"** — renombra la clave `LogLevel` → **`LogLevels`** (plural),
para que el nombre cuadre con la semántica de máscara que llegó el día anterior. El valor ya era
`0xff`.

`49cc2dc` **"Use `Info | Once` for config logging"**, con el motivo escrito: *"Prevents the
logfile bloating to hell and back with big configs"*. Cambia los `LOG_INFO` del volcado de config
—cada setting, cada entrada de `IdleStatus`, `DlcData`, `DenuvoGames`, y los tres templates
`getSetting`/`getList`/`getMap`— por `LOG_CUSTOM(Info | Once, …)`. El flag `Once` dedupe por
mensaje formateado contra un `msgHist` que vive todo el proceso.

**Y esto nos beneficia más que a un usuario vanilla**, por la misma razón que los stutters de
§7.7.10: **LumaDeck provoca recargas de config a propósito.** Cada `_commit_config()` hace el poke
para que SLSsteam hot-reloadee (§4.1), y cada recarga **re-volcaba el config entero al log** —
todos los settings más una línea por cada entrada de las listas. Un usuario con 40 juegos añadidos
y unas cuantas operaciones en la sesión acumulaba miles de líneas repetidas. Con `Once`, se
registran una vez y punto. **Quinta razón acumulada** para recomendar SLSsteam ≥ `20260815201341`
(tras §4.1, §7.2, §7.7.10 y §7.7.11).

`37a29f7` **carga `LogLevels` primero**, *"Otherwise on first load settings won't get logged"* —
coherente con la máscara del 10-08: si el `LOG_CUSTOM` de cada setting comprueba una máscara que
todavía no se ha leído, no se imprime nada. Orden de inicialización, no lógica.

`cd43f3b` pasa `DlcData` a usar `getMap` en lugar de parseo a mano — de rebote hereda el manejo de
errores por clave (§7.7.4) y el flag `silent`. `7059664` espacios en `if`/`for`.

**Neto de los dos días: cero accionables**, un cambio de comportamiento visible (`Notifications`
deja de funcionar; migración documentada), una confirmación de que el refresco del schema en
LumaDeck cuadra con upstream en los dos sentidos, y una mejora de tamaño de log que nos toca
especialmente.

#### 7.7.17 Día 2026-08-12 — 12 commits (`CDKeys`, y un experimento olvidado)

**`79905b0` — la opción `CDKeys`.** Ya está desglosada en la caja de corrección de **§7.2**
(genera una clave determinista `srand(accountId + appId)` en formato `XXXX-XXXX-XXXX-XXXX`, o usa
la del config, vía un hook nuevo sobre `IClientUser::GetLegacyCDKey`). No lo repito. Dos cosas que
añadir desde la perspectiva del día:

- **El flag `silent` de `getSetting`/`getList`/`getMap` nace aquí**, y nace *para esto*: no volcar
  las CD keys al log (sólo `"Added CDKey for %u"`). Es el mismo flag que resultó relevante en
  §7.7.4 al comprobar que las claves ausentes **sí** cuentan para el toast de errores — porque
  `silent` suprime el log del **valor**, no la llamada a `setError`.
- Y su motivo escrito enlaza todo el rango: *"Previously SLSsteam just disabled CD keys because it
  was the better choice to make seeing how **we didn't have a decompiler** and IClientUser's VFT
  tended to change a lot"*. El trabajo de julio (§7.7.1–7.7.5) es literalmente lo que hizo viable
  esta feature de agosto.

##### 7.7.17.a `3d7b17f` — un experimento olvidado en la ruta del live refresh (que nunca llegó a release)

Merece detalle porque toca **exactamente** el mecanismo que analicé en §7.7.7, y porque el
resultado de comprobarlo es tranquilizador.

`CUser::postCallback` estaba redirigido a través de otra función:

```cpp
-Hooks::CUser_PostCallbackToAppId.tramp.fn(this, 0, static_cast<unsigned int>(type), pCallback, callbackSize);
+const static auto fn = reinterpret_cast<void(*)(void*, ECallbackType, void*, uint32_t, uint32_t)>(Patterns::CUser::PostCallback.address);
+fn(this, type, pCallback, callbackSize, 0);
```

O sea: en vez de llamar a `CUser::PostCallback`, llamaba a **`CUser::PostCallbackToAppId` con
`appId = 0`**. Son dos funciones distintas de Steam. El título lo llama *"forgotten test"* — un
experimento que se quedó puesto por olvido, no una decisión.

**Por qué importa el sitio:** `CUser::postCallback` tiene **exactamente dos llamantes**,
comprobado, y los dos son el callback del live refresh:

```cpp
// feats/apps.cpp:271 y :279, dentro de Apps::postAppLicensesChanged
user->postCallback(ECallbackType::AppLicensesChanged_t, &cb, sizeof(cb));
```

Así que durante ese tiempo **el `AppLicensesChanged_t` de SLSsteam se emitía por una función que
no era la prevista**. No puedo determinar la diferencia de comportamiento entre
`PostCallback(type, cb, size, 0)` y `PostCallbackToAppId(0, type, cb, size)` sin desensamblar
Steam, así que no afirmo que estuviera roto — sólo que no era lo que él quería.

**Y lo importante: no afectó a ningún usuario.** El experimento entró el **05-08**
(`50c439a`, cuando añadió el hook de `PostCallbackToAppId`) y se retiró el **12-08**. Comprobado el
historial de `res/version.txt`: entre `20260728212859` (28-jul) y `20260815201341` (15-ago) **no
hay ninguna release**. La ventana entera cae **entre releases**, o sea vivió sólo en `dev`/`main`
y **nunca se publicó**. Cero impacto para usuarios de LumaDeck o de nadie.

*(Nota de detalle: llamaba a `.tramp.fn`, o sea la función original, **no** su propio hook — así
que el reencaminamiento real↔falso de `hkUser_PostCallbackToAppId` no entraba en juego. Y aunque
hubiera entrado: comprobado que `getFakeAppId(0)` devolvería el comodín `0:` si el usuario lo
tuviera puesto, pero **LumaDeck nunca escribe ese comodín** — `slssteam_ops.py:126` escribe
`"  {appid}: {fake_id}"`, siempre con el appid real como clave.)*

**`d680874` — el log del `/proc` ya dice qué proceso falló.** Lee `/proc/<pid>/comm` para sacar el
nombre del ejecutable y lo mete en los mensajes de error. **Sube el valor diagnóstico de la ruta
que anoté en §7.7.14** — allí queda actualizado.

**`2b4b411` — la contrapartida de haber quitado `Notifications`.** Añade `k_ELogLevelInfo` a
`notify`/`notifyLong` *"So it still gets logged when users turn of notifications"*. Cierra el
cambio del 11-08 (§7.7.16): ahora apagar los bits de notificación en `LogLevels` silencia el toast
**pero el mensaje sigue cayendo al fichero de log**. Sin esto, apagar los toasts te dejaba también
sin registro, que es justo lo que no quieres cuando algo va mal.

**Tipado y pulido (el resto del día).** `f0dab0e` sustituye `void*` por tipos de clase reales en las
firmas de los hooks; `df64d1d` pule el SDK y añade un `sdk.hpp` agregador; `ce8ce33` amplía
`CServerPipe` (de donde sale el `pid` del §7.7.14); `9d88160` borra definiciones de clase obsoletas
en `feats/`; `dbb3a09` quita un `static` redundante sobre un `constexpr`; `4d99883` usa
`ELogLevel_ToString` para loguear el `LogLevels`; `25e1a2f` evita que el `clean-tools` del Makefile
falle si un subjob devuelve non-zero.

**`ffc66d6`** cambia el `uint32_t type` de `BBuildAndAsyncSendFrame` por
`EWebSocketConnectionSendType` en la firma del hook, el typedef y los wrappers. El enum ya era
`: uint32_t`, así que **cero cambio de ABI ni de comportamiento** — es la misma disciplina de
"el tipo debe decir lo que el valor significa" de §7.7.2 y §7.7.11.

**Neto del 12-08: cero accionables.** La feature del día (`CDKeys`) ya estaba capturada en §7.2; el
experimento olvidado nunca salió en release; y el resto es tipado, pulido y una mejora de
diagnóstico que refuerza la nota de `/proc`.

#### 7.7.18 Días 2026-08-13 y 2026-08-14 — 13 commits (la API local crece y se documenta)

**13-08 (2 commits).** `0227a09` quita un include innecesario en `apps.cpp`. `1bdf1a6` es de
**DeveloperMikey**, un contribuidor externo, arreglando el build de nix — segundo aporte de fuera
en el rango (tras los `updates.yaml` de Deadboy666 el 28-07).

**14-08 — la API local pasa de dos comandos a seis, y se documenta.**

`aebac96` refactoriza los comandos de instalación: `Utils::isNumber` + `strtoul` por un
`Utils::tryConvertToNumber` tipado, y `InstallCommand_t` → `InstallOp_t`/`LibraryOp_t`. Luego
`b7407e4`, `89c70c8` y `1dc0fdd` añaden **`setcompat`**, **`getcompat`**, **`dumpcompat`** y
**`dumplibraries`**, todos vía `IClientCompat` (nuevo `src/sdk/IClientCompat.hpp`). `75ba3eb`
crea **`API.md`**, la primera documentación de usuario del canal:

```
dumplibraries : Lists all available libraries' label, their path & their index
install|appId|libraryIndex : Installs appId into library at libraryIndex
uninstall|appId : Uninstalls appId
dumpcompat|appId : Lists all available compatibility tools for appId
getcompat|appId : Lists the current compatibility tool for appId
setcompat|appId[|tool-name] : Set's or clears the compatibility tool for appId
```

`4eecaa4` añade un nivel de log dedicado **`k_ELogLevelAPI`**, para poder aislar la salida de la
API del resto del log — necesario si un consumidor externo va a parsearla.

Y `6e8951e` + `4135d51` + `aa35638` + `ac6830b` añaden **`CUtlString`, `CUtlMap` y `CUtlRBTree`**
al SDK, con `CUtlBuffer` extendido. No es casualidad: `dumpcompat` y `dumplibraries` tienen que
**recorrer contenedores internos de Steam** para enumerar herramientas y bibliotecas. El 15-08
(`9be780a`) los completa parseando el `CUtlRBTree` como árbol binario de verdad, verificado contra
`m_MapAppOwnershipTickets` y `m_mapPackages`.

`06603a0` quita una terminación nula innecesaria en la cdkey (*"std::string is already null
terminated since c++11"*); `9c350c1` mueve un comentario.

##### 7.7.18.a ★ Hallazgo: `set_compat_tool_for_app` viola su propia precondición

Perseguir `setcompat` llevó a algo en LumaDeck. **Segundo [PRESTABLE] técnico del barrido**, y
esta vez el arreglo ya está en casa.

`backend/steam_utils.py:440` escribe la entrada `CompatToolMapping` **directamente en
`localconfig.vdf`**, iterando los `userdata/<id>/config/`. Y su propia docstring declara la
precondición:

> *"**Safe to call while Steam is not running**; Steam reloads the file on start."*

**Pero el llamante la viola.** `downloads.py:1403` la invoca dentro de `_download_zip_for_app`,
para forzar Proton cuando el juego no trajo depot de Linux:

```python
if not _get_download_state(appid).get("hasLinuxDepot", False):
    from steam_utils import set_compat_tool_for_app
    if set_compat_tool_for_app(appid):
        logger.info(f"LumaDeck: Forced proton_experimental for {appid} (Windows-only depot)")
```

Y eso corre **con Steam vivo por definición** — es un plugin de Decky, vive dentro de Steam.

**El modo de fallo:** Steam mantiene `localconfig.vdf` **cacheado en memoria** y lo reserializa al
salir. Una edición hecha por fuera mientras Steam corre no está en esa copia, así que **al cerrar
Steam la sobreescribe**. El síntoma sería un juego Windows-only instalado por LumaDeck que se
queda **sin compat tool** y no arranca.

**Lo que NO he verificado, y es importante:** no he comprobado on-device que la pérdida ocurra.
Si ocurriera siempre, *todos* los juegos Windows-only instalados por LumaDeck fallarían al
lanzarse, que es un bug demasiado ruidoso para pasar inadvertido — así que o se está perdiendo y
los usuarios ponen Proton a mano por costumbre, o Steam se comporta de forma distinta a lo que
asumo. **La violación de precondición es real y verificable en el código; el fallo resultante es
inferencia.**

**Y el arreglo idiomático ya lo tenéis, en el fichero de al lado.** `GameDetail.tsx:188-196` ya
hace mutaciones **en vivo** contra Steam desde el frontend, con guarda defensiva:

```ts
const sc: any = (window as any).SteamClient;
if (sc?.Apps?.SetAppLaunchOptions) {
    sc.Apps.SetAppLaunchOptions(appid, r.launchOptions || "");
}
```

El compat tool tiene la misma forma: `SteamClient.Apps.SpecifyCompatTool(appid, tool)`. Va contra
el Steam en ejecución, así que **su estado en memoria se actualiza y él mismo persiste** — el
conflicto de caché desaparece. *(No doy por seguro el nombre exacto del método; hay que
confirmarlo en runtime. Y ojo: con el patrón `sc?.Apps?.X` un nombre equivocado **no falla, no
hace nada**, así que conviene loguear el caso `else`.)*

**La vía de SLSsteam (`setcompat`) también resolvería el problema, pero es peor para nosotros:**
- Exige `API: yes`, y upstream **envía `API: no`** en su `res/config.yaml` (comprobado, línea 110)
  — curioso: el default **en código** es `true` (`getSetting<bool>(node, "API", true)`), pero el
  fichero que se escribe dice `no`, así que un config nuevo la lleva apagada. LumaDeck tendría que
  encenderla, y ya tiene la maquinaria (`ensure_slssteam_flags` + los editores por líneas).
- Abre un canal de comandos en `/tmp/SLSsteam.API` que **cualquier proceso del mismo usuario**
  puede escribir, y por el que pasan `install`/`uninstall`/`setcompat`. En un Deck monousuario es
  una preocupación modesta, pero es ensanchar superficie por algo que `SteamClient` ya da gratis.

> 🔻 **REBAJADO a BAJO / cosmético — 2026-08-17, tras objeción del dev.** El razonamiento de
> arriba omitía lo decisivo: **en un Steam Deck, "Steam Play para todos los demás títulos" viene
> ACTIVADO por defecto**, así que un juego sólo-Windows **ya se lanza con Proton sin ninguna
> entrada en `CompatToolMapping`** — Steam usa el default global. `CompatToolMapping` sirve para
> **anular** ese default por juego (fijar una versión concreta), no para habilitar Proton.
>
> Consecuencias, y todas rebajan el hallazgo:
> 1. **La premisa del comentario de `downloads.py:1399-1401` es falsa**: *"Steam wouldn't otherwise
>    launch a Windows binary on the Deck without explicit compat tool"*. Sí lo lanzaría.
> 2. Por tanto `set_compat_tool_for_app` es **redundante** en la configuración normal de un Deck,
>    y su probable inefectividad es **invisible porque no hacía falta** — no porque los usuarios lo
>    corrijan a mano, como escribí. Es la explicación limpia de por qué el dev lleva semanas
>    jugando juegos Windows sin incidencias.
> 3. **Antigüedad**: `set_compat_tool_for_app` **y** su llamada entraron en el **mismo** commit,
>    `cda38b7` (**2026-07-29**). Tres semanas, sin síntoma — coherente con que sea redundante.
>
> **Lo que queda como accionable, todo de prioridad baja:** corregir la premisa del comentario,
> quitar la frase engañosa de la docstring, y decidir si el pin se elimina (no aporta) o se mueve a
> `SteamClient.Apps` para que funcione de verdad en el único caso donde importaría: un usuario que
> haya **desactivado** Steam Play global y necesite el pin por juego. Configuración rara en un Deck.
>
> *(Y esto no lo arregla actualizar SLSsteam: es código 100 % de LumaDeck. Independiente de la
> versión de SLSsteam instalada.)*

**Neto de los dos días: cero accionables de SLSsteam**, un [PRESTABLE] de prioridad media-baja en
LumaDeck, y la constatación de que la API local ya es un canal de control razonablemente completo
y documentado (aunque apagado por defecto).

#### 7.7.19 Día 2026-08-15 — 15 commits, release `20260815201341` (el día del cierre)

Último día del rango, y el más útil para nosotros de los tres finales. Tres cosas de sustancia, un
arreglo de build, la release, y cuatro commits peleándose con el Markdown de GitHub.

**`ac6830b` `chore(sdk): Fix names & function order in CUtl`** — cosmético, prepara el terreno.

**`9be780a` `feat(sdk): Parse CUtlRBTree as actual binary tree now`** — el commit técnico del día.
El día anterior había añadido `CUtlRBTree` con un `find()` que era una **mentira útil**: recorría
los elementos linealmente y comparaba claves, ignorando por completo que la estructura es un
árbol. Funciona, pero es O(n) sobre un contenedor diseñado para O(log n), y además asume que todos
los slots entre `0` y `size` están vivos (en un RBTree de Valve hay huecos: los nodos borrados
quedan en una free-list). Aquí lo hace bien: mapea los campos reales

```cpp
struct Element_t {
    int32_t leftIndex;      //0x0
    int32_t rightIndex;     //0x4
    uint8_t __pad0x0[0x8];  //0x8
    T key;                  //0x10
    T2* value;              //0x14
};                          //0x18
...
int32_t rootNodeIndex;      //0x14  (antes: dentro de un pad de 0x20)
uint32_t allocated;         //0x18
```

y `find()` desciende desde `rootNodeIndex` comparando `key` y saltando a `leftIndex`/`rightIndex`
hasta dar con `-1` (el centinela de "sin hijo"). También cambia la firma: devuelve
`Element_t*` en vez de `T2*`, y es `CUtlMap::at()` quien extrae `->value`. Detalle de diseño
correcto: quien tiene el nodo puede leer clave *y* valor.

`ee0021b` borra el comentario de "no verificado" con la nota de que **lo probó contra
`m_MapAppOwnershipTickets` y `m_mapPackages`, y ambos funcionaron**. Eso es la validación cruzada
que da credibilidad al layout.

> **Contexto honesto: `CUtlMap`/`CUtlRBTree` todavía NO se usan en ninguna parte.** Grep sobre
> todo el repo (`src/`, `tools/`, `include/`): la única aparición es su propia definición en
> `src/sdk/CUtl.hpp`. Es **andamiaje**, no una funcionalidad enviada. Ace mapeó los contenedores
> porque los va a necesitar (los mapas de paquetes y de tickets de ownership son exactamente lo
> que hace falta para dejar de adivinar el estado de licencias), pero al cerrar el rango son
> código muerto. No hay nada que "nos aporte" hoy.

**Un bug latente en ese `find()`, para el registro.** El chequeo `index == -1` está **al final**
del bucle, no al principio:

```cpp
int32_t index = rootNodeIndex;
for (;;) {
    const auto node = &elements[index];   // <-- deref antes de validar index
    if (node->key == key) return node;
    ...
    if (index == -1) break;
}
```

Con un **árbol vacío** (`rootNodeIndex == -1`, que es lo que Valve pone) la primera iteración lee
`elements[-1]->key`, o sea 0x18 bytes por debajo del array. Lectura fuera de rango, no escritura,
así que lo más probable es un falso negativo o un valor basura comparado — pero es un `read` OOB
real. No nos afecta (no usamos su SDK), y probablemente a él tampoco todavía porque el código está
sin usar. Lo apunto porque si algún día tomamos prestado el layout, el bucle hay que reescribirlo
con la guarda arriba.

**`d005614`** añade `steam.hpp` a `sdk.hpp` (include que faltaba). **`96cb06b`** afina la
redacción de `API.md`.

**`665fc8c` `fix(API): Fix first command getting executed twice`** — arreglo real de la API, y
explica por qué el bug existía. El código viejo hacía, en cada notificación de inotify:

```cpp
fstream.close();
fstream.open(path, std::fstream::in);
```

…partiendo de un `fstream` que `init()` había dejado **abierto**. Y `isEnabled()` se apoyaba en
`fstream.is_open()` como prueba de vida. El resultado es que el estado del stream y el estado
"¿estoy inicializado?" eran la misma variable, y un `echo > fichero` (que dispara **dos** eventos
de inotify: el truncado y el cierre-escritura) acababa procesando el primer comando dos veces.
El arreglo separa las dos cosas: un `bool initialized` propio para `isEnabled()`, `init()` cierra
el stream tras crear el fichero, y `onFileChange()` abre-lee-cierra en cada evento con un
`goto done` que garantiza el cierre en todos los caminos de error. De paso cambia
`strcmp(split[0].c_str(), "x") == 0` por `split[0] == "x"` — el `strcmp` era comparar un
`std::string` bajando a `const char*` por gusto.

*Relevancia para nosotros: si algún día LumaDeck usa esa API, **una operación duplicada importa**.
`install|appId|libraryIndex` ejecutado dos veces es benigno; `uninstall|appId` dos veces también;
pero el patrón de escritura correcto es `os.replace` de un fichero temporal (un solo `IN_MOVED_TO`)
y no `echo >`. Y ese arreglo llegó en `20260815201341`: en versiones anteriores el duplicado
está presente.*

**`182fdf0` `feat(Makefile): Makefile is now flag aware`** — calidad de vida de build. Calcula
`FLAGSSHA := sha256sum` de `CXXFLAGS + LDFLAGS` y mete los objetos en `obj/$(FLAGSSHA)/`, con el
`.so` como `bin/SLSsteam-$(FLAGSSHA).so` y un `link-bins` que hace `ln -f` al nombre canónico. Así
alternar entre builds (con/sin `TRACE`, distinto compilador) no invalida la caché del otro. El
detalle fino está en `embed-config.sh`/`embed-version.sh`: ahora **solo reescriben el header si el
contenido cambió**, porque si no el `mtime` nuevo forzaba recompilar `config.o` en cada cambio de
flags. Es el mismo problema de "generated header con timestamp" que nos morderá en `rva_feed` si
alguna vez generamos cabeceras; vale la pena tenerlo presente.

**`1444fa5` `fix(filewatcher): Add IN_MOVE_TO to watch events`** — el commit del rango con más
impacto directo en LumaDeck, ya documentado en §4.1:

```cpp
constexpr static int WATCH_MASK = IN_CLOSE_WRITE | IN_MOVED_TO;
```

Antes solo `IN_CLOSE_WRITE`. Un `os.replace()` —que es lo que hace `_commit_config()` de LumaDeck—
**no** genera `IN_CLOSE_WRITE` en el fichero destino; genera `IN_MOVED_TO` en el directorio. Por eso
en SLSsteam < `20260815201341` **la recarga en caliente de la config no se dispara cuando LumaDeck
escribe**, y el usuario tiene que reiniciar Steam sin saber por qué. Y afecta también a
`/tmp/SLSsteam.API`: escribir el comando con rename atómico no funcionaba antes de este commit.

**La release.** `c73e40b` sube `res/version.txt` y `src/version.hpp` de `20260728212859` a
`20260815201341`; `505b2c5` sube los dos PKGBUILDs (de `20260801163409`, o sea llevaban dos semanas
desfasados) y el `sha256sums` del tarball; `97364a2` añade la entrada de SafeMode:

```yaml
  20260815201341: #tag 20260815201341
    - d0c0ff6e...a6b3df8900 #ubuntu32_32 & steamdeck_stable - 20260804
```

> **Ojo con esto, es un detalle operativo real.** La entrada nueva lista **un solo hash** de
> `steamclient.so` (el build de Steam del 2026-08-04), mientras que la del tag anterior listaba
> dos. `Updater::verifySafeModeHash()` (`update.cpp:128`) busca `clientHashMap[VERSION]` y compara
> el SHA-256 del `steamclient.so` cargado. Si no coincide:
> - con **`SafeMode: yes`** → `LOG_NOTIFYERROR("Unknown steamclient.so hash! Aborting...")` y
>   `unload()`: **SLSsteam no arranca**;
> - con **`WarnHashMissmatch: yes`** → solo un toast de "please update".
>
> **Ambos vienen `no` por defecto** (`res/config.yaml` líneas 98 y 102, y los defaults en código
> `config.cpp:176-177` también son `false`), así que el usuario normal no ve nada. Pero si LumaDeck
> alguna vez activa `SafeMode` "por seguridad", combinado con un Steam que no sea exactamente el
> build del 08-04 el resultado es **SLSsteam abortando en silencio**. Recomendación: **no tocar
> `SafeMode`**, y si se quiere señal de desajuste, usar `WarnHashMissmatch`, que avisa sin matar.

**`5d76d5b`, `143dcba`, `a70939b`, `01a3b1e`** — cuatro commits, dos pares de mensajes idénticos,
todos arreglando el renderizado de `API.md` (*"Fix docs again I hate github's markdown. Why is it
so weird..."*). `01a3b1e` es el HEAD del rango. Cero contenido técnico; los cuento porque están en
los 205.

**Neto del día:** un arreglo que **nos importa de verdad** (`IN_MOVED_TO`, §4.1), un arreglo de la
API que importa si la usamos (`665fc8c`), andamiaje de contenedores que aún no se usa, y una
release cuya entrada de SafeMode conviene no activar. Cero accionables nuevos en lumalinux.

#### 7.7.20 Cierre del barrido — balance de los 22 días

**Lo que la ventana nos da gratis (nada que implementar; basta actualizar SLSsteam a
`20260815201341`):**

| # | Qué | Dónde | Por qué importa |
|---|---|---|---|
| 1 | `IN_MOVED_TO` en el filewatcher | §4.1, §7.7.19 | Sin esto el `os.replace` de `_commit_config()` **no** dispara la recarga en caliente |
| 2 | `CDKeys` | §7.2, §7.7.17 | Suministra la clave de CD en vez de saltarse el paso |
| 3 | Stutters de logros nativos | §7.7.10 | Micro-tirones al desbloquear, arreglados |
| 4 | Callbacks y lobbies con FakeAppIds | §7.7.11 | Multijugador con AppId falso ya no se rompe |
| 5 | Log de config con flag `Once` | §7.7.16 | Deja de inundar `~/.SLSsteam.log` |

**Accionables abiertos en nuestro lado, por prioridad:**

| Prioridad | Qué | Ref | Estado |
|---|---|---|---|
| **Media** | `std::realloc` de libc sobre memoria asignada por Steam en `AppendIdsToVec` | §7.7.12.a | **Abierto**, sin autorizar. Paso 1 = log de diagnóstico comparando `Plat_Realloc` vs `realloc` |
| Baja | Premisa falsa del comentario de `downloads.py:1399` + docstring de `steam_utils.py` | §7.7.18.a | **Abierto**, cosmético |
| Baja | Decidir si el pin de compat tool se elimina o se mueve a `SteamClient.Apps` | §7.7.18.a | **Abierto** |
| — | Refresco del schema embebido (`CDKeys`, `LogLevels`, sin `Notifications`) | §7.7.17 | **Hecho** (LumaDeck `a061b00`) |
| — | Borrado del round-trip dict de config (destructivo) | §4.1 | **Hecho** (LumaDeck `ecf2f65`) |
| — | §7.2 premisa falsa, §7.6 verificación falsa, `RESEARCH.md` §15.4 y §17 | — | **Corregidos** |

**Lo que el barrido corrigió de nuestra propia documentación** — y es el resultado menos cómodo
pero más valioso: §7.2 partía de una premisa **falsa** sobre `checkAppOwnership`; §7.6 afirmaba
haber verificado el parche de logros contra `20260723102618` con "firmas sin cambio", y era
**falso** — la ventana de rotura era 6 días y 2 releases más ancha de lo documentado (`59f8259`,
2026-07-20); §1.6 y §1.7 citaban precedentes upstream que **ya no existen**; `RESEARCH.md` §17
atribuía la rotura al día equivocado.

**Lo que NO nos aporta, dicho claro.** Seis de las siete "oportunidades prestables" que apunté
durante el barrido no sobrevivieron a la verificación: el escaneo ceñido a `.text` (con
`-z separate-code` el mapeo r-x ya es todo código), el cacheo del `ModuleRange` (medido: 0,06–0,22 ms
de parseo de maps frente a ~19 ms de SigScan, 43×–145× a favor de no optimizarlo), la navegación
por objeto raíz (3 de nuestros 5 objetivos no son virtuales; el único que lo es ya usa RTTI), la
regla de tipos (no leemos por máscaras en ningún sitio, y somos nosotros los que tenemos
`static_assert`, no él), copiar `Updater::isEnabled()` (`rva_feed.cpp:68` necesita el SHA de todas
formas), y la URL `master` del schema (el redirect de GitHub devuelve 200 y contenido idéntico).
El patrón es consistente y vale como lección: **el hallazgo sobrevive si se mide o se lee el
código; muere si se razona por analogía con lo que hizo upstream.**


---

### 7.8 Ventana `20260815201341` → `20260820085507` (2026-08-15 … 2026-08-20)

34 commits en 5 días activos y tres tags: `20260819120840` (sin release publicada),
`20260819131545` y `20260820085507`. Ventana corta y de bajo voltaje comparada con
§7.7 — no da para barrido día a día, así que va por temas.

#### 7.8.1 Panorama — más cimientos de SDK, y dos arreglos que sí importan

| Tema | Commits | Efecto para nosotros |
|---|---|---|
| Expansión del SDK | 9 (16 y 19-ago) | Ninguno hoy; confirma la dirección de §7.7.0 |
| `isUserSubscribedAppInTicket` con la firma mal | 1 (20-ago) | **Regalo gratis**: DLC en juegos con AuthSessions |
| Validación del feed de SafeMode | 1 (17-ago) | **Espejo de un fallo nuestro**, §7.8.3 |
| Logging: toggles y saltos de línea | 3 (19-ago) | Ninguno; no toca la superficie de config |
| Renombrado de assets de release | 1 (16-ago) | Nos rompió el instalador 44 h; ya arreglado |
| `chore(PKGBUILDs)` / `style` / docs de la API | 19 | Ninguno |

**El SDK sigue creciendo.** `CSteamClient` y `CValidator` nuevos, `CServerPipe`
ampliado, `SDK_Class`/`SDK_Struct` para fijar alineación y packing, y un repaso de
tipos por todas las interfaces `IClient*`. Es continuación directa del hilo
conductor de §7.7.0: sustituir hooking por patrones de bytes por navegación
estructural. Nada accionable, pero cuanto más se apoye en estructuras menos le
rompen los updates de Steam — y menos ruido de re-derivación para el ecosistema.

#### 7.8.2 `isUserSubscribedAppInTicket` — la firma estaba mal [REGALO GRATIS]

`6546411` (20-ago, release `20260820085507`). El hook estaba declarado con la
firma equivocada:

```c
- (IClientUser*, uint32_t steamId, uint32_t a2, uint32_t a3, AppId_t appId)
+ (IClientUser*, uint64_t steamId,                           AppId_t appId)
```

Un `SteamId` de 64 bits leído como tres argumentos de 32, así que el `appId` que
llegaba al hook era basura. Sus notas de release: *"Fix DLC unlocker in games using
AuthSessions"*.

**Nada que implementar.** Llega solo al instalar la release del 20-ago, que es la
que `setup.sh` resuelve hoy. Afecta a juegos que validan por ticket de sesión, no
al camino de instalación de LumaDeck.

#### 7.8.3 Validación del feed de SafeMode — el mismo agujero en `src/update.cpp`, y peor [BAJA, cosmético]

`aa27169` (17-ago), con el comentario del propio Ace: *"today github decided to do
some trolling by returning a wholly different message"*.

```cpp
- if (res == 0 && data.size() > 0)
+ if (res == 0 && data.starts_with("SafeModeHashes:\n"))
```

GitHub le devolvió **200 con un cuerpo que no era el feed**, y su comprobación de
tamaño lo dejó pasar.

**Nuestro `src/update.cpp:39-51` no tiene ni la comprobación de tamaño** en el
camino de éxito: solo valida en el camino de caché.

```cpp
int res = Curl::getString(url, data);
if (res != 0) { data = loadFromCache(); if (data.size() < 1) return false; }
// res == 0 -> se usa `data` tal cual, sea lo que sea
```

Y hay una segunda mitad que SLSsteam no tiene: `saveToCache(data)` (línea 80) se
ejecuta **incondicionalmente** tras un parseo sin excepción.

**Medido** (compilando contra yaml-cpp, no razonado por analogía — el parseo es
independiente de la arquitectura). Corrige una versión anterior de esta tabla que
daba el HTML por parseable: depende de la forma del cuerpo, y hay que medirlo caso
a caso.

| Cuerpo recibido con HTTP 200 | `YAML::Load` | Entradas | ¿Envenena la caché? | ¿Toast? |
|---|---|---|---|---|
| Vacío / solo espacios | parsea | 0 | **sí** | sí |
| JSON de error de la API | parsea | 0 | **sí** | sí |
| Página HTML (cualquier forma probada) | **lanza** | — | no | sí |
| Error de CDN (`Guru Meditation:`) | **lanza** | — | no | sí |
| Texto plano (`Not Found`) | **lanza** | — | no | sí |
| Feed bueno | parsea | 1 | — | no |

Dos lecturas distintas, y conviene no mezclarlas:

* **Envenenar la caché** solo lo consiguen el cuerpo vacío y el JSON de error —
  los dos parsean sin lanzar, dejan `clientHashMap` vacío y llegan a
  `saveToCache()`. El HTML no: `operator[]` sobre un escalar lanza, cae en el
  `catch` y `saveToCache()` no se ejecuta. Que sea la superficie estrecha no la
  hace teórica: el JSON de error es exactamente lo que devuelve el CDN de GitHub
  cuando se pone tonto, que es el caso que describe Ace.
* **Disparar el toast** lo consiguen *todos*, por caminos distintos: los que
  parsean porque `clientHashMap` queda vacío y `verifySafeModeHash()` no encuentra
  el grupo; los que lanzan porque `init()` devuelve `false`. `main.cpp:150` es un
  `||`, así que cualquiera de los dos basta.

**Severidad: baja, cosmética.** En lumalinux el hash es **advisory, no un gate**:
`main.cpp:150` solo dispara un toast, los hooks se instalan igual y el gate real es
el escaneo de patrones. El daño máximo es un aviso de *"steamclient.so hash not in
the verified list (Steam may have updated)"* cuando no ha pasado nada — y, en los
dos casos que envenenan, que persista en arranques sin red.

**Esto NO es una diferencia con SLSsteam.** Una versión anterior de esta sección
decía que allí el fallo sería más grave porque `SafeMode: yes` aborta la carga.
Es engañoso: `SafeMode: no` es el **default de upstream**, y `setup.sh:224` lo
fuerza a `no` de todas formas (`_sls_ensure_kv SafeMode no`), así que en nuestro
despliegue SLSsteam arranca exactamente igual que lumalinux cuando el hash no
cuadra — lo dice el propio comentario de `setup.sh:200` y el de `main.cpp:145`
(*"Mirrors SLSsteam's SafeMode=no"*). La única asimetría real que queda es que
SLSsteam **ofrece** el toggle y lumalinux no lo tiene; irrelevante aquí.

No rompe instalaciones. Pero es exactamente el tipo de mensaje que manda a
perseguir un problema inexistente, así que merece las líneas que cuesta.

**Accionable:** validar el cuerpo en el camino de éxito y, si no valida, tratarlo
como un fallo de fetch (tirar de caché) en vez de usarlo y guardarlo. Ojo con la
forma del validador: el `starts_with("SafeModeHashes:\n")` de Ace **no vale tal
cual** para nosotros, porque nuestro `res/updates.yaml` empieza por 26 líneas de
comentarios; y un `find("SafeModeHashes")` a secas casaría con la línea 3 de
nuestro propio encabezado. Tiene que ir anclado a principio de línea.

#### 7.8.4 `Notifications` — `setup.sh` escribe una clave que ya no existe [BAJA, cosmético]

La opción desapareció de `config_default.hpp` en la release del 15-ago (§7.7.19);
ahora se controla vía `LogLevels`. Verificado contra el upstream de hoy: `NotifyInit`
y `LogLevels` siguen; `Notifications` no.

`setup.sh:210` sigue haciendo `sed -i "s/^Notifications:.*/Notifications: yes/"`.

**Inocuo**, y conviene ser preciso sobre por qué: SLSsteam solo reporta claves
**faltantes** (`ELoadError::MissingKey`, `config.cpp:121`), no sobrantes, así que
un `Notifications:` residual en una config vieja se ignora en silencio; y en una
config nueva el `sed` simplemente no casa.

Lo que sí está mal es el mensaje de la línea 234, que anuncia
*"NotifyInit/Notifications=yes"*: dice haber hecho algo que no hace.

LumaDeck ya está al día por otra vía — `slssteam_schema.py` refrescó su config de
referencia en `a061b00` y no lleva `Notifications`.

**Accionable:** quitar el `sed` muerto y corregir el mensaje.

#### 7.8.5 Balance de la ventana

| # | Qué | Estado |
|---|---|---|
| 1 | DLC en juegos con AuthSessions | **Gratis** al actualizar SLSsteam |
| 2 | Validación del feed de SafeMode en `src/update.cpp` | Abierto, baja |
| 3 | `sed` muerto de `Notifications` en `setup.sh` | Abierto, baja |

Sin hallazgos de prioridad media o alta. El accionable de §7.7.12.a (`std::realloc`
sobre memoria de Steam) sigue siendo el único abierto de peso, y esta ventana no
aporta nada nuevo sobre él.

---

### 7.9 Ventana `20260820085507` → `dev@3f8e429` — barrido día a día (rama `dev`, **sin publicar**)

*Método: lectura de diffs commit a commit sobre el clon de `AceSLS/SLSsteam`, desde
`65b6ee1` (= tag `20260820085507`, donde acaba §7.8) hasta `3f8e429`. **112 commits,
2026-08-20 → 2026-09-01**, +22k/−1.2k líneas —de las que ~16k son la sustitución de
LuaBridge del 31-ago—. Cero releases y cero tags en el rango.*

> **⚠️ Esta sección NO documenta una release. Documenta la rama `dev`.**
>
> `main` sigue clavado en `65b6ee1`, exactamente donde lo dejó §7.8: ni un commit, ni
> un tag, ni una release en trece días. Los 112 commits viven **solo en `dev`**, y
> `setup.sh` resuelve la release publicada — la del 20-ago. **Nada de esto ha llegado
> a un usuario todavía.**
>
> Todos los §7.x anteriores documentan código desplegado; éste documenta código que
> *va a* desplegarse. Léelo como aviso, no como delta. **Lo que hay que vigilar es el
> merge de `dev` a `main` y el tag que venga detrás**: ese día aterriza todo de golpe,
> y es cuando los accionables de §7.9.13 pasan de "pendientes" a "urgentes".
>
> Corolario de método, y por eso esta ventana casi se nos pasa entera: los barridos de
> §7.4 a §7.8 se definieron siempre **de tag a tag**, y los tags de SLSsteam se ponen
> en `main`. Mirando solo ahí, esta ventana parecía vacía. **La rama `dev` no había
> aparecido nunca en este documento.** De aquí en adelante, un barrido que solo mire
> `main` está incompleto por construcción.

*Cierre de la ventana anterior: los dos accionables abiertos de §7.8 se cerraron el
20-ago, antes de que empezara ésta — `49e4279` (validación del cuerpo del feed) y
`51297c4` (el `sed` muerto de `Notifications`). La tabla de §7.8.5 todavía los da por
abiertos.*

#### 7.9.0 Panorama — dos hilos, y uno de ellos cambia la naturaleza del proyecto

| Fecha | Hito | Efecto para nosotros |
|---|---|---|
| 08-21 | Se saca `process.cpp`: "¿qué proceso se acaba de conectar?" deja de vivir dentro de FakeAppIds | §7.9.2 |
| 08-22 | Detección de SteamDRM y Denuvo leyendo el binario del juego. `SmartTickets` pasa de apagado a `0x1` **en un día** | §7.9.3 |
| 08-23 | El bug de excepciones que ya arrastramos, y `library-inject` vaciado | **★ §7.9.4** |
| 08-24 | **Nace la API de plugins Lua**: LuaJIT + LuaBridge, hooking desde script | **★ §7.9.5** |
| 08-26 | Plugins **desactivados por defecto** (`Plugins: no`) + aviso de código arbitrario | §7.9.7 |
| 08-27 | `setAdditionalApps`: el batch se comía las altas previas | **§7.9.8** |
| 08-29 | Refactor de `Hooks` a jerarquía de clases (579 líneas) | verificación en §7.9.10 |
| 08-31 → 09-01 | Quita y devuelve API pública publicada hace 7 días | **§7.9.11, §7.9.12** |

**Dos hilos conductores.** El primero continúa el de §7.7.0 y §7.8.1: sustituir bytes
por estructura, ahora extendido a *analizar el binario del juego* en vez de adivinar
por su nombre. El segundo es nuevo y es el que importa: **SLSsteam deja de ser una
librería con hooks y empieza a ser una plataforma con API de plugins.** 34 de los 112
commits son Lua.

**Regalos gratis en esta ventana:** ninguno. A diferencia de §7.7 y §7.8, aquí no hay
nada que nos llegue arreglado por actualizar — porque no hay nada que actualizar.

#### 7.9.1 Día 2026-08-20 — 2 commits (nada)

**`7186151` `feat(API): Change confirmations to LOG_API & add support for multiline
commands`** — su canal de comandos (`/tmp/SLSsteam.API`) leía **una** línea de 128
bytes; ahora lee el fichero entero y ejecuta línea a línea. De paso sustituye la
cadena de `goto done` por `return` y mueve el `lock_guard` al principio de
`parseCmd()` en vez de repetirlo en cada rama.

**`5d01066` `chore(PKGBUILDs): Update`** — plumbing de release.

**Nos afecta:** nada. `API: no` es el default y no lo usamos.

#### 7.9.2 Día 2026-08-21 — 7 commits (cimientos de tickets)

**`d056fda` `refactor(process): Move process logic out of FakeAppIds`** — saca de
`fakeappid.cpp` los ~86 líneas que leían `/proc/<pid>/comm` y `/proc/<pid>/environ`
para averiguar el `SteamAppId` del proceso conectado, y las lleva a un `process.cpp`
nuevo con un `g_processMap` global indexado por pipe. Su mensaje dice el porqué: *"lo
vamos a necesitar también para los tickets"*.

**`5882bed` `feat(apps): Hide family shared state from games`** — engancha
`IClientAppManager::GetAppStateInfo` y, tras llamar al original, reescribe el
resultado: pone `ownerAccountId` al usuario actual, `realOwner` a 0 y limpia
`k_EAppOwnershipFlagsBorrowed` de `ownershipFlags`. Gated en `DisableFamilyShareLock`
y en que haya un appId de pipe activo. Trae de paso el enum `EAppOwnershipFlags`
completo y la struct `AppStateInfo_t` con su padding documentado.

**`1b9b556` + `91a4336` `feat(ticket): Add analysis for 32 & 64 bit PE/ELF targets`** —
parsers de cabeceras de ejecutables Windows y Linux, en `process.cpp`.

**`de8d555` `feat(ticket): Add SmartTickets config option`** — clave nueva. **Nace
`SmartTickets: no`**, o sea apagada. Retener este detalle para §7.9.3.

`6dfaf3d` y `861b342` son README y estilo.

**Nos afecta:** nada. Capa de tickets y propiedad = frontera suya (§5). El
family-share ni nos roza.

#### 7.9.3 Día 2026-08-22 — 20 commits (el día más cargado: Denuvo, y dos que sí nos tocan)

El grueso es afinar el análisis de binarios:

- **`0fec99d` + `c1a958f`** — detección de Denuvo: cuenta secciones características
  **y** exige que `.text` tenga entropía > 7.0 (código ofuscado). Dos condiciones, para
  no disparar falsos positivos.
- **`9571cdc` `Scan open files to detect DRM in them aswell`** — no basta con el
  ejecutable: recorre los descriptores abiertos del proceso, porque muchos juegos
  meten la protección en una librería aparte.
- **`4aa94a7` `Probe file header instead of relying on file extensions`** — añade
  `checkMagic()` y deja de fiarse de la extensión.
- **`79a45a7`, `cd10f2c`** — más secciones de código, y `Elf_Sheader` → `Section_t`.
- **`95d56fd` `Make LogLevel dynamic`** — `IExecutableFile::load()` recibe ahora un
  `LogLevelFlags_t`: los fallos de lectura de ficheros que van a fallar siempre bajan
  de nivel, los del ejecutable principal se quedan. Buen detalle de higiene.

**`2febc71` `Change SmartTickets to bitwise flags`** — y aquí el cambio que hay que
anotar:

```cpp
- smartTickets = getSetting<bool>(rootNode, "SmartTickets", false);
+ smartTickets = getSetting<SmartTicketsFlags>(rootNode, "SmartTickets", 1);
```

`0x1` = SteamDRM, `0x2` = Denuvo. **En un solo día pasó de apagada a venir encendida
en su mitad barata.** Su propia medición, en el comentario de `config_default.hpp`:
*"less than 10ms for SteamDRM, up to 20-2000ms for Denuvo on my SSD"* — por eso Denuvo
se queda fuera del default. El análisis se dispara en `ConnectPipe`
(`hooks.cpp:499-506`), o sea **una vez por proceso que se conecta al cliente**.

**`f20ef6c` `Remove denuvo pipe counting for now`** — se arrepiente de una parte y la
retira, con una frase que retrata cómo trabaja: *"It only works on new games. I'd
rather have consistent & predictable behaviour than something that sometimes works"*.

**`79a67ac` `feat(achievements): Add cooldown when no achievements were fetched`** —
`Achievements::setCooldown()` marca el appId 10 minutos cuando no encuentra schema,
y `getReviewersForGame()` sale temprano mientras dure. Motivo: hay juegos que se
atragantan con `k_EResultNoConnection` repetido y provocan stutter. Las llamadas
nuevas caen dentro de `sendAndRecvGetPlayerStats` y `sendAndRecvGetUserStats` — las
dos funciones que ancla `sls_achievement_unblock` (ver §7.9.10).

**`1bbcbc8` `feat(apps): Add LaunchOptions config option`** — opciones de arranque por
juego con `%command%`, resueltas en `Apps::spawnGame` (`apps.cpp:396-410`) con dos
comodines: `UINT32_MAX-1` para todos los no poseídos, `UINT32_MAX` para todos (el
primero gana).

El resto (`ba13afd`, `2ce4ee3`, `8861fd7`, `ec7b3c2`, `cc6bb96`, `b963f31`, `ab39e8b`,
`0c84eba`, `3c8ebc7`) es logging, renombrados y un merge de `main`.

**Nos afecta:**
- El análisis de binarios: nada. No toca instalación, depot keys ni manifests.
  Operativamente: **si alguien reporta arranques lentos tras actualizar SLSsteam,
  `SmartTickets` es el primer sitio donde mirar**; se apaga con `SmartTickets: 0`.
- El cooldown de logros: inocuo, pero toca nuestras funciones ancla. Verificado en
  §7.9.10.
- `LaunchOptions`: **a vigilar**. LumaDeck también escribe opciones de compatibilidad
  (`steam_utils.py`). Hoy no chocan porque nadie usa la clave nueva, pero ahora hay
  dos sitios que pueden querer mandar sobre lo mismo.

#### 7.9.4 Día 2026-08-23 — 6 commits ★ (`library-inject` vaciado, y el bug que lo explica)

**Dos commits que parecen independientes son el mismo problema**, y esa conexión es lo
que hace útil este día.

**`148b9a1` `fix(library-inject): Replace with empty file`**, con el cuerpo: *"la_objsearch
messes up exceptions in a way that they do not get caught anymore"*. Comenta
`tools/library-inject/main.cpp` **entero**, de la primera línea a la última.

**`1f10312` `fix(config): Fix crashes when config is malformed/conversions are
invalid`**, el mismo día:

```cpp
- catch (YAML::BadFile& bf)        →  catch (...)
- catch (YAML::ParserException& pe)→  (fusionado en el anterior)
- catch (YAML::BadConversion& er)  →  catch (...)
```

Son el mismo bug visto por los dos extremos. `library-inject` se cargaba como
**auditor** del loader implementando `la_objsearch`, y eso rompía el despacho de
excepciones de C++: los `catch` tipados dejaban de capturar. Resultado: un
`config.yaml` mal formado, en vez de caer en el `catch` y tirar de defaults,
**tumbaba SLSsteam**. Ace lo tapó por los dos lados — quitó la herramienta y ensanchó
las capturas a `catch (...)`.

**a) Lo que ya estamos enviando: el manejo de excepciones de SLSsteam está roto hoy. [BAJA, pero presente — no futuro]**

Esta es la mitad que importa, y va primero porque **no llega con el merge: ya está
desplegada en todos nuestros usuarios**.

`lumalinux` no está expuesto: implementa `la_version`, `la_preinit` y `la_objopen`
(`main.cpp:377-399`) pero **no `la_objsearch`**, que es la interfaz culpable, y además
corre por `LD_PRELOAD` a propósito (§0.1).

Pero `library-inject` **sí** la implementa, y somos nosotros quienes lo instalamos
(`setup.sh:1044`) y quienes lo ponemos **el primero** del `LD_AUDIT`
(`setup.sh:881`). O sea que en nuestro despliegue, ahora mismo, SLSsteam corre con el
despacho de excepciones corrompido: sus `catch` tipados no capturan. Y hasta que llegue
una release con el `catch (...)` de `1f10312`, tampoco tiene la red que Ace le puso.

Consecuencia concreta, la que él mismo encontró: un `config.yaml` mal formado, en vez
de caer en el manejador y tirar de defaults, **tumba SLSsteam entero**. Superficie
estrecha —hace falta que la config se corrompa o que una conversión falle— pero real y
activa hoy, no hipotética.

**Valor operativo:** si alguna vez aparece un SLSsteam que muere en el arranque sin
explicación, `~/.config/SLSsteam/config.yaml` pasa a ser una hipótesis concreta y
comprobable. Antes de esto no teníamos ninguna.

Y una lectura de rebote sobre nosotros mismos: llevamos meses instalando un componente
cuyo efecto secundario nadie había caracterizado. No lo escribió Ace en ningún sitio
hasta que lo rompió — pero el punto es que lo desplegamos sin saber qué hacía más allá
de *"redirige libcurl"*, que es lo único que dice nuestro
`decouple-headcrab-plan.md:84`.

**b) Lo que nos llegará: un `.so` de 0 bytes, y nuestro `setup.sh` lo instalará. [BAJA, cosmético]**

No es que deje de construirse — **construye un fichero vacío**:

```make
library-inject.so: main.cpp
	#g++ main.cpp -O3 -m32 -fPIC -shared -std=c++20 -o library-inject.so
	#Disabled, since having a la_objsearch fucks up exceptions for some reason
	-rm "library-inject.so"
	touch "library-inject.so"
```

Y la cadena hasta nuestro disco está entera, porque **todos los controles preguntan
"¿existe?" y ninguno "¿tiene algo dentro?"**:

| Eslabón | Test | ¿Pasa un fichero de 0 bytes? |
|---|---|---|
| `Makefile` raíz → `audit-libs` | llama a `library-inject` | sí |
| `Makefile:63` → `link-bins` | `test -f tools/library-inject/library-inject.so && ln -f … bin/` | **sí** |
| `make-releases.sh` | empaqueta `bin/` | sí |
| `setup.sh:1044` | `[[ -f "${TMP_DIR}/sls/bin/library-inject.so" ]]` | **sí** → `install` + `ok "Deployed …"` |
| `setup.sh:881` | `[ -f "$SLS_DIR/library-inject.so" ]` | **sí** → `LD_AUDIT="…/library-inject.so:…/SLSsteam.so"` |

**Medido, no razonado.** Fichero de 0 bytes en `LD_AUDIT`, solo y acompañado:

```
$ LD_AUDIT="$PWD/empty.so" /bin/true
ERROR: ld.so: object '…/empty.so' cannot be loaded as audit interface: file too short; ignored.
exit=0
```

**No es fatal.** El loader lo descarta con `ignored.`, procesa las entradas
siguientes de forma independiente y el proceso arranca: **SLSsteam se carga igual y la
inyección no se pierde**. Verificado también que una entrada inválida no arrastra a la
otra.

Lo que produce es, por un lado, un `ERROR:` con todas las letras en el stderr de Steam
**en cada arranque** —justo la línea que manda a un usuario a abrir un issue por un
problema inexistente—, y por otro un `ok "Deployed …"` nuestro que afirma haber
desplegado algo útil. El mismo pecado que corregimos en §7.8.4 con el mensaje de `Notifications`:
decir que has hecho algo que no has hecho.

**Accionable:** `-f` → `-s` (existe **y no vacío**) en `setup.sh:1044` y `setup.sh:881`.
El camino de "no está" ya existe y ya hace lo correcto —
`warn "library-inject.so not in the archive — the wrapper will load SLSsteam.so alone."`
y `LD_AUDIT="$SLS_DIR/SLSsteam.so"` a secas—, así que con `-s` un `.so` vacío cae por
ahí solo. Dos caracteres, y cubre tanto hoy (release buena, nada cambia) como el día
del merge.

**No** conviene borrar el soporte de `library-inject`: Ace lo ha *comentado*, no
eliminado, y su propio comentario dice *"for some reason"*. Es una desactivación de
diagnóstico, no una decisión cerrada; si vuelve, `-s` lo reacepta sin tocar nada.

El resto del día: `82a9e85` mete `lib/libluajit.a` al repo sin usarlo aún —el aviso de
lo que viene—, `49186e7` pasa los `CFileWatcher*` a smart pointers, `5f9d9b4` acelera
`MTVariable` y `78789fb` limpia la config.

#### 7.9.5 Día 2026-08-24 — 18 commits ★ (nace la API de plugins)

El día que cambia el proyecto. `0420645 feat(lua): Add rudimentary API` mete LuaBridge
(~7k líneas de librería ajena en `include/`) y construye encima una API que expone las
tripas de SLSsteam a scripts en `config/plugins`.

Lo que suelta ese día, en orden: hooking de funciones arbitrarias desde Lua
(`a17692e` añade además vigilancia del directorio), un sistema de callbacks
(`07be566`), acceso al decompilador para resolver por nombre (`f4a6b9f`), y dos piezas
que apuntan directamente a lo nuestro:

- **`0168c7d` `feat(lua): Add CUser::postCallback & MemHlp::hexdump`**
- El callback **`SLSsteam::initialized`**, documentado como *"Fired when Steam has
  finished initializing `CUser`, making it safe to access"*.

También `6f7e0d1 fix(vft): Fix bug, overwriting vft` — un bug real: `VFTableInfo_t::init()`
tomaba **referencia** a `Decompiler::vftables[typeName]` y la machacaba al aplicar
`subClassIndex`; ahora hace copia. Y `1e6b96f` reconstruye `libluajit.a` con GCC 4 para
casar con la ABI antigua (`_GLIBCXX_USE_CXX11_ABI=0`) que usa Steam.

**Nos afecta — el hallazgo estructural de la ventana [PRESTABLE, alto]:**

| Expuesto | Qué resuelve de lo nuestro |
|---|---|
| `SLS.registerCallback("SLSsteam::initialized", fn)` | Hoy capturamos el `CUser*` **de rebote**, como efecto lateral del guard de logros que parcheamos (RESEARCH §18). Esto es un aviso soportado |
| `CUser::postCallback(type, ptr, size)` | Es lo que hace `license_reconcile.cpp` — **pero ver el matiz de abajo** |
| `LuaHook` + `place()`/`remove()` | El andamiaje que nos cuesta `lmhook.cpp` |
| `VFTableInfo_t` | La resolución por RTTI de §15 |

**Matiz importante sobre `postCallback`, y corrige una lectura inicial de esta
sección.** No es resolución por nombre. Así lo resuelve SLSsteam
(`src/sdk/CUser.cpp:48-52`):

```cpp
void CUser::postCallback(const ECallbackType type, void* pCallback, const uint32_t callbackSize)
{
    const static auto fn = reinterpret_cast<...>(Patterns::CUser::PostCallback.address);
    fn(this, type, pCallback, callbackSize, 0);
}
```

**Busca bytes, igual que nosotros** — `Patterns::CUser::PostCallback` está en su
`patterns.cpp:145`, con el mismo modo de fallo que nuestro `NotifyLicensesUpdated`. Y
el `example.lua` lo confirma por el otro lado: para engancharse a esa misma función
escanea a mano, `memhlp.patternScan("E8 ? ? ? ? 8B 75 ? 89 D8", modSteamClient)`.

Así que la lectura correcta es: **no elimina la fragilidad, la traslada**. Pasaríamos
de mantener *nuestro* patrón a depender de que él mantenga *el suyo*. Lo que se gana
es mantenimiento compartido —él re-deriva en cada release y eso lo aprovecha todo el
ecosistema—, que no es poco, pero no es lo mismo. **El único punto genuinamente limpio
sigue siendo `SLSsteam::initialized`**, porque ahí no hay patrón de por medio.

**Las pegas, que son serias y ninguna descartable:**

1. **Está en `dev`, sin release.** Y ver §7.9.11 y §7.9.12: la API cambia de semana a
   semana.
2. **`Plugins: no` por defecto** (§7.9.7), con el aviso *"SLSsteam plugins can run
   arbitrary code! So only run plugins you trust"*. Usarlo obligaría a `setup.sh` a
   **activar en la máquina del usuario la ejecución de Lua arbitrario desde un
   directorio**, por nuestra comodidad. Es un cambio de superficie de ataque que no se
   hace a la ligera ni en silencio.
3. **El propio README desaconseja el hot reload**: *"While hot reloading Luas is
   possible it's highly advised against doing so. You have been warned."*
4. **Reintroduce la dependencia dura que §0.1 celebra no tener.** Hoy la repartición
   `LD_AUDIT` (SLSsteam) / `LD_PRELOAD` (lumalinux) hace que lumalinux funcione
   *aunque SLSsteam cambie por dentro*. Como plugin, dependeríamos de que la API de
   otro no se mueva: cambiaríamos fragilidad ante los updates de **Steam** por
   fragilidad ante los de **SLSsteam** — y la segunda la controla quien hace 112
   commits en trece días.

**No accionable, con intención.** Si la API sobrevive al merge y se estabiliza, el
candidato **no es migrar lumalinux**: es un plugin mínimo que haga *una* cosa —
entregarnos el `CUser*` por `SLSsteam::initialized`— y dejar el `.so` como está. Eso
quita un hack sin crear una dependencia estructural. Revisar cuando haya tag.

#### 7.9.6 Día 2026-08-25 — 14 commits (la API se llena)

Callbacks nuevos: `8b9ccf6` `SLSsteam::luaReload` (*"úsalo para limpiar tus cambios en
memoria antes de que borre el estado"*), `ac0b16b` `SLSsteam::configLoaded` más lectura
del `rootNode`, `2b628b2` `Network::recvPkt` / `Network::sendPkt` con las clases
`CNetPacket` / `CNetPacketBody` expuestas. `81252c8` amplía la lectura de config a
tipos arbitrarios. Higiene: `d62707c` crea el directorio de plugins si falta,
`3ea3b87` solo ejecuta `.lua`, `0453823` en orden alfabético (recogidos en un
`std::set` porque `directory_iterator` no está ordenado), `7faec87` más niveles de
log, `1306feb` deja de escribir rutas completas al log.

**Nos afecta:** nada. Es su casa.

#### 7.9.7 Día 2026-08-26 — 12 commits (se echa el candado, y muere la herramienta)

**`c9c0bbb` `fix(library-inject): Disable`** — segunda mitad de §7.9.4: el día 23 vació
el fuente, hoy desactiva la compilación en el `Makefile`.

**`2432a37` `feat(lua): Disable plugins by default & add config option`** — clave nueva
`Plugins: no` en `config_default.hpp`, `g_config.plugins` gateando `Lua::runLua()`. El
comentario de `c0f42f2` lo completa: *"SLSsteam plugins can run arbitrary code! So only
run plugins you trust"*. Nota de diseño: **el gate está en ejecutar el fichero, no en
montar el sistema** — *"the rest of the system stays active to allow for hot
reloading"*.

**`60a60db` `fix(filewatcher): Fix stop`** — su `watchLoop` era un `for(;;)` bloqueado en
`read()` que no se podía parar; ahora hay flag `running` y un manejador de señal para
desbloquear la lectura. Relevante porque ese mismo `CFileWatcher` es el que sirve el
hot-reload de `config.yaml` del que depende nuestro Add Game.

`3f9c705` hace configurable el `eventMask` del watcher (ver el bug del día siguiente),
`85dac66` añade `reloadlua` al canal de comandos, `ee2620c` arregla que `Utils::exec`
se comiera el primer argumento (`argv[0]` debe ser el nombre del ejecutable),
`d648297` impide colocar un `LuaHook` cuando el target es `LM_ADDRESS_BAD`, `56bc770`
y `7d7a5e6` son movimientos de código.

**Nos afecta:** confirma §7.9.4. Y `Plugins` es la tercera clave nueva de la ventana
(ver §7.9.13).

#### 7.9.8 Día 2026-08-27 — 13 commits (el arreglo que más cerca nos pilla)

**`cf63d67` `fix(config): Fix batch setAdditionalApps`:**

```cpp
- auto _newApps = newApps.empty();
- auto _removedApps = removedApps.empty();
+ auto _newApps = newApps.get();
+ auto _removedApps = removedApps.get();
```

`MTVariable::empty()` no pregunta "¿está vacío?": **vacía la variable** y devuelve el
resultado. O sea que cada recarga de config **descartaba las altas pendientes** que
Steam aún no había consumido, y solo sobrevivían las de la última pasada. El escenario
que lo dispara es exactamente el nuestro: **dos Add Game lo bastante seguidos** como
para que la segunda recarga pise a la primera.

**No afirmo que explique ningún síntoma nuestro**, y conviene no llevárselo así:
nuestro camino de no-reinicio no depende del callback de SLSsteam sino del reconcile
propio (`LicensesUpdated_t`, disparado por el watcher de `keys.txt`, RESEARCH §18),
que es independiente de este bug. Queda como **hipótesis a descartar** si vuelve a
aparecer un "añadí dos y solo salió uno", no como causa identificada.

**`9c9de32` `fix(filewatcher): Fix eventMask not getting used in add watch`** — el
`eventMask` que `3f9c705` hizo configurable el día anterior **se ignoraba**:
`inotify_add_watch` seguía recibiendo la constante `WATCH_MASK`. Dos bugs de "el
código dice una cosa y hace otra" en dos días.

**`407b572` `fix(lua): Only full reload on DEBUG build`** — decisión prudente: en
release **no** recrea el `lua_State` al recargar, para no colocar hooks sobre código
que se está ejecutando. `f497d6d` añade recarga silenciosa de config en cada reload de
Lua (con un flag `silent` que evita disparar el callback y re-emitir los toasts de
error), `14034de` + `5d22865` meten `stateMutex`, `8397c56`/`df92dc5`/`9a4f779` pulen
el `YAMLNode` expuesto, `f801adb` amplía los eventos que disparan recarga.

**Nos afecta:** ver arriba. Y saca a la luz algo que no estaba escrito en ningún sitio
nuestro: **hay dos mecanismos de refresco de licencias en vuelo a la vez.** SLSsteam
emite `AppLicensesChanged_t` (`0xf90be`) al cambiar `AdditionalApps`; lumalinux emite
`LicensesUpdated_t` (`0x7d`) vía `NotifyLicensesUpdated`. Callbacks distintos, caminos
distintos, y ninguno sabe del otro. Funciona porque son idempotentes, pero es
coincidencia arquitectónica, no diseño — y es la clase de solape que un día da un
doble refresco raro. Frontera a vigilar, sin acción.

#### 7.9.9 Día 2026-08-28 — 4 commits

**`f03ad94` `fix(config): Do not track changes to AdditionalApps before
GetSubscribedApps was called`:**

```cpp
- if (!firstLoad)
+ //No need to post a AppLicenseChanged_t callback
+ //when GetSubscribedApps hasn't been called yet
+ if (!firstLoad && Apps::applistRequested)
```

Estrechamiento sensato. `76d9c7c` expone un `LuaMutex` RAII sobre el `stateMutex`
recursivo para que los plugins se protejan.

**Nos afecta:** inocuo. LumaDeck añade juegos con la sesión hecha desde hace rato, muy
por detrás de `GetSubscribedApps`.

#### 7.9.10 Día 2026-08-29 — 7 commits (el refactor grande, y la verificación de nuestro parche)

**`913a431` `refactor(hooks): Add proper class inheritation, automatic place & remove,
etc`** — 579 líneas de `hooks.cpp` y 196 de `hooks.hpp`. Aparece una interfaz `IHook`
con `std::unordered_set<IHook*> hooks` que se autopuebla desde el constructor, y
`DetourHook`/`VFTHook`/`LuaHook` heredan de `Hook<T>`; los constructores aceptan ya un
`Pattern_t&` o un `VFTableInfo_t&`. Efecto colateral visible en todo el árbol: los
objetos pasan a punteros.

```cpp
- return Hooks::CUser_CheckAppOwnership.tramp.fn(this, appId, pInfo);
+ return Hooks::CUser_CheckAppOwnership->tramp.fn(this, appId, pInfo);
```

`0a77412` borra acto seguido un comentario que decía que eso no podía funcionar
(*"This does not work! GCC sees no path to the hk* functions…"*) — el equivalente
exacto de lo que hicimos en `70e941e`/`67ff43f`.

También `f932133` mete `LOG_IF_EXISTS(g_pLog)` en **todas** las macros de log (el
cuerpo del commit dice *"Seems like SLSsteam was trying to preload libc.so.6"*; pese al
"preload" es un error de enlazado suyo, sin relación con nuestro orden de
`LD_PRELOAD`), `7850ab2` cambia `MTVariable` a `shared_ptr` con `.get()`→`.copy()` en
todo el árbol, `3131c5f` arregla `operator=(MTVariable<T>&)` (asignaba el puntero en
vez del valor) y `52bbe5e` pone inicializadores explícitos a las variables externas de
`Achievements`.

**Nos afecta: el parche `sls_achievement_unblock` sobrevive. [verificado]**

Un refactor de este tamaño en el fichero central es justo lo que mueve nuestros
anclajes, así que se comprobó pieza a pieza contra `src/sls_achievement_unblock.cpp`:

| Lo que el parche necesita | Estado en `dev@3f8e429` |
|---|---|
| `_ZN5CUser12isSubscribedE` | Sigue (`sdk/CUser.cpp:37`), ni inline ni virtual |
| `_ZN7CConfig12isAddedAppIdE` | Sigue (`config.cpp:324`) |
| `g_config` | Sigue |
| `_ZN12Achievements23sendAndRecvGetUserStatsE` | Sigue |
| `_ZN12Achievements25sendAndRecvGetPlayerStatsE` | Sigue |
| **Exactamente un `call isSubscribed` por función** (lo que exige `FindGuardCall`) | Se mantiene: `achievements.cpp:154` y `:236`, uno cada una |
| Forma del guard (`call` + `test` + `jne`) | Intacta: `if (…->isSubscribed(x)) return …;` |

El refactor no toca ninguno de los cinco símbolos, y los dos `setCooldown(appId)` que
§7.9.3 metió dentro de esas funciones no introducen llamadas a `isSubscribed`, así que
la unicidad que nos protege sigue en pie.

**Verificado sobre el fuente, no sobre el binario.** No se ha compilado `dev` ni
mirado el `.so` resultante: el orden de emisión y el inlining son cosa de GCC con
`-O3 -flto`. Esto dice que *el fuente conserva la forma que buscamos*, no que los bytes
vayan a caer igual. La red real es la que ya está puesta: `FindGuardCall` exige
coincidencia única y, si no la halla, `Apply()` sale con `LOG_WARN` y los logros
nativos se quedan apagados — fail-closed, nunca un crash (`maintenance.md` §D).

#### 7.9.11 Días 2026-08-30 y 2026-08-31 — 7 commits (limpieza, y la API empieza a moverse)

`d900d77` cierra el asunto de §7.9.4 arreglando que el `Makefile` fallara al borrar un
fichero inexistente (`-rm` + `touch`).

El 31 es el día de la inestabilidad: `32bb863` sustituye LuaBridge entero por una
versión moderna (+16k/−4.7k líneas, con `Coroutine`, `Expected`, `Enum`, `Overload`…)
y de paso `addConstant` → `addVariable`; `f4213e5` prepara el terreno para 64 bits y
saca `place_lua_hook` a export de C. Y entonces **quita API publicada hacía siete
días**: `2f9af1b` elimina `SLS.alloc/realloc/free` (que `957ece2` había añadido el
24-ago), `3a3ee4a` elimina `memhlp.getUserDataPtr`, `1721312` fusiona
`downloadStringWithHeaders` en `downloadString`.

**Nos afecta:** nada funcional. Pero es **la señal de estabilidad que más conviene
leer**: cualquier plugin escrito el 30-ago está roto el 31. Refuerza el "no planificar
contra esta API hasta que haya release" de §7.9.5.

#### 7.9.12 Día 2026-09-01 — 2 commits (y la pieza vuelve, en su tercera forma)

**`ac52879` `feat(lua): Readd unpackUserData as C export`** — devuelve lo que el día
anterior había quitado, pero con otra forma:

```cpp
+ extern void* unpack_user_data(const void* pData)
+ {
+     return reinterpret_cast<const luabridge::detail::Userdata*>(pData)->getPointer();
+ }
```

Tercera encarnación de la misma capacidad en ocho días: función Lua puenteada
(24-ago) → eliminada (31-ago) → **export de C** (01-sep). El porqué es técnico y
razonable: LuaBridge envuelve cada objeto C++ en un `Userdata`, y no se puede pasar
esa caja a un puntero a función crudo — hace falta desenvolverla. Como export de C se
puede llamar desde el FFI de LuaJIT, que es donde hace falta; como función puenteada
no servía para eso. Al quitarla el 31 rompió el camino de llamadas crudas y ha tenido
que devolverla.

**`3f8e429` `fix(docs): Fix example.lua & adjust Lua README`** — documenta el export
(*"Use this to convert `SLS.steamEngine` to the real `CSteamEngine` pointer etc."*) y
deja el `example.lua` como un plugin funcional, no un esqueleto. Merece leerse porque
**enseña el patrón real de uso** y de paso confirma el matiz de §7.9.5: el ejemplo
engancha `postCallback` **escaneando bytes de `steamclient.so` desde Lua**, usa
`VFTableInfo_t` con subclases, y en `SLSsteam::initialized` coge el `CUser`, llama a
`isSubscribed` y mete appIds en `AdditionalApps` vía `config:setAdditionalApps()`.

Es, en miniatura y desde un script, buena parte de lo que hace lumalinux — con la
diferencia de que **sigue habiendo patrones de bytes de por medio** para todo lo que
viva dentro de Steam.

#### 7.9.13 Cierre de la ventana — balance y qué vigilar

| # | Qué | Prioridad | Estado |
|---|---|---|---|
| 1 | `-f` → `-s` para `library-inject.so` en `setup.sh:1044` y `:881` | Baja | **Abierto** — nos llega solo el día del merge |
| 2 | `SLSsteam::initialized` como sustituto de la captura de rebote del `CUser` | Alta a futuro | Vigilar; **no** planificar hasta que haya tag |
| 3 | Doble mecanismo de refresco de licencias (suyo `0xf90be` + nuestro `0x7d`) | Baja | Anotado, sin acción |
| 4 | `LaunchOptions` vs. nuestras opciones de compatibilidad | Baja | Anotado, sin acción |
| 5 | `SmartTickets` como primer sospechoso de arranques lentos | Baja | Anotado, sin acción |
| 6 | Snapshot embebido de `slssteam_schema.py` | Baja | Esperando release, deliberadamente |

**Tres claves nuevas de config (`SmartTickets`, `LaunchOptions`, `Plugins`) y no nos
toca hacer nada [YA].** En cualquier ventana anterior habría sido trabajo: SLSsteam
solo escribe su config completa cuando el fichero **no existe**, y luego se queja por
cada clave que le falte (`ELoadError::MissingKey`, `config.cpp:127`) — tres claves
nuevas serían tres líneas de toast en cada arranque para todo usuario con config
previa. Ya está cubierto por el mecanismo correcto: `slssteam_schema.py` completa la
config contra el `config_default.hpp` de upstream **descargado en vivo**, añadiendo al
final sin tocar un byte existente. Y apunta a `main`:

```python
_CONFIG_DEFAULT_URL = "https://raw.githubusercontent.com/AceSLS/SLSsteam/main/src/config_default.hpp"
```

que en esta ventana es exactamente lo que queremos: las claves están en `dev`, así que
**no** las añade a nadie todavía, y las añadirá sola el día que se mergeen — que es el
día en que SLSsteam empezará a echarlas de menos. La rama elegida hace el trabajo sin
que intervengamos; el trabajo de `a061b00` se paga aquí. Solo queda desalineado el
snapshot embebido, y únicamente importa en un Deck sin red con SLSsteam recién
actualizada: se refresca cuando haya release, no antes (copiarlo de `dev` ahora sería
escribir claves que la release publicada no conoce).

El accionable de §7.7.12.a (`std::realloc` sobre memoria de Steam) sigue siendo el
único abierto de peso y esta ventana tampoco aporta nada sobre él.

**La conclusión no es ninguna de esas filas.** Es que **SLSsteam está dejando de ser
una librería con hooks para convertirse en una plataforma con API de plugins**, y eso
reordena la relación: hasta hoy la coexistencia se sostenía en que cada uno se quedaba
con un mecanismo de inyección y no se pisaban (§0.1, §5). Si la API cuaja, aparece por
primera vez un camino *soportado* para hacer desde dentro lo que hoy hacemos
parcheando desde fuera — con la contrapartida de atarnos a la estabilidad de un
proyecto que en nueve días quitó y devolvió su propia API pública dos veces.

**Y por eso esta sección se cierra abierta.** No hay release que analizar. Lo que hay
que vigilar es un evento concreto: **`dev` mergeado a `main` y tagueado.** Ese día
todo lo de arriba pasa de hipotético a desplegado, el fila 1 se vuelve urgente, y
tocará re-verificar §7.9.10 contra el binario publicado en vez de contra el fuente.
