#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# experiment_eh_frame.py — ¿puede .eh_frame_hdr sustituir a WalkBackToPrologue?
#
# El problema (src/gmrc_xref.hpp, bloque KNOWN LIMIT)
# ---------------------------------------------------
# El último paso del localizador por cadena de GMRC no encuentra "la entrada de
# la función": encuentra el idiom de preámbulo PIC más cercano hacia atrás y lo
# devuelve. Coinciden sólo cuando el preámbulo está en el offset 0. Dos casos
# reproducidos:
#   (a) un prólogo que hace push ANTES de la llamada al thunk —la forma que tiene
#       nuestro propio DepotKey (55 57 56 53 E8 …)— devuelve entrada+4;
#   (b) un `call X; add eax,imm32` normal en el cuerpo gana el escaneo hacia
#       atrás y devuelve entrada+0x48.
# Ninguno devuelve 0: devuelven una dirección plausible y equivocada, y el
# detour se escribe a mitad de instrucción.
#
# La idea
# -------
# El binario YA trae una tabla ordenada de inicios de función, para el manejo de
# excepciones: la tabla de búsqueda binaria de `.eh_frame_hdr`. Dada una
# dirección cualquiera dentro de una función, una búsqueda binaria da su entrada
# EXACTA. Sin caminar hacia atrás, sin reconocer prólogos, sin suposiciones — el
# mismo cambio de filosofía que la vtable en DepotKey: dejar de deducir y leer
# una tabla que el binario ya mantiene.
#
# Formato de .eh_frame_hdr (LSB Core Spec):
#   u8  version            (== 1)
#   u8  eh_frame_ptr_enc
#   u8  fde_count_enc
#   u8  table_enc
#   encoded eh_frame_ptr
#   encoded fde_count
#   fde_count × (initial_location, fde_ptr)   ordenados por initial_location
#
# Nota de diseño que este script quiere confirmar: NO hace falta parsear las FDE
# ni las CIE. El límite superior de una función se aproxima con el
# initial_location de la entrada siguiente, que la tabla ya trae ordenado. Eso da
# una comprobación de "¿está dentro?" —o sea fallo cerrado— sin decodificar nada
# más. Si eso se confirma, la versión C++ es sólo cabecera + búsqueda binaria.
#
# Qué responde
# ------------
#   1. ¿Existe .eh_frame_hdr y con qué codificaciones? ¿Son las esperadas?
#   2. ¿Cuántas funciones hay en la tabla?
#   3. Para cada entrada de hook conocida, ¿la tabla la contiene EXACTAMENTE?
#   4. La decisiva: mirando direcciones INTERIORES (entrada+4, +0x48, +178…),
#      ¿devuelve la entrada verdadera? Son justo los dos casos que hoy fallan.
#   5. ¿Y una dirección fuera de toda función se rechaza?
#
# Uso:
#   python3 tools/fetch_steamclient.py --output /tmp/steamclient.so
#   python3 tools/experiment_eh_frame.py /tmp/steamclient.so
#
# READ-ONLY: lee el fichero, no arranca Steam.

from __future__ import annotations

import argparse
import bisect
import os
import struct
import sys

# DWARF exception-header encodings (LSB Core Spec, §10.5)
DW_EH_PE_omit    = 0xFF
DW_EH_PE_uleb128 = 0x01
DW_EH_PE_udata2  = 0x02
DW_EH_PE_udata4  = 0x03
DW_EH_PE_udata8  = 0x04
DW_EH_PE_sleb128 = 0x09
DW_EH_PE_sdata2  = 0x0A
DW_EH_PE_sdata4  = 0x0B
DW_EH_PE_sdata8  = 0x0C

APP_absptr  = 0x00
APP_pcrel   = 0x10
APP_datarel = 0x30

ENC_NAMES = {
    0x03: "udata4", 0x0B: "sdata4",
    0x1B: "pcrel|sdata4", 0x3B: "datarel|sdata4",
    0x1A: "pcrel|sdata2", 0x3A: "datarel|sdata2",
    0xFF: "omit",
}


def enc_name(e):
    return ENC_NAMES.get(e, "0x%02x" % e)


