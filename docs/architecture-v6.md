---
doc_type: architecture-contract
status: accepted
authority: confit-architecture-v6
schema_version: 6
implementation_status: release-candidate-implemented
---

# Confit schema 6 architecture and security contract

## 1. Purpose and evidence boundary

This document fixes the architecture that implements
[`config-v6.md`](config-v6.md). Confit is a generic configuration tool. Its
product boundary ends at typed configuration data and immutable configuration
snapshots. It neither analyzes nor drives a consumer's ordinary build.

R01 froze this architecture before the inherited schema 5 surface was removed.
R22 has now closed the schema 6 release-candidate audit. Every implementation
claim still identifies its evidence class: source review, compile, unit,
integration, exact-I/O observation, compiled import surface, sanitizer, bounded
fuzz, PTY, or manual terminal review; the R22 audit is the current evidence
ledger.

The architecture is designed around four constraints:

1. only explicit TOML and selected-snapshot capabilities are visible to the
   product;
2. core semantics are independent of hosted filesystem and terminal code;
3. parsing, resolution, emission, and publication are bounded and fail-closed;
4. clang toolchain, bmake, shell, Confit, and Confit-owned C test binaries close
   the required bootstrap path.

## 2. Responsibility map

```text
CLI / terminal controller
        |
        v
project loader ---- user-config loader
        |                    |
        +------> catalog <---+
                    |
          expression linker/evaluator
                    |
               resolver
                    |
        +-----------+------------+
        |                        |
   safe emitters            UI state model
        |                        |
        +--------> snapshot publisher
                         |
                     verifier

host capability layer: exact bounded file and terminal operations only
core model: no filesystem, terminal, process, environment, or build knowledge
```

The arrows are data/control calls, not source discovery. No layer may gain a
second hidden path to consumer files or subprocesses.

### 2.1 Core model

The core owns:

- public resource limits;
- symbols, menus, source fragments, and declaration spans;
- bool, int, hex, string, and enum values;
- declaration defaults, user assignments, resolved origin, availability, and
  bounded causal reasons;
- deterministic comparison and canonical formatting helpers;
- explicit ownership and lifetime rules.

Core objects do not call `open`, `fopen`, `getenv`, stdout/stderr, terminal APIs,
or process APIs. A project path is display/provenance data in the core, never an
implicit capability to reopen a file.

### 2.2 Host capability layer

The host layer owns the smallest filesystem and terminal primitives needed by
the product. It does not own project semantics, document discovery, schema
interpretation, resolution, emitter policy, or snapshot membership.

Public host capabilities are separated by root:

- a project-root directory descriptor for entry and reachable fragment paths;
- an explicit user-config capability;
- an output-root directory descriptor for Confit-owned snapshots;
- terminal descriptors for `menuconfig`.

The host API exposes no directory-enumeration or process-execution capability to
product code.

### 2.3 Input-image and TOML adapter

The input layer owns exact byte images, SHA-256 membership digests, file identity,
source display path, and line-start indices. The TOML adapter parses memory, not a
path. It exposes typed read-only TOML nodes and source spans.

The adapter does not decide which documents belong to a project and does not
interpret schema fields.

### 2.4 Project loader

The loader starts with the single CLI-designated entry and follows only literal
`source` arrays. It owns graph reachability, path normalization, duplicate/cycle
checking, single presentation parent, traversal order, and cumulative resource
limits.

It does not enumerate, glob, infer conventional names, inspect project source, or
derive a build graph.

### 2.5 Schema loader

The schema loader validates exact entry/menu/config/user shapes and builds the
generic catalog. It rejects unknown fields, invalid TOML containers, duplicate
symbols, invalid type applicability, and invalid limits.

It does not evaluate dependencies, select sources for a build, or publish data.

### 2.6 Expression and resolver layers

The expression layer implements only the grammar in `config-v6.md`. It lexes,
parses, type-checks, links every symbol reference before evaluation, extracts
unique availability edges, rejects cycles, constructs a symbol-stable
prerequisite-first order, and evaluates availability without mutation. Logical
evaluation short-circuits, but linking never does, so an invalid reference in a
non-evaluated branch is still an error. `docs/expression-v6.md` records the R11
ownership and evidence boundary.

