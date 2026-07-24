#include "runtime_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char kInvalidArgument[] =
    "invalid schema v2 constraint validation argument";
static const char kAllocationFailed[] =
    "failed to allocate schema v2 constraint validation data";
static const char kMissingBinding[] =
    "final effective context is missing a schema v2 option binding";
static const char kConditionUnset[] =
    "schema v2 constraint condition is unset";
static const char kRequirementUnset[] =
    "schema v2 constraint requirement is unset";
static const char kConstraintFailureCode[] = "CONSTRAINT_REQUIRE_FAILED";
static const char kWhenNote[] = "constraint applicability expression";
static const char kRequireNote[] = "constraint requirement expression";

static const ConfitV2LinkedExpression *confit_v2_constraint_find_expression(
    const ConfitV2LinkedProject *linked, ConfitV2LinkedExpressionRole role,
    const char *owner_id, size_t occurrence) {
  size_t index;

  for (index = 0U; index < confit_v2_linked_project_expression_count(linked);
       ++index) {
    const ConfitV2LinkedExpression *expression =
        confit_v2_linked_project_expression_at(linked, index);
    if (expression->role == role && strcmp(expression->owner_id, owner_id) == 0) {
      if (occurrence == 0U) {
        return expression;
      }
      occurrence -= 1U;
    }
  }
  return 0;
}

static int confit_v2_constraint_compare_result(const void *left,
                                                const void *right) {
  const ConfitV2ConstraintResult *left_result =
      (const ConfitV2ConstraintResult *)left;
  const ConfitV2ConstraintResult *right_result =
      (const ConfitV2ConstraintResult *)right;
  const ConfitV2SourceSpan *left_span = &left_result->constraint->source->span;
  const ConfitV2SourceSpan *right_span = &right_result->constraint->source->span;
  const char *left_path = left_span->path != 0 ? left_span->path : "";
  const char *right_path = right_span->path != 0 ? right_span->path : "";
  int compared = strcmp(left_path, right_path);

  if (compared != 0) {
    return compared;
  }
  if (left_span->line != right_span->line) {
    return left_span->line < right_span->line ? -1 : 1;
  }
  if (left_span->column != right_span->column) {
    return left_span->column < right_span->column ? -1 : 1;
  }
  return strcmp(left_result->constraint->source->id,
                right_result->constraint->source->id);
}

static int confit_v2_constraint_value_equal(const ConfitV2Value *left,
                                             const ConfitV2Value *right) {
  size_t index;

  if (left->kind != right->kind) {
    return 0;
  }
  switch (left->kind) {
  case CONFIT_V2_VALUE_BOOL:
    return left->as.bool_value == right->as.bool_value;
  case CONFIT_V2_VALUE_TRISTATE:
    return left->as.tristate_value == right->as.tristate_value;
  case CONFIT_V2_VALUE_INT:
    return left->as.int_value == right->as.int_value;
  case CONFIT_V2_VALUE_UINT:
    return left->as.uint_value == right->as.uint_value;
  case CONFIT_V2_VALUE_FLOAT:
    return left->as.float_value == right->as.float_value;
  case CONFIT_V2_VALUE_STRING:
    return left->as.string_value != 0 && right->as.string_value != 0 &&
           strcmp(left->as.string_value, right->as.string_value) == 0;
  case CONFIT_V2_VALUE_STRING_LIST:
    if (left->as.string_list.count != right->as.string_list.count) {
      return 0;
    }
    for (index = 0U; index < left->as.string_list.count; ++index) {
      if (strcmp(left->as.string_list.items[index],
                 right->as.string_list.items[index]) != 0) {
        return 0;
      }
    }
    return 1;
  case CONFIT_V2_VALUE_UNSET:
  default:
    return 1;
  }
}

