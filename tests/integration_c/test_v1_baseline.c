#include "test_assert.h"
#include "test_fs.h"
#include "test_process.h"

#include <stdio.h>
#include <string.h>

#ifndef CONFIT_TEST_SOURCE_DIR
#define CONFIT_TEST_SOURCE_DIR "."
#endif

typedef struct ConfitV1BaselineContext {
  const char *confit_bin;
  const char *source_dir;
  const char *work_dir;
  char project_dir[4096];
  char golden_dir[4096];
  char generated_a_dir[4096];
  char generated_b_dir[4096];
  unsigned int run_index;
} ConfitV1BaselineContext;

static void test_join(char *out, size_t out_size, const char *left,
                      const char *right) {
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(out, out_size, left, right));
}

static void test_join3(char *out, size_t out_size, const char *first,
                       const char *second, const char *third) {
  char scratch[4096];

  test_join(scratch, sizeof(scratch), first, second);
  test_join(out, out_size, scratch, third);
}

static void test_join4(char *out, size_t out_size, const char *first,
                       const char *second, const char *third,
                       const char *fourth) {
  char scratch[4096];

  test_join3(scratch, sizeof(scratch), first, second, third);
  test_join(out, out_size, scratch, fourth);
}

static void test_join5(char *out, size_t out_size, const char *first,
                       const char *second, const char *third,
                       const char *fourth, const char *fifth) {
  char scratch[4096];

  test_join4(scratch, sizeof(scratch), first, second, third, fourth);
  test_join(out, out_size, scratch, fifth);
}

static void test_make_sibling_path(char *out, size_t out_size,
                                   const char *path, const char *sibling) {
  char directory[4096];
  size_t index;
  size_t slash_index;
  size_t size;

  size = strlen(path);
  slash_index = size;
  for (index = 0U; index < size; ++index) {
    if (path[index] == '/' || path[index] == '\\') {
      slash_index = index;
    }
  }

  if (slash_index == size) {
    test_join(out, out_size, ".", sibling);
    return;
  }
  CONFIT_TEST_ASSERT(slash_index + 1U < sizeof(directory));
  memcpy(directory, path, slash_index);
  directory[slash_index] = '\0';
  test_join(out, out_size, directory, sibling);
}

static void test_context_init(ConfitV1BaselineContext *context, int argc,
                              char **argv, char *confit_bin_buffer,
                              size_t confit_bin_buffer_size,
                              char *work_dir_buffer,
                              size_t work_dir_buffer_size) {
  if (argc == 4) {
    context->confit_bin = argv[1];
    context->source_dir = argv[2];
    context->work_dir = argv[3];
  } else if (argc == 1) {
    test_make_sibling_path(confit_bin_buffer, confit_bin_buffer_size, argv[0],
                           "confit");
    test_make_sibling_path(work_dir_buffer, work_dir_buffer_size, argv[0],
                           "round1-v1-baseline-direct");
    context->confit_bin = confit_bin_buffer;
    context->source_dir = CONFIT_TEST_SOURCE_DIR;
    context->work_dir = work_dir_buffer;
  } else {
    CONFIT_TEST_FAIL(
        "usage: confit_test_v1_baseline <confit-bin> <source-dir> <work-dir>");
  }

  test_join5(context->project_dir, sizeof(context->project_dir),
             context->source_dir, "tests", "fixtures", "v1-baseline",
             "project");
  test_join4(context->golden_dir, sizeof(context->golden_dir),
             context->source_dir, "tests", "golden", "v1-baseline");
  test_join(context->generated_a_dir, sizeof(context->generated_a_dir),
            context->work_dir, "generated-a");
  test_join(context->generated_b_dir, sizeof(context->generated_b_dir),
            context->work_dir, "generated-b");
  context->run_index = 0U;
}

static void test_run(ConfitV1BaselineContext *context,
                     const char *const *argv,
                     ConfitTestProcessResult *result) {
  char stderr_name[64];
  char stderr_path[4096];
  char stdout_name[64];
  char stdout_path[4096];

  confit_test_process_result_clear(result);
  (void)snprintf(stdout_name, sizeof(stdout_name), "run-%03u.out",
                 context->run_index);
  (void)snprintf(stderr_name, sizeof(stderr_name), "run-%03u.err",
                 context->run_index);
  context->run_index += 1U;
  test_join(stdout_path, sizeof(stdout_path), context->work_dir, stdout_name);
  test_join(stderr_path, sizeof(stderr_path), context->work_dir, stderr_name);
  CONFIT_TEST_ASSERT(confit_test_process_run(argv, 0, stdout_path, stderr_path,
                                             result));
}

