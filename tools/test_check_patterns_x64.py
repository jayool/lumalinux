#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Unit test for the pure scanners in tools/check_patterns_x64.py (the x86-64
# feed deriver): rip-relative lea sites, the Reconcile criteria in SysV shape,
# the .eh_frame_hdr function lookup and the feed emitter's policy. The real
# binary is exercised by .github/workflows/fetch-steamclient.yml, which runs the
# tool on every steamrt64 package it downloads; this covers the byte logic
# without a binary.
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_patterns_x64 as cp  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        fails += 1


# ── rip_lea_sites ─────────────────────────────────────────────────────────────
tx_va = 0x1000
target = 0x5000
# lea rsi,[rip+disp] with REX.W at tx+0x10: insn end = 0x1017; disp = target - end
disp = target - (tx_va + 0x10 + 7)
tx = bytearray(b"\x90" * 0x40)
tx[0x10:0x17] = b"\x48\x8d\x35" + struct.pack("<i", disp)
# lea esi,[rip+disp] without REX at tx+0x20: end = 0x1026
tx[0x20:0x26] = b"\x8d\x35" + struct.pack("<i", target - (tx_va + 0x20 + 6))
# a non-rip 8D (modrm mod=10) with a disp that would otherwise "match"
tx[0x30:0x36] = b"\x8d\xb6" + struct.pack("<i", target - (tx_va + 0x30 + 6))
sites = cp.rip_lea_sites(bytes(tx), tx_va, target)
check(sites == [tx_va + 0x10, tx_va + 0x20], "rip_lea_sites: REX site at the REX byte, non-REX site at 8D, [reg+disp] ignored (%s)" % [hex(s) for s in sites])
check(cp.rip_lea_sites(bytes(tx), tx_va, target + 1) == [], "rip_lea_sites: no site for another target")

# ── Reconcile criteria (SysV) ────────────────────────────────────────────────
head_ok = (b"\x41\x57\x41\x56"                       # push r15; push r14
           b"\x44\x8b\x8f\xc4\x23\x00\x00"           # mov r9d,[rdi+0x23c4]
           b"\x31\xc0"                               # xor eax,eax
           b"\x45\x85\xc9"                           # test r9d,r9d
           b"\x0f\x8e\xe3\x02\x00\x00")              # jle
ok, d = cp.reads_this_then_bails64(head_ok)
check(ok and d == 0x23c4, "reads_this_then_bails64: this-read + test + jle (disp 0x%x)" % d)
head_no_jle = head_ok.replace(b"\x0f\x8e", b"\x0f\x85")
check(cp.reads_this_then_bails64(head_no_jle)[0] is False, "reads_this_then_bails64: jne instead of jle refused")
head_rsi = head_ok.replace(b"\x44\x8b\x8f", b"\x44\x8b\x8e")   # [rsi+disp]: not `this`
check(cp.reads_this_then_bails64(head_rsi)[0] is False, "reads_this_then_bails64: a read off rsi is not a this-read")

code = bytearray(b"\x90" * 0x80)
code[0x10:0x15] = b"\xbe\x7d\x00\x00\x00"            # mov esi,0x7d
code[0x1c] = 0xe8                                    # call within 24 bytes
code[0x40:0x45] = b"\xbe\x7d\x00\x00\x00"            # mov esi,0x7d, no call after
s = cp.reconcile_sites64(bytes(code), 0x2000)
check(s == [0x2010], "reconcile_sites64: mov esi,0x7d needs a call within 24 bytes (%s)" % [hex(x) for x in s])

# ── func_of over a sorted table ──────────────────────────────────────────────
starts = [0x100, 0x200, 0x300]
check(cp.func_of(starts, 0x400, 0x250) == (0x200, 0x300), "func_of: inside the second function")
check(cp.func_of(starts, 0x400, 0x350) == (0x300, 0x400), "func_of: last function ends at exec_end")
check(cp.func_of(starts, 0x400, 0x50) == (None, None), "func_of: before the first function")
check(cp.func_of(starts, 0x400, 0x400) == (None, None), "func_of: past the end")

