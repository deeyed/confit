---
doc_type: implementation-plan
status: temporary
authority: operational
last_verified: 2026-06-23
---

# Confit 20-Round Implementation Plan

이 문서는 Confit 구현 자동화를 위한 임시 운영 계획이다. 사용자가 이후 라운드에서
`다음 라운드로 진행한다`라고만 말하더라도, Codex 구현팀은 이 문서를 기준으로 다음 미완료 라운드를
판별하고 구현, 검증, stage, commit, push를 수행한다.

이 문서는 장기 schema 정본이 아니다. 영속적인 규칙은 다음 문서를 우선한다.

1. `tools/confit/README.md`
2. `tools/confit/docs/architecture.md`
3. `tools/confit/docs/toml-schema.md`
4. `tools/confit/docs/resolution-dag.md`
5. `tools/confit/docs/generators.md`
6. `tools/confit/docs/cli-tui.md`
7. `tools/confit/docs/coding-and-doc-rules.md`

## Fixed Boundary

모든 라운드의 수정 범위는 반드시 다음 디렉터리 안으로 한정한다.

```text
/Users/gungye/workspace/delos/tools/confit
```

다음 경계는 라운드와 무관하게 항상 지킨다.

- `/Users/gungye/workspace/delos/tools/confit` 밖 파일은 수정하지 않는다.
- Parus/Delos 실제 `config/` source tree는 수정하지 않는다.
- Parus/Delos runtime source는 수정하지 않는다.
- Delos CMake, QStar, board profile, simulator, runtime build graph는 수정하지 않는다.
- Parus/Delos 샘플은 `tools/confit/tests/fixtures/` 아래 fixture로만 둔다.
- `config.cmake` generator와 `config.qst` generator는 구현하지 않는다.
- Runtime config service를 만들지 않는다.
- TOML parser와 TUI code가 Parus/Delos runtime에 들어가는 구조를 만들지 않는다.
- `src/core/`에서 hosted API를 직접 호출하지 않는다.
- TUI가 저장하는 정본은 TOML이어야 하며, hidden binary DB를 만들지 않는다.

## Automation Trigger

사용자가 다음 문장만 보내면, 구현팀은 다음 미완료 라운드를 수행한다.

```text
다음 라운드로 진행한다
```

라운드 판별 순서:

1. `git log --oneline`에서 아래 commit message가 이미 존재하는지 확인한다.
2. 존재하지 않는 가장 낮은 번호의 라운드를 다음 라운드로 선택한다.
3. 같은 commit message가 이미 있으나 구현이 불완전해 보이면, 새 라운드로 넘기지 말고 해당 라운드를
   보수한다. 이때 commit message는 상황에 맞는 `fix(confit): ...` 또는 `test(confit): ...`를 쓴다.
4. 사용자가 명시적으로 다른 라운드를 지시하면 그 지시를 우선한다. 그래도 수정 범위 제한은 유지한다.

각 라운드를 시작할 때 수행할 기본 확인:

```sh
git status --short
rg --files tools/confit
```

관련 문서가 필요한 경우 먼저 읽는다. 특히 schema, resolver, generator, TUI를 구현하는 라운드는
해당 정본 문서를 다시 확인한다.

## End-of-Round Protocol

모든 라운드는 끝날 때 다음 순서를 수행한다.

```text
검증 -> 관련 파일만 stage -> commit -> git push origin main
```

구체 절차:

1. 라운드별 완료 검증을 실행한다.
2. `git diff --check`를 실행한다.
3. `git status --short`로 수정 범위가 `tools/confit/` 안인지 확인한다.
4. `git add`는 해당 라운드에서 만든 관련 파일만 대상으로 한다.
5. 지정된 commit message로 commit한다.
6. `git push origin main`을 실행한다.
7. 최종 보고에는 수행한 라운드, 핵심 변경, 검증 결과, commit/push 결과를 짧게 남긴다.

