#include <stdlib.h>
#include <string.h>

#include "confit/diagnostic.h"
#include "confit/generator.h"
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

int main(void) {
  ConfitConfigHeaderOptions options;
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitResolvedConfig *config;
  ConfitBuildIntegrationOptions build_options;
  char *header;
  char *header_again;
  char *cmake_fragment;
  char *qstar_manifest;
  char *golden;
  char *cmake_golden;
  char *qstar_golden;
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

  options.profile_name = "sim-dsh";
  options.target_name = "host-sim";
  build_options.profile_name = "sim-dsh";
  build_options.target_name = "host-sim";
  build_options.header_path = "config.h";
  build_options.report_json_path = "config.report.json";
  build_options.explain_text_path = "config.explain.txt";
  build_options.graph_json_path = "config.graph.json";
  build_options.inputs_json_path = "config.inputs.json";
  header = 0;
  header_again = 0;
  cmake_fragment = 0;
  qstar_manifest = 0;
  golden = 0;
  cmake_golden = 0;
  qstar_golden = 0;
  if (confit_generate_config_header(project, config, &options, &header,
                                    &diagnostic) != CONFIT_OK ||
      confit_generate_config_header(project, config, &options, &header_again,
                                    &diagnostic) != CONFIT_OK ||
      confit_generate_cmake_fragment(project, config, &build_options,
                                     &cmake_fragment, &diagnostic) !=
          CONFIT_OK ||
      confit_generate_qstar_manifest(project, config, &build_options,
                                     &qstar_manifest, &diagnostic) !=
          CONFIT_OK ||
      !read_fixture_text("tests/golden/generator/sim-dsh-config.h", &golden) ||
      !read_fixture_text("tests/golden/generator/sim-dsh-config.cmake",
                         &cmake_golden) ||
      !read_fixture_text("tests/golden/generator/sim-dsh-config.qst",
                         &qstar_golden)) {
    confit_generator_string_free(header);
    confit_generator_string_free(header_again);
    confit_generator_string_free(cmake_fragment);
    confit_generator_string_free(qstar_manifest);
    free(golden);
    free(cmake_golden);
    free(qstar_golden);
    confit_resolved_config_free(config);
    confit_project_free(project);
    return 3;
  }

  ok = strcmp(header, golden) == 0 && strcmp(header, header_again) == 0 &&
       strcmp(cmake_fragment, cmake_golden) == 0 &&
       strcmp(qstar_manifest, qstar_golden) == 0 &&
       strstr(header, "timestamp") == 0 &&
       strstr(header, "/Users/") == 0 &&
       strstr(cmake_fragment, "timestamp") == 0 &&
       strstr(qstar_manifest, "/Users/") == 0;

  confit_generator_string_free(header);
  confit_generator_string_free(header_again);
  confit_generator_string_free(cmake_fragment);
  confit_generator_string_free(qstar_manifest);
  free(golden);
  free(cmake_golden);
  free(qstar_golden);
  confit_resolved_config_free(config);
  confit_project_free(project);
  return ok ? 0 : 4;
}