# ── licence re-check flags ───────────────────────────────────────────────────
md = cp.capstone_or_none()
if md is not None:
    callee = 0x3000
    code = bytearray()
    code += b"\xc6\x83\x70\x23\x00\x00\x00"            # mov byte ptr [rbx+0x2370],0
    code += b"\x48\x89\xdf"                               # mov rdi,rbx
    at = 0x2000 + len(code)
    code += b"\xe8" + struct.pack("<i", callee - (at + 5))  # call callee
    code += b"\xc6\x83\x80\x23\x00\x00\x00"            # mov byte ptr [rbx+0x2380],0 (after the call: ignored)
    code += b"\xe8" + struct.pack("<i", 0x4000 - (0x2000 + len(code) + 5))  # call elsewhere
    d = cp.flag_clear_before_call(md, bytes(code), 0x2000, callee)
    check(d == 0x2370, "flag_clear_before_call: the byte clear right before the call (0x%x)" % (d or 0))
    check(cp.flag_clear_before_call(md, bytes(code), 0x2000, 0x4000) == 0x2380, "flag_clear_before_call: each call sees its own preceding clear")
    check(cp.flag_clear_before_call(md, bytes(code), 0x2000, 0x5000) is None, "flag_clear_before_call: no such call -> None")
    code2 = b"\xc6\x83\x70\x23\x00\x00\x01" + b"\xe8" + struct.pack("<i", callee - (0x2000 + 7 + 5))   # mov byte,1 is not a clear
    check(cp.flag_clear_before_call(md, code2, 0x2000, callee) is None, "flag_clear_before_call: a store of 1 is not a clear")
else:
    print("skip flag_clear_before_call (no capstone)")
tx2 = bytearray(b"\x90" * 0x40)
tx2[0x10:0x15] = b"\xb9\x01\x01\x00\x00"                       # mov ecx,0x101
tx2[0x15:0x1c] = b"\x66\x89\x8f" + struct.pack("<I", 0x2370)    # mov word ptr [rdi+0x2370],cx
tx2[0x30:0x35] = b"\xb9\x01\x01\x00\x00"                       # mov ecx,0x101 with no store after
s2 = cp.license_setter_sites(bytes(tx2), 0x6000, 0x2370)
check(s2 == [0x6010], "license_setter_sites: mov ecx,0x101 + word store at the offset (%s)" % [hex(x) for x in s2])
check(cp.license_setter_sites(bytes(tx2), 0x6000, 0x2380) == [], "license_setter_sites: another offset has no setter")

# ── emit policy ──────────────────────────────────────────────────────────────
result = {"steamclient_sha256": "ab" * 32,
          "hooks": {"DepotKey": {"status": "UNIQUE", "rva": "0x1"}, "GMRC": {"status": "UNIQUE", "rva": "0x2"},
                    "ShaderDepot": {"status": "NO_SITE", "rva": None}, "BuildDep": {"status": "UNIQUE", "rva": "0x3"},
                    "Reconcile": {"status": "UNIQUE", "rva": "0x4"}},
          "rtti_byname": {"status": "UNIQUE", "class": "12CConfigStore", "slot": 6, "rva": "0x1"},
          "finder": {"status": "UNIQUE", "cache_global_rva": "0x331f210", "cache_root_off": "0x14b0", "cache_nodes_off": "0x14c8"},
          "license_check": {"status": "UNIQUE", "licenses_check_off": "0x2370"}}
with tempfile.TemporaryDirectory() as td:
    path = cp.emit_rvas_file(result, td, "1792192922")
    text = open(path).read()
    check(os.path.basename(path) == "ab" * 32 + ".yaml", "emit: file named by sha256")
    check("BuildDep" not in text, "emit: BuildDep (pin-only) is never published")
    check("ShaderDepot" not in text, "emit: a hook that did not resolve is left out")
    check(text.index("DepotKey:") < text.index("Reconcile:") < text.index("GMRC:"), "emit: 32-bit file order")
    check("client_arch: steamrt64" in text and 'cache_global_rva: "0x331f210"' in text, "emit: arch tag and the 64-bit finder keys")
    check("  slot: 6" in text, "emit: depotkey_rtti block")
    check(text.rstrip().endswith('reconcile:\n  licenses_check_off: "0x2370"'), "emit: reconcile block with the licence re-check offset")
    result["license_check"] = {"status": "ABSENT"}
    text = open(cp.emit_rvas_file(result, td, "1792618285")).read()
    check("reconcile:" not in text, "emit: no reconcile block on a build without the flags")

print("\n" + ("all ok" if not fails else "%d FAILED" % fails))
sys.exit(1 if fails else 0)
