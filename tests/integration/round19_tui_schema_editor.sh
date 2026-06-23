#!/bin/sh
set -eu

CONFIT_BIN=$1
SOURCE_DIR=$2
WORK_DIR=$3

PROJECT_SRC="$SOURCE_DIR/tests/fixtures/tui/schema-editor"
PROJECT_DIR="$WORK_DIR/schema-editor"
GOLDEN="$SOURCE_DIR/tests/golden/tui/schema-editor-tui-schema.toml"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"
cp -R "$PROJECT_SRC" "$PROJECT_DIR"

TERM=xterm
export TERM

printf 'ndelos.schema.mode\nenum\nInitial Prompt\npCreated Prompt\nhCreated help\ncschema\ntschema,created\nored,blue\nndelos.schema.limit\nint\nLimit Prompt\npLimit Prompt\nhLimit help\ncschema\ntschema,limit\nr0,16\nsq' |
  "$CONFIT_BIN" tui --project "$PROJECT_DIR" --schema-edit \
    >"$WORK_DIR/tui-schema.txt"

grep -aF "schema 0/0" "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "Confit Schema Editor - menuconfig guarded schema" \
  "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "project=" "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "Menu" "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "arrows/jk PgUp/PgDn Home/End n new p prompt h help ? keys" \
  "$WORK_DIR/tui-schema.txt" >/dev/null

printf 'n\033?q' |
  "$CONFIT_BIN" tui --project "$PROJECT_DIR" --schema-edit \
    >"$WORK_DIR/tui-schema-cancel-help.txt"

grep -aF "cancelled" "$WORK_DIR/tui-schema-cancel-help.txt" >/dev/null
grep -aF "keys: arrows/jk PgUp/PgDn Home/End" \
  "$WORK_DIR/tui-schema-cancel-help.txt" >/dev/null

diff -u "$GOLDEN" "$PROJECT_DIR/config/options/tui-schema.toml"
"$CONFIT_BIN" graph --project "$PROJECT_DIR" >"$WORK_DIR/graph.json"
grep -F '"id": "delos.schema.mode"' "$WORK_DIR/graph.json" >/dev/null
grep -F '"id": "delos.schema.limit"' "$WORK_DIR/graph.json" >/dev/null
