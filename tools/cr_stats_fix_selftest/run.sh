#!/usr/bin/env bash
# Self-test for src/cr_stats_fix.cpp: builds a fake cloud_redirect.so with the
# real exported signature, a test lib carrying the interposer, and runs the
# four scenarios. Needs gcc-multilib (same as the release build).
set -euo pipefail
cd "$(dirname "$0")"
SRC=../../src
CXX="${CXX:-g++} -m32 -O2 -fPIC -D_GLIBCXX_USE_CXX11_ABI=0"
$CXX -shared -o cloud_redirect.so fake_cr.cpp fake_cr_try.cpp -s
$CXX -shared -fvisibility=hidden -I"$SRC" -o libcrfixtest.so "$SRC/cr_stats_fix.cpp" test_glue.cpp -ldl
$CXX -o test_main test_main.cpp -L. -l:cloud_redirect.so -Wl,-rpath,'$ORIGIN' -ldl
# (grep -c, not -q: -q closes the pipe early and trips pipefail)
[ "$(nm -D cloud_redirect.so | grep -c " T _ZN13StatsHandlers18HandleGetUserStatsEjRKSt6vectorIN2PB5FieldESaIS2_EE")" = 1 ] || { echo "fake CR: symbol name mismatch"; exit 1; }
[ "$(objdump -d cloud_redirect.so | grep -c "call.*HandleGetUserStats.*@plt")" -ge 1 ] || { echo "fake CR: internal call not via PLT"; exit 1; }
fail=0
check() { local name="$1" want="$2" got="$3"; if [ "$got" = "$want" ]; then echo "PASS $name: $got"; else echo "FAIL $name: got '$got' want '$want'"; fail=1; fi; }
check "no interposer"        "init=-1 app1=2 app2=3 app3=6 app4=0"  "$(./test_main 2>/dev/null | head -1)"
check "armed"                "init=0 app1=0 app2=3 app3=6 app4=0"   "$(LD_PRELOAD=./libcrfixtest.so ./test_main 2>/dev/null | head -1)"
check "armed, second round"  "again app1=0 app2=3"                  "$(LD_PRELOAD=./libcrfixtest.so ./test_main 2>/dev/null | tail -1)"
check "kill switch"          "init=1 app1=2 app2=3 app3=6 app4=0"   "$(LUMA_NO_CR_STATS_FIX=1 LD_PRELOAD=./libcrfixtest.so ./test_main 2>/dev/null | head -1)"
check "CR first in preload"  "init=2 app1=2 app2=3 app3=6 app4=0"   "$(LD_PRELOAD=./cloud_redirect.so:./libcrfixtest.so ./test_main 2>/dev/null | head -1)"
logs=$(LD_PRELOAD=./libcrfixtest.so ./test_main 2>&1 >/dev/null | grep -c "CR-stats: app=1 " || true)
check "once-per-app log"     "1" "$logs"
echo "--- armed run log:"; LD_PRELOAD=./libcrfixtest.so ./test_main 2>&1 >/dev/null | sed 's/^/    /'
echo "--- CR-first run log:"; LD_PRELOAD=./cloud_redirect.so:./libcrfixtest.so ./test_main 2>&1 >/dev/null | sed 's/^/    /'
rm -f cloud_redirect.so libcrfixtest.so test_main
exit $fail
