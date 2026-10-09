# Los plugins Lua de SLSsteam — la API y los plugins que circulan, leídos desde el código

Leído desde el código el 2026-10-09. Cubre dos cosas:

- La **API de plugins** de SLSsteam, en `AceSLS/SLSsteam` `main@049bbdd` (igual
  que `dev@f3489d6` en todo lo de Lua).
- Los **plugins que circulan**: `download.lua` (de `ciscosweater/enter-the-wired`,
  el que sirve ASSella), `spliced-tickets.lua` (de Ace) y nuestra copia
  `lumadeck-spliced-tickets.lua`.

Sustituye a `slssteam-plugins-analysis.md`: sus mediciones con fecha están en
§5.2, lo que afirmaba y el código ya no sostiene en §5.1, y sus accionables con
el estado de hoy en §4.4. Su anexo sobre cómo localiza funciones cada proyecto
no trataba de los plugins y está ahora en `design/locator-anchors.md`. El resto
de SLSsteam está en `slssteam.md`; quién despliega estos plugins, en
`assella.md`; nuestra pila, en `nosotros.md`.

---

## §0 Ficha

| | |
|---|---|
| **Qué es** | Un **sistema de plugins** dentro de SLSsteam: LuaJIT embebido + LuaBridge, que ejecuta cada `~/.config/SLSsteam/plugins/*.lua` en un único estado Lua dentro del proceso `steam`, con acceso a memoria (`memhlp`), resolución por RTTI (`VFTableInfo_t`), hooks inline (`LuaHook` + `place_lua_hook`), un mutex compartido (`LuaMutex`), `curl`, log, lectura y escritura de la config y parte del SDK de Steam (`lua.cpp:354-519`). Apagado por defecto (`Plugins: no`, `res/config.yaml:127`). Más los **plugins** que circulan, que con FFI pueden hacer cualquier cosa que haga SLSsteam. |
| **Repos y ficheros leídos** | `AceSLS/SLSsteam` `main@049bbdd` (2026-10-06): `src/lua.{hpp,cpp}`, `hooks.{hpp,cpp}` (`LuaHook`), `memhlp.cpp` (`fixPICThunkCall`), `curl.cpp`, `utils.cpp` (`exec`), `api.cpp`, `main.cpp`, `docs/Lua/`. La API de Lua no cambia desde `3d45b14` (2026-09-03, primera release con plugins). `ciscosweater/enter-the-wired` `main@3cba346` (2026-09-04) y su release `latest` (`plugins-deps.zip`: `download.lua` 9.166 B, `spliced-tickets.lua` 2.187 B, ficheros del 2026-09-04). El bucket R2 de ASSella (manifest del 2026-10-05). LumaDeck `backend/deps/SplicedTickets/lumadeck-spliced-tickets.lua` e `installer.py`. |
| **Los plugins** | **`download.lua`** sha256 `8e822aec…`: el mismo fichero en la release de enter-the-wired, en R2 desde el 10-05 y en `niwia/ASSella@6677a05`. Engancha `GetPackage`, `CConfigStore::GetBinary` y el sitio de llamada de `GetManifestRequestCode`; inyecta `AdditionalDepots` en el paquete 0 y sirve claves de `DecryptionKeys` y códigos de `gmrc.wudrm.com`. **`spliced-tickets.lua`** sha256 `62f377e3…`, de Ace: engancha `CUser::GetAppOwnershipTicketExtendedData` y, si el ticket sale vacío, empalma el appid en el ticket de la app 7. **`lumadeck-spliced-tickets.lua`**: el de Ace con 19 líneas de cabecera, mismo sha256 sin ella. |
| **Quién** | La API: Ace (AceSLS). `download.lua`: lo publica **ciscosweater** (autor de ACCELA y de enter-the-wired) con su instalador de plugins y la app "psyche" (`install-plugins`, `2526311`, 2026-09-03). ASSella lo redistribuye y, en `canary`, lo modificó entre el 26 y el 30-sep (§5.3). `spliced-tickets.lua`: Ace, Discord de SLSsteam, 2026-08-30. |
| **Relación con los demás programas** | **ASSella** (`canary`) despliega los dos plugins desde R2 y escribe `AdditionalDepots`/`DecryptionKeys` (`assella.md` §2.1, §2.4). **enter-the-wired** los instala con Headcrab y pone `Plugins: yes`. **LumaDeck** instala solo spliced tickets, con nombre propio, y pone `Plugins: yes` (`installer.py:324-390`). **slsteam-moon** no tiene plugins (índices fijos, sin Lua; `slsteam-moon.md` §0); SLSDeck no despliega ninguno. **lumalinux** engancha dos de las tres funciones de `download.lua` (§4.1). |

**Lo que cambia respecto al doc anterior**, en una línea cada uno:

