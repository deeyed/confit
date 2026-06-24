#!/bin/sh
set -eu

CONFIT_BIN=$1
SOURCE_DIR=$2
WORK_DIR=$3

PROJECT_DIR="$SOURCE_DIR/tests/fixtures/schema/valid/basic"
PARUS_DIR="$SOURCE_DIR/tests/fixtures/compat/parus"
DELOS_DIR="$SOURCE_DIR/tests/fixtures/compat/delos"
GEN_DIR="$WORK_DIR/generated"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"

"$CONFIT_BIN" doctor --project "$PROJECT_DIR" >"$WORK_DIR/doctor.txt"
grep -F "doctor ok" "$WORK_DIR/doctor.txt" >/dev/null
grep -F "options:" "$WORK_DIR/doctor.txt" >/dev/null

"$CONFIT_BIN" check --project "$PROJECT_DIR" --profile sim-dsh \
  >"$WORK_DIR/check.txt"
grep -Fx "check ok" "$WORK_DIR/check.txt" >/dev/null

"$CONFIT_BIN" resolve --project "$PROJECT_DIR" --profile sim-dsh \
  --format json >"$WORK_DIR/resolve.json"
grep -F '"schema": "confit-resolved-v1"' "$WORK_DIR/resolve.json" >/dev/null

"$CONFIT_BIN" resolve --project "$PROJECT_DIR" --profile sim-dsh \
  --set delos.output.name=manual --format toml >"$WORK_DIR/resolve.toml"
grep -F '"delos.output.name" = "manual"' "$WORK_DIR/resolve.toml" >/dev/null
grep -F '"delos.output.name" = "cli --set"' "$WORK_DIR/resolve.toml" >/dev/null

"$CONFIT_BIN" list --project "$PROJECT_DIR" --category debug \
  >"$WORK_DIR/list.txt"
grep -F "delos.debug.ddc" "$WORK_DIR/list.txt" >/dev/null
if grep -F "delos.scheduler.task_slots" "$WORK_DIR/list.txt" >/dev/null; then
  echo "category filter leaked scheduler option" >&2
  exit 1
fi

"$CONFIT_BIN" profile list --project "$PROJECT_DIR" \
  >"$WORK_DIR/profile-list.txt"
grep -F "sim-dsh" "$WORK_DIR/profile-list.txt" >/dev/null

"$CONFIT_BIN" profile show --project "$PROJECT_DIR" sim-dsh \
  >"$WORK_DIR/profile-show.toml"
grep -F "[profile]" "$WORK_DIR/profile-show.toml" >/dev/null
grep -F "base = \"debug\"" "$WORK_DIR/profile-show.toml" >/dev/null
grep -F "\"delos.scheduler.task_slots\" = 32" "$WORK_DIR/profile-show.toml" \
  >/dev/null

"$CONFIT_BIN" profile validate --project "$PROJECT_DIR" sim-dsh \
  >"$WORK_DIR/profile-validate.txt"
grep -Fx "profile ok: sim-dsh" "$WORK_DIR/profile-validate.txt" >/dev/null

"$CONFIT_BIN" completion --shell bash >"$WORK_DIR/completion.bash"
grep -F "_confit()" "$WORK_DIR/completion.bash" >/dev/null
grep -F -- "--artifact" "$WORK_DIR/completion.bash" >/dev/null

"$CONFIT_BIN" completion --shell zsh >"$WORK_DIR/completion.zsh"
grep -F "#compdef confit" "$WORK_DIR/completion.zsh" >/dev/null

"$CONFIT_BIN" completion --shell fish >"$WORK_DIR/completion.fish"
grep -F "complete -c confit" "$WORK_DIR/completion.fish" >/dev/null

"$CONFIT_BIN" help profile >"$WORK_DIR/help-profile.txt"
grep -F "Confit command: profile" "$WORK_DIR/help-profile.txt" >/dev/null
grep -F "Global options:" "$WORK_DIR/help-profile.txt" >/dev/null
grep -F "profile validate" "$WORK_DIR/help-profile.txt" >/dev/null

"$CONFIT_BIN" check --project "$PROJECT_DIR" --profile sim-dsh --quiet \
  >"$WORK_DIR/check-quiet.txt"
