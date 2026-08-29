---
doc_type: audit
status: closed
authority: confit-v6-r21
audited_base_head: 7d0979d75d5b299b9104628dc6e127c089d0b1a1
schema_version: 6
---

# Confit v6 R21 security, bootstrap, and generic integration closure

## 1. Decision

R21 closed the implemented parser-to-TUI product plane against the hostile
path, exact-input, publication, output-language, terminal, bootstrap, and
direct-authoring requirements frozen in R01. No product Critical or High
security finding remains open in the executed scope:

| Severity | Open | Corrected during R21 |
| --- | ---: | ---: |
| Critical | 0 | 0 |
| High | 0 | 0 |
| Medium affecting R21 correctness | 0 | 3 |
| Low | 0 | 0 |

The three corrected Medium findings were defects in the new example/test
integration written during this round, not accepted pre-existing product
behavior. The first used the non-canonical macOS `/tmp` spelling despite the
product's no-symlink absolute-root policy. The second copied the conceptual
Makefile's `.SHELLSTATUS` check and then omitted one closing conditional while
making failed verification produce a single clear build error. The test now
canonicalizes its private fixture root, and the Makefile checks empty verified
artifact paths inside a fully closed conditional. The third left a `strcmp`
behind assertion macros whose terminating behavior the static analyzer cannot
prove; an explicit non-null guard now preserves both runtime and analysis
evidence. The focused generic test was rerun after each correction.

No TOML key, table, type, alias, CLI command, environment selector, consumer
exception, source discovery rule, or build meaning was added. The result is not
the R22 release-candidate decision.

## 2. Direct-authoring project

`examples/generic` is a complete handwritten project with these explicit
planes:

```text
examples/generic/
├── Confit.toml
├── config/
│   ├── runtime.toml
│   └── logging.toml
├── configs/
│   └── development.toml
├── Makefile
├── src/
│   ├── main.c
│   ├── metrics.c
│   └── poison.c
├── unrelated/
│   └── invalid.toml
└── build/
    └── poison.mk
```

The author writes ordinary schema-6 definitions and one small `[values]` file.
No owner, lifecycle metadata, placement, source object, compiler flag, link
edge, profile, or driver field exists. `Confit.toml` includes only
`config/runtime.toml`; that menu explicitly includes `config/logging.toml`.
The invalid TOML remains unreachable.

The handwritten bmake file owns its literal C source membership. It calls
`confit configure` only in the explicit `configure` or `menuconfig` targets.
For `all`, it calls `confit verify --print-artifact` for `values.mk` and
`values.h`, includes the verified Make projection, selects `src/metrics.c` from
`CONFIG_ENABLE_METRICS`, and invokes the caller-provided clang. The invalid
`src/poison.c` and `build/poison.mk` remain unobserved because neither Confit nor
the Makefile discovers directory members.

The dedicated `test_generic_project` C runner proves the following sequence:

1. a fresh pre-created configuration root and object root exist;
2. `bmake all` without a selected snapshot fails and does not create
   `selected`;
3. `bmake configure` publishes requested Make, C-header, and JSON projections;
4. `bmake all` verifies both consumed artifacts and compiles the explicit
   source set;
5. the resulting hosted program observes bool/int/hex/enum values and the
   selected metrics source;
6. changing the explicit external user config makes `all` fail stale without
   changing `selected` or running configure;
7. a second explicit configure followed by all rebuilds with metrics disabled;
8. the scaffold-free route works with only Confit, bmake, shell, clang, and the
   project files.

This is generic configuration-data consumption. The C program is not a kernel,
image, boot, emulation, deployment, or hardware result. Confit does not execute
bmake or clang and never opens the example's C or Make files.

## 3. Security and deterministic regression coverage

The normal literal-manifest suite owns 23 C binaries after adding the generic
project runner. Its executed hostile cases include:

- descriptor-rooted absolute and relative path validation;
- root, intermediate, final, candidate, destination, and selected symlink
  rejection;
- regular-file identity, size, growth, shrinkage, FIFO, socket, and device
  rejection;
- exclusive candidate collisions, advisory lock contention, atomic
  replacement, and complete concurrent writer outcomes;
- parse-once/hash-once behavior while the pathname is replaced;
- explicit source duplicate, cycle, depth, count, case identity, and poison
  ledger behavior;
- unknown field, excluded type, native scalar, numeric range, enum domain,
  dependency token/AST/type/cycle/no-auto-enable, and resolver-order behavior;
- unknown/stale user values and default-minimal serialization;
- immutable snapshot seal, exact manifest input, selected identity, failure
  preservation, concurrent publisher, relocation, and corrupt snapshot cases;
- Make closed literals, C string escaping, JSON control escaping, maximum
  output, requested-only projection, and compiled generated-header behavior;
- closed CLI option matrix, environment poison, stale verification, migration,
  and no implicit configuration;
- 5,000 deterministic pseudo-random UI actions, transactional invalid edits,
  bounded undo/redo, dirty state, and closed command parsing;
