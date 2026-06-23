# Round 11 Terminal Manual QA

Date: 2026-06-23

Scope: manual polish QA for the ncurses-backed Confit TUI after the
menuconfig-style schema editor work. The session used a PTY-backed terminal and
temporary fixture copies under `/tmp`; no repository fixture or real Delos/Parus
configuration was edited.

Build used:

```text
/tmp/confit-round11-build/confit
```

Workspace used:

```text
/tmp/confit-round11-qa
```

Build command:

```sh
cmake -S tools/confit -B /tmp/confit-round11-build
cmake --build /tmp/confit-round11-build
```

## Profile TUI

Command:

```sh
env TERM=xterm /tmp/confit-round11-build/confit \
  tui --project /tmp/confit-round11-qa/profile-editor --profile edit
```

Manual input sequence:

```text
/bool
Space
?
q
/count
e
7
s
Enter
/bool
Space
q
j
Enter
```

Observed flow:

- Browse opened on the menuconfig profile screen with an `edit` category and
  rows for bool, enum, int, uint, hex, float, string, and path options.
- `/bool` moved selection to `delos.edit.bool` and showed `result=1/1`.
- `Space` toggled the bool row from `[ ]` to `[*]` and marked the profile dirty.
- `?` opened the `Confit Help` detail view. It showed prompt, id, type, current
  value, default, source, category, tags, dependency state, and edit policy.
- `/count`, `e`, `7` opened the value dialog for `delos.edit.count`; the row
  changed from `(2)` to `(7)`.
- `s` opened the overwrite confirmation dialog. `Enter` selected overwrite.
- The status line reported `saved and reloaded`.
- After a second bool toggle, `q` opened `Unsaved Profile Changes`. Moving down
  once selected `Discard changes`, and `Enter` exited without rewriting the last
  saved profile.

Saved profile excerpt after the save/discard flow:

```toml
[values]
"delos.edit.bool" = true
"delos.edit.mode" = "sim"
"delos.edit.count" = 7
"delos.edit.threads" = 4
"delos.edit.mask" = 0x10
"delos.edit.gain" = 0.25
"delos.edit.name" = "old"
"delos.edit.path" = "old/path"
```

Post-save checks:

```sh
/tmp/confit-round11-build/confit check \
  --project /tmp/confit-round11-qa/profile-editor \
  --profile edit

/tmp/confit-round11-build/confit explain \
  --project /tmp/confit-round11-qa/profile-editor \
  --profile edit \
  delos.edit.count
```

Observed:

```text
check ok

option: delos.edit.count
state: enabled
value: 7
set by: profiles/edit.toml
```

Result: pass.

## Schema TUI

Command:

```sh
env TERM=xterm /tmp/confit-round11-build/confit \
  tui --project /tmp/confit-round11-qa/schema-editor --schema-edit
```

Manual input sequence:

```text
Enter
n
delos.round11.mode
enum
Round11 Mode
o
alpha,beta
d
beta
h
Manual QA schema option
c
qa
t
qa,manual
s
q
```

Observed flow:

- TUI first displayed `Schema Edit Warning`.
- The warning body said `SCHEMA EDIT MODE is a guarded workflow` and identified
  project `delos`.
- Entering the editor showed the menuconfig guarded schema screen with
  `SCHEMA EDIT MODE - guarded` in the header and `SCHEMA EDIT` in the status
  line.
- `n` opened schema field dialogs for option id, type, and prompt.
- `o`, `d`, `h`, `c`, and `t` reused the same field dialog style for choices,
  default, help, category, and tags.
- Saving reported `saved and validated`.

Saved schema excerpt:

```toml
[option."delos.round11.mode"]
type = "enum"
choices = ["alpha", "beta"]
default = "beta"
prompt = "Round11 Mode"
category = "qa"
tags = ["qa", "manual"]
help = "Manual QA schema option"
```

Post-save checks:

```sh
/tmp/confit-round11-build/confit graph \
  --project /tmp/confit-round11-qa/schema-editor

/tmp/confit-round11-build/confit list \
  --project /tmp/confit-round11-qa/schema-editor
```

Observed:

```text
{"id": "delos.round11.mode", "type": "enum"}
delos.round11.mode	enum	qa	Round11 Mode
```

Note: `check --project /tmp/confit-round11-qa/schema-editor` correctly rejected
the schema-only fixture because `check` requires `--profile`.

Result: pass.
