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

printf '\nndelos.schema.mode\nenum\nInitial Prompt\npCreated Prompt\nhCreated help\ncschema\ntschema,created\nored,blue\ndblue\nndelos.schema.limit\nint\nLimit Prompt\npLimit Prompt\nyint\nhLimit help\ncschema\ntschema,limit\nr0,16\nsq' |
  "$CONFIT_BIN" tui --project "$PROJECT_DIR" --schema-edit \
    >"$WORK_DIR/tui-schema.txt"

grep -aF "Schema Edit Warning" "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "SCHEMA EDIT MODE is a guarded workflow." \
  "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "Generated outputs are not written here" \
  "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "SCHEMA EDIT MODE - guarded" "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "option 0/0" "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "Confit Schema Editor - menuconfig guarded schema" \
  "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "Confit Schema Field" "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "project=" "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "Options" "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "keys: move jk/arrows Pg/Home/End | n new | edit y/d/p/h/c/t/r/o" \
  "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "saved and validated" "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "reloaded graph" "$WORK_DIR/tui-schema.txt" >/dev/null

printf '\nn\033?q' |
  "$CONFIT_BIN" tui --project "$PROJECT_DIR" --schema-edit \
    >"$WORK_DIR/tui-schema-cancel-help.txt"

grep -aF "cancelled" "$WORK_DIR/tui-schema-cancel-help.txt" >/dev/null
grep -aF "keys: move jk/arrows Pg/Home/End" \
  "$WORK_DIR/tui-schema-cancel-help.txt" >/dev/null
grep -aF "edit y/d/p/h/c/t/r/o" "$WORK_DIR/tui-schema-cancel-help.txt" \
  >/dev/null

printf '\nninvalid\n\033q' |
  "$CONFIT_BIN" tui --project "$PROJECT_DIR" --schema-edit \
    >"$WORK_DIR/tui-schema-invalid-id.txt"

grep -aF "option id:" "$WORK_DIR/tui-schema-invalid-id.txt" >/dev/null
grep -aF "invalid" "$WORK_DIR/tui-schema-invalid-id.txt" >/dev/null

printf '\nndelos.schema.bad\nint\nBad\nr10,1\n\033q' |
  "$CONFIT_BIN" tui --project "$PROJECT_DIR" --schema-edit \
    >"$WORK_DIR/tui-schema-invalid-range.txt"

grep -aF "schema range does not contain the default" \
  "$WORK_DIR/tui-schema-invalid-range.txt" >/dev/null

diff -u "$GOLDEN" "$PROJECT_DIR/config/options/tui-schema.toml"
"$CONFIT_BIN" graph --project "$PROJECT_DIR" >"$WORK_DIR/graph.json"
grep -F '"id": "delos.schema.mode"' "$WORK_DIR/graph.json" >/dev/null
grep -F '"id": "delos.schema.limit"' "$WORK_DIR/graph.json" >/dev/null
