#ifndef CONFIT_SNAPSHOT_INTERNAL_H
#define CONFIT_SNAPSHOT_INTERNAL_H

#include <stddef.h>

#include "confit/limits.h"
#include "confit/snapshot.h"

typedef enum ConfitSnapshotFailurePoint {
  CONFIT_SNAPSHOT_FAILURE_NONE = 0,
  CONFIT_SNAPSHOT_FAILURE_AFTER_LOCK,
  CONFIT_SNAPSHOT_FAILURE_AFTER_CANDIDATE,
  CONFIT_SNAPSHOT_FAILURE_AFTER_CORE_FILES,
  CONFIT_SNAPSHOT_FAILURE_AFTER_DIRECTORY_PUBLICATION,
  CONFIT_SNAPSHOT_FAILURE_BEFORE_SELECTED,
} ConfitSnapshotFailurePoint;

typedef struct ConfitSnapshotReadRecord {
  char path[CONFIT_LIMIT_SOURCE_PATH_BYTES + 1U];
  size_t byte_count;
} ConfitSnapshotReadRecord;

typedef struct ConfitSnapshotReadLedger {
  ConfitSnapshotReadRecord *records;
  size_t capacity;
  size_t count;
} ConfitSnapshotReadLedger;

void confit_snapshot_read_ledger_init(ConfitSnapshotReadLedger *ledger,
                                      ConfitSnapshotReadRecord *records,
                                      size_t capacity);

ConfitStatus confit_snapshot_publish_with_failure(
    ConfitHostRoot *output_root,
    const ConfitSnapshotPublishRequest *request,
    const ConfitAllocator *allocator,
    ConfitSnapshotFailurePoint failure_point,
    ConfitSnapshotPublication *out_publication,
    ConfitDiagnostic *diagnostic);

ConfitStatus confit_snapshot_verify_observed(
    const ConfitSnapshotVerifyRequest *request,
    const ConfitAllocator *allocator,
    ConfitSnapshotReadLedger *ledger,
    char *out_artifact_relative_path,
    size_t out_artifact_relative_path_size,
    ConfitDiagnostic *diagnostic);

ConfitStatus confit_snapshot_read_selected_artifact(
    ConfitHostRoot *output_root, const char *artifact_name,
    const ConfitAllocator *allocator, ConfitHostBuffer *out_artifact,
    char out_selected_digest[CONFIT_SNAPSHOT_DIGEST_TEXT_BYTES + 1U],
    ConfitDiagnostic *diagnostic);

#endif /* CONFIT_SNAPSHOT_INTERNAL_H */
