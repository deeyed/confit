# 10. 문제 해결

## 설치했는데 `confit` 명령이 없다

확인:

```sh
ls -l ~/.local/bin/confit
echo "$PATH"
```

`~/.local/bin`이 PATH에 없다면 shell 설정에 추가한다.

```sh
export PATH="$HOME/.local/bin:$PATH"
```

일시적으로는 전체 경로를 쓴다.

```sh
~/.local/bin/confit doctor
```

## `man confit`이 안 보인다

확인:

```sh
ls -l ~/.local/share/man/man1/confit.1
manpath
```

`~/.local/share/man`이 manpath에 없으면 일시적으로:

```sh
MANPATH="$HOME/.local/share/man:${MANPATH:-}" man confit
```

## TUI가 열리지 않는다

먼저:

```sh
confit doctor
```

macOS/Linux에서 curses가 없으면 CMake configure나 doctor 단계에서 드러난다. Linux라면 배포판의 ncurses
development package가 필요하다.

예:

```text
Debian/Ubuntu: libncurses-dev
Fedora: ncurses-devel
Arch: ncurses
```

Windows에서는 현재 TUI가 정식 지원되지 않는다. `confit tui`가 exit code `8`로 실패해야 정상이다.

## `check`가 schema error로 실패한다

다음 순서로 좁힌다.

```sh
confit check --project <project> --profile <profile>
confit check --project <project> --profile <profile> --strict
confit graph --project <project> --profile <profile> --format json
```

흔한 원인:

- option id typo.
- profile에서 존재하지 않는 option을 설정함.
- type이 맞지 않음.
- enum choice가 schema에 없음.
- range 밖 값.
- dependency cycle.
- `requires`나 `conflicts` 위반.

## `gen`이 기존 파일 때문에 실패한다

Confit은 기본적으로 generated file overwrite를 거부한다.

먼저 dry-run:

```sh
confit gen --project <project> --profile <profile> --out <out> --artifact all --dry-run
```

정말 덮어써도 된다면:

```sh
confit gen --project <project> --profile <profile> --out <out> --artifact all --force
```

## `compat`가 실패한다

1. 실패 message를 읽는다.
2. 문제 project의 option을 `explain`한다.
3. profile끼리 `diff`한다.
4. rule이 너무 넓거나 message가 부정확한지 확인한다.

예:

```sh
confit explain --project <delos-project> --profile <profile> delos.debug.dsh
confit explain --project <parus-project> --profile <profile> parus.test.qemu_pairing
```

## Exit code

```text
0  success
1  invalid command line
2  parse error
3  schema error
4  dependency or conflict error
5  compatibility error
6  generation error
7  internal error
8  unsupported command or platform
```

CI에서는 exit code를 그대로 사용한다. 특히 `5`는 compat failure이므로 build failure로 처리해야 한다.

## `--quiet`와 `--verbose`

`--quiet`는 성공 banner 같은 비필수 output을 줄인다. JSON payload나 error는 숨기지 않는다.

```sh
confit check --project <project> --profile <profile> --quiet
```

`--verbose`는 stderr에 추가 context를 쓴다.

```sh
confit check --project <project> --profile <profile> --verbose
```

## AI 자동화가 이상한 파일을 수정하려고 한다

중단하고 다음을 확인한다.

```sh
git status --short
git diff --name-only
```

Confit 자체 작업이면 수정 범위는 `tools/confit/` 안이어야 한다. 실제 Parus/Delos adoption 작업은 별도
명시 요청과 별도 review가 필요하다.
