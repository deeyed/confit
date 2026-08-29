#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <util.h>

#include "test_assert.h"
#include "test_fs.h"

#define TEST_PATH_BYTES 2048U
#define TEST_TRANSCRIPT_BYTES (2U * 1024U * 1024U)

typedef struct TestTerminalResult {
  int exit_code;
  char *transcript;
  size_t transcript_size;
} TestTerminalResult;

static int test_write(const char *root, const char *relative,
                      const char *text) {
  char path[TEST_PATH_BYTES];
  return confit_test_fs_path_join(path, sizeof(path), root, relative) &&
         confit_test_fs_write_file(path, text);
}

static int test_make_dir(const char *root, const char *relative) {
  char path[TEST_PATH_BYTES];
  return confit_test_fs_path_join(path, sizeof(path), root, relative) &&
         confit_test_fs_make_dirs(path);
}

static int test_project(char *root, size_t root_size, char *output,
                        size_t output_size) {
  char resolved[TEST_PATH_BYTES];
  if (!confit_test_fs_make_temp_dir(root, root_size, "confit-r19-pty") ||
      realpath(root, resolved) == 0 || strlen(resolved) + 1U > root_size)
    return 0;
  memcpy(root, resolved, strlen(resolved) + 1U);
  if (!test_make_dir(root, "config") || !test_make_dir(root, "configs") ||
      !test_make_dir(root, "output") ||
      !confit_test_fs_path_join(output, output_size, root, "output") ||
      !test_write(root, "Confit.toml",
                  "schema_version = 6\n"
                  "mainmenu = \"Terminal test\"\n"
                  "source = [\"config/options.toml\"]\n") ||
      !test_write(root, "config/options.toml",
                  "[menu]\n"
                  "prompt = \"Runtime\"\n"
                  "help = \"Terminal-safe runtime options.\"\n\n"
                  "[[config]]\n"
                  "symbol = \"ENABLE_LOGGING\"\n"
                  "type = \"bool\"\n"
                  "prompt = \"Enable logging\"\n"
                  "help = \"Enable bounded logging output.\"\n"
                  "default = false\n\n"
                  "[[config]]\n"
                  "symbol = \"WORKER_COUNT\"\n"
                  "type = \"int\"\n"
                  "prompt = \"Worker count\"\n"
                  "help = \"Set the worker count.\"\n"
                  "default = 4\n"
                  "range = { min = 1, max = 64 }\n") ||
      !test_write(root, "configs/development.toml",
                  "schema_version = 6\n"
                  "[values]\n"
                  "ENABLE_LOGGING = true\n"))
    return 0;
  return 1;
}

static void test_result_destroy(TestTerminalResult *result) {
  if (result == 0)
    return;
  free(result->transcript);
  memset(result, 0, sizeof(*result));
  result->exit_code = -1;
}

