#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "confit/diagnostic.h"
#include "confit/host.h"
#include "confit/schema_v2.h"
#include "confit/status.h"

#include "test_fs.h"

#ifndef CONFIT_TEST_SOURCE_DIR
#define CONFIT_TEST_SOURCE_DIR "."
#endif

typedef struct TrackingAllocator {
  size_t allocation_calls;
  size_t fail_after;
  size_t outstanding;
} TrackingAllocator;

static void *tracking_allocate(void *context, size_t size) {
  TrackingAllocator *tracking = (TrackingAllocator *)context;
  void *allocation;

  if (tracking->allocation_calls++ >= tracking->fail_after) {
    return 0;
  }
  allocation = malloc(size);
  if (allocation != 0) {
    tracking->outstanding += 1U;
  }
  return allocation;
}

static void *tracking_reallocate(void *context, void *allocation, size_t size) {
  TrackingAllocator *tracking = (TrackingAllocator *)context;
  void *grown;

  if (tracking->allocation_calls++ >= tracking->fail_after) {
    return 0;
  }
  grown = realloc(allocation, size);
  if (grown != 0 && allocation == 0) {
    tracking->outstanding += 1U;
  }
  return grown;
}

static void tracking_deallocate(void *context, void *allocation) {
  TrackingAllocator *tracking = (TrackingAllocator *)context;

  if (allocation != 0) {
    free(allocation);
    tracking->outstanding -= 1U;
  }
}

static int join_fixture(char *out, size_t out_size, const char *fixture) {
  ConfitDiagnostic diagnostic;

  confit_diagnostic_init(&diagnostic);
  return confit_host_path_join(out, out_size, CONFIT_TEST_SOURCE_DIR, fixture,
                               &diagnostic) == CONFIT_OK;
}

static const ConfitV2Symbol *find_symbol(const ConfitV2Project *project,
                                         const char *id) {
  size_t index;

  for (index = 0U; index < project->symbol_count; ++index) {
    if (strcmp(project->symbols[index].id, id) == 0) {
      return &project->symbols[index];
    }
  }
  return 0;
}

static int expect_status(const char *fixture, ConfitStatus expected,
                         const char *message) {
  ConfitDiagnostic diagnostic;
  ConfitV2Project *project;
  char path[512];

  if (!join_fixture(path, sizeof(path), fixture)) {
    return 0;
  }
  confit_diagnostic_init(&diagnostic);
  project = 0;
  if (confit_v2_schema_load_project(path, &project, &diagnostic) != expected ||
      project != 0 || diagnostic.message == 0 ||
      strcmp(diagnostic.message, message) != 0) {
    confit_v2_project_free(project);
    return 0;
  }
  return 1;
}

static int expect_valid_model(const char *path) {
  ConfitDiagnostic diagnostic;
  ConfitV2Project *project;
  const ConfitV2Symbol *symbol;

  confit_diagnostic_init(&diagnostic);
  project = 0;
  if (confit_v2_schema_load_project(path, &project, &diagnostic) != CONFIT_OK ||
      project == 0 || strcmp(project->name, "confit-v2-fixture") != 0 ||
      strcmp(project->namespace_name, "confit") != 0 ||
      project->import_count != 3U || project->symbol_count != 16U ||
      project->menu_count != 2U || project->choice_count != 1U ||
      project->constraint_count != 1U || project->profile_dirs.count != 1U ||
      strcmp(project->profile_dirs.items[0], "profiles") != 0) {
    confit_v2_project_free(project);
    return 0;
  }
  symbol = find_symbol(project, "confit.enum_set.features");
  if (symbol == 0 || symbol->type != CONFIT_V2_OPTION_TYPE_ENUM_SET ||
      !symbol->default_value.is_set ||
      symbol->default_value.value.kind != CONFIT_V2_VALUE_STRING_LIST ||
      symbol->default_value.value.as.string_list.count != 2U ||
      strcmp(symbol->default_value.value.as.string_list.items[0], "alpha") != 0 ||
      strcmp(symbol->default_value.value.as.string_list.items[1], "gamma") != 0) {
    confit_v2_project_free(project);
    return 0;
  }
  symbol = find_symbol(project, "confit.uint.capacity");
  if (symbol == 0 || !symbol->range.is_set ||
      symbol->range.min_value.as.uint_value != 1U ||
      symbol->range.max_value.as.uint_value != 128U ||
      symbol->span.path == 0 ||
      (strstr(symbol->span.path, "options/types.toml") == 0 &&
       strstr(symbol->span.path, "options\\types.toml") == 0) ||
      symbol->span.line == 0U || symbol->span.column == 0U) {
    confit_v2_project_free(project);
    return 0;
  }
  symbol = find_symbol(project, "confit.default.tick_hz");
  if (symbol == 0 || !symbol->required || symbol->default_value.is_set ||
      symbol->default_count != 1U ||
      symbol->defaults[0].assignment.value.as.uint_value != 1000U ||
      symbol->defaults[0].priority != 100) {
    confit_v2_project_free(project);
    return 0;
  }
  symbol = find_symbol(project, "confit.computed.total");
  if (symbol == 0 || symbol->write_domain != CONFIT_V2_WRITE_DOMAIN_COMPUTED ||
      symbol->computed.text == 0 || symbol->default_value.is_set ||
      symbol->available_if.text != 0) {
    confit_v2_project_free(project);
    return 0;
  }
  if (confit_v2_type_descriptor(CONFIT_V2_OPTION_TYPE_PATH_LIST) == 0 ||
      !confit_v2_type_descriptor(CONFIT_V2_OPTION_TYPE_ENUM_SET)->requires_values) {
    confit_v2_project_free(project);
    return 0;
  }
  confit_v2_project_free(project);
  return 1;
}

