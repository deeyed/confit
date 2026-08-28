#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "confit/host.h"
#include "host_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#error "Confit host I/O requires O_CLOEXEC"
#endif
#ifndef O_DIRECTORY
#error "Confit host I/O requires O_DIRECTORY"
#endif
#ifndef O_NOFOLLOW
#error "Confit host I/O requires O_NOFOLLOW"
#endif
#ifndef AT_SYMLINK_NOFOLLOW
#error "Confit host I/O requires AT_SYMLINK_NOFOLLOW"
#endif

#define CONFIT_HOST_CANDIDATE_ATTEMPTS 128U
#define CONFIT_HOST_CANDIDATE_NAME_BYTES 96U

struct ConfitHostRoot {
  int descriptor;
  ConfitAllocator allocator;
};

struct ConfitHostFile {
  int descriptor;
  ConfitHostFileIdentity identity;
  ConfitAllocator allocator;
};

struct ConfitHostDirectoryTransaction {
  ConfitHostRoot *root;
  ConfitAllocator allocator;
  int parent_descriptor;
  int directory_descriptor;
  int active;
  int sealed;
  char candidate_leaf[CONFIT_HOST_CANDIDATE_NAME_BYTES];
  char relative_path[CONFIT_LIMIT_SOURCE_PATH_BYTES + 1U];
  char created[CONFIT_LIMIT_SNAPSHOT_ARTIFACTS]
              [CONFIT_LIMIT_SNAPSHOT_ARTIFACT_NAME_BYTES + 1U];
  size_t created_count;
};

static atomic_uint confit_host_candidate_sequence;

static const char kInvalidArgument[] = "invalid host I/O argument";
static const char kInvalidAllocator[] = "allocator capability is incomplete";
static const char kInvalidPath[] = "host path is not normalized and bounded";
static const char kOpenRootFailed[] = "failed to open a no-follow directory root";
static const char kOpenPathFailed[] = "failed to walk a no-follow relative path";
static const char kNotRegular[] = "host path does not identify a regular file";
static const char kOutOfMemory[] = "failed to allocate a bounded host object";
static const char kFileChanged[] = "opened regular file changed before read completed";
static const char kFileTooLarge[] = "opened regular file exceeds the byte ceiling";
static const char kReadFailed[] = "failed to read the exact regular-file image";
static const char kWriteFailed[] = "failed to write a complete private candidate";
static const char kCandidateFailed[] = "failed to create a private exclusive candidate";
static const char kDestinationUnsafe[] = "atomic destination is not absent or regular";
static const char kPublicationFailed[] = "failed to replace and sync the regular file";
static const char kLockFailed[] = "failed to acquire the regular-file lock";
static const char kUnlockFailed[] = "failed to release the regular-file lock";
static const char kDirectoryFailed[] =
    "failed to create a private snapshot directory";
static const char kDirectoryWriteFailed[] =
    "failed to create and verify a snapshot file";
static const char kDirectorySyncFailed[] =
    "failed to seal and sync a snapshot directory";
static const char kDirectoryPublishFailed[] =
    "failed to publish a create-only snapshot directory";

static ConfitStatus confit_host_fail(ConfitDiagnostic *diagnostic,
                                     ConfitStatus status,
                                     const char *message) {
  confit_diagnostic_set(diagnostic, status, 0, 0U, 0U, message);
  return status;
}

static int confit_host_resolve_allocator(const ConfitAllocator *requested,
                                         ConfitAllocator *resolved) {
  if (resolved == 0) {
    return 0;
  }
  if (requested == 0) {
    confit_allocator_default(resolved);
    return 1;
  }
  if (!confit_allocator_is_valid(requested)) {
    return 0;
  }
  *resolved = *requested;
  return 1;
}

static int confit_host_bounded_length(const char *text, size_t limit,
                                      size_t *out_size) {
  size_t index;
  if (text == 0 || out_size == 0) {
    return 0;
  }
  for (index = 0U; index <= limit; ++index) {
    if (text[index] == '\0') {
      *out_size = index;
      return 1;
    }
  }
  return 0;
}

static int confit_host_utf8_continuation(unsigned char byte) {
  return (byte & 0xC0U) == 0x80U;
}

static int confit_host_utf8_is_valid(const char *text, size_t size) {
  const unsigned char *bytes = (const unsigned char *)text;
  size_t index = 0U;
  while (index < size) {
    const unsigned char first = bytes[index];
    size_t width;
    if (first < 0x80U) {
      width = 1U;
    } else if (first >= 0xC2U && first <= 0xDFU) {
      width = 2U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
      width = 3U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
      width = 4U;
    } else {
      return 0;
    }
    if (index + width > size ||
        (width >= 2U && !confit_host_utf8_continuation(bytes[index + 1U])) ||
        (width >= 3U && !confit_host_utf8_continuation(bytes[index + 2U])) ||
        (width >= 4U && !confit_host_utf8_continuation(bytes[index + 3U])) ||
        (width == 3U && first == 0xE0U && bytes[index + 1U] < 0xA0U) ||
        (width == 3U && first == 0xEDU && bytes[index + 1U] > 0x9FU) ||
        (width == 4U && first == 0xF0U && bytes[index + 1U] < 0x90U) ||
        (width == 4U && first == 0xF4U && bytes[index + 1U] > 0x8FU)) {
      return 0;
    }
    index += width;
  }
  return 1;
}

