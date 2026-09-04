# Análisis — La API de plugins de SLSsteam y el plugin `download.lua`

*Fecha de investigación: 2026-09-03. Base: [`AceSLS/SLSsteam`](https://github.com/AceSLS/SLSsteam)
@ `main` `71021ad`, tag `20260903114323` (publicado **hoy**), y el fichero
`download.lua` recibido por vía informal el mismo día.
Continuación directa de [`slssteam-analysis.md`](slssteam-analysis.md) §7.9 —
en particular de §7.9.5 y del cierre §7.9.13, que dejó anotado el evento a
vigilar: **`dev` mergeado a `main` y tagueado**. Ese evento ha ocurrido.*

Este documento no analiza SLSsteam otra vez; para eso está
`slssteam-analysis.md`. Analiza **una cosa nueva y sus consecuencias**: que
SLSsteam ha dejado de ser una librería con hooks y se ha convertido en una
plataforma con API de plugins, y que ya circula un plugin que hace, desde Lua,
tres de las cosas que lumalinux hace desde C++.

**Estado: completo.** Las seis secciones están cerradas; los accionables
consolidados están en §6.6.

El análisis va **por capas**, y cada una condiciona a la siguiente:

| Capa | Qué responde | Estado |
|---|---|---|
| **0** | Datación y autoría de `download.lua` | **§1 — cerrada** |
| **1** | La API de plugins como mecanismo | **§2 — cerrada** |
| **2** | `download.lua` como artefacto técnico | **§3 — cerrada** |
| **3** | La frontera de coexistencia (§5 de `slssteam-analysis` deja de valer) | **§4 — cerrada** |
| **4** | Consecuencias para LumaDeck | **§5 — cerrada** |
| **5** | Escenarios estratégicos y señales de vigilancia | **§6 — cerrada** |
| **+** | **Anexo técnico**: motores de localización comparados | **§7** |

Se mantienen los cubos del doc hermano — **[YA]**, **[PRESTABLE]**,
**[FRONTERA]** — y se añade uno:

- **[RIESGO]** — situación en la que la coexistencia deja de ser inocua y
  alguien (usuario o proceso) se rompe.

---

## 1. Capa 0 — Datación y autoría de `download.lua`

### 1.1 Por qué esta capa va primera

No es curiosidad. De la respuesta dependen las Capas 4 y 5 enteras: si el
plugin es de un tercero, es *un plugin* y se trata como tal — un artefacto de
la comunidad con el que hay que coexistir. Si es del propio dev de SLSsteam, es
**el roadmap de SLSsteam adelantado**, y entonces lo que hay que decidir no es
cómo convivir con un `.lua` sino qué hace lumalinux cuando su razón de ser pase
a estar cubierta upstream.

### 1.2 El check que parecía decisivo, y por qué no lo es

Hipótesis de partida: si el plugin es del dev, sus claves de configuración
(`AdditionalDepots`, `DecryptionKeys`) estarán en el core, porque las habría
cableado mientras lo desarrollaba en privado.

**Resultado: negativo, y rotundo.** Cero coincidencias en todo el repositorio:

```
$ grep -rn "AdditionalDepots\|DecryptionKeys" --include=*.cpp --include=*.hpp \
      --include=*.lua --include=*.md --include=*.yaml .
(sin salida)
```

No están en `config_default.hpp`, ni en el código, ni en los docs.

**Pero el check no discrimina**, y la razón está en `src/config.hpp`. Tanto
`getSetting<T>` (:110) como `getList<T>` (:148) resuelven una clave ausente
devolviendo el valor por defecto y marcando `ELoadError::MissingKey`; las claves
**desconocidas** simplemente se ignoran, porque la config se lee por nombre
contra el `rootNode`, no contra un esquema cerrado. Es decir: **la API permite
deliberadamente que un plugin defina sus propias claves**. El dev no habría
necesitado tocar el core aunque el plugin fuese suyo.

Un check bien planteado que sale negativo y además resulta no ser informativo.
Queda anotado para que nadie lo repita.

### 1.3 La datación sí es concluyente

Aquí está el hallazgo real de esta capa. `download.lua` usa piezas de la API
que se pueden fechar commit a commit:

| Pieza usada por `download.lua` | Commit que la introduce | Fecha |
|---|---|---|
| `ffi.C.place_lua_hook` (**export de C**) | `f4213e5` | 2026-08-31 |
| `curl.downloadString(url, headers, timeout)` (overload fusionado) | `1721312` | 2026-08-31 |
| `ffi.load("tier0_s")` + `Plat_Alloc`/`Plat_Free` (recomendado en el README) | `c2d3c9b` | 2026-08-31 |
| `LuaMutex` | `76d9c7c` | 2026-08-28 |
| `SLS.config:getNode` / `YAMLNode:asPairList` | `81252c8` | 2026-08-25 |
| callbacks `configLoaded` / `luaReload` | `ac0b16b` / `8b9ccf6` | 2026-08-25 |

Las tres primeras filas fijan el suelo en **2026-08-31**. Dos son inequívocas:

- `place_lua_hook` sólo es invocable desde el FFI de LuaJIT **desde que es
  export de C**; antes era una función puenteada por LuaBridge y no servía para
  esto (es justo lo narrado en `slssteam-analysis.md` §7.9.12).
- La línea comentada del propio fichero —
  `-- local codeStr = tostring(curl.downloadString(url, headers, 5))` — usa el
  overload de **tres** argumentos bajo el nombre `downloadString`. Antes de
  `1721312` esa función se llamaba `downloadStringWithHeaders`. El comentario es
  posterior al rename.

Y ahora el techo. La API de plugins nace el **2026-08-24** (`0420645`,
*"feat(lua): Add rudimentary API"*) y se desarrolla entera en `dev`. Los tags
publicados van:

```
20260820085507   2026-08-20   <- último release ANTES de la API de plugins
20260903114323   2026-09-03   <- hoy: el primero que la contiene
```

**Entre el 24 de agosto y hoy no hubo ninguna release con API de plugins.**

De donde se sigue la conclusión de la capa:

> `download.lua` fue escrito en una ventana de **tres días como máximo
> (2026-08-31 → 2026-09-03)**, contra una API que sólo existía en la rama `dev`
> **sin publicar**. Su autor no estaba usando SLSsteam: lo estaba **compilando
> desde `dev`**, y siguiendo su evolución día a día.

Eso no es un usuario. Es alguien dentro del ciclo de desarrollo, en la ventana
exacta en la que la API era más inestable — la misma en la que el dev quitó y
devolvió sus propios exports dos veces (§7.9.11, §7.9.12).

### 1.4 La evidencia estilística es más débil de lo que parece — corrección

En la lectura inicial dimos peso al parecido con `example.lua`. **Hay que
rebajarlo casi a cero**, y conviene dejarlo escrito para no reincidir.

Los idiomas compartidos son muchos y muy específicos:

| Idioma | `example.lua` | `download.lua` |
|---|---|---|
| Guarda de inclusión | `Example = Example or { setup = false, ... }` | `Downloader = Downloader or { setup = false, ... }` |
| Nombrado de hooks | `postCallbackHook` / `postCallbackTramp` | `getPackageHook` / `getPackageTramp`, … |
| Resolución | `getJmpTarget(patternScan("E8 ? ? ? ? 8B 75 ? 89 D8", mod))` | `getJmpTarget(patternScan("E8 ? ? ? ? 83 C4 ? 83 78 ? ? 0F 84", mod))` |
| Colocación | `ffi.cast(T, ffi.C.place_lua_hook(hook.index, hookFn))` | idéntico |
| VFT en dos pasos | `("14IClientUserMap","BLoggedOn")` → `("5CUser", …, map.index, 0)` | `("21IClientConfigStoreMap","GetBinary")` → `("12CConfigStore", …, map.index)` |
| Cierre | `log.notify("example.lua loaded!")` | `log.notify("download.lua loaded!")` |

Parece abrumador. **No lo es**: `example.lua` es *la documentación oficial*, y
todo lo anterior es exactamente lo que produce cualquier persona competente
copiando el ejemplo. Como prueba de autoría vale poco. Lo único que demuestra es
que el autor trabajó a partir del `example.lua` **vigente en esos días**, lo
cual refuerza la datación de §1.3 y nada más.

### 1.5 La asimetría que sí es informativa

Separando lo que el fichero sabe en dos ejes, aparece algo que la comparación
estilística tapaba:

- **Uso de la API de Lua: derivativo.** Todo lo que emplea está documentado en
  `docs/Lua/README.md` y casi todo aparece en `example.lua`. No usa una sola
  función no publicada.
- **Conocimiento de `steamclient.so`: original y profundo, y no está en ninguna
  documentación.** El layout `PackageInfo_t` con su `__pad0x0[0x44]`, la forma
  del `CUtlVector` (`mem`/`alloc`/`grow`/`size`), los tres patrones de bytes, la
  ruta KeyValues `%d+\DecryptionKey`, y saber que existe un servicio de
  *manifest request codes* al que preguntar.

Un fichero que aprende la API por el ejemplo pero trae la ingeniería inversa ya
hecha **no es un descubrimiento: es un port**. Alguien que ya tenía esto
funcionando en otra forma lo trasladó a la API nueva en cuanto la API existió.

### 1.6 Lo que apoya la hipótesis sin depender del estilo

Dos observaciones sobre el core, independientes del `.lua`:

**(a) SLSsteam ya está sentado en la ruta de depots.** Hookea
`CUserAppManager::BuildDepotDependency` (`hooks.cpp:756`) y en
`Apps::buildDepotDependency` (`feats/apps.cpp:58`) hace dos cosas sobre el vector
de depots que Steam acaba de construir: elimina los de `DepotBlacklist` y
sobreescribe el `manifestId` de los de `ManifestIds`. Es decir, **parchea**
entradas existentes — exactamente el modo `PATCH-only` que lumalinux documenta
para su propio BuildDep. Tiene el pie dentro de la ruta de instalación; lo que no
tiene son las tres piezas que faltan (claves, MRC, inyección de depots en el
paquete 0), que es *justo* lo que añade `download.lua`.

**(b) El comentario de `DisableUpdates` en `config_default.hpp`:**

> *"Only works for unowned games, since those do not get any depots from
> `CUserAppManager::BuildDepotDependency`."*

Eso es alguien que ha mapeado con precisión **por qué** un juego no poseído no
recibe depots. Es el conocimiento exacto que hace falta para escribir el plugin,
y está en el core, publicado, desde antes.

En el otro platillo, un negativo llamativo: `README.md` no menciona *download*,
*depot* ni *manifest* ni una sola vez. Públicamente, SLSsteam no dice hacer nada
de esto.

### 1.7 Conclusión de la capa — y el freno

Lo que la forense sostiene, con evidencia:

1. `download.lua` se escribió entre el **2026-08-31 y el 2026-09-03**, contra
   una rama sin publicar.
2. Su autor compilaba SLSsteam desde `dev` y seguía su churn de API a diario.
3. Trae ingeniería inversa de `steamclient.so` **previa** al plugin: es un port
   de algo que ya funcionaba, no un desarrollo desde cero.
4. El core de SLSsteam ya está posicionado sobre la ruta de depots y su autor ha
   demostrado por escrito conocer el mecanismo que falta.

Todo eso encaja con que el autor sea el dev de SLSsteam. **Pero no lo demuestra**,
y aquí conviene ser honesto con el propio razonamiento: el perfil que dibuja la
evidencia es *"experto en RE de `steamclient.so`, siguiendo `dev` a diario, con un
descargador ya funcionando en la mano"*. Ese conjunto no tiene un solo elemento.
Contiene a la gente de LumaCore y OpenSteamTool, al entorno de `slsteam-moon` y
SLSDeck — y **contiene al autor de lumalinux**, que cumple los cuatro puntos sin
excepción. Un perfil que nos incluye a nosotros no señala a nadie en particular.

**Estado: la datación queda cerrada y es sólida; la autoría queda como hipótesis
razonable y no verificada.** Y para lo que viene detrás, la buena noticia es que
**da casi igual**: lo que mueve las Capas 3, 4 y 5 no es quién firmó el fichero,
sino que la capacidad está publicada, es reproducible por cualquiera con la
release de hoy, y toca los mismos tres seams que lumalinux. La autoría cambia el
*pronóstico* (cuánto tarda esto en ser oficial), no el *trabajo*.

### 1.8 Nota lateral — el endurecimiento de última hora [RIESGO, mitigado upstream]

Fuera del eje de autoría pero conviene que quede fechado. El commit `3d45b14`,
de **hoy mismo**, horas antes del tag, añade `Lua::fixPerms`: sobre el directorio
de plugins y sobre **cada `.lua`**, si hay permisos de grupo u otros los reduce a
`owner_all`; si no puede, **desactiva los plugins para toda la sesión**.

Es alguien que, justo antes de publicar, se paró a pensar en el directorio de
plugins como superficie de ataque — y con razón: cualquiera que pueda escribir
ahí obtiene ejecución de código nativo dentro del proceso de Steam, vía FFI, sin
sandbox y **sin reiniciar** (el `inotify` los carga en caliente). Se desarrolla
en la Capa 1; aquí queda la fecha.

---

## 2. Capa 1 — La API de plugins como mecanismo

Esta capa no mira el `.lua`. Mira **qué ha construido SLSsteam**: qué expone, con
qué garantías, cuándo corre y qué pasa cuando algo va mal. Es la que decide si la
plataforma es un sitio sensato al que mudarse o un sitio del que conviene estar
lejos.

### 2.1 Qué se publicó, y el gesto que delata la apuesta

LuaJIT embebido + LuaBridge, con los plugins leídos de
`~/.config/SLSsteam/plugins/`. La superficie publicada (`initLuaState`,
`lua.cpp:354`) es amplia y está bien elegida:

| Espacio | Contenido |
|---|---|
| `memhlp` | `getModule`, `patternScan`, `getJmpTarget`, `findPrologue`, `hexdump` |
| `VFTableInfo_t` | resolución por RTTI, con `index` y `subClassIndex` |
| `LuaHook` | `place`/`remove` + `place_lua_hook` como export de C |
| `LuaMutex` | acceso al `recursive_mutex` del `lua_State` |
| `curl` | `downloadString` (con y sin cabeceras) |
| `log` | los ocho niveles + `notify*` |
| `YAMLNode` / `CConfig` | lectura y **escritura** de la config, incl. `setAdditionalApps` |
| SDK | `CSteamEngine`, `CUser` (`isSubscribed`, `postCallback`), `IClientApps`, `IClientUser`, `IClientUtils`, `CNetPacket` |
| Callbacks | `configLoading`, `configLoaded`, `initialized`, `luaReload`, `Network::recvPkt`, `Network::sendPkt` |

Con eso, más el FFI de LuaJIT, un plugin puede hacer literalmente cualquier cosa
que pueda hacer SLSsteam. No es una API de extensión acotada: es acceso completo
al proceso desde un script.

**El detalle que mide el compromiso** está en `main.cpp:220`, y es una línea
comentada:

```cpp
Lua::init(true);
SLSAPI::init();
//Disabled, since the Lua API can use it to find VFTables
//Decompiler::cleanUp();
```

El descompilador — la maquinaria que SLSsteam montó para resolver vtables sin
patrones frágiles (§7.5 del doc hermano) — **ya no se libera nunca**, y se paga la
memoria durante toda la sesión, sólo para que los plugins puedan usarlo. Eso no
es un experimento: es alguien reordenando el core para servir a la plataforma.

### 2.2 Orden de arranque — los plugins corren los últimos

De `setup()` en `main.cpp`, en orden:

1. **Filtro de proceso**: `strcmp(proc.name, "steam") != 0` → se descarga.
   SLSsteam vive **sólo** en el proceso `steam`. *(lumalinux admite además
   `steamwebhelper`; la divergencia importa en la Capa 3, no aquí.)*
2. Log, manejador de señales.
3. **SafeMode**: si el hash de `steamclient.so` no está en la lista y `SafeMode: yes`
   → aborta.
4. `Steam::init()` → `VFTIndexes::init()` → `Patterns::init()` → `Hooks::init()`.
5. **`Lua::init(true)`** ← aquí corren los plugins.
6. `SLSAPI::init()`.

De donde salen dos garantías firmes y una consecuencia:

- **Los plugins corren después de que SLSsteam haya colocado todos sus hooks.**
  Sobre las funciones que SLSsteam ya engancha, un plugin siempre engancha
  *encima*.
- **Entre plugins el orden es alfabético y determinista** — `files` es un
  `std::set<std::filesystem::path>` precisamente para eso (`0453823`, *"Run files
  in alphabetical order"*). Determinista, sí; pero también **manipulable**: un
  fichero llamado `aaa.lua` corre antes que `download.lua`, siempre.
- Si algo de los pasos 3–4 falla, `setup()` sale antes y **los plugins no corren
  en absoluto**. Fallo cerrado, correcto.

### 2.3 El ciclo de vida del estado Lua — y por qué la guarda de inclusión es estructural

`Lua::init(fullReload)` sólo recrea el `lua_State` si `fullReload` es cierto. Y
en `onFileChange` (`lua.cpp:548`):

```cpp
Lua::fireCallback(Lua::Callbacks::SLSsteam_LuaReload);
#ifdef DEBUG
    Lua::init(true);   // recrea el estado
#else
    Lua::init();       // NO lo recrea
#endif
```

Las dos ramas se comportan de forma **cualitativamente distinta**:

- **Debug**: el estado se recrea, los objetos `LuaHook` se destruyen, y su
  destructor llama a `remove()` → `LM_UnhookCode`. Eso es lo que la
  documentación llama *"LuaHooks get cleaned up automatically"*.
- **Release**: el estado **no** se recrea. Los globales sobreviven, los `LuaHook`
  siguen vivos y **los hooks siguen puestos**. `luaL_dofile` vuelve a ejecutar el
  fichero sobre el mismo estado.

Consecuencia que conviene tener muy clara: en release, **la guarda
`if X.setup then return end` de cada plugin no es una cortesía, es lo único que
impide que cada evento de fichero vuelva a enganchar la misma función**. Un
plugin sin esa guarda se auto-destruye a la segunda recarga. Que la guarda esté
en el `example.lua` oficial es, entonces, mucho más importante de lo que parece:
es documentación de una restricción real, disfrazada de estilo.

Tras cargar, `Lua::init` hace `g_config.loadSettings(false, true)` — una recarga
completa de la config, que vuelve a disparar `configLoaded`. Es deliberado y está
documentado: *"to allow changing it & remove any changes a previous Lua could have
done"*. La config es el canal de comunicación entre el core y los plugins, y se
reconstruye en cada ciclo.

### 2.4 Los hooks de Lua usan exactamente nuestra primitiva [YA]

`LuaHook::place()` (`hooks.cpp:205`):

```cpp
size = LM_HookCode(fn, hookFn, &tramp);
if (!size) return nullptr;
MemHlp::fixPICThunkCall(name.c_str(), fn, tramp);
```

`LM_HookCode` + reparación del thunk PIC. Es **la misma pareja de operaciones**
que `LmHook::Install` + `FixPicThunk` en lumalinux — y no por casualidad: nuestro
`FixPicThunk` es un port declarado del suyo (§1.3 del doc hermano). Un plugin y
lumalinux parchean el binario con la misma técnica y con la misma librería.

Guardas: `place()` rechaza `LM_ADDRESS_BAD` (`d648297`) y `remove()` no hace nada
si `size == 0`. **Lo que no comprueba ninguno de los dos es si la función destino
ya está enganchada.** Ese hueco es el que se paga en la Capa 3.

### 2.5 El modelo de hilos — el mayor pie de banco de la API

`stateMutex` es un `std::recursive_mutex`. SLSsteam lo bloquea él mismo en dos
sitios: alrededor de `Lua::init` y dentro de `fireCallback`. Es decir, **los
callbacks están protegidos por la casa**.

Los hooks **no**. Un hook de Lua colocado sobre una función de Steam se ejecuta en
**el hilo de Steam que llame a esa función**, que puede ser cualquiera y pueden ser
varios a la vez. Por eso todo cuerpo de hook tiene que abrir con `LuaMutex()` a
mano — y por eso `example.lua` lo explica en un comentario de cuatro líneas.

**Nada lo obliga.** Un plugin que se olvide del mutex mete varios hilos en el
mismo `lua_State`, que no es reentrante. El síntoma es corrupción, no un error
limpio. Es el fallo más fácil de cometer y el más difícil de diagnosticar de toda
la API.

### 2.6 La recarga en caliente, reconocida como peligrosa por el propio autor

El watcher (`CFileWatcher`) es un `inotify` en un **hilo propio**, con máscara
`IN_CREATE | IN_CLOSE_WRITE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM`. Cuando
salta, la carga del plugin —`luaL_dofile` **y la colocación de los detours**—
ocurre **en el hilo del watcher**, mientras Steam sigue corriendo.

O sea: `LM_HookCode` escribe 5 bytes sobre un prólogo que otro hilo puede estar
ejecutando **en ese mismo instante**. El código lo admite sin adornos:

```cpp
//There is no API in linux to freeze single threads, so we just wing it
```

y la documentación lo eleva a advertencia en negrita: *"While hot reloading Luas
is possible it's highly advised against doing so. You have been warned."*

Traducción operativa: **la recarga en caliente es una herramienta de desarrollo,
no un mecanismo de despliegue.** Cualquier diseño que dependa de dejar caer un
`.lua` en el directorio con Steam abierto está construyendo sobre algo que el
propio autor marca como inseguro.

El otro disparador es el comando `reloadlua` del canal `/tmp/SLSsteam.API`
(`api.cpp:143`), sólo con `API: yes`.

### 2.7 Aislamiento entre plugins: ninguno [RIESGO]

Todos los plugins comparten **un solo `lua_State` y un solo espacio de nombres
global**. `Downloader` y `Example` son variables globales de la misma tabla. Un
plugin puede leer, sobrescribir o borrar los globales de otro, incluidos sus
punteros a trampolín.

Y hay algo bastante peor, en el propio export de C:

```cpp
extern void* place_lua_hook(const int index, const void* pTarget)
{
    if (!LuaHook::hooks.contains(index)) return nullptr;
    LuaHook* hook = LuaHook::hooks.at(index);
    hook->hookFn = reinterpret_cast<lm_address_t>(pTarget);
    return hook->place();
}
```

`LuaHook::hooks` es un **mapa estático global**, y el índice se asigna como el
primer entero libre desde 0 (`hooks.cpp:186`). O sea, los índices son
`0, 1, 2, …` y **predecibles**. `place_lua_hook` acepta *cualquier* índice sin
comprobar quién lo pide.

Por tanto, un plugin puede pasar el índice de **otro** plugin, redirigirle el
`hookFn` a una función suya y llamar a `place()` — quedándose con el hook ajeno.
Combinado con el orden alfabético del §2.2, es un secuestro trivial y
determinista. No hay modelo de amenazas entre plugins porque **no hay frontera
entre plugins**: cargarlos es confiar plenamente en todos a la vez.

### 2.8 Superficie de seguridad

El commit de hoy `3d45b14` añade `Lua::fixPerms`: sobre el directorio y sobre
**cada** `.lua`, si hay bits de grupo u otros los reduce a `owner_all`, y si no
puede, **desactiva los plugins para toda la sesión**. Bien pensado y bien
colocado.

Pero conviene ser exacto sobre **qué defiende y qué no**:

- **Defiende** contra otros usuarios del sistema y contra ficheros dejados con
  permisos laxos.
- **No defiende** contra nada que corra **como el propio usuario**. Cualquier
  aplicación, script, instalador o juego con permisos de escritura en
  `~/.config/SLSsteam/plugins/` obtiene **ejecución de código nativo dentro del
  proceso de Steam**, vía FFI, sin sandbox, y **en caliente** — el `inotify` lo
  carga sin reiniciar nada.

Con `Plugins: no` (el defecto) los ficheros no se ejecutan, y eso es lo correcto.
Pero merece anotarse que **el resto de la maquinaria arranca igualmente**: el
`lua_State` se crea, los bindings se registran, el directorio se crea, se le
arreglan los permisos y **el watcher se pone a correr**. La puerta está sólo en
`runLua` (`lua.cpp:558`). No es una vulnerabilidad; sí es una superficie viva en
usuarios que creen tener la función apagada.

### 2.9 Lo que la API no garantiza

Ordenado por lo que más nos afectaría si algún día nos apoyáramos en ella:

| Ausencia | Consecuencia |
|---|---|
| **Sin versionado** de la API | No hay forma de que un plugin declare contra qué versión se escribió, ni de que SLSsteam lo rechace. Rompe en silencio. |
| **Sin estabilidad demostrada** | En nueve días quitó y devolvió sus propios exports dos veces (§7.9.11, §7.9.12). Un plugin del 30-ago está roto el 31. |
| **Sin aislamiento** entre plugins | §2.7. |
| **Sin dependencias ni orden declarable** | Sólo el alfabético. |
| **Sin contrato de desmontaje en release** | `luaReload` se dispara, pero los hooks no se quitan. El plugin es responsable de su propia idempotencia. |
| **Sin comprobación de doble hook** | §2.4 → Capa 3. |
| **Sin aislamiento de fallos** | Una excepción en un *callback* se captura y se registra (`fireCallback`); un error al **cargar** el fichero se registra y se sigue. Pero un fallo dentro de un **hook** ya colocado corre en un hilo de Steam y no lo captura nadie. |

### 2.10 Balance de la capa

Lo que hay, dicho con precisión: **un mecanismo de extensión potente, bien
integrado en el arranque, con el core reordenado para servirlo — y sin ninguna de
las garantías que uno pediría antes de apoyar un producto encima.** Sin
versionado, sin aislamiento, sin contrato de desmontaje, con recarga en caliente
que el propio autor desaconseja por escrito, y con un historial de nueve días en
el que la superficie pública cambió dos veces.

Eso no es una crítica: para lo que la API dice ser —*"plugins can run arbitrary
code! So only run plugins you trust"*— es coherente. La confianza se asume
completa desde el principio, y por eso no hay fronteras que diseñar.

Lo relevante para nosotros es la conclusión operativa, y esta capa ya la sostiene
sola, antes de mirar el `.lua`:

> **La API es un sitio excelente para prototipar y un sitio malo para vivir.**
> Escribir un plugin es barato y reversible; mover lumalinux a plugin sería atar
> la instalación entera a una plataforma sin contrato de estabilidad. El §7.9.13
> del doc hermano decía *"no planificar contra esta API hasta que haya release"*.
> La release ya está. La lectura del mecanismo dice: **haber release no es lo
> mismo que haber contrato**, y lo que falta es lo segundo.

Queda una pieza [PRESTABLE] que no depende de nada de lo anterior y que conviene
llevarse: `MemHlp::patternScan` + `getJmpTarget` sobre un **sitio de llamada** en
lugar de un prólogo. Se desarrolla en la Capa 2, donde el `.lua` la usa tres
veces.

## 3. Capa 2 — `download.lua` como artefacto técnico

En una frase: **es una reimplementación en 330 líneas de Lua de tres de los
seams de instalación de lumalinux**, sin nada del aparato de durabilidad que los
rodea aquí.

### 3.1 El mapa

| Seam | `download.lua` | lumalinux | Veredicto |
|---|---|---|---|
| Claves de depot | `CConfigStore::GetBinary`, claves desde `config.yaml` | **la misma función**, claves desde `keys.txt` | Convergencia total |
| Manifest request code | `GetManifestRequestCode`, wudrm en solitario | la misma, cascada de 3 + caché + UA anti-WAF | Vamos por delante |
| Depots del paquete 0 | hook de `GetPackage` → captura el puntero → reescribe | `LoadPackage` + `package_zero_finder` (377 líneas) | **Misma meta, ruta distinta** |
| Shader pre-cache | — | `ShaderDepot` (§13.9) | Sólo nosotros |
| Reconcile sin reinicio | — | `NotifyLicensesUpdated` (0x7d) | Sólo nosotros |
| Pinning de manifiestos | — (lo cubre `ManifestIds` del core) | BuildDep, desactivado por defecto | Empate por delegación |

Que los tres primeros coincidan no es sorpresa: son **los tres únicos sitios**
por los que se puede resolver el problema. Es convergencia forzada por el
binario, no copia.

### 3.2 Claves de depot — idénticos, salvo una guarda

Los dos enganchan el mismo accesor y lo reconocen por la misma ruta KeyValues
(`…\<depot>\DecryptionKey`). Diferencias reales:

- **Resolución.** El plugin la deriva por RTTI en dos pasos: saca el índice de
  ranura del mapa de interfaces (`21IClientConfigStoreMap`) y lo aplica sobre la
  clase concreta (`12CConfigStore`). Es **más limpio** que nuestro
  `ResolveVtableSlotBySignature`, que busca la ranura escaneando prólogos.
  Nosotros compensamos con el RVA feed por delante.
- **Orden.** El plugin llama al original **primero** y luego pisa el resultado si
  tiene clave propia; nosotros consultamos el `KeyStore` primero y volvemos sin
  llamar al original. Mismo resultado, una llamada de más en su caso.
- **Guarda de tamaño.** Nosotros validamos `keySize >= 32` antes del `memcpy`
  (`depot_key_hook.cpp:63`). **El plugin escribe 32 bytes sin mirar `outSize`.**
  En la práctica Steam siempre pasa un buffer suficiente para una clave de
  descifrado, así que es un defecto **latente**, no activo — pero es una
  escritura sin validar en memoria de Steam.
- **Depots sin clave.** Nuestro hook deja pasar deliberadamente las entradas
  *presence-only* (§13.8/13.9: fabricar una clave cero rompe el pre-caché de
  shaders). El plugin no tiene ese concepto porque tampoco tiene el hook que lo
  necesita.

### 3.3 Manifest request code — la pieza donde más ventaja tenemos

Misma función, misma intervención. Divergencias:

| | `download.lua` | lumalinux |
|---|---|---|
| Proveedores | `gmrc.wudrm.com` **en solitario**, y por **HTTP en claro** | cascada opensteamtool → wudrm → steamrun, HTTPS |
| Cloudflare | — | UA `OpenSteamTool/1.0`, hallazgo verificado en dispositivo |
| Caché | ninguna | en memoria (+ issue #21 para disco) |
| Reintentos | 5, recursivos, sin espera | por proveedor |
| Alcance | **cualquier** manifiesto que el original no codificara | sólo depots del `KeyStore` |

Las dos últimas filas son decisiones de diseño, no descuidos:

- Comprobar `pOutMRC[0] ~= 0` en vez de fiarse del valor de retorno **es mejor
  idea que la nuestra** y merece anotarse: es más robusto ante que la función
  devuelva éxito sin escribir.
- Pero **el alcance es más invasivo**: dispara también para juegos legítimamente
  comprados en los que Steam falle por cualquier motivo, y les mete un código de
  un tercero. Nosotros acotamos al `KeyStore` por el principio *do no harm*
  (§2.2 del doc hermano). Su cobertura es mayor; nuestra frontera es más segura.

Detalle menor de higiene: construye la URL con `ffi.string(buf, 32)` —**con
longitud explícita**—, así que la cadena Lua arrastra los NUL de relleno. Funciona
de casualidad, porque al pasarla como `const char*` el lado C corta en el primer
NUL. Doce líneas más abajo, para el log, usa `ffi.string(buf)` sin longitud, que
es lo correcto. Y deja un `mrcCStr` asignado y sin usar. Son marcas de código
escrito deprisa.

### 3.4 El paquete 0 — misma meta, y una divergencia de offset que hay que resolver

Aquí está lo más interesante de la capa.

**La ruta.** Nosotros llegamos al `PackageInfo` del paquete 0 de dos maneras: el
hook de `LoadPackage` (en el momento del parseo PICS) y, sobre todo, el
`package_zero_finder`, un hilo que deriva el GOT, escanea el idiom de acceso a la
caché y **recorre el árbol** con offsets de clase (`0xc58`, `0xc6c`, nodo `0x18`).
El plugin engancha `GetPackage` y **deja que Steam le entregue el puntero**.

Conviene deshacer una contradicción aparente con nuestro propio
`patterns.hpp`, que dice que `GetPackage` *"no está en la ruta de carga PICS"* y
que engancharlo (v0.10.3) rompió las descargas. **Las dos cosas son ciertas.** El
error de v0.10.3 fue engancharlo **en lugar de** `LoadPackage`, intentando
inyectar en el momento del parseo. El plugin no hace eso: usa `GetPackage`
sólo como **captura oportunista del puntero**, y reescribe de forma perezosa vía
el flag `RewriteDepots`. Ese es exactamente el papel de nuestro finder.

Y así comparados, **su ruta es mejor en un aspecto concreto**: obtiene el mismo
puntero sin un solo offset dependiente de build. Nuestros `0xc58`/`0xc6c`/`0x18`
son deuda que hay que revalidar en cada build de Steam; él no tiene ninguno.
A cambio es **reactivo**: si nadie vuelve a pedir el paquete 0, la inyección
perezosa nunca ocurre. El nuestro es **activo** y reinyecta cada 15 s.

**La divergencia que importa.** Los dos escriben ids de depot, pero **en vectores
distintos** del mismo `PackageInfo`:

```
+0x38  CUtlVector<uint32_t>   <- lumalinux escribe aquí  (lo llamamos AppIdVec)
+0x48  CUtlVector<uint32_t>   <- el plugin escribe aquí   (lo llamamos DepotIdVec)
```

Su `PackageInfo_t` (`packageId` + `__pad0x0[0x44]` + `depots`) sitúa su vector en
`+0x48` sin ambigüedad. Y nuestro propio `load_package_hook.hpp:41` documenta ese
`+0x48` como `DepotIdVec` — el sitio semánticamente correcto para ids de depot —
mientras nosotros escribimos en `+0x38`.

> **[CORRECCIÓN — 2026-09-04]** Una versión anterior de esta sección decía que
> el experimento de `DepotIdVec` se descartó *"antes de tener el reconcile"* y
> que por tanto la pregunta quedaba reabierta. **Es falso**, y contradice
> [`RESEARCH.md` §18](RESEARCH.md), que documenta el asunto con mucho más
> detalle. Lo que sigue es la versión corregida; el error se conserva anotado
> porque es justo el tipo de afirmación que reabre trabajo ya cerrado.

El comentario de `load_package_hook.cpp:179` remite a `RESEARCH.md` §18, que es
donde está la historia completa:

> *"**Dead end — `DepotIdVec` (tested, ruled out).** […] We shipped an experiment
> (v0.16.14, `LUMA_DEPOT_IDVEC`) that also seeds `DepotIdVec`. **Result: no
> change** — still `0 target depots` without a restart. **Because the failure is
> upstream (empty appinfo depot list), eligibility (whichever vector it reads) is
> never consulted.** So `DepotIdVec` is **not** the lever."*

O sea: el experimento **sí se hizo**, y no dio nada **no** por falta del
reconcile, sino porque el cuello de botella estaba aguas arriba — el `appinfo`
del juego llegaba con la lista de depots vacía, así que el filtro de
elegibilidad, **lea el vector que lea**, nunca se consultaba. La palanca real era
el **reconcile de licencias**, que es lo que se implementó en v0.16.15/16.

**La pregunta "¿qué vector lee el filtro?" no está abierta: es la pregunta
equivocada.**

#### Lo que SÍ queda abierto, y es más importante

§18 deja escrito el motivo real por el que `slsteam-moon` sí usa `DepotIdVec`:

> *"moon avoids the hang by keeping `AppIdVec` app-ids-only and depots in
> `DepotIdVec` — that is the **only** reason moon touches `DepotIdVec`."*

¿Qué cuelgue? El de OpenSteamTool:

> *"OST needs an anti-hang hook (`CAppInfoCache::GetOrAddAppData` → `bSkipFlag`)
> because it **injects depot ids into `AppIdVec` that never resolve appinfo**,
> blocking `ProcessPendingLicenseUpdates` forever; OST shipped that hook
> **disabled**."*

Y la posición de lumalinux:

> *"**Hang**: none observed. The cold-cache PICS-re-request hang doesn't apply —
> the finder injects after login (warm cache)."*

**lumalinux reúne las dos precondiciones del cuelgue de OST** — ids de depot en
`AppIdVec` **y** un reconcile que dispara `ProcessPendingLicenseUpdates` (por
defecto desde v0.16.16) — y se apoya en un *"no lo hemos visto"* con un argumento
de caché caliente. Eso es una observación, no una garantía estructural.

→ **Accionable A, reformulado**: no *"¿qué vector lee el filtro?"* sino
**"¿el argumento de la caché caliente aguanta, o el cuelgue es latente?"**.
Diseño del experimento en §3.4.1.

#### 3.4.1 Medición hecha (2026-09-04): los vectores son lo que dicen ser

Sonda de solo lectura (`tools/experiment_pkg_vectors.lua`) sobre un Steam real
(cliente `1788400362`) con SLSsteam y **sin lumalinux**, volcando ambos vectores
de paquetes que el usuario posee:

| Paquete | `appIds` (+0x38) | `depotIds` (+0x48) |
|---|---|---|
| 1081 | `[22300]` (Fallout 3) | `[22301, 22302, 22303, 22304, 22306]` |
| 11301 | `[6910]` (Deus Ex GOTY) | `[6911]` |
| 6442 | `[22380, 22480]` | `[22381…22386, 22393, 22481]` |
| 7194 | `[50300, 203180]` | `[50301…50309, 203185, 203189]` |
| 0 | 196 appids de juegos gratis (440, 480, 570…) | 878 depots de numeración baja |

**Concluyente**: `+0x38` contiene ids de aplicación y `+0x48` sus depots
(típicamente `appid+1, +2, …`). Las etiquetas heredadas de `Structs.h` de
LumaCore **son correctas**, ya no son una convención sin verificar.

Consecuencia: **lumalinux mete ids de depot en la lista de aplicaciones**, que es
exactamente la condición que produce el cuelgue en OST. La medición no dice que
haya que cambiarlo — dice que la premisa que sostiene el diseño de moon es cierta.

Dato lateral con valor operativo: en el paquete 0, `appIds` tiene `alloc=202` /
`size=196` (6 huecos) mientras `depotIds` tiene `alloc=1021` / `size=878` (143
huecos). Escribir en `+0x38` obliga a reasignar casi siempre; en `+0x48` casi
nunca — relevante para el Accionable C.

#### 3.4.2 Experimento 2 — forzar la caché fría (pendiente)

Hipótesis a refutar: *"el cuelgue no nos aplica porque el finder inyecta con la
caché de PICS caliente"*.

1. Entorno completo (SLSsteam + lumalinux + un juego desplegado con
   `steamidra_lite`).
2. Steam cerrado; `mv ~/.local/share/Steam/appcache ~/appcache.bak` para forzar
   que Steam re-pida `appinfo` de **todo** lo que haya en `AppIdVec`, incluidos
   nuestros ids de depot, que no son apps y no van a resolver nunca.
3. Arrancar y observar ~5 min: `lumalinux.log` (¿inyecta el finder? ¿dispara el
   reconcile?), `content_log.txt` (¿`0 target depots`?), y si la UI de la
   biblioteca responde.
4. **Brazo de control**: repetir con `LUMA_NO_RECONCILE=1`.

| Resultado | Lectura |
|---|---|
| No se atasca en ninguna combinación | El argumento de caché caliente aguanta en frío. **A se cierra, medido** |
| Se atasca con caché fría **y** reconcile, no sin él | **Cuelgue latente reproducido** → adoptar el diseño de moon (appids en `+0x38`, depots en `+0x48`) |
| Se atasca siempre en frío | Hay algo más aguas arriba; el reconcile no es el factor |

**La gestión de memoria también difiere**, y es la otra mitad del asunto: nosotros
**añadimos en el sitio** creciendo con `realloc` de libc; él **asigna un buffer
nuevo** con `Plat_Alloc`, copia la lista fusionada, cambia el puntero y libera el
viejo con `Plat_Free`. Ver §3.6.

### 3.5 Lo que el plugin no hace

No es una lista de reproches — es el foso, y conviene tenerlo escrito:

- **Sin skip de pre-caché de shaders.** Toda la clase de fallo de §13.8 (el
  `Invalid content configuration`, el bucle de `Missing decryption key`) le queda
  abierta.
- **Sin reconcile de licencias.** Sus juegos requieren reinicio de Steam; el
  no-restart de LumaDeck es nuestro.
- **Sin RVA feed ni SafeMode.** Resolución 100 % por patrones y RTTI: cuando Steam
  recompile, se rompe y alguien tiene que editar patrones a mano. Nosotros
  publicamos RVAs por hash desde CI.
- **Sin diagnóstico.** Ni `status.json`, ni recuento `X/Y hooks activos`, ni
  `maintenance.md`. Cuando falle, el usuario no tendrá con qué distinguir el caso.
- **Sin despliegue.** No escribe manifiestos al `depotcache`, ni `AdditionalApps`,
  ni claves en `config.vdf`. Espera que alguien haya dejado el `config.yaml`
  preparado. Todo eso lo hace `steamidra_lite` en nuestro lado.

### 3.6 Defectos encontrados

| # | Defecto | Severidad | Nota |
|---|---|---|---|
| 1 | `hkGetBinary` escribe 32 bytes sin comprobar `outSize` | Media (latente) | Nosotros validamos |
| 2 | `Downloader.Package` se cachea indefinidamente | **Alta** | Ver abajo |
| 3 | `Plat_Free` sobre el buffer original de Steam | Media | Ver §3.7 |
| 4 | MRC sin caché, un solo proveedor, HTTP en claro | Media | Fragilidad operativa |
| 5 | Inyección perezosa: si nadie vuelve a pedir el paquete 0, no ocurre | Media | Reactivo vs. activo |
| 6 | `ffi.string` con longitud en la URL; `mrcCStr` muerto | Baja | Higiene |

El **#2** merece detalle. `Downloader.Package` guarda el puntero al
`PackageInfo` la primera vez y no lo revalida nunca. Si Steam reconstruye el
paquete 0 —re-login, refresco del conjunto de licencias— ese puntero queda
colgando, y `writeDepotIds` desde el callback de `luaReload` escribiría sobre
memoria liberada. Nuestro finder vuelve a recorrer el árbol en cada ciclo y
reinyecta de forma idempotente; esa diferencia de diseño **nos da la razón**, y
conviene que quede anotado porque el finder ha costado bastante.

### 3.7 El fallo del `LuaMutex` — upstream, y en el ejemplo oficial [RIESGO]

Esto no es del plugin: es de SLSsteam, y lo hereda todo el que copie la
documentación. `lua.cpp:161`:

```cpp
class LuaMutex {
    std::recursive_mutex* mutex;
public:
    LuaMutex() : mutex(&Lua::stateMutex) { lock(); }
    ~LuaMutex()  { unlock(); }
    void lock()   { mutex->lock(); }
    void unlock() { mutex->unlock(); }
};
```

**No lleva ningún seguimiento de propiedad.** Y el idiom que enseña
`example.lua` —y que `download.lua` copia en sus tres hooks vía
`mutexReturn`— es:

```lua
local mutex = LuaMutex()   -- bloquea
...
mutex:unlock()             -- desbloquea (1)
-- más tarde, cuando el GC recoja el objeto: ~LuaMutex() -> desbloquea (2)
```

Es decir: **por construcción, cada hook desbloquea dos veces.** Y el segundo
desbloqueo ocurre cuando al recolector de basura le apetece, en el hilo que le
toque.

Desbloquear un `std::recursive_mutex` que no posees es comportamiento
indefinido. En glibc, en el caso benigno, devuelve `EPERM` y no hace nada —
por eso nadie lo ha visto todavía. Pero hay un camino nada exótico al fallo real:

1. Hilo A entra en un hook, `LuaMutex()` → contador 1.
2. `mutex:unlock()` → contador 0, liberado. El objeto queda para el GC.
3. Hilo A entra en **otro** hook, `LuaMutex()` → contador 1.
4. El GC corre en el hilo A y recoge el objeto del paso 1 → `~LuaMutex()` →
   **contador 0**.
5. El hook del paso 3 sigue ejecutándose creyendo que tiene el `lua_State` en
   exclusiva. Otro hilo puede entrar ya.

Y ese es exactamente el escenario que la propia documentación dice que hay que
evitar (*"Lua is not thread safe"*). El ayudante escrito para prevenirlo lo
reintroduce.

Con lo que la §2.5 de la capa anterior se agrava: no sólo nada obliga a poner el
candado — **es que el candado que se recomienda está mal**. Un plugin que use
`LuaMutex()` sin `unlock()` manual (dejando sólo el destructor) sería correcto,
pero entonces el bloqueo dura hasta el GC, que puede ser muchísimo después.
No hay forma correcta de usar esta clase tal como está.

→ **Accionable B**: no depende de nosotros arreglarlo, pero sí condiciona
cualquier plan que pase por escribir plugins, y es lo primero que reportaría
upstream.

### 3.8 Las dos preguntas de fondo que esta capa mueve

**(a) El asignador — §7.7.12.a queda resuelto, y no a nuestro favor.**

Teníamos abierto si es legítimo hacer `realloc` de libc sobre `m_pMemory`.
Nuestro comentario afirma *"CUtlMemory is malloc-backed on Steam Linux i386"*.
Hay ahora tres datos en contra:

1. El README de la API: *"malloc & free seem to work just fine. **I still
   recommend using Plat_Alloc, Plat_Realloc & Plat_Free** from libtier0_s.so
   instead"*.
2. `download.lua` usa `Plat_Alloc`/`Plat_Free`.
3. Y el decisivo: **el SDK del propio SLSsteam modela `CUtlMemory` con el
   asignador de tier0** (`sdk/CUtl.hpp`):

```cpp
~CUtlMemory() { if (base) Steam::Plat_Free(base); }
bool resize(...) { mem = base ? Steam::Plat_Realloc(base, n) : Steam::Plat_Alloc(n); }
```

Eso no es una recomendación en un README: es su posición expresada en código,
sobre la misma estructura que nosotros tocamos. Lo más probable es que en Linux
`Plat_Alloc` sea una capa fina sobre `malloc` —lo que explicaría el *"seem to
work just fine"*— pero apoyarse en eso es apostar a un detalle de implementación
de Valve. → **Accionable C**: resolver `Plat_Realloc`/`Plat_Free` de
`libtier0_s.so` por `dlsym` (ya hacemos ese baile con libcurl) y usarlos en
`AppendIdsToVec`, con `realloc` como respaldo si no se resuelven.

**(b) El offset.** §3.4 — y ojo, **reformulado**: no es qué vector lee el
filtro (pregunta cerrada en `RESEARCH.md` §18) sino si el cuelgue de
`ProcessPendingLicenseUpdates` es latente. → **Accionable A**.

### 3.9 Lo prestable [PRESTABLE]

1. **Resolución por sitio de llamada.** `patternScan` sobre el `call` +
   `getJmpTarget` para saltar al destino, en vez de anclar en el prólogo del
   destino. El plugin lo usa dos veces y el `example.lua` oficial una tercera.
   Sobrevive a un recompilado que reemita el prólogo — justo la clase de rotura
   que nos obligó a montar el RVA feed. Como **tercer resolvedor** detrás del
   feed y del RTTI, es barato y no rompe nada.
2. **Derivación de ranura por el mapa de interfaces** (§3.2), más limpia que
   escanear prólogos dentro de la vtable.
3. **Comprobar el parámetro de salida en vez del valor de retorno** en GMRC
   (§3.3).

### 3.10 Balance

El plugin **no es un reemplazo de lumalinux**: es la prueba de que los tres seams
son alcanzables desde un script, sin ninguna de las cosas que hacen que un
usuario pueda vivir con ello seis meses. Le faltan el shader skip, el reconcile,
el feed de RVAs, el SafeMode, el diagnóstico y todo el despliegue.

Pero técnicamente **nos ha enseñado tres cosas y nos ha reabierto dos**: la
resolución por sitio de llamada y la derivación de ranura son mejores que las
nuestras; la captura del puntero del paquete 0 vía `GetPackage` hace en 20 líneas
lo que a nosotros nos cuesta 377 con offsets de build dentro; y el asunto del
asignador y el del offset del vector quedan como trabajo real (Accionables A y C).

En el otro sentido, confirma dos decisiones nuestras que costaron caro: la
revalidación del puntero en cada ciclo del finder (su defecto #2) y el acotar la
intervención a nuestros propios depots (§3.3).

## 4. Capa 3 — La frontera de coexistencia

Esta es la capa con consecuencias operativas inmediatas. Las anteriores describen;
esta obliga a **retirar una afirmación publicada** y deja accionables con usuarios
detrás.

### 4.1 Lo que §5 del doc hermano afirmaba, y por qué deja de valer

`slssteam-analysis.md` §5 se titula, literalmente, *"los conjuntos de hooks son
**disjuntos** (la sección que importa)"*, y sostiene:

> *"Lo más valioso del análisis: **verificar que SLSsteam y lumalinux no tocan
> ninguna de las mismas funciones**."*

con una tabla de cinco filas, todas *"No"*, y un cierre en tres niveles: loader
distinto, superficies disjuntas, gate de versión compartido.

**Esa afirmación sigue siendo cierta para el core de SLSsteam y falsa para el
conjunto SLSsteam + plugins.** No es que se haya quedado desactualizada: es que
el objeto que describía ha cambiado de forma. Hasta hoy, "SLSsteam" era un
conjunto cerrado de hooks que se podía enumerar leyendo su código. Desde hoy,
**"SLSsteam" es un conjunto abierto**: lo que engancha depende de qué ficheros
haya en el directorio de plugins del usuario, que no es auditable desde nuestro
lado ni estable en el tiempo.

La disjunción no se ha roto por un hook nuevo. Se ha roto porque **ya no se puede
verificar leyendo un repositorio**.

→ §5 del doc hermano queda **[SUPERSEDED por esta sección]** en su alcance: vale
para el core, no para el sistema desplegado.

### 4.2 El orden de carga no es una incógnita — ya lo medimos

Aquí está lo que hace esta capa sólida en vez de especulativa. **Este escenario ya
ocurrió en producción**, y está documentado en nuestro propio `main.cpp:191`:

> *"BuildDep is OFF by default since SLSsteam 20260714 hooks `BuildDepotDependency`
> itself (for its `ManifestIds` / `DepotBlacklist` features) and **loads first
> (LD_AUDIT before our LD_PRELOAD), overwriting the prologue**. Our pattern scan
> then can't match and reports a false FAILED, which LumaDeck used to surface as
> 'Steam build not supported'."*

De ahí salen tres hechos medidos, no deducidos:

1. **El orden es determinista y conocido: SLSsteam primero, lumalinux después.**
   `LD_AUDIT` corre antes que `LD_PRELOAD`. Y como los plugins se ejecutan al
   final del `setup()` de SLSsteam (§2.2), **los plugins también llegan antes que
   nosotros**.
2. **El síntoma del solapamiento fue un fallo seguro**: nuestro escaneo de
   patrones no encontró el prólogo —porque ya estaba parcheado— y el hook no se
   instaló. Nadie se rompió.
3. **Pero el fallo seguro tuvo un coste de producto**: se reportó como `FAILED`, y
   LumaDeck lo tradujo a *"Steam build not supported"*. Un mensaje alarmante y
   falso.

Y la respuesta que dimos entonces fue la correcta y sigue siéndolo: **ceder el
seam**. BuildDep se apagó por defecto y el pinning se delegó en `ManifestIds` del
core. Ese es el precedente que gobierna esta capa entera.

**Corrección a una lectura anterior mía en esta misma investigación.** En la
primera pasada dije que el orden peligroso era *"lumalinux primero, plugin
después"* y el benigno el inverso. El precedente de BuildDep dice que **el que
ocurre en la realidad es siempre el segundo**: nosotros llegamos los últimos. El
mecanismo que describí era correcto; la dirección relevante es la contraria a la
que enfaticé.

### 4.3 El solapamiento real, hoy

| Función | Core SLSsteam | `download.lua` | lumalinux | Estado |
|---|---|---|---|---|
| `BuildDepotDependency` | **Sí** | no | apagado por defecto | **Resuelto** (cedido, 2026-07) |
| `CConfigStore::GetBinary` (claves) | no | **Sí** | **Sí** | **Solapa** |
| `GetManifestRequestCode` | no | **Sí** | **Sí** | **Solapa** |
| `GetPackage` / paquete 0 | no | **Sí** (`+0x48`) | finder (`+0x38`) | Ver §4.6 |
| `GetShaderCacheDepot` | no | no | **Sí** | Limpio |
| `NotifyLicensesUpdated` | no | no | **Sí** | Limpio |
| Ownership / tickets / DLC / IPC | **Sí** | no | no | Limpio (§5 doc hermano) |

**Dos funciones solapan de verdad**, y son justo las dos que nuestro
`main.cpp` marca como el **conjunto crítico**: si DepotKey o GMRC no están
activos, una descarga forzada no puede completarse.

### 4.4 Qué pasa con dos detours sobre la misma función — y lo que no sé

Los dos lados usan `LM_HookCode` de libmem más una reparación de thunk PIC
(§2.4). Ninguno comprueba si el destino ya está enganchado.

Encadenar detours **no es automáticamente un fallo**. En el papel, si Steam llama
a la función: salta al hook del plugin → éste llama a su trampolín → que contiene
el `jmp` de lumalinux relocalizado → salta al hook de lumalinux → que llama a su
trampolín → que contiene el prólogo original → vuelve al cuerpo de la función.
Funcionalmente incluso quedaría coherente: en `GetBinary` ganaría la clave del
plugin, en GMRC ganaría el código de lumalinux.

**Pero eso depende de que libmem relocalice correctamente un prólogo que ya es un
detour ajeno, y eso no está validado por nadie.** El caso normal que libmem
maneja es reubicar instrucciones reales; reubicar un `jmp rel32` que apunta fuera
del módulo es un caso de borde. Encima, nuestra `FixPicThunk` busca un
`call get_pc_thunk` dentro de los bytes robados: si lo robado es el `jmp` de otro,
no encuentra nada y devuelve `false` — inofensivo, pero es señal de que estamos
operando fuera de las suposiciones del código.

**Segunda corrección a mi lectura inicial:** dije que este escenario era un crash.
No lo sé, y afirmarlo era ir más lejos de lo que sostiene la evidencia. Lo
honesto es: **resultado indeterminado, plausiblemente funcional, nunca probado, y
sin ninguna de las dos partes comprobando nada.** Eso ya es razón suficiente para
no dejarlo al azar, pero no es lo mismo que un fallo garantizado.

### 4.5 La ironía: nuestras dos mejoras de robustez desactivan el fallo seguro

Este es el hallazgo de la capa, y es contraintuitivo.

En 2026-07, con BuildDep, el solapamiento se resolvió solo: SLSsteam parcheó el
prólogo, **nuestro escaneo de patrones dejó de encontrarlo**, y el hook no se
instaló. Feo en el log, seguro en la práctica. Ese fallo seguro **era una
propiedad accidental de resolver por patrón de bytes**: si alguien te pisa el
prólogo, no te reconoces y te retiras.

Desde entonces hemos añadido dos resolvedores **deliberadamente independientes
del prólogo**, precisamente para dejar de rompernos cuando Steam recompila:

- **El RVA feed**, que resuelve por hash del binario → dirección publicada desde CI.
- **La derivación por RTTI**, que llega por la vtable.

Y ahí está la ironía: **ambos siguen acertando aunque el prólogo esté parcheado
por otro.** El feed devuelve la dirección igual; la vtable sigue apuntando a la
función igual. Es decir:

> **Justo las dos piezas que construimos para no rompernos son las que convierten
> "no me instalo, fallo seguro" en "me instalo encima de otro, resultado
> desconocido".**

Concretamente, para GMRC (`gmrc_hook.cpp:69`): se prueba el feed primero, y sólo
si falla se cae al localizador por patrón. Si el feed tiene entrada para el build
del usuario —hoy hay dos publicadas, y CI añade más— acertará, y engancharemos
encima del plugin sin enterarnos. Para DepotKey pasa lo mismo, con el matiz de
que su ruta RTTI *sí* falla (busca la ranura comparando prólogos, que estarían
parcheados), pero el feed la salva igual.

La conclusión no es retirar el feed. Es que **el feed necesita la comprobación que
el patrón nos daba gratis**. → **Accionable D**.

### 4.6 El paquete 0: hoy no colisionan — y el Accionable A crearía la colisión

Un resultado que sólo aparece cruzando la Capa 2 con esta.

Los dos escriben en el `PackageInfo` del paquete 0, pero **en vectores distintos**:
nosotros en `+0x38`, el plugin en `+0x48` (§3.4). Al ser dos estructuras separadas,
**hoy no hay colisión de memoria**: cada uno gestiona su lista y no se pisan.

Pero el plugin no *añade*: **sustituye la lista entera** — reserva un buffer
nuevo, copia, cambia el puntero y **libera el viejo con `Plat_Free`**. Nosotros
añadimos en el sitio creciendo con `realloc` de libc.

Júntese eso con el **Accionable A** (evaluar mover nuestra inyección a `+0x48`):

> Si movemos la inyección a `+0x48` **sin más**, pasamos a compartir vector con el
> plugin, y entonces sí colisionan de la peor manera: él libera con el asignador
> de Valve un buffer que nosotros podemos haber redimensionado con el de libc, o
> al revés. **Liberación cruzada de asignadores = corrupción del heap de Steam.**

O sea: **el Accionable A y el Accionable C no son independientes, y el orden
importa.** Si algún día se mueve la inyección a `+0x48`, el cambio de asignador
(C) tiene que ir **antes o a la vez**, nunca después. Queda anotado como
dependencia dura.

### 4.7 Lo que sigue siendo disjunto

Para no exagerar el alcance del problema, conviene dejar escrito lo que **no**
cambia:

- **El loader.** SLSsteam por `LD_AUDIT`, nosotros por `LD_PRELOAD`, ambos desde
  el mismo wrapper. Intacto (§0.1 doc hermano).
- **El gate de versión.** El SafeMode de SLSsteam sigue decidiendo si el stack
  arranca; nosotros seguimos dentro de esa ventana, con nuestro hash como
  advertencia y no como puerta.
- **La capa de mensajes, ownership, tickets, DLC e IPC.** Sigue siendo suya en
  exclusiva y nosotros seguimos sin tocarla.
- **El alcance de proceso.** SLSsteam se descarga en todo lo que no se llame
  exactamente `steam` (`main.cpp:99`); nosotros admitimos además `steamwebhelper`.
  En ese proceso corremos solos: ningún plugin puede alcanzarnos ahí.
- **Los dos seams que sólo tenemos nosotros**: shader skip y reconcile de
  licencias. Ningún plugin conocido los toca.

El daño está acotado a dos funciones. Pero son las dos críticas.

### 4.8 Superficie nueva: el directorio de plugins, y LumaDeck como root

Un ángulo que no existía antes de hoy y que nos toca de lleno.

En `rva_feed.cpp:44` dejamos escrito, al retirar los overrides por variable de
entorno, un razonamiento que ahora aplica a otra puerta:

> *"this feed decides WHERE HOOKS GET INSTALLED inside Steam's process. Anything
> able to add an env var to Steam's launch (…) could point the feed at a server it
> controlled and choose our hook targets. **That turns 'can write a file in $HOME'
> into 'can run code inside Steam'**."*

Ese razonamiento es correcto, y describe **exactamente** lo que el directorio de
plugins es ahora: cualquiera que pueda escribir un `.lua` en
`~/.config/SLSsteam/plugins/` ejecuta código nativo dentro de Steam, en caliente,
sin reinicio (§2.8). Cerramos nuestra puerta y se ha abierto una equivalente al
lado, con la diferencia de que esa no la controlamos.

Y el detalle incómodo: **LumaDeck corre como root** (`plugin.json`, `flags:
["_root"]`). Es decir, LumaDeck es una de las cosas que trivialmente pueden
escribir ahí. Eso no es un problema hoy —no escribimos nada en ese directorio—
pero fija una regla de diseño para la Capa 4: **LumaDeck no debe instalar,
escribir ni modificar plugins de SLSsteam**, ni siquiera "para ayudar al usuario".
Hacerlo convertiría al plugin de Decky en un vector de ejecución de código en
Steam, y nos haría responsables de un `.lua` de terceros que no podemos auditar.

### 4.9 Accionables de la capa

| # | Acción | Prioridad | Nota |
|---|---|---|---|
| **D** | **Detección de doble enganche antes de instalar.** Antes de `LmHook::Install`, inspeccionar el destino: si el primer byte ya es un salto incondicional cuyo destino cae fuera del mapeo de `steamclient.so`, alguien ha llegado antes. Registrarlo, **no instalar**, y marcar el hook como `FOREIGN` en `status.json` (estado nuevo, distinto de `FAILED`). | **Alta** | Recupera el fallo seguro que el RVA feed nos quitó (§4.5). Es la contrapartida honesta de la robustez que ganamos. |
| **E** | **Que LumaDeck sepa distinguir `FOREIGN` de `FAILED`.** Hoy un hook no instalado se traduce a *"Steam build not supported"* — el mismo mensaje falso que dio BuildDep en julio. Con D en su sitio, el mensaje correcto es *"otro componente ha enganchado esta función"*. | **Alta** | Sin E, D sólo cambia un mensaje alarmante por otro. |
| **A′** | **Dependencia dura**: si se ejecuta el Accionable A (mover a `+0x48`), el Accionable C (asignador de tier0) va antes o a la vez. | Media | §4.6 |
| **F** | **Regla de diseño**: LumaDeck no escribe en el directorio de plugins de SLSsteam. Nunca, ni para instalar `download.lua` a petición del usuario. | Media | §4.8 |

Ninguno de estos pasa por "detectar `download.lua`" por nombre o por hash. Eso
sería una carrera imposible de ganar: el fichero puede llamarse de cualquier forma
y cambiar mañana. **La detección correcta es genérica y está en el sitio donde
importa: justo antes de escribir nuestro propio detour.**

## 5. Capa 4 — Consecuencias para LumaDeck

Las capas anteriores describen un ecosistema. Esta baja al producto: qué le pasa
**hoy** a un usuario de LumaDeck por lo que se publicó esta mañana.

### 5.1 El hallazgo: un `.lua` puede hacer que LumaDeck ofrezca degradar Steam

Esto no es un riesgo teórico ni futuro. Es una cadena que ya está cableada, y
conviene leerla entera:

1. El plugin engancha `GetBinary` y GMRC. **Siempre llega antes que nosotros**
   (§4.2), así que cuando llegamos, los prólogos ya están parcheados.
2. lumalinux intenta resolver. Si el **RVA feed no tiene entrada para el build
   del usuario** —hoy hay **dos** publicadas en `res/rvas/`, y CI añade según
   aparecen— caemos al escaneo por patrón, que **no encuentra nada** porque el
   prólogo ya no es el que buscamos.
3. `status.json` registra `DepotKey` y/o `GMRC` como `"failed"`.
4. `paths.py:766` los ve en `_CRITICAL_LUMALINUX_HOOKS` y devuelve:
   `{"state": "not_supported", "cause": "hooks", "action": "downgrade"}`.
5. `SystemStatus.tsx:155` pinta la fila de severidad `problem`:

   > **"Steam build not supported"**
   > *"A Steam update broke LumaDeck. Press Fix in Desktop to repair it."*

6. Y el botón de esa fila ejecuta `actions.downgrade`: el traspaso a Desktop que
   **degrada el cliente de Steam del usuario** y lo fija a un build anterior.

O sea:

> **Un fichero `.lua` en una carpeta hace que LumaDeck le diga al usuario que
> Steam le ha roto la instalación, y le ofrezca degradar su cliente de Steam para
> arreglarlo.** Steam está perfecto. No se ha roto nada. Y la acción propuesta es
> destructiva, lenta, y deja al usuario en un build antiguo por un diagnóstico
> falso.

Es **exactamente** la misma clase de error de diagnóstico que dio BuildDep en
julio (§4.2) — con la diferencia de que entonces el desenlace era un mensaje
alarmante, y ahora el mensaje viene con un botón que degrada Steam.

Esto **eleva el Accionable E** de "alta prioridad" a **la prioridad de toda la
investigación**. Los accionables D y E dejan de ser higiene y pasan a ser
contención de daño.

Matices honestos sobre la probabilidad, para no exagerar:

- Si el RVA feed **sí** tiene el build del usuario, resolvemos igual, enganchamos
  encima y reportamos `installed`: no hay falso `not_supported`… pero entonces
  estamos en el doble enganche no validado de §4.4. **Ninguna de las dos ramas es
  buena**; una miente al usuario y la otra hace algo indeterminado en silencio.
- El usuario tiene que haber activado `Plugins: yes` **y** haber puesto el `.lua`.
  No es el caso común. Pero es exactamente el perfil del usuario entusiasta que
  ya tiene LumaDeck instalado, que es justo el que va a probar el plugin que
  circula por Discord.

### 5.2 Lo que ya funciona solo, y era lo previsto

Una nota positiva que conviene registrar porque costó trabajo en su día.

`slssteam_schema.py` completa la config del usuario contra el `config_default.hpp`
de upstream **descargado en vivo desde `main`**. Con la release de hoy, eso
significa que las tres claves nuevas (`Plugins`, `SmartTickets`, `LaunchOptions`)
**se añaden solas** a la config de cada usuario, en su siguiente carga, sin que
hayamos tocado nada.

Y como `Plugins` viene con su valor por defecto (`no`), el efecto neto es que
**LumaDeck reparte la función de plugins desactivada** a toda su base. Que es
exactamente el defecto correcto.

§7.9.13 del doc hermano predijo esto y decidió no actuar: *"la rama elegida hace
el trabajo sin que intervengamos; el trabajo de `a061b00` se paga aquí"*. Se ha
pagado.

**Lo que sí queda pendiente** es lo que aquel mismo párrafo dejó anotado: el
**snapshot embebido** de `slssteam_schema.py` (el respaldo sin red) sigue sin las
tres claves nuevas — verificado, cero coincidencias de `Plugins:`,
`SmartTickets:` y `LaunchOptions:` en el fichero. Sólo importa en un Deck sin red
con SLSsteam recién actualizada, y la decisión era refrescarlo **cuando hubiera
release**. La hay. → **Accionable G**.

### 5.3 Lo que NO se rompe

Para acotar el alcance, y porque es tranquilizador:

- **Nuestra escritura de config no interfiere.** `slssteam_schema` es
  *append-only* y sólo añade claves que existan en upstream; `AdditionalDepots` y
  `DecryptionKeys` no están ahí (§1.2), así que nunca las tocamos ni las
  pisamos. Un usuario puede tener el plugin configurado y LumaDeck no le va a
  romper la configuración.
- **`AdditionalApps`, `ManifestIds` y `DisableUpdates`** siguen siendo nuestros y
  el plugin no los toca.
- **El despliegue de un juego** (`steamidra_lite`: manifiestos al `depotcache`,
  `keys.txt`, claves en `config.vdf`, el lua a `stplug-in/`) no tiene solape
  alguno con lo que hace el plugin, que no despliega nada (§3.5).
- **`steamwebhelper`.** SLSsteam se descarga en todo proceso que no se llame
  exactamente `steam`; nosotros seguimos cargando ahí. Ningún plugin nos alcanza
  en ese proceso.

### 5.4 La carga de soporte, y qué preguntar

El escenario que va a llegar por Discord es: *"LumaDeck me dice que Steam no está
soportado y me pide degradar"*. Hoy no tenemos forma de distinguirlo de una
rotura real por actualización de Steam, y las dos respuestas son opuestas
(degradar vs. **no** degradar).

Con el Accionable H (detección de sólo lectura) la pregunta se responde sola en
el propio panel. Sin él, el triaje manual es:

1. ¿`Plugins: yes` en `~/.config/SLSsteam/config.yaml`?
2. ¿Hay `.lua` en `~/.config/SLSsteam/plugins/`?
3. En `~/.cache/lumalinux/lumalinux.log`, ¿los hooks fallidos son `DepotKey`/`GMRC`
   **mientras** `ShaderDepot` instaló bien? Una actualización de Steam suele mover
   varios patrones a la vez; que fallen exactamente los dos que un plugin conocido
   engancha, y no los demás, es la firma del solape.

Ese punto 3 es un discriminador razonable **mientras** el plugin en circulación
sea éste. No es una base sólida para automatizar nada — por eso el Accionable D
comprueba el destino en memoria, que sí es genérico.

### 5.5 Postura de producto

Tres decisiones, y creo que las tres son claras:

**(a) No bloquear.** LumaDeck no debe negarse a funcionar, ni desactivar plugins
del usuario, ni "recomendar" quitarlos. La máquina es suya y la función es
legítima. Nuestra obligación es **diagnosticar bien y no proponer barbaridades**,
no tutelar.

**(b) No escribir nunca en el directorio de plugins** (Accionable F, §4.8).
Ni para instalar `download.lua` si alguien lo pide, ni para desactivarlo, ni para
"arreglar" permisos. LumaDeck corre como root; escribir ahí lo convertiría en una
vía de ejecución de código dentro de Steam y nos haría responsables de un script
de terceros que no podemos auditar. Esta regla debe sobrevivir a la petición
razonable de un usuario, que llegará.

**(c) Sí detectar, en sólo lectura.** Leer `Plugins:` de la config y listar los
`.lua` presentes es barato, no invasivo y resuelve el triaje de §5.4. Se muestra
como **información** en Settings → Components (no como advertencia), y alimenta la
desambiguación de D/E.

### 5.6 Accionables de la capa

| # | Acción | Prioridad | Nota |
|---|---|---|---|
| **E′** | **Que un hook `FOREIGN` no produzca `not_supported`.** En `paths.py:766`, `critical_failed` debe contar sólo `"failed"`, nunca el nuevo `"foreign"`. Estado propio, severidad `info` o `problem` **sin** `action: "downgrade"`, y copy nuevo: *"otro componente ha enganchado esta función"*. | **Crítica** | Sin esto, D convierte el falso `not_supported` en otro falso `not_supported`. Es la mitad que impide el daño. |
| **G** | Refrescar el snapshot embebido de `slssteam_schema.py` desde el `config_default.hpp` de la release `20260903114323` (añade `Plugins`, `SmartTickets`, `LaunchOptions`). | Media | §5.2. Decisión ya tomada en §7.9.13, condicionada a que hubiera release. |
| **H** | Detección de sólo lectura: `Plugins:` de la config + inventario de `~/.config/SLSsteam/plugins/*.lua`, expuesto en Settings → Components como información. | Media | Resuelve §5.4 y alimenta E′. |
| **F** | Regla de diseño: LumaDeck no escribe en el directorio de plugins. | Media | §4.8, §5.5(b). Documentar en `DESIGN.md` como entrada del decision log. |

**Orden de ejecución recomendado:** D (lumalinux) y E′ (LumaDeck) van juntos y
primero — son los que quitan de en medio la oferta de degradar Steam. H después,
porque mejora el diagnóstico pero no evita daño. G es independiente y puede ir
cuando toque.

## 6. Capa 5 — Escenarios, señales y decisión

Última capa. Las cuatro anteriores establecen los hechos; ésta dice qué hacer con
ellos y qué vigilar.

### 6.1 El reencuadre: dónde está realmente nuestro foso

La conclusión incómoda de la Capa 2, dicha sin rodeos:

> **Los tres hooks de instalación de lumalinux son, desde hoy, una mercancía.**
> Cualquiera con conocimiento de `steamclient.so` los reproduce en 330 líneas de
> script, sin compilar, sin toolchain de 32 bits, sin pipeline de releases.

Eso duele si uno cree que el valor de lumalinux **son** los hooks. No lo son. Lo
que un plugin no reproduce barato es el resto:

| Pieza | ¿Reproducible en un `.lua`? | Por qué |
|---|---|---|
| DepotKey / GMRC / paquete 0 | **Sí, ya está hecho** | 330 líneas |
| **Skip del pre-caché de shaders** | En principio sí, **pero nadie lo ha hecho** | Requiere §13.8/§13.9: entender por qué el juego se suspende, y descubrir la ruta de skip que Steam ya tiene. Meses de RE, no horas |
| **Reconcile de licencias** | Igual | El no-restart es un hallazgo, no una técnica |
| **RVA feed + CI** | **No** | Requiere infraestructura fuera del proceso: monitorizar builds, derivar direcciones, publicarlas, cachearlas |
| **SafeMode / diagnóstico / `status.json`** | **No en la práctica** | Es producto, no técnica |
| **Despliegue (`steamidra_lite`)** | **No** | Fuera del proceso por definición |
| **El corpus documental** | **No** | 500 KB de RE que es el activo que permitió todo lo anterior |

El foso no son los tres seams. **Es la maquinaria que hace que sigan funcionando
después de que Steam se actualice**, más los dos seams que costaron meses. Y esa
maquinaria es estructuralmente inalcanzable para un plugin: un `.lua` no tiene CI,
ni feed por hash, ni pipeline de publicación. Cuando Steam recompile, `download.lua`
se rompe y alguien tendrá que editar patrones a mano; lumalinux se repara sola.

Eso no es consuelo, es dirección: **si los hooks son mercancía, el sitio donde
invertir es lo que los mantiene vivos**, no defenderlos.

### 6.2 Los tres escenarios

**(a) Se oficializa como plugin.** El dev publica un descargador en
`docs/Lua/` o en el repo, y SLSsteam pasa a traer la capacidad de serie.

- lumalinux pierde la exclusiva de los tres hooks, pero conserva los dos seams
  propios y toda la maquinaria de mantenimiento.
- **La coexistencia pasa de accidente a norma**: en vez de un `.lua` de Discord
  que tiene el 1 % de los usuarios, sería el camino recomendado. Los Accionables
  D y E dejan de ser contención y pasan a ser requisito permanente.
- LumaDeck no cambia de razón de ser: sigue siendo la capa de orquestación y UX,
  y eso ningún plugin lo cubre.

**(b) Se queda extraoficial.** El `.lua` circula, unos pocos lo usan, upstream no
lo bendice.

- Es el escenario **actual**, y el peor para el soporte: la capacidad existe y se
  propaga, pero nadie es responsable de ella. Los usuarios que se rompan vendrán
  a nuestro Discord, no al suyo.
- Todos los accionables siguen valiendo exactamente igual.

**(c) Se absorbe al core.** SLSsteam mete claves y descarga directamente en el
binario, sin pasar por plugins.

- Es el escenario que más cambia las cosas y el menos probable a corto: su README
  no menciona descargas ni una vez, y meterlo al core convierte a SLSsteam en algo
  que hoy declara no ser.
- Si ocurriera: los tres hooks de lumalinux quedan redundantes de verdad, y la
  respuesta correcta es la misma que ya dimos con BuildDep en 2026-07 — **ceder el
  seam** y quedarnos con lo que sólo hacemos nosotros. No sería una derrota; sería
  el tercer seam cedido por la misma razón y con el mismo criterio.

**En los tres escenarios LumaDeck sobrevive intacto.** Es la capa de producto:
credenciales, catálogo, despliegue, salud, multi-librería, fixes, UX de Game Mode.
Nada de eso lo toca un plugin. **El expuesto es lumalinux**, y sólo en su tercio
más reproducible.

### 6.3 ¿Convertir lumalinux en plugin?

La pregunta que subyace a todo esto, contestada con lo que sabemos ya.

**Lo que se ganaría** es real y grande: desaparece toda la superficie de inyección
— el wrapper, la cobertura por `.desktop`, el drop-in de PATH, el drop-in de
systemd, el guardián que reafirma la cobertura, el fail-safe de crash-loop. Es, de
largo, **nuestro mayor coste de mantenimiento**, y buena parte de
`maintenance.md` y de los estados de salud de LumaDeck existen sólo por eso.

**Lo que se perdería**, con lo aprendido en las Capas 1 y 3:

1. **La independencia.** Nos ataríamos a una API sin versionado, sin contrato de
   desmontaje y que en nueve días quitó y devolvió sus exports dos veces (§2.9).
2. **`steamwebhelper`.** SLSsteam se descarga en todo proceso que no sea `steam`
   (§4.7). Perderíamos ese proceso.
3. **El aislamiento.** Pasaríamos a compartir espacio global con cualquier otro
   `.lua`, que puede secuestrar nuestros hooks por índice (§2.7).
4. **El arranque garantizado.** Requiere `Plugins: yes`, que viene apagado. Cada
   usuario tendría que activarlo — y **LumaDeck no debe activarlo por él** (§6.5).
5. **El `LuaMutex` roto** (§3.7) nos afectaría igual que a todos.

**Recomendación: no portar.** No por apego, sino porque el punto 1 es
descalificatorio por sí solo: la instalación entera de todos los usuarios
dependería de la estabilidad de una API que aún se está reordenando. Y el §7.9.13
del doc hermano ya lo formuló bien; lo único que añade esta investigación es que
*haber release no es haber contrato*.

**Pero sí hay un uso excelente para la API, y conviene aprovecharlo ya:**

> **Usar el sistema de plugins como instrumento de medida, no como vivienda.**

El Accionable A (`+0x38` vs `+0x48`) se responde con un `.lua` de treinta líneas
que enganche `GetPackage`, lea **los dos** vectores del paquete 0 y los registre
en el log. Sin recompilar lumalinux, sin tocar a ningún usuario, en una sesión de
pruebas. Lo mismo vale para verificar el asignador (Accionable C) y para
reproducir el escenario de doble enganche del §4.4 en un entorno controlado.

Eso es barato, reversible y no compromete nada. → **Accionable I**.

### 6.4 Señales de vigilancia

Concretas y comprobables, no "estar atentos":

| Señal | Dónde mirar | Qué significaría |
|---|---|---|
| `download.lua` (o equivalente) aparece en `docs/Lua/` | repo SLSsteam | Escenario (a) confirmado |
| `AdditionalDepots` / `DecryptionKeys` entran en `config_default.hpp` | repo SLSsteam | Oficialización en curso; y **cerraría la Capa 0** de paso |
| El core toca `GetBinary`, GMRC o el paquete 0 | `hooks.cpp` / `patterns.cpp` | Escenario (c) — ceder seams, como con BuildDep |
| Aparece versionado de la API o un campo de capacidades | `lua.cpp` / `docs/Lua/` | La API empieza a tener contrato: **reabrir la pregunta de portar** |
| Se arregla `LuaMutex` | `lua.cpp` | Indicador de cómo responde upstream a un reporte (§3.7) |
| Circulan **más** plugins que enganchan los mismos seams | Discord / repos | El problema de coexistencia se multiplica; D y E pasan a imprescindibles |
| El `example.lua` deja de enseñar el `unlock()` manual | `docs/Lua/example.lua` | Reconocimiento del fallo del mutex |

La primera y la segunda son la misma pregunta que dejó abierta la Capa 0. **No
hace falta perseguirla**: si se oficializa, se verá.

### 6.5 Una precisión sobre el Accionable F

Al escribir esta capa aparece un caso que F, tal como quedó redactado en §4.9, no
cubre: **LumaDeck tampoco debe poner `Plugins: yes` en la config del usuario.**

No es lo mismo que escribir en el directorio, pero el efecto es peor de lo que
parece: activar la opción **ejecuta cualquier `.lua` que ya esté en esa carpeta**,
lo pusiera quien lo pusiera. Sería encender la ejecución de código arbitrario en
Steam en nombre del usuario, por ficheros que no hemos visto.

F queda entonces en dos mitades: **ni escribir plugins, ni activarlos.** Escribir
`Plugins: no` tampoco — desactivarle al usuario algo que él encendió es la
tutela que §5.5(a) descarta. La postura es simétrica: **esa clave es suya y no la
tocamos en ninguna dirección.** Lo único que hacemos con ella es leerla
(Accionable H).

*(Nota: `slssteam_schema.py` **añade** `Plugins: no` cuando la clave no existe —
§5.2. Eso no es tocar la elección del usuario: es completar un fichero incompleto
con el defecto de upstream, que es exactamente su trabajo, y sin él SLSsteam
toastea en cada arranque. La regla prohíbe cambiar un valor existente, no sembrar
el defecto en su ausencia.)*

### 6.6 Accionables consolidados

Todo lo que sale de esta investigación, en un sitio:

| # | Acción | Dónde | Prioridad |
|---|---|---|---|
| **D** | Detección de doble enganche antes de instalar; estado `FOREIGN` en `status.json` | lumalinux | **Crítica** |
| **E′** | `FOREIGN` no cuenta como `critical_failed` → sin `action: "downgrade"`, copy propio | LumaDeck | **Crítica** |
| **B** | Reportar upstream el doble `unlock` de `LuaMutex` | SLSsteam (issue) | Alta |
| **I** | Plugin de diagnóstico para medir A y C sin tocar producción | herramienta | Alta |
| **A** | **Reformulado**: ¿es latente el cuelgue de `ProcessPendingLicenseUpdates`? (§3.4.1 medido, §3.4.2 pendiente) | lumalinux | Media |
| **C** | `Plat_Realloc`/`Plat_Free` vía `dlsym` sobre `libtier0_s.so`, con `realloc` de respaldo | lumalinux | Media |
| **A′** | Dependencia dura: si se ejecuta A, C va antes o a la vez | — | — |
| **H** | Detección sólo lectura de `Plugins:` + inventario de `.lua`, como información | LumaDeck | Media |
| **G** | Refrescar el snapshot embebido de `slssteam_schema.py` | LumaDeck | Media |
| **F** | Regla de diseño: ni escribir plugins, ni activarlos, ni desactivarlos | LumaDeck (`DESIGN.md`) | Media |
| **3ª** | Resolución por sitio de llamada como tercer resolvedor, tras feed y RTTI | lumalinux | Baja |

**Orden:** D + E′ juntos y primero (quitan la oferta de degradar Steam). Luego B
e I, que son baratos y desbloquean. Después A/C con su dependencia, y H, G, F
cuando toque.

### 6.7 Cierre

Lo que ha pasado hoy no es que haya aparecido un competidor. Es que **SLSsteam ha
dejado de ser una librería y se ha convertido en una plataforma**, y con eso ha
cambiado dos cosas para nosotros:

1. **Una afirmación que teníamos publicada ha dejado de ser verificable.** La
   disjunción de hooks (§5 del doc hermano) no se rompió por un hook nuevo: se
   rompió porque el conjunto contra el que se verificaba dejó de ser cerrado.
2. **Y nuestras dos mejoras de robustez se han convertido en un riesgo**, porque
   la red de seguridad que teníamos era un efecto secundario del método frágil que
   sustituimos (§4.5).

Ninguna de las dos cosas se ve mirando el `.lua`. Salen de mirar el mecanismo, y
por eso el análisis fue por capas.

Lo urgente es pequeño y está acotado: **dos accionables (D y E′) que impiden que
LumaDeck le ofrezca a alguien degradar su Steam por culpa de un fichero de texto.**
El resto es trabajo ordinario con calendario propio.

Y lo estratégico no requiere decisión hoy. Requiere vigilar §6.4 y aceptar el
reencuadre de §6.1: **el valor de lumalinux nunca estuvo en los tres hooks, sino
en seguir funcionando el día después de que Steam se actualice.** Eso ningún
plugin lo puede copiar, porque no es código: es una máquina de mantenimiento.

---

## 7. Anexo técnico — cómo localiza funciones cada proyecto

Esta sección nace del análisis del plugin pero **no va del plugin**: va de nuestro
propio motor de localización, visto por primera vez al lado de otros dos. Es la
parte más accionable de toda la investigación y no estaba escrita en ningún sitio.

### 7.1 Las cinco familias de anclaje

Todo el problema es el mismo: `steamclient.so` es código compilado sin nombres, y
Valve lo recompila cada pocas semanas. Hay cinco formas de encontrar una función
dentro, y se ordenan por resistencia:

| # | Ancla | ¿Sobrevive a recompilar? | ¿Sobrevive a que otro enganche antes? |
|---|---|---|---|
| 1 | **Prólogo de la función** | ❌ Mal | ❌ **No** — el detour lo sobreescribe |
| 2 | **Instrucción del cuerpo** + caminar atrás | ✅ | ✅ |
| 3 | **Sitio de llamada** + seguir el salto | ✅ | ✅ |
| 4 | **Cadena de texto** referenciada + caminar atrás | ✅✅ | ✅ (con el matiz de §7.6.b) |
| 5 | **Vtable por nombre** (RTTI + mapa de interfaces) | ✅✅ | ✅ · también a reordenaciones |

La fila 1 es la única con dos cruces, y es **nuestro método principal**.

**Por qué el prólogo es la peor elección**: casi toda función i386 PIC arranca
igual (`E8 …; 05 …; 55 89 E5 57 56 53`), así que para desambiguar hay que alargar
el patrón — y lo que se añade es justo lo volátil: tamaño de marco (`81 EC 10 01
00 00`), offsets de spill (`8B 44 24 44`), orden de registros. Nada de eso es la
función; es cómo la maquetó GCC. Nuestro `kDepotKeyFnPattern` mide **46 bytes**;
las firmas de cuerpo de SLSsteam miden **5**.

### 7.2 Quién usa qué

| Proyecto | Reparto |
|---|---|
| **SLSsteam core** | 10 × cuerpo+atrás · 4 × sitio de llamada · 6 × extracción de offset · 37 × vtable por nombre · 5 × slot fijo |
| **`download.lua`** | 2 × sitio de llamada · 1 × vtable por nombre |
| **lumalinux (runtime)** | 5 × prólogo · 1 × cadena (GMRC, plan C) · 1 × derivación de offset (finder) · + el RVA feed |
| **lumalinux (Ghidra)** | **5 × cadena de texto** |

Dos lecturas que sólo aparecen puestas así:

1. **El ancla de cadena no la usa ningún otro proyecto para localizar.** Nuestro
   `derive_patterns.py` tiene, en robustez, **el mejor método de los tres** — y
   corre offline, en CI, después de la rotura.
2. **Ya inventamos tres de las cuatro técnicas de SLSsteam.** Cuerpo+atrás y
   cadena (`gmrc_xref.cpp`, con una derivación del GOT por consenso **mejor que la
   suya**) y derivación de offset (`FindCacheGlobalDisp`). Cada una usada **una
   vez**, cada una como plan B. No falta técnica: falta ascenderla.

### 7.3 El flujo completo, de punta a punta

```
   Ghidra (derive_patterns.py)        ← ancla en CADENA
        │  encuentra la función por el texto
        │  y APUNTA SU PRÓLOGO                    ⚠ convierte el ancla buena en la mala
        ▼
   src/patterns.hpp  (compilado en el .so)        → cambiarlo exige release
        │
        ▼
   watch-steam.yml (cron diario) → check_patterns.py
        │  ejecuta ESOS MISMOS patrones contra el .so descargado
        ├─ CLEAN (0)  → whitelist + emite res/rvas/<hash>.yaml + auto-merge
        ├─ noCrit (2) → whitelist + lanza Ghidra + PR (necesita release)
        └─ BLOCK (3)  → NO whitelist + lanza Ghidra + PR
        ▼
   res/rvas/<hash>.yaml  ("la chuleta")           → viaja como DATO, sin release
        │
        ▼
   El Deck:  chuleta → [RTTI+firma, sólo DepotKey] → patrón → [xref, sólo GMRC]
```

**El punto clave del diagrama**: Ghidra deriva un **hecho** (dónde está la función
en *esta* build, encontrada por un ancla universal) y publica una **hipótesis**
(«así empezará también en las próximas»). La cadena vale para todas las builds; el
prólogo, para una. Se usa la buena para llegar y se guarda la mala.

Y lo hace por herencia, no por criterio: `patterns.hpp` sólo sabe guardar patrones
de bytes, así que la salida de Ghidra **tiene que ser** un patrón.

### 7.4 La chuleta no es un método: es una caché del patrón

`check_patterns.py` **parsea `src/patterns.hpp` y ejecuta esos patrones**. No
localiza nada por su cuenta. Y su propio código lo dice:

> *"a moved/ambiguous hook is omitted so that hook falls back to its byte pattern"*

De donde: **si el patrón falla, la chuleta no tiene entrada para ese hook.** No son
dos disparos independientes; es un disparo y una copia. La chuleta **nunca puede
rescatar un patrón roto** por sí sola — sólo se llena después de que Ghidra
re-derive.

Su valor real es **otro, y sí es grande**: es un **canal de distribución**.
Convierte «hay que recompilar y publicar» en «mergea un YAML», y el `.so`
desplegado se cura en el siguiente arranque.

**Recuento honesto de métodos realmente distintos por hook:**

| Hook | Métodos independientes |
|---|---|
| DepotKey | **1** (la frase: en la vtable, en `.text`, y precalculada en CI) |
| GMRC | **2** (la frase, y la cadena) |
| ShaderDepot / BuildDep / Reconcile | **1** cada uno |
| Finder | **1**, pero derivada en caliente y sin depender de nadie |

**Cuándo no hay chuleta** (más casos de los que parece):

*No existe aún* — la ventana entre que Valve publica y el cron pasa; un crítico
roto esperando merge; Ghidra no pudo re-derivar. *Existe pero no llega* — sin red
al arrancar (gamemode lanza Steam antes de que asocie el wifi); **la caché es por
hash, así que tras actualizar Steam la que tienes es del hash viejo e inútil**;
primer arranque; GitHub caído o el repo renombrado (URL fija a
`jayool/lumalinux/main`). *Llega inservible* — chuleta **parcial** (sólo se
escriben hooks `UNIQUE`); `VaddrXlate::Init` falla y se descarta entera; el YAML no
parsea. *Nunca la hay* — el finder no usa feed en absoluto.

El caso realista que junta dos: **el usuario actualiza Steam, reinicia, Steam
arranca antes que el wifi, la caché es del hash anterior.** Build nuevo, sin
validar, sin red, y todo el peso sobre el método más frágil.

### 7.5 Hallazgos

**a) El cable suelto del RTTI. [ACCIONABLE K]**
El run #71 lo enseña literal:

```
DepotKey-RTTI slot 6 @ 0x11a4500 (agrees-with-pattern=True)
```

CI resuelve DepotKey **por RTTI, sin patrón**, verifica que coincide con el patrón,
y lo **publica** en la chuleta (`depotkey_rtti: {class, slot}`). Y
`RvaFeed::load()` **sólo parsea `["hooks"]`** — nunca lo lee. Además
`Rtti::ResolveVtableSlot(nombre, slot)` (la variante de slot fijo, sin patrón)
existe en `rtti.cpp` y **no la llama nadie**.

Valor de conectarlo: (1) **contraste** RVA↔slot, que hoy no existe en runtime;
(2) y sobre todo, **el slot es mucho más portable que el RVA** — la dirección vale
para un hash, `"CConfigStore slot 6"` probablemente para meses. Sería un plan C que
aguanta builds sin chuleta. Coste ≈ 30 líneas.

**b) El walk-back del xref falla ABIERTO. [ACCIONABLE J]**
`gmrc_xref_core.hpp:94` ancla en el byte **`0xE8`** para reconocer la entrada de la
función. Un detour de 5 bytes lo convierte en `E9`. El bucle entonces **sigue
retrocediendo** y encuentra el prólogo de la **función anterior** — devuelve una
dirección equivocada en vez de rendirse.

La cabecera ya contempla el caso propio (*"must be called BEFORE the GMRC detour is
installed"*), pero no el de un tercero. Y el cruce de §D11.a
(*"si discrepan, uso el patrón"*) **no llega a correr**: sólo compara cuando ambos
resuelven, y aquí el patrón ya falló. Respuesta mala, sin contraste.

*Es un bug propio, independiente del plugin.* Arreglo local: aceptar `E9`/`FF 25`
como «entrada ya enganchada», o acotar el retroceso y fallar cerrado.

**c) Los dos offsets que no valida nadie.**
El informe de CI valida los cinco patrones, el idiom de la caché (`0xc58`), la cola
de GMRC y el slot RTTI. **`+0x38` y `+0x48` no aparecen.** Son constantes en el
fuente que nadie deriva, nadie comprueba y nadie publica. La única red es el
`SANITY FAIL` en caliente, que detecta el desastre *después* de intentarlo.

Toda la maquinaria existe para cuidar cinco frases; los cuatro números que deciden
dónde escribimos en la memoria de Steam no los mira nadie.

**d) La deriva del cron ensancha la ventana.**
El cron está en `17 7 * * *` y el run #71, con `event: schedule`, arrancó a las
**12:08 UTC** — casi **5 horas** tarde. Los cron de Actions son *best-effort*. La
ventana de exposición no es «hasta 24 h», es «24 h + lo que GitHub tarde».

**e) Lo que está bien hecho.** El guardado de caché **es condicional**
(`whitelisted || pr.merged`), así que una versión que rompió se sigue
re-comprobando cada día. Ya hubo una entrada envenenada y se arregló bumpeando la
clave a `v2`. Esa parte del diseño es sólida y conviene no tocarla.