static int test_terminal_run(const char *binary, const char *root,
                             const char *output, unsigned short columns,
                             unsigned short rows, const char *input_text,
                             unsigned short resize_columns,
                             unsigned short resize_rows, int signal_number,
                             TestTerminalResult *result) {
  struct winsize window;
  int master = -1;
  pid_t child;
  int child_status = 0;
  int input_sent = 0;
  int action_sent = 0;
  int exited = 0;
  size_t used = 0U;
  unsigned attempts;
  char *transcript;
  memset(result, 0, sizeof(*result));
  result->exit_code = -1;
  transcript = (char *)malloc(TEST_TRANSCRIPT_BYTES);
  if (transcript == 0)
    return 0;
  memset(&window, 0, sizeof(window));
  window.ws_col = columns;
  window.ws_row = rows;
  child = forkpty(&master, 0, 0, &window);
  if (child < 0) {
    free(transcript);
    return 0;
  }
  if (child == 0) {
    char *const arguments[] = {
        (char *)binary, "menuconfig",   "--root",   (char *)root,
        "--project",    "Confit.toml",  "--config", "configs/development.toml",
        "--output",     (char *)output, 0};
    (void)chdir(root);
    execv(binary, arguments);
    _exit(127);
  }
  for (attempts = 0U; attempts < 200U && !exited; ++attempts) {
    struct pollfd poll_fd;
    char bytes[4096];
    ssize_t count;
    pid_t waited;
    poll_fd.fd = master;
    poll_fd.events = POLLIN;
    poll_fd.revents = 0;
    (void)poll(&poll_fd, 1U, 25);
    if ((poll_fd.revents & POLLIN) != 0) {
      count = read(master, bytes, sizeof(bytes));
      if (count > 0) {
        const size_t available = TEST_TRANSCRIPT_BYTES - 1U - used;
        const size_t copied =
            (size_t)count < available ? (size_t)count : available;
        if (copied != 0U) {
          memcpy(transcript + used, bytes, copied);
          used += copied;
          transcript[used] = '\0';
        }
      }
    }
    if (!action_sent && strstr(transcript, "Confit menuconfig") != 0) {
      if (resize_columns != 0U && resize_rows != 0U) {
        window.ws_col = resize_columns;
        window.ws_row = resize_rows;
        if (ioctl(master, TIOCSWINSZ, &window) != 0)
          break;
        if (kill(child, SIGWINCH) != 0)
          break;
      }
      if (signal_number != 0) {
        if (kill(child, signal_number) != 0)
          break;
        action_sent = 1;
      } else if (resize_columns == 0U && input_text != 0) {
        const size_t input_size = strlen(input_text);
        if (write(master, input_text, input_size) != (ssize_t)input_size)
          break;
        input_sent = 1;
        action_sent = 1;
      } else if (resize_columns != 0U) {
        action_sent = 1;
      }
    } else if (action_sent && !input_sent && signal_number == 0 &&
               resize_columns != 0U && input_text != 0 &&
               strstr(transcript, "[tabbed]") != 0) {
      const size_t input_size = strlen(input_text);
      if (write(master, input_text, input_size) != (ssize_t)input_size)
        break;
      input_sent = 1;
    }
    waited = waitpid(child, &child_status, WNOHANG);
    if (waited == child)
      exited = 1;
  }
  if (!exited) {
    (void)kill(child, SIGKILL);
    (void)waitpid(child, &child_status, 0);
  }
  while (used + 1U < TEST_TRANSCRIPT_BYTES) {
    const ssize_t count =
        read(master, transcript + used, TEST_TRANSCRIPT_BYTES - 1U - used);
    if (count <= 0)
      break;
    used += (size_t)count;
  }
  (void)close(master);
  transcript[used] = '\0';
  result->transcript = transcript;
  result->transcript_size = used;
  if (exited && WIFEXITED(child_status))
    result->exit_code = WEXITSTATUS(child_status);
  else if (exited && WIFSIGNALED(child_status))
    result->exit_code = 128 + WTERMSIG(child_status);
  return exited;
}

static void test_clean_exit_and_layout(const char *binary, const char *root,
                                       const char *output) {
  TestTerminalResult result;
  CONFIT_TEST_ASSERT(test_terminal_run(binary, root, output, 100U, 20U, ":q\r",
                                       0U, 0U, 0, &result));
  if (result.exit_code != 0)
    (void)fprintf(stderr, "wide transcript: %s\n", result.transcript);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "[split]");
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "\033[?1049h");
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "\033[?25h\033[?1049l");
  test_result_destroy(&result);

  CONFIT_TEST_ASSERT(test_terminal_run(binary, root, output, 60U, 15U, ":q\r",
                                       0U, 0U, 0, &result));
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "[tabbed]");
  test_result_destroy(&result);

  CONFIT_TEST_ASSERT(test_terminal_run(binary, root, output, 600U, 300U, ":q\r",
                                       0U, 0U, 0, &result));
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT(result.transcript_size < TEST_TRANSCRIPT_BYTES);
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "[split]");
  test_result_destroy(&result);
}

