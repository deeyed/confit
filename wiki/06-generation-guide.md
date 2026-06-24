# 06. 생성물 가이드

`confit gen`은 resolved config를 build가 소비할 artifact로 바꾼다. Confit은 source tree를 몰래 수정하지
않고, 반드시 `--out`으로 지정한 directory에만 생성한다.

## 기본 명령

```sh
confit gen \
  --project tools/confit/tests/fixtures/realish/delos \
  --profile sim-dsh \
  --out /tmp/confit-generated/delos/sim-dsh \
  --artifact all
```

생성 결과:

```text
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

Delos build graph wiring을 검증할 때는 board/CPU/linker selection이 있는
target을 지정한다.

```sh
confit gen \
  --project fixtures/delos \
  --profile release \
  --target renode-nucleo-h753zi \
  --out build/generated/config/delos/release \
  --artifact all
```

Delos monorepo fixture에서는 project path만 다음처럼 바꾼다.

```sh
confit gen \
  --project tools/confit/tests/fixtures/realish/delos \
  --profile release \
  --target renode-nucleo-h753zi \
  --out build/generated/config/delos/release \
  --artifact all
```

## artifact 선택

전체 생성:

```sh
confit gen --project <project> --profile <profile> --out <out> --artifact all
```

header만 생성:

```sh
confit gen --project <project> --profile <profile> --out <out> --artifact header
```

reports만 생성:

```sh
confit gen --project <project> --profile <profile> --out <out> --artifact reports
```

CMake fragment만 생성:

```sh
confit gen --project <project> --profile <profile> --out <out> --artifact cmake
```

QStar core module과 compatibility artifact만 생성:

```sh
confit gen --project <project> --profile <profile> --out <out> --artifact qstar
```

Build selection module만 생성:

```sh
confit gen --project <project> --profile <profile> --out <out> --artifact build-selection
```

## `--dry-run`

실제로 쓰지 않고 무엇을 쓸지 확인한다.

```sh
confit gen \
  --project tools/confit/tests/fixtures/realish/delos \
  --profile sim-dsh \
  --out /tmp/confit-generated/delos/sim-dsh \
  --artifact all \
  --dry-run
```

source adoption이나 CI 작업 전에는 먼저 `--dry-run`을 돌리는 습관이 좋다.

## `--force`

이미 output file이 있으면 Confit은 기본적으로 덮어쓰기를 거부한다.

```sh
confit gen \
  --project <project> \
  --profile <profile> \
  --out <out> \
  --artifact all
```

덮어쓰려면 명시적으로 `--force`를 쓴다.

```sh
confit gen \
  --project <project> \
  --profile <profile> \
  --out <out> \
  --artifact all \
  --force
```

## config.h

`config.h`는 C source가 include하는 compile-time define을 담는다.

예시:

```c
#define DELOS_CONFIG_DEBUG_DSH 1
#define DELOS_CONFIG_TARGET_BOARD "host-sim"
#define DELOS_CONFIG_SCHEDULER_TASK_SLOTS 1U
#define DELOS_CONFIG_SOURCE_HASH 0x8080CE8307BD0C47ULL
```

특징:

- timestamp를 넣지 않는다.
- option id 순서를 stable하게 유지한다.
- project별 define prefix를 사용한다.
- source hash를 넣어 generated output 추적을 돕는다.

## config.report.json

machine-readable report다. CI, dashboard, review bot이 읽기 좋다.

사용 예:

```sh
jq '.values[] | select(.id == "delos.debug.dsh")' \
  /tmp/confit-generated/delos/sim-dsh/config.report.json
