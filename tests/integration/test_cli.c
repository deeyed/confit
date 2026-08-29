#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/limits.h"
#include "test_assert.h"
#include "test_fs.h"
#include "test_process.h"

#define TEST_PATH_BYTES 2048U

typedef struct TestCommandExpectation {
  int exit_code;
  const char *stdout_exact;
  const char *stdout_contains;
  const char *stderr_exact;
  const char *stderr_contains;
} TestCommandExpectation;

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

static int test_run(const char *const *arguments, const char *directory,
                    const TestCommandExpectation *expected) {
  static unsigned sequence;
  ConfitTestProcessResult result = {-1, 0, 0};
  char stdout_name[64];
  char stderr_name[64];
  char stdout_path[TEST_PATH_BYTES];
  char stderr_path[TEST_PATH_BYTES];
  int ok;
  const int stdout_written =
      snprintf(stdout_name, sizeof(stdout_name), "stdout-%u.txt", sequence);
  const int stderr_written =
      snprintf(stderr_name, sizeof(stderr_name), "stderr-%u.txt", sequence);
  sequence += 1U;
  if (stdout_written < 0 || (size_t)stdout_written >= sizeof(stdout_name) ||
      stderr_written < 0 || (size_t)stderr_written >= sizeof(stderr_name) ||
      !confit_test_fs_path_join(stdout_path, sizeof(stdout_path), directory,
                                stdout_name) ||
      !confit_test_fs_path_join(stderr_path, sizeof(stderr_path), directory,
                                stderr_name))
    return 0;
  confit_test_process_result_clear(&result);
  ok = confit_test_process_run(arguments, 0, stdout_path, stderr_path, &result) &&
       result.exit_code == expected->exit_code && result.stdout_text != 0 &&
       result.stderr_text != 0 &&
       (expected->stdout_exact == 0 ||
        strcmp(result.stdout_text, expected->stdout_exact) == 0) &&
       (expected->stdout_contains == 0 ||
        strstr(result.stdout_text, expected->stdout_contains) != 0) &&
       (expected->stderr_exact == 0 ||
        strcmp(result.stderr_text, expected->stderr_exact) == 0) &&
       (expected->stderr_contains == 0 ||
        strstr(result.stderr_text, expected->stderr_contains) != 0);
  if (!ok) {
    (void)fprintf(stderr,
                  "command failed: exit=%d stdout=[%s] stderr=[%s]\n",
                  result.exit_code,
                  result.stdout_text != 0 ? result.stdout_text : "(null)",
                  result.stderr_text != 0 ? result.stderr_text : "(null)");
  }
  confit_test_process_result_clear(&result);
  return ok;
}

static int test_run_input(const char *const *arguments, const char *directory,
                          const char *stdin_path,
                          const TestCommandExpectation *expected) {
  static unsigned sequence;
  ConfitTestProcessResult result = {-1, 0, 0};
  char stdout_name[64];
  char stderr_name[64];
  char stdout_path[TEST_PATH_BYTES];
  char stderr_path[TEST_PATH_BYTES];
  int ok;
  if (snprintf(stdout_name, sizeof(stdout_name), "input-stdout-%u.txt",
               sequence) < 0 ||
      snprintf(stderr_name, sizeof(stderr_name), "input-stderr-%u.txt",
               sequence) < 0 ||
      !confit_test_fs_path_join(stdout_path, sizeof(stdout_path), directory,
                                stdout_name) ||
      !confit_test_fs_path_join(stderr_path, sizeof(stderr_path), directory,
                                stderr_name))
    return 0;
  sequence += 1U;
  ok = confit_test_process_run_with_input(
           arguments, 0, stdin_path, stdout_path, stderr_path, &result) &&
       result.exit_code == expected->exit_code && result.stdout_text != 0 &&
       result.stderr_text != 0 &&
       (expected->stdout_exact == 0 ||
        strcmp(result.stdout_text, expected->stdout_exact) == 0) &&
       (expected->stdout_contains == 0 ||
        strstr(result.stdout_text, expected->stdout_contains) != 0) &&
       (expected->stderr_exact == 0 ||
        strcmp(result.stderr_text, expected->stderr_exact) == 0) &&
       (expected->stderr_contains == 0 ||
        strstr(result.stderr_text, expected->stderr_contains) != 0);
  if (!ok)
    (void)fprintf(stderr,
                  "input command failed: exit=%d stdout=[%s] stderr=[%s]\n",
                  result.exit_code,
                  result.stdout_text != 0 ? result.stdout_text : "(null)",
                  result.stderr_text != 0 ? result.stderr_text : "(null)");
  confit_test_process_result_clear(&result);
  return ok;
}