- **La autoría de `download.lua` deja de estar abierta**: lo publica `ciscosweater/enter-the-wired` desde el 2026-09-03/04 en su release, el mismo fichero que sirve hoy ASSella. Dentro no hay créditos (§5.1).
- **ASSella lo despliega y lo deja puesto**. LumaDeck pone `Plugins: yes` e instala spliced tickets, lo contrario de la regla "ni escribir plugins ni activarlos" del doc anterior (decisión del 29-sep).
- **El caso peligroso con lumalinux es desplegar `download.lua` con Steam abierto**: el trampolín copia nuestro `jmp` sin reubicarlo. Con el plugin ya en la carpeta al arrancar Steam, nosotros llegamos después y encadenamos bien. En release, borrar el plugin **no** quita sus hooks (§4.1-1).
- **Los hooks críticos que LumaDeck mira son DepotKey y PackageZeroFinder** (`paths.py:738`). GMRC ya no cuenta, y DepotKey resuelve por nombre aunque su prólogo esté parcheado (K hecho, §4.4).
- **La API no ha cambiado en cinco semanas**: el "sin estabilidad demostrada" del doc anterior se queda en "sin versionado".

---

## §1 Mapa del código

| Fichero | Líneas | Qué hace | F |
|---|---|---|---|
| SLSsteam `src/lua.hpp` | 83 | callbacks (`configLoaded`, `configLoading`, `initialized`, `luaReload`, `Network::recvPkt`, `Network::sendPkt`), `stateMutex` (un `recursive_mutex`), `fireCallback` con el mutex cogido | 11 |
| SLSsteam `src/lua.cpp` | 579 | `place_lua_hook` (export C, índice sin dueño, `:27-38`); `LuaConfig` (lectura y escritura de cualquier clave, `:45-96`); `curl.downloadString` (`:98-116`); `LuaMutex` (bloquea al crear, desbloquea al destruir, `:161-185`); `Lua::init` (`:278-352`); bindings (`:354-519`); `fixPerms` (`:521-539`); recarga en caliente (`:541-554`); `runLua` con la puerta `Plugins` (`:556-573`) | 1, 11 |
| SLSsteam `src/hooks.cpp` | — | `LuaHook`: índice = primer entero libre (`:179-197`), `place` = `LM_HookCode` + `fixPICThunkCall` (`:205-222`), `remove` = `LM_UnhookCode` (`:224-235`), el destructor llama a `remove` (`:199-203`) | 1 |
| SLSsteam `src/memhlp.cpp` | — | `fixPICThunkCall`: solo reescribe un `call` al thunk PIC dentro de los 5 primeros bytes del trampolín (`:214-306`); un `jmp` relativo copiado queda tal cual | 1 |
| SLSsteam `src/curl.cpp`, `utils.cpp` | 48, — | `fork` + `execve` de `/usr/bin/curl --silent --connect-timeout N`, sin `--max-time`; lectura bloqueante del pipe y `waitpid` (`utils.cpp:48-122`) | 4 |
| SLSsteam `src/api.cpp` | — | `reloadlua` por `/tmp/SLSsteam.API` (`:143-146`) | 11 |
| SLSsteam `src/main.cpp` | — | solo en el proceso `steam` (`:103`); `Hooks::init` → `Lua::init(true)` → `SLSAPI::init` (`:212-219`) | 1 |
| SLSsteam `docs/Lua/README.md`, `example.lua` | — | la documentación: recarga en caliente "highly advised against", `Plat_Alloc` recomendado, el `mutex:unlock()` a mano en el ejemplo (`example.lua:40-48`) | 11 |
| `download.lua` | 328 | §2.2, §2.4 | 2, 4 |
| `spliced-tickets.lua` | 79 | §2.6 | 6 |
| enter-the-wired `install-plugins` | 421 | baja `plugins-deps.zip` y `plugins-app.zip` (psyche) de `releases/download/latest`, sin hash; Headcrab de `main`; restaura `AdditionalDepots`/`DecryptionKeys` tras Headcrab; `Plugins: yes` | 9, 11 |
| LumaDeck `installer.py` | — | `_install_spliced_tickets` (`:324-390`): puerta de versión `>= 20260903114323`, interruptor `~/.config/lumadeck/no_spliced_tickets`, `Plugins: yes` y 1,5 s antes de soltar el fichero | 6, 11 |

---

## §2 Las doce funciones

### 2.1 Engancharse a Steam

**El sistema.**

- **Arranque.** Los plugins corren al final de la carga de SLSsteam, tras
  `Hooks::init` (`main.cpp:212-219`), solo en el proceso `steam`.
- **Orden.** Alfabético: `files` es un `std::set` (`lua.cpp:306-322`).
- **Puerta.** `runLua` no ejecuta nada si `Plugins` está apagado
  (`:560-563`). El resto (estado Lua, directorio, permisos, watcher) arranca
  igual.
