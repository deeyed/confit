#!/bin/sh
set -eu

CONFIT_BIN=$1
SOURCE_DIR=$2
WORK_DIR=$3

PROJECT_SRC="$SOURCE_DIR/tests/fixtures/tui/profile-editor"
PROJECT_DIR="$WORK_DIR/profile-editor"
GOLDEN="$SOURCE_DIR/tests/golden/tui/profile-editor-edit.toml"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"
cp -R "$PROJECT_SRC" "$PROJECT_DIR"

TERM=xterm
export TERM

printf '/bool\nex/mode\ne\nx/count\ne7\nx/gain\ne0.75\nx/name\neTUI name\nx/path\nebuild/new\nsq' |
  "$CONFIT_BIN" tui --project "$PROJECT_DIR" --profile edit \
    >"$WORK_DIR/tui-edit.txt"

grep -aF "row 1/7" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "Confit TUI - menuconfig profile" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "project=" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "Menu" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "[-]  edit" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "tags:" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "delos.edit.mode" "$WORK_DIR/tui-edit.txt" >/dev/null

diff -u "$GOLDEN" "$PROJECT_DIR/config/profiles/edit.toml"
"$CONFIT_BIN" check --project "$PROJECT_DIR" --profile edit \
  >"$WORK_DIR/check.txt"
grep -Fx "check ok" "$WORK_DIR/check.txt" >/dev/null