static int test_make_project(const char *root) {
  return test_make_dir(root, "config") && test_make_dir(root, "configs") &&
         test_make_dir(root, "output") &&
         test_make_dir(root, "output-make") &&
         test_make_dir(root, "output-absolute") &&
         test_make_dir(root, "output-oldconfig") &&
         test_make_dir(root, "output-olddef") &&
         test_make_dir(root, "output-eof") &&
         test_make_dir(root, "output-semantic") &&
         test_write(root, "Confit.toml",
                    "schema_version = 6\n"
                    "mainmenu = \"CLI integration\"\n"
                    "source = [\"config/options.toml\"]\n") &&
         test_write(root, "config/options.toml",
                    "[menu]\n"
                    "prompt = \"Runtime\"\n"
                    "help = \"Configure runtime logging and workers.\"\n\n"
                    "[[config]]\n"
                    "symbol = \"ENABLE_FEATURE\"\n"
                    "type = \"bool\"\n"
                    "prompt = \"Enable feature\"\n"
                    "help = \"Enable the optional generic feature.\"\n"
                    "default = false\n\n"
                    "[[config]]\n"
                    "symbol = \"WORKER_COUNT\"\n"
                    "type = \"int\"\n"
                    "prompt = \"Worker count\"\n"
                    "help = \"Set the number of generic workers.\"\n"
                    "default = 4\n"
                    "range = { min = 1, max = 64 }\n\n"
                    "[[config]]\n"
                    "symbol = \"DEVICE_ID\"\n"
                    "type = \"hex\"\n"
                    "prompt = \"Device identifier\"\n"
                    "help = \"Set an inert hexadecimal identifier.\"\n"
                    "default = 0x10e8\n\n"
                    "[[config]]\n"
                    "symbol = \"LOG_LEVEL\"\n"
                    "type = \"enum\"\n"
                    "prompt = \"Log level\"\n"
                    "help = \"Select the amount of logging output.\"\n"
                    "values = [\"quiet\", \"normal\", \"verbose\"]\n"
                    "default = \"normal\"\n"
                    "depends_on = \"ENABLE_FEATURE\"\n") &&
         test_write(root, "configs/development.toml",
                    "schema_version = 6\n\n"
                    "[values]\n"
                    "ENABLE_FEATURE = true\n"
                    "WORKER_COUNT = 8\n"
                    "LOG_LEVEL = \"verbose\"\n") &&
         test_write(root, "configs/other.toml",
                    "schema_version = 6\n\n"
                    "[values]\n"
                    "ENABLE_FEATURE = true\n"
                    "WORKER_COUNT = 12\n"
                    "LOG_LEVEL = \"quiet\"\n");
}

