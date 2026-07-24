#include "confit/resolver_v1.h"

#include <stdlib.h>

#include "confit/resolver.h"
#include "confit/resolver_v2.h"

#include "dispatch_internal.h"

struct ConfitSnapshotHandle {
  ConfitSchemaVersion schema_version;
  union {
    ConfitResolvedConfig *v1;
    ConfitV2Snapshot *v2;
  } implementation;
};

static const char kMissingSnapshotOutput[] = "missing snapshot output pointer";
static const char kWrongProjectVersion[] =
    "project handle does not use schema_version = 1";
static const char kSnapshotAllocationFailed[] =
    "failed to allocate opaque snapshot handle";

ConfitStatus confit_resolver_v1_resolve_handle(
    const ConfitProjectHandle *project, const char *profile_name,
    const char *target_name, const ConfitNamedValue *user_values,
    size_t user_value_count, ConfitSnapshotHandle **out_snapshot,
    ConfitDiagnostic *diagnostic) {
  ConfitResolvedConfig *resolved;
  ConfitSnapshotHandle *snapshot;
  ConfitStatus status;

  if (out_snapshot == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          kMissingSnapshotOutput);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_snapshot = 0;
  if (confit_project_handle_borrow_v1(project) == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          kWrongProjectVersion);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  resolved = 0;
  status = confit_resolver_resolve(confit_project_handle_borrow_v1(project),
                                   profile_name, target_name, user_values,
                                   user_value_count, &resolved, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  snapshot = (ConfitSnapshotHandle *)calloc(1U, sizeof(*snapshot));
  if (snapshot == 0) {
    confit_resolved_config_free(resolved);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0, 0,
                          kSnapshotAllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  snapshot->schema_version = CONFIT_SCHEMA_VERSION_V1;
  snapshot->implementation.v1 = resolved;
  *out_snapshot = snapshot;
  return CONFIT_OK;
}

ConfitStatus confit_resolver_v2_resolve_handle(
    const ConfitProjectHandle *project, ConfitSnapshotHandle **out_snapshot,
    ConfitDiagnostic *diagnostic) {
  ConfitV2LinkedProject *linked = 0;
  ConfitV2CompiledStructure *compiled = 0;
  ConfitV2Snapshot *resolved = 0;
  ConfitSnapshotHandle *snapshot;
  ConfitStatus status;

  if (out_snapshot == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          kMissingSnapshotOutput);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_snapshot = 0;
  if (project == 0 || confit_project_handle_schema_version(project) !=
                          CONFIT_SCHEMA_VERSION_V2) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "project handle does not use schema_version = 2");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  status = confit_v2_schema_link_project(
      confit_project_handle_borrow_v2(project), &linked, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_compile_structure(linked, &compiled, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_snapshot_resolve(compiled, 0, &resolved, diagnostic);
  }
  confit_v2_compiled_structure_free(compiled);
  confit_v2_linked_project_free(linked);
  if (status != CONFIT_OK) {
    return status;
  }
  snapshot = (ConfitSnapshotHandle *)calloc(1U, sizeof(*snapshot));
  if (snapshot == 0) {
    confit_v2_snapshot_free(resolved);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0, 0,
                          kSnapshotAllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  snapshot->schema_version = CONFIT_SCHEMA_VERSION_V2;
  snapshot->implementation.v2 = resolved;
  *out_snapshot = snapshot;
  return CONFIT_OK;
}

void confit_snapshot_handle_free(ConfitSnapshotHandle *snapshot) {
  if (snapshot == 0) {
    return;
  }
  if (snapshot->schema_version == CONFIT_SCHEMA_VERSION_V1) {
    confit_resolved_config_free(snapshot->implementation.v1);
  } else if (snapshot->schema_version == CONFIT_SCHEMA_VERSION_V2) {
    confit_v2_snapshot_free(snapshot->implementation.v2);
  }
  free(snapshot);
}

ConfitSchemaVersion
confit_snapshot_handle_schema_version(const ConfitSnapshotHandle *snapshot) {
  if (snapshot == 0) {
    return CONFIT_SCHEMA_VERSION_INVALID;
  }
  return snapshot->schema_version;
}
