---
doc_type: tool-spec
status: draft
authority: informative
last_verified: 2026-06-23
---

# Syntax Stability

Confit 문법은 Parus와 Delos가 수천 개 option을 다루게 되어도 기존 profile과 schema를 깨지 않도록
설계한다. 초기 구현이 작더라도 source format은 장기 확장을 전제로 고정한다.

## Version Fields

모든 project schema는 명시 version을 가진다.

```toml
[project]
name = "delos"
schema_version = 1
```

Profile과 target file도 version을 가질 수 있다.

```toml
[profile]
name = "sim-dsh"
schema_version = 1
```

`schema_version = 1`의 기존 의미는 깨지면 안 된다. 새로운 기능이 필요하면 optional field를 추가하고,
기존 field 의미를 바꾸지 않는다.

## Stable Option Identity

Option id는 compatibility surface다. 한 번 공개된 option id는 쉽게 바꾸지 않는다.

```text
delos.debug.dsh
parus.boot.path
```

이름을 바꿔야 한다면 새 option을 추가하고, 기존 option은 deprecated alias로 남긴다. Alias는
explanation report에 표시되어야 한다.

```toml
[option."delos.debug.dsh"]
type = "bool"
default = false
owner = "delos-runtime"
since = "0.1.0"
stability = "stable"
deprecated_aliases = ["delos.debug.old_dsh"]
```

Loader는 profile과 target 값에서 deprecated alias를 만나면 canonical option id로 해석해야 한다.
동일 alias가 둘 이상의 option을 가리키거나 실제 option id와 충돌하면 schema error다.

## Additive Evolution

Confit 문법은 additive evolution을 기본으로 한다.

허용:

- 새 optional field 추가.
- 새 type 추가.
- 새 report field 추가.
- 새 TUI metadata 추가.
- 새 generator 추가.

금지:

- 기존 field의 type 변경.
- 기존 `requires` 의미 변경.
- 기존 `conflicts` 의미 변경.
- 기존 profile merge order 변경.
- 기존 option id를 조용히 제거.

## Reserved Namespaces

Project option은 project prefix를 가진다.

```text
delos.*
parus.*
system.*
```

`system.*`은 cross-project compatibility와 shared profile에서 사용한다. 개별 project가 임의로
`system.*` option을 소유하면 안 된다.

## Unknown Fields

초기 schema loader는 unknown field를 기본적으로 error로 처리한다. 단, `x_` prefix field는 tool
extension metadata로 허용할 수 있다.

```toml
x_ui_group = "debug"
```

`x_` field는 resolver authority가 아니다. Core semantics에 영향을 주는 field는 정식 문법으로
승격해야 한다.

## Deprecation

Deprecation은 명시 metadata로 표현한다.

```toml
[option."delos.debug.old_dsh"]
type = "bool"
default = false
owner = "delos-runtime"
since = "0.1.0"
stability = "deprecated"
deprecated = true
replaced_by = "delos.debug.dsh"
```

Deprecated option은 즉시 삭제하지 않는다. Confit은 warning을 내고, generated report에 migration
hint를 남긴다.

`owner`, `since`, `stability` metadata는 장기 유지보수 표면이다. 일반 validation은 누락을 warning으로
보고, strict validation은 warning을 failure로 승격한다.

## Scale Requirements

Confit은 수천 개 option을 가정한다.

- Option lookup은 id 기반 index를 가져야 한다.
- Graph validation은 deterministic해야 한다.
- Report output order는 stable sort를 따라야 한다.
- TUI는 깊은 tree traversal이 아니라 검색, tag, dependency explanation을 기본 탐색 수단으로 둔다.
- Explanation은 큰 graph에서도 특정 option 중심으로 빠르게 조회 가능해야 한다.

Round 20 local gate는 2,500개 option synthetic project를 생성해 `check`, `list`, `graph`, `gen`을
실행한다. 이 수치는 최소 release-candidate smoke 기준이며, 이후 CI에서는 더 큰 profile과 platform
matrix를 추가한다.
