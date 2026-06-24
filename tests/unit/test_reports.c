#include <stdlib.h>
#include <string.h>

#include "confit/diagnostic.h"
#include "confit/generator.h"
#include "confit/graph.h"
#include "confit/host.h"
#include "confit/resolver.h"
#include "confit/schema.h"
#include "confit/status.h"

#ifndef CONFIT_TEST_SOURCE_DIR
#define CONFIT_TEST_SOURCE_DIR "."
#endif

static int join_fixture(char *out, size_t out_size, const char *fixture) {
  ConfitDiagnostic diagnostic;

  confit_diagnostic_init(&diagnostic);
  return confit_host_path_join(out, out_size, CONFIT_TEST_SOURCE_DIR, fixture,
                               &diagnostic) == CONFIT_OK;
}

static int read_fixture_text(const char *fixture, char **out_text) {
  ConfitDiagnostic diagnostic;
  size_t text_size;
  char path[512];

  if (!join_fixture(path, sizeof(path), fixture)) {
    return 0;
  }

  confit_diagnostic_init(&diagnostic);
  text_size = 0U;
  return confit_host_read_text_file(path, out_text, &text_size, &diagnostic) ==
         CONFIT_OK;
}

static int load_project(ConfitProject **out_project) {
  ConfitDiagnostic diagnostic;
  char path[512];

  if (!join_fixture(path, sizeof(path),
                    "tests/fixtures/schema/valid/basic")) {
    return 0;
  }

  confit_diagnostic_init(&diagnostic);
  *out_project = 0;
  return confit_schema_load_project(path, out_project, &diagnostic) ==
         CONFIT_OK;
}

static int expect_text(const char *actual, const char *golden_path) {
  char *expected;
  int ok;

  expected = 0;
  if (!read_fixture_text(golden_path, &expected)) {
    return 0;
  }

  ok = strcmp(actual, expected) == 0;
  free(expected);
  return ok;
}

static int text_has_no_cr(const char *text) {
  return text != 0 && strchr(text, '\r') == 0;
}

static int text_contains(const char *text, const char *needle) {
  return text != 0 && needle != 0 && strstr(text, needle) != 0;
}

static int load_project_fixture(const char *fixture,
                                ConfitProject **out_project) {
  ConfitDiagnostic diagnostic;
  char path[512];

  if (!join_fixture(path, sizeof(path), fixture)) {
    return 0;
  }

  confit_diagnostic_init(&diagnostic);
  *out_project = 0;
  return confit_schema_load_project(path, out_project, &diagnostic) ==
         CONFIT_OK;
}

static int check_portable_json_escaping(void) {
  static const ConfitInputFile portable_inputs[] = {
      {"config\\profiles\\windows.toml", "abcd"},
      {"config/options/paths.toml", "1234"},
  };
  ConfitReportOptions options;
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitResolvedConfig *config;
  char *report_json;
  char *inputs_json;
  int ok;

  if (!load_project_fixture("tests/fixtures/schema/valid/portable-paths",
                            &project)) {
    return 0;
  }

  confit_diagnostic_init(&diagnostic);
  config = 0;
  if (confit_resolver_resolve(project, "windows", 0, 0, 0U, &config,
                              &diagnostic) != CONFIT_OK) {
    confit_project_free(project);
    return 0;
  }

  options.profile_name = "windows";
  options.target_name = "C:\\targets\\renode";
  options.input_files = portable_inputs;
  options.input_file_count = sizeof(portable_inputs) / sizeof(portable_inputs[0]);

  report_json = 0;
  inputs_json = 0;
  if (confit_generate_report_json(project, config, &options, &report_json,
                                  &diagnostic) != CONFIT_OK ||
      confit_generate_inputs_json(project, &options, &inputs_json,
                                  &diagnostic) != CONFIT_OK) {
    confit_generator_string_free(report_json);
    confit_generator_string_free(inputs_json);
    confit_resolved_config_free(config);
    confit_project_free(project);
    return 0;
  }

  ok = text_has_no_cr(report_json) &&
       text_has_no_cr(inputs_json) &&
       text_contains(report_json,
                     "\"target\": \"C:\\\\targets\\\\renode\"") &&
       text_contains(report_json,
                     "\"value\": \"C:\\\\Users\\\\delos\\\\generated\\\\config\"") &&
       text_contains(report_json,
                     "\"value\": \"-DROOT=\\\"C:\\\\Delos SDK\\\";$ENV{DELOS_EXTRA}\"") &&
       text_contains(inputs_json,
                     "\"path\": \"config\\\\profiles\\\\windows.toml\"");

  confit_generator_string_free(report_json);
  confit_generator_string_free(inputs_json);
  confit_resolved_config_free(config);
  confit_project_free(project);
  return ok;
}

