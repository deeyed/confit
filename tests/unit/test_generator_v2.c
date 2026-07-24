#include <stdio.h>
#include <string.h>

#include "confit/constraint_v2.h"
#include "confit/diagnostic.h"
#include "confit/generator_v2.h"
#include "confit/host.h"
#include "confit/link_v2.h"
#include "confit/resolver_v2.h"
#include "confit/schema_v2.h"
#include "confit/status.h"

#include "test_assert.h"
#include "test_fs.h"
#include "test_process.h"

#ifndef CONFIT_TEST_SOURCE_DIR
#define CONFIT_TEST_SOURCE_DIR "."
#endif

static ConfitStatus load_compiled(const char *fixture,
                                  ConfitV2Project **out_project,
                                  ConfitV2LinkedProject **out_linked,
                                  ConfitV2CompiledStructure **out_compiled,
                                  ConfitDiagnostic *diagnostic) {
  char path[1024];
  ConfitStatus status;

  *out_project = 0;
  *out_linked = 0;
  *out_compiled = 0;
  status = confit_host_path_join(path, sizeof(path), CONFIT_TEST_SOURCE_DIR,
                                 fixture, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_schema_load_project(path, out_project, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_schema_link_project(*out_project, out_linked,
                                           diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_compile_structure(*out_linked, out_compiled, diagnostic);
  }
  if (status != CONFIT_OK) {
    confit_v2_compiled_structure_free(*out_compiled);
    confit_v2_linked_project_free(*out_linked);
    confit_v2_project_free(*out_project);
    *out_project = 0;
    *out_linked = 0;
    *out_compiled = 0;
  }
  return status;
}

static void free_compiled(ConfitV2Project *project,
                          ConfitV2LinkedProject *linked,
                          ConfitV2CompiledStructure *compiled) {
  confit_v2_compiled_structure_free(compiled);
  confit_v2_linked_project_free(linked);
  confit_v2_project_free(project);
}

static void assert_lf_text(const char *text) {
  const size_t size = strlen(text);

  CONFIT_TEST_ASSERT(size > 0U);
  CONFIT_TEST_ASSERT(text[size - 1U] == '\n');
  CONFIT_TEST_ASSERT(strchr(text, '\r') == 0);
}

static void run_external_smoke(const char *root) {
  char cmake_script_path[4096];
  char qstar_script_path[4096];
  char stdout_path[4096];
  char stderr_path[4096];
  ConfitTestProcessResult result;
  const char *const cmake_argv[] = {
      CONFIT_TEST_CMAKE_EXECUTABLE, "-P", cmake_script_path, 0};

  CONFIT_TEST_ASSERT(confit_test_fs_path_join(cmake_script_path,
                                              sizeof(cmake_script_path), root,
                                              "cmake-smoke.cmake"));
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(stdout_path, sizeof(stdout_path),
                                              root, "cmake-smoke.out"));
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(stderr_path, sizeof(stderr_path),
                                              root, "cmake-smoke.err"));
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      cmake_script_path,
      "include(\"${CMAKE_CURRENT_LIST_DIR}/config.cmake\")\n"
      "if(NOT CONFIT_V2_FIXTURE_CONFIG_CONFIT_ENUM_ARCH STREQUAL \"host\")\n"
      "  message(FATAL_ERROR \"unexpected V2 CMake value\")\n"
      "endif()\n"));
  result.exit_code = -1;
  result.stdout_text = 0;
  result.stderr_text = 0;
  CONFIT_TEST_ASSERT(confit_test_process_run(cmake_argv, root, stdout_path,
                                             stderr_path, &result));
  CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
  confit_test_process_result_clear(&result);

#ifdef CONFIT_TEST_QSTAR_EXECUTABLE
  {
    const char *const qstar_argv[] = {
        CONFIT_TEST_QSTAR_EXECUTABLE, "--file", "qstar.lua", "--color",
        "never", "--progress", "off", "check", 0};

    CONFIT_TEST_ASSERT(confit_test_fs_path_join(qstar_script_path,
                                                sizeof(qstar_script_path), root,
                                                "qstar.lua"));
    CONFIT_TEST_ASSERT(confit_test_fs_path_join(stdout_path, sizeof(stdout_path),
                                                root, "qstar-smoke.out"));
    CONFIT_TEST_ASSERT(confit_test_fs_path_join(stderr_path, sizeof(stderr_path),
                                                root, "qstar-smoke.err"));
    CONFIT_TEST_ASSERT(confit_test_fs_write_file(
        qstar_script_path,
        "qstar.project { name = \"confit-v2-artifact-smoke\", root = \".\" }\n"
        "local config = qstar.import_module(\"config\")\n"
        "local selection = qstar.import_module(\"fixture_selection\")\n"
        "if config.values[\"confit.enum.arch\"].effective.value ~= \"host\" then\n"
        "  error(\"unexpected canonical config value\")\n"
        "end\n"
        "if selection.values[\"confit.enum.arch\"].effective.value ~= \"host\" then\n"
        "  error(\"unexpected selection value\")\n"
        "end\n"
        "qstar.group \"all\" { deps = {} }\n"));
    result.exit_code = -1;
    result.stdout_text = 0;
    result.stderr_text = 0;
    CONFIT_TEST_ASSERT(confit_test_process_run(qstar_argv, root, stdout_path,
                                               stderr_path, &result));
    CONFIT_TEST_ASSERT_EQ_INT(0, result.exit_code);
    confit_test_process_result_clear(&result);
  }
