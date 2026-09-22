#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# experiment_cache_idiom.py — ¿el idiom de acceso a la caché resuelve ÚNICO?
#
# ESTADO (2026-09-08): los dos defectos descritos abajo ESTAN ARREGLADOS en el
# runtime. FindCacheGlobalDisp cuenta disp32 distintos y devuelve 0 salvo que
# haya exactamente uno (ambiguo == no resuelto, igual que
# Patterns::FindUniqueInSteamclient), y exige el encadenado de registros. Se
# deja el texto original porque explica POR QUE los predicados son los que son.
# La sonda sigue viva y es el primer paso del diagnostico de docs/maintenance.md
# §C: mide lo mismo que el runtime, pero sobre un .so estatico y en un segundo.
#
# El problema que motivo todo esto (src/hooks/package_zero_finder.cpp,
# FindCacheGlobalDisp)
# --------------------------------------------------------------------
# El finder localiza la ranura global de CPackageInfoCache buscando el idiom
#
#     lea r1,[GOT+X] ; mov r2,[r1] ; mov r3,[r2+0xc58]
#
# y sacando la X del disp32 del lea. Dos defectos, y el segundo causa el primero:
#
#   A) Si aparecen varios sitios con disp32 DISTINTOS, se queda con el primero
#      (el de menor dirección) y sigue. El comentario dice "la caché real es con
#      diferencia la más común", pero el código no coge la más común: coge la
#      primera. Y con esa X construye cacheGlobal, lee la ranura e INYECTA. Es
#      escribir en la memoria de Steam a partir de un número adivinado — el mismo
#      defecto que se quitó de DepotKey y GMRC con FindUniqueInSteamclient.
#      (Detalle: las dos ramas del else son idénticas, así que la distinción
#      "ambiguo" está documentada pero no existe.)
#
#   B) El reconocimiento comprueba los campos mod de los tres ModRM y descarta
#      los rm con SIB/disp32 puro —lo justo para fijar los tamaños 6+2+6— pero
#      NO comprueba que las tres instrucciones estén encadenadas:
#          reg(lea) == rm(mov1)   y   reg(mov1) == rm(mov2)
#      ni que la base del lea sea el registro del GOT. Tres instrucciones sin
#      relación pasan el filtro si casan las formas. Ejemplo aceptado hoy:
#          8D B0 xx xx xx xx   lea esi,[eax+disp32]
#          8B 39               mov edi,[ecx]
#          8B 82 58 0C 00 00   mov eax,[edx+0xc58]
#
# Qué mide esta sonda
# -------------------
#   1. Cuántos sitios casan con los predicados EXACTOS del runtime.
#   2. Cuántos disp32 distintos hay entre ellos  -> ¿UNIQUE o AMBIGUOUS hoy?
#   3. En cuántos de esos sitios se cumple el encadenado de registros
#      -> ¿bastaría el encadenado para dejarlo en UNIQUE?
#   4. La cadena completa en estático: deriva el GOT igual que DeriveGotBase
#      (cola del prólogo de GMRC + `add eax,imm32`), calcula GOT+X para cada
#      candidato y dice en qué sección cae. El bueno debe caer en datos
#      escribibles (.bss / .data); un falso positivo caerá en cualquier parte.
#
# Uso:  python3 tools/experiment_cache_idiom.py /ruta/a/steamclient.so

import struct
import sys

# Una fila por layout CONOCIDO de CPackageInfoCache: (nombre, offset de la raíz,
# offset del array de nodos). Espejo de kCacheLayouts en
# src/hooks/package_zero_finder.cpp y de CACHE_LAYOUTS en check_patterns.py /
# derive_patterns.py; las cuatro copias cambian juntas (test_cache_idiom.py).
# 2026-09-22: la beta 9cf4720f desplazó todos los campos del objeto 0x338.
# Cuando NINGUNA fila resuelve, el paso siguiente es experiment_cache_idiom_free.py
# (busca la raíz con el offset libre y casa los campos entre builds).
CACHE_LAYOUTS = [
    ("stable-0xc58", 0xC58, 0xC6C),
    ("beta-0xf90",   0xF90, 0xFA4),
]
GMRC_PROLOGUE_TAIL = bytes.fromhex(
    "55 89 E5 57 56 53 81 EC 10 01 00 00 8B 7D 08 8B 4D 20".replace(" ", ""))
