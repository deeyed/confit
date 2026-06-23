---
doc_type: developer-guide
status: draft
authority: operational
last_verified: 2026-06-23
---

# Local Build And Test

Confit은 Delos runtime build와 분리된 host-side tool이다. Confit 자체 build/test harness는
`tools/confit/` 안에서만 정의하고, build output은 source tree 밖 임시 디렉터리에 둔다.
TUI frontend는 실제 curses/ncurses library에 link하므로 local build host에는 CMake가 찾을 수 있는
curses/ncurses 개발 파일이 있어야 한다.

## Build Dependencies

Confit TUI는 `tools/confit/CMakeLists.txt`에서 `find_package(Curses REQUIRED)`로 system curses/ncurses를
찾는다. `vendor/`에는 TUI shim을 두지 않는다.

필수 항목:

```text
CMake >= 3.20
C17 compiler
system curses/ncurses headers and library
/bin/sh for integration scripts
```

Platform별 확인 사항:

| Platform | Dependency note |
|---|---|
| macOS | Xcode Command Line Tools 또는 Xcode SDK의 curses가 CMake에 잡혀야 한다. Round 11에서는 `/Applications/Xcode.app/.../libcurses.tbd`가 감지됐다. |
| Linux | 배포판 개발 package가 필요하다. 예: Debian/Ubuntu `libncurses-dev`, Fedora `ncurses-devel`, Arch `ncurses`. |
| Windows | native Windows curses build는 아직 release gate가 아니다. MSVC flag는 준비되어 있지만 `/bin/sh` integration wrapper와 curses provider 정책이 더 필요하다. |

의존성 탐지 실패 시 `cmake -S tools/confit -B /tmp/confit-build` 단계에서 Curses package 오류가 난다.

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
5. C 기반 integration runner가 shell 없이 `confit` child process를 실행해 stdout/stderr와 exit code를
   검증한다.
6. Round 1 smoke script를 실행해 직접 compiler 기반 smoke도 유지한다.

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
/tmp/confit-build/confit_test_cli_workflow
```

## Local Install

Confit의 필수 설치 산출물은 단일 실행 파일이다.

```text
<prefix>/bin/confit
```

local checkout에서 설치하려면 다음 명령을 사용한다.

```sh
tools/confit/scripts/install-local.sh --prefix ~/.local
```

이 스크립트는 network를 사용하지 않는다. source tree 밖 임시 build directory에서 `confit` target을
빌드하고, CMake install rule로 `<prefix>/bin/confit`을 설치한다. 설치 과정은 어떤 project `config/`
tree도 생성하거나 수정하지 않는다.

같은 동작을 수동으로 수행하면 다음과 같다.

```sh
cmake -S tools/confit -B /tmp/confit-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/confit-build --target confit
cmake --install /tmp/confit-build --prefix "$HOME/.local"
```

설치 후 기본 확인:

```sh
~/.local/bin/confit --version
~/.local/bin/confit doctor
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
