---
doc_type: developer-guide
status: draft
authority: operational
last_verified: 2026-06-24
---

# Release Candidate Notes

Confit v0 release-candidate scope is a host-side configuration workflow. It can
load fixture projects, validate option schemas and dependency graphs, resolve
profiles, compare profiles, explain resolved values, generate deterministic
configuration artifacts, check cross-project compatibility rules, and edit
profile/schema TOML through the TUI on supported terminal hosts.

Confit remains confined to `tools/confit/` in this repository. Real Parus/Delos
`config/`, CMake, QStar, and runtime source edits stay out of scope unless the
user explicitly widens the edit boundary. Generated files are written only under
an explicit `--out` directory.

## RC Gate

Run the full local gate from the repository root:

```sh
tools/confit/tests/run_tests.sh
git diff --check -- tools/confit
```

The test runner includes unit tests, C integration tests, CLI workflow tests,
TUI scripted tests, cutover dry-run checks, and the synthetic scale test. The
scale test generates a temporary project with 5,000 options, then runs:

```text
check -> list -> graph -> gen
```

The generated project lives under the CMake build directory and is not committed
as a large fixture.

For a focused scale rerun:

```sh
CONFIT_STRESS_OPTION_COUNT=5000 \
  /bin/sh tools/confit/tests/integration/round20_stress.sh \
  "${TMPDIR:-/tmp}/confit-build/confit" \
  tools/confit \
  /tmp/confit-stress-5000
```

## Installed Tool Smoke

After local install, the required artifact is a single executable:

```text
<prefix>/bin/confit
```

Basic smoke:

```sh
confit --version
confit doctor
confit help
confit help diff
```

The installed command must not create or modify any project `config/` tree.

## Example Commands

Single-project validation:

```sh
confit check \
  --project tools/confit/tests/fixtures/schema/valid/basic \
  --profile sim-dsh
```

Resolve without writing artifacts:

```sh
confit resolve \
  --project tools/confit/tests/fixtures/schema/valid/basic \
  --profile sim-dsh \
  --format json
```

Compare two resolved profiles:

```sh
confit diff \
  --project tools/confit/tests/fixtures/schema/valid/basic \
  --profile sim-dsh \
  --base debug \
  --format json
```

Generate deterministic artifacts:

```sh
confit gen \
  --project tools/confit/tests/fixtures/schema/valid/basic \
  --profile sim-dsh \
  --out /tmp/confit-generated/delos/sim-dsh \
  --artifact all
```

Expected generated files:

```text
config.h
config.report.json
config.explain.txt
config.graph.json
config.inputs.json
config.cmake
config.qst
```

Explain one option:

```sh
confit explain \
  --project tools/confit/tests/fixtures/schema/valid/basic \
  --profile sim-dsh \
  delos.debug.ddc
```

Check Parus/Delos-style compatibility fixtures:

```sh
confit compat \
  --parus tools/confit/tests/fixtures/realish/parus \
  --delos tools/confit/tests/fixtures/realish/delos \
  --profile parus-delos-debug \
  --compat tools/confit/tests/fixtures/realish/compat \
  --format json
```

Open profile editing TUI on macOS/Linux hosts with curses:

```sh
confit tui \
  --project tools/confit/tests/fixtures/tui/profile-editor \
  --profile edit
```

Open guarded schema editing TUI:

```sh
confit tui \
  --project tools/confit/tests/fixtures/tui/schema-editor \
  --schema-edit
```

## Safety Review

Confit release-candidate behavior follows these safety rules:

- No network access is required for build, tests, install, validation, or
  generation.
- No runtime service is started or required.
- No hidden binary database is used for profile or schema authority.
- Profile and schema writes are human-readable TOML writes.
- Generated artifacts are written only under an explicit `--out` directory.
- `confit init`, `confit profile`, and `confit tui --schema-edit` are the only
  project-source write paths.
- Real Parus/Delos source trees are represented by fixtures or dry-run mirrors
  until a separate integration round explicitly widens the boundary.

## Portability Review

| Area | Review |
|---|---|
| Language | C17 subset, compiled with strict warning flags on AppleClang. |
| Build | CMake 3.20 project contained entirely in `tools/confit/`. |
| Hosted services | Filesystem, paths, stdout/stderr, and directory creation are isolated behind `src/host/`; core/model layers do not call hosted APIs directly. |
| Paths | Confit uses host path helpers for source paths and generated output paths. |
| Determinism | Generated artifacts avoid timestamps and use stable option order. |
| macOS | Verified by the local gate on 2026-06-24. |
| Linux | Expected to work with CMake, a C17 compiler, `/bin/sh` for shell integration tests, and curses/ncurses development files for TUI builds. |
| Windows | Contracted as CLI-only with GNU-style Clang and a native build driver. TUI is explicitly unsupported and should return exit code `8`. Native Windows host execution still needs a dedicated machine/CI gate before it is called fully validated. |

## Manual TUI QA Evidence

Interactive TUI evidence is recorded under:

```text
tools/confit/tests/manual/
```

Current coverage includes profile browsing, search, bool toggle, typed edit,
help/detail, save, dirty quit confirmation, guarded schema-edit entry, schema
field editing, and schema save validation.

## Kconfiglib Comparison

Confit TUI is close enough to exercise real profile/schema workflows in a
menuconfig-style terminal interface, but it should not yet be described as fully
equivalent to Python kconfiglib menuconfig.

Comparable enough for early Confit use:

- ncurses-backed full-screen menu layout with title, header, boxed menu, key
  legend, and status line.
- Kconfig-like navigation keys for arrows, `j/k`, page movement, home/end,
  search, help/detail, save, and quit confirmation.
- Profile editing with bool toggle, enum popup, typed value dialogs, edit-time
  validation, save confirmation, reload after save, and dirty quit confirmation.
- Schema editing with an explicit warning, field dialogs, type/default/range/
  choice validation, schema/graph validation before save, and TOML output.

Remaining gaps before claiming kconfiglib-level maturity:

- Confit schema does not model a full Kconfig menu tree with explicit `menu`,
  `choice`, `comment`, and `source` nodes. Category grouping approximates this
  for Confit but is not the same structure.
- Search lacks a rich symbol-result browser with jump history and all symbol
  metadata.
- Help/detail is practical but less polished for long text wrapping, symbol
  references, and dependency expression rendering.
- Reverse dependency exploration and fix suggestions are still shallow.
- Schema editor is guarded and useful, but real project schema changes still
  need review outside the TUI.
- Native Windows terminal/curses behavior is intentionally out of scope for this
  release-candidate phase.
