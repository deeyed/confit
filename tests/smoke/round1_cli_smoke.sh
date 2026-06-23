#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BUILD_DIR="${TMPDIR:-/tmp}/confit-round1-smoke"

mkdir -p "$BUILD_DIR"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"$ROOT_DIR/include" \
  "$ROOT_DIR/src/core/status.c" \
  "$ROOT_DIR/src/core/diagnostic.c" \
  "$ROOT_DIR/src/core/model.c" \
  "$ROOT_DIR/src/core/version.c" \
  "$ROOT_DIR/tests/unit/test_status_diagnostic.c" \
  -o "$BUILD_DIR/test_status_diagnostic"

"$BUILD_DIR/test_status_diagnostic"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"$ROOT_DIR/include" \
  "$ROOT_DIR/src/core/status.c" \
  "$ROOT_DIR/src/core/diagnostic.c" \
  "$ROOT_DIR/src/core/model.c" \
  "$ROOT_DIR/src/core/version.c" \
  "$ROOT_DIR/src/host/host_file.c" \
  "$ROOT_DIR/src/host/host_io.c" \
  "$ROOT_DIR/src/host/host_path.c" \
  "$ROOT_DIR/src/cli/main.c" \
  -o "$BUILD_DIR/confit"

"$BUILD_DIR/confit" --version | grep -Fx "confit 0.1.0-round1" >/dev/null
"$BUILD_DIR/confit" help | grep -F "Usage:" >/dev/null
