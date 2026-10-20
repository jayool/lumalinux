#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# check_patterns_x64.py — CI-side VALIDATOR + RVA deriver for the x86-64
# (steamrt64) steamclient.so. The 64-bit counterpart of check_patterns.py.
#
# The 32-bit tool validates byte patterns from src/patterns.hpp; the 64-bit
# build has none — it resolves every hook through the RVA feed (res/rvas/
# <sha256>.yaml) and its runtime rescues are rip-relative string xrefs, the
# callback anchor and RTTI by name. So this tool DERIVES the feed values from
# the binary with the same locators the runtime uses, and VALIDATES the struct
# constants the 64-bit runtime hardcodes (node stride / PackageInfo* offset in
# the package cache, AppIdVec offset in PackageInfo). Nothing here is a byte
# pattern; everything is anchored on strings, RTTI and the shape of three
# functions. docs/steam-rt3-x64.md §1.3 is the measured reference.
#
# Usage:
#   python3 tools/check_patterns_x64.py <steamrt64/steamclient.so>
#       [--json result.json] [--emit-rvas DIR] [--steam-version N]
#
# Exit codes (the workflow branches on these, like check_patterns.py):
#   0  CLEAN      DepotKey + GMRC + finder + layouts resolved; feed emitted
#   2  NONCRIT    a non-critical hook (ShaderDepot, Reconcile) did not resolve;
#                 the feed is still emitted without it
#   3  BLOCKING   DepotKey, GMRC, the finder or a hardcoded layout failed —
#                 no feed (the .so would install nothing on this build)
#   1  usage / I/O / not an ELF64 / capstone missing
#
# Needs capstone (pip install capstone) for the two functions it reads by
# operands (the package-cache lookup and LoadPackage); the locators are plain
# byte scans.
import argparse
import bisect
import hashlib
import json
import os
import re
import struct
import sys

# ── runtime constants mirrored (the .so hardcodes these on x86_64) ───────────
RT_NODE_STRIDE      = 0x20     # package_zero_finder.cpp kNodeSize (LUMA_X86_64)
RT_NODE_PKGINFO_OFF = 0x18     # kNodePkgInfoOff
RT_NODE_PKGID_OFF   = 0x10     # kNodePkgIdOff
RT_APPIDVEC_OFF     = 0x40     # load_package_hook.hpp kOffAppIdVec
RT_APPIDVEC_SIZE    = 0x50     # m_Size = kOffAppIdVec + 0x10 (CUtlVector 64-bit)

ANCHORS = {            # label -> (needle, with NUL terminator?)
    "GMRC":        (b"ContentServerDirectory.GetManifestRequestCode#1", False),
    "ShaderDepot": (b"shadercachedepot", True),
    "BuildDep":    (b"BuildDepotDependency", True),
}
BYNAME = {"map_class": b"21IClientConfigStoreMap", "method": b"GetBinary",
          "impl_class": b"12CConfigStore", "max_slots": 64, "max_fn_size": 512}
PKG_LOOKUP_STRING = b"No package info for packageID %u found"
PKG_APPIDS_KEY    = b"appids"
CRITICAL = ("DepotKey", "GMRC")
NONCRITICAL = ("ShaderDepot", "Reconcile")


