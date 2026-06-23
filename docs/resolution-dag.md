---
doc_type: tool-spec
status: draft
authority: informative
last_verified: 2026-06-23
---

# Resolution DAG

Confit의 핵심은 option dependency를 tree가 아니라 DAG로 모델링하는 것이다. Kconfig의 menu tree는
사용자 UI에는 유용하지만, 실제 오류는 option 간 dependency, reverse dependency, default, conflict가
엉키는 지점에서 발생한다.

## Graph Model

Graph node는 option 또는 choice다.

Graph edge는 다음 kind를 가진다.

| Edge | 의미 |
|---|---|
| `requires` | 현재 option이 유효하려면 대상 조건이 만족되어야 한다. |
| `conflicts` | 두 option 조합이 동시에 유효할 수 없다. |
| `recommends` | 대상 option을 켜는 것이 좋지만 사용자가 거절할 수 있다. |
| `forces` | 제한된 reverse dependency. 강한 lint 대상이다. |
| `default_from` | 조건부 default가 참조하는 option. |
| `visible_if` | TUI/display visibility 조건. config validity authority는 아니다. |
| `compat_requires` | cross-project compatibility assertion. |
| `compat_forbids` | cross-project 금지 assertion. |

Option graph는 cycle을 가질 수 없다. 단, `conflicts`는 undirected constraint edge이므로 cycle check의
대상이 아니라 contradiction check의 대상이다.

## Resolution Order

추천 resolution 순서:

1. 모든 schema file parse.
2. option id 중복, type 충돌, unknown field 검사.
3. graph edge 구성.
4. graph cycle 검사.
5. base profile 적용.
6. target override 적용.
7. user profile override 적용.
8. default 적용.
9. recommends 적용 후보 계산.
10. forces 적용.
11. requires/conflicts/range/choice 검사.
12. cross-project compatibility 검사.
13. generated artifact와 explanation report 출력.

User override는 default보다 강하다. 그러나 user override도 dependency와 conflict를 이길 수 없다.

## Select Problem Avoidance

Kconfig의 `select`는 dependency를 방문하지 않고 다른 symbol 값을 강제할 수 있다. Confit의 `forces`는
이 위험을 줄이기 위해 다음 rule을 가진다.

- `forces` 대상은 기본적으로 hidden/internal option이어야 한다.
- `forces` 대상이 `requires`를 가지면 그 dependency도 만족해야 한다.
- `forces` 대상이 visible prompt를 가지면 warning 또는 error를 낸다.
- `forces` edge는 `config.explain.txt`에 반드시 기록한다.

권장 방식은 `forces`보다 `requires`와 `recommends`를 사용하는 것이다.

## Explanation

Confit은 단순히 실패만 말하면 안 된다. 다음 질문에 답해야 한다.

- 이 option은 왜 enabled 되었는가?
- 이 option은 왜 disabled 되었는가?
- 어떤 profile이 값을 override했는가?
- 어떤 option이 이 값을 요구했는가?
- 어떤 conflict 때문에 build가 중단되었는가?
- Parus와 Delos 중 어느 프로젝트의 선택이 mismatch를 만들었는가?

예상 출력:

```text
error: delos.debug.dsh_rx is forbidden in profile release

value:
  delos.debug.dsh_rx = true

set by:
  config/profiles/h753zi-debug.toml:12

conflicts with:
  delos.profile.release

hint:
  use profile debug or disable delos.debug.dsh_rx
```

## Compatibility Resolution

Parus와 Delos를 함께 검사할 때는 project별 resolution을 먼저 수행한 뒤, compatibility graph를 별도
phase로 검사한다.

```text
resolve(parus)
resolve(delos)
check_compat(parus, delos)
```

Compatibility checker는 한쪽 project의 option을 자동 수정하지 않는다. Cross-project mismatch는
명시 오류로 보고하고, 사용자가 profile을 고쳐야 한다.

## Determinism

Confit output은 deterministic해야 한다.

- file load order는 명시 import order를 따른다.
- map 출력은 option id lexical order를 따른다.
- report는 stable order를 갖는다.
- 같은 input은 byte-identical generated artifact를 만들어야 한다.

이 원칙은 CMake/QStar incremental build와 CI cache에 중요하다.

## Forbidden Runtime Behavior

Resolution DAG는 host-side build artifact다. 다음 동작은 금지한다.

- Runtime에서 option graph를 들고 다니는 것.
- Runtime에서 dependency를 다시 계산하는 것.
- Runtime에서 TOML profile을 바꾸는 것.
- Debug shell에서 config option을 직접 바꾸는 것.

Debug shell은 generated config 값을 표시할 수는 있지만, config authority가 될 수 없다.
