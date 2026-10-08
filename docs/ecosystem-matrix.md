# Matriz del ecosistema — qué hace cada programa, función por función, contra nosotros

Creada 2026-10-08, vacía a propósito: se rellena programa a programa conforme se releen. Un solo sitio para contestar "¿hay algo que ellos hacen y
nosotros no?" sin leerse los once docs de análisis (unas 19.000 líneas,
organizadas por programa y por fecha, no por función).

**Cómo se usa**

- §1 fija las doce funciones y, dentro de cada una, la lista de control: lo que
  hay que mirar en cada programa para dar la celda por completa.
- §2 es la matriz: una tabla por función, una fila por programa, "nosotros"
  siempre la primera. Cada celda dice **cómo** lo hace en pocas líneas y enlaza
  a la sección del doc del programa donde está el detalle y la medición.
  `n/a` = el programa no juega en esa función (y eso también es un dato).
  `?` = pendiente de relectura (§4).
- §3 es el registro de huecos: lo que ellos tienen y nosotros no, y al revés,
  cada uno con la decisión tomada (hecho, aparcado, descartado) y la fecha.
  Nada entra en §3 sin su fila en §2.
- §4 dice, por programa, qué doc lo cubre, cuándo se barrió por última vez y si
  ya se ha releído **por la matriz** (desde el código, función a función) o
  solo tiene los barridos antiguos.

**Regla de mantenimiento**: cada relectura o barrido rellena primero su columna
aquí y después añade el delta con fecha en el doc del programa. Un doc de
programa sin su columna al día no cuenta como analizado.

**Nosotros** = lumalinux (el `.so` y `setup.sh`) + SLSsteam (de AceSLS, tal como
lo configuramos) + LumaDeck (el plugin de Decky). Cada celda de "nosotros" dice
en cuál de los tres vive la función.

---

## §1 Las doce funciones y su lista de control

| # | Función | Lista de control (qué mirar en cada programa) |
|---|---|---|
| 1 | **Engancharse a Steam** | Cómo entra (LD_PRELOAD, LD_AUDIT, DLL, code cave, wrapper, flatpak). Cómo localiza lo que parchea (patrones, RTTI, RVAs fijos, feed remoto, xref de strings). Qué pasa cuando Steam actualiza (gate de hashes, SafeMode, se rompe, se apaga solo). Cómo se recupera (crash guard, kill switch, downgrade, logs). A qué procesos se limita. |
| 2 | **Propiedad y licencias** | Spoof de propiedad de apps. Inyección de packages o licencias y cuándo se reaplica. Qué hace con juegos que la cuenta posee de verdad. Family sharing. Depots compartidos (redists, 228980). |
| 3 | **DLC** | Criterio de unlock (todo, lista, por juego en marcha). DLC comprado de verdad. DLC sin depot (solo flag). Límites (los 64 de Steam). Cómo se quita. |
| 4 | **Claves y manifests** | De dónde salen las claves y dónde se guardan. De dónde salen los manifests (hubs con clave, generadores con sesión, archivos públicos, propios) y en qué orden. Códigos de petición de manifest: proveedores, cuota, qué pasa si mueren. Qué deja de funcionar si cae cada servicio. |
| 5 | **Updates de juegos** | Pins o congelación. Actualización automática y cómo decide. Cambio de versión a un build antiguo. Qué pasa con una update que Steam ya empezó. |
| 6 | **Fixes y DRM** | Denuvo (fuentes, cuenta). Steamless. Goldberg u otros emuladores. EOS. Cómo se aplican y se quitan. |
| 7 | **Logros, stats y tiempo de juego** | Dónde se guardan. Si sincronizan entre máquinas y por dónde. Convivencia con el unlocker (quién contesta GetUserStats). Esquemas de logros. |
| 8 | **Cloud saves** | Redirección y proveedores. Convivencia con Steam Cloud. |
| 9 | **Añadir y quitar un juego** | Entradas: appid, nombre, página de tienda, zip, lua suelto. Qué escribe al añadir. Qué limpia al quitar y qué deja. Varias bibliotecas y SD. Nombre de carpeta. |
| 10 | **Credenciales y proveedores** | Qué cuentas o claves exige, cómo se obtienen, caducidad y cómo la detecta, qué funciona sin ninguna. |
| 11 | **Mantenimiento propio** | Superficie (QAM, escritorio, CLI, web). Update de sí mismo. Update de componentes y cómo sabe la versión instalada. Salud, estado, logs legibles. |
| 12 | **Proyecto** | Plataformas. Código abierto, licencia, quién mantiene, ritmo. Servicios de terceros de los que depende. Telemetría, privacidad, riesgo de detección. |

