#!/bin/sh
set -eu

CONFIT_BIN=$1
SOURCE_DIR=$2
WORK_DIR=$3

PROJECT_SRC="$SOURCE_DIR/tests/fixtures/tui/profile-editor"
PROJECT_DIR="$WORK_DIR/profile-create"
GOLDEN="$SOURCE_DIR/tests/golden/tui/profile-create.toml"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"
cp -R "$PROJECT_SRC" "$PROJECT_DIR"
rm -f "$PROJECT_DIR/config/profiles/fresh.toml"

TERM=xterm
export TERM

printf 'sq' |
  "$CONFIT_BIN" tui --project "$PROJECT_DIR" --profile fresh \
    >"$WORK_DIR/tui-profile-create.txt"

grep -aF "breadcrumb=Main Menu | row 1/1" "$WORK_DIR/tui-profile-create.txt" \
  >/dev/null
grep -aF "Confit TUI - menuconfig profile" "$WORK_DIR/tui-profile-create.txt" \
  >/dev/null
grep -aF "project=" "$WORK_DIR/tui-profile-create.txt" >/dev/null
grep -aF "Options" "$WORK_DIR/tui-profile-create.txt" >/dev/null
grep -aF "[+]  edit" "$WORK_DIR/tui-profile-create.txt" >/dev/null
grep -aF "mode=profile project=delos profile=fresh" \
  "$WORK_DIR/tui-profile-create.txt" >/dev/null
grep -aF "keys: move jk/arrows Pg/Home/End | enter menu" \
  "$WORK_DIR/tui-profile-create.txt" >/dev/null
grep -aF "s save" "$WORK_DIR/tui-profile-create.txt" >/dev/null
grep -aF "created new profile fresh" "$WORK_DIR/tui-profile-create.txt" \
  >/dev/null
grep -aF "saved and reloaded" "$WORK_DIR/tui-profile-create.txt" >/dev/null

diff -u "$GOLDEN" "$PROJECT_DIR/config/profiles/fresh.toml"
"$CONFIT_BIN" check --project "$PROJECT_DIR" --profile fresh \
  >"$WORK_DIR/check.txt"
grep -Fx "check ok" "$WORK_DIR/check.txt" >/dev/null
