#!/bin/sh
set -eu

CONFIT_BIN=$1
SOURCE_DIR=$2
WORK_DIR=$3

SOURCE_DIR=$(CDPATH= cd -- "$SOURCE_DIR" && pwd)
SCRIPT="$SOURCE_DIR/scripts/confit-cutover-dry-run.sh"
OUT_DIR="$WORK_DIR/cutover"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"

"$SCRIPT" --project delos-realish --out "$OUT_DIR" \
  --confit-bin "$CONFIT_BIN" >"$WORK_DIR/delos.txt"
grep -F "cutover dry-run: Delos realish sim-dsh" \
  "$WORK_DIR/delos.txt" >/dev/null
grep -F "artifact verify: ok" "$WORK_DIR/delos.txt" >/dev/null
grep -F "rollback:" "$WORK_DIR/delos.txt" >/dev/null
grep -F "project: delos-realish" \
  "$OUT_DIR/delos-realish/CUTOVER_SUMMARY.txt" >/dev/null
grep -F "config.h" "$OUT_DIR/delos-realish/CUTOVER_SUMMARY.txt" >/dev/null
grep -F "rm -rf \"$OUT_DIR/delos-realish\"" \
  "$OUT_DIR/delos-realish/ROLLBACK.md" >/dev/null
grep -F '"schema": "confit-inputs-v1"' \
  "$OUT_DIR/delos-realish/generated/config.inputs.json" >/dev/null
grep -F "config.h: ok" "$OUT_DIR/delos-realish/artifact-diff.txt" \
  >/dev/null
grep -F "config/config.qsm: ok" \
  "$OUT_DIR/delos-realish/artifact-diff.txt" >/dev/null
grep -F "delos_build_selection/delos_build_selection.qsm: ok" \
  "$OUT_DIR/delos-realish/artifact-diff.txt" >/dev/null

"$SCRIPT" --project parus-realish --out "$OUT_DIR" \
  --confit-bin "$CONFIT_BIN" >"$WORK_DIR/parus.txt"
grep -F "cutover dry-run: Parus realish qemu-aarch64" \
  "$WORK_DIR/parus.txt" >/dev/null
grep -F "artifact verify: ok" "$WORK_DIR/parus.txt" >/dev/null
grep -F "project: parus-realish" \
  "$OUT_DIR/parus-realish/CUTOVER_SUMMARY.txt" >/dev/null
grep -F "config.report.json: ok" \
  "$OUT_DIR/parus-realish/artifact-diff.txt" >/dev/null
grep -F "config/config.qsm: ok" \
  "$OUT_DIR/parus-realish/artifact-diff.txt" >/dev/null
grep -F '"status": "ok"' "$OUT_DIR/parus-realish/compat.json" >/dev/null

"$SCRIPT" --project both --out "$OUT_DIR" --confit-bin "$CONFIT_BIN" \
  >"$WORK_DIR/both.txt"
grep -F "cutover dry-run ok: $OUT_DIR" "$WORK_DIR/both.txt" >/dev/null
test -f "$OUT_DIR/delos-realish/generated/config.h"
test -f "$OUT_DIR/parus-realish/generated/config.h"

if "$SCRIPT" --project delos-realish --out "$SOURCE_DIR" \
  --confit-bin "$CONFIT_BIN" >"$WORK_DIR/unsafe.txt" 2>&1
then
  exit 1
else
  status=$?
fi
test "$status" -eq 1
grep -F "refusing unsafe output directory" "$WORK_DIR/unsafe.txt" >/dev/null
