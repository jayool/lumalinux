#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# experiment_ownership_ticket.py — las incógnitas que faltan ANTES de portar el
# plugin `spliced-tickets.lua` de SLSsteam a un hook nativo de lumalinux.
#
# El plugin resuelve el objetivo en dos pasos (misma técnica que download.lua,
# docs/slssteam-plugins-analysis.md §7.5.a):
#
#     VFTableInfo_t("14IClientUserMap", "GetAppOwnershipTicketExtendedData")  -> .index
#     VFTableInfo_t("5CUser", ..., .index, 0)                                  -> .ptr
#
# El "0" final es el SUB-ÍNDICE DE VTABLE. `CUser` hereda de varias interfaces
# (`CBaseUser, IClientUser, IClientMatchmaking, …`), así que no tiene una vtable
# sino un GRUPO contiguo: la primaria (offset_to_top = 0, la de CBaseUser) y
# después una secundaria por cada base, todas apuntando al MISMO type_info
# `5CUser`. `subclasses[0]` en SLSsteam es la PRIMERA SECUNDARIA (IClientUser).
# Nuestro Rtti::FindTypeVtable sólo localiza la primaria, así que el port
# necesita saber leer la secundaria. Este script responde, contra el
# steamclient.so que se le pase (el del codespace o el de la Deck):
#
#   1. ¿Qué ranura de 14IClientUserMap referencia la cadena del método?
#   2. ¿Cómo es el grupo de vtables de 5CUser (cuántas, offset_to_top, tamaño)?
#   3. ¿A qué dirección apunta esa ranura en la PRIMERA SECUNDARIA (la del
#      plugin) y en la primaria (la que usaría el resolver actual, MAL)?
#   4. ¿Qué prólogo tiene la función objetivo? (para saber si LmHook la traga)
#
# Uso:
#   python3 tools/experiment_ownership_ticket.py ~/.local/share/Steam/ubuntu12_32/steamclient.so
#
# Sólo lee el fichero. No toca Steam, no necesita root, no necesita Ghidra.

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from experiment_ifacemap_slot import (  # noqa: E402
    load_sections, make_reader, vtable_slots, got_base_consensus, find_lea_refs,
)

MAP_CLASS = "14IClientUserMap"
METHOD = "GetAppOwnershipTicketExtendedData"
IMPL_CLASS = "5CUser"


def string_hits(data, secs, name):
    needle = name.encode() + b"\x00"
    out = []
    for nm, (a, o, s) in secs.items():
        if not a or not s or nm not in (".rodata", ".data.rel.ro", ".data"):
            continue
        blob = data[o:o + s]
        j = blob.find(needle)
        while j >= 0:
            standalone = (j == 0 or blob[j - 1] == 0)
            out.append((a + j, standalone))
            j = blob.find(needle, j + 1)
    return out


def map_slot_for(data, secs, max_slots, max_fn_size):
    """Ranura de MAP_CLASS cuyo cuerpo referencia METHOD (regla download.lua:
    la menor si hay sobrecargas)."""
    slots, note, _ = vtable_slots(data, secs, MAP_CLASS, max_slots)
    if slots is None:
        return None, "vtable %s: %s" % (MAP_CLASS, note)
    hits = [h for h in string_hits(data, secs, METHOD) if h[1]]
    if not hits:
        return None, "cadena %r no está como literal NUL-terminado" % METHOD
    got, _ = got_base_consensus(data, secs)
    if got is None:
        return None, "sin base del GOT"
    starts = sorted((fn, k) for k, fn in slots)
    found = {}
    for va, _ in hits:
        refs, _loose = find_lea_refs(data, secs, va, got)
        for ref_va, _reg in refs:
            owner = None
            for fn, k in starts:
                if fn <= ref_va:
                    owner = (fn, k)
                else:
                    break
            if owner and ref_va - owner[0] <= max_fn_size:
                found.setdefault(owner[1], []).append((ref_va, owner[0]))
    if not found:
        return None, "ninguna ranura de %s referencia la cadena de cerca" % MAP_CLASS
    return found, "ok"


