#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# verify_mask.py — the SAFETY GATE for issue #16 (M1 pattern byte-masking).
#
# Masking bytes in src/patterns.hpp (wildcarding layout-constant bytes we only
# need to LOCATE a function, not read) makes a signature survive a class of
# Steam-update drift. But loosening a pattern risks it matching the WRONG
# function — and check_patterns.py alone can't catch that: it confirms a pattern
# is UNIQUE, but a mask could uniquely match a DIFFERENT function at a different
# site and still read as "UNIQUE / CLEAN".
#
# This tool closes that hole. Given the OLD (shipping) patterns.hpp and the NEW
# (masked) patterns.hpp, for every pattern that changed it asserts, against a
# real steamclient.so:
#
#   (1) the OLD pattern resolves to exactly ONE site   (baseline is pinnable), and
#   (2) the NEW pattern resolves to exactly ONE site,  (mask stayed unique), and
#   (3) that site is the SAME address as the OLD one.  (mask didn't move it)
#
# If any changed pattern fails, the mask is rejected (non-zero exit). A mask that
# turns a pattern ambiguous, un-findable, or that re-points it self-rejects here
# — so a bad mask can never reach a release.
#
# The RVA moves between builds, so "same site" is checked WITHIN one binary
# (old-pattern RVA vs new-pattern RVA on the same file), not across builds.
# Run it on the current build (and ideally a couple of prior ones) on the CI
# runner, where Valve's CDN is reachable:
#
#   python3 tools/verify_mask.py steamclient.so \
#       --old <baseline src/patterns.hpp> --new src/patterns.hpp [--json out.json]
#
# In CI the baseline is the pre-PR patterns.hpp (e.g. `git show
# origin/main:src/patterns.hpp`).
#
# Exit codes:
#   0  every changed pattern stayed unique AND resolved to the same site.
#   2  at least one mask is UNSAFE (ambiguous / not found / moved site / the
#      baseline itself wasn't unique so the change can't be validated).
#   1  usage / I/O / unparseable-binary error.
import argparse
import json
import os
import sys

# Reuse the exact ELF loader, patterns.hpp parser and scanner check_patterns.py
# uses, so this tool and the CI validator can never disagree on how a pattern
# resolves.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_patterns import load_exec_segments, parse_patterns_hpp, scan_rvas  # noqa: E402


def evaluate_masks(segments, old, new):
    """Core comparison, decoupled from ELF/IO for unit testing.

    `segments` is [(vaddr, bytes), ...] (as load_exec_segments returns).
    `old` / `new` are {constant_name: "byte pattern"} dicts.

    Returns (all_safe: bool, results: list[dict]). One result per pattern that
    exists in BOTH dicts and whose pattern string CHANGED; unchanged patterns and
    patterns only present on one side are skipped (a pure add/remove isn't a mask
    to validate here).
    """
    results = []
    all_safe = True

    for name, new_pat in new.items():
        if name not in old:
            continue                      # brand-new pattern, not a mask
        old_pat = old[name]
        if old_pat == new_pat:
            continue                      # untouched

        old_rvas = scan_rvas(segments, old_pat)
        new_rvas = scan_rvas(segments, new_pat)

        entry = {
            "name": name,
            "old_pattern": old_pat,
            "new_pattern": new_pat,
            "old_count": len(old_rvas),
            "new_count": len(new_rvas),
            "old_rva": ("0x%x" % old_rvas[0]) if len(old_rvas) == 1 else None,
            "new_rva": ("0x%x" % new_rvas[0]) if len(new_rvas) == 1 else None,
        }

        if len(old_rvas) != 1:
            # Can't pin what the mask is supposed to preserve. Fail safe: on a
            # real build every critical baseline is unique (check_patterns would
            # already be BLOCKING otherwise), so this means the wrong binary or a
            # pattern that shouldn't be masked.
            entry["status"] = "BASELINE_NOT_UNIQUE"
            all_safe = False
        elif len(new_rvas) == 0:
            entry["status"] = "MASK_NOT_FOUND"
            all_safe = False
        elif len(new_rvas) > 1:
            entry["status"] = "MASK_AMBIGUOUS"
            all_safe = False
        elif new_rvas[0] != old_rvas[0]:
            entry["status"] = "MASK_MOVED_SITE"
            all_safe = False
        else:
            entry["status"] = "OK"        # unique AND same site
        results.append(entry)

    return all_safe, results


def main():
    ap = argparse.ArgumentParser(
        description="Verify src/patterns.hpp masks are safe against a steamclient.so.")
    ap.add_argument("steamclient", help="path to the ubuntu12_32/steamclient.so to check")
    ap.add_argument("--old", required=True, help="baseline (pre-mask) patterns.hpp")
    ap.add_argument("--new", default="src/patterns.hpp", help="candidate (masked) patterns.hpp")
    ap.add_argument("--json", default=None, help="optional machine-readable result path")
    args = ap.parse_args()

    try:
        segments = load_exec_segments(args.steamclient)
        old = parse_patterns_hpp(args.old)
        new = parse_patterns_hpp(args.new)
    except (OSError, ValueError) as e:
        print("ERROR: %s" % e, file=sys.stderr)
        return 1

    all_safe, results = evaluate_masks(segments, old, new)

    if not results:
        print("No masked patterns to verify (old and new patterns.hpp agree on every "
              "pattern present in both).")
    for r in results:
        same = ("same site" if r["status"] == "OK"
                else "old=%s new=%s" % (r["old_rva"], r["new_rva"]))
        print("  %-28s %-18s (old x%d, new x%d) %s"
              % (r["name"], r["status"], r["old_count"], r["new_count"], same))

    print("")
    if all_safe:
        print("VERDICT: MASKS SAFE (every changed pattern stayed unique and on-site).")
        exit_code = 0
    else:
        print("VERDICT: UNSAFE MASK — do NOT ship. See statuses above.")
        exit_code = 2

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"all_safe": all_safe, "results": results}, f, indent=2)

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
