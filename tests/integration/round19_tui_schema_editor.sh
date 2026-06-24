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
ESC_KEY=$(printf '\033')

printf '\nndelos.schema.mode\nenum\nInitial Prompt\npCreated Prompt\nhCreated help\ncschema\ntschema,created\nored,blue\ndblue\nndelos.schema.limit\nint\nLimit Prompt\npLimit Prompt\nyint\nhLimit help\ncschema\ntschema,limit\nr0,16\ns%s' "$ESC_KEY" |
  "$CONFIT_BIN" tui --project "$PROJECT_DIR" --schema-edit \
    >"$WORK_DIR/tui-schema.txt"

grep -aF "Schema Edit Warning" "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "SCHEMA EDIT MODE is a guarded workflow." \
  "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "Generated outputs are not written here" \
  "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "SCHEMA EDIT MODE - guarded" "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "breadcrumb=Main Menu | row 0/0 | view=tree" \
  "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "Confit TUI - menuconfig schema" \
  "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "Confit Schema Field" "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "project=" "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "Options" "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "schema edits change all profiles" \
  "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "keys: move jk/arrows Pg/Home/End | enter/d default | y type | n new" \
  "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "breadcrumb=Main Menu > schema" "$WORK_DIR/tui-schema.txt" \
  >/dev/null
grep -aF "Created Prompt <delos.schema.mode>" "$WORK_DIR/tui-schema.txt" \
  >/dev/null
grep -aF "Limit Prompt <delos.schema.limit>" "$WORK_DIR/tui-schema.txt" \
  >/dev/null
grep -aF "type=enum" "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "choices=red,blue" "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "category path:schema" "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "policy: dotted option id; letters, numbers, _, - allowed" \
  "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "policy: one of bool,int,uint,hex,string,enum,float,path" \
  "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "policy: enum comma-list; default must remain a choice" \
  "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "policy: numeric min,max containing the current default" \
  "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "required: numeric min,max containing the current default" \
  "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "saved and validated" "$WORK_DIR/tui-schema.txt" >/dev/null
grep -aF "reloaded graph" "$WORK_DIR/tui-schema.txt" >/dev/null

NAV_PROJECT_DIR="$WORK_DIR/schema-navigation"
cp -R "$PROJECT_SRC" "$NAV_PROJECT_DIR"
printf '\nndelos.schema.nav\nbool\nNav Prompt\ncschema/tree\n%s%s\n\n:help\n:verbose\n:noverbose\n:flat\n:tree\ns%s' \
  "$ESC_KEY" "$ESC_KEY" "$ESC_KEY" |
  "$CONFIT_BIN" tui --project "$NAV_PROJECT_DIR" --schema-edit \
    >"$WORK_DIR/tui-schema-navigation.txt"

grep -aF "commands: verbose noverbose tree flat filter <text> clear help quit" \
  "$WORK_DIR/tui-schema-navigation.txt" >/dev/null
grep -aF "flat view" "$WORK_DIR/tui-schema-navigation.txt" >/dev/null
grep -aF "view=tree" "$WORK_DIR/tui-schema-navigation.txt" >/dev/null
grep -aF "back to Main Menu > schema" "$WORK_DIR/tui-schema-navigation.txt" \
  >/dev/null
grep -aF "back to Main Menu" "$WORK_DIR/tui-schema-navigation.txt" \
  >/dev/null
grep -aF "entered menu Main Menu > schema" \
  "$WORK_DIR/tui-schema-navigation.txt" >/dev/null
grep -aF "breadcrumb=Main Menu > schema > tree" \
  "$WORK_DIR/tui-schema-navigation.txt" >/dev/null
grep -aF "schema menu path: schema/tree" \
  "$WORK_DIR/tui-schema-navigation.txt" >/dev/null
grep -aF "Nav Prompt <delos.schema.nav> bool category path:schema/tree" \
  "$WORK_DIR/tui-schema-navigation.txt" \
  >/dev/null
grep -aF "verbose inspector mode" "$WORK_DIR/tui-schema-navigation.txt" \
  >/dev/null
grep -aF "compact" "$WORK_DIR/tui-schema-navigation.txt" >/dev/null
grep -aF "saved and validated" "$WORK_DIR/tui-schema-navigation.txt" \
  >/dev/null

