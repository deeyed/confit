# Confit v6 POSIX terminal frontend

`confit menuconfig` connects the terminal-independent UI model to one
ANSI-capable POSIX TTY. This frontend is a configuration value editor. It does
not inspect source code, edit schema definitions, discover files, execute a
build, or invoke another process.

## Terminal contract

- Both standard input and standard output must be TTYs. A pipe or redirected
  stream is rejected before raw mode.
- The frontend queries `TIOCGWINSZ` before changing terminal state. A terminal
  smaller than 40 columns by 10 rows is rejected before raw mode.
- Reported dimensions are clamped to the public 512 by 256 logical surface.
- 40 through 79 columns use the compact layout. `Tab` switches its list and
  detail panes without moving the semantic cursor. HELP, DIFF, and enum picker
  states show their detail pane automatically. 80 columns and wider use a
  simultaneous list/detail split layout.
- The original `termios` state is captured before raw mode. Alternate-screen
  and hidden-cursor state are entered only for the session and restored on
  normal exit, I/O error, EOF, `SIGINT`, `SIGTERM`, and `SIGHUP`.
- `SIGWINCH` and terminating signal handlers only set `sig_atomic_t` flags.
  The poll loop performs resize, cleanup, diagnostics, and all allocation.
- `SIGKILL` restoration is intentionally not claimed.

The input decoder accepts ordinary bytes, Enter, Backspace, Tab, Ctrl-R, Escape,
and the four conventional CSI arrow sequences. It keeps at most 16 escape
bytes. Unknown or truncated CSI input is discarded and is never interpreted
as a shell command, terminal output, or arbitrary Ex command.

## Rendering and safety

The screen always includes a mode label, layout/pane label, mode-specific
footer, and diagnostic or notice line. Wide mode adds a fixed detail pane.
Compact mode retains the same selected row while switching panes. Detail shows
prompt, symbol, type, effective/default values, origin, availability, help,
range, dependency, and causal reason where applicable. Enum mode renders the
closed domain and current candidate. HELP and DIFF are bounded views rather
than external viewers.

Availability uses `-`, a changed row uses `*`, the title says `modified`, and
the detail pane repeats `UNAVAILABLE` or `MODIFIED`. These are textual
semantics, not color conventions. The renderer requires neither terminfo nor
ncurses. It redraws only after input, resize, or a pending diagnostic instead
of continuously repainting an unchanged terminal.

Prompt, help, symbol, value, and editor text pass through a bounded UTF-8
decoder before reaching the terminal. C0/C1 controls, escape bytes, malformed
UTF-8, and incomplete scalars are rendered as inert replacement text. A
bounded width policy treats combining marks as zero cells and common wide
Unicode ranges as two cells. No definition text can become terminal control.

## Save boundary

The frontend receives a callback that accepts only the current immutable
resolution. The CLI adapter converts user-origin values into a generic
assignment set and delegates publication to the existing immutable snapshot
API. The terminal layer never opens a project or output path itself. A failed
publication is reported to the UI model as a failed save, preserving the
working state and leaving the session active.

The closed Normal-mode mapping is `j`/`k` or arrows for movement, Enter/right
for open/edit, Space for bool, `e` for typed edit, `/` for search, `d` for
semantic diff, `?` for help, and `:` for command mode. Plain `q` only prints a
command hint. Escape cancels a transient mode and never exits. The command
footer exposes only `:w`, `:q`, `:q!`, `:wq`, `:x`, `:help`, and the two
unavailable-row settings. Tab completion succeeds only for a unique prefix in
that closed set; it cannot invoke shell or Ex-language behavior.

R20 PTY coverage checks wide and compact layouts, compact pane switching,
pre-raw tiny-terminal refusal, arrow and unknown CSI input, resize, every typed
editor and cancellation path, command completion and rejection, Vim-style
save/exit behavior, injected save failure, selected snapshot publication, and
restoration markers for normal exit and three terminating signals. The direct
TTY review and evidence matrix are recorded in
[`menuconfig-qa-v6.md`](menuconfig-qa-v6.md).
