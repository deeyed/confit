#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#include "confit/host.h"
#include "confit/digest.h"
#include "confit/version.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if !defined(_WIN32)
static int confit_host_decimal_invocation(const char *text) {
  size_t index;
  if (text == 0 || text[0] == '\0') return 0;
  for (index = 0U; text[index] != '\0'; ++index) {
    if (text[index] < '0' || text[index] > '9' || index >= 31U) return 0;
  }
  return 1;
}

static int confit_host_safe_build_root(const char *root, char parent[PATH_MAX],
                                       const char **leaf) {
  const char *separator;
  size_t parent_size;
  size_t index;
  if (root == 0 || root[0] != '/' || strlen(root) >= PATH_MAX ||
      strstr(root, "//") != 0 || strstr(root, "/../") != 0 ||
      strstr(root, "/./") != 0 || root[strlen(root) - 1U] == '/') return 0;
  separator = strrchr(root, '/');
  if (separator == 0 || separator == root || separator[1] == '\0') return 0;
  if (strcmp(separator + 1, ".") == 0 || strcmp(separator + 1, "..") == 0)
    return 0;
  for (index = 1U; separator[index] != '\0'; ++index) {
    const unsigned char value = (unsigned char)separator[index];
    if (!((value >= 'A' && value <= 'Z') ||
          (value >= 'a' && value <= 'z') ||
          (value >= '0' && value <= '9') || value == '.' || value == '_' ||
          value == '-')) return 0;
  }
  parent_size = (size_t)(separator - root);
  if (parent_size >= PATH_MAX) return 0;
  memcpy(parent, root, parent_size);
  parent[parent_size] = '\0';
  *leaf = separator + 1;
  return 1;
}

