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

static int build_evaluation(const char *profile, ConfitV2Project **out_project,
                            ConfitV2LinkedProject **out_linked,
                            ConfitV2CompiledStructure **out_compiled,
                            ConfitV2AssignmentLedger **out_ledger,
                            ConfitV2Evaluation **out_evaluation,
                            ConfitDiagnostic *diagnostic) {
  ConfitV2LedgerOptions options;
  ConfitStatus status;

  *out_ledger = 0;
  *out_evaluation = 0;
  if (load_compiled("tests/fixtures/schema-v2-constraint-runtime", out_project,
                    out_linked, out_compiled, diagnostic) != CONFIT_OK) {
    return 0;
  }
  memset(&options, 0, sizeof(options));
  options.profile_name = profile;
  status = confit_v2_assignment_ledger_build(*out_compiled, &options, out_ledger,
                                              diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_evaluation_build(*out_ledger, out_evaluation, diagnostic);
  }
  return status == CONFIT_OK;
}

static int related_spans_are_complete(const ConfitV2ConstraintResult *result) {
  size_t index;

  if (result == 0 || result->related_count == 0U || result->related == 0) {
    return 0;
  }
  for (index = 0U; index < result->related_count; ++index) {
    if (result->related[index].path == 0 || result->related[index].line == 0U ||
        result->related[index].note == 0) {
      return 0;
    }
  }
  return 1;
}

static int expect_failure_report(void) {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitV2CompiledStructure *compiled;
  ConfitV2AssignmentLedger *ledger;
  ConfitV2Evaluation *evaluation;
  ConfitV2ConstraintReport *report = 0;
  const ConfitV2ConstraintResult *first;
  const ConfitV2ConstraintResult *second;
  const ConfitV2ConstraintResult *third;
  const ConfitV2SuggestionResult *suggestion;
  const ConfitV2EffectiveValue *driver;
  ConfitDiagnostic diagnostic;
  int result;

  confit_diagnostic_init(&diagnostic);
  if (!build_evaluation(0, &project, &linked, &compiled, &ledger, &evaluation,
                        &diagnostic)) {
    return 0;
  }
  result = confit_v2_evaluation_validate_constraints(evaluation, &report,
                                                       &diagnostic) ==
               CONFIT_ERR_SCHEMA &&
           report != 0 &&
           confit_v2_constraint_report_source(report) == compiled &&
           confit_v2_constraint_report_result_count(report) == 3U &&
           confit_v2_constraint_report_failure_count(report) == 2U &&
           diagnostic.severity == CONFIT_DIAGNOSTIC_SEVERITY_ERROR &&
           diagnostic.code != 0 &&
           strcmp(diagnostic.code, "CONSTRAINT_REQUIRE_FAILED") == 0 &&
           diagnostic.message != 0 &&
           strcmp(diagnostic.message, "driver must be enabled") == 0 &&
           diagnostic.related != 0 && diagnostic.related_count >= 3U &&
           diagnostic.fix_candidates == 0 && diagnostic.fix_candidate_count == 0U;
  first = confit_v2_constraint_report_result_at(report, 0U);
  second = confit_v2_constraint_report_result_at(report, 1U);
  third = confit_v2_constraint_report_result_at(report, 2U);
  result = result && first != 0 && second != 0 && third != 0 &&
           strcmp(first->constraint->source->id, "constraint.alpha") == 0 &&
           first->outcome == CONFIT_V2_CONSTRAINT_FAILED &&
           first->read_count == 1U && first->reads[0].symbol != 0 &&
           strcmp(first->reads[0].symbol->id, "constraint.driver") == 0 &&
           first->reads[0].is_set && first->reads[0].value != 0 &&
           first->reads[0].value->kind == CONFIT_V2_VALUE_BOOL &&
           !first->reads[0].value->as.bool_value &&
           first->reads[0].source_path != 0 &&
           first->reads[0].source_line > 0U &&
           first->reads[0].expression_path != 0 &&
           first->reads[0].expression_line > 0U &&
           first->related_count >= 3U && related_spans_are_complete(first) &&
           strcmp(second->constraint->source->id, "constraint.beta") == 0 &&
           second->outcome == CONFIT_V2_CONSTRAINT_FAILED &&
           second->related_count >= 3U && related_spans_are_complete(second) &&
           strcmp(third->constraint->source->id, "constraint.gamma") == 0 &&
           third->outcome == CONFIT_V2_CONSTRAINT_NOT_APPLICABLE &&
           third->read_count == 0U && related_spans_are_complete(third);
  driver = confit_v2_evaluation_find(evaluation, "constraint.driver");
  result = result && driver != 0 && driver->is_set &&
           driver->value.kind == CONFIT_V2_VALUE_BOOL &&
           !driver->value.as.bool_value &&
           confit_v2_constraint_report_suggestion_count(report) == 4U;
  suggestion = confit_v2_constraint_report_suggestion_at(report, 0U);
  result = result && suggestion != 0 && suggestion->symbol != 0 &&
           strcmp(suggestion->symbol->id, "constraint.driver") == 0 &&
           suggestion->state == CONFIT_V2_SUGGESTION_CONFLICTING;
  suggestion = confit_v2_constraint_report_suggestion_at(report, 1U);
  result = result && suggestion != 0 && suggestion->symbol != 0 &&
           strcmp(suggestion->symbol->id, "constraint.driver") == 0 &&
           suggestion->state == CONFIT_V2_SUGGESTION_CONFLICTING;
  suggestion = confit_v2_constraint_report_suggestion_at(report, 2U);
  result = result && suggestion != 0 && suggestion->symbol != 0 &&
           strcmp(suggestion->symbol->id, "constraint.gate") == 0 &&
           suggestion->state == CONFIT_V2_SUGGESTION_SATISFIED;
  suggestion = confit_v2_constraint_report_suggestion_at(report, 3U);
  result = result && suggestion != 0 && suggestion->symbol != 0 &&
           strcmp(suggestion->symbol->id, "constraint.other") == 0 &&
           suggestion->state == CONFIT_V2_SUGGESTION_NOT_APPLICABLE;
  confit_v2_constraint_report_free(report);
  confit_v2_evaluation_free(evaluation);
  confit_v2_assignment_ledger_free(ledger);
  free_compiled(project, linked, compiled);
  return result;
}

