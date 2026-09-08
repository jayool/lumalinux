#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# experiment_cache_idiom.py — ¿el idiom de acceso a la caché resuelve ÚNICO?
#
# El problema (src/hooks/package_zero_finder.cpp, FindCacheGlobalDisp)
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

CACHE_ROOT_OFFSET  = 0xC58
CACHE_IDIOM_NEEDLE = bytes([0x58, 0x0C, 0x00, 0x00])
GMRC_PROLOGUE_TAIL = bytes.fromhex(
    "55 89 E5 57 56 53 81 EC 10 01 00 00 8B 7D 08 8B 4D 20".replace(" ", ""))
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
    def __init__(self, rva, disp, m1, m2, m3):
        self.rva, self.disp = rva, disp
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
        return ("lea %s,[%s+0x%x] ; mov %s,[%s] ; mov %s,[%s+0xc58]" % (
            REG32[self.lea_dst], REG32[self.lea_base], self.disp & 0xFFFFFFFF,
            REG32[self.mov1_dst], REG32[self.mov1_base],
            REG32[self.mov2_dst], REG32[self.mov2_base]))


def scan_idiom(segs):
    """Predicados IDÉNTICOS a FindCacheGlobalDisp. Se ancla en el needle 0xc58
    (equivalente y mucho más rápido que recorrer byte a byte: el needle sólo
    puede estar en i+10)."""
    out = []
    for vaddr, buf in segs:
        start = 0
        while True:
            o = buf.find(CACHE_IDIOM_NEEDLE, start)
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
            out.append(Site(vaddr + i, disp, m1, m2, m3))
    return out


# ── GOT, igual que DeriveGotBase ─────────────────────────────────────────────

def derive_got(segs):
    """GOT = (RVA del byte 0x05) + imm32, con la cola del prólogo de GMRC detrás.
    Es la misma cuenta que hace la CPU al ejecutar `call thunk; add eax,imm32`."""
    hits = []
    for vaddr, buf in segs:
        start = 0
        while True:
            o = buf.find(GMRC_PROLOGUE_TAIL, start)
            if o < 0:
                break
            start = o + 1
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

    sites = scan_idiom(segs)
    disps = sorted({s.disp for s in sites})
    chained = [s for s in sites if s.chained]
    cdisps = sorted({s.disp for s in chained})

    print("=" * 74)
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
