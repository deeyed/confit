#ifndef CONFIT_SNAPSHOT_H
#define CONFIT_SNAPSHOT_H

#include <stddef.h>

#include "confit/config.h"
#include "confit/diagnostic.h"
#include "confit/host.h"
#include "confit/resolver.h"
#include "confit/schema.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIT_SNAPSHOT_DIGEST_TEXT_BYTES ((size_t)64U)

/** @brief One caller-supplied, inert optional snapshot data artifact. */
typedef struct ConfitSnapshotArtifactSpec {
  const char *role;
  const char *name;
  const void *bytes;
  size_t size;
  int printable;
} ConfitSnapshotArtifactSpec;

/** @brief Complete input to one immutable snapshot publication. */
typedef struct ConfitSnapshotPublishRequest {
  const ConfitSchemaProject *project;
  const ConfitUserConfig *user_config;
  const ConfitResolution *resolution;
  const ConfitAssignment *explicit_assignments;
  size_t explicit_assignment_count;
  const ConfitSnapshotArtifactSpec *optional_artifacts;
  size_t optional_artifact_count;
  int resolved_values_printable;
} ConfitSnapshotPublishRequest;

typedef struct ConfitSnapshotPublication {
  char digest[CONFIT_SNAPSHOT_DIGEST_TEXT_BYTES + 1U];
  int reused_existing;
} ConfitSnapshotPublication;

/**
 * @brief Publish one create-only sealed snapshot and atomically select it.
 *
 * The request borrows all inputs for the duration of the call.  Definition and
 * user inputs are represented by their already-owned exact byte images; they
 * are not reopened during publication.  When `explicit_assignments` is null,
 * the optional user configuration is the exact user-origin authority.  A
 * non-null explicit set lets an interactive controller publish newly reviewed
 * values while retaining an optional original user input in provenance.
 * Optional artifacts are inert bounded
 * byte strings and cannot replace a required core role.  On failure the
 * previous selected record remains the only active authority.
 */
ConfitStatus confit_snapshot_publish(
    ConfitHostRoot *output_root,
    const ConfitSnapshotPublishRequest *request,
    const ConfitAllocator *allocator,
    ConfitSnapshotPublication *out_publication,
    ConfitDiagnostic *diagnostic);

typedef struct ConfitSnapshotVerifyRequest {
  ConfitHostRoot *project_root;
  ConfitHostRoot *output_root;
  const char *expected_entry_path;
  const char *artifact_name;
} ConfitSnapshotVerifyRequest;

/**
 * @brief Verify the selected seal and only its exact manifest-listed inputs.
 *
 * This operation never parses the project schema, reruns dependency
 * evaluation or resolution, enumerates a directory, or searches for inputs.
 * When `artifact_name` is non-null, a successful call writes the normalized
 * path relative to `output_root`; an unsealed or non-printable artifact never
 * produces a path.  Passing no artifact requires a null path buffer and zero
 * capacity.
 */
ConfitStatus confit_snapshot_verify(
    const ConfitSnapshotVerifyRequest *request,
    const ConfitAllocator *allocator,
    char *out_artifact_relative_path,
    size_t out_artifact_relative_path_size,
    ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_SNAPSHOT_H */
