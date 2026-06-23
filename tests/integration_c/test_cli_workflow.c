#include "test_assert.h"
#include "test_fs.h"
#include "test_process.h"

#include <stdio.h>
#include <string.h>

#ifndef CONFIT_TEST_SOURCE_DIR
#define CONFIT_TEST_SOURCE_DIR "."
#endif

typedef struct ConfitCliWorkflowContext {
  const char *confit_bin;
  const char *source_dir;
  const char *work_dir;
  char project_dir[4096];
  char parus_dir[4096];
  char delos_dir[4096];
  char gen_dir[4096];
  char init_minimal_dir[4096];
  char init_delos_dir[4096];
  char init_parus_dir[4096];
  char init_dry_run_dir[4096];
  unsigned int run_index;
} ConfitCliWorkflowContext;

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
  size_t size;
  size_t index;
  size_t slash_index;

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

static void test_context_init(ConfitCliWorkflowContext *context,
                              int argc, char **argv, char *confit_bin_buffer,
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
                           "round3-cli-c-direct");
    context->confit_bin = confit_bin_buffer;
    context->source_dir = CONFIT_TEST_SOURCE_DIR;
    context->work_dir = work_dir_buffer;
  } else {
    CONFIT_TEST_FAIL(
        "usage: confit_test_cli_workflow <confit-bin> <source-dir> <work-dir>");
  }

  test_join5(context->project_dir, sizeof(context->project_dir),
             context->source_dir, "tests", "fixtures", "schema/valid",
             "basic");
  test_join5(context->parus_dir, sizeof(context->parus_dir),
             context->source_dir, "tests", "fixtures", "compat", "parus");
  test_join5(context->delos_dir, sizeof(context->delos_dir),
             context->source_dir, "tests", "fixtures", "compat", "delos");
  test_join(context->gen_dir, sizeof(context->gen_dir), context->work_dir,
            "generated");
  test_join(context->init_minimal_dir, sizeof(context->init_minimal_dir),
            context->work_dir, "init-minimal");
  test_join(context->init_delos_dir, sizeof(context->init_delos_dir),
            context->work_dir, "init-delos");
  test_join(context->init_parus_dir, sizeof(context->init_parus_dir),
            context->work_dir, "init-parus");
  test_join(context->init_dry_run_dir, sizeof(context->init_dry_run_dir),
            context->work_dir, "init-dry-run");
  context->run_index = 0U;
}

static void test_run(ConfitCliWorkflowContext *context,
                     const char *const *argv,
                     ConfitTestProcessResult *result) {
  char stdout_path[4096];
  char stderr_path[4096];
  char stdout_name[64];
  char stderr_name[64];

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

static void test_expect_file_contains(const char *path, const char *needle) {
  char *text;

  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(path));
  text = confit_test_fs_read_file(path);
  CONFIT_TEST_ASSERT(text != 0);
  CONFIT_TEST_ASSERT_CONTAINS(text, needle);
  confit_test_fs_free(text);
}

static void test_doctor(ConfitCliWorkflowContext *context) {
  ConfitTestProcessResult result;
  const char *argv[] = {0, "doctor", "--project", 0, 0};

  result.exit_code = -1;
  result.stdout_text = 0;
  result.stderr_text = 0;
  argv[0] = context->confit_bin;
  argv[3] = context->project_dir;
  test_run(context, argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "doctor ok");
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "options:");
  confit_test_process_result_clear(&result);
}

static void test_init_template(ConfitCliWorkflowContext *context,
                               const char *template_name,
                               const char *project_dir) {
  ConfitTestProcessResult result;
  char project_toml[4096];
  const char *argv[] = {0, "init", "--project", 0, "--template", 0, 0};

  result.exit_code = -1;
  result.stdout_text = 0;
  result.stderr_text = 0;
  argv[0] = context->confit_bin;
  argv[3] = project_dir;
  argv[5] = template_name;
  test_run(context, argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "init template:");
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "create dir:");
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "create file:");
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "init ok:");
  confit_test_process_result_clear(&result);

  test_join3(project_toml, sizeof(project_toml), project_dir, "config",
             "project.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(project_toml));
}

static void test_check_project_profile(ConfitCliWorkflowContext *context,
                                       const char *project_dir,
                                       const char *profile_name) {
  ConfitTestProcessResult result;
  const char *argv[] = {0, "check", "--project", 0, "--profile",
                        0, 0};

  result.exit_code = -1;
  result.stdout_text = 0;
  result.stderr_text = 0;
  argv[0] = context->confit_bin;
  argv[3] = project_dir;
  argv[5] = profile_name;
  test_run(context, argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "check ok");
  confit_test_process_result_clear(&result);
}

static void test_check(ConfitCliWorkflowContext *context) {
  test_check_project_profile(context, context->project_dir, "sim-dsh");
}

