---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-24
---

# Confit Schema Version 1

이 문서는 `schema_version = 1`의 실제 source 형식과 현재 구현 의미를 고정한다.
장래 기능이나 v2 예시는 이 문서의 계약이 아니다.

## Project

v1 project는 `config/project.toml`에서 시작한다.

```toml
[project]
name = "delos"
version = "0.1.0"
schema_version = 1
imports = [
  "options/debug.toml",
  "options/scheduler.toml",
]
```

`imports`는 option schema file을 deterministic order로 읽기 위한 목록이다.
Profile과 target은 각각 `config/profiles/`, `config/targets/`에서 읽는다.

## Option

```toml
[option."delos.debug.ddc"]
type = "bool"
default = false
prompt = "Enable Delos Debug Console"
category = "debug"
tags = ["debug", "development"]
help = "DDC parser와 command table을 build에 포함한다."
owner = "delos-debug"
since = "0.1.0"
stability = "stable"
emit = ["header", "cmake", "qstar", "report"]
```

v1이 지원하는 option type은 다음과 같다.

| Type | Payload |
|---|---|
| `bool` | `true`, `false` |
| `int` | signed 64-bit integer |
| `uint` | unsigned 64-bit integer |
| `hex` | unsigned 64-bit integer, hex 표시 관례 |
| `string` | string |
| `enum` | `choices` 안의 string |
| `float` | finite double |
| `path` | string payload를 가진 경로 |

`float`의 NaN과 infinity는 허용하지 않는다. `path`는 filesystem 존재 여부를
검사하는 type이 아니며, host path API와 build system이 실제 경로 소비를
담당한다.

## Enum Candidate

v1의 `choices`는 별도 choice group이 아니라 한 enum option의 후보 목록이다.

```toml
[option."delos.target.arch"]
type = "enum"
choices = ["host", "armv7m"]
default = "host"
```

현재 v1 loader는 `[choice."..."]` table을 project source에서 완전한 의미 모델로
읽지 않는다. Model API에 `ConfitChoice` skeleton이 있더라도 source 계약으로
간주하면 안 된다.

## Range

숫자 option은 닫힌 구간 range 하나를 가질 수 있다.

```toml
[option."delos.scheduler.task_slots"]
type = "uint"
default = 16
range = [1, 128]
```

Default, profile, target, user override가 range를 벗어나면 오류다.

## Category

`category`는 slash-separated TUI path다.

```toml
category = "runtime/trace"
```

v1 category는 dependency authority가 아니다. 현재 제한은 다음과 같다.

- 빈 segment, leading slash, trailing slash는 오류다.
- 최대 byte 길이는 63이다.
- 권장 depth는 2다.
- 지원 최대 depth는 3이며 더 깊은 path는 warning/strict failure 대상이다.

## Dependency Field

v1 dependency field는 option id 문자열 배열만 받는다.

```toml
requires = ["delos.debug.ddc"]
conflicts = ["delos.profile.release"]
recommends = ["delos.internal.debug_surface"]
forces = ["delos.internal.debug_surface"]
visible_if = ["delos.debug.ddc"]
```

Object expression, 비교 연산, `all`/`any`, 조건부 dependency는 v1 문법이
아니다.

현재 resolver가 configuration validity에 직접 적용하는 것은 `requires`와
`conflicts`다.

- source option이 active이고 `requires` target이 inactive면 오류다.
- source option과 `conflicts` target이 모두 active면 오류다.
- `recommends`와 `forces`는 graph/explanation metadata이며 값을 변경하지 않는다.
- `visible_if`는 TUI visibility에 사용하며 generated effective value를 변경하지
  않는다.

Active 판정은 [resolution-v1.md](resolution-v1.md)에 고정한다.

## Emit

`emit`은 option이 노출될 generator surface를 제한한다.

```toml
emit = ["header", "cmake", "qstar", "report", "selection"]
```

중복, 빈 이름, unknown surface는 오류다. Build-selection template이 참조하는
option은 `selection` surface를 포함해야 한다.

v1의 emit 동작과 artifact shape는 [generators.md](generators.md)와
[build-selection-template-schema.md](build-selection-template-schema.md)를
따른다.

## Profile

```toml
[profile]
name = "sim-dsh"
schema_version = 1
base = "debug"
target = "sim-dsh"

[values]
"delos.debug.ddc" = true
"delos.debug.dsh" = true
```

Profile inheritance는 단일 `base` chain이다. Unknown base와 cycle은 오류다.
Profile은 target-owned/profile-owned option을 구분하지 않으므로 target과 같은
option을 설정할 수 있으며, selected profile 값이 target 값을 덮어쓴다.

## Target

```toml
[target]
name = "sim-dsh"
schema_version = 1
arch = "host"
board = "host-sim"

[target.claim]
level = "host-simulator"

[values]
"delos.target.arch" = "host"
"delos.target.board" = "host-sim"
```

Target metadata의 `claim.level`은 support evidence 그 자체가 아니다. 실제
hardware 또는 emulator claim은 소비 project의 별도 검증 계약을 따른다.

## Stability Metadata

v1은 다음 metadata를 지원한다.

```text
owner
since
stability = experimental | stable | deprecated | internal
deprecated
replaced_by
deprecated_aliases
```

`deprecated_aliases`는 v1 profile/target migration을 위한 기존 compatibility
surface다. v2 loader는 이를 v1 호환 목적으로 읽지 않는다.

## Unknown Field

Unknown field는 오류다. `x_` extension metadata는 resolver authority가 아니며
현재 loader가 명시적으로 허용하는 범위 밖에서는 사용할 수 없다.

## V1에서 지원하지 않는 것

- typed expression
- `tristate`
- requested/effective value 분리
- option write domain
- computed option
- named constraint
- conditional default
- real choice cardinality
- `forces` 값 전파
- dependency 기반 자동 값 수정
- mixed v1/v2 project

이 기능들은 v1에 backport하지 않고 v2에서만 제공한다.
