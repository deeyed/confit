# 03. 프로젝트 구조

Confit project는 사람이 관리하는 TOML 설정 묶음이다. 가장 작은 project는 다음 구조를 가진다.

```text
config/
  project.toml
  options/
  profiles/
  targets/
  compat/
```

`--project`에는 보통 이 `config/`를 포함하는 project root를 넘긴다. 현재 fixture들은 project root 아래에
`config/` directory를 둔다.

```sh
confit check --project tools/confit/tests/fixtures/realish/delos --profile sim-dsh
```

Confit은 내부에서 다음 파일을 찾는다.

```text
tools/confit/tests/fixtures/realish/delos/config/project.toml
tools/confit/tests/fixtures/realish/delos/config/options/*.toml
tools/confit/tests/fixtures/realish/delos/config/profiles/*.toml
tools/confit/tests/fixtures/realish/delos/config/targets/*.toml
```

## project.toml

`project.toml`은 project의 이름, schema version, include guard prefix 같은 project-level metadata를 담는다.

예시:

```toml
[project]
name = "delos"
schema_version = 1
include_guard = "DELOS_CONFIG_H"
define_prefix = "DELOS_CONFIG"
```

중요한 원칙:

- `schema_version = 1`을 명시한다.
- project 이름은 generated report와 compatibility context에 나타난다.
- generated `config.h` define prefix는 project와 충돌하지 않게 정한다.

## options/

`options/*.toml`은 option schema를 정의한다.

예시:

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

option 파일은 domain별로 나누는 것이 좋다.

```text
options/
  build.toml
  debug.toml
  runtime.toml
  target.toml
```

## profiles/

`profiles/*.toml`은 특정 목적의 값 묶음이다.

예시:

```toml
[profile]
name = "sim-dsh"
schema_version = 1
base = "debug"
target = "sim-dsh"

[values]
"delos.debug.dsh" = true
"delos.scheduler.task_slots" = 1
"delos.generated.config_root" = "build/generated/config/delos/sim-dsh"
```

profile은 모든 option을 반복해서 쓰지 않는다. 필요한 override만 적고 나머지는 default, base profile,
target에서 온다.

## targets/

`targets/*.toml`은 board, arch, output root처럼 target 성격이 강한 값을 담는다.

예시:

```toml
[target]
name = "sim-dsh"
schema_version = 1

[values]
"delos.target.arch" = "host"
"delos.target.board" = "host-sim"
"delos.build.output_root" = "build/target"
```

target은 profile과 분리해 둔다. 같은 profile family가 여러 target을 가질 수 있기 때문이다.

## compat/

`compat/*.toml`은 여러 project를 함께 검사하는 rule을 담는다. 보통 Delos project 옆이나 별도 compat
fixture에 둔다.

예시:

```toml
[[assert]]
id = "parus-delos-debug-pair"
when = { project = "delos", option = "delos.debug.dsh", equals = true }
requires = [
  { project = "parus", option = "parus.test.qemu_pairing", equals = true }
]
message = "Delos DSH debug profile requires a matching Parus QEMU pairing profile."
```

## Source와 Generated Output을 분리한다

source config:

```text
config/project.toml
config/options/*.toml
config/profiles/*.toml
config/targets/*.toml
config/compat/*.toml
```

generated output:

```text
build/generated/config/<project>/<profile>/
  config.h
  config.report.json
  config.explain.txt
  config.graph.json
  config.inputs.json
  config.cmake
  config/
    config.qsm
  config.qst  # deprecated compatibility artifact
```

절대로 generated file을 source `config/` 안에 몰래 쓰지 않는다. `confit gen`은 항상 명시적인 `--out`을
받는다.
