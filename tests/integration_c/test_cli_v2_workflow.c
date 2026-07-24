#include "test_assert.h"
#include "test_fs.h"
#include "test_process.h"

#include <stdio.h>
#include <string.h>

#ifndef CONFIT_TEST_SOURCE_DIR
#define CONFIT_TEST_SOURCE_DIR "."
#endif

typedef struct ConfitCliV2WorkflowContext {
  const char *confit_bin;
  const char *source_dir;
  const char *work_dir;
  char parus_dir[4096];
  char delos_dir[4096];
  char compat_path[4096];
  char v1_dir[4096];
  char generated_dir[4096];
  char candidate_dir[4096];
  unsigned int run_index;
} ConfitCliV2WorkflowContext;

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

static void test_join5(char *out, size_t out_size, const char *first,
                       const char *second, const char *third,
                       const char *fourth, const char *fifth) {
  char scratch[4096];
  char scratch2[4096];

  test_join3(scratch, sizeof(scratch), first, second, third);
  test_join(scratch2, sizeof(scratch2), scratch, fourth);
  test_join(out, out_size, scratch2, fifth);
}

static void test_sibling_path(char *out, size_t out_size, const char *path,
                              const char *name) {
  const char *separator = strrchr(path, '/');
  char directory[4096];
  size_t size;

  if (separator == 0) {
    test_join(out, out_size, ".", name);
    return;
  }
  size = (size_t)(separator - path);
  CONFIT_TEST_ASSERT(size + 1U < sizeof(directory));
  memcpy(directory, path, size);
  directory[size] = '\0';
  test_join(out, out_size, directory, name);
}

static void test_context_init(ConfitCliV2WorkflowContext *context, int argc,
                              char **argv, char *bin_buffer,
                              size_t bin_buffer_size, char *work_buffer,
                              size_t work_buffer_size) {
  if (argc == 4) {
    context->confit_bin = argv[1];
    context->source_dir = argv[2];
    context->work_dir = argv[3];
  } else if (argc == 1) {
    test_sibling_path(bin_buffer, bin_buffer_size, argv[0], "confit");
    test_sibling_path(work_buffer, work_buffer_size, argv[0],
                      "round16-cli-v2-direct");
    context->confit_bin = bin_buffer;
    context->source_dir = CONFIT_TEST_SOURCE_DIR;
    context->work_dir = work_buffer;
  } else {
    CONFIT_TEST_FAIL("usage: confit_test_cli_v2_workflow <bin> <source> <work>");
  }
  test_join5(context->parus_dir, sizeof(context->parus_dir), context->source_dir,
             "tests", "fixtures", "compat-v2", "parus");
  test_join5(context->delos_dir, sizeof(context->delos_dir), context->source_dir,
             "tests", "fixtures", "compat-v2", "delos");
  test_join3(context->compat_path, sizeof(context->compat_path), context->source_dir,
             "tests/fixtures/compat-v2", "realish.toml");
  test_join5(context->v1_dir, sizeof(context->v1_dir), context->source_dir,
             "tests", "fixtures", "schema/valid", "basic");
  test_join(context->generated_dir, sizeof(context->generated_dir),
            context->work_dir, "generated");
  test_join(context->candidate_dir, sizeof(context->candidate_dir),
            context->work_dir, "candidate");
  context->run_index = 0U;
}

static void test_run(ConfitCliV2WorkflowContext *context,
                     const char *const *argv, ConfitTestProcessResult *result) {
  char stdout_path[4096];
  char stderr_path[4096];
  char stdout_name[64];
  char stderr_name[64];

  confit_test_process_result_clear(result);
  (void)snprintf(stdout_name, sizeof(stdout_name), "run-%03u.out", context->run_index);
  (void)snprintf(stderr_name, sizeof(stderr_name), "run-%03u.err", context->run_index);
  context->run_index += 1U;
  test_join(stdout_path, sizeof(stdout_path), context->work_dir, stdout_name);
  test_join(stderr_path, sizeof(stderr_path), context->work_dir, stderr_name);
  CONFIT_TEST_ASSERT(confit_test_process_run(argv, 0, stdout_path, stderr_path,
                                             result));
}

static void test_v2_commands(ConfitCliV2WorkflowContext *context) {
  ConfitTestProcessResult result = {-1, 0, 0};
  char artifact[4096];
  const char *check[] = {0, "check", "--project", 0, "--profile", "release", 0};
  const char *resolve[] = {0, "resolve", "--project", 0, "--profile", "release",
                           "--format", "json", 0};
  const char *gen[] = {0, "gen", "--project", 0, "--profile", "release", "--out",
                       0, "--artifact", "all", 0};
  const char *explain[] = {0, "explain", "--project", 0, "--profile", "release",
                           "parus.compat.capacity", 0};
  const char *graph[] = {0, "graph", "--project", 0, "--format", "json", 0};
  const char *list[] = {0, "list", "--project", 0, "--kind", "options", 0};
  const char *diff[] = {0, "diff", "--project", 0, "--profile", "release", "--base",
                        "base", "--format", "json", 0};

  check[0] = context->confit_bin;
  check[3] = context->parus_dir;
  test_run(context, check, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "check ok");
  confit_test_process_result_clear(&result);

  resolve[0] = context->confit_bin;
  resolve[3] = context->parus_dir;
  test_run(context, resolve, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "confit-resolved-v2");
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "effective_origin");
  confit_test_process_result_clear(&result);

  gen[0] = context->confit_bin;
  gen[3] = context->parus_dir;
  gen[7] = context->generated_dir;
  test_run(context, gen, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  test_join3(artifact, sizeof(artifact), context->generated_dir, "config", "config.qsm");
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(artifact));
  test_join(artifact, sizeof(artifact), context->generated_dir, "config.cmake");
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(artifact));
  test_join3(artifact, sizeof(artifact), context->generated_dir, "build_selection",
             "build_selection.qsm");
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(artifact));
  confit_test_process_result_clear(&result);

  explain[0] = context->confit_bin;
  explain[3] = context->parus_dir;
  test_run(context, explain, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "parus.compat.capacity");
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "effective origin:");
  confit_test_process_result_clear(&result);

  graph[0] = context->confit_bin;
  graph[3] = context->parus_dir;
  test_run(context, graph, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "confit-graph-v2");
  confit_test_process_result_clear(&result);

  list[0] = context->confit_bin;
  list[3] = context->parus_dir;
  test_run(context, list, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "parus.compat.capacity");
  confit_test_process_result_clear(&result);

  diff[0] = context->confit_bin;
  diff[3] = context->parus_dir;
  test_run(context, diff, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "confit-diff-v2");
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "parus.compat.capacity");
  confit_test_process_result_clear(&result);
}

