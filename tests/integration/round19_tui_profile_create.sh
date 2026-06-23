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

printf 'sq' |
  "$CONFIT_BIN" tui --project "$PROJECT_DIR" --profile fresh \
    >"$WORK_DIR/tui-profile-create.txt"

grep -F "profile=fresh" "$WORK_DIR/tui-profile-create.txt" >/dev/null
grep -F "saved " "$WORK_DIR/tui-profile-create.txt" >/dev/null

diff -u "$GOLDEN" "$PROJECT_DIR/config/profiles/fresh.toml"
"$CONFIT_BIN" check --project "$PROJECT_DIR" --profile fresh \
  >"$WORK_DIR/check.txt"
grep -Fx "check ok" "$WORK_DIR/check.txt" >/dev/null
