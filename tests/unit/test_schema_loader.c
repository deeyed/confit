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
  if (project->option_count != 7U) {
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
      option->default_value.as.uint_value != 16U ||
      !option->has_range || option->range_min.as.uint_value != 1U ||
      option->range_max.as.uint_value != 128U) {
    confit_project_free(project);
    return 6;
  }

  option = confit_project_find_option(project, "delos.target.board");
  if (option == 0 || option->type != CONFIT_OPTION_TYPE_ENUM ||
      option->default_value.kind != CONFIT_VALUE_ENUM ||
      strcmp(option->default_value.as.string_value, "host-sim") != 0 ||
      option->enum_value_count != 2U) {
    confit_project_free(project);
    return 7;
  }

  option = confit_project_find_option(project, "delos.memory.flash_base");
  if (option == 0 || option->type != CONFIT_OPTION_TYPE_HEX ||
      option->default_value.kind != CONFIT_VALUE_UINT ||
      option->default_value.as.uint_value != 0x08000000U ||
      option->range_max.as.uint_value != 0x080FFFFFU) {
    confit_project_free(project);
    return 8;
  }

  option = confit_project_find_option(project, "delos.sim.default_gain");
  if (option == 0 || option->type != CONFIT_OPTION_TYPE_FLOAT ||
      option->default_value.kind != CONFIT_VALUE_FLOAT ||
      option->default_value.as.float_value < 0.124 ||
      option->default_value.as.float_value > 0.126 ||
      option->range_max.as.float_value < 0.99) {
    confit_project_free(project);
    return 9;
  }

  option = confit_project_find_option(project, "delos.paths.config_root");
  if (option == 0 || option->type != CONFIT_OPTION_TYPE_PATH ||
      option->default_value.kind != CONFIT_VALUE_PATH ||
      strcmp(option->default_value.as.string_value,
             "build/generated/config") != 0) {
    confit_project_free(project);
    return 10;
  }
  confit_project_free(project);

  if (!expect_schema_error("tests/fixtures/schema/invalid/duplicate",
                           "duplicate option id")) {
    return 11;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/unknown-field",
                           "unknown option field")) {
    return 12;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/missing-type",
                           "missing option type")) {
    return 13;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/invalid-id",
                           "invalid option id")) {
    return 14;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/bad-project-version",
                           "unsupported schema_version")) {
    return 15;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/out-of-range",
                           "default outside range")) {
    return 16;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/enum-candidate",
                           "enum default is not a candidate")) {
    return 17;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/nonfinite-float",
                           "float default must be finite")) {
    return 18;
  }
  if (!expect_schema_error("tests/fixtures/schema/invalid/bad-range-type",
                           "range is only valid for numeric options")) {
    return 19;
  }

  return 0;
}