static int confit_host_open_directory_at(int parent, const char *name,
                                         int create, int *out_created) {
  int created = 0;
  int descriptor = openat(parent, name,
                          O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (descriptor < 0 && create && errno == ENOENT) {
    if (mkdirat(parent, name, 0700) == 0) {
      created = 1;
    } else if (errno != EEXIST) {
      return -1;
    }
    descriptor = openat(parent, name,
                        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  }
  if (descriptor >= 0 && out_created != 0) *out_created = created;
  return descriptor;
}

static int confit_host_create_directory_at(int parent, const char *name) {
  if (mkdirat(parent, name, 0700) != 0) return -1;
  return openat(parent, name,
                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
}

static int confit_host_write_all(int descriptor, const char *bytes,
                                 size_t size) {
  size_t offset = 0U;
  while (offset < size) {
    const ssize_t written = write(descriptor, bytes + offset, size - offset);
    if (written <= 0) return 0;
    offset += (size_t)written;
  }
  return 1;
}

static int confit_host_root_marker_text(
    char bytes[4096], const char *root, const char *repository, int root_fd,
    int repository_fd, int marker_fd) {
  struct stat root_metadata;
  struct stat repository_metadata;
  struct stat marker_metadata;
  if (fstat(root_fd, &root_metadata) != 0 ||
      fstat(repository_fd, &repository_metadata) != 0 ||
      fstat(marker_fd, &marker_metadata) != 0 ||
      !S_ISDIR(root_metadata.st_mode) ||
      !S_ISDIR(repository_metadata.st_mode) ||
      !S_ISREG(marker_metadata.st_mode) || marker_metadata.st_nlink != 1U) {
    return 0;
  }
  return snprintf(bytes, 4096U,
                  "PARUS-CONFIT-ROOT-V1\n"
                  "root=%s\nrepository=%s\n"
                  "root.device=%llu\nroot.inode=%llu\n"
                  "repository.device=%llu\nrepository.inode=%llu\n"
                  "marker.device=%llu\nmarker.inode=%llu\n",
                  root, repository,
                  (unsigned long long)root_metadata.st_dev,
                  (unsigned long long)root_metadata.st_ino,
                  (unsigned long long)repository_metadata.st_dev,
                  (unsigned long long)repository_metadata.st_ino,
                  (unsigned long long)marker_metadata.st_dev,
                  (unsigned long long)marker_metadata.st_ino);
}

static int confit_host_root_marker(int root_fd, int repository_fd,
                                   const char *root, const char *repository,
                                   int create) {
  static const char name[] = ".parus-root-v1";
  char actual[4096];
  char expected[4096];
  struct stat metadata;
  ssize_t got;
  int marker_fd;
  int length;
  if (create) {
    marker_fd = openat(root_fd, name,
                       O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                       0600);
    if (marker_fd < 0) return 0;
    length = confit_host_root_marker_text(expected, root, repository, root_fd,
                                          repository_fd, marker_fd);
    if (length <= 0 || (size_t)length >= sizeof(expected) ||
        !confit_host_write_all(marker_fd, expected, (size_t)length) ||
        fsync(marker_fd) != 0 || close(marker_fd) != 0 ||
        fsync(root_fd) != 0) {
      (void)unlinkat(root_fd, name, 0);
      return 0;
    }
    return 1;
  }
  marker_fd = openat(root_fd, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (marker_fd < 0 || fstat(marker_fd, &metadata) != 0 ||
      !S_ISREG(metadata.st_mode) || metadata.st_nlink != 1U ||
      metadata.st_size <= 0 || metadata.st_size >= (off_t)sizeof(actual)) {
    if (marker_fd >= 0) (void)close(marker_fd);
    return 0;
  }
  got = read(marker_fd, actual, sizeof(actual));
  length = confit_host_root_marker_text(expected, root, repository, root_fd,
                                        repository_fd, marker_fd);
  (void)close(marker_fd);
  return length > 0 && (size_t)length < sizeof(expected) &&
         got == (ssize_t)length && memcmp(actual, expected, (size_t)length) == 0;
}

typedef struct ConfitHostStage0Tool {
  char path[PATH_MAX];
  char sha256[65];
  char version[64];
  unsigned long long device;
  unsigned long long inode;
  unsigned long long size;
} ConfitHostStage0Tool;

#define CONFIT_STAGE0_ADMISSION_MAX_BYTES \
  (UINT64_C(8) * UINT64_C(1024) * UINT64_C(1024))
#define CONFIT_STAGE0_ADMISSION_OPERATION "parus-admit-c17-v1"
#define CONFIT_STAGE0_ADMISSION_VERSION "1.0.0"

static int confit_host_stage0_path(const char *path) {
  size_t index;
  if (path == 0 || path[0] != '/' || strlen(path) >= PATH_MAX ||
      strstr(path, "//") != 0 || strstr(path, "/../") != 0 ||
      strstr(path, "/./") != 0 || path[strlen(path) - 1U] == '/') return 0;
  for (index = 0U; path[index] != '\0'; ++index) {
    const unsigned char value = (unsigned char)path[index];
    if (!((value >= 'A' && value <= 'Z') ||
          (value >= 'a' && value <= 'z') ||
          (value >= '0' && value <= '9') || value == '.' || value == '_' ||
          value == '+' || value == '-' || value == '/')) return 0;
  }
  return 1;
}

/*
 * Darwin에는 fd-bound executable launch가 없으므로 stage-0 compiler pathname은
 * unprivileged writer가 교체할 수 없는 system hierarchy로 제한한다. 이후 build
 * toolchain은 ToolGEN seal이 별도로 소유한다.
 */
static int confit_host_immutable_system_executable(const char *path) {
  char copy[PATH_MAX];
  char *cursor;
  char *separator;
  int current = -1;
  if (!confit_host_stage0_path(path) || strlen(path) >= sizeof(copy)) return 0;
  memcpy(copy, path, strlen(path) + 1U);
  current = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (current < 0) return 0;
  cursor = copy + 1U;
  for (;;) {
    struct stat metadata;
    int next;
    separator = strchr(cursor, '/');
    if (separator != 0) *separator = '\0';
    next = openat(current, cursor,
                  (separator == 0 ? O_RDONLY : O_RDONLY | O_DIRECTORY) |
                      O_CLOEXEC | O_NOFOLLOW);
    (void)close(current);
    if (next < 0 || fstat(next, &metadata) != 0 || metadata.st_uid != 0U ||
        (metadata.st_mode & (S_IWGRP | S_IWOTH)) != 0 ||
        (separator == 0 &&
         (!S_ISREG(metadata.st_mode) || access(path, X_OK) != 0)) ||
        (separator != 0 && !S_ISDIR(metadata.st_mode))) {
      if (next >= 0) (void)close(next);
      return 0;
    }
    current = next;
    if (separator == 0) break;
    cursor = separator + 1U;
  }
  (void)close(current);
  return 1;
}

static int confit_host_numeric_version(const char *banner, char output[64]) {
  size_t start = 0U;
  size_t length = 0U;
  while (banner[start] != '\0' &&
         (banner[start] < '0' || banner[start] > '9')) ++start;
  while (banner[start + length] != '\0' &&
         ((banner[start + length] >= '0' &&
           banner[start + length] <= '9') ||
          banner[start + length] == '.')) {
    if (length + 1U >= 64U) return 0;
    ++length;
  }
  if (length == 0U || banner[start + length - 1U] == '.') return 0;
  memcpy(output, banner + start, length);
  output[length] = '\0';
  return 1;
}

static int confit_host_capture_bmake_version(const char *path,
                                             char output[64]) {
  int descriptors[2];
  pid_t child;
  char bytes[128];
  size_t used = 0U;
  int overflow = 0;
  int status;
  if (pipe(descriptors) != 0) return 0;
  child = fork();
  if (child == 0) {
    char *const arguments[] = {(char *)path, (char *)"-f",
                               (char *)"/dev/null", (char *)"-V",
                               (char *)"MAKE_VERSION", 0};
    char *const environment[] = {(char *)"PATH=/usr/bin:/bin",
                                 (char *)"LC_ALL=C", 0};
    (void)close(descriptors[0]);
    if (dup2(descriptors[1], STDOUT_FILENO) < 0) _exit(126);
    (void)close(descriptors[1]);
    execve(path, arguments, environment);
    _exit(127);
  }
  (void)close(descriptors[1]);
  if (child < 0) {
    (void)close(descriptors[0]);
    return 0;
  }
  for (;;) {
    char chunk[64];
    const ssize_t got = read(descriptors[0], chunk, sizeof(chunk));
    if (got == 0) break;
    if (got < 0) {
      if (errno == EINTR) continue;
      overflow = 1;
      break;
    }
    if ((size_t)got > sizeof(bytes) - 1U - used) {
      overflow = 1;
    } else if (!overflow) {
      memcpy(bytes + used, chunk, (size_t)got);
      used += (size_t)got;
    }
  }
  (void)close(descriptors[0]);
  while (waitpid(child, &status, 0) < 0) {
    if (errno != EINTR) return 0;
  }
  while (used > 0U && (bytes[used - 1U] == '\n' ||
                       bytes[used - 1U] == '\r')) --used;
  bytes[used] = '\0';
  if (overflow || used == 0U || !WIFEXITED(status) ||
      WEXITSTATUS(status) != 0) return 0;
  for (size_t index = 0U; index < used; ++index) {
    if (bytes[index] < '0' || bytes[index] > '9') return 0;
  }
  if (used + 1U > 64U) return 0;
  memcpy(output, bytes, used + 1U);
  return 1;
}

static int confit_host_measure_stage0_tool(
    const char *path, int kind, ConfitHostStage0Tool *out,
    ConfitDiagnostic *diagnostic) {
  char canonical[PATH_MAX];
  char banner[512];
  struct stat before;
  struct stat opened;
  struct stat after;
  int descriptor = -1;
  ConfitStatus status;
  if (out == 0 || !confit_host_stage0_path(path) ||
      realpath(path, canonical) == 0 || !confit_host_stage0_path(canonical) ||
      lstat(canonical, &before) != 0 || !S_ISREG(before.st_mode) ||
      before.st_nlink == 0U || before.st_size <= 0 ||
      access(canonical, X_OK) != 0) return 0;
  descriptor = open(canonical, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (descriptor < 0 || fstat(descriptor, &opened) != 0 ||
      opened.st_dev != before.st_dev || opened.st_ino != before.st_ino ||
      opened.st_size != before.st_size) {
    if (descriptor >= 0) (void)close(descriptor);
    return 0;
  }
  status = confit_v4_sha256_file(canonical, out->sha256, diagnostic);
  if (status != CONFIT_OK || lstat(canonical, &after) != 0 ||
      after.st_dev != opened.st_dev || after.st_ino != opened.st_ino ||
      after.st_size != opened.st_size) {
    (void)close(descriptor);
    return 0;
  }
  (void)close(descriptor);
  if (kind == 0) {
    char expected[96];
    status = confit_host_capture_first_line_argument(
        banner, sizeof(banner), canonical, "--version", diagnostic);
    if (status != CONFIT_OK ||
        snprintf(expected, sizeof(expected), "confit %s",
                 CONFIT_VERSION_RELEASE) <= 0 ||
        strcmp(banner, expected) != 0 ||
        strlen(CONFIT_VERSION_RELEASE) >= sizeof(out->version)) return 0;
    memcpy(out->version, CONFIT_VERSION_RELEASE,
           strlen(CONFIT_VERSION_RELEASE) + 1U);
  } else if (kind == 1) {
    if (!confit_host_capture_bmake_version(canonical, out->version)) return 0;
  } else if (kind == 2) {
    status = confit_host_capture_first_line_argument(
        banner, sizeof(banner), canonical, "--version", diagnostic);
    if (status != CONFIT_OK ||
        !confit_host_numeric_version(banner, out->version)) return 0;
  } else {
    static const char admission_banner[] =
        "parus-admit " CONFIT_STAGE0_ADMISSION_VERSION;
    status = confit_host_capture_first_line_argument(
        banner, sizeof(banner), canonical, "--version", diagnostic);
    if (status != CONFIT_OK || strcmp(banner, admission_banner) != 0) return 0;
    memcpy(out->version, CONFIT_STAGE0_ADMISSION_VERSION,
           sizeof(CONFIT_STAGE0_ADMISSION_VERSION));
  }
  memcpy(out->path, canonical, strlen(canonical) + 1U);
  out->device = (unsigned long long)opened.st_dev;
  out->inode = (unsigned long long)opened.st_ino;
  out->size = (unsigned long long)opened.st_size;
  return 1;
}

static int confit_host_stage0_receipt(
    int invocation_fd, const char *root, const char *repository,
    const char *invocation, const char *stage0_path, const char *bmake_path,
    const char *compiler_path, const char *source_path,
    const char *source_sha256, const char *admission_path,
    ConfitDiagnostic *diagnostic) {
  static const char receipt_name[] = ".parus-stage0-v1";
  ConfitHostStage0Tool stage0;
  ConfitHostStage0Tool bmake;
  ConfitHostStage0Tool compiler;
  ConfitHostStage0Tool admission;
  char bytes[8192];
  int descriptor = -1;
  int length;
  if (!confit_host_measure_stage0_tool(stage0_path, 0, &stage0, diagnostic)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, stage0_path, 0U,
                          0U, "stage-0 Confit identity measurement failed");
    return 0;
  }
  if (!confit_host_measure_stage0_tool(bmake_path, 1, &bmake, diagnostic)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, bmake_path, 0U,
                          0U, "stage-0 bmake identity measurement failed");
    return 0;
  }
  if (!confit_host_measure_stage0_tool(compiler_path, 2, &compiler,
                                      diagnostic)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, compiler_path,
                          0U, 0U,
                          "stage-0 C compiler identity measurement failed");
    return 0;
  }
  if (!confit_host_measure_stage0_tool(admission_path, 3, &admission,
                                      diagnostic)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, admission_path,
                          0U, 0U,
                          "generated admission identity measurement failed");
    return 0;
  }
  descriptor = openat(invocation_fd, receipt_name,
                      O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                      0600);
  if (descriptor < 0) return 0;
  length = snprintf(
      bytes, sizeof(bytes),
      "PARUS-STAGE0-RECEIPT-V1\n"
      "root=%s\nrepository=%s\ninvocation=%s\n"
      "stage0.path=%s\nstage0.sha256=%s\nstage0.version=%s\n"
      "stage0.device=%llu\nstage0.inode=%llu\nstage0.size=%llu\n"
      "bmake.path=%s\nbmake.sha256=%s\nbmake.version=%s\n"
      "bmake.device=%llu\nbmake.inode=%llu\nbmake.size=%llu\n"
      "compiler.path=%s\ncompiler.sha256=%s\ncompiler.version=%s\n"
      "compiler.device=%llu\ncompiler.inode=%llu\ncompiler.size=%llu\n"
      "source.path=%s\nsource.sha256=%s\n"
      "compile.operation=" CONFIT_STAGE0_ADMISSION_OPERATION "\n"
      "admission.path=%s\nadmission.sha256=%s\nadmission.version=%s\n"
      "admission.device=%llu\nadmission.inode=%llu\nadmission.size=%llu\n",
      root, repository, invocation,
      stage0.path, stage0.sha256, stage0.version,
      stage0.device, stage0.inode, stage0.size,
      bmake.path, bmake.sha256, bmake.version,
      bmake.device, bmake.inode, bmake.size,
      compiler.path, compiler.sha256, compiler.version,
      compiler.device, compiler.inode, compiler.size,
      source_path, source_sha256,
      admission.path, admission.sha256, admission.version,
      admission.device, admission.inode, admission.size);
  if (length <= 0 || (size_t)length >= sizeof(bytes) ||
      !confit_host_write_all(descriptor, bytes, (size_t)length) ||
      fchmod(descriptor, 0400) != 0 || fsync(descriptor) != 0 ||
      close(descriptor) != 0 ||
      fsync(invocation_fd) != 0) {
    if (descriptor >= 0) (void)close(descriptor);
    (void)unlinkat(invocation_fd, receipt_name, 0);
    return 0;
  }
  return 1;
}

static int confit_host_compile_admission(
    int parent_fd, const char *root_leaf, int root_fd, int bootstrap_fd,
    const char *invocation, int invocation_fd, const char *repository,
    const char *source, const char *compiler_path,
    char out_source_sha256[65], ConfitDiagnostic *diagnostic) {
  static const char stage_name[] = ".parus-admit-stage";
  static const char temporary_name[] = "parus-admit.bin";
  static const char output_name[] = "parus-admit";
  char canonical_source[PATH_MAX];
  char canonical_compiler[PATH_MAX];
  char source_sha256[65];
  char source_after_sha256[65];
  char source_define[128];
  char operation_define[128];
  struct stat source_before;
  struct stat source_after;
  struct stat root_metadata;
  struct stat current_root;
  struct stat invocation_metadata;
  struct stat current_invocation;
  struct stat output_metadata;
  struct stat sealed_output;
  pid_t child;
  int status;
  int stage_fd = -1;
  int output = -1;
  int output_linked = 0;
  const size_t repository_size = strlen(repository);
  if (source == 0 || compiler_path == 0 ||
      realpath(source, canonical_source) == 0 ||
      strcmp(source, canonical_source) != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, source, 0U, 0U,
                          "admission source is not one canonical path");
    return 0;
  }
  if (strncmp(source, repository, repository_size) != 0 ||
      source[repository_size] != '/') {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, source, 0U, 0U,
                          "admission source is outside the repository");
    return 0;
  }
  if (lstat(source, &source_before) != 0 ||
      !S_ISREG(source_before.st_mode) || source_before.st_nlink != 1U ||
      source_before.st_size <= 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, source, 0U, 0U,
                          "admission source is not one nonempty regular file");
    return 0;
  }
  if (confit_v4_sha256_file(canonical_source, source_sha256, diagnostic) !=
      CONFIT_OK) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, source, 0U, 0U,
                          "admission source digest measurement failed");
    return 0;
  }
  if (snprintf(source_define, sizeof(source_define),
               "-DPARUS_ADMIT_SOURCE_SHA256=\"%s\"", source_sha256) <= 0 ||
      snprintf(operation_define, sizeof(operation_define),
               "-DPARUS_ADMIT_COMPILE_OPERATION=\"%s\"",
               CONFIT_STAGE0_ADMISSION_OPERATION) <= 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, source, 0U, 0U,
                          "admission compile identity encoding failed");
    return 0;
  }
  if (realpath(compiler_path, canonical_compiler) == 0 ||
      !confit_host_stage0_path(canonical_compiler) ||
      !confit_host_immutable_system_executable(canonical_compiler)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, compiler_path,
                          0U, 0U,
                          "stage-0 compiler is outside the immutable system boundary");
    return 0;
  }
  if (fstat(root_fd, &root_metadata) != 0 ||
      fstat(invocation_fd, &invocation_metadata) != 0 ||
      fstatat(invocation_fd, output_name, &output_metadata,
              AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, source, 0U, 0U,
                          "admission output leaf boundary validation failed");
    return 0;
  }
  stage_fd = confit_host_create_directory_at(invocation_fd, stage_name);
  if (stage_fd < 0) return 0;
  if (fstatat(stage_fd, temporary_name, &output_metadata,
              AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT) goto cleanup;
  child = fork();
  if (child == 0) {
    struct rlimit file_limit;
    char *const arguments[] = {
        canonical_compiler, (char *)"-std=c17", (char *)"-O2",
        (char *)"-Wall", (char *)"-Wextra", (char *)"-Werror",
        (char *)"-pedantic", (char *)"-D_POSIX_C_SOURCE=200809L",
        (char *)"-D_DARWIN_C_SOURCE", source_define, operation_define,
        (char *)"-o",
        (char *)temporary_name, canonical_source, 0};
    char *const environment[] = {(char *)"PATH=/usr/bin:/bin",
                                 (char *)"LC_ALL=C",
                                 (char *)"LANG=C", 0};
    file_limit.rlim_cur = (rlim_t)CONFIT_STAGE0_ADMISSION_MAX_BYTES;
    file_limit.rlim_max = (rlim_t)CONFIT_STAGE0_ADMISSION_MAX_BYTES;
    if (fchdir(stage_fd) != 0 ||
        setrlimit(RLIMIT_FSIZE, &file_limit) != 0) _exit(126);
    (void)umask(077);
    execve(canonical_compiler, arguments, environment);
    _exit(127);
  }
  if (child < 0) goto cleanup;
  while (waitpid(child, &status, 0) < 0) {
    if (errno != EINTR) goto cleanup;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, source, 0U, 0U,
                          "stage-0 C compiler rejected the admission source");
    goto cleanup;
  }
  if (lstat(source, &source_after) != 0 ||
      source_after.st_dev != source_before.st_dev ||
      source_after.st_ino != source_before.st_ino ||
      source_after.st_size != source_before.st_size ||
      confit_v4_sha256_file(canonical_source, source_after_sha256,
                            diagnostic) != CONFIT_OK ||
      strcmp(source_after_sha256, source_sha256) != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, source, 0U, 0U,
                          "admission source identity changed during compilation");
    goto cleanup;
  }
  if (fstatat(parent_fd, root_leaf, &current_root, AT_SYMLINK_NOFOLLOW) != 0 ||
      !S_ISDIR(current_root.st_mode) ||
      current_root.st_dev != root_metadata.st_dev ||
      current_root.st_ino != root_metadata.st_ino ||
      fstatat(bootstrap_fd, invocation, &current_invocation,
              AT_SYMLINK_NOFOLLOW) != 0 ||
      !S_ISDIR(current_invocation.st_mode) ||
      current_invocation.st_dev != invocation_metadata.st_dev ||
      current_invocation.st_ino != invocation_metadata.st_ino) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, source, 0U, 0U,
                          "admission output root identity changed during compilation");
    goto cleanup;
  }
  output = openat(stage_fd, temporary_name,
                  O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (output < 0 || fstat(output, &output_metadata) != 0 ||
      !S_ISREG(output_metadata.st_mode) || output_metadata.st_nlink != 1U ||
      output_metadata.st_size <= 0 ||
      (uint64_t)output_metadata.st_size > CONFIT_STAGE0_ADMISSION_MAX_BYTES ||
      fchmod(output, 0500) != 0 || fsync(output) != 0 || close(output) != 0 ||
      fchmod(stage_fd, 0500) != 0) {
    if (output >= 0) (void)close(output);
    output = -1;
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, source, 0U, 0U,
                          "admission compiler output validation failed");
    goto cleanup;
  }
  output = -1;
  sealed_output = output_metadata;
  if (linkat(stage_fd, temporary_name, invocation_fd, output_name, 0) != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, source, 0U, 0U,
                          "admission output create-only publication failed");
    goto cleanup;
  }
  output_linked = 1;
  if (fstatat(invocation_fd, output_name, &output_metadata,
              AT_SYMLINK_NOFOLLOW) != 0 ||
      !S_ISREG(output_metadata.st_mode) || output_metadata.st_nlink != 2U ||
      output_metadata.st_dev != sealed_output.st_dev ||
      output_metadata.st_ino != sealed_output.st_ino ||
      fchmod(stage_fd, 0700) != 0 ||
      unlinkat(stage_fd, temporary_name, 0) != 0 || fsync(stage_fd) != 0 ||
      close(stage_fd) != 0 ||
      unlinkat(invocation_fd, stage_name, AT_REMOVEDIR) != 0 ||
      fsync(invocation_fd) != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, source, 0U, 0U,
                          "admission output publication identity check failed");
    goto cleanup;
  }
  stage_fd = -1;
  memcpy(out_source_sha256, source_sha256, sizeof(source_sha256));
  return 1;
