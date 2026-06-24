# 라운드 4 TUI command workflow 수동 QA

날짜: 2026-06-25

호스트: macOS 로컬 터미널, 작업 경로는
`/Users/gungye/workspace/delos/tools/confit`.

이번 검증은 로컬 설치 바이너리인 `/Users/gungye/.local/bin/confit`를 사용했다.
`script(1)` pseudo-terminal로 실제 curses 화면 경로를 열었고, 원본 fixture는
수정하지 않았다. profile/schema 저장 검증은 `/tmp` 아래 임시 복사본에서만
수행했다.

```text
/tmp/confit-round4-manual-qa
```

설치 확인:

```text
Confit doctor
  version: confit 0.1.0-rc1
  executable: /Users/gungye/.local/bin/confit
  build mode: Release
  platform: Darwin
  platform lane: macos-cli-tui
  platform note: macOS CLI and curses TUI lane
  compiler: AppleClang 21.0.0.21000101
  curses: available; TUI enabled
  tui: enabled
  install rule: single executable artifact: <prefix>/bin/confit
  generators: header, reports, cmake, qstar, build-selection
  generators enabled: header, reports, cmake, qstar, build-selection
  deferred generators: none in this build
  project: not checked
doctor ok
```

## Local install refresh

명령 형태:

```sh
scripts/install-local.sh --prefix "$HOME/.local" --build-dir /tmp/confit-round4-install-build
```

관찰 결과:

```text
installed /Users/gungye/.local/bin/confit
installed /Users/gungye/.local/share/man/man1/confit.1
```

결과: pass.

## Profile command row

명령 형태:

```sh
script -q /tmp/confit-round4-manual-qa/profile-command.typescript \
  /bin/sh -c 'printf ":\033:verbose\n:noverbose\n:filter bool\n:clear\n:help\n/bool\n:quit\n" |
  env TERM=xterm /Users/gungye/.local/bin/confit tui \
    --project /tmp/confit-round4-manual-qa/profile-flow \
    --profile edit'
```

확인한 흐름:

```text
: prompt opens on the reserved bottom command row
Esc cancels command input immediately
:verbose enables verbose inspector
:noverbose returns to compact inspector
:filter applies list filter
:clear clears filter/search state
:help shows command help
/ search uses the same bottom command row
:quit exits through the normal root quit flow
```

관찰한 marker:

```text
command cancelled
verbose inspector mode
compact
filter: bool
cleared filters
commands: verbose noverbose tree flat filter <text> clear help quit
search:
search 1/1: Edit Bool <delos.edit.bool> category=edit/bool
quit
```

참고: `script(1)` raw capture에는 curses escape sequence가 포함된다. 화면 갱신
최적화 때문에 status line 첫 글자가 일부 capture에서 생략될 수 있어, marker
검증은 안정적인 substring 기준으로 수행했다.

결과: pass.

## Profile submenu Esc back

명령 형태:

```sh
script -q /tmp/confit-round4-manual-qa/profile-submenu.typescript \
  /bin/sh -c 'printf "\n\033\033" |
  env TERM=xterm /Users/gungye/.local/bin/confit tui \
    --project /tmp/confit-round4-manual-qa/profile-submenu \
    --profile edit'
```

확인한 흐름:

```text
Enter enters the child menu
Esc returns to the parent menu without delay
Esc at root exits the clean session
```

관찰한 marker:

```text
entered menu Main Menu > edit
back to
```

결과: pass.

## Profile dirty Esc exit

명령 형태:

```sh
script -q /tmp/confit-round4-manual-qa/profile-dirty.typescript \
  /bin/sh -c 'printf "/bool\ne\033\033\033j\n" |
  env TERM=xterm /Users/gungye/.local/bin/confit tui \
    --project /tmp/confit-round4-manual-qa/profile-dirty \
    --profile edit'
```

확인한 흐름:

```text
/ search jumps to the bool option
e toggles the bool value
Esc exits the root view
dirty confirmation opens
Discard changes is selectable
```

관찰한 marker:

```text
toggled delos.edit.bool = true
Unsaved Profile Changes
Discard changes
```

결과: pass.

## Profile q alias

명령 형태:

```sh
script -q /tmp/confit-round4-manual-qa/profile-q.typescript \
  /bin/sh -c 'printf "q" |
  env TERM=xterm /Users/gungye/.local/bin/confit tui \
    --project /tmp/confit-round4-manual-qa/profile-q \
    --profile edit'
```

확인한 흐름:

```text
q remains a compatibility alias for the root quit flow
```

관찰한 marker:

```text
Confit TUI - menuconfig profile
```

결과: pass.

## Schema command row

명령 형태:

```sh
script -q /tmp/confit-round4-manual-qa/schema-command.typescript \
  /bin/sh -c 'printf "\n:\033:verbose\n:noverbose\n:filter schema\n:clear\n:help\n:quit\n" |
  env TERM=xterm /Users/gungye/.local/bin/confit tui \
    --project /tmp/confit-round4-manual-qa/schema-command \
    --schema-edit'
```

확인한 흐름:

```text
schema editor warning opens first
schema editor uses the same bottom command row
Esc cancels command input immediately
:verbose and :noverbose affect inspector mode
:filter and :clear work in schema mode
:help shows command help
:quit exits through the root quit flow
```

관찰한 marker:

```text
Schema Edit Warning
command cancelled
verbose inspector mode
compact
filter: schema
cleared filters
commands: verbose noverbose tree flat filter <text> clear help quit
```

결과: pass.

## Schema q alias

명령 형태:

```sh
script -q /tmp/confit-round4-manual-qa/schema-q.typescript \
  /bin/sh -c 'printf "\nq" |
  env TERM=xterm /Users/gungye/.local/bin/confit tui \
    --project /tmp/confit-round4-manual-qa/schema-q \
    --schema-edit'
```

확인한 흐름:

```text
q remains a compatibility alias in schema editor too
```

관찰한 marker:

```text
Confit TUI - menuconfig schema
```

결과: pass.

## Non-interactive checks

명령 형태:

```sh
/Users/gungye/.local/bin/confit check \
  --project /tmp/confit-round4-manual-qa/profile-flow \
  --profile edit

MANPATH="$HOME/.local/share/man:${MANPATH:-}" MANPAGER=cat man confit
```

관찰한 marker:

```text
check ok
TUI COMMAND ROW
```

결과: pass.

## 판정

라운드 4의 TUI command workflow는 pass다. Profile editor와 schema editor 모두
맨 아래 blank command row, `:` command prompt, `/` search prompt, 즉시 Esc
취소/뒤로/종료, dirty confirm, q compatibility alias가 같은 정책으로 동작한다.
