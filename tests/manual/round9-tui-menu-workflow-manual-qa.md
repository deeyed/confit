# 라운드 9 TUI menu workflow 수동 QA

날짜: 2026-06-24

호스트: macOS 로컬 터미널, 작업 경로는
`/Users/gungye/workspace/delos/tools/confit`.

이번 검증은 `script(1)` pseudo-terminal에서 `confit tui`를 실행해 실제 curses
화면 경로를 확인했다. 원본 fixture는 수정하지 않았고, 모든 profile/schema
저장 검증은 `/tmp` 아래 임시 복사본에서 수행했다.

```text
/tmp/confit-r9-manual.DReEPT
```

사용한 바이너리:

```text
/tmp/confit-r9-build/confit
```

## Profile tree, command, edit, save

명령 형태:

```sh
script -q /tmp/confit-r9-manual.DReEPT/profile-main.typescript \
  /bin/sh -c 'printf "\n\n\033/mode\n:verbose\n:noverbose\n:flat\n:tree\n:filter bool\n:clear\n/bool\nex/mode\nej\nx/count\ne99\n7\n?\033s\n\033\033\033" |
  env TERM=xterm /tmp/confit-r9-build/confit tui \
    --project /tmp/confit-r9-manual.DReEPT/profile-flow \
    --profile edit'
```

확인한 흐름:

```text
tree browse
submenu enter/back
search jump across menus
:verbose / :noverbose
:flat / :tree
:filter / :clear
bool toggle
enum edit
invalid int input
help/detail
save
```

관찰한 marker:

```text
breadcrumb=Main Menu
Main Menu > edit
search 1/1: Edit Mode <delos.edit.mode> category=edit/mode
verbose inspector mode
compact
flat view
tree
filter: bool
cleared filters
search 1/1: Edit Bool <delos.edit.bool> category=edit/bool
toggled delos.edit.bool = true
Confit Choice
selected delos.edit.mode = hw
invalid int: outside range [0, 10]
Confit Help
Overwrite Profile
saved and reloaded; full validation ok
back to menu Main Menu > edit
```

저장된 profile 발췌:

```toml
[profile]
name = "edit"
schema_version = 1

[values]
"delos.edit.bool" = true
"delos.edit.mode" = "hw"
"delos.edit.count" = 7
"delos.edit.threads" = 4
"delos.edit.mask" = 0x10
"delos.edit.gain" = 0.25
"delos.edit.name" = "old"
"delos.edit.path" = "old/path"
```

저장 후 검증:

```sh
/tmp/confit-r9-build/confit check \
  --project /tmp/confit-r9-manual.DReEPT/profile-flow \
  --profile edit
```

관찰 결과:

```text
check ok
```

결과: pass.

## Dirty Esc exit

명령 형태:

```sh
script -q /tmp/confit-r9-manual.DReEPT/profile-dirty.typescript \
  /bin/sh -c 'printf "/bool\ne\033\033\033j\n" |
  env TERM=xterm /tmp/confit-r9-build/confit tui \
    --project /tmp/confit-r9-manual.DReEPT/profile-dirty \
    --profile edit'
```

확인한 흐름:

```text
search jump
bool toggle
Esc root exit
dirty confirmation
Discard changes
```

관찰한 marker:

```text
toggled delos.edit.bool = true
Unsaved Profile Changes
Discard changes
```

결과: pass.

## Schema warning, category path, save

명령 형태:

```sh
script -q /tmp/confit-r9-manual.DReEPT/schema-main.typescript \
  /bin/sh -c 'printf "\nndelos.manual.mode\nenum\nManual Mode\nhManual schema help\ncmanual/tree\ntmanual,schema\nored,blue\ndblue\ns\n\033\033\033" |
  env TERM=xterm /tmp/confit-r9-build/confit tui \
    --project /tmp/confit-r9-manual.DReEPT/schema-flow \
    --schema-edit'
```

확인한 흐름:

```text
schema warning
schema option create
schema help field edit
schema category path edit
schema enum choices/default edit
schema save
schema submenu back
```

관찰한 marker:

