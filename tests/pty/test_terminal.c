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

typedef struct TestTerminalStep {
  const char *wait_for;
  const char *input;
  unsigned short resize_columns;
  unsigned short resize_rows;
} TestTerminalStep;

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
                  "range = { min = 1, max = 64 }\n\n"
                  "[[config]]\n"
                  "symbol = \"DEVICE_ID\"\n"
                  "type = \"hex\"\n"
                  "prompt = \"Device identifier\"\n"
                  "help = \"Set a bounded hexadecimal identifier.\"\n"
                  "default = 0x10e8\n"
                  "range = { min = 0x0, max = 0xffff }\n\n"
                  "[[config]]\n"
                  "symbol = \"INSTANCE_LABEL\"\n"
                  "type = \"string\"\n"
                  "prompt = \"Instance label\"\n"
                  "help = \"Set a descriptive instance label.\"\n"
                  "default = \"terminal-test\"\n\n"
                  "[[config]]\n"
                  "symbol = \"LOG_LEVEL\"\n"
                  "type = \"enum\"\n"
                  "prompt = \"Log level\"\n"
                  "help = \"Select quiet, normal, or verbose logging.\"\n"
                  "values = [\"quiet\", \"normal\", \"verbose\"]\n"
                  "default = \"normal\"\n"
                  "depends_on = \"ENABLE_LOGGING\"\n\n"
                  "[[config]]\n"
                  "symbol = \"BOARD_A\"\n"
                  "type = \"bool\"\n"
                  "prompt = \"Board A\"\n"
                  "help = \"Select the first terminal test board.\"\n"
                  "default = true\n"
                  "choice = \"board\"\n\n"
                  "[[config]]\n"
                  "symbol = \"BOARD_B\"\n"
                  "type = \"bool\"\n"
                  "prompt = \"Board B\"\n"
                  "help = \"Select the second terminal test board.\"\n"
                  "default = false\n"
                  "choice = \"board\"\n") ||
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