- **Recarga en caliente.** La dispara un watcher inotify sobre `plugins/`
  (`IN_CREATE|IN_CLOSE_WRITE|IN_DELETE|IN_MOVED_TO|IN_MOVED_FROM`, `:275`) en su
  propio hilo, o `reloadlua` por el API.
  - Ese hilo coloca los hooks con Steam corriendo: "There is no API in linux
    to freeze single threads, so we just wing it" (`:324`).
  - Antes de recargar dispara `luaReload` (`:548`).
  - **En release llama a `Lua::init()` sin recrear el estado** (`:549-553`):
    los `LuaHook` siguen vivos, los hooks puestos, y cada `.lua` se vuelve a
    ejecutar sobre el mismo estado. La guarda `if X.setup then return end` de
    cada plugin es lo único que evita un segundo hook.
  - En debug se recrea el estado y los destructores quitan los hooks.
  - Borrar un plugin o poner `Plugins: no`, en release, **no desengancha nada**
    hasta reiniciar Steam.
- **`LuaHook::place`** = `LM_HookCode` + `fixPICThunkCall` (`hooks.cpp:205-222`).
  - No comprueba si la función ya está enganchada.
  - El trampolín lleva los 5 bytes del prólogo copiados. `fixPICThunkCall`
    solo arregla un `call` al thunk PIC (`memhlp.cpp:233-236`), así que **un
    `jmp` ajeno copiado salta a una dirección equivocada**. No consta que la
    libmem que enlaza SLSsteam (`lib/liblibmem.a`, actualizada 2026-03-13)
    lo corrija.
- **Aislamiento: ninguno.** Un estado y un espacio global para todos.
  `place_lua_hook` acepta cualquier índice, y los índices son 0, 1, 2…
  (`lua.cpp:27-38`, `hooks.cpp:186-193`), así que un plugin puede redirigir el
  hook de otro.
- **`Lua::init` sale sin soltar `stateMutex`** si no puede crear `plugins/` o
  arreglar sus permisos (`:292-304`). Con el mutex cogido para siempre, todo
  hook que lo pida se queda esperando.

**`download.lua`.**

- **`GetPackage`**: patrón de sitio de llamada `E8 ? ? ? ? 83 C4 ? 83 78 ? ? 0F 84`
  + `getJmpTarget`.
- **`GetManifestRequestCode`**: patrón de sitio de llamada de 30 bytes +
  `getJmpTarget`.
- **`GetBinary`**: índice por nombre en `21IClientConfigStoreMap` aplicado a
  `12CConfigStore`; si `init()` falla, aviso y no carga.

Los dos patrones no comprueban `nil`: si no casan, `getJmpTarget(nil)` y
`LuaHook` con dirección mala, que `place` rechaza (`hooks.cpp:207-211`). Los dos
eran únicos en los tres `steamclient.so` de septiembre (§5.2).

**`spliced-tickets.lua`**: `GetAppOwnershipTicketExtendedData` por
`14IClientUserMap` → `5CUser` (subclase 0).

### 2.2 Propiedad y licencias

**`download.lua`, paquete 0.**

- `hkGetPackage` guarda el primer `PackageInfo*` del paquete 0 que devuelve
  Steam y nunca lo revalida.
- Con `RewriteDepots` (puesto en cada `configLoaded`), `writeDepotIds`:
  1. Hace copia de los ids originales.
  2. Reserva con `Plat_Alloc` la unión con `AdditionalDepots`.
  3. Cambia el puntero de **`depots` (`+0x48`)**.
  4. Libera el viejo con `Plat_Free`.
- **Reactivo**: solo cuando Steam vuelve a pedir el paquete 0.
- Si Steam reconstruye el paquete, el puntero guardado queda colgando.
- `luaReload` llama a `writeDepotIds` con el puntero guardado.

Medido en un Steam real: `+0x38` son appids y `+0x48` depots (§5.2).
lumalinux escribe en `+0x38` (`load_package_hook`/finder): vectores distintos,
no se pisan.

Sin reconcile de licencias: un juego nuevo necesita reiniciar Steam.

### 2.3 DLC

Nada en los plugins.

### 2.4 Claves y manifests

**Claves** (`hkGetBinary`):

- Llama primero al original y, si el nombre casa `%d+\DecryptionKey` y hay
  clave en `DecryptionKeys` del config, escribe los 32 bytes en `pOut`
  **sin mirar `outSize`**.
- Valida longitud y hexadecimal.
- Sin clave en ningún sitio: `log.warn("Missing decryptionkey …")`.

**Códigos de manifest** (`hkGetMRC`):

- Llama al original y solo actúa si **`pOutMRC[0]` sigue a 0**, mejor señal
  que el valor de retorno.
