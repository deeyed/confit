#!/bin/sh
set -eu

CONFIT_BIN=$1
SOURCE_DIR=$2
WORK_DIR=$3

PROJECT_SRC="$SOURCE_DIR/tests/fixtures/tui-v2/profile-editor"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"

TERM=xterm
export TERM
ESC_KEY=$(printf '\033')

printf '%s' "$ESC_KEY" | env TERM=xterm COLUMNS=160 LINES=24 \
  "$CONFIT_BIN" tui --project "$PROJECT_SRC" --profile edit \
  >"$WORK_DIR/tui-v2-smoke.txt"

grep -aF "Confit TUI - schema v2 profile" "$WORK_DIR/tui-v2-smoke.txt" >/dev/null
grep -aF "schema=v2 project=v2-tui-editor profile=edit" \
  "$WORK_DIR/tui-v2-smoke.txt" >/dev/null
grep -aF "[+]  Core" "$WORK_DIR/tui-v2-smoke.txt" >/dev/null
grep -aF "[+]  Tuning" "$WORK_DIR/tui-v2-smoke.txt" >/dev/null
grep -aF "schema v2 profile transaction ready" "$WORK_DIR/tui-v2-smoke.txt" >/dev/null

printf '\njj?%s%s%s' "$ESC_KEY" "$ESC_KEY" "$ESC_KEY" | \
  env TERM=xterm COLUMNS=180 LINES=24 \
  "$CONFIT_BIN" tui --project "$PROJECT_SRC" --profile edit \
  >"$WORK_DIR/tui-v2-detail.txt"

grep -aF "Confit V2 Detail" "$WORK_DIR/tui-v2-detail.txt" >/dev/null
grep -aF "current effective:" "$WORK_DIR/tui-v2-detail.txt" >/dev/null
grep -aF "requested source:" "$WORK_DIR/tui-v2-detail.txt" >/dev/null
grep -aF "effective source:" "$WORK_DIR/tui-v2-detail.txt" >/dev/null
grep -aF "Derived Feature State" "$WORK_DIR/tui-v2-detail.txt" >/dev/null

printf ':flat\n:tree\n:filter trace\n:clear\n%s' "$ESC_KEY" | \
  env TERM=xterm COLUMNS=180 LINES=24 \
  "$CONFIT_BIN" tui --project "$PROJECT_SRC" --profile edit \
  >"$WORK_DIR/tui-v2-command.txt"

grep -aF "flat view" "$WORK_DIR/tui-v2-command.txt" >/dev/null
grep -aF "filter: trace" "$WORK_DIR/tui-v2-command.txt" >/dev/null
grep -aF "search and filter cleared" "$WORK_DIR/tui-v2-command.txt" >/dev/null

EDIT_DIR="$WORK_DIR/profile-edit"
cp -R "$PROJECT_SRC" "$EDIT_DIR"
printf '\nejs%s%s' "$ESC_KEY" "$ESC_KEY" | \
  env TERM=xterm COLUMNS=180 LINES=24 \
  "$CONFIT_BIN" tui --project "$EDIT_DIR" --profile edit \
  >"$WORK_DIR/tui-v2-edit.txt"

grep -aF "entered menu v2tui.core" "$WORK_DIR/tui-v2-edit.txt" >/dev/null
grep -aF "preview accepted: v2tui.enabled = true" \
  "$WORK_DIR/tui-v2-edit.txt" >/dev/null
grep -aF "saved schema v2 profile atomically" "$WORK_DIR/tui-v2-edit.txt" >/dev/null
grep -F '"v2tui.enabled" = true' "$EDIT_DIR/config/profiles/edit.toml" >/dev/null
"$CONFIT_BIN" check --project "$EDIT_DIR" --profile edit \
  >"$WORK_DIR/tui-v2-edit-check.txt"
grep -Fx "check ok" "$WORK_DIR/tui-v2-edit-check.txt" >/dev/null

TYPED_DIR="$WORK_DIR/profile-typed"
cp -R "$PROJECT_SRC" "$TYPED_DIR"
printf 'j\nje99\n12\n%s%s\n' "$ESC_KEY" "$ESC_KEY" | \
  env TERM=xterm COLUMNS=180 LINES=24 \
  "$CONFIT_BIN" tui --project "$TYPED_DIR" --profile edit \
  >"$WORK_DIR/tui-v2-typed.txt"

grep -aF "invalid schema v2 input value" "$WORK_DIR/tui-v2-typed.txt" >/dev/null
grep -aF "preview accepted: v2tui.capacity = 12" \
  "$WORK_DIR/tui-v2-typed.txt" >/dev/null
grep -aF '"v2tui.capacity" = 12' "$TYPED_DIR/config/profiles/edit.toml" >/dev/null
"$CONFIT_BIN" check --project "$TYPED_DIR" --profile edit \
  >"$WORK_DIR/tui-v2-typed-check.txt"
grep -Fx "check ok" "$WORK_DIR/tui-v2-typed-check.txt" >/dev/null

printf '%s' "$ESC_KEY" | env TERM=xterm COLUMNS=120 LINES=24 \
  "$CONFIT_BIN" tui --project "$PROJECT_SRC" --schema-edit \
  >"$WORK_DIR/tui-v2-schema.txt"

grep -aF "SCHEMA EDIT MODE - guarded" "$WORK_DIR/tui-v2-schema.txt" >/dev/null
grep -aF "V2 profile editing is transactional" "$WORK_DIR/tui-v2-schema.txt" >/dev/null
grep -aF "not persisted by this frontend" "$WORK_DIR/tui-v2-schema.txt" >/dev/null
