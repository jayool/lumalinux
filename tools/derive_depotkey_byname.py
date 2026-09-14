#!/usr/bin/env python3
# derive_depotkey_byname.py — re-derive kDepotKeyFnPattern WITHOUT Ghidra.
#
# Why this exists (selftest run #7, 2026-09-14): derive_patterns.py reaches
# DepotKey by walking the dispatcher's `CALL [reg+0x18]`, and headless Ghidra
# does not resolve indirect calls — the walk fails on every build we have tried
# ("found N dispatcher candidate(s) but none had a resolvable CALL [reg+0x18]").
# So a moved DepotKey could never be auto-derived: exit 3 went straight to an
# issue and a human re-derived it by hand (maintenance.md A.3).
#
# But the CI already KNOWS where DepotKey is, without reading a byte of it:
# check_patterns.py's 1c resolves the accessor BY NAME (IClientConfigStoreMap
# "GetBinary" -> map slot -> CConfigStore's vtable -> the function). That is the
# runtime's own last-resort resolver (Rtti::ResolveVtableSlotByName), the one
# the Deck falls back to when the pattern misses. This tool takes that address,
# reads the function's prologue from the ELF and wildcards it with the SAME
# rules derive_patterns.py's extract_pattern uses:
#   - the PIC get_pc_thunk `call` rel32,
#   - the GOT `add reg, imm32` right after it,
#   - any `mov/lea r32, [picbase + disp32]` global load off that register.
# It grows the pattern by whole instructions from PROLOGUE_BYTES until it is
# UNIQUE in the executable segments, then writes it into derived.json exactly
# as derive_patterns.py would, so apply_derived_pattern.py needs no change.
#
# Safety: every emitted byte is the function's own, wildcards only generalise,
# and uniqueness is checked with the same scanner check_patterns.py uses. An
# opcode the mini-decoder does not know is a hard refusal (exit 3), never a
# guess. check_patterns.py re-validates the result afterwards in CI anyway.
#
# Usage:
#   tools/derive_depotkey_byname.py steamclient.so [--derived derived.json]
#       [--min-bytes 28] [--max-bytes 96] [--patterns src/patterns.hpp]
# Exit codes: 0 derived UNIQUE (and written) | 2 by-name did not resolve |
#             3 could not build a unique pattern (decoder or ambiguity) |
#             4 internal sanity failure (the pattern does not match at its
#               own address — should be impossible).
import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_patterns import (BYNAME_DEPOTKEY, RTTI_DEPOTKEY,   # noqa: E402
                            load_exec_segments, rtti_derive_slot_byname,
                            scan_rvas)

LABEL = RTTI_DEPOTKEY["const"]          # kDepotKeyFnPattern
PROLOGUE_BYTES = 28                     # same as derive_patterns.py


class Undecodable(Exception):
    pass


def _modrm_len(buf, i):
    """Bytes used by a ModR/M byte at buf[i] plus its SIB/displacement."""
    if i >= len(buf):
        raise Undecodable("truncated ModR/M")
    m = buf[i]
    mod, rm = m >> 6, m & 7
    n = 1
    if mod != 3 and rm == 4:            # SIB follows
        if i + 1 >= len(buf):
            raise Undecodable("truncated SIB")
        n += 1
        if mod == 0 and (buf[i + 1] & 7) == 5:
            n += 4                      # [index*s + disp32], no base
    if mod == 1:
        n += 1
    elif mod == 2:
        n += 4
    elif mod == 0 and rm == 5:
        n += 4                          # [disp32]
    return n


# Two-byte (0F xx) opcodes: value = extra bytes after the ModR/M ("m" means a
# ModR/M is present). Kept to what real prologues contain; anything else refuses.
_0F_MODRM = set(list(range(0x10, 0x18)) + list(range(0x28, 0x30)) + [0x1F] +
                list(range(0x40, 0x50)) + list(range(0x50, 0x70)) +
                list(range(0x74, 0x77)) + [0x7E, 0x7F] +
                list(range(0x90, 0xA0)) + [0xA3, 0xAB, 0xAF, 0xB0, 0xB1, 0xB3,
                0xB6, 0xB7, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xC1, 0xC7] +
                list(range(0xD0, 0xFF)))
_0F_MODRM_IMM8 = {0x70, 0x71, 0x72, 0x73, 0xA4, 0xAC, 0xBA, 0xC2, 0xC4, 0xC5, 0xC6}


