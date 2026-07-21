#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# check_patterns.py — CI-side pattern VALIDATOR (no Ghidra).
#
# This is the VALIDATE half of tools/derive_patterns.py, ported off Ghidra so a
# GitHub Action (.github/workflows/watch-steam.yml) can run it against a
# freshly-downloaded steamclient.so. It does NOT re-derive moved patterns — that
# stays in derive_patterns.py (Ghidra). It only answers the question the daily
# monitor needs:
#
#   "Do the CURRENT patterns from src/patterns.hpp still resolve UNIQUELY in this
#    binary, and are the package-0 finder's runtime anchors still present?"
#
# i.e. the A.1 (hash bump) vs A.2 (pattern moved) vs C (finder anchor moved)
# distinction from docs/maintenance.md, computed in pure Python (no Ghidra, no
# capstone, no running the .so).
#
# Pattern source of truth: src/patterns.hpp is PARSED at runtime — patterns are
# never re-hardcoded here, so this can't drift from what the binary ships. The
# two finder anchors are NOT in patterns.hpp (the finder derives them at runtime
# from stable struct offsets); they are mirrored here from
# src/hooks/package_zero_finder.cpp exactly as derive_patterns.py mirrors them.
#
# Usage:
#   python3 tools/check_patterns.py <steamclient.so> [--hpp src/patterns.hpp]
#                                                    [--json result.json]
#
# Exit codes (the workflow branches on these):
#   0  CLEAN             all criticals UNIQUE, finder anchors present,
#                        ShaderDepot UNIQUE  -> A.1: PR the hash, no issue.
#   2  NONCRITICAL_MOVED criticals + anchors OK but ShaderDepot moved
#                        -> A.1: PR the hash AND open a ShaderDepot issue.
#   3  BLOCKING          a critical pattern or a finder anchor failed
#                        -> open an issue (A.2 / C); do NOT whitelist.
#   1  usage / I/O / unparseable-binary error.
import argparse
import hashlib
import json
import re
import struct
import sys

# ── hook classification ──────────────────────────────────────────────────────
# Keyed by the patterns.hpp constant name. The display name is what the
# workflow puts in the issue/PR. "expect" is how many matches mean OK.
#
#   CRITICAL    — must resolve to exactly ONE site or the hook can't install.
#                 A miss/ambiguity here blocks the whitelist (A.2).
#   NONCRITICAL — a miss loses an optional feature but installs still work, so
#                 the hash can still be whitelisted; we open an issue + auto-derive
#                 so it gets fixed. ShaderDepot (per-game shader skip) and Reconcile
#                 (NotifyLicensesUpdated, the no-restart Add Game path — a miss only
#                 costs a Steam restart, degrades cleanly) live here. Which of these
#                 resolved on a build is recorded per-hash in updates.yaml (`caps:`)
#                 so LumaDeck's update gate knows what a build would lose.
#   DIAGNOSTIC  — never blocks, never opens an issue; reported for information
#                 only. LoadPackage (opt-in, multi-match EXPECTED — 3 candidates,
#                 runtime picks index 0) and BuildDep (disabled since SLSsteam
#                 20260714 owns BuildDepotDependency and hooks it first) live here.
CRITICAL = {
    "kDepotKeyFnPattern":          "DepotKey",
    "kGmrcFunctionPattern":        "GMRC",
}
NONCRITICAL = {
    "kShaderCacheDepotPattern":    "ShaderDepot",
    "kNotifyLicensesUpdatedPattern": "Reconcile",
}
# Short capability token per non-critical const, written into updates.yaml as a
# `# caps: <token>=ok|moved ...` comment next to each whitelisted hash. LumaDeck
# text-scans it to decide, per build, whether an update would lose a feature
# (shader pre-cache skip / no-restart Add Game). `ok` = resolved uniquely on this
# build; `moved` = the pattern didn't resolve (the feature degrades, non-blocking).
CAP_TOKEN = {
    "kShaderCacheDepotPattern":      "shader",
    "kNotifyLicensesUpdatedPattern": "reconcile",
}
DIAGNOSTIC = {
    "kLoadPackagePattern": "LoadPackage",
    # BuildDep is disabled at runtime since SLSsteam 20260714 owns
    # BuildDepotDependency (it hooks the prologue in memory first). We no longer
    # install the hook, so a pattern miss must NOT block the whitelist. Validate
    # for information only; re-promote to CRITICAL if BuildDep is re-enabled.
    "kBuildDepotDependencyPattern": "BuildDep",
}

