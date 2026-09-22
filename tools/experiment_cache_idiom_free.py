#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# experiment_cache_idiom_free.py — ¿a dónde se ha ido el idiom de la caché?
#
# Sonda OFFLINE (docs/maintenance.md §C, después de experiment_cache_idiom.py)
# para el caso en que el finder de package-0 dice `cache_idiom NOT_FOUND`: la
# forma exacta `lea r1,[GOT+X] ; mov r2,[r1] ; mov r3,[r2+0xc58]` ya no está en
# el binario y hay que averiguar si el ROOT OFFSET se ha movido, si el compilador
# ha cambiado la FORMA de las instrucciones, o si el acceso se ha reescrito.
#
# Motivación (2026-09-22): la beta 1790036264 (sha 9cf4720f…) recompila
# steamclient.so (49,8 → 53,5 MB) y tanto check_patterns.py como derive_patterns.py
# reportan las dos anclas del finder NOT_FOUND. Ninguna de las dos herramientas
# busca con el offset libre — es su trabajo decir "no está", no "a dónde fue".
#
# Cuatro pasadas, todas de sólo lectura sobre el .so:
#
#   1. MISMO idiom, offset libre — los predicados de FindCacheGlobalDisp
#      (opcodes, campos mod, exclusiones rm, ENCADENADO de registros) tal cual,
#      pero sin fijar el disp32 del último mov en 0xc58. Agrupa por offset.
#   2. FORMA ALTERNATIVA: `mov r2,[GOT+X] ; mov r3,[r2+OFF]` (lea+mov fundidos
#      en un solo mov con disp32, mod=10), con encadenado. Agrupa por offset.
#   3. Por cada sitio de 1 y 2 dentro de la ventana: RVA, X, y en qué sección
#      cae GOT+X (en un binario sano, .bss). El GOT sale de la pasada 4.
#   4. El TAIL del prólogo de GMRC con el tamaño de frame como comodín
#      (`81 EC ?? ?? 00 00`), clasificado como DeriveGotBase: sitios, GOTs
#      distintos, UNIQUE / AMBIGUOUS. Dice qué frame lleva cada sitio.
#
# Validación: en la estable conocida (bc54101b) la pasada 1 debe dar la fila
# `0xc58: 2 sitios, 1 X (0x3b7d4)` y la 4 un sitio con frame 0x110. Si no
# reproduce lo conocido, no vale para la beta.
#
# Uso:  python3 tools/experiment_cache_idiom_free.py <steamclient.so>
#           [--window LO-HI]   ventana de offsets a listar (hex, defecto 0xb00-0xe00)
#           [--all]            lista TODOS los offsets, no sólo la ventana
#
# Reutiliza el cargador ELF, las secciones y los nombres de registro de
# experiment_cache_idiom.py; no duplica nada del runtime.
import argparse
import os
import struct
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from experiment_cache_idiom import (  # noqa: E402
    REG32, exec_segments, read_elf, section_of, sections)

KNOWN_ROOT = 0xC58
# Cola del prólogo de GMRC con el frame comodinado: 55 89 E5 57 56 53 81 EC lo hi 00 00
# seguido de 8B 7D 08 8B 4D 20. Se busca por las dos mitades fijas.
TAIL_HEAD = bytes.fromhex("5589E5575653" "81EC")
TAIL_REST = bytes.fromhex("0000" "8B7D08" "8B4D20")
TAIL_LEN = len(TAIL_HEAD) + 2 + len(TAIL_REST)      # 18, como el runtime


def parse_window(s):
    lo, hi = s.split("-")
    return int(lo, 16), int(hi, 16)


# ── pasada 1: el idiom de tres instrucciones, offset libre ───────────────────

