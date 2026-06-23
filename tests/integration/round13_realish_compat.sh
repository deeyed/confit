#!/bin/sh
set -eu

CONFIT_BIN=$1
SOURCE_DIR=$2
WORK_DIR=$3

PARUS_DIR="$SOURCE_DIR/tests/fixtures/realish/parus"
DELOS_DIR="$SOURCE_DIR/tests/fixtures/realish/delos"
COMPAT_DIR="$SOURCE_DIR/tests/fixtures/realish/compat"
NEGATIVE_COMPAT_DIR="$SOURCE_DIR/tests/fixtures/realish/compat-negative/forbid-dcg"
GOLDEN_DIR="$SOURCE_DIR/tests/golden/realish/compat"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"

"$CONFIT_BIN" compat \
  --parus "$PARUS_DIR" \
  --delos "$DELOS_DIR" \
  --profile parus-delos-debug \
  --compat "$COMPAT_DIR" >"$WORK_DIR/pass.txt"
grep -Fx "compat ok" "$WORK_DIR/pass.txt" >/dev/null

"$CONFIT_BIN" compat \
  --parus "$PARUS_DIR" \
  --delos "$DELOS_DIR" \
  --profile parus-delos-debug \
  --compat "$COMPAT_DIR" \
  --format json >"$WORK_DIR/pass.json"
cmp "$GOLDEN_DIR/parus-delos-debug.json" "$WORK_DIR/pass.json"

if "$CONFIT_BIN" compat \
  --parus "$PARUS_DIR" \
  --delos "$DELOS_DIR" \
  --profile parus-delos-mismatch \
  --compat "$COMPAT_DIR" \
  --format json >"$WORK_DIR/mismatch.json"
then
  exit 1
else
  status=$?
fi
test "$status" -eq 5
cmp "$GOLDEN_DIR/parus-delos-mismatch.json" "$WORK_DIR/mismatch.json"
grep -F '"status": "failed"' "$WORK_DIR/mismatch.json" >/dev/null
grep -F "Parus Delos executor target requires Delos DCG." \
  "$WORK_DIR/mismatch.json" >/dev/null

if "$CONFIT_BIN" compat \
  --parus "$PARUS_DIR" \
  --delos "$DELOS_DIR" \
  --profile parus-delos-debug \
  --compat "$NEGATIVE_COMPAT_DIR" \
  --format json >"$WORK_DIR/negative-rule.json"
then
  exit 1
else
  status=$?
fi
test "$status" -eq 5
grep -F '"suite": "parus-delos-realish-negative"' \
  "$WORK_DIR/negative-rule.json" >/dev/null
grep -F "Negative realish fixture forbids the required Delos DCG path." \
  "$WORK_DIR/negative-rule.json" >/dev/null
