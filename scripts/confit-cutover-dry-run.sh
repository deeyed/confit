#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PROJECT=both
OUT_ROOT=
CONFIT_BIN=
BUILD_DIR="${TMPDIR:-/tmp}/confit-cutover-dry-run-build"

usage() {
  cat <<'USAGE'
Usage:
  confit-cutover-dry-run.sh --project delos-realish|parus-realish|both --out <abs-path> [options]

Options:
  --confit-bin <path>  Use an existing confit executable.
  --build-dir <path>   Build directory used when --confit-bin is omitted.
  --help, -h           Show this help.

The script validates realish fixtures, runs the cross-project compatibility
suite, generates artifacts under the output directory, compares them with
checked-in golden artifacts, and writes rollback notes. It never edits real
Delos/Parus project roots.
USAGE
}

die() {
  echo "confit-cutover-dry-run.sh: $*" >&2
  exit 1
}

is_owned_run_dir() {
  [ -f "$1/.confit-cutover-dry-run" ]
}

require_absolute_path() {
  case "$1" in
    /*)
      ;;
    *)
      die "$2 must be an absolute path"
      ;;
  esac
}

guard_output_root() {
  require_absolute_path "$OUT_ROOT" "--out"
  case "$OUT_ROOT" in
    "" | "/" | "$ROOT_DIR" | "$ROOT_DIR"/*)
      die "refusing unsafe output directory: $OUT_ROOT"
      ;;
  esac
}

guard_build_dir() {
  require_absolute_path "$BUILD_DIR" "--build-dir"
  case "$BUILD_DIR" in
    "" | "/" | "$ROOT_DIR" | "$ROOT_DIR"/*)
      die "refusing unsafe build directory: $BUILD_DIR"
      ;;
  esac
}

ensure_confit_bin() {
  if [ -n "$CONFIT_BIN" ]; then
    [ -x "$CONFIT_BIN" ] || die "confit binary is not executable: $CONFIT_BIN"
    return
  fi

  guard_build_dir
  cmake -S "$ROOT_DIR" -B "$BUILD_DIR"
  cmake --build "$BUILD_DIR" --target confit
  CONFIT_BIN="$BUILD_DIR/confit"
  [ -x "$CONFIT_BIN" ] || die "built confit binary is missing: $CONFIT_BIN"
}

project_field() {
  case "$1" in
    delos-realish)
      case "$2" in
        project_dir) echo "$ROOT_DIR/tests/fixtures/realish/delos" ;;
        profile) echo "sim-dsh" ;;
        golden_dir) echo "$ROOT_DIR/tests/golden/realish/delos/sim-dsh" ;;
        label) echo "Delos realish sim-dsh" ;;
        *) die "unknown project field: $2" ;;
      esac
      ;;
    parus-realish)
      case "$2" in
        project_dir) echo "$ROOT_DIR/tests/fixtures/realish/parus" ;;
        profile) echo "qemu-aarch64" ;;
        golden_dir) echo "$ROOT_DIR/tests/golden/realish/parus/qemu-aarch64" ;;
        label) echo "Parus realish qemu-aarch64" ;;
        *) die "unknown project field: $2" ;;
      esac
      ;;
    *)
      die "unknown project selector: $1"
      ;;
  esac
}

prepare_run_dir() {
  run_dir=$1

  if [ -e "$run_dir" ]; then
    if is_owned_run_dir "$run_dir"; then
      rm -rf "$run_dir"
    else
      die "refusing to overwrite unowned output directory: $run_dir"
    fi
  fi
  mkdir -p "$run_dir/generated" "$run_dir/diff"
  : >"$run_dir/.confit-cutover-dry-run"
}

write_rollback_note() {
  run_dir=$1
  selector=$2
  profile=$3

  cat >"$run_dir/ROLLBACK.md" <<EOF
# Confit Cutover Dry-Run Rollback

Project: $selector
Profile: $profile

This directory is a generated dry-run package. The dry-run did not edit the
source project or any real Delos/Parus configuration tree.

To discard this rehearsal:

\`\`\`sh
rm -rf "$run_dir"
\`\`\`

If these artifacts were manually copied into a build tree, remove the copied
generated files listed in \`CUTOVER_SUMMARY.txt\` and rerun the pre-existing
build configuration flow. No source TOML rollback is required for this dry-run.
EOF
}

write_summary() {
  run_dir=$1
  selector=$2
  project_dir=$3
  profile=$4
  golden_dir=$5

  cat >"$run_dir/CUTOVER_SUMMARY.txt" <<EOF
project: $selector
project_dir: $project_dir
profile: $profile
golden_dir: $golden_dir
generated_dir: $run_dir/generated
compat_report: $run_dir/compat.json
artifact_diff: $run_dir/artifact-diff.txt
rollback_note: $run_dir/ROLLBACK.md

generated files:
  config.h
  config.cmake
  config.qst
  config.report.json
  config.explain.txt
  config.graph.json
  config.inputs.json
EOF
}

verify_artifact_set() {
  generated_dir=$1

  for artifact in \
    config.h \
    config.cmake \
    config.qst \
    config.report.json \
    config.explain.txt \
    config.graph.json \
    config.inputs.json
  do
    [ -f "$generated_dir/$artifact" ] ||
      die "generated artifact is missing: $generated_dir/$artifact"
  done
}

compare_golden_artifacts() {
  run_dir=$1
  generated_dir=$2
  golden_dir=$3
  diff_log="$run_dir/artifact-diff.txt"

  : >"$diff_log"
  for artifact in \
    config.h \
    config.cmake \
    config.qst \
    config.report.json \
    config.explain.txt \
    config.graph.json \
    config.inputs.json
  do
    if cmp -s "$golden_dir/$artifact" "$generated_dir/$artifact"; then
      echo "$artifact: ok" >>"$diff_log"
    else
      echo "$artifact: changed" >>"$diff_log"
      diff -u "$golden_dir/$artifact" "$generated_dir/$artifact" \
        >"$run_dir/diff/$artifact.diff" || true
      die "generated artifact differs from golden: $artifact"
    fi
  done
}

run_compat_gate() {
  run_dir=$1
  parus_dir="$ROOT_DIR/tests/fixtures/realish/parus"
  delos_dir="$ROOT_DIR/tests/fixtures/realish/delos"
  compat_dir="$ROOT_DIR/tests/fixtures/realish/compat"

  "$CONFIT_BIN" compat \
    --parus "$parus_dir" \
    --delos "$delos_dir" \
    --profile parus-delos-debug \
    --compat "$compat_dir" \
    --format json >"$run_dir/compat.json"
  grep -F '"status": "ok"' "$run_dir/compat.json" >/dev/null
}

run_project() {
  selector=$1
  project_dir=$(project_field "$selector" project_dir)
  profile=$(project_field "$selector" profile)
  golden_dir=$(project_field "$selector" golden_dir)
  label=$(project_field "$selector" label)
  run_dir="$OUT_ROOT/$selector"

  prepare_run_dir "$run_dir"
  write_summary "$run_dir" "$selector" "$project_dir" "$profile" "$golden_dir"
  write_rollback_note "$run_dir" "$selector" "$profile"

  echo "cutover dry-run: $label"
  echo "  output: $run_dir"

  "$CONFIT_BIN" check --project "$project_dir" --profile "$profile" \
    >"$run_dir/check.txt"
  grep -Fx "check ok" "$run_dir/check.txt" >/dev/null

  "$CONFIT_BIN" check --project "$project_dir" --profile "$profile" --strict \
    >"$run_dir/check-strict.txt"
  grep -Fx "check ok" "$run_dir/check-strict.txt" >/dev/null

  run_compat_gate "$run_dir"

  "$CONFIT_BIN" gen --project "$project_dir" --profile "$profile" \
    --out "$run_dir/generated" --artifact all >"$run_dir/gen.txt"
  grep -F "gen ok:" "$run_dir/gen.txt" >/dev/null

  verify_artifact_set "$run_dir/generated"
  compare_golden_artifacts "$run_dir" "$run_dir/generated" "$golden_dir"

  grep -F '"schema": "confit-inputs-v1"' \
    "$run_dir/generated/config.inputs.json" >/dev/null
  grep -F '"schema": "confit-report-v1"' \
    "$run_dir/generated/config.report.json" >/dev/null

  echo "  check: ok"
  echo "  compat: ok"
  echo "  artifact verify: ok"
  echo "  rollback: $run_dir/ROLLBACK.md"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --project)
      [ "$#" -ge 2 ] || die "missing value for --project"
      PROJECT=$2
      shift 2
      ;;
    --out)
      [ "$#" -ge 2 ] || die "missing value for --out"
      OUT_ROOT=$2
      shift 2
      ;;
    --confit-bin)
      [ "$#" -ge 2 ] || die "missing value for --confit-bin"
      CONFIT_BIN=$2
      shift 2
      ;;
    --build-dir)
      [ "$#" -ge 2 ] || die "missing value for --build-dir"
      BUILD_DIR=$2
      shift 2
      ;;
    --help | -h)
      usage
      exit 0
      ;;
    *)
      die "unknown option: $1"
      ;;
  esac
done

[ -n "$OUT_ROOT" ] || die "missing --out"

case "$PROJECT" in
  delos-realish | parus-realish | both)
    ;;
  *)
    die "--project must be delos-realish, parus-realish, or both"
    ;;
esac

guard_output_root
ensure_confit_bin
mkdir -p "$OUT_ROOT"

case "$PROJECT" in
  delos-realish)
    run_project delos-realish
    ;;
  parus-realish)
    run_project parus-realish
    ;;
  both)
    run_project delos-realish
    run_project parus-realish
    ;;
esac

echo "cutover dry-run ok: $OUT_ROOT"