The resolver applies one declaration default and at most one explicit user value
per symbol. It produces an immutable successful result or no publishable result.
Assignment order has no precedence; lookup and result iteration use a lexical
symbol index while availability uses the linked stable topological plan. It
copies the bounded causal reason, rejects unavailable non-default user intent,
and never calls an emitter, writes a user file, or changes another declaration.
`docs/resolver-v6.md` records the R12 API, ownership, bounds, and evidence.

### 2.7 Emitters

Make, C-header, and JSON emitters are independent target-language encoders over
an immutable resolved result. Each owns its own escaping. No generic “escape for
everything” routine is an authority for multiple output grammars.

Emitters produce configuration data only. They do not generate rules, source
lists, compiler options, commands, or consumer-specific names.

### 2.8 Snapshot publisher and verifier

The publisher owns multi-file candidate construction, content addressing,
durability ordering, locking, sealing, and the atomic `selected` replacement.

The verifier owns selected-record validation, sealed snapshot identity, artifact
digests, and exact manifest-listed input remeasurement. It does not own schema
resolution and does not search for changed files.

### 2.9 UI model and terminal frontend

The UI model is terminal-independent. It owns rows, navigation, search, typed
candidate edits, undo/redo, diff, dirty state, closed command parsing, and
save/exit decisions. It returns side-effect requests to a controller rather than
writing files itself.

The POSIX frontend owns termios, bounded input decoding, polling, resize,
rendering, and terminal restoration. It cannot edit schema definitions, inspect
project source, invoke an external editor, or execute a command.

R18 implements the first half of this split in `include/confit/ui.h` and
`src/tui/ui_model.c`. R19 links the second half from `src/tui/terminal.c` while
keeping its project I/O behind a resolution-only save callback. The exact model
state and controller handshake are recorded in
[`ui-model-v6.md`](ui-model-v6.md); the terminal capability and restoration
contract is recorded in [`terminal-v6.md`](terminal-v6.md).

## 3. Product capability boundary

### 3.1 Files the product may read

The product may read only:

1. the entry TOML explicitly selected by `--root` and `--project`;
2. TOML fragments reachable through literal entry/menu `source` edges;
3. the user configuration explicitly selected by `--config` or
   `--other-config`;
4. Confit-owned snapshot records beneath the explicit `--output` root;
5. exact prior input paths listed in a selected snapshot manifest;
6. its own executable image when a self-identity digest is required;
7. stdin/stdout/stderr and terminal descriptors.

The product must not open an unreferenced TOML, project source file, Makefile,
object, archive, linked image, script, or sibling directory. It must not inspect
Git metadata or infer project selection from an environment variable.

### 3.2 Process and discovery capabilities that must be absent

The final product binary must not import or call these API families:

```text
system
popen
fork
vfork
exec*
posix_spawn*
opendir
readdir
scandir
glob
nftw
fts_*
dlopen
```

Absence is verified on the final linked product, not inferred from a grep alone.
The explicit product source manifest and compiled undefined-import table are
reviewed together.

A test-only PTY or concurrency supervisor may need child-process APIs. Such code
is a separate C binary in a separate test source manifest and must not link into
the product. A test runner's capability is not a product capability.

### 3.3 No consumer analysis

Confit never:

- reads a C or header file;
- reads a Makefile or included make fragment;
- discovers sources by file suffix;
- maps a configuration symbol to an object or source;
- checks an include graph, link order, artifact format, or compile command;
- probes a compiler or linker feature;
- starts bmake, a compiler, a shell, a runner, or a hook;
- predicts whether a build or executable will succeed.

The only connection between configuration and ordinary build is a sealed typed
value artifact that the consumer chooses to read after successful verification.

## 4. Descriptor-rooted filesystem model

### 4.1 Root acquisition

`--root` and `--output` are explicit absolute directories. The host layer opens
each root as a directory descriptor with close-on-exec and no symlink following,
then verifies the result with `fstat`. A string produced by `realpath` is not the
security authority.

An entry or source path is walked from the project-root descriptor one component
at a time. Intermediate components must be real directories and the final input
must be a regular file. No fallback follows a symlink.

An absolute user-config path is an explicit separate capability. Its directory
components are walked descriptor-first from the filesystem root under the same
no-follow and regular-file policy. A relative user-config path is walked beneath
the project root. The implementation does not search for a missing config.