static int test_write_evolved_options(const char *root,
                                      int changed_new_default) {
  const char *new_bool_default = changed_new_default ? "true" : "false";
  char text[16384];
  const int written = snprintf(
      text, sizeof(text),
      "[menu]\n"
      "prompt = \"Runtime\"\n"
      "help = \"Configure runtime logging and workers.\"\n\n"
      "[[config]]\n"
      "symbol = \"ENABLE_FEATURE\"\n"
      "type = \"bool\"\n"
      "prompt = \"Enable feature\"\n"
      "help = \"Enable the optional generic feature.\"\n"
      "default = false\n\n"
      "[[config]]\n"
      "symbol = \"WORKER_COUNT\"\n"
      "type = \"int\"\n"
      "prompt = \"Worker count\"\n"
      "help = \"Set the number of generic workers.\"\n"
      "default = 4\n"
      "range = { min = 1, max = 64 }\n\n"
      "[[config]]\n"
      "symbol = \"DEVICE_ID\"\n"
      "type = \"hex\"\n"
      "prompt = \"Device identifier\"\n"
      "help = \"Set an inert hexadecimal identifier.\"\n"
      "default = 0x10e8\n\n"
      "[[config]]\n"
      "symbol = \"LOG_LEVEL\"\n"
      "type = \"enum\"\n"
      "prompt = \"Log level\"\n"
      "help = \"Select the amount of logging output.\"\n"
      "values = [\"quiet\", \"normal\", \"verbose\"]\n"
      "default = \"normal\"\n"
      "depends_on = \"ENABLE_FEATURE\"\n\n"
      "[[config]]\n"
      "symbol = \"NEW_BOOL\"\n"
      "type = \"bool\"\n"
      "prompt = \"New bool\"\n"
      "help = \"Review a newly added boolean.\"\n"
      "default = %s\n\n"
      "[[config]]\n"
      "symbol = \"NEW_ENUM\"\n"
      "type = \"enum\"\n"
      "prompt = \"New enum\"\n"
      "help = \"Review a newly added enum.\"\n"
      "values = [\"slow\", \"fast\"]\n"
      "default = \"slow\"\n\n"
      "[[config]]\n"
      "symbol = \"NEW_HEX\"\n"
      "type = \"hex\"\n"
      "prompt = \"New hex\"\n"
      "help = \"Review a newly added hexadecimal value.\"\n"
      "default = 0x10\n"
      "range = { min = 0x0, max = 0xff }\n\n"
      "[[config]]\n"
      "symbol = \"NEW_INT\"\n"
      "type = \"int\"\n"
      "prompt = \"New int\"\n"
      "help = \"Review a newly added integer.\"\n"
      "default = 3\n"
      "range = { min = 1, max = 16 }\n\n"
      "[[config]]\n"
      "symbol = \"NEW_STRING\"\n"
      "type = \"string\"\n"
      "prompt = \"New string\"\n"
      "help = \"Review a newly added string.\"\n"
      "default = \"default-label\"\n",
      new_bool_default);
  return written > 0 && (size_t)written < sizeof(text) &&
         test_write(root, "config/options.toml", text);
}

static void test_help_usage_and_environment(const char *binary,
                                            const char *root) {
  const TestCommandExpectation help = {0, 0, "savedefconfig", "", 0};
  const TestCommandExpectation version = {
      0, 0, "schema_implementation=configuration-cli", "", 0};
  const TestCommandExpectation usage = {2, "", 0, 0, "usage error"};
  const TestCommandExpectation valid = {
      0, "configuration is valid\n", 0, "", 0};
  const char *help_args[] = {binary, "help", 0};
  const char *version_args[] = {binary, "--version", 0};
  const char *unknown[] = {binary, "build-enter", 0};
  const char *missing[] = {binary, "check", "--root", root, 0};
  const char *duplicate[] = {binary, "check", "--root", root, "--root",
                             root, "--project", "Confit.toml", 0};
  const char *wrong_option[] = {binary, "check", "--root", root,
                                "--project", "Confit.toml", "--output", root,
                                0};
  const char *relative_root[] = {binary, "check", "--root", "relative",
                                 "--project", "Confit.toml", 0};
  const char *absolute_project[] = {binary, "check", "--root", root,
                                    "--project", "/Confit.toml", 0};
  const char *empty_query[] = {binary, "search", "--root", root,
                               "--project", "Confit.toml", "--query", "", 0};
  const char *check[] = {binary, "check", "--root", root, "--project",
                         "Confit.toml", 0};
  char oversized_query[CONFIT_LIMIT_STRING_BYTES + 2U];
  const char *oversized_query_args[] = {
      binary, "search", "--root", root, "--project", "Confit.toml",
      "--query", oversized_query, 0};
  char selected_path[TEST_PATH_BYTES];
  char *unexpected_selected;
  CONFIT_TEST_ASSERT(test_run(help_args, root, &help));
  CONFIT_TEST_ASSERT(test_run(version_args, root, &version));
  CONFIT_TEST_ASSERT(test_run(unknown, root, &usage));
  CONFIT_TEST_ASSERT(test_run(missing, root, &usage));
  CONFIT_TEST_ASSERT(test_run(duplicate, root, &usage));
  CONFIT_TEST_ASSERT(test_run(wrong_option, root, &usage));
  CONFIT_TEST_ASSERT(test_run(relative_root, root, &usage));
  CONFIT_TEST_ASSERT(test_run(absolute_project, root, &usage));
  CONFIT_TEST_ASSERT(test_run(empty_query, root, &usage));
  memset(oversized_query, 'a', sizeof(oversized_query) - 1U);
  oversized_query[sizeof(oversized_query) - 1U] = '\0';
  CONFIT_TEST_ASSERT(test_run(oversized_query_args, root, &usage));
#if defined(_WIN32)
  CONFIT_TEST_ASSERT(_putenv_s("ARCH", "poison") == 0);
  CONFIT_TEST_ASSERT(_putenv_s("KERNCONF", "poison") == 0);
#else
  CONFIT_TEST_ASSERT(setenv("ARCH", "poison", 1) == 0);
  CONFIT_TEST_ASSERT(setenv("KERNCONF", "poison", 1) == 0);
#endif
  CONFIT_TEST_ASSERT(test_run(check, root, &valid));
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      selected_path, sizeof(selected_path), root, "output/selected"));
  unexpected_selected = confit_test_fs_read_file(selected_path);
  CONFIT_TEST_ASSERT(unexpected_selected == 0);
}

