---
doc_type: tool-spec
status: accepted
authority: normative
last_verified: 2026-06-24
---

# QStar Build Manifest Contract

이 문서는 Confit generated artifact 중 QStar가 소비하는 build manifest의
정본 계약이다. QStar 문법 기준은 `/Users/gungye/workspace/Cale/qstar`의
`wiki/AI_INDEX.md`, `wiki/reference/modules.md`,
`wiki/reference/configs.md`를 기준으로 확인했다.

## QStar 문법 기준

Confit generated QStar artifact는 다음 QStar DSL 규칙을 따라야 한다.

- `.qst`는 graph declaration fragment다.
- `.qst`는 `qstar.import_file("path.qst")`로 읽는다.
- `.qst` 안에서는 target, config, toolset, group, stage 같은 graph
  declaration을 만들 수 있다.
- `.qsm`은 helper module이다.
- `.qsm`은 `qstar.import_module("folder/path")`로 읽는다.
- `qstar.import_module("folder/path")`는 실제로
  `folder/path/path.qsm`을 읽는다.
- `qstar.import_module(...)`에는 `.qsm` file path를 직접 넘기지 않는다.
- `.qsm`은 table을 반환해야 한다.
- `.qsm` 안에서는 `qstar.project`, `qstar.toolset`, `qstar.config`,
  target rule, `qstar.custom_target`, `qstar.stage`,
  `qstar.target_family`, `qstar.subdir`, `qstar.import_file` 같은 graph
  declaration이 금지된다.

따라서 Confit의 canonical QStar import artifact는 `.qst`가 아니라 `.qsm`
이어야 한다. Confit이 QStar graph fragment를 생성할 수는 있지만, 그것은
별도 artifact이며 pure resolved-value manifest와 섞지 않는다.

## Canonical Artifact

새 정본 QStar artifact는 다음 path다.

```text
<out>/config/config.qsm
```

QStar에서 읽을 때는 file name이 아니라 folder path를 넘긴다.

```lua
local config = qstar.import_module(
  "build/generated/config/delos/release/config"
)
```

`config/config.qsm`은 graph declaration을 하지 않는 pure table module이다.
이 module은 Delos/Parus source tree, CMake file, QStar root graph를 수정하지
않고, 명시적인 `confit gen --out <out>` directory 아래에만 생성된다.

## `config/config.qsm` Schema

`config/config.qsm`은 다음 top-level field를 가진다.

```lua
return {
  schema = "confit-config-manifest-v1",
  project = "delos",
  profile = "release",
  target = "renode-nucleo-h753zi",
  confit_version = "confit 0.1.0-round1",
  source_hash = "0x0123456789ABCDEF",
  option_count = 42,

  artifacts = {
    header = "config.h",
    report_json = "config.report.json",
    explain_text = "config.explain.txt",
    graph_json = "config.graph.json",
    inputs_json = "config.inputs.json",
    cmake = "config.cmake",
    legacy_qst = "config.qst",
  },

  values = {
    ["delos.target.arch"] = {
      type = "enum",
      value = "armv7m",
      text = "armv7m",
      source = "targets/renode-nucleo-h753zi.toml",
    },
  },
}
```

Field 규칙:

- `schema`는 반드시 `confit-config-manifest-v1`이다.
- `project`, `profile`, `target`, `confit_version`, `source_hash`는
  generated artifact provenance다.
- `option_count`는 `values` entry 개수와 일치해야 한다.
- `artifacts` path는 `<out>` 기준 상대 path다.
- `values` key는 stable option id다.
- `values[option].type`은 Confit option type name이다.
- `values[option].value`는 Lua에서 바로 쓰기 쉬운 primitive 값이다.
- `values[option].text`는 Confit의 deterministic textual value다.
- `values[option].source`는 resolved value provenance다.

Numeric value는 Lua number 한계를 고려해야 한다. QStar graph가 64-bit
정밀도를 요구하는 값은 `value`만 보지 말고 `text` 또는 generated `config.h`
를 기준으로 삼는다.

## Legacy `config.qst`

기존 `config.qst`는 compatibility artifact다. 현재 형태가 `return { ... }`
인 table manifest라면 QStar 문법상 `.qsm` 의미에 더 가깝다.

새 QStar integration은 `config.qst`를 정본 import 대상으로 사용하지 않는다.
다음 경로를 사용한다.

```lua
local config = qstar.import_module("build/generated/config/delos/release/config")
```

`config.qst`는 다음 조건을 만족하는 동안에만 유지한다.

- 기존 사용자와 golden output 호환을 깨지 않는다.
- 문서에서는 deprecated compatibility artifact라고 표시한다.
- 새 Delos/Parus wiring 예시는 `config/config.qsm`을 사용한다.

향후 `config.qst`를 진짜 `.qst` graph fragment로 바꾸는 경우에는
`qstar.config`, `qstar.group`, target-local option 같은 graph declaration만
담아야 하며, pure table manifest와 같은 역할을 하면 안 된다.

## Project-Specific Build Selection Module

Project-specific build selection은 Confit core에 Delos/Parus board logic을
넣지 않고 template 기반 artifact로 생성한다. Delos 예시는 다음 path다.

```text
<out>/delos_build_selection/delos_build_selection.qsm
```

QStar import 예:

```lua
local selection = qstar.import_module(
  "build/generated/config/delos/release/delos_build_selection"
)
```

Module 예:

```lua
return {
  schema = "delos-build-selection-v1",
  project = "delos",
  profile = "release",
  target = "renode-nucleo-h753zi",
  source_hash = "0x0123456789ABCDEF",

  arch = {
    id = "armv7m",
    cpu = "cortex-m7",
    toolchain = "arm-none-eabi",
  },

  board = {
    id = "nucleo-h753zi",
    family = "stm32h7",
    objects = {
      "//src/board/armv7m/stm32h7/nucleo-h753zi:board_objects",
    },
    include_dirs = {
      "src/board/armv7m/stm32h7/nucleo-h753zi",
    },
    linker_script = "linker/armv7m/nucleo-h753zi.ld",
  },

  config = {
    header = "config.h",
    report_json = "config.report.json",
  },

  claim = {
    level = "renode-probe",
    kind = "renode:nucleo-h753zi",
  },
}
```

`objects`는 `qstar.config`에 넣지 않는다. QStar 문법상 `qstar.config`는
compile/link option bundle이며 `objects`, `sources`, generated output을 담을
수 없다. `selection.board.objects`는 실제 executable/staticlib/objectlib/test
target의 `objects = { ... }` 또는 target-local field에서 소비한다.

## CMake Parity

QStar module과 CMake fragment는 같은 resolved config에서 생성되어야 한다.
따라서 이후 `config.cmake`도 resolved value와 build selection을 같은 prefix로
노출한다.

```cmake
set(DELOS_CONFIG_TARGET_ARCH "armv7m")
set(DELOS_CONFIG_TARGET_BOARD "nucleo-h753zi")
set(DELOS_CONFIG_TARGET_CPU "cortex-m7")
set(DELOS_CONFIG_TARGET_ARCH_SOURCE "targets/renode-nucleo-h753zi.toml")
```

QStar와 CMake selector를 사람이 따로 관리하는 방식은 금지한다.

## Non-Goals

- Confit은 Delos/Parus root `qstar.lua`를 자동 수정하지 않는다.
- Confit core는 Delos board path나 linker script 규칙을 하드코딩하지 않는다.
- `.qsm` module은 QStar graph declaration을 하지 않는다.
- Runtime image에는 Confit parser, TUI, QStar manifest loader가 들어가지 않는다.
