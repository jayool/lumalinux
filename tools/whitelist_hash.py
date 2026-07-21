#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# whitelist_hash.py — append a steamclient.so SHA-256 to res/updates.yaml.
#
# Used by .github/workflows/watch-steam.yml on a CLEAN check_patterns verdict
# (A.1 hash bump): the new client's hash is added under the CURRENT SafeMode
# version group (the value in res/version.txt) so the deployed .so accepts it on
# next boot — no rebuild, no release.
#
# Appends the hash to SafeModeHashes (the flat list src/update.cpp reads) under
# the current version group, preceded by a `# steam_version: <n>` COMMENT when
# --steam-version is given, and a `# caps: <tokens>` comment when --caps is given
# (e.g. `caps: shader=ok reconcile=moved` — which non-critical hooks resolved on
# that build). lumalinux ignores both comments; LumaDeck's safe-to-update gate
# text-scans this file for `steam_version: <pin>` and `caps:` to answer "is this
# build supported, and what would it lose?", so the metadata lives next to its
# hash — one section, no separate Builds sidecar. Text-based to preserve comments.
#
# Idempotent per section: re-running never duplicates an entry.
#
# Usage:
#   tools/whitelist_hash.py <sha256> [--note "free text"] \
#       [--steam-version 1782344391] [--build-id <hex>] \
#       [--updates res/updates.yaml] [--version-file res/version.txt]
import argparse
import sys


def _insert_into_safemode(lines, group, sha, steam_version, build_id, note, caps):
    """Append `  - <sha>` under SafeModeHashes[<group>], preceded by a
    `# steam_version: <n>` comment and (when given) a `# caps: <tokens>` comment
    (both read by LumaDeck's text-scan gate; ignored by lumalinux). Returns
    (ok, msg)."""
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
    if steam_version:
        meta = "# steam_version: %s" % steam_version
        if build_id:
            meta += "   (build %s)" % build_id
        ins.append("%s%s" % (item_indent, meta))
    elif build_id:
        ins.append("%s# build %s" % (item_indent, build_id))
    if caps:
        ins.append("%s# caps: %s" % (item_indent, caps))
    if note:
        for nl in note.splitlines():
            ins.append("%s# %s" % (item_indent, nl))
    ins.append("%s- %s" % (item_indent, sha))
    lines[last_item + 1:last_item + 1] = ins
    tail = " with steam_version %s" % steam_version if steam_version else ""
    return True, "added to SafeModeHashes[%s]%s" % (group, tail)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sha256")
    ap.add_argument("--note", default="")
    ap.add_argument("--steam-version", default="")
    ap.add_argument("--build-id", default="")
    ap.add_argument("--caps", default="",
                    help="per-build non-critical capability tokens, e.g. "
                         "'shader=ok reconcile=moved' (from check_patterns caps_str). "
                         "Written as a '# caps:' comment; read by LumaDeck's gate.")
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
        ok, msg = _insert_into_safemode(lines, group, sha, args.steam_version,
                                        args.build_id, args.note, args.caps)
        if not ok:
            print("ERROR: %s in %s" % (msg, args.updates), file=sys.stderr)
            return 1
        did.append(msg)

    if not did:
        print("nothing to do (hash fully present).")
        return 0

    open(args.updates, "w").write("\n".join(lines) + "\n")
    print("%s under group %s: %s" % (sha, group, "; ".join(did)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