static void test_resolve(ConfitCliWorkflowContext *context) {
  ConfitTestProcessResult result;
  ConfitTestProcessResult result_again;
  const char *text_argv[] = {0, "resolve", "--project", 0, "--profile",
                             "sim-dsh", 0};
  const char *json_argv[] = {0, "resolve", "--project", 0, "--profile",
                             "sim-dsh", "--format", "json", 0};
  const char *toml_set_argv[] = {0, "resolve", "--project", 0, "--profile",
                                 "sim-dsh", "--set",
                                 "delos.output.name=manual", "--format",
                                 "toml", 0};
  const char *explain_set_argv[] = {0, "explain", "--project", 0, "--profile",
                                    "sim-dsh", "--set",
                                    "delos.output.name=manual",
                                    "delos.output.name", 0};

  result.exit_code = -1;
  result.stdout_text = 0;
  result.stderr_text = 0;
  result_again.exit_code = -1;
  result_again.stdout_text = 0;
  result_again.stderr_text = 0;

  text_argv[0] = context->confit_bin;
  text_argv[3] = context->project_dir;
  test_run(context, text_argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text,
                              "delos.debug.ddc = true");
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text,
                              "source: profiles/debug.toml");
  confit_test_process_result_clear(&result);

  json_argv[0] = context->confit_bin;
  json_argv[3] = context->project_dir;
  test_run(context, json_argv, &result);
  test_run(context, json_argv, &result_again);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_EQ_INT(0, result_again.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text,
                              "\"schema\": \"confit-resolved-v1\"");
  CONFIT_TEST_ASSERT(strcmp(result.stdout_text, result_again.stdout_text) == 0);
  confit_test_process_result_clear(&result);
  confit_test_process_result_clear(&result_again);

  toml_set_argv[0] = context->confit_bin;
  toml_set_argv[3] = context->project_dir;
  test_run(context, toml_set_argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text,
                              "\"delos.output.name\" = \"manual\"");
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text,
                              "\"delos.output.name\" = \"cli --set\"");
  confit_test_process_result_clear(&result);

  explain_set_argv[0] = context->confit_bin;
  explain_set_argv[3] = context->project_dir;
  test_run(context, explain_set_argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "set by: cli --set");
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text,
                              "value comes from cli --set");
  confit_test_process_result_clear(&result);
}

static void test_check_strict(ConfitCliWorkflowContext *context) {
  ConfitTestProcessResult result;
  const char *argv[] = {0, "check", "--project", 0, "--profile",
                        "sim-dsh", "--strict", 0};

  result.exit_code = -1;
  result.stdout_text = 0;
  result.stderr_text = 0;
  argv[0] = context->confit_bin;
  argv[3] = context->project_dir;
  test_run(context, argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(3, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stderr_text, "owner metadata missing");
  CONFIT_TEST_ASSERT_CONTAINS(result.stderr_text,
                              "schema warnings are fatal under --strict");
  confit_test_process_result_clear(&result);
}

static void test_init_templates(ConfitCliWorkflowContext *context) {
  ConfitTestProcessResult result;
  char dry_run_project_toml[4096];
  const char *dry_run_argv[] = {0, "init", "--project", 0, "--template",
                                "minimal", "--dry-run", 0};
  const char *refuse_argv[] = {0, "init", "--project", 0, "--template",
                               "delos", 0};
  const char *force_argv[] = {0, "init", "--project", 0, "--template",
                              "delos", "--force", 0};

  test_init_template(context, "minimal", context->init_minimal_dir);
  test_init_template(context, "delos", context->init_delos_dir);
  test_init_template(context, "parus", context->init_parus_dir);

  test_check_project_profile(context, context->init_minimal_dir, "default");
  test_check_project_profile(context, context->init_delos_dir, "default");
  test_check_project_profile(context, context->init_parus_dir, "default");

  result.exit_code = -1;
  result.stdout_text = 0;
  result.stderr_text = 0;
  dry_run_argv[0] = context->confit_bin;
  dry_run_argv[3] = context->init_dry_run_dir;
  test_run(context, dry_run_argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "init dry-run ok:");
  test_join3(dry_run_project_toml, sizeof(dry_run_project_toml),
             context->init_dry_run_dir, "config", "project.toml");
  CONFIT_TEST_ASSERT(!confit_test_fs_file_exists(dry_run_project_toml));
  confit_test_process_result_clear(&result);

  result.exit_code = -1;
  result.stdout_text = 0;
  result.stderr_text = 0;
  refuse_argv[0] = context->confit_bin;
  refuse_argv[3] = context->init_delos_dir;
  test_run(context, refuse_argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(1, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stderr_text,
                              "init refuses to overwrite existing file");
  confit_test_process_result_clear(&result);

  result.exit_code = -1;
  result.stdout_text = 0;
  result.stderr_text = 0;
  force_argv[0] = context->confit_bin;
  force_argv[3] = context->init_delos_dir;
  test_run(context, force_argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "overwrite file:");
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "init ok:");
  confit_test_process_result_clear(&result);
}

static void test_list(ConfitCliWorkflowContext *context) {
  ConfitTestProcessResult result;
  const char *argv[] = {0, "list", "--project", 0, "--category", "debug", 0};

  result.exit_code = -1;
  result.stdout_text = 0;
  result.stderr_text = 0;
  argv[0] = context->confit_bin;
  argv[3] = context->project_dir;
  test_run(context, argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "delos.debug.ddc");
  CONFIT_TEST_ASSERT_NOT_CONTAINS(result.stdout_text,
                                  "delos.scheduler.task_slots");
  confit_test_process_result_clear(&result);
}

static void test_graph_json(ConfitCliWorkflowContext *context) {
  ConfitTestProcessResult result;
  const char *argv[] = {0, "graph", "--project", 0, "--profile",
                        "sim-dsh", 0};

  result.exit_code = -1;
  result.stdout_text = 0;
  result.stderr_text = 0;
  argv[0] = context->confit_bin;
  argv[3] = context->project_dir;
  test_run(context, argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "\"nodes\":");
  confit_test_process_result_clear(&result);
}

static void test_graph_dot(ConfitCliWorkflowContext *context) {
  ConfitTestProcessResult result;
  const char *argv[] = {0, "graph", "--project", 0, "--profile", "sim-dsh",
                        "--format", "dot", 0};

  result.exit_code = -1;
  result.stdout_text = 0;
  result.stderr_text = 0;
  argv[0] = context->confit_bin;
  argv[3] = context->project_dir;
  test_run(context, argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "digraph confit");
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "delos.debug.ddc");
  confit_test_process_result_clear(&result);
}

