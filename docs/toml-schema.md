---
doc_type: tool-spec
status: draft
authority: informative
last_verified: 2026-06-23
---

# Confit TOML Schema

Confit source format은 TOML이다. TOML은 사람이 읽기 쉽고, 기존 Kconfig 문법처럼 indentation과
keyword nesting에 강하게 의존하지 않는다.

## Directory Layout

각 프로젝트는 root에 `config/` directory를 둔다.

```text
config/
  project.toml
  options/
    debug.toml
    scheduler.toml
    dcg.toml
    boot.toml
  profiles/
    debug.toml
    release.toml
    sim-dsh.toml
  targets/
    qemu-mps2-an500.toml
    nucleo-h753zi.toml
  compat/
    parus-delos.toml
```

`config/project.toml`은 project identity와 import list를 가진다.

```toml
[project]
name = "delos"
version = "0.1.0"
schema_version = 1

imports = [
  "options/debug.toml",
  "options/scheduler.toml",
  "options/dcg.toml",
]
```

## Option Definition

Option은 global id를 가진다. id는 project prefix를 포함한다.

```toml
[option."delos.debug.ddc"]
type = "bool"
default = false
prompt = "Enable Delos Debug Console"
category = "debug"
tags = ["debug", "host-tooling"]
help = "개발용 DDC command parser와 command table을 build에 포함한다."
owner = "delos-runtime"
since = "0.1.0"
stability = "stable"
```

지원 type:

| Type | 의미 |
|---|---|
| `bool` | true/false |
| `int` | signed integer |
| `uint` | unsigned integer |
| `hex` | integer를 hex display로 표시 |
| `string` | short string |
| `enum` | 정해진 문자열 중 하나 |
| `float` | finite floating-point value |
| `path` | source-relative 또는 build-relative path |

초기에는 Kconfig의 `tristate`를 기본 type으로 넣지 않는다. Parus/Delos는 loadable module model을
정본으로 갖고 있지 않기 때문이다. 나중에 필요하면 `tri`를 별도 type으로 추가한다.

`float`는 지원하지만 제한적으로 사용한다. `NaN`, `inf`, locale-dependent 표현은 금지한다. Runtime
configuration에서 exact value가 중요한 곳은 `int`, `uint`, fixed-point convention을 우선한다.
`float`는 simulation, threshold, tuning metadata처럼 사람이 조정하는 profile 값에 적합하다.

## Stability Metadata

Public option은 migration과 ownership metadata를 가질 수 있다.

```toml
[option."delos.debug.dsh"]
type = "bool"
default = false
owner = "delos-runtime"
since = "0.1.0"
stability = "stable"
deprecated_aliases = ["delos.debug.old_dsh"]
```

`stability` 값은 `experimental`, `stable`, `deprecated`, `internal` 중 하나다. `owner`,
`since`, `stability`가 없으면 기본 check에서는 warning이며, strict validation에서는 failure로
승격된다. `deprecated_aliases`는 기존 profile/target 값이 새 canonical option id로 deterministic하게
해석되도록 하는 migration surface다.

## Dependencies

`requires`는 해당 option이 유효하기 위한 hard dependency다.

```toml
[option."delos.debug.dsh"]
type = "bool"
default = false
requires = ["delos.debug.ddc"]
forbidden_in = ["release"]
```

조건식이 필요하면 object form을 사용한다.

```toml
requires = [
  { option = "delos.debug.ddc", equals = true },
  { option = "delos.target.arch", equals = "armv7m" },
]
```

## Conflicts

`conflicts`는 동시에 참일 수 없는 option을 나타낸다.

```toml
[option."delos.debug.dsh_rx"]
type = "bool"
default = false
conflicts = ["delos.profile.release"]
```

Conflict는 resolver가 자동으로 한쪽을 끄는 기능이 아니다. Confit은 conflict를 발견하면 오류와
explanation을 출력한다.

## Recommends

`recommends`는 Kconfig의 `imply`에 가깝다. 권고 default를 제공하지만 사용자가 끌 수 있다.

