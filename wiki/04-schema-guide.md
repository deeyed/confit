# 04. 스키마 작성 가이드

Schema는 option이 어떤 값을 가질 수 있는지 정의한다. Confit에서는 schema도 TOML이다.

## 가장 작은 bool option

```toml
[option."delos.debug.dsh"]
type = "bool"
default = false
prompt = "Enable DSH debug shell"
category = "debug"
tags = ["debug", "shell"]
help = "개발용 DSH debug shell을 build에 포함한다."
owner = "delos-runtime"
since = "0.1.0"
stability = "stable"
```

필수에 가까운 field:

- `type`: 값의 type.
- `default`: 기본값.
- `prompt`: TUI와 report에 보이는 짧은 설명.
- `category`: TUI menu path와 list filtering에 쓰는 표시용 분류. 단순
  이름도 가능하지만, 큰 project에서는 `runtime/trace`처럼 얕은 path를 쓴다.
- `tags`: 검색과 filtering에 쓰는 label.
- `help`: 사용자가 판단할 수 있는 긴 설명.
- `owner`: 담당 영역.
- `since`: 도입 version.
- `stability`: `stable`, `experimental`, `deprecated` 같은 안정성 표시.

metadata가 빠지면 일반 validation에서는 warning, strict validation에서는 failure가 될 수 있다.

## Category path와 menu depth

Confit의 dependency 정본은 tree가 아니라 DAG다. `category`는 resolver가 쓰는
권위 구조가 아니라 TUI와 문서가 사람이 읽기 좋은 menu를 만들기 위한 표시용
metadata다.

작은 project는 단일 category만 써도 된다.

```toml
category = "debug"
```

Parus/Delos처럼 option이 많아지면 slash-separated path를 쓴다.

```toml
category = "runtime/trace"
```

권장 규칙:

- 2단계를 기본으로 한다. 예: `runtime/trace`.
- 큰 영역에서만 3단계를 허용한다. 예: `runtime/scheduler/policy`.
- 4단계 이상은 피한다. 너무 깊은 menu는 search, tag, help/detail보다 사용성이
  나빠진다.
- category path는 표시용이다. Option validity는 `requires`, `conflicts`,
  `forces`, `visible_if` 같은 dependency field로 표현한다.

## 지원 type

### bool

```toml
[option."delos.debug.ddc"]
type = "bool"
default = false
prompt = "Enable DDC"
category = "debug"
tags = ["debug"]
help = "DDC debug surface를 build에 포함한다."
owner = "delos-runtime"
since = "0.1.0"
stability = "stable"
```

profile 값:

```toml
[values]
"delos.debug.ddc" = true
```

### int

```toml
[option."delos.scheduler.dispatch_window_ticks"]
type = "int"
default = 1000
range = [1, 10000]
prompt = "Scheduler dispatch window"
category = "runtime"
tags = ["scheduler"]
help = "Scheduler dispatch window를 tick 단위로 설정한다."
owner = "delos-runtime"
since = "0.1.0"
stability = "stable"
```

### uint

```toml
[option."delos.scheduler.task_slots"]
type = "uint"
default = 16
range = [1, 256]
prompt = "Scheduler task slots"
category = "runtime"
tags = ["scheduler"]
help = "동시에 관리할 task slot 개수."
owner = "delos-runtime"
since = "0.1.0"
stability = "stable"
```

### hex

```toml
[option."delos.memory.flash_origin"]
type = "hex"
default = 0x0
range = [0x0, 0xFFFFFFFF]
prompt = "Flash origin"
category = "memory"
tags = ["memory", "link"]
help = "Flash memory origin address."
owner = "delos-runtime"
since = "0.1.0"
stability = "stable"
```

### float

```toml
[option."delos.sim.default_gain"]
type = "float"
default = 0.125
range = [0.0, 1.0]
prompt = "Simulation default gain"
category = "simulation"
tags = ["simulation"]
help = "Simulator default gain. NaN과 infinity는 허용하지 않는다."
owner = "delos-sim"
since = "0.1.0"
stability = "stable"
```

Confit float는 finite value만 허용한다.

### string

```toml
[option."delos.target.output_name"]
type = "string"
default = "delos"
prompt = "Output name"
category = "target"
tags = ["target", "output"]
help = "Generated target output name."
owner = "delos-build"
since = "0.1.0"
stability = "stable"
```

### path

```toml
[option."delos.generated.config_root"]
type = "path"
default = "build/generated/config/delos"
prompt = "Generated config root"
category = "generated"
tags = ["generated", "path"]
help = "Confit generated artifact output root."
owner = "delos-build"
since = "0.1.0"
stability = "stable"
```

path는 source-relative 또는 build-relative path로 쓰는 것이 좋다. Absolute path를 source schema에 박으면
reproducibility가 나빠진다.

### enum

```toml
[option."parus.boot.stage_limit"]
type = "enum"
default = "eb2"
choices = ["eb1", "eb2", "kernel"]
prompt = "Boot stage limit"
category = "boot"
tags = ["boot"]
help = "부팅이 어디까지 진행되어야 하는지 선택한다."
owner = "parus-boot"
since = "0.1.0"
stability = "stable"
```

profile 값은 choice 중 하나여야 한다.

```toml
[values]
"parus.boot.stage_limit" = "eb2"
```

## Dependency field

Confit은 option 사이의 관계를 graph edge로 본다.

```toml
[option."delos.debug.dsh"]
type = "bool"
default = false
requires = ["delos.debug.ddc"]
conflicts = ["delos.profile.release"]
recommends = ["delos.debug.observability"]
visible_if = ["delos.profile.debug"]
```

의미:

- `requires`: 이 option이 active이면 대상 option도 active여야 한다.
- `conflicts`: 동시에 active이면 안 된다.
- `recommends`: 강제는 아니지만 함께 켜는 것을 추천한다.
- `forces`: 제한된 reverse dependency. Confit은 위험한 force를 검증한다.
- `visible_if`: TUI 표시 조건이다. 숨기기보다 비활성/흐림 표시 정책을 쓸 수 있다.

## 좋은 option id 규칙

좋은 id:

```text
delos.debug.dsh
delos.scheduler.task_slots
parus.boot.direct_dtb
parus.target.qstar_label
```

피해야 할 id:

```text
debug
dsh
enable
foo
```

id는 project prefix와 domain을 포함해야 한다. 그래야 compat rule과 generated define이 예측 가능해진다.

## Schema 작성 후 확인

```sh
confit check --project <project-root> --profile <profile>
confit check --project <project-root> --profile <profile> --strict
confit graph --project <project-root> --profile <profile> --format json
```

schema를 수정했다면 `--strict`로 metadata warning도 확인한다.