static ConfitStatus confit_v2_constraint_build_environment(
    const ConfitV2CompiledStructure *compiled,
    const ConfitV2ConstraintBinding *bindings, size_t binding_count,
    ConfitV2ExpressionBinding **out_bindings, size_t *out_count,
    ConfitDiagnostic *diagnostic) {
  const ConfitV2LinkedProject *linked =
      confit_v2_compiled_structure_source(compiled);
  const ConfitV2Project *project = confit_v2_linked_project_source(linked);
  ConfitV2ExpressionBinding *environment_bindings;
  size_t index;

  *out_bindings = 0;
  *out_count = 0U;
  if (binding_count != project->symbol_count ||
      (binding_count > 0U && bindings == 0)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          kInvalidArgument);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  environment_bindings = (ConfitV2ExpressionBinding *)calloc(
      project->symbol_count, sizeof(*environment_bindings));
  if (project->symbol_count > 0U && environment_bindings == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0U, 0U,
                          kAllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < project->symbol_count; ++index) {
    const ConfitV2Symbol *symbol = &project->symbols[index];
    const ConfitV2ConstraintBinding *binding;
    size_t matches;

    binding = confit_v2_constraint_find_binding(bindings, binding_count,
                                                symbol->id, &matches);
    if (binding == 0 || matches != 1U || binding->symbol != symbol ||
        (binding->is_set && binding->value == 0)) {
      free(environment_bindings);
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                            symbol->span.path, symbol->span.line,
                            symbol->span.column, kMissingBinding);
      return CONFIT_ERR_INVALID_ARGUMENT;
    }
    environment_bindings[index].id = symbol->id;
    environment_bindings[index].type =
        confit_v2_expression_type_from_option_type(symbol->type, symbol->id);
    environment_bindings[index].value = binding->is_set ? binding->value : 0;
  }
  *out_bindings = environment_bindings;
  *out_count = project->symbol_count;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_constraint_boolean_expression(
    const ConfitV2LinkedExpression *expression,
    const ConfitV2ExpressionEnvironment *environment, const char *unset_message,
    int *out_value, ConfitDiagnostic *diagnostic) {
  ConfitV2ExpressionValue value;
  ConfitStatus status;

  memset(&value, 0, sizeof(value));
  status = confit_v2_expression_evaluate(expression->typed, environment, &value,
                                          diagnostic);
  if (status != CONFIT_OK) {
    confit_v2_expression_value_clear(&value);
    return status;
  }
  if (!value.is_set) {
    confit_v2_expression_value_clear(&value);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                          expression->expression->source_span.path,
                          expression->expression->source_span.line,
                          expression->expression->source_span.column,
                          unset_message);
    return CONFIT_ERR_SCHEMA;
  }
  *out_value = value.value.as.bool_value != 0;
  confit_v2_expression_value_clear(&value);
  return CONFIT_OK;
}