# ── ELF64 ────────────────────────────────────────────────────────────────────
class Elf64:
    def __init__(self, path):
        self.path = path
        self.data = bytearray(open(path, "rb").read())
        d = self.data
        if d[:5] != b"\x7fELF\x02":
            raise ValueError("not an ELF64 object")
        e_shoff = struct.unpack_from("<Q", d, 0x28)[0]
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", d, 0x3A)
        e_phoff = struct.unpack_from("<Q", d, 0x20)[0]
        e_phentsize, e_phnum = struct.unpack_from("<HH", d, 0x36)
        self.loads = []                       # (off, vaddr, filesz, memsz, flags)
        self.eh_frame_hdr_va = 0
        for i in range(e_phnum):
            o = e_phoff + i * e_phentsize
            p_type, p_flags, p_off, p_va, _pa, p_filesz, p_memsz = struct.unpack_from("<IIQQQQQ", d, o)
            if p_type == 1:
                self.loads.append((p_off, p_va, p_filesz, p_memsz, p_flags))
            elif p_type == 0x6474E550:
                self.eh_frame_hdr_va = p_va
        shdrs = []
        for i in range(e_shnum):
            o = e_shoff + i * e_shentsize
            sh = struct.unpack_from("<IIQQQQIIQQ", d, o)
            shdrs.append(sh)
        self.secs = {}                        # name -> (addr, off, size, type, entsize)
        stroff = shdrs[e_shstrndx][4]
        for name, typ, _flags, addr, off, size, _l, _i, _al, entsize in shdrs:
            e = d.index(b"\0", stroff + name)
            self.secs[d[stroff + name:e].decode()] = (addr, off, size, typ, entsize)
        if ".text" not in self.secs:
            raise ValueError("no .text")
        self.text = self.secs[".text"][:3]
        ex = [l for l in self.loads if l[4] & 1]
        if not ex:
            raise ValueError("no executable PT_LOAD")
        self.exec_load = ex[0]
        self._ranges = sorted((o, v, fs) for o, v, fs, _m, _f in self.loads)
        self.n_reloc = self._apply_relative()

    def va_off(self, va, need=1):
        for o, v, fs in self._ranges:
            if v <= va and va + need <= v + fs:
                return o + (va - v)
        return None

    def read(self, va, n):
        o = self.va_off(va, n)
        return bytes(self.data[o:o + n]) if o is not None else b""

    def u64(self, va):
        b = self.read(va, 8)
        return struct.unpack("<Q", b)[0] if len(b) == 8 else None

    def _apply_relative(self):
        """R_X86_64_RELATIVE (type 8): the file holds zeros, the value is the
        addend. Applying them makes vtables and typeinfo readable like at run
        time (load address 0)."""
        n = 0
        if ".rela.dyn" not in self.secs:
            return 0
        a, o, s, _t, es = self.secs[".rela.dyn"]
        es = es or 24
        for i in range(0, s - s % es, es):
            r_off, r_info, r_add = struct.unpack_from("<QQq", self.data, o + i)
            if (r_info & 0xffffffff) == 8:
                oo = self.va_off(r_off, 8)
                if oo is not None:
                    struct.pack_into("<Q", self.data, oo, r_add & 0xffffffffffffffff)
                    n += 1
        return n

    def in_text(self, va):
        a, _o, s = self.text
        return a <= va < a + s

    def exec_region(self):
        o, v, fs, _m, _f = self.exec_load
        return bytes(self.data[o:o + fs]), v

    def cstrings(self, needle, nul_after=True, nul_before=True):
        """VAs of exact occurrences in the non-executable loads."""
        out = []
        nd = needle + (b"\0" if nul_after else b"")
        for o, v, fs, _m, f in self.loads:
            if f & 1:
                continue
            blob = self.data[o:o + fs]
            i = 0
            while True:
                j = blob.find(nd, i)
                if j < 0:
                    break
                i = j + 1
                if nul_before and j > 0 and blob[j - 1] != 0:
                    continue
                out.append(v + j)
        return sorted(out)

    def ptr_slots(self, value):
        """8-byte aligned slots (after relocs) holding `value`, in data sections."""
        needle = struct.pack("<Q", value)
        out = []
        for nm in (".data.rel.ro", ".data.rel.ro.local", ".data", ".rodata"):
            if nm not in self.secs:
                continue
            a, o, s, _t, _e = self.secs[nm]
            blob = self.data[o:o + s]
            i = 0
            while True:
                j = blob.find(needle, i)
                if j < 0:
                    break
                i = j + 1
                if (a + j) % 8 == 0:
                    out.append(a + j)
        return out

    def vtable_funcs(self, mangled):
        """VA of the first function slot of every primary vtable of the class."""
        res = []
        for name_va in self.cstrings(mangled):
            for name_slot in self.ptr_slots(name_va):
                typeinfo = name_slot - 8
                for slot in self.ptr_slots(typeinfo):
                    if self.u64(slot - 8) == 0:          # offset_to_top == 0
                        res.append(slot + 8)
        return sorted(set(res))

    def slots(self, vt, max_slots):
        out = []
        for k in range(max_slots):
            fn = self.u64(vt + k * 8)
            if fn is None or not self.in_text(fn):
                break
            out.append(fn)
        return out


