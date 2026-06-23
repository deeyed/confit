# Round 14 Terminal Manual QA

Date: 2026-06-24

Scope: production polish QA for the Confit ncurses TUI. The session used copied
fixtures under `/tmp` and terminal capture scripts; no real Delos/Parus
configuration was edited.

Build used:

```text
/var/folders/1g/wddr4j993gzd7y87094d7k340000gn/T/confit-build/confit
```

## Profile Workflow

Command:

```sh
/bin/sh tools/confit/tests/integration/round18_tui_profile_editor.sh \
  /var/folders/1g/wddr4j993gzd7y87094d7k340000gn/T/confit-build/confit \
  tools/confit \
  /tmp/confit-round14-profile-editor
```

Observed:

- The profile editor opened as `Confit TUI - menuconfig profile`.
- The header included `search fields=id,prompt,help,category,tags`.
- Search, enum popup, numeric/string/path dialogs, invalid-input rejection, save
  confirmation, dirty quit, discard, and quit-save flows were exercised.
- The help/detail view wrapped the long `delos.edit.name` help text, including
  `production TUI help panel` and `without losing` on captured output lines.
- Explicit save reported `saved and reloaded; full validation ok`.
- The saved profile matched the golden profile, and a post-save `check`
  reported `check ok`.

Result: pass.

## Compact Terminal Fallback

Command:

```sh
/bin/sh tools/confit/tests/integration/round17_tui_smoke.sh \
  /var/folders/1g/wddr4j993gzd7y87094d7k340000gn/T/confit-build/confit \
  tools/confit \
  /tmp/confit-round14-tui-smoke
```

Observed:

- The standard profile screen still showed menuconfig navigation, grouped rows,
  dependency state, help/detail, and search result movement.
- A small terminal run with `LINES=8 COLUMNS=35` rendered the compact fallback.
- The compact screen included `compact terminal fallback` instead of leaving the
  user with a blank or malformed boxed layout.

Result: pass.

## Schema Edit Workflow

Command:

```sh
/bin/sh tools/confit/tests/integration/round19_tui_schema_editor.sh \
  /var/folders/1g/wddr4j993gzd7y87094d7k340000gn/T/confit-build/confit \
  tools/confit \
  /tmp/confit-round14-schema-editor
```

Observed:

- Schema edit mode opened with `Schema Edit Warning`.
- The warning stated that generated outputs are not written by this mode and
  only source TOML changes are saved after validation.
- The guarded schema screen stayed visually distinct from profile editing.
- New enum and int options were created through field dialogs.
- Invalid id and invalid range input were rejected in-dialog.
- Save reported `schema saved and validated; reloaded graph`.
- The reloaded graph contained the newly created schema options.

Result: pass.

## Notes

The TUI is now consistent enough for macOS/Linux fixture editing and save/reload
validation. It still intentionally depends on a terminal and ncurses; Windows
TUI support remains outside this round's scope.
