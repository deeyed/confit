---
doc_type: tool-spec
status: draft
authority: informative
last_verified: 2026-06-23
---

# Confit Architecture

Confit은 Parus와 Delos를 위한 compile-time configuration authority다. Confit의 핵심 역할은
configuration source를 읽고, option DAG를 구성하고, profile을 resolve하고, conflict를 설명하고,
build system과 C code가 소비할 generated artifact를 만드는 것이다.

## Layer Model

```text
TOML source files
  -> parser adapter
  -> schema loader
  -> option graph builder
  -> profile resolver
  -> compatibility checker
  -> artifact generators
  -> generated config consumers
```

각 layer는 명확히 분리한다.

| Layer | 책임 |
|---|---|
| Parser adapter | TOML file을 syntax tree 또는 table 형태로 읽는다. |
| Schema loader | option, choice, profile, target, compat block을 typed model로 만든다. |
| Graph builder | option reference, dependency, conflict, force, recommend edge를 DAG로 구성한다. |
| Resolver | default, profile override, target override, user override를 합성한다. |
| Checker | range, type, dependency, conflict, cross-project assertion을 검증한다. |
| Generator | `config.h`, report, explanation, graph, input manifest를 만든다. |
| Frontend | CLI 또는 TUI를 제공한다. Core evaluator를 대체하지 않는다. |

## Project Roles

Parus와 Delos는 각자 독립 project config를 가진다. Confit은 둘 중 하나만 검사할 수도 있고, 둘을
함께 로드해서 compatibility를 검사할 수도 있다.

```text
Parus config
  + Delos config
  + system compatibility profile
  -> compatibility report
```

예를 들어 Parus가 `rt_executor.delos = true`를 선택했는데 Delos가 `dcg.enabled = false`라면,
Confit은 build 전에 오류를 내야 한다.

## Runtime Boundary

Confit은 host-side tool이다. 다음 요소는 Parus/Delos runtime image에 들어갈 수 없다.

- TOML parser
- TUI code
- interactive config editor
- constraint solver
- config mutation service
- profile merge engine

Runtime은 generated `#define` 또는 generated constant만 본다. Runtime에서 config를 바꾸는 기능은
Confit의 목표가 아니다.

## Build System Boundary

Confit은 CMake나 QStar를 대체하지 않는다. Confit은 build graph를 만들지 않는다. 초기 Confit은
C code가 소비할 `config.h`와 검증 report를 먼저 만든다. CMake/QStar fragment 생성은 Confit core,
TUI, Parus/Delos 적용이 안정화된 뒤 붙이는 장기 확장이다.

```text
Confit:
  "이 profile에서는 DDC가 켜져 있고 DSH RX는 꺼져 있다."

C source:
  "generated config.h를 include해서 compile-time #define을 본다."

CMake/QStar future integration:
  "같은 resolved config에서 생성된 fragment를 읽고 source와 flag를 선택한다."
```

QStar의 `qstar.config`와 future Confit `config.qst`는 역할이 다르다. Confit은 profile 결과를
생성하고, QStar는 그 결과를 build graph policy로 소비한다. 이 통합은 초기 필수 기능이 아니다.

## Dependency Philosophy

Kconfig는 tree menu를 사용자에게 보여주지만, 실제 문제는 option dependency graph다. Confit은
사용자 표시용 category tree와 정본 dependency graph를 분리한다.

- Category tree: TUI/문서/검색용 view.
- Option DAG: resolver와 checker가 사용하는 권위 graph.

깊은 menu nesting은 피한다. 사용자는 option 이름, category, tag, dependency explanation으로 탐색할
수 있어야 한다.

## Safety Philosophy

Confit은 illegal configuration을 build 전에 막는 도구다. “사용자가 선택했으니 그대로 진행”하는
방식은 Parus/Delos에 맞지 않는다.

특히 다음 조합은 강하게 검증한다.

- debug-only 기능이 release profile에 남는 경우
- DDC/DSH RX parser가 release image에 들어가는 경우
- Parus가 Delos DCG를 기대하지만 Delos가 DCG를 끈 경우
- board profile과 arch profile이 서로 맞지 않는 경우
- generated header와 QStar/CMake config가 서로 다른 값을 갖는 경우

## Implementation Language

Confit core는 C로 작성한다. 목표는 C23-host-friendly code지만, host portability를 위해 C17 subset을
초기 기준으로 둘 수 있다. Runtime 프로젝트와 달리 hosted libc 사용은 허용되지만, hosted 기능은
`src/host/` 같은 host boundary에 엄격하게 격리한다.

TOML parser는 permissive license C library를 vendoring하거나 wrapper로 연결한다. Parser API는
Confit 내부 adapter 뒤에 숨긴다. 나중에 parser를 바꾸더라도 core model과 generator가 흔들리면 안
된다.

## Source Layout Boundary

구현은 다음 경계를 따른다.

```text
tools/confit/
  include/confit/
  src/core/
  src/parser/
  src/generator/
  src/host/
  src/cli/
  src/tui/
  vendor/
```

`src/core/`는 terminal, filesystem, path separator, environment variable, clock, locale을 직접 알지
않는다. 이런 hosted 기능은 `src/host/`가 제공하는 작은 interface 뒤에 둔다.
