#!/bin/sh
set -eu

CONFIT_BIN=$1
SOURCE_DIR=$2
WORK_DIR=$3

PROJECT_DIR="$SOURCE_DIR/tests/fixtures/realish/delos"
GOLDEN_DIR="$SOURCE_DIR/tests/golden/realish/delos/sim-dsh"
OUT_DIR="$WORK_DIR/generated"
QSTAR_ONLY_DIR="$WORK_DIR/qstar-only"
BUILD_SELECTION_ONLY_DIR="$WORK_DIR/build-selection-only"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"

"$CONFIT_BIN" check --project "$PROJECT_DIR" --profile sim-dsh \
  >"$WORK_DIR/check.txt"
grep -Fx "check ok" "$WORK_DIR/check.txt" >/dev/null

"$CONFIT_BIN" check --project "$PROJECT_DIR" --profile sim-dsh --strict \
  >"$WORK_DIR/check-strict.txt"
grep -Fx "check ok" "$WORK_DIR/check-strict.txt" >/dev/null

"$CONFIT_BIN" explain --project "$PROJECT_DIR" --profile sim-dsh \
  delos.debug.dsh >"$WORK_DIR/explain-dsh.txt"
grep -F "option: delos.debug.dsh" "$WORK_DIR/explain-dsh.txt" >/dev/null
grep -F "delos.debug.ddc = true (active)" "$WORK_DIR/explain-dsh.txt" \
  >/dev/null
grep -F "delos.profile.release = false (inactive)" \
  "$WORK_DIR/explain-dsh.txt" >/dev/null

"$CONFIT_BIN" gen --project "$PROJECT_DIR" --profile sim-dsh \
  --out "$OUT_DIR" --artifact all >"$WORK_DIR/gen.txt"
grep -F "gen ok:" "$WORK_DIR/gen.txt" >/dev/null

for artifact in \
  config.h \
  config.cmake \
  config/config.qsm \
  config.qst \
  delos_build_selection/delos_build_selection.qsm \
  config.report.json \
  config.explain.txt \
  config.graph.json \
  config.inputs.json
do
  cmp "$GOLDEN_DIR/$artifact" "$OUT_DIR/$artifact"
done

grep -F "DELOS_CONFIG_DEBUG_DSH 1" "$OUT_DIR/config.h" >/dev/null
grep -F "DELOS_CONFIG_SIM_HOSTED_STDIO 1" "$OUT_DIR/config.h" >/dev/null
grep -F "confit-config-manifest-v1" "$OUT_DIR/config/config.qsm" >/dev/null
grep -F "delos-build-selection-v1" \
  "$OUT_DIR/delos_build_selection/delos_build_selection.qsm" >/dev/null
grep -F "//src/board/host/sim:board_objects" \
  "$OUT_DIR/delos_build_selection/delos_build_selection.qsm" >/dev/null
grep -F '"delos.target.kind", "type": "enum", "value": "sim:dsh"' \
  "$OUT_DIR/config.report.json" >/dev/null

"$CONFIT_BIN" gen --project "$PROJECT_DIR" --profile sim-dsh \
  --out "$QSTAR_ONLY_DIR" --artifact qstar >"$WORK_DIR/gen-qstar.txt"
test -s "$QSTAR_ONLY_DIR/config/config.qsm"
test -s "$QSTAR_ONLY_DIR/config.qst"
test ! -e "$QSTAR_ONLY_DIR/delos_build_selection/delos_build_selection.qsm"

"$CONFIT_BIN" gen --project "$PROJECT_DIR" --profile sim-dsh \
  --out "$BUILD_SELECTION_ONLY_DIR" --artifact build-selection \
  >"$WORK_DIR/gen-build-selection.txt"
test -s "$BUILD_SELECTION_ONLY_DIR/delos_build_selection/delos_build_selection.qsm"
test ! -e "$BUILD_SELECTION_ONLY_DIR/config/config.qsm"
test ! -e "$BUILD_SELECTION_ONLY_DIR/config.qst"
grep -F "delos-build-selection-v1" \
  "$BUILD_SELECTION_ONLY_DIR/delos_build_selection/delos_build_selection.qsm" \
  >/dev/null
