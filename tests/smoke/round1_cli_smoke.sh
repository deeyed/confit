#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BUILD_DIR="${TMPDIR:-/tmp}/confit-round1-smoke"
CONFIT_BIN=${CONFIT_BIN:-""}

mkdir -p "$BUILD_DIR"

if [ "$CONFIT_BIN" = "" ]; then
  CONFIT_BUILD_DIR="$BUILD_DIR/cmake"
  cmake -S "$ROOT_DIR" -B "$CONFIT_BUILD_DIR"
  cmake --build "$CONFIT_BUILD_DIR" --target confit
  CONFIT_BIN="$CONFIT_BUILD_DIR/confit"
fi

if [ ! -x "$CONFIT_BIN" ]; then
  echo "Confit smoke binary is not executable: $CONFIT_BIN" >&2
  exit 1
fi

"$CONFIT_BIN" --version | grep -Fx "confit 0.2.0-rc1" >/dev/null
"$CONFIT_BIN" --color never --quiet help >"$BUILD_DIR/help.txt"
grep -F "Usage:" "$BUILD_DIR/help.txt" >/dev/null
for command in help doctor init check resolve gen explain list graph diff compat profile tui completion
do
  grep -F "  $command" "$BUILD_DIR/help.txt" >/dev/null
done

"$CONFIT_BIN" help check | grep -F "confit check --project" >/dev/null
"$CONFIT_BIN" resolve --help | grep -F "confit resolve --project" >/dev/null
"$CONFIT_BIN" --verbose help completion | \
  grep -F "confit completion --shell" >/dev/null
"$CONFIT_BIN" completion --shell bash | grep -F "_confit()" >/dev/null
"$CONFIT_BIN" completion --shell zsh | grep -F "#compdef confit" \
  >/dev/null
"$CONFIT_BIN" completion --shell fish | grep -F "complete -c confit" \
  >/dev/null

"$CONFIT_BIN" doctor >"$BUILD_DIR/doctor.out"
grep -F "Confit doctor" "$BUILD_DIR/doctor.out" >/dev/null
grep -F "version: confit 0.2.0-rc1" "$BUILD_DIR/doctor.out" >/dev/null
grep -F "build mode:" "$BUILD_DIR/doctor.out" >/dev/null
grep -F "platform lane:" "$BUILD_DIR/doctor.out" >/dev/null
grep -F "curses:" "$BUILD_DIR/doctor.out" >/dev/null
grep -F "tui:" "$BUILD_DIR/doctor.out" >/dev/null
grep -F "generators enabled:" "$BUILD_DIR/doctor.out" >/dev/null
grep -F "doctor ok" "$BUILD_DIR/doctor.out" >/dev/null

"$CONFIT_BIN" doctor \
  --project "$ROOT_DIR/tests/fixtures/schema/valid/basic" \
  >"$BUILD_DIR/doctor-project.out"
grep -F "project:" "$BUILD_DIR/doctor-project.out" >/dev/null
grep -F "options:" "$BUILD_DIR/doctor-project.out" >/dev/null
grep -F "doctor ok" "$BUILD_DIR/doctor-project.out" >/dev/null

set +e
"$CONFIT_BIN" unknown >"$BUILD_DIR/unknown.out" 2>"$BUILD_DIR/unknown.err"
STATUS=$?
set -e
if [ "$STATUS" -ne 1 ]; then
  echo "unknown exit code was $STATUS, expected 1" >&2
  exit 1
fi
grep -F "try 'confit help'" "$BUILD_DIR/unknown.err" >/dev/null
