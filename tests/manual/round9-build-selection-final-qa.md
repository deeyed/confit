# Round 9 Build Selection Final QA

Date: 2026-06-24
Host: macOS Darwin, AppleClang 21.0.0.21000101
QStar: `qstar 0.7.19-beta`

이 기록은 build selection workflow 문서화 라운드의 manual QA transcript다.
QStar/CMake wiring 자체를 Delos root graph에 적용하지는 않았고, Confit fixture와
generated artifact 계약만 검증했다.

## Documentation Scope

Updated Korean documentation:

- `docs/build-selection-workflow.md`
- `docs/qstar-build-manifest-contract.md`
- `docs/generators.md`
- `wiki/06-generation-guide.md`
- `wiki/11-parus-delos-adoption.md`
- `wiki/README.md`
- `man/confit.1`
- `README.md`

New canonical user flow:

```sh
confit gen \
  --project fixtures/delos \
  --profile release \
  --target renode-nucleo-h753zi \
  --out build/generated/config/delos/release \
  --artifact all
```

QStar import flow:

```lua
local config = qstar.import_module("build/generated/config/delos/release/config")
local selection = qstar.import_module(
  "build/generated/config/delos/release/delos_build_selection"
)
```

CMake include flow:

```cmake
include("${CMAKE_BINARY_DIR}/generated/config/delos/release/config.cmake")
message(STATUS "board=${DELOS_CONFIG_TARGET_BOARD}")
```

## Local Full Test

Command:

```sh
./tests/run_tests.sh /tmp/confit-round9-build
```

Result:

```text
100% tests passed, 0 tests failed out of 27
```

The suite included:

- unit tests
- CLI workflow tests
- realish Delos/Parus fixture tests
- QStar module artifact CTest
- TUI smoke/scripted tests
- synthetic stress test

## Build Selection Generation Smoke

Command:

```sh
/tmp/confit-round9-build/confit \
  gen \
  --project tests/fixtures/realish/delos \
  --profile release \
  --target renode-nucleo-h753zi \
  --out /tmp/confit-round9-generated/build/generated/config/delos/release \
  --artifact all
```

Result:

```text
gen ok: /tmp/confit-round9-generated/build/generated/config/delos/release
```

Generated files:

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

Resolved value spot checks:

```text
DELOS_CONFIG_TARGET_ARCH="armv7m"
DELOS_CONFIG_TARGET_BOARD="nucleo-h753zi"
DELOS_CONFIG_TARGET_CPU="cortex-m7"
DELOS_CONFIG_TARGET_CLAIM_LEVEL="renode-probe"
config.values["delos.target.board"].value="nucleo-h753zi"
```

## QStar Smoke

Command:

```sh
CONFIT_QSTAR_BIN=/Users/gungye/.local/bin/qstar \
  sh tests/integration/round8_qstar_module_artifacts.sh \
  /tmp/confit-round9-build/confit \
  /Users/gungye/workspace/delos/tools/confit \
  /tmp/confit-round9-qstar-smoke
```

Result: success.

This smoke verified the checked-in QStar module fixture and generated realish
Delos modules using QStar import semantics:

```lua
qstar.import_module(".../config")
qstar.import_module(".../delos_build_selection")
```

## Local Install Refresh

Command:

```sh
./scripts/install-local.sh --prefix "$HOME/.local" --build-dir /tmp/confit-round9-install-build
```

Result:

```text
installed /Users/gungye/.local/bin/confit
installed /Users/gungye/.local/share/man/man1/confit.1
```

Installed binary checks:

```sh
~/.local/bin/confit --version
~/.local/bin/confit doctor --project tests/fixtures/realish/delos
```

Observed:

```text
confit 0.1.0-round1
generators: header, reports, cmake, qstar, build-selection
doctor ok
```

Installed manpage smoke:

```sh
MANPATH="$HOME/.local/share/man" MANPAGER=cat man confit | col -b |
  rg -n "Delos build selection|qstar.import_module|DELOS_CONFIG_TARGET_BOARD|build-selection-workflow"
```

Observed matches:

```text
Delos build selection
local config = qstar.import_module("build/generated/config/delos/release/config")
message(STATUS "board=${DELOS_CONFIG_TARGET_BOARD}")
tools/confit/docs/build-selection-workflow.md
```

Installed binary QStar smoke:

```sh
CONFIT_QSTAR_BIN=/Users/gungye/.local/bin/qstar \
  sh tests/integration/round8_qstar_module_artifacts.sh \
  "$HOME/.local/bin/confit" \
  /Users/gungye/workspace/delos/tools/confit \
  /tmp/confit-round9-qstar-smoke-installed
```

Result: success.

## GitHub Actions

This round updates documentation and installed local artifacts. The Confit
repository GitHub Actions Linux/macOS CI is expected to run after the round
commit is pushed to `origin/main`. The final round report should name the actual
workflow result observed after push.