if grep -F "check ok" "$WORK_DIR/check-quiet.txt" >/dev/null; then
  echo "quiet check printed non-essential success text" >&2
  exit 1
fi

"$CONFIT_BIN" check --project "$PROJECT_DIR" --profile sim-dsh --verbose \
  >"$WORK_DIR/check-verbose.out" 2>"$WORK_DIR/check-verbose.err"
grep -F "confit: verbose: command=check" \
  "$WORK_DIR/check-verbose.err" >/dev/null
grep -Fx "check ok" "$WORK_DIR/check-verbose.out" >/dev/null

"$CONFIT_BIN" graph --project "$PROJECT_DIR" --profile sim-dsh \
  >"$WORK_DIR/graph.json"
grep -F '"nodes":' "$WORK_DIR/graph.json" >/dev/null

"$CONFIT_BIN" graph --project "$PROJECT_DIR" --profile sim-dsh --format dot \
  >"$WORK_DIR/graph.dot"
grep -F "digraph confit" "$WORK_DIR/graph.dot" >/dev/null
grep -F "delos.debug.ddc" "$WORK_DIR/graph.dot" >/dev/null

"$CONFIT_BIN" diff --project "$PROJECT_DIR" --profile sim-dsh --base debug \
  >"$WORK_DIR/diff.txt"
grep -F "diff: debug -> sim-dsh" "$WORK_DIR/diff.txt" >/dev/null
grep -F "delos.scheduler.task_slots" "$WORK_DIR/diff.txt" >/dev/null

"$CONFIT_BIN" diff --project "$PROJECT_DIR" --profile sim-dsh --base debug \
  --format json >"$WORK_DIR/diff.json"
grep -F '"schema": "confit-diff-v1"' "$WORK_DIR/diff.json" >/dev/null
grep -F '"id": "delos.scheduler.task_slots"' "$WORK_DIR/diff.json" >/dev/null

"$CONFIT_BIN" explain --project "$PROJECT_DIR" --profile sim-dsh \
  delos.debug.ddc >"$WORK_DIR/explain.txt"
grep -F "forced by:" "$WORK_DIR/explain.txt" >/dev/null

"$CONFIT_BIN" gen --project "$PROJECT_DIR" --profile sim-dsh --out "$GEN_DIR" \
  >"$WORK_DIR/gen.txt"
grep -F "gen ok:" "$WORK_DIR/gen.txt" >/dev/null
test -s "$GEN_DIR/config.h"
test -s "$GEN_DIR/config.report.json"
test -s "$GEN_DIR/config.explain.txt"
test -s "$GEN_DIR/config.graph.json"
test -s "$GEN_DIR/config.inputs.json"
test -s "$GEN_DIR/config.cmake"
test -s "$GEN_DIR/config/config.qsm"
test -s "$GEN_DIR/config.qst"
grep -F "DELOS_CONFIG_DEBUG_DDC" "$GEN_DIR/config.h" >/dev/null
grep -F "CONFIT_CONFIG_HEADER" "$GEN_DIR/config.cmake" >/dev/null
grep -F "confit-config-manifest-v1" "$GEN_DIR/config/config.qsm" >/dev/null
grep -F "[\"delos.target.board\"]" "$GEN_DIR/config/config.qsm" >/dev/null
grep -F "confit-qstar-manifest-v1" "$GEN_DIR/config.qst" >/dev/null
grep -F '"confit_version": "confit 0.1.0-round1"' \
  "$GEN_DIR/config.inputs.json" >/dev/null
grep -F '"config/targets/host-sim.toml"' "$GEN_DIR/config.inputs.json" \
  >/dev/null

"$CONFIT_BIN" compat --parus "$PARUS_DIR" --delos "$DELOS_DIR" \
  --profile parus-delos-debug >"$WORK_DIR/compat.txt"
grep -Fx "compat ok" "$WORK_DIR/compat.txt" >/dev/null

set +e
"$CONFIT_BIN" unknown-command >"$WORK_DIR/unknown.out" \
  2>"$WORK_DIR/unknown.err"
STATUS=$?
set -e
if [ "$STATUS" -ne 1 ]; then
  echo "unknown command exit code was $STATUS, expected 1" >&2
  exit 1
fi
grep -F "try 'confit help'" "$WORK_DIR/unknown.err" >/dev/null
