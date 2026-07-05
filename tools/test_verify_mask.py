#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Unit test for verify_mask.py's core (evaluate_masks), using synthetic exec
# segments so it needs no real steamclient.so. Exercises the three outcomes that
# make masking safe: an on-site mask passes; an ambiguous mask and a
# site-moving mask both fail.
#
#   python3 tools/test_verify_mask.py     # from repo root, after nothing special
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from verify_mask import evaluate_masks  # noqa: E402

VADDR = 0x1000
fails = 0


def check(cond, msg):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        fails += 1


def seg(*chunks):
    """Build one exec segment: chunks are (offset, hexbytes) placed into a zero
    buffer. Returns [(vaddr, bytes)] as load_exec_segments would."""
    size = 256
    buf = bytearray(size)
    for off, hexbytes in chunks:
        b = bytes(int(t, 16) for t in hexbytes.split())
        buf[off:off + len(b)] = b
    return [(VADDR, bytes(buf))]


# The "function prologue" whose last byte is a frame-size constant (0x24) we want
# to mask. Distinct filler around it keeps matches from bleeding together.
FN = "55 89 E5 83 EC 24"        # ...sub esp,0x24
MASK = "55 89 E5 83 EC ??"      # frame size wildcarded


# 1) On-site mask: the masked pattern still matches the SAME single site.
segments = seg((40, FN))
safe, res = evaluate_masks(segments, {"kFoo": FN}, {"kFoo": MASK})
check(safe is True, "on-site mask: verdict SAFE")
check(len(res) == 1 and res[0]["status"] == "OK", "on-site mask: status OK")
check(res[0]["old_rva"] == res[0]["new_rva"] == hex(VADDR + 40),
      "on-site mask: same RVA old and new")

# 2) Ambiguous mask: a SECOND site differs only in the masked byte, so the mask
#    now matches two places while the original matched one -> must FAIL.
segments = seg((40, FN), (90, "55 89 E5 83 EC 99"))
safe, res = evaluate_masks(segments, {"kFoo": FN}, {"kFoo": MASK})
check(safe is False, "ambiguous mask: verdict UNSAFE")
check(res[0]["status"] == "MASK_AMBIGUOUS" and res[0]["new_count"] == 2,
      "ambiguous mask: status MASK_AMBIGUOUS (new matched 2)")

# 3) Site-moving mask: a bogus "mask" that changes a real identifying byte so it
#    now uniquely matches a DIFFERENT function -> unique but wrong -> must FAIL.
#    old "55 89 E5 83 EC 24" @40 ; new "55 89 FF 83 EC ??" @120 only.
segments = seg((40, FN), (120, "55 89 FF 83 EC 24"))
safe, res = evaluate_masks(segments, {"kFoo": FN}, {"kFoo": "55 89 FF 83 EC ??"})
check(safe is False, "site-moving mask: verdict UNSAFE")
check(res[0]["status"] == "MASK_MOVED_SITE",
      "site-moving mask: status MASK_MOVED_SITE")
check(res[0]["old_rva"] == hex(VADDR + 40) and res[0]["new_rva"] == hex(VADDR + 120),
      "site-moving mask: old and new resolved to different RVAs")

# 4) Unchanged pattern: nothing to validate, and an unrelated new-only pattern is
#    ignored (a pure add is not a mask).
segments = seg((40, FN))
safe, res = evaluate_masks(segments, {"kFoo": FN}, {"kFoo": FN, "kBar": "90 90"})
check(safe is True and res == [], "unchanged + new-only: nothing to verify, SAFE")

# 5) Baseline not unique: if the OLD pattern isn't unique on this binary we can't
#    validate the mask -> fail safe.
segments = seg((40, FN), (90, FN))
safe, res = evaluate_masks(segments, {"kFoo": FN}, {"kFoo": MASK})
check(safe is False and res[0]["status"] == "BASELINE_NOT_UNIQUE",
      "non-unique baseline: fails safe (BASELINE_NOT_UNIQUE)")

print("")
if fails:
    print("%d CHECK(S) FAILED" % fails)
    sys.exit(1)
print("all checks passed")
