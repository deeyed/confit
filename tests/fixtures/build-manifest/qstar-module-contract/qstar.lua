qstar.project {
  name = "confit-qstar-module-contract",
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

expect_equal(config.values["delos.target.board"].value, "nucleo-h753zi", "target board")

expect_equal(selection.schema, "delos-build-selection-v1", "selection schema")

expect_equal(selection.arch.cpu, "cortex-m7", "selection cpu")

expect_equal(selection.board.objects[1], "//src/board/armv7m/stm32h7/nucleo-h753zi:board_objects", "selection board object")

expect_equal(selection.board.linker_script, "linker/armv7m/nucleo-h753zi.ld", "selection linker script")

qstar.config "selected_board_c" {
  lang = {
    c = {
      public_include_dirs = selection.board.include_dirs,
      compile_options = {
        "-DDELOS_CONFIG_HEADER=\"" .. config.artifacts.header .. "\"",
      },
    },
  },
  link_options = {
    "-T",
    selection.board.linker_script,
  },
}

-- Real Delos executable/staticlib targets consume selection.board.objects in
-- target-local fields such as objects = selection.board.objects.

qstar.group "manifest_contract" {
  deps = {
  },
}
