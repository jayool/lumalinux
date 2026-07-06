#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# experiment_rtti_depotkey.py — GROUND-TRUTH gate for the DepotKey→RTTI migration.
#
# Resolves DepotKey by RTTI on a steamclient.so — the same walk CloudRedirect's
# vtable_hook.cpp::FindVtableByRTTIName does at runtime, but on the STATIC file:
#
#   1. find the Itanium RTTI type-name string "12CConfigStore" -> its vaddr S
#   2. find the type_info: a pointer field whose stored value == S (on a shared
#      object, i386 REL relocations store the addend = target vaddr in-place, so
#      the file already holds S). The type_info object begins one word before
#      that name field, so typeinfo_vaddr = (name_field_vaddr) - 4.
#   3. find the vtable: the Itanium header is [offset_to_top=0, typeinfo_ptr];
#      scan for two consecutive words == (0, typeinfo_vaddr). The virtual fn
#      pointers start right after, so vtable_funcs = header + 8 bytes.
#   4. read slot 6 (vtable_funcs + 6*4) -> the LoadDepotDecryptionKey accessor.
#
# Then it resolves DepotKey the CURRENT way (byte pattern kDepotKeyFnPattern via
# check_patterns) and asserts they point at the SAME RVA. If they match across
# real builds, the RTTI resolver is safe to ship (it targets the exact function
# the byte pattern hooks today); if not, we learn it BEFORE writing any C++.
#
# vaddr == RVA here (a .so has image base 0), matching check_patterns' reporting.
#
#   python3 tools/experiment_rtti_depotkey.py <steamclient.so> [--hpp src/patterns.hpp]
#
# Runs on the CI runner (Valve's CDN is firewalled from the dev sandbox). The
# pure walk logic is unit-tested with synthetic segments in test_experiment_rtti.py.
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_patterns import load_exec_segments, parse_patterns_hpp, scan_rvas  # noqa: E402

RTTI_NAME = b"12CConfigStore\x00"
DEPOTKEY_SLOT = 6


def load_all_segments(path):
    """[(vaddr, bytes), ...] for every PT_LOAD segment (not just executable —
    the string/typeinfo/vtable live in .rodata / .data.rel.ro, which are not
    PF_X). Mirrors load_exec_segments but without the PF_X filter."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise ValueError("not a 32-bit little-endian ELF")
    e_phoff = struct.unpack_from("<I", data, 0x1C)[0]
    e_phentsize = struct.unpack_from("<H", data, 0x2A)[0]
    e_phnum = struct.unpack_from("<H", data, 0x2C)[0]
    segs = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_offset, p_vaddr = struct.unpack_from("<III", data, off)
        p_filesz = struct.unpack_from("<I", data, off + 16)[0]
        if p_type == 1:  # PT_LOAD
            segs.append((p_vaddr, data[p_offset:p_offset + p_filesz]))
    if not segs:
        raise ValueError("no PT_LOAD segment found")
    return segs


# ── walk primitives (operate on [(vaddr, bytes)] — unit-testable) ────────────

def find_bytes(segments, needle):
    out = []
    for vaddr, buf in segments:
        start = 0
        while True:
            i = buf.find(needle, start)
            if i < 0:
                break
            out.append(vaddr + i)
            start = i + 1
    return out


def find_u32(segments, value):
    """vaddrs of every 4-byte little-endian word == value (word-aligned scan)."""
    target = struct.pack("<I", value & 0xFFFFFFFF)
    out = []
    for vaddr, buf in segments:
        n = len(buf) - 3
        i = vaddr % 4
        i = (4 - i) % 4  # align the scan start to a 4-byte vaddr boundary
        while i < n:
            if buf[i:i + 4] == target:
                out.append(vaddr + i)
            i += 4
    return out


def read_u32(segments, vaddr):
    for base, buf in segments:
        if base <= vaddr < base + len(buf) - 3:
            return struct.unpack_from("<I", buf, vaddr - base)[0]
    return None


def rtti_resolve_slot(segments, name, slot):
    """Resolve `name`'s vtable and return (slot_vaddr, diagnostics). slot_vaddr
    is None if the walk fails; diag carries every step for reporting."""
    diag = {}
    strs = find_bytes(segments, name)
    diag["string_vaddrs"] = strs
    if not strs:
        return None, diag
    for s in strs:
        name_fields = find_u32(segments, s)          # typeinfo.name == string vaddr
        for nf in name_fields:
            typeinfo_vaddr = nf - 4                    # type_info starts one word before name
            headers = find_u32(segments, typeinfo_vaddr)
            for h in headers:                          # vtable header: [0, typeinfo]
                if read_u32(segments, h - 4) == 0:
                    vtable_funcs = h + 4               # first virtual fn ptr
                    slot_vaddr = read_u32(segments, vtable_funcs + slot * 4)
                    diag.update(string_vaddr=s, typeinfo_vaddr=typeinfo_vaddr,
                                vtable_vaddr=vtable_funcs)
                    return slot_vaddr, diag
    return None, diag


def main():
    ap = argparse.ArgumentParser(description="Ground-truth check: DepotKey RTTI slot 6 vs byte pattern.")
    ap.add_argument("steamclient")
    ap.add_argument("--hpp", default="src/patterns.hpp")
    args = ap.parse_args()
    try:
        all_segs = load_all_segments(args.steamclient)
        exec_segs = load_exec_segments(args.steamclient)
        patterns = parse_patterns_hpp(args.hpp)
    except (OSError, ValueError) as e:
        print("ERROR: %s" % e, file=sys.stderr)
        return 1

    print("DepotKey RTTI ground-truth check on %s\n" % args.steamclient)

    rtti_addr, diag = rtti_resolve_slot(all_segs, RTTI_NAME, DEPOTKEY_SLOT)
    print("RTTI walk (12CConfigStore -> vtable -> slot 6):")
    print("  string  @ %s" % (", ".join("0x%x" % v for v in diag.get("string_vaddrs", [])) or "NOT FOUND"))
    if "typeinfo_vaddr" in diag:
        print("  typeinfo @ 0x%x" % diag["typeinfo_vaddr"])
        print("  vtable   @ 0x%x" % diag["vtable_vaddr"])
    print("  slot 6   -> %s" % (("0x%x" % rtti_addr) if rtti_addr else "UNRESOLVED"))

    pat = patterns.get("kDepotKeyFnPattern")
    pat_rvas = scan_rvas(exec_segs, pat) if pat else []
    pat_addr = pat_rvas[0] if len(pat_rvas) == 1 else None
    print("\nByte pattern (kDepotKeyFnPattern):")
    print("  matches  -> %d %s" % (len(pat_rvas), "(" + ", ".join("0x%x" % r for r in pat_rvas[:4]) + ")" if pat_rvas else ""))

    print("")
    if rtti_addr and pat_addr and rtti_addr == pat_addr:
        print("VERDICT: MATCH ✓  RTTI slot 6 == byte pattern @ 0x%x — RTTI resolver is safe to ship." % rtti_addr)
        return 0
    if rtti_addr and pat_addr:
        print("VERDICT: MISMATCH ✗  RTTI=0x%x  pattern=0x%x — do NOT migrate; investigate." % (rtti_addr, pat_addr))
        return 2
    print("VERDICT: INCONCLUSIVE — one side did not resolve uniquely (see above).")
    return 2


if __name__ == "__main__":
    sys.exit(main())
