---
doc_type: contract
status: accepted
authority: confit-schema-v6
schema_version: 6
implementation_status: contract-frozen-implementation-in-progress
---

# Confit schema 6 configuration contract

## 1. Authority, scope, and current status

This document is the repository-local authority for the public schema 6
configuration language, command names, user-visible value semantics, and
interactive editing semantics. Implementations and tests must conform to this
document. A test may not relax this contract, and an implementation convenience
may not silently add syntax.

Schema 6 is a hard cut. It does not provide a legacy parser, a dual-schema
dispatcher, field aliases, ordered overrides, or a compatibility fallback.
Unknown fields and unsupported types are errors.

At R01 this is a frozen contract, not an implementation-completion statement.
The inherited source tree still contains an older implementation until later
rounds remove it. Examples in this document describe required schema 6 behavior;
they are not evidence that the current binary already provides it.

Confit owns only configuration-document membership, typed option declarations,
explicit user values, dependency availability, deterministic resolution, safe
configuration-data emission, immutable selected snapshots, configuration review,
and value-editing UX. It does not own project source membership, compiler flags,
link order, build rules, executable probes, or project-specific semantics.

## 2. Closed document model

A schema 6 project has one entry document. The entry and reachable menu fragments
form an explicit directed graph through literal `source` arrays. A separate user
configuration file contains only explicit values.

There are exactly four public shapes:

1. entry document;
2. menu/config fragment;
3. `[[config]]` declaration;
4. user configuration document.

File names do not determine a document's meaning. The CLI identifies the entry
document, graph reachability identifies fragments, and the CLI separately
identifies the user configuration. A directory name or naming convention never
adds a document to the graph.

## 3. Entry document

The entry is the graph's only root.

```toml
schema_version = 6
mainmenu = "Example project configuration"

source = [
  "config/runtime.toml",
  "config/storage.toml",
  "config/diagnostics.toml",
]
```

The only entry keys are:

| Key | TOML type | Required | Meaning |
| --- | --- | --- | --- |
| `schema_version` | integer | yes | Must be exactly `6`. |
| `mainmenu` | string | yes | Non-empty UTF-8 title for the root view. |
| `source` | array of strings | yes | Ordered literal fragment paths; an empty array is valid. |

Rules:

- `mainmenu` must not contain a newline, NUL, ESC, or C0/C1 terminal control.
- `source` order determines presentation order only. It is not value precedence.
- The entry must not contain `[menu]`, `[[config]]`, or `[values]`.
- `include`, `imports`, `files`, and `fragments` are unknown keys, not aliases.
- A project has exactly one entry for a single invocation.

## 4. Menu and fragment documents

A reachable fragment may contain one `[menu]`, zero or more `[[config]]` tables,
or both. A fragment with neither is an error.

```toml
[menu]
prompt = "Runtime"
help = "Configure runtime behavior."
source = [
  "config/runtime/logging.toml",
  "config/runtime/workers.toml",
]
```

The only menu keys are:

| Key | TOML type | Required | Meaning |
| --- | --- | --- | --- |
| `prompt` | string | yes | Non-empty one-line menu label. |
| `help` | string | yes | Non-empty explanatory text. |
| `source` | array of strings | no | Literal child fragments; omission means no children. |

Menu rules:

- A file contains at most one `[menu]` table.
- Config declarations in the same file appear beneath that menu.
- A config-only fragment attaches beneath the menu that includes it.
- `[menu].source` owns child presentation membership.
- A fragment has one presentation parent. Including one fragment from two places
  is a duplicate-include error, not menu reuse.
- Sibling menus use separate fragment files.
- The root `mainmenu` is not counted in visible menu depth.
- Visible menu depth is at most three. The recommended depth is two.
- A menu has no symbol, type, value, default, or dependency expression.

## 5. Config declaration

```toml
[[config]]
symbol = "ENABLE_LOGGING"
type = "bool"
prompt = "Enable logging"
help = "Include runtime logging in the configured product."
default = false
depends_on = "RUNTIME_SUPPORT"
```

The only config keys are:

| Key | TOML type | Required | Applies to | Meaning |
| --- | --- | --- | --- | --- |
| `symbol` | string | yes | all | Stable configuration identifier. |
| `type` | string | yes | all | One of `bool`, `int`, `hex`, `string`, `enum`. |
| `prompt` | string | yes | all | Non-empty one-line user label. |
| `help` | string | yes | all | Non-empty explanation of the user-visible effect. |
| `default` | typed scalar | type-specific | all | Declaration default, never an override layer. |
| `depends_on` | string | no | all | Bounded availability expression. |
| `values` | array of strings | enum only | enum | Closed enum domain. |
| `range` | inline table | no | int, hex | Inclusive `{ min = ..., max = ... }`. |

Common rules:

- `symbol` matches `[A-Z][A-Z0-9_]{0,127}`.
- Symbols are globally unique in one reachable graph.
- A duplicate declaration, alias, redefinition, or override is an error.
- `prompt` is at most 256 UTF-8 bytes and contains no newline.
- `help` is at most 8,192 UTF-8 bytes and must provide an actual explanation.
- Prompt/help NUL, ESC, and unsafe C0/C1 control bytes are rejected.
- Declaration order affects presentation only.
- A value has exactly one declaration default and at most one user assignment.
- A field that does not apply to the declared type is an error even when its
  value looks harmless.

The following are deliberately not public fields or tables:

```text
owner
since
stability
tags
menu_order
placement
allowed
enabled_values
cardinality
namespace
target
profile
selection
build
source_file
object
provider
driver
visible_if
needs
select
imply
choice
rule
assert
inherit
extends
override
source_if
conditional_source
```

Each spelling above must produce an unknown-field or unknown-table error in the
context where it is used. None is accepted and ignored.

## 6. Public value types

### 6.1 `bool`

```toml
[[config]]
symbol = "ENABLE_METRICS"
type = "bool"
prompt = "Enable metrics"
help = "Collect bounded runtime metrics."
```

- Values are native TOML `true` or `false`.
- Omitted `default` means `false`.
- `range` and `values` are errors.
- Integer `0` and `1` and quoted strings are not coerced to bool.
- A new false-default option does not require existing user files to add a
  redundant `OPTION = false` assignment.

### 6.2 `int`

```toml
[[config]]
symbol = "WORKER_COUNT"
type = "int"
prompt = "Worker count"
help = "Set the maximum number of worker contexts."
default = 4
range = { min = 1, max = 64 }
```

- Values are native TOML signed 64-bit integers.
- Omitted `default` means `0`.
- `range` is optional; when present it contains exactly `min` and `max`.
- `min <= max` is required.
- The default and user value must lie in the inclusive range.
- `values` is an error.
- Numeric strings, booleans, floats, and arithmetic expressions are not coerced.

### 6.3 `hex`

```toml
[[config]]
symbol = "DEVICE_ID"
type = "hex"
prompt = "Device identifier"
help = "Set the hexadecimal identifier emitted as configuration data."
default = 0x10e8
range = { min = 0x0000, max = 0xffff }
```

- Input uses TOML's native hexadecimal integer spelling.
- Omitted `default` means `0x0`.
- The schema 6.0 domain is `0x0` through `0x7fff_ffff_ffff_ffff`.
- A quoted hexadecimal string, negative value, decimal spelling, float, and
  arbitrary unsigned 64-bit pattern are not accepted as a hex value.
- `range` follows the same inclusive rules as int and uses hex values.
- Canonical output is lowercase `0x...` without insignificant underscores.
- `values` is an error.

### 6.4 `string`

```toml
[[config]]
symbol = "INSTANCE_LABEL"
type = "string"
prompt = "Instance label"
help = "Set the descriptive label emitted in safe configuration outputs."
default = ""
```

- Values are native UTF-8 TOML strings of at most 4,096 UTF-8 bytes.
- Omitted `default` means the empty string.
- NUL and unsafe terminal control bytes are rejected.
- `range` and `values` are errors.
- The C-header and JSON emitters escape strings for their own target grammars.
- The Make emitter does not support arbitrary string values. Requesting Make
  output for a resolved catalog containing a string is an explicit error; the
  value is never silently omitted.

### 6.5 `enum`

```toml
[[config]]
symbol = "LOG_LEVEL"
type = "enum"
prompt = "Log level"
help = "Select the amount of runtime logging."
values = ["quiet", "normal", "verbose"]
default = "normal"
```