static void test_check_search_explain_diff(const char *binary,
                                           const char *root,
                                           const char *absolute_config) {
  const TestCommandExpectation valid = {
      0, "configuration is valid\n", 0, "", 0};
  const TestCommandExpectation search = {
      0, 0, "LOG_LEVEL\tenum\tLog level", "", 0};
  const TestCommandExpectation explain = {
      0, 0, "value: \"verbose\"", "", 0};
  const TestCommandExpectation unavailable = {
      0, 0, "available: false", "", 0};
  const TestCommandExpectation unknown = {
      3, "", 0, 0, "configuration symbol was not found"};
  const TestCommandExpectation diff = {
      0,
      "ENABLE_FEATURE: false [available] -> true [available]\n"
      "LOG_LEVEL: \"normal\" [unavailable] -> \"quiet\" [available]\n"
      "WORKER_COUNT: 4 [available] -> 12 [available]\n",
      0, "", 0};
  const char *check_relative[] = {
      binary,          "check", "--root",   root,       "--project",
      "Confit.toml", "--config", "configs/development.toml", 0};
  const char *check_absolute[] = {binary, "check", "--root", root,
                                  "--project", "Confit.toml", "--config",
                                  absolute_config, 0};
  const char *search_args[] = {binary, "search", "--root", root,
                               "--project", "Confit.toml", "--query",
                               "LoGgInG", 0};
  const char *explain_args[] = {
      binary,          "explain", "--root",   root,       "--project",
      "Confit.toml", "--config", "configs/development.toml",
      "--symbol",     "LOG_LEVEL", 0};
  const char *unavailable_args[] = {binary, "explain", "--root", root,
                                    "--project", "Confit.toml", "--symbol",
                                    "LOG_LEVEL", 0};
  const char *unknown_args[] = {binary, "explain", "--root", root,
                                "--project", "Confit.toml", "--symbol",
                                "MISSING", 0};
  const char *diff_args[] = {binary, "diff", "--root", root, "--project",
                             "Confit.toml", "--other-config",
                             "configs/other.toml", 0};
  CONFIT_TEST_ASSERT(test_run(check_relative, root, &valid));
  CONFIT_TEST_ASSERT(test_run(check_absolute, root, &valid));
  CONFIT_TEST_ASSERT(test_run(search_args, root, &search));
  CONFIT_TEST_ASSERT(test_run(explain_args, root, &explain));
  CONFIT_TEST_ASSERT(test_run(unavailable_args, root, &unavailable));
  CONFIT_TEST_ASSERT(test_run(unknown_args, root, &unknown));
  CONFIT_TEST_ASSERT(test_run(diff_args, root, &diff));
}

