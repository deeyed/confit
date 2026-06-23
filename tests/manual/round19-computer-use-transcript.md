# Round 19 Computer Use QA

Date: 2026-06-23

Tooling note: Computer Use denied direct access to iTerm, Terminal, and Codex.
The manual QA was therefore performed through the allowed Visual Studio Code
integrated terminal window. The TUI commands used fixture copies under `/tmp`
and did not edit repository files outside `tools/confit`.

Build used:

```text
/var/folders/1g/wddr4j993gzd7y87094d7k340000gn/T/confit-build/confit
```

Workspace used:

```text
/tmp/confit-round19-cu.LKhiJI
```

Profile creation flow:

```text
cp -R tools/confit/tests/fixtures/tui/profile-editor \
  /tmp/confit-round19-cu.LKhiJI/profile-create
printf 'sq' | confit tui \
  --project /tmp/confit-round19-cu.LKhiJI/profile-create \
  --profile cu-created
```

Observed:

```text
== Confit TUI ==
project=delos profile=cu-created ...
[status] ... s save q quit ...
saved /tmp/confit-round19-cu.LKhiJI/profile-create/config/profiles/cu-created.toml
```

Saved profile:

```toml
[profile]
name = "cu-created"
schema_version = 1

[values]
```

Guarded schema edit flow:

```text
cp -R tools/confit/tests/fixtures/tui/schema-editor \
  /tmp/confit-round19-cu.LKhiJI/schema-editor
printf 'ndelos.schema.mode\nenum\nInitial Prompt\npCreated Prompt\nhCreated help\ncschema\ntschema,created\nored,blue\nndelos.schema.limit\nint\nLimit Prompt\npLimit Prompt\nhLimit help\ncschema\ntschema,limit\nr0,16\nsq' |
  confit tui --project /tmp/confit-round19-cu.LKhiJI/schema-editor --schema-edit
```

Observed warning:

```text
Schema edit mode changes project configuration semantics.
Prefer code review for schema changes.
```

Saved schema:

```toml
[option."delos.schema.mode"]
type = "enum"
choices = ["red", "blue"]
default = "red"
prompt = "Created Prompt"
category = "schema"
tags = ["schema", "created"]
help = "Created help"

[option."delos.schema.limit"]
type = "int"
default = 0
prompt = "Limit Prompt"
category = "schema"
tags = ["schema", "limit"]
help = "Limit help"
range = [0,16]
```

Graph check:

```text
{"id": "delos.schema.mode", "type": "enum"}
{"id": "delos.schema.limit", "type": "int"}
```

Result: pass.
