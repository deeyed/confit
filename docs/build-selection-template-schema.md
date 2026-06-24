---
doc_type: tool-spec
status: draft
authority: normative
last_verified: 2026-06-24
---

# Build Selection Template Schema

Build selection template은 project가 resolved config 값 중 어떤 항목을
build-time selection artifact로 내보낼지 선언하는 TOML 파일이다. Confit은
Delos board 이름, linker script 규칙, QStar label 규칙을 하드코딩하지 않는다.
Project가 mapping을 선언하고, 이후 generator가 그 mapping을 resolved config에
적용한다.

Template 파일은 config root 아래 `selection/*.toml`에 둔다.

```text
config/
  project.toml
  options/
  profiles/
  targets/
  selection/
    delos.toml
```

## Top-Level Table

각 파일은 `[selection]` table을 가져야 한다.

```toml
[selection]
schema = "confit-build-selection-template-v1"
output = "delos_build_selection"
```

`schema`는 반드시 `confit-build-selection-template-v1`이어야 한다.
`output`은 generated artifact의 project-defined 이름이다. `/`, `..`, 빈
segment, shell-sensitive path는 허용하지 않는다. 같은 project 안에서
`output` 이름은 중복될 수 없다.

## Section Mapping

`[selection.<section>]` table은 output manifest 안의 section을 뜻한다.
Section 이름과 field 이름은 project가 정한다. Confit은 Delos board나 linker
규칙을 해석하지 않고, field value가 기존 option id를 가리키는지만 검증한다.
Generator는 field 이름이 `objects`, `include_dirs`, `*_dirs`, `*_paths`,
`*_labels`인 문자열/path 값을 QStar Lua array로 출력한다. 한 option 안에 여러
항목을 담아야 할 때는 세미콜론으로 구분한다.

```toml
[selection.arch]
id = "delos.target.arch"
cpu = "delos.target.cpu"
toolchain = "delos.toolchain.id"

[selection.board]
id = "delos.target.board"
objects = "delos.board.objects"
include_dirs = "delos.board.include_dirs"
linker_script = "delos.board.linker_script"
```

위 예시는 이후 generator가 다음 모양의 project-specific selection table을 만들
수 있게 하는 mapping이다.

```lua
return {
  schema = "delos-build-selection-v1",
  arch = {
    id = "armv7m",
    cpu = "cortex-m7",
    toolchain = "arm-none-eabi",
  },
  board = {
    id = "nucleo-h753zi",
    objects = {
      "//src/board/armv7m/stm32h7/nucleo-h753zi:board_objects",
    },
    include_dirs = {
      "src/board/armv7m/stm32h7/nucleo-h753zi",
    },
    linker_script = "linker/armv7m/nucleo-h753zi.ld",
  },
}
```

`confit gen --artifact build-selection` 또는 `--artifact all`은 template마다
`<output>/<output>.qsm` pure Lua module을 생성한다. 위 예시라면
`delos_build_selection/delos_build_selection.qsm`이 생성된다.

## Validation Rules

- `[selection]`에는 `schema`, `output`, `x_*` extension field만 허용한다.
- `schema`가 다르면 schema error다.
- `output`이 비어 있거나 path처럼 보이면 schema error다.
- `[selection.<section>]` 이름은 비어 있을 수 없다.
- 같은 template 안에서 section 이름은 중복될 수 없다.
- 같은 section 안에서 field 이름은 중복될 수 없다.
- field value는 반드시 quoted option id여야 한다.
- field value가 project option schema에 없으면 schema error다.
- Template은 최소 하나의 section과 최소 하나의 field를 가져야 한다.
