---
doc_type: release-note
status: candidate
authority: operational
last_verified: 2026-06-24
---

# Confit Final Release Note

이 문서는 Confit 18라운드 종료 시점의 실전 투입 후보 판정이다. Confit은 아직 실제 Parus/Delos
source tree에 적용된 상태가 아니라, `tools/confit/tests/fixtures/realish/` mirror와 dry-run workflow를
통해 검증된 host-side configuration tool이다.

## Verdict

Confit은 Parus/Delos가 kconfig를 도입하지 않고 자체 TOML 기반 configuration authority를 쓰기 위한
실전 투입 후보 수준에 도달했다.

투입 가능한 범위:

- 프로젝트 schema/profile/target TOML validation.
- deterministic profile resolution.
- dependency graph validation and graph output.
- profile diff.
- option explanation.
- compatibility checks across Parus/Delos-style project roots.
- generated `config.h`, reports, `config.cmake`, and `config.qst` under explicit `--out`.
- profile TOML management through CLI and ncurses TUI on macOS/Linux hosts.
- guarded schema-edit TUI for reviewed schema changes.

아직 별도 integration round가 필요한 범위:

- 실제 Parus/Delos `config/` source tree adoption.
- 실제 Delos CMake/QStar build graph wiring.
- native Windows machine or CI validation.
- Linux host validation outside this macOS checkout.
- kconfiglib와 동등한 full menu tree/search/help maturity.

## Supported Command Surface

The release-candidate CLI surface is:

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

Windows remains CLI-only in this candidate. `confit tui` is intentionally
unsupported there until a dedicated Windows TUI effort starts.

## Final Verification Commands

The final gate is:

```sh
tools/confit/tests/run_tests.sh
git diff --check -- tools/confit
```

Realish Delos mirror:

```sh
confit check --project tools/confit/tests/fixtures/realish/delos --profile sim-dsh
confit resolve --project tools/confit/tests/fixtures/realish/delos --profile sim-dsh --format json
confit diff --project tools/confit/tests/fixtures/realish/delos --profile sim-dsh --base debug
confit gen --project tools/confit/tests/fixtures/realish/delos --profile sim-dsh --out /tmp/confit-final-delos --artifact all
```

Realish Parus mirror:

```sh
confit check --project tools/confit/tests/fixtures/realish/parus --profile qemu-aarch64
confit resolve --project tools/confit/tests/fixtures/realish/parus --profile qemu-aarch64 --format json
confit diff --project tools/confit/tests/fixtures/realish/parus --profile qemu-aarch64 --base bringup
confit gen --project tools/confit/tests/fixtures/realish/parus --profile qemu-aarch64 --out /tmp/confit-final-parus --artifact all
```

Cross-project compatibility:

```sh
confit compat \
  --parus tools/confit/tests/fixtures/realish/parus \
  --delos tools/confit/tests/fixtures/realish/delos \
  --profile parus-delos-debug \
  --compat tools/confit/tests/fixtures/realish/compat
```

Generated artifact checks:

```text
config.h
config.report.json
config.explain.txt
config.graph.json
config.inputs.json
config.cmake
config.qst
```

## Safety Rules

Confit remains a host-side tool:

- No TOML parser or TUI is linked into Parus/Delos runtime images.
- No hidden binary profile database is used.
- No network access is required for normal build, validation, generation, or install.
- Generated files are written only under explicit output paths.
- Real Parus/Delos source adoption must happen in a separate reviewed integration round.

## TUI Status

macOS/Linux TUI status:

- ncurses-backed menuconfig-style profile editor works for browsing, search,
  help/detail, bool toggle, enum selection, typed value editing, save, and dirty
  quit confirmation.
- guarded schema editor works for creating/editing option metadata and saving
  human-readable TOML after validation.
- scripted CTest coverage and prior manual transcripts are checked in under
  `tools/confit/tests/manual/`.

Remaining TUI gaps:

- full Kconfig menu/choice/comment/source tree semantics are not implemented.
- search and help are useful but not as mature as kconfiglib menuconfig.
- Windows TUI is intentionally unsupported.

## Production Adoption Recommendation

Adopt Confit in two separate follow-up phases:

1. Add real Parus/Delos `config/` trees by copying from reviewed fixture mirrors
   and running `confit check`, `confit compat`, and `confit gen` in CI.
2. Wire generated artifacts into Delos/Parus build systems only after generated
   outputs and rollback notes are reviewed.

Do not skip the first phase. Confit is ready as a configuration authority
candidate, but source-tree adoption and build graph wiring should remain
separate reviewed changes.
