# Round 17 Production Readiness Manual Notes

Date: 2026-06-24

Host: macOS local checkout under `/Users/gungye/workspace/delos`.

## Commands Checked

```sh
/var/folders/1g/wddr4j993gzd7y87094d7k340000gn/T/confit-build/confit \
  diff --project tools/confit/tests/fixtures/schema/valid/basic \
  --profile sim-dsh \
  --base debug
```

Observed:

```text
diff: debug -> sim-dsh
base target: <none>
profile target: host-sim
...
changes: 4
```

```sh
/var/folders/1g/wddr4j993gzd7y87094d7k340000gn/T/confit-build/confit \
  diff --project tools/confit/tests/fixtures/schema/valid/basic \
  --profile sim-dsh \
  --base debug \
  --format json
```

Observed:

```text
"schema": "confit-diff-v1"
"summary": {"changed": 4}
```

```sh
/var/folders/1g/wddr4j993gzd7y87094d7k340000gn/T/confit-build/confit help diff
```

Observed: `diff` now shows command help and no longer reports an
unimplemented-command status.

## Stress Check

The focused 5,000-option stress gate was run during the round:

```sh
CONFIT_STRESS_OPTION_COUNT=5000 \
  /bin/sh tools/confit/tests/integration/round20_stress.sh \
  /var/folders/1g/wddr4j993gzd7y87094d7k340000gn/T/confit-build/confit \
  tools/confit \
  /tmp/confit-round17-stress-5000-clean
```

Observed:

```text
stress ok: 5000 options
```

The generated stress options now include metadata, so the stress output is not
dominated by schema metadata warnings.

## TUI Status

No new full interactive TUI pass was required for the `diff` implementation.
The current TUI evidence remains the existing manual transcripts and scripted
TUI tests under `tools/confit/tests/manual/` and the CTest suite. Release notes
continue to describe the TUI as menuconfig-style and useful, but not fully
kconfiglib-equivalent.
