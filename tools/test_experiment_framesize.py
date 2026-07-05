#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Unit test for experiment_framesize_mask.py's frame-size masking transform.
# No steamclient.so needed — tests the token surgery only.
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from experiment_framesize_mask import mask_framesize  # noqa: E402

fails = 0


def check(cond, msg):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        fails += 1


# imm8 form (DepotKey-like): 83 EC 24
check(mask_framesize("55 83 EC 24 8B", "high_keep") == "55 83 EC ?? 8B",
      "imm8 high_keep wildcards the single imm byte")
check(mask_framesize("55 83 EC 24 8B", "full") == "55 83 EC ?? 8B",
      "imm8 full wildcards the single imm byte (same as high_keep)")

# imm32 form (BuildDep/GMRC-like): 81 EC 2C 02 00 00
check(mask_framesize("81 EC 2C 02 00 00 8B", "high_keep") == "81 EC ?? ?? 00 00 8B",
      "imm32 high_keep wildcards low 2 bytes, keeps high 00 00")
check(mask_framesize("81 EC 2C 02 00 00 8B", "full") == "81 EC ?? ?? ?? ?? 8B",
      "imm32 full wildcards all 4 imm bytes")

# no frame-size op -> None
check(mask_framesize("55 89 E5 57 56 53", "full") is None,
      "no frame-size op returns None")

# already-wildcarded imm is left alone (idempotent-ish; nothing new touched)
check(mask_framesize("81 EC ?? ?? 00 00", "high_keep") is None,
      "already-masked low bytes -> nothing to do -> None")

print("")
if fails:
    print("%d CHECK(S) FAILED" % fails)
    sys.exit(1)
print("all checks passed")