- `values` and `default` are both required.
- The domain contains at least one and at most 256 atoms.
- Each atom is at most 128 bytes and matches `[A-Za-z0-9_.+-]+`.
- Duplicate atoms are errors.
- `default` and every user value must be exact domain members.
- There is no separate display-label mapping and no object-array alternative.
- `range` is an error.

### 6.6 Excluded types

Schema 6.0 has no `tristate`, `placement`, `uint`, `float`, `path`, `file`,
`directory`, `object`, `target`, `driver`, or `module` type. An implementation
must not infer a type from a default's TOML representation.

## 7. Dependency expression

`depends_on` has a closed expression grammar:

```text
expression        := or-expression
or-expression     := and-expression ("||" and-expression)*
and-expression    := unary-expression ("&&" unary-expression)*
unary-expression  := "!" unary-expression
                   | "(" expression ")"
                   | SYMBOL
                   | SYMBOL "==" literal
                   | SYMBOL "!=" literal
literal           := true | false | integer | hexadecimal | quoted-string
```

`!` binds more tightly than `&&`, which binds more tightly than `||`.
Parentheses override precedence.

Semantic rules:

- A bare symbol is allowed only for a bool declaration.
- A comparison's right literal must match the referenced symbol type.
- Enum comparison literals must be members of the enum domain.
- Symbol-to-symbol comparison is not allowed.
- Every symbol reference is linked before evaluation, including references in a
  short-circuited branch.
- Unknown references, self-cycles, multi-symbol cycles, wrong literal types,
  excessive text, nodes, or nesting are errors.
- The expression text limit is 4,096 bytes, AST limit 512 nodes, and expression
  nesting limit 32.

The only purpose of a dependency is availability. It never enables, disables, or
rewrites another value. There is no auto-enable, select, imply, force, conditional
source, environment lookup, file-existence lookup, regular expression, function,
shell expansion, compiler probe, arithmetic, or relational operator.

An unavailable option is disabled in the TUI and shown by default. An explicit
assignment equal to its declaration default is accepted but unnecessary. An
explicit non-default assignment to an unavailable option is an error.

## 8. User configuration

The complete user-file grammar is:

```toml
schema_version = 6

[values]
ENABLE_LOGGING = true
WORKER_COUNT = 8
DEVICE_ID = 0x10e8
LOG_LEVEL = "normal"
```

The only top-level entries are integer `schema_version = 6` and the `[values]`
table. `[values]` may be empty. A missing `[values]` table is equivalent to an
empty table; serializers always write it for a stable canonical shape.

Rules:

- Each key is an exact reachable symbol and occurs at most once.
- Each value is a native TOML scalar matching the declared type.
- Unknown, removed, duplicate, stale, or type-invalid assignments are errors.
- There are no assignment arrays, profiles, targets, inheritance, includes,
  ordered fragments, or last-wins behavior.
- Omission means “use the declaration default”; it does not mean “unknown.”
- `savedefconfig` writes only assignments different from declaration defaults in
  stable symbol order.
- `menuconfig` saves to an immutable snapshot. It never rewrites a source-tracked
  user file implicitly.
- Only `savedefconfig --destination PATH` explicitly updates a destination file.

## 9. Explicit source graph

Every `source` item is a normalized path relative to the project root identified
by `--root`. It is not relative to the including fragment.

Rejected paths include:

- absolute paths;
- empty paths;
- `.` or `..` segments;
- repeated slash;
- backslash;
- glob metacharacters;
- a symlink component;
- a non-regular final object;
- a final name without the `.toml` suffix;
- a duplicate canonical path;
- an include cycle;
- a case-fold collision on a host where the paths would collide.

The implementation follows only literal reachable edges. It must not enumerate a
directory, expand a glob, search for conventional file names, or inspect project
source. An unreferenced invalid TOML file is irrelevant and unread.

Source traversal is deterministic and bounded. Source order determines display
order, but it never creates assignment precedence.

## 10. Resource limits

All limits are public fail-closed limits. They are defined once in the public
limits API and checked before overflow or allocation. Exceeding a limit is an
error, not truncation, partial success, or a warning.