- Entonces pide `http://gmrc.wudrm.com/manifest/<gid>` con
  `curl.downloadString(url, 5)`:
  - un proveedor, HTTP, el UA de `curl` (al que Cloudflare reta, §5.2);
  - hasta 5 intentos por recursión, sin espera;
  - **todo dentro del hook con el mutex de Lua cogido**, y cada intento es un
    `fork` de `/usr/bin/curl` sin `--max-time`.
- Alcanza **cualquier** manifest que Steam no consiga codificar, también de
  juegos comprados.
- Sin caché.
- Lleva comentado `manifest.opensteamtool.com` con UA `OpenSteamTool/1.0`.

**El fallo que tumba Steam.** Si la respuesta llega **vacía** (timeout, conexión
caída), `log.warn("Failed to download manifest request code for " .. manifestId)`
concatena un `uint64_t`. LuaJIT lanza un error en un callback FFI sin `pcall`
y Steam muere sin dejar línea en el log.

Con el reto de Cloudflare el cuerpo no está vacío: va a
`log.error("Invalid MRC response " .. codeStr)`, reintenta y devuelve el
resultado de Steam. El fallo es el mismo que el de la build de ASSella del
29-sep, en otra rama (§5.2).

**Defectos menores**: `ffi.string(buf, 32)` en la URL arrastra NUL que el lado
C corta, y hay un `mrcCStr` asignado sin usar.

### 2.5 Updates de juegos

Nada propio. Un código servido por el plugin vale para cualquier update que
Steam pida.

### 2.6 Fixes y DRM

**`spliced-tickets.lua`**:

- Si el original devuelve tamaño 0 (juego no poseído), pide el ticket de la
  **app 7**, inserta los 4 bytes del appid justo antes de la firma y ajusta
  `pOffAppId`/`pOffSig`.
- SteamStub acepta el ticket y se desempaqueta sin Steamless.
- La firma no cuadra con el contenido: cae en los stubs que verifican firma.
  Ace: 19 de 20 juegos.
- Requiere `SmartTickets` con el bit `0x1` (de serie, `slssteam.md` §2.6).
- El bucle `for i = 0, size` lee un byte más allá del tamaño del ticket.
- Medido en LumaDeck con Joe Danger (§5.2).

### 2.7 Logros, stats y tiempo de juego

Nada.

### 2.8 Cloud saves

Nada.

### 2.9 Añadir y quitar un juego

Los plugins no añaden juegos: leen lo que otro escribe en el config
(`AdditionalDepots`, `DecryptionKeys`). Quién los instala y cómo:

- **enter-the-wired `install-plugins`**: `plugins-deps.zip` y `plugins-app.zip`
  (psyche, ~47 MB, en `~/.local/share/psyche`) de `releases/download/latest`,
  sin hash.
  - Headcrab de `main`.
  - Copia el `config.yaml` antes de Headcrab y fusiona después las claves que
    Headcrab tira (`AdditionalDepots`, `DecryptionKeys`).
  - Pone `Plugins: yes` en las configs nativa y Flatpak.
  - Comprueba rutas `..` en el zip de la app, pero no en el de plugins: lo
    extrae con `unzip -j`, que aplana.
- **ASSella**: desde su R2, con hash del mismo origen y un manifest cacheado que
  no se refresca (`assella.md` §2.1).
- **LumaDeck**: solo spliced tickets, como `lumadeck-spliced-tickets.lua`, `0600`,
  temporal + `rename`, con Steam abierto (recarga en caliente) o al siguiente
  arranque (`installer.py:324-390`).

### 2.10 Credenciales y proveedores

`download.lua` no usa cuentas: wudrm sin clave.

### 2.11 Mantenimiento propio

- **Sin versionado de la API** ni campo de capacidades. Un plugin roto falla al
  cargar (se registra y se sigue) o, si falla dentro de un hook, tumba Steam.
- **Sin contrato de desmontaje en release** (§2.1).
- **Sin diagnóstico en release**: `log.debug` de Lua va a `CLog::__debug`, que
  solo existe en builds debug, así que "Spliced ticket for …" o "Using config
  key for …" no aparecen nunca en un log normal.
- **`LuaMutex` desbloquea dos veces**: el `mutex:unlock()` a mano que enseña el
  ejemplo y el destructor cuando pasa el recolector (`lua.cpp:166-174`;
  `example.lua:40-48`).
  - En el caso benigno, `EPERM`.
  - En el malo, el recolector corre en un hilo que ya tiene el mutex dentro de
    otro hook y le baja el contador: otro hilo entra en el mismo estado Lua.
  - Los tres plugins lo heredan (`mutexReturn`).
- **`fixPerms`** baja a `0700` los `.lua` y el directorio, o apaga los plugins
  si no puede (`:521-539`).
