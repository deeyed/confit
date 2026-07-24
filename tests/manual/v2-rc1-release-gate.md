# V2 RC1 Terminal QA

날짜: 2026-07-24

호스트: macOS 로컬 terminal pseudo-terminal (`script(1)`). 사용한 build는
`build-r20/confit`이며, fixture 원본은 수정하지 않았다. V1/V2 profile fixture는
각각 `/tmp/confit-v2-rc1-tui.rc0ZZR` 아래로 복사해서 실행했다.

## V1 Profile TUI

```sh
printf ':verbose\n:noverbose\n\033' | script -q v1.typescript /bin/sh -c \
  'env TERM=xterm COLUMNS=160 LINES=24 build-r20/confit tui \
    --project /tmp/.../v1-profile --profile edit'
```

확인한 marker:

```text
verbose inspector mode
compact inspector mode
```

결과: pass. V1 profile editor의 command row와 Esc exit path가 정상 종료했다.

## V2 Transactional Profile TUI

```sh
printf '\nejs\033\033' | script -q v2.typescript /bin/sh -c \
  'env TERM=xterm COLUMNS=180 LINES=24 build-r20/confit tui \
    --project /tmp/.../v2-profile --profile edit'
build-r20/confit check --project /tmp/.../v2-profile --profile edit
```

확인한 marker와 저장 결과:

```text
preview accepted: v2tui.enabled = true
saved schema v2 profile atomically
check ok
"v2tui.enabled" = true
```

결과: pass. 저장은 V2 profile TOML에만 반영됐고, 이어지는 schema validation이
성공했다.

## V2 Guarded Schema View

```sh
printf '\033' | script -q v2-schema.typescript /bin/sh -c \
  'env TERM=xterm COLUMNS=120 LINES=24 build-r20/confit tui \
    --project /tmp/.../v2-profile --schema-edit'
```

확인한 marker:

```text
SCHEMA EDIT MODE - guarded
```

결과: pass. V2 schema TUI는 guarded information view이며 source schema를
무단으로 저장하지 않는다.

## 범위

이 기록은 macOS terminal interaction을 확인한 것이다. Windows는 CLI-only
preview이고 TUI 결과를 주장하지 않는다. 실제 Parus/Delos source tree나 board
build를 이 QA에서 수정하거나 검증하지 않았다.

## Local Install Smoke

```sh
scripts/install-local.sh --prefix ~/.local --build-dir /tmp/confit-v2-rc1-install
~/.local/bin/confit --version --verbose
~/.local/bin/confit doctor --project tests/fixtures/realish-v2/delos
~/.local/bin/confit check --project tests/fixtures/v1-baseline/project --profile sim-dsh
~/.local/bin/confit check --project tests/fixtures/realish-v2/delos --profile sim-dsh --strict
MANPATH="$HOME/.local/share/man:${MANPATH:-}" man -w confit
```

확인한 결과:

```text
confit 0.2.0-rc1
build mode: Release
supported schema versions: 1, 2
project schema: 2
check ok                         # V1 fixture
check ok                         # V2 fixture
/Users/gungye/.local/share/man/man1/confit.1
```

결과: pass. 설치 산출물은 `<prefix>/bin/confit` 단일 실행 파일과 설치된 manpage다.