static void test_v2_compat_and_diagnostic(ConfitCliV2WorkflowContext *context) {
  ConfitTestProcessResult result = {-1, 0, 0};
  const char *compat[] = {0, "compat", "--parus", 0, "--delos", 0, "--compat", 0,
                          "--format", "json", 0};
  const char *invalid[] = {0, "resolve", "--project", 0, "--set",
                           "delos.missing=true", "--diagnostic-format", "json", 0};

  compat[0] = context->confit_bin;
  compat[3] = context->parus_dir;
  compat[5] = context->delos_dir;
  compat[7] = context->compat_path;
  test_run(context, compat, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "confit-compat-report-v2");
  confit_test_process_result_clear(&result);

  invalid[0] = context->confit_bin;
  invalid[3] = context->delos_dir;
  test_run(context, invalid, &result);
  CONFIT_TEST_ASSERT(result.exit_code != 0);
  CONFIT_TEST_ASSERT_CONTAINS(result.stderr_text, "confit-diagnostic-v2");
  confit_test_process_result_clear(&result);
}

static void test_v1_migration(ConfitCliV2WorkflowContext *context) {
  ConfitTestProcessResult result = {-1, 0, 0};
  char source_path[4096];
  char source_config_dir[4096];
  char candidate_project[4096];
  char report_path[4096];
  char *before;
  char *after;
  char *report_before;
  char *report_after;
  const char *migrate[] = {0, "migrate", "--project", 0, "--out", 0, 0};
  const char *check[] = {0, "check", "--project", 0, 0};
  const char *same_output[] = {0, "migrate", "--project", 0, "--out", 0, 0};

  test_join3(source_path, sizeof(source_path), context->v1_dir, "config", "project.toml");
  before = confit_test_fs_read_file(source_path);
  CONFIT_TEST_ASSERT(before != 0);
  migrate[0] = context->confit_bin;
  migrate[3] = context->v1_dir;
  migrate[5] = context->candidate_dir;
  test_run(context, migrate, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "migration candidate ok");
  confit_test_process_result_clear(&result);
  after = confit_test_fs_read_file(source_path);
  CONFIT_TEST_ASSERT(after != 0);
  CONFIT_TEST_ASSERT(strcmp(before, after) == 0);
  confit_test_fs_free(before);
  confit_test_fs_free(after);

  test_join3(candidate_project, sizeof(candidate_project), context->candidate_dir,
             "config", "project.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(candidate_project));
  test_join(report_path, sizeof(report_path), context->candidate_dir,
            "migration-report.json");
  report_before = confit_test_fs_read_file(report_path);
  CONFIT_TEST_ASSERT(report_before != 0);
  CONFIT_TEST_ASSERT_CONTAINS(report_before, "candidate only");

  check[0] = context->confit_bin;
  check[3] = context->candidate_dir;
  test_run(context, check, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  confit_test_process_result_clear(&result);

  test_run(context, migrate, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  confit_test_process_result_clear(&result);
  report_after = confit_test_fs_read_file(report_path);
  CONFIT_TEST_ASSERT(report_after != 0);
  CONFIT_TEST_ASSERT(strcmp(report_before, report_after) == 0);
  confit_test_fs_free(report_before);
  confit_test_fs_free(report_after);

  same_output[0] = context->confit_bin;
  same_output[3] = context->v1_dir;
  same_output[5] = context->v1_dir;
  test_run(context, same_output, &result);
  CONFIT_TEST_ASSERT(result.exit_code != 0);
  CONFIT_TEST_ASSERT_CONTAINS(result.stderr_text, "separate from the source");
  confit_test_process_result_clear(&result);

  test_join(source_config_dir, sizeof(source_config_dir), context->v1_dir,
            "config");
  same_output[3] = source_config_dir;
  same_output[5] = context->v1_dir;
  test_run(context, same_output, &result);
  CONFIT_TEST_ASSERT(result.exit_code != 0);
  CONFIT_TEST_ASSERT_CONTAINS(result.stderr_text, "separate from the source");
  confit_test_process_result_clear(&result);
}

int main(int argc, char **argv) {
  ConfitCliV2WorkflowContext context;
  char bin_buffer[4096];
  char work_buffer[4096];

  memset(&context, 0, sizeof(context));
  test_context_init(&context, argc, argv, bin_buffer, sizeof(bin_buffer),
                    work_buffer, sizeof(work_buffer));
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(context.work_dir));
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(context.work_dir));
  test_v2_commands(&context);
  test_v2_compat_and_diagnostic(&context);
  test_v1_migration(&context);
  return 0;
}
