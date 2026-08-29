---
doc_type: audit
status: closed
authority: confit-v6-r22
audited_parent_head: 156e47f8a1602e2ac0e55bba0a4cfff29a5eed39
product_version: 1.0.0-rc1
schema_version: 6
verified_at: 2026-08-29T11:21:18+09:00
---

# Confit v6 R22 final adversarial audit

## 1. Decision

R22 compared the complete tracked schema 6 product against the frozen R01
configuration and architecture contracts and closed the `1.0.0-rc1` release
candidate. The containing commit is the R22 release commit. Its exact local and
remote identity is recorded after push in the untracked single-writer command
queue, because a commit cannot truthfully contain its own hash.

No public TOML key, table, type, alias, dependency operator, CLI command, path
selector, source-discovery behavior, environment selector, consumer exception,
or build meaning was added during this audit.

| Severity / boundary | Open | Corrected in R22 |
| --- | ---: | ---: |
| Critical | 0 | 0 |
| High | 0 | 0 |
| Medium affecting release correctness or audit authority | 0 | 3 |
| Unapproved public syntax | 0 | 0 |
| Consumer-specific product API | 0 | 0 |
| Product subprocess or directory-enumeration capability | 0 | 0 |
| Blocking manual/PTY TUI defect | 0 | 0 |

## 2. Corrected findings

### R22-F01: unused inherited diagnostic repair transport

Severity: Medium. Status: corrected.

The public diagnostic header still carried an inherited
`ConfitDiagnosticFixCandidate` with `option_id`, value text, related spans,
machine code, severity, and a detailed setter. No schema 6 product or test used
that transport. It exposed a repair/option vocabulary that schema 6 neither
specified nor implemented and was exactly the kind of temporary public
structure that can become accidental API.

R22 removed the unused candidate, related-span, code, severity, counters, and
detailed setter. The retained diagnostic is the minimal generic status,
path/line/column, message, and bounded stable-path storage actually consumed by
the product. This removes public surface; it does not add or reinterpret syntax.

### R22-F02: stale conceptual bmake status example

Severity: Medium. Status: corrected.

The normative configuration contract still showed a conceptual Makefile using
the global `.SHELLSTATUS`, although R21 had already corrected the tracked
runnable example. That form becomes ambiguous as soon as a consumer captures
more than one verified artifact, and an unsuppressed failed shell assignment can
obscure the intended single configuration error. It also made the contract
diverge from the direct-authoring evidence.

The contract now makes failed `verify --print-artifact` yield an empty value,
tests only that value, and includes it solely in the non-empty branch. Confit
still does not parse or own the Makefile. No Make semantics entered the product.

### R22-F03: analyzer-opaque test assertions

Severity: Medium to audit authority. Status: corrected.

The runtime test assertion function exits on failure, but clang's path-sensitive
analyzer could not infer that a successful call proved its expression. Product
translation units were warning-free; test translation units consequently
reported false possible-null and invalid-descriptor continuations. Leaving those
warnings would weaken the claim that the final verifier itself was clean.

The test-only assertion macro now branches visibly to a standard `_Noreturn`
failure function. No product object or public product header depends on it.
Static analysis was rerun over every first-party translation unit from the
literal manifest and completed with zero diagnostic.

## 3. Frozen public surface comparison

The loader's accepted-key sets match the contract exactly:

| Shape | Accepted names |
| --- | --- |
| entry | `schema_version`, `mainmenu`, `source` |
| fragment top level | `menu`, `config` |
| menu | `prompt`, `help`, `source` |
| config | `symbol`, `type`, `prompt`, `help`, `default`, `depends_on`, `values`, `range` |
| range | `min`, `max` |
| user document | `schema_version`, `values` |

The implemented value kinds are exactly bool, int, hex, string, and enum.
Unknown fields/tables, legacy fields, aliases, duplicate symbols, excluded
types, inapplicable fields, invalid native scalar kinds, bad range/domain, and
stale user assignments are fail-closed regression cases. Default omission is
type-defined; users do not provide filler assignments.

The dependency grammar remains only `!`, `&&`, `||`, `==`, `!=`, parentheses,
symbols, and bounded literals. Unknown symbols, invalid comparisons, excessive
text/nodes/nesting, self/multi-symbol cycles, and unavailable explicit
non-default values are errors. Evaluation controls availability only. It cannot
enable another option, change source membership, inspect a file, or invoke a
probe.

The public CLI contains only `help`, `--version`, `check`, `configure`,
`menuconfig`, `verify`, `search`, `explain`, `diff`, `listnewconfig`,
`oldconfig`, `olddefconfig`, and `savedefconfig`, with the closed option matrix
in the contract. Environment poison for historical project selectors does not
change selection. The binary imports no `getenv`.