cleanup:
  if (output >= 0) (void)close(output);
  if (stage_fd >= 0) {
    (void)fchmod(stage_fd, 0700);
    (void)unlinkat(stage_fd, temporary_name, 0);
    (void)close(stage_fd);
    (void)unlinkat(invocation_fd, stage_name, AT_REMOVEDIR);
  }
  if (output_linked) (void)unlinkat(invocation_fd, output_name, 0);
  return 0;
}
#endif

ConfitStatus confit_host_prepare_parus_build_root(
    const char *root, const char *repository, const char *invocation,
    const char *stage0_confit, const char *bmake, const char *host_compiler,
    ConfitDiagnostic *diagnostic) {
#if defined(_WIN32)
  (void)repository;
  (void)invocation;
  (void)stage0_confit;
  (void)bmake;
  (void)host_compiler;
  confit_diagnostic_set(diagnostic, CONFIT_ERR_UNSUPPORTED, root, 0U, 0U,
                        "Parus stage-0 root admission is unavailable on this host");
  return CONFIT_ERR_UNSUPPORTED;
#else
  char parent[PATH_MAX];
  char canonical[PATH_MAX];
  char admission_source[PATH_MAX];
  char admission_path[PATH_MAX];
  char source_sha256[65];
  const char *leaf = 0;
  struct stat before;
  struct stat after;
  int parent_fd = -1;
  int root_fd = -1;
  int repository_fd = -1;
  int bootstrap_fd = -1;
  int invocation_fd = -1;
  int invocation_created = 0;
  int source_length;
  int admission_length;
  int root_created = 0;
  ConfitStatus status = CONFIT_ERR_GENERATION;
  if (!confit_host_decimal_invocation(invocation) ||
      !confit_host_safe_build_root(root, parent, &leaf) ||
      lstat(parent, &before) != 0 || !S_ISDIR(before.st_mode) ||
      realpath(parent, canonical) == 0 || strcmp(parent, canonical) != 0 ||
      repository == 0 || repository[0] != '/' ||
      realpath(repository, canonical) == 0 || strcmp(repository, canonical) != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, root, 0U, 0U,
                          "build root parent or invocation is not canonical");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  source_length = snprintf(admission_source, sizeof(admission_source),
                           "%s/tools/host/admit/main.c", repository);
  if (source_length <= 0 || (size_t)source_length >= sizeof(admission_source)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, repository,
                          0U, 0U,
                          "Parus admission source path exceeds host limit");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  parent_fd = open(parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  repository_fd = open(repository,
                       O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (parent_fd < 0 || fstat(parent_fd, &after) != 0 ||
      before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
      repository_fd < 0 ||
      (root_fd = confit_host_open_directory_at(parent_fd, leaf, 1,
                                                &root_created)) < 0 ||
      flock(root_fd, LOCK_EX | LOCK_NB) != 0 ||
      !confit_host_root_marker(root_fd, repository_fd, root, repository,
                               root_created) ||
      (bootstrap_fd = confit_host_open_directory_at(
           root_fd, ".parus-admission-bootstrap", 1, 0)) < 0 ||
      fchmod(bootstrap_fd, 0700) != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, root, 0U, 0U,
                          "descriptor-rooted build admission directory failed");
    goto cleanup;
  }
  invocation_fd = confit_host_create_directory_at(bootstrap_fd, invocation);
  if (invocation_fd < 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, root, 0U, 0U,
                          "build invocation identity already exists");
    goto cleanup;
  }
  invocation_created = 1;
  admission_length = snprintf(
      admission_path, sizeof(admission_path),
      "%s/.parus-admission-bootstrap/%s/parus-admit", root, invocation);
  if (admission_length <= 0 ||
      (size_t)admission_length >= sizeof(admission_path)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, root, 0U, 0U,
                          "generated admission path exceeds the host limit");
    goto cleanup;
  }
  if (!confit_host_compile_admission(
          parent_fd, leaf, root_fd, bootstrap_fd, invocation, invocation_fd,
          repository, admission_source, host_compiler, source_sha256,
          diagnostic)) {
    if (diagnostic->message == 0 || diagnostic->message[0] == '\0') {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION,
                            admission_source, 0U, 0U,
                            "descriptor-rooted admission compilation failed");
    }
    goto cleanup;
  }
  if (!confit_host_stage0_receipt(
          invocation_fd, root, repository, invocation, stage0_confit, bmake,
          host_compiler, admission_source, source_sha256, admission_path,
          diagnostic)) {
    if (diagnostic->message == 0 || diagnostic->message[0] == '\0') {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, root, 0U, 0U,
                            "stage-0 tool receipt publication failed");
    }
    goto cleanup;
  }
  if (fchmod(invocation_fd, 0500) != 0 ||
      fchmod(bootstrap_fd, 0500) != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, root, 0U, 0U,
                          "stage-0 admission permissions could not be sealed");
    goto cleanup;
  }
  if (fsync(invocation_fd) != 0 || fsync(bootstrap_fd) != 0 ||
      fsync(root_fd) != 0 || fsync(parent_fd) != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, root, 0U, 0U,
                          "stage-0 admission directory sync failed");
    goto cleanup;
  }
  status = CONFIT_OK;