# ── code scans (byte-level, mirror of the runtime cores) ─────────────────────
def rip_lea_sites(tx, tx_va, target):
    """Sites of `lea r,[rip+disp32]` resolving to target (gmrc_xref_core
    ScanRipLeaAll): 8D with modrm mod=00 rm=101, optional REX before it."""
    out = []
    i = 0
    n = len(tx) - 6
    while True:
        j = tx.find(b"\x8d", i)
        if j < 0 or j > n:
            break
        i = j + 1
        if (tx[j + 1] & 0xC7) != 0x05:
            continue
        disp = struct.unpack_from("<i", tx, j + 2)[0]
        if tx_va + j + 6 + disp != target:
            continue
        rex = j > 0 and (tx[j - 1] & 0xF0) == 0x40
        out.append(tx_va + j - (1 if rex else 0))
    return out


def eh_frame_starts(e):
    """Sorted function starts from .eh_frame_hdr (datarel/pcrel/abs sdata4|udata4)."""
    hv = e.eh_frame_hdr_va
    h = e.read(hv, 16)
    if len(h) < 16 or h[0] != 1:
        raise ValueError("no usable .eh_frame_hdr")
    full_off = e.va_off(hv)
    d = e.data

    def rd(off, enc):
        fmt, app = enc & 0x0F, enc & 0x70
        if fmt == 0x03:
            v = struct.unpack_from("<I", d, full_off + off)[0]
        elif fmt == 0x0B:
            v = struct.unpack_from("<i", d, full_off + off)[0]
        else:
            raise ValueError("eh_frame_hdr encoding 0x%02x" % enc)
        base = {0x00: 0, 0x10: hv + off, 0x30: hv}[app]
        return base + v, off + 4
    _p, off = rd(4, h[1])
    count, off = rd(off, h[2])
    starts = []
    for _ in range(count):
        loc, off = rd(off, h[3])
        _f, off = rd(off, h[3])
        starts.append(loc)
    if starts != sorted(starts):
        raise ValueError("eh_frame_hdr table not sorted")
    return starts


def func_of(starts, exec_end, va):
    i = bisect.bisect_right(starts, va) - 1
    if i < 0:
        return None, None
    end = starts[i + 1] if i + 1 < len(starts) else exec_end
    if va >= end:
        return None, None
    return starts[i], end


def reconcile_sites64(tx, tx_va):
    """c1 on x86-64: mov esi,0x7d followed by a call within 24 bytes
    (reconcile_anchor_core FindCallbackPostSites64)."""
    out = []
    for m in re.finditer(rb"\xbe\x7d\x00\x00\x00", tx):
        j = m.start()
        if b"\xe8" in tx[j + 5:j + 5 + 24]:
            out.append(tx_va + j)
    return out


def reads_this_then_bails64(head):
    """c2+c3 (ReadsThisThenBails64): [REX] 8B modrm(mod=10,rm=rdi) disp32, then
    [REX] 85 /r … 0F 8E within 32 bytes."""
    n = len(head)
    k = 0
    while k + 7 < n:
        o = k
        if (head[o] & 0xF0) == 0x40:
            o += 1
        if o + 6 <= n and head[o] == 0x8B and (head[o + 1] & 0xC7) == 0x87:
            disp = struct.unpack_from("<I", head, o + 2)[0]
            w0, w1 = o + 6, min(o + 6 + 32, n)
            for t in range(w0, w1 - 3):
                tt = t + 1 if (head[t] & 0xF0) == 0x40 else t
                if tt + 4 <= w1 and head[tt] == 0x85 and head[tt + 2] == 0x0F and head[tt + 3] == 0x8E:
                    return True, disp
            return False, disp
        k += 1
    return False, 0