- **Ejecución de código**: cualquier cosa que escriba en `plugins/` como el
  usuario mete código nativo en Steam, en caliente.

### 2.12 Proyecto

- **API**: de AceSLS, AGPL-3.0, en `main` desde la release `20260903114323`;
  sin cambios desde entonces.
- **`download.lua`**: sin licencia ni cabecera; distribuido por enter-the-wired
  (MIT en el repo, `LICENSE`) y por ASSella.
- **`spliced-tickets.lua`**: Ace, Discord.
- **Señal de 2026-09-15**: LuaTools Linux (SWay) planea ofrecer "SLSsteam
  oficial + los plugins" como alternativa a moon.

---

## §3 Superficies externas

### 3.1 Hosts y URLs

| Host | Quién | Para qué | Notas |
|---|---|---|---|
| `gmrc.wudrm.com` | `download.lua` | códigos de manifest | HTTP, UA de `curl`, dentro del hook |
| `github.com/ciscosweater/enter-the-wired/releases/download/latest/` | `install-plugins` | `plugins-deps.zip`, `plugins-app.zip`, `deps.tar.gz` (ACCELA, 249 MB, 2026-06-08) | sin hash |
| `raw.githubusercontent.com/Deadboy666/h3adcr-b/main/headcrab.sh` | `install-plugins` | SLSsteam | `main` |
| `pub-19657b4f385d424b91a909253efdb29c.r2.dev` | ASSella | los mismos dos plugins | `assella.md` §3.1 |

### 3.2 Ficheros

| Ruta | Qué | Quién más la toca |
|---|---|---|
| `~/.config/SLSsteam/plugins/*.lua` | los plugins; `0700` forzado | ASSella (borra otros `download*.lua`, ofrece borrar todos), enter-the-wired, **LumaDeck** (`lumadeck-spliced-tickets.lua`) |
| `~/.config/SLSsteam/config.yaml` | `Plugins`; `AdditionalDepots` y `DecryptionKeys` (claves que solo leen los plugins) | SLSsteam, LumaDeck, ASSella, Headcrab (tira las claves que no conoce) |
| `/tmp/SLSsteam.API` | `reloadlua` | SLSsteam |

### 3.3 Claves del config que usan los plugins

`Plugins` (de SLSsteam), `AdditionalDepots` (lista) y `DecryptionKeys` (mapa
depot → hex), ninguna de las dos últimas en `res/config.yaml`. SLSsteam ignora
las claves que no conoce y los plugins las leen por nombre (`config.hpp`
`getSetting`/`getList`). `ImpersonateBin` la usó una versión de ASSella del
26-sep (§5.3).

---

## §4 Lo que nos afecta

### 4.1 Hallazgos pendientes de decisión

Aparcados el 2026-10-09. Las decisiones de fondo están en §4.4 (P1: analizar,
no defenderse).

1. **`download.lua` y lumalinux en el mismo Steam.** Engancharían las mismas dos
   funciones: `CConfigStore::GetBinary` (DepotKey) y `GetManifestRequestCode`
   (GMRC). Lo que pasa depende de quién llega antes.
   - **Plugin presente al arrancar Steam.** Engancha antes que nosotros
     (`LD_AUDIT` antes que `LD_PRELOAD`, y los plugins al final del arranque de
     SLSsteam). lumalinux ve el `E9` (`lmhook.cpp:138`) y reubica el salto en
     su trampolín (`RelocateChainedJmp`, `:97,152-153`). Corre primero el
     nuestro, luego el del plugin.
     - En `GetBinary` gana `keys.txt`: el hook del plugin no llega a correr
       cuando tenemos clave.
     - En GMRC respondemos nosotros antes que Steam y el plugin solo actúa si
       el código sigue a 0.

     En release, borrar el plugin no lo desengancha (§2.1), así que la cadena
     aguanta hasta reiniciar Steam.
   - **Plugin desplegado con Steam abierto**, por ejemplo desde los Ajustes de
     ASSella o con enter-the-wired. Su `LuaHook::place` copia al trampolín
     nuestro `jmp` sin reubicar (`memhlp.cpp:233-236`), y la primera llamada a
     `GetBinary` (Steam la llama sin parar) salta a basura: **Steam se cae**.
     Tras reiniciar Steam se está en el caso anterior.
   - **Build debug de SLSsteam**: cada recarga recrea el estado. El
     destructor de `LuaHook` restaura el prólogo que guardó, que es el
     original. Se lleva nuestro `jmp` y **lumalinux queda mudo** hasta
     reiniciar.

   Lo medido es la mecánica de libmem en los dos lados (§5.2), no la secuencia
   en un Steam real.