int main(void) {
  static const ConfitInputFile input_files[] = {
      {"config/project.toml",
       "568a5d26cbdc652cfd6d2416ceb8cff1f7ed54f183eaceecd67c82a1fad90227"},
      {"config/targets/host-sim.toml",
       "729472c78a5c3f1e29b8fe1f283a0b5cf3999487da96bbb2d3f3296bd4061c55"},
      {"config/options/types.toml",
       "ccb82865dcd88df2aeb7fad84b39db111a5889312555fe280ccbb7a1dfaa61b1"},
      {"config/profiles/sim-dsh.toml",
       "928fbd76f266e144f224930acda86a8b255297feec6c015fa246c7fd6111633f"},
      {"config/options/debug.toml",
       "8736b1e8de009bf1f83f2708ee4dc7505f851bf1458ef659aa49414e9542e99b"},
      {"config/profiles/debug.toml",
       "d82b7c08158f8456b60d44be4a85eae59c7008cc196a803d95e798f3399054d1"},
      {"config/options/runtime.toml",
       "36d9fc94969ef669fbeed84a103191c7a9f238fa49c89f2b1d796da754eb3a0d"},
  };
  ConfitReportOptions options;
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitResolvedConfig *config;
  ConfitGraph *graph;
  char *report_json;
  char *report_json_again;
  char *explain_text;
  char *graph_json;
  char *inputs_json;
  char *inputs_json_again;
  int ok;

  if (!load_project(&project)) {
    return 1;
  }

  confit_diagnostic_init(&diagnostic);
  config = 0;
  if (confit_resolver_resolve(project, "sim-dsh", 0, 0, 0U, &config,
                              &diagnostic) != CONFIT_OK) {
    confit_project_free(project);
    return 2;
  }

  graph = 0;
  if (confit_graph_build(project, &graph, &diagnostic) != CONFIT_OK ||
      confit_graph_validate(graph, &diagnostic) != CONFIT_OK) {
    confit_graph_free(graph);
    confit_resolved_config_free(config);
    confit_project_free(project);
    return 3;
  }

  options.profile_name = "sim-dsh";
  options.target_name = "host-sim";
  options.input_files = input_files;
  options.input_file_count = sizeof(input_files) / sizeof(input_files[0]);

  report_json = 0;
  report_json_again = 0;
  explain_text = 0;
  graph_json = 0;
  inputs_json = 0;
  inputs_json_again = 0;
  if (confit_generate_report_json(project, config, &options, &report_json,
                                  &diagnostic) != CONFIT_OK ||
      confit_generate_report_json(project, config, &options, &report_json_again,
                                  &diagnostic) != CONFIT_OK ||
      confit_generate_explain_report(project, config, &options, &explain_text,
                                     &diagnostic) != CONFIT_OK ||
      confit_graph_to_json(graph, &graph_json) != CONFIT_OK ||
      confit_generate_inputs_json(project, &options, &inputs_json,
                                  &diagnostic) != CONFIT_OK ||
      confit_generate_inputs_json(project, &options, &inputs_json_again,
                                  &diagnostic) != CONFIT_OK) {
    confit_generator_string_free(report_json);
    confit_generator_string_free(report_json_again);
    confit_generator_string_free(explain_text);
    confit_graph_string_free(graph_json);
    confit_generator_string_free(inputs_json);
    confit_generator_string_free(inputs_json_again);
    confit_graph_free(graph);
    confit_resolved_config_free(config);
    confit_project_free(project);
    return 4;
  }

  ok = expect_text(report_json, "tests/golden/reports/config.report.json") &&
       expect_text(explain_text, "tests/golden/reports/config.explain.txt") &&
       expect_text(graph_json, "tests/golden/reports/config.graph.json") &&
       expect_text(inputs_json, "tests/golden/reports/config.inputs.json") &&
       strcmp(report_json, report_json_again) == 0 &&
       strcmp(inputs_json, inputs_json_again) == 0 &&
       check_portable_json_escaping() &&
       strstr(report_json, "timestamp") == 0 &&
       strstr(inputs_json, "/Users/") == 0 &&
       text_has_no_cr(report_json) &&
       text_has_no_cr(explain_text) &&
       text_has_no_cr(graph_json) &&
       text_has_no_cr(inputs_json);

  confit_generator_string_free(report_json);
  confit_generator_string_free(report_json_again);
  confit_generator_string_free(explain_text);
  confit_graph_string_free(graph_json);
  confit_generator_string_free(inputs_json);
  confit_generator_string_free(inputs_json_again);
  confit_graph_free(graph);
  confit_resolved_config_free(config);
  confit_project_free(project);
  return ok ? 0 : 5;
}
