# Juegos owned en LumaDeck: piezas y flujos

Guía de lo que interviene cuando LumaDeck añade o quita DLC a un juego que la
cuenta ya tiene (juego *owned*), y de cómo se diferencia de un juego *added*
(el juego entero añadido por LumaDeck). Todo lo que se afirma aquí está medido
en el codespace SteamOS el 2026-10-06 con Darkest Dungeon (262060), salvo
donde se dice "no medido".

Vocabulario:

- **Juego owned**: la cuenta tiene licencia del juego base. LumaDeck solo le añade DLC.
- **Juego added**: la cuenta no tiene licencia. LumaDeck añade el juego entero (base + DLC).
- **Depot**: un paquete de archivos de Steam. Un juego tiene varios (base Windows, base Linux, cada DLC por plataforma...).
- **Manifest**: la lista de archivos de un depot en una versión concreta (gid).

---

## 1. Las piezas

### 1.1 Lado Steam

**Licencias.** Lo que la cuenta tiene. Steam las da por *paquetes* (un paquete
agrupa apps). Al iniciar sesión el servidor manda la lista de paquetes de la
cuenta y el cliente la cachea en `appcache/packageinfo.vdf` (paquete → apps).
El paquete 0 es el que tienen todas las cuentas (juegos gratis). "Your stuff"
en la ficha sale de aquí.

Lo que deciden las licencias: **qué puedes descargar**. Lo que NO deciden:
qué tienes instalado. Perder una licencia no quita nada del disco. Eso es
comportamiento de Steam, no nuestro (un reembolso de DLC deja los archivos).

**appinfo.** La descripción del juego que manda Valve: qué depots tiene, a qué
DLC pertenece cada depot (`dlcappid`), para qué sistema operativo, qué
manifest (gid) es el actual de cada depot.

**`appmanifest_<appid>.acf`.** El estado de instalación de un juego. Lo
escribe Steam y solo Steam. Lo que importa dentro:

- `InstalledDepots`: qué depots hay instalados y con qué manifest.
- `UserConfig` / `MountedConfig`: lo que el usuario eligió: idioma, beta y
  `DisabledDLC`, la lista de DLC que el usuario desmarcó en Propiedades → DLC.
- `StateFlags`: instalado, actualizando, etc.

Esto decide **qué tienes instalado**.

**`depotcache/<depot>_<gid>.manifest`.** La lista de archivos de cada depot
instalado, con tamaños y hashes. Steam lo usa para verificar, para calcular
deltas al actualizar y **para borrar los archivos de un depot que deja de
estar**. Sin ese manifest Steam no sabe qué archivos eran de ese depot y no
borra nada (medido: `0 deleted files`). Steam nunca limpia los manifests
viejos; se quedan ahí.

**Claves de depot en `config/config.vdf`.** Steam cachea en `DecryptionKeys`
la clave de cada depot que ha visto. steamidra escribe ahí las nuestras al
añadir, pero Steam no relee el archivo mientras corre, y lo que tiene en
memoria no es de fiar en ningún sentido (a las 10:14 las claves volvieron
tras un reinicio; a las 14:51 no estaban ni en disco ni en memoria). Desde
el 2026-10-06 LumaDeck no edita este archivo al desinstalar. Sin clave:
"Missing decryption key" y el update se cancela.

**La clave hace falta para borrar.** Los manifests de depotcache llevan los
nombres de archivo cifrados con la clave del depot. Quien borre los archivos
de un DLC, Steam o quien sea, necesita la clave en ese momento (medido a las
14:51: Steam desmarcó el DLC, fue a borrar, la clave ya no estaba, y dejó el
juego en "Update required" reintentando cada 5 minutos; en cuanto la clave
volvió, borró los 4242 archivos al instante).

**Los archivos del juego** en `steamapps/common/<juego>/`. Steam los crea y
los borra siguiendo sus manifests. Un archivo que no está en ningún manifest
suyo es invisible para él: ni lo verifica, ni lo borra, ni lo reutiliza en
una reinstalación (medido: volvió a bajar 861 MB con los archivos ya en disco).

