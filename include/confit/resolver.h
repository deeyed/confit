#ifndef CONFIT_RESOLVER_H
#define CONFIT_RESOLVER_H

#include <stddef.h>

#include "confit/diagnostic.h"
#include "confit/expression.h"
#include "confit/model.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Immutable successful resolution of one catalog.
 *
 * The result owns every resolved value, lexical index, and causal reason.  It
 * borrows `catalog` only as declaration context; the catalog must therefore
 * outlive the result.  The dependency plan is needed only during construction.
 */
typedef struct ConfitResolution ConfitResolution;

/**
 * @brief Resolve defaults and at most one explicit assignment per symbol.
 *
 * Assignments are an unordered set.  Unknown or duplicate symbols, wrong
 * types/domains/ranges, and an unavailable non-default assignment fail closed.
 * On every failure `*out_resolution` remains null.
 */
ConfitStatus confit_resolve(
    const ConfitCatalog *catalog, const ConfitDependencyPlan *plan,
    const ConfitAssignment *assignments, size_t assignment_count,
    const ConfitAllocator *allocator, ConfitResolution **out_resolution,
    ConfitDiagnostic *diagnostic);

void confit_resolution_destroy(ConfitResolution *resolution);

/** @brief Borrow the exact catalog used to construct this result. */
const ConfitCatalog *
confit_resolution_catalog(const ConfitResolution *resolution);

/** @brief Number of resolved symbols. */
size_t confit_resolution_value_count(const ConfitResolution *resolution);

/**
 * @brief Borrow a resolved value in lexical symbol order.
 *
 * The pointer remains valid until resolution destroy.
 */
int confit_resolution_value_at(const ConfitResolution *resolution,
                               size_t lexical_index,
                               const ConfitResolvedValue **out_value);

/** @brief Borrow one resolved value by exact public symbol. */
int confit_resolution_find_value(const ConfitResolution *resolution,
                                 const char *symbol,
                                 const ConfitResolvedValue **out_value);

size_t confit_resolution_reason_count(const ConfitResolution *resolution);

/** @brief Borrow one owned causal-reason node until resolution destroy. */
int confit_resolution_reason_at(const ConfitResolution *resolution,
                                size_t index,
                                const ConfitReasonNode **out_reason);

/**
 * @brief Write a deterministic length-framed core representation.
 *
 * This helper is for identity tests and later snapshot preparation.  It is not
 * TOML, Make, C, or JSON.  Values are emitted in lexical symbol order and the
 * representation includes availability, origin, default, and effective value.
 * `out_size` excludes the trailing NUL.  An undersized buffer is untouched.
 */
ConfitStatus confit_resolution_format_canonical(
    const ConfitResolution *resolution, char *buffer, size_t buffer_size,
    size_t *out_size, ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_RESOLVER_H */