```text
Schema Edit Warning
SCHEMA EDIT MODE is a guarded workflow.
SCHEMA EDIT MODE - guarded | schema edits change all profiles
field=category path
category path:manual/tree
schema category path updated
Manual Mode <delos.manual.mode> enum category path:manual/tree
saved and validated; reloaded graph
back to Main Menu > manual
```

저장된 schema 발췌:

```toml
[option."delos.manual.mode"]
type = "enum"
choices = ["red", "blue"]
default = "blue"
prompt = "Manual Mode"
category = "manual/tree"
tags = ["manual", "schema"]
help = "Manual schema help"
```

Graph smoke:

```sh
/tmp/confit-r9-build/confit graph \
  --project /tmp/confit-r9-manual.DReEPT/schema-flow
```

관찰 결과:

```json
{"id": "delos.manual.mode", "type": "enum"}
```

결과: pass.

## Schema depth warning

명령 형태:

```sh
script -q /tmp/confit-r9-manual.DReEPT/schema-depth.typescript \
  /bin/sh -c 'printf "\nndelos.manual.deep\nbool\nManual Deep\ncmanual/tree/deep/too\ns\n\033\033\033\033" |
  env TERM=xterm /tmp/confit-r9-build/confit tui \
    --project /tmp/confit-r9-manual.DReEPT/schema-depth \
    --schema-edit'
```

관찰한 marker:

```text
Manual Deep <delos.manual.deep> bool category path:manual/tree/deep/too
warning: category path depth exceeds 3 levels
schema saved and validated; reloaded graph
```

저장된 schema 발췌:

```toml
[option."delos.manual.deep"]
type = "bool"
default = false
prompt = "Manual Deep"
category = "manual/tree/deep/too"
```

결과: pass. 4단계 이상 category path는 저장은 가능하지만 warning으로 표시된다.
`--strict` 검증에서는 이 warning을 failure로 승격할 수 있다.

## 추가 command check

`:flat` 직후 `:tree` marker를 분리해서 보기 위해 짧은 추가 세션을 실행했다.

```sh
script -q /tmp/confit-r9-manual.DReEPT/profile-tree-command.typescript \
  /bin/sh -c 'printf ":flat\n:tree\n\033" |
  env TERM=xterm /tmp/confit-r9-build/confit tui \
    --project /tmp/confit-r9-manual.DReEPT/profile-tree-command \
    --profile edit'
```

관찰한 marker:

```text
flat view
tree
Main Menu
```

결과: pass.

## 메모

- Profile editor의 `?`와 `h`는 help/detail view를 연다.
- Schema editor의 `?`는 keymap/status를 보여주고, `h`는 선택한 schema
  option의 help field를 수정한다.
- Schema editor 저장 대상은 guarded draft schema 파일인
  `config/options/tui-schema.toml`이다.
- Raw `script(1)` capture에는 curses escape sequence가 포함되므로, 이 문서는
  안정적으로 확인 가능한 marker와 저장 결과만 기록한다.

## 라운드 종료 로컬 검증

Full test:

```sh
./tests/run_tests.sh /tmp/confit-r9-full-build
```

결과:

```text
100% tests passed, 0 tests failed out of 26
```

Stress:

```sh
sh tests/integration/round20_stress.sh \
  /tmp/confit-r9-build/confit \
  /Users/gungye/workspace/delos/tools/confit \
  /tmp/confit-r9-stress
```

결과:

```text
stress ok: 5000 options
```

Diff hygiene:

```sh
git diff --check
```

결과: pass.

Local install:

```sh
scripts/install-local.sh --prefix "$HOME/.local"
$HOME/.local/bin/confit doctor
```

관찰 결과:

```text
curses: available; TUI enabled
install rule: single executable artifact: <prefix>/bin/confit
doctor ok
```

Manpage smoke:

```sh
MANPATH="$HOME/.local/share/man:${MANPATH:-}" MANPAGER=cat man confit
```

관찰한 key section:

```text
profile ? 또는 h  help/detail
schema ?          keymap/status
schema h          선택 option help field 수정
```
