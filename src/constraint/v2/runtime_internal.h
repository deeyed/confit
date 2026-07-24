#ifndef CONFIT_CONSTRAINT_V2_RUNTIME_INTERNAL_H
#define CONFIT_CONSTRAINT_V2_RUNTIME_INTERNAL_H

#include "confit/constraint_v2.h"

struct ConfitV2ConstraintReport {
  const ConfitV2CompiledStructure *compiled;
  ConfitV2ConstraintResult *results;
  size_t result_count;
  size_t failure_count;
  ConfitV2SuggestionResult *suggestions;
  size_t suggestion_count;
};

const ConfitV2ConstraintBinding *confit_v2_constraint_find_binding(
    const ConfitV2ConstraintBinding *bindings, size_t binding_count,
    const char *symbol_id, size_t *out_match_count);

ConfitStatus confit_v2_constraint_collect_expression_reads(
    ConfitV2ConstraintResult *result, const ConfitV2LinkedExpression *expression,
    const ConfitV2ConstraintBinding *bindings, size_t binding_count,
    const char *expression_note, ConfitDiagnostic *diagnostic);

void confit_v2_constraint_result_clear(ConfitV2ConstraintResult *result);

#endif /* CONFIT_CONSTRAINT_V2_RUNTIME_INTERNAL_H */