static void test_expect_text_equal(const char *actual, const char *expected,
                                   const char *label) {
  if (actual == 0 || expected == 0 || strcmp(actual, expected) != 0) {
    fprintf(stderr, "v1 baseline mismatch: %s\n", label);
    CONFIT_TEST_FAIL("text differs from frozen v1 baseline");
  }
}

static void test_expect_file_equal(const char *actual_path,
                                   const char *expected_path) {
  char *actual;
  char *expected;

  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(actual_path));
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(expected_path));
  actual = confit_test_fs_read_file(actual_path);
  expected = confit_test_fs_read_file(expected_path);
  test_expect_text_equal(actual, expected, expected_path);
  confit_test_fs_free(actual);
  confit_test_fs_free(expected);
}

static void test_expect_result_file(ConfitV1BaselineContext *context,
                                    const char *relative_path) {
  char actual_path[4096];
  char expected_path[4096];

  test_join(actual_path, sizeof(actual_path), context->generated_a_dir,
            relative_path);
  test_join(expected_path, sizeof(expected_path), context->golden_dir,
            relative_path);
  test_expect_file_equal(actual_path, expected_path);
}

static void test_expect_repeat_file(ConfitV1BaselineContext *context,
                                    const char *relative_path) {
  char first_path[4096];
  char second_path[4096];

  test_join(first_path, sizeof(first_path), context->generated_a_dir,
            relative_path);
  test_join(second_path, sizeof(second_path), context->generated_b_dir,
            relative_path);
  test_expect_file_equal(first_path, second_path);
}

static void test_version_and_doctor(ConfitV1BaselineContext *context) {
  ConfitTestProcessResult result = {-1, 0, 0};
  const char *doctor_argv[] = {0, "doctor", "--project", 0, 0};
  const char *version_argv[] = {0, "--version", "--verbose", 0};

  version_argv[0] = context->confit_bin;
  test_run(context, version_argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  test_expect_text_equal(result.stdout_text, "confit 0.1.0-rc1\n",
                         "--version");
  confit_test_process_result_clear(&result);

  doctor_argv[0] = context->confit_bin;
  doctor_argv[3] = context->project_dir;
  test_run(context, doctor_argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text,
                              "version: confit 0.1.0-rc1");
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text,
                              "generators enabled: header, reports, cmake, "
                              "qstar, build-selection");
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "options: 8");
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "profiles: 2");
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "targets: 2");
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "doctor ok");
  confit_test_process_result_clear(&result);
}