# ── capstone-backed reads of two functions ───────────────────────────────────
def capstone_or_none():
    try:
        import capstone  # noqa: F401
        from capstone import Cs, CS_ARCH_X86, CS_MODE_64
        md = Cs(CS_ARCH_X86, CS_MODE_64)
        md.detail = True
        return md
    except Exception:  # noqa: BLE001
        return None


def derive_finder(e, md, starts, exec_end, result):
    """From the package-cache lookup (the function loading
    'No package info for packageID %u found'): the .bss slot of the cache
    object (lea r,[rip+X] whose target is in .bss), the root-index and node-
    array offsets (movsxd r,[base+root]; mov r,[base+nodes]), the node stride
    (shl r,5) and the PackageInfo* offset ([nodes+idx*stride+off])."""
    from capstone import x86
    info = {"status": "NOT_FOUND"}
    strs = e.cstrings(PKG_LOOKUP_STRING, nul_after=False, nul_before=True)
    tx, tx_va = e.exec_region()
    sites = [s for v in strs for s in rip_lea_sites(tx, tx_va, v)]
    fns = sorted(set(func_of(starts, exec_end, s)[0] for s in sites if func_of(starts, exec_end, s)[0]))
    info["string_sites"] = ["0x%x" % s for s in sites]
    if len(fns) != 1:
        info["status"] = "AMBIGUOUS" if fns else "NO_FUNCTION"
        info["functions"] = ["0x%x" % f for f in fns]
        return info
    f, en = func_of(starts, exec_end, fns[0])
    info["function"] = "0x%x" % f
    bss = e.secs.get(".bss")
    slot = root = nodes = stride = pkginfo = None
    for ins in md.disasm(e.read(f, en - f), f):
        for op in ins.operands:
            if op.type != x86.X86_OP_MEM:
                continue
            m = op.mem
            if ins.mnemonic == "lea" and m.base == x86.X86_REG_RIP:
                tgt = ins.address + ins.size + m.disp
                if bss and bss[0] <= tgt < bss[0] + bss[2]:
                    slot = tgt
            elif ins.mnemonic == "movsxd" and m.base and m.index == 0 and m.disp > 0x100:
                root = m.disp
            elif ins.mnemonic == "mov" and m.base and m.index == 0 and m.disp > 0x100 and op.size == 8 and op.access & 1:
                nodes = m.disp
            elif ins.mnemonic == "mov" and m.base and m.index and m.scale == 1 and 0 < m.disp < 0x40 and op.size == 8:
                pkginfo = m.disp
        if ins.mnemonic == "shl" and len(ins.operands) == 2 and ins.operands[1].type == x86.X86_OP_IMM:
            stride = 1 << ins.operands[1].imm
    info.update({"cache_global_rva": "0x%x" % slot if slot else None,
                 "cache_root_off": "0x%x" % root if root else None,
                 "cache_nodes_off": "0x%x" % nodes if nodes else None,
                 "node_stride": "0x%x" % stride if stride else None,
                 "node_pkginfo_off": "0x%x" % pkginfo if pkginfo else None})
    if not (slot and root and nodes and stride and pkginfo):
        info["status"] = "PARTIAL"
        return info
    if nodes <= root or nodes - root > 0x40:
        info["status"] = "IMPLAUSIBLE_LAYOUT"
        return info
    info["status"] = "UNIQUE"
    info["layout_matches_runtime"] = (stride == RT_NODE_STRIDE and pkginfo == RT_NODE_PKGINFO_OFF)
    return info


