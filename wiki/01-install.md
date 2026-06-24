# 01. 설치와 확인

## 현재 checkout에서 바로 build하기

Standalone Confit repository root에서는 다음처럼 실행한다.

```sh
./tests/run_tests.sh
```

Delos repository 안의 subtree checkout에서는 다음처럼 실행한다.

```sh
tools/confit/tests/run_tests.sh
```

이 명령은 Confit을 임시 build directory에 clean build하고, unit test, CLI integration test, TUI scripted
test, stress test를 실행한다.

기본 build output은 다음 위치다.

```text
${TMPDIR:-/tmp}/confit-build/confit
```

macOS의 현재 checkout에서는 보통 다음처럼 된다.

```text
/var/folders/.../T/confit-build/confit
```

바로 확인한다.

```sh
${TMPDIR:-/tmp}/confit-build/confit --version
${TMPDIR:-/tmp}/confit-build/confit doctor
${TMPDIR:-/tmp}/confit-build/confit help
```

## 로컬 설치

Confit의 local install은 단일 실행 파일과 manpage를 설치한다.

```sh
# Standalone Confit repository root
scripts/install-local.sh --prefix ~/.local

# Delos subtree checkout
tools/confit/scripts/install-local.sh --prefix ~/.local
```

설치 결과:

```text
~/.local/bin/confit
~/.local/share/man/man1/confit.1
```

`~/.local/bin`이 PATH에 들어 있으면 다음처럼 쓸 수 있다.

```sh
confit doctor
confit help
man confit
```

PATH에 없다면 실행 파일 전체 경로를 쓴다.

```sh
~/.local/bin/confit doctor
```

## `man confit` 확인

이 machine의 `manpath`에 `~/.local/share/man`이 포함되어 있으면 설치 후 바로 다음 명령이 동작한다.

```sh
man confit
```

만약 manpage가 설치되었는데 `man confit`이 보이지 않으면 일시적으로 다음처럼 확인한다.

```sh
MANPATH="$HOME/.local/share/man:${MANPATH:-}" man confit
```

manpage 파일을 직접 확인할 수도 있다.

```sh
man ~/.local/share/man/man1/confit.1
```

## 설치가 제대로 되었는지 보는 기준

```sh
command -v confit
confit --version
confit doctor
man confit
```

`confit doctor`에서 다음을 확인한다.

```text
version: confit 0.1.0-rc1
build mode: Release 또는 unspecified
platform: Darwin 또는 Linux
platform lane: macos-cli-tui 또는 linux-cli-tui
curses: available; TUI enabled
tui: enabled
generators enabled: header, reports, cmake, qstar, build-selection
doctor ok
```

Windows에서는 현재 CLI-only lane이므로 TUI는 unsupported로 실패해야 정상이다.
Windows compiler lane은 GNU-style clang만 지원하며, MSVC와 `clang-cl`은
지원하지 않는다. Windows에서 `CONFIT_ENABLE_TUI=ON`을 넘겨도 CMake는
`CONFIT_ENABLE_TUI=OFF`로 강제한다.

Windows preview 설치 산출물은 단일 실행 파일이다.

```text
<prefix>/bin/confit.exe
```

Windows rc1 preview는 PowerShell installer를 제공하지 않는다. CMake/Ninja로
빌드한 뒤 `confit.exe`를 원하는 `bin` directory로 복사하는 것이 공식 preview
규칙이다. 문서와 manpage는 repository checkout의 `docs/`, `wiki/`,
`man/confit.1`을 읽는다.

## CI에서 확인되는 범위

`Confit CI`는 push와 pull request마다 다음 platform을 확인한다.

```text
ubuntu-latest
macos-latest
windows-latest / MSYS2 CLANG64
```

macOS/Linux job은 CLI, Unix integration tests, TUI scripted tests, stress
test를 포함한 full local gate를 실행한다. Windows job은 CLI-only preview로
`doctor`, `check`, `gen --artifact all`, generated artifact existence,
`confit tui` unsupported exit code `8`, `confit.exe` install smoke를 확인한다.

## 수동 설치

script를 쓰지 않고 CMake만으로 설치할 수도 있다.

```sh
# Standalone Confit repository root
cmake -S . -B /tmp/confit-build -DCMAKE_BUILD_TYPE=Release

# Delos subtree checkout
cmake -S tools/confit -B /tmp/confit-build -DCMAKE_BUILD_TYPE=Release

cmake --build /tmp/confit-build --target confit
cmake --install /tmp/confit-build --prefix "$HOME/.local"
```

## 제거

local install이 만든 파일만 지우면 된다.

```sh
rm -f ~/.local/bin/confit
rm -f ~/.local/share/man/man1/confit.1
```

Confit install은 project `config/` tree를 만들거나 수정하지 않는다.