def vtable_group(data, secs, mangled, max_entries, max_subs):
    """Primaria + secundarias contiguas de `mangled`, delimitadas como hace
    SLSsteam: una cabecera es un par [offset_to_top NEGATIVO, type_info].
    Devuelve [(offset_to_top, hdr_va, [fn...], n_fuera_de_text), ...];
    índice 0 = primaria. Las entradas que no caen en .text (PLT de
    __cxa_pure_virtual, thunks…) se conservan para no perder la cuenta de
    ranuras, y se cuentan aparte."""
    prim, note, vt_hdr = vtable_slots(data, secs, mangled, 8)
    if prim is None:
        return None, note
    read_va = make_reader(data, secs)
    typeinfo_va = struct.unpack_from("<I", read_va(vt_hdr + 4, 4), 0)[0]
    tx_a, _o, tx_s = secs.get(".text", (0, 0, 0))
    drr_a, _do, drr_s = secs[".data.rel.ro"]

    def in_text(v):
        return tx_a <= v < tx_a + tx_s

    def looks_like_typeinfo(v):
        # puntero a .data.rel.ro cuyo campo +4 apunta a una cadena de .rodata
        if not (drr_a <= v < drr_a + drr_s):
            return False
        b = read_va(v + 4, 4)
        if len(b) < 4:
            return False
        nm = struct.unpack_from("<I", b, 0)[0]
        ro_a, _ro_o, ro_s = secs[".rodata"]
        return ro_a <= nm < ro_a + ro_s

    group = []
    cur_hdr, cur_off, fns, n_out = vt_hdr, 0, [], 0
    p = vt_hdr + 8
    for _ in range(max_entries):
        b = read_va(p, 8)
        if len(b) < 8:
            break
        v, nxt = struct.unpack_from("<iI", b, 0)
        if v < 0 and nxt == typeinfo_va:
            group.append((cur_off, cur_hdr, fns, n_out))
            if len(group) >= max_subs:
                return group, "ok (tope de sub-vtables)"
            cur_hdr, cur_off, fns, n_out = p, v, [], 0
            p += 8
            continue
        if v == 0 and looks_like_typeinfo(nxt) and nxt != typeinfo_va:
            break  # primaria de OTRA clase: fin del grupo
        u = v & 0xFFFFFFFF
        fns.append(u)
        if not in_text(u):
            n_out += 1
        p += 4
    group.append((cur_off, cur_hdr, fns, n_out))
    return group, "ok"


def dispatcher_uses_index(read_va, fn_va, index, span=768):
    """¿El despachador del mapa hace `call [reg+index*4]`? (FF /2 con disp32,
    ModRM 0x90..0x97; con disp8 0x50..0x57 si cabe). Confirmación independiente
    de que el índice se aplica a la vtable de la interfaz."""
    code = read_va(fn_va, span)
    disp = index * 4
    hits = []
    for i in range(len(code) - 6):
        if code[i] != 0xFF:
            continue
        m = code[i + 1]
        if 0x90 <= m <= 0x97 and struct.unpack_from("<I", code, i + 2)[0] == disp:
            hits.append((fn_va + i, "call [r%d+0x%x] (disp32)" % (m & 7, disp)))
        elif disp < 128 and 0x50 <= m <= 0x57 and code[i + 2] == disp:
            hits.append((fn_va + i, "call [r%d+0x%x] (disp8)" % (m & 7, disp)))
    return hits


