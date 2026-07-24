#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "confit/constraint_v2.h"
#include "confit/diagnostic.h"
#include "confit/host.h"
#include "confit/link_v2.h"
#include "confit/resolver_v2.h"
#include "confit/schema_v2.h"
#include "confit/status.h"

#include "test_fs.h"

#ifndef CONFIT_TEST_SOURCE_DIR
#define CONFIT_TEST_SOURCE_DIR "."
#endif

#define CONFIT_TEST_LARGE_COUNT 10000U

static int join_fixture(char *out, size_t out_size, const char *fixture) {
  ConfitDiagnostic diagnostic;

  confit_diagnostic_init(&diagnostic);
  return confit_host_path_join(out, out_size, CONFIT_TEST_SOURCE_DIR, fixture,
                               &diagnostic) == CONFIT_OK;
}

static ConfitStatus load_compiled_path(const char *path,
                                       ConfitV2Project **out_project,
                                       ConfitV2LinkedProject **out_linked,
                                       ConfitV2CompiledStructure **out_compiled,
                                       ConfitDiagnostic *diagnostic) {
  ConfitStatus status;

  *out_project = 0;
  *out_linked = 0;
  *out_compiled = 0;
  status = confit_v2_schema_load_project(path, out_project, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_schema_link_project(*out_project, out_linked, diagnostic);
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

static ConfitStatus load_compiled(const char *fixture, ConfitV2Project **out_project,
                                  ConfitV2LinkedProject **out_linked,
                                  ConfitV2CompiledStructure **out_compiled,
                                  ConfitDiagnostic *diagnostic) {
  char path[1024];

  if (!join_fixture(path, sizeof(path), fixture)) {
    return CONFIT_ERR_INTERNAL;
  }
  return load_compiled_path(path, out_project, out_linked, out_compiled,
                            diagnostic);
}

static void free_compiled(ConfitV2Project *project, ConfitV2LinkedProject *linked,
                          ConfitV2CompiledStructure *compiled) {
  confit_v2_compiled_structure_free(compiled);
  confit_v2_linked_project_free(linked);
  confit_v2_project_free(project);
}

static int expect_value(const ConfitV2Evaluation *evaluation, const char *id,
                        ConfitV2EffectiveValueOrigin origin, uint64_t value) {
  const ConfitV2EffectiveValue *effective =
      confit_v2_evaluation_find(evaluation, id);

  return effective != 0 && effective->is_set && effective->origin == origin &&
         effective->value.kind == CONFIT_V2_VALUE_UINT &&
         effective->value.as.uint_value == value;
}

static int expect_valid_evaluation(void) {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitV2CompiledStructure *compiled;
  ConfitV2AssignmentLedger *ledger = 0;
  ConfitV2Evaluation *evaluation = 0;
  ConfitV2Evaluation *repeat = 0;
  ConfitV2LedgerOptions options;
  ConfitDiagnostic diagnostic;
  uint64_t ledger_before;
  uint64_t ledger_after;
  uint64_t hash;
  uint64_t repeated_hash;
  int result;

  confit_diagnostic_init(&diagnostic);
  if (load_compiled("tests/fixtures/schema-v2-evaluation/valid", &project,
                    &linked, &compiled, &diagnostic) != CONFIT_OK) {
    return 0;
  }
  memset(&options, 0, sizeof(options));
  options.profile_name = "debug";
  result = confit_v2_assignment_ledger_build(compiled, &options, &ledger,
                                              &diagnostic) == CONFIT_OK &&
           confit_v2_assignment_ledger_hash(ledger, &ledger_before) == CONFIT_OK &&
           confit_v2_evaluation_build(ledger, &evaluation, &diagnostic) ==
               CONFIT_OK &&
           confit_v2_evaluation_build(ledger, &repeat, &diagnostic) == CONFIT_OK &&
           confit_v2_assignment_ledger_hash(ledger, &ledger_after) == CONFIT_OK &&
           ledger_before == ledger_after &&
           confit_v2_evaluation_source(evaluation) == ledger &&
           confit_v2_evaluation_value_count(evaluation) == 4U &&
           confit_v2_evaluation_hash(evaluation, &hash) == CONFIT_OK &&
           confit_v2_evaluation_hash(repeat, &repeated_hash) == CONFIT_OK &&
           hash == repeated_hash;
  result = result && expect_value(evaluation, "eval.base",
                                  CONFIT_V2_EFFECTIVE_VALUE_DEFAULT, 2U) &&
           expect_value(evaluation, "eval.conditional",
                        CONFIT_V2_EFFECTIVE_VALUE_CONDITIONAL_DEFAULT, 9U) &&
           expect_value(evaluation, "eval.requested",
                        CONFIT_V2_EFFECTIVE_VALUE_REQUESTED, 4U) &&
           expect_value(evaluation, "eval.total",
                        CONFIT_V2_EFFECTIVE_VALUE_COMPUTED, 11U);
  confit_v2_evaluation_free(repeat);
  confit_v2_evaluation_free(evaluation);
  confit_v2_assignment_ledger_free(ledger);
  free_compiled(project, linked, compiled);
  return result;
}

static int expect_availability_visibility_and_choice(void) {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitV2CompiledStructure *compiled;
  ConfitV2AssignmentLedger *ledger = 0;
  ConfitV2Evaluation *evaluation = 0;
  const ConfitV2EffectiveValue *hidden;
  const ConfitV2EffectiveValue *disabled;
  const ConfitV2EffectiveValue *optional;
  const ConfitV2EffectiveValue *first;
  const ConfitV2EffectiveValue *second;
  const ConfitV2ChoiceResolution *backend;
  const ConfitV2ChoiceResolution *unavailable;
  ConfitDiagnostic diagnostic;
  int result;

  confit_diagnostic_init(&diagnostic);
  if (load_compiled("tests/fixtures/schema-v2-availability/valid", &project,
                    &linked, &compiled, &diagnostic) != CONFIT_OK) {
    return 0;
  }
  result = confit_v2_assignment_ledger_build(compiled, 0, &ledger,
                                              &diagnostic) == CONFIT_OK &&
           confit_v2_evaluation_build(ledger, &evaluation, &diagnostic) ==
               CONFIT_OK;
  hidden = confit_v2_evaluation_find(evaluation, "availability.hidden");
  disabled = confit_v2_evaluation_find(evaluation, "availability.disabled");
  optional = confit_v2_evaluation_find(evaluation, "availability.optional");
  first = confit_v2_evaluation_find(evaluation, "availability.backend.a");
  second = confit_v2_evaluation_find(evaluation, "availability.backend.b");
  backend = confit_v2_evaluation_find_choice(evaluation, "availability.backend");
  unavailable = confit_v2_evaluation_find_choice(
      evaluation, "availability.optional_choice");
  result = result && hidden != 0 && hidden->is_set && hidden->available &&
           !hidden->visible && hidden->value.kind == CONFIT_V2_VALUE_BOOL &&
           hidden->value.as.bool_value && disabled != 0 &&
           disabled->is_set && !disabled->available && disabled->visible &&
           disabled->value.kind == CONFIT_V2_VALUE_BOOL &&
           !disabled->value.as.bool_value && optional != 0 && !optional->is_set &&
           !optional->available && optional->visible && first != 0 && second != 0 &&
           !first->value.as.bool_value && !second->value.as.bool_value &&
           confit_v2_evaluation_choice_count(evaluation) == 2U &&
           backend != 0 && backend->available && !backend->visible &&
           backend->effective_member_count == 0U &&
           backend->origin == CONFIT_V2_CHOICE_SELECTION_DEFAULT &&
           backend->selected_member != 0 &&
           strcmp(backend->selected_member->id, "availability.backend.a") == 0 &&
           unavailable != 0 && !unavailable->available && unavailable->visible &&
           unavailable->effective_member_count == 0U &&
           unavailable->selected_member == 0 &&
           unavailable->origin == CONFIT_V2_CHOICE_SELECTION_NONE;
  confit_v2_evaluation_free(evaluation);
  confit_v2_assignment_ledger_free(ledger);
  free_compiled(project, linked, compiled);
  return result;
}

static int expect_evaluation_error(const char *fixture, const char *message) {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitV2CompiledStructure *compiled;
  ConfitV2AssignmentLedger *ledger = 0;
  ConfitV2Evaluation *evaluation = 0;
  ConfitDiagnostic diagnostic;
  int result;

  confit_diagnostic_init(&diagnostic);
  if (load_compiled(fixture, &project, &linked, &compiled, &diagnostic) !=
      CONFIT_OK) {
    return 0;
  }
  result = confit_v2_assignment_ledger_build(compiled, 0, &ledger,
                                              &diagnostic) == CONFIT_OK &&
           confit_v2_evaluation_build(ledger, &evaluation, &diagnostic) ==
               CONFIT_ERR_SCHEMA &&
           evaluation == 0 && diagnostic.message != 0 &&
           strstr(diagnostic.message, message) != 0;
  confit_v2_evaluation_free(evaluation);
  confit_v2_assignment_ledger_free(ledger);
  free_compiled(project, linked, compiled);
  return result;
}

static int expect_unavailable_request_error(void) {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitV2CompiledStructure *compiled;
  ConfitV2AssignmentLedger *ledger = 0;
  ConfitV2Evaluation *evaluation = 0;
  ConfitV2LedgerOptions options;
  const ConfitV2LedgerEntry *requested;
  ConfitDiagnostic diagnostic;
  uint64_t before;
  uint64_t after;
  int result;

  confit_diagnostic_init(&diagnostic);
  if (load_compiled("tests/fixtures/schema-v2-availability/unavailable-request",
                    &project, &linked, &compiled, &diagnostic) != CONFIT_OK) {
    return 0;
  }
  memset(&options, 0, sizeof(options));
  options.profile_name = "enable";
  result = confit_v2_assignment_ledger_build(compiled, &options, &ledger,
                                              &diagnostic) == CONFIT_OK &&
           confit_v2_assignment_ledger_hash(ledger, &before) == CONFIT_OK &&
           confit_v2_evaluation_build(ledger, &evaluation, &diagnostic) ==
               CONFIT_ERR_SCHEMA &&
           evaluation == 0 && diagnostic.message != 0 &&
           strstr(diagnostic.message, "requested schema v2 value is unavailable") !=
               0 &&
           confit_v2_assignment_ledger_hash(ledger, &after) == CONFIT_OK &&
           before == after;
  requested = confit_v2_assignment_ledger_requested(ledger,
                                                     "availability.feature");
  result = result && requested != 0 && !requested->is_unset &&
           requested->value.kind == CONFIT_V2_VALUE_BOOL &&
           requested->value.as.bool_value;
  confit_v2_evaluation_free(evaluation);
  confit_v2_assignment_ledger_free(ledger);
  free_compiled(project, linked, compiled);
  return result;
}

static int expect_cycle_diagnostic(void) {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitV2CompiledStructure *compiled;
  ConfitV2AssignmentLedger *ledger = 0;
  ConfitV2Evaluation *evaluation = 0;
  ConfitDiagnostic diagnostic;
  int result;

  confit_diagnostic_init(&diagnostic);
  if (load_compiled("tests/fixtures/schema-v2-evaluation/cycle", &project,
                    &linked, &compiled, &diagnostic) != CONFIT_OK) {
    return 0;
  }
  result = confit_v2_assignment_ledger_build(compiled, 0, &ledger,
                                              &diagnostic) == CONFIT_OK &&
           confit_v2_evaluation_build(ledger, &evaluation, &diagnostic) ==
               CONFIT_ERR_SCHEMA &&
           evaluation == 0 && diagnostic.message != 0 && diagnostic.line > 0U &&
           strstr(diagnostic.message, "eval.first") != 0 &&
           strstr(diagnostic.message, "eval.second") != 0 &&
           strstr(diagnostic.message, "options.toml:") != 0;
  confit_v2_evaluation_free(evaluation);
  confit_v2_assignment_ledger_free(ledger);
  free_compiled(project, linked, compiled);
  return result;
}

static int write_large_project(const char *root, int reverse) {
  char config[4096];
  char project[4096];
  char options[4096];
  FILE *file;
  size_t step;

  if (!confit_test_fs_path_join(config, sizeof(config), root, "config") ||
      !confit_test_fs_path_join(project, sizeof(project), config, "project.toml") ||
      !confit_test_fs_path_join(options, sizeof(options), config, "options.toml") ||
      !confit_test_fs_make_dirs(config) ||
      !confit_test_fs_write_file(
          project,
          "[project]\n"
          "name = \"evaluation-large\"\n"
          "namespace = \"eval\"\n"
          "version = \"0.2.0\"\n"
          "schema_version = 2\n"
          "imports = [\"options.toml\"]\n")) {
    return 0;
  }
  file = fopen(options, "wb");
  if (file == 0) {
    return 0;
  }
  if (fputs("schema_version = 2\n\n", file) == EOF) {
    fclose(file);
    return 0;
  }
  for (step = 0U; step < CONFIT_TEST_LARGE_COUNT; ++step) {
    const size_t index = reverse ? CONFIT_TEST_LARGE_COUNT - 1U - step : step;
    int written;

    if (index == 0U || index >= 100U) {
      written = fprintf(
          file,
          "[option.\"eval.n%05zu\"]\n"
          "type = \"uint\"\n"
          "default = 1\n"
          "write_domain = \"profile\"\n"
          "owner = \"confit\"\n"
          "since = \"0.2.0\"\n"
          "stability = \"stable\"\n\n",
          index);
    } else {
      written = fprintf(
          file,
          "[option.\"eval.n%05zu\"]\n"
          "type = \"uint\"\n"
          "write_domain = \"computed\"\n"
          "computed = \"eval.n%05zu + 0x1\"\n"
          "owner = \"confit\"\n"
          "since = \"0.2.0\"\n"
          "stability = \"stable\"\n\n",
          index, index - 1U);
    }
    if (written < 0) {
      fclose(file);
      return 0;
    }
  }
  return fclose(file) == 0;
}

static int evaluate_large_project(const char *root, uint64_t *out_hash) {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitV2CompiledStructure *compiled;
  ConfitV2AssignmentLedger *ledger = 0;
  ConfitV2Evaluation *evaluation = 0;
  const ConfitV2EffectiveValue *last;
  ConfitDiagnostic diagnostic;
  int result;

  confit_diagnostic_init(&diagnostic);
  result = load_compiled_path(root, &project, &linked, &compiled, &diagnostic) ==
               CONFIT_OK &&
           confit_v2_assignment_ledger_build(compiled, 0, &ledger, &diagnostic) ==
               CONFIT_OK &&
           confit_v2_evaluation_build(ledger, &evaluation, &diagnostic) ==
               CONFIT_OK &&
           confit_v2_evaluation_value_count(evaluation) == CONFIT_TEST_LARGE_COUNT &&
           confit_v2_evaluation_hash(evaluation, out_hash) == CONFIT_OK;
  last = confit_v2_evaluation_find(evaluation, "eval.n00099");
  result = result && last != 0 && last->is_set &&
           last->origin == CONFIT_V2_EFFECTIVE_VALUE_COMPUTED &&
           last->value.kind == CONFIT_V2_VALUE_UINT &&
           last->value.as.uint_value == 100U;
  confit_v2_evaluation_free(evaluation);
  confit_v2_assignment_ledger_free(ledger);
  free_compiled(project, linked, compiled);
  return result;
}

static int expect_large_evaluation(void) {
  char first_root[4096] = {0};
  uint64_t first_hash = 0U;
  int result;

  if (!confit_test_fs_make_temp_dir(first_root, sizeof(first_root), "confit-v2")) {
    confit_test_fs_remove_tree(first_root);
    return 0;
  }
  result = write_large_project(first_root, 1) &&
           evaluate_large_project(first_root, &first_hash) && first_hash != 0U;
  result = confit_test_fs_remove_tree(first_root) && result;
  return result;
}

static int evaluate_fixture_hash(const char *fixture, const char *profile,
                                 uint64_t *out_hash) {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitV2CompiledStructure *compiled;
  ConfitV2AssignmentLedger *ledger = 0;
  ConfitV2Evaluation *evaluation = 0;
  ConfitV2LedgerOptions options;
  ConfitDiagnostic diagnostic;
  int result;

  confit_diagnostic_init(&diagnostic);
  if (load_compiled(fixture, &project, &linked, &compiled, &diagnostic) !=
      CONFIT_OK) {
    return 0;
  }
  memset(&options, 0, sizeof(options));
  options.profile_name = profile;
  result = confit_v2_assignment_ledger_build(compiled, &options, &ledger,
                                              &diagnostic) == CONFIT_OK &&
           confit_v2_evaluation_build(ledger, &evaluation, &diagnostic) ==
               CONFIT_OK &&
           confit_v2_evaluation_hash(evaluation, out_hash) == CONFIT_OK;
  confit_v2_evaluation_free(evaluation);
  confit_v2_assignment_ledger_free(ledger);
  free_compiled(project, linked, compiled);
  return result;
}

static int expect_declaration_order_hash(void) {
  uint64_t normal_hash = 0U;
  uint64_t reordered_hash = 0U;

  return evaluate_fixture_hash("tests/fixtures/schema-v2-evaluation/valid",
                               "debug", &normal_hash) &&
         evaluate_fixture_hash(
             "tests/fixtures/schema-v2-evaluation/valid-reordered", "debug",
             &reordered_hash) &&
         normal_hash == reordered_hash;
}

int main(void) {
  if (!expect_valid_evaluation()) {
    return 2;
  }
  if (!expect_availability_visibility_and_choice()) {
    return 3;
  }
  if (!expect_cycle_diagnostic()) {
    return 4;
  }
  if (!expect_evaluation_error(
          "tests/fixtures/schema-v2-evaluation/ambiguous-default",
          "ambiguous schema v2 conditional default")) {
    return 5;
  }
  if (!expect_evaluation_error("tests/fixtures/schema-v2-evaluation/required",
                               "required schema v2 option is unset")) {
    return 6;
  }
  if (!expect_evaluation_error(
          "tests/fixtures/schema-v2-evaluation/conditional-range",
          "invalid schema v2 input value")) {
    return 7;
  }
  if (!expect_unavailable_request_error()) {
    return 8;
  }
  if (!expect_evaluation_error("tests/fixtures/schema-v2-choice/multiple",
                               "schema v2 choice has too many selected members")) {
    return 9;
  }
  if (!expect_evaluation_error(
          "tests/fixtures/schema-v2-choice/required",
          "schema v2 choice requires an explicit member selection")) {
    return 10;
  }
  if (!expect_evaluation_error(
          "tests/fixtures/schema-v2-choice/unavailable-default",
          "schema v2 choice default member is unavailable")) {
    return 11;
  }
  if (!expect_evaluation_error(
          "tests/fixtures/schema-v2-choice/ambiguous-default",
          "ambiguous schema v2 choice default")) {
    return 12;
  }
  if (!expect_declaration_order_hash()) {
    return 13;
  }
  if (!expect_large_evaluation()) {
    return 14;
  }
  return 0;
}
