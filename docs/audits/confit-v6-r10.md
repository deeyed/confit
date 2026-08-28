---
doc_type: audit
status: closed
authority: confit-v6-r10
audited_base_head: 5fb7342d189d447d7137a2811e701cc7efad3353
schema_version: 6
---

# Confit v6 R10 mid-program adversarial audit

## 1. Decision

R01 through R09 were audited as one cumulative product plane: tracked
contracts, public headers, literal build manifest, final linked product, host
I/O, exact input ownership, explicit source graph, closed schema, and five typed
declarations. Three findings were found and corrected in R10. After correction,
the R10 release-blocking totals are:

| Severity | Open | Closed |
| --- | ---: | ---: |
| Critical | 0 | 0 |
| High | 0 | 1 |
| Medium affecting correctness, security, or genericity | 0 | 2 |
| Low | 0 | 0 |

No new TOML key, table, type, alias, CLI option, consumer exception, source
discovery rule, or build meaning was introduced. R11 remains the first round
allowed to implement dependency expressions.

## 2. Audited authority and scope

The public syntax authority was `docs/config-v6.md`. The implementation scope
was the literal product and test membership in `share/mk/confit.sources.mk`, not
a discovered directory listing. Every public header under `include/confit` was
read directly. The current product objects and final Mach-O executable were
then checked separately from test-support objects.

The audit did not inspect any consumer checkout. It did not infer build
dependencies from C, Makefiles, objects, or compiler commands. Agent-side
lexical searches were only navigation aids; accepted/rejected fixture behavior,
compiled imports, object symbols, and the source-graph read ledger are the
behavioral evidence.

## 3. Finding ledger

| ID | Severity | Boundary | Reproduction | Root cause | Correction | Regression | Status |
| --- | --- | --- | --- | --- | --- | --- | --- |
| R10-001 | High | file-read | Link the R09 product and inspect undefined imports; `_fopen`, `_fread`, `_fclose`, `_feof`, and `_ferror` were present even though Confit used only `toml_parse_named`. | The entire vendored tomlc17 translation unit exported optional pathname and `FILE *` parsers, so unused file-I/O code remained in the final binary. | Added the vendor-private `TOMLC17_NO_FILE_IO` compile mode and excluded all three file parser entry points. The memory parser and upstream license/provenance remain. | Rebuilt the product and inspected the final undefined and global symbol tables. File-parser symbols and file-I/O imports are absent; TOML, input, schema, fuzz, and aggregate tests pass. | closed |
| R10-002 | Medium | parser-hash | The R09 hex spelling helper was declared in the public TOML header and accepted an unrelated document/value pair, using the value's line and column against the wrong byte image. | A schema-only lexical distinction was exposed as generic public parser API without document ownership validation. | Moved the enum and helper declaration to `toml_internal.h`, renamed the seam to `confit_toml_integer_base_from_image`, and require the value's parser-owned source identity to match the document root. | The TOML adapter test passes all four native integer bases and now verifies that two separately parsed documents carrying the same source-name text cannot exchange value handles. The full type suite passes. | closed |
| R10-003 | Medium | syntax | Direct construction through the public catalog model accepted newline-bearing `dependency_text`, while the schema loader rejected the same value and the contracted expression surface is single-line bounded text. | The core model copied dependency text with its multiline-help policy instead of the dependency policy. | Changed the core copy policy to reject layout/control bytes and retained expression parsing for R11. Added early catalog count checks so one-over input fails before allocating and copying another candidate. | Model tests now cover exact and one-over dependency text, newline rejection, exact prompt/help size, and exact plus one-over fragment, menu, and config counts. Schema and type integration remain green. | closed |

The first focused rebuild after R10-001 intentionally removed the transitive
`stdio.h` include from the memory-only vendor header and exposed that the header
also needed `size_t`. Adding the correct direct `stddef.h` dependency fixed that
compile-time authoring defect. No product binary from that failed build existed,
and the owning focused gate was rerun before aggregate validation.

