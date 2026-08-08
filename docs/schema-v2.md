---
doc_type: language-spec
status: accepted-design
authority: normative
last_verified: 2026-08-08
---

# Confit Schema Version 2

이 문서는 `schema_version = 2` TOML source의 정본 형식을 정의한다.
Expression 세부 문법은 [expression-v2.md](expression-v2.md), 값 계산은
[resolution-v2.md](resolution-v2.md), 생성물은
[artifacts-v2.md](artifacts-v2.md)를 따른다.

## Project Layout

기본 layout은 다음과 같다.

```text
config/
  project.toml
  options/
  menus/
  profiles/
  targets/
  constraints/
  selection/
  compat/
```

Option, menu, constraint는 `project.toml`의 explicit import로 읽는다. Profile과
target은 project가 선언한 directory에서 filename lexical order로 발견한다.

## Component Selection Input

V2 profile 또는 target table은 typed option overlay와 별도로 bounded component selection field를
선언할 수 있다.

```toml
[target]
name = "virt"
schema_version = 2
root_components = ["sys.boot.ingress.rph1"]
required_capabilities = ["image.arm64.qemu.virt.rph1"]
optional_capabilities = ["driver.net.virtio"]
```

`root_components`는 lower-case dot-separated component ID 목록이고,
`required_capabilities`와 `optional_capabilities`는 bounded ASCII capability atom 목록이다.
Base chain과 selected document에서 같은 atom이 반복되면 one effective request로 deduplicate한다.
Unknown field, invalid atom, list length limit 초과는 schema error다.

이 field는 component Makefile, source file, compiler flag, runtime device state나 artifact path를
포함하지 않는다. Component catalog와 dependency/KAPI schema는 별도 `component.toml`이 소유한다.

## TOML Syntax Boundary

V2 source는 TOML 1.1 strict syntax parser로 읽는다. 이 parser는 string, array,
inline table, array-of-tables, date/time value를 syntax tree로 보존한다. 그러나
Confit field가 모든 TOML value type을 그대로 받는 것은 아니다. 각 field가 허용하는
type과 value shape는 이 문서의 개별 field 규칙이 정하며, 문서화되지 않은 type은
schema error다.

TOML parsing은 [vendor-tomlc17.md](vendor-tomlc17.md)의 adapter 경계를 따른다.
V2 loader는 table level을 직접 순회하고 TOML library의 merge 또는 multipart-key
helper로 import, namespace, profile precedence를 구현하지 않는다.

V2 loader는 project metadata, explicit import, typed option/menu/choice/constraint
source model과 source span을 만들며, linker는 canonical option namespace를 만들고
expression을 parse/typecheck한 뒤 모든 reference를 symbol에 연결하고 write-domain
ownership을 검증한다. Profile/target input은 assignment ledger 단계에서 typed record로
읽고 base chain, write-domain, duplicate assignment를 검증한다. Ledger는 schema
default, target, profile, user request의 모든 provenance를 보존하고 winning requested
record를 결정한다. Conditional default, expression evaluation, effective snapshot,
choice와 constraint enforcement는 resolution 단계가 소유한다. Loader와 linker 모두
expression text를 평가하지 않는다.

## Project

```toml
[project]
name = "delos"
namespace = "delos"
version = "0.2.0"
schema_version = 2
default_target = "sim-dsh"

imports = [
  "menus/root.toml",
  "options/target.toml",
  "options/debug.toml",
  "constraints/build.toml",
]

profile_dirs = ["profiles"]
target_dirs = ["targets"]
selection_dirs = ["selection"]
```

규칙:

- `name`, `namespace`, `schema_version`은 필수다.
- `default_target`은 선택 사항이다. CLI/TUI와 leaf profile이 target을 명시하지
  않았을 때만 사용한다.
- `namespace`는 option id의 첫 segment와 일치해야 한다.
- Import path는 project config root 상대 forward-slash path다.
- Import 대상도 top-level `schema_version = 2`를 선언해야 하며 `imports`로 다시
  declaration source를 가져올 수 있다. `project` table은 root `project.toml`에만
  둔다.
