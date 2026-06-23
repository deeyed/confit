#!/bin/sh
set -eu

CONFIT_BIN=$1
SOURCE_DIR=$2
WORK_DIR=$3

PROJECT_DIR="$SOURCE_DIR/tests/fixtures/schema/valid/basic"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"

TERM=xterm
export TERM

printf 'jq' | "$CONFIT_BIN" tui --project "$PROJECT_DIR" --profile sim-dsh \
  >"$WORK_DIR/tui.txt"

grep -aF "option 1/" "$WORK_DIR/tui.txt" >/dev/null
grep -aF "Confit TUI - menuconfig profile" "$WORK_DIR/tui.txt" >/dev/null
grep -aF "project=" "$WORK_DIR/tui.txt" >/dev/null
grep -aF "Menu" "$WORK_DIR/tui.txt" >/dev/null
grep -aF "/ search c category t tag x clear e edit s save q quit" \
  "$WORK_DIR/tui.txt" >/dev/null

printf 'q' | env TERM=xterm LINES=8 COLUMNS=35 \
  "$CONFIT_BIN" tui --project "$PROJECT_DIR" --profile sim-dsh \
  >"$WORK_DIR/tui-small.txt"

grep -aF "Confit TUI - menuconfig profile" "$WORK_DIR/tui-small.txt" \
  >/dev/null
grep -aF "option 1/" "$WORK_DIR/tui-small.txt" >/dev/null