static int confit_host_path_byte_is_forbidden(unsigned char byte) {
  return byte == '\\' || byte == '*' || byte == '?' || byte == '[' ||
         byte == ']' || byte == '{' || byte == '}' || byte < 0x20U ||
         byte == 0x7FU;
}

static int confit_host_path_is_valid(const char *path, int absolute) {
  size_t length;
  size_t index;
  size_t segment_start;

  if (!confit_host_bounded_length(path, CONFIT_LIMIT_SOURCE_PATH_BYTES,
                                  &length) ||
      length == 0U || !confit_host_utf8_is_valid(path, length)) {
    return 0;
  }
  if ((absolute && path[0] != '/') || (!absolute && path[0] == '/')) {
    return 0;
  }
  if (absolute && length == 1U) {
    return 1;
  }
  if (path[length - 1U] == '/') {
    return 0;
  }

  index = absolute ? 1U : 0U;
  segment_start = index;
  for (; index <= length; ++index) {
    const unsigned char byte = (unsigned char)path[index];
    if (index < length && confit_host_path_byte_is_forbidden(byte)) {
      return 0;
    }
    if (index + 1U < length && byte == 0xC2U &&
        (unsigned char)path[index + 1U] >= 0x80U &&
        (unsigned char)path[index + 1U] <= 0x9FU) {
      return 0;
    }
    if (index == length || byte == '/') {
      const size_t segment_size = index - segment_start;
      if (segment_size == 0U ||
          (segment_size == 1U && path[segment_start] == '.') ||
          (segment_size == 2U && path[segment_start] == '.' &&
           path[segment_start + 1U] == '.')) {
        return 0;
      }
      segment_start = index + 1U;
    }
  }
  return 1;
}

int confit_host_relative_path_is_valid(const char *path) {
  return confit_host_path_is_valid(path, 0);
}

static int confit_host_copy_component(const char *path, size_t start,
                                      size_t end, char *component,
                                      size_t component_size) {
  const size_t length = end - start;
  if (length == 0U || length + 1U > component_size) {
    return 0;
  }
  memcpy(component, path + start, length);
  component[length] = '\0';
  return 1;
}

static int confit_host_open_directory_component(int parent,
                                                const char *component) {
  struct stat information;
  int descriptor = openat(parent, component,
                          O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (descriptor < 0) {
    return -1;
  }
  if (fstat(descriptor, &information) != 0 ||
      !S_ISDIR(information.st_mode)) {
    (void)close(descriptor);
    errno = ENOTDIR;
    return -1;
  }
  return descriptor;
}

static int confit_host_open_absolute_directory(const char *path) {
  char component[CONFIT_LIMIT_SOURCE_PATH_BYTES + 1U];
  size_t length;
  size_t start;
  size_t end;
  int current;

  if (!confit_host_path_is_valid(path, 1) ||
      !confit_host_bounded_length(path, CONFIT_LIMIT_SOURCE_PATH_BYTES,
                                  &length)) {
    errno = EINVAL;
    return -1;
  }
  current = open("/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (current < 0 || length == 1U) {
    return current;
  }

  start = 1U;
  while (start < length) {
    int next;
    end = start;
    while (end < length && path[end] != '/') {
      end += 1U;
    }
    if (!confit_host_copy_component(path, start, end, component,
                                    sizeof(component))) {
      (void)close(current);
      errno = EINVAL;
      return -1;
    }
    next = confit_host_open_directory_component(current, component);
    (void)close(current);
    if (next < 0) {
      return -1;
    }
    current = next;
    start = end + 1U;
  }
  return current;
}

ConfitStatus confit_host_root_open_absolute(
    const char *absolute_path, const ConfitAllocator *allocator,
    ConfitHostRoot **out_root, ConfitDiagnostic *diagnostic) {
  ConfitAllocator resolved;
  ConfitHostRoot *root;
  struct stat information;
  int descriptor;

  if (absolute_path == 0 || out_root == 0) {
    return confit_host_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  }
  *out_root = 0;
  if (!confit_host_path_is_valid(absolute_path, 1)) {
    return confit_host_fail(diagnostic, CONFIT_ERR_IO, kInvalidPath);
  }
  if (!confit_host_resolve_allocator(allocator, &resolved)) {
    return confit_host_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidAllocator);
  }
  descriptor = confit_host_open_absolute_directory(absolute_path);
  if (descriptor < 0 || fstat(descriptor, &information) != 0 ||
      !S_ISDIR(information.st_mode)) {
    if (descriptor >= 0) {
      (void)close(descriptor);
    }
    return confit_host_fail(diagnostic, CONFIT_ERR_IO, kOpenRootFailed);
  }
  root = (ConfitHostRoot *)resolved.allocate(resolved.context, sizeof(*root));
  if (root == 0) {
    (void)close(descriptor);
    return confit_host_fail(diagnostic, CONFIT_ERR_INTERNAL, kOutOfMemory);
  }
  root->descriptor = descriptor;
  root->allocator = resolved;
  *out_root = root;
  return CONFIT_OK;
}