| Resource | Maximum |
| --- | ---: |
| One TOML input | 1 MiB |
| Total configuration input | 64 MiB |
| Source fragments | 4,096 |
| Source edges | 16,384 |
| Include depth | 64 |
| Menus | 4,096 |
| Visible menu depth | 3 |
| Config symbols | 16,384 |
| Source path | 1,024 bytes |
| Prompt | 256 UTF-8 bytes |
| Help | 8,192 UTF-8 bytes |
| String value | 4,096 UTF-8 bytes |
| Enum values per config | 256 |
| Enum atom | 128 bytes |
| Dependency text | 4,096 bytes |
| Dependency AST | 512 nodes |
| Dependency nesting | 32 |
| Diagnostics per invocation | 1,024 |
| Logical render surface | 512 x 256 cells |

## 11. Deterministic resolution

Resolution has no writer precedence graph.

1. Load one declaration for every symbol.
2. Start from its typed declaration default.
3. Validate at most one explicit user assignment.
4. Link and type-check every dependency.
5. Reject dependency cycles.
6. Evaluate availability in a stable topological order.
7. Use a valid user candidate when available; otherwise use the default.
8. Reject an unavailable explicit non-default candidate.

Every successful resolved value records:

- symbol and declared type;
- effective value and declaration default;
- origin `default` or `user`;
- availability;
- a bounded causal reason for unavailability.

There is no derived writer. Source order, declaration order, hash-table order,
menu placement, and emitter selection do not change the resolved result. Output
uses stable lexical symbol order unless a presentation view explicitly uses
declaration order.

## 12. Public CLI

The public command names are exactly:

```text
confit help
confit --version
confit check
confit configure
confit menuconfig
confit verify
confit search
confit explain
confit diff
confit listnewconfig
confit oldconfig
confit olddefconfig
confit savedefconfig
```

There are no command aliases or environment-selected project/profile modes.
Unknown commands and unknown, duplicated, missing-value, or command-inapplicable
options are usage errors.

### 12.1 Path and value options

The closed option vocabulary is:

| Option | Value | Meaning |
| --- | --- | --- |
| `--root` | absolute directory | Explicit project root. |
| `--project` | relative TOML path | Entry document beneath `--root`. |
| `--config` | path | Optional user configuration; relative paths use `--root`. |
| `--other-config` | path | Second user configuration for `diff`. |
| `--output` | absolute directory | Confit-owned snapshot root. |
| `--emit` | `make`, `c-header`, or `json` | Requested optional value projection; repeatable as a set. |
| `--print-artifact` | artifact name | Verified selected artifact to print. |
| `--query` | UTF-8 text | Bounded `search` query. |
| `--symbol` | symbol | Exact symbol for `explain`. |
| `--destination` | path | Explicit `savedefconfig` destination. |

`--root`, `--project`, `--output`, and `--destination` do not have environment
fallbacks. `--config` and `--other-config` may be explicit absolute regular-file
paths or normalized paths relative to `--root`; neither is auto-discovered.

### 12.2 Command option matrix

`R` means required, `O` optional, `M` optional and repeatable, and `-` forbidden.

| Command | root | project | config | other-config | output | emit | print-artifact | query | symbol | destination |
| --- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| `help` | - | - | - | - | - | - | - | - | - | - |
| `--version` | - | - | - | - | - | - | - | - | - | - |
| `check` | R | R | O | - | - | - | - | - | - | - |
| `configure` | R | R | O | - | R | M | - | - | - | - |
| `menuconfig` | R | R | O | - | R | M | - | - | - | - |
| `verify` | R | R | - | - | R | - | O | - | - | - |
| `search` | R | R | O | - | - | - | - | R | - | - |
| `explain` | R | R | O | - | - | - | - | - | R | - |
| `diff` | R | R | O | R | - | - | - | - | - | - |
| `listnewconfig` | R | R | O | - | R | - | - | - | - | - |
| `oldconfig` | R | R | O | - | R | M | - | - | - | - |
| `olddefconfig` | R | R | O | - | R | M | - | - | - | - |
| `savedefconfig` | R | R | - | - | R | - | - | - | - | R |

Command meanings:

- `check` loads, links, and resolves without writing a snapshot.
- `configure` resolves non-interactively and publishes a selected snapshot.
- `menuconfig` edits a memory working copy and publishes only on an explicit
  successful save.
