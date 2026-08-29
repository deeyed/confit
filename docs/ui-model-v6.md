---
doc_type: implementation-contract
status: implemented
authority: confit-ui-model-v6
schema_version: 6
implementation_round: R18
---

# Confit schema 6 terminal-independent UI model

## 1. Boundary and durable outcome

The R18 UI model turns an immutable schema-6 catalog, dependency plan, and
initial assignment set into a bounded in-memory editing session. It is generic
configuration code. It has no terminal decoder, renderer, filesystem handle,
snapshot writer, process capability, build meaning, or consumer source
knowledge.

`include/confit/ui.h` is the public controller boundary and
`src/tui/ui_model.c` is its product implementation. The model borrows the
catalog and dependency plan and owns the current resolution, working values,
saved baseline, pending-save baseline, rows, candidate text, and history. The
borrowed objects must outlive the UI model. All borrowed row, diff, and
resolution views remain valid only until the next successful mutation or model
destruction.

R18 does not make `confit menuconfig` interactive. CLI/controller integration,
raw input decoding, POSIX terminal operation, resize, signal handling, and
visual rendering belong to R19 and R20.

## 2. Closed state machine

The complete model states are:

```text
NORMAL
EDIT
SEARCH
COMMAND
HELP
DIFF
ENUM_PICKER
```

The model accepts semantic actions, not bytes or terminal key sequences. A
frontend may map `j`, arrows, Space, `e`, `/`, `d`, `?`, `:`, `Esc`, and plain
`q` to these actions, but that mapping is outside this module.

`CANCEL` always clears transient candidate text and returns to `NORMAL`. It
never requests exit. `QUIT_HINT` only exposes the bounded notice
`use :q, :wq, or :q!`; it never requests exit. Therefore neither repeated Esc
nor plain q can terminate the session through the core model.

`OPEN` enters a child menu, toggles a bool, or enters the selected option's
typed editor. `BEGIN_EDIT` opens the enum picker for enum values and the text
candidate editor for int, hex, and string. Bool has no text editor. Invalid
actions fail without changing values.

## 3. Rows, hierarchy, cursor, and viewport

Rows show only direct children of the current menu. Child menus occur in stable
catalog order followed by direct configuration declarations in stable catalog
order. This ordering is presentation only; it cannot establish value
precedence. A root menu is not synthesized as a writable symbol.

Each row exposes a stable menu or config index, depth, prompt, help, and, for a
config, symbol, type, effective/default value, origin, and availability. Menu
parent traversal uses only the catalog's declared presentation relation.

Cursor and viewport are model state. The controller supplies a bounded number
of viewport rows; the core does not query a terminal and does not know columns,
pixels, ANSI cells, or window descriptors. Cursor clamping and viewport
adjustment preserve these invariants:

- an empty row set has cursor and offset zero;
- a non-empty row set has `cursor < row_count`;
- the selected row remains within the non-zero viewport;
- filtering unavailable rows changes presentation only, never resolution.

Unavailable rows are shown by default. `:set nounavailable` hides them and
`:set unavailable` restores them. An unavailable value cannot be edited even
when visible.

## 4. Transactional typed editing

Every edit follows this sequence:

1. parse or choose a candidate without touching working state;
2. copy the candidate and prospective history record into owned storage;
3. construct a minimal assignment set from semantic non-default values;
4. invoke the normal dependency-aware resolver with the candidate substituted;
5. publish the new working value, resolution, and history record only after
   every prior step succeeds.

This is copy-on-write at the semantic transaction boundary. A malformed int or
hex value, invalid UTF-8/control-bearing string, out-of-range value, enum-domain
error, dependency conflict, unavailable edit, or allocation failure leaves the
previous working values and resolution unchanged. The UI does not implement a
second range, dependency, or availability authority.

Int input is a bounded canonical decimal integer. Hex input is a non-negative
`0x`-prefixed integer with bounded TOML-style underscore separators and the
schema-6 signed-hex ceiling. String validation is delegated to the value model.
Enum editing uses only the declaration's closed domain; arbitrary enum text is
never accepted.

## 5. Search, diff, dirty state, and history

Search matches prompt, help, and symbol with ASCII case folding while leaving
non-ASCII UTF-8 bytes literal. It operates only on the already loaded catalog;
it never searches paths or files. Next and previous search wrap through stable
catalog order and obey the unavailable-row filter.

Dirty state is semantic equality between every current typed value and the
saved baseline. It does not depend on declaration order, assignment spelling,
or whether a source user file redundantly wrote a default. Diff iteration
returns changed symbols in stable catalog order with borrowed saved and working
typed values.

Undo/redo uses a 128-entry owned ring. Each entry contains one config index and
typed before/after values. Adding a change after undo drops the redo tail. On
overflow the oldest record is discarded deterministically. Undo and redo use
the same resolver transaction as direct editing; neither bypasses dependency
validation.

## 6. Closed command language and save handshake

The entire command set is exact string comparison:

```text
:w
:q
:q!
:wq
:x
:help
:set unavailable
:set nounavailable
```

There is no abbreviation, history expansion, mapping, pipe, redirection, glob,
shell escape, external editor, plugin, or arbitrary Ex command. In particular,
`:!` is an unknown command.

The UI model never writes a user file or snapshot. It returns only these
controller effects:

- `REQUEST_SAVE` for `:w`, `:wq`, and dirty `:x`;
- `EXIT` for clean `:q`, clean `:x`, or successful save completion requested
  by `:wq`/dirty `:x`;
- `DISCARD_AND_EXIT` only for `:q!`;
- `NONE` for ordinary edits and navigation.

Before returning `REQUEST_SAVE`, the model transactionally copies the exact
working values into a pending baseline. While a save is pending, ordinary UI
actions are rejected. The controller publishes the immutable snapshot and then
calls `confit_ui_save_result`.

A successful result atomically adopts the already prepared baseline. `:w`
stays in the UI; `:wq` and dirty `:x` then return `EXIT`. A failed result drops
the pending baseline, preserves working values and dirty state, returns no exit
effect, and exposes a failure notice. Thus a publish failure cannot accidentally
exit or silently mark changes saved.

Dirty `:q` is rejected. Plain q and Esc are not aliases for any command.

## 7. Semantic view and evidence

`confit_ui_format_view` emits a deterministic terminal-free text view for
golden testing and controller diagnostics. It contains state, dirty flag,
current menu, cursor, row kind/depth, symbol, availability, and canonical typed
value. It contains no ANSI control and is not the R19 renderer.

`tests/unit/test_ui_model.c` covers hierarchy and viewport invariants, all five
typed edits, invalid edit preservation, unavailable edit rejection, search in
both directions, unavailable filtering, undo/redo and ring overflow, semantic
diff/dirty state, cancellation of every transient mode, plain-q behavior,
every closed command class, `:!` rejection, failed/successful save handshakes,
and 5,000 deterministic bounded action sequences.

The source is listed literally in the direct bmake manifest. R18 validation
also compiles the model with strict C17 warnings, runs the focused and aggregate
test sets normally and with clang sanitizers, and checks the model object import
surface for terminal, filesystem-discovery, subprocess, and dynamic-loader
capabilities. Those checks establish terminal independence of this module; they
do not establish the later POSIX frontend or manual terminal UX.
