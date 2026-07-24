---
doc_type: semantics-contract
status: accepted-design
authority: normative
implementation_status: not-implemented
last_verified: 2026-07-24
---

# Confit Resolution Version 2

이 문서는 `schema_version = 2` project가 requested assignment를 immutable
effective snapshot으로 만드는 정확한 순서를 정의한다.

## 핵심 불변식

1. Resolver는 사용자의 requested value를 조용히 다른 값으로 고치지 않는다.
2. 서로 다른 write domain은 같은 option을 쓸 수 없다.
3. Visibility는 effective value를 변경하지 않는다.
4. Suggestion은 effective value를 변경하지 않는다.
5. Computed option만 다른 option으로부터 값을 생산한다.
6. 모든 generator는 동일 snapshot을 사용한다.
7. 실패한 resolution은 부분 성공 snapshot을 반환하지 않는다.

## 입력

Resolution 입력은 다음과 같다.

```text
compiled v2 project
selected profile name 또는 없음
selected target name 또는 없음
typed user override 목록
strict lint policy
```

Schema parse/link/type-check에 실패한 project는 resolver 입력이 될 수 없다.

## Phase 1: Target 이름 선택

Target name은 다음 순서로 선택한다.

```text
explicit CLI/TUI target
-> selected leaf profile target
-> project default_target
-> 없음
```

서로 다른 source가 target name을 제공했다는 사실은 provenance에 남긴다. Explicit
target이 profile target과 다르면 warning이 아니라 정상적인 명시 override이며,
snapshot에 두 이름을 함께 기록한다.

## Phase 2: Profile/Target Chain

Profile과 target은 각각 단일 base chain을 가진다.

```text
profile root base -> ... -> selected profile
target root base  -> ... -> selected target
```

Cycle은 compile 단계에서 오류다. 동일 chain 안에서 뒤의 assignment가 앞의
assignment를 덮을 수 있지만 전체 assignment ledger는 provenance에 보존한다.

Base profile로 참조되는 profile이 target을 선언하면 schema error다.

## Phase 3: Requested Assignment

Option별 requested assignment는 write domain에 따라 별도 lane에서 수집한다.

### Schema Domain

Schema default만 허용한다. Profile, target, user request가 있으면 ownership error다.

### Target Domain

```text
target base chain
-> selected target
-> user override, user_override=true인 경우
```

### Profile Domain

```text
profile base chain
-> selected profile
-> user override, user_override=true인 경우
```

### Computed Domain

Requested assignment를 받지 않는다.

Profile/target assignment가 같은 값이라도 잘못된 domain이면 허용하지 않는다.

## Phase 4: Default 선택

Requested assignment가 없는 non-computed option은 default를 선택한다.

1. `when`이 true인 conditional default를 찾는다.
2. 가장 높은 priority를 선택한다.
3. 같은 최고 priority에서 값이 다르면 ambiguity error다.
4. Conditional default가 없으면 unconditional `default`를 사용한다.
5. 값이 없고 `required = false`면 unset이다.
6. 값이 없고 `required = true`면 아직 pending required 상태다.

Default expression은 evaluation graph의 dependency 순서를 따른다. Source/import
선언 순서가 우선순위를 결정하지 않는다.

## Phase 5: Input Effective Value

Profile/target/schema-domain option의 effective candidate를 다음에서 고른다.

```text
최종 requested assignment
-> selected default
-> unset/pending required
```

각 candidate에 type, enum/set membership, finite float, range validation을 적용한다.
Invalid value를 default로 되돌리거나 clamp하지 않는다.

## Phase 6: Computed DAG

Input effective candidate가 준비되면 topological order로 computed expression을
평가한다.

- Computed result type은 option type과 정확히 같아야 한다.
- Overflow, non-finite float, range 위반은 hard error다.
- Computed option은 requested value가 없다.
- Evaluation node마다 input provenance edge를 기록한다.

Evaluation graph cycle은 compile 단계에서 이미 거부되어야 하지만 runtime
snapshot API도 방어적으로 cycle-free plan만 받는다.

## Phase 7: Availability

모든 option의 `available_if`를 평가한다.

```text
available == true
  -> candidate 허용

available == false and candidate is disabled/unset
  -> candidate 허용, unavailable state 기록

available == false and candidate is non-disabled
  -> hard error
```

Disabled value는 bool `false`, tristate `n`, 나머지 type의 unset이다. 숫자 0, 빈
string, 빈 list는 valid data이며 disabled로 간주하지 않는다.

Availability 실패 diagnostic은 다음을 포함한다.

```text
option id
requested/effective candidate
assignment source
available_if expression
false가 된 reference 값과 provenance
```

## Phase 8: Choice

Choice를 canonical choice id 순서로 검증한다.

