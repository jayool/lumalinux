# Cómo localiza funciones cada proyecto — familias de anclaje

*Sacado del análisis de los plugins de SLSsteam (2026-09-03 → 09-08), donde era
el anexo §7. No trata de los plugins sino del motor de localización de
lumalinux visto junto al de SLSsteam y al de `download.lua`. Las referencias
"§3.x", "§4.x", "§5.x" y "§6.x" apuntan a aquel documento, que sigue en el
historial de git (`docs/slssteam-plugins-analysis.md`); su sucesor es
`slssteam-plugins.md`. Estado de los accionables J, K, M1-M4 y "3" al día de hoy:
`slssteam-plugins.md` §4.4.*

## 1 Las cinco familias de anclaje

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

## 2 Quién usa qué

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

## 3 El flujo completo, de punta a punta

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

## 4 La chuleta no es un método: es una caché del patrón

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
parsea. *Nunca la hay* — **corregido el 2026-09-08**: el finder ya lee
`finder.cache_global_disp` de la chuleta (`RvaFeed::CacheGlobalDisp()`), así que
esta categoría deja de aplicarle. Con matiz: la chuleta le da uno de los dos
números que necesita, no los dos. El GOT lo sigue derivando escaneando porque
`finder.got_rva` no se publica (`design/rva-feed-design.md` §5), o sea que el finder
está a medio camino, no fuera.

El caso realista que junta dos: **el usuario actualiza Steam, reinicia, Steam
arranca antes que el wifi, la caché es del hash anterior.** Build nuevo, sin
validar, sin red, y todo el peso sobre el método más frágil.

## 5 Hallazgos

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

La intuición de valor es correcta —**el slot es mucho más portable que el RVA**:
la dirección vale para un hash, `"CConfigStore slot 6"` probablemente para
meses— pero **el mecanismo que esta sección proponía no la realiza**:

> **[CORRECCIÓN — 2026-09-07]** «Conectar el `depotkey_rtti` de la chuleta» **no
> sirve para nada**, y el motivo se ve en el propio YAML: el bloque contiene
> `rva: "0x11a4500"`, que es **el mismo valor que `hooks.DepotKey`**. Es el dato
> duplicado, no un dato nuevo. Y la chuleta se descarga por hash del binario
> (`rva_feed.cpp:55`), así que en un build sin ficha **no hay ni RVA ni slot**:
> el fichero entero no existe. Leerlo de ahí da información justo cuando ya
> sobra, y nada cuando hace falta.

> **[CORRECCIÓN 2 — 2026-09-07]** Esta sección pasó entonces a proponer
> **compilar el slot como constante** (`kDepotKeyVtableSlot = 6`). También es la
> respuesta mala, y la buena estaba desde el principio en §3.2 de este mismo
> documento: **`download.lua` no usa ninguna constante — deriva el índice en
> caliente, por nombre.** Se corrige abajo con el código del plugin delante.

**Lo que sí realiza la intuición — el método de `download.lua` (líneas 69-79):**

```lua
local configStoreMapGetBinary = VFTableInfo_t("21IClientConfigStoreMap", "GetBinary")
if not configStoreMapGetBinary:init() then
    log.notifyError("Failed to find GetBinary!")
    return
end

local configStoreGetBinary = VFTableInfo_t("12CConfigStore", "GetBinary",
                                           configStoreMapGetBinary.index)
```

Dos pasos, cero constantes:

1. **Derivar el índice por nombre.** En la vtable de la clase *mapa de interfaz*
   `21IClientConfigStoreMap`, busca la ranura **cuya función referencia la cadena
   `"GetBinary"`**. Las clases `*Map` son la capa de despacho y cada método lleva
   su propio nombre dentro, así que el índice sale del binario que se tiene
   delante. El mecanismo de SLSsteam es `Decompiler::parseInterfaceMapBase`
   (`decompiler.cpp:593`): recorre ranuras, desensambla cada una, recoge sus
   referencias a cadenas y construye `nombre → índice`.
2. **Aplicar el índice a la clase concreta**, `12CConfigStore`, para obtener el
   puntero real.

Y **falla cerrado**: si `init()` no resuelve, error y `return` — el plugin no se
carga. No sigue a ciegas.