```toml
[option."delos.sim.dsh"]
type = "bool"
default = false
recommends = ["delos.debug.ddc", "delos.debug.dsh"]
```

## Forces

`forces`는 Kconfig의 `select`에 해당하지만, 기본적으로 매우 제한한다.

규칙:

- visible option을 강제로 켜면 안 된다.
- dependency가 있는 option을 dependency 검증 없이 force하면 안 된다.
- force edge는 explanation report에 반드시 나타난다.
- release/debug boundary에 영향을 주는 force는 hard error로 격상할 수 있다.

```toml
[option."delos.target.qemu_mps2"]
type = "bool"
default = false
forces = ["delos.arch.armv7m"]
```

## Ranges

정수 type은 range를 가질 수 있다.

```toml
[option."delos.scheduler.task_slots"]
type = "uint"
default = 16
range = [1, 128]
```

## Choices

Choice는 여러 option 중 하나를 고르는 구조다.

```toml
[choice."delos.target.board"]
type = "enum"
options = ["qemu-mps2-an500", "nucleo-h753zi", "stm32f4-discovery"]
default = "qemu-mps2-an500"
```

Choice 결과는 generated header에서 enum-like define 또는 string define으로 표현할 수 있다.

## Profiles

Profile은 option override 묶음이다. Profile은 option schema를 정의하지 않는다.

```toml
[profile]
name = "sim-dsh"
schema_version = 1
base = "debug"
target = "host-sim"

[values]
"delos.debug.ddc" = true
"delos.debug.dsh" = true
"delos.sim.host" = true
"delos.target.board" = "host-sim"
"delos.sim.tick_hz" = 1000
"delos.sim.default_gain" = 0.125
```

`base`는 단일 inheritance만 허용한다. Profile merge order가 복잡해지면 config 결과를 이해하기
어렵다. `target`은 profile이 기본으로 선택하는 target name이다. 여러 fragment가 필요하면
`overrides = []`를 명시하고, report에 merge order를 출력한다.

TUI가 profile을 저장할 때도 이 형식을 사용한다. TUI는 profile file을 생성하고 수정할 수 있어야 한다.

## Targets

Target file은 board/arch/toolchain 관련 option override를 가진다.

```toml
[target]
name = "nucleo-h753zi"
schema_version = 1
arch = "armv7m"
board = "nucleo-h753zi"

[values]
"delos.target.arch" = "armv7m"
"delos.target.board" = "nucleo-h753zi"
"delos.mcu.cpu" = "cortex-m7"
```

Target file은 support claim이 아니다. Delos에서 portability probe로만 존재하는 target은 metadata에
그 성격을 명시한다.

```toml
[target.claim]
level = "portability-probe"
```

## Compatibility

Compatibility file은 여러 project config를 함께 검사한다.

```toml
[compat]
name = "parus-delos"

[[assert]]
when = { option = "parus.rt_executor.delos", equals = true }
requires = { option = "delos.dcg.enabled", equals = true }
message = "Parus Delos RT Executor requires Delos DCG."

[[assert]]
when = { option = "parus.debug.release", equals = true }
forbids = { option = "delos.debug.dsh_rx", equals = true }
message = "Release system must not expose Delos DSH RX command parser."
```

Compatibility assertion은 generated runtime code가 아니다. Build 전에 실행되는 host-side 검증이다.

## Schema Editing

TUI는 schema editing을 지원할 수 있어야 한다. 즉 option 추가, prompt/help 수정, category/tag 수정,
range 수정, choice 후보 추가 같은 작업을 TUI에서 할 수 있어야 한다.

그러나 schema editing은 profile editing보다 위험하다. TUI는 schema edit mode에 들어갈 때 다음
경고를 표시해야 한다.

```text
Schema edit mode changes project configuration semantics.
Prefer code review for schema changes.
```

TUI로 schema를 수정하더라도 저장 결과는 같은 TOML schema file이어야 한다. TUI 전용 binary format이나
sidecar database를 정본으로 삼지 않는다.