**El cálculo de depots.** Cada vez que Steam "planifica" un juego instalado
calcula los depots que debería tener:

    objetivo = depots de appinfo
             ∩ los que la cuenta tiene licencia (base o su DLC)
             ∩ los del sistema operativo
             − los DLC en DisabledDLC

y lo compara con `InstalledDepots`:

- objetivo tiene uno que no está instalado → `added depots`, lo descarga.
- instalado hay uno que no está en objetivo → `removed depots`, borra sus
  archivos con el manifest de depotcache.

Cuándo planifica: al arrancar Steam, al instalar, al verificar, y **cuando
cambia la configuración del usuario** (idioma, beta, casilla de DLC). Cuándo
NO: cuando aparece o desaparece una licencia con Steam abierto. Por eso
añadir licencia en caliente no baja nada (medido dos veces) y quitarla no
borra nada (medido, ni en caliente ni al reiniciar).

**`SteamClient.Apps.SetDLCEnabled(app, dlc, bool)`.** La casilla de cada DLC
en Propiedades → DLC, llamada desde el frontend. `false` añade el DLC a
`DisabledDLC` y Steam planifica: `removed depots`, borra los archivos.
`true` lo quita de `DisabledDLC` y Steam planifica: `added depots` si hay
licencia, nada si no la hay. **Solo planifica si cambia algo**: un `true`
sobre un DLC que no estaba desmarcado no hace nada (medido a las 12:43).

### 1.2 Lado nuestro

**`~/.config/lumalinux/retired_keys.txt`.** Mismo formato que keys.txt.
Claves que lumalinux **sirve** si Steam las pide pero que **no** cuentan como
licencia (no se inyectan en el paquete 0, no se listan como nuestras). Es lo
que hace Steam con sus propias claves: guardarlas para siempre. LumaDeck
mueve aquí las líneas de los DLC de un owned al desinstalar, y las quita
cuando el DLC se vuelve a añadir y la línea regresa a keys.txt. Solo
`KeyStore::Lookup` lo lee; se carga con keys.txt y en cada cambio de
cualquiera de los dos.

**`~/.config/lumalinux/keys.txt`.** Una línea por depot:
`depot;parent;gid;tamaño;clave`. lumalinux hace dos cosas con ella: sirve la
clave a Steam cuando la pide, y mete los AppIDs en el vector de apps del
paquete 0 del cliente, que es como el cliente ve "tengo licencia de esto".
Al añadir una línea lo inyecta en caliente y dispara un *reconcile*
(aviso de licencias cambiadas). Al quitar una línea, desde el 2026-10-07
(`4f8579c`), **quita del vector** los ids que él mismo metió y lanza el
mismo reconcile: el planificador de Steam deja de ver la licencia al
momento (medido: un verify tras el uninstall ya no baja nada; antes volvía
a bajar los DLC enteros). Lo que no se actualiza es la caché de propiedad
de la interfaz: "Your stuff" sigue listando los DLC hasta reiniciar Steam.
Cosmético.

