---
doc_type: developer-guide
status: draft
authority: operational
last_verified: 2026-08-08
---

# Local Build And Test

Confit은 Delos runtime build와 분리된 host-side tool이다. Confit 자체 build/test harness는
Confit source tree 안에서만 정의하고, build output은 source tree 밖 임시 디렉터리에 둔다.
정본 build language는 pinned `bmake` 20240909다. CMake는 migration 기간의 결과 비교기이며 Confit의
normal build나 Parus bootstrap authority가 아니다. macOS/Linux TUI frontend는 실제
curses/ncurses library에 link한다. `CONFIT_ENABLE_TUI=no`는 같은 parser, resolver와 generator를
사용하고 frontend만 unsupported stub으로 바꾼다.

Standalone repository root에서는 Confit source path가 `.`이다. Delos subtree checkout에서는 같은 source
path가 `tools/confit`이다.

## Build Dependencies

Confit bmake graph는 filesystem scan 없이 `share/mk/confit.sources.mk`의 명시적 semantic source
group만 소비한다. `tests/check_bmake_manifest.sh`의 scan은 누락을 검출하는 lint일 뿐 build source
selection이 아니다. `vendor/`에는 TUI shim을 두지 않는다.

필수 항목:

```text
pinned bmake 20240909
C17 compiler
system curses/ncurses headers and library (macOS/Linux TUI build only)
/bin/sh for Unix integration scripts
CMake >= 3.20 (legacy artifact/comparator tests only)
```

Platform별 확인 사항:

| Platform | Dependency note |
|---|---|
| macOS | Xcode Command Line Tools 또는 Xcode SDK의 curses header/library가 필요하다. |
| Linux | 배포판 개발 package가 필요하다. 예: Debian/Ubuntu `libncurses-dev`, Fedora `ncurses-devel`, Arch `ncurses`. |
| Windows | 기존 CMake CLI-only lane은 migration comparator다. bmake Windows support는 아직 executed claim이 아니다. |

TUI dependency가 없는 host는 `CONFIT_ENABLE_TUI=no`로 core CLI를 빌드한다. TUI availability가
parser/resolver/generator의 build를 막아서는 안 된다.

## Canonical bmake Gate

라운드별 기본 local gate는 다음 명령이다.

```sh
# Parus checkout: normal core CLI bootstrap (no legacy comparator tools)
tools/build/bootstrap/parus-bmake confit

# Standalone Confit checkout: exact verified bmake path를 사용한다.
/absolute/path/to/bmake -r -C . -f Makefile \
  CONFIT_OBJROOT=/tmp/confit-build \
  CONFIT_HOST_CC=/usr/bin/cc \
  CONFIT_HOST_CC_FAMILY=clang \
  CONFIT_LEGACY_CMAKE=/absolute/path/to/cmake check
```

이 gate는 다음 순서로 동작한다.

1. explicit source manifest가 실제 C source inventory를 완전히 닫는지 검사한다.
2. fixed host compiler로 core CLI와 test binary를 output root 아래에 build한다.
3. unit, bounded fuzz, CLI, TUI/unsupported와 integration tests를 직접 실행한다.
4. C 기반 integration runner가 shell 없이 `confit` child process를 실행해 stdout/stderr와 exit code를
   검증한다.
5. CLI-only lane은 bmake를 재귀 호출하되 별도 output root와 unsupported TUI backend를 사용한다.

Gate에는 synthetic scale test도 포함된다. 이 test는 build directory 안에 5,000개 option을 가진 임시
project를 생성하고 `check`, `list`, `graph`, `gen`을 순서대로 실행한다.

Schema V2의 bounded fuzz, import depth, incremental reconcile, sanitizer 실행
방법과 release-size stress 범위는 [v2-hardening.md](v2-hardening.md)를 정본으로
따른다. 기본 CTest의 빠른 regression 규모와 dedicated high-memory release stress
규모를 같은 보장으로 취급하면 안 된다.

