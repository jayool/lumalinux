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
# Since 2026-09-22 the decoder, the masking rules and the growth loop live in
# derive_from_address.py (shared with every other Python deriver); this file
# is the LOCATOR (RTTI name -> vtable slot) plus the CLI.
#
# Safety: every emitted byte is the function's own, wildcards only generalise,
# and uniqueness is checked with the same scanner check_patterns.py uses. An
# opcode the mini-decoder does not know is a hard refusal (exit 3), never a
# guess. check_patterns.py re-validates the result afterwards in CI anyway.
#
# Usage:
#   tools/derive_depotkey_byname.py steamclient.so [--derived derived.json]
#       [--min-bytes 28] [--max-bytes 96]
# Exit codes: 0 derived UNIQUE (and written) | 2 by-name did not resolve |
#             3 could not build a unique pattern (decoder or ambiguity) |
#             4 internal sanity failure (the pattern does not match at its
#               own address — should be impossible).
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_patterns import (BYNAME_DEPOTKEY, RTTI_DEPOTKEY,   # noqa: E402
                            load_exec_segments, rtti_derive_slot_byname)

LABEL = RTTI_DEPOTKEY["const"]          # kDepotKeyFnPattern


from derive_from_address import (PROLOGUE_BYTES, Undecodable,  # noqa: E402,F401
                                 _0F_MODRM, _0F_MODRM_IMM8, _modrm_len,
                                 derive_at, insn_len, read_rva,
                                 wildcard_prologue, write_derived)


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
    print("  by-name: %s %r -> map slot %d -> %s @ 0x%x"
          % (BYNAME_DEPOTKEY["map_class"], BYNAME_DEPOTKEY["method"], bn["slot"],
             BYNAME_DEPOTKEY["impl_class"], rva))
    entry, note = derive_at(path, rva, "rtti-byname", min_bytes, max_bytes,
                            segments=load_exec_segments(path))
    if entry is not None:
        entry["slot"] = bn["slot"]
    return entry, note


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
        write_derived(args.derived, LABEL, entry)
    return 0


if __name__ == "__main__":
    sys.exit(main())
