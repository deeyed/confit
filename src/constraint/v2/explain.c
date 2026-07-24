#include "runtime_internal.h"

#include <stdlib.h>
#include <string.h>

static const char kAllocationFailed[] =
    "failed to allocate schema v2 constraint diagnostic data";
static const char kReadNote[] = "constraint expression reads this effective value";

const ConfitV2ConstraintBinding *confit_v2_constraint_find_binding(
    const ConfitV2ConstraintBinding *bindings, size_t binding_count,
    const char *symbol_id, size_t *out_match_count) {
  const ConfitV2ConstraintBinding *found = 0;
  size_t index;
  size_t matches = 0U;

  for (index = 0U; index < binding_count; ++index) {
    if (bindings[index].symbol != 0 && bindings[index].symbol->id != 0 &&
        strcmp(bindings[index].symbol->id, symbol_id) == 0) {
      found = &bindings[index];
      matches += 1U;
    }
  }
  if (out_match_count != 0) {
    *out_match_count = matches;
  }
  return found;
}

static void confit_v2_constraint_expression_position(
    const ConfitV2LinkedExpression *expression, size_t offset, const char **path,
    size_t *line, size_t *column) {
  const char *text = expression->expression->source;
  size_t cursor;

  *path = expression->expression->source_span.path;
  *line = expression->expression->source_span.line;
  *column = expression->expression->source_span.column;
  for (cursor = 0U; text != 0 && cursor < offset && text[cursor] != '\0';
       ++cursor) {
    if (text[cursor] == '\n') {
      *line += 1U;
      *column = 1U;
    } else if (*column != 0U) {
      *column += 1U;
    }
  }
}

static ConfitStatus confit_v2_constraint_append_related(
    ConfitV2ConstraintResult *result, const char *path, size_t line,
    size_t column, const char *note, ConfitDiagnostic *diagnostic) {
  ConfitDiagnosticRelatedSpan *grown;

  if (result->related_count == SIZE_MAX / sizeof(*result->related)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, path, line, column,
                          kAllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  grown = (ConfitDiagnosticRelatedSpan *)realloc(
      result->related,
      (result->related_count + 1U) * sizeof(*result->related));
  if (grown == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, path, line, column,
                          kAllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  result->related = grown;
  result->related[result->related_count].path = path;
  result->related[result->related_count].line = line;
  result->related[result->related_count].column = column;
  result->related[result->related_count].note = note;
  result->related_count += 1U;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_constraint_append_read(
    ConfitV2ConstraintResult *result, const ConfitV2LinkedReference *reference,
    const ConfitV2LinkedExpression *expression,
    const ConfitV2ConstraintBinding *bindings, size_t binding_count,
    ConfitDiagnostic *diagnostic) {
  ConfitV2ConstraintRead *grown;
  const ConfitV2ConstraintBinding *binding;
  const char *expression_path;
  size_t matches;
  size_t expression_line;
  size_t expression_column;
  ConfitStatus status;

  binding = confit_v2_constraint_find_binding(bindings, binding_count,
                                              reference->symbol->id, &matches);
  if (binding == 0 || matches != 1U) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                          expression->expression->source_span.path,
                          expression->expression->source_span.line,
                          expression->expression->source_span.column,
                          "invalid schema v2 constraint binding context");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  if (result->read_count == SIZE_MAX / sizeof(*result->reads)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL,
                          expression->expression->source_span.path,
                          expression->expression->source_span.line,
                          expression->expression->source_span.column,
                          kAllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  grown = (ConfitV2ConstraintRead *)realloc(
      result->reads, (result->read_count + 1U) * sizeof(*result->reads));
  if (grown == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL,
                          expression->expression->source_span.path,
                          expression->expression->source_span.line,
                          expression->expression->source_span.column,
                          kAllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  confit_v2_constraint_expression_position(expression, reference->node->start_offset,
                                            &expression_path, &expression_line,
                                            &expression_column);
  result->reads = grown;
  result->reads[result->read_count].symbol = reference->symbol;
  result->reads[result->read_count].value = binding->is_set ? binding->value : 0;
  result->reads[result->read_count].is_set = binding->is_set;
  result->reads[result->read_count].source_path = binding->source_path;
  result->reads[result->read_count].source_line = binding->source_line;
  result->reads[result->read_count].source_column = binding->source_column;
  result->reads[result->read_count].expression_path = expression_path;
  result->reads[result->read_count].expression_line = expression_line;
  result->reads[result->read_count].expression_column = expression_column;
  result->read_count += 1U;
  status = confit_v2_constraint_append_related(
      result, expression_path, expression_line, expression_column, kReadNote,
      diagnostic);
  return status;
}

ConfitStatus confit_v2_constraint_collect_expression_reads(
    ConfitV2ConstraintResult *result, const ConfitV2LinkedExpression *expression,
    const ConfitV2ConstraintBinding *bindings, size_t binding_count,
    const char *expression_note, ConfitDiagnostic *diagnostic) {
  size_t index;
  ConfitStatus status;

  status = confit_v2_constraint_append_related(
      result, expression->expression->source_span.path,
      expression->expression->source_span.line,
      expression->expression->source_span.column, expression_note, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  for (index = 0U; index < expression->reference_count; ++index) {
    status = confit_v2_constraint_append_read(
        result, &expression->references[index], expression, bindings,
        binding_count, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

void confit_v2_constraint_result_clear(ConfitV2ConstraintResult *result) {
  if (result == 0) {
    return;
  }
  free(result->reads);
  free(result->related);
  memset(result, 0, sizeof(*result));
}
