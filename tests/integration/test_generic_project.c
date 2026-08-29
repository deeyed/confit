#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "test_fs.h"
#include "test_process.h"

#ifndef CONFIT_TEST_SOURCE_DIR
#define CONFIT_TEST_SOURCE_DIR "."
#endif

#ifndef CONFIT_TEST_HOST_CC
#define CONFIT_TEST_HOST_CC "/usr/bin/clang"
#endif

#ifndef CONFIT_TEST_BMAKE_TOOL
#define CONFIT_TEST_BMAKE_TOOL "/usr/bin/bmake"
#endif

#define TEST_PATH_BYTES 4096U

static unsigned test_sequence;

static void join_path(char *out, const char *left, const char *right) {
  CONFIT_TEST_ASSERT(
      confit_test_fs_path_join(out, TEST_PATH_BYTES, left, right));
}

static void make_assignment(char *out, const char *name, const char *value) {
  const int written = snprintf(out, TEST_PATH_BYTES, "%s=%s", name, value);
  CONFIT_TEST_ASSERT(written > 0 && (size_t)written < TEST_PATH_BYTES);
}

static ConfitTestProcessResult run_command(const char *const *arguments,
                                           const char *working_directory,
                                           const char *capture_root) {
  ConfitTestProcessResult result = {-1, 0, 0};
  char stdout_name[64];
  char stderr_name[64];
  char stdout_path[TEST_PATH_BYTES];
  char stderr_path[TEST_PATH_BYTES];
  const int stdout_written = snprintf(stdout_name, sizeof(stdout_name),
                                      "stdout-%u.txt", test_sequence);
  const int stderr_written = snprintf(stderr_name, sizeof(stderr_name),
                                      "stderr-%u.txt", test_sequence);

  test_sequence += 1U;
  CONFIT_TEST_ASSERT(stdout_written > 0 &&
                     (size_t)stdout_written < sizeof(stdout_name));
  CONFIT_TEST_ASSERT(stderr_written > 0 &&
                     (size_t)stderr_written < sizeof(stderr_name));
  join_path(stdout_path, capture_root, stdout_name);
  join_path(stderr_path, capture_root, stderr_name);
  CONFIT_TEST_ASSERT(confit_test_process_run(
      arguments, working_directory, stdout_path, stderr_path, &result));
  CONFIT_TEST_ASSERT(result.stdout_text != 0 && result.stderr_text != 0);
  return result;
}

static void expect_exit(ConfitTestProcessResult *result, int expected) {
  if (result->exit_code != expected) {
    (void)fprintf(stderr, "exit=%d stdout=[%s] stderr=[%s]\n",
                  result->exit_code, result->stdout_text, result->stderr_text);
  }
  CONFIT_TEST_ASSERT_EQ_INT(expected, result->exit_code);
  confit_test_process_result_clear(result);
}

static void expect_build_rejected(ConfitTestProcessResult *result) {
  if (result->exit_code == 0 ||
      strstr(result->stderr_text, "configuration is missing or stale") == 0) {
    (void)fprintf(stderr,
                  "unexpected build result: exit=%d stdout=[%s] stderr=[%s]\n",
                  result->exit_code, result->stdout_text, result->stderr_text);
  }
  CONFIT_TEST_ASSERT(result->exit_code != 0);
  CONFIT_TEST_ASSERT_CONTAINS(result->stderr_text,
                              "configuration is missing or stale");
  confit_test_process_result_clear(result);
}

static void expect_example_output(const char *binary, const char *capture_root,
                                  const char *expected) {
  const char *arguments[] = {binary, 0};
  ConfitTestProcessResult result = run_command(arguments, 0, capture_root);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT(strcmp(result.stdout_text, expected) == 0);
  CONFIT_TEST_ASSERT(strcmp(result.stderr_text, "") == 0);
  confit_test_process_result_clear(&result);
}

