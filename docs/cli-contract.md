---
doc_type: cli-contract
status: accepted
authority: normative
---

# Confit CLI Contract

Confit is a host-side configuration authority tool. Its command line interface
must support validation, resolution, generation, explanation, compatibility
checks, profile management, and optional terminal editing without requiring a
runtime service.

This document defines the agreed command surface and local installation rules.
It is not an implementation progress log.

## Command Set

Confit has thirteen operational top-level commands plus the `help` command.
`--version` is a global option, not a command.

| Command | Contract |
|---|---|
| `help [command]` | Show global help or command-specific help. |
| `doctor` | Check host tool installation, platform support, build metadata, project layout, and generator availability. |
| `init` | Create a Confit project skeleton from a named template. |
| `check` | Parse project TOML, validate schema, validate graph constraints, and resolve the selected profile. |
| `resolve` | Emit the resolved configuration without generating build artifacts. |
| `gen` | Generate deterministic configuration artifacts into an explicit output directory. |
| `explain` | Explain one resolved option value and the reasons that affect it. |
| `list` | List schema entities such as options, profiles, targets, categories, and tags. |
| `graph` | Emit the option dependency graph as JSON or DOT. |
| `diff` | Compare resolved configuration results between two profiles or targets. |
| `compat` | Check compatibility assertions across project roots. |
| `profile` | Manage profile TOML without opening the TUI. |
| `tui` | Open the terminal UI where supported by the host platform. |
| `completion` | Emit shell completion text. |

## Global Options

| Option | Contract |
|---|---|
| `--help` | Show command help and exit successfully. |
| `--version` | Print the Confit version and exit successfully. |
| `--color auto|always|never` | Control diagnostic color. `auto` is the default. |
| `--quiet` | Suppress non-essential informational output. |
| `--verbose` | Print additional diagnostic context. |

Global options may appear before or after the command name. `--quiet` must not
hide command payloads, validation errors, or generated completion text; it only
suppresses non-essential success banners such as `check ok`. `--verbose` writes
additional execution context to stderr and must not alter stdout payloads.

## Project Options

| Option | Contract |
|---|---|
| `--project <path>` | Select a Confit project root. |
| `--profile <name>` | Select a profile by name. |
| `--target <name>` | Select a target by name. |
| `--set <option-id=value>` | Apply a transient override for the current command. |
| `--format text|json|toml|dot` | Select output format where a command supports multiple formats. |

`--set` must not edit profile TOML. Persistent profile changes belong to
`confit profile` or `confit tui`.

## Validation And Generation Options

| Option | Contract |
|---|---|
| `--strict` | Treat warnings as command failures where applicable. |
| `--dry-run` | Validate and report intended writes without writing files. |
| `--out <path>` | Select the output directory for generated artifacts. |
| `--artifact header|reports|cmake|qstar|all` | Select generated artifact groups. |
| `--force` | Permit overwriting existing output files when the command would otherwise refuse. |

Generators must be deterministic. Generated files must not contain timestamps or
host-local absolute paths unless the selected artifact explicitly requires them.

## Query Options

| Option | Contract |
|---|---|
| `--kind options|profiles|targets|categories|tags` | Select the entity kind for listing. |
| `--category <name>` | Filter options by category. |
| `--tag <name>` | Filter options by tag. |
| `--query <text>` | Search ids, prompts, help text, categories, and tags where supported. |
| `--show-hidden` | Include hidden or inactive options in query output. |

## Compatibility Options

| Option | Contract |
|---|---|
| `--parus <path>` | Select the Parus-side project root for compatibility checks. |
| `--delos <path>` | Select the Delos-side project root for compatibility checks. |
| `--compat <path>` | Select compatibility rule files. |
| `--format text|json` | Select human text or structured compatibility report output. |

Compatibility checks must read explicit project roots and rule paths. They must
not discover or modify sibling repositories implicitly.

## Editing And Utility Options

