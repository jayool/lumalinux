#!/usr/bin/env python3
"""Count how many times IDA-style byte signatures occur in a binary.

    python3 tools/scan_signature.py steamclient.so \
        --sig "GMRC=E8 ? ? ? ? 83 C4 ? 83 F8 ? 0F 85" --sig "GetPackage=E8 ? ? ? ? 83 C4 ? 83 78 ? ? 0F 84"
    python3 tools/scan_signature.py steamclient.so --sigs "A=E8 ? ? ? ? 83;B=55 89 E5"

A signature is hex bytes separated by spaces; `?` or `??` is a wildcard byte.
For each signature the report gives the match count and the first file
offsets. One match is what a plugin-style `patternScan` (first hit) needs to
land on the right function; zero means the scan returns nil, two or more means
it may hook the wrong site. Read-only; no dependency on the rest of the tools.
"""
import argparse
import re
import sys


def compile_sig(sig: str) -> re.Pattern:
    parts = sig.split()
    if not parts:
        raise ValueError("empty signature")
    out = []
    for p in parts:
        if p in ("?", "??"):
            out.append(b".")
        else:
            out.append(re.escape(bytes([int(p, 16)])))
    return re.compile(b"".join(out), re.DOTALL)


def parse_sigs(args) -> list:
    sigs = []
    for s in args.sig or []:
        sigs.append(s)
    if args.sigs:
        sigs.extend(x for x in args.sigs.split(";") if x.strip())
    result = []
    for i, s in enumerate(sigs):
        s = s.strip()
        if "=" in s:
            label, body = s.split("=", 1)
        else:
            label, body = f"sig{i + 1}", s
        result.append((label.strip(), body.strip()))
    return result


def text_section_range(data: bytes):
    """(start, end) file offsets of the ELF .text section, or None.

    Mirrors what SLSsteam's MemHlp::patternScan does (it scans the module's
    .text only), so a hit in .rodata or .data does not count.
    """
    import struct
    if data[:4] != b"\x7fELF":
        return None
    is64 = data[4] == 2
    le = data[5] == 1
    e = "<" if le else ">"
    if is64:
        shoff, = struct.unpack_from(e + "Q", data, 0x28)
        shentsize, shnum, shstrndx = struct.unpack_from(e + "HHH", data, 0x3A)
    else:
        shoff, = struct.unpack_from(e + "I", data, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from(e + "HHH", data, 0x2E)
    if not shoff or not shnum:
        return None

    def sh(i):
        off = shoff + i * shentsize
        if is64:
            name, typ, flags, addr, offset, size = struct.unpack_from(e + "IIQQQQ", data, off)
        else:
            name, typ, flags, addr, offset, size = struct.unpack_from(e + "IIIIII", data, off)
        return name, typ, flags, addr, offset, size

    _, _, _, _, stroff, strsize = sh(shstrndx)
    strtab = data[stroff:stroff + strsize]
    for i in range(shnum):
        name, typ, flags, addr, offset, size = sh(i)
        end = strtab.find(b"\0", name)
        if strtab[name:end] == b".text":
            return offset, offset + size
    return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("--sig", action="append", help="LABEL=BYTES (repeatable)")
    ap.add_argument("--sigs", help="several LABEL=BYTES separated by ';'")
    ap.add_argument("--max-offsets", type=int, default=5)
    ap.add_argument("--whole-file", action="store_true", help="scan every byte, not just .text")
    args = ap.parse_args()

    sigs = parse_sigs(args)
    if not sigs:
        print("no signatures given", file=sys.stderr)
        return 2
    with open(args.binary, "rb") as fh:
        data = fh.read()
    rng = None if args.whole_file else text_section_range(data)
    if rng:
        lo, hi = rng
        scope = f".text @ 0x{lo:x}..0x{hi:x} ({hi - lo} bytes)"
    else:
        lo, hi = 0, len(data)
        scope = "whole file (no .text section found)" if not args.whole_file else "whole file"

    worst = 0
    print(f"binary: {args.binary} ({len(data)} bytes), scanning {scope}")
    for label, body in sigs:
        try:
            rx = compile_sig(body)
        except ValueError as exc:
            print(f"  {label:<14} INVALID  {exc}")
            worst = max(worst, 3)
            continue
        offs = [m.start() for m in rx.finditer(data, lo, hi)]
        n = len(offs)
        verdict = "UNIQUE" if n == 1 else ("NOT FOUND" if n == 0 else "AMBIGUOUS")
        shown = ", ".join(f"0x{o:x}" for o in offs[: args.max_offsets])
        more = f" (+{n - args.max_offsets} more)" if n > args.max_offsets else ""
        print(f"  {label:<14} {verdict:<10} matches={n}  {shown}{more}")
        worst = max(worst, 0 if n == 1 else (3 if n == 0 else 2))
    return worst


if __name__ == "__main__":
    sys.exit(main())
