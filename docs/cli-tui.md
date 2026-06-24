---
doc_type: tool-spec
status: draft
authority: informative
last_verified: 2026-06-23
---

# CLI And TUI Strategy

Confit의 첫 interface는 CLI다. TUI는 ncurses 기반 host frontend로 제공한다. TUI가 없더라도 모든
configuration resolution, validation, generation, explanation은 CLI만으로 가능해야 한다.

CLI command, option, exit-code, and installation contracts are defined by
`docs/cli-contract.md`. This document only defines the TUI strategy and
keyboard behavior that sits behind `confit tui`.

## TUI Principles

TUI는 Kconfig menuconfig의 좋은 점을 가져오되, 깊은 tree traversal 중심 UX를 그대로 복제하지 않는다.

TUI는 다음 작업을 지원해야 한다.

- profile 생성
- profile 수정
- target 선택
- option 검색과 값 변경
- schema option 추가
- schema prompt/help/category/tag 수정
- choice 후보 수정
- 저장 전 full validation

TUI는 다음 panel을 가져야 한다.

```text
+----------------------+-------------------------------+
| Category / Search    | Option List                   |
|                      |                               |
| debug                | [ ] delos.debug.ddc           |
| scheduler            | [*] delos.debug.dsh           |
| dcg                  |                               |
+----------------------+-------------------------------+
| Explanation / Dependency Graph                    |
| why disabled, who requires, conflicts, generated  |
+---------------------------------------------------+
```

필수 기능:

- option id 검색
- category/tag filter
- dependency explanation
- disabled 이유 표시
- conflict 이유 표시
- generated value preview
- profile override 표시
- save 전에 full validation

## TUI Styling

ncurses frontend는 raw color pair 번호를 화면 코드에서 직접 쓰지 않고 의미 기반 style slot을 사용한다.
현재 고정된 style slot은 `title`, `path`, `header`, `list`, `selection`, `disabled`, `forced`,
`warning`, `status`, `key`, `search_match`, `category`, `separator`, `help`, `dialog`, `edit`이다.

macOS/Linux에서 terminal이 color를 지원하면 built-in palette를 사용한다. Terminal이 color를 지원하지
않거나 `NO_COLOR` 환경변수가 설정되어 있으면 monochrome fallback으로 전환하고, 색상 대신 `bold`, `dim`,
`reverse`, `underline` 같은 curses attribute만 사용한다. 따라서 TUI는 color가 없어도 선택 row, disabled
row, warning/status row를 구분할 수 있어야 한다.

## Fixed TUI Keymap

Confit TUI keymap은 kconfiglib/menuconfig 계열 조작감을 기준으로 고정한다. Profile editor와 schema editor는
동일한 navigation key를 공유하고, mode별 edit command만 다르게 해석한다.

공통 navigation:

| Key | 동작 |
|---|---|
| `Up`, `k` | 한 row 위로 이동한다. |
| `Down`, `j` | 한 row 아래로 이동한다. |
| `PageUp` | 현재 화면 높이 기준으로 한 page 위로 이동한다. |
| `PageDown` | 현재 화면 높이 기준으로 한 page 아래로 이동한다. |
| `Home` | 첫 row로 이동한다. |
| `End` | 마지막 row로 이동한다. |
| `Enter`, `Space` | 현재 row의 기본 동작을 실행한다. Profile mode에서는 category를 펼치거나 접고, option은 값을 edit/toggle한다. |
| `?` | 현재 선택 항목의 help/detail 화면을 연다. Schema editor에서는 mode key help를 status line에 표시한다. |
| `Esc` | 열린 prompt를 취소한다. 기본 화면에서는 현재 search/filter를 취소하거나 cancel 상태를 표시한다. |
| `q` | TUI를 종료한다. Profile editor가 dirty이면 save/discard/cancel 확인을 먼저 띄운다. |

Profile editor:

| Key | 동작 |
|---|---|
| `/` | search jump query를 입력한다. id, prompt, help, category, tag를 대상으로 찾고 첫 결과로 이동한다. 목록 자체를 줄이지 않는다. |
| `n` | 현재 search query의 다음 결과로 이동한다. |
| `N` | 현재 search query의 이전 결과로 이동한다. |
| `c` | category filter를 입력한다. |
| `t` | tag filter를 입력한다. |
| `x` | search query와 category/tag filter를 모두 지운다. |
| `e` | 현재 option 값을 edit한다. 현재 row가 category heading이면 Enter/Space와 같이 펼치거나 접는다. |
| `h` | 현재 category 또는 option의 help/detail 화면을 연다. |
| `s` | profile TOML을 저장한다. 기존 profile file은 overwrite confirm을 거친다. |

Search jump mode와 filter mode는 분리한다. Search jump mode(`/`, `n`, `N`)는 전체 menu tree 모양을 유지한 채
일치하는 option으로 이동하고, header/status에 `result=current/total`을 표시한다. Filter mode(`c`, `t`)는
category/tag에 맞지 않는 menu row를 숨긴다. Search는 현재 category/tag filter 안에서만 결과를 센다.

