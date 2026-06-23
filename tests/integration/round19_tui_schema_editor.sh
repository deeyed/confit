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

printf 'ndelos.schema.mode\nenum\nInitial Prompt\npCreated Prompt\nhCreated help\ncschema\ntschema,created\nored,blue\nndelos.schema.limit\nint\nLimit Prompt\npLimit Prompt\nhLimit help\ncschema\ntschema,limit\nr0,16\nsq' |
  "$CONFIT_BIN" tui --project "$PROJECT_DIR" --schema-edit \
    >"$WORK_DIR/tui-schema.txt"

grep -F "Schema edit mode changes project configuration semantics." \
  "$WORK_DIR/tui-schema.txt" >/dev/null
grep -F "Prefer code review for schema changes." \
  "$WORK_DIR/tui-schema.txt" >/dev/null
grep -F "schema saved " "$WORK_DIR/tui-schema.txt" >/dev/null

diff -u "$GOLDEN" "$PROJECT_DIR/config/options/tui-schema.toml"
"$CONFIT_BIN" graph --project "$PROJECT_DIR" >"$WORK_DIR/graph.json"
grep -F '"id": "delos.schema.mode"' "$WORK_DIR/graph.json" >/dev/null
grep -F '"id": "delos.schema.limit"' "$WORK_DIR/graph.json" >/dev/null