static void test_resolve_outputs(ConfitV1BaselineContext *context) {
  ConfitTestProcessResult result = {-1, 0, 0};
  char expected_path[4096];
  const char *json_argv[] = {0, "resolve", "--project", 0, "--profile",
                             "sim-dsh", "--format", "json", 0};
  const char *set_argv[] = {0, "resolve", "--project", 0, "--profile",
                            "sim-dsh", "--set", "delos.output.name=manual",
                            "--format", "toml", 0};
  const char *text_argv[] = {0, "resolve", "--project", 0, "--profile",
                             "sim-dsh", "--format", "text", 0};

  json_argv[0] = context->confit_bin;
  json_argv[3] = context->project_dir;
  test_run(context, json_argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  test_join(expected_path, sizeof(expected_path), context->golden_dir,
            "resolve.json");
  {
    char *expected = confit_test_fs_read_file(expected_path);
    test_expect_text_equal(result.stdout_text, expected, "resolve --format json");
    confit_test_fs_free(expected);
  }
  confit_test_process_result_clear(&result);

  text_argv[0] = context->confit_bin;
  text_argv[3] = context->project_dir;
  test_run(context, text_argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  test_join(expected_path, sizeof(expected_path), context->golden_dir,
            "resolve.txt");
  {
    char *expected = confit_test_fs_read_file(expected_path);
    test_expect_text_equal(result.stdout_text, expected, "resolve --format text");
    confit_test_fs_free(expected);
  }
  confit_test_process_result_clear(&result);

  set_argv[0] = context->confit_bin;
  set_argv[3] = context->project_dir;
  test_run(context, set_argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text,
                              "\"delos.output.name\" = \"manual\"");
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text,
                              "\"delos.output.name\" = \"cli --set\"");
  confit_test_process_result_clear(&result);
}

static void test_explain_and_graph(ConfitV1BaselineContext *context) {
  ConfitTestProcessResult result = {-1, 0, 0};
  char expected_path[4096];
  const char *explain_argv[] = {0, "explain", "--project", 0, "--profile",
                                "sim-dsh", "delos.debug.ddc", 0};
  const char *graph_argv[] = {0, "graph", "--project", 0, "--profile",
                              "sim-dsh", 0};

  explain_argv[0] = context->confit_bin;
  explain_argv[3] = context->project_dir;
  test_run(context, explain_argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  test_join(expected_path, sizeof(expected_path), context->golden_dir,
            "explain-delos-debug-ddc.txt");
  {
    char *expected = confit_test_fs_read_file(expected_path);
    test_expect_text_equal(result.stdout_text, expected, "explain");
    confit_test_fs_free(expected);
  }
  confit_test_process_result_clear(&result);

  graph_argv[0] = context->confit_bin;
  graph_argv[3] = context->project_dir;
  test_run(context, graph_argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  test_join(expected_path, sizeof(expected_path), context->golden_dir,
            "config.graph.json");
  {
    char *expected = confit_test_fs_read_file(expected_path);
    test_expect_text_equal(result.stdout_text, expected, "graph");
    confit_test_fs_free(expected);
  }
  confit_test_process_result_clear(&result);
}

static void test_generation(ConfitV1BaselineContext *context) {
  static const char *const artifacts[] = {
      "config.h",          "config.report.json", "config.explain.txt",
      "config.graph.json", "config.inputs.json", "config.cmake",
      "config/config.qsm", "config.qst",
  };
  ConfitTestProcessResult result = {-1, 0, 0};
  const char *first_argv[] = {0, "gen", "--project", 0, "--profile",
                              "sim-dsh", "--out", 0, "--artifact", "all",
                              0};
  const char *second_argv[] = {0, "gen", "--project", 0, "--profile",
                               "sim-dsh", "--out", 0, "--artifact", "all",
                               0};
  size_t index;

  first_argv[0] = context->confit_bin;
  first_argv[3] = context->project_dir;
  first_argv[7] = context->generated_a_dir;
  test_run(context, first_argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "gen ok:");
  confit_test_process_result_clear(&result);

  second_argv[0] = context->confit_bin;
  second_argv[3] = context->project_dir;
  second_argv[7] = context->generated_b_dir;
  test_run(context, second_argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "gen ok:");
  confit_test_process_result_clear(&result);

  for (index = 0U; index < sizeof(artifacts) / sizeof(artifacts[0]);
       ++index) {
    test_expect_result_file(context, artifacts[index]);
    test_expect_repeat_file(context, artifacts[index]);
  }
}

static void test_strict_v1_semantics(ConfitV1BaselineContext *context) {
  ConfitTestProcessResult result = {-1, 0, 0};
  const char *argv[] = {0, "check", "--project", 0, "--profile", "sim-dsh",
                        "--strict", 0};

  argv[0] = context->confit_bin;
  argv[3] = context->project_dir;
  test_run(context, argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(3, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stderr_text, "owner metadata missing");
  CONFIT_TEST_ASSERT_CONTAINS(result.stderr_text,
                              "schema warnings are fatal under --strict");
  confit_test_process_result_clear(&result);
}

static void test_schema_diagnostic(ConfitV1BaselineContext *context,
                                   const char *fixture_name,
                                   const char *source_name,
                                   const char *message) {
  ConfitTestProcessResult result = {-1, 0, 0};
  char fixture_dir[4096];
  const char *argv[] = {0, "check", "--project", 0, "--profile", "default",
                        0};

  test_join5(fixture_dir, sizeof(fixture_dir), context->source_dir, "tests",
             "fixtures", "v1-baseline", fixture_name);
  argv[0] = context->confit_bin;
  argv[3] = fixture_dir;
  test_run(context, argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(3, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stderr_text, source_name);
  CONFIT_TEST_ASSERT_CONTAINS(result.stderr_text, "schema error:");
  CONFIT_TEST_ASSERT_CONTAINS(result.stderr_text, message);
  confit_test_process_result_clear(&result);
}

static void test_schema_diagnostics(ConfitV1BaselineContext *context) {
  test_schema_diagnostic(context, "duplicate", "config/options/b.toml",
                         "duplicate option id");
  test_schema_diagnostic(context, "unknown-field", "config/options/debug.toml",
                         "unknown option field");
  test_schema_diagnostic(context, "missing-type", "config/options/debug.toml",
                         "missing option type");
  test_schema_diagnostic(context, "invalid-id", "config/options/debug.toml",
                         "invalid option id");
}

int main(int argc, char **argv) {
  ConfitV1BaselineContext context;
  char confit_bin_buffer[4096];
  char work_dir_buffer[4096];

  test_context_init(&context, argc, argv, confit_bin_buffer,
                    sizeof(confit_bin_buffer), work_dir_buffer,
                    sizeof(work_dir_buffer));
  (void)confit_test_fs_remove_tree(context.work_dir);
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(context.work_dir));

  test_version_and_doctor(&context);
  test_resolve_outputs(&context);
  test_explain_and_graph(&context);
  test_generation(&context);
  test_strict_v1_semantics(&context);
  test_schema_diagnostics(&context);

  return 0;
}