static void test_explain(ConfitCliWorkflowContext *context) {
  ConfitTestProcessResult result;
  const char *argv[] = {0, "explain", "--project", 0, "--profile", "sim-dsh",
                        "delos.debug.ddc", 0};

  result.exit_code = -1;
  result.stdout_text = 0;
  result.stderr_text = 0;
  argv[0] = context->confit_bin;
  argv[3] = context->project_dir;
  test_run(context, argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "forced by:");
  confit_test_process_result_clear(&result);
}

static void test_gen(ConfitCliWorkflowContext *context) {
  ConfitTestProcessResult result;
  char config_h[4096];
  char report_json[4096];
  char explain_txt[4096];
  char graph_json[4096];
  char inputs_json[4096];
  const char *argv[] = {0, "gen", "--project", 0, "--profile", "sim-dsh",
                        "--out", 0, 0};

  result.exit_code = -1;
  result.stdout_text = 0;
  result.stderr_text = 0;
  argv[0] = context->confit_bin;
  argv[3] = context->project_dir;
  argv[7] = context->gen_dir;
  test_run(context, argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "gen ok:");
  confit_test_process_result_clear(&result);

  test_join(config_h, sizeof(config_h), context->gen_dir, "config.h");
  test_join(report_json, sizeof(report_json), context->gen_dir,
            "config.report.json");
  test_join(explain_txt, sizeof(explain_txt), context->gen_dir,
            "config.explain.txt");
  test_join(graph_json, sizeof(graph_json), context->gen_dir,
            "config.graph.json");
  test_join(inputs_json, sizeof(inputs_json), context->gen_dir,
            "config.inputs.json");

  test_expect_file_contains(config_h, "DELOS_CONFIG_DEBUG_DDC");
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(report_json));
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(explain_txt));
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(graph_json));
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(inputs_json));
}

static void test_compat(ConfitCliWorkflowContext *context) {
  ConfitTestProcessResult result;
  const char *argv[] = {0, "compat", "--parus", 0, "--delos", 0,
                        "--profile", "parus-delos-debug", 0};

  result.exit_code = -1;
  result.stdout_text = 0;
  result.stderr_text = 0;
  argv[0] = context->confit_bin;
  argv[3] = context->parus_dir;
  argv[5] = context->delos_dir;
  test_run(context, argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stdout_text, "compat ok");
  confit_test_process_result_clear(&result);
}

static void test_unknown_command(ConfitCliWorkflowContext *context) {
  ConfitTestProcessResult result;
  const char *argv[] = {0, "unknown-command", 0};

  result.exit_code = -1;
  result.stdout_text = 0;
  result.stderr_text = 0;
  argv[0] = context->confit_bin;
  test_run(context, argv, &result);
  CONFIT_TEST_ASSERT_EQ_INT(1, result.exit_code);
  CONFIT_TEST_ASSERT_CONTAINS(result.stderr_text, "try 'confit help'");
  confit_test_process_result_clear(&result);
}

int main(int argc, char **argv) {
  ConfitCliWorkflowContext context;
  char confit_bin_buffer[4096];
  char work_dir_buffer[4096];

  test_context_init(&context, argc, argv, confit_bin_buffer,
                    sizeof(confit_bin_buffer), work_dir_buffer,
                    sizeof(work_dir_buffer));

  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(context.work_dir));
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(context.work_dir));

  test_init_templates(&context);
  test_doctor(&context);
  test_check(&context);
  test_resolve(&context);
  test_check_strict(&context);
  test_list(&context);
  test_graph_json(&context);
  test_graph_dot(&context);
  test_explain(&context);
  test_gen(&context);
  test_compat(&context);
  test_unknown_command(&context);

  return 0;
}
