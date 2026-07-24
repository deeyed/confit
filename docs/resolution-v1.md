---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-24
---

# Confit Resolution Version 1

이 문서는 `schema_version = 1` resolver의 현재 동작을 고정한다.

## Merge Order

v1은 다음 순서로 값을 적용한다. 뒤의 값이 앞의 값을 덮어쓴다.

```text
1. option default
2. base profile chain, 가장 먼 base부터
3. selected target
4. selected profile
5. CLI/TUI user override
```

Profile이 target을 지정하고 CLI가 별도 target을 지정하지 않으면 profile target을
사용한다. CLI target이 있으면 그것을 선택한다.

Profile과 target이 같은 option을 설정하면 selected profile이 이긴다. 이 규칙은
v1 compatibility surface이며 변경하지 않는다.

## Active 판정

v1 dependency 검증은 type별로 다음 값을 active로 본다.

| Value kind | Active |
|---|---|
| bool | `true` |
| int | 0이 아님 |
| uint/hex | 0이 아님 |
| float | 0.0이 아님 |
| string/enum/path | 빈 문자열이 아님 |
| empty | false |

이 truthiness 규칙은 v2 expression으로 이어지지 않는다.

## Dependency Validation

모든 값 merge와 정렬이 끝난 뒤 active source option을 순회한다.

- `requires`: target이 inactive면 `CONFIT_ERR_DEPENDENCY`
- `conflicts`: target이 active면 `CONFIT_ERR_CONFLICT`
- `recommends`: 값 변경 없음
- `forces`: 값 변경 없음
- `visible_if`: resolver validity에 영향 없음

Resolver는 conflict를 자동으로 해소하지 않으며 required option을 자동으로 켜지
않는다.

## Provenance

각 resolved value는 최종 값과 마지막 source label 하나를 가진다. 기본값,
base profile, target, selected profile을 거친 전체 변경 chain은 v1 model의
정본 필드가 아니다.

따라서 v1 explanation은 dependency graph와 마지막 source를 조합한다. v2의
requested/effective/provenance graph를 v1 report에 소급 적용하지 않는다.

## Ordering

Resolved value는 option id lexical order로 정렬한다. JSON, text, TOML,
header, report, QSM/CMake generator는 이 deterministic snapshot을 사용한다.

## Failure

다음 상황은 hard error다.

- unknown profile 또는 target
- profile inheritance cycle
- unknown option override
- value type 불일치
- enum candidate 위반
- range 위반
- unmet `requires`
- active `conflicts`

v1 resolver는 일부 값을 적용한 결과를 성공 snapshot으로 반환하지 않는다.

## Freeze Gate

v2 구현 중 다음 v1 regression을 모두 유지해야 한다.

1. 모든 기존 unit/golden test 통과
2. v1 realish Parus/Delos fixture의 resolved value 동일
3. `config.h`, JSON report, CMake, QSM, selection artifact 동일
4. 동일 input에서 stable output hash 유지
5. v1 TUI가 저장하는 sparse profile 형식 유지

새 Confit 바이너리를 설치하기 전에 기존 바이너리와 새 바이너리의 v1 artifact를
비교해야 한다.