검증 명령은 라운드가 진행되며 구체화된다. 아직 build harness가 없는 초기 라운드에서는 그 라운드에서
도입한 최소 실행 파일이나 smoke test를 사용한다.

## Common Implementation Rules

- 공개 header의 함수, 타입, enum, macro에는 한국어 Doxygen 주석을 쓴다.
- Core는 C로 작성한다. C17 subset을 기본으로 하되 C23-friendly style을 유지한다.
- C++, Rust, Python-heavy generator는 명시 결정 전까지 쓰지 않는다.
- Parser와 TUI vendor code는 각각 `vendor/toml/`, `vendor/tui/` 뒤에 격리한다.
- CLI만 사람이 읽는 diagnostic을 format한다.
- Core model은 numeric status와 diagnostic record를 함께 사용한다.
- Generated artifact에는 timestamp나 absolute path를 넣지 않는다.
- Deterministic ordering을 유지한다.
- fixture는 작게 시작하되, negative fixture를 반드시 누적한다.
- 실제 Parus/Delos config를 적용하지 말고 fixture로만 재현한다.

## Round 1: Project Skeleton

목표:

- `include/confit/`, `src/core/`, `src/host/`, `src/cli/`, `tests/`를 만든다.
- `confit --version`과 `confit help`만 동작하게 한다.
- 상태 코드와 diagnostic 기본형을 추가한다.
- 아직 TOML, schema, resolver, generator는 구현하지 않는다.

권장 파일:

- `include/confit/status.h`
- `include/confit/diagnostic.h`
- `src/core/status.c`
- `src/core/diagnostic.c`
- `src/cli/main.c`
- `tests/smoke/` 또는 `tests/unit/`의 최소 smoke test

완료 검증:

```sh
confit --version
confit help
unit smoke
git diff --check
```

Commit:

```text
feat(confit): add project skeleton and cli shell
```

## Round 2: Build/Test Harness

목표:

- Confit 자체 CMake 또는 최소 build script를 `tools/confit` 안에 둔다.
- 테스트 runner를 만든다.
- fixture directory와 golden output convention을 정의한다.
- CI-like local command를 문서화한다.

범위:

- `tools/confit/CMakeLists.txt` 또는 `tools/confit/build.sh` 같은 local-only harness.
- `tools/confit/tests/fixtures/`
- `tools/confit/tests/golden/`
- `tools/confit/docs/`의 local command 문서.

완료 검증:

```sh
clean build
empty unit suite pass
documented CI-like local command
git diff --check
```

Commit:

```text
build(confit): add local build and test harness
```

## Round 3: Host Boundary

목표:

- `src/host/`에 file, path, stdout, stderr abstraction을 추가한다.
- Core는 `fopen`, path separator, terminal API를 직접 사용하지 않는다.
- CLI는 host adapter를 통해 출력과 파일 접근을 수행한다.

권장 파일:

- `include/confit/host.h`
- `src/host/host_file.c`
- `src/host/host_path.c`
- `src/host/host_io.c`

완료 검증:

```sh
source grep shows no direct hosted API in src/core
unit tests pass
git diff --check
```

Commit:

```text
feat(confit): isolate hosted platform services
```

## Round 4: TOML Vendor Adapter

목표:

- Permissive C TOML library를 `vendor/toml/`에 격리한다.
- `src/parser/` adapter를 추가한다.
- 아직 schema 해석은 하지 않는다.
- TOML load와 error 위치 reporting만 검증한다.

범위:

- `vendor/toml/`
- `include/confit/parser.h`
- `src/parser/`
- valid/invalid TOML fixtures

완료 검증:

```sh
valid TOML fixture parse tests
invalid TOML fixture parse tests
git diff --check
```

Commit:

```text
feat(confit): add toml parser adapter
```

## Round 5: Core Model Types

목표:

- `ConfitProject`, `ConfitOption`, `ConfitValue`, `ConfitChoice`, `ConfitProfile`,
  `ConfitTarget` 구조를 정의한다.