cleanup:
  if (status != CONFIT_OK && invocation_fd >= 0) {
    (void)fchmod(invocation_fd, 0700);
    (void)unlinkat(invocation_fd, ".parus-stage0-v1", 0);
    (void)unlinkat(invocation_fd, "parus-admit.tmp", 0);
    (void)unlinkat(invocation_fd, "parus-admit", 0);
  }
  if (invocation_fd >= 0) (void)close(invocation_fd);
  if (status != CONFIT_OK && invocation_created && bootstrap_fd >= 0) {
    (void)fchmod(bootstrap_fd, 0700);
    (void)unlinkat(bootstrap_fd, invocation, AT_REMOVEDIR);
  }
  if (bootstrap_fd >= 0) (void)fchmod(bootstrap_fd, 0500);
  if (bootstrap_fd >= 0) (void)close(bootstrap_fd);
  if (root_fd >= 0) (void)close(root_fd);
  if (repository_fd >= 0) (void)close(repository_fd);
  if (parent_fd >= 0) (void)close(parent_fd);
  return status;
#endif
}

static int confit_host_is_path_separator_local(char value) {
  return value == '/' || value == '\\';
}

int confit_host_directory_exists(const char *path) {
  if (path == 0 || path[0] == '\0') {
    return 0;
  }
#if defined(_WIN32)
  const DWORD attributes = GetFileAttributesA(path);
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U;
#else
  struct stat info;
  return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

static int confit_host_create_directory_once(const char *path) {
  if (confit_host_directory_exists(path)) {
    return 1;
  }

#if defined(_WIN32)
  if (CreateDirectoryA(path, 0) != 0) {
    return 1;
  }
  return GetLastError() == ERROR_ALREADY_EXISTS &&
         confit_host_directory_exists(path);
#else
  if (mkdir(path, 0777) == 0) {
    return 1;
  }
  return errno == EEXIST && confit_host_directory_exists(path);
#endif
}

static int confit_host_should_create_component(const char *path) {
  const size_t size = strlen(path);

  if (size == 0U) {
    return 0;
  }
  if (size == 2U && path[1] == ':') {
    return 0;
  }
  return 1;
}

ConfitStatus confit_host_make_directories(const char *path,
                                          ConfitDiagnostic *diagnostic) {
  char *copy;
  size_t size;
  size_t index;

  if (path == 0 || path[0] == '\0') {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, path, 0, 0,
                          "missing directory path");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  size = strlen(path);
  copy = (char *)malloc(size + 1U);
  if (copy == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, path, 0, 0,
                          "failed to allocate directory path");
    return CONFIT_ERR_INTERNAL;
  }
  memcpy(copy, path, size + 1U);

  for (index = 1U; index < size; ++index) {
    char saved;

    if (!confit_host_is_path_separator_local(copy[index])) {
      continue;
    }

    saved = copy[index];
    copy[index] = '\0';
    if (confit_host_should_create_component(copy) &&
        !confit_host_create_directory_once(copy)) {
      copy[index] = saved;
      free(copy);
      confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, path, 0, 0,
                            "failed to create output directory");
      return CONFIT_ERR_GENERATION;
    }
    copy[index] = saved;
  }

  if (!confit_host_create_directory_once(path)) {
    free(copy);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, path, 0, 0,
                          "failed to create output directory");
    return CONFIT_ERR_GENERATION;
  }

  free(copy);
  return CONFIT_OK;
}