def scan_three(segs):
    """[(rva, X, OFF, m1, m2, m3)] — predicados idénticos a FindCacheGlobalDisp
    salvo que el disp32 del tercer mov (OFF) es libre. Recorre byte a byte
    (sin needle: el needle ERA el offset)."""
    out = []
    for vaddr, buf in segs:
        n = len(buf) - 14
        find = buf.find
        i = find(b"\x8d", 0)
        while 0 <= i <= n:
            m1 = buf[i + 1]
            if (m1 & 0xC0) == 0x80 and (m1 & 0x07) != 0x04 \
                    and buf[i + 6] == 0x8B:
                m2 = buf[i + 7]
                if (m2 & 0xC0) == 0x00 and (m2 & 0x07) not in (0x04, 0x05) \
                        and buf[i + 8] == 0x8B:
                    m3 = buf[i + 9]
                    if (m3 & 0xC0) == 0x80 and (m3 & 0x07) != 0x04 \
                            and ((m1 >> 3) & 7) == (m2 & 7) \
                            and ((m2 >> 3) & 7) == (m3 & 7):
                        x = struct.unpack_from("<i", buf, i + 2)[0]
                        off = struct.unpack_from("<i", buf, i + 10)[0]
                        out.append((vaddr + i, x, off, m1, m2, m3))
            i = find(b"\x8d", i + 1)
    return out


def asm_three(x, off, m1, m2, m3):
    return "lea %s,[%s+0x%x] ; mov %s,[%s] ; mov %s,[%s+0x%x]" % (
        REG32[(m1 >> 3) & 7], REG32[m1 & 7], x & 0xFFFFFFFF,
        REG32[(m2 >> 3) & 7], REG32[m2 & 7],
        REG32[(m3 >> 3) & 7], REG32[m3 & 7], off & 0xFFFFFFFF)


# ── pasada 2: la forma fundida, dos instrucciones ────────────────────────────

def scan_two(segs):
    """[(rva, X, OFF, m1, m2)] — `mov r2,[base+X] ; mov r3,[r2+OFF]`, ambos
    8B con mod=10 y disp32, rm != SIB, encadenados (reg(mov1) == rm(mov2)).
    Es lo que emite Clang cuando funde el lea con la carga de la ranura."""
    out = []
    for vaddr, buf in segs:
        n = len(buf) - 12
        find = buf.find
        i = find(b"\x8b", 0)
        while 0 <= i <= n:
            m1 = buf[i + 1]
            if (m1 & 0xC0) == 0x80 and (m1 & 0x07) != 0x04 \
                    and buf[i + 6] == 0x8B:
                m2 = buf[i + 7]
                if (m2 & 0xC0) == 0x80 and (m2 & 0x07) != 0x04 \
                        and ((m1 >> 3) & 7) == (m2 & 7):
                    x = struct.unpack_from("<i", buf, i + 2)[0]
                    off = struct.unpack_from("<i", buf, i + 8)[0]
                    out.append((vaddr + i, x, off, m1, m2))
            i = find(b"\x8b", i + 1)
    return out


def asm_two(x, off, m1, m2):
    return "mov %s,[%s+0x%x] ; mov %s,[%s+0x%x]" % (
        REG32[(m1 >> 3) & 7], REG32[m1 & 7], x & 0xFFFFFFFF,
        REG32[(m2 >> 3) & 7], REG32[m2 & 7], off & 0xFFFFFFFF)


# ── pasada 4: el tail de GMRC con el frame libre ─────────────────────────────

def scan_tail(segs):
    """[(rva_del_05, frame, got)] — como DeriveGotBase, pero el imm32 del
    `sub esp,imm32` (los dos bytes bajos) es comodín; los dos altos siguen 00 00."""
    out = []
    for vaddr, buf in segs:
        start = 0
        while True:
            o = buf.find(TAIL_HEAD, start)
            if o < 0:
                break
            start = o + 1
            e = o + len(TAIL_HEAD)
            if buf[e + 2:e + 2 + len(TAIL_REST)] != TAIL_REST:
                continue
            i = o - 5
            if i < 0 or buf[i] != 0x05:
                continue
            frame = struct.unpack_from("<H", buf, e)[0]
            imm = struct.unpack_from("<i", buf, i + 1)[0]
            out.append((vaddr + i, frame, vaddr + i + imm))
    return out


# ── informe ──────────────────────────────────────────────────────────────────