def insn_len(buf, i, opsize16=False):
    """Length of the x86-32 instruction at buf[i] for the subset that appears
    in function prologues/bodies we capture. Raises Undecodable otherwise."""
    if i >= len(buf):
        raise Undecodable("truncated")
    op = buf[i]
    imm32 = 2 if opsize16 else 4
    # prefixes
    if op in (0x66,):
        return 1 + insn_len(buf, i + 1, opsize16=True)
    if op in (0xF2, 0xF3, 0x2E, 0x36, 0x3E, 0x26, 0x64, 0x65, 0xF0):
        return 1 + insn_len(buf, i + 1, opsize16)
    if op == 0x0F:
        if i + 1 >= len(buf):
            raise Undecodable("truncated 0F")
        op2 = buf[i + 1]
        if 0x80 <= op2 <= 0x8F:
            return 2 + 4                                   # jcc rel32
        if op2 in _0F_MODRM_IMM8:
            return 2 + _modrm_len(buf, i + 2) + 1
        if op2 in _0F_MODRM:
            return 2 + _modrm_len(buf, i + 2)
        if op2 in (0x05, 0x0B, 0x31, 0xA2) or 0xC8 <= op2 <= 0xCF:
            return 2                                       # syscall/ud2/rdtsc/cpuid/bswap
        raise Undecodable("unknown 0F %02X" % op2)
    # 00-3F: the eight ALU groups (add/or/adc/sbb/and/sub/xor/cmp)
    if op < 0x40:
        low = op & 7
        if low < 4:
            return 1 + _modrm_len(buf, i + 1)
        if low == 4:
            return 2                                       # al, imm8
        if low == 5:
            return 1 + imm32                               # eax, imm32
        raise Undecodable("segment/BCD opcode %02X" % op)
    if 0x40 <= op <= 0x5F:
        return 1                                           # inc/dec/push/pop r32
    if op in (0x68,):
        return 1 + imm32
    if op in (0x6A,):
        return 2
    if op == 0x69:
        return 1 + _modrm_len(buf, i + 1) + imm32
    if op == 0x6B:
        return 1 + _modrm_len(buf, i + 1) + 1
    if 0x70 <= op <= 0x7F:
        return 2                                           # jcc rel8
    if op in (0x80, 0x83):
        return 1 + _modrm_len(buf, i + 1) + 1
    if op == 0x81:
        return 1 + _modrm_len(buf, i + 1) + imm32
    if 0x84 <= op <= 0x8F:
        return 1 + _modrm_len(buf, i + 1)                  # test/xchg/mov/lea/pop
    if 0x90 <= op <= 0x99:
        return 1
    if op in (0xA0, 0xA1, 0xA2, 0xA3):
        return 5                                           # mov acc, moffs32
    if op == 0xA8:
        return 2
    if op == 0xA9:
        return 1 + imm32
    if 0xB0 <= op <= 0xB7:
        return 2
    if 0xB8 <= op <= 0xBF:
        return 1 + imm32
    if op in (0xC0, 0xC1):
        return 1 + _modrm_len(buf, i + 1) + 1
    if op == 0xC2:
        return 3
    if op in (0xC3, 0xC9, 0xCC):
        return 1
    if op == 0xC6:
        return 1 + _modrm_len(buf, i + 1) + 1
    if op == 0xC7:
        return 1 + _modrm_len(buf, i + 1) + imm32
    if 0xD0 <= op <= 0xD3 or 0xD8 <= op <= 0xDF:
        return 1 + _modrm_len(buf, i + 1)                  # shifts, x87
    if op in (0xE8, 0xE9):
        return 5
    if op == 0xEB:
        return 2
    if op in (0xF6, 0xF7):
        n = 1 + _modrm_len(buf, i + 1)
        reg = (buf[i + 1] >> 3) & 7
        if reg in (0, 1):                                  # test r/m, imm
            n += 1 if op == 0xF6 else imm32
        return n
    if op in (0xFE, 0xFF):
        return 1 + _modrm_len(buf, i + 1)
    raise Undecodable("unknown opcode %02X" % op)


