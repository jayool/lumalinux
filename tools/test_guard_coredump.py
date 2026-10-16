#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Unit test for the crash guard's second source of evidence: systemd-coredump.
#
# Why this test exists: SLSsteam 20261014151919 chokes crashhandler.so, so on a
# Deck with that release Steam creates /tmp/dumps and never writes a minidump
# into it. Measured on the Deck on 2026-10-16: crashhandler not mapped, dumps
# dir empty, and `coredumpctl list` the only record of Steam crashes. Reading an
# empty dumps dir as "clean boot" would reset the counter on every crash-loop
# iteration, so the guard must also ask coredumpctl, and must only treat the
# client's own core as a startup crash.
#
# The guard library is EXTRACTED FROM setup.sh (the heredoc write_guard_lib
# writes), so this tests the program that ships. coredumpctl is a stub on PATH
# that honours --since/--until against a list of fake cores, so the window
# logic is exercised without systemd.
#
#   python3 tools/test_guard_coredump.py     # from anywhere
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SETUP = os.path.join(ROOT, "setup.sh")

STUB = r'''#!/bin/sh
# fake coredumpctl: FAKE_CORES holds "epoch exe" lines; honours --since=@N --until=@N
since=0; until=9999999999
for a in "$@"; do case "$a" in --since=@*) since="${a#--since=@}";; --until=@*) until="${a#--until=@}";; esac; done
printf '%s\n' "$FAKE_CORES" | while read -r t exe; do
    [ -n "$t" ] || continue
    [ "$t" -ge "$since" ] && [ "$t" -le "$until" ] && printf 'Thu 2026-10-16 14:25:00 CEST 3379 1000 1000 SIGSEGV inaccessible %s -\n' "$exe"
done
'''


def extract_guard(src: str) -> str:
    m = re.search(r"cat > \"\$GUARD_LIB\" <<'GUARD'\n(.*?)\nGUARD\n", src, re.S)
    if not m:
        sys.exit("FAIL could not find the guard heredoc in setup.sh — did write_guard_lib change?")
    return m.group(1)


def run(guard: str, home: str, bindir: str, launch_epoch, fails_before: int, cores: str, dumps_dir: str | None):
    state = os.path.join(home, ".local/state/lumalinux")
    shutil.rmtree(state, ignore_errors=True); os.makedirs(state)
    if launch_epoch is not None:
        last = os.path.join(state, "last_launch")
        open(last, "w").close(); os.utime(last, (launch_epoch, launch_epoch))
    if fails_before:
        with open(os.path.join(state, "boot_fail_count"), "w") as fh:
            fh.write(str(fails_before))
    script = guard + f'''
STEAM_BIN=/bin/true
luma_exec_vanilla() {{ echo "VANILLA"; exit 0; }}
luma_guard_run
echo "RETURNED"
'''
    env = dict(os.environ, HOME=home, PATH=bindir + os.pathsep + os.environ["PATH"],
               FAKE_CORES=cores, LUMA_GUARD_DUMPS_DIR=dumps_dir or os.path.join(home, "no-such-dumps"))
    p = subprocess.run(["sh", "-c", script], capture_output=True, text=True, env=env)
    log = ""
    try:
        with open(os.path.join(state, "guard.log")) as fh:
            log = fh.read()
    except OSError:
        pass
    count = ""
    try:
        with open(os.path.join(state, "boot_fail_count")) as fh:
            count = fh.read().strip()
    except OSError:
        pass
    return p.stdout + p.stderr, log, count, os.path.exists(os.path.join(state, "safe_mode"))


def main() -> int:
    with open(SETUP, encoding="utf-8") as fh:
        guard = extract_guard(fh.read())
    base = tempfile.mkdtemp()
    home = os.path.join(base, "home"); os.makedirs(home)
    bindir = os.path.join(base, "bin"); os.makedirs(bindir)
    stub = os.path.join(bindir, "coredumpctl")
    with open(stub, "w") as fh:
        fh.write(STUB)
    os.chmod(stub, os.stat(stub).st_mode | stat.S_IEXEC)
    dumps = os.path.join(base, "dumps"); os.makedirs(dumps)   # exists, empty: the choked-crashhandler case
    client = "/home/deck/.local/share/Steam/ubuntu12_32/steam"
    launch = int(time.time()) - 600
    failures = 0

    def check(label, cond):
        nonlocal failures
        print(("ok   " if cond else "FAIL ") + label)
        if not cond:
            failures += 1

    out, log, count, safe = run(guard, home, bindir, launch, 0, f"{launch + 20} {client}", dumps)
    check("client core inside the window -> fail 1/3 (dumps dir empty)",
          "crashed at startup -> fail 1/3" in log and count == "1" and not safe)

    out, log, count, safe = run(guard, home, bindir, launch, 1, f"{launch + 400} {client}", dumps)
    check("client core after the window -> clean boot, counter reset",
          "previous boot ok -> reset fail count" in log and count == "0")

    helper = "/home/deck/.local/share/Steam/ubuntu12_64/steamwebhelper"
    out, log, count, safe = run(guard, home, bindir, launch, 1, f"{launch + 20} {helper}", dumps)
    check("steamwebhelper core is not a client crash", "reset fail count" in log and count == "0")

    out, log, count, safe = run(guard, home, bindir, launch, 0,
                                f"{launch + 20} /home/deck/.local/share/Steam/steamrt64/steam", dumps)
    check("64-bit client core counts too", count == "1")

    out, log, count, safe = run(guard, home, bindir, launch, 2, f"{launch + 20} {client}", dumps)
    check("third startup crash latches safe mode and execs vanilla",
          safe and "latching safe mode" in log and "VANILLA" in out)

    # No coredumpctl on PATH and no dumps dir: blind, counters untouched.
    env_bin = os.path.join(base, "emptybin"); os.makedirs(env_bin, exist_ok=True)
    out, log, count, safe = run(guard, home, env_bin, launch, 1, "", None)
    check("no coredumpctl and no dumps dir -> cannot assess, counter kept",
          "cannot assess previous boot" in log and count == "1")

    out, log, count, safe = run(guard, home, bindir, launch, 1, f"{launch + 20} {client}", dumps)
    os.environ["LUMA_GUARD_NO_COREDUMP"] = "1"
    out2, log2, count2, _ = run(guard, home, bindir, launch, 1, f"{launch + 20} {client}", dumps)
    del os.environ["LUMA_GUARD_NO_COREDUMP"]
    check("LUMA_GUARD_NO_COREDUMP=1 ignores coredumpctl (empty dumps dir reads clean)",
          count == "2" and count2 == "0")

    shutil.rmtree(base, ignore_errors=True)
    print("\nall ok" if not failures else f"\n{failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
