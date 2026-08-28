---
doc_type: implementation-contract
status: active
authority: confit-v6-migration
depends_on:
  - docs/config-v6.md
  - docs/user-config-v6.md
  - docs/snapshot-v6.md
  - docs/cli-v6.md
---

# Confit v6 configuration review and oldconfig workflows

R17 implements schema-6-to-schema-6 catalog evolution. It is not a schema 5
compatibility parser, a source-control history reader, a rename engine, or a
consumer build migration tool. The comparison has exactly two authorities:

1. the previous selected snapshot's sealed, bounded `catalog.summary`;
2. the current catalog loaded from the caller's explicit `--root` and
   `--project` graph.

Confit does not enumerate a directory, search for a previous file, inspect Git,
open C or Make sources, infer source membership, or execute another program.

## 1. Sealed catalog identity

Every newly published snapshot contains the non-printable required core member
`catalog.summary`. It is a private `confit-catalog-summary-v1` line protocol in
lexical symbol order. Each record binds:

- exact symbol and one of the five public types;
- SHA-256 identities of prompt and help text;
- the canonical typed default;
- numeric range or ordered enum domain;
- exact dependency text.

Length-framed canonical values are hashed before entering the summary. Arbitrary
string, help, or dependency bytes therefore cannot inject a line or field. The
summary is bounded by the existing symbol and snapshot byte ceilings, sealed as
role `catalog`, and never eligible for `verify --print-artifact`.

Migration review validates `selected`, the seal content address, every
seal-enumerated artifact identity, and the exact summary bytes. It intentionally
does not remeasure the old manifest's definition inputs: those bytes changing is
the reason review is running. Ordinary `verify` retains its stricter behavior
and still rejects any changed manifest-listed input.

A selected snapshot created before R17, a schema-5 snapshot, a missing summary,
an invalid summary, or a summary whose bytes no longer match the seal fails
closed. There is no fallback to `resolved-values.json`, timestamps, Git, or a
filesystem search.

## 2. Change categories

The generic review model reports a lexical union of previous and current
symbols. One symbol can have several flags:

| Category | Meaning | Automatic oldconfig action |
| --- | --- | --- |
| `new` | Current exact symbol was absent previously. | Prompt or accept current default. |
| `removed` | Previous exact symbol is absent currently. | Stop for explicit review. |
| `type` | Public value kind changed. | Stop; never coerce. |
| `domain` | Numeric range or ordered enum values changed. | Stop; never clamp or remap. |
| `default` | Canonical typed default changed. | Stop; never silently reinterpret omission. |
| `dependency` | Exact availability expression changed. | Stop; never silently reinterpret availability. |
| `prompt` | User-facing prompt changed. | Informational; not a value migration. |
| `help` | Explanatory help changed. | Informational; not a value migration. |

Declaration order and `source` order are not catalog identity. Reordering an
otherwise identical catalog produces no change. Exact symbol identity means a
rename is one removal plus one addition; v6 has no alias or rename-map syntax.

The four CLI workflows refuse every semantic category before publishing. A
stale current user assignment is already rejected by the normal user-config
loader, so a removed assignment cannot be hidden by `olddefconfig`.

## 3. Commands

### `listnewconfig`

`listnewconfig` loads and resolves the optional current `--config`, validates
the previous selected summary, and writes one genuinely new symbol per line:

```text
SYMBOL<TAB>type<TAB>prompt
```

An unchanged catalog succeeds with empty stdout. A semantic incompatibility,
missing selection, corrupt summary, stale current assignment, or invalid current
graph is an error rather than a partial or misleading list.

### `olddefconfig`

`olddefconfig` performs the same comparison, accepts the current declaration
default for every genuinely new symbol, resolves the optional existing user
configuration, and publishes one new immutable snapshot. It never writes the
source user file. Requested `--emit` projections use the same emitter and
snapshot path as `configure`.

### `oldconfig`

`oldconfig` asks only about genuinely new symbols, in lexical symbol order.
Prompts are type-aware:

- bool: `y/N` or `Y/n`, accepting `y`, `yes`, `true`, `n`, `no`, `false`;
- int: exact signed decimal text;
- hex: lowercase-prefix `0x` followed by hexadecimal digits;
- enum: one exact member from the displayed closed list;
- string: bounded raw UTF-8 text, with `""` denoting an explicit empty string.

An empty response accepts the displayed current default and adds no filler
assignment. EOF before all new symbols are reviewed, an overlong line, invalid
typed input, range/domain failure, unavailable non-default value, output error,
or publication failure leaves the previous `selected` record unchanged.

The controller combines the optional source user assignments with reviewed new
assignments as one unordered explicit set and invokes the ordinary deterministic
resolver. Snapshot publication verifies that every user-origin resolved value
matches that explicit set. The source user input remains provenance when one was
provided; reviewed values are represented by the canonical sealed
`user-values.toml`.

### `savedefconfig`

`savedefconfig` first performs ordinary full `verify` on the selected snapshot,
including exact current manifest inputs. It then loads the verified sealed
`user-values.toml`, links it to the current catalog, resolves it, and invokes the
same R13 minimal serializer used by every other save path. Only an explicit
`--destination` is changed through the descriptor-rooted atomic single-file
writer.

A relative destination is beneath `--root`; an absolute destination opens its
explicit parent capability without symlink traversal. Defaults are omitted,
symbols are lexical, and a failed write exposes the old complete destination or
no destination, never a partial file.

## 4. Nonclaims

R17 evidence covers schema-6 catalog comparison, line-oriented review,
non-interactive default acceptance, minimal selected-intent saving, corruption
rejection, and selected non-mutation on EOF. It does not prove schema-5
migration, rename inference, consumer build correctness, terminal menuconfig,
kernel configuration adoption, boot, emulation, hardware behavior, arbitrary
input safety, or all-filesystem crash consistency.
