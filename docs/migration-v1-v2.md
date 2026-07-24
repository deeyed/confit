---
doc_type: migration-contract
status: accepted-design
authority: normative
implementation_status: not-implemented
last_verified: 2026-07-24
---

# Confit V1에서 V2로 Migration

이 문서는 v1 project를 v2로 전환할 때 지켜야 할 도구와 검증 경계를 정의한다.
V2 loader 안에는 v1 compatibility layer가 없으므로 migration은 source 변환과
semantic review를 거치는 명시적 작업이다.

## 원칙

- Project 전체를 한 번에 v2로 전환한다.
- V1과 v2 file을 같은 project에서 혼합하지 않는다.
- 자동 변환 결과를 검토 없이 commit하지 않는다.
- 기존 v1 generated artifact를 baseline으로 보존한다.
- 값뿐 아니라 provenance와 build-selection 차이를 검토한다.
- 외부 project source는 해당 project 수정 승인을 받은 뒤에만 변경한다.

## Migration Command

구현할 offline command:

```text
confit migrate schema \
  --project <v1-root> \
  --to 2 \
  --out <candidate-root> \
  --report <migration-report.json>
```

이 command는 source project를 in-place 수정하지 않는다. Candidate root와
machine-readable report만 만든다.

## 자동 변환 가능 항목

| V1 | V2 candidate |
|---|---|
| `schema_version = 1` | `schema_version = 2` |
| bool/int/uint/hex/float/string/path | 동일 type |
| enum `choices` | enum `values` |
| `category` | explicit menu declaration과 `menu` |
| `requires = ["x"]` | named constraint 또는 bool availability candidate |
| `conflicts = ["x"]` | named negative constraint candidate |
| `recommends` | suggestion candidate |
| profile/target values | domain 분석 후 candidate assignment |
| deprecated alias 사용 | canonical id source rewrite |

자동 변환은 문법 candidate만 만든다. 어떤 option이 profile-domain인지
target-domain인지 최종 결정하지 않는다.

## 수동 결정 필수 항목

다음은 migration report에서 blocker로 표시한다.

- `forces`
- profile과 target이 같은 option을 설정함
- non-bool v1 truthiness dependency
- visibility가 실제 availability 의도로 사용됨
- enum option을 real choice로 바꿀 필요가 있음
- default가 없는 option
- list처럼 사용된 delimiter string
- build-selection field가 불명확한 output surface를 가짐
- deprecated option을 유지할지 삭제할지 결정해야 함
- base profile이 target을 선택함

`forces`는 다음 중 하나로 수동 전환한다.

```text
computed option
named constraint
explicit profile/target assignment
suggestion
```

## Domain 분석

Migration tool은 option별 writer inventory를 만든다.

```text
option id
schema default
writing profiles
writing targets
CLI/TUI override 사용 기록, 제공된 경우
build-selection 사용 여부
```

판정 후보:

- target에서만 쓰면 `target`
- profile에서만 쓰면 `profile`
- 어느 곳에서도 쓰지 않고 상수면 `schema`
- 다른 option의 aggregate면 `computed` 후보
- profile과 target 모두에서 쓰면 blocker

Tool은 domain을 추측해 silent 적용하지 않고 candidate와 confidence를 보고한다.

## Semantic Shadow 비교

V2 candidate가 load되면 모든 실제 profile/target 조합에 대해 v1과 v2를 각각
resolve한다.

비교 항목:

```text
effective value
requested value
last source와 full provenance
config.h macro
config.report.json
config.cmake
config/config.qsm
project build-selection QSM
graph/constraint 결과
source hash input set
```

차이는 다음으로 분류한다.

| Class | 의미 |
|---|---|
| `identical` | 값과 artifact 동일 |
| `provenance-only` | 값은 같지만 ownership/source가 달라짐 |
| `intentional-semantic` | 승인된 v2 hard cut 결과 |
| `unexpected-semantic` | migration blocker |
| `artifact-abi` | 소비 build/tool 변경 필요 |

`unexpected-semantic`이 하나라도 있으면 cutover할 수 없다.

## Parus/Delos 전환

Parus와 Delos는 private source와 실제 build graph를 소유하므로 public Confit
fixture만으로 migration 완료를 주장할 수 없다.

각 project 전환 절차:

1. 해당 project의 v1 artifact baseline 생성
2. Candidate v2 tree를 `/tmp` 또는 별도 branch에 생성
3. 전체 profile/target matrix shadow 비교
4. QStar/CMake source selection과 linker 결과 비교
5. project-specific verification 실행
6. migration report review
7. project 수정에 대한 명시적 승인
8. config source와 Confit pin을 같은 reviewed change로 반영
9. rollback point와 v1 baseline 보존

Parus/Delos source를 public Confit fixture로 복사할 때는 private 정보 공개 승인을
별도로 받아야 한다. 기본값은 sanitized fixture 또는 외부 local compatibility
harness다.

## Cutover 후

V2로 전환한 project는 v1 fallback을 build command에 남기지 않는다.

- `project.toml`은 v2다.
- 모든 profile/target도 v2다.
- CI는 v2 resolver/artifact ABI를 검사한다.
- PATH 바이너리 identity가 project 요구 version과 맞지 않으면 build 전에
  실패한다.
- 이전 v1 source는 release tag 또는 migration branch에서만 보존한다.

V2 실패 시 runtime fallback이 아니라 source-control rollback을 사용한다.
