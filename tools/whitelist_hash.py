#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# whitelist_hash.py — append a steamclient.so SHA-256 to res/updates.yaml.
#
# Used by .github/workflows/watch-steam.yml on a CLEAN check_patterns verdict
# (A.1 hash bump): the new client's hash is added under the CURRENT SafeMode
# version group (the value in res/version.txt) so the deployed .so accepts it on
# next boot — no rebuild, no release.
#
# Writes TWO sections (both keyed to the SAME current group), text-based to keep
# the file's comments/formatting intact:
#   • SafeModeHashes — the flat hash whitelist read by src/update.cpp (field
#     binaries). Shape frozen: {group: [scalar_sha256, ...]}.
#   • Builds (v0.16, additive) — {sha256: {steam_version?, steam_build_id?,
#     pattern_group}} for LumaDeck's safe-to-update gate + lumalinux's runtime
#     pattern override. Skipped silently if the file has no `Builds:` section
#     (pre-v0.16 file) so this stays backward-safe.
#
# NOT written here: PatternGroups. A plain hash bump reuses the CURRENT group's
# patterns (they didn't change), so no PatternGroups edit is needed. A pattern
# CHANGE creates a NEW group and is handled by the pattern-derivation path.
#
# Idempotent per section: re-running never duplicates an entry.
#
# Usage:
#   tools/whitelist_hash.py <sha256> [--note "free text"] \
#       [--steam-version 1782344391] [--build-id <hex>] \
#       [--updates res/updates.yaml] [--version-file res/version.txt]
import argparse
import sys


def _find_top_key(lines, key):
    """Index of a top-level `key:` line (indent 0), or None."""
    for i, l in enumerate(lines):
        if l.rstrip() == key + ":" and not l.startswith((" ", "\t")):
            return i
    return None


def _insert_into_safemode(lines, group, sha, note):
    """Append `  - <sha>` under SafeModeHashes[<group>]. Returns (ok, msg)."""
    # locate the group key line: "  <group>:" (2-space indent, trailing colon)
    gi = None
    for i, l in enumerate(lines):
        if l.strip() == group + ":" and l.startswith("  "):
            gi = i
            break
    if gi is None:
        return False, "version group %r not found" % group

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
    if note:
        for nl in note.splitlines():
            ins.append("%s# %s" % (item_indent, nl))
    ins.append("%s- %s" % (item_indent, sha))
    lines[last_item + 1:last_item + 1] = ins
    return True, "added to SafeModeHashes[%s]" % group


def _insert_into_builds(lines, sha, group, steam_version, build_id):
    """Insert a Builds entry for <sha> (first child of `Builds:`). Returns msg.

    No-op (returns a note) when there's no `Builds:` section — keeps the tool
    safe against a pre-v0.16 updates.yaml.
    """
    bi = _find_top_key(lines, "Builds")
    if bi is None:
        return "no Builds section — skipped (pre-v0.16 file)"

    block = ["  %s:" % sha]
    if steam_version:
        block.append("    steam_version: %s" % steam_version)
    if build_id:
        block.append("    steam_build_id: %s" % build_id)
    block.append('    pattern_group: "%s"' % group)

    lines[bi + 1:bi + 1] = block          # insert as first entry under Builds:
    return "added to Builds"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sha256")
    ap.add_argument("--note", default="")
    ap.add_argument("--steam-version", default="")
    ap.add_argument("--build-id", default="")
    ap.add_argument("--updates", default="res/updates.yaml")
    ap.add_argument("--version-file", default="res/version.txt")
    args = ap.parse_args()

    sha = args.sha256.strip().lower()
    if len(sha) != 64 or any(c not in "0123456789abcdef" for c in sha):
        print("ERROR: not a 64-hex SHA-256: %r" % args.sha256, file=sys.stderr)
        return 1

    group = open(args.version_file).read().strip()
    text = open(args.updates).read()
    lines = text.splitlines()

    did = []

    # SafeModeHashes — skip if the exact "- <sha>" item already present.
    if any(l.strip() == "- " + sha for l in lines):
        print("SafeModeHashes: hash already present.")
    else:
        ok, msg = _insert_into_safemode(lines, group, sha, args.note)
        if not ok:
            print("ERROR: %s in %s" % (msg, args.updates), file=sys.stderr)
            return 1
        did.append(msg)

    # Builds — skip if a "<sha>:" key already present.
    if any(l.strip() == sha + ":" for l in lines):
        print("Builds: entry already present.")
    else:
        did.append(_insert_into_builds(lines, sha, group,
                                       args.steam_version, args.build_id))

    if not did:
        print("nothing to do (hash fully present).")
        return 0

    open(args.updates, "w").write("\n".join(lines) + "\n")
    print("%s under group %s: %s" % (sha, group, "; ".join(did)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
