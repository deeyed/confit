---
doc_type: tool-spec
status: draft
authority: operational
last_verified: 2026-06-25
---

# CLI And TUI Strategy

Confit의 첫 interface는 CLI다. TUI는 macOS/Linux host에서 동작하는
ncurses frontend이며, CLI가 제공하는 validation, resolution, generation,
explanation 기능을 사람이 탐색하고 편집하기 쉽게 감싸는 역할을 한다.

CLI command, option, exit-code, installation contract는
`docs/cli-contract.md`가 정본이다. 이 문서는 `confit tui`의 화면 구조,
탐색 방식, keymap, command mode, 저장 정책을 정의한다.

## TUI 목표

Confit TUI는 kconfiglib/menuconfig의 장점인 keyboard 중심 탐색, 검색,
help/detail, dirty-save 흐름을 가져온다. 하지만 Kconfig를 그대로 복제하지
않는다. Confit의 정본은 option dependency DAG이고, menu tree는 사람이 보는
표시용 view다.

TUI가 반드시 지원해야 하는 작업은 다음과 같다.

- profile 생성과 profile TOML 편집
- target 선택과 resolved value 확인
- bool toggle, enum 선택, typed value 입력
- option 검색과 dependency reason 확인
- guarded schema option 생성/수정
- prompt/help/category/tag/range/choice 수정
- 저장 전 full validation과 저장 후 reload/resolve

## Menu Tree 정책

기존 flat category 접기/펼치기만으로는 Parus/Delos 규모의 설정을 다루기
어렵다. 따라서 TUI는 얕은 중첩 menu navigation을 지원해야 한다.

표시용 menu path는 schema metadata에서 만든다. 기본 표현은 slash-separated
category path다.

```toml
[option."delos.trace.capacity"]
type = "uint"
default = 4096
prompt = "Trace ring capacity"
category = "runtime/trace"
tags = ["runtime", "trace", "observability"]
```

TUI는 위 option을 다음 위치에 둔다.

```text
Main Menu
  Runtime
    Trace
      Trace ring capacity
```

Tree depth 정책:

- 권장 depth는 2단계다. 예: `runtime/trace`.
- 큰 영역에서만 3단계를 허용한다. 예: `runtime/scheduler/policy`.
- 4단계 이상은 schema 설계 냄새로 본다. 일반 validation은 warning,
  strict validation은 failure로 승격할 수 있어야 한다.
- 깊은 tree로 모든 것을 숨기기보다 search, tag, dependency explanation을
  기본 탐색 수단으로 유지한다.

Menu row 동작:

- `Enter`는 menu row 안으로 들어간다.
- `Esc` 또는 `Left`는 상위 menu로 돌아간다.
- root menu에서 `Esc`는 종료 흐름으로 들어간다.
- collapsed/expanded inline tree는 보조 view일 뿐, 기본 UX는 menu stack이다.

Header는 현재 위치를 breadcrumb로 보여준다.

```text
Delos / Runtime / Trace
```

Search 결과로 다른 menu의 option에 jump하면 TUI는 필요한 menu stack을 자동으로
열고 breadcrumb를 갱신한다.

## 기본 화면 Layout

기본 browse 화면은 내부 box 안에 option을 가두지 않는다. 화면은 위에서부터
다음 band로 나눈다.

```text
title/path band
context header + breadcrumb
full-width list viewport
fixed inspector/status band
key legend/status band
blank command row
```

Dialog, help/detail, schema warning, choice popup처럼 focus 전환이 필요한
view만 box 또는 fullscreen overlay를 쓴다.

## Style과 색상

ncurses frontend는 raw color pair 번호를 화면 코드에서 직접 쓰지 않고 의미
기반 style slot을 사용한다. 기본 slot은 `title`, `path`, `header`, `list`,
`selection`, `disabled`, `forced`, `warning`, `status`, `key`,
`search_match`, `category`, `separator`, `help`, `dialog`, `edit`이다.

Terminal이 color를 지원하면 built-in palette를 사용한다. Terminal이 color를
지원하지 않거나 `NO_COLOR` 환경변수가 설정되어 있으면 monochrome fallback으로
전환하고, 색상 대신 `bold`, `dim`, `reverse`, `underline` 같은 curses
attribute만 사용한다. 따라서 TUI는 color가 없어도 선택 row, disabled row,
warning/status row를 구분할 수 있어야 한다.

## Header, Status, Footer

상단 header는 안정적인 context를 보여준다.

- mode: profile editor, schema editor, help, search, dialog
- project
- profile 또는 schema-edit mode
- target
- dirty state
- breadcrumb
- search/filter state

하단 inspector/status band는 현재 선택 항목과 즉시성 메시지를 보여준다.
`ready`, `search 1/3`, `blocked: ...`, `invalid ...`, `saved and reloaded ...`,
`cancelled` 같은 결과나 이유를 표시한다.