void confit_host_root_destroy(ConfitHostRoot *root) {
  ConfitAllocator allocator;
  if (root == 0) {
    return;
  }
  allocator = root->allocator;
  if (root->descriptor >= 0) {
    (void)close(root->descriptor);
  }
  memset(root, 0, sizeof(*root));
  allocator.deallocate(allocator.context, root);
}

static int confit_host_open_parent(const ConfitHostRoot *root,
                                   const char *relative_path,
                                   int *out_parent, int *out_parent_owned,
                                   char *out_leaf, size_t out_leaf_size) {
  size_t length;
  size_t start = 0U;
  size_t end;
  int current;
  int current_owned = 0;

  if (root == 0 || out_parent == 0 || out_parent_owned == 0 || out_leaf == 0 ||
      !confit_host_relative_path_is_valid(relative_path) ||
      !confit_host_bounded_length(relative_path,
                                  CONFIT_LIMIT_SOURCE_PATH_BYTES, &length)) {
    errno = EINVAL;
    return 0;
  }
  current = root->descriptor;
  while (start < length) {
    char component[CONFIT_LIMIT_SOURCE_PATH_BYTES + 1U];
    end = start;
    while (end < length && relative_path[end] != '/') {
      end += 1U;
    }
    if (!confit_host_copy_component(relative_path, start, end, component,
                                    sizeof(component))) {
      if (current_owned) {
        (void)close(current);
      }
      errno = EINVAL;
      return 0;
    }
    if (end == length) {
      const size_t leaf_size = end - start;
      if (leaf_size + 1U > out_leaf_size) {
        if (current_owned) {
          (void)close(current);
        }
        errno = ENAMETOOLONG;
        return 0;
      }
      memcpy(out_leaf, component, leaf_size + 1U);
      *out_parent = current;
      *out_parent_owned = current_owned;
      return 1;
    }
    {
      const int next = confit_host_open_directory_component(current, component);
      if (current_owned) {
        (void)close(current);
      }
      if (next < 0) {
        return 0;
      }
      current = next;
      current_owned = 1;
    }
    start = end + 1U;
  }
  if (current_owned) {
    (void)close(current);
  }
  errno = EINVAL;
  return 0;
}

static int confit_host_identity_from_stat(const struct stat *information,
                                          ConfitHostFileIdentity *identity) {
  if (information == 0 || identity == 0 || information->st_size < 0 ||
      !S_ISREG(information->st_mode)) {
    return 0;
  }
  identity->device = (uint64_t)information->st_dev;
  identity->inode = (uint64_t)information->st_ino;
  identity->size = (uint64_t)information->st_size;
  return 1;
}

static int confit_host_identity_equal(const ConfitHostFileIdentity *left,
                                      const ConfitHostFileIdentity *right) {
  return left != 0 && right != 0 && left->device == right->device &&
         left->inode == right->inode && left->size == right->size;
}

