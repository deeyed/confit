---
doc_type: implementation-contract
status: active
authority: confit-v6-cli
depends_on:
  - docs/config-v6.md
  - docs/architecture-v6.md
  - docs/schema-v6.md
  - docs/resolver-v6.md
  - docs/snapshot-v6.md
---

# Confit v6 conventional configuration CLI

This document records the R16-R17 implementation boundary for the conventional,
non-interactive schema 6 command line. The normative public syntax remains
`docs/config-v6.md`; this document says which frozen commands are executable in
R16, how their output is bounded, and which names are only reserved for later
rounds.

The CLI is a controller over the generic project loader, user-value linker,
resolver, emitter, and immutable snapshot APIs. It does not add configuration
semantics. It does not know source files, build rules, kernels, drivers,
compilers, linkers, targets, profiles, or consumer repositories.

## 1. Implemented command set

The conventional controller implements these commands:

| Command | Successful operation | Writes project source? |
| --- | --- | :---: |
| `help` | Print the closed command and option vocabulary. | No |
| `--version` | Print product, schema contract, and implementation identity. | No |
| `check` | Load the explicit graph and optional user file, then resolve. | No |
| `configure` | Resolve, emit only requested projections, and publish a selected snapshot. | No |
| `verify` | Verify the selected snapshot, its seal, and exact manifest-listed inputs. | No |
| `search` | Match one bounded query against loaded symbols, prompts, and help. | No |
| `explain` | Print one resolved symbol's type, values, origin, availability, and reason. | No |
| `diff` | Resolve two user configurations and print semantic differences. | No |
| `listnewconfig` | List genuinely new symbols against the selected sealed catalog. | No |
| `oldconfig` | Prompt only for new symbols and publish a reviewed snapshot. | No |
| `olddefconfig` | Accept new defaults and publish a reviewed snapshot. | No |
| `savedefconfig` | Atomically write selected minimal intent to an explicit destination. | Yes, destination only |

`menuconfig` runs the full-screen POSIX terminal frontend over the same loaded
catalog, resolver, emitter request, and immutable snapshot publisher used by
`configure`. It requires TTY standard input and output; redirected or piped
use fails before raw mode. The terminal layer owns no project filesystem I/O
and delegates an explicit save request back to the CLI snapshot adapter.

The migration commands use the same frozen option matrix and are detailed in
`docs/migration-v6.md`. No alias, rename map, schema-5 parser, or hidden history
lookup accompanies them.

## 2. Closed parsing rules

The option vocabulary and per-command matrix are exactly those in
`docs/config-v6.md`. Parsing has the following properties:

- every option has exactly one following value;
- an ordinary option may occur once;
- `--emit` may repeat only for distinct `make`, `c-header`, and `json` members;
- duplicate emitters, duplicate ordinary options, unknown options, unknown
  commands, missing values, and command-inapplicable options are usage errors;
- positional operands, abbreviated options, `--option=value`, aliases, and an
  option terminator are not accepted;
- `help` and `--version` accept no options;
- `--root` and `--output` are normalized absolute paths;
- `--project` is a normalized project-root-relative `.toml` path;
- `--config` and `--other-config` are normalized relative or absolute `.toml`
  paths and are never discovered;
- `--symbol` uses the public configuration-symbol grammar;
- `--print-artifact` accepts only the seven frozen artifact names;
- `--query` is non-empty, bounded UTF-8 without C0/C1 terminal controls.

The parser reads no environment variable. Names such as `ARCH`, `KERNCONF`,
`TARGET`, `PROFILE`, or consumer-specific selectors cannot alter root, entry,
user configuration, output, emitters, or command behavior.

## 3. Loading and resolution

Commands that need configuration meaning open the explicit absolute project
root as a descriptor capability. They load only the explicit relative entry,
follow only its literal reachable source graph, optionally load the exact user
configuration named on the command line, and invoke the deterministic resolver.
Omitting `--config` means an empty assignment set and therefore current schema
defaults; it does not trigger a file-name convention or search.

An absolute user configuration is opened through its explicit absolute parent
directory capability and a normalized leaf. Its exact input image owns the full
absolute display path. Parse and hash still use the same one-time byte image. If
that user input is published in a snapshot, the manifest records the exact
absolute path for the `user` role so `verify` can remeasure the same file. Entry
and fragment records remain project-root-relative and an absolute path in either
role is rejected.