static int test_terminal_script(const char *binary, const char *root,
                                const char *output, unsigned short columns,
                                unsigned short rows,
                                const TestTerminalStep *steps,
                                size_t step_count, TestTerminalResult *result) {
  struct winsize window;
  int master = -1;
  pid_t child;
  int child_status = 0;
  int exited = 0;
  size_t used = 0U;
  size_t step_index = 0U;
  size_t search_offset = 0U;
  unsigned attempts;
  char *transcript;
  memset(result, 0, sizeof(*result));
  result->exit_code = -1;
  transcript = (char *)malloc(TEST_TRANSCRIPT_BYTES);
  if (transcript == 0)
    return 0;
  transcript[0] = '\0';
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
  for (attempts = 0U; attempts < 800U && !exited; ++attempts) {
    struct pollfd poll_fd;
    char bytes[4096];
    pid_t waited;
    poll_fd.fd = master;
    poll_fd.events = POLLIN;
    poll_fd.revents = 0;
    (void)poll(&poll_fd, 1U, 25);
    if ((poll_fd.revents & POLLIN) != 0) {
      const ssize_t count = read(master, bytes, sizeof(bytes));
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
    if (step_index < step_count) {
      const TestTerminalStep *step = &steps[step_index];
      const char *visible = transcript + search_offset;
      if (step->wait_for == 0 || strstr(visible, step->wait_for) != 0) {
        if (step->resize_columns != 0U && step->resize_rows != 0U) {
          window.ws_col = step->resize_columns;
          window.ws_row = step->resize_rows;
          if (ioctl(master, TIOCSWINSZ, &window) != 0 ||
              kill(child, SIGWINCH) != 0)
            break;
        }
        if (step->input != 0) {
          const size_t input_size = strlen(step->input);
          if (write(master, step->input, input_size) != (ssize_t)input_size)
            break;
        }
        ++step_index;
        search_offset = used;
      }
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
  return exited && step_index == step_count;
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
  if (bytes != 0)
    CONFIT_TEST_ASSERT(strcmp(bytes, "schema_version = 6\n\n[values]\n") == 0);
  confit_test_fs_free(bytes);
  confit_test_fs_free(digest);
  test_result_destroy(&result);
}

static void test_vim_style_exit_paths(const char *binary, const char *root,
                                      const char *output) {
  TestTerminalResult result;
  const TestTerminalStep hint_steps[] = {
      {"Confit menuconfig", "\033", 0U, 0U},
      {"[NORMAL]", "\033", 0U, 0U},
      {"[NORMAL]", "q", 0U, 0U},
      {"use :q", ":q\r", 0U, 0U},
  };
  const TestTerminalStep dirty_quit_steps[] = {
      {"Confit menuconfig", "\r ", 0U, 0U},
      {"[modified]", ":q\r", 0U, 0U},
      {"unsaved changes", ":q!\r", 0U, 0U},
  };
  const TestTerminalStep write_steps[] = {
      {"Confit menuconfig", "\r ", 0U, 0U},
      {"[modified]", ":w\r", 0U, 0U},
      {"configuration saved", ":q\r", 0U, 0U},
  };
  const TestTerminalStep dirty_x_steps[] = {
      {"Confit menuconfig", "\r ", 0U, 0U},
      {"[modified]", ":x\r", 0U, 0U},
  };
  const TestTerminalStep clean_x_steps[] = {
      {"Confit menuconfig", ":x\r", 0U, 0U},
  };
  CONFIT_TEST_ASSERT(test_terminal_script(
      binary, root, output, 100U, 20U, hint_steps,
      sizeof(hint_steps) / sizeof(hint_steps[0]), &result));
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "use :q, :wq, or :q!");
  test_result_destroy(&result);

  CONFIT_TEST_ASSERT(test_terminal_script(
      binary, root, output, 100U, 20U, dirty_quit_steps,
      sizeof(dirty_quit_steps) / sizeof(dirty_quit_steps[0]), &result));
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "unsaved changes");
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "[modified]");
  test_result_destroy(&result);

  CONFIT_TEST_ASSERT(test_terminal_script(
      binary, root, output, 100U, 20U, write_steps,
      sizeof(write_steps) / sizeof(write_steps[0]), &result));
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "configuration saved");
  test_result_destroy(&result);

  CONFIT_TEST_ASSERT(test_terminal_script(
      binary, root, output, 100U, 20U, dirty_x_steps,
      sizeof(dirty_x_steps) / sizeof(dirty_x_steps[0]), &result));
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  test_result_destroy(&result);

  CONFIT_TEST_ASSERT(test_terminal_script(
      binary, root, output, 100U, 20U, clean_x_steps,
      sizeof(clean_x_steps) / sizeof(clean_x_steps[0]), &result));
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  test_result_destroy(&result);
}

static void test_edit_search_enum_and_command_modes(const char *binary,
                                                    const char *root,
                                                    const char *output) {
  TestTerminalResult result;
  const TestTerminalStep int_steps[] = {
      {"Confit menuconfig", "\rjeabc\r", 0U, 0U},
      {"canonical decimal integer", "\033", 0U, 0U},
      {"[NORMAL]", ":q\r", 0U, 0U},
  };
  const TestTerminalStep hex_steps[] = {
      {"Confit menuconfig", "\rjjezzz\r", 0U, 0U},
      {"0x-prefixed integer", "\033", 0U, 0U},
      {"[NORMAL]", ":q\r", 0U, 0U},
  };
  const TestTerminalStep string_steps[] = {
      {"Confit menuconfig", "\rjjjechanged", 0U, 0U},
      {"[EDIT]", "\033", 0U, 0U},
      {"[NORMAL]", ":q\r", 0U, 0U},
  };
  const TestTerminalStep enum_steps[] = {
      {"Confit menuconfig", "\rjjjje", 0U, 0U},
      {"Choose a value", "j", 0U, 0U},
      {"> verbose", "\033", 0U, 0U},
      {"[NORMAL]", ":q\r", 0U, 0U},
  };
  const TestTerminalStep search_steps[] = {
      {"Confit menuconfig", "/Worker", 0U, 0U},
      {"[SEARCH]", "\033", 0U, 0U},
      {"[NORMAL]", ":q\r", 0U, 0U},
  };
  const TestTerminalStep command_steps[] = {
      {"Confit menuconfig", ":he\t\r", 0U, 0U},
      {"Menuconfig help", "\033", 0U, 0U},
      {"[NORMAL]", ":!\r", 0U, 0U},
      {"unknown menuconfig command", "\033", 0U, 0U},
      {"[NORMAL]", ":q\r", 0U, 0U},
  };
#define RUN_MODE_STEPS(STEPS)                                                  \
  do {                                                                         \
    CONFIT_TEST_ASSERT(                                                        \
        test_terminal_script(binary, root, output, 100U, 20U, (STEPS),         \
                             sizeof(STEPS) / sizeof((STEPS)[0]), &result));    \
    CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);                            \
    test_result_destroy(&result);                                              \
  } while (0)
  RUN_MODE_STEPS(int_steps);
  RUN_MODE_STEPS(hex_steps);
  RUN_MODE_STEPS(string_steps);
  RUN_MODE_STEPS(enum_steps);
  RUN_MODE_STEPS(search_steps);
  RUN_MODE_STEPS(command_steps);
