---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-24
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

`schema_version = 1`의 기존 의미는 깨지면 안 된다. Typed expression, computed
option, write domain처럼 의미를 바꾸는 기능은 v1 optional field로 추가하지 않고
`schema_version = 2`에서만 제공한다.

V1과 v2의 dispatch와 혼합 금지는 [schema-versions.md](schema-versions.md)를
따른다.

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

## V1 Evolution

V1 문법은 의미 보존을 기본으로 한다.

허용:

- 기존 resolution에 영향을 주지 않는 optional display metadata 추가.
- 새 report field 추가.
- 새 TUI metadata 추가.
- 새 generator 추가.

금지:

- 기존 field의 type 변경.
- 기존 `requires` 의미 변경.
- 기존 `conflicts` 의미 변경.
- 기존 profile merge order 변경.
- 기존 option id를 조용히 제거.
- `forces` 또는 `recommends`가 값을 변경하게 만들기.
- visibility가 requested/effective value를 바꾸게 만들기.
- v2 expression을 v1 field의 새 해석으로 넣기.

## V2 Hard Cut

V2는 v1 source compatibility를 제공하지 않는다.

- V1 field를 v2 loader가 추측해서 변환하지 않는다.
- V1/v2 profile, target, option file을 섞지 않는다.
- Alias로 v1 option을 runtime resolution에 남기지 않는다.
- Migration은 offline candidate 생성과 semantic diff로 수행한다.

V2 안에서 장래 additive field를 추가할 수는 있지만 expression type, write
domain, requested/effective 구분, deterministic resolution 같은 v2 핵심 의미는
같은 major version에서 바꾸지 않는다.

## Reserved Namespaces

Project option은 project prefix를 가진다.

```text
delos.*
parus.*
system.*
```

`system.*`은 cross-project compatibility가 명시적으로 소유하는 경우에만
사용한다. 개별 project가 임의로 `system.*` option을 소유하면 안 된다.

## Unknown Fields

초기 schema loader는 unknown field를 기본적으로 error로 처리한다. 단, `x_` prefix field는 tool
extension metadata로 허용할 수 있다.

```toml
x_ui_group = "debug"
```

`x_` field는 resolver authority가 아니다. Core semantics에 영향을 주는 field는
해당 schema major version의 정식 문법으로 승격해야 한다.

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

이 alias/deprecation 동작은 v1 compatibility surface다. V2에서는 rename을
offline migration으로 수행하며 v1 `deprecated_aliases`를 읽지 않는다.

`owner`, `since`, `stability` metadata는 장기 유지보수 표면이다. 일반 validation은 누락을 warning으로
보고, strict validation은 warning을 failure로 승격한다.

## Scale Requirements

Confit은 수천 개 option을 가정한다.

- Option lookup은 id 기반 index를 가져야 한다.
- Graph validation은 deterministic해야 한다.
- Report output order는 stable sort를 따라야 한다.
- TUI는 얕은 menu stack navigation을 지원하되, 검색, tag, dependency explanation을 기본 탐색 수단으로
  유지한다. 권장 menu depth는 2단계이고, 4단계 이상은 strict mode에서 failure로 승격 가능한 설계
  warning으로 취급한다.
- Explanation은 큰 graph에서도 특정 option 중심으로 빠르게 조회 가능해야 한다.

V1 local scale gate는 기존 5,000 option synthetic project를 유지한다. V2는
20,000 option과 100,000 expression/constraint edge를 최소 stress 기준으로 삼고,
incremental TUI update도 별도로 측정한다.