2. **El mutex de Lua compartido con nuestro plugin de tickets.** Mientras el GMRC
   de `download.lua` espera a `curl` (sin `--max-time`) con `LuaMutex` cogido,
   `lumadeck-spliced-tickets.lua` espera para dar un ticket: el juego no arranca
   hasta que wudrm conteste o caiga. A eso se suma el doble `unlock` de
   `LuaMutex` (§2.11), que heredan nuestro plugin y el suyo.

3. **El crash de la respuesta vacía.** Con `download.lua` cargado, una caída de
   wudrm que devuelva cuerpo vacío tumba Steam en el primer código que Steam no
   consiga (§2.4). Steam se cae aunque nuestra cascada GMRC tenga código, salvo
   que respondamos antes. Respondemos antes en los depots de `keys.txt`; en los
   demás, el plugin pide.

4. **`Plugins: yes` lo pone LumaDeck.** Desde el 29-sep, para spliced tickets
   (`installer.py:359`). Eso ejecuta cualquier otro `.lua` que haya en la
   carpeta, sea de quien sea, y la regla F del doc anterior decía lo contrario.

   Un `download.lua` dejado por ASSella o enter-the-wired se carga en el
   siguiente arranque de Steam con nuestra pila debajo, en el caso benigno del
   punto 1. Uno que llegue con Steam abierto cae en el malo.

5. **Dos copias de spliced tickets.** La guarda global `SplicedTickets.setup`
   hace que la segunda copia no enganche, porque comparten estado. Comprobado
   en el fuente. Nombre propio para que ASSella no borre la nuestra; su botón
   "Standard Plugin" sí la borraría (`assella.md` §4.1-7).

### 4.2 Dependencias y qué rompe si cambia

| Si cambia… | Qué nos pasa |
|---|---|
| la API de Lua (`LuaHook`, `VFTableInfo_t`, `place_lua_hook`, `LuaMutex`) | `lumadeck-spliced-tickets.lua` deja de cargar (se ve: no arranca un juego con SteamStub) |
| `LuaHook::place` aprende a reubicar `jmp` | el caso "desplegado con Steam abierto" de §4.1-1 deja de tumbar Steam |
| `CUser::GetAppOwnershipTicketExtendedData` o su vtable | spliced tickets falla en `init()` y no carga |
| wudrm cae o cambia | nada para nosotros salvo §4.1-3 con `download.lua` cargado |
| enter-the-wired cambia su `latest` | lo que reciba quien use su instalador; ASSella copia a mano |

### 4.3 Lo que los plugins tienen y nosotros no (y al revés)

| Función | Plugins | Nosotros | Notas |
|---|---|---|---|
| 1 | índice de vtable por nombre; sitio de llamada + `getJmpTarget` | por nombre en DepotKey (K, hecho); patrón + feed + `.eh_frame` en GMRC | `design/locator-anchors.md` |
| 2 | captura del paquete 0 por `GetPackage`, sin offsets de build | finder activo con offsets (`0xc58`, `0xc6c`, nodo `0x18`) y reconcile | |
| 4 | GMRC: mirar `pOutMRC[0]` en vez del retorno | cascada de cuatro con UA propio, caché, validación en el CDN, solo depots de `keys.txt` | |
| 6 | spliced tickets | lo instalamos como plugin (§4.4 P5) | |
| — | shader skip, reconcile, feed de RVAs, `status.json` | solo nosotros | |

### 4.4 Decisiones ya tomadas (del doc anterior, con fecha) y estado hoy

