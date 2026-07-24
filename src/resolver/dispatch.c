#include "confit/resolver_v1.h"

#include <stdlib.h>

#include "confit/resolver.h"
#include "confit/resolver_v2.h"

#include "dispatch_internal.h"

struct ConfitSnapshotHandle {
  ConfitSchemaVersion schema_version;
  union {
    ConfitResolvedConfig *v1;
    void *opaque;
  } implementation;
};

static const char kMissingSnapshotOutput[] = "missing snapshot output pointer";
static const char kWrongProjectVersion[] =
    "project handle does not use schema_version = 1";
static const char kSnapshotAllocationFailed[] =
    "failed to allocate opaque snapshot handle";
static const char kV2ResolverNotImplemented[] =
    "schema_version = 2 resolver is not implemented";

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
  confit_diagnostic_set(diagnostic, CONFIT_ERR_UNSUPPORTED, 0, 0, 0,
                        kV2ResolverNotImplemented);
  return CONFIT_ERR_UNSUPPORTED;
}

void confit_snapshot_handle_free(ConfitSnapshotHandle *snapshot) {
  if (snapshot == 0) {
    return;
  }
  if (snapshot->schema_version == CONFIT_SCHEMA_VERSION_V1) {
    confit_resolved_config_free(snapshot->implementation.v1);
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
