# Windows CLI Support Plan

Date: 2026-06-24

This note records the working plan for Windows support during the next Confit
implementation push. It is intentionally a devlog entry, not a normative CLI
contract.

## Decision

Windows support during the 18-round implementation push is CLI-only.

Supported target:

```text
Windows native host
Clang-family compiler
Ninja or equivalent non-Visual-Studio build driver
single installed executable: confit.exe
```

Out of scope for the 18-round push:

```text
Windows TUI
PDCurses integration
MSVC compiler support
Visual Studio solution workflow
external runtime DLL bundle
PowerShell-only test or install requirement
```

The Windows TUI should be explicitly unsupported rather than partially enabled.
`confit tui ...` on Windows should fail with a clear unsupported-platform
diagnostic until a dedicated Windows TUI effort starts.

## Compiler Policy

The preferred Windows route is LLVM/MinGW-style Clang, not MSVC.

Rationale:

- It keeps the compiler and warning model close to macOS/Linux Clang.
- It avoids CMake `MSVC` semantics and MSVC-specific flags.
- It matches the single-binary host-tool direction better than a Visual Studio
  oriented workflow.

`clang-cl` is deferred because it commonly enters MSVC ABI and CMake behavior
even though the compiler front-end is Clang.

## Current Code Observations

Current state that must be cleaned up during implementation:

- `tools/confit/CMakeLists.txt` still contains repeated `if(MSVC)` warning
  branches.
- TUI currently links through CMake `find_package(Curses REQUIRED)`.
- Scripted integration tests are POSIX shell based.
- The host boundary is already structurally useful: core/model/schema/graph/
  resolver/generator code should continue to avoid direct platform APIs.

## 18-Round Windows Goal

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

Expected Windows TUI behavior during this phase:

```text
confit.exe tui ...
=> unsupported command or platform
```

## C Test Runner Direction

Windows integration tests should be written in C rather than shell or Python.

Proposed layout:

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

1. Create a temporary project directory.
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

## Post-18-Round Windows TUI Work

Windows TUI support should be a separate effort after the CLI authority path is
usable. That effort should decide whether to vendor PDCurses, how to static link
it, how to test real terminal behavior, and how to keep curses calls isolated in
the TUI frontend.
