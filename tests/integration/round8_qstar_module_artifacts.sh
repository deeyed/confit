#!/bin/sh
set -eu

CONFIT_BIN=$1
SOURCE_DIR=$2
WORK_DIR=$3

find_qstar() {
  if [ "${CONFIT_QSTAR_BIN:-}" != "" ] && [ -x "$CONFIT_QSTAR_BIN" ]; then
    echo "$CONFIT_QSTAR_BIN"
    return 0
  fi

  for candidate in \
    /Users/gungye/.local/bin/qstar \
    /Users/gungye/workspace/Cale/qstar/build/bin/qstar
  do
    if [ -x "$candidate" ]; then
      echo "$candidate"
      return 0
    fi
  done

  if command -v qstar >/dev/null 2>&1; then
    command -v qstar
    return 0
  fi

  return 1
}

QSTAR_BIN=$(find_qstar || true)
if [ "$QSTAR_BIN" = "" ]; then
  echo "qstar module artifact smoke skipped: qstar not found"
  exit 77
fi

STATIC_FIXTURE="$SOURCE_DIR/tests/fixtures/build-manifest/qstar-module-contract"
DELOS_PROJECT="$SOURCE_DIR/tests/fixtures/realish/delos"
GEN_DIR="$WORK_DIR/confit-generated"
QSTAR_PROJECT="$WORK_DIR/qstar-generated"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"

"$QSTAR_BIN" --version >"$WORK_DIR/qstar-version.txt"

"$QSTAR_BIN" --file "$STATIC_FIXTURE/qstar.lua" check //:manifest_contract \
  >"$WORK_DIR/static-qstar-check.txt"
grep -F "status ok" "$WORK_DIR/static-qstar-check.txt" >/dev/null
grep -F "root //:manifest_contract" "$WORK_DIR/static-qstar-check.txt" \
  >/dev/null

"$CONFIT_BIN" gen --project "$DELOS_PROJECT" --profile release \
  --target renode-nucleo-h753zi --out "$GEN_DIR" \
  --artifact qstar --artifact build-selection >"$WORK_DIR/confit-gen.txt"
grep -F "gen ok:" "$WORK_DIR/confit-gen.txt" >/dev/null

test -s "$GEN_DIR/config/config.qsm"
test -s "$GEN_DIR/config.qst"
test -s "$GEN_DIR/delos_build_selection/delos_build_selection.qsm"

mkdir -p "$QSTAR_PROJECT/generated"
cp -R "$GEN_DIR/config" "$QSTAR_PROJECT/generated/config"
cp -R "$GEN_DIR/delos_build_selection" \
  "$QSTAR_PROJECT/generated/delos_build_selection"

cat >"$QSTAR_PROJECT/qstar.lua" <<'EOF'
qstar.project {
  name = "confit-generated-qstar-smoke",
  version = "0.1.0",
  root = ".",
}

local config = qstar.import_module("generated/config")
local selection = qstar.import_module("generated/delos_build_selection")

local function expect_equal(actual, expected, label)
  if actual ~= expected then
    error(label .. ": expected " .. tostring(expected) .. ", got " .. tostring(actual))
  end
end

expect_equal(config.schema, "confit-config-manifest-v1", "config schema")
expect_equal(config.project, "delos", "config project")
expect_equal(config.profile, "release", "config profile")
expect_equal(config.target, "renode-nucleo-h753zi", "config target")
expect_equal(config.values["delos.target.arch"].value, "armv7m", "target arch")
expect_equal(config.values["delos.target.cpu"].value, "cortex-m7", "target cpu")

expect_equal(selection.schema, "delos-build-selection-v1", "selection schema")
expect_equal(selection.project, "delos", "selection project")
expect_equal(selection.profile, "release", "selection profile")
expect_equal(selection.target, "renode-nucleo-h753zi", "selection target")
expect_equal(selection.arch.id, "armv7m", "selection arch")
expect_equal(selection.arch.toolchain, "arm-none-eabi", "selection toolchain")
expect_equal(selection.board.id, "nucleo-h753zi", "selection board")
expect_equal(selection.board.family, "stm32h7", "selection board family")
expect_equal(selection.board.objects[1], "//src/board/armv7m/stm32h7/nucleo-h753zi:board_objects", "selection board object")
expect_equal(selection.board.include_dirs[1], "src/board/armv7m/stm32h7/nucleo-h753zi", "selection include dir")
expect_equal(selection.board.linker_script, "linker/armv7m/nucleo-h753zi.ld", "selection linker script")

qstar.config "selected_board_c" {
  lang = {
    c = {
      public_include_dirs = selection.board.include_dirs,
      compile_options = {
        "-DDELOS_CONFIG_HEADER=\"" .. config.artifacts.header .. "\"",
        "-DDELOS_TARGET_BOARD=\"" .. selection.board.id .. "\"",
      },
    },
  },
  link_options = {
    "-T",
    selection.board.linker_script,
  },
}

qstar.group "manifest_contract" {
  deps = {
  },
}
EOF

"$QSTAR_BIN" --file "$QSTAR_PROJECT/qstar.lua" check //:manifest_contract \
  >"$WORK_DIR/generated-qstar-check.txt"
grep -F "status ok" "$WORK_DIR/generated-qstar-check.txt" >/dev/null
grep -F "root //:manifest_contract" "$WORK_DIR/generated-qstar-check.txt" \
  >/dev/null
