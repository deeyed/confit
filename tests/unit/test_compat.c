#include <string.h>

#include "confit/compat.h"
#include "confit/diagnostic.h"
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

static int load_resolved_pair(ConfitProject **out_parus,
                              ConfitResolvedConfig **out_parus_config,
                              ConfitProject **out_delos,
                              ConfitResolvedConfig **out_delos_config) {
  ConfitDiagnostic diagnostic;
  char path[512];

  *out_parus = 0;
  *out_parus_config = 0;
  *out_delos = 0;
  *out_delos_config = 0;

  if (!join_fixture(path, sizeof(path), "tests/fixtures/compat/parus")) {
    return 0;
  }

  confit_diagnostic_init(&diagnostic);
  if (confit_schema_load_project(path, out_parus, &diagnostic) != CONFIT_OK ||
      confit_resolver_resolve(*out_parus, "parus-delos-debug", 0, 0, 0U,
                              out_parus_config, &diagnostic) != CONFIT_OK) {
    return 0;
  }

  if (!join_fixture(path, sizeof(path), "tests/fixtures/compat/delos")) {
    return 0;
  }
  if (confit_schema_load_project(path, out_delos, &diagnostic) != CONFIT_OK ||
      confit_resolver_resolve(*out_delos, "parus-delos-debug", 0, 0, 0U,
                              out_delos_config, &diagnostic) != CONFIT_OK) {
    return 0;
  }

  return 1;
}

static int load_suite(const char *fixture, ConfitCompatSuite **out_suite) {
  ConfitDiagnostic diagnostic;
  char path[512];

  if (!join_fixture(path, sizeof(path), fixture)) {
    return 0;
  }

  confit_diagnostic_init(&diagnostic);
  *out_suite = 0;
  return confit_compat_load_directory(path, out_suite, &diagnostic) ==
         CONFIT_OK;
}

static int expect_compat_error(const ConfitCompatSuite *suite,
                               const ConfitCompatProject *projects,
                               size_t project_count, const char *message) {
  ConfitDiagnostic diagnostic;
  ConfitStatus status;

  confit_diagnostic_init(&diagnostic);
  status = confit_compat_check(suite, projects, project_count, &diagnostic);
  return status == CONFIT_ERR_COMPATIBILITY &&
         confit_diagnostic_has_error(&diagnostic) &&
         diagnostic.message != 0 && strcmp(diagnostic.message, message) == 0;
}

int main(void) {
  ConfitProject *parus;
  ConfitProject *delos;
  ConfitResolvedConfig *parus_config;
  ConfitResolvedConfig *delos_config;
  ConfitCompatProject projects[2];
  ConfitCompatSuite *suite;
  ConfitDiagnostic diagnostic;

  if (!load_resolved_pair(&parus, &parus_config, &delos, &delos_config)) {
    return 1;
  }
  projects[0].project = parus;
  projects[0].config = parus_config;
  projects[1].project = delos;
  projects[1].config = delos_config;

  suite = 0;
  if (!load_suite("tests/fixtures/compat/rules/pass", &suite)) {
    confit_resolved_config_free(parus_config);
    confit_resolved_config_free(delos_config);
    confit_project_free(parus);
    confit_project_free(delos);
    return 2;
  }
  confit_diagnostic_init(&diagnostic);
  if (suite->assertion_count != 2U ||
      confit_compat_check(suite, projects, 2U, &diagnostic) != CONFIT_OK) {
    confit_compat_suite_free(suite);
    confit_resolved_config_free(parus_config);
    confit_resolved_config_free(delos_config);
    confit_project_free(parus);
    confit_project_free(delos);
    return 3;
  }
  confit_compat_suite_free(suite);

  if (!load_suite("tests/fixtures/compat/rules/fail-requires", &suite)) {
    confit_resolved_config_free(parus_config);
    confit_resolved_config_free(delos_config);
    confit_project_free(parus);
    confit_project_free(delos);
    return 4;
  }
  if (!expect_compat_error(
          suite, projects, 2U,
          "Parus Delos RT Executor requires Delos DSH RX in this negative fixture.")) {
    confit_compat_suite_free(suite);
    confit_resolved_config_free(parus_config);
    confit_resolved_config_free(delos_config);
    confit_project_free(parus);
    confit_project_free(delos);
    return 5;
  }
  confit_compat_suite_free(suite);

  if (!load_suite("tests/fixtures/compat/rules/fail-forbids", &suite)) {
    confit_resolved_config_free(parus_config);
    confit_resolved_config_free(delos_config);
    confit_project_free(parus);
    confit_project_free(delos);
    return 6;
  }
  if (!expect_compat_error(
          suite, projects, 2U,
          "This negative fixture forbids the required Delos DCG path.")) {
    confit_compat_suite_free(suite);
    confit_resolved_config_free(parus_config);
    confit_resolved_config_free(delos_config);
    confit_project_free(parus);
    confit_project_free(delos);
    return 7;
  }
  confit_compat_suite_free(suite);

  confit_resolved_config_free(parus_config);
  confit_resolved_config_free(delos_config);
  confit_project_free(parus);
  confit_project_free(delos);
  return 0;
}
