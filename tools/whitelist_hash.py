#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# whitelist_hash.py — append a steamclient.so SHA-256 to res/updates.yaml.
#
# Used by .github/workflows/watch-steam.yml on a CLEAN check_patterns verdict
# (A.1 hash bump): the new client's hash is added under the CURRENT SafeMode
# version group (the value in res/version.txt) so the deployed .so accepts it on
# next boot — no rebuild, no release. Kept as its own script (not inline YAML)
# so the insertion logic is unit-tested.
#
# Idempotent: if the hash is already present anywhere in the file it does
# nothing and exits 0.
#
# Usage:
#   tools/whitelist_hash.py <sha256> [--note "free text"] \
#       [--updates res/updates.yaml] [--version-file res/version.txt]
import argparse
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sha256")
    ap.add_argument("--note", default="")
    ap.add_argument("--updates", default="res/updates.yaml")
    ap.add_argument("--version-file", default="res/version.txt")
    args = ap.parse_args()

    sha = args.sha256.strip().lower()
    if len(sha) != 64 or any(c not in "0123456789abcdef" for c in sha):
        print("ERROR: not a 64-hex SHA-256: %r" % args.sha256, file=sys.stderr)
        return 1

    group = open(args.version_file).read().strip()
    text = open(args.updates).read()
    if sha in text:
        print("hash already present — nothing to do.")
        return 0

    lines = text.splitlines()

    # locate the group key line: "  <group>:" (2-space indent, trailing colon)
    gi = None
    for i, l in enumerate(lines):
        if l.strip() == group + ":" and l.startswith("  "):
            gi = i
            break
    if gi is None:
        print("ERROR: version group %r not found in %s" % (group, args.updates),
              file=sys.stderr)
        return 1

    # the group's members are indented deeper than the key. Walk forward and
    # remember the last list item ("- ...") that belongs to this group; stop at
    # the first line indented <= the key (a new group / top-level key).
    key_indent = len(lines[gi]) - len(lines[gi].lstrip())
    last_item = gi
    for i in range(gi + 1, len(lines)):
        l = lines[i]
        if l.strip() == "":
            continue
        indent = len(l) - len(l.lstrip())
        if indent <= key_indent:
            break                       # left the group
        if l.lstrip().startswith("- "):
            last_item = i

    item_indent = " " * (key_indent + 2)
    ins = []
    if args.note:
        for nl in args.note.splitlines():
            ins.append("%s# %s" % (item_indent, nl))
    ins.append("%s- %s" % (item_indent, sha))

    lines[last_item + 1:last_item + 1] = ins
    open(args.updates, "w").write("\n".join(lines) + "\n")
    print("added %s under group %s" % (sha, group))
    return 0


if __name__ == "__main__":
    sys.exit(main())
