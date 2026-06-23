---
doc_type: tool-spec
status: draft
authority: informative
last_verified: 2026-06-23
---

# CLI And TUI Strategy

Confit의 첫 interface는 CLI다. TUI는 ncurses 기반 host frontend로 제공한다. TUI가 없더라도 모든
configuration resolution, validation, generation, explanation은 CLI만으로 가능해야 한다.

## CLI Commands

추천 command surface:

```sh
confit check --project /path/to/delos --profile sim-dsh
confit gen --project /path/to/delos --profile sim-dsh --out build/generated/config/delos/sim-dsh
confit explain --project /path/to/delos --profile sim-dsh delos.debug.dsh
confit compat --parus /path/to/kairon --delos /path/to/delos --profile parus-delos-debug
confit list --project /path/to/delos --category debug
confit graph --project /path/to/delos --profile sim-dsh --format dot
confit tui --project /path/to/delos --profile sim-dsh
confit tui --project /path/to/delos --schema-edit
```

## Command Semantics

| Command | 의미 |
|---|---|
| `check` | parse, schema validate, graph validate, profile resolve를 수행한다. |
| `gen` | `check` 후 `config.h`, report, explanation, graph, input manifest를 만든다. |
| `explain` | 특정 option의 value와 이유를 출력한다. |
| `compat` | 여러 project profile 조합의 compatibility를 검사한다. |
| `list` | option 목록을 category/tag/filter로 표시한다. |
| `graph` | dependency graph를 DOT/JSON으로 출력한다. |
| `tui` | profile editing 또는 schema editing TUI를 실행한다. Core evaluator를 재구현하지 않는다. |

## Exit Codes

```text
0  success
1  invalid command line
2  parse error
3  schema error
4  dependency or conflict error
5  compatibility error
6  generation error
7  internal error
```

Codex agent와 CI가 오류 종류를 분리할 수 있어야 한다.

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
| `?` | 현재 mode의 key help를 status line에 표시한다. |
| `Esc` | 열린 prompt를 취소한다. 기본 화면에서는 현재 filter를 취소하거나 cancel 상태를 표시한다. |
| `q` | TUI를 종료한다. |

Profile editor:

| Key | 동작 |
|---|---|
| `/` | option id, prompt, help, category, tag 검색어를 입력한다. |
| `c` | category filter를 입력한다. |
| `t` | tag filter를 입력한다. |
| `x` | search/category/tag filter를 모두 지운다. |
| `e` | 현재 option 값을 edit한다. 현재 row가 category heading이면 Enter/Space와 같이 펼치거나 접는다. |
| `s` | profile TOML을 저장한다. |

Schema editor:

| Key | 동작 |
|---|---|
| `n` | 새 schema option draft를 만든다. |
| `p` | 현재 option prompt를 수정한다. |
| `h` | 현재 option help text를 수정한다. `?` help와 구분한다. |
| `c` | 현재 option category를 수정한다. |
| `t` | 현재 option tag comma-list를 수정한다. |
| `r` | 현재 option range를 수정한다. |
| `o` | 현재 enum option choices comma-list를 수정한다. |
| `s` | schema TOML을 저장하기 전에 full validation을 수행하고 저장한다. |

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