### 4.2 Relative path validation

Before I/O, source paths reject empty text, absolute form, empty/dot/dot-dot
segments, repeated slash, backslash, glob metacharacters, excessive bytes, and a
non-`.toml` final component. Validation and descriptor walk both fail closed; a
canonical-looking string does not override an unsafe filesystem identity.

### 4.3 Bounded input read

Input read proceeds as follows:

1. open the final component no-follow and close-on-exec;
2. verify regular-file identity;
3. reject a stat size above the per-file or cumulative limit;
4. allocate with overflow-checked size arithmetic;
5. read with EINTR handling and an explicit byte ceiling;
6. reject unexpected growth, shrinkage, short-read inconsistency, and embedded
   NUL where TOML text requires none;
7. close the descriptor after forming the owned byte image.

FIFO, socket, device, directory, symlink, and oversized input are errors. Partial
bytes never become a document.

### 4.4 Safe write primitives

No product write uses a predictable fixed temporary leaf with truncating open.
Candidate leaves use bounded unique generation and
`O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC`. The writer verifies a regular file, handles
short writes, `fsync`s the file, and uses descriptor-relative rename. It then
`fsync`s the containing directory where required by the publication protocol.

Cleanup removes only a candidate created and owned by the current operation. It
never removes an unrelated pre-existing path. A target that is a symlink or an
unexpected file type is rejected.

### 4.5 Locking

The output root uses a regular no-follow lock file and `fcntl` locking to
serialize publishers. A stale pathname alone does not prove a live lock.
Content-addressed create-only snapshots and atomic selected replacement still
ensure that a reader sees an old complete snapshot or a new complete snapshot,
not mixed partial state.

## 5. Parse-once, hash-once ownership

`ConfitInputImage` conceptually owns:

- exact bytes and length;
- SHA-256 over those exact bytes;
- file identity captured at read time;
- normalized display path;
- line-start index for diagnostics.

The same byte buffer is passed to the TOML adapter and digest computation. The
parser does not reopen a path. Manifest generation does not reopen a path to hash
it again. Diagnostic spans point into or are derived from the same immutable byte
image.

If a pathname is replaced immediately after the read, the parsed document and
stored membership digest must both describe the originally read bytes. A
controlled test hook performs that replacement race. Hashing a later path image
is a contract failure.

The digest is a membership identity, not an authenticity signature. The contract
does not claim protection against every same-identity hostile peer or a fully
compromised host.

## 6. Explicit graph loading

The project loader uses an explicit stack or bounded recursion with these node
states:

```text
unseen -> visiting -> visited
```

An edge to `visiting` is a cycle. An edge to `visited` is a duplicate include and
also an error because one fragment has one presentation parent. Node, edge,
depth, path, file-size, and total-byte bounds are checked before adding state.

The stable input ledger records every opened configuration path, purpose, and
byte count in tests. The ledger is instrumentation, not a product output format.

A required poison fixture has this shape:

```text
project/
├── Confit.toml
├── config/
│   └── reachable.toml
├── unrelated/
│   └── invalid.toml
├── src/
│   └── poison.c
├── Makefile
└── build/
    └── poison.mk
```

When only `config/reachable.toml` is sourced, the invalid TOML, C source,
Makefile, and make fragment must not appear in the read ledger or affect output.
Fixing poison content so a test passes is not a valid correction.

## 7. Parser and catalog invariants

The TOML adapter provides values and source spans; a schema-specific loader owns
the exact key sets. Every table is checked against a closed allowed-key set before
individual fields are consumed. A parser library's last-wins handling of
duplicate keys is not accepted; the adapter or bounded source-token layer must
detect duplicates and fail closed.

The catalog has:

- one declaration per symbol;
- one menu parent per fragment;
- one root entry;
- type-correct owned defaults and enum domains;
- declaration spans into owned input images;
- a linked bounded dependency graph.

Catalog construction is transactional. An error frees partial owned state and
does not expose a usable catalog.

## 8. Expression and resolution invariants

Expression parsing checks byte length, token size, AST-node count, and nesting
before expansion. Unknown operators and invalid UTF-8/control text fail. Linking
occurs for all references before evaluation so short-circuit behavior cannot hide
an unknown symbol or type error.