Public product identifiers and exported symbols were inspected for consumer,
kernel, build-system, placement, and legacy command nouns. No consumer-specific
product API remains. The local variable word `owner` denotes ordinary memory or
value ownership only and is not a configuration field.

## 4. Source visibility and exact-read authority

The source graph opens one CLI-selected entry and follows only normalized
literal `source` paths. The source-graph read ledger observes the exact entry
and reachable fragments. Its fixture leaves an invalid sibling TOML, poison C
source, Makefile, and poison make fragment present and unobserved. Duplicate,
cycle, depth, edge/count, traversal, symlink, non-regular, identity, and
case-collision cases are bounded and fail closed.

Input parsing and SHA-256 use one owned byte image. The pathname-replacement
race test proves the parsed bytes and recorded digest do not split across two
opens. Snapshot verification does not parse or resolve. It reads the bounded
regular `selected` record, exact sealed roles, and manifest-listed configuration
inputs. Its read ledger excludes siblings and consumer source.

The permanent tests do not recursively scan a project for semantic authority.
Test-support directory enumeration exists only to clean a private fixture tree
created by the same test process. Process and directory imports in the PTY and
integration runners remain test-only and are absent from the product link.

## 5. Publication, emitter, and hostile-data result

Host and snapshot regression covers absolute/relative normalization,
intermediate/final/candidate/selected symlinks, FIFO/socket/device rejection,
file growth/shrink and identity, exclusive candidates, atomic replacement,
advisory lock contention, concurrent writers, complete-before-select ordering,
seal and manifest corruption, relocation, stale inputs, and failure
preservation.

Emitter regression covers `${...}`, `#`, `.include`, newlines, quotes,
backslashes, C controls, JSON controls, overlong text, enum/domain limits, and
requested-only output. Make accepts only bool, int, hex, and enum closed
literals; arbitrary string makes the request fail. Generated C headers are
compiled as strict C17. Every artifact is sealed before selection.

## 6. TUI and terminal result

The normal aggregate includes pure model tests with 5,000 deterministic
pseudo-random actions and a C `forkpty` suite. R22 also drove the release binary
through two direct TTY sessions rather than relying only on the harness.

In the successful-output session:

- plain `q` displayed `use :q, :wq, or :q!` and did not exit;
- Escape cleared transient state and remained in NORMAL;
- an invalid bool change that would strand a non-default dependent enum was
  rejected without changing the working value;
- the enum picker changed `verbose` to default `normal` transactionally;
- Space then changed the bool to false and marked the session modified;
- dirty `:q` was rejected with the working state intact;
- `:!` was rejected as an unknown command;
- `:w` published, cleared dirty state, and kept the session active;
- clean `:q` exited and restored cursor/alternate-screen state.

In a read-only-output session, dirty `:wq` failed publication and left the UI
active and modified. Only the following explicit `:q!` discarded and exited.
The PTY suite independently covers `:x`, successful `:wq`, repeated Escape,
typed invalid edits, search/help/diff, narrow/split resize, 39x10 refusal,
truncated escape input, EOF, and terminal restoration for SIGINT, SIGTERM, and
SIGHUP. SIGKILL recovery, universal terminal compatibility, grapheme shaping,
mouse input, localization, and accessibility certification remain nonclaims.

## 7. Compile, sanitizer, fuzz, analysis, and stress evidence

The final normal object root built with Apple clang 21.0.0, strict C17,
`-Wall -Wextra -Werror -pedantic`, bmake 20260714, and `/bin/sh`. The literal
manifest contains 19 product, two CLI, three test-support, and 23 test
translation units. All 23 direct test binaries passed. The product digest was:

```text
7c0921b768d224741eb443feb0260f670cc171133e44c06b926d93c93be8ae18
```

A second full build compiled the product and all 23 binaries with Apple clang
AddressSanitizer and UndefinedBehaviorSanitizer. Every binary passed with
`UBSAN_OPTIONS=halt_on_error=1` and macOS-compatible
`ASAN_OPTIONS=detect_leaks=0`. Leak freedom is not claimed. The sanitized
product digest was:

```text
35303da9c6721ffffde1a94d97c5dc8de7b0c89841149e71c0b6364e0ff9c40e
```

Homebrew clang 20.1.8, whose resource directory contains libFuzzer, built the
same current parser and expression harnesses. Fixed seeds 2201 and 2202 each ran
2,048 bounded mutations with `max_len=4096` and no crash. Digests were:

```text
TOML       0e377585d58bc235791dd640b435832c4d0da7444634fe5dda72542931854c55
expression 8f4486fc23d34c47933c1808640a4ad52db551e2ab509088460bb604dffc913c
```