---

## §2 La matriz

Programas (columna de §4 para el doc de cada uno): **nosotros**, SLSsteam,
SteaMidra/SFF, LumaCore, LuaTools + BetterSteamTools, ASSella, SLSDeck,
SteamFlipper, slsteam-moon, plugins de SLSsteam, CloudRedirect.

### 1. Engancharse a Steam

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | ? | |
| SLSsteam | ? | |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 2. Propiedad y licencias

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | ? | |
| SLSsteam | ? | |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 3. DLC

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | ? | |
| SLSsteam | ? | |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 4. Claves y manifests

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | ? | |
| SLSsteam | ? | |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 5. Updates de juegos

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | ? | |
| SLSsteam | ? | |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 6. Fixes y DRM

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | ? | |
| SLSsteam | ? | |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 7. Logros, stats y tiempo de juego

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | ? | |
| SLSsteam | ? | |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 8. Cloud saves

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | ? | |
| SLSsteam | ? | |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 9. Añadir y quitar un juego

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | ? | |
| SLSsteam | ? | |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 10. Credenciales y proveedores

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | ? | |
| SLSsteam | ? | |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 11. Mantenimiento propio

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | ? | |
| SLSsteam | ? | |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

### 12. Proyecto

| Programa | Cómo | Fuente |
|---|---|---|
| **nosotros** | ? | |
| SLSsteam | ? | |
| SteaMidra/SFF | ? | |
| LumaCore | ? | |
| LuaTools + BST | ? | |
| ASSella | ? | |
| SLSDeck | ? | |
| SteamFlipper | ? | |
| slsteam-moon | ? | |
| plugins SLSsteam | ? | |
| CloudRedirect | ? | |

---
## §3 Registro de huecos

Cada entrada nace de una celda de §2. Estado: **hecho**, **aparcado** (con la
condición para desaparcarlo), **descartado** (con el motivo), **pendiente**.
Se rellena cuando la relectura de un programa termina, no antes.

### 3.1 Ellos sí, nosotros no

| Función | Programa | Qué | Decisión | Fecha |
|---|---|---|---|---|

### 3.2 Nosotros sí, ellos no

| Función | Qué | Quién más lo tiene | Notas |
|---|---|---|---|

### 3.3 Fuentes retiradas

| Función | Qué | Cuándo | Motivo |
|---|---|---|---|

---

## §4 Estado de relectura por programa

"Barrido" = delta por fechas (lo que había hasta ahora). "Relectura por la
matriz" = desde el código actual, las doce funciones, y contraste con el doc
viejo: lo que el doc afirma y el código no sostiene se marca o se borra.
Una celda solo se rellena cuando la relectura la ha sacado del código y lo
dice en su columna Fuente; hasta entonces es `?`.

| Programa | Doc | Último barrido | Relectura por la matriz | Orden propuesto |
|---|---|---|---|---|
| SLSsteam | `slssteam-analysis.md` | 2026-10-07 | pendiente | 1 |
| SteaMidra/SFF (drappula) | `steamidra-linux-analysis.md` | 2026-10-07 | pendiente | 2 |
| LumaCore (drappula) | `lumacore-findings.md` | 2026-10-07 | pendiente | 3 |
| LuaTools + BetterSteamTools (+ OpenSteamTool) | `luatools-app-analysis.md`, `bettersteamtools-findings.md` | 2026-10-07 | pendiente | 4 |
| ASSella | `assella-analysis.md` | 2026-10-07 | pendiente | 5 |
| SLSDeck | `slsdeck-analysis.md` | 2026-10-05 | pendiente | 6 |
| SteamFlipper | `steamflipper-analysis.md` | 2026-09-28 | pendiente | 7 |
| slsteam-moon | `slsteam-moon-findings.md` | 2026-10-07 | pendiente | 8 |
| plugins de SLSsteam | `slssteam-plugins-analysis.md` | 2026-09-29 | pendiente | 9 |
| CloudRedirect | `cloudredirect.md` | 2026-10-07 (2.6.6) | pendiente | 10 |
| nosotros | `RESEARCH.md`, `owned-games-guide.md`, docs de LumaDeck | — | pendiente | 0 |
