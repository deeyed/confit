#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "confit/host.h"

#include "test_assert.h"
#include "test_fs.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST_PATH_BYTES 4096U
#define TEST_CANDIDATE_ATTEMPTS 128U

static void join_path(char *out, const char *root, const char *leaf) {
  CONFIT_TEST_ASSERT(
      confit_test_fs_path_join(out, TEST_PATH_BYTES, root, leaf));
}

static void write_exact(const char *path, const void *bytes, size_t size) {
  const unsigned char *source = (const unsigned char *)bytes;
  size_t offset = 0U;
  int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  CONFIT_TEST_ASSERT(descriptor >= 0);
  while (offset < size) {
    const ssize_t amount = write(descriptor, source + offset, size - offset);
    CONFIT_TEST_ASSERT(amount > 0);
    offset += (size_t)amount;
  }
  CONFIT_TEST_ASSERT(close(descriptor) == 0);
}

static void assert_text_file(const char *path, const char *expected) {
  char *actual = confit_test_fs_read_file(path);
  CONFIT_TEST_ASSERT(actual != 0);
  CONFIT_TEST_ASSERT(strcmp(actual, expected) == 0);
  confit_test_fs_free(actual);
}

static void test_relative_path_validation(void) {
  char maximum[CONFIT_LIMIT_SOURCE_PATH_BYTES + 1U];
  char overlong[CONFIT_LIMIT_SOURCE_PATH_BYTES + 2U];

  memset(maximum, 'a', sizeof(maximum));
  maximum[sizeof(maximum) - 1U] = '\0';
  memset(overlong, 'a', sizeof(overlong));
  overlong[sizeof(overlong) - 1U] = '\0';

  CONFIT_TEST_ASSERT(confit_host_relative_path_is_valid("Confit.toml"));
  CONFIT_TEST_ASSERT(
      confit_host_relative_path_is_valid("config/runtime.toml"));
  CONFIT_TEST_ASSERT(confit_host_relative_path_is_valid("space name.toml"));
  CONFIT_TEST_ASSERT(confit_host_relative_path_is_valid(maximum));
  CONFIT_TEST_ASSERT(!confit_host_relative_path_is_valid(0));
  CONFIT_TEST_ASSERT(!confit_host_relative_path_is_valid(""));
  CONFIT_TEST_ASSERT(!confit_host_relative_path_is_valid("/absolute.toml"));
  CONFIT_TEST_ASSERT(!confit_host_relative_path_is_valid("."));
  CONFIT_TEST_ASSERT(!confit_host_relative_path_is_valid(".."));
  CONFIT_TEST_ASSERT(!confit_host_relative_path_is_valid("a/./b"));
  CONFIT_TEST_ASSERT(!confit_host_relative_path_is_valid("a/../b"));
  CONFIT_TEST_ASSERT(!confit_host_relative_path_is_valid("a//b"));
  CONFIT_TEST_ASSERT(!confit_host_relative_path_is_valid("a/b/"));
  CONFIT_TEST_ASSERT(!confit_host_relative_path_is_valid("a\\b"));
  CONFIT_TEST_ASSERT(!confit_host_relative_path_is_valid("a*.toml"));
  CONFIT_TEST_ASSERT(!confit_host_relative_path_is_valid("a?.toml"));
  CONFIT_TEST_ASSERT(!confit_host_relative_path_is_valid("a[0].toml"));
  CONFIT_TEST_ASSERT(!confit_host_relative_path_is_valid("a{b}.toml"));
  CONFIT_TEST_ASSERT(!confit_host_relative_path_is_valid("a\033b"));
  CONFIT_TEST_ASSERT(!confit_host_relative_path_is_valid("a\302\200b"));
  CONFIT_TEST_ASSERT(!confit_host_relative_path_is_valid("a\300\200b"));
  CONFIT_TEST_ASSERT(!confit_host_relative_path_is_valid(overlong));
}

