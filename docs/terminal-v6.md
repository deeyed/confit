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
- 40 through 79 columns use the tabbed compact layout. 80 columns and wider
  use a list/detail split layout.
- The original `termios` state is captured before raw mode. Alternate-screen
  and hidden-cursor state are entered only for the session and restored on
  normal exit, I/O error, EOF, `SIGINT`, `SIGTERM`, and `SIGHUP`.
- `SIGWINCH` and terminating signal handlers only set `sig_atomic_t` flags.
  The poll loop performs resize, cleanup, diagnostics, and all allocation.
- `SIGKILL` restoration is intentionally not claimed.

The input decoder accepts ordinary bytes, Enter, Backspace, Ctrl-R, Escape,
and the four conventional CSI arrow sequences. It keeps at most 16 escape
bytes. Unknown or truncated CSI input is discarded and is never interpreted
as a shell command, terminal output, or arbitrary Ex command.

## Rendering and safety

The screen always includes a mode label, layout label, list, footer, and
diagnostic or notice line. Wide mode adds a fixed detail pane. Availability
and dirty state have textual markers, so color is not required. The R19
renderer does not require terminfo or ncurses.

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

R19 PTY coverage checks wide and compact layouts, pre-raw tiny-terminal
refusal, arrow and unknown CSI input, resize, selected snapshot publication,
and restoration markers for normal exit and three terminating signals. R20
remains responsible for final human usability review and interaction polish.
