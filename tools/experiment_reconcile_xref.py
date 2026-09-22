#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# experiment_reconcile_xref.py — sonda de MEDICIÓN (2026-09-22), no forma parte de CI.
#   python3 tools/experiment_reconcile_xref.py ~/so/stable.so ~/so/beta.so
# Medición: ¿se puede derivar Reconcile (CUser::NotifyLicensesUpdated) por xref
# del type_info de LicensesUpdated_t, con la misma técnica que el rescate de GMRC?
# Y de paso, ¿cuántos `push 0x7d ; push eax ; call` hay (B.1)?
import bisect, struct, sys
sys.path.insert(0, "tools")
import check_patterns as cp

EXPECTED = {"bc54101b": 0x188c950, "9cf4720f": 0x1a0d010}

def fn_of(starts, va):
    i = bisect.bisect_right(starts, va) - 1
    return starts[i] if i >= 0 else None

for so in sys.argv[1:]:
    print("=" * 70); print(so)
    data, secs = cp._load_sections(so)
    got = cp._got_base_consensus(data, secs)
    starts, _info, note = cp.eh_frame_starts(so)
    print("  GOT 0x%x  eh_frame %s (%d fns)" % (got, note, len(starts or [])))
    tx_a, tx_o, tx_s = secs[".text"]; tx = data[tx_o:tx_o + tx_s]

    # 1) la string del nombre RTTI
    needle = b"17LicensesUpdated_t\x00"
    s_va = None
    for nm, (a, o, sz) in sorted(secs.items()):
        if a and sz:
            j = data[o:o + sz].find(needle)
            if j >= 0:
                s_va = a + j; print("  nombre RTTI @0x%x (%s)" % (s_va, nm)); break
    if s_va is None:
        print("  nombre RTTI NO encontrado"); continue

    # 2) el objeto type_info: {vtable*, name*} -> palabra == s_va en secciones de datos
    ti = []
    for nm, (a, o, sz) in sorted(secs.items()):
        if not a or not sz or nm.startswith(".text") or nm.startswith(".rodata"):
            continue
        blob = data[o:o + sz]; k = 0
        while True:
            j = blob.find(struct.pack("<I", s_va), k)
            if j < 0: break
            k = j + 4
            if j >= 4 and (j % 4) == 0:
                ti.append((a + j - 4, nm))
    print("  candidatos type_info:", ["0x%x(%s)" % t for t in ti])

    # 3) referencias en código: (a) lea reg,[GOT+disp]  (b) mov reg,[GOT+slot] con slot->ti
    for ti_va, nm in ti:
        disp = struct.pack("<I", (ti_va - got) & 0xFFFFFFFF)
        sites = []
        i = 0
        while True:
            j = tx.find(disp, i)
            if j < 0: break
            i = j + 1
            if j >= 2 and tx[j - 2] in (0x8D, 0x8B) and (tx[j - 1] & 0xC0) == 0x80 and (tx[j - 1] & 7) != 4:
                sites.append(("lea" if tx[j-2] == 0x8D else "mov", tx_a + j - 2))
        # slots de la GOT cuyo contenido (en fichero) sea ti_va
        for gnm in (".got", ".got.plt", ".data.rel.ro", ".data"):
            if gnm not in secs: continue
            a, o, sz = secs[gnm]; blob = data[o:o + sz]; k = 0
            while True:
                j = blob.find(struct.pack("<I", ti_va), k)
                if j < 0: break
                k = j + 4
                slot_disp = struct.pack("<I", (a + j - got) & 0xFFFFFFFF)
                i2 = 0
                while True:
                    j2 = tx.find(slot_disp, i2)
                    if j2 < 0: break
                    i2 = j2 + 1
                    if j2 >= 2 and tx[j2 - 2] == 0x8B and (tx[j2 - 1] & 0xC0) == 0x80 and (tx[j2 - 1] & 7) != 4:
                        sites.append(("mov-slot(%s)" % gnm, tx_a + j2 - 2))
        print("  type_info 0x%x: %d referencia(s)" % (ti_va, len(sites)))
        for kind, va in sites:
            f = fn_of(starts, va) if starts else None
            tag = ""
            for k8, exp in EXPECTED.items():
                if k8 in so and f == exp: tag = "  <== Reconcile esperado"
            print("     %-22s @0x%08x  en función 0x%08x%s" % (kind, va, f or 0, tag))

    # 4) B.1: push 0x7d ; push eax ; call
    hits = []
    i = 0
    while True:
        j = tx.find(b"\x6A\x7D\x50\xE8", i)
        if j < 0: break
        i = j + 1
        hits.append(tx_a + j)
    print("  B.1 `6A 7D 50 E8`: %d sitio(s)" % len(hits))
    for va in hits[:12]:
        f = fn_of(starts, va) if starts else None
        tag = "  <== Reconcile esperado" if any(k in so and f == e for k, e in EXPECTED.items()) else ""
        print("     @0x%08x  en función 0x%08x%s" % (va, f or 0, tag))