| Option | Contract |
|---|---|
| `--schema-edit` | Open guarded schema editing mode for `confit tui`. |
| `--template minimal|delos|parus` | Select the project skeleton template for `confit init`. |
| `--base <profile>` | Select a base profile for `profile new` or `diff`. |
| `--shell bash|zsh|fish` | Select completion shell output. |

Schema editing is a guarded workflow. It writes human-readable TOML and must
perform validation before saving.

## Profile Subcommands

`confit profile` provides non-interactive profile TOML management:

```text
confit profile list --project <path>
confit profile show --project <path> <name>
confit profile new --project <path> <name> [--base <profile>] [--target <target>] [--force]
confit profile set --project <path> <name> <option-id=value>
confit profile unset --project <path> <name> <option-id>
confit profile validate --project <path> <name>
```

Profile writes must be deterministic, human-readable TOML writes. `profile new`
must refuse to replace an existing profile unless `--force` is present.
`profile set` and `profile unset` must validate option ids and value types before
writing, then resolve the profile successfully before saving the TOML file.

## Command Examples

```sh
confit doctor
confit init --project config/confit --template delos
confit check --project config/confit --profile sim-dsh --target sim
confit resolve --project config/confit --profile sim-dsh --format json
confit gen --project config/confit --profile sim-dsh --out build/generated/config/delos/sim-dsh --artifact all
confit explain --project config/confit --profile sim-dsh delos.debug.dsh
confit list --project config/confit --kind options --category debug
confit graph --project config/confit --profile sim-dsh --format dot
confit diff --project config/confit --profile sim-dsh --base hw-debug
confit compat --parus ../parus/config/confit --delos config/confit --profile parus-delos-debug --compat config/compat
confit profile list --project config/confit
confit profile show --project config/confit sim-dsh
confit profile new --project config/confit debug-dsh --base sim-dsh
confit profile set --project config/confit debug-dsh delos.debug.dsh=true
confit profile unset --project config/confit debug-dsh delos.debug.dsh
confit profile validate --project config/confit debug-dsh
confit tui --project config/confit --profile sim-dsh
confit tui --project config/confit --schema-edit
confit completion --shell zsh
```

## Exit Codes

```text
0  success
1  invalid command line
2  parse error
3  schema error
4  dependency or conflict error
5  compatibility error
6  generation error
7  internal error
8  unsupported command or platform
```

Unsupported platform behavior must be explicit. A command must not silently
degrade into partial output when a required host capability is unavailable.

## Write Rules

Confit commands are read-only unless the command contract explicitly includes a
write. Write-capable commands must require an explicit project path or output
path.

Allowed persistent writes:

```text
confit init       project skeleton TOML
confit gen        generated artifacts under --out
confit profile    profile TOML
confit tui        profile TOML, or guarded schema TOML in --schema-edit mode
```

Forbidden implicit writes:

```text
real Parus/Delos runtime source edits
runtime service state
hidden binary databases
network-fetched configuration
sibling repository discovery or mutation
```

## Local Install Contract

The local install command is:

```sh
tools/confit/scripts/install-local.sh --prefix ~/.local
```

The equivalent portable install flow is:

```sh
cmake -S tools/confit -B /tmp/confit-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/confit-build --target confit
cmake --install /tmp/confit-build --prefix "$HOME/.local"
```

The required install artifact is a single executable:

```text
<prefix>/bin/confit
```

On Windows hosts, the executable name is:

```text
<prefix>/bin/confit.exe
```

Optional install artifacts are allowed, but Confit must run without them:

```text
<prefix>/share/man/man1/confit.1
<prefix>/share/zsh/site-functions/_confit
<prefix>/share/bash-completion/completions/confit
<prefix>/share/fish/vendor_completions.d/confit.fish
```

Completion installers must use `confit completion --shell bash|zsh|fish` as the
source of truth instead of maintaining separate handwritten shell scripts.

Installation must not modify any project `config/` tree. Project creation and
project edits belong to explicit Confit commands, not to installation.
