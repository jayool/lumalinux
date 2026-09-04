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

El análisis va **por capas**, y cada una condiciona a la siguiente:

| Capa | Qué responde | Estado |
|---|---|---|
| **0** | Datación y autoría de `download.lua` | **§1 — cerrada** |
| **1** | La API de plugins como mecanismo | **§2 — cerrada** |
| **2** | `download.lua` como artefacto técnico | **§3 — cerrada** |
| **3** | La frontera de coexistencia (§5 de `slssteam-analysis` deja de valer) | §4 — pendiente de redactar |
| **4** | Consecuencias para LumaDeck | §5 — pendiente de redactar |
| **5** | Escenarios estratégicos y señales de vigilancia | §6 — pendiente de redactar |

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

El comentario de `load_package_hook.cpp:179` explica por qué:

> *"The DepotIdVec experiment was removed: it did nothing without a license
> reconcile, which is the actual no-restart mechanism."*

O sea: **probamos `+0x48`, no funcionó, y lo descartamos — antes de tener el
reconcile.** Hoy sí lo tenemos. Que un tercero use `+0x48` y le funcione es un
segundo dato que reabre la pregunta.

No afirmo que lo nuestro esté mal: `+0x38` funciona en producción y está
verificado. Lo que digo es que **hay dos implementaciones que hacen lo mismo por
vectores distintos, y una de las dos está pasando por un camino indirecto**.
Merece medirse. → **Accionable A**.

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

**(b) El offset.** §3.4. → **Accionable A**.

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

*Pendiente de redactar.*

## 5. Capa 4 — Consecuencias para LumaDeck

*Pendiente de redactar.*

## 6. Capa 5 — Escenarios y señales de vigilancia

*Pendiente de redactar.*
