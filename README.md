---
doc_type: tool-spec
status: draft
authority: informative
last_verified: 2026-06-24
---

# Confit

Confit은 `config + fit`에서 온 이름이다. 목적은 Parus와 Delos가 서로 맞는 compile-time
configuration을 선택했는지 검증하고, 각 프로젝트가 소비할 generated config artifact를 만드는
host-side configuration tool을 제공하는 것이다.

Confit은 Delos runtime 기능이 아니다. Parus kernel, Delos runtime, MCU image 안에는 TOML parser,
TUI, constraint solver, config service가 들어가지 않는다. Confit은 build 전에 실행되는 host
tool이며, 초기 결과는 `config.h`, machine-readable report, explanation report 같은 정적 산출물로만
전달된다. `config.cmake`, QStar용 `config/config.qsm`, project-specific build selection module, 기존
호환용 `config.qst`도 명시적인 `--out` directory 아래에 생성되는 보조 build integration artifact로만
다룬다.

## Goals

- TOML 기반 schema와 profile을 읽는다.
- 옵션 간 dependency를 tree가 아니라 DAG로 모델링한다.
- Kconfig의 주요 기능을 흡수하되, dependency 함정을 줄인다.
- Parus와 Delos의 cross-project compatibility를 검증한다.
- C source가 소비할 generated `config.h`를 먼저 만든다.
- Python 대량 의존성 없이 C-first host tool로 동작한다.
- TUI가 profile 생성, profile 편집, schema 편집을 수행할 수 있게 metadata를 보존한다.

## Non-Goals

- Runtime config service를 만들지 않는다.
- Parus 또는 Delos firmware 안에서 TOML을 parse하지 않는다.
- Kconfig 문법을 그대로 복제하지 않는다.
- `select`의 위험한 dependency 우회를 그대로 허용하지 않는다.
- TUI를 core evaluator로 만들지 않는다.
- Build system, package manager, dependency resolver를 Confit 안에 합치지 않는다.
- 실제 Parus/Delos build graph를 암묵적으로 수정하지 않는다.

## Document Map

- [architecture.md](docs/architecture.md): Confit의 전체 구조와 책임 경계.
- [cli-contract.md](docs/cli-contract.md): CLI command, option, exit code, local install 계약.
- [toml-schema.md](docs/toml-schema.md): schema version별 TOML 정본 index.
- [schema-versions.md](docs/schema-versions.md): v1 freeze, v2 hard cut, dispatch와 artifact ABI.
- [schema-v1.md](docs/schema-v1.md): 현재 `schema_version = 1` source 계약.
- [resolution-v1.md](docs/resolution-v1.md): 현재 v1 merge/dependency 의미론.
- [schema-v2.md](docs/schema-v2.md): 새 `schema_version = 2` source 언어.
- [expression-v2.md](docs/expression-v2.md): v2 typed expression 문법.
- [resolution-v2.md](docs/resolution-v2.md): v2 requested/effective resolution.
- [architecture-v2.md](docs/architecture-v2.md): v1/v2 source module과 graph 경계.
- [artifacts-v2.md](docs/artifacts-v2.md): v2 report/header/CMake/QStar와 증분 build artifact.
- [compat-v2.md](docs/compat-v2.md): v2 cross-project typed compatibility constraint.
- [cli-v2.md](docs/cli-v2.md): 기존 CLI의 v2 dispatch, override, explain, migrate 계약.
- [kconfiglib-v2-analysis.md](docs/kconfiglib-v2-analysis.md): upstream Kconfiglib 함수 단위 분석과 채택 판단.
- [migration-v1-v2.md](docs/migration-v1-v2.md): compatibility layer 없는 v1-to-v2 migration.
- [syntax-stability.md](docs/syntax-stability.md): 문법 안정성과 major version 원칙.
- [resolution-dag.md](docs/resolution-dag.md): version별 resolution 정본 index.
- [generators.md](docs/generators.md): generated `config.h`, report, graph, input manifest 산출물.
- [build-selection-workflow.md](docs/build-selection-workflow.md): QStar/CMake가 resolved config와 build selection을 소비하는 정본 흐름.
- [cli-tui.md](docs/cli-tui.md): CLI, TUI, profile/schema editing workflow 전략.
- [cutover-dry-run.md](docs/cutover-dry-run.md): fixture mirror 기반 cutover rehearsal 절차.
- [rollback.md](docs/rollback.md): generated artifact와 TOML edit rollback 규칙.
- [coding-and-doc-rules.md](docs/coding-and-doc-rules.md): 구현팀이 따라야 할 C/Doxygen/문서 규칙.
- [local-build-and-test.md](docs/local-build-and-test.md): Confit local build/test harness와 fixture/golden 규약.
- [release-candidate.md](docs/release-candidate.md): v0 RC 검증 gate, 예제 command, portability review.
- [final-release-note.md](docs/final-release-note.md): 18라운드 종료 시점 실전 투입 후보 판정과 남은 위험.
- [wiki/](wiki/README.md): 처음 쓰는 사용자와 AI 자동화를 위한 한국어 실전 사용 설명서.
- [man/confit.1](man/confit.1): `man confit`으로 읽는 한국어 CLI reference.
- [.github/workflows/ci.yml](.github/workflows/ci.yml): standalone repository용 macOS/Linux build/test CI.

## Recommended Repository Placement

Confit은 standalone repository로 사용할 수 있다. Delos monorepo 안에 vendored copy 또는 subtree로 둘 때는
`tools/confit/` 아래에 위치한다.

이 문서의 명령 예시는 standalone repository root에서는 `.` 기준으로 실행한다. Delos subtree에서 실행할 때는
같은 경로 앞에 `tools/confit/`을 붙이면 된다.

Parus와 Delos 각 프로젝트는 다음 source layout을 가진다.

```text
config/
  project.toml
  options/
  profiles/
  targets/
  compat/
```

Confit generated output은 source tree가 아니라 build tree 아래에 둔다.

```text
build/generated/config/<project>/<profile>/
  config.h
  config.report.json
  config.explain.txt
  config.graph.json
  config.inputs.json
  config.cmake
  config/
    config.qsm
  config.qst
  delos_build_selection/
    delos_build_selection.qsm  # selection/*.toml이 선언한 경우
```

`config/` 아래에는 사람이 관리하는 source config만 둔다. Generated 파일을 `config/`에 쓰는 것은
기본 정책이 아니다.

## Canonical Local Build

Confit의 정본 build engine은 pinned `bmake` 20240909다. Source selection은
`share/mk/confit.sources.mk`에 명시하며 output은 source tree 밖에 둔다.

```sh
# Standalone Confit repository root; BMAKE는 검증된 absolute path다.
"$BMAKE" -r -C . -f Makefile CONFIT_OBJROOT=/tmp/confit-build all
/tmp/confit-build/bin/confit doctor
```

TUI dependency가 없는 host에서는 frontend만 unsupported stub으로 바꾼다.

```sh
"$BMAKE" -r -C . -f Makefile \
  CONFIT_OBJROOT=/tmp/confit-cli-only CONFIT_ENABLE_TUI=no all
```

CMake build와 install script는 migration comparator로만 남아 있으며 canonical backend가 아니다.
Build는 project `config/` tree를 만들거나 수정하지 않는다. Project skeleton 생성과 profile/schema
수정은 명시적인 Confit command가 담당한다.