static void test_configure_verify(const char *binary, const char *root,
                                  const char *output) {
  const TestCommandExpectation configured = {
      0, 0, "configured snapshot ", "", 0};
  const TestCommandExpectation current = {
      0, "configuration is current\n", 0, "", 0};
  const TestCommandExpectation stale = {4, "", 0, 0, "error:"};
  const TestCommandExpectation duplicate_emit = {
      2, "", 0, 0, "usage error"};
  const char *configure[] = {
      binary,          "configure", "--root", root,
      "--project",    "Confit.toml", "--config",
      "configs/development.toml", "--output", output,
      "--emit",       "make", "--emit", "c-header", "--emit", "json", 0};
  const char *verify[] = {binary, "verify", "--root", root, "--project",
                          "Confit.toml", "--output", output, 0};
  const char *duplicate[] = {
      binary, "configure", "--root", root, "--project", "Confit.toml",
      "--output", output, "--emit", "make", "--emit", "make", 0};
  char selected_path[TEST_PATH_BYTES];
  char *selected;
  char expected[TEST_PATH_BYTES * 2U];
  char expected_no_newline[TEST_PATH_BYTES * 2U];
  char *newline;
  const char *print_make[] = {binary, "verify", "--root", root, "--project",
                              "Confit.toml", "--output", output,
                              "--print-artifact", "values.mk", 0};
  TestCommandExpectation printed = {0, 0, 0, "", 0};
  CONFIT_TEST_ASSERT(test_run(configure, root, &configured));
  CONFIT_TEST_ASSERT(test_run(verify, root, &current));
  CONFIT_TEST_ASSERT(test_run(duplicate, root, &duplicate_emit));
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(selected_path,
                                              sizeof(selected_path), output,
                                              "selected"));
  selected = confit_test_fs_read_file(selected_path);
  CONFIT_TEST_ASSERT(selected != 0);
  newline = strchr(selected, '\n');
  CONFIT_TEST_ASSERT(newline != 0 && newline[1] == '\0');
  *newline = '\0';
  CONFIT_TEST_ASSERT(snprintf(expected_no_newline, sizeof(expected_no_newline),
                              "%s/snapshots/%s/values.mk", output, selected) >
                     0);
  CONFIT_TEST_ASSERT(snprintf(expected, sizeof(expected), "%s\n",
                              expected_no_newline) > 0);
  printed.stdout_exact = expected;
  CONFIT_TEST_ASSERT(test_run(print_make, root, &printed));
  confit_test_fs_free(selected);

  {
    char make_output[TEST_PATH_BYTES];
    const char *configure_make[] = {
        binary, "configure", "--root", root, "--project", "Confit.toml",
        "--output", make_output, "--emit", "make", 0};
    const char *missing_header[] = {
        binary, "verify", "--root", root, "--project", "Confit.toml",
        "--output", make_output, "--print-artifact", "values.h", 0};
    CONFIT_TEST_ASSERT(confit_test_fs_path_join(
        make_output, sizeof(make_output), root, "output-make"));
    CONFIT_TEST_ASSERT(test_run(configure_make, root, &configured));
    CONFIT_TEST_ASSERT(test_run(missing_header, root, &stale));
  }
}

static void test_absolute_snapshot_and_stale(const char *binary,
                                             const char *root,
                                             const char *absolute_config,
                                             const char *output) {
  const TestCommandExpectation configured = {
      0, 0, "configured snapshot ", "", 0};
  const TestCommandExpectation current = {
      0, "configuration is current\n", 0, "", 0};
  const TestCommandExpectation stale = {4, "", 0, 0, "error:"};
  const char *configure[] = {binary, "configure", "--root", root,
                             "--project", "Confit.toml", "--config",
                             absolute_config, "--output", output, 0};
  const char *verify[] = {binary, "verify", "--root", root, "--project",
                          "Confit.toml", "--output", output, 0};
  CONFIT_TEST_ASSERT(test_run(configure, root, &configured));
  CONFIT_TEST_ASSERT(test_run(verify, root, &current));
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      absolute_config,
      "schema_version = 6\n[values]\nWORKER_COUNT = 9\n"));
  CONFIT_TEST_ASSERT(test_run(verify, root, &stale));
}

static void test_terminal_and_current_migration(const char *binary,
                                                const char *root,
                                                const char *output) {
  const TestCommandExpectation terminal = {
      6, "", 0, 0, "menuconfig requires POSIX TTY input and output"};
  const TestCommandExpectation no_new = {0, "", 0, "", 0};
  const char *menuconfig[] = {binary, "menuconfig", "--root", root,
                              "--project", "Confit.toml", "--output", output,
                              0};
  const char *listnew[] = {binary, "listnewconfig", "--root", root,
                           "--project", "Confit.toml", "--output", output, 0};
  CONFIT_TEST_ASSERT(test_run(menuconfig, root, &terminal));
  CONFIT_TEST_ASSERT(test_run(listnew, root, &no_new));
}

