#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Unit test for experiment_rtti_depotkey.py's RTTI walk, using a synthetic
# Itanium layout in memory (no steamclient.so needed). Builds:
#   .rodata @0x1000 : "12CConfigStore\0"
#   .data   @0x2000 : type_info [own_vtptr, name->0x1000] at 0x2000
#                     vtable header [0, typeinfo=0x2000] at 0x2100
#                     fn slots start at 0x2108; slot 6 (@0x2120) = 0x1188300
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from experiment_rtti_depotkey import rtti_resolve_slot, RTTI_NAME  # noqa: E402

fails = 0


def check(cond, msg):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        fails += 1


def build_segments():
    rodata = bytearray(0x100)
    rodata[0:len(RTTI_NAME)] = RTTI_NAME          # string at vaddr 0x1000

    data = bytearray(0x200)
    struct.pack_into("<I", data, 0x000, 0x9999)   # type_info own vtable ptr (junk)
    struct.pack_into("<I", data, 0x004, 0x1000)   # type_info.name -> string vaddr
    struct.pack_into("<I", data, 0x100, 0x0)      # vtable header: offset_to_top = 0
    struct.pack_into("<I", data, 0x104, 0x2000)   # vtable header: typeinfo ptr
    for i in range(6):                            # fn slots 0..5 (junk, distinct)
        struct.pack_into("<I", data, 0x108 + i * 4, 0x300000 + i)
    struct.pack_into("<I", data, 0x120, 0x1188300)  # slot 6 = DepotKey target
    return [(0x1000, bytes(rodata)), (0x2000, bytes(data))]


segs = build_segments()

# 1) happy path: resolves to slot 6 == 0x1188300
addr, diag = rtti_resolve_slot(segs, RTTI_NAME, 6)
check(addr == 0x1188300, "resolves slot 6 to the planted target 0x1188300 (got %s)"
      % (hex(addr) if addr else None))
check(diag.get("typeinfo_vaddr") == 0x2000, "found type_info at 0x2000")
check(diag.get("vtable_vaddr") == 0x2108, "found vtable funcs at 0x2108")

# 2) a different slot reads a different (junk) target — proves slot indexing
addr0, _ = rtti_resolve_slot(segs, RTTI_NAME, 0)
check(addr0 == 0x300000, "slot 0 reads the first fn ptr (0x300000)")

# 3) missing RTTI string -> unresolved, no crash
addr_missing, diag_missing = rtti_resolve_slot(segs, b"99NoSuchClass\x00", 6)
check(addr_missing is None and diag_missing.get("string_vaddrs") == [],
      "missing RTTI string -> UNRESOLVED, no crash")

# 4) header must be preceded by offset_to_top==0: corrupt it -> no vtable found
bad = build_segments()
d = bytearray(bad[1][1]); struct.pack_into("<I", d, 0x100, 0xBADBAD)  # offset_to_top != 0
bad[1] = (0x2000, bytes(d))
addr_bad, _ = rtti_resolve_slot(bad, RTTI_NAME, 6)
check(addr_bad is None, "header without offset_to_top==0 is rejected -> UNRESOLVED")

print("")
if fails:
    print("%d CHECK(S) FAILED" % fails)
    sys.exit(1)
print("all checks passed")