GMRC_TAIL_WILDCARD = (8, 9)     # los dos bytes del tamaño de frame: cualquier valor
REG32 = ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"]


# ── ELF32 ────────────────────────────────────────────────────────────────────

def read_elf(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF":
        raise ValueError("no es un ELF")
    if data[4] != 1 or data[5] != 1:
        raise ValueError("no es ELF32 little-endian (steamclient.so es de 32 bits)")
    return data


def exec_segments(data):
    """[(vaddr, bytes)] de cada PT_LOAD ejecutable. Como es un objeto compartido
    (base 0), vaddr ES la RVA base: un match en el offset o tiene RVA vaddr+o."""
    e_phoff = struct.unpack_from("<I", data, 0x1C)[0]
    e_phentsize = struct.unpack_from("<H", data, 0x2A)[0]
    e_phnum = struct.unpack_from("<H", data, 0x2C)[0]
    segs = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_offset, p_vaddr = struct.unpack_from("<III", data, off)
        p_filesz = struct.unpack_from("<I", data, off + 16)[0]
        p_flags = struct.unpack_from("<I", data, off + 24)[0]
        if p_type == 1 and (p_flags & 0x1):          # PT_LOAD | PF_X
            segs.append((p_vaddr, data[p_offset:p_offset + p_filesz]))
    if not segs:
        raise ValueError("sin segmento PT_LOAD ejecutable")
    return segs


def sections(data):
    """[(nombre, sh_addr, sh_size)] de las secciones con dirección asignada."""
    e_shoff = struct.unpack_from("<I", data, 0x20)[0]
    e_shentsize = struct.unpack_from("<H", data, 0x2E)[0]
    e_shnum = struct.unpack_from("<H", data, 0x30)[0]
    e_shstrndx = struct.unpack_from("<H", data, 0x32)[0]
    if e_shoff == 0 or e_shnum == 0:
        return []
    stroff = struct.unpack_from("<I", data, e_shoff + e_shstrndx * e_shentsize + 16)[0]
    out = []
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        sh_name, _sh_type, _sh_flags, sh_addr = struct.unpack_from("<IIII", data, o)
        sh_size = struct.unpack_from("<I", data, o + 20)[0]
        if sh_addr == 0:
            continue
        end = data.index(b"\0", stroff + sh_name)
        out.append((data[stroff + sh_name:end].decode("ascii", "replace"), sh_addr, sh_size))
    return out


def section_of(secs, rva):
    for name, addr, size in secs:
        if addr <= rva < addr + size:
            return name
    return "(ninguna)"


# ── el idiom ─────────────────────────────────────────────────────────────────

class Site:
    def __init__(self, rva, disp, m1, m2, m3, root=0xC58):
        self.rva, self.disp, self.root = rva, disp, root
        self.lea_dst,  self.lea_base  = (m1 >> 3) & 7, m1 & 7
        self.mov1_dst, self.mov1_base = (m2 >> 3) & 7, m2 & 7
        self.mov2_dst, self.mov2_base = (m3 >> 3) & 7, m3 & 7

    @property
    def chained(self):
        """El idiom REAL exige que el destino de cada instrucción sea la base de
        la siguiente. El runtime de hoy no lo comprueba."""
        return (self.lea_dst == self.mov1_base and
                self.mov1_dst == self.mov2_base)

    def asm(self):
        return ("lea %s,[%s+0x%x] ; mov %s,[%s] ; mov %s,[%s+0x%x]" % (
            REG32[self.lea_dst], REG32[self.lea_base], self.disp & 0xFFFFFFFF,
            REG32[self.mov1_dst], REG32[self.mov1_base],
            REG32[self.mov2_dst], REG32[self.mov2_base], self.root))


def scan_idiom(segs, root_off=0xC58):
    """Predicados IDÉNTICOS a FindCacheGlobalDisp para UNA fila de la tabla. Se
    ancla en el needle (la raíz como disp32 little-endian; equivalente y mucho
    más rápido que recorrer byte a byte: el needle sólo puede estar en i+10)."""
    needle = struct.pack("<I", root_off)
    out = []
    for vaddr, buf in segs:
        start = 0
        while True:
            o = buf.find(needle, start)
            if o < 0:
                break
            start = o + 1
            i = o - 10
            if i < 0 or i + 14 > len(buf):
                continue
            if buf[i] != 0x8D:
                continue
            m1 = buf[i + 1]
            if (m1 & 0xC0) != 0x80 or (m1 & 0x07) == 0x04:
                continue
            if buf[i + 6] != 0x8B:
                continue
            m2 = buf[i + 7]
            if (m2 & 0xC0) != 0x00 or (m2 & 0x07) == 0x04 or (m2 & 0x07) == 0x05:
                continue
            if buf[i + 8] != 0x8B:
                continue
            m3 = buf[i + 9]
            if (m3 & 0xC0) != 0x80 or (m3 & 0x07) == 0x04:
                continue
            disp = struct.unpack_from("<i", buf, i + 2)[0]
            out.append(Site(vaddr + i, disp, m1, m2, m3, root_off))
    return out


def scan_layouts(segs):
    """{nombre: scan_idiom(...)} para cada fila de CACHE_LAYOUTS."""
    return {name: scan_idiom(segs, root) for name, root, _n in CACHE_LAYOUTS}


# ── GOT, igual que DeriveGotBase ─────────────────────────────────────────────

def derive_got(segs):
    """GOT = (RVA del byte 0x05) + imm32, con la cola del prólogo de GMRC detrás.
    Es la misma cuenta que hace la CPU al ejecutar `call thunk; add eax,imm32`."""
    head = GMRC_PROLOGUE_TAIL[:min(GMRC_TAIL_WILDCARD)]
    hits = []
    for vaddr, buf in segs:
        start = 0
        while True:
            o = buf.find(head, start)
            if o < 0:
                break
            start = o + 1
            # el resto de la cola, con los bytes del frame libres (como tailMatches)
            if o + len(GMRC_PROLOGUE_TAIL) > len(buf) or any(
                    buf[o + k] != GMRC_PROLOGUE_TAIL[k]
                    for k in range(len(GMRC_PROLOGUE_TAIL)) if k not in GMRC_TAIL_WILDCARD):
                continue
            i = o - 5                       # el byte 0x05 del `add eax,imm32`
            if i < 0 or buf[i] != 0x05:
                continue
            imm = struct.unpack_from("<i", buf, i + 1)[0]
            hits.append(vaddr + i + imm)
    return hits


# ── informe ──────────────────────────────────────────────────────────────────

USAGE = """uso: python3 tools/experiment_cache_idiom.py <steamclient.so>

El binario tiene que ser el steamclient.so de 32 bits. Si no lo tienes a mano,
el propio repo se lo baja de la CDN publica de Valve (sin login, sin steamcmd —
es lo que hace .github/workflows/watch-steam.yml):

    python3 tools/fetch_steamclient.py --output steamclient.so
    python3 tools/experiment_cache_idiom.py steamclient.so

Y si quieres el de una instalacion local (una Deck, por ejemplo):

    find ~ -name steamclient.so -path '*ubuntu12_32*' 2>/dev/null
"""


def main():
    if len(sys.argv) != 2:
        print(USAGE)
        return 2
    try:
        data = read_elf(sys.argv[1])
    except FileNotFoundError:
        print("no existe: %s\n" % sys.argv[1])
        print(USAGE)
        return 2
    except ValueError as e:
        print("no sirve como entrada: %s (%s)\n" % (sys.argv[1], e))
        print(USAGE)
        return 2
    segs = exec_segments(data)
    secs = sections(data)

    per_layout = scan_layouts(segs)
    hit = [(name, ss) for name, ss in per_layout.items() if ss]
    print("=" * 74)
    for name, root, _n in CACHE_LAYOUTS:
        print("layout %-14s raíz 0x%x : %d sitio(s)" % (name, root, len(per_layout[name])))
    if len(hit) > 1:
        print("AMBIGUOUS-LAYOUT: %d filas con sitios -> el finder rehúsa (no inyecta)" % len(hit))
    elif not hit:
        print("NOT_FOUND en todas las filas -> siguiente paso: experiment_cache_idiom_free.py")
    sites = hit[0][1] if len(hit) == 1 else [s for _, ss in hit for s in ss]
    disps = sorted({s.disp for s in sites})
    chained = [s for s in sites if s.chained]
    cdisps = sorted({s.disp for s in chained})

    print("sitios que casan con los predicados del runtime : %d" % len(sites))
    print("disp32 distintos entre ellos                    : %d" % len(disps))
    print("de esos sitios, con registros ENCADENADOS       : %d" % len(chained))
    print("disp32 distintos entre los encadenados          : %d" % len(cdisps))
    print("=" * 74)

    gots = derive_got(segs)
    got = gots[0] if gots else None
    if not gots:
        print("\n[!] GOT no derivable: la cola del prologo de GMRC no aparece.")
    elif len(set(gots)) > 1:
        print("\n[!] GOT ambiguo: %d colas de prologo dan %d GOT distintos %s"
              % (len(gots), len(set(gots)), [hex(g) for g in sorted(set(gots))]))
    else:
        print("\nGOT (RVA) = 0x%x  (de %d cola(s) de prologo, todas de acuerdo)"
              % (got, len(gots)))

    print("\n--- cada sitio ---")
    for s in sites:
        line = "  rva=0x%-9x disp=0x%-8x encadenado=%-3s  %s" % (
            s.rva, s.disp & 0xFFFFFFFF, "si" if s.chained else "NO", s.asm())
        if got is not None:
            tgt = (got + s.disp) & 0xFFFFFFFF
            line += "\n      -> GOT+disp = 0x%x  en seccion %s" % (tgt, section_of(secs, tgt))
        print(line)

    print("\n--- que haria cada politica ---")
    first = sites[0].disp if sites else 0
    print("  HOY (se queda con el primero)      : disp=0x%x%s"
          % (first & 0xFFFFFFFF,
             "   <-- ADIVINANDO, hay %d disps" % len(disps) if len(disps) > 1 else ""))
    print("  fallo en cerrado                   : %s"
          % ("disp=0x%x" % (disps[0] & 0xFFFFFFFF) if len(disps) == 1
             else "0 (no inyecta: %d disps distintos)" % len(disps) if disps
             else "0 (no hay sitios)"))
    print("  fallo en cerrado + encadenado      : %s"
          % ("disp=0x%x" % (cdisps[0] & 0xFFFFFFFF) if len(cdisps) == 1
             else "0 (no inyecta: %d disps distintos)" % len(cdisps) if cdisps
             else "0 (no hay sitios encadenados)"))

    print("\n--- veredicto ---")
    if len(disps) == 1:
        print("  UNIQUE ya hoy. Las piezas 1-3 (fallo en cerrado + log honesto +")
        print("  CI) son riesgo CERO: no cambian el resultado en este build, solo")
        print("  cierran el agujero para un build futuro. El encadenado es mejora")
        print("  opcional, no urgente.")
    elif len(cdisps) == 1:
        print("  AMBIGUO hoy, pero el ENCADENADO lo deja en uno solo. El fix debe")
        print("  incluir la comprobacion de registros; sin ella el fallo en cerrado")
        print("  dejaria al finder sin resolver en un build donde SI puede.")
    else:
        print("  AMBIGUO incluso con encadenado. Hace falta un discriminador mas")
        print("  (p.ej. exigir que GOT+disp caiga en .bss/.data, que la columna de")
        print("  arriba ya calcula). NO tocar el runtime hasta decidirlo.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
