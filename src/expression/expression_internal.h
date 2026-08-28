#ifndef CONFIT_EXPRESSION_INTERNAL_H
#define CONFIT_EXPRESSION_INTERNAL_H

#include "confit/expression.h"

/*
 * Resolver-only evaluation seam.  The caller has already validated the full
 * catalog-aligned value array once.  This keeps maximum-size resolution from
 * repeating an O(config-count) validation for every dependency expression.
 */
ConfitStatus confit_dependency_plan_evaluate_prevalidated(
    const ConfitDependencyPlan *plan, size_t config_index,
    const ConfitValue *values, size_t value_count,
    const ConfitAllocator *allocator,
    ConfitDependencyEvaluation **out_evaluation,
    ConfitDiagnostic *diagnostic);

#endif /* CONFIT_EXPRESSION_INTERNAL_H */
