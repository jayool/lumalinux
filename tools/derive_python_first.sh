#!/usr/bin/env bash
# derive_python_first.sh — the ONE derivation chain every workflow leg runs:
#   Python locators first (seconds, deterministic), Ghidra only for what they
#   could not derive (second opinion), everything merged into ONE derived.json
#   for tools/apply_derived_pattern.py.
#
# Usage: derive_python_first.sh <steamclient.so> <const,const,…> <out-derived.json>
# Env:   GH_TOKEN (only needed if Ghidra has to run — run_ghidra_derive.sh
#        downloads the pinned release with gh).
#
# Per constant:
#   kDepotKeyFnPattern              tools/derive_depotkey_byname.py    (RTTI name -> slot)
#   kNotifyLicensesUpdatedPattern   tools/derive_reconcile_byanchor.py (callback 125 anchor)
#   kGmrcFunctionPattern, kBuildDepotDependencyPattern, kShaderCacheDepotPattern
#                                   tools/derive_bytext.py             (string -> xref -> fn)
#   anything else                   no Python locator -> straight to Ghidra
# A Python deriver that fails (anchor not unique, decoder refusal, …) hands
# ONLY that constant to Ghidra; a Python result is never overwritten by
# Ghidra's. Ghidra writes to its own file (run_ghidra_derive.sh deletes its
# output first) and the merge below copies in the UNIQUE entries for the
# constants still missing.
#
# Exit: 0 every requested constant is in <out> with matches == 1
#       3 at least one is not (the caller falls back to its issue path)
#       1 usage
set -uo pipefail

SO="${1:?usage: derive_python_first.sh <steamclient.so> <consts> <out.json>}"
ONLY="${2:?usage: derive_python_first.sh <steamclient.so> <consts> <out.json>}"
OUT="${3:?usage: derive_python_first.sh <steamclient.so> <consts> <out.json>}"
HERE="$(cd "$(dirname "$0")" && pwd)"

rm -f "$OUT"
FAILED=()
for c in $(echo "$ONLY" | tr ',' ' '); do
  echo "── $c: Python locator ──"
  case "$c" in
    kDepotKeyFnPattern)
      python3 "$HERE/derive_depotkey_byname.py" "$SO" --derived "$OUT" || FAILED+=("$c") ;;
    kNotifyLicensesUpdatedPattern)
      python3 "$HERE/derive_reconcile_byanchor.py" "$SO" --derived "$OUT" || FAILED+=("$c") ;;
    kGmrcFunctionPattern|kBuildDepotDependencyPattern|kShaderCacheDepotPattern)
      python3 "$HERE/derive_bytext.py" "$SO" --only "$c" --derived "$OUT" || FAILED+=("$c") ;;
    *)
      echo "  no Python locator for $c"; FAILED+=("$c") ;;
  esac
done

if [ "${#FAILED[@]}" -gt 0 ]; then
  echo "── Ghidra (second opinion) for: ${FAILED[*]} ──"
  GOUT="${OUT%.json}.ghidra.json"
  if bash "$HERE/run_ghidra_derive.sh" "$SO" "$GOUT"; then
    python3 - "$OUT" "$GOUT" "${FAILED[@]}" <<'PY'
import json, os, sys
out, gout, missing = sys.argv[1], sys.argv[2], sys.argv[3:]
data = json.load(open(out)) if os.path.exists(out) else {}
ghidra = json.load(open(gout)) if os.path.exists(gout) else {}
for c in missing:
    e = ghidra.get(c)
    if e and int(e.get("matches", 0)) == 1 and c not in data:
        e = dict(e); e["source"] = "ghidra"
        data[c] = e
        print("  merged from Ghidra: %s @ %s" % (c, e.get("rva")))
    else:
        print("  Ghidra did not derive %s either" % c)
json.dump(data, open(out, "w"), indent=2, sort_keys=True)
PY
  else
    echo "  Ghidra derivation failed"
  fi
fi

echo "── $OUT ──"
[ -f "$OUT" ] && cat "$OUT" || echo "{}"
python3 - "$OUT" "$ONLY" <<'PY'
import json, os, sys
out, only = sys.argv[1], [c for c in sys.argv[2].split(",") if c]
data = json.load(open(out)) if os.path.exists(out) else {}
bad = [c for c in only if int(data.get(c, {}).get("matches", 0)) != 1]
if bad:
    print("NOT derived: %s" % ", ".join(bad)); sys.exit(3)
print("derived: %s" % ", ".join("%s(%s)" % (c, data[c].get("source", "?")) for c in only))
PY
