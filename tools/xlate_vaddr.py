#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# xlate_vaddr.py — PROTOTYPE for the "static RTTI + runtime translation" approach.
#
# Translates a steamclient.so FILE vaddr (RVA) to its RUNTIME address in a live
# process, correct even when the loader maps segments at DIFFERENT biases (the
# split-mapping build that breaks rtti.cpp's single-`base` walk). It does NOT
# assume one bias: it maps  rva -> file offset (via the ELF program headers) ->
# runtime address (via the file-offset column of /proc/<pid>/maps).
#
# Validation on the current build: the DepotKey accessor is at file vaddr
# 0x118c1f0 (static RTTI == byte pattern). Run this while Steam is up and compare
# the output to lumalinux's logged `DepotKey ... target=0x...` for that same run
# (ASLR differs per launch, so compare within one run).
#
#   python3 tools/xlate_vaddr.py <steamclient.so> [rva_hex] [--pid N] [--expect 0xADDR]
#   (rva defaults to 0x118c1f0, the DepotKey accessor on this build)
import glob
import struct
import sys


def parse_loads(path):
    """PT_LOAD segments of an ELF32: list of (p_offset, p_vaddr, p_filesz, flags)."""
    with open(path, "rb") as f:
        hdr = f.read(0x34)
        if hdr[:4] != b"\x7fELF" or hdr[4] != 1:
            sys.exit("not an ELFCLASS32 file")
        e_phoff = struct.unpack_from("<I", hdr, 0x1C)[0]
        e_phentsize = struct.unpack_from("<H", hdr, 0x2A)[0]
        e_phnum = struct.unpack_from("<H", hdr, 0x2C)[0]
        f.seek(e_phoff)
        ph = f.read(e_phentsize * e_phnum)
    loads = []
    for i in range(e_phnum):
        o = i * e_phentsize
        p_type, p_offset, p_vaddr, _, p_filesz, _, p_flags, _ = \
            struct.unpack_from("<IIIIIIII", ph, o)
        if p_type == 1:  # PT_LOAD
            loads.append((p_offset, p_vaddr, p_filesz, p_flags))
    return loads


def read_sc_mappings(maps_text):
    """steamclient.so mappings from a /proc/<pid>/maps text: (start, end, off, perms)."""
    out = []
    for line in maps_text.splitlines():
        if "steamclient.so" not in line:
            continue
        parts = line.split()
        if len(parts) < 3:
            continue
        s, e = (int(x, 16) for x in parts[0].split("-"))
        out.append((s, e, int(parts[2], 16), parts[1]))
    return out


def find_pid(explicit):
    if explicit:
        return explicit
    best = None  # prefer a process that ALSO has liblumalinux.so (the hooked one)
    for mp in glob.glob("/proc/[0-9]*/maps"):
        try:
            txt = open(mp).read()
        except OSError:
            continue
        if "steamclient.so" in txt:
            pid = int(mp.split("/")[2])
            has_luma = "liblumalinux.so" in txt
            if best is None or (has_luma and not best[0]):
                best = (has_luma, pid)
    if best is None:
        sys.exit("no live process has steamclient.so mapped (is Steam running?)")
    return best[1]


def translate(rva, loads, maps):
    """rva -> file offset (ELF phdrs) -> runtime address (maps file-offsets)."""
    seg = next((L for L in loads if L[1] <= rva < L[1] + L[2]), None)  # within p_filesz
    if not seg:
        sys.exit("rva 0x%x is not inside any file-backed PT_LOAD" % rva)
    p_offset, p_vaddr, _, flags = seg
    foff = p_offset + (rva - p_vaddr)
    m = next((M for M in maps if M[2] <= foff < M[2] + (M[1] - M[0])), None)
    if not m:
        sys.exit("file offset 0x%x not covered by any steamclient.so mapping" % foff)
    s, e, off, perms = m
    return s + (foff - off), (p_vaddr, p_offset, flags, foff, s, e, off, perms)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = {sys.argv[i]: sys.argv[i + 1]
             for i in range(1, len(sys.argv) - 1) if sys.argv[i].startswith("--")}
    if not args:
        sys.exit("usage: xlate_vaddr.py <steamclient.so> [rva_hex] [--pid N] [--expect 0x..]")
    path = args[0]
    rva = int(args[1], 16) if len(args) > 1 else 0x118c1f0
    pid = find_pid(int(flags["--pid"], 0) if "--pid" in flags else None)

    loads = parse_loads(path)
    maps = read_sc_mappings(open("/proc/%d/maps" % pid).read())
    runtime, d = translate(rva, loads, maps)
    p_vaddr, p_offset, fl, foff, s, e, off, perms = d

    print("pid           : %d" % pid)
    print("input rva     : 0x%x" % rva)
    print("  segment     : p_vaddr=0x%x p_offset=0x%x flags=%d -> file off 0x%x"
          % (p_vaddr, p_offset, fl, foff))
    print("  mapping      : %x-%x off=0x%x [%s]" % (s, e, off, perms))
    print("RESULT        : rva 0x%x -> runtime 0x%x" % (rva, runtime))
    if "--expect" in flags:
        exp = int(flags["--expect"], 16)
        print("EXPECT 0x%x   : %s" % (exp, "MATCH ✓" if runtime == exp else "MISMATCH"))


if __name__ == "__main__":
    main()
