#include <stdlib.h>
#include <string.h>

#include "confit/diagnostic.h"
#include "confit/explain.h"
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

static int expect_explanation(const ConfitProject *project,
                              const ConfitResolvedConfig *config,
                              const char *option_id, const char *golden_path) {
  ConfitDiagnostic diagnostic;
  char *actual;
  char *expected;
  int ok;

  confit_diagnostic_init(&diagnostic);
  actual = 0;
  expected = 0;
  if (confit_explain_option(project, config, option_id, &actual,
                            &diagnostic) != CONFIT_OK ||
      !read_fixture_text(golden_path, &expected)) {
    confit_explain_string_free(actual);
    free(expected);
    return 0;
  }

  ok = strcmp(actual, expected) == 0;
  confit_explain_string_free(actual);
  free(expected);
  return ok;
}

int main(void) {
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitResolvedConfig *config;
  char *text;

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

  if (!expect_explanation(project, config, "delos.debug.ddc",
                          "tests/golden/explain/delos-debug-ddc.txt")) {
    confit_resolved_config_free(config);
    confit_project_free(project);
    return 3;
  }
  if (!expect_explanation(
          project, config, "delos.internal.debug_gate",
          "tests/golden/explain/delos-internal-debug-gate.txt")) {
    confit_resolved_config_free(config);
    confit_project_free(project);
    return 4;
  }

  text = 0;
  if (confit_explain_option(project, config, "delos.missing", &text,
                            &diagnostic) != CONFIT_ERR_SCHEMA ||
      !confit_diagnostic_has_error(&diagnostic) ||
      diagnostic.message == 0 ||
      strcmp(diagnostic.message, "unknown explained option") != 0) {
    confit_explain_string_free(text);
    confit_resolved_config_free(config);
    confit_project_free(project);
    return 5;
  }
  confit_explain_string_free(text);

  confit_resolved_config_free(config);
  confit_project_free(project);
  return 0;
}
