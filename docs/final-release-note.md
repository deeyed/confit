---
doc_type: release-note
status: v2-rc1
authority: operational
last_verified: 2026-07-24
---

# Confit v0.2.0-rc1 Release Candidate

이 문서는 Confit `0.2.0-rc1` 후보를 Delos/Parus 팀이 어떤 조건에서 믿고
사용할 수 있는지 정리한 release candidate 판정 기록이다. Confit은 host-side
configuration authority tool이며, Parus/Delos runtime image 안에 들어가는
library나 service가 아니다.

`schema_version = 1`은 동결된 기존 의미론과 artifact ABI를 유지한다.
`schema_version = 2`는 parser, typed model, expression/link, resolver,
artifact, CLI/TUI를 별도 hard cut으로 구현한다. V2 generator는 V1 artifact
shape로 fallback하지 않으며, 실제 Parus/Delos source migration은 이 문서의
검증만으로 자동 승인되지 않는다.

## 판정

Confit `0.2.0-rc1`은 **fixture-backed V2 release candidate**다. V1 baseline,
V2 typed schema validation, deterministic requested/effective resolution,
generated artifact, TUI editing, CMake/QStar manifest, compatibility check,
V1-to-V2 read-only migration rehearsal은 Confit fixture와 CI gate로 검증한다.
실제 Parus/Delos source tree adoption과 build graph wiring은 별도 integration
commit과 review로 진행해야 한다.

실전 투입 가능 범위:

- TOML project/schema/profile/target parsing and validation.
- bool, int, uint, hex, float, string, path, enum option validation.
- category path validation and shallow TUI menu path support.
- deterministic profile/target/user override resolution.
- dependency graph build and validation.
- `check`, `resolve`, `diff`, `list`, `graph`, `explain`, `compat`.
- `profile` CLI workflow for list/show/new/set/unset/validate.
- generated `config.h`, reports, `config.cmake`, QStar `config/config.qsm`,
  compatibility `config.qst`, and build selection module.
- Parus/Delos-style cross-project compatibility checks.
- macOS/Linux ncurses TUI profile editor.
- macOS/Linux guarded ncurses TUI schema editor.
- Windows GNU-style clang CLI-only preview.
- `schema_version = 2`의 explicit import, typed expression/link, menu/choice,
  requested/effective writer provenance, snapshot artifact와 compatibility ABI.
- V2 synthetic scale, parser fuzz, sanitizer, migration shadow rehearsal.

아직 별도 integration/release round가 필요한 범위:

- 실제 Parus/Delos `config/` source tree adoption.
- 실제 Delos/Parus CMake and QStar build graph wiring.
- real hardware/board build validation using generated selection artifacts.
- Windows TUI.
- MSVC and `clang-cl`.
- 실제 project의 모든 Kconfig-derived policy를 V2 schema로 옮기는 작업.
- generated artifact를 실제 CMake/QStar graph와 board build에 연결한 결과의
  hardware claim.

## Platform Support

| Platform | Status | Notes |
|---|---|---|
| macOS | Supported for CLI + TUI | Uses system curses/ncurses detected by CMake. Local V2 RC validation is on macOS. |
| Linux | Supported for CLI + TUI | Requires CMake, C17 compiler, `/bin/sh` for Unix integration tests, and curses/ncurses development files for TUI builds. |
| Windows | CLI-only preview | Requires `windows-latest` style host, MSYS2 `CLANG64`, GNU-style clang, CMake, and Ninja. TUI is unsupported and must exit with code `8`. |

Windows support is intentionally narrow. It validates the important CLI and
generated-artifact path without pretending that curses/TUI behavior is portable
yet.

## Command Surface

The `0.2.0-rc1` command surface is:

```text
confit help
confit --version
confit doctor
confit init
confit check
confit resolve
confit gen
confit explain
confit list
confit graph
confit diff
confit compat
confit profile
confit tui
confit completion
```

`confit tui` is available only when Confit is built with curses support on
macOS/Linux. On Windows and no-TUI builds, `confit tui` must fail cleanly with
exit code `8` and `unsupported command or platform`.

## Generated Artifacts

`confit gen --artifact all` writes only below the explicit `--out` directory.
The rc1 artifact set is:

```text
config.h
config.report.json
config.explain.txt
config.graph.json
config.inputs.json
config.cmake
config/
  config.qsm
config.qst
<selection-output>/
  <selection-output>.qsm
```

Artifact meaning:

- `config.h`: C compile-time defines and source hash.
- `config.report.json`: machine-readable resolved value report.
- `config.explain.txt`: human-readable explanation report.
- `config.graph.json`: deterministic dependency graph dump.
- `config.inputs.json`: input manifest for provenance/review.
- `config.cmake`: CMake include fragment with artifact paths and resolved
  values.
- `config/config.qsm`: canonical QStar importable pure table module.
- `config.qst`: deprecated compatibility artifact.
- `<selection-output>/<selection-output>.qsm`: project-specific build selection
  module generated from `selection/*.toml` templates.

## QStar/CMake Build Selection

Confit does not edit QStar or CMake graphs. It emits explicit manifests that
the build graph can import.

QStar import contract:

```lua
local config = qstar.import_module("build/generated/config/delos/release/config")
local selection = qstar.import_module(
  "build/generated/config/delos/release/delos_build_selection"
)
```

`qstar.import_module(".../config")` reads `.../config/config.qsm`; callers pass
the module folder path, not the `.qsm` file path.