- bool, int, uint, string, enum skeleton을 만든다.
- Allocation ownership과 free 규칙을 문서화한다.

권장 파일:

- `include/confit/model.h`
- `src/core/model.c`
- model construction/free tests

완료 검증:

```sh
model construction/free tests
ownership tests
git diff --check
```

Commit:

```text
feat(confit): define core configuration model
```

## Round 6: Schema Loader v1

목표:

- `project.toml`과 `options/*.toml`을 로드한다.
- Duplicate option id, unknown field, missing type, invalid id를 검사한다.
- `schema_version = 1`을 처리한다.

범위:

- `src/schema/`
- `include/confit/schema.h`
- positive/negative schema fixtures

완료 검증:

```sh
schema positive fixtures
schema negative fixtures
git diff --check
```

Commit:

```text
feat(confit): load option schema files
```

## Round 7: Type System Full v1

목표:

- bool, int, uint, hex, string, enum, float, path를 지원한다.
- float는 finite value만 허용한다.
- range와 enum candidate를 검증한다.

완료 검증:

```sh
type coercion tests
range tests
float negative tests
git diff --check
```

Commit:

```text
feat(confit): implement option type validation
```

## Round 8: Profile/Target Loader

목표:

- `profiles/*.toml`, `targets/*.toml`을 로드한다.
- `base`, `values`, `target`, `claim.level`을 처리한다.
- 아직 resolve는 단순 merge로 둔다.

완료 검증:

```sh
profile inheritance fixture tests
target fixture tests
git diff --check
```

Commit:

```text
feat(confit): load profiles and targets
```

## Round 9: Dependency Graph Builder

목표:

- `requires`, `conflicts`, `recommends`, `forces`, `visible_if` edge를 생성한다.
- Unknown reference와 self-edge를 검사한다.

완료 검증:

```sh
graph JSON dump smoke
unknown dependency tests
git diff --check
```

Commit:

```text
feat(confit): build option dependency graph
```

## Round 10: Graph Validation

목표:

- Cycle detection을 구현한다.
- Illegal `forces`를 검증한다.
- Visible option force warning/error를 만든다.
- Conflict contradiction을 검증한다.
- Large synthetic graph fixture를 추가한다.

완료 검증:

```sh
cycle negative tests
forces negative tests
conflict negative tests
large synthetic graph fixture
git diff --check
```

Commit:

```text
feat(confit): validate dependency graph constraints
```

## Round 11: Resolver v1

목표:

- default -> base profile -> target -> profile override -> user override 순서로 resolve한다.
- Deterministic ordering을 보장한다.

주의:

- User override는 dependency와 conflict를 이길 수 없다.
- Report ordering은 option id lexical order를 따른다.

완료 검증:

```sh
merge order golden tests
stable output hash test
git diff --check
```

Commit:

```text
feat(confit): resolve profiles deterministically
```

## Round 12: Explanation Engine

목표:

- why enabled/disabled를 설명한다.
- who set value를 기록한다.
- who requires/conflicts를 추적한다.
- force/recommend trace를 생성한다.
- `confit explain` command를 추가한다.

완료 검증:

```sh
explain golden tests
git diff --check
```

Commit:

```text
feat(confit): add explanation engine
```

## Round 13: Config Header Generator

목표:

- `config.h`를 생성한다.
- `#define`, include guard, source hash, stable ordering을 지원한다.
- Timestamp는 넣지 않는다.
- CMake/QStar generator는 구현하지 않는다.

완료 검증:

```sh
config.h golden tests
git diff --check
```

Commit:

```text
feat(confit): generate C configuration header
```

## Round 14: Reports v1

목표:

- `config.report.json`
- `config.explain.txt`
- `config.graph.json`
- `config.inputs.json`
- Deterministic JSON writer

완료 검증:

```sh
golden JSON tests
golden text tests
git diff --check
```

Commit:

```text
feat(confit): emit reports and input manifests
```