| # | Decisión | Fecha | Hoy |
|---|---|---|---|
| P1 | Los plugins se tratan como análisis; no se modifica lumalinux ni LumaDeck para defenderse. Archivados D (estado `foreign` en `status.json`), E′ (`foreign` no cuenta como fallo crítico), L (vigencia del hook), H (detección de `Plugins:` y `.lua`), F (no escribir ni activar plugins), B (reportar el doble `unlock`), I (plugin de medida) | 2026-09-07 | vigente salvo F (P5) |
| P2 | A: ¿el cuelgue de `ProcessPendingLicenseUpdates` de OST es latente en lumalinux? | 2026-09-04 | **cerrado tras medir**: no se reproduce ni en frío (§5.2) |
| P3 | K: índice de vtable por nombre en caliente (método de `download.lua`) para DepotKey | 2026-09-07 | **hecho** (`Rtti::ResolveVtableSlotByName`, `rtti.cpp:289`; rescate en `depot_key_hook.cpp`); no aplica a GMRC ni a ShaderDepot (medido) |
| P4 | J, K′, K″, M1-M3: el xref de GMRC falla cerrado (`eh_frame.{hpp,cpp}`), patrones exigiendo coincidencia única, rescates solo si deciden, contraste en CI | 2026-09-07 | **hechos** |
| P5 | Spliced tickets como **plugin** de LumaDeck (no hook inline: sería otro punto de choque) con `Plugins: yes`, puerta de versión e interruptor | 2026-09-29 | **hecho** (LumaDeck `b70ea44`); revierte F |
| P6 | M4: retirar `WalkBackToPrologue` si los logs nunca emiten ".eh_frame_hdr unavailable" | 2026-09-07 | condicional; sigue en `gmrc_xref.cpp:117,146` |
| P7 | R: probar el derivador automático de Reconcile contra una corrida real | 2026-09-07 | abierto |
| P8 | G: refrescar el snapshot embebido de `slssteam_schema.py` con `Plugins`, `SmartTickets`, `LaunchOptions` | 2026-09-07 | **abierto**: la referencia viva se baja de `res/config.yaml` de upstream y las trae, pero el snapshot de reserva (`_BUNDLED_YAML`, `slssteam_schema.py:58`) sigue sin ninguna de las tres; solo importa sin red |
| P9 | C: `Plat_Realloc`/`Plat_Free` de `libtier0_s.so` en vez de `realloc` de libc | 2026-09-04 | abierto (sin uso de `Plat_*` en `src/`) |
| P10 | 3 y 3ª: ancla de cadena en runtime para los demás hooks; sitio de llamada como tercer resolvedor (exige convergencia en el auditor) | 2026-09-07 | abierto (`design/locator-anchors.md`) |
| P11 | Portar lumalinux a plugin | 2026-09-03 | **no**: sin versionado ni contrato de desmontaje, sin `steamwebhelper`, sin aislamiento; reabrir si la API trae versionado |
| P12 | §12 de ASSella: sin acción por el choque con `download.lua`; opciones (1)-(5) | 2026-09-29 | vigente (`assella.md` §4.4 A5), con la corrección de release frente a debug de §4.1-1 |

---

## §5 Historial

### 5.1 Lo que se creía y ya no es así

- **"La autoría de `download.lua` queda como hipótesis".** Lo publica
  enter-the-wired: `install-plugins` (2026-09-03) baja `plugins-deps.zip`, y el
  `download.lua` de esa release es el mismo fichero que sirven R2 y
  `niwia/ASSella@6677a05` ("restore original upstream"). El perfil de §1 del doc
  anterior (RE previa, port a la API nueva) encaja con el autor de ACCELA.
- **"Orden inverso: al borrar ASSella sus plugins, `Lua::init` cierra el estado
  y lumalinux queda mudo"** (análisis de ASSella del 29-sep). Solo en debug; en
  release el estado no se cierra y los hooks siguen (§2.1).
- **"Nuestra cascada GMRC: opensteamtool → wudrm → steam.run con UA
  `OpenSteamTool/1.0`".** Hoy 20770407 → manifestdex → wudrm → steam.run con UA
  `lumalinux/<versión>` (`gmrc_store.hpp:57-66`).
- **"Crítico = DepotKey + GMRC" en LumaDeck.** Hoy `DepotKey` y
  `PackageZeroFinder` (`paths.py:738`).
- **"Una API sin estabilidad demostrada: quitó y devolvió sus exports dos
  veces en nueve días".** Cierto en agosto; sin cambios desde el 2026-09-03.
- **"LumaDeck no debe escribir ni activar plugins".** Lo hace desde el 29-sep
  (P5).

**Lo que sigue sin medir**:

- §4.1-1 con lumalinux y `download.lua` en un Steam real, en los dos órdenes.
- Si la libmem que enlaza SLSsteam reubica un `jmp` copiado.
- Cuántos stubs verifican la firma del ticket empalmado.

### 5.2 Pruebas y mediciones con fecha

#### Función 1 — Engancharse a Steam

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-07 | lumalinux con SLSsteam `20260714` | BuildDep con SLSsteam enganchando la misma función | SLSsteam llega antes (`LD_AUDIT`) y parchea el prólogo; nuestro patrón no casa y BuildDep sale `failed` | origen de BuildDep apagado por defecto (`main.cpp`) |
| 2026-09-04 | libmem 5.1.5 (`src/common/arch/x86.c`) | qué escribe un detour en i386 | siempre `E9` + rel32, 5 bytes; copia los bytes robados tal cual al trampolín | por eso `RelocateChainedJmp` |
| 2026-09-07 | `steamclient.so` `bc54101b29` (`tools/experiment_ifacemap_slot.py`) | índice de `GetBinary` por nombre | `21IClientConfigStoreMap` → ranura 6 → `12CConfigStore` `0x011a4500` = `hooks.DepotKey`; la ranura 7 es otra función | "GetBinary" es prefijo de "GetBinaryWatermarked" y hay 4 copias en `.rodata` |
| 2026-09-07 | ídem | "GetManifestRequestCode" y "GetShaderCacheDepot" como métodos de interfaz | la primera aparece una vez y ninguna de las 52 `*Map` la referencia; la segunda no existe | K solo aplica a DepotKey |
| 2026-09-29 | SteamOS + SLSsteam de serie, sin lumalinux | 15 recargas de `config.yaml` con `download.lua` cargado; 5 ciclos de quitar y reponer el plugin | Steam vivo en todos | `log.debug` no existe en release |
| 2026-09-30 | AppImage de ASSella `3.0.0testing290926005` + LuaJIT 2.1 | `"…" .. manifestId` con `uint64_t` | `attempt to concatenate 'string' and 'uint64_t'`; dentro de un callback FFI tumba Steam | el original tiene la misma concatenación en la rama de respuesta vacía |
| 2026-10-01 | CI de lumalinux (`probe-steam`, runs 13-15) | los dos patrones de `download.lua` en `.text` | únicos en Deck estable `bc54101b`, escritorio estable `237495b4` y beta `a3661f5b` | |