def describe_prologue(b):
    """Lectura a ojo de los primeros bytes: lo que LmHook necesita saber."""
    notes = []
    if b[:3] == b"\x55\x89\xe5":
        notes.append("push ebp; mov ebp,esp (prólogo clásico)")
    if b[:1] == b"\x55" and b[1:2] in (b"\x57", b"\x56", b"\x53"):
        notes.append("push ebp; push reg… (guarda callee-saved)")
    if b[:1] in (b"\x53", b"\x56", b"\x57"):
        notes.append("empieza con push de callee-saved")
    if b[:3] == b"\x83\xec" or b[:2] == b"\x81\xec":
        notes.append("sub esp,imm al inicio")
    i = b.find(b"\xe8")
    if 0 <= i < 8:
        notes.append("call rel32 en +%d (probable get_pc_thunk: LmHook lo reescribe)" % i)
    if b[:2] == b"\xf3\x0f" or b[:3] == b"\x0f\x1f\x44":
        notes.append("endbr32/nop multibyte al inicio")
    return "; ".join(notes) if notes else "no reconocido a ojo (mirar con objdump)"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("steamclient", help="ubuntu12_32/steamclient.so (32-bit)")
    ap.add_argument("--max-slots", type=int, default=512,
                    help="tope de ranuras del mapa")
    ap.add_argument("--max-fn-size", type=int, default=512)
    ap.add_argument("--max-subs", type=int, default=12)
    args = ap.parse_args()

    data, secs = load_sections(args.steamclient)
    read_va = make_reader(data, secs)
    print("steamclient.so: %s (%d bytes)\n" % (args.steamclient, len(data)))

    # [1] ranura del mapa
    found, note = map_slot_for(data, secs, args.max_slots, args.max_fn_size)
    print("[1] %s::%s" % (MAP_CLASS, METHOD))
    if found is None:
        print("    FALLO: %s" % note)
        return 1
    for k in sorted(found):
        refs = ", ".join("0x%08x (fn 0x%08x, +%d)" % (r, f, r - f) for r, f in found[k])
        print("    ranura %-4d <- %s" % (k, refs))
    index = min(found)
    print("    índice elegido (menor): %d%s\n"
          % (index, "  (hay sobrecargas: %s)" % sorted(found) if len(found) > 1 else ""))

    # [1b] ¿el despachador aplica ese índice?
    map_fn = found[index][0][1]
    hits = dispatcher_uses_index(read_va, map_fn, index)
    print("[1b] despachador 0x%08x: %s" % (map_fn, ", ".join("0x%08x %s" % h for h in hits) or
          "NO se ve `call [reg+0x%x]` en %d bytes (mirar con objdump)" % (index * 4, 768)))
    print()

    # [2] grupo de vtables de CUser
    group, note = vtable_group(data, secs, IMPL_CLASS, 6000, args.max_subs)
    print("[2] grupo de vtables de %s (%s)" % (IMPL_CLASS, note))
    if group is None:
        print("    FALLO: %s" % note)
        return 1
    for i, (off_top, hdr, fns, n_out) in enumerate(group):
        tag = "primaria" if i == 0 else "secundaria %d (subclasses[%d] en SLSsteam)" % (i - 1, i - 1)
        print("    [%d] hdr 0x%08x  offset_to_top %-7d  %4d ranuras (%d fuera de .text)  %s"
              % (i, hdr, off_top, len(fns), n_out, tag))
    print()

    # [3] aplicar el índice
    print("[3] ranura %d aplicada" % index)
    if len(group) < 2:
        print("    FALLO: %s no tiene vtable secundaria; el plugin usa subclasses[0]" % IMPL_CLASS)
        return 1
    tx_a, _o, tx_s = secs.get(".text", (0, 0, 0))
    def show(label, fns):
        if index >= len(fns):
            print("    %-58s fuera de rango (%d ranuras)" % (label, len(fns)))
            return None
        v = fns[index]
        where = "en .text" if tx_a <= v < tx_a + tx_s else "FUERA de .text"
        print("    %-58s 0x%08x  %s" % (label, v, where))
        return v
    prim_fn = show("primaria (lo que resuelve Rtti::FindTypeVtable HOY):", group[0][2])
    sec_fn = show("secundaria 0 (lo que usa el plugin):", group[1][2])
    if prim_fn and sec_fn:
        print("    %s" % ("DISTINTAS: aplicar el índice a la primaria engancharía otra función"
                          if prim_fn != sec_fn else "iguales (raro)"))
    print()
    if not sec_fn or not (tx_a <= sec_fn < tx_a + tx_s):
        return 1

    # [4] prólogo
    b = read_va(sec_fn, 24)
    print("[4] prólogo en 0x%08x" % sec_fn)
    print("    " + " ".join("%02x" % x for x in b))
    print("    " + describe_prologue(b))
    print("    objdump -d --start-address=0x%x --stop-address=0x%x %s"
          % (sec_fn, sec_fn + 32, args.steamclient))
    return 0


if __name__ == "__main__":
    sys.exit(main())
