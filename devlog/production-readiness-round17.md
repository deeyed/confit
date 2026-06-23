# Production Readiness Round 17 Notes

Date: 2026-06-24

This note records current readiness observations. It is not a CLI contract.

## Current Readiness

Confit is locally installable as a single host executable and covers the main
non-interactive authority path:

```text
doctor
init
check
resolve
gen
explain
list
graph
diff
compat
profile
completion
```

macOS/Linux hosts with curses can also run the ncurses-backed `tui` command.
Windows remains CLI-only in this phase.

## Round 17 Fixups

- `confit diff` was implemented because it was already part of the CLI contract
  and completion surface.
- The synthetic stress gate now defaults to 5,000 options.
- Generated stress options include metadata to avoid warning noise hiding scale
  failures.
- README, local build/test notes, and release-candidate notes were updated to
  describe the current generated artifact set and safety rules.

## Remaining Release Risk

- Native Windows execution still needs a real Windows host or CI lane.
- Linux execution should be run on a Linux host before claiming complete
  multi-platform validation.
- TUI behavior is useful and tested, but still not fully equivalent to mature
  kconfiglib menuconfig.
- Real Parus/Delos source adoption remains a separate integration phase.
