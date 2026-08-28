---
doc_type: implementation-contract
status: active
authority: confit-v6-core-model
depends_on:
  - docs/config-v6.md
  - docs/architecture-v6.md
---

# Confit v6 generic core model

This document defines the implemented R04 in-memory model boundary.  It does not
claim that schema loading, dependency evaluation, resolution, filesystem access,
snapshot publication, emitters, CLI configuration commands, or the terminal
interface are implemented.

## 1. Responsibility

The core model owns only bounded configuration data:

- the public resource ceilings in `confit/limits.h`;
- bool, signed int, bounded nonnegative hex, string, and enum values;
- a project-wide catalog of explicit source fragments, presentation menus, and
  option declarations;
- declaration defaults, optional numeric ranges, enum domains, and unparsed
  dependency text;
- explicit user-assignment records;
- resolved-value records with default/user origin and availability;
- bounded causal reason nodes for a later expression evaluator.

The model has no path-open capability.  A stored path is owned display and
diagnostic data, not authority to reopen a file.  The implementation imports no
file, stream, environment, terminal, process, or directory-enumeration API.

## 2. Public limits

`include/confit/limits.h` is the single numeric authority.  Parser, loader,
resolver, emitter, snapshot, and TUI layers must include this header rather than
copying numbers into private headers.  Every maximum is inclusive, and one unit
over the maximum must fail before multiplication, allocation, or mutation.

The header fixes all schema 6 limits from the accepted configuration contract:
per-file and aggregate bytes, fragment and edge counts, include depth, menus,
visible menu depth, symbols, paths, prompt/help/string/enum/dependency sizes,
expression node/depth counts, diagnostics, and render dimensions.

## 3. Allocation capability and failure semantics

Owned model objects accept an optional `ConfitAllocator`.  A null allocator
selects `malloc` and `free`.  A non-null allocator is valid only when allocate
and deallocate functions are both present.  The allocator and its context are
copied into every object that owns storage; the context therefore outlives that
object.

Allocation is not a hidden global policy.  Tests inject deterministic failures
through this capability.  Each mutating operation constructs a private candidate
first and swaps it into the destination only after every allocation and invariant
check succeeds.  On failure:

- an existing value, assignment, reason, or resolved record is unchanged;
- a catalog count and every previously returned object remain unchanged;
- all candidate allocations are released with the same allocator;
- no partially initialized object becomes visible.

Destroy operations accept zero-initialized/empty objects, and catalog destroy
accepts null.  `confit_catalog_reset` frees all owned members while retaining the
catalog allocation and allocator capability for reuse.

## 4. Values

`ConfitValueKind` is closed to:

```text
CONFIT_VALUE_BOOL
CONFIT_VALUE_INT
CONFIT_VALUE_HEX
CONFIT_VALUE_STRING
CONFIT_VALUE_ENUM
```

`CONFIT_VALUE_INVALID` is an empty lifecycle state, not a schema type.

The `ConfitValue` union stores bool as normalized zero/one, int as `int64_t`, hex
as `uint64_t` restricted to `INT64_MAX`, and string/enum as owned byte-counted
text.  String text must be valid UTF-8 and is bounded by
`CONFIT_LIMIT_STRING_BYTES`.  NUL, ESC, DEL, unsafe C0 controls, and C1 controls
are rejected; layout whitespace remains data for later output-language escaping.
Enum values use the closed ASCII atom grammar and enum-atom bound.

Callers initialize a stack value with `confit_value_init`, use a typed setter or
`confit_value_copy`, and finish with `confit_value_destroy`.  A failed setter or
copy preserves the prior destination.  `confit_value_text` returns a borrowed
view valid until mutation or destroy.

`confit_value_format_canonical` is an internal core identity representation, not
a public configuration file or emitter language.  Its forms are:

```text
bool:true
int:-12
hex:0x10e8
string:5:value
enum:7:verbose
```

String-like payloads are byte-length framed and copied raw, which is deterministic
and unambiguous without pretending to be TOML, Make, C, or JSON escaping.  The
function computes the required size before writing and leaves an undersized
destination untouched.

## 5. Project-wide catalog

`ConfitCatalog` is opaque.  It owns:

- one optional root `mainmenu` title;
- bounded source-fragment records;
- bounded presentation-menu records;
- bounded option declarations.

The catalog does not load these records.  A later explicit loader supplies
already-decided data through specs.  Add operations deep-copy every string,
value, range, enum atom, dependency text, and declaration path.  Accessors return
borrowed read-only views valid until reset or destroy.

### 5.1 Source fragments

A fragment stores a display path, one parent-fragment index or
`CONFIT_INDEX_NONE`, and its ordinal in the parent's literal source array.  The
model rejects duplicate paths, unknown parent indexes, excess fragment count,
overlong path text, invalid UTF-8, and terminal controls.  It does not normalize,
open, stat, hash, enumerate, or infer the path; those are later host and loader
responsibilities.

### 5.2 Menus

A menu stores its fragment, optional parent menu, prompt/help, and declaration
span.  One fragment has at most one menu.  A parent must already exist, and the
visible parent chain cannot exceed the public depth of three.  A menu remains a
presentation relation; it has no value or dependency behavior.

### 5.3 Declarations

A declaration stores:

- exact symbol and `ConfitValueKind`;
- prompt and help;
- an owned default of the same kind;
- an optional int/hex range whose minimum is not above its maximum and contains
  the default;
- an enum domain with no duplicate atoms and a member default;
- optional owned dependency text that is not parsed or evaluated in R04;
- fragment/menu indexes and an owned declaration span.

Symbols use `[A-Z][A-Z0-9_]{0,127}` and are unique in the catalog.  Model-level
type consistency prevents construction of a corrupt catalog, but TOML field
applicability, omitted defaults, native lexical type identity, and full schema
diagnostics remain later schema/type-loader responsibilities.

No declaration contains an ownership label, maturity label, placement,
configuration precedence, compilation input, command, or project-specific
metadata.

## 6. Assignment, resolved value, and causal reason

`ConfitAssignment` owns exactly one symbol and one typed value.  It represents
explicit user intent only; it has no priority, ordering, inherited source, or
implicit writer.

`ConfitResolvedValue` owns a symbol, declaration default, effective value,
origin (`CONFIT_ORIGIN_DEFAULT` or `CONFIT_ORIGIN_USER`), normalized availability,
and an optional reason index.  The record constructor requires default and
effective kinds to match.  It does not calculate either value.

`ConfitReasonNode` is a bounded data shape for a later expression layer.  A node
may own subject/related symbols, bounded detail text, and at most two child
indexes.  Its kinds can describe literal, reference, boolean composition,
comparison, and unavailable outcomes, but R04 supplies no lexer, parser, linker,
cycle detector, or evaluator.  A reason is diagnostic configuration causality,
not a task graph or execution plan.

## 7. Lifecycle evidence and non-claims

R04 unit tests cover all five value kinds, minimum/maximum values, UTF-8 and
control rejection, symbol and enum domains, value copy/compare/format, catalog
relations and read-only accessors, numeric range invariants, duplicate rejection,
assignment/resolved/reason ownership, reset/destroy, and injected allocation
failure at each catalog-add allocation point.

This evidence establishes pure in-memory model behavior for the executed corpus.
It does not establish TOML schema acceptance, source-graph reachability,
dependency semantics, configuration resolution, safe host I/O, immutable
publication, generated artifact safety, or interactive behavior.  The public C
layout is still a development API and is not declared to be a stable 1.0 ABI.
