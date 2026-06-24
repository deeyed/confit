qstar.project {
  name = "confit-qstar-module-contract",
  version = "0.1.0",
  root = ".",
}

local config = qstar.import_module("generated/config")

local selection = qstar.import_module("generated/delos_build_selection")

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
