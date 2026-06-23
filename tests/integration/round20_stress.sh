#!/bin/sh
set -eu

CONFIT_BIN=$1
SOURCE_DIR=$2
WORK_DIR=$3
COUNT=${CONFIT_STRESS_OPTION_COUNT:-2500}

case "$COUNT" in
  "" | *[!0-9]*)
    echo "CONFIT_STRESS_OPTION_COUNT must be a positive integer" >&2
    exit 1
    ;;
esac

if [ "$COUNT" -lt 2000 ]; then
  echo "stress count must stay in the thousands" >&2
  exit 1
fi

PROJECT_DIR="$WORK_DIR/synthetic-scale"
OUT_DIR="$WORK_DIR/generated"
MIDPOINT=$((COUNT / 2))

rm -rf "$WORK_DIR"
mkdir -p "$PROJECT_DIR/config/options" "$PROJECT_DIR/config/profiles"

{
  printf '[project]\n'
  printf 'name = "delos"\n'
  printf 'version = "0.1.0"\n'
  printf 'schema_version = 1\n'
  printf 'imports = ["options/stress.toml"]\n'
} >"$PROJECT_DIR/config/project.toml"

{
  index=1
  while [ "$index" -le "$COUNT" ]; do
    option_id=$(printf 'delos.stress.opt%04d' "$index")
    printf '[option."%s"]\n' "$option_id"
    printf 'type = "bool"\n'
    printf 'default = false\n'
    printf 'prompt = "Synthetic Stress Option %04d"\n' "$index"
    printf 'category = "stress"\n'
    printf 'tags = ["stress", "scale"]\n'
    printf 'help = "Synthetic scale fixture option %04d."\n\n' "$index"
    index=$((index + 1))
  done
} >"$PROJECT_DIR/config/options/stress.toml"

{
  printf '[profile]\n'
  printf 'name = "scale"\n'
  printf 'schema_version = 1\n\n'
  printf '[values]\n'
  printf '"delos.stress.opt0001" = true\n'
  printf '"delos.stress.opt%04d" = true\n' "$MIDPOINT"
  printf '"delos.stress.opt%04d" = true\n' "$COUNT"
} >"$PROJECT_DIR/config/profiles/scale.toml"

"$CONFIT_BIN" check --project "$PROJECT_DIR" --profile scale \
  >"$WORK_DIR/check.txt"
grep -Fx "check ok" "$WORK_DIR/check.txt" >/dev/null

"$CONFIT_BIN" list --project "$PROJECT_DIR" --tag scale \
  >"$WORK_DIR/list.txt"
listed_count=$(grep -c '^delos\.stress\.opt' "$WORK_DIR/list.txt")
if [ "$listed_count" -ne "$COUNT" ]; then
  echo "expected $COUNT listed options, got $listed_count" >&2
  exit 1
fi

"$CONFIT_BIN" graph --project "$PROJECT_DIR" \
  >"$WORK_DIR/graph.json"
grep -F "\"id\": \"$(printf 'delos.stress.opt%04d' "$COUNT")\"" \
  "$WORK_DIR/graph.json" >/dev/null

"$CONFIT_BIN" gen --project "$PROJECT_DIR" --profile scale --out "$OUT_DIR" \
  >"$WORK_DIR/gen.txt"
test -f "$OUT_DIR/config.h"
test -f "$OUT_DIR/config.report.json"
test -f "$OUT_DIR/config.explain.txt"
test -f "$OUT_DIR/config.graph.json"
test -f "$OUT_DIR/config.inputs.json"
grep -F "$(printf 'DELOS_CONFIG_STRESS_OPT%04d' "$COUNT")" \
  "$OUT_DIR/config.h" >/dev/null

printf 'stress ok: %s options\n' "$COUNT"
