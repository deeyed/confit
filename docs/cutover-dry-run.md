---
doc_type: operations-guide
status: accepted
authority: operational
---

# Confit Cutover Dry-Run

Confit cutover dry-run은 Parus/Delos가 실제 빌드 흐름에 Confit generated
artifacts를 연결하기 전에 수행하는 rehearsal 절차다. 이 절차는 fixture mirror를
사용하며 실제 Parus/Delos source tree, runtime source, CMake, QStar file을
수정하지 않는다.

## Command

```sh
tools/confit/scripts/confit-cutover-dry-run.sh \
  --project delos-realish \
  --out /tmp/confit-cutover

tools/confit/scripts/confit-cutover-dry-run.sh \
  --project parus-realish \
  --out /tmp/confit-cutover
```

Both fixture mirrors can be checked in one invocation:

```sh
tools/confit/scripts/confit-cutover-dry-run.sh \
  --project both \
  --out /tmp/confit-cutover
```

If `--confit-bin <path>` is omitted, the script builds a local `confit` binary in
a temporary build directory outside the source tree.

## Output Layout

For `--project delos-realish`, the script writes:

```text
<out>/delos-realish/
  .confit-cutover-dry-run
  check.txt
  check-strict.txt
  compat.json
  gen.txt
  artifact-diff.txt
  CUTOVER_SUMMARY.txt
  ROLLBACK.md
  diff/
  generated/
    config.h
    config.cmake
    config.qst
    config.report.json
    config.explain.txt
    config.graph.json
    config.inputs.json
```

`parus-realish` uses the same layout under `<out>/parus-realish/`.

## Gates

The dry-run performs the same high-level cutover recipe that a CI job should use
before generated artifacts are wired into a build tree:

```text
check
strict check
compat
gen
artifact set verify
golden diff
input manifest verify
rollback note verify
```

The script fails if any generated artifact is missing or differs from the
checked-in realish golden output. Diff files are written under `diff/` when a
comparison fails.

## Safety Rules

`--out` must be an absolute path. The script refuses to write into the Confit
source tree or filesystem root.

The script may replace a previous output directory only when that directory
contains `.confit-cutover-dry-run`. This marker prevents accidental overwrite of
unrelated files.

The script does not copy generated artifacts into a real build tree. Applying
artifacts to a real Parus/Delos build remains a separate, explicit integration
step.