- PTY resize, escape decoding, failed save, EOF, `SIGINT`, `SIGTERM`, `SIGHUP`,
  and Vim-style save/exit separation.

The R21 repeated stress lane executed host, parse/hash input, and snapshot tests
32 times each; source graph, expression, emitter unit/integration, and UI model
tests 16 times each; and PTY plus generic configure/build tests eight times
each. This is a bounded one-host race regression, not a proof against every
same-UID denial of service or every power-loss schedule.

## 4. Sanitizer and fuzz evidence

A fresh Apple clang 21 object root rebuilt the product and all 23 direct C test
binaries with AddressSanitizer and UndefinedBehaviorSanitizer. Every binary ran
to completion with `UBSAN_OPTIONS=halt_on_error=1`. The macOS run used
`ASAN_OPTIONS=detect_leaks=0`; leak freedom is not claimed. The sanitized
product SHA-256 was:

```text
697bcc4a9b356f4c713d0f623cca75422da57c67dc02a1168a730355c0d06f17
```

The normal deterministic fuzz binaries each retain their fixed corpus and
2,048 generated cases. R21 additionally made those same source files usable as
real `LLVMFuzzerTestOneInput` targets under a private compile define. Homebrew
clang 20.1.8, whose resource directory actually includes libFuzzer, ran 2,048
bounded mutations for TOML and 2,048 for dependency expressions with
`max_len=4096` and fixed seeds 2101 and 2102. Both completed with no crash. The
target digests were:

```text
TOML       b7645275405066bcb7e26310a9f7371f1898e5f84ff6fea4684b7586cad10143
expression ff7e53f3d611e27825cff4c7f42026e9e69403a41a5231881d7e9cb303d2fbc4
```

An initial Apple clang link attempt correctly failed because that Xcode
installation lacked `libclang_rt.fuzzer_osx.a`; it was not reported as a fuzz
pass. An initial Homebrew clang build combining ASan and libFuzzer stalled in
`AsanInitInternal` before reaching Confit code. A live stack sample established
the tool-runtime boundary. The accepted lanes therefore use Apple clang for the
full ASan/UBSan suite and Homebrew clang for libFuzzer alone. Sanitizer and
bounded fuzz are complementary observations, not arbitrary-input safety proof.

## 5. Bootstrap and compiled capability audit

The fresh direct-bmake bootstrap used a pre-created empty object root and these
explicit identities:

```text
product/test compiler  Apple clang 21.0.0
fuzz compiler          Homebrew clang 20.1.8
bmake                  20260714
shell                  /bin/sh
```

The normal graph still accepts only the existing object root, clang, bmake,
and shell parameters. Product, test, and fuzz membership are literal; no
wildcard or recursive discovery was added. Python, Perl, Ruby, CMake, Ninja,
Meson, GNU Make, ncurses, pkg-config, parser generator, external TOML/JSON
processor, template engine, or external hash program participates in the
mandatory build or runtime path.

The final normal product SHA-256 at the audited base was:

```text
7ef24023f61329773c1a1ae4f9db2fd1713f07d486127e88de66a7d0cad9cf42
```

Compiled undefined-import review found zero product import from the forbidden
families:

```text
system popen fork vfork exec* posix_spawn*
opendir readdir scandir glob nftw fts_*
dlopen
```

The product retains only its explicit configuration/terminal capabilities,
including descriptor-rooted file operations, standard streams, `poll`,
`ioctl`, termios, and signal handling. Separate test runners import `_fork`,
`_execv`, `_waitpid`, `_opendir`, and `_readdir` for process supervision and
private fixture cleanup. Those support objects are not linked into the product.

## 6. Exact-read and verifier boundary

The source-graph poison test's bounded internal ledger observes exactly the
entry and its reachable literal fragment sequence. The new end-to-end example
also succeeds while an invalid unreferenced TOML, invalid C source, and failing
make fragment remain present. Snapshot verification reads `selected`, the exact
sealed roles, and manifest-listed inputs. It does not rerun the resolver, scan a
directory, search source text, inspect Makefiles, infer object membership, or
invoke the consumer build.

Permanent tests have explicit source membership. Test-support recursive cleanup
is limited to a private tree created by that test process and is not product or
verification authority. No recursive raw-project verifier was introduced.

## 7. Non-claims and R22 handoff

R21 establishes compile, unit, integration, sanitizer, bounded fuzz, PTY, and
generic hosted-build consumption evidence on one macOS host. It does not
establish:

- arbitrary-input, power-loss, all-host, terminal, or accessibility
  certification;
- LUCA, Delos, or any consumer migration;
- repair of active schema-5 consumers;
- kernel build, image generation, boot, QEMU, deployment, or hardware success;
- correspondence between a config symbol and arbitrary C source;
- build-graph, compiler-feature, linker, or ABI analysis by Confit;
- a schema-6 release candidate.

R22 must independently rerun the final adversarial audit against its own fresh
binary. This R21 pass is regression-selection input, not R22 success authority.
