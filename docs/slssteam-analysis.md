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

**Estado: EN CURSO.** Días desglosados a fondo: `2026-07-23`. El resto del rango está
inventariado y clasificado (ver §7.7.0) pero pendiente de desglose fino; se irá
completando día a día.

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
| 08-15 | Release `20260815201341`. **`IN_MOVED_TO`** en el filewatcher | **§4.1 actualizado** |

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
