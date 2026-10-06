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
la clave de cada depot que ha visto. Las tiene en memoria y las vuelve a
escribir al cerrar, así que editarlas con Steam abierto no sirve (medido).
Sin clave: "Missing decryption key" y el update se cancela.

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

**`~/.config/lumalinux/keys.txt`.** Una línea por depot:
`depot;parent;gid;tamaño;clave`. lumalinux hace dos cosas con ella: sirve la
clave a Steam cuando la pide, y mete los AppIDs en el vector de apps del
paquete 0 del cliente, que es como el cliente ve "tengo licencia de esto".
Al añadir una línea lo inyecta en caliente y dispara un *reconcile*
(aviso de licencias cambiadas). Al quitar una línea **no quita nada del
vector**: la licencia inyectada sigue viva hasta que Steam reconstruye el
paquete 0, es decir, hasta reiniciar Steam. Hoy solo añade.

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

Pasos 1 a 4 iguales. Pero Steam no planifica solo, así que:

5. El frontend llama `SetDLCEnabled(base, dlc, false)` y luego `true` por
   cada DLC añadido. El `false`→`true` es un cambio de configuración, Steam
   planifica, ve la licencia y baja los DLC. Medido a las 12:48: `added
   depots`, 861 MB. De paso, el `true` limpia cualquier `DisabledDLC` viejo
   de esos DLC.

Alternativa sin API: reiniciar Steam, que también planifica al arrancar
(medido a las 12:20). El ciclo evita el reinicio.

### D. Uninstall owned

1. LumaDeck lee del lua los AppIDs de DLC que añadió.
2. **Guarda**: por cada DLC mira packageinfo. Si la cuenta ya tiene licencia
   real de ese DLC (lo compró después), ese DLC no se toca: solo se limpian
   nuestras líneas, que sobran.
3. El frontend llama `SetDLCEnabled(base, dlc, false)` por cada DLC nuestro.
   Steam planifica: `removed depots`, borra los archivos con el manifest de
   depotcache, reescribe el `.acf` (4242 archivos en 2 s, medido a las
   12:26). Deja las carpetas vacías, como con cualquier DLC desmarcado.
4. El backend borra lua, líneas de keys.txt, AppIDs de DLC de
   AdditionalApps, y `owned` de pins.json. Nada más.

No hay espera: lo que borramos en 4 no lo necesita Steam para 3. Lo único
que Steam necesita es que **no le hayamos borrado los manifests de
depotcache**, que es lo que pasó el 2026-10-06 a las 10:01 y dejó 1 GB
huérfano.

Lo que queda después:
- `DisabledDLC` con esos DLC en el `.acf`. Sin licencia no hace nada.
  Desaparece sola en el siguiente add owned (paso C.5) o si el usuario
  compra el DLC y lo marca. Para que no quedara ni eso, lumalinux tendría
  que quitar la licencia del vector en caliente; hoy no lo hace.
- La licencia inyectada sigue en el cliente hasta reiniciar Steam ("Your
  stuff" la muestra hasta entonces). No afecta a nada: no hay archivos ni
  depots, y el juego, por SLSsteam, no verá el DLC porque no hay archivos.

### E. Uninstall added (sin cambios)

Igual que siempre: borra carpeta, `.acf`, compatdata opcional, manifests,
lua, claves, AdditionalApps, pins.

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
