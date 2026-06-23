---
doc_type: developer-guide
status: draft
authority: operational
last_verified: 2026-06-23
---

# Local Build And Test

Confit은 Delos runtime build와 분리된 host-side tool이다. Confit 자체 build/test harness는
`tools/confit/` 안에서만 정의하고, build output은 source tree 밖 임시 디렉터리에 둔다.

## CI-like Local Gate

라운드별 기본 local gate는 다음 명령이다.

```sh
tools/confit/tests/run_tests.sh
```

이 script는 다음 순서로 동작한다.

1. 기존 임시 build directory를 삭제한다.
2. `cmake -S tools/confit -B <build-dir>`로 clean configure를 수행한다.
3. `cmake --build <build-dir>`로 `confit`과 unit test binary를 build한다.
4. `ctest --test-dir <build-dir> --output-on-failure`로 unit/CLI tests를 실행한다.
5. Round 1 smoke script를 실행해 직접 compiler 기반 smoke도 유지한다.

CTest에는 Round 20 synthetic scale gate도 포함된다. 이 gate는 build directory 안에 2,500개 option을
가진 임시 project를 생성하고 `check`, `list`, `graph`, `gen`을 순서대로 실행한다.

기본 build directory:

```text
${TMPDIR:-/tmp}/confit-build
```

다른 build directory를 쓰려면 첫 번째 인자로 넘긴다.

```sh
tools/confit/tests/run_tests.sh /tmp/confit-custom-build
```

## Manual Commands

수동으로 같은 과정을 나누어 실행할 수 있다.

```sh
cmake -S tools/confit -B /tmp/confit-build
cmake --build /tmp/confit-build
ctest --test-dir /tmp/confit-build --output-on-failure
/tmp/confit-build/confit --version
/tmp/confit-build/confit help
```

## Fixture Convention

Fixture는 사람이 관리하는 input project나 negative input을 담는다.

```text
tools/confit/tests/fixtures/
  delos/
  parus/
  invalid/
```

실제 Parus/Delos `config/` tree를 테스트 중 직접 수정하지 않는다. 필요한 예시는 fixture로 복사하거나
최소 재현 TOML로 작성한다.

## Golden Convention

Golden output은 deterministic output의 byte-for-byte 비교 기준이다.

```text
tools/confit/tests/golden/
  config-h/
  reports/
  explain/
  graph/
```

Golden file에는 timestamp와 absolute path를 넣지 않는다. Source path가 필요하면 fixture root 기준
상대 경로를 사용한다.

## Manual QA Notes

Computer Use 또는 사람이 직접 확인한 TUI 흐름은 다음 위치에 짧은 transcript로 남긴다.

```text
tools/confit/tests/manual/
```

Manual note는 자동 테스트를 대체하지 않는다. 자동 테스트가 다루기 어려운 실제 TUI 화면 흐름과
platform/tooling 제한을 기록하는 보조 evidence다.