static int confit_host_has_toml_suffix(const char *name) {
  const size_t size = name != 0 ? strlen(name) : 0U;
  return size > 5U && strcmp(name + size - 5U, ".toml") == 0;
}

static char *confit_host_make_child_path(const char *directory,
                                         const char *name) {
  const size_t directory_size = strlen(directory);
  const size_t name_size = strlen(name);
  const int needs_separator =
      directory_size > 0U && directory[directory_size - 1U] != '/' &&
      directory[directory_size - 1U] != '\\';
  const size_t total_size =
      directory_size + name_size + (needs_separator ? 1U : 0U);
  char *path;
  size_t offset;

  path = (char *)malloc(total_size + 1U);
  if (path == 0) {
    return 0;
  }

  offset = 0U;
  memcpy(path + offset, directory, directory_size);
  offset += directory_size;
  if (needs_separator) {
    path[offset] = confit_host_path_separator();
    offset += 1U;
  }
  memcpy(path + offset, name, name_size);
  offset += name_size;
  path[offset] = '\0';
  return path;
}

static ConfitStatus confit_host_append_path(char ***items, size_t *item_count,
                                            const char *directory,
                                            const char *name,
                                            ConfitDiagnostic *diagnostic) {
  char **new_items;
  char *path;

  path = confit_host_make_child_path(directory, name);
  if (path == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, directory, 0, 0,
                          "failed to allocate directory entry path");
    return CONFIT_ERR_INTERNAL;
  }

  new_items =
      (char **)realloc(*items, (*item_count + 1U) * sizeof((*items)[0]));
  if (new_items == 0) {
    free(path);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, directory, 0, 0,
                          "failed to allocate directory entry list");
    return CONFIT_ERR_INTERNAL;
  }

  *items = new_items;
  (*items)[*item_count] = path;
  *item_count += 1U;
  return CONFIT_OK;
}

