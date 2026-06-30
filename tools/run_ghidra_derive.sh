#!/usr/bin/env bash
# run_ghidra_derive.sh — CI helper: install Ghidra (once), run the headless
# derivation postScript against a steamclient.so, and leave a machine-readable
# derived.json for tools/apply_derived_pattern.py.
#
# Usage: run_ghidra_derive.sh <steamclient.so> <out-derived.json>
# Env:   GH_TOKEN must be set (gh resolves + downloads the Ghidra release asset).
#
# Only invoked by watch-steam.yml when check_patterns.py reports a MOVED pattern
# (exit 2/3) — never on the daily happy path — so the multi-minute Ghidra
# analysis cost is paid only when there is real re-derivation work to do. Java
# (JDK 21) is expected to be on PATH already (the workflow sets it up).
set -euo pipefail

SO="${1:?usage: run_ghidra_derive.sh <steamclient.so> <out.json>}"
OUT="${2:?usage: run_ghidra_derive.sh <steamclient.so> <out.json>}"
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="${RUNNER_TEMP:-/tmp}/ghidra-derive"
mkdir -p "$WORK"

# ── 1) install (and cache within the job) a PINNED Ghidra PUBLIC release ──────
# PIN to 11.2.1: it's the last release that bundles Jython, which derive_patterns.py
# needs. Ghidra 11.3+ dropped Jython for PyGhidra (CPython), and a plain
# analyzeHeadless there refuses .py scripts with "Ghidra was not started with
# PyGhidra. Python is not available". Override with GHIDRA_TAG if ever needed.
GHIDRA_TAG="${GHIDRA_TAG:-Ghidra_11.2.1_build}"
GHIDRA_DIR="$WORK/ghidra"
if [ ! -x "$GHIDRA_DIR/support/analyzeHeadless" ]; then
  echo "Downloading Ghidra $GHIDRA_TAG (PUBLIC)..."
  rm -rf "$WORK/dl"; mkdir -p "$WORK/dl"
  gh release download "$GHIDRA_TAG" --repo NationalSecurityAgency/ghidra \
     --pattern '*_PUBLIC_*.zip' --dir "$WORK/dl" --clobber
  ZIP="$(ls "$WORK"/dl/*_PUBLIC_*.zip | head -1)"
  if [ -z "${ZIP:-}" ]; then echo "no Ghidra zip downloaded"; exit 1; fi
  echo "Extracting $(basename "$ZIP")..."
  rm -rf "$WORK/unz"; mkdir -p "$WORK/unz"
  unzip -q "$ZIP" -d "$WORK/unz"
  INNER="$(find "$WORK/unz" -maxdepth 1 -type d -name 'ghidra_*_PUBLIC' | head -1)"
  if [ -z "${INNER:-}" ]; then echo "unexpected Ghidra zip layout"; exit 1; fi
  rm -rf "$GHIDRA_DIR"; mv "$INNER" "$GHIDRA_DIR"
fi
echo "Ghidra: $GHIDRA_DIR"

# Bump the headless max heap: the default (2G) OOMs mid-analysis on the large
# steamclient.so. analyzeHeadless reads MAXMEM from this properties file; sed it
# up (no-op if the layout ever changes) and also export it as a belt-and-suspenders.
for pf in "$GHIDRA_DIR/support/analyzeHeadless" "$GHIDRA_DIR/support/launch.properties"; do
  [ -f "$pf" ] && sed -i 's/^MAXMEM=.*/MAXMEM=6G/' "$pf" || true
done
export MAXMEM=6G

# ── 2) headless import + analyze + derive (fresh project each run) ─────────────
PROJ="$WORK/proj"
rm -rf "$PROJ"; mkdir -p "$PROJ"
rm -f "$OUT"
echo "Running headless derivation against $SO (this can take several minutes)..."
"$GHIDRA_DIR/support/analyzeHeadless" "$PROJ" lumaderive \
    -import "$SO" \
    -scriptPath "$HERE" \
    -postScript derive_patterns.py --json "$OUT" \
    -deleteProject

if [ ! -f "$OUT" ]; then
  echo "ERROR: derive_patterns.py did not emit $OUT"; exit 1
fi
echo "── derived.json ──"
cat "$OUT"
echo
