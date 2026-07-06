# Análisis — SLSsteam (vanilla) para lumalinux

*Fecha de investigación: 2026-07-06. Referencia: [`AceSLS/SLSsteam`](https://github.com/AceSLS/SLSsteam)
@ `main` (commit `ebfb079`, `VERSION = 20260624075231`), i386, C++20/CMake, libmem.
Compañero de [`slsteam-moon-findings.md`](slsteam-moon-findings.md) (el mod) y
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

### 1.6 Captura de instancia viva + self-heal vía RunIPCFrame [PRESTABLE — cómo obtener una vtable en runtime]

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

### 1.7 Hook "naked" ensamblado a mano (GetSteamId) [contexto, no prestable]

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

### 4.1 Hot-reload vía inotify

`CFileWatcher` (`filewatcher.cpp`) crea un `inotify_init`, observa el fichero con
`IN_MODIFY` en un hilo (`watchLoop`), y llama a un callback al cambiar.
`config.cpp:75-86` lo cablea: al editar `config.yaml`, `loadSettings()` recarga
**sin reiniciar Steam**. Los settings viven en `mtvar` (variables thread-safe),
así que los hooks leen el valor nuevo en caliente.

Si lumalinux algún día quiere recargar su `KeyStore` / config sin reiniciar el
cliente, `inotify + IN_MODIFY + recarga a estructura thread-safe` es el patrón
exacto, ~50 líneas. Candidato de baja prioridad.

### 4.2 API local (canal de control)

`api.cpp`: SLSsteam abre `/tmp/SLSsteam.API`, lo observa con el mismo filewatcher,
y ejecuta comandos escritos ahí (`echo "install|<appid>|<library>" > /tmp/SLSsteam.API`
→ `IClientAppManager::installApp`). Es un canal de control fichero-basado,
hot-reloadable. lumalinux no expone control en runtime; si hiciera falta (p.ej.
"recarga keys ahora"), este es un patrón mínimo y sin dependencias.

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
