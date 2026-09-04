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
| **2** | `download.lua` como artefacto técnico | §3 — pendiente de redactar |
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

*Pendiente de redactar.*

## 4. Capa 3 — La frontera de coexistencia

*Pendiente de redactar.*

## 5. Capa 4 — Consecuencias para LumaDeck

*Pendiente de redactar.*

## 6. Capa 5 — Escenarios y señales de vigilancia

*Pendiente de redactar.*
