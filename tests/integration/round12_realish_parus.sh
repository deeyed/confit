#!/bin/sh
set -eu

CONFIT_BIN=$1
SOURCE_DIR=$2
WORK_DIR=$3

PROJECT_DIR="$SOURCE_DIR/tests/fixtures/realish/parus"
GOLDEN_DIR="$SOURCE_DIR/tests/golden/realish/parus/qemu-aarch64"
OUT_DIR="$WORK_DIR/generated"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"

"$CONFIT_BIN" check --project "$PROJECT_DIR" --profile qemu-aarch64 \
  >"$WORK_DIR/check.txt"
grep -Fx "check ok" "$WORK_DIR/check.txt" >/dev/null

"$CONFIT_BIN" check --project "$PROJECT_DIR" --profile qemu-aarch64 --strict \
  >"$WORK_DIR/check-strict.txt"
grep -Fx "check ok" "$WORK_DIR/check-strict.txt" >/dev/null

"$CONFIT_BIN" explain --project "$PROJECT_DIR" --profile qemu-aarch64 \
  parus.boot.direct_dtb >"$WORK_DIR/explain-direct-dtb.txt"
grep -F "option: parus.boot.direct_dtb" \
  "$WORK_DIR/explain-direct-dtb.txt" >/dev/null
grep -F "conflicted by:" "$WORK_DIR/explain-direct-dtb.txt" >/dev/null
grep -F "parus.boot.ribon_lbpb = false (inactive)" \
  "$WORK_DIR/explain-direct-dtb.txt" >/dev/null

"$CONFIT_BIN" gen --project "$PROJECT_DIR" --profile qemu-aarch64 \
  --out "$OUT_DIR" --artifact all >"$WORK_DIR/gen.txt"
grep -F "gen ok:" "$WORK_DIR/gen.txt" >/dev/null

for artifact in \
  config.h \
  config.cmake \
  config.qst \
  config.report.json \
  config.explain.txt \
  config.graph.json \
  config.inputs.json
do
  cmp "$GOLDEN_DIR/$artifact" "$OUT_DIR/$artifact"
done

grep -F "PARUS_CONFIG_BOARD_QEMU_VIRT_AARCH64 1" "$OUT_DIR/config.h" \
  >/dev/null
grep -F "PARUS_CONFIG_BOOT_DIRECT_DTB 1" "$OUT_DIR/config.h" >/dev/null
grep -F '"parus.target.board", "type": "enum", "value": "qemu-virt-aarch64"' \
  "$OUT_DIR/config.report.json" >/dev/null
