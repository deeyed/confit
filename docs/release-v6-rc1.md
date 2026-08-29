---
doc_type: release-notes
status: release-candidate
authority: confit-1.0.0-rc1
product_version: 1.0.0-rc1
schema_version: 6
release_branch: codex/confit-v6
---

# Confit 1.0.0-rc1 / schema 6 release notes

## Release identity

Confit 1.0.0-rc1 is the first release candidate for the hard-cut schema 6
generic configuration product. The command below identifies the product and
schema without selecting a project from the environment:

```text
confit 1.0.0-rc1
schema_contract=6
schema_implementation=configuration-cli
```

This release candidate exists only on `codex/confit-v6`. It is not a tag, a
main-branch merge, or a compatibility update for a schema 5 consumer.

## What the product does

Confit reads one CLI-selected entry TOML, only the fragments reachable through
literal `source` edges, and an optional CLI-selected user value TOML. It
validates a closed set of bool, int, hex, string, and enum declarations,
evaluates bounded availability expressions, resolves values deterministically,
and publishes selected immutable configuration snapshots.

The complete user definition surface remains:

```text
entry:       schema_version, mainmenu, source
fragment:    [menu], [[config]]
menu:        prompt, help, source
config:      symbol, type, prompt, help, default, depends_on, values, range
range:       min, max
user config: schema_version, [values]
```

Unknown keys and tables are errors. There is no owner, placement, source-file,
object, driver, target, profile, ordered override, inheritance, `select`,
`imply`, hidden writer, or conditional include surface. A user does not fill in
false defaults or lifecycle metadata. `savedefconfig` writes only explicit
non-default intent.

The conventional command set is:

```text
help --version check configure menuconfig verify search explain diff
listnewconfig oldconfig olddefconfig savedefconfig
```

## Direct-authoring workflow

The primary workflow requires no scaffold. A project author writes
`Confit.toml`, its literal fragments, a small `[values]` file when desired, and
an ordinary build file. Configuration and the ordinary build remain separate:

```text
confit check/configure/menuconfig
        -> selected immutable snapshot
        -> verify one sealed values artifact
        -> the project build consumes typed values
```

The tracked `examples/generic` project demonstrates explicit configure followed
by bmake consumption. The build file alone owns source membership. Confit does
not open its Makefile or C source and does not execute bmake or clang.

## Interactive configuration

`menuconfig` uses a bounded terminal-independent model and a POSIX terminal
frontend. Normal, edit, enum, search, command, help, and diff states are
separate. Escape cancels a submode but never exits. Plain `q` never exits. The
closed commands are `:w`, `:q`, `:q!`, `:wq`, `:x`, `:help`, and the two
unavailable-row display settings. Dirty `:q` is rejected and a failed save
leaves both the working state and terminal session active.

## Security and bootstrap boundary

The product has no subprocess, directory enumeration, dynamic plugin, project
environment selector, compiler probe, build-graph inspection, or consumer
source-analysis capability. Snapshot publication uses bounded descriptor-rooted
I/O, exclusive candidates, fsync ordering, an immutable content-addressed
directory, a regular selected record, and an advisory writer lock. Verification
checks the selected seal and exact manifest-listed input paths without resolving
or scanning again.

Make, C-header, and JSON projections each use a closed target-language encoder.
Arbitrary strings are not accepted by the Make emitter. C and JSON text is
escaped for its own grammar. Only requested optional projections are published.

The supported bootstrap claim starts from an already existing writable object
directory and provisioned clang, bmake, and shell. Source and test membership is
literal in bmake files. Python, CMake, Ninja, ncurses, a parser generator, an
external TOML processor, and a plugin system are not mandatory build or runtime
dependencies.

## Audit result and nonclaims

The final R22 audit closed with Critical 0, High 0, release-affecting Medium 0,
unapproved syntax 0, product subprocess capability 0, product source-tree
traversal 0, and blocking TUI defects 0. The evidence includes strict C17
builds, 23 literal C test binaries, ASan/UBSan, bounded libFuzzer runs, complete
first-party static analysis, repeated concurrency/PTY regression, compiled
import inspection, exact-read ledgers, a direct terminal session, and a fresh
clang+bmake build.

This remains a release candidate, not arbitrary-input certification,
power-loss certification, accessibility certification, all-host portability,
consumer migration, build correctness, kernel-image evidence, boot evidence,
deployment evidence, or hardware evidence. See
[`audits/confit-v6-r22.md`](audits/confit-v6-r22.md) for exact evidence and
limitations.
