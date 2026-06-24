# 01. 설치와 확인

## 현재 checkout에서 바로 build하기

Delos repository root에서 실행한다.

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
platform: Darwin 또는 Linux
curses: available; TUI enabled
generators: header, reports, cmake, qstar manifest
doctor ok
```

Windows에서는 현재 CLI-only lane이므로 TUI는 unsupported로 실패해야 정상이다.

## 수동 설치

script를 쓰지 않고 CMake만으로 설치할 수도 있다.

```sh
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

