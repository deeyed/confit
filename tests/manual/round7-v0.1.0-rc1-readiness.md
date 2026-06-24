# Round 7 v0.1.0-rc1 Readiness Note

Date: 2026-06-25

Scope: Confit `0.1.0-rc1` readiness polish.

## Supported Scope

This round treats Confit as a fixture-backed release candidate.

Supported:

- macOS/Linux CLI.
- macOS/Linux ncurses TUI.
- Windows MSYS2 `CLANG64` / GNU-style clang CLI-only preview.
- Schema/profile/target validation.
- Deterministic resolution.
- Dependency graph validation.
- Explanation, diff, graph, list, compat workflows.
- `config.h`, reports, `config.cmake`, QStar `config/config.qsm`,
  compatibility `config.qst`, and build selection `.qsm` generation.
- Local single-binary install contract.

Unsupported or intentionally deferred:

- Windows TUI.
- MSVC and `clang-cl`.
- Automatic Parus/Delos source tree migration.
- Automatic CMake/QStar graph rewriting.
- Full Kconfig language parity.

## Local Install Refresh

Command:

```sh
scripts/install-local.sh \
  --prefix /Users/gungye/.local \
  --build-dir /tmp/confit-rc1-install-build
```

Result:

```text
installed /Users/gungye/.local/bin/confit
installed /Users/gungye/.local/share/man/man1/confit.1
```

Installed binary smoke:

```sh
/Users/gungye/.local/bin/confit --version
/Users/gungye/.local/bin/confit doctor
```

Observed:

```text
confit 0.1.0-rc1
platform lane: macos-cli-tui
curses: available; TUI enabled
tui: enabled
install rule: single executable artifact: <prefix>/bin/confit
generators enabled: header, reports, cmake, qstar, build-selection
doctor ok
```

Installed manpage smoke:

```sh
MANPATH=/Users/gungye/.local/share/man man confit | col -b | \
  rg -n "RC1|0.1.0-rc1|Windows|gen --artifact all|confit.exe"
```

Observed rc1 support text, Windows preview text, `gen --artifact all`, and
`confit.exe` install text in the installed manpage.

## Realish Fixture Smoke

Commands:

```sh
/Users/gungye/.local/bin/confit check \
  --project tests/fixtures/realish/delos \
  --profile sim-dsh \
  --strict

/Users/gungye/.local/bin/confit check \
  --project tests/fixtures/realish/parus \
  --profile qemu-aarch64 \
  --strict

/Users/gungye/.local/bin/confit compat \
  --parus tests/fixtures/realish/parus \
  --delos tests/fixtures/realish/delos \
  --profile parus-delos-debug \
  --compat tests/fixtures/realish/compat
```

Observed:

```text
check ok
check ok
compat ok
```

Build selection generation smoke:

```sh
/Users/gungye/.local/bin/confit gen \
  --project tests/fixtures/realish/delos \
  --profile release \
  --target renode-nucleo-h753zi \
  --out /tmp/confit-rc1-durable/delos-release \
  --artifact all \
  --force
```

Observed:

```text
gen ok: /tmp/confit-rc1-durable/delos-release
```

Generated artifact existence checked:

```text
config.h
config.report.json
config.explain.txt
config.graph.json
config.inputs.json
config.cmake
config/config.qsm
config.qst
delos_build_selection/delos_build_selection.qsm
```

Content spot checks:

```text
config/config.qsm: confit-config-manifest-v1
config.cmake: DELOS_CONFIG_TARGET_BOARD "nucleo-h753zi"
delos_build_selection/delos_build_selection.qsm: delos-build-selection-v1
```

## TUI Readiness

No new interactive terminal transcript was recorded in this round. TUI behavior
is covered by scripted tests in the full local gate, and prior manual
transcripts remain in:

```text
tests/manual/round9-tui-readability-manual-qa.md
tests/manual/round9-tui-menu-workflow-manual-qa.md
tests/manual/round4-tui-command-workflow-manual-qa.md
```

rc1 TUI support statement:

- macOS/Linux: CLI + ncurses TUI supported.
- Windows: CLI-only preview; `confit tui` must fail with exit code `8`.

## CI Gate

The `Confit CI` workflow is expected to run on push and pull request:

```text
ubuntu-latest
macos-latest
windows-latest / MSYS2 CLANG64
```

The Windows job is expected to verify:

- CMake forces `CONFIT_ENABLE_TUI=OFF`.
- CTest runs the C integration workflow.
- `doctor`, `check`, and `gen --artifact all` work.
- generated artifacts exist.
- `confit tui` fails with exit code `8`.
- `install-windows-ci/bin/confit.exe` exists after CMake install.

## Final Readiness Position

Confit `0.1.0-rc1` is ready as a configuration authority candidate for
Parus/Delos integration planning. It should not be treated as an automatic
source migration or build graph rewrite tool.
