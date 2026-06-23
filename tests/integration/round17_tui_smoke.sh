#!/bin/sh
set -eu

CONFIT_BIN=$1
SOURCE_DIR=$2
WORK_DIR=$3

PROJECT_DIR="$SOURCE_DIR/tests/fixtures/schema/valid/basic"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"

printf 'jq' | "$CONFIT_BIN" tui --project "$PROJECT_DIR" --profile sim-dsh \
  >"$WORK_DIR/tui.txt"

grep -F "== Confit TUI ==" "$WORK_DIR/tui.txt" >/dev/null
grep -F "project=delos profile=sim-dsh target=host-sim" \
  "$WORK_DIR/tui.txt" >/dev/null
grep -F "delos.debug.ddc" "$WORK_DIR/tui.txt" >/dev/null
grep -F "[status] option" "$WORK_DIR/tui.txt" >/dev/null
grep -F "keys: j/down k/up q quit" "$WORK_DIR/tui.txt" >/dev/null
