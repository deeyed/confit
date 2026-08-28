---
doc_type: implementation-contract
status: active
authority: confit-v6-input-image
depends_on:
  - docs/config-v6.md
  - docs/architecture-v6.md
  - docs/model-v6.md
  - docs/host-v6.md
---

# Confit v6 exact input-image ownership

This document defines the implemented R06 boundary between descriptor-rooted
file reads and later configuration-document loading.  It does not claim that
entry/menu/config fields, a source graph, configuration values, dependencies,
resolution, snapshots, emitters, configuration CLI commands, or a TUI are
implemented.

## 1. One image, one identity

`confit_input_load_toml` performs one bounded `confit_host_file_open` and one
bounded `confit_host_file_read`.  The successful `ConfitHostBuffer` becomes the
sole exact input image owned by an opaque `ConfitInputImage`.  That image also
owns:

- its normalized display path;
- the regular-file device, inode, and size captured by the host layer;
- lowercase SHA-256 over the exact bytes;
- a bounded line-start index;
- the read-only TOML document parsed from those bytes.

The TOML adapter receives the exact buffer by borrowed memory view.  It does not
copy the image for this product path and it has no file-open API.  The adapter's
parsed values and copied source-name metadata remain valid until the image
destroys the document.  Image destruction frees the document before releasing
the exact byte buffer.

The existing `confit_toml_parse_text` helper remains an owned in-memory adapter
for direct bounded unit and fuzz inputs.  It may copy caller memory because that
API has no enclosing input-image lifetime.  Product file loading uses the
input-image path and never routes a pathname to the TOML library.

R16 adds `confit_input_load_toml_absolute()` for the already-frozen explicit
absolute `--config` and `--other-config` forms. It separates the normalized
absolute path into an explicit parent-directory capability and one relative
leaf, then uses the same transactional loader below. The image owns the full
absolute display path; it does not weaken literal `source` paths, enumerate the
parent, or introduce an ambient current-directory lookup.

## 2. Parse and digest ordering

After the host read completes, Confit validates the byte image, constructs its
line-start index, calculates SHA-256 directly from the image, and passes the
same pointer and explicit byte count to the memory-only TOML adapter.  Neither
digest generation nor TOML parsing reopens the display path.  The digest is an
input-membership identity, not an authenticity signature.

Replacing the pathname after the host read cannot redirect either operation.
The original image continues to produce the original parsed values and digest;
a later independent load observes the replacement as a different image.  The
controlled regression performs this replacement at the explicit boundary
between `confit_host_file_read` and the internal transactional image constructor.
This seam is a compiled internal function, not a callback, plugin, process,
environment probe, or global mutation hook.

## 3. Text validation and bounds

Every file load applies `CONFIT_LIMIT_TOML_FILE_BYTES` before publishing an
image.  The memory adapter applies the same public limit defensively.  An empty
TOML document is a valid empty root table.  An input one byte above the limit is
an error.

The host buffer retains a NUL sentinel outside its exact byte count for the
vendored parser API.  A NUL inside the exact image is rejected before TOML
parsing with its deterministic 1-based line and byte column.  Malformed UTF-8
and TOML syntax are validation failures, and a failed load publishes no image.
The byte count is authoritative; `strlen` never determines configuration input
membership or hashing.

## 4. Line-start index

One line-start entry always exists at byte offset zero.  Every LF adds the byte
immediately following it, including an empty final line after a trailing LF.
The index allocation is checked before multiplication.  Lookup accepts offsets
from zero through the exact byte count and uses bounded binary search to return
1-based line and byte column.  An invalid offset leaves caller outputs
unchanged.

The column is a byte column.  CRLF therefore retains CR as one byte in the
previous line and begins the next line after LF.  Later schema diagnostics may
translate owned byte spans with this index without rescanning or reopening a
file.

## 5. Aggregate accounting hook

`confit_input_image_accumulate` adds an image's exact byte count to a caller's
current total only when the result is no greater than
`CONFIT_LIMIT_TOTAL_INPUT_BYTES`.  It checks subtraction before addition and
leaves the prior total unchanged on failure.  It does not enumerate fragments,
load a graph, or decide document membership; R07 owns those operations.

## 6. Failure and lifetime rules

Construction is transactional.  The caller's host buffer transfers into the
candidate image only after path copy, line index, digest, and TOML parse all
succeed.  On failure the host buffer remains owned by the caller and every
candidate allocation or parse tree is released.  On success the caller buffer
is reset and the image becomes the only owner.

Diagnostics borrow the caller-provided display path on failure.  A successful
image owns its display path and parsed TOML source metadata.  Accessors return
borrowed immutable data that expires when the image is destroyed.

## 7. Evidence and non-claims

The R06 unit corpus covers native TOML values, stable bytes/path/identity,
same-buffer document source, known SHA-256 behavior, line lookup, empty input,
the exact one-file bound and one-over rejection, embedded NUL, malformed UTF-8,
malformed TOML position, aggregate-byte accounting, and controlled pathname
replacement.  Sanitizer and bounded TOML fuzz execution cover the executed
memory corpus.  Compiled-object review distinguishes the input/parser objects
from the host object: the former import no `open`, `fopen`, directory
enumeration, subprocess, or external hash API.

R10 additionally compiles the vendored TOML translation unit in memory-only
mode. Its optional pathname and `FILE *` parser entry points are absent from the
product object, so the final product no longer inherits unused `fopen`, `fread`,
or `fclose` imports. Hex spelling recovery is an internal schema seam, requires
the value to belong to the supplied document, and reads only that document's
already-owned byte image.

This evidence does not establish schema interpretation, reachable membership,
manifest serialization, snapshot verification, arbitrary-input safety,
filesystem authenticity, or protection from every same-user denial of service.
R07 must use these images as explicit graph nodes and must not add a second path
read for convenience.