- `verify` validates the selected record, sealed artifact digests, and only the
  exact manifest-listed inputs. It does not rerun the resolver.
- `search` performs case-insensitive bounded matching over prompt, help, and
  symbol in the loaded catalog. It never searches the filesystem.
- `explain` reports a symbol's typed value, origin, availability, and bounded
  causal reason.
- `diff` compares the optional left `--config` (all-default when omitted) with
  required `--other-config` semantically in stable symbol order.
- `listnewconfig` reports symbols present in the current graph but absent from
  the previous selected snapshot's sealed catalog.
- `oldconfig` prompts only for genuinely new user-visible symbols.
- `olddefconfig` accepts current defaults for genuinely new symbols without
  prompting.
- `savedefconfig` atomically writes the selected user intent, omitting all
  default-equal assignments, only to the explicit destination.

Removed, renamed, type-changed, range-changed, and enum-domain-changed symbols
are not silently migrated. A stale assignment is an error. Schema 6 migration
commands do not read schema 5 snapshots.

### 12.3 Invocation examples

```text
confit check \
  --root /path/to/project \
  --project Confit.toml \
  --config configs/development.toml

confit configure \
  --root /path/to/project \
  --project Confit.toml \
  --config configs/development.toml \
  --output /path/to/objects/config \
  --emit make \
  --emit c-header

confit verify \
  --root /path/to/project \
  --project Confit.toml \
  --output /path/to/objects/config \
  --print-artifact values.mk
```

On successful `verify --print-artifact NAME`, stdout contains one canonical
absolute path and one newline. Diagnostics go to stderr. An unverified, missing,
unknown, or unsealed artifact is never printed.

`NAME` is one slash-free exact snapshot role: `user-values.toml`,
`resolved-values.json`, `inputs.manifest`, `provenance.json`, `snapshot.seal`,
`values.mk`, or `values.h`. The optional Make/C roles must have been requested
when the snapshot was created. `resolved-values.json` is a mandatory sealed core
role because verification and configuration review need the exact resolved
record; `--emit json` additionally marks that same role as an explicitly
requested consumer projection. It does not create a second JSON file. Without
that request, `verify --print-artifact resolved-values.json` is refused even
though the internal sealed role exists.

### 12.4 Exit status

| Status | Meaning |
| ---: | --- |
| 0 | Success. |
| 2 | Usage error or unsupported public syntax. |
| 3 | Schema, typed value, or dependency validation failure. |
| 4 | Missing or stale selected snapshot. |
| 5 | Bounded I/O, identity, path, or publication failure. |
| 6 | Terminal capability failure or explicit interactive cancellation. |
| 70 | Internal invariant failure. |

Source diagnostics use:

```text
path:line:column: severity: message
```

Diagnostic count is bounded. Schema 6.0 does not define a machine-readable
diagnostic mode.

## 13. Immutable output and emitters

The selected output root has this logical form:

```text
/objects/config/
├── snapshots/
│   └── <sha256>/
│       ├── catalog.summary
│       ├── user-values.toml
│       ├── resolved-values.json
│       ├── inputs.manifest
│       ├── provenance.json
│       ├── values.mk
│       ├── values.h
│       └── snapshot.seal
└── selected
```

`catalog.summary`, `user-values.toml`, `resolved-values.json`, `inputs.manifest`,
`provenance.json`, and `snapshot.seal` are required core roles. `values.mk` and
`values.h` exist only when their emitters are requested. `--emit json` marks the
required resolved JSON role as an explicitly requested consumer projection; the
sealed core role remains required and no duplicate JSON artifact is created.
`catalog.summary` is a non-printable private role for bounded schema-6 catalog
review and is intentionally absent from the `--print-artifact` vocabulary.

`selected` is a bounded regular file containing a lowercase SHA-256 digest and a
newline. It is not a symlink. Snapshot directories are content-addressed and
create-only. Publication makes all files durable and sealed before atomically
replacing `selected`. A failed candidate never becomes selected.

### 13.1 Make emitter

Supported types are bool, int, hex, and enum:

```make
CONFIG_ENABLE_METRICS=true
CONFIG_WORKER_COUNT=4
CONFIG_DEVICE_ID=0x10e8
CONFIG_LOG_LEVEL=verbose
```