#else
  (void)qstar_script_path;
#endif
}

static void expect_artifact_bundle(void) {
  static const ConfitV2ArtifactInput inputs[] = {
      {"config/options/types.toml", "sha256:types", "schema"},
      {"config/project.toml", "sha256:project", "schema"},
  };
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitV2CompiledStructure *compiled;
  ConfitV2Snapshot *snapshot = 0;
  ConfitV2ArtifactOptions options;
  ConfitV2ArtifactSet artifacts;
  ConfitDiagnostic diagnostic;
  char root[4096] = {0};
  char qsm_path[4096];
  char *first_qsm;
  char *second_qsm;
  size_t changed = 0U;

  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(load_compiled("tests/fixtures/schema-v2/valid", &project,
                                  &linked, &compiled, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_v2_snapshot_resolve(compiled, 0, &snapshot,
                                                &diagnostic) == CONFIT_OK);
  memset(&options, 0, sizeof(options));
  options.compiled = compiled;
  options.inputs = inputs;
  options.input_count = sizeof(inputs) / sizeof(inputs[0]);
  options.selection_name = "fixture_selection";
  memset(&artifacts, 0, sizeof(artifacts));
  CONFIT_TEST_ASSERT(confit_v2_generate_artifacts(snapshot, &options, &artifacts,
                                                   &diagnostic) == CONFIT_OK);

  assert_lf_text(artifacts.config_header);
  assert_lf_text(artifacts.report_json);
  assert_lf_text(artifacts.explain_text);
  assert_lf_text(artifacts.graph_json);
  assert_lf_text(artifacts.inputs_json);
  assert_lf_text(artifacts.changes_json);
  assert_lf_text(artifacts.cmake_fragment);
  assert_lf_text(artifacts.qsm_module);
  assert_lf_text(artifacts.selection_module);
  CONFIT_TEST_ASSERT_CONTAINS(artifacts.config_header, "CONFIT_SCHEMA_VERSION 2");
  CONFIT_TEST_ASSERT_CONTAINS(artifacts.report_json, "\"requested\"");
  CONFIT_TEST_ASSERT_CONTAINS(artifacts.report_json, "\"effective\"");
  CONFIT_TEST_ASSERT_CONTAINS(artifacts.report_json, "\"provenance\"");
  CONFIT_TEST_ASSERT_CONTAINS(artifacts.cmake_fragment,
                              "CONFIT_V2_FIXTURE_CONFIG_CONFIT_ENUM_ARCH");
  CONFIT_TEST_ASSERT_CONTAINS(artifacts.qsm_module,
                              "confit-config-manifest-v2");
  CONFIT_TEST_ASSERT_CONTAINS(artifacts.selection_module,
                              "confit-build-selection-v2");
  CONFIT_TEST_ASSERT_CONTAINS(artifacts.selection_module,
                              "confit.enum.arch");

  CONFIT_TEST_ASSERT(
      confit_test_fs_make_temp_dir(root, sizeof(root), "confit-v2-artifacts"));
  CONFIT_TEST_ASSERT(confit_v2_write_artifacts(root, &artifacts, &changed,
                                                &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(changed == 9U);
  run_external_smoke(root);
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(qsm_path, sizeof(qsm_path), root,
                                              "config/config.qsm"));
  first_qsm = confit_test_fs_read_file(qsm_path);
  CONFIT_TEST_ASSERT(first_qsm != 0);
  changed = 99U;
  CONFIT_TEST_ASSERT(confit_v2_write_artifacts(root, &artifacts, &changed,
                                                &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(changed == 0U);
  second_qsm = confit_test_fs_read_file(qsm_path);
  CONFIT_TEST_ASSERT(second_qsm != 0);
  CONFIT_TEST_ASSERT(strcmp(first_qsm, second_qsm) == 0);

  confit_test_fs_free(second_qsm);
  confit_test_fs_free(first_qsm);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
  confit_v2_artifact_set_clear(&artifacts);
  confit_v2_snapshot_free(snapshot);
  free_compiled(project, linked, compiled);
}

static void expect_selection_name_validation(void) {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitV2CompiledStructure *compiled;
  ConfitV2Snapshot *snapshot = 0;
  ConfitV2ArtifactOptions options;
  ConfitV2ArtifactSet artifacts;
  ConfitDiagnostic diagnostic;

  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(load_compiled("tests/fixtures/schema-v2/valid", &project,
                                  &linked, &compiled, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_v2_snapshot_resolve(compiled, 0, &snapshot,
                                                &diagnostic) == CONFIT_OK);
  memset(&options, 0, sizeof(options));
  options.selection_name = "../outside";
  memset(&artifacts, 0, sizeof(artifacts));
  CONFIT_TEST_ASSERT(confit_v2_generate_artifacts(snapshot, &options, &artifacts,
                                                   &diagnostic) ==
                      CONFIT_ERR_INVALID_ARGUMENT);
  confit_v2_artifact_set_clear(&artifacts);
  confit_v2_snapshot_free(snapshot);
  free_compiled(project, linked, compiled);
}

int main(void) {
  expect_artifact_bundle();
  expect_selection_name_validation();
  return 0;
}