### 7.6 Prerrequisito para mover anclas: convergencia

`slsteam-moon-findings.md` §D11.b se retiró el 2026-09-01 con el argumento
correcto: anclando en prólogos, la convergencia no puede darse. Pero dejó escrita
la condición, y **es exactamente el prerrequisito de cualquier movimiento a la
familia 3**:

> *"si un hook crítico pasa algún día a anclar en un sitio de llamada, hay que
> enseñar convergencia al auditor antes de ese cambio."*

Hoy `classify_hit_count()` marca `AMBIGUOUS` cualquier n>1. Con un ancla en el
sitio de llamada, N llamadores del mismo destino son N coincidencias **legítimas**.
`slsteam-moon` ya pisó ese charco (un localizador con veinte llamadores les rompió
el arranque) y lo resolvió siguiendo cada coincidencia y exigiendo que **todas
converjan al mismo destino**.

**Sin ese cambio en el auditor, el primer hook que se mueva saldrá BLOCKING sin
estarlo.**

### 7.7 Balance

> **SLSsteam invirtió en resolución. lumalinux invirtió en un feed.**

Su resolución funciona en un build **que nadie ha visto nunca**, porque lo deduce
todo del binario que tiene delante. La nuestra, cuando el patrón falla, necesita
que CI haya pasado por ahí, que haya red, y que el repo siga vivo. El feed es en
buena parte **compensación por una resolución frágil** — no un error, pero sí un
cálculo local convertido en servicio remoto.

Y la conclusión no pide inventar nada:

| Prioridad | Qué | Estado |
|---|---|---|
| 1 | **J** — que el walk-back falle cerrado | Bug propio, hoy |
| 2 | **K** — conectar el `depotkey_rtti` ya publicado | ~30 líneas, ya validado por CI |
| 3 | Subir el ancla de cadena a runtime en los otros cuatro hooks | Las cadenas ya están identificadas en `derive_patterns.py`; el código de referencia es `gmrc_xref.cpp` |
| 4 | Familia 3 (sitio de llamada) donde no haya cadena usable | **Requiere §7.6 primero** |

*No falta técnica: falta haberla puesto en primera fila.*