#undef RUN_MODE_STEPS
}

static void test_narrow_detail_and_resize(const char *binary, const char *root,
                                          const char *output) {
  TestTerminalResult result;
  const TestTerminalStep steps[] = {
      {"[tabbed] [list]", "\t", 0U, 0U},
      {"[tabbed] [detail]", "\r", 0U, 0U},
      {"ENABLE_LOGGING", 0, 100U, 20U},
      {"[split]", 0, 60U, 15U},
      {"[tabbed] [detail]", ":q\r", 0U, 0U},
  };
  CONFIT_TEST_ASSERT(test_terminal_script(binary, root, output, 60U, 15U, steps,
                                          sizeof(steps) / sizeof(steps[0]),
                                          &result));
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript,
                              "Terminal-safe runtime options");
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "[split]");
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "[tabbed] [detail]");
  test_result_destroy(&result);
}

static void test_choice_radio_render_and_toggle(const char *binary,
                                                const char *root,
                                                const char *output) {
  TestTerminalResult result;
  const TestTerminalStep steps[] = {
      {"Confit menuconfig", "\r", 0U, 0U},
      {"(*) Board A", "jjjjjj ", 0U, 0U},
      {"(*) Board B", ":q!\r", 0U, 0U},
  };
  CONFIT_TEST_ASSERT(test_terminal_script(binary, root, output, 100U, 20U,
                                          steps,
                                          sizeof(steps) / sizeof(steps[0]),
                                          &result));
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "(*) Board A");
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "( ) Board B");
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "(*) Board B");
  test_result_destroy(&result);
}

static void test_failed_save_stays_interactive(const char *binary) {
  char root[TEST_PATH_BYTES];
  char output[TEST_PATH_BYTES];
  TestTerminalResult result;
  const TestTerminalStep steps[] = {
      {"Confit menuconfig", "\r :wq\r", 0U, 0U},
      {"error:", ":q!\r", 0U, 0U},
  };
  CONFIT_TEST_ASSERT(test_project(root, sizeof(root), output, sizeof(output)));
  CONFIT_TEST_ASSERT(test_make_dir(root, "output/selected"));
  CONFIT_TEST_ASSERT(
      test_terminal_script(binary, root, output, 100U, 20U, steps,
                           sizeof(steps) / sizeof(steps[0]), &result));
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "[modified]");
  CONFIT_TEST_ASSERT_CONTAINS(result.transcript, "error:");
  test_result_destroy(&result);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
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
  test_vim_style_exit_paths(argv[1], root, output);
  test_edit_search_enum_and_command_modes(argv[1], root, output);
  test_narrow_detail_and_resize(argv[1], root, output);
  test_choice_radio_render_and_toggle(argv[1], root, output);
  test_failed_save_stays_interactive(argv[1]);
  test_signal_restore(argv[1], root, output, SIGINT);
  test_signal_restore(argv[1], root, output, SIGTERM);
  test_signal_restore(argv[1], root, output, SIGHUP);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
  return 0;
}