## 4. Syntax and user-authored surface

The implemented parser and fixtures were compared against the contract matrix:

- entry keys are exactly `schema_version`, `mainmenu`, and `source`;
- menu keys are exactly `prompt`, `help`, and `source`;
- config keys are exactly `symbol`, `type`, `prompt`, `help`, `default`,
  `depends_on`, `values`, and `range`;
- user-file keys are exactly `schema_version` and optional `values`;
- accepted declaration types are exactly `bool`, `int`, `hex`, `string`, and
  `enum`.

The structural suite rejects every recorded v5/metadata field, unknown entry,
menu, config, and user key, duplicate TOML key, duplicate symbol, wrong table
shape, and menu-depth violation. The type suite rejects excluded types,
cross-type coercion, wrong field applicability, invalid native hex spelling,
invalid ranges, invalid enum domains, unsafe strings, and values beyond the
declared bounds.

A bool declaration with no `default` resolves in the typed catalog to false.
No `owner`, lifecycle metadata, placement, build dependency, or explicit
`OPTION = false` filler is required. Configuration-only authors still provide
the useful Kconfig-like identity fields: symbol, type, prompt, and help.

## 5. Genericity and build separation

Direct review found no consumer, operating-system, architecture, target,
profile, kernel, driver, source-file, object, link, or image meaning in the
public C model. Occurrences of excluded nouns in schema documents and tests are
rejection lists, not accepted fields. The source graph stores configuration
fragment path, parent presentation node, ordinal, depth, and exact input image;
it stores no C source, object, rule, compiler, linker, or ordinary-build edge.

The final product source manifest contains no v5 schema, v5 resolver, v5
generator, v5 workflow, process launcher, admission layer, compatibility
dispatcher, or dual parser. CLI commands remain a closed development skeleton:
only `help` and `--version` succeed, and configuration commands perform no file
I/O until their owning later rounds implement them.

The Make-style words in the future emitter contract have not entered the core
value grammar. Current canonical values are length-framed generic data, not
Make, C, JSON, shell, or build declarations.

## 6. File-read and process capability evidence

The post-correction final product has no undefined import from these families:

```text
system popen fork vfork exec* posix_spawn*
opendir readdir scandir glob nftw fts_*
dlopen
fopen fread fclose feof ferror
```

The product retains direct descriptor operations such as `open`, `openat`,
`pread`, `fstatat`, `renameat`, and `fsync`; these are the reviewed
descriptor-rooted host capabilities required for explicit configuration input
and later output publication. Test-only support separately imports process and
directory APIs for CLI supervision and bounded temporary-fixture cleanup. Those
objects are not members of the product link.

The source-graph poison fixture creates an entry, one reachable TOML fragment,
an unrelated invalid TOML file, a C source, a Makefile, and a make fragment. Its
fixed-capacity internal read ledger records exactly the entry and reachable
fragment in stable order. None of the poison paths becomes a graph node or
read record. Manual call-edge review confirms graph loading reaches host I/O
only through one `confit_input_load_toml` call for the explicit entry and each
validated literal source edge; it performs no directory enumeration or glob.

## 7. Input, host, and bound evidence

The controlled replacement test opens and reads one initial regular file,
replaces its pathname, and constructs parse tree and SHA-256 identity from the
already-read buffer. The first image remains internally consistent while a
later independent explicit load observes replacement bytes. Parser and digest
never reopen the path.

The host suite covers invalid and traversing paths, symlink roots/components,
non-regular FIFO/socket/device inputs, overlarge and changed inputs, occupied
candidate names, unsafe replacement destinations, advisory-lock contention,
and concurrent complete-image replacement. It uses descriptor-relative
no-follow opens, exclusive candidate creation, exact writes, and fsync/rename
ordering.