- Absolute path, 빈 segment, `..`, symlink escape는 허용하지 않는다.
- Import cycle과 같은 canonical file의 중복 import는 오류다. Logical path 표기가
  달라도 canonical file이 같으면 중복이다.
- Glob, environment substitution, shell preprocessor를 제공하지 않는다.
- 발견한 모든 source file은 `config.inputs.json`에 content hash와 함께 기록한다.

가져온 declaration source의 최소 형식은 다음과 같다.

```toml
# config/options/trace.toml
schema_version = 2

[option."delos.trace.enabled"]
type = "bool"
default = false
write_domain = "profile"
owner = "delos-observability"
since = "0.2.0"
```

## Option ID

Option id는 dot-separated ASCII identifier다.

```text
delos.debug.ddc
parus.target.arch
```

Project-owned option은 project namespace로 시작해야 한다. `system.*`은
cross-project contract가 명시적으로 소유하는 경우에만 사용할 수 있다.

같은 option id를 여러 파일에서 재정의할 수 없다. Prompt/help를 추가하기 위한
partial definition도 허용하지 않는다.

## Value Type

v2 type은 다음과 같다.

| Type | TOML value | 설명 |
|---|---|---|
| `bool` | `true`, `false` | 이진 설정 |
| `tristate` | `"n"`, `"m"`, `"y"` | disabled/module/builtin |
| `int` | integer | signed 64-bit |
| `uint` | integer | unsigned 64-bit |
| `hex` | integer | unsigned 64-bit, hex 표시 |
| `float` | float/integer | finite IEEE-754 double |
| `string` | string | UTF-8 byte string |
| `enum` | string | 선언 후보 중 하나 |
| `path` | string | canonical forward-slash logical path |
| `string_list` | string array | 순서가 의미 있는 string 목록 |
| `path_list` | string array | 순서가 의미 있는 path 목록 |
| `enum_set` | string array | 중복 없는 enum 집합 |

Map/object를 option value로 허용하지 않는다. 구조화된 build-selection output은
selection template가 여러 typed option을 section으로 묶는다.

## 기본 Option

```toml
[option."delos.debug.ddc"]
type = "bool"
default = false
write_domain = "profile"
user_override = true

prompt = "Enable Delos Debug Console"
help = "DDC parser와 command table을 build에 포함한다."
menu = "debug.console"
tags = ["debug", "development"]
owner = "delos-debug"
since = "0.2.0"
stability = "stable"

available_if = 'delos.target.kind != "release-minimal"'
visible_if = 'true'
emit = ["header", "cmake", "qstar"]
```

필수 field:

```text
type
write_domain
owner
since
stability
```

Non-computed option은 `default` 또는 `required = true` 중 하나를 가져야 한다.
Bool과 tristate도 implicit false/`n` default를 갖지 않는다.

## Write Domain

`write_domain`은 option 값을 쓸 수 있는 source layer를 고정한다.

| Domain | 허용 writer |
|---|---|
| `schema` | schema default만 |
| `profile` | base/selected profile |
| `target` | base/selected target |
| `computed` | `computed` expression만 |

`user_override = true`는 `profile` 또는 `target` option에만 사용할 수 있다.
Schema constant와 computed option은 user override를 허용하지 않는다.

Profile이 target-domain option을 쓰거나 target이 profile-domain option을 쓰면
값이 같더라도 hard error다.

## Required와 Unset

```toml
[option."delos.target.linker_script"]
type = "path"
required = true
write_domain = "target"
user_override = false
```

`required = true` option은 resolution 종료 전에 effective value가 있어야 한다.
`required = false`이고 default가 없으면 unset 상태를 가질 수 있다.

Unset은 false, 0, 빈 문자열과 다르다. Artifact가 unset option을 emit하려 하면
해당 output encoding이 omission을 허용하는지 검사하고, 그렇지 않으면 오류다.

## Enum

```toml
[option."delos.target.arch"]
type = "enum"
values = ["host", "armv7m", "aarch64"]
default = "host"
write_domain = "target"
owner = "delos-build"
since = "0.2.0"
stability = "stable"
emit = ["header", "cmake", "qstar", "selection"]
```

v1의 `choices` field는 v2에서 unknown field다. Enum 후보는 `values`로 선언한다.

## List와 Set

