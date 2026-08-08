---
doc_type: artifact-contract
status: accepted-design
authority: normative
implementation_status: implemented-api
last_verified: 2026-07-24
---

# Confit V2 Generated Artifact

이 문서는 `schema_version = 2` immutable snapshot을 build system과 사람이
소비하는 파일로 직렬화하는 계약을 정의한다. V1 artifact shape는
[generators.md](generators.md)가 담당한다.

> 이 문서는 V2 compatibility serializer API의 artifact shape다. Schema V2 CLI의
> normal `gen --artifact bundle` publication은 sealed artifact ABI v3를 사용하며,
> `docs/cli-v2.md`의 generation/alias contract가 우선한다. 이 compatibility API는
> Parus normal build input이 아니다.

## 기본 Output

```text
<out>/
  config.h
  config.report.json
  config.explain.txt
  config.graph.json
  config.inputs.json
  config.changes.json
  config.cmake
  config/
    config.qsm
  <selection-name>/
    <selection-name>.qsm
```

모든 파일은 같은 immutable snapshot에서 만들어야 한다. Generator가 option을
다시 resolve하거나 availability/constraint를 재평가하면 안 된다.

## 공통 Identity

모든 machine-readable artifact는 가능한 형식 범위에서 다음 identity를 가진다.

```text
project
profile
target
schema_version = 2
resolver_abi
artifact_abi
confit_version
source_hash
input_hash
snapshot_hash
```

Build timestamp, current directory의 absolute path, locale, process id는 artifact에
넣지 않는다.

## config.report.json

Schema id:

```json
"schema": "confit-report-v2"
```

최소 shape:

```json
{
  "schema": "confit-report-v2",
  "schema_version": 2,
  "resolver_abi": "confit-resolver-v2",
  "project": "delos",
  "profile": "debug",
  "target": "sim-dsh",
  "source_hash": "...",
  "options": [
    {
      "id": "delos.debug.ddc",
      "type": "bool",
      "write_domain": "profile",
      "requested": {
        "state": "set",
        "value": true,
        "source": "profiles/debug.toml"
      },
      "effective": {
        "state": "set",
        "value": true
      },
      "available": true,
      "visible": true
    }
  ],
  "choices": [],
  "constraints": [],
  "provenance": []
}
```

Option은 canonical id lexical order다. Requested assignment chain 전체는
provenance node로 보존하며 `requested.source`는 최종 request를 요약한다.

Unset은 field omission이 아니라 명시 state로 표현한다.

```json
"effective": {"state": "unset"}
```

## config.explain.txt

사람이 읽는 deterministic report다. 다음 section을 순서대로 가진다.

```text
Identity
Selected Inputs
Effective Values
Choices
Constraints
```

Option explanation은 type, requested/effective origin, availability, visibility를
stable row로 기록한다. 상세 causal graph와 provenance source는
`config.report.json` 및 `config.graph.json`이 담당한다. ANSI color와 terminal width
wrapping을 파일에 넣지 않는다.

## config.graph.json

Schema id:

```json
"schema": "confit-graph-v2"
```

Graph 종류를 한 edge kind space에 섞지 않고 section으로 나눈다.

```text
evaluation
choice
constraint_reference
visibility
provenance
reverse_invalidation
```

각 edge는 owner/target canonical id로, provenance node는 kind/subject로 출력한다.
Pointer address나 allocation order는 출력하지 않는다.

## config.inputs.json

Schema id:

```json
"schema": "confit-inputs-v2"
```

각 input:

```text
config-root 상대 canonical path
content hash
input role
```

Environment variable, host current directory, filesystem mtime는 semantic input이
아니며 기록하지 않는다. CLI user override의 typed input record는 artifact caller가
`inputs` 목록으로 전달한다.

## config.changes.json

Kconfig `sync_deps()`와 유사한 증분 build 지원을 제공하되 timestamp stamp file
대신 option semantic digest를 사용한다.

Schema id:

```json
"schema": "confit-changes-v2"
```

```json
{
  "schema": "confit-changes-v2",
  "snapshot_hash": "...",
  "options": [
    {
      "id": "delos.debug.ddc",
      "effective_hash": "...",
      "build_hash": "..."
    }
  ]
}
```

- `effective_hash`: type과 effective value의 digest
- `build_hash`: effective value와 emit/output encoding의 digest
- 이전 output이 제공되면 changed option id 목록을 추가할 수 있다.

Digest는 timestamp와 provenance text 변화 때문에 불필요한 compile을 일으키지
않는다. Build selection에 영향을 주는 provenance-independent semantic value를
기준으로 한다.

## config.h

Header schema identity:

```c
#define CONFIT_SCHEMA_VERSION 2
#define CONFIT_RESOLVER_ABI 2
```

Option id를 uppercase underscore macro로 변환한다. 서로 다른 id가 같은 macro로
정규화되면 schema compile 단계에서 collision error다.

Type encoding:

| Type | C encoding |
|---|---|
| bool | `0` 또는 `1` |
| tristate | `CONFIT_TRISTATE_N/M/Y` |
| int | signed decimal literal |
| uint | decimal literal + `ULL` |
| hex | hex literal + `ULL` |
| float | locale-independent C hex-float literal |
| string/enum/path | escaped UTF-8 string literal |
| list/set | 기본 지원 없음, encoding 없으면 error |

Tristate helper:

```c
#define CONFIT_TRISTATE_N 0
#define CONFIT_TRISTATE_M 1
#define CONFIT_TRISTATE_Y 2

#define DELOS_DRIVER_UART CONFIT_TRISTATE_Y
```

Header generator는 implicit `#undef` mode를 사용하지 않는다. Bool/tristate는
항상 값 macro로 출력해 C expression이 build flag에 따라 달라지지 않게 한다.

## config.cmake

Schema identity:

```cmake
set(CONFIT_SCHEMA_VERSION "2")
set(CONFIT_RESOLVER_ABI "confit-resolver-v2")
```

Project prefix variable은 project name과 canonical option id에서 만든다.

```cmake
set(DELOS_CONFIG_DEBUG_DDC "ON")
set(DELOS_CONFIG_TARGET_ARCH "armv7m")
```

Tristate는 `"n"`, `"m"`, `"y"`다. List는 CMake list로 출력하되 각 element 안의
semicolon, backslash, quote를 deterministic하게 escape한다. Unset option은
`<PREFIX>_SET "OFF"` companion variable을 내고 value variable은 만들지 않는다.

## config/config.qsm

Schema id:

```lua
schema = "confit-config-manifest-v2"
```

QStar는 다음처럼 import한다.

```lua
local config = qstar.import_module("build/generated/.../config")
```

각 option은 requested/effective를 구분한다. 이 canonical module은 emit surface와
무관하게 snapshot의 모든 option을 기록한다.

```lua
values = {
  ["delos.debug.ddc"] = {
    type = "bool",
    requested = { state = "set", value = true, source = "profiles/debug.toml" },
    effective = { state = "set", value = true },
    available = true,
  },
}
```

QStar build graph는 `effective.value`만 selector로 사용한다. Requested와
provenance는 diagnostic/audit 용도다.

## Build Selection

V2 selection module schema:

```lua
schema = "confit-build-selection-v2"
```

Core generator의 build selection module은 `emit = ["selection"]`이 있는 option의
typed `effective` value를 canonical id key로 기록한다. 따라서 generator는 board,
CPU, linker처럼 project-specific 의미를 해석하지 않는다.

Project-specific section/field template은 이 artifact ABI에 포함되지 않는다. 그런
mapping은 canonical build selection module을 소비하는 project-side module에서
명시적으로 수행해야 하며, 그 mapping을 Confit core에 암묵적으로 넣어서는 안 된다.

List/path type은 Lua array/string으로 유지한다. Delimiter string으로 합치지
않는다.

## Serialization

- JSON은 UTF-8, LF, 2-space indent를 사용한다.
- JSON object key order와 array order를 contract에서 고정한다.
- Lua/QSM table key는 deterministic order다.
- CMake/QSM/JSON escape helper는 format별로 분리한다.
- Path는 source에서 검증한 forward slash logical path를 유지한다.
- NaN, infinity, host-locale float는 생성 전에 거부한다.
- 모든 text file 끝에는 LF 하나가 있다.

## Atomic Write와 Write-if-changed

Generator는 먼저 memory에서 전체 artifact를 만든다.

1. 기존 file과 byte compare한다.
2. 같으면 file을 다시 쓰지 않는다.
3. 다르면 같은 directory의 temporary file에 쓴 뒤 atomic replace한다.

Atomicity는 file 단위다. 쓰기 중 host I/O failure가 나면 이미 publish된 valid file은
snapshot identity가 서로 일치하므로, consumer는 성공 status가 반환된 bundle만
새 generation으로 채택해야 한다.

Filesystem 구현은 `src/host/`를 통해서만 수행한다.

## Failure

다음은 generation hard error다.

- snapshot identity 불일치
- required/emit value unset
- unsupported header encoding
- macro/variable name collision
- invalid UTF-8 또는 escape 실패
- build-selection mapping type 불일치
- output root 밖 path
- partial write 또는 atomic replace 실패

Generator는 실패한 snapshot의 일부 artifact를 성공 결과처럼 남기지 않는다.
