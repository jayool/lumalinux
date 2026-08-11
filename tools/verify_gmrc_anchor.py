#!/usr/bin/env python3
# verify_gmrc_anchor.py — standalone (no Ghidra, no capstone) verifier for the
# #13 Part 1 precondition: is the GMRC job-name string referenced from EXACTLY
# ONE function inside steamclient.so?  If >1, the string-xref anchor is NOT clean
# and the runtime finder must disambiguate (or the approach is wrong — the
# v0.10.3 LoadPackage lesson: never anchor on a string that lives in another
# function).
#
# It also prints the derived function-entry RVA(s) so you can ground-truth them
# against the current byte-pattern target (GMRC getter, file vaddr ~0x12c3bd0 per
# src/patterns.hpp).  This mirrors, in pure Python, the derivation the in-client
# finder would do: .rodata string -> GOT base -> `lea reg,[base + (S-GOT)]` in
# .text -> walk back to the PIC prologue.
#
# Usage:   python3 tools/verify_gmrc_anchor.py /path/to/steamclient.so
#          python3 tools/verify_gmrc_anchor.py steamclient.so --str "Some.Other.String"
#
# i386 (32-bit) PIC ELF only — that's what lumalinux hooks.  Heuristic by design;
# the authoritative check remains tools/ghidra_find_gmrc.py (Ghidra's real
# reference resolver).  Use this for a fast first answer on-device.
import struct
import sys

DEFAULT_STR = "ContentServerDirectory.GetManifestRequestCode#1"
# src/patterns.hpp kGmrcFunctionPattern — the current byte-pattern locator.
# We scan for it on the SAME binary and check it lands on the xref-derived entry
# (ground-truth validation, #13 Part 1 step 5), instead of comparing to a stale
# per-build RVA constant.
GMRC_PATTERN = ("E8 ?? ?? ?? ?? 05 ?? ?? ?? ?? 55 89 E5 57 56 53 "
                "81 EC 10 01 00 00 8B 7D 08 8B 4D 20")


def die(msg):
    sys.stderr.write("error: " + msg + "\n")
    sys.exit(2)


def s32(u):
    return u - 0x100000000 if u & 0x80000000 else u


def read32(b, o):
    return struct.unpack_from("<I", b, o)[0]


class Sec:
    __slots__ = ("name", "addr", "off", "size", "type")


def parse_sections(data):
    if data[:4] != b"\x7fELF":
        die("not an ELF file")
    if data[4] != 1:
        die("not ELFCLASS32 (this tool is i386-only; steamclient.so is 32-bit)")
    e_shoff = read32(data, 0x20)
    e_shentsize = struct.unpack_from("<H", data, 0x2E)[0]
    e_shnum = struct.unpack_from("<H", data, 0x30)[0]
    e_shstrndx = struct.unpack_from("<H", data, 0x32)[0]
    secs = []
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        s = Sec()
        s.name = read32(data, o)          # index into shstrtab for now
        s.type = read32(data, o + 4)
        s.addr = read32(data, o + 12)
        s.off = read32(data, o + 16)
        s.size = read32(data, o + 20)
        secs.append(s)
    stro = secs[e_shstrndx].off
    for s in secs:
        end = data.index(b"\0", stro + s.name)
        s.name = data[stro + s.name:end].decode("ascii", "replace")
    return secs


def sec_by_name(secs, name):
    for s in secs:
        if s.name == name:
            return s
    return None


def file_off_to_vaddr(secs, off):
    for s in secs:
        if s.type != 8 and s.size and s.off <= off < s.off + s.size:  # not NOBITS
            return s.addr + (off - s.off)
    return None


def derive_got_base(text, taddr):
    # Consensus GOT base from PIC preambles: `call get_pc_thunk; add reg,imm32`.
    # GOT base(vaddr) = (call_site_vaddr + 5) + imm32.  Real preambles all agree.
    from collections import Counter
    votes = Counter()
    n = len(text)
    i = 0
    while i < n - 10:
        if text[i] == 0xE8:
            j = i + 5
            ret_va = taddr + i + 5
            if text[j] == 0x05:                                   # add eax,imm32
                votes[ret_va + read32(text, j + 1)] += 1
            elif text[j] == 0x81 and (text[j + 1] & 0xF8) == 0xC0:  # add r32,imm32
                votes[ret_va + read32(text, j + 2)] += 1
        i += 1
    if not votes:
        return None, 0
    got, cnt = votes.most_common(1)[0]
    return got, cnt


def scan_lea_sites(text, taddr, disp):
    # `lea reg,[base + disp32]`: 8D, mod=10, r/m != 100(SIB); disp32 == disp.
    sites = []
    n = len(text)
    i = 0
    while i < n - 6:
        if text[i] == 0x8D:
            modrm = text[i + 1]
            if (modrm & 0xC0) == 0x80 and (modrm & 0x07) != 0x04:
                if s32(read32(text, i + 2)) == disp:
                    sites.append(taddr + i)      # vaddr of the lea
        i += 1
    return sites


def parse_pattern(pat):
    vals, mask = [], []
    for tok in pat.split():
        if tok == "??":
            vals.append(0); mask.append(0)
        else:
            vals.append(int(tok, 16)); mask.append(0xFF)
    return bytes(vals), bytes(mask)


