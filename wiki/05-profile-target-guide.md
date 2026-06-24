# 05. Profile과 Target

Schema가 “무엇을 설정할 수 있는가”를 정의한다면, profile과 target은 “이번 build에서 어떤 값을 쓸 것인가”를
정한다.

## Resolve 순서

Confit은 값을 다음 순서로 합친다.

```text
default
-> base profile chain
-> target
-> selected profile
-> user override
```

뒤에 오는 값이 앞의 값을 덮어쓴다. 최종 값에는 source가 남는다.

예를 들어:

```toml
[profile]
name = "sim-dsh"
base = "debug"
target = "sim-dsh"

[values]
"delos.debug.dsh" = true
```

이 profile은 다음 값을 합친다.

1. 모든 option default.
2. `debug` profile 값.
3. `sim-dsh` target 값.
4. `sim-dsh` profile 값.

## Profile 파일

```toml
[profile]
name = "debug"
schema_version = 1

[values]
"delos.profile.debug" = true
"delos.debug.ddc" = true
"delos.debug.observability" = true
```

base profile을 쓰는 profile:

```toml
[profile]
name = "sim-dsh"
schema_version = 1
base = "debug"
target = "sim-dsh"

[values]
"delos.debug.dsh" = true
"delos.scheduler.task_slots" = 1
```

## Target 파일

```toml
[target]
name = "sim-dsh"
schema_version = 1

[values]
"delos.target.arch" = "host"
"delos.target.board" = "host-sim"
"delos.build.output_root" = "build/target"
```

target은 profile보다 platform 성격이 강한 값을 담는다.

## CLI에서 profile 보기

profile 목록:

```sh
confit profile list --project tools/confit/tests/fixtures/realish/delos
```

profile 내용:

```sh
confit profile show \
  --project tools/confit/tests/fixtures/realish/delos \
  sim-dsh
```

profile validation:

```sh
confit profile validate \
  --project tools/confit/tests/fixtures/realish/delos \
  sim-dsh
```

## CLI에서 profile 만들기

```sh
confit profile new \
  --project /tmp/my-confit-project \
  debug-dsh \
  --base debug \
  --target sim-dsh
```

이미 있으면 실패한다. 덮어쓰려면 명시적으로 `--force`를 써야 한다.

```sh
confit profile new \
  --project /tmp/my-confit-project \
  debug-dsh \
  --base debug \
  --force
```

## CLI에서 값 수정하기

```sh
confit profile set \
  --project /tmp/my-confit-project \
  debug-dsh \
  delos.debug.dsh=true
```

값 제거:

```sh
confit profile unset \
  --project /tmp/my-confit-project \
  debug-dsh \
  delos.debug.dsh
```

`profile set`은 type validation을 수행한 뒤 profile resolve까지 확인하고 저장한다.

## 임시 override

`resolve`와 `explain`은 `--set`으로 임시 값을 받을 수 있다.

```sh
confit resolve \
  --project tools/confit/tests/fixtures/realish/delos \
  --profile sim-dsh \
  --set delos.debug.dsh=false
```

`--set`은 profile TOML을 수정하지 않는다. 영구 변경은 `confit profile set`이나 TUI에서 한다.

## 어떤 값을 어디에 넣어야 하는가

default에 넣을 값:

- 거의 모든 build에서 같은 값.
- 안전하고 보수적인 기본값.

target에 넣을 값:

- board, arch, output root, image path.
- hardware나 simulator target에 묶이는 값.

profile에 넣을 값:

- debug/release/simulation/bringup 같은 build 의도.
- 특정 profile에서만 켜는 option.

user override에 넣을 값:

- 일회성 실험.
- CI matrix의 임시 override.
- profile을 수정하기 전 확인.