CMake include contract:

```cmake
include("${CMAKE_BINARY_DIR}/generated/config/delos/release/config.cmake")
message(STATUS "board=${DELOS_CONFIG_TARGET_BOARD}")
```

Build selection templates live under `config/selection/*.toml`. They let a
project map resolved option values into a project-specific manifest without
hard-coding Delos or Parus board logic into Confit core.

## Install Contract

macOS/Linux local install:

```sh
scripts/install-local.sh --prefix ~/.local
```

Delos subtree checkout:

```sh
tools/confit/scripts/install-local.sh --prefix ~/.local
```

Installed files:

```text
<prefix>/bin/confit
<prefix>/share/man/man1/confit.1
```

Windows preview install is manual binary copy after build:

```text
<prefix>/bin/confit.exe
```

Windows docs and manpage are read from the repository checkout. No PowerShell
installer is part of rc1.

## CI Matrix

Push and pull request CI run the `Confit CI` workflow on:

- `ubuntu-latest`
- `macos-latest`
- `windows-latest` with MSYS2 `CLANG64`

macOS/Linux jobs run the full local test harness, including CLI, Unix shell
integration, TUI scripted tests, and stress coverage.

The Windows job is CLI-only and verifies:

- GNU-style clang + Ninja configure/build.
- `CONFIT_ENABLE_TUI=ON` is forced to `OFF`.
- CTest, including the C integration workflow.
- `confit doctor`.
- `confit check`.
- `confit gen --artifact all`.
- generated artifact existence.
- `confit tui` fails with exit code `8`.
- CMake install smoke produces `install-windows-ci/bin/confit.exe`.

## Verification Gate

Local rc1 gate:

```sh
./tests/run_tests.sh
git diff --check
scripts/install-local.sh --prefix ~/.local --build-dir /tmp/confit-v2-rc1-install-build
~/.local/bin/confit --version
~/.local/bin/confit doctor
MANPATH="$HOME/.local/share/man:${MANPATH:-}" man confit
```

Realish fixture smoke:

```sh
confit check --project tests/fixtures/realish/delos --profile sim-dsh --strict
confit check --project tests/fixtures/realish/parus --profile qemu-aarch64 --strict
confit compat \
  --parus tests/fixtures/realish/parus \
  --delos tests/fixtures/realish/delos \
  --profile parus-delos-debug \
  --compat tests/fixtures/realish/compat
confit gen \
  --project tests/fixtures/realish/delos \
  --profile release \
  --target renode-nucleo-h753zi \
  --out /tmp/confit-v2-rc1-durable/delos-release \
  --artifact all
```

V2 hard-cut gate는 별도 realish fixture에서 실행한다. 이 fixture는 실제 project
source가 아니라 migration review용 mirror다.

```sh
confit doctor --project tests/fixtures/realish-v2/delos
confit check --project tests/fixtures/realish-v2/delos --profile sim-dsh --strict
confit gen --project tests/fixtures/realish-v2/delos --profile sim-dsh \
  --out /tmp/confit-v2-rc1-durable/delos-v2 --artifact all
confit migrate --project tests/fixtures/realish/delos \
  --out /tmp/confit-v2-candidate
```

`doctor`는 실행 파일의 supported schema versions, V2 resolver/artifact ABI,
tomlc17 revision과 project schema dispatch 결과를 함께 출력한다. 이는 source의
의미론을 바꾸지 않는 설치 및 incident triage 정보다.

## TUI Readiness

macOS/Linux TUI requires ncurses/curses support. Current TUI capabilities:

- menuconfig-style profile browsing.
- shallow category path navigation.
- stable one-line rows with fixed inspector.
- `:` command row with `:verbose`, `:noverbose`, `:tree`, `:flat`,
  `:filter`, `:clear`, `:help`, `:quit`.
- `/` search.
- help/detail view.
- bool toggle.
- enum popup.
- int/uint/hex/float/string/path edit dialogs with validation.
- save and dirty exit confirmation.
- guarded schema-edit mode with warning and validation.

Known TUI gaps:

- It is not a full kconfiglib clone.
- Deep menu trees should still be avoided; recommended category depth is 2,
  allowed depth is 3, deeper paths are warnings and strict failures.
- Windows TUI is unsupported.

## Safety Rules

- Confit remains host-side.
- Runtime images do not link Confit parser, resolver, or TUI code.
- Installation does not create or mutate project `config/` trees.
- Generated files are written only under explicit `--out`.
- No hidden binary profile database exists.
- No network-fetched configuration is used.
- Real Parus/Delos adoption must be reviewed separately from Confit tool
  development.

## Adoption Recommendation

Use Confit in this order:

1. Keep using fixture mirrors as the reference.
2. Add real Parus/Delos `config/` source trees in separate reviewed changes.
3. Add `confit check`, `confit compat`, and `confit gen --dry-run` to project
   CI before wiring generated files into builds.
4. Wire `config.h`, `config.cmake`, `config/config.qsm`, and build selection
   modules into CMake/QStar in separate integration commits.
5. Keep rollback simple: remove generated output, remove build graph include,
   revert source `config/` changes.

Confit `0.2.0-rc1`은 configuration authority candidate로 사용할 수 있다.
다만 실제 source tree migration, build graph import, board/linker selection,
hardware validation은 명시적으로 승인된 project integration change에서만
수행한다. Confit은 silent source-tree migration mechanism이 아니다.
