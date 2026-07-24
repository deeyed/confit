---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-24
---

# Confit Schema Version 계약

이 문서는 Confit source schema의 major version 경계와 dispatch 규칙을 정의한다.
`schema_version`은 단순한 문서 버전이 아니라 parser, model, resolver, explanation,
artifact ABI를 선택하는 의미론 버전이다.

## 지원 버전

Confit은 다음 두 major schema를 서로 독립적으로 취급한다.

| Version | 상태 | 의미 |
|---|---|---|
| `schema_version = 1` | 동결된 실사용 버전 | 현재 Parus/Delos 설정과 생성물의 기존 의미를 보존한다. |
| `schema_version = 2` | 구현 대상 정본 | typed expression, write domain, requested/effective value, computed option, 명시적 constraint를 사용한다. |

v2는 v1의 확장 모드가 아니다. v2 loader는 v1 field를 추측하거나 호환 변환하지
않는다. 같은 실행 파일에 두 버전이 들어가더라도 parser와 resolver 의미론은
분리한다.

## Dispatch

Confit은 `config/project.toml`에서 다음 bootstrap field만 먼저 읽는다.

```toml
[project]
name = "delos"
schema_version = 2
```

그 뒤 version에 맞는 loader로 전체 source tree를 다시 읽는다.

```text
project.toml bootstrap
  -> schema_version == 1 -> v1 loader/model/resolver
  -> schema_version == 2 -> v2 loader/model/resolver
  -> 그 밖의 값          -> unsupported schema error
```

CLI option으로 source가 선언한 major version을 덮어쓸 수 없다. 예를 들어
`schema_version = 1` project를 `--schema-version 2`처럼 강제로 해석하는 기능은
제공하지 않는다.

## Tree 일관성

하나의 project tree는 하나의 major version만 사용한다.

- v2 project가 읽는 profile과 target은 모두 `schema_version = 2`여야 한다.
- v1 profile 또는 target을 v2 project에서 불러오면 hard error다.
- v1 project가 v2 profile 또는 target을 불러오는 것도 hard error다.
- v2 option import 안에 v1 compatibility field가 있으면 unknown field error다.
- compatibility 검사에서 v1 snapshot과 v2 snapshot을 섞지 않는다.

혼합 실행을 허용하면 어느 resolver가 option 값을 소유하는지 불명확해지므로
자동 승격, 자동 강등, 부분 fallback을 모두 금지한다.

## V1 동결 규칙

v1에는 다음 규칙이 적용된다.

- 현재 parser가 허용하는 field와 type 의미를 유지한다.
- 기존 profile merge order를 변경하지 않는다.
- `requires`와 `conflicts`의 현재 active-value 판정을 변경하지 않는다.
- `forces`, `recommends`, `visible_if`가 현재 담당하는 graph/TUI 역할을 값
  전파 의미론으로 바꾸지 않는다.
- 기존 `config.h`, `confit-report-v1`, QSM, CMake variable, build-selection
  shape를 호환성 없이 변경하지 않는다.
- v2 구현을 이유로 v1 source를 자동 수정하지 않는다.

v1의 실제 계약은 [schema-v1.md](schema-v1.md)와
[resolution-v1.md](resolution-v1.md)에만 기록한다. 장래 설계 예시는 v1 문서에
섞지 않는다.

## V2 Hard Cut 규칙

v2에서는 다음 v1 문법을 거부한다.

- option id 문자열 배열 형태의 `requires`, `conflicts`, `recommends`,
  `forces`, `visible_if`
- resolver가 다른 option을 강제로 변경하는 `forces`
- 값의 0, 빈 문자열 여부를 사용하는 암묵적 active 판정
- profile과 target이 같은 option을 마지막-write-wins로 덮어쓰는 방식
- enum 후보와 실제 choice group을 같은 `choices` field로 표현하는 방식
- undefined option을 암묵적으로 만들거나 문자열로 비교하는 방식
- visibility가 false라는 이유로 requested value를 조용히 삭제하는 방식
- range 밖 값을 clamp하거나 default로 조용히 되돌리는 방식

v2 source는 v2 문법으로 명시적으로 작성해야 한다. 변환 도구는 candidate source와
semantic diff를 만들 수 있지만, v2 runtime loader 안에는 v1 호환 계층을 넣지
않는다.

## Artifact Version

Schema version과 artifact schema는 별도로 versioning한다.

| Source semantics | Report | QStar module | 설명 |
|---|---|---|---|
| v1 | `confit-report-v1` | `confit-config-manifest-v1` | 현재 ABI |
| v2 | `confit-report-v2` | `confit-config-manifest-v2` | requested/effective/provenance 포함 |

v2 project가 v1 report를 생성하는 호환 option은 두지 않는다. `config.h`처럼
파일명이 유지되는 artifact도 header 안에 schema/resolver ABI identity를
기록한다.

V2 artifact shape와 write-if-changed 규칙은
[artifacts-v2.md](artifacts-v2.md)를 따른다.

## Binary Identity

`confit --version --verbose`와 `confit doctor`는 최소한 다음 identity를 노출해야
한다.

```text
tool version
source revision
supported schema versions
v1 resolver ABI
v2 resolver ABI
artifact ABI 목록
TUI availability
platform lane
```

Parus와 Delos 같은 소비 project는 PATH의 바이너리 이름만 신뢰하지 않고 이
identity를 검증할 수 있어야 한다.

## 변경 승인 경계

v2 구현과 v1 regression fixture 추가는 Confit 저장소 안에서 수행할 수 있다.
실제 Parus/Delos source를 v2로 바꾸는 작업은 다음 절차를 거친다.

1. v2 parser/resolver/generator가 완성된다.
2. v1 전체 결과가 기존 결과와 동일한지 검증한다.
3. 별도 migration 도구로 v2 candidate와 semantic diff를 만든다.
4. 변경 option, provenance, build-selection 차이를 검토한다.
5. Parus/Delos 수정에 대한 명시적 승인을 받는다.
6. 각 project에서 독립 commit과 rollback point를 만든다.

Confit은 migration 승인이 없으면 외부 project의 `config/`, CMake, QStar source를
수정하지 않는다.
