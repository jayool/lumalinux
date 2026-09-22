#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# derive_from_address.py — the COMMON CORE of every Python (no-Ghidra) pattern
# deriver: given the address of a hook function, fabricate a fresh, masked byte
# pattern for src/patterns.hpp and write it into derived.json.
#
# Extracted verbatim from derive_depotkey_byname.py on 2026-09-22 so that every
# locator shares ONE decoder, ONE set of masking rules and ONE growth loop:
#
#   derive_depotkey_byname.py    RTTI name -> vtable slot        -> derive_at()
#   derive_reconcile_byanchor.py callback 125 + this-read + jle  -> derive_at()
#   (next) text -> GOT-relative xref -> function                 -> derive_at()
#
# What derive_at() does, exactly like derive_patterns.py's extract_pattern but
# on raw ELF bytes:
#   1. read up to max_bytes of the function at `rva`;
#   2. wildcard what changes between builds — the PIC get_pc_thunk `call`
#      rel32, the GOT `add reg,imm32` after it, every `[picbase+disp32]`
#      global load; and, when the caller asks for it, the frame size
#      (`sub esp,imm32`) and any other `[reg+disp32]` displacement (member and
#      spill offsets — struct-layout numbers, the kind that drifts);
#   3. grow by whole instructions from min_bytes until the pattern matches
#      exactly ONE site (check_patterns.scan_rvas, the same scanner the
#      nightly uses), refusing on an opcode the mini-decoder does not know;
#   4. sanity: the pattern must match at its own address.
# Every emitted byte is the function's own; wildcards only generalise.
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_patterns import load_exec_segments, scan_rvas  # noqa: E402

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


def wildcard_prologue(buf, min_bytes=PROLOGUE_BYTES, mask_frame=False, mask_disp32=False):
    """Mirror of derive_patterns.extract_pattern on raw bytes. Returns the list
    of tokens ('AB' or '??'), covering whole instructions until >= min_bytes.

    Always masked: the get_pc_thunk `call` rel32, the GOT `add reg,imm32`
    right after it, and every `mov/lea r32,[picbase+disp32]`.
    mask_frame:  also the imm32 of `sub esp,imm32` (81 EC) — a locals-only
                 change (0x110 -> 0x120 on the 9cf4720f beta) must not break
                 a pattern.
    mask_disp32: also the disp32 of every OTHER `[reg+disp32]` operand
                 (mod=10, not the picbase): member offsets and stack spills,
                 i.e. struct-layout numbers — and, for a `mov r,[reg+disp32]`
                 load and a register `test r,r`, the ModR/M byte too, because
                 which register holds the temporary is the compiler's choice
                 (edi on bc54101b, another on 9cf4720f). Kept off for
                 DepotKey, whose shipped pattern never carried one; on for
                 Reconcile, whose `mov edi,[eax+0x1b14]` became a different
                 register at 0x1b10 on the beta."""
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
        elif mask_frame and n == 6 and op == 0x81 and raw[1] == 0xEC:
            for k in range(2, 6):
                wild[k] = True                             # sub esp, imm32
        elif n >= 6 and op in (0x8B, 0x8D, 0x89) and (raw[1] >> 6) == 2 and (raw[1] & 7) != 4:
            modrm = raw[1]
            if picbase_reg is not None and (modrm & 7) == picbase_reg and op != 0x89:
                for k in range(2, 6):
                    wild[k] = True                         # [picbase + disp32]
            elif mask_disp32:
                for k in range(2, 6):
                    wild[k] = True                         # [reg + disp32]: layout number
                if op == 0x8B:
                    wild[1] = True                         # and WHICH register got it:
                                                           # the compiler's choice (edi on
                                                           # bc54101b, another on 9cf4720f)
        elif mask_disp32 and n == 2 and op == 0x85 and (raw[1] >> 6) == 3:
            wild[1] = True                                 # test r,r: same register choice
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




def derive_at(path, rva, source, min_bytes=PROLOGUE_BYTES, max_bytes=96,
              mask_frame=False, mask_disp32=False, log=print, segments=None):
    """Fabricate a UNIQUE masked pattern for the function at `rva`.
    Returns (entry_dict, note): entry_dict is shaped like a derived.json value
    ({pattern, matches, rva, source}) or None on refusal, with `note` saying why:
      'only N byte(s) readable'   the address is not inside an exec segment
      'decoder refused …'         an opcode the mini-decoder does not know
      'SANITY: …'                 the pattern does not match at its own address
      'pattern still matches …'   no unique pattern within max_bytes
    `segments` lets a caller (or a test) hand in pre-loaded exec segments."""
    if segments is None:
        segments = load_exec_segments(path)
    raw = read_rva(segments, rva, max_bytes)
    if len(raw) < min_bytes:
        return None, "only %d byte(s) readable at 0x%x" % (len(raw), rva)
    want = min_bytes
    last = None
    while True:
        try:
            toks = wildcard_prologue(raw, want, mask_frame=mask_frame, mask_disp32=mask_disp32)
        except Undecodable as e:
            return None, "decoder refused at 0x%x (+%d): %s" % (rva, want, e)
        pat = " ".join(toks)
        hits = scan_rvas(segments, pat)
        if rva not in hits:
            return None, "SANITY: pattern does not match at its own address 0x%x" % rva
        log("  %3d bytes: %d match(es)" % (len(toks), len(hits)))
        if len(hits) == 1:
            return ({"pattern": pat, "matches": 1, "rva": "0x%x" % rva, "source": source}, "ok")
        if len(toks) == last or len(toks) >= max_bytes:
            return None, ("pattern still matches %d sites at %d bytes (limit %d) — "
                          "widen --max-bytes or tighten by hand" % (len(hits), len(toks), max_bytes))
        last = len(toks)
        want = len(toks) + 1                               # grow by one instruction


def write_derived(derived_path, label, entry, log=print):
    """Create or merge derived.json (derive_patterns.py's format) with `entry`
    under `label`, so apply_derived_pattern.py needs no change."""
    data = {}
    if os.path.exists(derived_path):
        with open(derived_path) as f:
            data = json.load(f) or {}
    prev = data.get(label)
    data[label] = entry
    with open(derived_path, "w") as f:
        json.dump(data, f, indent=2, sort_keys=True)
    log("  wrote %s (%s %s)" % (derived_path, "replaced" if prev else "added", label))


def exit_code_for(note):
    """The exit-code convention every deriver shares: 2 locator did not
    resolve, 4 sanity, 3 anything else the core refused."""
    if note.startswith("SANITY"):
        return 4
    return 3
