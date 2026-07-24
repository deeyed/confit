---
doc_type: architecture-contract
status: accepted-design
authority: normative
implementation_status: partially-implemented
last_verified: 2026-07-24
---

# Confit V2 Architecture

이 문서는 `schema_version = 2` 구현의 module 경계와 data flow를 정의한다.
v2의 목표는 커널 의미론을 내장하는 것이 아니라, 커널과 대형 시스템
소프트웨어를 포함한 복잡한 build configuration을 typed, deterministic,
explainable 방식으로 처리하는 것이다.

## 설계 목표

- 수만 option과 dependency edge를 처리한다.
- 모든 expression을 resolution 전에 정적 type-check한다.
- user 요청과 build가 소비할 effective value를 분리한다.
- profile과 target의 option ownership을 schema에서 검증한다.
- resolver가 다른 입력 option을 부수적으로 변경하지 않는다.
- 모든 generator가 하나의 immutable snapshot을 공유한다.
- 특정 결과가 나온 이유와 실패 constraint의 causal path를 설명한다.
- TUI가 resolver 의미론을 복제하지 않는다.
- core evaluator가 filesystem, environment, clock, terminal을 직접 사용하지 않는다.

## 비목표

- Linux kernel Kconfig 문법 호환
- Kconfig `select`/`imply`의 값 전파 재현
- SAT/SMT solver를 이용한 임의 configuration 자동 탐색
- filesystem 존재 여부나 shell command를 사용하는 expression
- build graph 자체의 생성 또는 실행
- runtime configuration service
- v1 source를 v2 loader 안에서 해석하는 compatibility mode

## Source Layout

v2 구현이 시작되면 현재 단일 v1 module을 다음 경계로 분리한다.

```text
include/confit/
  project.h                 # version-tagged public dispatch handle
  snapshot.h                # version-tagged resolved snapshot handle
  schema_v1.h
  schema_v2.h
  resolver_v1.h
  resolver_v2.h
  expression_v2.h
  constraint_v2.h

src/model/
  v1/                       # 현재 ConfitProject model 보존
  v2/                       # Symbol, Choice, MenuNode, Constraint, Provenance

src/schema/
  dispatch.c                # project.toml bootstrap과 major version dispatch
  v1/                       # 현재 loader를 의미 변경 없이 이동
  v2/
    loader.c
    linker.c
    typecheck.c
    validate.c

src/parser/
  v1/                       # 현재 first-party scanner와 v1 parser adapter
  v2/
    tomlc17_adapter.c        # vendor TOML tree를 Confit read-only view로 격리

vendor/
  tomlc17/                   # unmodified TOML v1.1 amalgamation과 MIT license

src/resolver/
  v1/                       # 현재 merge/active semantics 보존
  v2/
    plan.c
    evaluate.c
    snapshot.c
    incremental.c

src/expression/
  v2/
    lexer.c
    parser.c
    typecheck.c
    evaluate.c

src/constraint/
  v2/
    graph.c
    choice.c
    validate.c
    explain.c

src/generator/
  dispatch.c
  v1/
  v2/

src/tui/
  model_adapter_v1.c
  model_adapter_v2.c
  ...                       # curses renderer와 input은 공유 가능
```

`src/schema/dispatch.c`와 `src/resolver/dispatch.c`는 `[project].schema_version`을
strict TOML bootstrap으로 읽고 version-tagged opaque handle을 만든다. v1은 기존
loader와 resolver를 adapter로 호출하고, v2는 link/compile/resolve/constraint를 모두
통과한 immutable snapshot만 handle에 넣는다. 어느 경로도 다른 major version의 raw
model pointer를 public API로 노출하지 않는다.

## Public Handle

CLI와 TUI가 raw v1/v2 struct를 혼합하지 않도록 version-tagged opaque handle을
사용한다.

```c
typedef struct ConfitProjectHandle ConfitProjectHandle;
typedef struct ConfitSnapshotHandle ConfitSnapshotHandle;
```

Handle은 schema version을 조회할 수 있지만, v1 model을 v2 model로 cast하거나
공통 필드가 같다고 가정할 수 없다. Version별 세부 API는 adapter가 사용한다.

## V2 Model 분리

Kconfiglib의 `Symbol`, `Choice`, `MenuNode` 분리는 채택하되 의미론은 더
엄격하게 정의한다.

### Symbol

`Symbol`은 다음을 소유한다.

- canonical option id
- declared value type
- write domain
- unconditional/conditional default
- computed expression
- availability expression
- visibility expression
- output surface
- stability metadata
- source span

한 Symbol의 semantic definition은 정확히 한 곳에만 존재한다. 여러 파일에서 같은
Symbol을 재정의하거나 prompt만 추가하는 방식을 허용하지 않는다.

### Choice

`Choice`는 명시적인 member id, member type, cardinality, default rule을 소유한다.
Menu 배치나 dependency adjacency로 member를 추론하지 않는다.

### MenuNode

`MenuNode`는 prompt, help, parent, order, visibility 같은 UI hierarchy만
소유한다. Menu tree는 option dependency graph를 변경하지 않는다.

동일 Symbol을 여러 menu에 복제 표시해야 한다면 semantic definition 복제가
아니라 read-only menu reference를 사용한다.