def load_sections(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF" or data[4] != 1:
        raise ValueError("no es un ELF32")
    e_shoff = struct.unpack_from("<I", data, 0x20)[0]
    e_shentsize = struct.unpack_from("<H", data, 0x2E)[0]
    e_shnum = struct.unpack_from("<H", data, 0x30)[0]
    e_shstrndx = struct.unpack_from("<H", data, 0x32)[0]

    def shdr(i):
        o = e_shoff + i * e_shentsize
        name, _t, _f, addr, off, size = struct.unpack_from("<IIIIII", data, o)
        return name, addr, off, size

    strtab_off = shdr(e_shstrndx)[2]
    secs = {}
    for i in range(e_shnum):
        nmoff, addr, off, size = shdr(i)
        end = data.index(b"\x00", strtab_off + nmoff)
        secs[data[strtab_off + nmoff:end].decode("latin1")] = (addr, off, size)
    return data, secs


def read_encoded(data, off, enc, pc_va, data_va):
    """Devuelve (valor, nuevo_offset). Sólo las codificaciones de tamaño fijo que
    usa gcc aquí; una uleb/sleb haría falta parsearla y se avisa."""
    fmt = enc & 0x0F
    app = enc & 0x70
    if fmt == DW_EH_PE_udata4:
        v = struct.unpack_from("<I", data, off)[0]; n = 4
    elif fmt == DW_EH_PE_sdata4:
        v = struct.unpack_from("<i", data, off)[0]; n = 4
    elif fmt == DW_EH_PE_udata2:
        v = struct.unpack_from("<H", data, off)[0]; n = 2
    elif fmt == DW_EH_PE_sdata2:
        v = struct.unpack_from("<h", data, off)[0]; n = 2
    else:
        raise ValueError("codificación no soportada por esta sonda: 0x%02x" % enc)

    if app == APP_absptr:
        base = 0
    elif app == APP_pcrel:
        base = pc_va
    elif app == APP_datarel:
        base = data_va
    else:
        raise ValueError("aplicación de codificación no soportada: 0x%02x" % app)
    return (base + v) & 0xFFFFFFFF, off + n


def parse_eh_frame_hdr(data, secs):
    if ".eh_frame_hdr" not in secs:
        raise ValueError("el binario no tiene .eh_frame_hdr")
    hdr_va, hdr_off, hdr_size = secs[".eh_frame_hdr"]

    version = data[hdr_off]
    eh_frame_ptr_enc = data[hdr_off + 1]
    fde_count_enc = data[hdr_off + 2]
    table_enc = data[hdr_off + 3]

    info = {
        "va": hdr_va, "size": hdr_size, "version": version,
        "eh_frame_ptr_enc": eh_frame_ptr_enc,
        "fde_count_enc": fde_count_enc,
        "table_enc": table_enc,
    }
    if version != 1:
        raise ValueError("versión %d desconocida" % version)

    off = hdr_off + 4
    eh_frame_ptr, off = read_encoded(data, off, eh_frame_ptr_enc,
                                     hdr_va + (off - hdr_off), hdr_va)
    fde_count, off = read_encoded(data, off, fde_count_enc,
                                  hdr_va + (off - hdr_off), hdr_va)
    info["eh_frame_ptr"] = eh_frame_ptr
    info["fde_count"] = fde_count

    # La tabla: fde_count pares (initial_location, fde_ptr), ordenados.
    starts, fdes = [], []
    for _ in range(fde_count):
        loc, off = read_encoded(data, off, table_enc, hdr_va + (off - hdr_off), hdr_va)
        fde, off = read_encoded(data, off, table_enc, hdr_va + (off - hdr_off), hdr_va)
        starts.append(loc)
        fdes.append(fde)
    info["table_bytes"] = off - hdr_off
    return info, starts, fdes


def lookup(starts, addr, text_end=None):
    """Entrada de la función que contiene `addr`, y su límite superior.

    El límite es el initial_location de la entrada SIGUIENTE, que la tabla ya trae
    ordenado — por eso no hace falta decodificar la FDE para saber dónde acaba una
    función. La última entrada no tiene siguiente, así que se acota con el final
    del span ejecutable; sin eso, cualquier dirección posterior al último inicio
    caería dentro de la última función y el fallo dejaría de ser cerrado.

    Devuelve (None, None) si `addr` cae fuera de toda función."""
    i = bisect.bisect_right(starts, addr) - 1
    if i < 0:
        return None, None
    end = starts[i + 1] if i + 1 < len(starts) else text_end
    if end is not None and addr >= end:
        return None, None
    return starts[i], end


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("binary")
    ap.add_argument("--probe-offsets", default="0,4,0x48,178,0x110",
                    help="offsets interiores a probar sobre cada entrada conocida")
    args = ap.parse_args()

    data, secs = load_sections(args.binary)
    tx_a, _tx_o, tx_s = secs.get(".text", (0, 0, 0))
    text_end = (tx_a + tx_s) if tx_a else None

    print("=" * 74)
    print("[1] .eh_frame_hdr")
    try:
        info, starts, fdes = parse_eh_frame_hdr(data, secs)
    except ValueError as e:
        print("    FALLO: %s" % e)
        return 1
    print("    va=0x%x  size=%d  version=%d" % (info["va"], info["size"], info["version"]))
    print("    eh_frame_ptr_enc = %-16s fde_count_enc = %s"
          % (enc_name(info["eh_frame_ptr_enc"]), enc_name(info["fde_count_enc"])))
    print("    table_enc        = %-16s eh_frame     = 0x%x"
          % (enc_name(info["table_enc"]), info["eh_frame_ptr"]))
    print()

    print("[2] tabla")
    print("    %d funciones, tabla de %d bytes" % (info["fde_count"], info["table_bytes"]))
    ordered = all(starts[i] <= starts[i + 1] for i in range(len(starts) - 1))
    print("    ordenada: %s   rango 0x%x .. 0x%x"
          % ("SÍ" if ordered else "NO (¡la búsqueda binaria no valdría!)",
             starts[0], starts[-1]))
    print()

    # Entradas conocidas: las del feed publicado para este build, si lo hay.
    here = os.path.dirname(os.path.abspath(__file__))
    import hashlib
    sha = hashlib.sha256(open(args.binary, "rb").read()).hexdigest()
    feed = os.path.join(os.path.dirname(here), "res", "rvas", sha + ".yaml")
    known = {}
    if os.path.isfile(feed):
        cur = None
        for line in open(feed, encoding="utf-8"):
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            if not line.startswith((" ", "\t")):
                cur = line.split(":")[0].strip(); continue
            if cur == "hooks":
                k, _, v = line.strip().partition(":")
                known[k.strip()] = int(v.strip().strip('"'), 16)
    if not known:
        print("[3] sin ficha en res/rvas para este build — nada que contrastar")
        return 1

    print("[3] ¿la tabla conoce nuestras entradas de hook?")
    exact = True
    for name, va in sorted(known.items(), key=lambda kv: kv[1]):
        start, nxt = lookup(starts, va)
        ok = (start == va)
        exact = exact and ok
        size = (nxt - start) if (nxt and start) else 0
        print("    %-12s 0x%08x  ->  entrada 0x%08x  %s  (tamaño aprox %d B)"
              % (name, va, start or 0, "EXACTA" if ok else "¡NO COINCIDE!", size))
    print()

    print("[4] la decisiva: direcciones INTERIORES -> ¿devuelven la entrada?")
    print("    (son los dos casos que hoy fallan: entrada+4 y un punto del cuerpo)")
    offsets = [int(x, 0) for x in args.probe_offsets.split(",")]
    allgood = True
    for name, va in sorted(known.items(), key=lambda kv: kv[1]):
        _s, own_end = lookup(starts, va, text_end)
        res = []
        for d in offsets:
            if own_end is not None and va + d >= own_end:
                # El offset se sale de ESTA función, así que devolver otra entrada
                # es el comportamiento correcto, no un fallo. Se compara contra el
                # límite de la función original, no contra el de la que salga.
                res.append("+%-5s %s" % (hex(d), "fuera"))
                continue
            start, _e = lookup(starts, va + d, text_end)
            good = (start == va)
            res.append("+%-5s %s" % (hex(d), "ok" if good else "MAL"))
            if not good:
                allgood = False
        print("    %-12s %s" % (name, "  ".join(res)))
    print()

    print("[5] fallo cerrado: ¿se rechaza lo que no es código?")
    print("    (límite de la última función = fin del span ejecutable, 0x%x)" % text_end)
    closed = True
    for probe, label in ((starts[0] - 0x1000, "antes de la primera función"),
                         (starts[-1] + 0x100000, "más allá de la última"),
                         (text_end + 0x10, "pasado el fin de .text")):
        start, _e = lookup(starts, probe, text_end)
        if start is None:
            verdict = "rechazada"
        else:
            verdict = "entrada 0x%08x  <- NO cierra" % start
            closed = False
        print("    0x%08x  %-28s -> %s" % (probe & 0xFFFFFFFF, label, verdict))
    print()

    print("=" * 74)
    ok = exact and allgood and ordered and closed
    print("VEREDICTO: %s" % ("VIABLE — cabecera + búsqueda binaria basta, "
                             "sin decodificar FDE ni CIE"
                             if ok else "revisar los puntos marcados arriba"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
