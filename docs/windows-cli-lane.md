---
doc_type: platform-contract
status: draft
authority: operational
last_verified: 2026-06-25
---

# Windows CLI-Only Lane

Confit의 Windows 지원은 `0.1.0-rc1` 이후부터 CLI-only preview로 시작한다.
이 문서는 Windows host에서 기대하는 build lane과 금지 사항을 고정한다.

## 지원 범위

지원하는 기능:

- `confit --version`
- `confit doctor`
- `confit init`
- `confit check`
- `confit resolve`
- `confit gen`
- `confit explain`
- `confit list`
- `confit graph`
- `confit diff`
- `confit compat`
- `confit profile`
- `confit completion`

지원하지 않는 기능:

- `confit tui`
- ncurses 또는 PDCurses 기반 TUI
- MSVC
- `clang-cl`
- MinGW GCC

Windows에서 `confit tui`를 실행하면 partial UI를 시도하지 않고 exit code `8`
(`unsupported command or platform`)로 실패해야 한다.

## Compiler Lane

Windows preview lane은 GNU-style clang만 지원한다. GitHub Actions 초안은
MSYS2 `CLANG64` 환경을 기준으로 둔다.

```sh
cmake -S . -B build/confit-windows -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_BUILD_TYPE=Release \
  -DCONFIT_ENABLE_TUI=ON
cmake --build build/confit-windows
ctest --test-dir build/confit-windows --output-on-failure
```

Windows에서는 `CONFIT_ENABLE_TUI=ON`을 전달해도 CMake가 CLI-only lane을
보호하기 위해 `OFF`로 강제한다.

```text
CONFIT_ENABLE_TUI:BOOL=OFF
```

## Doctor Contract

Windows build의 `confit doctor`는 다음을 보여야 한다.

```text
version: confit 0.1.0-rc1
platform: Windows
platform lane: windows-cli-only
platform note: windows clang-only CLI lane; TUI unsupported
curses: not available; TUI unsupported
tui: unsupported
generators enabled: header, reports, cmake, qstar, build-selection
doctor ok
```

macOS/Linux에서 `-DCONFIT_ENABLE_TUI=OFF`로 빌드한 no-TUI smoke도 같은
unsupported TUI path를 검증한다. 이 smoke는 Windows native build가 없어도
`src/tui/tui_unsupported.c`와 CLI exit-code 계약을 계속 보호하기 위한 것이다.

## CI Status

기본 push CI는 macOS/Linux를 계속 release gate로 둔다. Windows job은
`Confit Windows CLI Preview` workflow로 분리되어 있으며, 현재는
`workflow_dispatch` 수동 실행 초안이다.

Windows preview가 안정화되면 별도 라운드에서 push/pull-request matrix에
편입한다.
