# Round 18 Final Readiness Manual Notes

Date: 2026-06-24

Host: macOS local checkout under `/Users/gungye/workspace/delos`.

## Automated Gate

```sh
tools/confit/tests/run_tests.sh
```

Observed:

```text
100% tests passed, 0 tests failed out of 26
```

## Realish Mirror Commands

The following commands were run with:

```text
/var/folders/1g/wddr4j993gzd7y87094d7k340000gn/T/confit-build/confit
```

Delos:

```sh
confit check --project tools/confit/tests/fixtures/realish/delos --profile sim-dsh
confit resolve --project tools/confit/tests/fixtures/realish/delos --profile sim-dsh --format json
confit diff --project tools/confit/tests/fixtures/realish/delos --profile sim-dsh --base debug
confit gen --project tools/confit/tests/fixtures/realish/delos --profile sim-dsh --out /tmp/confit-final-delos --artifact all
```

Observed:

```text
check ok
diff changes: 7
gen ok: /tmp/confit-final-delos
```

Parus:

```sh
confit check --project tools/confit/tests/fixtures/realish/parus --profile qemu-aarch64
confit resolve --project tools/confit/tests/fixtures/realish/parus --profile qemu-aarch64 --format json
confit diff --project tools/confit/tests/fixtures/realish/parus --profile qemu-aarch64 --base bringup
confit gen --project tools/confit/tests/fixtures/realish/parus --profile qemu-aarch64 --out /tmp/confit-final-parus --artifact all
```

Observed:

```text
check ok
diff changes: 3
gen ok: /tmp/confit-final-parus
```

Compatibility:

```sh
confit compat \
  --parus tools/confit/tests/fixtures/realish/parus \
  --delos tools/confit/tests/fixtures/realish/delos \
  --profile parus-delos-debug \
  --compat tools/confit/tests/fixtures/realish/compat
```

Observed:

```text
compat ok
```

## Generated Artifact Check

Both `/tmp/confit-final-delos` and `/tmp/confit-final-parus` contain:

```text
config.h
config.report.json
config.explain.txt
config.graph.json
config.inputs.json
config.cmake
config.qst
```

Marker checks passed for project-specific `*_CONFIG_*` defines,
`CONFIT_CONFIG_HEADER`, `confit-qstar-manifest-v1`, and `confit-inputs-v1`.

## TUI Scripted Manual QA

The following ncurses-backed scripted terminal flows were rerun:

```sh
/bin/sh tools/confit/tests/integration/round17_tui_smoke.sh ...
/bin/sh tools/confit/tests/integration/round18_tui_profile_editor.sh ...
/bin/sh tools/confit/tests/integration/round19_tui_profile_create.sh ...
/bin/sh tools/confit/tests/integration/round19_tui_schema_editor.sh ...
```

Observed:

```text
all four scripts exited 0
```

Coverage:

```text
browse/search/help/detail/navigation
profile bool/enum/int/uint/hex/float/string/path edit
edit-time validation failures
save and reload
dirty quit discard confirmation
schema-edit warning
schema option create/edit/save
schema graph validation after save
```

## Final Caveat

This final readiness pass validates Confit as a fixture-backed host tool and
configuration authority candidate. It does not claim that real Parus/Delos
source trees have already adopted Confit.