ConfitStatus confit_host_file_open(ConfitHostRoot *root,
                                   const char *relative_path,
                                   ConfitHostFile **out_file,
                                   ConfitDiagnostic *diagnostic) {
  char leaf[CONFIT_LIMIT_SOURCE_PATH_BYTES + 1U];
  ConfitHostFileIdentity identity;
  ConfitHostFile *file;
  struct stat information;
  int descriptor;
  int parent;
  int parent_owned;

  if (root == 0 || relative_path == 0 || out_file == 0) {
    return confit_host_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  }
  *out_file = 0;
  if (!confit_host_relative_path_is_valid(relative_path)) {
    return confit_host_fail(diagnostic, CONFIT_ERR_IO, kInvalidPath);
  }
  if (!confit_host_open_parent(root, relative_path, &parent, &parent_owned,
                               leaf, sizeof(leaf))) {
    return confit_host_fail(diagnostic, CONFIT_ERR_IO, kOpenPathFailed);
  }
  descriptor = openat(parent, leaf,
                      O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
  if (parent_owned) {
    (void)close(parent);
  }
  if (descriptor < 0) {
    return confit_host_fail(diagnostic, CONFIT_ERR_IO, kOpenPathFailed);
  }
  if (fstat(descriptor, &information) != 0 ||
      !confit_host_identity_from_stat(&information, &identity)) {
    (void)close(descriptor);
    return confit_host_fail(diagnostic, CONFIT_ERR_IO, kNotRegular);
  }
  file = (ConfitHostFile *)root->allocator.allocate(root->allocator.context,
                                                    sizeof(*file));
  if (file == 0) {
    (void)close(descriptor);
    return confit_host_fail(diagnostic, CONFIT_ERR_INTERNAL, kOutOfMemory);
  }
  file->descriptor = descriptor;
  file->identity = identity;
  file->allocator = root->allocator;
  *out_file = file;
  return CONFIT_OK;
}

void confit_host_file_destroy(ConfitHostFile *file) {
  ConfitAllocator allocator;
  if (file == 0) {
    return;
  }
  allocator = file->allocator;
  if (file->descriptor >= 0) {
    (void)close(file->descriptor);
  }
  memset(file, 0, sizeof(*file));
  allocator.deallocate(allocator.context, file);
}

void confit_host_buffer_init(ConfitHostBuffer *buffer) {
  if (buffer != 0) {
    memset(buffer, 0, sizeof(*buffer));
  }
}

void confit_host_buffer_destroy(ConfitHostBuffer *buffer) {
  if (buffer == 0) {
    return;
  }
  if (buffer->bytes != 0 && confit_allocator_is_valid(&buffer->allocator)) {
    buffer->allocator.deallocate(buffer->allocator.context, buffer->bytes);
  }
  memset(buffer, 0, sizeof(*buffer));
}

static int confit_host_file_current_identity(
    const ConfitHostFile *file, ConfitHostFileIdentity *identity) {
  struct stat information;
  return file != 0 && identity != 0 &&
         fstat(file->descriptor, &information) == 0 &&
         confit_host_identity_from_stat(&information, identity);
}

ConfitStatus confit_host_file_read(ConfitHostFile *file, size_t maximum_bytes,
                                   const ConfitAllocator *allocator,
                                   ConfitHostBuffer *out_buffer,
                                   ConfitDiagnostic *diagnostic) {
  ConfitAllocator resolved;
  ConfitHostBuffer candidate;
  ConfitHostFileIdentity before;
  ConfitHostFileIdentity after;
  size_t expected;
  size_t offset = 0U;

  if (file == 0 || out_buffer == 0) {
    return confit_host_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  }
  if (!confit_host_resolve_allocator(allocator, &resolved)) {
    return confit_host_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidAllocator);
  }
  if (maximum_bytes == SIZE_MAX ||
      maximum_bytes > CONFIT_LIMIT_TOTAL_INPUT_BYTES ||
      file->identity.size > (uint64_t)maximum_bytes ||
      file->identity.size > (uint64_t)SIZE_MAX - 1U ||
      file->identity.size > (uint64_t)INT64_MAX) {
    return confit_host_fail(diagnostic, CONFIT_ERR_IO, kFileTooLarge);
  }
  if (!confit_host_file_current_identity(file, &before) ||
      !confit_host_identity_equal(&file->identity, &before)) {
    return confit_host_fail(diagnostic, CONFIT_ERR_IO, kFileChanged);
  }

  confit_host_buffer_init(&candidate);
  expected = (size_t)file->identity.size;
  candidate.bytes = (unsigned char *)resolved.allocate(resolved.context,
                                                        expected + 1U);
  if (candidate.bytes == 0) {
    return confit_host_fail(diagnostic, CONFIT_ERR_INTERNAL, kOutOfMemory);
  }
  candidate.allocator = resolved;
  while (offset < expected) {
    const ssize_t amount = pread(file->descriptor, candidate.bytes + offset,
                                 expected - offset, (off_t)offset);
    if (amount < 0 && errno == EINTR) {
      continue;
    }
    if (amount <= 0) {
      confit_host_buffer_destroy(&candidate);
      return confit_host_fail(diagnostic, CONFIT_ERR_IO, kReadFailed);
    }
    offset += (size_t)amount;
  }
  for (;;) {
    unsigned char extra;
    const ssize_t amount = pread(file->descriptor, &extra, 1U,
                                 (off_t)expected);
    if (amount < 0 && errno == EINTR) {
      continue;
    }
    if (amount != 0) {
      confit_host_buffer_destroy(&candidate);
      return confit_host_fail(diagnostic, CONFIT_ERR_IO, kFileChanged);
    }
    break;
  }
  if (!confit_host_file_current_identity(file, &after) ||
      !confit_host_identity_equal(&file->identity, &after)) {
    confit_host_buffer_destroy(&candidate);
    return confit_host_fail(diagnostic, CONFIT_ERR_IO, kFileChanged);
  }
  candidate.bytes[expected] = '\0';
  candidate.size = expected;
  candidate.identity = file->identity;
  confit_host_buffer_destroy(out_buffer);
  *out_buffer = candidate;
  return CONFIT_OK;
}

static int confit_host_destination_is_safe(int parent, const char *leaf) {
  struct stat information;
  if (fstatat(parent, leaf, &information, AT_SYMLINK_NOFOLLOW) == 0) {
    return S_ISREG(information.st_mode);
  }
  return errno == ENOENT;
}

