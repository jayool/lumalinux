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
| **1** | La API de plugins como mecanismo | §2 — pendiente de redactar |
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

*Pendiente de redactar.*

## 3. Capa 2 — `download.lua` como artefacto técnico

*Pendiente de redactar.*

## 4. Capa 3 — La frontera de coexistencia

*Pendiente de redactar.*

## 5. Capa 4 — Consecuencias para LumaDeck

*Pendiente de redactar.*

## 6. Capa 5 — Escenarios y señales de vigilancia

*Pendiente de redactar.*
