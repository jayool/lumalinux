#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# experiment_framesize_mask.py — EMPIRICAL test of the RESEARCH §8.1 claim that
# wildcarding the stack-frame size (`sub esp, imm`) makes patterns collide.
#
# Context (issue #16): the M1 proposal was to wildcard the frame-size immediate
# so a build that only changes it stays CLEAN. RESEARCH §8.1 (2026-06-24 audit)
# says NOT to: "wildcarding it leaves GMRC unique but makes BuildDep collide
# (6 matches) and LoadPackage (6)". This script re-checks that against a real
# steamclient.so, and — crucially — tests TWO mask strengths, because §8.1 does
# not record which it used:
#
#   HIGH-KEEP : wildcard only the low bytes of the imm, keep the high `00 00`
#               (the M1 proposal: 81 EC 2C 02 00 00 -> 81 EC ?? ?? 00 00,
#                83 EC 24          -> 83 EC ??)
#   FULL      : wildcard the whole imm (likely what §8.1 tested:
#               81 EC 2C 02 00 00 -> 81 EC ?? ?? ?? ??)
#
# For each pattern it prints the baseline match count/site and, for each mask
# variant, the new match count and whether the (unique) match is the SAME site.
# A variant that stays UNIQUE and on-site would REOPEN #16 for that pattern; a
# variant that goes ambiguous CONFIRMS §8.1.
#
#   python3 tools/experiment_framesize_mask.py <steamclient.so> [--hpp src/patterns.hpp]
#
# Pure logic (frame-size masking) is unit-tested in test_experiment_framesize.py
# with synthetic buffers; the live count needs a real binary (run on the CI
# runner — Valve's CDN is firewalled from the dev sandbox).
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_patterns import load_exec_segments, parse_patterns_hpp, scan_rvas  # noqa: E402

# Patterns whose frame-size we test. We include LoadPackage precisely because
# §8.1 cites it as collision evidence.
TARGETS = ["kDepotKeyFnPattern", "kBuildDepotDependencyPattern",
           "kGmrcFunctionPattern", "kLoadPackagePattern"]


def _find_frame_ops(toks):
    """Yield (op_index, imm_len) for each `sub esp, imm` in the token list.
    83 EC ib  -> sub esp, imm8  (1 imm byte)
    81 EC id  -> sub esp, imm32 (4 imm bytes)
    op_index points at the opcode (83/81); the imm follows EC."""
    for i in range(len(toks) - 1):
        op, modrm = toks[i].upper(), toks[i + 1].upper()
        if modrm != "EC":
            continue
        if op == "83":
            yield i, 1
        elif op == "81":
            yield i, 4


def mask_framesize(patstr, mode):
    """Return a copy of patstr with every `sub esp, imm` immediate wildcarded.
    mode='full'      -> all imm bytes wildcarded.
    mode='high_keep' -> imm8: wildcard the 1 byte; imm32: wildcard the low 2
                        bytes, keep the high 2 (usually `00 00`) for uniqueness.
    Returns None if the pattern has no frame-size op (nothing to mask)."""
    toks = patstr.split()
    out = list(toks)
    touched = False
    for op_i, imm_len in _find_frame_ops(toks):
        imm_start = op_i + 2
        if mode == "full":
            n = imm_len
        elif mode == "high_keep":
            n = 1 if imm_len == 1 else 2   # imm32: low 2 bytes only
        else:
            raise ValueError("bad mode")
        for k in range(imm_start, imm_start + n):
            if k < len(out) and out[k] != "??":
                out[k] = "??"
                touched = True
    return " ".join(out) if touched else None


def _count(segments, pat):
    rvas = scan_rvas(segments, pat)
    return len(rvas), rvas


def run(segments, patterns):
    lines = []
    for name in TARGETS:
        pat = patterns.get(name)
        if pat is None:
            lines.append("  %-30s (not in patterns.hpp)" % name)
            continue
        base_n, base_rvas = _count(segments, pat)
        base_site = base_rvas[0] if base_n == 1 else None
        lines.append("  %-30s baseline: %d match%s%s"
                     % (name, base_n, "" if base_n == 1 else "es",
                        (" @ 0x%x" % base_site) if base_site is not None else ""))
        for mode in ("high_keep", "full"):
            masked = mask_framesize(pat, mode)
            if masked is None:
                lines.append("      %-10s (no frame-size op to mask)" % mode)
                continue
            n, rvas = _count(segments, masked)
            if n == 1 and base_site is not None and rvas[0] == base_site:
                verdict = "UNIQUE + same site  -> mask would be SAFE"
            elif n == 1:
                verdict = "UNIQUE but DIFFERENT site (0x%x) -> unsafe" % rvas[0]
            elif n == 0:
                verdict = "NOT FOUND -> unsafe"
            else:
                verdict = "AMBIGUOUS (%d matches) -> confirms §8.1" % n
            lines.append("      %-10s -> %d match%s : %s"
                         % (mode, n, "" if n == 1 else "es", verdict))
    return lines


def main():
    ap = argparse.ArgumentParser(description="Empirically test frame-size masking (issue #16 / §8.1).")
    ap.add_argument("steamclient")
    ap.add_argument("--hpp", default="src/patterns.hpp")
    args = ap.parse_args()
    try:
        segments = load_exec_segments(args.steamclient)
        patterns = parse_patterns_hpp(args.hpp)
    except (OSError, ValueError) as e:
        print("ERROR: %s" % e, file=sys.stderr)
        return 1
    print("frame-size mask experiment on %s" % args.steamclient)
    print("(high_keep = M1 proposal; full = likely §8.1)\n")
    for line in run(segments, patterns):
        print(line)
    print("\nRead: any 'UNIQUE + same site' reopens #16 for that pattern/variant;")
    print("any 'AMBIGUOUS' confirms RESEARCH §8.1 that the frame size is load-bearing.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