static int confit_host_write_all(int descriptor, const unsigned char *bytes,
                                 size_t size) {
  size_t offset = 0U;
  while (offset < size) {
    const ssize_t amount = write(descriptor, bytes + offset, size - offset);
    if (amount < 0 && errno == EINTR) {
      continue;
    }
    if (amount <= 0) {
      return 0;
    }
    offset += (size_t)amount;
  }
  return 1;
}

ConfitStatus confit_host_atomic_replace(ConfitHostRoot *root,
                                        const char *relative_path,
                                        const void *bytes, size_t size,
                                        unsigned permissions,
                                        ConfitDiagnostic *diagnostic) {
  const unsigned char *source = (const unsigned char *)bytes;
  char candidate_name[CONFIT_HOST_CANDIDATE_NAME_BYTES];
  char leaf[CONFIT_LIMIT_SOURCE_PATH_BYTES + 1U];
  struct stat information;
  unsigned attempt;
  int candidate = -1;
  int candidate_owned = 0;
  int parent;
  int parent_owned;
  int renamed = 0;
  ConfitStatus status = CONFIT_OK;

  if (root == 0 || relative_path == 0 || (bytes == 0 && size != 0U) ||
      permissions > 0777U) {
    return confit_host_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  }
  if (size > CONFIT_LIMIT_TOTAL_INPUT_BYTES) {
    return confit_host_fail(diagnostic, CONFIT_ERR_IO, kFileTooLarge);
  }
  if (!confit_host_relative_path_is_valid(relative_path)) {
    return confit_host_fail(diagnostic, CONFIT_ERR_IO, kInvalidPath);
  }
  if (!confit_host_open_parent(root, relative_path, &parent, &parent_owned,
                               leaf, sizeof(leaf))) {
    return confit_host_fail(diagnostic, CONFIT_ERR_IO, kOpenPathFailed);
  }
  if (!confit_host_destination_is_safe(parent, leaf)) {
    status = confit_host_fail(diagnostic, CONFIT_ERR_IO, kDestinationUnsafe);
    goto cleanup;
  }
  for (attempt = 0U; attempt < CONFIT_HOST_CANDIDATE_ATTEMPTS; ++attempt) {
    const unsigned sequence = atomic_fetch_add_explicit(
        &confit_host_candidate_sequence, 1U, memory_order_relaxed);
    const int length = snprintf(candidate_name, sizeof(candidate_name),
                                ".confit-candidate-%ld-%u", (long)getpid(),
                                sequence);
    if (length < 0 || (size_t)length >= sizeof(candidate_name)) {
      status = confit_host_fail(diagnostic, CONFIT_ERR_INTERNAL,
                                kCandidateFailed);
      goto cleanup;
    }
    candidate = openat(parent, candidate_name,
                       O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                       0600);
    if (candidate >= 0) {
      candidate_owned = 1;
      break;
    }
    if (errno != EEXIST) {
      status = confit_host_fail(diagnostic, CONFIT_ERR_IO, kCandidateFailed);
      goto cleanup;
    }
  }
  if (candidate < 0) {
    status = confit_host_fail(diagnostic, CONFIT_ERR_IO, kCandidateFailed);
    goto cleanup;
  }
  if (fstat(candidate, &information) != 0 ||
      !S_ISREG(information.st_mode) || information.st_nlink != 1 ||
      !confit_host_write_all(candidate, source, size) ||
      fchmod(candidate, (mode_t)permissions) != 0 || fsync(candidate) != 0 ||
      fstat(candidate, &information) != 0 ||
      !S_ISREG(information.st_mode) || information.st_size < 0 ||
      (uint64_t)information.st_size != (uint64_t)size) {
    status = confit_host_fail(diagnostic, CONFIT_ERR_IO, kWriteFailed);
    goto cleanup;
  }
  if (close(candidate) != 0) {
    candidate = -1;
    status = confit_host_fail(diagnostic, CONFIT_ERR_IO, kWriteFailed);
    goto cleanup;
  }
  candidate = -1;
  if (!confit_host_destination_is_safe(parent, leaf)) {
    status = confit_host_fail(diagnostic, CONFIT_ERR_IO, kDestinationUnsafe);
    goto cleanup;
  }
  if (renameat(parent, candidate_name, parent, leaf) != 0) {
    status = confit_host_fail(diagnostic, CONFIT_ERR_IO, kPublicationFailed);
    goto cleanup;
  }
  renamed = 1;
  candidate_owned = 0;
  if (fsync(parent) != 0) {
    status = confit_host_fail(diagnostic, CONFIT_ERR_IO, kPublicationFailed);
  }

cleanup:
  if (candidate >= 0) {
    (void)close(candidate);
  }
  if (candidate_owned && !renamed) {
    (void)unlinkat(parent, candidate_name, 0);
  }
  if (parent_owned) {
    (void)close(parent);
  }
  return status;
}

void confit_host_lock_init(ConfitHostLock *lock) {
  if (lock != 0) {
    lock->descriptor = -1;
  }
}