### Constraint

`Constraint`는 name, `when`, `require`, message, source span을 가진다. Constraint
평가는 값을 생산하거나 변경하지 않고 최종 snapshot을 검증한다.

### Provenance

Provenance node는 schema default, target/profile assignment, user override,
conditional default, computed expression, choice decision을 기록한다. 최종 값 하나만
남기지 않고 causal edge를 보존한다.

## Compile Pipeline

v2 source는 다음 단계로 compile된다.

```text
1. TOML syntax parse
2. import와 symbol link
3. duplicate/unknown reference 검사
4. expression parse와 static type check
5. write domain과 assignment ownership 검사
6. evaluation graph 작성
7. graph cycle 검사
8. choice/constraint structural validation
9. immutable compiled project 생성
```

오류가 발생하면 부분 compiled project를 resolver에 넘기지 않는다.

## Graph 분리

하나의 범용 edge graph에 모든 의미를 넣지 않는다.

| Graph | 역할 | Cycle 정책 |
|---|---|---|
| Evaluation graph | conditional default, computed, availability dependency | cycle hard error |
| Choice graph | member와 conditional default | structural cycle hard error |
| Constraint reference graph | assertion이 읽는 option | 값 생산 graph가 아니므로 상호 참조 허용 |
| Visibility graph | TUI visibility 재계산 | value graph와 분리, self-reference error |
| Provenance graph | explanation causal edge | evaluation 결과로 생성 |
| Reverse invalidation index | TUI incremental evaluation | compiled graph에서 파생 |

Cycle diagnostic은 첫 node만 출력하지 않고 전체 path와 각 edge의 source span을
출력한다.

## Resolve Pipeline

Compiled project와 선택 profile/target/user override는 다음 순서로 snapshot을
만든다.

```text
requested assignment 수집
-> conditional default 선택
-> input effective value 확정
-> computed DAG 평가
-> availability 검증
-> choice 검증
-> named constraint 검증
-> immutable snapshot freeze
-> provenance/explanation index 생성
```

Resolver는 conflict를 해결하기 위해 사용자가 지정한 값을 임의 변경하지 않는다.
실패하면 수정 후보를 diagnostic으로 제안할 수 있지만 성공 snapshot을 조작해서
반환하지 않는다.

## Incremental Evaluation

대형 project TUI는 key 입력마다 전체 project를 다시 resolve하지 않는다.

- option id는 hash/index table로 조회한다.
- AST와 graph node는 arena에 저장하고 compiled project lifetime 동안 immutable로
  유지한다.
- 각 symbol의 reverse dependent index를 만든다.
- requested value가 바뀌면 영향받는 evaluation/visibility/constraint slice만
  invalidation한다.
- 변경 전후 semantic hash가 같은 node에서 전파를 중단한다.
- search index와 menu row index는 compiled project에서 한 번 만든다.

CLI batch resolution은 같은 compiled project를 여러 profile/target matrix에
재사용할 수 있어야 한다.

## Snapshot

V2 snapshot은 최소한 다음을 포함한다.

```text
project/schema/resolver identity
selected profile과 target
option별 requested value
option별 effective value
write domain과 assignment source
availability와 visibility 결과
choice selection
constraint 결과
provenance graph
source semantic hash, selected input hash, final semantic hash
```

Snapshot을 만든 뒤 generator가 값을 다시 계산하거나 dependency를 재평가하면
안 된다.

## Generator Boundary

Header, JSON, CMake, QStar, build-selection generator는 같은 immutable snapshot을
읽는다. Generator별 serialization은 달라도 effective value는 같아야 한다.

Filesystem write, path creation, atomic replacement, write-if-changed는 `src/host/`
API를 통해서만 수행한다. V2 expression과 resolver는 host API를 호출하지 않는다.
구체적인 output ABI는 [artifacts-v2.md](artifacts-v2.md)를 따른다.

## TUI Boundary

TUI는 다음 세 model view만 소비한다.

```text
Compiled MenuTree
Resolved Snapshot
Explanation Graph
```

TUI가 option assignability, dependency truthiness, choice default를 자체 계산하면
안 된다. Edit 요청은 resolver API에 requested value로 전달하고, 새 snapshot 또는
diagnostic을 받는다.

## 성능 기준

v2 release gate의 최소 규모는 다음과 같다.

- option 20,000개
- expression/constraint edge 100,000개
- profile 200개
- target 100개
- menu search 100회
- 단일 option 변경 incremental resolve 1,000회

정확한 시간 제한은 CI runner 등급별로 정하되, algorithmic gate는 다음을
유지한다.

- parse/link: 입력 크기에 선형에 가까운 동작
- graph validation: `O(V + E)`
- deterministic output sort: `O(V log V)`
- incremental update: 전체 graph가 아니라 reachable reverse slice에 비례

## Thread와 Global State

Compiled project와 snapshot은 global singleton을 사용하지 않는다. 서로 다른
project를 같은 process에서 검사할 수 있어야 하며, read-only compiled project를
여러 resolution에 재사용할 수 있어야 한다.

초기 구현이 single-threaded여도 API가 process-global parser/evaluator 상태에
의존하지 않게 한다.