def derive_pkginfo_layout(e, md, starts, exec_end):
    """From LoadPackage (the one function, besides its initializer, that reads
    the 'appids' CUtlSymbol slot): the AppIdVec offset is the base+disp pair
    that is both lea'd (vector object) and whose +0x10 is written as a dword
    (m_Size). The runtime hardcodes 0x40."""
    from capstone import x86
    info = {"status": "NOT_FOUND"}
    tx, tx_va = e.exec_region()
    key_strs = e.cstrings(PKG_APPIDS_KEY)
    sites = [s for v in key_strs for s in rip_lea_sites(tx, tx_va, v)]
    if len(sites) != 1:
        info["status"] = "AMBIGUOUS_KEY_STRING" if sites else "NO_KEY_STRING"
        return info
    # the initializer: lea rsi,<"appids"> ; lea rdi,[rip+slot] ; call
    ins = list(md.disasm(e.read(sites[0], 16), sites[0]))
    if len(ins) < 2 or ins[1].mnemonic != "lea":
        info["status"] = "NO_SYMBOL_SLOT"
        return info
    slot = ins[1].address + ins[1].size + ins[1].operands[1].mem.disp
    info["symbol_slot"] = "0x%x" % slot
    init_fn = func_of(starts, exec_end, sites[0])[0]
    # every rip-relative reference to the slot (any instruction: last 4 bytes)
    refs = []
    for j in range(0, len(tx) - 4):
        v = struct.unpack_from("<i", tx, j)[0]
        if tx_va + j + 4 + v == slot:
            refs.append(tx_va + j)
    fns = sorted(set(func_of(starts, exec_end, r)[0] for r in refs) - {init_fn, None})
    info["readers"] = ["0x%x" % f for f in fns]
    if len(fns) != 1:
        info["status"] = "AMBIGUOUS_READER" if fns else "NO_READER"
        return info
    f, en = func_of(starts, exec_end, fns[0])
    info["loadpackage"] = "0x%x" % f
    lea_disps, size_writes = set(), set()
    for i in md.disasm(e.read(f, en - f), f):
        for op in i.operands:
            if op.type != x86.X86_OP_MEM or op.mem.base in (x86.X86_REG_RSP, x86.X86_REG_RBP, x86.X86_REG_RIP) or op.mem.index:
                continue
            if i.mnemonic == "lea":
                lea_disps.add(op.mem.disp)
            elif i.mnemonic == "mov" and op.size == 4 and (op.access & 2):
                size_writes.add(op.mem.disp)
    cands = sorted(d for d in lea_disps if d + 0x10 in size_writes and d > 0)
    info["vector_candidates"] = ["0x%x" % c for c in cands]
    if not cands:
        info["status"] = "NO_VECTOR"
        return info
    info["appidvec_off"] = "0x%x" % cands[0]        # AppIdVec precedes DepotIdVec
    info["status"] = "UNIQUE"
    info["layout_matches_runtime"] = cands[0] == RT_APPIDVEC_OFF
    return info


# ── emit ─────────────────────────────────────────────────────────────────────
def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def emit_rvas_file(result, out_dir, steam_version):
    sha = result["steamclient_sha256"]
    lines = ["# Auto-generated by tools/check_patterns_x64.py --emit-rvas. Do NOT edit by hand.",
             "# Per-build hook RVAs (file vaddrs) of the x86-64 (steamrt64) steamclient.so,",
             "# keyed by its SHA-256 and consumed by the 64-bit runtime, which resolves",
             "# through this feed first (docs/steam-rt3-x64.md §1.3).",
             'steamclient_sha256: "%s"' % sha]
    if steam_version:
        lines.append("steam_version: %s" % steam_version)
    lines.append("client_arch: steamrt64")
    # Same policy as the 32-bit feed: the hooks the runtime installs by default
    # (BuildDep is pin-only and off; LoadPackage is a debug aid), in the 32-bit
    # file's order so the two feeds read alike.
    order = ("DepotKey", "ShaderDepot", "Reconcile", "GMRC")
    hooks = [(k, result["hooks"][k]["rva"]) for k in order
             if result["hooks"].get(k, {}).get("status") == "UNIQUE" and result["hooks"][k].get("rva")]
    lines.append("hooks:" if hooks else "hooks: {}")
    lines += ['  %s: "%s"' % (k, r) for k, r in hooks]
    bn = result.get("rtti_byname", {})
    if bn.get("status") == "UNIQUE":
        lines += ["depotkey_rtti:", '  class: "%s"' % bn["class"], "  slot: %d" % bn["slot"], '  rva: "%s"' % bn["rva"]]
    fi = result.get("finder", {})
    if fi.get("status") == "UNIQUE":
        lines += ["finder:",
                  '  cache_global_rva: "%s"' % fi["cache_global_rva"],
                  '  cache_root_off: "%s"' % fi["cache_root_off"],
                  '  cache_nodes_off: "%s"' % fi["cache_nodes_off"]]
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, sha + ".yaml")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
    return path


