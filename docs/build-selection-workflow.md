---
doc_type: user-guide
status: accepted
authority: normative
last_verified: 2026-06-24
---

# Build Selection Workflow

이 문서는 Confit generated artifact를 Delos/Parus build graph에 연결하는
정본 흐름이다. 목표는 Confit TUI나 profile에서 선택한 target, board, CPU,
driver, linker 값을 `config.h`뿐 아니라 QStar와 CMake build selection에도
같은 값으로 전달하는 것이다.

Confit은 build graph를 몰래 수정하지 않는다. 사용자는 `confit gen`으로
generated directory를 만들고, QStar/CMake 쪽에서 그 directory를 명시적으로
읽는다.

## 1. Generate

Delos 팀이 project fixture 또는 실제 config source를 `fixtures/delos`에 둔
경우의 canonical command는 다음과 같다.

```sh
confit gen \
  --project fixtures/delos \
  --profile release \
  --target renode-nucleo-h753zi \
  --out build/generated/config/delos/release \
  --artifact all
```

현재 Confit repository 안의 realish fixture로 같은 흐름을 확인할 때는 project
path만 다음처럼 바꾼다.

```sh
confit gen \
  --project tools/confit/tests/fixtures/realish/delos \
  --profile release \
  --target renode-nucleo-h753zi \
  --out build/generated/config/delos/release \
  --artifact all
```

이미 output file이 있으면 Confit은 기본적으로 덮어쓰지 않는다. CI나 반복
실행에서는 output directory를 지우거나 `--force`를 명시한다.

```sh
confit gen \
  --project fixtures/delos \
  --profile release \
  --target renode-nucleo-h753zi \
  --out build/generated/config/delos/release \
  --artifact all \
  --force
```

## 2. Generated Files

`--artifact all`은 다음 artifact를 만든다.

```text
build/generated/config/delos/release/
  config.h
  config.report.json
  config.explain.txt
  config.graph.json
  config.inputs.json
  config.cmake
  config/
    config.qsm
  config.qst
  delos_build_selection/
    delos_build_selection.qsm
```

각 파일의 역할은 다음과 같다.

| Artifact | 역할 |
|---|---|
| `config.h` | C source가 include하는 compile-time define |
| `config.report.json` | CI, review bot, dashboard가 읽는 resolved value report |
| `config.explain.txt` | 사람이 값의 출처와 dependency를 읽는 explanation |
| `config.graph.json` | option dependency graph dump |
| `config.inputs.json` | 어떤 TOML input이 사용됐는지 기록한 manifest |
| `config.cmake` | CMake가 include하는 resolved value fragment |
| `config/config.qsm` | QStar가 import하는 canonical resolved config module |
| `config.qst` | deprecated compatibility artifact |
| `delos_build_selection/delos_build_selection.qsm` | Delos build graph가 소비하는 board/CPU/linker selection module |

새 QStar wiring은 `config.qst`를 import하지 않는다. `config.qst`는 기존
사용자와 golden output 호환을 위해 남긴 compatibility artifact다.

## 3. QStar Import

QStar에서 `.qsm` file path를 직접 import하지 않는다. QStar의 module import는
folder path를 받고, 그 안의 같은 이름 `.qsm` 파일을 읽는다.

```lua
local config = qstar.import_module("build/generated/config/delos/release/config")
local selection = qstar.import_module(
  "build/generated/config/delos/release/delos_build_selection"
)
```

위 호출은 실제로 다음 파일을 읽는다.

```text
build/generated/config/delos/release/config/config.qsm
build/generated/config/delos/release/delos_build_selection/delos_build_selection.qsm
```

`config` module은 모든 resolved option을 table로 제공한다.

```lua
local board = config.values["delos.target.board"].value
local arch = config.values["delos.target.arch"].value
local trace_capacity = config.values["delos.trace.capacity"].value
```

`selection` module은 build graph가 바로 소비하기 쉬운 project-specific
selection table이다.

```lua
local board_id = selection.board.id
local board_objects = selection.board.objects
local include_dirs = selection.board.include_dirs
local linker_script = selection.board.linker_script
```

QStar 문법상 `qstar.config`는 compile/link option bundle이다. 따라서
`selection.board.objects`는 `qstar.config`에 넣지 않고, 실제 executable,
static library, object library, test target의 `objects = { ... }` 또는
target-local field에서 소비한다. Include directory와 linker script는 project의
QStar graph 규칙에 맞춰 compile/link option으로 변환할 수 있다.

예시 wiring:

```lua
local config = qstar.import_module("build/generated/config/delos/release/config")
local selection = qstar.import_module(
  "build/generated/config/delos/release/delos_build_selection"
)

qstar.config "delos_generated_board_config" {
  lang = {
    c = {
      public_include_dirs = selection.board.include_dirs,
      compile_options = {
        "-DDELOS_CONFIG_HEADER=\"" .. selection.config.header .. "\"",
      },
    },
  },
  link_options = {
    "-T" .. selection.board.linker_script,
  },
}

qstar.executable "delos_image" {
  objects = selection.board.objects,
  configs = {"//:delos_generated_board_config"},
}
```