static int confit_host_compare_strings(const void *left, const void *right) {
  const char *const *left_string = (const char *const *)left;
  const char *const *right_string = (const char *const *)right;
  return strcmp(*left_string, *right_string);
}

static void confit_host_sort_strings(char **items, size_t item_count) {
  if (item_count > 1U) {
    qsort(items, item_count, sizeof(items[0]), confit_host_compare_strings);
  }
}

#if defined(_WIN32)
static ConfitStatus confit_host_list_toml_files_impl(
    const char *directory, char ***out_paths, size_t *out_count,
    ConfitDiagnostic *diagnostic) {
  WIN32_FIND_DATAA data;
  HANDLE handle;
  char *pattern;
  size_t directory_size;
  size_t pattern_size;

  directory_size = strlen(directory);
  pattern_size = directory_size + 8U;
  pattern = (char *)malloc(pattern_size);
  if (pattern == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, directory, 0, 0,
                          "failed to allocate directory search pattern");
    return CONFIT_ERR_INTERNAL;
  }

  if (directory_size > 0U &&
      (directory[directory_size - 1U] == '/' ||
       directory[directory_size - 1U] == '\\')) {
    memcpy(pattern, directory, directory_size);
    memcpy(pattern + directory_size, "*.toml", 7U);
  } else {
    memcpy(pattern, directory, directory_size);
    pattern[directory_size] = confit_host_path_separator();
    memcpy(pattern + directory_size + 1U, "*.toml", 7U);
  }

  handle = FindFirstFileA(pattern, &data);
  free(pattern);
  if (handle == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();

    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
      return CONFIT_OK;
    }
    confit_diagnostic_set(diagnostic, CONFIT_ERR_PARSE, directory, 0, 0,
                          "failed to open directory");
    return CONFIT_ERR_PARSE;
  }

  do {
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U &&
        confit_host_has_toml_suffix(data.cFileName)) {
      ConfitStatus status = confit_host_append_path(
          out_paths, out_count, directory, data.cFileName, diagnostic);
      if (status != CONFIT_OK) {
        FindClose(handle);
        return status;
      }
    }
  } while (FindNextFileA(handle, &data) != 0);

  FindClose(handle);
  return CONFIT_OK;
}
#else
static ConfitStatus confit_host_list_toml_files_impl(
    const char *directory, char ***out_paths, size_t *out_count,
    ConfitDiagnostic *diagnostic) {
  DIR *dir;
  struct dirent *entry;

  errno = 0;
  dir = opendir(directory);
  if (dir == 0) {
    if (errno == ENOENT) {
      return CONFIT_OK;
    }
    confit_diagnostic_set(diagnostic, CONFIT_ERR_PARSE, directory, 0, 0,
                          "failed to open directory");
    return CONFIT_ERR_PARSE;
  }

  while ((entry = readdir(dir)) != 0) {
    ConfitStatus status;

    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
        !confit_host_has_toml_suffix(entry->d_name)) {
      continue;
    }

    status = confit_host_append_path(out_paths, out_count, directory,
                                     entry->d_name, diagnostic);
    if (status != CONFIT_OK) {
      closedir(dir);
      return status;
    }
  }

  if (closedir(dir) != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_PARSE, directory, 0, 0,
                          "failed to close directory");
    return CONFIT_ERR_PARSE;
  }

  return CONFIT_OK;
}
#endif