Profile edit는 입력 시점에 검증한다. Bool은 즉시 toggle된다. Enum은 choice popup에서 후보만 선택할 수
있다. Int/uint/hex/float/string/path는 value dialog에서 입력하며, range, enum candidate, finite float,
relative path, non-empty string/path 조건을 만족하지 않으면 dialog를 닫지 않고 같은 자리에서 다시 입력받는다.
저장 직전에는 현재 edits를 포함해 resolver full validation을 다시 수행한다. 저장 후에는 profile TOML을
디스크에서 다시 load하고 graph/resolve를 재실행해 화면 state와 file state를 맞춘다. 실패 시 status line은
`error: <option-id>: <reason>` 또는 `saved but reload failed: ...` 형태로 원인 option을 표시한다.
새 profile은 메모리에서 생성되는 즉시 dirty로 표시되며, `s`로 `config/profiles/<profile>.toml`을 만든다.

Dependency/visibility UX 정책은 "숨기지 않고 흐리게 보여준다"이다. `visible_if`가 만족되지 않거나 active
`forces`, active `requires`, active `conflicts` 때문에 값을 바꿀 수 없는 option도 menu row에 남겨서 reason을
볼 수 있게 한다. Row detail과 help/detail은 `hidden: visible_if inactive ...`, `blocked: forced by ...`,
`blocked: required by ...`, `blocked: conflicts with ...`, `recommended by ...`처럼 현재 profile 기준 상태를
표시한다. `recommends`는 편집을 막지 않고 안내 상태로만 표시한다.

Schema editor:

| Key | 동작 |
|---|---|
| `n` | 새 schema option draft를 만든다. |
| `y` | 현재 option type을 수정한다. type이 바뀌면 default가 새 type 기준으로 reset되고, 호환되지 않는 range/choices draft는 지운다. |
| `d`, `e`, `Enter` | 현재 option default value를 수정한다. |
| `p` | 현재 option prompt를 수정한다. |
| `h` | 현재 option help text를 수정한다. `?` help와 구분한다. |
| `c` | 현재 option category를 수정한다. |
| `t` | 현재 option tag comma-list를 수정한다. |
| `r` | 현재 option range를 수정한다. |
| `o` | 현재 enum option choices comma-list를 수정한다. |
| `s` | schema TOML을 저장하기 전에 full validation을 수행하고 저장한다. |

Schema edit mode는 진입 시 `Schema Edit Warning` 선택 dialog를 먼저 표시한다. 기본 선택은 실제
editor 진입이고, `Esc`, `q`, 또는 cancel 선택은 파일을 건드리지 않고 종료한다. Editor 화면 header와
status line은 항상 `SCHEMA EDIT MODE`를 포함해서 profile value 편집이 아니라 schema authority 편집임을
계속 보여준다.

Schema field 수정은 profile value editor와 같은 menuconfig-style field dialog를 쓴다. `id`, `type`,
`default`, `prompt`, `help`, `category`, `tags`, `range`, `choices`는 입력 시점에 검증하고, 실패하면
dialog를 닫지 않은 채 status row에 원인을 표시한다. `range`는 numeric type에만 허용하고 현재 default를
포함해야 한다. `choices`는 enum에만 허용하며 comma-list empty item과 duplicate를 거부한다. 저장 경로는
draft option을 core model로 다시 구성한 뒤 schema validation, graph build, graph validation을 통과해야
TOML write를 수행한다. Write 후에도 project를 reload하고 graph validation을 다시 실행한다.

## TUI Data Source

TUI는 core evaluator의 structured model과 report를 사용한다. TUI 안에 별도 dependency evaluator를
넣지 않는다.

```text
core evaluator -> model/report -> TUI view
```

## Save Format

TUI의 기본 저장 대상은 profile override TOML이다.

```text
config/profiles/user-sim-dsh.toml
```

Schema edit mode에서는 `config/options/*.toml` 같은 schema file도 수정할 수 있다. 다만 이 mode는
profile editing보다 위험하므로 TUI가 명확한 경고를 표시해야 한다.

```text
Schema edit mode changes project configuration semantics.
Prefer code review for schema changes.
```

TUI가 저장하는 파일은 항상 사람이 읽을 수 있는 TOML이어야 한다. TUI 전용 binary workspace나 hidden
database를 정본으로 삼지 않는다.

## No Runtime TUI

TUI는 host tool이다. Delos DSH는 Confit TUI가 아니다. DSH에서 config를 바꾸는 기능은 금지한다.

## Future UX Ideas

초기 scope 밖이지만, core model이 안정화되면 다음 기능을 검토할 수 있다.

- `why-not` command: 어떤 option을 켤 수 없는 이유를 출력.
- `fix` suggestion: conflict를 해결하는 가능한 edit set 제안.
- side-by-side profile diff.
- Parus/Delos compatibility dashboard.
- release readiness checklist.