Project별 QStar helper와 target field는 실제 graph 규칙에 맞춰 조정할 수
있다. 위 예시는 QStar 문서 기준으로 `qstar.config "name" { ... }`,
`qstar.executable "name" { ... }`, `lang.c.public_include_dirs`,
`lang.c.compile_options`, target-local `objects = { ... }` 표면을 사용한다.
중요한 계약은 Confit이 `config`와 `selection` table을 stable하게 생성하고,
QStar graph가 그 값을 단일 source of truth로 읽는다는 점이다.

## 4. CMake Include

CMake는 generated `config.cmake`를 include한다.

```cmake
include("${CMAKE_BINARY_DIR}/generated/config/delos/release/config.cmake")
message(STATUS "board=${DELOS_CONFIG_TARGET_BOARD}")
```

`config.cmake`에는 artifact path와 resolved option value가 같이 들어간다.

```cmake
set(CONFIT_PROJECT "delos")
set(CONFIT_PROFILE "release")
set(CONFIT_TARGET "renode-nucleo-h753zi")
set(CONFIT_CONFIG_HEADER "${CMAKE_CURRENT_LIST_DIR}/config.h")

set(DELOS_CONFIG_TARGET_BOARD "nucleo-h753zi")
set(DELOS_CONFIG_TARGET_BOARD_TYPE "enum")
set(DELOS_CONFIG_TARGET_BOARD_VALUE "nucleo-h753zi")
set(DELOS_CONFIG_TARGET_BOARD_TEXT "nucleo-h753zi")
set(DELOS_CONFIG_TARGET_BOARD_SOURCE "targets/renode-nucleo-h753zi.toml")
```

CMake selector는 direct value variable을 쓰고, review나 diagnostic은
`_TYPE`, `_VALUE`, `_TEXT`, `_SOURCE` metadata variable을 함께 볼 수 있다.
QStar와 CMake selector file을 사람이 따로 관리하지 않는다.

## 5. Review Gate

실제 Delos/Parus wiring 전에 다음 순서로 확인한다.

```sh
confit check \
  --project fixtures/delos \
  --profile release \
  --target renode-nucleo-h753zi \
  --strict

confit gen \
  --project fixtures/delos \
  --profile release \
  --target renode-nucleo-h753zi \
  --out build/generated/config/delos/release \
  --artifact all \
  --dry-run
```

Confit repository에서는 full test와 QStar smoke를 함께 돌린다.

```sh
./tests/run_tests.sh
sh tests/integration/round8_qstar_module_artifacts.sh \
  /tmp/confit-build/confit \
  "$(pwd)" \
  /tmp/confit-qstar-smoke
git diff --check
```

`tests/run_tests.sh`는 stress test를 포함한다. QStar smoke는 사용 가능한
`qstar` binary가 없으면 skip될 수 있으므로, release gate에서는 다음처럼
`CONFIT_QSTAR_BIN`을 지정하는 것이 좋다.

```sh
CONFIT_QSTAR_BIN=/path/to/qstar \
  sh tests/integration/round8_qstar_module_artifacts.sh \
  /tmp/confit-build/confit \
  "$(pwd)" \
  /tmp/confit-qstar-smoke
```

## 6. Migration Rules

- Source `config/` tree에는 사람이 관리하는 TOML만 둔다.
- Generated output은 build tree 아래에 둔다.
- 새 QStar integration은 `config/config.qsm`과 build selection module을 import한다.
- `config.qst`는 새 wiring의 정본 import 대상이 아니다.
- Confit core는 Delos board path, linker naming, QStar target naming을
  하드코딩하지 않는다.
- Runtime image에는 Confit parser, TUI, QStar manifest loader가 들어가지 않는다.

## 7. Troubleshooting

QStar import가 실패하면 먼저 import path가 folder path인지 확인한다.

```lua
-- 맞음
qstar.import_module("build/generated/config/delos/release/config")

-- 틀림
qstar.import_module("build/generated/config/delos/release/config/config.qsm")
```

Build selection module이 없으면 `selection/*.toml` template이 있는지와
`--artifact all` 또는 `--artifact build-selection`을 썼는지 확인한다.

CMake variable이 비어 있으면 `config.cmake`를 include한 path가 generated
directory와 일치하는지 확인한다. `DELOS_CONFIG_TARGET_BOARD_SOURCE`를 보면
해당 값이 어떤 profile 또는 target TOML에서 왔는지 알 수 있다.

Output overwrite 오류가 나면 output directory를 지우거나 `--force`를 쓴다.
CI에서는 clean build directory를 쓰는 편이 가장 단순하다.
