#ifndef CONFIT_EXPRESSION_H
#define CONFIT_EXPRESSION_H

#include <stddef.h>

#include "confit/diagnostic.h"
#include "confit/model.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque, immutable compilation of every catalog dependency.
 *
 * The plan borrows its catalog.  It links and type-checks every reference,
 * rejects cycles, and owns a stable prerequisite-first order.  It does not
 * own or mutate configuration values and does not imply build dependencies.
 */
typedef struct ConfitDependencyPlan ConfitDependencyPlan;

/** @brief Borrowed view of one deterministic evaluation-reason node. */
typedef struct ConfitDependencyReasonView {
  ConfitReasonKind kind;
  int result;
  const char *subject_symbol;
  const char *detail;
  size_t children[CONFIT_REASON_CHILD_LIMIT];
  size_t child_count;
} ConfitDependencyReasonView;

/** @brief Opaque owner of one read-only dependency evaluation result. */
typedef struct ConfitDependencyEvaluation ConfitDependencyEvaluation;

ConfitStatus confit_dependency_plan_create(
    const ConfitCatalog *catalog, const ConfitAllocator *allocator,
    ConfitDependencyPlan **out_plan, ConfitDiagnostic *diagnostic);
void confit_dependency_plan_destroy(ConfitDependencyPlan *plan);

size_t confit_dependency_plan_config_count(const ConfitDependencyPlan *plan);
size_t confit_dependency_plan_edge_count(const ConfitDependencyPlan *plan);
/** @brief Return nonzero only when the plan borrows this exact catalog. */
int confit_dependency_plan_matches_catalog(const ConfitDependencyPlan *plan,
                                           const ConfitCatalog *catalog);
int confit_dependency_plan_has_expression(const ConfitDependencyPlan *plan,
                                          size_t config_index);

/** Return a catalog index in stable prerequisite-first order. */
int confit_dependency_plan_order_at(const ConfitDependencyPlan *plan,
                                    size_t order_index,
                                    size_t *out_config_index);

/**
 * @brief Evaluate one option's availability against a read-only value array.
 *
 * `values` is indexed exactly like the borrowed catalog and must contain one
 * value of the declared kind for every config.  Evaluation never changes an
 * input value and never enables a referenced symbol.
 */
ConfitStatus confit_dependency_plan_evaluate(
    const ConfitDependencyPlan *plan, size_t config_index,
    const ConfitValue *values, size_t value_count,
    const ConfitAllocator *allocator,
    ConfitDependencyEvaluation **out_evaluation,
    ConfitDiagnostic *diagnostic);

void confit_dependency_evaluation_destroy(
    ConfitDependencyEvaluation *evaluation);
int confit_dependency_evaluation_available(
    const ConfitDependencyEvaluation *evaluation);
size_t confit_dependency_evaluation_reason_count(
    const ConfitDependencyEvaluation *evaluation);
size_t confit_dependency_evaluation_reason_root(
    const ConfitDependencyEvaluation *evaluation);
int confit_dependency_evaluation_reason_at(
    const ConfitDependencyEvaluation *evaluation, size_t index,
    ConfitDependencyReasonView *out_view);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_EXPRESSION_H */
