---
doc_type: tool-spec
status: draft
authority: informative
last_verified: 2026-06-23
---

# Confit

Confit은 `config + fit`에서 온 이름이다. 목적은 Parus와 Delos가 서로 맞는 compile-time
configuration을 선택했는지 검증하고, 각 프로젝트가 소비할 generated config artifact를 만드는
host-side configuration tool을 제공하는 것이다.

Confit은 Delos runtime 기능이 아니다. Parus kernel, Delos runtime, MCU image 안에는 TOML parser,
TUI, constraint solver, config service가 들어가지 않는다. Confit은 build 전에 실행되는 host
tool이며, 초기 결과는 `config.h`, machine-readable report, explanation report 같은 정적 산출물로만
전달된다. `config.cmake`와 `config.qst` 생성은 장기 확장으로 둔다.

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
- CMake/QStar build graph fragment 생성을 초기 필수 기능으로 넣지 않는다.

## Document Map

- [architecture.md](docs/architecture.md): Confit의 전체 구조와 책임 경계.
- [toml-schema.md](docs/toml-schema.md): TOML source format과 option/profile/target/compat schema.
- [syntax-stability.md](docs/syntax-stability.md): 문법 안정성, versioning, deprecation 원칙.
- [resolution-dag.md](docs/resolution-dag.md): option graph, dependency resolution, conflict explanation.
- [generators.md](docs/generators.md): generated `config.h`, report, graph, input manifest 산출물.
- [cli-tui.md](docs/cli-tui.md): CLI, TUI, profile/schema editing workflow 전략.
- [coding-and-doc-rules.md](docs/coding-and-doc-rules.md): 구현팀이 따라야 할 C/Doxygen/문서 규칙.
- [local-build-and-test.md](docs/local-build-and-test.md): Confit local build/test harness와 fixture/golden 규약.
- [implementation-rounds.md](docs/implementation-rounds.md): 임시 20라운드 구현 자동화 계획과 라운드별 검증/커밋 계약.

## Recommended Repository Placement

Confit prototype은 처음에는 Delos repo의 `tools/confit/` 아래에서 문서와 실험 구현을 시작한다.
안정화 뒤에는 별도 repository 또는 Parus/Delos가 공유하는 tools workspace로 분리할 수 있다.

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
```

`config/` 아래에는 사람이 관리하는 source config만 둔다. Generated 파일을 `config/`에 쓰는 것은
기본 정책이 아니다.