# ── 5) ¿es NotifyLicensesUpdated un método VIRTUAL? (familia DepotKey: RTTI + slot)
# Busca la dirección de la función como entrada de vtable en las secciones de datos.
# Layout Itanium: [offset_to_top=0][type_info*][slot0][slot1]... Sube desde la
# entrada hasta el par (0, ptr) que abre la vtable y lee el nombre de la clase.
# ── 6) Opción 3: de los sitios `6A 7D 50 E8`, ¿cuántas funciones tienen el prólogo
# de Reconcile (55 89 E5 57 56 53 E8 …) y una lectura mov r,[eax+0x1bXX]?
def _extra(so):
    data, secs = cp._load_sections(so)
    starts, _i, _n = cp.eh_frame_starts(so)
    read_va = cp._read_va(data, secs)
    tx_a, tx_o, tx_s = secs[".text"]; tx = data[tx_o:tx_o + tx_s]
    hits = []
    i = 0
    while True:
        j = tx.find(b"\x6A\x7D\x50\xE8", i)
        if j < 0: break
        i = j + 1
        hits.append(tx_a + j)
    fns = sorted({fn_of(starts, h) for h in hits if fn_of(starts, h)})

    print("  [5] entradas de vtable que apuntan a cada función candidata:")
    data_secs = [(nm, a, o, sz) for nm, (a, o, sz) in secs.items()
                 if a and sz and nm in (".data.rel.ro", ".data.rel.ro.local", ".data", ".rodata")]
    for f in fns:
        w = struct.pack("<I", f)
        for nm, a, o, sz in data_secs:
            blob = data[o:o + sz]; k = 0
            while True:
                j = blob.find(w, k)
                if j < 0: break
                k = j + 4
                if j % 4: continue
                # subir hasta [0][ti] : buscar el 0 de offset_to_top
                slot = None
                q = j
                while q >= 8:
                    if struct.unpack_from("<I", blob, q - 8)[0] == 0 and struct.unpack_from("<I", blob, q - 4)[0] != 0:
                        slot = (j - q) // 4; ti = struct.unpack_from("<I", blob, q - 4)[0]; break
                    q -= 4
                if slot is None:
                    print("     fn 0x%08x: palabra @0x%08x (%s) sin cabecera de vtable cerca" % (f, a + j, nm)); continue
                name = read_va(struct.unpack_from("<I", read_va(ti, 8) or b"\0"*8, 4)[0], 64) if read_va(ti, 8) else b""
                name = name.split(b"\0")[0].decode("latin1", "replace") if name else "(type_info no legible en fichero)"
                print("     fn 0x%08x: vtable @0x%08x (%s) slot %d clase %s" % (f, a + q, nm, slot, name))

    print("  [6] de las %d funciones con `push 0x7d;push eax;call`, prólogo 55 89 E5 57 56 53 E8 y lectura [eax+0x1bXX]:" % len(fns))
    for f in fns:
        b = read_va(f, 64)
        pro = b[:6] == bytes.fromhex("5589E5575653") and b[6] == 0xE8
        # 8B ?? xx 1B 00 00 en los primeros 64 bytes (mov r,[eax+0x1bXX])
        member = any(b[k] == 0x8B and (b[k+1] & 0xC0) == 0x80 and b[k+3] == 0x1B and b[k+4] == 0 and b[k+5] == 0
                     for k in range(0, 58))
        mark = "  <== candidato" if pro and member else ""
        print("     fn 0x%08x  prólogo=%s  miembro0x1bXX=%s%s" % (f, pro, member, mark))

for so in sys.argv[1:]:
    print("=" * 70); print(so, "(pasadas 5 y 6)")
    _extra(so)
