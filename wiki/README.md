# Confit 한국어 Wiki

이 wiki는 Confit을 처음 쓰는 사람과 AI 자동화 에이전트가 같은 기준으로 도구를 이해하고 사용할 수
있도록 만든 실전 설명서다. kconfig를 써 본 적이 없어도 따라갈 수 있게, 개념 설명과 실제 명령 예시를
함께 둔다.

Confit의 핵심 역할은 다음과 같다.

- 사람이 관리하는 TOML 설정 원본을 읽는다.
- option schema, profile, target, dependency graph를 검증한다.
- 선택한 profile을 deterministic하게 resolve한다.
- `config.h`, report, graph, input manifest, `config.cmake`,
  `config/config.qsm`, project-specific build selection module을 생성한다.
  기존 `config.qst`는 compatibility artifact다.
- Parus와 Delos처럼 서로 맞아야 하는 두 프로젝트의 compatibility를 검사한다.
- macOS/Linux에서는 ncurses TUI로 profile 편집과 guarded schema 편집을 제공한다.

## v0.1.0-rc1 지원 범위

Confit `0.1.0-rc1`은 fixture-backed release candidate다. Confit 자체의 CLI,
TUI, generated artifact, compatibility check, CMake/QStar manifest 생성은
Confit repository CI와 realish fixture로 검증한다. 실제 Parus/Delos source
tree adoption과 build graph wiring은 별도 integration review에서 진행한다.

지원 범위:

- macOS/Linux: CLI + ncurses TUI.
- Windows: MSYS2 `CLANG64` / GNU-style clang 기반 CLI-only preview.
- Windows TUI: unsupported. `confit tui`는 exit code `8`로 실패해야 정상이다.
- generated artifact: `config.h`, reports, `config.cmake`,
  `config/config.qsm`, compatibility `config.qst`, build selection `.qsm`.
- CI matrix: `ubuntu-latest`, `macos-latest`, `windows-latest`.

정본 판정은 [../docs/final-release-note.md](../docs/final-release-note.md)를
따른다.

## 읽는 순서

처음이라면 아래 순서대로 읽는다.

1. [00-overview.md](00-overview.md): Confit의 목적과 기본 개념.
2. [01-install.md](01-install.md): build, local install, `man confit` 확인.
3. [02-cli-quickstart.md](02-cli-quickstart.md): 바로 실행할 수 있는 CLI 빠른 실습.
4. [03-project-layout.md](03-project-layout.md): Confit project directory 구조.
5. [04-schema-guide.md](04-schema-guide.md): option schema 작성법.
6. [05-profile-target-guide.md](05-profile-target-guide.md): profile, target, override 이해.
7. [06-generation-guide.md](06-generation-guide.md): generated artifact와 `confit gen`.
8. [07-compat-guide.md](07-compat-guide.md): Parus/Delos compatibility check.
9. [08-tui-guide.md](08-tui-guide.md): TUI 진입, 얕은 menu tree 탐색, `:` command mode, 저장, schema edit.
10. [09-ai-usage-guide.md](09-ai-usage-guide.md): AI가 Confit을 다룰 때 지켜야 할 규칙.
11. [10-troubleshooting.md](10-troubleshooting.md): 오류, exit code, 흔한 문제 해결.
12. [11-parus-delos-adoption.md](11-parus-delos-adoption.md): 실제 Parus/Delos 적용 순서.

## 빠른 명령 모음

설치 전 build tree binary를 직접 쓸 수 있다.

```sh
tools/confit/tests/run_tests.sh
${TMPDIR:-/tmp}/confit-build/confit doctor
${TMPDIR:-/tmp}/confit-build/confit help
```

로컬 설치 후에는 `confit` 명령을 직접 쓴다.

```sh
tools/confit/scripts/install-local.sh --prefix ~/.local
confit doctor
man confit
```

fixture project를 검사한다.

```sh
confit check \
  --project tools/confit/tests/fixtures/realish/delos \
  --profile sim-dsh
```

generated artifact를 만든다.

```sh
confit gen \
  --project tools/confit/tests/fixtures/realish/delos \
  --profile sim-dsh \
  --out /tmp/confit-generated/delos/sim-dsh \
  --artifact all
```

TUI를 연다.

```sh
confit tui \
  --project tools/confit/tests/fixtures/tui/profile-editor \
  --profile edit
```

## 문서의 권위

- `docs/cli-contract.md`: CLI 계약의 정본.
- `docs/toml-schema.md`: TOML schema의 정본.
- `docs/build-selection-workflow.md`: QStar/CMake build selection 연결의 정본.
- `docs/qstar-build-manifest-contract.md`: QStar build manifest 계약의 정본.
- `docs/final-release-note.md`: 현재 release-candidate 판정.
- `wiki/`: 사용자가 따라 하기 위한 설명과 예시.
- `man/confit.1`: 터미널에서 빠르게 보는 압축 reference.
