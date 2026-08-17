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
