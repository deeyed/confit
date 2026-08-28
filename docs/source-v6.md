---
doc_type: implementation-contract
status: active
authority: confit-v6-source-graph
depends_on:
  - docs/config-v6.md
  - docs/architecture-v6.md
  - docs/model-v6.md
  - docs/host-v6.md
  - docs/input-v6.md
---

# Confit v6 explicit source graph

This document defines the implemented R07 configuration-document membership
boundary.  It does not claim that the complete entry, menu, config, user-value,
type, dependency, resolver, snapshot, emitter, CLI workflow, or TUI semantics
are implemented.

## 1. Explicit roots and edges

`confit_source_graph_load` receives one already-opened project-root capability
and one normalized project-root-relative entry path.  It opens that entry and
follows exactly two source carriers:

- the entry document's required top-level `source` array;
- each reachable fragment's optional `[menu].source` array.

This narrow carrier parsing exists only to decide graph membership before R08
validates the complete schema.  R07 does not accept aliases, interpret config
declarations, validate menu prompts, or assign configuration values.  A
fragment without a menu contributes no further edges at this layer; R08 may
later reject an otherwise empty or malformed fragment.

Every literal path remains relative to the project root.  It is never resolved
relative to the including fragment.  The path must pass the descriptor-rooted
host validator and end in lowercase `.toml`.  Absolute paths, empty or dot
segments, repeated slash, backslash, glob syntax, controls, symlink components,
and non-regular final objects fail closed.

## 2. Traversal and presentation

The graph uses bounded depth-first preorder.  Literal source-array order
therefore determines node presentation order and `source_ordinal`, but it never
creates value, override, assignment, or build precedence.  Each node retains:

- the exact `ConfitInputImage` from R06;
- one parent node or `CONFIT_INDEX_NONE` for the entry;
- its ordinal in the parent's literal source array;
- include depth, with the entry at depth zero.

The public graph exposes borrowed node views, exact node and edge counts, and
the cumulative exact input byte count.  It retains the parsed documents so R08
can validate schema without reopening any path.

## 3. Duplicate, cycle, and filesystem identity

Node state follows `unseen -> visiting -> visited`.  A literal edge to a
visiting path is an include cycle.  An edge to a visited path is a duplicate
include.  Both are errors because one fragment has exactly one presentation
parent.  The loader checks a path before opening it, so an exact duplicate is
not read a second time.

After opening a new spelling, the loader compares the captured regular-file
device and inode with every prior node.  Two distinct path strings that resolve
to the same file are rejected.  This covers case-fold collisions on hosts where
the spellings alias and also rejects hard-link aliases that would undermine
single-node identity.  Equal contents at distinct regular-file identities are
not themselves duplicates; each image keeps its own path, identity, and digest.

## 4. Bounds and failure publication

The loader applies the central public ceilings before addition or allocation:

- 4,096 total graph nodes, including the entry;
- 16,384 literal source edges;
- include depth 64 beyond the entry;
- 64 MiB cumulative exact input bytes;
- 1 MiB and 1,024-byte file/path limits inherited from R06 and R05.

An entire source array is preflighted against remaining edge and fragment
capacity before any of its children are opened.  Cumulative bytes use the R06
overflow-safe accounting hook.  A failure destroys every owned input image and
node allocation and publishes no partial graph.

R07 diagnostics are anchored to the caller-owned entry path after transactional
cleanup because the public diagnostic currently borrows path text.  Exact
per-fragment declaration spans and owned diagnostic provenance belong to the
R08 schema loader; R07 does not retain a failed partial graph merely to keep a
borrowed child path alive.

## 5. Exact-read observation

The internal test-only ledger records, in actual open order, the bounded path,
entry-or-fragment purpose, and exact byte count after each successful input
load.  It has caller-provided fixed capacity, performs no discovery, and is not
a product output format or public runtime option.  Insufficient ledger capacity
fails the observed test load instead of silently dropping evidence.

The poison regression places an unreferenced invalid TOML file, C source,
Makefile, and make fragment next to a reachable document.  A successful ledger
contains only the entry and reachable fragment.  Fixing, parsing, hashing, or
even opening poison siblings would be a test failure.

## 6. Capability and non-claims

The source loader imports no directory enumeration, glob, process, shell,
compiler, linker, or external hash capability.  It does not read C, headers,
Makefiles, objects, images, Git metadata, or environment-selected project
paths.  The graph is a configuration-document membership graph, not a build,
source-code, include, object, link, or runtime dependency graph.

R07 evidence covers explicit membership, traversal relations, root-relative
paths, duplicates, cycles, depth/count guards, cumulative byte accounting,
filesystem alias rejection, exact-read observation, poison non-observation,
transactional cleanup, and executed sanitizer cases.  It does not establish
complete schema acceptance, configuration resolution, arbitrary-host safety,
snapshot durability, or consumer build correctness.
