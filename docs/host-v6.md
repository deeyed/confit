---
doc_type: implementation-contract
status: active
authority: confit-v6-host-io
depends_on:
  - docs/config-v6.md
  - docs/architecture-v6.md
  - docs/model-v6.md
---

# Confit v6 descriptor-rooted bounded host I/O

This document defines the implemented R05 filesystem capability boundary.  It
does not claim that TOML input images, source-graph loading, schema parsing,
emitters, CLI configuration commands, or the terminal interface are implemented.
R14 now composes these primitives through a private directory transaction
described in `docs/snapshot-v6.md`; that transaction does not expand the public
host API with directory enumeration.

## 1. Responsibility

The host module supplies only these POSIX filesystem primitives:

- open one explicit absolute directory as an opaque root capability;
- validate one bounded normalized relative path;
- walk a relative path beneath a root without following a symlink;
- pin one opened regular-file identity and read its exact bounded byte image;
- atomically replace one bounded regular file through a private exclusive
  candidate in the same parent directory;
- acquire and release a nonblocking advisory lock on a no-follow regular file.

It does not discover documents, enumerate directories, parse TOML, decide
project membership, interpret a file suffix, calculate a digest, publish a
multi-file generation, resolve configuration values, execute a process, inspect
an environment variable, or know an ordinary build.

## 2. Directory-root capability

`confit_host_root_open_absolute` accepts one explicit absolute path bounded by
`CONFIT_LIMIT_SOURCE_PATH_BYTES`.  It opens `/` and then opens every named
component with `openat`, `O_DIRECTORY`, `O_NOFOLLOW`, and `O_CLOEXEC`.  Every
opened component is checked with `fstat`.  A `realpath` string is not used as
authority, and no symlink-follow fallback exists.

The resulting `ConfitHostRoot` owns a descriptor and a copied allocator
capability.  Project input and output roots use separate instances at later
layers; the host type itself contains no project/output policy.  Destroying a
root closes only its owned descriptor and storage.  A file capability opened
from a root has its own descriptor and may outlive the root object.

## 3. Relative-path grammar

`confit_host_relative_path_is_valid` accepts a non-empty valid UTF-8 relative
path of at most 1,024 bytes.  It rejects:

- an absolute path;
- an empty, `.` or `..` component;
- repeated or trailing slash;
- backslash;
- `*`, `?`, `[`, `]`, `{`, or `}` glob syntax;
- C0, C1, ESC, or DEL controls;
- malformed UTF-8;
- a path one byte over the public bound.

This validator deliberately does not require `.toml`.  File-language and graph
membership are caller policies owned by later source/schema layers.  It also
does not claim that textual validation proves filesystem safety: each path is
still opened component by component from the root descriptor.

## 4. Opened regular file and exact byte buffer

`confit_host_file_open` walks intermediate components with no-follow directory
opens and opens the final component with no-follow, close-on-exec, and
nonblocking flags.  Nonblocking open ensures a hostile FIFO cannot suspend the
process before its identity is rejected.  The final `fstat` must report a
regular file.  Directory, symlink, FIFO, socket, device, and other file kinds
are errors.

The opaque `ConfitHostFile` captures device, inode, and byte size at open time.
`confit_host_file_read` compares that identity before and after reading, checks
the requested ceiling before allocation, handles interrupted and short reads,
and probes one byte beyond the captured size.  Growth, shrinkage, identity
change, oversized input, allocation failure, and read error leave the caller's
previous `ConfitHostBuffer` unchanged.

The requested read ceiling itself cannot exceed
`CONFIT_LIMIT_TOTAL_INPUT_BYTES`; later configuration loaders impose the smaller
per-TOML and cumulative limits appropriate to each purpose.

A successful buffer owns exactly `size` input bytes plus one convenience NUL at
`bytes[size]`.  Embedded NUL inside the image is preserved: R05 is binary-safe
host I/O, while R06 decides whether TOML text may contain such a byte.  The
buffer also records the identity of the bytes read.  The caller can append an
exact-read ledger record immediately after each successful call; the host layer
has no hidden second open and exposes no callback, plugin, or global test hook.

The split open/read API is intentional.  It lets later input ownership bind one
descriptor to one byte image, and lets hostile tests deterministically change a
file between identity capture and read without adding a mutation hook to the
product.

## 5. Atomic single-file replacement

`confit_host_atomic_replace` is a single-file primitive, not the R14 snapshot
transaction.  It:

1. validates the bounded relative destination;
2. walks and pins its parent directory descriptor;
3. rejects an existing symlink or non-regular destination;
4. tries at most 128 process-and-counter candidate names;
5. creates a candidate with `O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC`;
6. verifies a one-link regular file;
7. writes every byte with interrupted/short-write handling;
8. applies exact permission bits and fsyncs the candidate;
9. rechecks the destination type;
10. renames within the same parent descriptor and fsyncs that directory.

One call writes at most `CONFIT_LIMIT_TOTAL_INPUT_BYTES`.  A zero-byte file is
valid.  Candidate names are collision-resistant through exclusive creation,
not claimed to be cryptographically random.  An attacker may occupy the bounded
candidate namespace and cause a denial of service, but cannot make Confit
truncate the occupied file or a symlink's referenced object.  Failure cleanup calls
`unlinkat` only for a candidate that the current call successfully created.

A directory-fsync failure after rename is reported even though the atomic name
replacement has occurred.  Higher publication layers must account for that
durability outcome; R05 does not call a partial multi-file bundle active.

## 6. Advisory lock

`confit_host_lock_acquire` opens or creates only a no-follow regular file, checks
that it has one link, and takes a nonblocking whole-file `fcntl` write lock.  A
stale pathname is reusable because the active advisory lock state, not pathname
age, is authority.  A contended lock returns a bounded host-I/O error.  Release unlocks
and closes the descriptor; releasing an initialized empty lock is safe.

The lock serializes cooperating writers.  It does not claim to stop a malicious
same-identity peer from unlinking or replacing names, and it is not a substitute
for create-only content addressing and atomic activation in R14.

## 7. Security evidence and non-claims

The R05 hostile C unit covers invalid paths, file and symlink roots,
intermediate/final symlinks, directory/FIFO/socket/device inputs, byte ceilings,
deterministic growth and shrinkage, 128 preoccupied candidate symlinks, regular
replacement and permission setting, symlink/directory destinations, unrelated
file preservation, advisory-lock contention/reuse, and concurrent complete-file
replacement.

Compiled-product review must distinguish intended host imports such as
`openat`, `fstatat`, `pread`, `write`, `fsync`, `renameat`, `unlinkat`, and
`fcntl` from forbidden directory-enumeration and process-execution imports.  The
product still must not import `opendir`, `readdir`, `scandir`, `glob`, `nftw`,
`fts_*`, `system`, `popen`, `fork`, `exec*`, `posix_spawn*`, or `dlopen`.
Test binaries may use `fork` to create lock and writer contention; that test-only
capability is not linked into the product binary.

R05 evidence is local POSIX filesystem behavior for the executed corpus.  R14
adds create-only private-directory publication, exact member reopening and
selected activation while retaining the same descriptor/no-follow boundary.
The R05 evidence alone is
not schema parsing, source reachability, parse/hash identity, multi-file
snapshot crash certification, arbitrary-filesystem certification, consumer
build evidence, or protection from every same-user denial of service.