def group(sites, off_index):
    by = defaultdict(list)
    for s in sites:
        by[s[off_index]].append(s)
    return by


def print_table(title, by, lo, hi, show_all):
    print("\n--- %s ---" % title)
    rows = sorted(by.items())
    shown = 0
    total_in = 0
    for off, ss in rows:
        in_win = lo <= off <= hi
        if in_win:
            total_in += 1
        if not (show_all or in_win):
            continue
        xs = sorted({s[1] for s in ss})
        mark = "  <== 0xc58 conocido" if off == KNOWN_ROOT else ""
        print("  OFF 0x%-6x  sitios=%-3d  X distintos=%-2d  %s%s" % (
            off & 0xFFFFFFFF, len(ss), len(xs),
            " ".join("0x%x" % (x & 0xFFFFFFFF) for x in xs[:6])
            + (" …" if len(xs) > 6 else ""), mark))
        shown += 1
    print("  (%d offset(s) distintos en total, %d en la ventana, %d mostrados)"
          % (len(rows), total_in, shown))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("steamclient")
    ap.add_argument("--window", default="b00-e00",
                    help="ventana de offsets a listar, hex LO-HI (defecto b00-e00)")
    ap.add_argument("--all", action="store_true", help="listar todos los offsets")
    args = ap.parse_args()
    lo, hi = parse_window(args.window)

    try:
        data = read_elf(args.steamclient)
    except (OSError, ValueError) as e:
        print("no sirve como entrada: %s (%s)" % (args.steamclient, e))
        return 2
    segs = exec_segments(data)
    secs = sections(data)
    print("binario: %s  (%d bytes, %d segmento(s) r-x)"
          % (args.steamclient, len(data), len(segs)))

    # 4 primero: el GOT hace falta para situar GOT+X en la pasada 3.
    tails = scan_tail(segs)
    gots = sorted({t[2] for t in tails})
    print("\n--- pasada 4: tail del prólogo de GMRC, frame comodín ---")
    if not tails:
        print("  NOT_FOUND: ni con el frame libre aparece el tail; el prólogo ha "
              "cambiado en algo más que el tamaño del frame")
    for rva, frame, got in tails:
        print("  @0x%08x  sub esp,0x%x  -> GOT 0x%x" % (rva, frame, got))
    if len(gots) == 1:
        print("  UNIQUE: %d sitio(s), GOT 0x%x" % (len(tails), gots[0]))
    elif gots:
        print("  AMBIGUOUS: %d sitio(s), %d GOT distintos" % (len(tails), len(gots)))
    got = gots[0] if len(gots) == 1 else None

    three = scan_three(segs)
    two = scan_two(segs)
    print_table("pasada 1: lea;mov;mov [r+OFF], offset libre (ventana 0x%x-0x%x)"
                % (lo, hi), group(three, 2), lo, hi, args.all)
    print_table("pasada 2: mov r,[base+X];mov [r+OFF], offset libre (ventana 0x%x-0x%x)"
                % (lo, hi), group(two, 2), lo, hi, args.all)

    print("\n--- pasada 3: sitios en la ventana, con GOT+X situado ---")
    if got is None:
        print("  (sin GOT único, GOT+X no se puede situar; se listan igual)")

    def where(x):
        if got is None:
            return "?"
        return "%s" % section_of(secs, (got + x) & 0xFFFFFFFF)

    for rva, x, off, m1, m2, m3 in sorted(three):
        if lo <= off <= hi:
            print("  [3] @0x%08x  OFF 0x%x  X 0x%-8x GOT+X en %-10s %s" % (
                rva, off & 0xFFFFFFFF, x & 0xFFFFFFFF, where(x),
                asm_three(x, off, m1, m2, m3)))
    for rva, x, off, m1, m2 in sorted(two):
        if lo <= off <= hi:
            print("  [2] @0x%08x  OFF 0x%x  X 0x%-8x GOT+X en %-10s %s" % (
                rva, off & 0xFFFFFFFF, x & 0xFFFFFFFF, where(x),
                asm_two(x, off, m1, m2)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