COMMAND_PROJECT_DIR="$WORK_DIR/schema-command-mode"
cp -R "$PROJECT_SRC" "$COMMAND_PROJECT_DIR"
printf '\n:\n:%s:filter\n:bogus\n%s' "$ESC_KEY" "$ESC_KEY" |
  "$CONFIT_BIN" tui --project "$COMMAND_PROJECT_DIR" --schema-edit \
    >"$WORK_DIR/tui-schema-command-mode.txt"

grep -aF "empty command" "$WORK_DIR/tui-schema-command-mode.txt" >/dev/null
grep -aF "command cancelled" "$WORK_DIR/tui-schema-command-mode.txt" \
  >/dev/null
grep -aF "usage: :filter <text>" "$WORK_DIR/tui-schema-command-mode.txt" \
  >/dev/null
grep -aF "known command: bogus" "$WORK_DIR/tui-schema-command-mode.txt" \
  >/dev/null

DEPTH_PROJECT_DIR="$WORK_DIR/schema-depth"
cp -R "$PROJECT_SRC" "$DEPTH_PROJECT_DIR"
printf '\nndelos.schema.deep\nbool\nDeep Prompt\ncschema/deep/extra/too\ns%s' \
  "$ESC_KEY" |
  "$CONFIT_BIN" tui --project "$DEPTH_PROJECT_DIR" --schema-edit \
    >"$WORK_DIR/tui-schema-depth-warning.txt"

grep -aF "warning: category path depth exceeds 3 levels" \
  "$WORK_DIR/tui-schema-depth-warning.txt" >/dev/null
grep -aF "category path:schema/deep/extra/too" \
  "$WORK_DIR/tui-schema-depth-warning.txt" >/dev/null
grep -aF "saved and validated" "$WORK_DIR/tui-schema-depth-warning.txt" \
  >/dev/null
grep -F 'category = "schema/deep/extra/too"' \
  "$DEPTH_PROJECT_DIR/config/options/tui-schema.toml" >/dev/null
"$CONFIT_BIN" graph --project "$DEPTH_PROJECT_DIR" \
  >"$WORK_DIR/depth-graph.json"
grep -F '"id": "delos.schema.deep"' "$WORK_DIR/depth-graph.json" \
  >/dev/null

printf '\nn%s?%s' "$ESC_KEY" "$ESC_KEY" |
  "$CONFIT_BIN" tui --project "$PROJECT_DIR" --schema-edit \
    >"$WORK_DIR/tui-schema-cancel-help.txt"

grep -aF "schema field cancelled" "$WORK_DIR/tui-schema-cancel-help.txt" \
  >/dev/null
grep -aF "keys: move jk/arrows Pg/Home/End" \
  "$WORK_DIR/tui-schema-cancel-help.txt" >/dev/null
grep -aF "edit y/d/p/h/c/t/r/o" "$WORK_DIR/tui-schema-cancel-help.txt" \
  >/dev/null

printf '\nninvalid\n%s%s' "$ESC_KEY" "$ESC_KEY" |
  "$CONFIT_BIN" tui --project "$PROJECT_DIR" --schema-edit \
    >"$WORK_DIR/tui-schema-invalid-id.txt"

grep -aF "option id:" "$WORK_DIR/tui-schema-invalid-id.txt" >/dev/null
grep -aF "invalid schema option id" "$WORK_DIR/tui-schema-invalid-id.txt" \
  >/dev/null
grep -aF "required: dotted option id; letters, numbers, _, - allowed" \
  "$WORK_DIR/tui-schema-invalid-id.txt" >/dev/null

printf '\nndelos.schema.bad\nint\nBad\nr10,1\n%s%sj\n' "$ESC_KEY" "$ESC_KEY" |
  "$CONFIT_BIN" tui --project "$PROJECT_DIR" --schema-edit \
    >"$WORK_DIR/tui-schema-invalid-range.txt"

grep -aF "required: numeric min,max containing the current default" \
  "$WORK_DIR/tui-schema-invalid-range.txt" >/dev/null
grep -aF "schema range does not contain the default" \
  "$WORK_DIR/tui-schema-invalid-range.txt" >/dev/null

diff -u "$GOLDEN" "$PROJECT_DIR/config/options/tui-schema.toml"
"$CONFIT_BIN" graph --project "$PROJECT_DIR" >"$WORK_DIR/graph.json"
grep -F '"id": "delos.schema.mode"' "$WORK_DIR/graph.json" >/dev/null
grep -F '"id": "delos.schema.limit"' "$WORK_DIR/graph.json" >/dev/null
