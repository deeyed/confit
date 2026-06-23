# Windows CLI Support Notes

Date: 2026-06-24

This note records current implementation status and remaining Windows work. It
is intentionally a devlog entry, not a normative CLI contract.

## Decision

Windows support during the current Confit hardening push is CLI-only.

Supported target:

```text
Windows native host
GNU-style Clang compiler
Ninja or equivalent non-Visual-Studio build driver
single installed executable: confit.exe
```

Out of scope for this push:

```text
Windows TUI
PDCurses integration
MSVC compiler support
clang-cl / MSVC frontend mode
Visual Studio solution workflow
external runtime DLL bundle
PowerShell-only test or install requirement
```

The Windows TUI is explicitly unsupported rather than partially enabled.
`confit tui ...` on Windows should fail with a clear unsupported-platform
diagnostic and exit code `8` until a dedicated Windows TUI effort starts.

## Current Implementation State

Current code already has the main Windows boundary work in place:

- `tools/confit/CMakeLists.txt` rejects MSVC and MSVC-frontend Clang.
- `tools/confit/CMakeLists.txt` skips curses discovery on `WIN32`.
- Windows builds use `src/tui/tui_unsupported.c` instead of ncurses-backed TUI
  sources.
- `confit doctor` reports a Windows clang-only CLI lane when compiled for
  Windows.
- `confit tui` on Windows is covered by the C integration runner as an
  unsupported-platform command.
- The C integration runner exercises CLI workflows without requiring POSIX shell
  scripts.

Remaining evidence gap:

```text
Native Windows machine or CI execution has not yet been completed in this repo.
The lane is implemented and testable, but not fully validated on a Windows host.
```

## Round 18 Final Status

As of the final Confit 18-round candidate, Windows is not claimed as a fully
validated platform. The codebase has a deliberate Windows CLI-only path, rejects
MSVC/clang-cl style compiler lanes, and keeps TUI disabled through the
unsupported frontend.

What is ready for a Windows host validation round:

```text
configure with CMake + Ninja + GNU-style clang
build confit.exe
run C integration runner
run doctor/check/resolve/gen/explain/list/graph/diff/compat/profile/completion
verify tui exits with unsupported command/platform
```

What remains open:

```text
native Windows CI or machine run
Windows local install smoke
Windows path edge cases outside the current C runner
any PDCurses or real Windows TUI design
```

## Compiler Policy

The preferred Windows route is LLVM/MinGW-style Clang, not MSVC.

Rationale:

- It keeps the compiler and warning model close to macOS/Linux Clang.
- It avoids CMake `MSVC` semantics and MSVC-specific flags.
- It matches the single-binary host-tool direction better than a Visual Studio
  oriented workflow.

`clang-cl` remains deferred because it commonly enters MSVC ABI and CMake
behavior even though the compiler front-end is Clang.

## CLI Goal

Windows should be able to run non-interactive Confit workflows:

```text
confit.exe help
confit.exe --version
confit.exe doctor
confit.exe check
confit.exe resolve
confit.exe gen
confit.exe explain
confit.exe list
confit.exe graph
confit.exe diff
confit.exe compat
confit.exe profile
confit.exe completion
```

Expected Windows TUI behavior:

```text
confit.exe tui ...
=> unsupported command or platform
```

## C Test Runner Direction

Windows integration tests should continue to be written in C rather than shell
or Python.

Current layout:

```text
tools/confit/tests/integration_c/
  test_cli_workflow.c

tools/confit/tests/support/
  test_assert.c
  test_assert.h
  test_fs.c
  test_fs.h
  test_process.c
  test_process.h
```

The C runner should:

1. Create temporary project directories.
2. Materialize minimal TOML fixtures or copy checked-in fixtures.
3. Invoke `confit.exe` as a child process.
4. Capture exit code, stdout, and stderr.
5. Verify golden substrings and generated files.
6. Avoid requiring Python, POSIX shell, or network access.

Platform-specific process and filesystem details belong inside test support
code, not inside Confit core.

## Install Direction

Windows local install should mirror the single-binary install contract:

```powershell
cmake -S tools/confit -B build/confit -G Ninja `
  -DCMAKE_C_COMPILER=clang `
  -DCMAKE_BUILD_TYPE=Release

cmake --build build/confit --target confit
cmake --install build/confit --prefix "$env:USERPROFILE\.local"
```

Expected artifact:

```text
%USERPROFILE%\.local\bin\confit.exe
```

No project configuration should be edited by install.

## Post-CLI Windows TUI Work

Windows TUI support should be a separate effort after the CLI authority path is
usable on a real Windows host. That effort should decide whether to vendor
PDCurses, how to static link it, how to test real terminal behavior, and how to
keep curses calls isolated in the TUI frontend.
