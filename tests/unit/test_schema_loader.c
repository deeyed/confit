#include <string.h>

#include "confit/diagnostic.h"
#include "confit/host.h"
#include "confit/model.h"
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

static int expect_schema_error(const char *fixture, const char *message) {
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  char path[512];

  if (!join_fixture(path, sizeof(path), fixture)) {
    return 0;
  }

  confit_diagnostic_init(&diagnostic);
  project = 0;
  if (confit_schema_load_project(path, &project, &diagnostic) !=
      CONFIT_ERR_SCHEMA) {
    confit_project_free(project);
    return 0;
  }
  if (!confit_diagnostic_has_error(&diagnostic) ||
      diagnostic.message == 0 ||
      strcmp(diagnostic.message, message) != 0) {
    return 0;
  }

  return 1;
}

int main(void) {
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitOption *option;
  char path[512];

  if (!join_fixture(path, sizeof(path),
                    "tests/fixtures/schema/valid/basic")) {
    return 1;
  }

  confit_diagnostic_init(&diagnostic);
  project = 0;
  if (confit_schema_load_project(path, &project, &diagnostic) != CONFIT_OK) {
    return 2;
  }
  if (strcmp(project->name, "delos") != 0 ||
      strcmp(project->version, "0.1.0") != 0 ||
      project->schema_version != 1U) {
    confit_project_free(project);
    return 3;
  }
  if (project->option_count != 3U) {
    confit_project_free(project);
    return 4;
  }

  option = confit_project_find_option(project, "delos.debug.ddc");
  if (option == 0 || option->type != CONFIT_OPTION_TYPE_BOOL ||
      option->default_value.kind != CONFIT_VALUE_BOOL ||
      option->default_value.as.bool_value != 0 ||
      strcmp(option->prompt, "Enable DDC") != 0 ||
      option->tag_count != 2U) {
    confit_project_free(project);
    return 5;
  }

  option = confit_project_find_option(project, "delos.scheduler.task_slots");
  if (option == 0 || option->type != CONFIT_OPTION_TYPE_UINT ||
      option->default_value.kind != CONFIT_VALUE_UINT ||
      option->default_value.as.uint_value != 16U) {
    confit_project_free(project);
    return 6;
  }

  option = confit_project_find_option(project, "delos.target.board");
  if (option == 0 || option->type != CONFIT_OPTION_TYPE_ENUM ||
      option->default_value.kind != CONFIT_VALUE_ENUM ||
      strcmp(option->default_value.as.string_value, "host-sim") != 0) {
    confit_project_free(project);
    return 7;
  }
  confit_project_free(project);

  if (!expect_schema_error("tests/fixtures/schema/invalid/duplicate",
                           "duplicate option id")) {
    return 8;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/unknown-field",
                           "unknown option field")) {
    return 9;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/missing-type",
                           "missing option type")) {
    return 10;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/invalid-id",
                           "invalid option id")) {
    return 11;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/bad-project-version",
                           "unsupported schema_version")) {
    return 12;
  }

  return 0;
}
