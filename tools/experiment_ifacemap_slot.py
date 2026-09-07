#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# experiment_ifacemap_slot.py — ¿podemos derivar el índice de vtable POR NOMBRE,
# como hace download.lua, en vez de por patrón de bytes o por constante?
#
# Contexto (docs/slssteam-plugins-analysis.md §7.5.a)
# ---------------------------------------------------
# DepotKey tiene HOY un solo método real de localización: la chuleta es una caché
# del patrón, y la vía RTTI (`ResolveVtableSlotBySignature`) busca la ranura
# COMPARANDO PRÓLOGOS — o sea, el mismo patrón otra vez. Los tres caen a la vez
# cuando Valve recompila.
#
# `download.lua` (líneas 69-79) no hace eso. Hace:
#
#     VFTableInfo_t("21IClientConfigStoreMap", "GetBinary")   -> .index
#     VFTableInfo_t("12CConfigStore", "GetBinary", .index)    -> .ptr
#
# Las clases `*Map` de Steam son la capa de despacho de la interfaz, y cada uno
# de sus métodos referencia SU PROPIO NOMBRE como cadena. Así que el índice se
# deriva del binario que se tiene delante: cero constantes, cero bytes de la
# función objetivo. Inmune a la recompilación Y a que Valve reordene la vtable.
#
# Qué responde este script (las 4 incógnitas antes de escribir C++)
# -----------------------------------------------------------------
#   1. ¿Existe la vtable de 21IClientConfigStoreMap en este steamclient.so?
#   2. ¿Dónde está la cadena "GetBinary" y cuántas veces aparece?
#   3. ¿Qué ranura la referencia — una sola o varias?
#   4. ¿El índice resultante coincide con el slot 6 que CI publica hoy, y
#      apunta a la misma dirección que `hooks.DepotKey` de la chuleta?
#
# Cómo lo hace, y por qué NO necesita desensamblador
# ---------------------------------------------------
# SLSsteam construye el diccionario completo (`Decompiler::parseInterfaceMapBase`,
# decompiler.cpp:593): recorre ranuras, desensambla cada función y recoge TODAS
# las cadenas que menciona, para 37 interfaces. Aquí sólo hace falta UNA entrada,
# así que se le da la vuelta a la pregunta:
#
#   SLSsteam:  "¿qué cadenas menciona cada ranura?"      -> mapa entero, caro
#   nosotros:  "¿qué ranura menciona ESTA dirección?"    -> dirigido, barato
#
# En i386 PIC una cadena se referencia como `lea reg, [got_base + disp32]`
# (opcode 8D, ModRM mod=10). Conocida la dirección G de la cadena y la base del
# GOT, el disp32 buscado es `G - got_base` — un valor de 4 bytes concreto. Buscar
# `8D <modrm> <disp32>` en .text es un escaneo de bytes, igual que el resto de
# tools/. Es la misma técnica que `gmrc_xref_core.hpp` ya usa en runtime.
#
# La base del GOT se deriva por consenso del idiom PIC `E8 <thunk>; 05 <imm32>`
# (call get_pc_thunk.ax; add eax, GOT) — el mismo método que
# `DeriveGotBaseConsensus` (gmrc_xref_core.hpp) y `DeriveGotBase`
# (package_zero_finder.cpp:106), portado a Python.
#
# Uso
# ---
#   python3 tools/fetch_steamclient.py --output /tmp/steamclient.so
#   python3 tools/experiment_ifacemap_slot.py /tmp/steamclient.so
#
#   # varios builds de golpe, para ver si el método es estable:
#   python3 tools/experiment_ifacemap_slot.py /tmp/sc_a.so /tmp/sc_b.so
#
#   # otra interfaz/método:
#   python3 tools/experiment_ifacemap_slot.py sc.so \
#       --map-class 21IClientConfigStoreMap --method GetBinary \
#       --impl-class 12CConfigStore
#
# READ-ONLY: abre el fichero, no arranca Steam, no toca producción.

from __future__ import annotations

import argparse
import collections
import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
RVAS_DIR = os.path.join(REPO, "res", "rvas")


# ── ELF32 ────────────────────────────────────────────────────────────────────

