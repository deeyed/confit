# 11. Parus/Delos 적용 가이드

이 문서는 Confit을 실제 Parus/Delos에 적용할 때의 권장 순서다. 현재 Confit은 fixture-backed
release-candidate이며, 실제 source tree adoption은 별도 integration 작업으로 진행해야 한다.

## 전체 단계

```text
1. fixture mirror 검토
2. 실제 Parus/Delos config source tree 추가
3. CI에 check/compat/gen --dry-run 추가
4. generated artifact output 위치 확정
5. config.h 소비 code 연결
6. config.cmake와 config/config.qsm build graph 연결
7. rollback 절차 검증
```

각 단계는 별도 commit이나 review 단위로 나누는 것이 좋다.

## 1단계: fixture mirror 검토

현재 reference는:

```text
tools/confit/tests/fixtures/realish/delos
tools/confit/tests/fixtures/realish/parus
tools/confit/tests/fixtures/realish/compat
```

검증:

```sh
confit check --project tools/confit/tests/fixtures/realish/delos --profile sim-dsh --strict
confit check --project tools/confit/tests/fixtures/realish/parus --profile qemu-aarch64 --strict
confit compat \
  --parus tools/confit/tests/fixtures/realish/parus \
  --delos tools/confit/tests/fixtures/realish/delos \
  --profile parus-delos-debug \
  --compat tools/confit/tests/fixtures/realish/compat
```

## 2단계: 실제 config source tree 추가

실제 project에는 다음 구조를 추가한다.

```text
config/
  project.toml
  options/
  profiles/
  targets/
  compat/
```

주의:

- generated file을 `config/` 아래에 넣지 않는다.
- profile은 처음에는 적게 만든다.
- release/debug/sim/bringup처럼 실제로 쓰는 profile부터 시작한다.
- schema metadata(`owner`, `since`, `stability`)를 빠뜨리지 않는다.

## 3단계: CI validation 추가

처음 CI에는 generated artifact를 build에 연결하지 말고 validation만 넣는다.

```sh
confit doctor --project config/confit
confit check --project config/confit --profile sim-dsh --strict
confit graph --project config/confit --profile sim-dsh --format json >/tmp/confit-graph.json
```

Parus/Delos 함께:

```sh
confit compat \
  --parus ../parus/config/confit \
  --delos config/confit \
  --profile parus-delos-debug \
  --compat config/compat
```

## 4단계: generated output 위치 확정

추천:

```text
build/generated/config/<project>/<profile>/
```

예:

```sh
confit gen \
  --project config/confit \
  --profile sim-dsh \
  --out build/generated/config/delos/sim-dsh \
  --artifact all
```

## 5단계: config.h 연결

C source는 generated include directory를 통해 `config.h`를 include한다.

예상 CMake 방향:

```cmake
include("${CMAKE_BINARY_DIR}/generated/config/delos/sim-dsh/config.cmake")
get_filename_component(DELOS_CONFIG_INCLUDE_DIR "${CONFIT_CONFIG_HEADER}" DIRECTORY)
target_include_directories(delos_runtime PRIVATE "${DELOS_CONFIG_INCLUDE_DIR}")

if(DELOS_CONFIG_TARGET_BOARD STREQUAL "host-sim")
  target_sources(delos_runtime PRIVATE src/board/sim/board.c)
endif()
```

C code:

```c
#include "config.h"

#if DELOS_CONFIG_DEBUG_DSH
/* debug shell path */
#endif
```

## 6단계: CMake/QStar 연결

Confit은 `config.cmake`와 QStar용 canonical pure module
`config/config.qsm`을 만들지만, build graph를 자동으로 수정하지 않는다.
기존 `config.qst`는 compatibility artifact로 남는다. 실제 연결은 별도
review에서 한다.

QStar 연결 예:

```lua
local config = qstar.import_module(
  "build/generated/config/delos/sim-dsh/config"
)
```

`qstar.import_module(".../config")`는
`.../config/config.qsm`을 읽는다. `.qsm` file path를 직접 넘기지 않는다.

review checklist:

- generated path가 build tree 아래인가.
- profile 이름이 CI matrix와 일치하는가.
- generated artifact가 source control에 들어가지 않는가.
- QStar graph가 `config.qst`가 아니라 `config/config.qsm`을 import하는가.
- rollback이 단순한가.

## 7단계: rollback

문제가 생기면:

1. generated output directory를 삭제한다.
2. build graph에서 Confit include를 되돌린다.
3. source `config/` change를 revert한다.
4. 기존 build path로 돌아간다.

rollback 문서는 다음을 따른다.

```text
tools/confit/docs/rollback.md
```

## 도입 완료 기준

다음이 모두 참이면 “Confit을 실사용 중”이라고 말할 수 있다.

- 실제 Parus/Delos `config/` source tree가 있다.
- CI에서 `confit check`가 돈다.
- Parus/Delos 조합에 대해 `confit compat`가 돈다.
- generated artifact가 build tree 아래에 생긴다.
- `config.h`가 실제 C source build에 들어간다.
- build graph wiring이 review되었다.
- rollback 절차가 문서화되어 있다.
