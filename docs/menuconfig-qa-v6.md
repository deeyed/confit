---
doc_type: verification-record
status: passed
authority: confit-menuconfig-r20-qa
schema_version: 6
implementation_round: R20
verified_at: 2026-08-29T10:10:55+09:00
---

# Confit schema 6 menuconfig R20 QA

## Evidence identity and limits

This record closes the R20 interaction review for the generic Confit product.
It is not evidence about any kernel, build graph, consumer source tree, btop,
or Vim implementation. btop 1.4.6 informed the dense list/detail and fixed-key
hint presentation. Vim 9.1 patches 1–1752 informed the separation of Escape,
command entry, save, clean exit, forced exit, and write-if-changed behavior.
Neither program is a Confit build or runtime dependency.

- product: `/private/tmp/confit-r20-build/confit`
- product SHA-256:
  `7ef24023f61329773c1a1ae4f9db2fd1713f07d486127e88de66a7d0cad9cf42`
- source branch: `codex/confit-v6`
- host: macOS POSIX PTY, ANSI/ECMA-48, no ncurses
- direct review sizes: 80x24 split and 60x15 compact
- automated PTY sizes: 39x10, 60x15, 100x20, 600x300 clamped, and resize
  transitions between 100x20 and 60x15
- fixture: explicit `Confit.toml`, one reachable option fragment, and one
  explicit user configuration; no directory discovery

The direct TTY session was driven through the product binary attached to a real
pseudo-terminal. The regression session used the C `forkpty` harness. Expected
and actual results were compared after each screen marker, not by submitting
one blind key batch. Paths under `/private/tmp` are ephemeral evidence inputs,
not tracked product dependencies.

## Direct terminal transcript summary

| Size | Keys | Expected | Actual |
| --- | --- | --- | --- |
| 80x24 | Enter, `j` x4, `e` | enum editor is visibly distinct | title changed to `ENUM`; quiet/normal/verbose were listed with `normal` selected |
| 80x24 | `j`, Enter | candidate applies without exiting | `verbose` became the working value; title and row showed `modified`/`*` |
| 80x24 | `:q` | dirty exit refused | diagnostic said unsaved changes and the UI returned to `NORMAL` with `verbose` intact |
| 80x24 | `:q!` | explicit discard exit | alternate screen and cursor state were restored; exit status was zero |
| 60x15 | Tab | compact list changes to detail without cursor loss | title changed from `[list]` to `[detail]`; selected Runtime help appeared |
| 60x15 | Enter while in detail | semantic open still works | Runtime opened and ENABLE_LOGGING detail replaced the menu detail |
| 60x15 | `:q` | clean explicit exit | terminal state restored with exit status zero |

The first direct run exposed continuous repainting while idle. That was treated
as an R20 defect rather than accepted as cosmetic: the poll loop now repaints
only after an input event, resize, or diagnostic. The second direct run showed
one initial frame and one frame per action. The compact footer was also shortened
so `Tab pane` and `: cmd` remain visible at 60 columns.

## Stepwise PTY interaction matrix

| Scenario | Expected invariant | Actual result |
| --- | --- | --- |
| Space on bool | change value, remain active | passed; row gained `*`, title gained `modified` |
| invalid int | diagnostic, candidate and working state preserved | passed with canonical-decimal diagnostic; Escape returned to Normal |
| invalid hex | diagnostic, candidate and working state preserved | passed with `0x`-prefix diagnostic; Escape returned to Normal |
| string edit then Escape | candidate discarded | passed; clean `:q` exited without save |
| enum move then Escape | selection candidate discarded | passed; original enum remained effective |
| search then Escape | query canceled, no exit | passed |
| repeated Escape | never exits | passed with two independently timed Escape events |
| plain `q` | never exits | passed; `use :q, :wq, or :q!` appeared |
| dirty `:q` | refuse exit and retain working state | passed; diagnostic and `modified` remained visible |
| `:w` | publish and stay active | passed; `configuration saved` appeared, then clean `:q` exited |
| `:wq` | exit only after successful save | passed; selected immutable snapshot existed |
| failed `:wq` | no exit; dirty working state remains | passed with an injected invalid selected-record destination, followed by explicit `:q!` |
| `:q!` | explicit discard exit only | passed |
| clean `:x` | exit without write | passed |
| dirty `:x` | save, then exit | passed |
| `:!` | unknown closed-set command | passed; no shell or process action occurred |
| `:he` then Tab | unique closed-set completion | passed; completed to `:help` and entered HELP |
| narrow Tab pane | switch view, preserve selected row | passed |
| split-to-compact-to-split resize | preserve model cursor/detail | passed |
| tiny 39x10 | refuse before raw mode | passed |
| SIGINT/SIGTERM/SIGHUP | restore terminal and report interruption | passed for all three |

## Visual and accessibility findings

- Wide mode simultaneously displays the option list and selected detail.
- Compact mode provides an explicit list/detail pane instead of silently
  dropping help and value context.
- Footer text changes for NORMAL, EDIT, SEARCH, COMMAND, ENUM, HELP, and DIFF.
- Availability and modification remain understandable without color: `-`,
  `*`, `UNAVAILABLE`, `MODIFIED`, and the title state are redundant markers.
- The selected detail includes type, current/default value, origin, range,
  dependency, and causal reason when meaningful.
- Enum editing is a closed list. Int, hex, and string use bounded candidate
  input. Bool never opens a text editor.
- Definition and value text still passes through the bounded UTF-8/control
  sanitizer before rendering.

## Remaining nonclaims

The review does not claim recovery from SIGKILL, universal terminal-emulator
compatibility, full Unicode grapheme shaping, mouse input, localization, or
accessibility certification. It does establish that the tested keyboard-only
flow has no blocking R20 save/exit defect, no accidental Escape/plain-q exit,
and no failed-save exit. R21 and R22 retain broader sanitizer, security,
bootstrap, integration, and adversarial release authority.