Dependency cycles are reported with a stable symbol path independent of source
or hash-table order. Evaluation uses a deterministic topological plan and reads
resolved candidates without mutating them.

The resolver maintains separate concepts:

- declaration default;
- optional explicit user candidate;
- dependency availability;
- effective value;
- origin (`default` or `user`);
- causal availability reason.

There is no origin for a hidden writer. Failure leaves no publishable partial
result. Reordering source arrays or declarations may change presentation but must
not change canonical result bytes.

## 9. User-file serialization

Parser and serializer share one native TOML type model. The serializer writes a
stable schema 6 document, stable lexical symbol order, and only non-default user
intent. It escapes strings for TOML, preserves hex type identity with canonical
hex spelling, and computes a bounded output size before publication.

CLI and TUI call the same serializer. Neither has a private alternate syntax.
Source-tracked files are not modified by a save unless the user explicitly
invokes `savedefconfig --destination`.

## 10. Snapshot transaction

### 10.1 Logical content

A selected snapshot contains required roles:

- `catalog.summary`;
- `user-values.toml`;
- `resolved-values.json`;
- `inputs.manifest`;
- `provenance.json`;
- `snapshot.seal`;

and requested optional roles such as `values.mk` and `values.h`.

`catalog.summary` is a non-printable, sealed, lexical identity of schema-6
symbol membership, type, prompt/help, default, domain, and dependency. Migration
review validates the selected seal and its exact artifact bytes without
remeasuring the old manifest inputs, because changed definition bytes are the
comparison subject. Ordinary `verify` retains exact manifest-input validation.

`resolved-values.json` is always a sealed core role. An explicit JSON emitter
request records that the same role is also a consumer projection eligible for
artifact-path output; it does not create a duplicate file. Optional Make and
C-header roles exist only when requested.

`inputs.manifest` lists only the entry, reachable fragments, and explicit user
configuration with normalized identity, exact size, and SHA-256 in stable order.
`provenance.json` records schema and Confit build identity without making an
absolute project location part of value semantics. `snapshot.seal` binds every
role name, byte size, and digest.

### 10.2 Publication protocol

Publication follows this order:

1. acquire the output-root writer lock;
2. serialize all required and requested artifacts in memory with bounds;
3. create a private candidate directory without following or replacing a path;
4. create each candidate file exclusively;
5. write, `fsync`, reopen, and verify every candidate file;
6. create and verify the seal;
7. `fsync` the candidate directory;
8. compute the content address and publish a create-only final snapshot
   directory, or verify an exact existing snapshot with that address;
9. create a private regular selected-record candidate containing digest plus
   newline;
10. `fsync` it and atomically replace the regular `selected` record;
11. `fsync` the output root and release the lock.

The selected record is the only activation point. A complete orphan snapshot is
not active. Failure before selected replacement leaves the previous selected
record unchanged. Failure after atomic replacement may report durability failure,
but must never expose a mixed bundle.

### 10.3 Verify protocol

`verify` performs only:

1. validate `selected` as bounded regular digest text;
2. validate the addressed snapshot directory and file identities;
3. validate the seal and every requested artifact digest;
4. parse the manifest without directory enumeration;
5. reopen and hash only exact manifest-listed input paths;
6. return success, and only then optionally print an artifact path.

It does not rerun parser/resolver semantics, scan for new TOML, inspect project
source, or infer staleness from directory timestamps. A file absent from the
manifest is irrelevant to verification.

The protocol uses local filesystem `fsync`/rename guarantees. It is not a
certification for every filesystem or arbitrary power-loss model. SHA-256 is not
a signature.

## 11. Emitter security

### 11.1 Make data

The Make emitter can form only:

```text
CONFIG_<VALID_SYMBOL>=<CLOSED_LITERAL>\n
```

Bool, signed decimal int, canonical hex, and validated enum atoms are closed
literals. An arbitrary string makes the request fail. `$`, `{`, `}`, `#`,
newline, `.include`, rule syntax, variable expansion, and comments cannot enter
through a value. The emitter produces no rule or source membership.

### 11.2 C-header data

The C emitter uses a generic include guard and validates every macro name. It
encodes bool as `0`/`1`, numeric values canonically, and string/enum data using a
C-specific escaping routine. Quotes, backslashes, newline, control bytes, and
non-printing bytes cannot terminate or inject a directive. A generated header is
compiled as a strict C17 translation unit in tests.