1. Choice availability를 평가한다.
2. Available member를 계산한다.
3. Explicit/requested selected member를 센다.
4. 선택이 없으면 conditional choice default를 평가한다.
5. Cardinality를 검증한다.
6. Member value를 choice 때문에 자동 promote/demote하지 않는다.

`exactly-one` default가 필요하지만 없으면 첫 visible member를 선택하지 않고
오류를 낸다.

## Phase 9: Named Constraint

모든 최종 effective value와 choice selection이 준비된 뒤 constraint를 평가한다.

```text
when == false -> not-applicable
when == true and require == true -> pass
when == true and require == false -> fail
```

Constraint는 값을 변경하지 않는다. 여러 constraint가 실패하면 fail-fast 옵션이
없는 기본 `check`에서는 가능한 모든 독립 실패를 deterministic order로
수집한다.

Diagnostic은 단순 expression 문자열뿐 아니라 false 결과에 기여한 option
reference와 provenance causal slice를 포함한다.

## Phase 10: Required와 Output 검증

- `required = true` option이 unset이면 오류다.
- Emit 대상 value가 해당 generator에서 표현 가능한지 검사한다.
- Selection template reference가 effective/set 상태인지 검사한다.
- Header emit을 요청한 list/set에 encoding이 없으면 오류다.

Generator가 실행된 뒤 표현 불가능함을 발견하지 않도록 snapshot freeze 전에
검사한다.

## Phase 11: Snapshot Freeze

검증이 모두 통과하면 option id lexical order로 immutable snapshot을 만든다.

Option record:

```text
id
type
write_domain
requested state와 assignment chain
effective value
default/computed source
availability
visibility
menu
choice membership
emit surface
provenance root
```

Snapshot metadata:

```text
schema = 2
resolver ABI
tool/source revision
selected/requested profile과 target
source hash
input manifest hash
constraint summary
```

## Visibility

Visibility는 effective snapshot이 완성된 뒤 평가할 수 있다. TUI는 visibility가
false인 option을 숨기거나 disabled row로 보여주는 view policy를 선택할 수 있다.

Visibility가 false여도 requested/effective value와 artifact emission은 변하지
않는다. Artifact에 영향을 주려면 별도 availability/constraint/computed
semantics를 선언해야 한다.

## Suggestion

Suggestion은 snapshot 위에서 평가한다.

- 적용 가능한 suggestion value와 message를 TUI/explain에 노출한다.
- 자동 적용하지 않는다.
- 사용자가 suggestion을 선택하면 일반 user request로 다시 resolve한다.
- Suggestion 거절은 warning/error가 아니다.

## Requested와 Effective

예:

```text
requested:
  delos.debug.dsh = true
  source = profiles/debug.toml

effective:
  delos.debug.dsh = true
  availability = pass
  constraint = pass
```

Unavailable request는 다음처럼 조용히 false로 바뀌지 않는다.

```text
requested = true
availability = false
resolution = error
```

Optional unset은 명시적으로 기록한다.

```text
requested = unset by profiles/minimal.toml
effective = unset
```

## Provenance Graph

Provenance edge kind:

```text
declared_default
conditional_default
profile_assignment
target_assignment
user_assignment
unset_assignment
overridden_by
computed_from
availability_depends_on
choice_default
constraint_reads
```

`confit explain`은 이 graph를 option 중심으로 slice한다. Generator가 별도의
provenance 문자열을 추측하지 않는다.

## Incremental Resolve

TUI edit는 다음 절차를 사용한다.

1. 기존 compiled project와 snapshot을 유지한다.
2. 한 requested assignment를 임시 ledger에 교체한다.
3. reverse invalidation index로 영향 node를 찾는다.
4. affected default/computed/availability/choice/constraint만 재평가한다.
5. 성공하면 새 immutable snapshot을 publish한다.
6. 실패하면 기존 snapshot을 유지하고 diagnostic을 반환한다.

Full validation은 저장 직전에 다시 수행한다. Incremental 결과와 full result의
semantic hash가 달라지면 internal error다.

## Error Recovery 금지

V2 resolver는 다음 recovery를 하지 않는다.

- unknown option을 false constant로 생성
- type mismatch를 string 비교로 전환
- invalid number를 무시
- range 밖 값을 clamp
- invisible option requested value 제거
- failed dependency를 위해 다른 option 활성화
- conflict를 위해 한 option 비활성화
- choice 첫 member 자동 선택
- cycle 일부 edge 무시

사용자가 수정할 수 있는 후보는 diagnostic으로 제시하되 configuration 자체는
변경하지 않는다.

## Deterministic Matrix

같은 compiled project로 여러 profile/target 조합을 resolve할 때 다음이 같아야
한다.

- 결과는 matrix 실행 순서와 무관하다.
- 이전 resolution cache가 다음 결과를 오염시키지 않는다.
- Diagnostic order가 stable하다.
- Snapshot hash가 process와 platform에 무관하다.
- macOS/Linux/Windows CLI lane에서 LF canonical artifact가 같다.