ConfitStatus confit_host_lock_acquire(ConfitHostRoot *root,
                                      const char *relative_path,
                                      ConfitHostLock *lock,
                                      ConfitDiagnostic *diagnostic) {
  char leaf[CONFIT_LIMIT_SOURCE_PATH_BYTES + 1U];
  struct flock request;
  struct stat information;
  int descriptor;
  int parent;
  int parent_owned;

  if (root == 0 || relative_path == 0 || lock == 0 || lock->descriptor >= 0) {
    return confit_host_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  }
  if (!confit_host_relative_path_is_valid(relative_path)) {
    return confit_host_fail(diagnostic, CONFIT_ERR_IO, kInvalidPath);
  }
  if (!confit_host_open_parent(root, relative_path, &parent, &parent_owned,
                               leaf, sizeof(leaf))) {
    return confit_host_fail(diagnostic, CONFIT_ERR_IO, kOpenPathFailed);
  }
  if (!confit_host_destination_is_safe(parent, leaf)) {
    if (parent_owned) {
      (void)close(parent);
    }
    return confit_host_fail(diagnostic, CONFIT_ERR_IO, kNotRegular);
  }
  descriptor = openat(parent, leaf,
                      O_RDWR | O_CREAT | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC,
                      0600);
  if (parent_owned) {
    (void)close(parent);
  }
  if (descriptor < 0 || fstat(descriptor, &information) != 0 ||
      !S_ISREG(information.st_mode) || information.st_nlink != 1) {
    if (descriptor >= 0) {
      (void)close(descriptor);
    }
    return confit_host_fail(diagnostic, CONFIT_ERR_IO, kNotRegular);
  }
  memset(&request, 0, sizeof(request));
  request.l_type = F_WRLCK;
  request.l_whence = SEEK_SET;
  if (fcntl(descriptor, F_SETLK, &request) != 0) {
    (void)close(descriptor);
    return confit_host_fail(diagnostic, CONFIT_ERR_IO, kLockFailed);
  }
  lock->descriptor = descriptor;
  return CONFIT_OK;
}

ConfitStatus confit_host_lock_release(ConfitHostLock *lock,
                                      ConfitDiagnostic *diagnostic) {
  struct flock request;
  int failed = 0;
  if (lock == 0) {
    return confit_host_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  }
  if (lock->descriptor < 0) {
    return CONFIT_OK;
  }
  memset(&request, 0, sizeof(request));
  request.l_type = F_UNLCK;
  request.l_whence = SEEK_SET;
  if (fcntl(lock->descriptor, F_SETLK, &request) != 0) {
    failed = 1;
  }
  if (close(lock->descriptor) != 0) {
    failed = 1;
  }
  lock->descriptor = -1;
  if (failed) {
    return confit_host_fail(diagnostic, CONFIT_ERR_IO, kUnlockFailed);
  }
  return CONFIT_OK;
}

static int confit_host_leaf_is_valid(const char *leaf, size_t maximum) {
  size_t size;
  return confit_host_bounded_length(leaf, maximum, &size) && size != 0U &&
         strchr(leaf, '/') == 0 && confit_host_relative_path_is_valid(leaf);
}

static int confit_host_open_or_create_directory(ConfitHostRoot *root,
                                                 const char *relative_path,
                                                 int *out_descriptor) {
  char leaf[CONFIT_LIMIT_SOURCE_PATH_BYTES + 1U];
  struct stat information;
  int descriptor;
  int parent;
  int parent_owned;
  if (root == 0 || out_descriptor == 0 ||
      !confit_host_open_parent(root, relative_path, &parent, &parent_owned,
                               leaf, sizeof(leaf))) {
    return 0;
  }
  descriptor = confit_host_open_directory_component(parent, leaf);
  if (descriptor < 0 && errno == ENOENT) {
    if (mkdirat(parent, leaf, 0700) != 0) {
      if (parent_owned) (void)close(parent);
      return 0;
    }
    if (fsync(parent) != 0) {
      if (parent_owned) (void)close(parent);
      return 0;
    }
    descriptor = confit_host_open_directory_component(parent, leaf);
  }
  if (parent_owned) (void)close(parent);
  if (descriptor < 0 || fstat(descriptor, &information) != 0 ||
      !S_ISDIR(information.st_mode)) {
    if (descriptor >= 0) (void)close(descriptor);
    return 0;
  }
  *out_descriptor = descriptor;
  return 1;
}

