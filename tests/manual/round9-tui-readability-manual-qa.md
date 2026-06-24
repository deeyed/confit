# 라운드 9 TUI 시인성 수동 QA

날짜: 2026-06-24

호스트: macOS 로컬 터미널, 작업 경로는
`/Users/gungye/workspace/delos/tools/confit`.

이번 수동 검증은 단순 pipe가 아니라 `script(1)` 기반 pseudo-terminal에서
`confit tui`를 실행해 curses 화면 경로를 확인했다. 원본 fixture는 직접
수정하지 않았고, 모든 쓰기 검증은 아래 임시 복사본에서 수행했다.

```text
/tmp/confit-round9-manual.d1ULOX
```

사용한 바이너리:

```text
/var/folders/1g/wddr4j993gzd7y87094d7k340000gn/T/confit-build/confit
```

## Profile Browse/Edit/Save

명령 형태:

```sh
script -q /tmp/confit-round9-manual.d1ULOX/profile-main.typescript \
  /bin/sh -c 'cat /tmp/confit-round9-manual.d1ULOX/profile-main.keys |
  env TERM=xterm <confit-bin> tui \
    --project /tmp/confit-round9-manual.d1ULOX/profile-flow \
    --profile edit'
```

확인한 조작:

```text
category 접기/펼치기
/mode 검색
? help/detail 화면 진입
enum popup 편집
/bool 검색 후 bool toggle
/count 검색 후 잘못된 int 입력과 정상 int 입력
/name 검색 후 빈 string 입력 차단과 정상 string 입력
/path 검색 후 absolute path 입력 차단과 relative path 입력
저장 및 overwrite 확인
clean save 이후 quit
```

관찰한 TUI marker:

```text
Confit TUI - menuconfig profile
collapsed menu edit
search 1/1: Edit Mode <delos.edit.mode> category=edit
Confit Help
Confit Choice
selected delos.edit.mode = hw
toggled delos.edit.bool = true
invalid int: outside range [0, 10]
invalid string: value required
invalid path: expected relative path
Overwrite Profile
saved and reloaded; full validation ok
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
"delos.edit.name" = "Manual Name"
"delos.edit.path" = "build/manual"
```

저장 후 검증:

```sh
confit check --project /tmp/confit-round9-manual.d1ULOX/profile-flow --profile edit
```

관찰 결과:

```text
check ok
```

결과: pass.

## Dirty Quit

명령 형태:

```sh
script -q /tmp/confit-round9-manual.d1ULOX/profile-dirty.typescript \
  /bin/sh -c 'cat /tmp/confit-round9-manual.d1ULOX/profile-dirty.keys |
  env TERM=xterm <confit-bin> tui \
    --project /tmp/confit-round9-manual.d1ULOX/profile-dirty \
    --profile edit'
```

확인한 조작:

```text
/bool 검색
bool toggle
quit
Discard changes 선택
```

관찰한 TUI marker:

```text
toggled delos.edit.bool = true
Unsaved Profile Changes
Discard changes
```

실행 후 `config/profiles/edit.toml`은 원본 fixture와 동일했다.

결과: pass.

## Schema Warning/Edit/Save

명령 형태:

```sh
script -q /tmp/confit-round9-manual.d1ULOX/schema-main.typescript \
  /bin/sh -c 'cat /tmp/confit-round9-manual.d1ULOX/schema-main.keys |
  env TERM=xterm <confit-bin> tui \
    --project /tmp/confit-round9-manual.d1ULOX/schema-flow \
    --schema-edit'
```

확인한 조작:

```text
schema warning 진입
새 option 생성
id/type/prompt/help/category/tags/choices/default field 편집
schema 저장
저장 후 quit
```

관찰한 TUI marker:

```text
Schema Edit Warning
SCHEMA EDIT MODE is a guarded workflow.
Confit TUI - menuconfig schema
SCHEMA EDIT MODE - guarded | schema edits change all profiles
Confit Schema Field
Manual Mode <delos.manual.mode>
policy: enum comma-list; default must remain a choice
policy: must match one enum choice
schema saved and validated; reloaded graph
```

저장된 schema 발췌:

```toml
[option."delos.manual.mode"]
type = "enum"
choices = ["red", "blue"]
default = "blue"
prompt = "Manual Mode"
category = "manual"
tags = ["manual", "schema"]
help = "Manual schema help"
```

Graph smoke:

```sh
confit graph --project /tmp/confit-round9-manual.d1ULOX/schema-flow
```

관찰 결과:

```json
{"id": "delos.manual.mode", "type": "enum"}
```

결과: pass.

## Schema Warning Cancel

명령 형태:

```sh
script -q /tmp/confit-round9-manual.d1ULOX/schema-cancel.typescript \
  /bin/sh -c 'cat /tmp/confit-round9-manual.d1ULOX/schema-cancel.keys |
  env TERM=xterm <confit-bin> tui \
    --project /tmp/confit-round9-manual.d1ULOX/schema-cancel \
    --schema-edit'
```

관찰한 TUI marker:

```text
Schema Edit Warning
SCHEMA EDIT MODE is a guarded workflow.
Cancel
```

schema option 파일 목록은 변경되지 않았다.

결과: pass.

## 남은 TUI 메모

- 테스트한 macOS 터미널에서 menuconfig 스타일 layout은 충분히 읽을 수
  있었고, profile/schema warning과 status surface도 계속 보였다.
- `script(1)` capture에는 curses escape sequence가 포함되므로, 이 문서는
  raw transcript 전체 대신 안정적으로 확인 가능한 marker와 저장 결과를
  기록한다.
- Linux는 GitHub Actions Ubuntu job과 ncurses dependency를 통해 검증
  범위를 잡았다. 이번 손검증 자체는 macOS에서 수행했다.
- 현재 TUI는 Confit profile/schema workflow에 사용할 수 있는 수준이다.
  다만 kconfiglib menuconfig의 symbol browser, reverse dependency 탐색,
  완전한 Kconfig menu tree 의미론까지 복제한 것은 아니다.
