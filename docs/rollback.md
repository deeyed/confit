---
doc_type: operations-guide
status: accepted
authority: operational
---

# Confit Rollback Guide

Confit rollback is intentionally file-based. Confit does not run a runtime
service, does not keep hidden binary state, and does not mutate sibling
repositories implicitly.

## Dry-Run Rollback

For a cutover dry-run package, rollback means deleting the generated rehearsal
directory:

```sh
rm -rf /tmp/confit-cutover/delos-realish
rm -rf /tmp/confit-cutover/parus-realish
```

Each dry-run output directory contains a `ROLLBACK.md` file with the exact
directory path for that run.

No source TOML rollback is required after a dry-run because the dry-run only
writes under the requested `--out` directory.

## Generated Artifact Rollback

If generated artifacts were manually copied from a dry-run package into a build
tree, remove only those copied generated files:

```text
config.h
config.cmake
config/config.qsm
config.qst
config.report.json
config.explain.txt
config.graph.json
config.inputs.json
```

After removing generated files, rerun the previous non-Confit build
configuration flow. Do not edit source TOML while rolling back generated build
artifacts.

## Profile Or Schema Edit Rollback

Profile edits and guarded schema edits are TOML edits. Roll them back with the
normal source-control workflow for the Confit project directory:

```sh
git diff -- <project>/config
git restore -- <project>/config
```

Use the project-specific source-control command only after confirming that the
path points to the intended Confit project. Do not run broad restore/reset
commands from a parent repository.

## Incident Notes

When rollback is needed, record:

```text
project root
profile
target
generated output directory
failed command
first failing diagnostic
files removed or restored
```

This keeps generated-artifact rollback separate from source TOML rollback and
prevents accidental runtime source changes.
