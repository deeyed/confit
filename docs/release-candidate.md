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
`config/`, CMake, QStar, and runtime source edits stay out of scope unless the
user explicitly widens the edit boundary. Confit may generate apply-ready build
fragments such as `config.cmake` and `config.qst` under an explicit `--out`
directory.

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

## Round 11 Manual TUI QA

Round 11 performed an interactive PTY-backed terminal QA session with temporary
fixture copies under `/tmp/confit-round11-qa`.

Evidence:

```text
tools/confit/tests/manual/round11-terminal-manual-qa.md
```

Coverage:

```text
profile tui:
  browse -> search -> bool toggle -> help/detail -> int edit -> save
  -> dirty quit confirmation -> discard

schema tui:
  schema warning -> guarded schema editor -> new enum option
  -> choices/default/help/category/tags field dialogs -> save
```

The saved profile was rechecked with `confit check` and `confit explain`. The
saved schema was rechecked with `confit graph` and `confit list`.

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
  --parus tools/confit/tests/fixtures/realish/parus \
  --delos tools/confit/tests/fixtures/realish/delos \
  --profile parus-delos-debug \
  --compat tools/confit/tests/fixtures/realish/compat \
  --format json
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
- The TUI is ncurses-based and no longer uses a first-party vendor shim.
- Schema edit mode writes human-readable TOML and displays a warning, but schema
  changes still require code review.

## Kconfiglib Comparison

Confit TUI is now close enough to exercise real profile/schema workflows in a
menuconfig-style terminal interface, but it should not yet be described as fully
equivalent to Python kconfiglib menuconfig.

What is comparable enough for early Confit use:

- ncurses-backed full-screen menu layout with title, header, boxed menu, key
  legend, and status line.
- Kconfig-like navigation keys for arrows, `j/k`, page movement, home/end,
  search, help/detail, save, and quit confirmation.
- Profile editing with bool toggle, enum choice popup, typed value dialogs,
  validation at edit time, save confirmation, reload after save, and dirty quit
  confirmation.
- Schema editing with an explicit guarded warning, field dialogs, type/default/
  range/choice validation, schema/graph validation before save, and TOML output.

Remaining gaps before claiming kconfiglib-level maturity:

- Confit schema does not yet model a full Kconfig menu tree with explicit menu,
  choice, comment, and source/include nodes. Category grouping approximates this
  for Confit but is not the same structure.
- Search is useful but still simpler than kconfiglib symbol search. It lacks a
  dedicated rich result browser with jump history and all symbol metadata.
- Help/detail is practical, but not as polished as mature menuconfig help for
  long text wrapping, symbol references, and dependency expression rendering.
- Dependency UX explains current blocking state, but complex reverse dependency
  exploration and fix suggestions are still shallow.
- Schema editor is guarded and useful for fixture-level schema work, but real
  project schema changes still need review outside the TUI.
- Native Windows terminal/curses behavior has not been validated.