```toml
[option."delos.board.include_dirs"]
type = "path_list"
default = []
write_domain = "target"
owner = "delos-build"
since = "0.2.0"
stability = "stable"
emit = ["cmake", "qstar", "selection"]
```

List는 순서와 중복을 보존한다. `enum_set`은 중복을 오류로 처리하고 canonical
enum declaration order로 정렬한다.

List type은 기본적으로 C header로 emit할 수 없다. Header output이 필요하면
향후 명시적인 header encoding contract를 추가해야 하며 string macro 하나로
조용히 join하지 않는다.

## Numeric Range

```toml
[option."delos.scheduler.task_slots"]
type = "uint"
default = 16
range = { min = 1, max = 128 }
write_domain = "profile"
user_override = true
owner = "delos-scheduler"
since = "0.2.0"
stability = "stable"
emit = ["header", "cmake", "qstar"]
```

Range bound는 같은 숫자 type이어야 한다. 범위를 벗어난 default, profile,
target, override, computed result는 hard error다. Clamp와 fallback은 없다.

조건부 range가 필요하면 named constraint로 표현한다.

## Conditional Default

```toml
[option."delos.scheduler.tick_hz"]
type = "uint"
required = true
write_domain = "profile"
user_override = true
owner = "delos-scheduler"
since = "0.2.0"
stability = "stable"
emit = ["header", "cmake", "qstar"]

[[option."delos.scheduler.tick_hz".defaults]]
when = 'delos.target.kind == "sim"'
value = 1000
priority = 100

[[option."delos.scheduler.tick_hz".defaults]]
when = 'delos.target.kind == "hardware"'
value = 100
priority = 100
```

Priority는 signed 32-bit integer다. 선언/import 순서가 default 결과를 결정하지
않는다.

## Computed Option

```toml
[option."delos.memory.total_bytes"]
type = "uint"
write_domain = "computed"
computed = 'delos.memory.page_size * delos.memory.page_count'
owner = "delos-memory"
since = "0.2.0"
stability = "internal"
emit = ["header", "cmake", "qstar", "selection"]
```

Computed option에는 `default`, `required`, `user_override`, profile/target assignment를
허용하지 않는다. `available_if`도 허용하지 않는다. 조건부 computed value가
필요하면 expression이 bool/tristate의 disabled value 또는 type에 맞는 명시 값을
반환하도록 설계한다. Presentation 조건은 `visible_if`로 둘 수 있다.

## Availability와 Visibility

```toml
available_if = 'enum_name(delos.target.arch) == "armv7m"'
visible_if = 'enabled(delos.debug.advanced_ui)'
```

- `available_if`는 semantic eligibility다.
- `visible_if`는 TUI/document presentation만 결정한다.
- 두 expression은 bool이어야 한다.
- 생략 시 `true`다.

Unavailable option이 disabled/unset이 아닌 requested value를 가지면 오류다.
Resolver가 값을 false/default로 조용히 변경하지 않는다.

Bool/tristate가 아닌 option에 `available_if`를 쓸 때는 unavailable branch에서
선택되는 unconditional default를 두면 안 된다. 조건부 default 또는 unset을
사용해 unavailable 상태에서 effective value가 없도록 해야 한다.

Type별 disabled value:

| Type | Disabled |
|---|---|
| bool | `false` |
| tristate | `"n"` |
| 나머지 | unset |

숫자 0과 빈 문자열은 disabled로 취급하지 않는다.

## Suggestion

Suggestion은 값을 바꾸지 않고 TUI/diagnostic에 후보를 제공한다.

```toml
[[option."delos.debug.dsh".suggestions]]
when = 'enabled(delos.sim.host)'
value = true
message = "host simulation에서는 DSH를 함께 켜는 것이 편리합니다."
```

Suggestion을 거절해도 configuration validity에는 영향이 없다. `--strict`도
suggestion 미적용을 오류로 승격하지 않는다.

## Menu

Menu는 별도 declaration이다.

```toml
[menu."debug"]
prompt = "Debug"
order = 200

[menu."debug.console"]
prompt = "Console"
parent = "debug"
order = 100
visible_if = 'true'

# 다른 menu에는 read-only display만 추가할 수 있다.
[menu."observability"]
prompt = "Observability"
order = 300
references = [{ option = "delos.debug.ddc", read_only = true }]
```

