#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${1:-"${TMPDIR:-/tmp}/confit-build"}

case "$BUILD_DIR" in
  "" | "/" | "$ROOT_DIR" | "$ROOT_DIR"/*)
    echo "refusing unsafe Confit build directory: $BUILD_DIR" >&2
    exit 1
    ;;
esac

rm -rf "$BUILD_DIR"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR"
ctest --test-dir "$BUILD_DIR" --output-on-failure
"$ROOT_DIR/tests/smoke/round1_cli_smoke.sh"