#### Función 2 — Propiedad y licencias

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-04 | Steam `1788400362` + SLSsteam, sin lumalinux (`tools/experiment_pkg_vectors.lua`) | qué hay en `+0x38` y `+0x48` | appids y depots: 1081 → `[22300]` / `[22301…22306]`; paquete 0: 196 appids (`alloc 202`) y 878 depots (`alloc 1021`) | las etiquetas `AppIdVec`/`DepotIdVec` son correctas |
| 2026-09-04 | lumalinux v0.18.2, depot inexistente en `keys.txt` | cuelgue de `ProcessPendingLicenseUpdates` (el de OST) con caché caliente, fría y doble disparo | sin cuelgue en los tres; `AppIdVec` igual en frío y en caliente | sintético; `Reconcile: broadcast` no prueba que el procesado terminase |

#### Función 4 — Claves y manifests

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-10-05 | misma IP, `gmrc.wudrm.com/manifest/<gid>` | UA de `curl` frente a navegador | 403 `Cf-Mitigated: challenge` frente a 200 | lo que pide `download.lua` |
| 2026-10-09 | release `latest` de enter-the-wired y bucket R2 de ASSella | qué se sirve | los dos con `download.lua` `8e822aec…` y `spliced-tickets.lua` `62f377e3…` | R2 volvió a este el 10-05 desde el `84d6c23f` (con `pcall`) del 30-sep |

#### Función 6 — Fixes y DRM

| Fecha | Dónde | Qué se probó | Resultado | Detalle |
|---|---|---|---|---|
| 2026-09-28/29 | SteamOS + SLSsteam, Joe Danger 229890 (SteamStub) | spliced tickets | sin plugin `Application load error 6:0000065432`; con plugin arranca; `Plugins: no` vuelve el error | la línea de log del plugin es `debug`, no sale en release |
| 2026-09-29 | `tools/experiment_ownership_ticket.py` | port nativo | `IClientUserMap` ranura 105; `5CUser` con cinco vtables; SLSsteam ya engancha la función por VFT | port aparcado: carrera con el VFT y otro punto de choque inline |

### 5.3 Cronología

- **2026-08-24.** `0420645` primera API de Lua en `dev`.
- **2026-08-25 → 31.** `getNode`/`asPairList`, callbacks, `LuaMutex` (`76d9c7c`),
  `place_lua_hook` como export de C (`f4213e5`), `curl.downloadString` con
  cabeceras (`1721312`), `Plat_Alloc` (`c2d3c9b`).
- **2026-08-30.** Ace publica `spliced-tickets.lua` en su Discord.
- **2026-09-03.** Release `20260903114323` con la API y `fixPerms`
  (`3d45b14`). Enter-the-wired añade `install-plugins` (`2526311`). Llega
  `download.lua`.
- **2026-09-04.** Release `latest` de enter-the-wired con `plugins-deps.zip`;
  `3cba346` restaura `AdditionalDepots`/`DecryptionKeys` tras Headcrab.
- **2026-09-07.** Decisión P1; K, J, M1-M3 hechos en lumalinux.
- **2026-09-19.** ASSella mete los dos plugins en su AppImage (`78dbda7`).
- **2026-09-26 → 30.** ASSella modifica `download.lua` (reintentos, `curl-impersonate`,
  `io.popen`), lo revierte (`6677a05`), las builds del 29 tumban Steam, y el 30
  lo pasa a R2 con `pcall` (`84d6c23f`).
- **2026-09-29.** LumaDeck instala spliced tickets como plugin (`b70ea44`).
- **2026-10-05.** R2 vuelve al `download.lua` de enter-the-wired.
- **2026-10-06.** niwia reparte por Discord `download-1.4.0-spacetest.lua`
  (dos proveedores, timeouts en ms tomados como segundos; `assella.md` §5.2);
  no está en R2.