Ni constantes, ni bytes de la función, ni chuleta: **dos nombres de texto**. Es
inmune a la recompilación *y* a que Valve reorganice la vtable, porque el índice
se vuelve a derivar en cada arranque.

**Qué falta en lumalinux — una sola pieza:**

| Paso | ¿Existe? |
|---|---|
| Vtable de una clase por nombre RTTI | **Sí** — `FindTypeVtable`, en uso |
| Recorrer ranuras acotado | **Sí** — `ResolveVtableSlotBySignature`, `maxSlots=40` |
| Desensamblar una instrucción | **Sí** — `LM_Disassemble`, usado en `FixPicThunk` |
| **Ranura → cadenas que referencia → índice por nombre** | **No. Es lo único que falta** |
| Aplicar el índice a la clase concreta | **Sí** — `Rtti::ResolveVtableSlot(nombre, slot)`, escrita y **sin llamar** |

No hace falta un decompilador: SLSsteam tiene `decompiler.cpp` entero porque lo
hace genérico para 37 interfaces; aquí hace falta **una**. Y su consumidor
—`ResolveVtableSlot`— lleva meses escrito esperando exactamente este productor.

**Verificado en binario real — 2026-09-07.** `tools/experiment_ifacemap_slot.py`
sobre el build `bc54101b29…` (STEAMDECK_STABLE, `version=1788652215`), el mismo
que ya tiene ficha en `res/rvas/`:

| Comprobación | Resultado |
|---|---|
| vtable de `21IClientConfigStoreMap` | existe, `0x2ebe7a0`, **21 ranuras de código** |
| base del GOT por consenso (`E8 …; 05 imm32`) | `0x02f4a34c` con **13500/13590 votos = 99,3 %** |
| refs `lea reg,[got+disp32]` | **cero falsos positivos**: el disp32 suelto aparece exactamente las mismas veces que la ref `lea` en los cuatro casos |
| ranura → cadena | `0xbee6e0` → **ranura 6** (+178 B) · `0xbee6ec` → **ranura 7** (+157 B). 1 a 1, sin ambigüedad |
| `12CConfigStore` ranura 6 | **`0x011a4500` = `hooks.DepotKey`** ✅ |
| `12CConfigStore` ranura 7 | `0x011a4700` — **otra función** |
| contraste con la ficha del CI | `slot 6` **COINCIDE** · `rva` **COINCIDE** |

**El método por nombre reproduce el resultado del patrón, sin leer un byte de la
función objetivo.** Es la primera vez que DepotKey tiene dos vías verdaderamente
independientes verificadas contra el mismo binario.

**Tres requisitos que salen del experimento y que la implementación debe cumplir**
(el volcado de `.rodata` los hace evidentes: `GetString · GetBinary · GetBinary ·
GetBinaryWatermarked`):

1. **Casar la cadena completa con su NUL.** `"GetBinary"` es prefijo de
   `"GetBinaryWatermarked"`; buscar el prefijo casa de más.
2. **No vale la primera aparición en `.rodata`.** Hay **cuatro**, en dos bloques
   idénticos de la misma tabla de nombres (`0xbe1c40…` y `0xbee6e0…`), y **sólo
   el segundo lo referencia la vtable del Map**. Hay que probar cada candidata
   contra las ranuras y quedarse con la que resuelve.
3. **Desempate de sobrecargas: el menor índice**, que es la regla de
   `download.lua` (pide `"GetBinary"` a secas). No es cosmético: la ranura 7 es
   **otra función** (`0x11a4700`), así que elegir mal da un hook silenciosamente
   equivocado — hay que atribuir la ref a la ranura por proximidad y exigir que
   caiga dentro de un tamaño de función razonable, como hace la sonda.

**Nota de encuadre**, para no sobrevalorar al plugin: la vía por nombre la usa
**sólo** para `GetBinary`. Sus otros dos hooks (líneas 66-67) son patrones de
bytes en el **sitio de llamada** (`E8 ? ? ? ? 83 C4 …` + `getJmpTarget`). O sea:
usa la técnica cara donde no hay ancla dentro de la función —que es justo el
caso de DepotKey (§7.5.a: *"NO usable in-function anchor"*)— y patrones cortos
donde sí la hay.

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