Apple clang could not link libFuzzer because the installed Xcode runtime lacked
`libclang_rt.fuzzer_osx.a`; that attempt is not evidence. As already observed in
R21, combining this host's Homebrew libFuzzer and ASan did not reach a bounded
completion, so that private run was terminated. The accepted evidence keeps the
full Apple ASan/UBSan lane separate from the Homebrew libFuzzer lane. Missing
external symbolizer warnings did not affect exit status or target execution and
are not represented as symbolized-crash evidence.

Clang static analysis ran over every first-party product, CLI, support, unit,
integration, PTY, and fuzz translation unit listed by bmake, excluding only the
vendored TOML implementation. After R22-F03 it emitted no diagnostic.

The repeated one-host stress lane ran host, parse/hash input, and snapshot tests
32 times each; source graph, expression, emitter unit/integration, and UI model
tests 16 times each; and PTY plus direct generic configure/build tests eight
times each. This is bounded race regression, not all-schedule or power-loss
proof.

## 8. Compiled capability and bootstrap audit

The final product links only `/usr/lib/libSystem.B.dylib`. Undefined-import
inspection found zero member of:

```text
system popen fork vfork exec* posix_spawn*
opendir readdir scandir glob nftw fts_* dlopen getenv
```

The separately linked PTY runner imports `_fork`, `_forkpty`, `_execv`,
`_waitpid`, `_kill`, `_opendir`, and `_readdir` for supervision and private
fixture lifecycle. None is in the product binary. Product strings and exported
symbols contain no LUCA, Delos, Parus, Bake, or KERNCONF exception.

A newly created empty object root rebuilt the product and all C test binaries
using only the explicit clang, bmake, shell, literal source manifest, and source
tree. No wildcard, recursive discovery, generated parser, Python, Perl, Ruby,
CMake, Ninja, Meson, GNU Make, ncurses, pkg-config, external TOML/JSON processor,
template engine, plugin, or network operation is in the mandatory build/runtime
path. Agent audit utilities and Git closeout are evidence tools, not product
bootstrap dependencies.

## 9. Direct-authoring generic integration

The `examples/generic` fixture remains handwritten and scaffold-free. Its
end-to-end test proves missing configuration makes ordinary `all` fail without
implicitly configuring; explicit `configure` publishes Make/C/JSON projections;
`all` verifies sealed Make and header paths before including them; changed user
input makes the build stop stale; and a second configure selects the changed
typed values. Invalid unreferenced TOML, C, and make files remain present.

The resulting hosted example is evidence that an ordinary project can consume
configuration data. It is not evidence that Confit analyzed or built the C
source, and it is not kernel, image, boot, emulator, deployment, or hardware
evidence.

## 10. Branch isolation and closeout boundary

R22 began with local HEAD, upstream, and `origin/codex/confit-v6` at
`156e47f8a1602e2ac0e55bba0a4cfff29a5eed39`, exactly 21 ordered round commits
after base `bdb37296695742e8fcbdbc2b980ee4f31587c024`. Preflight confirmed:

```text
origin/main c9ae9b2102199e9edccacdb7ff345c25b8dc8c9d
origin/codex/bake-proto-config-v4-production bdb37296695742e8fcbdbc2b980ee4f31587c024
```

No tag matching schema 6, v6, 1.0.0, or rc1 existed. R22 does not create a tag,
merge, rebase, amend, consumer pin, or commit outside `codex/confit-v6`. The
single queue file remains untracked and absent from history. Temporary analyzer
and fuzz outputs accidentally written under the checkout were enumerated as
untracked, removed, and never staged; tracked source and corpus identity remained
under Git review.

This R22 turn did not read, modify, stage, commit, or push the LUCA, Delos, or
Bake checkouts. Completion of this release candidate does not migrate those
consumers or repair their currently pinned schema 5 behavior.

The containing commit is pushed only to `origin/codex/confit-v6`. Final remote
SHA equality, unchanged protected refs, exact 22-round commit count, zero tag,
and queue absence are post-commit predicates recorded by the untracked queue.

## 11. Final nonclaims

R22 establishes a one-host release-candidate result across source review,
strict compile, unit/integration, exact-read ledger, hostile fixture, sanitizer,
bounded fuzz, static analysis, repeated stress, PTY, direct terminal review,
compiled imports, and generic hosted consumption. It does not establish:

- arbitrary-input or formal memory-safety proof;
- all-filesystem, all-terminal, all-locale, or all-host portability;
- every concurrent schedule, crash-consistency, or power-loss certification;
- SIGKILL terminal recovery or accessibility certification;
- compatibility with or automatic migration from schema 5;
- correctness of a consumer's source membership, Makefile, compiler flags,
  linker graph, ABI, image, boot path, deployment, or hardware;
- completion of any consumer repository's schema 6 migration.

Within the accepted schema 6 boundary, the final counts are Critical 0, High 0,
release-affecting unresolved Medium 0, unapproved syntax 0, product source-tree
traversal 0, product subprocess 0, external mandatory dependency violation 0,
and blocking TUI defect 0.
