#if !defined(_WIN32)
#define _XOPEN_SOURCE 700
#endif

#include <stdio.h>
#include <string.h>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "confit/diagnostic.h"
#include "confit/host.h"
#include "confit/link_v2.h"
#include "confit/schema_v2.h"
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

static int expect_load_status(const char *fixture, const char *message) {
  ConfitV2Project *project;
  ConfitDiagnostic diagnostic;
  char path[512];

  if (!join_fixture(path, sizeof(path), fixture)) {
    return 0;
  }
  confit_diagnostic_init(&diagnostic);
  project = 0;
  if (confit_v2_schema_load_project(path, &project, &diagnostic) !=
          CONFIT_ERR_SCHEMA ||
      project != 0 || diagnostic.message == 0 ||
      strcmp(diagnostic.message, message) != 0) {
    confit_v2_project_free(project);
    return 0;
  }
  return 1;
}

static int expect_link_status(const char *fixture, const char *message) {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitDiagnostic diagnostic;
  char path[512];

  if (!join_fixture(path, sizeof(path), fixture)) {
    return 0;
  }
  confit_diagnostic_init(&diagnostic);
  project = 0;
  linked = 0;
  if (confit_v2_schema_load_project(path, &project, &diagnostic) != CONFIT_OK ||
      confit_v2_schema_link_project(project, &linked, &diagnostic) !=
          CONFIT_ERR_SCHEMA ||
      linked != 0 || diagnostic.message == 0 ||
      strcmp(diagnostic.message, message) != 0) {
    confit_v2_linked_project_free(linked);
    confit_v2_project_free(project);
    return 0;
  }
  confit_v2_project_free(project);
  return 1;
}

static int expect_write(ConfitV2LinkedProject *linked, const char *option_id,
                        ConfitV2AssignmentWriter writer, ConfitStatus expected,
                        const char *message) {
  ConfitV2WriteRequest request;
  ConfitDiagnostic diagnostic;

  memset(&request, 0, sizeof(request));
  request.option_id = option_id;
  request.writer = writer;
  request.span.path = "write-request";
  request.span.line = 9U;
  request.span.column = 4U;
  confit_diagnostic_init(&diagnostic);
  if (confit_v2_linked_project_validate_write(linked, &request, &diagnostic) !=
      expected) {
    return 0;
  }
  if (expected != CONFIT_OK &&
      (diagnostic.message == 0 || strcmp(diagnostic.message, message) != 0 ||
       diagnostic.line != 9U || diagnostic.column != 4U)) {
    return 0;
  }
  return 1;
}

static int expect_valid_link(void) {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  const ConfitV2LinkedExpression *expression;
  ConfitDiagnostic diagnostic;
  char path[512];
  size_t index;
  size_t references = 0U;

  if (!join_fixture(path, sizeof(path), "tests/fixtures/schema-v2/valid")) {
    return 0;
  }
  confit_diagnostic_init(&diagnostic);
  project = 0;
  linked = 0;
  if (confit_v2_schema_load_project(path, &project, &diagnostic) != CONFIT_OK ||
      confit_v2_schema_link_project(project, &linked, &diagnostic) != CONFIT_OK ||
      confit_v2_linked_project_source(linked) != project ||
      confit_v2_linked_project_symbol_count(linked) != project->symbol_count ||
      confit_v2_linked_project_find_symbol(linked, "confit.computed.total") == 0 ||
      confit_v2_linked_project_expression_count(linked) != 11U) {
    confit_v2_linked_project_free(linked);
    confit_v2_project_free(project);
    return 0;
  }
  for (index = 0U; index < confit_v2_linked_project_expression_count(linked);
       ++index) {
    expression = confit_v2_linked_project_expression_at(linked, index);
    if (expression == 0 || expression->expression == 0 || expression->typed == 0) {
      confit_v2_linked_project_free(linked);
      confit_v2_project_free(project);
      return 0;
    }
    references += expression->reference_count;
  }
  if (references < 4U ||
      !expect_write(linked, "confit.bool.enabled",
                    CONFIT_V2_ASSIGNMENT_WRITER_PROFILE, CONFIT_OK, 0) ||
      !expect_write(linked, "confit.bool.enabled",
                    CONFIT_V2_ASSIGNMENT_WRITER_TARGET, CONFIT_ERR_SCHEMA,
                    "schema v2 write domain violation") ||
      !expect_write(linked, "confit.bool.enabled",
                    CONFIT_V2_ASSIGNMENT_WRITER_USER, CONFIT_OK, 0) ||
      !expect_write(linked, "confit.tristate.mode",
                    CONFIT_V2_ASSIGNMENT_WRITER_USER, CONFIT_ERR_SCHEMA,
                    "schema v2 write domain violation") ||
      !expect_write(linked, "confit.computed.total",
                    CONFIT_V2_ASSIGNMENT_WRITER_PROFILE, CONFIT_ERR_SCHEMA,
                    "computed schema v2 option rejects external assignment") ||
      !expect_write(linked, "confit.computed.total",
                    CONFIT_V2_ASSIGNMENT_WRITER_COMPUTED, CONFIT_OK, 0) ||
      !expect_write(linked, "confit.unknown.option",
                    CONFIT_V2_ASSIGNMENT_WRITER_PROFILE, CONFIT_ERR_SCHEMA,
                    "write request references unknown schema v2 option")) {
    confit_v2_linked_project_free(linked);
    confit_v2_project_free(project);
    return 0;
  }
  confit_v2_linked_project_free(linked);
  confit_v2_project_free(project);
  return 1;
}

