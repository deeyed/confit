#include <string.h>

#include "confit/diagnostic.h"
#include "confit/host.h"
#include "confit/project.h"
#include "confit/resolver_v1.h"
#include "confit/resolver_v2.h"
#include "confit/schema_v1.h"
#include "confit/snapshot.h"
#include "confit/status.h"

#ifndef CONFIT_TEST_SOURCE_DIR
#define CONFIT_TEST_SOURCE_DIR "."
#endif

static int join_fixture(char *out, size_t out_size, const char *fixture) {
  ConfitDiagnostic diagnostic;

  confit_diagnostic_init(&diagnostic);
  return confit_host_path_join(out, out_size, CONFIT_TEST_SOURCE_DIR, fixture,
                               &diagnostic) == CONFIT_OK;
}

static int expect_project_status(const char *fixture, ConfitStatus expected,
                                 const char *message) {
  ConfitDiagnostic diagnostic;
  ConfitProjectHandle *project;
  char path[512];
  ConfitStatus status;

  if (!join_fixture(path, sizeof(path), fixture)) {
    return 0;
  }
  confit_diagnostic_init(&diagnostic);
  project = 0;
  status = confit_project_load(path, &project, &diagnostic);
  if (status != expected || project != 0 || diagnostic.message == 0 ||
      strcmp(diagnostic.message, message) != 0) {
    confit_project_handle_free(project);
    return 0;
  }
  return 1;
}

static int expect_v1_handle_and_snapshot(void) {
  ConfitDiagnostic diagnostic;
  ConfitProjectHandle *project;
  ConfitSnapshotHandle *snapshot;
  char path[512];
  size_t iteration;

  if (!join_fixture(path, sizeof(path), "tests/fixtures/schema/valid/basic")) {
    return 0;
  }
  for (iteration = 0U; iteration < 16U; ++iteration) {
    confit_diagnostic_init(&diagnostic);
    project = 0;
    snapshot = 0;
    if (confit_project_load(path, &project, &diagnostic) != CONFIT_OK ||
        project == 0 ||
        confit_project_handle_schema_version(project) !=
            CONFIT_SCHEMA_VERSION_V1 ||
        confit_resolver_v1_resolve_handle(project, "sim-dsh", 0, 0, 0U,
                                          &snapshot, &diagnostic) != CONFIT_OK ||
        snapshot == 0 ||
        confit_snapshot_handle_schema_version(snapshot) !=
            CONFIT_SCHEMA_VERSION_V1) {
      confit_snapshot_handle_free(snapshot);
      confit_project_handle_free(project);
      return 0;
    }
    confit_snapshot_handle_free(snapshot);
    confit_project_handle_free(project);
  }
  return confit_project_handle_schema_version(0) ==
             CONFIT_SCHEMA_VERSION_INVALID &&
         confit_snapshot_handle_schema_version(0) ==
             CONFIT_SCHEMA_VERSION_INVALID;
}

static int expect_v1_adapter(void) {
  ConfitDiagnostic diagnostic;
  ConfitProjectHandle *project;
  char path[512];

  if (!join_fixture(path, sizeof(path), "tests/fixtures/schema/valid/basic")) {
    return 0;
  }
  confit_diagnostic_init(&diagnostic);
  project = 0;
  if (confit_schema_v1_load_project_handle(path, &project, &diagnostic) !=
          CONFIT_OK ||
      project == 0 ||
      confit_project_handle_schema_version(project) !=
          CONFIT_SCHEMA_VERSION_V1) {
    confit_project_handle_free(project);
    return 0;
  }
  confit_project_handle_free(project);
  return 1;
}

static int expect_v2_handle(void) {
  ConfitDiagnostic diagnostic;
  ConfitProjectHandle *project;
  ConfitSnapshotHandle *snapshot;
  char path[512];

  if (!join_fixture(path, sizeof(path), "tests/fixtures/schema-v2/valid")) {
    return 0;
  }
  confit_diagnostic_init(&diagnostic);
  project = 0;
  snapshot = 0;
  if (confit_project_load(path, &project, &diagnostic) != CONFIT_OK ||
      project == 0 ||
      confit_project_handle_schema_version(project) !=
          CONFIT_SCHEMA_VERSION_V2 ||
      confit_resolver_v1_resolve_handle(project, 0, 0, 0, 0U, &snapshot,
                                        &diagnostic) != CONFIT_ERR_INVALID_ARGUMENT ||
      snapshot != 0 ||
      confit_resolver_v2_resolve_handle(project, &snapshot, &diagnostic) !=
          CONFIT_OK ||
      snapshot == 0 ||
      confit_snapshot_handle_schema_version(snapshot) !=
          CONFIT_SCHEMA_VERSION_V2) {
    confit_snapshot_handle_free(snapshot);
    confit_project_handle_free(project);
    return 0;
  }
  confit_project_handle_free(project);
  return 1;
}

int main(void) {
  if (!expect_v1_handle_and_snapshot()) {
    return 2;
  }
  if (!expect_v1_adapter()) {
    return 3;
  }
  if (!expect_v2_handle()) {
    return 4;
  }
  if (!expect_project_status("tests/fixtures/schema-dispatch/missing-version",
                             CONFIT_ERR_SCHEMA,
                             "[project].schema_version is required")) {
    return 5;
  }
  if (!expect_project_status("tests/fixtures/schema-dispatch/unsupported-version",
                             CONFIT_ERR_UNSUPPORTED,
                             "unsupported [project].schema_version")) {
    return 6;
  }
  if (!expect_project_status(
          "tests/fixtures/schema-dispatch/v1-profile-mismatch",
          CONFIT_ERR_SCHEMA, "unsupported schema_version")) {
    return 7;
  }
  if (!expect_project_status(
          "tests/fixtures/schema-dispatch/v1-target-mismatch",
          CONFIT_ERR_SCHEMA, "unsupported schema_version")) {
    return 8;
  }
  return 0;
}