# ── finder anchors (§13.5) — mirrored from package_zero_finder.cpp ────────────
# (a) cache-access idiom, anchored on the 0xc58 tree-root offset of
#     CPackageInfoCache:  lea r1,[GOT+X] ; mov r2,[r1] ; mov r3,[r2+0xc58].
#     The 0xc58 disp32 little-endian (58 0C 00 00) is the unambiguous needle;
#     we validate the lea/mov/mov shape backwards from each hit.
# (b) GMRC prologue tail that survives the hook detour — the bytes DeriveGotBase
#     scans for.
CACHE_ROOT_OFFSET = 0xC58
CACHE_IDIOM_NEEDLE = "58 0C 00 00"
GMRC_PROLOGUE_TAIL = "55 89 E5 57 56 53 81 EC 10 01 00 00 8B 7D 08 8B 4D 20"


# ── patterns.hpp parsing ─────────────────────────────────────────────────────

def parse_patterns_hpp(path):
    """Extract {constant_name: "byte pattern"} for every kXxxPattern in the
    header. The decls look like:
        inline constexpr const char* kDepotKeyFnPattern =
            "55 57 56 ...";
    so we match the name and the (possibly next-line) string literal."""
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    out = {}
    for m in re.finditer(r'(\bk\w+Pattern)\s*=\s*"([0-9A-Fa-f? ]+)"', text):
        out[m.group(1)] = m.group(2).strip()
    return out


# ── ELF: executable segments ─────────────────────────────────────────────────