### 11.3 JSON data

The JSON emitter owns JSON-specific escaping and stable member ordering. It
records typed configuration facts only. It does not embed callback names,
commands, source paths, or consumer instructions.

All emitters preflight size arithmetic and fail without a partial published
artifact. A hostile corpus independently tests each target grammar.

## 12. CLI controller

The CLI parser is bounded and closed. It accepts only commands and options listed
in `config-v6.md`. Duplicate options are rejected except `--emit`, whose values
form a bounded set; repeating the same emitter is also an error rather than an
ordering mechanism.

The CLI constructs explicit request objects. It does not read an environment
variable for project, configuration, output, type, platform, or profile
selection. A command that does not own an option rejects it.

Output channels are separated:

- human diagnostics: stderr;
- normal human command output: stdout;
- `verify --print-artifact`: exactly one verified absolute path and newline on
  stdout, no extra banner.

`check` is write-free. `configure` publishes a snapshot. `verify` does not
resolve. Search is catalog search, not filesystem search. Diff is semantic value
comparison, not a general text or source-control diff. No command starts an
ordinary build.

`listnewconfig`, `oldconfig`, and `olddefconfig` compare only the selected sealed
catalog with the current explicit graph. New symbols are exact membership
differences; removal, type, range/enum-domain, default, or dependency changes
stop automatic migration. `savedefconfig` full-verifies selected input and calls
the shared minimal serializer only for an explicit destination. No migration
command reads Git history, scans a directory, infers a rename, or coerces a
typed value.

## 13. Terminal-independent UI state

The UI state contains an immutable catalog, saved baseline, copy-on-write working
values, bounded undo/redo ring, cursor/view state, and one of the public modes.

Every edit follows:

```text
current state -> candidate -> typed validation -> dependency validation
              -> commit candidate OR discard candidate
```

The implementation must not mutate a current value and then attempt validation.
On a failed edit, working values, dirty state, cursor, undo/redo, and selected
snapshot remain byte-for-byte or semantically unchanged as applicable.

The closed command parser returns a decision:

- no side effect;
- save request;
- clean exit request;
- discard exit request;
- save-then-exit request.

An outer controller performs publication and reports success/failure back to the
model. Exit after save is permitted only on success. Unavailable filtering is a
view operation and cannot modify availability or values.

## 14. POSIX terminal frontend

The frontend uses C17 or C23, POSIX termios, `poll`, `ioctl(TIOCGWINSZ)`,
`SIGWINCH`, and ANSI/ECMA-48. It has no ncurses or external terminal library.

Before raw mode, it:

1. verifies a TTY and obtains dimensions;
2. rejects dimensions below 40 x 10 without entering raw mode;
3. stores original termios;
4. installs bounded signal notification;
5. enters alternate screen and hides the cursor only after cleanup state is
   ready.

Cleanup is idempotent. Normal return, parse/render/write failure, EOF, SIGINT,
SIGTERM, and SIGHUP request restoration in the main loop. A signal handler does
not allocate or perform complex terminal I/O; it sets an atomic flag or writes a
bounded self-pipe notification. SIGKILL restoration is explicitly not claimed.

The input decoder recognizes the documented ASCII/control keys and required CSI
arrow sequences. Truncated or unknown escape sequences do not execute a command.
There is no shell escape. Prompt/help/user text is sanitized so ESC and C0/C1
bytes cannot become terminal controls.

Layout policy:

- width at least 80: list/menu and fixed detail split pane;
- width 40 through 79: list/detail tabbed layout;
- smaller than 40 x 10: diagnostic and no raw mode;
- reported surface above 512 x 256: clamp logical allocation;
- color is decoration, never the sole unavailable/dirty/error signal.

## 15. Bootstrap closure

### 15.1 Required executable set

The mandatory product/test bootstrap path is limited to:

- a provisioned clang C compiler, linker, and clang toolchain analysis tools;
- bmake;
- shell for bounded orchestration;
- an already-built Confit where self-use is required;
- Confit-owned C test binaries built by clang.

Git is source-control closeout tooling, not a product build/runtime dependency.
Documentation utilities used to count or inspect authoring files are not part of
the bootstrap claim.

