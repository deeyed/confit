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

## macOS/Linux TUI 지원 범위

이 release-candidate에서 TUI 지원 범위는 curses/ncurses를 사용할 수 있는
macOS/Linux terminal 환경이다. TUI는 host tool일 뿐이며, Confit
profile/schema TOML을 편집한다. Parus/Delos runtime service가 되지 않는다.

macOS에서 검증한 항목:

- AppleClang과 platform curses library를 사용한 local CMake build
- 전체 `tests/run_tests.sh` gate
- `script(1)` pseudo-terminal manual QA: profile browse/search/help/edit,
  save, dirty exit, schema warning, schema edit, schema save

Linux에서 검증 또는 기대하는 항목:

- GitHub Actions에 `ubuntu-latest` job이 있다.
- Linux CI는 `build-essential`, `cmake`, `libncurses-dev`를 설치한다.
- Linux와 macOS 모두 같은 `./tests/run_tests.sh` gate를 사용한다.

아직 구현 완료로 보지 않는 항목:

- native Windows TUI 지원
- terminal emulator matrix 전수 테스트
- kconfiglib와 동등한 symbol browser 또는 reverse dependency browser
- 구현 완료된 shallow menu-stack navigation. 해당 작업의 문서화된 방향은
  `docs/cli-tui.md`에 있다.

## Manual TUI QA 근거

Interactive TUI 근거는 다음 위치에 기록한다.

```text
tools/confit/tests/manual/
```

현재 coverage는 profile browsing, search, bool toggle, typed edit, help/detail,
save, dirty exit confirmation, guarded schema-edit entry, schema field editing,
schema save validation을 포함한다. 최신 macOS pseudo-terminal manual pass는
다음 문서다.

```text
tests/manual/round9-tui-readability-manual-qa.md
```

## Kconfiglib 비교

Confit TUI는 실제 profile/schema workflow를 menuconfig-style terminal
interface에서 수행할 수 있을 만큼 가까워졌다. 다만 아직 Python kconfiglib
menuconfig와 완전히 동등하다고 표현해서는 안 된다.

초기 Confit 사용에 충분히 가까운 부분:

- title, context header, list viewport, key legend, status line을 갖춘
  ncurses 기반 full-screen profile/schema editor
- arrows, `j/k`, page movement, home/end, search, help/detail, save,
  dirty-exit confirmation을 포함한 Kconfig 계열 navigation key
- bool toggle, enum popup, typed value dialog, edit-time validation, save
  confirmation, save 후 reload를 포함한 profile editing
- 명시적 warning, field dialog, type/default/range/choice validation, 저장 전
  schema/graph validation, TOML output을 포함한 schema editing

Kconfiglib 수준 성숙도를 주장하기 전에 문서화한 다음 TUI 계약:

- category path 기반 shallow menu-stack navigation. 2단계를 권장하고 4단계
  이상은 schema design warning으로 취급한다.
- 안정적인 one-line option row. id/type/deps/tags는 선택 row 아래가 아니라
  고정 하단 inspector로 이동한다.
- `verbose`, `noverbose`, `tree`, `flat`, `filter`, `clear` 같은 view control을
  위한 `:` command mode
- `Esc`를 기본 cancel/back/root-exit flow로 사용한다. `q`는 compatibility
  alias로만 남길 수 있다.

남은 gap:

- Search는 jump history와 전체 symbol metadata를 갖춘 풍부한 symbol-result
  browser 수준은 아니다.
- Help/detail은 긴 text wrapping, symbol reference, dependency expression
  rendering에서 더 다듬을 여지가 있다.
- Reverse dependency exploration과 fix suggestion은 아직 얕다.
- Schema editor는 guarded workflow로 유용하지만, 실제 project schema 변경은
  여전히 TUI 밖 code review가 필요하다.
- Native Windows terminal/curses behavior는 이 release-candidate 단계의 범위
  밖이다.