static void test_root_and_no_follow(const char *root_path,
                                    ConfitHostRoot **out_root) {
  char directory[TEST_PATH_BYTES];
  char file[TEST_PATH_BYTES];
  char link[TEST_PATH_BYTES];
  ConfitDiagnostic diagnostic;
  ConfitHostRoot *root = 0;
  ConfitHostRoot *invalid = 0;

  confit_diagnostic_init(&diagnostic);
  join_path(directory, root_path, "real-root");
  join_path(file, root_path, "not-a-root");
  join_path(link, root_path, "root-link");
  CONFIT_TEST_ASSERT(mkdir(directory, 0700) == 0);
  write_exact(file, "file", 4U);
  CONFIT_TEST_ASSERT(symlink("real-root", link) == 0);

  CONFIT_TEST_ASSERT(confit_host_root_open_absolute(
                         root_path, 0, &root, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(root != 0);
  CONFIT_TEST_ASSERT(confit_host_root_open_absolute(
                         file, 0, &invalid, &diagnostic) == CONFIT_ERR_IO);
  CONFIT_TEST_ASSERT(invalid == 0);
  CONFIT_TEST_ASSERT(confit_host_root_open_absolute(
                         link, 0, &invalid, &diagnostic) == CONFIT_ERR_IO);
  CONFIT_TEST_ASSERT(invalid == 0);
  CONFIT_TEST_ASSERT(confit_host_root_open_absolute(
                         "relative", 0, &invalid, &diagnostic) ==
                     CONFIT_ERR_IO);
  CONFIT_TEST_ASSERT(confit_host_root_open_absolute(
                         "/private//tmp", 0, &invalid, &diagnostic) ==
                     CONFIT_ERR_IO);
  *out_root = root;
}

static void test_regular_file_read(const char *root_path,
                                   ConfitHostRoot *root) {
  char directory[TEST_PATH_BYTES];
  char file_path[TEST_PATH_BYTES];
  char final_link[TEST_PATH_BYTES];
  char intermediate_link[TEST_PATH_BYTES];
  char fifo_path[TEST_PATH_BYTES];
  char socket_path[TEST_PATH_BYTES];
  struct sockaddr_un address;
  ConfitDiagnostic diagnostic;
  ConfitHostBuffer buffer;
  ConfitHostFile *file = 0;
  int socket_descriptor;

  confit_diagnostic_init(&diagnostic);
  confit_host_buffer_init(&buffer);
  join_path(directory, root_path, "input");
  CONFIT_TEST_ASSERT(mkdir(directory, 0700) == 0);
  join_path(file_path, root_path, "input/value.toml");
  write_exact(file_path, "value", 5U);
  CONFIT_TEST_ASSERT(confit_host_file_open(root, "input/value.toml", &file,
                                           &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_host_file_read(file, 5U, 0, &buffer,
                                           &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(buffer.size == 5U);
  CONFIT_TEST_ASSERT(buffer.bytes[5] == '\0');
  CONFIT_TEST_ASSERT(memcmp(buffer.bytes, "value", 5U) == 0);
  CONFIT_TEST_ASSERT(buffer.identity.size == 5U);
  CONFIT_TEST_ASSERT(confit_host_file_read(
                         file, CONFIT_LIMIT_TOTAL_INPUT_BYTES + 1U, 0, &buffer,
                         &diagnostic) == CONFIT_ERR_IO);
  CONFIT_TEST_ASSERT(buffer.size == 5U &&
                     memcmp(buffer.bytes, "value", 5U) == 0);
  confit_host_file_destroy(file);
  file = 0;

  CONFIT_TEST_ASSERT(confit_host_file_open(root, "input/value.toml", &file,
                                           &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_host_file_read(file, 4U, 0, &buffer,
                                           &diagnostic) == CONFIT_ERR_IO);
  CONFIT_TEST_ASSERT(buffer.size == 5U &&
                     memcmp(buffer.bytes, "value", 5U) == 0);
  confit_host_file_destroy(file);
  file = 0;

  join_path(final_link, root_path, "input/final-link.toml");
  CONFIT_TEST_ASSERT(symlink("value.toml", final_link) == 0);
  CONFIT_TEST_ASSERT(confit_host_file_open(root, "input/final-link.toml", &file,
                                           &diagnostic) == CONFIT_ERR_IO);
  CONFIT_TEST_ASSERT(file == 0);
  join_path(intermediate_link, root_path, "input-link");
  CONFIT_TEST_ASSERT(symlink("input", intermediate_link) == 0);
  CONFIT_TEST_ASSERT(confit_host_file_open(root, "input-link/value.toml", &file,
                                           &diagnostic) == CONFIT_ERR_IO);
  CONFIT_TEST_ASSERT(file == 0);
  CONFIT_TEST_ASSERT(confit_host_file_open(root, "input", &file,
                                           &diagnostic) == CONFIT_ERR_IO);
  CONFIT_TEST_ASSERT(file == 0);

  join_path(fifo_path, root_path, "input/channel");
  CONFIT_TEST_ASSERT(mkfifo(fifo_path, 0600) == 0);
  CONFIT_TEST_ASSERT(confit_host_file_open(root, "input/channel", &file,
                                           &diagnostic) == CONFIT_ERR_IO);
  CONFIT_TEST_ASSERT(file == 0);

  join_path(socket_path, root_path, "input/socket");
  socket_descriptor = socket(AF_UNIX, SOCK_STREAM, 0);
  CONFIT_TEST_ASSERT(socket_descriptor >= 0);
  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  CONFIT_TEST_ASSERT(strlen(socket_path) < sizeof(address.sun_path));
  memcpy(address.sun_path, socket_path, strlen(socket_path) + 1U);
#if defined(__APPLE__)
  address.sun_len = (unsigned char)(offsetof(struct sockaddr_un, sun_path) +
                                    strlen(address.sun_path) + 1U);
#endif
  CONFIT_TEST_ASSERT(bind(socket_descriptor, (struct sockaddr *)&address,
                          (socklen_t)(offsetof(struct sockaddr_un, sun_path) +
                                      strlen(address.sun_path) + 1U)) == 0);
  CONFIT_TEST_ASSERT(confit_host_file_open(root, "input/socket", &file,
                                           &diagnostic) == CONFIT_ERR_IO);
  CONFIT_TEST_ASSERT(file == 0);
  CONFIT_TEST_ASSERT(close(socket_descriptor) == 0);

  join_path(file_path, root_path, "input/empty.toml");
  write_exact(file_path, "", 0U);
  CONFIT_TEST_ASSERT(confit_host_file_open(root, "input/empty.toml", &file,
                                           &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_host_file_read(file, 0U, 0, &buffer,
                                           &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(buffer.size == 0U && buffer.bytes[0] == '\0');
  confit_host_file_destroy(file);
  file = 0;

  {
    static const unsigned char embedded_nul[] = {'a', 0U, 'b'};
    join_path(file_path, root_path, "input/binary.toml");
    write_exact(file_path, embedded_nul, sizeof(embedded_nul));
    CONFIT_TEST_ASSERT(confit_host_file_open(root, "input/binary.toml", &file,
                                             &diagnostic) == CONFIT_OK);
    CONFIT_TEST_ASSERT(confit_host_file_read(file, sizeof(embedded_nul), 0,
                                             &buffer, &diagnostic) == CONFIT_OK);
    CONFIT_TEST_ASSERT(buffer.size == sizeof(embedded_nul));
    CONFIT_TEST_ASSERT(memcmp(buffer.bytes, embedded_nul,
                              sizeof(embedded_nul)) == 0);
    confit_host_file_destroy(file);
  }

  confit_host_buffer_destroy(&buffer);
}

static void test_growth_and_shrink(const char *root_path,
                                   ConfitHostRoot *root) {
  char path[TEST_PATH_BYTES];
  ConfitDiagnostic diagnostic;
  ConfitHostBuffer buffer;
  ConfitHostFile *file = 0;

  confit_diagnostic_init(&diagnostic);
  confit_host_buffer_init(&buffer);
  join_path(path, root_path, "changing.toml");

  write_exact(path, "ab", 2U);
  CONFIT_TEST_ASSERT(confit_host_file_open(root, "changing.toml", &file,
                                           &diagnostic) == CONFIT_OK);
  write_exact(path, "abcd", 4U);
  CONFIT_TEST_ASSERT(confit_host_file_read(file, 16U, 0, &buffer,
                                           &diagnostic) == CONFIT_ERR_IO);
  CONFIT_TEST_ASSERT(buffer.bytes == 0);
  confit_host_file_destroy(file);
  file = 0;

  write_exact(path, "abcd", 4U);
  CONFIT_TEST_ASSERT(confit_host_file_open(root, "changing.toml", &file,
                                           &diagnostic) == CONFIT_OK);
  write_exact(path, "ab", 2U);
  CONFIT_TEST_ASSERT(confit_host_file_read(file, 16U, 0, &buffer,
                                           &diagnostic) == CONFIT_ERR_IO);
  CONFIT_TEST_ASSERT(buffer.bytes == 0);
  confit_host_file_destroy(file);
  confit_host_buffer_destroy(&buffer);
}

static void test_device_rejection(void) {
  ConfitDiagnostic diagnostic;
  ConfitHostFile *file = 0;
  ConfitHostRoot *root = 0;

  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_host_root_open_absolute(
                         "/dev", 0, &root, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_host_file_open(root, "null", &file,
                                           &diagnostic) == CONFIT_ERR_IO);
  CONFIT_TEST_ASSERT(file == 0);
  confit_host_root_destroy(root);
}

static void test_candidate_collision_and_atomic_replace(const char *root_path,
                                                        ConfitHostRoot *root) {
  char candidate[TEST_PATH_BYTES];
  char destination[TEST_PATH_BYTES];
  char victim[TEST_PATH_BYTES];
  char unrelated[TEST_PATH_BYTES];
  ConfitDiagnostic diagnostic;
  struct stat information;
  unsigned index;

  confit_diagnostic_init(&diagnostic);
  join_path(destination, root_path, "destination.txt");
  join_path(victim, root_path, "victim.txt");
  join_path(unrelated, root_path, "unrelated.txt");
  write_exact(destination, "old", 3U);
  write_exact(victim, "victim", 6U);
  write_exact(unrelated, "unrelated", 9U);

  for (index = 0U; index < TEST_CANDIDATE_ATTEMPTS; ++index) {
    char leaf[96];
    const int length = snprintf(leaf, sizeof(leaf),
                                ".confit-candidate-%ld-%u", (long)getpid(),
                                index);
    CONFIT_TEST_ASSERT(length > 0 && (size_t)length < sizeof(leaf));
    join_path(candidate, root_path, leaf);
    CONFIT_TEST_ASSERT(symlink("victim.txt", candidate) == 0);
  }
  CONFIT_TEST_ASSERT(confit_host_atomic_replace(
                         root, "destination.txt", "new", 3U, 0640U,
                         &diagnostic) ==
                     CONFIT_ERR_IO);
  assert_text_file(destination, "old");
  assert_text_file(victim, "victim");
  assert_text_file(unrelated, "unrelated");
  for (index = 0U; index < TEST_CANDIDATE_ATTEMPTS; ++index) {
    char leaf[96];
    (void)snprintf(leaf, sizeof(leaf), ".confit-candidate-%ld-%u",
                   (long)getpid(), index);
    join_path(candidate, root_path, leaf);
    CONFIT_TEST_ASSERT(unlink(candidate) == 0);
  }

  CONFIT_TEST_ASSERT(confit_host_atomic_replace(
                         root, "destination.txt", "new", 3U, 0640U,
                         &diagnostic) ==
                     CONFIT_OK);
  assert_text_file(destination, "new");
  CONFIT_TEST_ASSERT(stat(destination, &information) == 0);
  CONFIT_TEST_ASSERT((information.st_mode & 0777) == 0640);
  assert_text_file(unrelated, "unrelated");
}

static void test_unsafe_destination(const char *root_path,
                                    ConfitHostRoot *root) {
  char intermediate_link[TEST_PATH_BYTES];
  char real_directory[TEST_PATH_BYTES];
  char real_file[TEST_PATH_BYTES];
  char selected[TEST_PATH_BYTES];
  char victim[TEST_PATH_BYTES];
  ConfitDiagnostic diagnostic;

  confit_diagnostic_init(&diagnostic);
  join_path(selected, root_path, "selected");
  join_path(victim, root_path, "selected-victim");
  write_exact(victim, "safe", 4U);
  CONFIT_TEST_ASSERT(symlink("selected-victim", selected) == 0);
  CONFIT_TEST_ASSERT(confit_host_atomic_replace(root, "selected", "digest\n",
                                                7U, 0600U, &diagnostic) ==
                     CONFIT_ERR_IO);
  assert_text_file(victim, "safe");
  CONFIT_TEST_ASSERT(unlink(selected) == 0);
  CONFIT_TEST_ASSERT(mkdir(selected, 0700) == 0);
  CONFIT_TEST_ASSERT(confit_host_atomic_replace(root, "selected", "digest\n",
                                                7U, 0600U, &diagnostic) ==
                     CONFIT_ERR_IO);
  CONFIT_TEST_ASSERT(rmdir(selected) == 0);

  join_path(real_directory, root_path, "output-real");
  join_path(real_file, root_path, "output-real/value");
  join_path(intermediate_link, root_path, "output-link");
  CONFIT_TEST_ASSERT(mkdir(real_directory, 0700) == 0);
  write_exact(real_file, "old", 3U);
  CONFIT_TEST_ASSERT(symlink("output-real", intermediate_link) == 0);
  CONFIT_TEST_ASSERT(confit_host_atomic_replace(root, "output-link/value",
                                                "new", 3U, 0600U,
                                                &diagnostic) == CONFIT_ERR_IO);
  assert_text_file(real_file, "old");
}

static void test_locking(const char *root_path, ConfitHostRoot *root) {
  char lock_path[TEST_PATH_BYTES];
  char victim[TEST_PATH_BYTES];
  ConfitDiagnostic diagnostic;
  ConfitHostLock lock;
  pid_t child;
  int status;

  confit_diagnostic_init(&diagnostic);
  confit_host_lock_init(&lock);
  join_path(lock_path, root_path, "writer.lock");
  join_path(victim, root_path, "lock-victim");
  write_exact(victim, "safe", 4U);
  CONFIT_TEST_ASSERT(symlink("lock-victim", lock_path) == 0);
  CONFIT_TEST_ASSERT(confit_host_lock_acquire(root, "writer.lock", &lock,
                                              &diagnostic) == CONFIT_ERR_IO);
  CONFIT_TEST_ASSERT(lock.descriptor == -1);
  assert_text_file(victim, "safe");
  CONFIT_TEST_ASSERT(unlink(lock_path) == 0);

  CONFIT_TEST_ASSERT(confit_host_lock_acquire(root, "writer.lock", &lock,
                                              &diagnostic) == CONFIT_OK);
  child = fork();
  CONFIT_TEST_ASSERT(child >= 0);
  if (child == 0) {
    ConfitDiagnostic child_diagnostic;
    ConfitHostLock contender;
    ConfitStatus result;
    confit_diagnostic_init(&child_diagnostic);
    confit_host_lock_init(&contender);
    result = confit_host_lock_acquire(root, "writer.lock", &contender,
                                      &child_diagnostic);
    if (result == CONFIT_OK) {
      (void)confit_host_lock_release(&contender, &child_diagnostic);
    }
    _exit(result == CONFIT_ERR_IO ? 0 : 1);
  }
  CONFIT_TEST_ASSERT(waitpid(child, &status, 0) == child);
  CONFIT_TEST_ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  CONFIT_TEST_ASSERT(confit_host_lock_release(&lock, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_host_lock_acquire(root, "writer.lock", &lock,
                                              &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_host_lock_release(&lock, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_host_lock_release(&lock, &diagnostic) == CONFIT_OK);
}

static int bytes_are_one_value(const char *bytes, size_t size, char value) {
  size_t index;
  for (index = 0U; index < size; ++index) {
    if (bytes[index] != value) {
      return 0;
    }
  }
  return 1;
}

static void test_concurrent_atomic_replace(const char *root_path,
                                           ConfitHostRoot *root) {
  char path[TEST_PATH_BYTES];
  char first[8192];
  char second[8192];
  char *result;
  pid_t children[2];
  size_t index;

  memset(first, 'A', sizeof(first));
  memset(second, 'B', sizeof(second));
  for (index = 0U; index < 2U; ++index) {
    children[index] = fork();
    CONFIT_TEST_ASSERT(children[index] >= 0);
    if (children[index] == 0) {
      ConfitDiagnostic diagnostic;
      const void *bytes = index == 0U ? (const void *)first
                                      : (const void *)second;
      confit_diagnostic_init(&diagnostic);
      _exit(confit_host_atomic_replace(root, "race.txt", bytes, sizeof(first),
                                       0600U, &diagnostic) == CONFIT_OK
                ? 0
                : 1);
    }
  }
  for (index = 0U; index < 2U; ++index) {
    int status;
    CONFIT_TEST_ASSERT(waitpid(children[index], &status, 0) == children[index]);
    CONFIT_TEST_ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  }
  join_path(path, root_path, "race.txt");
  result = confit_test_fs_read_file(path);
  CONFIT_TEST_ASSERT(result != 0);
  CONFIT_TEST_ASSERT(strlen(result) == sizeof(first));
  CONFIT_TEST_ASSERT(bytes_are_one_value(result, sizeof(first), 'A') ||
                     bytes_are_one_value(result, sizeof(first), 'B'));
  confit_test_fs_free(result);
}

int main(void) {
  char raw_root[TEST_PATH_BYTES];
  char root_path[TEST_PATH_BYTES];
  ConfitHostRoot *root = 0;

  test_relative_path_validation();
  CONFIT_TEST_ASSERT(confit_test_fs_make_temp_dir(raw_root, sizeof(raw_root),
                                                  "confit-r05"));
  CONFIT_TEST_ASSERT(realpath(raw_root, root_path) != 0);
  test_root_and_no_follow(root_path, &root);
  test_regular_file_read(root_path, root);
  test_growth_and_shrink(root_path, root);
  test_device_rejection();
  test_candidate_collision_and_atomic_replace(root_path, root);
  test_unsafe_destination(root_path, root);
  test_locking(root_path, root);
  test_concurrent_atomic_replace(root_path, root);
  confit_host_root_destroy(root);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root_path));
  return 0;
}