Mandatory paths must not require:

- Python, Perl, Ruby;
- CMake, Ninja, Meson, GNU Make;
- ncurses or another UI library;
- pkg-config;
- a generated parser generator;
- external TOML/JSON/schema processors;
- an external hash executable;
- filesystem-based source/test discovery.

### 15.2 Explicit bmake graph

Product, vendor, unit-test, integration-test, PTY-test, and fuzz-test sources are
listed literally in bmake manifests. Wildcards, recursive scans, generated source
lists, and discovery scripts are not source-membership authority.

First-party source uses a strict warning policy. Vendored source may use a
separate reviewed warning policy without weakening first-party compilation.

### 15.3 Exact bootstrap claim

The supported claim is:

> Given an already existing writable object directory and provisioned clang
> toolchain, bmake, and shell, the Confit product and required Confit-owned C test
> binaries can be regenerated and executed.

The claim does not include creating directories from a blank filesystem,
provisioning the toolchain, installing Confit, all-host portability, or ordinary
consumer build success. Mandatory build targets do not depend on external
`mkdir`, `rm`, source discovery, or test discovery. Confit-owned C test support
may create and remove its bounded temporary fixtures with direct POSIX file APIs.

## 16. Threat model and required regressions

### 16.1 Predictable temporary overwrite

Threat: an attacker precreates a candidate leaf as a symlink or regular file.

Required property: exclusive no-follow creation fails without truncating or
overwriting the target. Cleanup cannot unlink the attacker's unrelated object.

### 16.2 Intermediate path substitution

Threat: a path component is replaced by a symlink or different directory between
validation and access.

Required property: descriptor-relative component walk and identity checks fail
closed; canonical text alone is insufficient.

### 16.3 Parse/hash TOCTOU

Threat: the pathname is replaced between parse and manifest hashing.

Required property: parse and digest use one owned byte image with no path reopen.

### 16.4 Selected substitution

Threat: `selected`, a snapshot directory, or an artifact is replaced by a
symlink/non-regular object.

Required property: no-follow descriptor checks reject it; verify prints no path
and publication does not follow it.

### 16.5 Concurrent publication

Threat: two publishers interleave files or one observes a stale lock pathname.

Required property: `fcntl` serialization, create-only snapshots, sealed reuse,
and one atomic regular selected record expose only complete states.

### 16.6 Incomplete activation

Threat: failure after some candidate files are written.

Required property: the old selected remains active until every candidate role is
sealed and durable. A candidate is never build authority.

### 16.7 Stale manifest acceptance

Threat: exact referenced input bytes change, while unrelated inputs also exist.

Required property: manifest-listed change makes verify stale; unlisted change is
unread and irrelevant.

### 16.8 Output-language injection

Threats: `${...}`, newline, `#`, `.include`, quote, backslash, JSON controls, C
controls, terminal ESC, oversized values.

Required property: each target grammar rejects or escapes independently and no
partial artifact is selected.

### 16.9 Expression exhaustion

Threat: excessive text, tokens, nesting, AST nodes, or dependency cycles.

Required property: limits are checked before recursive or multiplicative
resource growth, and failure produces a bounded diagnostic.

### 16.10 Input-graph exhaustion

Threat: excessive source edges, fragments, depth, bytes, duplicate/cyclic paths,
or case collisions.

Required property: deterministic fail-closed behavior with no partial catalog.

### 16.11 Numeric and enum corruption

Threat: signed overflow, hex-domain overflow, invalid range, duplicate enum,
wrong domain, or coercion.

Required property: native TOML type and declared domain are checked before value
construction; no truncation or coercion occurs.

### 16.12 Failed interactive edit

Threat: invalid value or failed save partially changes state or exits.

Required property: candidate validation is transactional; dirty/undo/value state
is preserved; failed save stays open; only explicit discard exits dirty state.

### 16.13 Terminal restoration and allocation

Threat: error/signal leaves raw mode, or hostile dimensions allocate excessive
memory.

Required property: idempotent cleanup and clamped 512 x 256 logical surface;
tiny terminals never enter raw mode.

### 16.14 Capability creep

Threat: a convenience feature adds subprocesses, hooks, directory scans,
environment selection, or consumer-specific exceptions.

