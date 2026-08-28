#ifndef CONFIT_HOST_H
#define CONFIT_HOST_H

#include <stddef.h>
#include <stdint.h>

#include "confit/diagnostic.h"
#include "confit/model.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque capability rooted at one explicitly opened directory. */
typedef struct ConfitHostRoot ConfitHostRoot;

/** @brief Opaque no-follow capability for one already opened regular file. */
typedef struct ConfitHostFile ConfitHostFile;

/** @brief Stable identity captured from an opened regular file. */
typedef struct ConfitHostFileIdentity {
  uint64_t device;
  uint64_t inode;
  uint64_t size;
} ConfitHostFileIdentity;

/**
 * @brief Owned exact byte image returned by the bounded host reader.
 *
 * `bytes[size]` is a convenience NUL byte and is not part of the image.  The
 * image may contain embedded NUL bytes; a later text parser decides whether
 * those bytes are valid for its language.  Initialize before use and destroy
 * after the final consumer releases the image.
 */
typedef struct ConfitHostBuffer {
  unsigned char *bytes;
  size_t size;
  ConfitHostFileIdentity identity;
  ConfitAllocator allocator;
} ConfitHostBuffer;

/** @brief A held POSIX advisory lock; initialize before acquire. */
typedef struct ConfitHostLock {
  int descriptor;
} ConfitHostLock;

/**
 * @brief Validate one normalized non-empty relative path without performing I/O.
 *
 * Absolute paths, empty/dot/dot-dot components, repeated slash, trailing slash,
 * backslash, glob metacharacters, controls, and paths over the public bound are
 * rejected.  Suffix and project membership policy belong to later callers.
 */
int confit_host_relative_path_is_valid(const char *path);

/**
 * @brief Open an absolute directory one component at a time without symlinks.
 *
 * The returned capability owns its descriptor and allocator.  A textual
 * `realpath` result is never used as the authority.
 */
ConfitStatus confit_host_root_open_absolute(
    const char *absolute_path, const ConfitAllocator *allocator,
    ConfitHostRoot **out_root, ConfitDiagnostic *diagnostic);

void confit_host_root_destroy(ConfitHostRoot *root);

/**
 * @brief Open one regular file beneath a root by descriptor-relative walk.
 *
 * Intermediate components must be real directories and the final component a
 * regular file.  The returned capability pins the file identity and size used
 * by the later bounded read.
 */
ConfitStatus confit_host_file_open(ConfitHostRoot *root,
                                   const char *relative_path,
                                   ConfitHostFile **out_file,
                                   ConfitDiagnostic *diagnostic);

void confit_host_file_destroy(ConfitHostFile *file);

void confit_host_buffer_init(ConfitHostBuffer *buffer);
void confit_host_buffer_destroy(ConfitHostBuffer *buffer);

/**
 * @brief Read the exact opened file image with a hard byte ceiling.
 *
 * Growth, shrinkage, identity change, short reads, and allocation failure leave
 * `out_buffer` unchanged.  A successful call replaces its previous contents.
 */
ConfitStatus confit_host_file_read(ConfitHostFile *file, size_t maximum_bytes,
                                   const ConfitAllocator *allocator,
                                   ConfitHostBuffer *out_buffer,
                                   ConfitDiagnostic *diagnostic);

/**
 * @brief Atomically replace or create one bounded regular file beneath a root.
 *
 * A private candidate in the final parent directory is created with
 * `O_CREAT|O_EXCL|O_NOFOLLOW`, written completely, assigned exact permission
 * bits, and fsynced before descriptor-relative rename and parent fsync.  A
 * pre-existing non-regular destination is rejected.  Cleanup removes only the
 * candidate created by this call.
 */
ConfitStatus confit_host_atomic_replace(ConfitHostRoot *root,
                                        const char *relative_path,
                                        const void *bytes, size_t size,
                                        unsigned permissions,
                                        ConfitDiagnostic *diagnostic);

void confit_host_lock_init(ConfitHostLock *lock);

/**
 * @brief Acquire a nonblocking write lock on a regular no-follow lock file.
 *
 * An existing regular pathname is reused; pathname age is not lock authority.
 */
ConfitStatus confit_host_lock_acquire(ConfitHostRoot *root,
                                      const char *relative_path,
                                      ConfitHostLock *lock,
                                      ConfitDiagnostic *diagnostic);

/** @brief Release and close a held lock.  Safe on an initialized empty lock. */
ConfitStatus confit_host_lock_release(ConfitHostLock *lock,
                                      ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_HOST_H */
