#include <stdint.h>
#include <string.h>

#include "confit/constraint_v2.h"
#include "confit/diagnostic.h"
#include "confit/host.h"
#include "confit/link_v2.h"
#include "confit/resolver_v2.h"
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

static ConfitStatus load_compiled(const char *fixture, ConfitV2Project **out_project,
                                  ConfitV2LinkedProject **out_linked,
                                  ConfitV2CompiledStructure **out_compiled,
                                  ConfitDiagnostic *diagnostic) {
  char path[1024];
  ConfitStatus status;

  *out_project = 0;
  *out_linked = 0;
  *out_compiled = 0;
  if (!join_fixture(path, sizeof(path), fixture)) {
    return CONFIT_ERR_INTERNAL;
  }
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

static void free_compiled(ConfitV2Project *project, ConfitV2LinkedProject *linked,
                          ConfitV2CompiledStructure *compiled) {
  confit_v2_compiled_structure_free(compiled);
  confit_v2_linked_project_free(linked);
  confit_v2_project_free(project);
}

static int expect_positive_ledger(void) {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitV2CompiledStructure *compiled;
  ConfitV2AssignmentLedger *ledger = 0;
  ConfitV2AssignmentLedger *repeat = 0;
  ConfitV2AssignmentLedger *explicit_target = 0;
  ConfitV2UserOverride overrides[2];
  ConfitV2LedgerOptions options;
  ConfitV2LedgerOptions target_options;
  const ConfitV2LedgerEntry *entry;
  const ConfitV2TargetSelection *selection;
  ConfitDiagnostic diagnostic;
  uint64_t hash;
  uint64_t repeated_hash;
  int result;

  confit_diagnostic_init(&diagnostic);
  if (load_compiled("tests/fixtures/schema-v2-ledger/valid", &project, &linked,
                    &compiled, &diagnostic) != CONFIT_OK) {
    return 0;
  }
  memset(overrides, 0, sizeof(overrides));
  overrides[0].option_id = "ledger.profile.value";
  overrides[0].value_text = "4";
  overrides[0].span.path = "test --set";
  overrides[0].span.line = 1U;
  overrides[1].option_id = "ledger.target.board";
  overrides[1].value_text = "user";
  overrides[1].span.path = "test --set";
  overrides[1].span.line = 2U;
  memset(&options, 0, sizeof(options));
  options.profile_name = "debug";
  options.user_overrides = overrides;
  options.user_override_count = sizeof(overrides) / sizeof(overrides[0]);
  result = confit_v2_assignment_ledger_build(compiled, &options, &ledger,
                                              &diagnostic) == CONFIT_OK &&
           confit_v2_assignment_ledger_build(compiled, &options, &repeat,
                                              &diagnostic) == CONFIT_OK &&
           confit_v2_assignment_ledger_entry_count(ledger) == 12U &&
           strcmp(confit_v2_assignment_ledger_profile_name(ledger), "debug") == 0 &&
           strcmp(confit_v2_assignment_ledger_target_name(ledger), "sim") == 0 &&
           confit_v2_assignment_ledger_hash(ledger, &hash) == CONFIT_OK &&
           confit_v2_assignment_ledger_hash(repeat, &repeated_hash) == CONFIT_OK &&
           hash == repeated_hash;
  entry = confit_v2_assignment_ledger_requested(ledger, "ledger.profile.value");
  result = result && entry != 0 && entry->wins &&
           entry->origin == CONFIT_V2_ASSIGNMENT_ORIGIN_USER &&
           entry->value.kind == CONFIT_V2_VALUE_UINT &&
           entry->value.as.uint_value == 4U &&
           strcmp(entry->source_path, "test --set") == 0;
  entry = confit_v2_assignment_ledger_requested(ledger, "ledger.target.board");
  result = result && entry != 0 &&
           entry->origin == CONFIT_V2_ASSIGNMENT_ORIGIN_USER &&
           strcmp(entry->value.as.string_value, "user") == 0;
  entry = confit_v2_assignment_ledger_requested(ledger, "ledger.profile.optional");
  result = result && entry != 0 && entry->is_unset &&
           entry->origin == CONFIT_V2_ASSIGNMENT_ORIGIN_UNSET;
  entry = confit_v2_assignment_ledger_requested(ledger, "ledger.schema.constant");
  result = result && entry != 0 &&
           entry->origin == CONFIT_V2_ASSIGNMENT_ORIGIN_SCHEMA_DEFAULT &&
           entry->value.as.bool_value;
  selection = confit_v2_assignment_ledger_target_selection(ledger);
  result = result && selection != 0 &&
           selection->origin == CONFIT_V2_TARGET_SELECTION_PROFILE &&
           selection->source_path != 0 &&
           strstr(selection->source_path, "profiles/debug.toml") != 0 &&
           selection->source_line > 0U;
  memset(&target_options, 0, sizeof(target_options));
  target_options.profile_name = "debug";
  target_options.target_name = "common";
  result = result &&
           confit_v2_assignment_ledger_build(compiled, &target_options,
                                              &explicit_target, &diagnostic) ==
               CONFIT_OK &&
           strcmp(confit_v2_assignment_ledger_target_name(explicit_target),
                  "common") == 0;
  selection = confit_v2_assignment_ledger_target_selection(explicit_target);
  result = result && selection != 0 &&
           selection->origin == CONFIT_V2_TARGET_SELECTION_EXPLICIT &&
           strcmp(selection->source_path, "cli --target") == 0;
  entry = confit_v2_assignment_ledger_requested(explicit_target,
                                                 "ledger.target.board");
  result = result && entry != 0 &&
           entry->origin == CONFIT_V2_ASSIGNMENT_ORIGIN_TARGET &&
           strcmp(entry->value.as.string_value, "host") == 0;
  confit_v2_assignment_ledger_free(explicit_target);
  confit_v2_assignment_ledger_free(repeat);
  confit_v2_assignment_ledger_free(ledger);
  free_compiled(project, linked, compiled);
  return result;
}

static int expect_ledger_error(const char *fixture, const char *profile,
                               const char *message) {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitV2CompiledStructure *compiled;
  ConfitV2AssignmentLedger *ledger = 0;
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
                                              &diagnostic) == CONFIT_ERR_SCHEMA &&
           ledger == 0 && diagnostic.message != 0 &&
           strcmp(diagnostic.message, message) == 0;
  confit_v2_assignment_ledger_free(ledger);
  free_compiled(project, linked, compiled);
  return result;
}