static void test_tiny_refusal(const char *binary, const char *root,
                              const char *output) {
  TestTerminalResult result;
  CONFIT_TEST_ASSERT(
      test_terminal_run(binary, root, output, 39U, 10U, 0, 0U, 0U, 0, &result));
  CONFIT_TEST_ASSERT_EQ_INT(6, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript,
                              "terminal must be at least 40 columns");
  CONFIT_TEST_ASSERT_NOT_CONTAINS(result.transcript, "\033[?1049h");
  test_result_destroy(&result);
}

static void test_resize_and_escape_decoder(const char *binary, const char *root,
                                           const char *output) {
  TestTerminalResult result;
  const int ran = test_terminal_run(binary, root, output, 100U, 20U, ":q\r",
                                    60U, 15U, 0, &result);
  if (!ran)
    (void)fprintf(stderr, "resize transcript: %s\n", result.transcript);
  CONFIT_TEST_ASSERT(ran);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "[split]");
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "[tabbed]");
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "\033[?25h\033[?1049l");
  test_result_destroy(&result);

  CONFIT_TEST_ASSERT(test_terminal_run(binary, root, output, 100U, 20U,
                                       "\033[B\033[999~:q\r", 0U, 0U, 0,
                                       &result));
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "\033[?25h\033[?1049l");
  test_result_destroy(&result);
}

static void test_save_and_exit(const char *binary, const char *root,
                               const char *output) {
  TestTerminalResult result;
  char selected[TEST_PATH_BYTES];
  char snapshots[TEST_PATH_BYTES];
  char snapshot[TEST_PATH_BYTES];
  char user_values[TEST_PATH_BYTES];
  char *digest;
  char *bytes;
  CONFIT_TEST_ASSERT(test_terminal_run(binary, root, output, 100U, 20U,
                                       "\r :wq\r", 0U, 0U, 0, &result));
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT(
      confit_test_fs_path_join(selected, sizeof(selected), output, "selected"));
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(selected));
  digest = confit_test_fs_read_file(selected);
  CONFIT_TEST_ASSERT(digest != 0);
  if (strchr(digest, '\n') != 0)
    *strchr(digest, '\n') = '\0';
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(snapshots, sizeof(snapshots),
                                              output, "snapshots"));
  CONFIT_TEST_ASSERT(
      confit_test_fs_path_join(snapshot, sizeof(snapshot), snapshots, digest));
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(user_values, sizeof(user_values),
                                              snapshot, "user-values.toml"));
  bytes = confit_test_fs_read_file(user_values);
  CONFIT_TEST_ASSERT(bytes != 0);
  CONFIT_TEST_ASSERT(strcmp(bytes, "schema_version = 6\n\n[values]\n") == 0);
  confit_test_fs_free(bytes);
  confit_test_fs_free(digest);
  test_result_destroy(&result);
}

static void test_signal_restore(const char *binary, const char *root,
                                const char *output, int signal_number) {
  TestTerminalResult result;
  CONFIT_TEST_ASSERT(test_terminal_run(binary, root, output, 100U, 20U, 0, 0U,
                                       0U, signal_number, &result));
  CONFIT_TEST_ASSERT_EQ_INT(6, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "\033[?25h\033[?1049l");
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "menuconfig was interrupted");
  test_result_destroy(&result);
}

int main(int argc, char **argv) {
  char root[TEST_PATH_BYTES];
  char output[TEST_PATH_BYTES];
  if (argc != 2)
    CONFIT_TEST_FAIL("test_terminal requires product binary");
  CONFIT_TEST_ASSERT(test_project(root, sizeof(root), output, sizeof(output)));
  test_clean_exit_and_layout(argv[1], root, output);
  test_tiny_refusal(argv[1], root, output);
  test_resize_and_escape_decoder(argv[1], root, output);
  test_save_and_exit(argv[1], root, output);
  test_signal_restore(argv[1], root, output, SIGINT);
  test_signal_restore(argv[1], root, output, SIGTERM);
  test_signal_restore(argv[1], root, output, SIGHUP);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
  return 0;
}
