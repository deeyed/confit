---
doc_type: tool-spec
status: draft
authority: informative
last_verified: 2026-06-23
---

# Generators

Confit의 결과는 build-time generated artifact다. Parus와 Delos runtime은 Confit core를 링크하지 않고,
generated file만 소비한다.

## Output Root

Generated artifact는 source `config/` directory에 쓰지 않는다. 정본 출력 root는 build tree다.

```text
build/generated/config/<project>/<profile>/
```

예시:

```text
build/generated/config/delos/sim-dsh/
  config.h
  config.report.json
  config.explain.txt
  config.graph.json
  config.inputs.json
```

## C Header

`config.h`는 C code가 소비하는 정본 header다.

```c
#ifndef DELOS_GENERATED_CONFIG_H
#define DELOS_GENERATED_CONFIG_H

#define DELOS_CONFIG_DEBUG_DDC 1
#define DELOS_CONFIG_DEBUG_DSH 1
#define DELOS_CONFIG_DEBUG_DSH_RX 0
#define DELOS_CONFIG_SCHEDULER_TASK_SLOTS 16U

#endif /* DELOS_GENERATED_CONFIG_H */
```

Header generator 규칙:

- Project prefix를 붙인다: `DELOS_CONFIG_`, `PARUS_CONFIG_`.
- Boolean은 `0` 또는 `1`로 출력한다.
- Unsigned integer에는 필요한 경우 `U` suffix를 붙인다.
- String은 C string literal로 출력하되 escape를 검증한다.
- Generated header 상단에 source profile과 hash를 주석으로 남긴다.
- Runtime code는 generated header 없이 fallback default를 몰래 가져서는 안 된다.

## CMake Artifact

이 기능은 초기 필수 기능이 아니다. Confit core, TUI, Parus/Delos 적용이 안정화된 뒤 도입한다.

`config.cmake`는 CMake가 option value를 읽는 파일이다.

```cmake
set(DELOS_CONFIG_DEBUG_DDC ON)
set(DELOS_CONFIG_DEBUG_DSH ON)
set(DELOS_CONFIG_DEBUG_DSH_RX OFF)
set(DELOS_CONFIG_SCHEDULER_TASK_SLOTS 16)
```

CMake generator 규칙:

- `ON/OFF`와 숫자/string을 명확히 구분한다.
- Generated file은 include-only fragment다.
- Fragment 안에서 target을 만들지 않는다.
- Fragment 안에서 source list를 직접 선언하지 않는다.

## QStar Artifact

이 기능은 초기 필수 기능이 아니다. Confit core, TUI, Parus/Delos 적용이 안정화된 뒤 도입한다.

`config.qst`는 QStar graph가 소비하는 generated fragment다.

```lua
qstar.config "delos_generated_config" {
  defines = {
    "DELOS_CONFIG_DEBUG_DDC=1",
    "DELOS_CONFIG_DEBUG_DSH=1",
    "DELOS_CONFIG_DEBUG_DSH_RX=0",
    "DELOS_CONFIG_SCHEDULER_TASK_SLOTS=16",
  },
}
```

QStar generator 규칙:

- Generated fragment는 config declaration만 포함한다.
- Project, toolset, target, stage, run target을 선언하지 않는다.
- `.qsm` helper로 숨기지 않는다. Generated config는 graph declaration이므로 `.qst`가 맞다.
- QStar root or target layer가 명시적으로 import한다.

## JSON Report

`config.report.json`은 machine-readable result다.

```json
{
  "schema": "confit-report-v1",
  "project": "delos",
  "profile": "sim-dsh",
  "status": "ok",
  "options": [
    {
      "id": "delos.debug.dsh",
      "type": "bool",
      "value": true,
      "source": "config/profiles/sim-dsh.toml"
    }
  ],
  "compat": []
}
```

이 report는 CI, Codex agent, release gate가 소비할 수 있다.

## Graph Report

`config.graph.json`은 option DAG를 기록한다.

```json
{
  "schema": "confit-graph-v1",
  "project": "delos",
  "nodes": [
    {"id": "delos.debug.dsh", "type": "bool"}
  ],
  "edges": [
    {"from": "delos.debug.dsh", "to": "delos.debug.ddc", "kind": "requires"}
  ]
}
```

이 파일은 TUI, CI, Codex agent가 dependency를 설명하거나 시각화할 때 사용한다.

## Input Manifest

`config.inputs.json`은 Confit 실행에 사용한 입력 파일과 hash를 기록한다.

```json
{
  "schema": "confit-inputs-v1",
  "project": "delos",
  "profile": "sim-dsh",
  "files": [
    {"path": "config/project.toml", "sha256": "..."},
    {"path": "config/profiles/sim-dsh.toml", "sha256": "..."}
  ]
}
```

Timestamp와 absolute path는 기본 manifest에 넣지 않는다.

## Explanation Report

`config.explain.txt`는 사람이 읽는 explanation이다.

```text
profile: sim-dsh
target: host-sim

enabled:
  delos.debug.ddc
    because profile sim-dsh set it to true
  delos.debug.dsh
    because profile sim-dsh set it to true

disabled:
  delos.debug.dsh_rx
    because default is false
```

Explanation은 TUI의 detail panel과 같은 정보를 공유해야 한다.

## Compatibility Report

Parus/Delos compatibility check는 별도 report를 만들 수 있다.

```text
build/generated/config/system/parus-delos-debug/
  compatibility.report.json
  compatibility.explain.txt
```

Compatibility report는 두 project의 generated header를 직접 수정하지 않는다. 불일치가 있으면 실패한다.

## Reproducibility

Generated artifact는 reproducible해야 한다.

- Timestamp를 넣지 않는다.
- Absolute path를 기본 출력에 넣지 않는다.
- Source file path는 repo-relative path로 표시한다.
- Option order는 stable sort를 따른다.
- Hash는 input content hash를 사용한다.

## Release Boundary

Release profile에서 debug-only option이 꺼져 있으면 generated artifact가 이를 명확히 표현해야 한다.

예:

```c
#define DELOS_CONFIG_DEBUG_DDC 0
#define DELOS_CONFIG_DEBUG_DSH 0
#define DELOS_CONFIG_DEBUG_DSH_RX 0
```

Build graph는 이 값을 기준으로 DDC/DSH source를 link하지 않아야 한다. Runtime flag로 숨기는 것은
release boundary가 아니다.