**`AdditionalApps` en `~/.config/SLSsteam/config.yaml`.** Las apps que
SLSsteam declara como propiedad de la cuenta en el cliente (launch, "Your
stuff"). En un juego added va el AppID del juego. En un juego owned van
solo los AppIDs de los DLC; el juego base no, porque ya es tuyo.

**`config/stplug-in/<appid>.lua`.** La receta que vino del zip:
`addappid(base)`, `addappid(dlc)`, `addappid(depot, 1, "clave")`,
`setManifestid(...)`. Es la fuente de verdad de "qué añadió LumaDeck a este
juego". En owned el lua solo lleva el base sin clave y los depots de DLC.

**`~/.local/share/lumadeck/manifests/<appid>/`.** Copia de los manifests del
zip, por si hay que reinstalar sin volver a pedirlos.

**Markers de ACCELA.** Ya no existen. steamidra escribía `.DepotDownloader`
dentro de la carpeta del juego y `~/.local/share/ACCELA/depots/<appid>.depot`
para que ASSella listara los juegos como suyos; en un juego owned marcaban la
carpeta de un juego legítimo, y usar las dos herramientas a la vez no está
soportado. El uninstall limpia los que dejara una versión anterior.

**`pins.json`.** Por juego: `owned: true` cuando LumaDeck lo añadió como
owned, y los pins de versión. `owned: true` no pinea nada: es solo la marca
"este juego lo añadí como DLC-only", para que cualquier operación posterior
(re-add, update, fix) mantenga esa forma. Los pins de versión (congelar un
depot a un manifest) solo existen con el modelo gmrc "down", y solo sobre
depots que están en keys.txt: en un juego added son todos; en un owned son
solo los depots de DLC que añadimos. Los depots del juego base y los de los
DLC que la cuenta tiene nunca se pinean, porque nunca están en keys.txt.

**`steam_licenses.py`.** Lee `packageinfo.vdf` y responde "¿la cuenta tiene
licencia de este AppID?". Es como LumaDeck decide owned/added al añadir
(`resolve_owned`): si ya lo gestionamos como added, sigue added; si pins dice
owned, sigue owned; si no, lo que diga packageinfo.

**SLSsteam dentro del juego.** Cuando el juego corre y pregunta "¿tiene el
usuario este DLC?", SLSsteam dice que sí a cualquier DLC que la cuenta no
tenga. Consecuencia: si los archivos del DLC están en disco, el DLC
funciona aunque no haya licencia. Por eso desinstalar un DLC owned tiene que
borrar los archivos de verdad; quitar la licencia no basta.

---

## 2. Lo que toca cada operación

| | lua | keys.txt | AdditionalApps | pins | depotcache | config.vdf claves | .acf | archivos | SetDLCEnabled |
|---|---|---|---|---|---|---|---|---|---|
| Add added | escribe | base+DLC | base | pin si "down" | escribe manifests | escribe | no (Steam al instalar) | no (Steam) | no |
| Add owned | escribe (sin claves base) | solo DLC añadidos | solo DLC añadidos | owned:true (+pin de depots DLC si "down") | escribe manifests | escribe | no | no (Steam) | false+true si instalado |
| Uninstall added | borra | borra | quita base | borra | borra manifests | borra | borra | borra carpeta | no |
| Uninstall owned | borra | borra | quita DLC | borra | **no toca** | **no toca** | **no toca** | **no toca** (Steam) | false por DLC |

Lo de "no toca" en uninstall owned es la diferencia clave: depotcache,
config.vdf, `.acf` y archivos son de Steam, y es Steam quien los cambia
cuando le desmarcamos el DLC.

---

## 3. Flujos

### A. Add de un juego added (sin cambios)

Igual que siempre. Zip → lua completo → steamidra escribe claves de todos
los depots, el AppID en AdditionalApps, manifests en depotcache, claves en
config.vdf → el usuario da a Install → Steam planifica con la licencia
inyectada y baja base y DLC.

### B. Add owned, juego NO instalado

1. LumaDeck ve en packageinfo que el juego es tuyo → `owned=True`.
2. Del lua se quitan las claves de los depots del base (son tuyos, Steam ya
   tiene clave) y se dejan los depots de DLC. Se queda `addappid(base)` sin
   clave para que steamidra sepa de qué juego es.
3. steamidra escribe keys.txt solo con depots de DLC, los AppIDs de los DLC
   en AdditionalApps, el `.acf` sin tocar. `pins.json` → `owned: true`.
4. lumalinux inyecta los DLC en el paquete 0 y lanza el reconcile.
5. El usuario da a Install. Steam planifica desde cero, ve la licencia
   inyectada y baja base (con sus claves reales) y DLC (con las nuestras).
   Medido a las 09:47: 8 depots, lumalinux sirvió solo las 4 claves de DLC.

Sin reinicio, sin `SetDLCEnabled`.

### C. Add owned, juego YA instalado

Pasos 1 a 4 iguales. Pero Steam no planifica un juego instalado mientras
corre por una licencia que no le llega del servidor:

- Licencia inyectada + reconcile, en caliente: nada (12:12, 12:43).
- Lanzar el juego: nada (2026-10-07 06:10: `App Running`, sin plan, sin DLC).
- Abrir su página: nada.
- **Arrancar Steam**: planifica todos los instalados contra sus licencias y
  baja los DLC solos (12:20, `config changed: added depots`, 861 MB).

Así que la regla es una: **los DLC se instalan la próxima vez que arranque
Steam**, o antes si el usuario verifica los archivos del juego desde su
página (medido 07:36: el verify planifica con la licencia y los baja). La tarjeta lo dice al terminar el add. Lo único que LumaDeck hace
además es marcar una vez la casilla de cada DLC (`SetDLCEnabled(true)`, sin
esperar nada): si un uninstall anterior los dejó desmarcados
(`DisabledDLC`), el plan de arranque los respetaría y no bajaría nada
(16:53). Si el usuario los quiere antes, Propiedades → DLC del juego y
marcar: con la licencia puesta baja al momento.

**Lo que se probó y se quitó** (2026-10-06, commits `9001216`…`beff30b`):
forzar la descarga sin reiniciar con el ciclo `false`→`true` de la casilla,
que replanifica. Funcionó 5 de 5 cuando el juego estaba "despierto" y Steam
había digerido la licencia (señal: `RequestAppInfoUpdate` de los DLC +
`UpdatesJob: finished OK` en `appinfo_log.txt`), y falló cuando el add caía
en los primeros segundos tras arrancar Steam, antes de que el inicio hubiera
pintado el juego (20:29: la casilla cambió, Steam no escribió ni `user
config changed`; abrir la página y repetir el ciclo a las 20:36 sí bajó), y
una vez en que Steam ignoró el aviso (18:47). Cada condición era otra capa
(señal, despertar con `RegisterForAppDetails`, reintentos) sobre un camino
que Steam no ofrece. Se dejó la regla del arranque, que es determinista.

### D. Uninstall owned

1. El frontend pide al backend la lista (`owned_dlc_to_disable`): los
   AppIDs de DLC del lua, si el juego está instalado y si está en marcha.
2. **Guarda**: por cada DLC el backend mira packageinfo. Si la cuenta ya tiene
   licencia real de ese DLC (lo compró después), ese DLC no va en la lista:
   solo se limpian nuestras líneas, que sobran.
3. El frontend llama `SetDLCEnabled(base, dlc, false)` por cada DLC de la
   lista. Si Steam no acepta alguna, se para ahí con un aviso y **no se toca
   nada**. Si acepta, Steam planifica: `removed depots`, borra los archivos
   con el manifest de depotcache, reescribe el `.acf` (4242 archivos en 2 s,
   medido a las 12:26). Deja las carpetas vacías, como con cualquier DLC
   desmarcado.
4. El frontend llama al backend con `steam_dlc_disabled=True`. Sin esa
   confirmación, un owned instalado con DLC se rechaza. El backend **retira**
   las líneas de keys.txt (las mueve a retired_keys.txt: la clave sigue
   disponible, la licencia no), quita los AppIDs de DLC de AdditionalApps y
   `owned` de pins.json, y borra el lua **lo último**, para que un reintento
   tras un fallo a mitad todavía sepa qué DLC eran. Nada más. Con el juego
   en marcha se rechaza.

No hay espera, y Steam puede ejecutar el borrado cuando quiera (un segundo
después, cinco minutos después, tras un reinicio): la clave está en
retired_keys.txt. Lo que Steam necesita de nosotros para ese borrado son dos
cosas, y las dos se quedan: **los manifests de depotcache** (borrarlos antes
fue lo que dejó 1 GB huérfano a las 10:01) y **la clave** (quitarla con la
licencia fue lo que dejó el juego en "Update required" a las 14:51).
config.vdf no se toca para ningún juego: no servía de nada.

Lo que queda después:
- `DisabledDLC` con esos DLC en el `.acf`. Sin licencia no hace nada.
  Desaparece sola en el siguiente add owned (paso C.5) o si el usuario
  compra el DLC y lo marca. Para que no quedara ni eso, lumalinux tendría
  que quitar la licencia del vector en caliente; hoy no lo hace.
- "Your stuff" sigue listando los DLC hasta reiniciar Steam (caché de la
  interfaz). El planificador ya no los ve como tuyos: un verify o marcar la
  casilla no baja nada (medido 2026-10-07 08:10).

### E. Uninstall added

Igual que siempre: borra carpeta, `.acf`, compatdata opcional, shadercache
(como el uninstall de Steam), manifests, lua, claves, AdditionalApps, pins.
Sin `.acf`, la carpeta solo se busca por nombre exacto (installdir de appinfo
o nombre del juego); nunca por prefijo ni por appid en el nombre.

El juego sigue en la biblioteca hasta reiniciar Steam (medido 09:59). Al
añadir sí aparece en caliente porque SLSsteam emite `AppLicensesChanged_t`
solo para las apps **nuevas** en el diff de su config (`146e28f`, ver
`slssteam-analysis.md` §7.7.7); al quitar no emite nada, y el
`LicensesUpdated_t` de lumalinux (que sí se emite: `- AppIdVec` ×5,
`Reconcile: broadcast`) no hace que la interfaz suelte la entrada. Es la
capa de SLSsteam; LumaDeck no tiene forma de forzar esa lista.

### F. Compras el juego que tenías added

Steam: nada cambia en la instalación. El `.acf` y los depots son los
mismos. En la siguiente actualización Steam baja la build real con sus
claves reales, salvo que haya pin.

LumaDeck: hoy no se entera. Sigue tratándolo como added: lo actualiza por
zips y, si gmrc.json está en "down", le pone pin de manifests por SLSsteam,
que congela un juego ya legítimo. Pendiente ("added → owned"): detectar en
packageinfo que un juego gestionado ya tiene licencia y convertirlo: base
fuera de AdditionalApps, claves y pins de depots base fuera, DLC se quedan
como added, `owned: true`. La instalación no se toca.

### G. Compras un DLC que tenías added (juego owned)

Steam: nada. Ya estaba instalado. Ahora con licencia y clave reales. No se
desmarca nada: `DisabledDLC` solo lo escribe un uninstall nuestro.

LumaDeck: la guarda de D.2 evita que un uninstall posterior desmarque y
borre un DLC que ya es tuyo. Falta lo mismo que en F para limpiar nuestras
líneas sin que el usuario pase por uninstall.

### H. El usuario desmarca el DLC desde Steam (Propiedades → DLC)

Steam borra los archivos y pone `DisabledDLC`. Nuestra licencia y líneas se
quedan. Si luego marca el DLC otra vez, Steam lo baja (hay licencia
inyectada). Si hace uninstall en LumaDeck, D.3 no encuentra nada que borrar
y D.4 limpia lo nuestro. Coherente, nada que hacer.

### I. Actualización de un juego owned

El base lo actualiza Steam con sus claves, sin nosotros. Los depots de DLC
también los actualiza Steam, pero necesita la clave: si el DLC cambia de
depot o aparece uno nuevo, LumaDeck en su pasada de updates pide el zip
nuevo y renueva solo líneas de depots de DLC (`check_update` filtra por
`dlcappid`). Un pin, si lo hay, solo toca esos depots de DLC. No medido todavía: no ha salido ninguna
actualización de Darkest Dungeon desde el add.

---

## 4. Medidas del 2026-10-06 (Darkest Dungeon, codespace SteamOS)

| Hora | Acción | Steam |
|---|---|---|
| 09:47 | Add owned + Install (juego no instalado) | `added depots` base+DLC, lumalinux sirvió 4 claves |
| 10:01 | Uninstall owned (versión vieja: borró manifests de depotcache) | nada; depots siguen montados |
| 10:11 | Verify sin claves en config.vdf | `Missing decryption key` |
| 10:15 | Reinicio sin licencia + verify | 8 depots: quitar licencia no quita depots |
| 10:23 | `SetDLCEnabled(false)` sin manifests en depotcache | `removed depots`, `0 deleted files`: 1 GB huérfano |
| 12:12 | Add owned, juego instalado, Steam abierto | nada |
| 12:20 | Reinicio con licencia | `config changed: added depots`, baja 861 MB, no reutiliza huérfanos |
| 12:26 | `SetDLCEnabled(false)` con manifests | `4242 deleted files`, 4 depots, carpetas vacías |
| 12:31 | `SetDLCEnabled(true)` con licencia inyectada rancia y marca puesta | `added depots` al instante |
| 12:43 | Add owned en caliente + `true` sin marca | nada (no cambió config) |
| 12:48 | `false` + `true` con licencia en caliente | `added depots`, baja 861 MB |
| 14:51 | Uninstall owned (primera versión): `false` y clave quitada en el mismo segundo | `removed depots`, luego `Missing decryption key`, "Update required", reintento cada 5 min, archivos intactos |
| 14:58 | Clave de vuelta (re-add) | el reintento borra 4242 archivos al instante |
| 16:45 | Add owned instalado, ciclo lanzado en el mismo segundo que el reconcile | marca limpiada, nada añadido: el `true` llegó antes que la licencia |
| 16:52 | `liblumalinux.so` copiado encima del que Steam tenía cargado | Steam vuelca (`assert_…dmp`) al pasar por el hook; la medida de ese minuto no vale. El `.so` solo se reemplaza con Steam parado |
| 17:03 | `true` con la licencia asentada (18 min) | `added depots`, baja 861 MB |
| 17:16 | Uninstall owned (versión final): `false`, claves retiradas | Steam pide la clave, lumalinux la sirve desde retired_keys.txt, borra los archivos, 4 depots |
| 17:19 | Add owned instalado: ciclo 1 s tras el reconcile | `added depots`, baja 861 MB |
| 18:47 | Ciclo 26 ms tras el reconcile, Steam de 2 min | nada; Steam no procesó el aviso (sin `OnAppLicensesChanged`) |
| 19:07 | Add tras reiniciar Steam; `reconcile.json` viejo hizo esperar los 20 s de tope | `added depots` a los 20 s, baja |
| 19:25, 19:32, 19:37 | Uninstall owned (claves retiradas) | Steam borra en 2-7 s, 3/3 |
| 19:28, 19:32, 19:37 | Add owned instalado, desde la principal, con el menú cerrado y desde la página del juego | `added depots` 1 s tras el reconcile, 3/3; `RequestAppInfoUpdate` + `UpdatesJob: finished OK` en ese segundo |
| 20:25 | Add owned instalado, ciclo tras la señal de `appinfo_log` | `added depots` 1 s después |
| 20:29 | Add 42 s tras arrancar Steam, juego sin pintar en el inicio | casilla cambiada, Steam sin plan; a las 20:36 tras abrir la página, el mismo ciclo baja |
| 20:53 | Steam de 5 min, add sin tocar el juego, luego abrir la página y ciclar | sin página: nada; con página: `added depots` |
| 20:55, 21:00, 21:04 | Arranques con el juego en el inicio | Steam carga sus stats a los 3-7 s: "despierto" |
| 2026-10-07 06:10 | Licencia inyectada, lanzar el juego | `App Running` sin plan; sin DLC |
| 2026-10-07 06:39 | Add owned en juego instalado, código final (licencia + `true` suelto) | marca limpia, Steam sin tocar nada |
| 2026-10-07 06:42 | Reinicio de Steam | `config changed: added depots`, 861 MB a los pocos segundos del arranque |
| 2026-10-07 06:45 | Uninstall owned, código final | `removed depots`, borrado con la clave retirada en 5 s, 4 depots |
| 2026-10-07 07:20 | `true` tras un uninstall, sin reiniciar, vector append-only | Steam vuelve a bajar los DLC enteros (licencia rancia en el vector, manifests en depotcache, clave retirada) |
| 2026-10-07 07:36 | Add owned en juego instalado + verify desde la página | el verify planifica con la licencia y baja los DLC |
| 2026-10-07 07:47 | Arranque con marca puesta, sin licencia, claves retiradas (uninstall "dormido" simulado) | `config changed: removed depots`, borra con la clave retirada: el fallback del uninstall es el siguiente arranque |
| 2026-10-07 08:10 | Uninstall con el `erase` del vector (`4f8579c`) y verify después | `- AppIdVec` ×12, reconcile; verify: `4 target`, nada baja. "Your stuff" sigue hasta reiniciar |
| 2026-10-07 09:59 | Uninstall added (Brotato) sin reiniciar | lumalinux: watcher, `- AppIdVec` ×5, `Reconcile: broadcast`; SLSsteam: "Config reloaded!" ×6 sin callback (solo lo emite para apps nuevas). Fuera: carpeta, `.acf`, lua, claves, AdditionalApps. Quedan: compatdata (sin marcar), shadercache (ahora se borra), archivo y zip propios (por diseño), librarycache (de Steam). El juego sigue en la biblioteca |