## Legacy CMake Comparator

`CMakeLists.txt`, `tests/run_tests.sh`와 Windows preview CI는 bmake cutover의 semantic parity를
비교하기 위해 일시적으로 남아 있다. 이 경로의 통과는 dual canonical backend를 의미하지 않는다.
Parus가 Confit을 bootstrap할 때는 CMake나 QStar executable을 탐색하거나 실행하지 않는다.

Windows CTest lane에서는 POSIX shell integration tests를 등록하지 않는다. 대신 C 기반
`confit_test_cli_workflow`가 child process로 CLI command를 실행하고, `doctor`가 Windows clang-only
CLI lane을 보고하는지와 `confit tui`가 exit code `8`로 실패하는지를 검증한다.

GitHub Actions의 `Confit CI`는 push와 pull request마다 `windows-latest`에서
MSYS2 `CLANG64` 기반 Windows preview job을 실행한다. 이 job은 `CONFIT_ENABLE_TUI=ON`을
일부러 전달해도 CMake가 Windows lane을 `OFF`로 강제하는지 확인하고, CTest 이후 다음 CLI smoke를
명시적으로 수행한다.

```text
confit doctor
confit check --project tests/fixtures/realish/delos --profile sim-dsh
confit gen --project tests/fixtures/realish/delos --profile sim-dsh --artifact all
generated artifact existence checks
confit tui ...  # exit code 8 unsupported
CMake install smoke for install-windows-ci/bin/confit.exe
```

macOS/Linux 기본 CTest에는 `confit.cli.cli_only_lane`도 포함된다. 이 test는
별도 `-DCONFIT_ENABLE_TUI=OFF` build를 만들고, no-TUI build에서 `confit tui`가
exit code `8`로 실패하는지 확인한다. macOS/Darwin host에서는
`-DCMAKE_SYSTEM_NAME=Windows` configure smoke도 실행해 Windows에서
`CONFIT_ENABLE_TUI=OFF`가 강제되는지 확인한다. Linux push CI에서는 이
cross-configure smoke를 건너뛰고 no-TUI unsupported path만 검증한다.

기본 build directory:

```text
${TMPDIR:-/tmp}/confit-build
```

다른 build directory를 쓰려면 첫 번째 인자로 넘긴다.

```sh
# Standalone Confit repository root
./tests/run_tests.sh /tmp/confit-custom-build

# Delos subtree checkout
tools/confit/tests/run_tests.sh /tmp/confit-custom-build
```

## Manual bmake Commands

수동으로 같은 과정을 나누어 실행할 수 있다.

```sh
BMAKE=/absolute/path/to/verified/bmake
"$BMAKE" -r -C . -f Makefile CONFIT_OBJROOT=/tmp/confit-build all
/tmp/confit-build/bin/confit --version
/tmp/confit-build/bin/confit help
"$BMAKE" -r -C . -f Makefile \
  CONFIT_OBJROOT=/tmp/confit-cli-only CONFIT_ENABLE_TUI=no all
```

Windows native CLI-only 확인 예시는 다음과 같다.

```sh
cmake -S . -B build/confit -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_BUILD_TYPE=Release \
  -DCONFIT_ENABLE_TUI=ON
grep -Fx "CONFIT_ENABLE_TUI:BOOL=OFF" build/confit/CMakeCache.txt
cmake --build build/confit --target confit
ctest --test-dir build/confit --output-on-failure
build/confit/confit.exe doctor
```

Windows에서 `CONFIT_ENABLE_TUI=ON`을 넘겨도 CMake는 CLI-only lane을 보호하기
위해 TUI를 `OFF`로 강제한다.

## Cutover Dry-Run

실제 Parus/Delos build tree에 generated artifact를 연결하기 전에는 fixture mirror 기반 dry-run을 먼저
수행한다.

```sh
tools/confit/scripts/confit-cutover-dry-run.sh \
  --project delos-realish \
  --out /tmp/confit-cutover

tools/confit/scripts/confit-cutover-dry-run.sh \
  --project parus-realish \
  --out /tmp/confit-cutover
```