## Round 15: Compatibility Checker

목표:

- `compat/*.toml`을 로드한다.
- Parus/Delos fixture config를 동시에 resolve한다.
- `assert when/requires/forbids`를 처리한다.
- 실제 repo 수정 없이 fixture만 사용한다.

완료 검증:

```sh
compatibility pass fixture tests
compatibility fail fixture tests
git diff --check
```

Commit:

```text
feat(confit): add cross-project compatibility checks
```

## Round 16: CLI Completion

목표:

- `check`, `gen`, `explain`, `compat`, `list`, `graph` command를 완성한다.
- Exit code 정책을 고정한다.
- `--project`, `--profile`, `--target`, `--out`을 처리한다.

Exit code:

```text
0 success
1 invalid command line
2 parse error
3 schema error
4 dependency or conflict error
5 compatibility error
6 generation error
7 internal error
```

완료 검증:

```sh
CLI integration tests
git diff --check
```

Commit:

```text
feat(confit): complete command line workflow
```

## Round 17: TUI Vendor + Shell

목표:

- TUI library를 `vendor/tui/`에 격리한다.
- `confit tui` 실행 skeleton을 만든다.
- Keyboard input, list view, status bar를 만든다.
- Core dependency inversion을 만들지 않는다.

완료 검증:

```sh
TUI starts in fixture project
no crash
git diff --check
```

Commit:

```text
feat(confit): add tui frontend skeleton
```

## Round 18: TUI Profile Editor

목표:

- Option 검색을 지원한다.
- Category/tag filter를 지원한다.
- bool toggle, enum select, int/float/string edit를 지원한다.
- Profile TOML 저장을 구현한다.
- 저장 전 full validation을 수행한다.

완료 검증:

```sh
TUI scripted/input harness where possible
unit and golden tests
git diff --check
```

Commit:

```text
feat(confit): implement tui profile editing
```

## Round 19: TUI Schema Editor + Computer Use QA

목표:

- Schema edit mode를 추가한다.
- Option 생성, prompt/help/category/tag/range/choice 수정을 지원한다.
- Schema edit mode 진입 시 경고를 표시한다.
- Computer Use로 실제 TUI를 열어 profile 생성과 schema 편집 흐름을 수동 검증한다.

필수 경고 문구:

```text
Schema edit mode changes project configuration semantics.
Prefer code review for schema changes.
```

완료 검증:

```sh
unit/golden tests
Computer Use manual transcript
git diff --check
```

Commit:

```text
feat(confit): add guarded tui schema editing
```

## Round 20: Polish, Scale, Release Candidate

목표:

- 수천 option synthetic stress test를 추가한다.
- Windows/macOS/Linux portability review를 수행한다.
- Docs와 examples를 정리한다.
- Computer Use로 최종 TUI 전체 흐름을 검증한다.

최종 TUI 검증 흐름:

```text
browse -> search -> toggle -> explain -> save -> schema-edit warning
```

완료 검증:

```sh
full test suite
stress test
Computer Use QA notes
git diff --check
```

Commit:

```text
chore(confit): harden v0 configuration workflow
```

## Target End State

20라운드 후 다음 명령이 동작해야 한다.

```sh
confit check --project fixtures/delos --profile sim-dsh
confit gen --project fixtures/delos --profile sim-dsh --out build/generated/config/delos/sim-dsh
confit explain --project fixtures/delos --profile sim-dsh delos.debug.dsh
confit compat --parus fixtures/parus --delos fixtures/delos --profile parus-delos-debug
confit tui --project fixtures/delos --profile sim-dsh
```

생성물:

```text
config.h
config.report.json
config.explain.txt
config.graph.json
config.inputs.json
```

그 이후 별도 라운드에서만 Parus/Delos 실제 적용을 검토한다. `config.cmake`와 `config.qst` generator도
Confit v0 core, CLI, TUI, compatibility workflow가 안정화된 뒤의 별도 확장이다.