Footer key legend는 현재 mode에서 필요한 키만 짧게 보여준다. 전체 keymap을
항상 나열하지 않는다. Key legend/status row와 command row는 서로 다른 줄이다.
맨 아래 command row는 평소에 blank이며, `:`, `/`, `c`, `t` 같은 하단 입력 중에만
cursor와 prompt를 표시한다. 입력이 끝나면 command row는 다시 blank가 된다.

## Row와 Inspector

List row는 한 줄 높이를 안정적으로 유지한다. 선택 포인터가 움직인다고 row
아래에 id/type/deps/tags detail을 삽입하지 않는다. Row 높이가 흔들리면
대형 설정 화면에서 시선 기준점이 무너지기 때문이다.

기본 row는 prompt와 현재 value 중심이다.

```text
[*] Trace enabled                         y
(4) Trace ring capacity                   4096
<hw> Execution mode                       hw
```

Option id, type, deps, tags, source, blocked reason 같은 보조 정보는 화면
맨 아래의 고정 inspector/status 영역에서만 보여준다. 기본 compact mode의
inspector는 짧게 유지한다.

```text
Trace ring capacity <delos.trace.capacity>  uint  deps ok
```

자세한 정보는 verbose mode에서만 보여준다.

```text
:verbose
```

Verbose inspector 예:

```text
id=delos.trace.capacity | type=uint | source=profile:sim-dsh |
deps=- | tags=runtime,trace,observability | state=deps ok
```

Profile editor의 `?` 또는 `h` help/detail view는 inspector보다 긴 설명을 볼
때 사용한다. 여기에는 prompt, id, type, current value, default, source,
category path, tags, dependency state, blocked reason, declared
dependencies, help text를 섹션 단위로 표시한다.

Schema editor에서는 `h`가 선택한 schema option의 help text field를 수정하는
키다. Schema editor의 `?`는 현재 keymap/status 도움말을 보여준다.

## Command Mode

TUI는 vim처럼 `:`로 command prompt를 연다. Command mode는 많은 단축키를
footer에 늘어놓지 않고도 view option을 확장하기 위한 표준 입구다.
Command prompt는 화면 맨 아래의 전용 command row에만 표시한다. Command row는
normal mode에서는 빈 줄이고, key legend나 status message를 덮지 않는다.

기본 command:

| Command | 동작 |
|---|---|
| `:verbose` | 하단 inspector를 자세한 정보 모드로 전환한다. |
| `:noverbose` | 하단 inspector를 compact mode로 되돌린다. |
| `:tree` | menu stack navigation view를 사용한다. |
| `:flat` | 현재 project option을 flat list로 본다. |
| `:filter <text>` | id, prompt, help, category path, tag를 대상으로 filter한다. |
| `:clear` | search/filter/command state를 지운다. |
| `:help` | command 목록을 보여준다. |
| `:quit` | root에서 종료 흐름을 실행한다. dirty이면 확인 dialog를 띄운다. |

Command prompt에서 `Esc`는 입력을 취소하고 이전 화면으로 돌아간다.
빈 command, unknown command, `:filter` 인자 누락 같은 실패는 command row에
남기지 않고 status row에 표시한다. Command row는 입력 prompt의 lifecycle만
담당한다.

`q`는 과거 menuconfig식 muscle memory를 위한 숨은 alias로만 둘 수 있다.
Footer와 wiki에서는 `Esc back/exit`를 기본 종료 모델로 설명한다.

## Keymap

공통 navigation:

| Key | 동작 |
|---|---|
| `Up`, `k` | 한 row 위로 이동한다. |
| `Down`, `j` | 한 row 아래로 이동한다. |
| `PageUp` | 현재 화면 높이 기준으로 한 page 위로 이동한다. |
| `PageDown` | 현재 화면 높이 기준으로 한 page 아래로 이동한다. |
| `Home` | 현재 menu/view의 첫 row로 이동한다. |
| `End` | 현재 menu/view의 마지막 row로 이동한다. |
| `Enter`, `Space` | menu row는 진입, option row는 기본 edit/toggle을 실행한다. |
| `Left` | 상위 menu로 돌아간다. root에서는 아무 것도 변경하지 않는다. |
| `Esc` | prompt/dialog 취소, submenu에서는 뒤로, root에서는 종료 흐름. |
| `q` | 호환 alias로 종료 흐름을 실행한다. 문서와 footer의 기본 키는 `Esc`다. |
| `:` | command prompt를 연다. |
| `/` | search jump query를 입력한다. |
| `n` | 현재 search query의 다음 결과로 이동한다. |
| `N` | 현재 search query의 이전 결과로 이동한다. |
| `s` | dirty state가 있을 때 저장한다. |

Profile editor:

| Key | 동작 |
|---|---|
| `Enter`, `Space` | bool은 즉시 toggle, enum/string/int/path 등은 edit dialog를 연다. |
| `e` | 현재 option 값을 edit한다. |
| `?`, `h` | 현재 menu 또는 option의 help/detail 화면을 연다. |
| `c` | category path filter prompt를 연다. |
| `t` | tag filter prompt를 연다. |
| `x` | search/filter state를 지운다. |

Schema editor:

| Key | 동작 |
|---|---|
| `?` | schema editor keymap/status 도움말을 보여준다. |
| `n` | 새 schema option draft를 만든다. |
| `y` | 현재 option type을 수정한다. |
| `d`, `e`, `Enter` | 현재 option default value를 수정한다. |
| `p` | 현재 option prompt를 수정한다. |
| `h` | 현재 option help text를 수정한다. |
| `c` | 현재 option category path를 수정한다. |
| `t` | 현재 option tag comma-list를 수정한다. |
| `r` | 현재 option range를 수정한다. |
| `o` | 현재 enum option choices comma-list를 수정한다. |
| `s` | schema TOML 저장 전 full validation을 수행하고 저장한다. |

## Search와 Filter

Search jump mode(`/`, `n`, `N`)는 전체 menu tree에서 option을 찾고 해당
위치로 이동한다. Search는 list 모양을 줄이지 않는다.

Filter mode(`c`, `t`, `:filter`)는 현재 view에 보이는 row를 줄인다. Filter
상태는 context header에 표시하고, `x` 또는 `:clear`로 지운다.

검색 대상:

- option id
- prompt
- help text
- category path
- tag

Search 결과가 다른 menu 아래에 있으면 TUI는 그 위치로 들어간 뒤 breadcrumb와
status를 갱신한다.

## Edit와 Validation

Profile edit는 입력 시점에 검증한다.

- Bool은 즉시 toggle된다.
- Enum은 choice popup에서 후보만 선택할 수 있다.
- Int/uint/hex/float/string/path는 value dialog에서 입력한다.
- range, enum candidate, finite float, relative path, non-empty string/path
  조건을 만족하지 않으면 dialog를 닫지 않는다.

저장 직전에는 현재 edits를 포함해 resolver full validation을 다시 수행한다.
저장 후에는 profile TOML을 다시 load하고 graph/resolve를 재실행해 화면 state와
file state를 맞춘다.

Dependency/visibility UX 정책은 "숨기지 않고 설명한다"이다. `visible_if`가
만족되지 않거나 active `forces`, `requires`, `conflicts` 때문에 값을 바꿀 수
없는 option도 볼 수 있어야 한다. 기본 row는 흐리게 표시하고, inspector와
help/detail에서 blocked reason을 설명한다.

## Schema Editor

Schema edit mode는 진입 시 `Schema Edit Warning` dialog를 먼저 표시한다.
Schema edit는 project의 의미를 바꾸므로 profile 편집보다 강한 경고와 review
정책이 필요하다.

Schema editor도 profile editor와 같은 layout, tree navigation, command mode,
inspector, keymap을 공유한다. 화면 title과 context header는 항상
`SCHEMA EDIT MODE - guarded`를 포함해야 한다.

Schema field 수정은 profile value editor와 같은 dialog primitive를 쓴다.
`id`, `type`, `default`, `prompt`, `help`, `category path`, `tags`, `range`,
`choices`는 입력 시점에 검증한다. 저장 경로는 draft option을 core model로
다시 구성한 뒤 schema validation, graph build, graph validation을 통과해야
TOML write를 수행한다. Write 후에도 project를 reload하고 graph validation을
다시 실행한다.

## TUI Data Source

TUI는 core evaluator의 structured model과 report를 사용한다. TUI 안에 별도
dependency evaluator를 넣지 않는다.

```text
core evaluator -> model/report -> TUI view
```

## Save Format

TUI의 기본 저장 대상은 profile override TOML이다.

```text
config/profiles/user-sim-dsh.toml
```

Schema edit mode에서는 guarded draft schema 파일인
`config/options/tui-schema.toml`에 option draft를 저장한다. TUI가 저장하는
파일은 항상 사람이 읽을 수 있는 TOML이어야 한다. TUI 전용 binary workspace나
hidden database를 정본으로 삼지 않는다.

## No Runtime TUI

TUI는 host tool이다. Delos DSH는 Confit TUI가 아니다. DSH에서 config를
바꾸는 기능은 금지한다.

## Future UX Ideas

초기 실사용 범위 밖이지만, core model이 안정화되면 다음 기능을 검토할 수
있다.

- `:diff`: 현재 dirty edits와 저장된 profile의 차이 보기.
- `:why-not`: 어떤 option을 켤 수 없는 이유를 command mode에서 직접 조회.
- `:fix`: conflict를 해결하는 가능한 edit set 제안.
- Parus/Delos compatibility dashboard.
- release readiness checklist.