Option은 `menu = "debug.console"`로 배치한다. Menu parent cycle은 오류다.
Dependency가 implicit submenu를 만들지 않는다.

같은 parent 아래에서 `order`는 유일해야 한다. 동일 order를 canonical id lexical
order로 조용히 해소하지 않으며 source span을 포함한 hard error로 처리한다. 동일
option을 여러 menu에 표시하려면 primary `option.menu` 외의 모든 표시는
`references`의 `read_only = true`여야 한다. 읽기 가능한 option 정의를 여러 menu에
복제하거나 menu placement로 dependency/choice membership을 추론하지 않는다.
깊은 menu는 lint warning 대상일 수 있지만 resolver validity를 바꾸지 않는다.

## Choice

Choice member는 명시적인 bool 또는 tristate option이다.

```toml
[choice."delos.console.backend"]
member_type = "bool"
members = [
  "delos.console.uart",
  "delos.console.semihosting",
  "delos.console.host_stdio",
]
cardinality = "exactly-one"
available_if = 'true'
visible_if = 'true'

[[choice."delos.console.backend".defaults]]
when = 'enum_name(delos.target.arch) == "host"'
member = "delos.console.host_stdio"
priority = 100
```

지원 cardinality:

```text
exactly-one
zero-or-one
one-or-more
```

규칙:

- 모든 member type은 `member_type`과 같아야 한다.
- 모든 member는 같은 write domain이어야 한다.
- Member를 menu adjacency로 추론하지 않는다.
- Choice를 다른 choice의 member로 넣는 nesting은 허용하지 않는다.
- Default member는 해당 choice의 `members` 안에 있어야 한다. 같은 `when`과
  priority로 다른 member를 고르는 default는 structural hard error다.
- `exactly-one`에서 선택이 없고 적용 가능한 default도 없으면 오류다.
- 같은 최고 priority에서 다른 member가 default가 되면 ambiguity error다.
- 첫 visible member 자동 선택은 하지 않는다.
- Bool member는 true를, tristate member는 `m` 또는 `y`를 selected로 센다.
- Tristate의 `m`/`y` mode는 member value가 명시하며 choice가 자동 승격하지
  않는다.

Simple string selection은 choice가 아니라 enum option을 사용한다.

## Constraint

복합 configuration rule은 named constraint로 작성한다.

```toml
[[constraint]]
id = "delos.armv7m.toolchain"
when = 'enum_name(delos.target.arch) == "armv7m"'
require = 'delos.toolchain.kind == "arm-none-eabi"'
message = "armv7m target에는 arm-none-eabi toolchain이 필요합니다."
```

`id`, `when`, `require`, 비어 있지 않은 `message`는 필수다. `when`이 true이고
`require`가 false면 hard error다.

Constraint는 option 값을 변경하지 않는다. Conflict는 별도 mutation edge가
아니라 명시적 부정 constraint로 쓴다.

```toml
[[constraint]]
id = "delos.release.no_dsh"
when = 'delos.build.profile == "release"'
require = '!enabled(delos.debug.dsh)'
message = "release profile에서는 DSH를 활성화할 수 없습니다."
```

Semantic constraint에 warning severity를 허용하지 않는다. Build를 막지 않는
규칙은 별도 lint 또는 suggestion으로 작성한다.

## Profile

```toml
[profile]
name = "debug"
schema_version = 2
base = "common"
target = "sim-dsh"

[values]
"delos.build.profile" = "debug"
"delos.debug.ddc" = true
"delos.debug.dsh" = true

[unset]
options = ["delos.debug.output_path"]
```

규칙:

- 단일 base inheritance만 허용한다.
- Base cycle은 오류다.
- Base chain은 가장 먼 base부터 selected profile 순서로 적용한다.
- Profile은 profile-domain option만 설정한다.
- Unset은 optional profile-domain option에만 사용할 수 있다.
- 같은 file에서 value와 unset을 동시에 선언하면 오류다.
- 다른 profile의 base로 쓰이는 profile은 target을 선언할 수 없다.
- TUI는 sparse `[values]`와 `[unset]`만 저장하고 full snapshot을 덮어쓰지 않는다.