static ConfitStatus confit_v2_constraint_evaluate_one(
    const ConfitV2CompiledConstraint *constraint,
    const ConfitV2ExpressionEnvironment *environment,
    const ConfitV2ConstraintBinding *bindings, size_t binding_count,
    ConfitV2ConstraintResult *out_result, ConfitDiagnostic *diagnostic) {
  int applicable;
  int required;
  ConfitStatus status;

  out_result->constraint = constraint;
  status = confit_v2_constraint_boolean_expression(
      constraint->when, environment, kConditionUnset, &applicable, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_v2_constraint_collect_expression_reads(
      out_result, constraint->when, bindings, binding_count, kWhenNote,
      diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  if (!applicable) {
    out_result->outcome = CONFIT_V2_CONSTRAINT_NOT_APPLICABLE;
    return CONFIT_OK;
  }
  status = confit_v2_constraint_boolean_expression(
      constraint->require, environment, kRequirementUnset, &required, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_v2_constraint_collect_expression_reads(
      out_result, constraint->require, bindings, binding_count, kRequireNote,
      diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  out_result->outcome = required ? CONFIT_V2_CONSTRAINT_PASSED
                                 : CONFIT_V2_CONSTRAINT_FAILED;
  return CONFIT_OK;
}

static int confit_v2_constraint_symbol_compare(const void *left,
                                                const void *right) {
  const ConfitV2Symbol *const *left_symbol = (const ConfitV2Symbol *const *)left;
  const ConfitV2Symbol *const *right_symbol =
      (const ConfitV2Symbol *const *)right;

  return strcmp((*left_symbol)->id, (*right_symbol)->id);
}

static ConfitStatus confit_v2_constraint_suggestions(
    const ConfitV2CompiledStructure *compiled,
    const ConfitV2ExpressionEnvironment *environment,
    const ConfitV2ConstraintBinding *bindings, size_t binding_count,
    ConfitV2ConstraintReport *report, ConfitDiagnostic *diagnostic) {
  const ConfitV2LinkedProject *linked =
      confit_v2_compiled_structure_source(compiled);
  const ConfitV2Project *project = confit_v2_linked_project_source(linked);
  const ConfitV2Symbol **symbols;
  size_t result_index = 0U;
  size_t index;

  for (index = 0U; index < project->symbol_count; ++index) {
    report->suggestion_count += project->symbols[index].suggestion_count;
  }
  if (report->suggestion_count == 0U) {
    return CONFIT_OK;
  }
  report->suggestions = (ConfitV2SuggestionResult *)calloc(
      report->suggestion_count, sizeof(*report->suggestions));
  symbols = (const ConfitV2Symbol **)calloc(project->symbol_count,
                                              sizeof(*symbols));
  if (report->suggestions == 0 || symbols == 0) {
    free(symbols);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0U, 0U,
                          kAllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < project->symbol_count; ++index) {
    symbols[index] = &project->symbols[index];
  }
  if (project->symbol_count > 1U) {
    qsort(symbols, project->symbol_count, sizeof(*symbols),
          confit_v2_constraint_symbol_compare);
  }
  for (index = 0U; index < project->symbol_count; ++index) {
    const ConfitV2Symbol *symbol = symbols[index];
    size_t suggestion_index;

    for (suggestion_index = 0U; suggestion_index < symbol->suggestion_count;
         ++suggestion_index) {
      const ConfitV2LinkedExpression *expression =
          confit_v2_constraint_find_expression(
              linked, CONFIT_V2_LINKED_EXPRESSION_SUGGESTION_WHEN, symbol->id,
              suggestion_index);
      const ConfitV2ConstraintBinding *binding;
      ConfitV2SuggestionResult *result = &report->suggestions[result_index];
      int applicable;
      size_t matches;
      ConfitStatus status;

      if (expression == 0) {
        free(symbols);
        confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, symbol->span.path,
                              symbol->span.line, symbol->span.column,
                              kAllocationFailed);
        return CONFIT_ERR_INTERNAL;
      }
      status = confit_v2_constraint_boolean_expression(
          expression, environment, "schema v2 suggestion condition is unset",
          &applicable, diagnostic);
      if (status != CONFIT_OK) {
        free(symbols);
        return status;
      }
      result->symbol = symbol;
      result->suggestion = &symbol->suggestions[suggestion_index];
      result->state = CONFIT_V2_SUGGESTION_NOT_APPLICABLE;
      if (applicable) {
        binding = confit_v2_constraint_find_binding(bindings, binding_count,
                                                    symbol->id, &matches);
        if (binding == 0 || matches != 1U) {
          free(symbols);
          confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                                symbol->span.path, symbol->span.line,
                                symbol->span.column, kMissingBinding);
          return CONFIT_ERR_INVALID_ARGUMENT;
        }
        result->state = binding->is_set &&
                                confit_v2_constraint_value_equal(
                                    binding->value,
                                    &result->suggestion->assignment.value)
                            ? CONFIT_V2_SUGGESTION_SATISFIED
                            : CONFIT_V2_SUGGESTION_APPLICABLE;
      }
      result_index += 1U;
    }
  }
  free(symbols);
  for (index = 0U; index < report->suggestion_count; ++index) {
    size_t other;
    ConfitV2SuggestionResult *result = &report->suggestions[index];

    if (result->state == CONFIT_V2_SUGGESTION_NOT_APPLICABLE) {
      continue;
    }
    for (other = 0U; other < report->suggestion_count; ++other) {
      const ConfitV2SuggestionResult *candidate = &report->suggestions[other];
      if (index != other && candidate->state != CONFIT_V2_SUGGESTION_NOT_APPLICABLE &&
          candidate->symbol == result->symbol &&
          !confit_v2_constraint_value_equal(
              &candidate->suggestion->assignment.value,
              &result->suggestion->assignment.value)) {
        result->state = CONFIT_V2_SUGGESTION_CONFLICTING;
        break;
      }
    }
  }
  return CONFIT_OK;
}

ConfitStatus confit_v2_constraint_validate(
    const ConfitV2CompiledStructure *compiled,
    const ConfitV2ConstraintBinding *bindings, size_t binding_count,
    ConfitV2ConstraintReport **out_report, ConfitDiagnostic *diagnostic) {
  const ConfitV2LinkedProject *linked;
  ConfitV2ConstraintReport *report;
  ConfitV2ExpressionBinding *environment_bindings = 0;
  ConfitV2ExpressionEnvironment environment;
  size_t environment_count = 0U;
  size_t index;
  ConfitStatus status;

  if (compiled == 0 || out_report == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          kInvalidArgument);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_report = 0;
  linked = confit_v2_compiled_structure_source(compiled);
  if (linked == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          kInvalidArgument);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  status = confit_v2_constraint_build_environment(
      compiled, bindings, binding_count, &environment_bindings,
      &environment_count, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  report = (ConfitV2ConstraintReport *)calloc(1U, sizeof(*report));
  if (report == 0) {
    free(environment_bindings);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0U, 0U,
                          kAllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  report->compiled = compiled;
  report->result_count = confit_v2_compiled_structure_constraint_count(compiled);
  if (report->result_count > 0U) {
    report->results = (ConfitV2ConstraintResult *)calloc(
        report->result_count, sizeof(*report->results));
    if (report->results == 0) {
      status = CONFIT_ERR_INTERNAL;
      confit_diagnostic_set(diagnostic, status, 0, 0U, 0U, kAllocationFailed);
      goto fail;
    }
  }
  environment.bindings = environment_bindings;
  environment.binding_count = environment_count;
  for (index = 0U; index < report->result_count; ++index) {
    status = confit_v2_constraint_evaluate_one(
        confit_v2_compiled_structure_constraint_at(compiled, index), &environment,
        bindings, binding_count, &report->results[index], diagnostic);
    if (status != CONFIT_OK) {
      goto fail;
    }
    if (report->results[index].outcome == CONFIT_V2_CONSTRAINT_FAILED) {
      report->failure_count += 1U;
    }
  }
  status = confit_v2_constraint_suggestions(compiled, &environment, bindings,
                                             binding_count, report, diagnostic);
  if (status != CONFIT_OK) {
    goto fail;
  }
  if (report->result_count > 1U) {
    qsort(report->results, report->result_count, sizeof(*report->results),
          confit_v2_constraint_compare_result);
  }
  free(environment_bindings);
  *out_report = report;
  if (report->failure_count > 0U) {
    for (index = 0U; index < report->result_count; ++index) {
      const ConfitV2ConstraintResult *result = &report->results[index];
      if (result->outcome == CONFIT_V2_CONSTRAINT_FAILED) {
        const ConfitV2Constraint *source = result->constraint->source;
        confit_diagnostic_set_detail(
            diagnostic, CONFIT_ERR_SCHEMA, CONFIT_DIAGNOSTIC_SEVERITY_ERROR,
            kConstraintFailureCode, source->span.path, source->span.line,
            source->span.column, source->message, result->related,
            result->related_count, 0, 0U);
        break;
      }
    }
    return CONFIT_ERR_SCHEMA;
  }
  return CONFIT_OK;

fail:
  free(environment_bindings);
  confit_v2_constraint_report_free(report);
  return status;
}

void confit_v2_constraint_report_free(ConfitV2ConstraintReport *report) {
  size_t index;

  if (report == 0) {
    return;
  }
  for (index = 0U; index < report->result_count; ++index) {
    confit_v2_constraint_result_clear(&report->results[index]);
  }
  free(report->suggestions);
  free(report->results);
  free(report);
}

const ConfitV2CompiledStructure *confit_v2_constraint_report_source(
    const ConfitV2ConstraintReport *report) {
  return report != 0 ? report->compiled : 0;
}

size_t confit_v2_constraint_report_result_count(
    const ConfitV2ConstraintReport *report) {
  return report != 0 ? report->result_count : 0U;
}

const ConfitV2ConstraintResult *confit_v2_constraint_report_result_at(
    const ConfitV2ConstraintReport *report, size_t index) {
  if (report == 0 || index >= report->result_count) {
    return 0;
  }
  return &report->results[index];
}

size_t confit_v2_constraint_report_failure_count(
    const ConfitV2ConstraintReport *report) {
  return report != 0 ? report->failure_count : 0U;
}

size_t confit_v2_constraint_report_suggestion_count(
    const ConfitV2ConstraintReport *report) {
  return report != 0 ? report->suggestion_count : 0U;
}

const ConfitV2SuggestionResult *confit_v2_constraint_report_suggestion_at(
    const ConfitV2ConstraintReport *report, size_t index) {
  if (report == 0 || index >= report->suggestion_count) {
    return 0;
  }
  return &report->suggestions[index];
}
