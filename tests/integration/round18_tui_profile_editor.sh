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
PAGE_DOWN_KEY=$(tput knp 2>/dev/null || printf '\033[6~')

{
  printf '%s\n' '/bool'
  printf '%s' 'e'
  printf '%s\n' 'x/mode'
  printf '%s' 'e'
  printf '%s\n' 'j'
  printf '%s\n' 'x/count'
  printf '%s' 'e'
  printf '%s\n' '99'
  printf '%s\n' '7'
  printf '%s\n' 'x/threads'
  printf '%s' 'e'
  printf '%s\n' '99'
  printf '%s\n' '6'
  printf '%s\n' 'x/mask'
  printf '%s' 'e'
  printf '%s\n' '0x100'
  printf '%s\n' '0x2A'
  printf '%s\n' 'x/gain'
  printf '%s' 'e'
  printf '%s\n' 'nan'
  printf '%s\n' '0.75'
  printf '%s\n' 'x/name'
  printf '%s' 'e'
  printf '\n'
  printf '%s\n' 'TUI name'
  printf '%s' '?'
  printf '%s' "$PAGE_DOWN_KEY"
  printf '%s' "$PAGE_DOWN_KEY"
  printf '%s' "$PAGE_DOWN_KEY"
  printf '%s' 'q'
  printf '%s\n' 'x/path'
  printf '%s' 'e'
  printf '%s\n' '/tmp/bad'
  printf '%s\n' 'build/new'
  printf '%s' 's'
  printf '\n'
  printf '%s' 'q'
} >"$WORK_DIR/tui-edit.keys"

"$CONFIT_BIN" tui --project "$PROJECT_DIR" --profile edit \
  <"$WORK_DIR/tui-edit.keys" >"$WORK_DIR/tui-edit.txt"

grep -aF "row 1/9" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "Confit TUI - menuconfig profile" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "project=" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "Options" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "[-]  edit" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "tags:" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "delos.edit.mode" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "Confit Choice" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "Confit Value" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "invalid int: outside range" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "invalid uint: outside range" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "invalid hex: outside range" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "invalid float: expected finite value" "$WORK_DIR/tui-edit.txt" \
  >/dev/null
grep -aF "invalid string: value required" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "production TUI help panel" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "without losing" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "invalid path: expected relative path" "$WORK_DIR/tui-edit.txt" \
  >/dev/null
grep -aF "Overwrite Profile" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "saved and reloaded" "$WORK_DIR/tui-edit.txt" >/dev/null
grep -aF "full validation ok" "$WORK_DIR/tui-edit.txt" >/dev/null

diff -u "$GOLDEN" "$PROJECT_DIR/config/profiles/edit.toml"
"$CONFIT_BIN" check --project "$PROJECT_DIR" --profile edit \
  >"$WORK_DIR/check.txt"
grep -Fx "check ok" "$WORK_DIR/check.txt" >/dev/null

DISCARD_DIR="$WORK_DIR/profile-discard"
cp -R "$PROJECT_SRC" "$DISCARD_DIR"

printf '/bool\neqj\n' |
  "$CONFIT_BIN" tui --project "$DISCARD_DIR" --profile edit \
    >"$WORK_DIR/tui-discard.txt"

grep -aF "Unsaved Profile Changes" "$WORK_DIR/tui-discard.txt" >/dev/null
grep -aF "Discard changes" "$WORK_DIR/tui-discard.txt" >/dev/null
diff -u "$PROJECT_SRC/config/profiles/edit.toml" \
  "$DISCARD_DIR/config/profiles/edit.toml"

QUIT_SAVE_DIR="$WORK_DIR/profile-quit-save"
cp -R "$PROJECT_SRC" "$QUIT_SAVE_DIR"

printf '/bool\neq\n\n' |
  "$CONFIT_BIN" tui --project "$QUIT_SAVE_DIR" --profile edit \
    >"$WORK_DIR/tui-quit-save.txt"

grep -aF "Unsaved Profile Changes" "$WORK_DIR/tui-quit-save.txt" >/dev/null
grep -aF "Overwrite Profile" "$WORK_DIR/tui-quit-save.txt" >/dev/null
grep -aF '"delos.edit.bool" = true' \
  "$QUIT_SAVE_DIR/config/profiles/edit.toml" >/dev/null
"$CONFIT_BIN" check --project "$QUIT_SAVE_DIR" --profile edit \
  >"$WORK_DIR/check-quit-save.txt"
grep -Fx "check ok" "$WORK_DIR/check-quit-save.txt" >/dev/null
