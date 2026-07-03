#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# write_pattern_group.py — publish a PatternGroups entry into res/updates.yaml.
#
# When a Steam update CHANGES a byte pattern, tools/apply_derived_pattern.py
# updates src/patterns.hpp and res/version.txt bumps to a NEW group id. This tool
# then extracts the (now current) 5 patterns from patterns.hpp and writes them as
# a new PatternGroups[<group>] block, so a deployed lumalinux self-heals to the
# new patterns via its runtime fetch (see src/update.cpp applyPatternOverrides)
# instead of requiring a rebuild + reinstall.
#
# Insert-only + idempotent: a group's patterns never change (a change = a NEW
# group), so if PatternGroups[<group>] already exists this is a no-op. Text-based
# to keep the file's comments/formatting intact.
#
# Usage:
#   tools/write_pattern_group.py [--group <id>] \
#       [--patterns src/patterns.hpp] [--updates res/updates.yaml] \
#       [--version-file res/version.txt]
# --group defaults to the value in res/version.txt.
import argparse
import re
import sys

# C++ constant name in patterns.hpp -> YAML pattern name in PatternGroups.
_NAME_MAP = {
    "kDepotKeyFnPattern":           "DepotKey",
    "kBuildDepotDependencyPattern": "BuildDep",
    "kLoadPackagePattern":          "LoadPackage",
    "kGmrcFunctionPattern":         "Gmrc",
    "kShaderCacheDepotPattern":     "ShaderDepot",
}


def _extract_patterns(hpp_text):
    """Return {yaml_name: pattern_string} pulled from the kXxx constants."""
    out = {}
    for cname, yname in _NAME_MAP.items():
        # inline constexpr const char* kName = "....";  (string on same or next line)
        m = re.search(re.escape(cname) + r'\s*=\s*"([^"]*)"', hpp_text)
        if m:
            out[yname] = m.group(1)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--group", default="")
    ap.add_argument("--patterns", default="src/patterns.hpp")
    ap.add_argument("--updates", default="res/updates.yaml")
    ap.add_argument("--version-file", default="res/version.txt")
    args = ap.parse_args()

    group = args.group.strip() or open(args.version_file).read().strip()

    pats = _extract_patterns(open(args.patterns).read())
    missing = [y for y in _NAME_MAP.values() if y not in pats]
    if missing:
        print("ERROR: could not extract patterns: %s" % ", ".join(missing),
              file=sys.stderr)
        return 1

    lines = open(args.updates).read().splitlines()

    # Locate PatternGroups:
    pg = None
    for i, l in enumerate(lines):
        if l.rstrip() == "PatternGroups:" and not l.startswith((" ", "\t")):
            pg = i
            break
    if pg is None:
        print("ERROR: no PatternGroups: section in %s" % args.updates, file=sys.stderr)
        return 1

    # Idempotent: skip if this group already exists (quoted or bare key).
    for l in lines[pg + 1:]:
        if l.strip() in ('"%s":' % group, "%s:" % group):
            print("PatternGroups[%s] already present — nothing to do." % group)
            return 0

    block = ['  "%s":' % group, "    schema: 1"]
    for name in ("DepotKey", "BuildDep", "Gmrc", "ShaderDepot"):
        block.append('    %s: "%s"' % (name, pats[name]))
    # LoadPackage carries a match_index hint (multi-match prologue).
    block.append("    LoadPackage:")
    block.append('      pattern: "%s"' % pats["LoadPackage"])
    block.append("      match_index: 0")

    lines[pg + 1:pg + 1] = block          # insert as first group under PatternGroups:
    open(args.updates, "w").write("\n".join(lines) + "\n")
    print("wrote PatternGroups[%s] (%d patterns)" % (group, len(pats)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