```

## config.explain.txt

사람이 읽는 explanation report다. option별 값과 이유를 훑어볼 때 쓴다.

```sh
less /tmp/confit-generated/delos/sim-dsh/config.explain.txt
```

## config.graph.json

dependency graph dump다. option graph를 분석하거나 시각화하기 전단계로 쓴다.

```sh
jq '.nodes | length' /tmp/confit-generated/delos/sim-dsh/config.graph.json
```

## config.inputs.json

어떤 input TOML이 artifact 생성에 들어갔는지 기록한다. review와 reproducibility에서 중요하다.

```sh
jq '.inputs[].path' /tmp/confit-generated/delos/sim-dsh/config.inputs.json
```

## config.cmake

CMake가 명시적으로 include할 수 있는 generated fragment다. Confit이 CMake graph를 자동으로 수정하지는
않는다. Fragment에는 artifact path와 resolved option 값이 함께 들어간다.

예상 사용 방식:

```cmake
include("${CMAKE_BINARY_DIR}/generated/config/delos/sim-dsh/config.cmake")
get_filename_component(DELOS_CONFIG_INCLUDE_DIR "${CONFIT_CONFIG_HEADER}" DIRECTORY)
target_include_directories(delos_runtime PRIVATE "${DELOS_CONFIG_INCLUDE_DIR}")

if(DELOS_CONFIG_TARGET_BOARD STREQUAL "host-sim")
  target_sources(delos_runtime PRIVATE src/board/sim/board.c)
endif()
```

Delos release target selection을 CMake에서 확인하는 최소 예시는 다음과 같다.

```cmake
include("${CMAKE_BINARY_DIR}/generated/config/delos/release/config.cmake")
message(STATUS "board=${DELOS_CONFIG_TARGET_BOARD}")
message(STATUS "board-source=${DELOS_CONFIG_TARGET_BOARD_SOURCE}")
```

주요 변수 예:

```cmake
set(DELOS_CONFIG_TARGET_BOARD "host-sim")
set(DELOS_CONFIG_TARGET_BOARD_TYPE "enum")
set(DELOS_CONFIG_TARGET_BOARD_VALUE "host-sim")
set(DELOS_CONFIG_TARGET_BOARD_TEXT "host-sim")
set(DELOS_CONFIG_TARGET_BOARD_SOURCE "profiles/sim-dsh.toml")
```

실제 build graph wiring은 별도 integration review에서 한다.

## config/config.qsm

QStar graph가 읽을 수 있는 canonical pure module이다. Confit이 QStar graph를
자동으로 수정하지는 않는다. QStar에서는 `.qsm` file path가 아니라 module
folder path를 넘긴다.

예상 사용 방식:

```lua
local config = qstar.import_module(
  "build/generated/config/delos/sim-dsh/config"
)
```

이 호출은 실제로
`build/generated/config/delos/sim-dsh/config/config.qsm`을 읽는다.

`config/config.qsm`은 resolved value table과 generated artifact path를 함께
담는다. Delos/Parus QStar graph는 이 table을 읽어서 모든 resolved option을
조회할 수 있다. Board object label, include directory, linker script처럼
build graph 선택에 바로 쓰는 project-specific table은
`--artifact build-selection`이 생성하는 별도 module을 import한다.

Delos release target selection 예시는 다음과 같다.

```lua
local config = qstar.import_module("build/generated/config/delos/release/config")
local selection = qstar.import_module(
  "build/generated/config/delos/release/delos_build_selection"
)
```

`config.values["delos.target.board"].value`는 resolved value table에서 board
값을 읽는 경로다. `selection.board.objects`,
`selection.board.include_dirs`, `selection.board.linker_script`는 build graph가
바로 쓰기 좋게 template으로 재구성한 값이다.

기존 `config.qst`는 compatibility artifact로 남긴다. 새 integration에서
정본 import 대상으로 삼지 않는다.

더 자세한 QStar/CMake 연결 절차는 `docs/build-selection-workflow.md`를
정본으로 따른다.

## Generated output review checklist

```sh
confit check --project <project> --profile <profile> --strict
confit gen --project <project> --profile <profile> --out <out> --artifact all --dry-run
confit gen --project <project> --profile <profile> --out <out> --artifact all
test -s <out>/config.h
test -s <out>/config.inputs.json
```

review할 내용:

- `config.h` define prefix가 project에 맞는가.
- `config.inputs.json`에 예상 input만 들어갔는가.
- generated path가 source tree가 아니라 build/output tree인가.
- `config.cmake`와 `config/config.qsm`을 실제 build graph에 연결하는 change가 별도 review 대상인가.
- 기존 `config.qst`를 새 QStar integration의 정본으로 사용하지 않는가.
