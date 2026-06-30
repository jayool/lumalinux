#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_derived_pattern.py — CI-side patcher (no Ghidra).
#
# Takes the machine-readable derivations emitted by tools/derive_patterns.py
# (`--json`) and rewrites the matching byte-pattern string literals in
# src/patterns.hpp, so .github/workflows/watch-steam.yml can turn a "pattern
# moved" event into a ready-to-review PR instead of a bare issue.
#
# It is deliberately conservative:
#   * it ONLY touches a constant when the derivation reports a UNIQUE
#     (matches == 1) fresh pattern, and
#   * it never invents a constant that isn't already present in patterns.hpp.
# Anything it can't apply is reported and left for a human (the workflow falls
# back to opening an issue).
#
# The literal it edits is matched with the SAME regex shape that
# tools/check_patterns.py uses to read patterns.hpp, so what we patch is exactly
# what the validator (and the shipped binary) sees.
#
# Usage:
#   apply_derived_pattern.py --derived derived.json \
#       --only kShaderCacheDepotPattern[,kGmrcFunctionPattern,...] \
#       [--hpp src/patterns.hpp]
#
# Exit codes (the workflow branches on these):
#   0  every requested constant was applied or already current
#   4  at least one requested constant could NOT be applied (not derived, not
#      unique, or absent from the header) -> caller falls back to an issue
#   1  usage / I/O / parse error
import argparse
import json
import re
import sys

# Same character class check_patterns.parse_patterns_hpp accepts inside the
# quotes: hex digits, '?', and spaces. Kept on one line (patterns are single
# line literals) so the token never swallows the closing quote / following code.
_PAT_TOKEN = r'[0-9A-Fa-f? ]+'


def literal_re(const):
    """Regex capturing (prefix up to the opening quote)(pattern)(closing quote)
    for a given patterns.hpp constant, e.g. kShaderCacheDepotPattern = "..."."""
    return re.compile(r'(\b' + re.escape(const) + r'\s*=\s*")(' + _PAT_TOKEN + r')(")')


def apply_one(text, const, entry):
    """Try to apply `entry` for `const` to `text`. Returns
    (new_text, status) where status is one of:
      'applied' | 'unchanged' | 'not-derived' | 'not-unique' | 'absent'."""
    if not entry or "pattern" not in entry:
        return text, "not-derived"
    try:
        matches = int(entry.get("matches", 0))
    except (TypeError, ValueError):
        matches = 0
    if matches != 1:
        return text, "not-unique"
    new_pat = " ".join(str(entry["pattern"]).split())   # normalise whitespace
    rx = literal_re(const)
    m = rx.search(text)
    if not m:
        return text, "absent"
    if m.group(2).strip() == new_pat:
        return text, "unchanged"
    new_text = text[:m.start()] + m.group(1) + new_pat + m.group(3) + text[m.end():]
    return new_text, "applied"


def main():
    ap = argparse.ArgumentParser(
        description="Apply derived byte-patterns into patterns.hpp.")
    ap.add_argument("--derived", required=True,
                    help="derived.json emitted by derive_patterns.py --json")
    ap.add_argument("--only", required=True,
                    help="comma-separated patterns.hpp constant names to apply")
    ap.add_argument("--hpp", default="src/patterns.hpp",
                    help="path to src/patterns.hpp (default: %(default)s)")
    args = ap.parse_args()

    try:
        with open(args.hpp, "r", encoding="utf-8") as f:
            text = f.read()
        with open(args.derived, "r", encoding="utf-8") as f:
            derived = json.load(f)
    except (OSError, ValueError) as e:
        print("ERROR: %s" % e, file=sys.stderr)
        return 1

    wanted = [c.strip() for c in args.only.split(",") if c.strip()]
    if not wanted:
        print("ERROR: --only listed no constants", file=sys.stderr)
        return 1

    applied, unchanged, failed = [], [], []
    for const in wanted:
        text, status = apply_one(text, const, derived.get(const))
        if status == "applied":
            new_pat = " ".join(str(derived[const]["pattern"]).split())
            print("  %-30s APPLIED" % const)
            print("      new: %s" % new_pat)
            applied.append(const)
        elif status == "unchanged":
            print("  %-30s unchanged (already current)" % const)
            unchanged.append(const)
        elif status == "not-derived":
            print("  %-30s NOT DERIVED (absent from %s)" % (const, args.derived))
            failed.append(const)
        elif status == "not-unique":
            print("  %-30s REFUSED (matches != 1, not unique)" % const)
            failed.append(const)
        else:  # absent
            print("  %-30s ABSENT from %s" % (const, args.hpp))
            failed.append(const)

    if applied:
        try:
            with open(args.hpp, "w", encoding="utf-8") as f:
                f.write(text)
        except OSError as e:
            print("ERROR: writing %s: %s" % (args.hpp, e), file=sys.stderr)
            return 1

    print("\nsummary: %d applied, %d unchanged, %d failed"
          % (len(applied), len(unchanged), len(failed)))
    return 4 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