def wildcard_prologue(buf, min_bytes=PROLOGUE_BYTES):
    """Mirror of derive_patterns.extract_pattern on raw bytes. Returns the list
    of tokens ('AB' or '??'), covering whole instructions until >= min_bytes."""
    toks = []
    i = 0
    prev_was_call = False
    picbase_reg = None
    while i < min_bytes:
        n = insn_len(buf, i)
        if i + n > len(buf):
            raise Undecodable("instruction runs past the captured bytes")
        raw = buf[i:i + n]
        wild = [False] * n
        op = raw[0]
        is_call = (op == 0xE8 and n == 5)
        if is_call:
            for k in range(1, 5):
                wild[k] = True                             # rel32
        elif prev_was_call and n == 5 and op == 0x05:
            for k in range(1, 5):
                wild[k] = True                             # add eax, imm32
            picbase_reg = 0
        elif prev_was_call and n == 6 and op == 0x81 and ((raw[1] >> 3) & 7) == 0 \
                and (raw[1] >> 6) == 3:
            for k in range(2, 6):
                wild[k] = True                             # add r32, imm32
            picbase_reg = raw[1] & 7
        elif picbase_reg is not None and n == 6 and op in (0x8B, 0x8D):
            modrm = raw[1]
            if (modrm >> 6) == 2 and (modrm & 7) == picbase_reg and (modrm & 7) != 4:
                for k in range(2, 6):
                    wild[k] = True                         # [picbase + disp32]
        for k in range(n):
            toks.append("??" if wild[k] else "%02X" % raw[k])
        prev_was_call = is_call
        i += n
    return toks


def read_rva(segments, rva, n):
    for vaddr, buf in segments:
        if vaddr <= rva < vaddr + len(buf):
            return buf[rva - vaddr: rva - vaddr + n]
    return b""


def derive(path, min_bytes=PROLOGUE_BYTES, max_bytes=96):
    """Returns (entry_dict, note) with entry_dict shaped like derived.json's
    values, or (None, note) on refusal."""
    bn, note = rtti_derive_slot_byname(
        path, BYNAME_DEPOTKEY["map_class"], BYNAME_DEPOTKEY["method"],
        BYNAME_DEPOTKEY["impl_class"], BYNAME_DEPOTKEY["max_slots"],
        BYNAME_DEPOTKEY["max_fn_size"])
    if bn.get("status") != "UNIQUE":
        return None, "by-name did not resolve (%s): %s" % (bn.get("status"), note)
    rva = int(bn["rva"], 16)
    segments = load_exec_segments(path)
    raw = read_rva(segments, rva, max_bytes)
    if len(raw) < min_bytes:
        return None, "only %d byte(s) readable at 0x%x" % (len(raw), rva)
    print("  by-name: %s %r -> map slot %d -> %s @ 0x%x"
          % (bn["map_class"], bn["method"], bn["slot"], bn["impl_class"], rva))
    want = min_bytes
    last = None
    while True:
        try:
            toks = wildcard_prologue(raw, want)
        except Undecodable as e:
            return None, "decoder refused at 0x%x (+%d): %s" % (rva, want, e)
        pat = " ".join(toks)
        hits = scan_rvas(segments, pat)
        if rva not in hits:
            return None, "SANITY: pattern does not match at its own address 0x%x" % rva
        print("  %3d bytes: %d match(es)" % (len(toks), len(hits)))
        if len(hits) == 1:
            return ({"pattern": pat, "matches": 1, "rva": "0x%x" % rva,
                     "slot": bn["slot"], "source": "rtti-byname"}, "ok")
        if len(toks) == last or len(toks) >= max_bytes:
            return None, ("pattern still matches %d sites at %d bytes (limit %d) — "
                          "widen --max-bytes or tighten by hand" % (len(hits), len(toks), max_bytes))
        last = len(toks)
        want = len(toks) + 1                               # grow by one instruction


def main():
    ap = argparse.ArgumentParser(description=__doc__ or "derive kDepotKeyFnPattern by RTTI name")
    ap.add_argument("steamclient")
    ap.add_argument("--derived", default=None,
                    help="derived.json to create/merge the result into (derive_patterns.py format)")
    ap.add_argument("--min-bytes", type=int, default=PROLOGUE_BYTES)
    ap.add_argument("--max-bytes", type=int, default=96)
    args = ap.parse_args()

    print("---- %s (RTTI by-name, no Ghidra) ----" % LABEL)
    entry, note = derive(args.steamclient, args.min_bytes, args.max_bytes)
    if entry is None:
        print("  NOT DERIVED: %s" % note)
        return 2 if note.startswith("by-name") else (4 if note.startswith("SANITY") else 3)
    print("  UNIQUE @ %s : %s" % (entry["rva"], entry["pattern"]))
    if args.derived:
        data = {}
        if os.path.exists(args.derived):
            with open(args.derived) as f:
                data = json.load(f) or {}
        prev = data.get(LABEL)
        data[LABEL] = entry
        with open(args.derived, "w") as f:
            json.dump(data, f, indent=2, sort_keys=True)
        print("  wrote %s (%s %s)" % (args.derived, "replaced" if prev else "added", LABEL))
    return 0


if __name__ == "__main__":
    sys.exit(main())