# ── main ─────────────────────────────────────────────────────────────────────
def main():
    ap = argparse.ArgumentParser(description="Derive + validate the x86-64 (steamrt64) steamclient.so hook RVAs.")
    ap.add_argument("steamclient")
    ap.add_argument("--json", default=None)
    ap.add_argument("--emit-rvas", metavar="DIR", default=None)
    ap.add_argument("--steam-version", default=None)
    args = ap.parse_args()
    try:
        e = Elf64(args.steamclient)
        sha = sha256_file(args.steamclient)
        starts = eh_frame_starts(e)
    except (OSError, ValueError) as err:
        print("ERROR: %s" % err, file=sys.stderr)
        return 1
    md = capstone_or_none()
    if md is None:
        print("ERROR: capstone is required (pip install capstone)", file=sys.stderr)
        return 1
    tx, tx_va = e.exec_region()
    exec_end = tx_va + len(tx)
    result = {"steamclient_sha256": sha, "client_arch": "steamrt64",
              "relocs_applied": e.n_reloc, "functions": len(starts),
              "hooks": {}, "rtti_byname": {}, "finder": {}, "pkginfo": {},
              "blocking": [], "noncritical_moved": []}

    # 1) DepotKey by name (mirror of Rtti::ResolveVtableSlotByName on x86-64)
    bn = {"status": "NOT_RESOLVED"}
    map_vts = e.vtable_funcs(BYNAME["map_class"])
    impl_vts = e.vtable_funcs(BYNAME["impl_class"])
    name_vas = e.cstrings(BYNAME["method"])
    if len(map_vts) == 1 and len(impl_vts) == 1 and name_vas:
        mslots = e.slots(map_vts[0], BYNAME["max_slots"])
        by_addr = sorted((fn, k) for k, fn in enumerate(mslots))
        hits = {}
        for s in name_vas:
            for r in rip_lea_sites(tx, tx_va, s):
                i = bisect.bisect_right(by_addr, (r, 1 << 30)) - 1
                if i >= 0 and r - by_addr[i][0] <= BYNAME["max_fn_size"]:
                    hits.setdefault(by_addr[i][1], []).append(r)
        if hits:
            slot = min(hits)
            fn = e.u64(impl_vts[0] + slot * 8)
            if fn and e.in_text(fn):
                bn = {"status": "UNIQUE", "class": BYNAME["impl_class"].decode(), "slot": slot,
                      "rva": "0x%x" % fn, "overloads": sorted(hits),
                      "map_vtable": "0x%x" % map_vts[0], "impl_vtable": "0x%x" % impl_vts[0]}
        else:
            bn["note"] = "no map slot references the method name"
    else:
        bn["note"] = "map vtables=%d impl vtables=%d name strings=%d" % (len(map_vts), len(impl_vts), len(name_vas))
    result["rtti_byname"] = bn
    result["hooks"]["DepotKey"] = {"status": bn["status"], "rva": bn.get("rva"), "method": "rtti-byname"}

    # 2) string-anchored hooks (mirror of GmrcXref::Locate on x86-64)
    for label, (needle, nul) in ANCHORS.items():
        strs = e.cstrings(needle, nul_after=nul, nul_before=False)
        sites = [s for v in strs for s in rip_lea_sites(tx, tx_va, v)]
        fns = sorted(set(f for f in (func_of(starts, exec_end, s)[0] for s in sites) if f))
        st = "UNIQUE" if len(sites) == 1 and len(fns) == 1 else ("AMBIGUOUS" if sites else "NO_SITE")
        result["hooks"][label] = {"status": st, "rva": ("0x%x" % fns[0]) if st == "UNIQUE" else None,
                                  "method": "string-xref", "strings": ["0x%x" % v for v in strs],
                                  "sites": ["0x%x" % s for s in sites]}

    # 3) Reconcile (mirror of ReconcileAnchorCore::LocateNotifyLicensesUpdated64)
    sites = reconcile_sites64(tx, tx_va)
    fns = {}
    for s in sites:
        f, en = func_of(starts, exec_end, s)
        if f:
            fns[f] = en
    kept = []
    for f, en in sorted(fns.items()):
        head = tx[f - tx_va:min(en, f + 96) - tx_va]
        ok, disp = reads_this_then_bails64(head)
        if ok:
            kept.append((f, disp))
    st = "UNIQUE" if len(kept) == 1 else ("AMBIGUOUS" if kept else "NOT_FOUND")
    result["hooks"]["Reconcile"] = {"status": st, "rva": ("0x%x" % kept[0][0]) if st == "UNIQUE" else None,
                                    "method": "callback-anchor", "sites": len(sites), "candidates": len(fns),
                                    "kept": ["0x%x" % k[0] for k in kept],
                                    "member_disp": ("0x%x" % kept[0][1]) if st == "UNIQUE" else None}

    # 4) the package-0 finder and the PackageInfo layout
    result["finder"] = derive_finder(e, md, starts, exec_end, result)
    result["pkginfo"] = derive_pkginfo_layout(e, md, starts, exec_end)

    # verdict
    for label in CRITICAL:
        if result["hooks"][label]["status"] != "UNIQUE":
            result["blocking"].append(label)
    for label in NONCRITICAL:
        if result["hooks"][label]["status"] != "UNIQUE":
            result["noncritical_moved"].append(label)
    fi, pk = result["finder"], result["pkginfo"]
    if fi.get("status") != "UNIQUE":
        result["blocking"].append("finder:%s" % fi.get("status"))
    elif not fi.get("layout_matches_runtime"):
        result["blocking"].append("finder:node-layout-differs-from-runtime (stride %s pkginfo %s)"
                                  % (fi.get("node_stride"), fi.get("node_pkginfo_off")))
    if pk.get("status") != "UNIQUE":
        result["blocking"].append("pkginfo:%s" % pk.get("status"))
    elif not pk.get("layout_matches_runtime"):
        result["blocking"].append("pkginfo:appidvec-off-differs-from-runtime (%s)" % pk.get("appidvec_off"))
    if result["blocking"]:
        verdict, code = "BLOCKING", 3
    elif result["noncritical_moved"]:
        verdict, code = "NONCRITICAL_MOVED", 2
    else:
        verdict, code = "CLEAN", 0
    result["verdict"], result["exit_code"] = verdict, code

    print("check_patterns_x64: %s  (sha256 %s…, %d relocs, %d functions)" % (verdict, sha[:16], e.n_reloc, len(starts)))
    for label, info in result["hooks"].items():
        print("  %-12s %-10s %s  [%s]" % (label, info["status"], info.get("rva") or "-", info["method"]))
    print("  %-12s %-10s slot=%s cache=%s root=%s nodes=%s stride=%s pkginfo=%s" % (
        "finder", fi.get("status"), fi.get("cache_global_rva"), fi.get("cache_global_rva"),
        fi.get("cache_root_off"), fi.get("cache_nodes_off"), fi.get("node_stride"), fi.get("node_pkginfo_off")))
    print("  %-12s %-10s loadpackage=%s appidvec=%s" % ("pkginfo", pk.get("status"), pk.get("loadpackage"), pk.get("appidvec_off")))
    if result["blocking"]:
        print("  BLOCKING: " + ", ".join(result["blocking"]))
    if result["noncritical_moved"]:
        print("  non-critical not resolved: " + ", ".join(result["noncritical_moved"]))
    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump(result, fh, indent=2)
    if args.emit_rvas and code != 3:
        print("  wrote " + emit_rvas_file(result, args.emit_rvas, args.steam_version))
    return code


if __name__ == "__main__":
    sys.exit(main())