Bounds already owned by R01-R09 have exact-limit and one-over coverage in their
owning tests: path, file bytes, aggregate bytes, fragment/edge count arithmetic,
include depth, visible menu depth, prompt/help/string size, enum count/atom size,
dependency text storage, numeric/hex range, and catalog growth. Expression AST,
diagnostic collection, and render-surface limits are declared centrally but
their consumers do not exist before R11, later diagnostic aggregation, and
R18-R20; R10 does not pretend those future planes were exercised.

## 8. Bootstrap and verifier boundary

The bmake graph takes only four explicit public parameters: pre-existing object
root, clang, bmake, and shell. Source and test membership are literal. Normal
targets invoke no source/test discovery, Python, CMake, Ninja, ncurses,
parser-generator, external TOML processor, external hash program, `mkdir`, or
`rm`. The vendored memory-only define is fixed inside the reviewed build graph
and is not a user option.

The permanent tests use explicit fixture paths. They do not recursively scan
project TOML, C, headers, Makefiles, or objects, and they do not infer consumer
relationships. Test-support directory traversal is limited to removing the
temporary directory tree created by that same C test process; it is not product
membership or verification authority.

## 9. Executed evidence

The authoritative R10 runs used `/usr/bin/clang`,
`/opt/homebrew/bin/bmake`, and `/bin/sh` with pre-created object roots under
`/private/tmp`.

- Focused: TOML adapter, typed schema, and public-header tests passed after both
  corrections.
- Aggregate: fresh `all check-host` in
  `/private/tmp/confit-r10-final-5fb7342` built the product and twelve literal C
  test binaries and completed successfully. The managed sandbox denied an
  earlier run of the existing pathname Unix-domain socket fixture; the final
  fresh run completed outside that restriction.
- Sanitizer: eleven semantic/host/integration/fuzz binaries were compiled
  directly from the same literal product and test-support sources with clang
  AddressSanitizer and UndefinedBehaviorSanitizer and passed. Apple
  AddressSanitizer on this host rejects leak detection, so the accepted run used
  `detect_leaks=0`; leak freedom is not claimed. Earlier command-construction,
  unsupported-option, and literal-path mistakes did not complete the suite and
  were not accepted as evidence. The corrected invocation reran all eleven from
  the start. The CLI supervisor remained covered by the normal aggregate lane
  rather than being represented as sanitized.
- Static analysis: every first-party product translation unit and the CLI unit
  were analyzed individually by clang with no finding.
- Parser fuzz regression: the fixed corpus plus 2,048 deterministic generated
  inputs ran through the aggregate and sanitizer lanes.
- Import/symbol audit: the final product contains no file parser symbol and no
  forbidden process, directory-enumeration, dynamic-loading, or stdio file-read
  import. Its SHA-256 is
  `2d2307ff075f79dc45f70dc3426a1447bc36478173ab85e9b3643c7befa75b28`.

The final tool identities were Apple clang 21.0.0 and bmake 20260714. Tool
versions and artifact digests identify this evidence; they are not a portability
claim.

These evidence classes establish the implemented configuration plane on this
host. They do not establish dependency evaluation, resolution, serialization,
snapshot durability, emitters, full CLI, TUI, consumer build, boot, emulation,
hardware operation, arbitrary-input safety, power-loss certification, or
all-host portability.

## 10. Remote isolation and remaining non-claims

At R10 preflight, local HEAD, upstream, and `origin/codex/confit-v6` were all
`5fb7342d189d447d7137a2811e701cc7efad3353`. The read-only remote identities
were still:

```text
origin/main c9ae9b2102199e9edccacdb7ff345c25b8dc8c9d
origin/codex/bake-proto-config-v4-production bdb37296695742e8fcbdbc2b980ee4f31587c024
```

R10 changes are confined to the v6 branch. No tag or merge is part of this
round. Completion of R10 does not migrate or repair any active schema-5
consumer. The release line remains `0.7.0-schema6-dev`; `1.0.0-rc1` is reserved
for a successful R22 audit.