static int expect_nested_import(void) {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitDiagnostic diagnostic;
  char path[512];

  if (!join_fixture(path, sizeof(path),
                    "tests/fixtures/schema-v2-link/import-nested")) {
    return 0;
  }
  confit_diagnostic_init(&diagnostic);
  project = 0;
  linked = 0;
  if (confit_v2_schema_load_project(path, &project, &diagnostic) != CONFIT_OK ||
      confit_v2_schema_link_project(project, &linked, &diagnostic) != CONFIT_OK ||
      project->import_count != 2U ||
      confit_v2_linked_project_source(linked) != project ||
      confit_v2_linked_project_find_symbol(linked, "confit.nested.flag") == 0) {
    confit_v2_linked_project_free(linked);
    confit_v2_project_free(project);
    return 0;
  }
  confit_v2_linked_project_free(linked);
  confit_v2_project_free(project);
  return 1;
}

#if !defined(_WIN32)
static int write_fixture_file(const char *path, const char *text) {
  FILE *file = fopen(path, "wb");

  if (file == 0) {
    return 0;
  }
  if (fputs(text, file) == EOF || fclose(file) != 0) {
    return 0;
  }
  return 1;
}

static int expect_symlink_escape(void) {
  char root[256];
  char config[256];
  char options[256];
  char project[256];
  char outside[256];
  char escaped[256];
  ConfitV2Project *loaded;
  ConfitDiagnostic diagnostic;
  int result;

  if (snprintf(root, sizeof(root), "/tmp/confit-v2-link-%ld",
               (long)getpid()) < 0 ||
      snprintf(config, sizeof(config), "%s/config", root) < 0 ||
      snprintf(options, sizeof(options), "%s/options", config) < 0 ||
      snprintf(project, sizeof(project), "%s/project.toml", config) < 0 ||
      snprintf(outside, sizeof(outside), "%s/outside.toml", root) < 0 ||
      snprintf(escaped, sizeof(escaped), "%s/escape.toml", options) < 0 ||
      mkdir(root, 0700) != 0 || mkdir(config, 0700) != 0 ||
      mkdir(options, 0700) != 0 ||
      !write_fixture_file(project,
                          "[project]\nname = \"escape\"\nnamespace = \"confit\"\nschema_version = 2\nimports = [\"options/escape.toml\"]\n") ||
      !write_fixture_file(outside, "schema_version = 2\n") ||
      symlink("../../outside.toml", escaped) != 0) {
    (void)unlink(escaped);
    (void)unlink(project);
    (void)unlink(outside);
    (void)rmdir(options);
    (void)rmdir(config);
    (void)rmdir(root);
    return 0;
  }
  confit_diagnostic_init(&diagnostic);
  loaded = 0;
  result = confit_v2_schema_load_project(root, &loaded, &diagnostic) ==
               CONFIT_ERR_SCHEMA &&
           loaded == 0 && diagnostic.message != 0 &&
           strcmp(diagnostic.message,
                  "schema v2 import escapes project config root") == 0;
  confit_v2_project_free(loaded);
  (void)unlink(escaped);
  (void)unlink(project);
  (void)unlink(outside);
  (void)rmdir(options);
  (void)rmdir(config);
  (void)rmdir(root);
  return result;
}
#else
static int expect_symlink_escape(void) { return 1; }
#endif

int main(void) {
  if (!expect_valid_link()) {
    return 2;
  }
  if (!expect_nested_import()) {
    return 3;
  }
  if (!expect_symlink_escape()) {
    return 4;
  }
  if (!expect_load_status("tests/fixtures/schema-v2-link/import-traversal",
                          "invalid schema v2 import path")) {
    return 5;
  }
  if (!expect_load_status("tests/fixtures/schema-v2-link/import-duplicate",
                          "duplicate schema v2 canonical import")) {
    return 6;
  }
  if (!expect_load_status("tests/fixtures/schema-v2-link/import-cycle",
                          "schema v2 import cycle")) {
    return 7;
  }
  if (!expect_load_status("tests/fixtures/schema-v2-link/import-unsupported-version",
                          "import source does not declare schema_version = 2")) {
    return 8;
  }
  if (!expect_link_status("tests/fixtures/schema-v2-link/unknown-reference",
                          "unknown expression reference")) {
    return 9;
  }
  if (!expect_link_status("tests/fixtures/schema-v2-link/self-reference",
                          "self reference in schema v2 expression")) {
    return 10;
  }
  return 0;
}