ConfitStatus confit_host_directory_transaction_begin(
    ConfitHostRoot *root, const char *parent_path,
    ConfitHostDirectoryTransaction **out_transaction,
    ConfitDiagnostic *diagnostic) {
  ConfitHostDirectoryTransaction *transaction;
  unsigned attempt;
  int descriptor = -1;
  if (root == 0 || parent_path == 0 || out_transaction == 0 ||
      !confit_host_relative_path_is_valid(parent_path)) {
    return confit_host_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  }
  *out_transaction = 0;
  transaction = (ConfitHostDirectoryTransaction *)root->allocator.allocate(
      root->allocator.context, sizeof(*transaction));
  if (transaction == 0)
    return confit_host_fail(diagnostic, CONFIT_ERR_INTERNAL, kOutOfMemory);
  memset(transaction, 0, sizeof(*transaction));
  transaction->root = root;
  transaction->allocator = root->allocator;
  transaction->parent_descriptor = -1;
  transaction->directory_descriptor = -1;
  if (!confit_host_open_or_create_directory(root, parent_path,
                                             &transaction->parent_descriptor))
    goto fail;
  for (attempt = 0U; attempt < CONFIT_HOST_CANDIDATE_ATTEMPTS; ++attempt) {
    const unsigned sequence = atomic_fetch_add_explicit(
        &confit_host_candidate_sequence, 1U, memory_order_relaxed);
    const int length = snprintf(transaction->candidate_leaf,
                                sizeof(transaction->candidate_leaf),
                                ".confit-snapshot-%ld-%u", (long)getpid(),
                                sequence);
    if (length < 0 || (size_t)length >= sizeof(transaction->candidate_leaf))
      goto fail;
    if (mkdirat(transaction->parent_descriptor,
                transaction->candidate_leaf, 0700) == 0) {
      descriptor = confit_host_open_directory_component(
          transaction->parent_descriptor, transaction->candidate_leaf);
      if (descriptor < 0) goto fail;
      break;
    }
    if (errno != EEXIST) goto fail;
  }
  if (descriptor < 0) goto fail;
  transaction->directory_descriptor = descriptor;
  if (snprintf(transaction->relative_path,
               sizeof(transaction->relative_path), "%s/%s", parent_path,
               transaction->candidate_leaf) < 0 ||
      !confit_host_relative_path_is_valid(transaction->relative_path))
    goto fail;
  transaction->active = 1;
  *out_transaction = transaction;
  return CONFIT_OK;

fail:
  if (descriptor >= 0) (void)close(descriptor);
  if (transaction->parent_descriptor >= 0 &&
      transaction->candidate_leaf[0] != '\0')
    (void)unlinkat(transaction->parent_descriptor,
                   transaction->candidate_leaf, AT_REMOVEDIR);
  if (transaction->parent_descriptor >= 0)
    (void)close(transaction->parent_descriptor);
  transaction->allocator.deallocate(transaction->allocator.context,
                                    transaction);
  return confit_host_fail(diagnostic, CONFIT_ERR_IO, kDirectoryFailed);
}

const char *confit_host_directory_transaction_relative_path(
    const ConfitHostDirectoryTransaction *transaction) {
  return transaction != 0 && transaction->active
             ? transaction->relative_path
             : 0;
}

