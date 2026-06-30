#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# blocking_constants.py — map check_patterns.py's BLOCKING list to the
# patterns.hpp constants that auto-derivation (derive_patterns.py) can re-derive.
#
# watch-steam.yml uses this on a BLOCKING (exit 3) verdict to decide what, if
# anything, the auto-derive path should try to fix. It prints a comma-separated
# list of patterns.hpp constant names on stdout, or NOTHING when the blocker is
# not fixable by re-deriving a pattern — in which case the caller falls back to
# opening the tracking issue.
#
# We print an EMPTY list (so the caller bails to the issue) when:
#   * any blocker is a finder anchor ("finder:cache_idiom" / "finder:gmrc_tail"):
#     those are runtime struct offsets in package_zero_finder.cpp, a CODE fix,
#     not a pattern bump — re-deriving patterns.hpp can never make
#     check_patterns return CLEAN, so spinning up Ghidra would be wasted, or
#   * a blocking hook name isn't one we know how to auto-derive (so we can't
#     promise the re-validation will come back clean).
#
# DepotKey IS included (derive_patterns tries the dispatcher vcall walk); if that
# walk fails it simply won't appear in derived.json and the apply step will fall
# back to the issue. BuildDep / GMRC are the reliable string-anchored ones.
#
# Usage:  blocking_constants.py result.json   ->  prints e.g.
#         "kBuildDepotDependencyPattern,kGmrcFunctionPattern"  (or empty)
import json
import sys

# check_patterns.py display name -> patterns.hpp constant. Mirrors the CRITICAL
# map in check_patterns.py (kept in sync by hand; both are tiny).
HOOK_CONST = {
    "BuildDep":  "kBuildDepotDependencyPattern",
    "GMRC":      "kGmrcFunctionPattern",
    "DepotKey":  "kDepotKeyFnPattern",
}


def constants_for(result):
    blocking = result.get("blocking", [])
    if not blocking:
        return []
    consts = []
    for b in blocking:
        b = str(b)
        if b.startswith("finder:"):
            return []          # code fix, not a pattern bump -> bail to issue
        c = HOOK_CONST.get(b)
        if not c:
            return []          # unknown blocker -> can't promise CLEAN -> bail
        consts.append(c)
    # de-dup while preserving order
    seen = set()
    out = []
    for c in consts:
        if c not in seen:
            seen.add(c)
            out.append(c)
    return out


def main():
    if len(sys.argv) != 2:
        sys.stderr.write("usage: blocking_constants.py result.json\n")
        return 1
    try:
        with open(sys.argv[1], "r", encoding="utf-8") as f:
            result = json.load(f)
    except (OSError, ValueError):
        return 0  # unreadable -> print nothing, caller bails to issue
    sys.stdout.write(",".join(constants_for(result)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
