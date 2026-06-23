# Round 20 Computer Use QA

Date: 2026-06-23

Tooling note: Computer Use denied direct access to iTerm, Terminal, and Codex in
Round 19. The final QA therefore used the allowed Visual Studio Code integrated
terminal again. All project writes happened in a `/tmp` fixture copy.

Build used:

```text
/var/folders/1g/wddr4j993gzd7y87094d7k340000gn/T/confit-build/confit
```

Workspace used:

```text
/tmp/confit-round20-cu.obvFPq
```

## Profile TUI Flow

Command:

```sh
printf 'j/mode\nx/bool\nesq' |
  confit tui --project /tmp/confit-round20-cu.obvFPq/profile-flow --profile edit
```

Observed browse:

```text
> delos.edit.mode = sim
```

Observed search:

```text
project=delos profile=edit target=- search=mode category=- tag=- dirty=no
> delos.edit.mode = sim
```

Observed toggle:

```text
project=delos profile=edit target=- search=bool category=- tag=- dirty=yes
> delos.edit.bool = true
[status] ... edited delos.edit.bool
```

Observed save:

```text
[status] ... saved /tmp/confit-round20-cu.obvFPq/profile-flow/config/profiles/edit.toml
```

Saved profile excerpt:

```toml
[profile]
name = "edit"
schema_version = 1

[values]
"delos.edit.bool" = true
"delos.edit.mode" = "sim"
"delos.edit.count" = 2
"delos.edit.gain" = 0.25
"delos.edit.name" = "old"
"delos.edit.path" = "old/path"
```

## Explain Flow

Command:

```sh
confit explain \
  --project /tmp/confit-round20-cu.obvFPq/profile-flow \
  --profile edit \
  delos.edit.bool
```

Observed:

```text
option: delos.edit.bool
state: enabled
value: true
set by: profiles/edit.toml

why:
  enabled because resolved bool is true
  value comes from profiles/edit.toml
```

## Schema Edit Warning

Command:

```sh
printf 'q' |
  confit tui --project /tmp/confit-round20-cu.obvFPq/schema-warning --schema-edit
```

Observed:

```text
== Confit Schema Editor ==
Schema edit mode changes project configuration semantics.
Prefer code review for schema changes.
project=delos dirty=no
```

Result: pass.
