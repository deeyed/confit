---
doc_type: contract
status: active
authority: confit-v6-bootstrap
depends_on:
  - docs/architecture-v6.md
  - docs/r02-clean-schema6-line.md
---

# Confit v6 bootstrap contract

This document closes only the local build and C-test regeneration boundary. It
does not claim schema 6 parser, resolver, snapshot, emitter, TUI, installation,
consumer build, or all-host support.

## Preconditions

The caller provides all four build parameters on the bmake command line:

- `CONFIT_OBJROOT`: an existing, writable, canonical absolute directory outside
  the source tree;
- `CONFIT_HOST_CC`: one existing absolute clang executable;
- `CONFIT_BMAKE_TOOL`: the absolute bmake identity that is executing the graph;
- `CONFIT_SHELL`: one existing absolute shell executable.

The build graph rejects `bmake -e`, missing or relative parameters, a bmake
identity different from the running executable, an object root inside the source
tree, and command-line attempts to override source lists, warning flags, link
inputs, output names, or other internal variables. A forced compiler header also
rejects a compiler that does not define clang and C17 identities.

Creating and provisioning the object root precedes this claim. It is not a
mandatory Confit target. Given that root, ordinary build and test targets do not
execute external `mkdir`, `rm`, source discovery, test discovery, or hash tools.

## Exact invocation

On the currently verified host, the product-only build is:

```sh
/opt/homebrew/bin/bmake \
  CONFIT_OBJROOT=/private/tmp/confit-v6-bootstrap-product \
  CONFIT_HOST_CC=/usr/bin/clang \
  CONFIT_BMAKE_TOOL=/opt/homebrew/bin/bmake \
  CONFIT_SHELL=/bin/sh \
  all
```

The aggregate direct-C-test gate is:

```sh
/opt/homebrew/bin/bmake \
  CONFIT_OBJROOT=/private/tmp/confit-v6-bootstrap-check \
  CONFIT_HOST_CC=/usr/bin/clang \
  CONFIT_BMAKE_TOOL=/opt/homebrew/bin/bmake \
  CONFIT_SHELL=/bin/sh \
  check-host
```

Both object roots must already exist. A verification run should use new empty
roots so stale files cannot substitute for compilation. There is intentionally no
destructive `clean` target; provisioning a fresh root is the clean-room gate.

## Explicit graph and flat outputs

`share/mk/confit.sources.mk` literally lists product, CLI, test-support, unit,
integration, PTY, and fuzz translation units. Empty categories are still defined
explicitly. The graph rejects duplicate, missing, absolute, non-C, wildcard-like,
or unsafe listed paths. It does not enumerate the source or test directories.

Every output is a direct child of `CONFIT_OBJROOT`. Source slashes are replaced
with underscores in object and dependency names, and collisions are rejected at
parse time. Examples are:

```text
confit
src_core_digest.o
src_core_digest.d
tests_unit_test_digest.o
confit_test_digest
```

Flattening means compilation needs no nested output directory creator. First-
party code uses C17 with `-Wall -Wextra -Werror -pedantic`. Vendored tomlc17 uses
its separately reviewed warning policy, while the same clang/C17 contract remains
mandatory. Linking is performed through the provisioned clang driver.

## Executable boundary

The mandatory observed set is limited to:

- the explicitly invoked bmake;
- the explicitly selected shell used by bmake recipes;
- the explicitly selected clang driver and its clang toolchain linker components;
- the newly built Confit product for CLI smoke checks;
- the newly built Confit-owned C test binaries.

Test-only C support may use direct host APIs to create and remove bounded
temporary fixtures and to supervise the product CLI. Those APIs are not linked
into the product binary. Git closeout, queue character counting, and preparation
of the pre-existing object directory are outside the product/test bootstrap
claim.

## Evidence and non-claims

The R03 evidence class is fresh compilation, direct C-test execution, explicit
manifest review, verbose recipe observation, and compiled product import review.
It does not establish configuration semantics, arbitrary-project support,
consumer build success, execution, boot, emulation, deployment, or hardware
behavior. A successful local run is not an installation or all-host portability
claim.