Output contains assignments only. Names derive only from validated symbols and
values use closed literals. User data cannot produce a directive, comment,
variable expansion, rule, newline, or include. Arbitrary strings cause an
explicit whole-emitter error rather than silent omission.

### 13.2 C-header emitter

```c
#ifndef CONFIT_GENERATED_VALUES_H
#define CONFIT_GENERATED_VALUES_H

#define CONFIG_ENABLE_METRICS 1
#define CONFIG_WORKER_COUNT 4
#define CONFIG_DEVICE_ID 0x10e8
#define CONFIG_LOG_LEVEL "verbose"

#endif
```

Bool is always `0` or `1`; integers and hex values use canonical literals; enum
and string values use C-string escaping. The include guard is generic.

### 13.3 JSON emitter

JSON records every resolved symbol, declared type, effective value, declaration
default, origin, and availability in stable symbol order. It uses canonical
escaping and numeric representations. It contains configuration data, not source
paths, commands, hooks, or executable content.

## 14. Direct-authoring example

A project can be authored without a scaffold:

```text
example/
├── Confit.toml
├── config/
│   ├── runtime.toml
│   └── logging.toml
├── configs/
│   └── development.toml
├── Makefile
└── src/
    ├── main.c
    └── metrics.c
```

`Confit.toml`:

```toml
schema_version = 6
mainmenu = "Example application configuration"
source = ["config/runtime.toml"]
```

`config/runtime.toml`:

```toml
[menu]
prompt = "Runtime"
help = "Configure runtime behavior."
source = ["config/logging.toml"]

[[config]]
symbol = "WORKER_COUNT"
type = "int"
prompt = "Worker count"
help = "Set the maximum number of worker contexts."
default = 4
range = { min = 1, max = 64 }
```

`config/logging.toml`:

```toml
[menu]
prompt = "Logging"
help = "Configure optional logging and metrics."

[[config]]
symbol = "ENABLE_METRICS"
type = "bool"
prompt = "Enable metrics"
help = "Compile the optional metrics implementation."
default = false

[[config]]
symbol = "LOG_LEVEL"
type = "enum"
prompt = "Log level"
help = "Select the amount of log output."
values = ["quiet", "normal", "verbose"]
default = "normal"
depends_on = "ENABLE_METRICS"
```

`configs/development.toml`:

```toml
schema_version = 6

[values]
ENABLE_METRICS = true
LOG_LEVEL = "verbose"
```

The project Makefile may call `confit configure` or `confit menuconfig`, then use
`confit verify --print-artifact values.mk` before including the returned path.
The Makefile alone decides which project source is selected from those values.
Confit does not open `Makefile`, `src/main.c`, or `src/metrics.c`, and it does not
run bmake. The direct-authored files are the primary route; a scaffold is not
required for configure or ordinary build.

A conceptual direct-authored bmake integration is:

```make
CONFIT?=confit
CONFIG?=${.CURDIR}/configs/development.toml
CONFIG_OUTPUT?=${.OBJDIR}/config

.PHONY: configure menuconfig all

configure:
	@${CONFIT:Q} configure \
	    --root ${.CURDIR:Q} \
	    --project Confit.toml \
	    --config ${CONFIG:Q} \
	    --output ${CONFIG_OUTPUT:Q} \
	    --emit make \
	    --emit c-header

menuconfig:
	@${CONFIT:Q} menuconfig \
	    --root ${.CURDIR:Q} \
	    --project Confit.toml \
	    --config ${CONFIG:Q} \
	    --output ${CONFIG_OUTPUT:Q} \
	    --emit make \
	    --emit c-header

.if !make(configure) && !make(menuconfig)
CONFIG_VALUES_MK!=${CONFIT:Q} verify \
	--root ${.CURDIR:Q} \
	--project Confit.toml \
	--output ${CONFIG_OUTPUT:Q} \
	--print-artifact values.mk

.if ${.SHELLSTATUS} != 0 || empty(CONFIG_VALUES_MK)
.error configuration is missing or stale; run bmake menuconfig or bmake configure
.endif

.include "${CONFIG_VALUES_MK}"
.endif

SRCS=src/main.c

.if ${CONFIG_ENABLE_METRICS:Ufalse} == "true"
SRCS+=src/metrics.c
.endif

all:
	@# Project-owned compile and link rules consume SRCS and generated values.
```