Required property: compiled product import audit, explicit I/O ledger, poison
fixtures, and public API review fail the change. A new scanner is not an accepted
way to verify genericity.

## 17. Verification architecture

### 17.1 Allowed permanent evidence

- public-header and explicit source-manifest review;
- final product import/symbol-table analysis with clang toolchain tools;
- exact opened-path ledger through an injected host-I/O boundary;
- explicit valid, invalid, poison, race, concurrency, and injection fixtures;
- product-versus-test-only link-surface comparison;
- direct unit/integration/PTY binaries listed in the bmake manifest;
- strict compilation of a generated C header;
- generic bmake inclusion of `values.mk`;
- sanitizer and bounded fuzz execution over explicit corpora;
- manual terminal key-sequence review with binary identity and terminal size.

### 17.2 Forbidden verifier designs

A verifier must not:

- recursively find all TOML files;
- scan C, header, Makefile, make fragment, object, or linked image content;
- infer source membership from configuration-symbol uses;
- reverse engineer compiler invocations;
- inspect an external consumer checkout;
- make a lexical grep the sole semantic proof;
- link a source scanner into the product;
- add hidden proper-name exceptions or allowlists to pass a test.

An agent reviewing Confit's own explicit changed source is distinct from an
installed product or permanent test authority analyzing arbitrary consumer
source. Strong auditing cannot expand the product's responsibilities.

### 17.3 Evidence classes and non-substitution

| Evidence | Establishes | Does not establish |
| --- | --- | --- |
| Source review | Implementation shape and visible authority | Runtime behavior |
| Compile | Compiler acceptance for exact source/tool identity | Semantic correctness |
| Unit | Local behavior for executed cases | Full integration |
| Integration | Combined behavior for explicit fixtures | Arbitrary projects |
| Exact-I/O ledger | Observed opened paths in executed cases | Every possible host behavior |
| Import audit | Linked capability surface for exact binary | Absence of all logic bugs |
| Sanitizer | No observed memory/UB report in executed corpus | Universal memory safety |
| Bounded fuzz | No crash in bounded corpus/time | Proof for arbitrary input |
| PTY | Terminal I/O/restoration for executed cases | Human usability |
| Manual review | Human-observed key/layout behavior on one terminal | All-terminal certification |

No class substitutes for ordinary consumer build, execution, deployment, or
physical-system evidence. Those are outside Confit's product claim.

## 18. Architecture falsifiers

The schema 6 architecture is violated if any of these becomes true:

- core model code performs hosted I/O;
- the product imports a subprocess, directory-enumeration, or plugin-loading API;
- a path is reopened for hashing after parsing;
- project membership requires anything other than the entry and literal sources;
- a raw project source or Makefile is read for configuration validation;
- a dependency expression mutates a value;
- an emitter changes core type grammar;
- a verifier reruns the resolver or scans the project tree;
- an active snapshot can contain a partial or mixed generation;
- CLI selection depends on ambient environment state;
- UI rendering changes resolution semantics;
- a key decoder can execute an arbitrary command;
- a save failure causes exit or selected-state change;
- product/test source membership depends on wildcard or recursion;
- mandatory bootstrap uses an executable outside the closed set;
- tests pass only when an external project checkout is present.

Later strong-audit rounds must correct such a finding in code and add an
exploit-shaped regression. Recording a blocker without correction is not a pass.
If correction requires a new public field, type, command, external dependency,
or wider responsibility, implementation stops for an explicit contract decision.

## 19. Explicit non-claims

Even after all schema 6 rounds pass, Confit does not claim:

- that an older schema consumer has migrated;
- that an older schema's security issues are remediated in its pinned binary;
- compatibility with schema 5 input or output;
- correctness of a consumer's project source selection;
- success of compilation, linking, ordinary build, execution, emulation, or
  deployment;
- Windows-native terminal support;
- certification on every POSIX host, terminal, or filesystem;
- loadable-module semantics, a three-state bool, profiles, inheritance, choices,
  plugins, hooks, or schema editing;
- protection against every same-user denial-of-service or a compromised host;
- authenticity or signatures from SHA-256 membership digests;
- arbitrary-input safety from bounded sanitizer/fuzz evidence.

The release-candidate label, when later earned, applies only to this generic
configuration product and this closed contract.