> **Al día 2026-09-08.** Sigue siendo cierto en lo esencial, con dos cambios. Lo
> que mejoró: el CI ya no sólo *valida* el idiom y la cola de GMRC, **bloquea**
> si no resuelven únicos; y antes de escribir, el finder cruza la clave del nodo
> del árbol contra el `PackageId` que el propio objeto declara en `+0x00`, o sea
> que hay una comprobación de identidad *antes* del intento, no sólo un
> `SANITY FAIL` después. Lo que no cambió: `+0x38` y `+0x48` siguen sin derivar,
> sin comprobar y sin publicar — **y no son validables** en un binario sin
> símbolos, que es la razón de que sigan ahí. Está anotado como riesgo asumido
> en el bloque `KNOWN LIMITS` de `package_zero_finder.cpp` (límite [14]) en vez
> de fingir que no existe.

**d) La deriva del cron ensancha la ventana.**
El cron está en `17 7 * * *` y el run #71, con `event: schedule`, arrancó a las
**12:08 UTC** — casi **5 horas** tarde. Los cron de Actions son *best-effort*. La
ventana de exposición no es «hasta 24 h», es «24 h + lo que GitHub tarde».

**e) Lo que está bien hecho.** El guardado de caché **es condicional**
(`whitelisted || pr.merged`), así que una versión que rompió se sigue
re-comprobando cada día. Ya hubo una entrada envenenada y se arregló bumpeando la
clave a `v2`. Esa parte del diseño es sólida y conviene no tocarla.

## 6 Prerrequisito para mover anclas: convergencia

`slsteam-moon.md` §5.2 M53 (D11.b del doc anterior) se retiró el 2026-09-01 con el argumento
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

## 7 Balance

> **SLSsteam invirtió en resolución. lumalinux invirtió en un feed.**

Su resolución funciona en un build **que nadie ha visto nunca**, porque lo deduce
todo del binario que tiene delante. La nuestra, cuando el patrón falla, necesita
que CI haya pasado por ahí, que haya red, y que el repo siga vivo. El feed es en
buena parte **compensación por una resolución frágil** — no un error, pero sí un
cálculo local convertido en servicio remoto.

Y la conclusión no pide inventar nada:

| Prioridad | Qué | Estado |
|---|---|---|
| 1 | **J** — que el walk-back falle cerrado | Bug propio, hoy · **cimiento del punto 3** |
| 2 | **K** — índice de vtable derivado por nombre en caliente (método de `download.lua`) | Falta **una** función: ranura → cadenas → índice. Todo lo demás ya existe |
| 3 | Subir el ancla de cadena a runtime en los otros cuatro hooks | Las cadenas ya están identificadas en `derive_patterns.py`; el código de referencia es `gmrc_xref.cpp` |
| 4 | Familia 3 (sitio de llamada) donde no haya cadena usable | **Requiere §7.6 primero** |

*No falta técnica: falta haberla puesto en primera fila.*

**Y esta tabla es la mitad de resolución del Accionable D** (§4.5), no una lista
paralela. D hace que un enganche compartido se pueda **nombrar**; J, K y el punto
3 hacen que se pueda **resolver** aunque no haya chuleta. Los dos casos de §5.1
—feed presente y feed ausente— se cierran uno con cada mitad.

Nota sobre el orden interno: **J antes que el punto 3**, porque de la cadena
`frase → GOT → sitio de uso → caminar atrás → entrada` sólo el último paso
depende del prólogo. Arreglado ahí una vez, los cuatro hooks que adopten el
ancla de cadena lo heredan. La alternativa, cuando caminar atrás no valga, ya
está escrita en `package_zero_finder.cpp` (el comentario sobre `DeriveGotBase`,
hoy hacia la línea 156): anclarse en el trozo de prólogo que
**sobrevive** al detour en vez de en el que lo recibe — resuelto en julio para
nuestro propio detour, y aplicable igual a uno ajeno **mientras el ajeno venga de
libmem** (§4.4: en i386 son 5 bytes por construcción de la librería, y eso cubre
todo lo que pase por `place_lua_hook`).

Fuera de ahí no cubre nada, así que J **no se escribe sobre esa premisa**: se
escribe con lista blanca —`E8 …` intacto, o una forma de desvío reconocida— y
**cero para todo lo demás**, más un `maxBack` acotado (hoy `0x8000`, absurdo para
la distancia entre el uso de una cadena y su entrada). Lo desconocido se rechaza;
no se acepta por parecerse.
