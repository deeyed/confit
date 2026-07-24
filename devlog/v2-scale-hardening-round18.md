# Schema V2 Scale Hardening Round 18 Notes

Date: 2026-07-24

This is a maintainer note, not a schema or CLI contract.

## Delivered Boundaries

- V2 import traversal stops at 128 documents and reports a schema diagnostic
  without returning a partial project.
- Project identifier lookup, linked-expression lookup, loader arrays, and
  compiled graph edges use bounded lookup or geometric capacity growth.
- Bounded TOML parser corpus fuzzing is available through the `fuzz` CTest
  label alongside the existing expression fuzz coverage.
- `CONFIT_ENABLE_SANITIZERS=ON` enables ASan and UBSan on supported
  GNU-style Clang/GCC macOS/Linux hosts. The Linux CI lane runs leak detection.

## Verified Locally

- Normal Debug CTest: all 46 tests passed.
- The V2 large regression resolves 10,000 synthetic options. Menu lookup and
  incremental reconciliation regressions exercise the new lookup paths.
- macOS ASan/UBSan passed with `detect_leaks=0`; the platform runtime does not
  provide a usable LeakSanitizer implementation. Linux CI keeps
  `detect_leaks=1`.

## Deliberate Limits

- The 20,000-option and 100,000-edge release workload remains a dedicated
  high-memory release QA gate. It is not represented as a default CTest claim.
- Sanitizer runs exclude memory-heavy scale tests and C integration tests that
  intentionally assert child process exit behavior. The normal platform lanes
  continue to run those tests.
- Windows remains GNU-style clang CLI-only. Windows TUI and MSVC/clang-cl are
  outside this round.