이 명령은 `check`, `strict check`, `compat`, `gen`, golden diff, input manifest 검증을 수행하고
`CUTOVER_SUMMARY.txt`와 `ROLLBACK.md`를 output directory에 남긴다. 자세한 절차는
`docs/cutover-dry-run.md`와 `docs/rollback.md`를 따른다.

## Local Install

Confit의 설치 규칙은 단순하다. 실행 시 필요한 산출물은 플랫폼별 단일 바이너리 하나이고,
문서 산출물은 플랫폼 정책에 따라 제공한다. 설치는 어떤 project `config/` tree도 생성하거나
수정하지 않는다.

### macOS/Linux 설치

```text
<prefix>/bin/confit
```

macOS/Linux의 사용자 문서 산출물은 manpage다.

```text
<prefix>/share/man/man1/confit.1
```

local checkout에서 설치하려면 다음 명령을 사용한다. 이 스크립트는 POSIX shell을 사용하는
macOS/Linux용 installer다.

```sh
# Standalone Confit repository root
scripts/install-local.sh --prefix ~/.local

# Delos subtree checkout
tools/confit/scripts/install-local.sh --prefix ~/.local
```

이 스크립트는 network를 사용하지 않는다. source tree 밖 임시 build directory에서 `confit` target을
빌드하고, CMake install rule로 `<prefix>/bin/confit`과
`<prefix>/share/man/man1/confit.1`을 설치한다.

같은 동작을 수동으로 수행하면 다음과 같다.

```sh
CONFIT_SRC=.
# Delos subtree checkout에서는 다음 값을 사용한다.
# CONFIT_SRC=tools/confit

cmake -S "$CONFIT_SRC" -B /tmp/confit-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/confit-build --target confit
cmake --install /tmp/confit-build --prefix "$HOME/.local"
```

설치 후 기본 확인:

```sh
~/.local/bin/confit --version
~/.local/bin/confit doctor
man confit
```

`confit doctor`는 설치 규칙을 다음처럼 노출해야 한다.

```text
install rule: single executable artifact: <prefix>/bin/confit
```

### Windows preview 설치

Windows는 현재 CLI-only preview lane이다. TUI는 빌드하지 않고, 설치 산출물도 단일 실행 파일
하나로 제한한다.

```text
<prefix>/bin/confit.exe
```

지원 compiler는 GNU-style Clang이다. MSVC와 `clang-cl`은 지원하지 않는다. Windows preview에서는
PowerShell install script를 아직 제공하지 않는다. 복잡한 installer를 만들기 전에 CLI 동작, path
직렬화, generated artifact parity를 먼저 안정화하기 위한 결정이다.

Windows에서 local preview를 구성하려면 CMake로 빌드한 뒤 실행 파일만 복사한다.

```powershell
cmake -S . -B build/confit-windows -G Ninja `
  -DCMAKE_C_COMPILER=clang `
  -DCMAKE_BUILD_TYPE=Release `
  -DCONFIT_ENABLE_TUI=OFF
cmake --build build/confit-windows --target confit
New-Item -ItemType Directory -Force "$env:USERPROFILE\.local\bin"
Copy-Item build\confit-windows\confit.exe "$env:USERPROFILE\.local\bin\confit.exe" -Force
& "$env:USERPROFILE\.local\bin\confit.exe" doctor
```

Windows 문서와 manpage는 설치 위치로 복사하지 않고 repository checkout의 문서로 제공한다.

```text
docs/*.md
wiki/*.md
man/confit.1
```

Windows `confit doctor`는 설치 규칙을 다음처럼 노출해야 한다.

```text
install rule: single executable artifact: <prefix>/bin/confit.exe
```

Windows에서 `confit tui`를 실행하면 partial UI를 시도하지 않고 exit code `8`
(`unsupported command or platform`)로 실패해야 한다.

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
