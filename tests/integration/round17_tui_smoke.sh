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
PAGE_DOWN_KEY=$(tput knp 2>/dev/null || printf '\033[6~')
HOME_KEY=$(tput khome 2>/dev/null || printf '\033[H')
END_KEY=$(tput kend 2>/dev/null || printf '\033[F')

printf 'jq' | "$CONFIT_BIN" tui --project "$PROJECT_DIR" --profile sim-dsh \
  >"$WORK_DIR/tui.txt"

grep -aF "row 1/" "$WORK_DIR/tui.txt" >/dev/null
grep -aF "Confit TUI - menuconfig profile" "$WORK_DIR/tui.txt" >/dev/null
grep -aF "project=" "$WORK_DIR/tui.txt" >/dev/null
grep -aF "Menu" "$WORK_DIR/tui.txt" >/dev/null
grep -aF "[-]  debug" "$WORK_DIR/tui.txt" >/dev/null
grep -aF "tags:" "$WORK_DIR/tui.txt" >/dev/null
grep -aF "deps" "$WORK_DIR/tui.txt" >/dev/null
grep -aF "arrows/jk move PgUp/PgDn Home/End Enter/Space toggle / search n/N result" \
  "$WORK_DIR/tui.txt" >/dev/null

printf '\nq' | "$CONFIT_BIN" tui --project "$PROJECT_DIR" --profile sim-dsh \
  >"$WORK_DIR/tui-collapse.txt"

grep -aF "collapsed menu debug" "$WORK_DIR/tui-collapse.txt" >/dev/null

printf '%sq' "$PAGE_DOWN_KEY" |
  "$CONFIT_BIN" tui --project "$PROJECT_DIR" --profile sim-dsh \
    >"$WORK_DIR/tui-page-down.txt"

grep -aF "moved PageDown" "$WORK_DIR/tui-page-down.txt" >/dev/null

printf 'j%sq' "$HOME_KEY" |
  "$CONFIT_BIN" tui --project "$PROJECT_DIR" --profile sim-dsh \
    >"$WORK_DIR/tui-home.txt"

grep -aF "moved Home" "$WORK_DIR/tui-home.txt" >/dev/null

printf '%sq' "$END_KEY" |
  "$CONFIT_BIN" tui --project "$PROJECT_DIR" --profile sim-dsh \
    >"$WORK_DIR/tui-end.txt"

grep -aF "moved End" "$WORK_DIR/tui-end.txt" >/dev/null

printf '/board\nq' | "$CONFIT_BIN" tui --project "$PROJECT_DIR" \
  --profile sim-dsh >"$WORK_DIR/tui-search-single.txt"

grep -aF "search 1/1: delos.target.board" "$WORK_DIR/tui-search-single.txt" \
  >/dev/null
grep -aF "result=1/1" "$WORK_DIR/tui-search-single.txt" >/dev/null
grep -aF "delos.target.board" "$WORK_DIR/tui-search-single.txt" >/dev/null

printf '/delos\nnNq' | "$CONFIT_BIN" tui --project "$PROJECT_DIR" \
  --profile sim-dsh >"$WORK_DIR/tui-search-next-prev.txt"

grep -aF "search 1/" "$WORK_DIR/tui-search-next-prev.txt" >/dev/null
grep -aF "next result 2/" "$WORK_DIR/tui-search-next-prev.txt" >/dev/null
grep -aF "previous result 1/" "$WORK_DIR/tui-search-next-prev.txt" >/dev/null

printf 'j?qq' | "$CONFIT_BIN" tui --project "$PROJECT_DIR" \
  --profile sim-dsh >"$WORK_DIR/tui-detail-question.txt"

grep -aF "Confit Help" "$WORK_DIR/tui-detail-question.txt" >/dev/null
grep -aF "prompt: Enable DDC" "$WORK_DIR/tui-detail-question.txt" >/dev/null
grep -aF "id: delos.debug.ddc" "$WORK_DIR/tui-detail-question.txt" >/dev/null
grep -aF "type: bool" "$WORK_DIR/tui-detail-question.txt" >/dev/null
grep -aF "current: true" "$WORK_DIR/tui-detail-question.txt" >/dev/null
grep -aF "default: false" "$WORK_DIR/tui-detail-question.txt" >/dev/null
grep -aF "source:" "$WORK_DIR/tui-detail-question.txt" >/dev/null
grep -aF "category: debug" "$WORK_DIR/tui-detail-question.txt" >/dev/null
grep -aF "tags: debug, host-tooling" "$WORK_DIR/tui-detail-question.txt" \
  >/dev/null
grep -aF "requires:" "$WORK_DIR/tui-detail-question.txt" >/dev/null
grep -aF "conflicts:" "$WORK_DIR/tui-detail-question.txt" >/dev/null
grep -aF "forces:" "$WORK_DIR/tui-detail-question.txt" >/dev/null
grep -aF "recommends:" "$WORK_DIR/tui-detail-question.txt" >/dev/null
grep -aF "Help" "$WORK_DIR/tui-detail-question.txt" >/dev/null

printf 'jhqq' | "$CONFIT_BIN" tui --project "$PROJECT_DIR" \
  --profile sim-dsh >"$WORK_DIR/tui-detail-h.txt"

grep -aF "Confit Help" "$WORK_DIR/tui-detail-h.txt" >/dev/null
grep -aF "closed detail" "$WORK_DIR/tui-detail-h.txt" >/dev/null

printf '/\033q' | "$CONFIT_BIN" tui --project "$PROJECT_DIR" \
  --profile sim-dsh >"$WORK_DIR/tui-cancel.txt"

grep -aF "cancelled" "$WORK_DIR/tui-cancel.txt" >/dev/null

printf 'q' | env TERM=xterm LINES=8 COLUMNS=35 \
  "$CONFIT_BIN" tui --project "$PROJECT_DIR" --profile sim-dsh \
  >"$WORK_DIR/tui-small.txt"

grep -aF "Confit TUI - menuconfig profile" "$WORK_DIR/tui-small.txt" \
  >/dev/null
grep -aF "row 1/" "$WORK_DIR/tui-small.txt" >/dev/null