def load_exec_segments(path):
    """Return [(vaddr, bytes), ...] for every PT_LOAD|PF_X segment of a 32-bit
    little-endian ELF. vaddr is the segment's p_vaddr, which for a shared object
    (image base 0) IS the RVA base — so a match at byte offset `o` within the
    segment has RVA = vaddr + o, matching derive_patterns.py's RVA reporting."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF":
        raise ValueError("not an ELF file")
    ei_class, ei_data = data[4], data[5]
    if ei_class != 1:
        raise ValueError("not ELFCLASS32 (steamclient.so is 32-bit)")
    if ei_data != 1:
        raise ValueError("not little-endian")
    # Elf32_Ehdr: e_phoff@0x1C(4), e_phentsize@0x2A(2), e_phnum@0x2C(2)
    e_phoff = struct.unpack_from("<I", data, 0x1C)[0]
    e_phentsize = struct.unpack_from("<H", data, 0x2A)[0]
    e_phnum = struct.unpack_from("<H", data, 0x2C)[0]
    PT_LOAD, PF_X = 1, 0x1
    segs = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        # Elf32_Phdr: p_type(0) p_offset(4) p_vaddr(8) ... p_filesz(16) p_flags(24)
        p_type, p_offset, p_vaddr = struct.unpack_from("<III", data, off)
        p_filesz = struct.unpack_from("<I", data, off + 16)[0]
        p_flags = struct.unpack_from("<I", data, off + 24)[0]
        if p_type == PT_LOAD and (p_flags & PF_X):
            segs.append((p_vaddr, data[p_offset:p_offset + p_filesz]))
    if not segs:
        raise ValueError("no executable PT_LOAD segment found")
    return segs


# ── byte-pattern scanning ────────────────────────────────────────────────────

def pattern_to_regex(patstr):
    """Compile a 'AA BB ?? CC' pattern into a bytes regex: concrete bytes are
    escaped literally, '??' becomes '.'. DOTALL is essential so a wildcard can
    match a 0x0A byte (without it, '.' would skip newlines and miss matches)."""
    parts = []
    for tok in patstr.split():
        if tok.startswith("?"):
            parts.append(b".")
        else:
            parts.append(re.escape(bytes([int(tok, 16)])))
    return re.compile(b"".join(parts), re.DOTALL)


def scan_rvas(segments, patstr):
    """Return the list of RVAs where `patstr` matches across all exec segments.
    Patterns are long, distinctive prologues, so non-overlapping finditer is
    sufficient (overlaps are not a real concern at 28+ bytes)."""
    rx = pattern_to_regex(patstr)
    hits = []
    for vaddr, buf in segments:
        for m in rx.finditer(buf):
            hits.append(vaddr + m.start())
    return hits


def verify_cache_idiom(segments):
    """Find every valid cache-access idiom and return [(rva, lea_disp32), ...].
    For each `58 0C 00 00` needle the layout backwards is:
        base+0 = 8D (lea r1,[GOT+disp32])   base+2..5 = disp32
        base+6 = 8B (mov r2,[r1])
        base+8 = 8B (mov r3,[r2+0xc58])      base+10 = 58 0C 00 00 (the needle)
    so base = needle_off - 10. Validate the 8D/8B/8B shape before accepting."""
    needle = bytes(int(t, 16) for t in CACHE_IDIOM_NEEDLE.split())
    out = []
    for vaddr, buf in segments:
        start = 0
        while True:
            o = buf.find(needle, start)
            if o < 0:
                break
            start = o + 1
            base = o - 10
            if base < 0:
                continue
            if buf[base] == 0x8D and buf[base + 6] == 0x8B and buf[base + 8] == 0x8B:
                disp = struct.unpack_from("<i", buf, base + 2)[0]
                out.append((vaddr + base, disp))
    return out


# ── status helpers ───────────────────────────────────────────────────────────

def classify_hit_count(n):
    if n == 1:
        return "UNIQUE"
    if n == 0:
        return "NOT_FOUND"
    return "AMBIGUOUS"


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# ── main ─────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(
        description="Validate lumalinux patterns against a steamclient.so (no Ghidra).")
    ap.add_argument("steamclient", help="path to the ubuntu12_32/steamclient.so to check")
    ap.add_argument("--hpp", default="src/patterns.hpp",
                    help="path to src/patterns.hpp (pattern source of truth)")
    ap.add_argument("--json", default=None,
                    help="optional path to write the machine-readable result")
    args = ap.parse_args()

    try:
        segments = load_exec_segments(args.steamclient)
        patterns = parse_patterns_hpp(args.hpp)
        sha = sha256_file(args.steamclient)
    except (OSError, ValueError) as e:
        print("ERROR: %s" % e, file=sys.stderr)
        return 1

    result = {
        "steamclient_sha256": sha,
        "hooks": {},          # name -> {label, status, count, rvas}
        "finder": {},         # anchor -> {status, ...}
        "blocking": [],       # display names of blocking failures
        "noncritical_moved": [],        # display names of moved non-criticals
        "noncritical_moved_consts": [], # patterns.hpp consts of the above (for --only)
        "caps": {},           # cap token -> "ok" | "moved" (all non-criticals)
        "caps_str": "",       # "shader=ok reconcile=moved" for the updates.yaml comment
        "verdict": None,
        "exit_code": None,
    }

    def record(const, label, tier):
        pat = patterns.get(const)
        if pat is None:
            # The constant vanished from patterns.hpp — treat as a hard error so
            # a rename can't silently drop a hook from the check.
            result["hooks"][label] = {"tier": tier, "status": "MISSING_FROM_HPP",
                                      "count": 0, "rvas": []}
            return "MISSING_FROM_HPP"
        rvas = scan_rvas(segments, pat)
        status = classify_hit_count(len(rvas))
        result["hooks"][label] = {
            "tier": tier, "status": status, "count": len(rvas),
            "rvas": ["0x%x" % r for r in rvas[:8]],
        }
        return status

    # 1) critical hooks — must be UNIQUE
    for const, label in CRITICAL.items():
        if record(const, label, "critical") != "UNIQUE":
            result["blocking"].append(label)

    # 2) non-criticals (ShaderDepot, Reconcile) — a miss opens an issue + auto-
    # derive but still PR-able. Record per-hook capability (ok/moved) so the
    # whitelist can carry it and LumaDeck knows what a build would lose.
    for const, label in NONCRITICAL.items():
        ok = record(const, label, "noncritical") == "UNIQUE"
        token = CAP_TOKEN.get(const, label.lower())
        result["caps"][token] = "ok" if ok else "moved"
        if not ok:
            result["noncritical_moved"].append(label)
            result["noncritical_moved_consts"].append(const)
    # stable order: follow CAP_TOKEN declaration order
    result["caps_str"] = " ".join(
        "%s=%s" % (CAP_TOKEN[c], result["caps"][CAP_TOKEN[c]])
        for c in NONCRITICAL if CAP_TOKEN.get(c) in result["caps"])

    # 3) LoadPackage — diagnostic-only, multi-match expected; never blocks
    for const, label in DIAGNOSTIC.items():
        record(const, label, "diagnostic")

    # 4a) finder cache-access idiom (0xc58)
    idiom = verify_cache_idiom(segments)
    if idiom:
        result["finder"]["cache_idiom"] = {
            "status": "PRESENT",
            "sites": [{"rva": "0x%x" % r, "disp32": "0x%x" % (d & 0xFFFFFFFF)}
                      for r, d in idiom[:8]],
        }
    else:
        result["finder"]["cache_idiom"] = {"status": "NOT_FOUND"}
        result["blocking"].append("finder:cache_idiom(0x%x)" % CACHE_ROOT_OFFSET)

    # 4b) GMRC prologue tail
    tail_rvas = scan_rvas(segments, GMRC_PROLOGUE_TAIL)
    tail_status = classify_hit_count(len(tail_rvas))
    result["finder"]["gmrc_tail"] = {
        "status": tail_status, "count": len(tail_rvas),
        "rvas": ["0x%x" % r for r in tail_rvas[:8]],
    }
    # NOT_FOUND blocks (DeriveGotBase can't work). AMBIGUOUS is only a warning:
    # DeriveGotBase finds the right one at runtime, but flag it for a tighter
    # anchor — it does not block the whitelist.
    if tail_status == "NOT_FOUND":
        result["blocking"].append("finder:gmrc_tail")

    # ── verdict + exit code ──────────────────────────────────────────────────
    if result["blocking"]:
        result["verdict"] = "BLOCKING"
        result["exit_code"] = 3
    elif result["noncritical_moved"]:
        result["verdict"] = "NONCRITICAL_MOVED"
        result["exit_code"] = 2
    else:
        result["verdict"] = "CLEAN"
        result["exit_code"] = 0

    # ── human report ─────────────────────────────────────────────────────────
    print("steamclient.so SHA-256: %s" % sha)
    print("patterns.hpp:           %s" % args.hpp)
    print("")
    for label, info in result["hooks"].items():
        rva = (" @ " + info["rvas"][0]) if info["rvas"] else ""
        extra = ""
        if info["tier"] == "diagnostic":
            extra = "  (diagnostic-only; does not block)"
        elif info["tier"] == "noncritical":
            extra = "  (non-critical)"
        print("  %-12s %-13s%s%s" % (label, info["status"], rva, extra))
    ci = result["finder"]["cache_idiom"]
    print("  %-12s %s" % ("cache_idiom", ci["status"]))
    gt = result["finder"]["gmrc_tail"]
    print("  %-12s %s (%d)" % ("gmrc_tail", gt["status"], gt["count"]))
    print("")
    print("VERDICT: %s (exit %d)" % (result["verdict"], result["exit_code"]))
    if result["blocking"]:
        print("  blocking: %s  -> open issue (A.2/C), do NOT whitelist" %
              ", ".join(result["blocking"]))
    if result["noncritical_moved"]:
        print("  shader/non-critical moved: %s  -> PR the hash AND open issue" %
              ", ".join(result["noncritical_moved"]))
    if result["verdict"] == "CLEAN":
        print("  -> A.1 hash bump: PR the hash to res/updates.yaml, no release.")

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(result, f, indent=2)

    return result["exit_code"]


if __name__ == "__main__":
    sys.exit(main())