def load_sections(path):
    """{name: (sh_addr, sh_offset, sh_size)} + bytes. Mismo parser que
    check_patterns.py::_load_sections — sin pyelftools, a propósito."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF" or data[4] != 1:
        raise ValueError("no es un ELF32")
    e_shoff = struct.unpack_from("<I", data, 0x20)[0]
    e_shentsize = struct.unpack_from("<H", data, 0x2E)[0]
    e_shnum = struct.unpack_from("<H", data, 0x30)[0]
    e_shstrndx = struct.unpack_from("<H", data, 0x32)[0]
    if not e_shoff or not e_shnum:
        raise ValueError("sin cabeceras de sección")

    def shdr(i):
        o = e_shoff + i * e_shentsize
        name, _typ, _flags, addr, off, size = struct.unpack_from("<IIIIII", data, o)
        return name, addr, off, size

    strtab_off = shdr(e_shstrndx)[2]
    secs = {}
    for i in range(e_shnum):
        nmoff, addr, off, size = shdr(i)
        end = data.index(b"\x00", strtab_off + nmoff)
        secs[data[strtab_off + nmoff:end].decode("latin1")] = (addr, off, size)
    return data, secs


def make_reader(data, secs):
    ranges = [(a, o, s) for (a, o, s) in secs.values() if a and s]

    def read_va(va, n):
        for a, o, s in ranges:
            if a <= va < a + s and va + n <= a + s:
                return data[o + (va - a): o + (va - a) + n]
        return b""
    return read_va


# ── RTTI: nombre de clase -> vtable -> ranuras ───────────────────────────────

def vtable_slots(data, secs, mangled, max_slots):
    """type-name (.rodata) -> type_info -> vtable (.data.rel.ro) -> ranuras.

    En i386 las relocalizaciones son REL: el addend va EN SITIO, así que el
    fichero ya contiene los vaddr. Es el mismo camino que Rtti::FindTypeVtable
    hace en caliente (rtti.cpp:125), sin comparar un solo byte de código."""
    if ".rodata" not in secs or ".data.rel.ro" not in secs:
        return None, "faltan .rodata/.data.rel.ro", None

    ro_a, ro_o, ro_s = secs[".rodata"]
    i = data[ro_o:ro_o + ro_s].find(mangled.encode() + b"\x00")
    if i < 0:
        return None, "type-name %r no está en .rodata" % mangled, None
    name_va = ro_a + i

    drr_a, drr_o, drr_s = secs[".data.rel.ro"]
    drr = data[drr_o:drr_o + drr_s]

    ti_field = None
    for off in range(0, len(drr) - 4, 4):
        if struct.unpack_from("<I", drr, off)[0] == name_va:
            ti_field = drr_a + off
            break
    if ti_field is None:
        return None, "type_info de %r no encontrado" % mangled, None
    typeinfo_va = ti_field - 4

    vt_hdr = None
    for off in range(0, len(drr) - 8, 4):
        if (struct.unpack_from("<I", drr, off)[0] == 0
                and struct.unpack_from("<I", drr, off + 4)[0] == typeinfo_va):
            vt_hdr = drr_a + off
            break
    if vt_hdr is None:
        return None, "vtable de %r no encontrada (typeinfo=0x%x)" % (mangled, typeinfo_va), None

    read_va = make_reader(data, secs)
    tx_a, _tx_o, tx_s = secs.get(".text", (0, 0, 0))
    slots = []
    for k in range(max_slots):
        b = read_va(vt_hdr + 8 + k * 4, 4)
        if len(b) < 4:
            break
        fn = struct.unpack_from("<I", b, 0)[0]
        # Parar en la primera entrada que no sea código: fin de la vtable.
        if not (tx_a <= fn < tx_a + tx_s):
            break
        slots.append((k, fn))
    return slots, "ok", vt_hdr


# ── base del GOT por consenso ────────────────────────────────────────────────

def got_base_consensus(data, secs):
    """`E8 <rel32>; 05 <imm32>` = call get_pc_thunk.ax; add eax, GOT.
    El thunk devuelve la dirección SIGUIENTE al call, así que
    got = (va del 05) + imm32. Se toma el valor más repetido.

    Puerto de DeriveGotBaseConsensus (gmrc_xref_core.hpp) / DeriveGotBase
    (package_zero_finder.cpp:106)."""
    if ".text" not in secs:
        return None, {}
    a, o, s = secs[".text"]
    tx = data[o:o + s]
    votes = collections.Counter()
    i = 0
    n = len(tx) - 10
    while i < n:
        j = tx.find(b"\xE8", i)
        if j < 0 or j > n:
            break
        if tx[j + 5] == 0x05:
            imm = struct.unpack_from("<i", tx, j + 6)[0]
            votes[(a + j + 5 + imm) & 0xFFFFFFFF] += 1
        i = j + 1
    if not votes:
        return None, {}
    return votes.most_common(1)[0][0], votes


# ── referencias a una dirección, sin desensamblar ────────────────────────────

def find_lea_refs(data, secs, target_va, got_base):
    """`8D <modrm mod=10> <disp32>` con got_base + disp32 == target_va.

    Devuelve [(va_de_la_instrucción, registro_base)]. Se exige mod=10 (disp32) y
    se descarta rm=100 (SIB), igual que FindDisp32Ref en gmrc_xref_core.hpp."""
    a, o, s = secs[".text"]
    tx = data[o:o + s]
    want = (target_va - got_base) & 0xFFFFFFFF
    needle = struct.pack("<I", want)
    refs = []
    i = 0
    while True:
        j = tx.find(needle, i)
        if j < 0:
            break
        i = j + 1
        if j < 2:
            continue
        if tx[j - 2] != 0x8D:
            continue
        modrm = tx[j - 1]
        if (modrm & 0xC0) != 0x80:
            continue
        if (modrm & 0x07) == 0x04:      # SIB, otro encoding
            continue
        refs.append((a + j - 2, modrm & 0x07))
    # Superconjunto: el disp32 en cualquier sitio, para ver si nos dejamos algo.
    loose = tx.count(needle)
    return refs, loose


# ── chuleta publicada, para contrastar ───────────────────────────────────────

def feed_for(path):
    h = hashlib.sha256(open(path, "rb").read()).hexdigest()
    p = os.path.join(RVAS_DIR, h + ".yaml")
    if not os.path.isfile(p):
        return h, None
    out = {}
    cur = None
    for line in open(p, encoding="utf-8"):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if not line.startswith((" ", "\t")):
            cur = line.split(":")[0].strip()
            continue
        k, _, v = line.strip().partition(":")
        out["%s.%s" % (cur, k.strip())] = v.strip().strip('"')
    return h, out


# ── informe ──────────────────────────────────────────────────────────────────

def analyse(path, args):
    print("=" * 78)
    print("BINARIO  %s" % path)
    data, secs = load_sections(path)
    if ".text" not in secs or not secs[".text"][2]:
        print("    FALLO: el ELF no tiene .text utilizable")
        return False
    sha, feed = feed_for(path)
    print("SHA-256  %s" % sha)
    print("CHULETA  %s" % ("res/rvas/%s.yaml" % sha[:16] if feed else "(no hay ficha para este build)"))
    if feed:
        for k in ("hooks.DepotKey", "depotkey_rtti.class", "depotkey_rtti.slot", "depotkey_rtti.rva"):
            if k in feed:
                print("           %-22s %s" % (k, feed[k]))
    print()

    # (1) la vtable del mapa de interfaz
    print("[1] vtable de %s" % args.map_class)
    map_slots, note, map_vt = vtable_slots(data, secs, args.map_class, args.max_slots)
    if map_slots is None:
        print("    FALLO: %s" % note)
        return False
    print("    vtable en 0x%x — %d ranuras de código" % (map_vt, len(map_slots)))
    print()

    # (2) la cadena
    print("[2] cadena %r" % args.method)
    needle = args.method.encode() + b"\x00"
    hits = []
    for nm, (a, o, s) in sorted(secs.items()):
        if not a or not s:
            continue
        blob = data[o:o + s]
        start = 0
        while True:
            j = blob.find(needle, start)
            if j < 0:
                break
            start = j + 1
            # exigir NUL delante: cadena completa, no sufijo de otra más larga
            standalone = (j == 0) or (blob[j - 1] == 0)
            hits.append((nm, a + j, standalone))
    if not hits:
        print("    FALLO: no aparece como cadena terminada en NUL")
        return False
    for nm, va, standalone in hits:
        print("    0x%08x  en %-14s %s" % (va, nm, "" if standalone else "(¡sufijo de otra cadena!)"))
    print()

    # (3) base del GOT
    got, votes = got_base_consensus(data, secs)
    if got is None:
        print("[3] FALLO: no se pudo derivar la base del GOT")
        return False
    top = votes.most_common(3)
    total = sum(votes.values())
    print("[3] base del GOT  0x%08x  (%d/%d votos = %.1f%%)"
          % (got, top[0][1], total, 100.0 * top[0][1] / total))
    for va, n in top[1:]:
        print("      alternativa descartada 0x%08x (%d votos)" % (va, n))
    print()

    # (4) qué ranura la referencia
    print("[4] ranuras que referencian la cadena")
    slot_starts = sorted((fn, k) for k, fn in map_slots)
    verdict_slot = None
    for nm, va, standalone in hits:
        refs, loose = find_lea_refs(data, secs, va, got)
        print("    cadena 0x%08x: %d ref(s) `lea` en .text  (disp32 suelto: %d)"
              % (va, len(refs), loose))
        for ref_va, reg in refs:
            owner = None
            for fn, k in slot_starts:
                if fn <= ref_va:
                    owner = (fn, k)
                else:
                    break
            if owner is None:
                print("        0x%08x  -> antes de la primera ranura" % ref_va)
                continue
            fn, k = owner
            dist = ref_va - fn
            flag = "" if dist <= args.max_fn_size else "  <-- LEJOS, probablemente otra función"
            print("        0x%08x  -> ranura %-3d (fn 0x%08x, +%d bytes)%s"
                  % (ref_va, k, fn, dist, flag))
            if dist <= args.max_fn_size and standalone:
                if verdict_slot is None:
                    verdict_slot = k
                elif verdict_slot != k:
                    verdict_slot = -1   # ambiguo
    print()

    # (5) aplicar el índice a la clase concreta y contrastar
    print("[5] veredicto")
    if verdict_slot is None:
        print("    NO CONCLUYENTE: ninguna ranura referencia la cadena de cerca.")
        return False
    if verdict_slot < 0:
        print("    AMBIGUO: más de una ranura la referencia. Haría falta desempatar")
        print("    (SLSsteam desempata sobrecargas añadiendo sufijos 2,3,... al nombre).")
        return False

    print("    índice derivado por nombre: %d" % verdict_slot)
    impl_slots, note, impl_vt = vtable_slots(data, secs, args.impl_class, args.max_slots)
    if impl_slots is None:
        print("    pero no se pudo leer la vtable de %s: %s" % (args.impl_class, note))
        return False
    got_fn = dict(impl_slots).get(verdict_slot)
    if got_fn is None:
        print("    %s no tiene ranura %d (sólo %d)" % (args.impl_class, verdict_slot, len(impl_slots)))
        return False
    print("    %s vtable 0x%x, ranura %d -> 0x%08x" % (args.impl_class, impl_vt, verdict_slot, got_fn))

    ok = True
    if feed:
        want_slot = feed.get("depotkey_rtti.slot")
        want_rva = feed.get("hooks.DepotKey") or feed.get("depotkey_rtti.rva")
        if want_slot is not None:
            same = int(want_slot) == verdict_slot
            print("    contraste slot   CI=%s  nombre=%d  -> %s"
                  % (want_slot, verdict_slot, "COINCIDE" if same else "DISCREPA"))
            ok = ok and same
        if want_rva:
            same = int(want_rva, 16) == got_fn
            print("    contraste rva    CI=%s  nombre=0x%08x  -> %s"
                  % (want_rva, got_fn, "COINCIDE" if same else "DISCREPA"))
            ok = ok and same
    else:
        print("    (sin ficha publicada para este build: nada con lo que contrastar)")
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("binaries", nargs="+", help="uno o más steamclient.so (32-bit)")
    ap.add_argument("--map-class", default="21IClientConfigStoreMap")
    ap.add_argument("--method", default="GetBinary")
    ap.add_argument("--impl-class", default="12CConfigStore")
    ap.add_argument("--max-slots", type=int, default=64)
    ap.add_argument("--max-fn-size", type=int, default=512,
                    help="distancia máxima ref-inicio para atribuirla a esa ranura")
    args = ap.parse_args()

    results = []
    for p in args.binaries:
        try:
            results.append((p, analyse(p, args)))
        except Exception as e:
            print("    ERROR: %s" % e)
            results.append((p, False))
        print()

    print("=" * 78)
    print("RESUMEN")
    for p, ok in results:
        print("  %-50s %s" % (os.path.basename(p), "OK" if ok else "revisar"))
    return 0 if all(ok for _, ok in results) else 1


if __name__ == "__main__":
    sys.exit(main())
