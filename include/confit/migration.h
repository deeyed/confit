#ifndef CONFIT_MIGRATION_H
#define CONFIT_MIGRATION_H

#include <stddef.h>

#include "confit/diagnostic.h"
#include "confit/host.h"
#include "confit/model.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Closed change categories for one schema-6 catalog comparison. */
typedef enum ConfitMigrationChange {
  CONFIT_MIGRATION_CHANGE_NONE = 0,
  CONFIT_MIGRATION_CHANGE_NEW = 1U << 0,
  CONFIT_MIGRATION_CHANGE_REMOVED = 1U << 1,
  CONFIT_MIGRATION_CHANGE_TYPE = 1U << 2,
  CONFIT_MIGRATION_CHANGE_DOMAIN = 1U << 3,
  CONFIT_MIGRATION_CHANGE_DEFAULT = 1U << 4,
  CONFIT_MIGRATION_CHANGE_DEPENDENCY = 1U << 5,
  CONFIT_MIGRATION_CHANGE_PROMPT = 1U << 6,
  CONFIT_MIGRATION_CHANGE_HELP = 1U << 7,
} ConfitMigrationChange;

/** @brief Borrowed view of one symbol's stable comparison result. */
typedef struct ConfitMigrationChangeView {
  const char *symbol;
  unsigned changes;
  ConfitValueKind current_kind;
  const char *current_prompt;
} ConfitMigrationChangeView;

/** @brief Opaque owner of a previous-selected versus current-catalog review. */
typedef struct ConfitMigrationReview ConfitMigrationReview;

/**
 * @brief Compare the selected sealed schema-6 catalog with `current_catalog`.
 *
 * This reads only `selected`, its exact seal, and seal-enumerated snapshot
 * artifacts beneath `output_root`.  It deliberately does not remeasure the
 * previous manifest inputs: changed definition bytes are the subject of this
 * comparison.  Missing, schema-5, unsealed, corrupt, or unbounded summaries
 * fail closed.  No directory is enumerated and no project source is opened.
 * A successful review borrows `current_catalog`; it must outlive the review.
 */
ConfitStatus confit_migration_review_selected(
    ConfitHostRoot *output_root, const ConfitCatalog *current_catalog,
    const ConfitAllocator *allocator, ConfitMigrationReview **out_review,
    ConfitDiagnostic *diagnostic);

void confit_migration_review_destroy(ConfitMigrationReview *review);
size_t confit_migration_review_change_count(
    const ConfitMigrationReview *review);
int confit_migration_review_change_at(
    const ConfitMigrationReview *review, size_t lexical_index,
    ConfitMigrationChangeView *out_view);

/** @brief Return nonzero when automatic migration must stop for review. */
int confit_migration_review_has_semantic_changes(
    const ConfitMigrationReview *review);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_MIGRATION_H */