static int expect_pass_report(void) {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitV2CompiledStructure *compiled;
  ConfitV2AssignmentLedger *ledger;
  ConfitV2Evaluation *evaluation;
  ConfitV2ConstraintReport *report = 0;
  const ConfitV2ConstraintResult *first;
  const ConfitV2ConstraintResult *second;
  const ConfitV2ConstraintResult *third;
  ConfitDiagnostic diagnostic;
  int result;

  confit_diagnostic_init(&diagnostic);
  if (!build_evaluation("pass", &project, &linked, &compiled, &ledger,
                        &evaluation, &diagnostic)) {
    return 0;
  }
  result = confit_v2_evaluation_validate_constraints(evaluation, &report,
                                                       &diagnostic) == CONFIT_OK &&
           report != 0 &&
           confit_v2_constraint_report_failure_count(report) == 0U;
  first = confit_v2_constraint_report_result_at(report, 0U);
  second = confit_v2_constraint_report_result_at(report, 1U);
  third = confit_v2_constraint_report_result_at(report, 2U);
  result = result && first != 0 && second != 0 && third != 0 &&
           first->outcome == CONFIT_V2_CONSTRAINT_PASSED &&
           second->outcome == CONFIT_V2_CONSTRAINT_PASSED &&
           third->outcome == CONFIT_V2_CONSTRAINT_NOT_APPLICABLE;
  confit_v2_constraint_report_free(report);
  confit_v2_evaluation_free(evaluation);
  confit_v2_assignment_ledger_free(ledger);
  free_compiled(project, linked, compiled);
  return result;
}

int main(void) {
  if (!expect_failure_report()) {
    return 2;
  }
  if (!expect_pass_report()) {
    return 3;
  }
  return 0;
}