static void test_migration_commands(const char *binary, const char *root) {
  const TestCommandExpectation configured = {
      0, 0, "configured snapshot ", "", 0};
  const TestCommandExpectation new_symbols = {
      0,
      "NEW_BOOL\tbool\tNew bool\n"
      "NEW_ENUM\tenum\tNew enum\n"
      "NEW_HEX\thex\tNew hex\n"
      "NEW_INT\tint\tNew int\n"
      "NEW_STRING\tstring\tNew string\n",
      0, "", 0};
  const TestCommandExpectation prompted = {
      0, 0, "NEW_BOOL (New bool) [y/N]: ", "", 0};
  const TestCommandExpectation cancelled = {
      6, 0, "NEW_ENUM (New enum)", 0,
      "oldconfig input ended before every new symbol was reviewed"};
  const TestCommandExpectation semantic = {
      4, "", 0, 0, "selected catalog has an incompatible semantic change"};
  const TestCommandExpectation saved = {
      0, "saved defconfig configs/saved.toml\n", 0, "", 0};
  char output[TEST_PATH_BYTES];
  char output_oldconfig[TEST_PATH_BYTES];
  char output_olddef[TEST_PATH_BYTES];
  char output_eof[TEST_PATH_BYTES];
  char output_semantic[TEST_PATH_BYTES];
  char prompt_input[TEST_PATH_BYTES];
  char eof_input[TEST_PATH_BYTES];
  char selected_path[TEST_PATH_BYTES];
  char saved_path[TEST_PATH_BYTES];
  char *selected_before;
  char *selected_after;
  char *saved_text;
  const char *baseline_output;
  const char *baseline[] = {binary, "configure", "--root", root,
                            "--project", "Confit.toml", "--config",
                            "configs/development.toml", "--output", 0, 0};
  const char *listnew[] = {binary, "listnewconfig", "--root", root,
                           "--project", "Confit.toml", "--config",
                           "configs/development.toml", "--output", output,
                           0};
  const char *olddef[] = {binary, "olddefconfig", "--root", root,
                          "--project", "Confit.toml", "--config",
                          "configs/development.toml", "--output",
                          output_olddef, 0};
  const char *oldconfig[] = {binary, "oldconfig", "--root", root,
                             "--project", "Confit.toml", "--config",
                             "configs/development.toml", "--output",
                             output_oldconfig, 0};
  const char *oldconfig_eof[] = {binary, "oldconfig", "--root", root,
                                 "--project", "Confit.toml", "--config",
                                 "configs/development.toml", "--output",
                                 output_eof, 0};
  const char *save[] = {binary, "savedefconfig", "--root", root,
                        "--project", "Confit.toml", "--output",
                        output_oldconfig, "--destination",
                        "configs/saved.toml", 0};
  const char *semantic_olddef[] = {
      binary, "olddefconfig", "--root", root, "--project", "Confit.toml",
      "--config", "configs/development.toml", "--output", output_semantic,
      0};
  CONFIT_TEST_ASSERT(
      confit_test_fs_path_join(output, sizeof(output), root, "output"));
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      output_oldconfig, sizeof(output_oldconfig), root, "output-oldconfig"));
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      output_olddef, sizeof(output_olddef), root, "output-olddef"));
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      output_eof, sizeof(output_eof), root, "output-eof"));
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      output_semantic, sizeof(output_semantic), root, "output-semantic"));
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      prompt_input, sizeof(prompt_input), root, "oldconfig-input.txt"));
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      eof_input, sizeof(eof_input), root, "oldconfig-eof.txt"));
  baseline_output = output_oldconfig;
  baseline[9] = baseline_output;
  CONFIT_TEST_ASSERT(test_run(baseline, root, &configured));
  baseline[9] = output_olddef;
  CONFIT_TEST_ASSERT(test_run(baseline, root, &configured));
  baseline[9] = output_eof;
  CONFIT_TEST_ASSERT(test_run(baseline, root, &configured));
  CONFIT_TEST_ASSERT(test_write_evolved_options(root, 0));
  CONFIT_TEST_ASSERT(test_run(listnew, root, &new_symbols));
  CONFIT_TEST_ASSERT(test_run(olddef, root, &configured));
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      prompt_input, "y\nfast\n0x2a\n9\nworker\n"));
  CONFIT_TEST_ASSERT(test_run_input(oldconfig, root, prompt_input, &prompted));
  CONFIT_TEST_ASSERT(test_run(save, root, &saved));
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      saved_path, sizeof(saved_path), root, "configs/saved.toml"));
  saved_text = confit_test_fs_read_file(saved_path);
  CONFIT_TEST_ASSERT(saved_text != 0);
  CONFIT_TEST_ASSERT(strcmp(
                         saved_text,
                         "schema_version = 6\n\n[values]\n"
                         "ENABLE_FEATURE = true\n"
                         "LOG_LEVEL = \"verbose\"\n"
                         "NEW_BOOL = true\n"
                         "NEW_ENUM = \"fast\"\n"
                         "NEW_HEX = 0x2a\n"
                         "NEW_INT = 9\n"
                         "NEW_STRING = \"worker\"\n"
                         "WORKER_COUNT = 8\n") == 0);
  confit_test_fs_free(saved_text);
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      selected_path, sizeof(selected_path), output_eof, "selected"));
  selected_before = confit_test_fs_read_file(selected_path);
  CONFIT_TEST_ASSERT(selected_before != 0);
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(eof_input, "y\n"));
  CONFIT_TEST_ASSERT(
      test_run_input(oldconfig_eof, root, eof_input, &cancelled));
  selected_after = confit_test_fs_read_file(selected_path);
  CONFIT_TEST_ASSERT(selected_after != 0 &&
                     strcmp(selected_before, selected_after) == 0);
  confit_test_fs_free(selected_after);
  confit_test_fs_free(selected_before);
  baseline[9] = output_semantic;
  CONFIT_TEST_ASSERT(test_run(baseline, root, &configured));
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      selected_path, sizeof(selected_path), output_semantic, "selected"));
  selected_before = confit_test_fs_read_file(selected_path);
  CONFIT_TEST_ASSERT(selected_before != 0);
  CONFIT_TEST_ASSERT(test_write_evolved_options(root, 1));
  CONFIT_TEST_ASSERT(test_run(semantic_olddef, root, &semantic));
  selected_after = confit_test_fs_read_file(selected_path);
  CONFIT_TEST_ASSERT(selected_after != 0 &&
                     strcmp(selected_before, selected_after) == 0);
  confit_test_fs_free(selected_after);
  confit_test_fs_free(selected_before);
}