static int expect_allocation_failure_cleanup(const char *path) {
  size_t fail_after;
  int saw_success = 0;

  for (fail_after = 0U; fail_after < 4096U; ++fail_after) {
    ConfitDiagnostic diagnostic;
    TrackingAllocator tracking;
    ConfitV2Allocator allocator;
    ConfitV2Project *project;
    ConfitStatus status;

    memset(&tracking, 0, sizeof(tracking));
    tracking.fail_after = fail_after;
    allocator.context = &tracking;
    allocator.allocate = tracking_allocate;
    allocator.reallocate = tracking_reallocate;
    allocator.deallocate = tracking_deallocate;
    confit_diagnostic_init(&diagnostic);
    project = 0;
    status = confit_v2_schema_load_project_with_allocator(path, &allocator,
                                                          &project, &diagnostic);
    if (status == CONFIT_OK) {
      saw_success = 1;
      confit_v2_project_free(project);
      if (tracking.outstanding != 0U) {
        return 0;
      }
      break;
    }
    if (status != CONFIT_ERR_INTERNAL || project != 0 ||
        tracking.outstanding != 0U) {
      return 0;
    }
  }
  return saw_success;
}

static int expect_import_depth_limit(void) {
  char root[4096] = {0};
  char config[4096];
  char imports[4096];
  char project_path[4096];
  ConfitV2Project *project = 0;
  ConfitDiagnostic diagnostic;
  size_t index;
  int result;

  if (!confit_test_fs_make_temp_dir(root, sizeof(root), "confit-v2-import") ||
      !confit_test_fs_path_join(config, sizeof(config), root, "config") ||
      !confit_test_fs_path_join(imports, sizeof(imports), config, "imports") ||
      !confit_test_fs_path_join(project_path, sizeof(project_path), config,
                                "project.toml") ||
      !confit_test_fs_make_dirs(imports) ||
      !confit_test_fs_write_file(
          project_path,
          "[project]\n"
          "name = \"import-depth\"\n"
          "namespace = \"depth\"\n"
          "version = \"0.2.0\"\n"
          "schema_version = 2\n"
          "imports = [\"imports/000.toml\"]\n")) {
    (void)confit_test_fs_remove_tree(root);
    return 0;
  }
  for (index = 0U; index <= 128U; ++index) {
    char name[32];
    char path[4096];
    char text[128];

    (void)snprintf(name, sizeof(name), "%03zu.toml", index);
    if (!confit_test_fs_path_join(path, sizeof(path), imports, name)) {
      (void)confit_test_fs_remove_tree(root);
      return 0;
    }
    if (index < 128U) {
      (void)snprintf(text, sizeof(text),
                     "schema_version = 2\nimports = [\"imports/%03zu.toml\"]\n",
                     index + 1U);
    } else {
      (void)snprintf(text, sizeof(text), "schema_version = 2\n");
    }
    if (!confit_test_fs_write_file(path, text)) {
      (void)confit_test_fs_remove_tree(root);
      return 0;
    }
  }
  confit_diagnostic_init(&diagnostic);
  result = confit_v2_schema_load_project(root, &project, &diagnostic) ==
               CONFIT_ERR_SCHEMA &&
           project == 0 && diagnostic.message != 0 &&
           strcmp(diagnostic.message,
                  "schema v2 import depth exceeds the supported limit") == 0;
  confit_v2_project_free(project);
  return confit_test_fs_remove_tree(root) && result;
}

int main(void) {
  char path[512];

  if (!join_fixture(path, sizeof(path), "tests/fixtures/schema-v2/valid")) {
    return 2;
  }
  if (!expect_valid_model(path)) {
    return 3;
  }
  if (!expect_allocation_failure_cleanup(path)) {
    return 4;
  }
  if (!expect_import_depth_limit()) {
    return 5;
  }
  if (!expect_status("tests/fixtures/schema-v2/invalid-unknown-field",
                     CONFIT_ERR_SCHEMA, "unknown schema v2 option field")) {
    return 6;
  }
  if (!expect_status("tests/fixtures/schema-v2/invalid-duplicate-option",
                     CONFIT_ERR_SCHEMA, "duplicate schema v2 semantic definition")) {
    return 7;
  }
  if (!expect_status("tests/fixtures/schema-v2/invalid-invalid-value",
                     CONFIT_ERR_SCHEMA, "schema v2 field has an invalid value")) {
    return 8;
  }
  if (!expect_status("tests/fixtures/schema-v2/invalid-missing-required",
                     CONFIT_ERR_SCHEMA, "schema v2 field has an invalid value")) {
    return 9;
  }
  if (!expect_status("tests/fixtures/schema-v2/invalid-v1-field",
                     CONFIT_ERR_SCHEMA, "unknown schema v2 option field")) {
    return 10;
  }
  if (!expect_status("tests/fixtures/schema-v2/invalid-namespace",
                     CONFIT_ERR_SCHEMA, "option id is outside project namespace")) {
    return 11;
  }
  return 0;
}
