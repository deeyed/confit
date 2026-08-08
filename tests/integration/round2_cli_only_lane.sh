#!/bin/sh
set -eu

SOURCE_DIR=$1
WORK_DIR=$2
BMAKE_BIN=${3:-}

NO_TUI_BUILD_DIR="$WORK_DIR/no-tui"
WINDOWS_CONFIGURE_DIR="$WORK_DIR/windows-configure"
PROJECT_DIR="$SOURCE_DIR/tests/fixtures/schema/valid/basic"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"

if [ "$BMAKE_BIN" != "" ]; then
  "$BMAKE_BIN" -r -C "$SOURCE_DIR" -f "$SOURCE_DIR/Makefile" \
    CONFIT_OBJROOT="$NO_TUI_BUILD_DIR" CONFIT_ENABLE_TUI=no \
    confit "$NO_TUI_BUILD_DIR/tests/bin/confit_test_cli_workflow"
  "$NO_TUI_BUILD_DIR/bin/confit" doctor >/dev/null
  "$NO_TUI_BUILD_DIR/tests/bin/confit_test_cli_workflow" \
    "$NO_TUI_BUILD_DIR/bin/confit" "$SOURCE_DIR" "$WORK_DIR/workflow-c"
  CONFIT_BIN="$NO_TUI_BUILD_DIR/bin/confit"
else
  cmake -S "$SOURCE_DIR" -B "$NO_TUI_BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCONFIT_ENABLE_TUI=OFF
  cmake --build "$NO_TUI_BUILD_DIR" --target confit confit_test_cli_workflow
  ctest --test-dir "$NO_TUI_BUILD_DIR" --output-on-failure \
    -R 'confit\.cli\.(doctor|workflow_c)'
  CONFIT_BIN="$NO_TUI_BUILD_DIR/confit"
fi

"$CONFIT_BIN" doctor >"$WORK_DIR/no-tui-doctor.txt"
grep -F "tui: unsupported" "$WORK_DIR/no-tui-doctor.txt" >/dev/null
grep -F "curses: not available; TUI unsupported" \
  "$WORK_DIR/no-tui-doctor.txt" >/dev/null
grep -E "platform lane: (macos|linux)-cli-only" \
  "$WORK_DIR/no-tui-doctor.txt" >/dev/null

set +e
"$CONFIT_BIN" tui --project "$PROJECT_DIR" --profile sim-dsh \
  >"$WORK_DIR/no-tui.out" 2>"$WORK_DIR/no-tui.err"
STATUS=$?
set -e
if [ "$STATUS" -ne 8 ]; then
  echo "expected no-TUI confit tui to exit 8, got $STATUS" >&2
  exit 1
fi
grep -F "unsupported command or platform" "$WORK_DIR/no-tui.err" >/dev/null
grep -F "confit tui is unsupported in this CLI-only platform lane" \
  "$WORK_DIR/no-tui.err" >/dev/null

HOST_SYSTEM=$(uname -s)
if [ "$BMAKE_BIN" != "" ]; then
  echo "skipped legacy CMake Windows configure guard in bmake-native lane"
elif [ "$HOST_SYSTEM" = "Darwin" ] && command -v clang >/dev/null 2>&1; then
  cmake -S "$SOURCE_DIR" -B "$WINDOWS_CONFIGURE_DIR" \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_C_COMPILER="$(command -v clang)" \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCMAKE_BUILD_TYPE=Release \
    -DCONFIT_ENABLE_TUI=ON
  grep -Fx "CONFIT_ENABLE_TUI:BOOL=OFF" \
    "$WINDOWS_CONFIGURE_DIR/CMakeCache.txt" >/dev/null
else
  echo "skipped Windows configure guard smoke on $HOST_SYSTEM"
fi

echo "cli-only lane ok"