This location-sensitive rule is limited to an explicitly selected external
user file. It does not permit absolute `source` edges, project relocation to
reinterpret an external user file, directory enumeration, or filesystem search.

## 4. Command results

### 4.1 `check`

`check` builds and destroys an in-memory project, optional typed user config,
and resolution. Success writes exactly:

```text
configuration is valid
```

It creates no output root, snapshot, generated value file, or source change.

### 4.2 `configure`

`configure` asks the emitter for only the explicitly requested projection set.
Make and C header bytes are passed as inert optional snapshot artifacts. JSON
uses the required `resolved-values.json` core member and makes it printable only
when `--emit json` was requested. With no `--emit`, the immutable core snapshot
is still valid and contains no optional Make or C artifact.

Publication uses the snapshot layer's lock, create-only content address, seal,
and atomic regular `selected` record. Success writes the selected digest in:

```text
configured snapshot <lowercase-sha256>
```

The CLI does not create an absent output root. The caller provisions the
explicit writable directory under the bootstrap contract.

### 4.3 `verify`

`verify` opens the explicit project and output roots and calls only the snapshot
verifier. It does not load TOML as a schema, link dependencies, or resolve.
Without `--print-artifact`, success writes:

```text
configuration is current
```

With a requested printable artifact, successful verification first returns a
bounded output-root-relative sealed path. The CLI joins that path to the already
validated absolute `--output` and writes exactly one absolute path and one
newline. It prints no path if selection, seal, artifact, or input verification
fails.

### 4.4 `search`

`search` iterates the resolved catalog's stable symbol order and checks only the
already loaded symbol, prompt, and help strings. Matching folds ASCII `A-Z` to
`a-z` and compares every non-ASCII UTF-8 byte exactly. It is therefore
locale-independent and deterministic; schema 6 does not claim Unicode case
folding or normalization.

Each match is one tab-separated line:

```text
SYMBOL<TAB>type<TAB>prompt
```

No match is a successful empty result. Search never inspects a source path,
directory entry, arbitrary TOML file, Makefile, or C file.

### 4.5 `explain`

`explain` requires one exact symbol and prints stable labelled lines for symbol,
type, prompt, effective value, default, origin, and availability. If the
resolver owns a bounded causal reason, it appends one escaped `reason` line.
String-like values and reason text are quoted with deterministic escaping so
embedded data cannot create terminal control sequences or forged output lines.

An absent symbol is a validation error. Explain does not infer build effects,
source selection, or consumer behavior from the symbol.

### 4.6 `diff`

`diff` uses schema defaults on the left when `--config` is omitted and always
requires `--other-config`. Both sides resolve against the same loaded catalog
and dependency plan. It compares effective typed values and availability in
stable symbol order. Each difference is one line:

```text
SYMBOL: left-value [available] -> right-value [unavailable]
```

An empty output is a successful semantic equality. The command does not use
assignment order, textual TOML diff, last-wins precedence, or filesystem
metadata.

## 5. Diagnostics and exit status

Diagnostics go to stderr. A diagnostic with an owned input path uses
`path:line:column: error: message`; controller diagnostics use
`confit: command: error: message`. Usage failures are explicitly labelled as
usage errors before any configuration operation.

Exit status follows the frozen status table:

| Code | Class |
| ---: | --- |
| 0 | success |
| 2 | usage or unavailable reserved command |
| 3 | parse |
| 4 | validation or stale configuration |
| 5 | I/O or publication |
| 6 | terminal frontend unavailable, interrupted, or failed |
| 70 | internal invariant |

Output stream failure is an I/O error. A command never reports success after
failed configuration publication or failed verified-path output.

## 6. Security and genericity boundary

The CLI adds no subprocess, shell, environment lookup, directory walk,
plugin, network, compiler probe, build invocation, or raw consumer-source read.
Its only file inputs are those already authorized by the product contract. Its
only mutation is immutable snapshot publication under an explicit output root.

The implementation deliberately contains no C-source-to-symbol check, source
membership inference, object graph, link ordering, Makefile interpretation, or
project layout convention. Emitters are value projections; the CLI cannot turn
them into build semantics.

R16-R17 integration evidence exercises exact option rejection, environment poison,
relative and absolute user input, check, search, explain, semantic diff,
requested-only emission, immutable publication, verify-before-print, stale
external user input, and the explicit deferred-command statuses. That evidence
does not establish TUI behavior, schema-5 migration, an ordinary project build,
release-candidate quality, or compatibility with schema 5.