def scan_pattern(text, taddr, pat):
    vals, mask = parse_pattern(pat)
    m = len(vals)
    hits = []
    n = len(text)
    i = 0
    while i <= n - m:
        ok = True
        for j in range(m):
            if mask[j] and text[i + j] != vals[j]:
                ok = False
                break
        if ok:
            hits.append(taddr + i)      # match start = function entry (E8 preamble)
        i += 1
    return hits


def walk_back_to_prologue(text, taddr, site_va, max_back=0x4000):
    # Nearest preceding PIC preamble (`E8 rel32; add reg,imm32`) = function entry.
    off = site_va - taddr
    lo = max(0, off - max_back)
    k = off
    while k >= lo:
        if text[k] == 0xE8 and k + 6 < len(text):
            nb = text[k + 5]
            if nb == 0x05 or (nb == 0x81 and (text[k + 6] & 0xF8) == 0xC0):
                return taddr + k     # function entry vaddr
        k -= 1
    return None


def main():
    if len(sys.argv) < 2:
        die("usage: verify_gmrc_anchor.py <steamclient.so> [--str STRING]")
    path = sys.argv[1]
    needle = DEFAULT_STR
    if "--str" in sys.argv:
        needle = sys.argv[sys.argv.index("--str") + 1]
    with open(path, "rb") as f:
        data = f.read()
    secs = parse_sections(data)
    text = sec_by_name(secs, ".text")
    if not text:
        die("no .text section")
    taddr = text.addr
    tbytes = data[text.off:text.off + text.size]

    nb = needle.encode() + b"\0"
    occurrences = []
    start = 0
    while True:
        p = data.find(nb, start)
        if p < 0:
            break
        occurrences.append(p)
        start = p + 1
    if not occurrences:
        die("string %r not found in binary" % needle)

    print("binary        : %s (%d bytes)" % (path, len(data)))
    print("string        : %r" % needle)
    print("string copies : %d" % len(occurrences))

    got, votes = derive_got_base(tbytes, taddr)
    if got is None:
        die("could not derive GOT base (no PIC preambles found?)")
    print("GOT base      : 0x%x  (consensus of %d preambles)" % (got, votes))

    all_entries = {}
    for p in occurrences:
        s_va = file_off_to_vaddr(secs, p)
        if s_va is None:
            continue
        disp = s32((s_va - got) & 0xFFFFFFFF)
        sites = scan_lea_sites(tbytes, taddr, disp)
        print("\nstring @ vaddr 0x%x  ->  disp (S-GOT) = 0x%x  ->  %d lea site(s)"
              % (s_va, disp & 0xFFFFFFFF, len(sites)))
        for site in sites:
            entry = walk_back_to_prologue(tbytes, taddr, site)
            tag = ("0x%x" % entry) if entry else "??"
            print("    lea @ 0x%x   in function entry %s" % (site, tag))
            if entry:
                all_entries.setdefault(entry, []).append(site)

    # Ground-truth: does the current byte pattern resolve to the SAME entry on
    # THIS binary? (#13 Part 1 step 5 — validate the xref against the sig.)
    if needle == DEFAULT_STR:
        pat_hits = scan_pattern(tbytes, taddr, GMRC_PATTERN)
    else:
        pat_hits = None  # pattern only meaningful for the real GMRC anchor

    print("\n" + "=" * 64)
    n = len(all_entries)
    if n == 0:
        print("RESULT: no .text lea reference found. The string may be reached")
        print("        via a GOT slot / different addressing — inspect in Ghidra.")
    elif n == 1:
        entry = next(iter(all_entries))
        print("RESULT: CLEAN — job-name referenced from exactly 1 function.")
        print("        xref-derived function entry RVA = 0x%x" % entry)
        if pat_hits is not None:
            print("        byte-pattern (kGmrcFunctionPattern) matches: %d" % len(pat_hits))
            if len(pat_hits) == 1 and pat_hits[0] == entry:
                print("        GROUND-TRUTH VALIDATED: xref entry == pattern entry (0x%x)."
                      % entry)
                print("        -> xref and sig agree on this build. #13 Part 1 GO.")
            elif len(pat_hits) == 1:
                print("        pattern entry = 0x%x  (xref = 0x%x)  -> MISMATCH, investigate!"
                      % (pat_hits[0], entry))
            elif len(pat_hits) == 0:
                print("        pattern does NOT match on this (newer) build — this is")
                print("        exactly the fragility the xref rescues. Cross-validate on")
                print("        a build where the sig still resolves before shipping.")
            else:
                print("        pattern matched %d sites (non-unique): %s"
                      % (len(pat_hits), ", ".join("0x%x" % h for h in pat_hits)))
        print("        -> #13 Part 1 anchor precondition SATISFIED.")
    else:
        print("RESULT: AMBIGUOUS — referenced from %d functions:" % n)
        for e in sorted(all_entries):
            print("        entry 0x%x  (%d site(s))" % (e, len(all_entries[e])))
        print("        -> anchor NOT clean; the runtime finder must disambiguate")
        print("           (prologue / arg-shape) or pick a different anchor.")
    print("Heuristic tool — confirm with tools/ghidra_find_gmrc.py before shipping.")


if __name__ == "__main__":
    main()