static void test_direct_authoring(const char *confit_binary) {
  static const char first_config[] = "schema_version = 6\n"
                                     "\n"
                                     "[values]\n"
                                     "ENABLE_METRICS = true\n"
                                     "WORKER_COUNT = 8\n"
                                     "LOG_LEVEL = \"verbose\"\n";
  static const char second_config[] = "schema_version = 6\n"
                                      "\n"
                                      "[values]\n"
                                      "ENABLE_METRICS = false\n"
                                      "WORKER_COUNT = 9\n";
  char root[TEST_PATH_BYTES];
  char canonical_root[TEST_PATH_BYTES];
  char example_root[TEST_PATH_BYTES];
  char config_root[TEST_PATH_BYTES];
  char object_root[TEST_PATH_BYTES];
  char user_config[TEST_PATH_BYTES];
  char selected_path[TEST_PATH_BYTES];
  char example_binary[TEST_PATH_BYTES];
  char confit_assignment[TEST_PATH_BYTES];
  char compiler_assignment[TEST_PATH_BYTES];
  char config_assignment[TEST_PATH_BYTES];
  char config_root_assignment[TEST_PATH_BYTES];
  char object_root_assignment[TEST_PATH_BYTES];
  char *selected_before;
  char *selected_after;
  const char *all_arguments[8];
  const char *configure_arguments[8];
  ConfitTestProcessResult result;

  CONFIT_TEST_ASSERT(
      confit_test_fs_make_temp_dir(root, sizeof(root), "confit-generic"));
#if defined(_WIN32)
  CONFIT_TEST_ASSERT(strlen(root) + 1U <= sizeof(canonical_root));
  memcpy(canonical_root, root, strlen(root) + 1U);
#else
  CONFIT_TEST_ASSERT(realpath(root, canonical_root) != 0);
#endif
  join_path(example_root, CONFIT_TEST_SOURCE_DIR, "examples/generic");
  join_path(config_root, canonical_root, "config-output");
  join_path(object_root, canonical_root, "object-output");
  join_path(user_config, canonical_root, "development.toml");
  join_path(selected_path, config_root, "selected");
  join_path(example_binary, object_root, "generic-example");
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(config_root));
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(object_root));
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(user_config, first_config));

  make_assignment(confit_assignment, "CONFIT", confit_binary);
  make_assignment(compiler_assignment, "CONFIT_CC", CONFIT_TEST_HOST_CC);
  make_assignment(config_assignment, "CONFIG", user_config);
  make_assignment(config_root_assignment, "CONFIG_OUTPUT", config_root);
  make_assignment(object_root_assignment, "EXAMPLE_OBJROOT", object_root);

  all_arguments[0] = CONFIT_TEST_BMAKE_TOOL;
  all_arguments[1] = confit_assignment;
  all_arguments[2] = compiler_assignment;
  all_arguments[3] = config_assignment;
  all_arguments[4] = config_root_assignment;
  all_arguments[5] = object_root_assignment;
  all_arguments[6] = "all";
  all_arguments[7] = 0;
  configure_arguments[0] = CONFIT_TEST_BMAKE_TOOL;
  configure_arguments[1] = confit_assignment;
  configure_arguments[2] = compiler_assignment;
  configure_arguments[3] = config_assignment;
  configure_arguments[4] = config_root_assignment;
  configure_arguments[5] = object_root_assignment;
  configure_arguments[6] = "configure";
  configure_arguments[7] = 0;

  result = run_command(all_arguments, example_root, canonical_root);
  expect_build_rejected(&result);
  CONFIT_TEST_ASSERT(!confit_test_fs_file_exists(selected_path));

  result = run_command(configure_arguments, example_root, canonical_root);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "configured snapshot ");
  expect_exit(&result, 0);
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(selected_path));

  result = run_command(all_arguments, example_root, canonical_root);
  expect_exit(&result, 0);
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(example_binary));
  expect_example_output(example_binary, canonical_root,
                        "workers=8 device=0x10e8 level=verbose metrics=17\n");

  selected_before = confit_test_fs_read_file(selected_path);
  CONFIT_TEST_ASSERT(selected_before != 0);
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(user_config, second_config));
  result = run_command(all_arguments, example_root, canonical_root);
  expect_build_rejected(&result);
  selected_after = confit_test_fs_read_file(selected_path);
  CONFIT_TEST_ASSERT(selected_after != 0);
  if (selected_before != 0 && selected_after != 0)
    CONFIT_TEST_ASSERT(strcmp(selected_before, selected_after) == 0);
  confit_test_fs_free(selected_after);
  confit_test_fs_free(selected_before);

  result = run_command(configure_arguments, example_root, canonical_root);
  expect_exit(&result, 0);
  result = run_command(all_arguments, example_root, canonical_root);
  expect_exit(&result, 0);
  expect_example_output(
      example_binary, canonical_root,
      "workers=9 device=0x10e8 level=normal metrics=disabled\n");

  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

int main(int argc, char **argv) {
  CONFIT_TEST_ASSERT(argc == 2);
  test_direct_authoring(argv[1]);
  return 0;
}