ConfitStatus confit_host_list_toml_files(const char *directory,
                                         char ***out_paths, size_t *out_count,
                                         ConfitDiagnostic *diagnostic) {
  ConfitStatus status;

  if (out_paths == 0 || out_count == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, directory, 0,
                          0, "missing directory list output");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  *out_paths = 0;
  *out_count = 0U;
  if (directory == 0 || directory[0] == '\0') {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, directory, 0,
                          0, "missing directory path");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status =
      confit_host_list_toml_files_impl(directory, out_paths, out_count,
                                       diagnostic);
  if (status != CONFIT_OK) {
    confit_host_string_list_free(*out_paths, *out_count);
    *out_paths = 0;
    *out_count = 0U;
    return status;
  }

  confit_host_sort_strings(*out_paths, *out_count);
  return CONFIT_OK;
}

void confit_host_string_list_free(char **items, size_t count) {
  size_t index;

  for (index = 0U; index < count; ++index) {
    free(items[index]);
  }
  free(items);
}

static ConfitStatus confit_host_append_named_path(char ***items,
                                                  size_t *item_count,
                                                  size_t max_count,
                                                  const char *path,
                                                  ConfitDiagnostic *diagnostic) {
  char **grown;
  char *copy;
  const size_t size = strlen(path);

  if (*item_count >= max_count) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, path, 0U, 0U,
                          "configuration member count exceeds the supported limit");
    return CONFIT_ERR_SCHEMA;
  }
  copy = (char *)malloc(size + 1U);
  if (copy == 0) return CONFIT_ERR_INTERNAL;
  memcpy(copy, path, size + 1U);
  grown = (char **)realloc(*items, (*item_count + 1U) * sizeof((*items)[0]));
  if (grown == 0) {
    free(copy);
    return CONFIT_ERR_INTERNAL;
  }
  *items = grown;
  (*items)[*item_count] = copy;
  *item_count += 1U;
  return CONFIT_OK;
}

