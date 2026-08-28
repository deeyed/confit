# Schema 6 deterministic resolver

이 문서는 R12가 구현한 generic in-memory resolution 계층의 소유권과 검증 경계를
기록한다. Resolver는 새 TOML field나 사용자 파일 문법을 만들지 않는다. 입력은 typed
`ConfitCatalog`, 그 catalog에 정확히 결속된 `ConfitDependencyPlan`, 그리고 아직 파일
형식을 갖지 않는 explicit `ConfitAssignment` set이다. Native TOML `[values]`를 이 set으로
변환하고 다시 최소 파일로 쓰는 책임은 R13에 남아 있다.

## 1. 입력과 single-writer 규칙

각 public symbol에는 schema loader가 이미 검증한 declaration default 하나가 있고 explicit
user assignment는 0개 또는 1개만 허용된다. Assignment 배열의 순서는 precedence가 아니다.
Resolver는 먼저 lexical symbol index를 만든 뒤 다음을 fail-closed로 검사한다.

- assignment symbol이 catalog에 존재하는가;
- 같은 symbol이 두 번 나타나지 않는가;
- value kind가 declaration kind와 정확히 같은가;
- int/hex 값이 optional range 안에 있는가;
- enum atom이 declaration domain의 exact member인가;
- owned string/enum value가 model invariant를 만족하는가.

Unknown symbol은 무시하지 않고 duplicate는 last-wins로 처리하지 않는다. Default override,
fragment writer, `select`, `imply`, force 또는 derived origin은 없다. Plan이 같은 수의 다른
catalog에서 만들어졌더라도 identity mismatch로 거부한다.

## 2. Candidate, availability, effective value

Resolver는 catalog index에 맞춘 private candidate array를 모든 declaration default로
초기화한 뒤 유효한 user assignment만 해당 candidate에 복사한다. Dependency evaluator는
이 array를 read-only로 보며 다른 symbol을 enable하거나 수정하지 않는다. Availability는
R11이 만든 symbol-stable prerequisite-first order로 평가한다.

- dependency가 true이면 candidate가 effective value다.
- dependency가 false이면 declaration default가 effective value다.
- unavailable symbol에 user가 non-default를 명시하면 전체 resolution이 실패한다.
- unavailable symbol의 explicit default-equal assignment는 유효하고 origin `user`를
  보존한다. R13 minimal serializer가 그 불필요한 assignment를 생략한다.

이 정책은 unavailable default가 consumer build에서 안전한지 판정하지 않는다. Project
author가 의미에 맞는 default를 선언해야 하며 Confit은 C source, Makefile, object 또는 build
graph를 검사해 이를 추론하지 않는다.

## 3. Immutable successful result

`ConfitResolution`은 성공할 때만 caller에게 publish된다. 실패 시 output pointer는 null이고
private candidate, partial resolved record, copied reason은 모두 해제된다. 성공 결과는 다음을
소유한다.

- catalog index에 맞춘 owned `ConfitResolvedValue` record;
- lexical symbol iteration index;
- copied causal reason nodes와 child indexes;
- default/effective typed values, `default|user` origin, normalized availability.

결과는 declaration context를 위해 catalog를 빌리므로 catalog가 result보다 오래 살아야 한다.
Dependency plan은 construction 중에만 필요하고 result가 빌리지 않는다. Accessor는 const
borrowed pointer만 반환하며 mutator는 없다.

R11 reason view의 boolean result도 owned reason에 복사한다. Unavailable result는 false인
expression root를 child로 가진 `CONFIT_REASON_UNAVAILABLE` wrapper를 추가하므로 `explain`이
causal symbol과 comparison을 안정적으로 읽을 수 있다. Reason은 configuration availability의
설명이지 build/runtime dependency graph가 아니다.

## 4. Determinism과 bounds

Final value iteration과 canonical core representation은 lexical symbol order다. Source edge,
fragment, declaration, assignment 배열 순서는 semantic identity를 바꾸지 않는다. Canonical
helper는 symbol, availability, origin, default와 effective typed value를 length-framed core
bytes로 만든다. 이것은 TOML, Make, C, JSON 또는 snapshot format이 아니며 R14/R15의 public
artifact contract를 선행하지 않는다.

Public config ceiling 16,384개를 그대로 적용한다. Assignment 수는 config 수를 넘을 수 없다.
Dependency AST별 reason은 R11의 512-node ceiling 아래에 있고 resolver의 total reason ownership도
유한 catalog와 그 bounded expression들에서만 생긴다. Allocation overflow와 failure는 partial
result 없이 internal error로 닫힌다.

Public evaluator는 독립 caller를 위해 전체 catalog-aligned value kind를 검사한다. Resolver는
후보 전체를 한 번 검증하므로 internal prevalidated evaluation seam을 사용해 최대 graph에서
같은 O(N) scan을 N번 반복하지 않는다. 이 seam은 public header에 없고 resolver 외 caller의
authority가 아니다.

## 5. R12 evidence와 non-claims

Direct C test는 all-default, 다섯 type override, explicit default-equal origin, unavailable no
assignment/default-equal/non-default, unknown/duplicate/wrong type/range/domain, mismatched plan,
declaration reorder의 canonical byte와 SHA-256 identity, reason stability, allocation-failure
cleanup, exact 16,384-symbol graph를 검증한다. Schema integration은 explicit TOML graph가 만든
typed catalog와 linked plan을 resolver에 직접 연결한다.

이 증거는 in-memory deterministic resolution과 실행한 reason graph에 한정된다. R12는 user
TOML value parsing/serialization, file write, snapshot publication, emitter, configuration CLI,
TUI, ordinary build, consumer migration, boot 또는 hardware behavior를 구현하거나 증명하지 않는다.