static int confit_host_verify_file_bytes(int parent, const char *leaf,
                                         const unsigned char *bytes,
                                         size_t size) {
  unsigned char buffer[4096];
  struct stat information;
  size_t offset = 0U;
  int descriptor = openat(parent, leaf,
                          O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (descriptor < 0 || fstat(descriptor, &information) != 0 ||
      !S_ISREG(information.st_mode) || information.st_nlink != 1 ||
      information.st_size < 0 ||
      (uint64_t)information.st_size != (uint64_t)size) {
    if (descriptor >= 0) (void)close(descriptor);
    return 0;
  }
  while (offset < size) {
    const size_t wanted = size - offset < sizeof(buffer)
                              ? size - offset
                              : sizeof(buffer);
    ssize_t amount = pread(descriptor, buffer, wanted, (off_t)offset);
    if (amount < 0 && errno == EINTR) continue;
    if (amount <= 0 || (size_t)amount != wanted ||
        memcmp(buffer, bytes + offset, wanted) != 0) {
      (void)close(descriptor);
      return 0;
    }
    offset += wanted;
  }
  if (close(descriptor) != 0) return 0;
  return 1;
}

ConfitStatus confit_host_directory_transaction_write(
    ConfitHostDirectoryTransaction *transaction, const char *leaf,
    const void *bytes, size_t size, unsigned permissions,
    ConfitDiagnostic *diagnostic) {
  const unsigned char *source = (const unsigned char *)bytes;
  struct stat information;
  int descriptor;
  int write_ok;
  if (transaction == 0 || !transaction->active || transaction->sealed ||
      !confit_host_leaf_is_valid(
          leaf, CONFIT_LIMIT_SNAPSHOT_ARTIFACT_NAME_BYTES) ||
      (bytes == 0 && size != 0U) || size > CONFIT_LIMIT_SNAPSHOT_BYTES ||
      permissions > 0777U ||
      transaction->created_count >= CONFIT_LIMIT_SNAPSHOT_ARTIFACTS) {
    return confit_host_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  }
  descriptor = openat(transaction->directory_descriptor, leaf,
                      O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                      0600);
  if (descriptor < 0)
    return confit_host_fail(diagnostic, CONFIT_ERR_IO,
                            kDirectoryWriteFailed);
  memcpy(transaction->created[transaction->created_count], leaf,
         strlen(leaf) + 1U);
  transaction->created_count += 1U;
  write_ok = fstat(descriptor, &information) == 0 &&
             S_ISREG(information.st_mode) && information.st_nlink == 1 &&
             confit_host_write_all(descriptor, source, size) &&
             fchmod(descriptor, (mode_t)permissions) == 0 &&
             fsync(descriptor) == 0;
  if (!write_ok) {
    (void)close(descriptor);
    return confit_host_fail(diagnostic, CONFIT_ERR_IO,
                            kDirectoryWriteFailed);
  }
  if (close(descriptor) != 0)
    return confit_host_fail(diagnostic, CONFIT_ERR_IO,
                            kDirectoryWriteFailed);
  if (!confit_host_verify_file_bytes(transaction->directory_descriptor, leaf,
                                     source, size))
    return confit_host_fail(diagnostic, CONFIT_ERR_IO,
                            kDirectoryWriteFailed);
  return CONFIT_OK;
}

ConfitStatus confit_host_directory_transaction_seal(
    ConfitHostDirectoryTransaction *transaction, unsigned permissions,
    ConfitDiagnostic *diagnostic) {
  if (transaction == 0 || !transaction->active || transaction->sealed ||
      permissions > 0777U)
    return confit_host_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  if (fchmod(transaction->directory_descriptor, (mode_t)permissions) != 0 ||
      fsync(transaction->directory_descriptor) != 0)
    return confit_host_fail(diagnostic, CONFIT_ERR_IO, kDirectorySyncFailed);
  transaction->sealed = 1;
  return CONFIT_OK;
}

static int confit_host_existing_directory(int parent, const char *leaf) {
  struct stat information;
  if (fstatat(parent, leaf, &information, AT_SYMLINK_NOFOLLOW) != 0)
    return errno == ENOENT ? 0 : -1;
  return S_ISDIR(information.st_mode) ? 1 : -1;
}

ConfitStatus confit_host_directory_transaction_publish(
    ConfitHostDirectoryTransaction *transaction, const char *final_leaf,
    int *out_created, ConfitDiagnostic *diagnostic) {
  int exists;
  int renamed = 0;
  if (transaction == 0 || !transaction->active || !transaction->sealed ||
      out_created == 0 ||
      !confit_host_leaf_is_valid(
          final_leaf, CONFIT_LIMIT_SNAPSHOT_ARTIFACT_NAME_BYTES))
    return confit_host_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  *out_created = 0;
  exists = confit_host_existing_directory(transaction->parent_descriptor,
                                           final_leaf);
  if (exists < 0)
    return confit_host_fail(diagnostic, CONFIT_ERR_IO,
                            kDirectoryPublishFailed);
  if (exists > 0) return CONFIT_OK;
#if defined(__APPLE__)
  if (renameatx_np(transaction->parent_descriptor,
                   transaction->candidate_leaf,
                   transaction->parent_descriptor, final_leaf,
                   RENAME_EXCL) == 0)
    renamed = 1;
  else if (errno == EEXIST &&
           confit_host_existing_directory(transaction->parent_descriptor,
                                           final_leaf) > 0)
    return CONFIT_OK;
#else
  /*
   * POSIX renameat() may replace a destination created after the check above.
   * Until a host supplies an atomic no-replace directory rename, fail closed
   * instead of weakening the create-only snapshot invariant.
   */
  errno = ENOTSUP;
#endif
  if (!renamed)
    return confit_host_fail(diagnostic, CONFIT_ERR_IO,
                            kDirectoryPublishFailed);
  transaction->active = 0;
  *out_created = 1;
  if (fsync(transaction->parent_descriptor) != 0)
    return confit_host_fail(diagnostic, CONFIT_ERR_IO,
                            kDirectoryPublishFailed);
  return CONFIT_OK;
}

void confit_host_directory_transaction_destroy(
    ConfitHostDirectoryTransaction *transaction) {
  ConfitAllocator allocator;
  size_t index;
  if (transaction == 0) return;
  allocator = transaction->allocator;
  if (transaction->active && transaction->directory_descriptor >= 0) {
    (void)fchmod(transaction->directory_descriptor, 0700);
    for (index = transaction->created_count; index > 0U; --index)
      (void)unlinkat(transaction->directory_descriptor,
                     transaction->created[index - 1U], 0);
  }
  if (transaction->directory_descriptor >= 0)
    (void)close(transaction->directory_descriptor);
  if (transaction->active && transaction->parent_descriptor >= 0 &&
      transaction->candidate_leaf[0] != '\0')
    (void)unlinkat(transaction->parent_descriptor,
                   transaction->candidate_leaf, AT_REMOVEDIR);
  if (transaction->parent_descriptor >= 0)
    (void)close(transaction->parent_descriptor);
  memset(transaction, 0, sizeof(*transaction));
  allocator.deallocate(allocator.context, transaction);
}