int main(int argc, char **argv) {
  char raw_root[TEST_PATH_BYTES];
  char root[TEST_PATH_BYTES];
  char project[TEST_PATH_BYTES];
  char output[TEST_PATH_BYTES];
  char output_absolute[TEST_PATH_BYTES];
  char absolute_config[TEST_PATH_BYTES];
  int result = 0;
  if (argc != 2 || !confit_test_fs_make_temp_dir(raw_root, sizeof(raw_root),
                                                  "confit-cli"))
    return 1;
#if defined(_WIN32)
  if (strlen(raw_root) + 1U > sizeof(root)) return 2;
  memcpy(root, raw_root, strlen(raw_root) + 1U);
#else
  if (realpath(raw_root, root) == 0) return 2;
#endif
  if (!confit_test_fs_path_join(project, sizeof(project), root, "project") ||
      !confit_test_fs_make_dirs(project) || !test_make_project(project) ||
      !confit_test_fs_path_join(output, sizeof(output), project, "output") ||
      !confit_test_fs_path_join(output_absolute, sizeof(output_absolute),
                                project, "output-absolute") ||
      !confit_test_fs_path_join(absolute_config, sizeof(absolute_config), root,
                                "absolute.toml") ||
      !confit_test_fs_write_file(
          absolute_config,
          "schema_version = 6\n[values]\nENABLE_FEATURE = true\n"
          "WORKER_COUNT = 7\nLOG_LEVEL = \"verbose\"\n")) {
    (void)confit_test_fs_remove_tree(root);
    return 3;
  }
  test_help_usage_and_environment(argv[1], project);
  test_check_search_explain_diff(argv[1], project, absolute_config);
  test_configure_verify(argv[1], project, output);
  test_absolute_snapshot_and_stale(argv[1], project, absolute_config,
                                   output_absolute);
  test_terminal_and_current_migration(argv[1], project, output);
  test_migration_commands(argv[1], project);
  result = confit_test_fs_remove_tree(root) ? 0 : 4;
  return result;
}