#if !defined(_WIN32)
static ConfitStatus confit_host_list_named_files_recursive_impl(
    const char *directory, const char *file_name, size_t depth,
    size_t max_depth, size_t max_count, size_t max_file_bytes, char ***items,
    size_t *item_count, ConfitDiagnostic *diagnostic) {
  DIR *dir;
  struct dirent *entry;
  struct stat info;

  if (lstat(directory, &info) != 0) {
    if (errno == ENOENT) return CONFIT_OK;
    confit_diagnostic_set(diagnostic, CONFIT_ERR_PARSE, directory, 0U, 0U,
                          "failed to inspect configuration discovery root");
    return CONFIT_ERR_PARSE;
  }
  if (S_ISLNK(info.st_mode)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, directory, 0U, 0U,
                          "configuration discovery rejects symlink roots");
    return CONFIT_ERR_SCHEMA;
  }
  if (!S_ISDIR(info.st_mode)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, directory, 0U, 0U,
                          "configuration discovery root is not a directory");
    return CONFIT_ERR_SCHEMA;
  }
  dir = opendir(directory);
  if (dir == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_PARSE, directory, 0U, 0U,
                          "failed to open configuration discovery root");
    return CONFIT_ERR_PARSE;
  }
  while ((entry = readdir(dir)) != 0) {
    char *child;
    ConfitStatus status;
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
        entry->d_name[0] == '.') continue;
    child = confit_host_make_child_path(directory, entry->d_name);
    if (child == 0) {
      (void)closedir(dir);
      return CONFIT_ERR_INTERNAL;
    }
    if (lstat(child, &info) != 0) {
      free(child);
      (void)closedir(dir);
      confit_diagnostic_set(diagnostic, CONFIT_ERR_PARSE, directory, 0U, 0U,
                            "failed to inspect configuration entry");
      return CONFIT_ERR_PARSE;
    }
    if (S_ISLNK(info.st_mode)) {
      free(child);
      (void)closedir(dir);
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, directory, 0U, 0U,
                            "configuration discovery rejects symlink entries");
      return CONFIT_ERR_SCHEMA;
    }
    if (S_ISDIR(info.st_mode)) {
      if (depth >= max_depth) {
        free(child);
        (void)closedir(dir);
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, directory, 0U, 0U,
                              "configuration discovery depth exceeds the supported limit");
        return CONFIT_ERR_SCHEMA;
      }
      status = confit_host_list_named_files_recursive_impl(
          child, file_name, depth + 1U, max_depth, max_count, max_file_bytes,
          items, item_count, diagnostic);
      free(child);
      if (status != CONFIT_OK) {
        (void)closedir(dir);
        return status;
      }
      continue;
    }
    if (S_ISREG(info.st_mode) && strcmp(entry->d_name, file_name) == 0) {
      if ((uintmax_t)info.st_size > (uintmax_t)max_file_bytes) {
        free(child);
        (void)closedir(dir);
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, directory, 0U, 0U,
                              "configuration member exceeds the supported size");
        return CONFIT_ERR_SCHEMA;
      }
      status = confit_host_append_named_path(items, item_count, max_count, child,
                                             diagnostic);
      free(child);
      if (status != CONFIT_OK) {
        (void)closedir(dir);
        return status;
      }
      continue;
    }
    free(child);
  }
  if (closedir(dir) != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_PARSE, directory, 0U, 0U,
                          "failed to close configuration discovery root");
    return CONFIT_ERR_PARSE;
  }
  return CONFIT_OK;
}
#endif

ConfitStatus confit_host_list_named_files_recursive(
    const char *directory, const char *file_name, size_t max_depth,
    size_t max_count, size_t max_file_bytes, char ***out_paths,
    size_t *out_count, ConfitDiagnostic *diagnostic) {
  ConfitStatus status;
  if (directory == 0 || directory[0] == '\0' || file_name == 0 ||
      file_name[0] == '\0' || max_depth == 0U || max_count == 0U ||
      max_file_bytes == 0U || out_paths == 0 || out_count == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, directory, 0U,
                          0U, "invalid bounded component discovery argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_paths = 0;
  *out_count = 0U;
#if defined(_WIN32)
  (void)max_depth;
  (void)max_count;
  (void)max_file_bytes;
  confit_diagnostic_set(diagnostic, CONFIT_ERR_UNSUPPORTED, directory, 0U, 0U,
                        "bounded recursive component discovery is unsupported on this host");
  status = CONFIT_ERR_UNSUPPORTED;
#else
  status = confit_host_list_named_files_recursive_impl(
      directory, file_name, 0U, max_depth, max_count, max_file_bytes, out_paths,
      out_count, diagnostic);
#endif
  if (status != CONFIT_OK) {
    confit_host_string_list_free(*out_paths, *out_count);
    *out_paths = 0;
    *out_count = 0U;
    return status;
  }
  confit_host_sort_strings(*out_paths, *out_count);
  return CONFIT_OK;
}
