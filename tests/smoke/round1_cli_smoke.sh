#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BUILD_DIR="${TMPDIR:-/tmp}/confit-round1-smoke"
CURSES_LIBS=${CONFIT_CURSES_LIBS:-"-lcurses"}

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
  -I"$ROOT_DIR/src/parser" \
  "$ROOT_DIR/src/core/status.c" \
  "$ROOT_DIR/src/core/diagnostic.c" \
  "$ROOT_DIR/src/core/model.c" \
  "$ROOT_DIR/src/core/version.c" \
  "$ROOT_DIR/src/compat/compat.c" \
  "$ROOT_DIR/src/explain/explain.c" \
  "$ROOT_DIR/src/generator/config_header.c" \
  "$ROOT_DIR/src/generator/reports.c" \
  "$ROOT_DIR/src/graph/graph.c" \
  "$ROOT_DIR/src/parser/parser.c" \
  "$ROOT_DIR/src/parser/toml_scan.c" \
  "$ROOT_DIR/src/resolver/resolver.c" \
  "$ROOT_DIR/src/schema/schema.c" \
  "$ROOT_DIR/src/tui/curses_frontend.c" \
  "$ROOT_DIR/src/tui/profile_editor.c" \
  "$ROOT_DIR/src/tui/schema_editor.c" \
  "$ROOT_DIR/src/tui/tui.c" \
  "$ROOT_DIR/src/tui/tui_common.c" \
  "$ROOT_DIR/src/host/host_directory.c" \
  "$ROOT_DIR/src/host/host_file.c" \
  "$ROOT_DIR/src/host/host_io.c" \
  "$ROOT_DIR/src/host/host_path.c" \
  "$ROOT_DIR/src/cli/main.c" \
  $CURSES_LIBS \
  -o "$BUILD_DIR/confit"

"$BUILD_DIR/confit" --version | grep -Fx "confit 0.1.0-round1" >/dev/null
"$BUILD_DIR/confit" help | grep -F "Usage:" >/dev/null