static int expect_user_override_errors(void) {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitV2CompiledStructure *compiled;
  ConfitV2AssignmentLedger *ledger = 0;
  ConfitV2UserOverride duplicates[2];
  ConfitV2UserOverride invalid;
  ConfitV2LedgerOptions options;
  ConfitDiagnostic diagnostic;
  int result;

  confit_diagnostic_init(&diagnostic);
  if (load_compiled("tests/fixtures/schema-v2-ledger/valid", &project, &linked,
                    &compiled, &diagnostic) != CONFIT_OK) {
    return 0;
  }
  memset(duplicates, 0, sizeof(duplicates));
  duplicates[0].option_id = "ledger.profile.value";
  duplicates[0].value_text = "4";
  duplicates[1].option_id = "ledger.profile.value";
  duplicates[1].value_text = "5";
  memset(&options, 0, sizeof(options));
  options.user_overrides = duplicates;
  options.user_override_count = sizeof(duplicates) / sizeof(duplicates[0]);
  result = confit_v2_assignment_ledger_build(compiled, &options, &ledger,
                                              &diagnostic) == CONFIT_ERR_SCHEMA &&
           ledger == 0 && diagnostic.message != 0 &&
           strcmp(diagnostic.message, "duplicate schema v2 user override") == 0;
  confit_v2_assignment_ledger_free(ledger);
  ledger = 0;
  confit_diagnostic_init(&diagnostic);
  memset(&invalid, 0, sizeof(invalid));
  invalid.option_id = "ledger.profile.value";
  invalid.value_text = "not-a-number";
  options.user_overrides = &invalid;
  options.user_override_count = 1U;
  result = result &&
           confit_v2_assignment_ledger_build(compiled, &options, &ledger,
                                              &diagnostic) != CONFIT_OK &&
           ledger == 0;
  confit_v2_assignment_ledger_free(ledger);
  free_compiled(project, linked, compiled);
  return result;
}

static int expect_profile_transaction_override(void) {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitV2CompiledStructure *compiled;
  ConfitV2AssignmentLedger *ledger = 0;
  ConfitV2ProfileOverride override;
  ConfitV2LedgerOptions options;
  const ConfitV2LedgerEntry *entry;
  ConfitDiagnostic diagnostic;
  int result;

  confit_diagnostic_init(&diagnostic);
  if (load_compiled("tests/fixtures/schema-v2-ledger/valid", &project, &linked,
                    &compiled, &diagnostic) != CONFIT_OK) {
    return 0;
  }
  memset(&override, 0, sizeof(override));
  override.option_id = "ledger.profile.value";
  override.value_text = "7";
  override.span.path = "test profile transaction";
  override.span.line = 1U;
  memset(&options, 0, sizeof(options));
  options.profile_name = "debug";
  options.profile_overrides = &override;
  options.profile_override_count = 1U;
  result = confit_v2_assignment_ledger_build(compiled, &options, &ledger,
                                              &diagnostic) == CONFIT_OK;
  entry = confit_v2_assignment_ledger_requested(ledger, "ledger.profile.value");
  result = result && entry != 0 && entry->wins &&
           entry->origin == CONFIT_V2_ASSIGNMENT_ORIGIN_PROFILE &&
           entry->value.kind == CONFIT_V2_VALUE_UINT &&
           entry->value.as.uint_value == 7U &&
           strcmp(entry->source_path, "test profile transaction") == 0;
  confit_v2_assignment_ledger_free(ledger);
  free_compiled(project, linked, compiled);
  return result;
}

int main(void) {
  if (!expect_positive_ledger()) {
    return 2;
  }
  if (!expect_ledger_error("tests/fixtures/schema-v2-ledger/invalid-write-domain",
                           "bad", "schema v2 write domain violation")) {
    return 3;
  }
  if (!expect_ledger_error("tests/fixtures/schema-v2-ledger/profile-cycle", "a",
                           "schema v2 input inheritance cycle")) {
    return 4;
  }
  if (!expect_ledger_error("tests/fixtures/schema-v2-ledger/duplicate-assignment",
                           "duplicate",
                           "schema v2 input has duplicate option assignment")) {
    return 5;
  }
  if (!expect_user_override_errors()) {
    return 6;
  }
  if (!expect_profile_transaction_override()) {
    return 7;
  }
  return 0;
}