This Makefile is consumer code, not Confit syntax. Confit neither parses the
conditional nor verifies the source list.

Ordinary build must not configure implicitly. When selected configuration is
missing or stale, the Makefile should stop with guidance such as:

```text
configuration is missing or stale
run:
  bmake menuconfig
or:
  bmake configure
```

## 15. `menuconfig` interaction contract

The layout takes inspiration from btop 1.4.6: dense boxed regions, a fixed key
footer, a fixed detail pane, resize-aware viewports, color-independent state, and
a narrow-terminal fallback. It does not take process-monitoring behavior,
configuration-file semantics, or mandatory mouse input.

The state separation takes inspiration from Vim 9.1 (patches 1-1752): Esc
cancels a submode, colon enters a closed command mode, and save is distinct from
exit. It does not embed Vim script, the general Ex language, mappings, macros,
plugins, shell escape, or an external editor. These tools are UX references, not
build, link, or runtime dependencies, and their source is not incorporated.

The interactive model has exactly these states:

```text
NORMAL
EDIT
SEARCH
COMMAND
HELP
DIFF
ENUM_PICKER
```

### 15.1 Normal mode

| Key | Action |
| --- | --- |
| `j`, Down | Next row. |
| `k`, Up | Previous row. |
| `h`, Left | Parent menu or previous pane. |
| `l`, Right, Enter | Enter child menu or typed editor. |
| Space | Toggle bool only. |
| `e` | Open enum/int/hex/string editor. |
| `/` | Enter search. |
| `n`, `N` | Next/previous search match. |
| `u` | Undo. |
| Ctrl-R | Redo. |
| `d` | Semantic diff view. |
| `?` | Help. |
| `:` | Closed command mode. |
| Esc | Cancel transient state and remain in NORMAL. |
| `q` | Do not exit; show `use :q, :wq, or :q!`. |

Enter accepts an edit candidate only after typed validation. Esc discards the
candidate and returns to NORMAL without changing value, dirty bit, or undo
history. Bool does not use a text editor. Enum uses a closed picker; int, hex,
and string use bounded editors. Search matches prompt, help, and symbol only.

### 15.2 Command mode

The command set is exactly:

```text
:w
:q
:q!
:wq
:x
:help
:set unavailable
:set nounavailable
```

- `:w` publishes a complete immutable selected snapshot and stays in the TUI.
- `:q` exits only when clean; dirty state is refused.
- `:q!` explicitly discards working changes and exits.
- `:wq` exits only after a successful save; a failed save keeps the TUI open.
- `:x` saves then exits when dirty, and exits directly when clean.
- `:help` shows command help.
- `:set unavailable` shows unavailable rows.
- `:set nounavailable` hides unavailable rows without changing semantics.
- Unknown commands execute nothing. There is no shell escape, pipe, redirect,
  glob, command history, mapping, macro, plugin, or external editor.

Esc is never an exit key. To leave an editor, the user first presses Esc to
return to NORMAL, then enters one of the explicit colon exit commands.

## 16. Compatibility and conformance falsifiers

An implementation does not conform to this contract if any of the following is
true:

- it accepts an unknown field, table, type, alias, or legacy schema fallback;
- adding a normal project option requires changing Confit core code;
- graph membership depends on directory enumeration or a naming convention;
- an unreferenced file changes a result or diagnostic;
- a dependency writes another symbol or automatically enables a prerequisite;
- a user must provide ownership metadata, placement metadata, build dependency,
  or redundant false-default assignments;
- source order or duplicate declarations create last-wins behavior;
- a product command executes a compiler, bmake, shell, hook, or plugin;
- a product command reads project source or a Makefile;
- `verify` reruns resolution or scans a project tree;
- emitter data can create target-language syntax outside its closed literal;
- a partial snapshot can become selected;
- invalid TUI input changes the working state;
- Esc or plain `q` exits the TUI;
- a failed save exits or changes selected state;
- a mandatory build or runtime path depends on Python, CMake, Ninja, ncurses, or
  another external schema tool.

Passing documentation review alone does not establish conformance. Later rounds
must provide compile, unit, integration, exact-I/O, import-surface, hostile-file,
snapshot, PTY, sanitizer, and manual UX evidence for the implemented portions.
