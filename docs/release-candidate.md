---
doc_type: developer-guide
status: draft
authority: operational
last_verified: 2026-06-23
---

# Release Candidate Notes

Confit v0 release-candidate scope is a host-side configuration workflow. It can
load fixture projects, validate option schemas and dependency graphs, resolve
profiles, explain resolved values, generate C configuration artifacts, check
cross-project compatibility rules, and edit profile/schema TOML through the TUI.

Confit remains confined to `tools/confit/` in this repository. Real Parus/Delos
`config/`, CMake, QStar, runtime source, and generated build fragments stay out
of scope for this 20-round prototype.

## RC Gate

Run the full local gate from the repository root:

```sh
tools/confit/tests/run_tests.sh
git diff --check -- tools/confit
```

The test runner includes unit tests, CLI workflow tests, TUI scripted tests, and
the Round 20 synthetic scale test. The scale test generates a temporary project
with 2,500 options, then runs:

```text
check -> list -> graph -> gen
```

The generated project lives under the CMake build directory and is not committed
as a large fixture.

## Example Commands

After the local gate, the default binary is:

```sh
CONFIT_BIN=${TMPDIR:-/tmp}/confit-build/confit
```

Single-project validation:

```sh
$CONFIT_BIN check \
  --project tools/confit/tests/fixtures/schema/valid/basic \
  --profile sim-dsh
```

Generate `config.h` and deterministic reports:

```sh
$CONFIT_BIN gen \
  --project tools/confit/tests/fixtures/schema/valid/basic \
  --profile sim-dsh \
  --out /tmp/confit-generated/delos/sim-dsh
```

Explain one option:

```sh
$CONFIT_BIN explain \
  --project tools/confit/tests/fixtures/schema/valid/basic \
  --profile sim-dsh \
  delos.debug.ddc
```

Check Parus/Delos-style compatibility fixtures:

```sh
$CONFIT_BIN compat \
  --parus tools/confit/tests/fixtures/compat/parus \
  --delos tools/confit/tests/fixtures/compat/delos \
  --profile parus-delos-debug \
  --compat tools/confit/tests/fixtures/compat/rules/pass
```

Open profile editing TUI:

```sh
$CONFIT_BIN tui \
  --project tools/confit/tests/fixtures/tui/profile-editor \
  --profile edit
```

Open guarded schema editing TUI:

```sh
$CONFIT_BIN tui \
  --project tools/confit/tests/fixtures/tui/schema-editor \
  --schema-edit
```

## Portability Review

| Area | Review |
|---|---|
| Language | C17 subset, compiled with `-Wall -Wextra -Werror -pedantic` on AppleClang. |
| Build | CMake 3.20 project contained entirely in `tools/confit/`. |
| Hosted services | Filesystem, paths, stdout/stderr, and directory creation are isolated behind `src/host/`; core/model layers do not call hosted APIs directly. |
| Paths | Confit uses host path join helpers for source paths and generated output paths. |
| Determinism | Generated `config.h` and reports avoid timestamps and use stable option order. |
| macOS | Verified by the local gate on 2026-06-23. |
| Linux | Expected to work with CMake, a C17 compiler, and POSIX `/bin/sh` test scripts; not executed in this round. |
| Windows | Source has MSVC warning flags and host path boundaries, but the current integration tests use `/bin/sh`; native Windows CI needs wrapper scripts before claiming full Windows validation. |

## Release Caveats

- `config.cmake` and `config.qst` generators are intentionally not implemented.
- Real Parus/Delos `config/` adoption remains a later integration round.
- The TUI vendor is a small local shim for deterministic tests, not a polished
  terminal UI library.
- Schema edit mode writes human-readable TOML and displays a warning, but schema
  changes still require code review.
