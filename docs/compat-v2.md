---
doc_type: compatibility-contract
status: accepted-design
authority: normative
implementation_status: implemented-library
last_verified: 2026-07-24
---

# Confit V2 Cross-Project Compatibility

이 문서는 두 개 이상의 `schema_version = 2` project snapshot을 함께 검증하는
compatibility 계약을 정의한다. Compatibility는 각 project 값을 변경하지 않고
최종 effective snapshot 사이의 assertion만 검사한다.

## Version 규칙

- 참여 project는 모두 v2여야 한다.
- V1/v2 mixed compatibility는 지원하지 않는다.
- 각 project는 독립적으로 resolution에 성공해야 한다.
- Compatibility resolver가 project profile/target/user value를 덮어쓰지 않는다.

V1 compatibility file은 기존 v1 checker가 처리한다.

## Compatibility File

```toml
[compat]
name = "parus-delos"
schema_version = 2

[projects]
parus = "../parus"
delos = "../delos"

[[constraint]]
id = "parus-delos.executor-contract"
when = 'enabled(parus::parus.executor.delos_target)'
require = 'enabled(delos::delos.dcg.enabled)'
message = "Parus Delos executor에는 Delos DCG가 필요합니다."
```

`projects` key는 expression에서 사용하는 local alias다. `::` 왼쪽은 alias,
오른쪽은 해당 project의 canonical option id다. alias는 소문자로 시작하고
소문자, 숫자, `_`만 쓴다. Compatibility loader는 declaration과 runtime snapshot
alias가 정확히 같은지, 모든 snapshot이 `schema_version = 2`와
`confit-artifact-v2` ABI인지 먼저 hard error로 검사한다.

Embedding caller는 snapshot view에 canonical config root, source hash, snapshot
hash를 optional expected identity로 함께 넘길 수 있다. 하나라도 현재 immutable
snapshot과 다르면 stale/path identity mismatch로 hard error다. 이 precondition은
compatibility source가 project를 자동 reload하거나 값을 갱신하지 않게 한다.

## Project Selection

Compatibility 실행은 project별 profile과 target을 명시적으로 받는다.

```text
confit compat \
  --compat config/compat/parus-delos.toml \
  --project parus=/path/to/parus \
  --profile parus=parus-delos-debug \
  --target parus=qemu-virt-aarch64 \
  --project delos=/path/to/delos \
  --profile delos=parus-delos-debug \
  --target delos=sim-dsh
```

Compatibility file의 path는 default discovery 용도로만 사용할 수 있으며 CLI가
명시한 project root와 다르면 오류다.

## Expression

Cross-project expression은 [expression-v2.md](expression-v2.md)의 type
규칙을 그대로 사용한다.

- Alias와 option id는 link 단계에서 존재해야 한다.
- 서로 다른 project의 enum declaration을 직접 비교할 수 없다.
- Cross-project enum 비교가 필요하면 string compatibility key를 별도 stable
  option으로 선언한다.
- Requested value가 아니라 effective value를 읽는다.
- Project의 provenance는 compatibility failure trace에 연결한다.

## Constraint

Compatibility constraint는 값을 변경하지 않는다.

```toml
[[constraint]]
id = "parus-delos.target-arch"
when = 'enabled(parus::parus.executor.delos_target)'
require = '''
  parus::parus.compat.delos_arch
    == delos::delos.compat.arch
'''
message = "Parus와 Delos의 compatibility architecture가 일치해야 합니다."
```

`when`은 생략하면 `true`다. action은 `require` 또는 `forbid` 중 정확히 하나여야
한다. `require`는 action expression이 true일 때 통과하고, `forbid`는 false일 때
통과한다.

```toml
[[constraint]]
id = "parus-delos.debug-forbid"
when = 'enabled(parus::parus.debug.enabled)'
forbid = 'enabled(delos::delos.release.only_driver)'
message = "Parus debug profile은 Delos release-only driver를 선택할 수 없습니다."
```

긴 expression은 TOML multi-line literal string을 사용할 수 있다. Whitespace는
expression lexer가 token separator로 처리한다.

## Compatibility Key

Project 내부 enum/type은 서로 독립적이므로 cross-project ABI로 비교할 값은
stable compatibility option으로 노출한다.

```toml
[option."delos.compat.arch"]
type = "string"
write_domain = "computed"
computed = 'enum_name(delos.target.arch)'
owner = "delos-compat"
since = "0.2.0"
stability = "stable"
emit = []
```

`enum_name()`은 명시적인 enum-to-string 변환이다. 일반 enum/string 비교에는
암묵 변환이 없다.

Compatibility key는 다른 project가 의존하는 public ABI이므로 rename 시 양쪽
project migration이 필요하다.

## Report

V2 compatibility report schema:

```json
"schema": "confit-compat-report-v2"
```

포함 항목:

```text
compat file/source hash
project alias별 snapshot identity와 hash
profile/target selection
constraint pass/fail/not-applicable
failure causal slice
project provenance reference
```

Report order는 alias, constraint id lexical order다.

각 causal value는 alias, option id, declared type, effective value, effective
provenance, source file/line/column을 보존한다. 따라서 실패 report만으로도 두
project의 어떤 effective 값이 비교되었는지 확인할 수 있다. `when`이 false인
constraint는 `not_applicable`으로 report에 남고 action expression은 평가하지
않는다.

## Error

다음은 hard error다.

- unknown project alias/option
- mixed schema version
- project resolution failure
- expression type mismatch
- duplicate constraint id
- failed compatibility constraint
- project path identity mismatch
- stale snapshot/source hash mismatch

Compatibility failure를 해결하기 위해 어느 project 값도 자동으로 변경하지
않는다.