## Target

```toml
[target]
name = "nucleo-h753zi"
schema_version = 2
base = "stm32h7-common"

[target.claim]
level = "renode-probe"
kind = "renode:nucleo-h753zi"

[values]
"delos.target.arch" = "armv7m"
"delos.target.board" = "nucleo-h753zi"
"delos.target.cpu" = "cortex-m7"

[unset]
options = ["delos.board.optional_probe"]
```

Target은 단일 base target을 가질 수 있다. Target chain은 가장 먼 base부터 selected
target 순서로 적용한다.

Target은 target-domain option만 설정한다. Optional target-domain value는
`[unset]`으로 base target assignment를 지울 수 있다. Claim metadata는 generated
selection과 report에 전달되지만 실제 hardware 증거를 대신하지 않는다.

## Target 선택

Selected target name은 다음 우선순위로 결정한다.

```text
1. CLI/TUI session에서 명시한 target
2. selected leaf profile의 target
3. project.default_target
```

Base profile은 target을 선택할 수 없다. Target name 선택과 target-domain option
value merge를 혼동하지 않는다.

## User Override

CLI `--set`은 user override request다. V2 profile TUI의 unsaved edit는 profile
transaction request이며 selected profile chain보다 높은 preview priority를 갖는다.

- CLI `--set`에는 option의 `user_override`가 true여야 한다.
- TUI profile transaction은 profile-domain option만 쓸 수 있으며
  `user_override` flag를 요구하지 않는다.
- Profile/target write domain을 바꾸지 않는다.
- Computed/schema option을 덮어쓸 수 없다.
- Type/range/enum/availability 검증을 즉시 수행한다.
- 저장 시 해당 domain의 정본 source로만 기록한다.

Target-domain user override를 profile TOML에 저장하지 않는다. Target edit mode
또는 별도 ephemeral override로 처리한다.

## Stability Metadata

v2의 `stability`는 다음 값을 가진다.

```text
experimental
stable
deprecated
internal
```

v2 runtime loader는 v1 `deprecated_aliases` compatibility를 제공하지 않는다.
Option rename은 migration command가 profile/target source를 명시적으로
변환해야 한다.

## Emit

V2 report는 audit를 위해 모든 option을 포함한다. `emit`은 다음 build output만
선택한다.

```text
header
cmake
qstar
selection
```

```toml
emit = ["header", "cmake", "qstar", "selection"]
```

`report`는 v2 emit candidate가 아니다. Empty `emit = []`은 report/explanation에만
존재하는 internal option을 의미한다.

List/set type의 header emit은 명시 encoding이 없으면 오류다.

## Build Selection

`selection` emit은 option의 typed effective value를 canonical id key로 V2 build
selection module에 기록한다. Mapping target은 `selection` emit을 가진 option이어야
한다.

Project-specific section/field mapping은 별도 template contract로 선언할 수 있지만,
generic V2 artifact generator는 option id 의미를 해석하지 않는다. Board, CPU,
linker, object label의 의미는 project build graph가 명시적으로 정한다.

V2 selection artifact는 `confit-build-selection-v2` metadata와 snapshot source hash를
포함한다.

## 금지 Field

v2 loader는 다음 v1 field를 unknown field error로 거부한다.

```text
requires
conflicts
recommends
forces
choices
category
deprecated_aliases
forbidden_in
```

대체 관계:

| V1 | V2 |
|---|---|
| `requires` | `available_if` 또는 named constraint |
| `conflicts` | named constraint의 부정 expression |
| `recommends` | suggestion |
| `forces` | computed option 또는 constraint |
| enum `choices` | enum `values` |
| `category` | explicit `menu` |
| alias | offline migration |

## Diagnostic

다음은 모두 hard error다.

- unknown/duplicate option, menu, choice, constraint
- type mismatch
- invalid default/range
- undefined expression reference
- write domain violation
- unavailable non-disabled request
- computed/evaluation cycle
- choice cardinality violation
- failed constraint
- required value unset
- invalid artifact encoding
- v1/v2 source 혼합

Style, deep menu, unused option, suggestion은 code가 있는 deterministic lint
warning으로 처리한다.
