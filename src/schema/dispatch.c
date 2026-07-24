#include "confit/project.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "confit/host.h"
#include "confit/parser_v2.h"
#include "confit/schema.h"
#include "confit/schema_v1.h"
#include "confit/schema_v2.h"

#include "dispatch_internal.h"

struct ConfitProjectHandle {
  ConfitSchemaVersion schema_version;
  union {
    ConfitProject *v1;
    void *opaque;
  } implementation;
};

static const char kMissingProjectRoot[] = "missing project root";
static const char kMissingProjectOutput[] = "missing project output pointer";
static const char kMissingProjectTable[] = "[project] table is required";
static const char kMissingSchemaVersion[] =
    "[project].schema_version is required";
static const char kInvalidSchemaVersion[] =
    "[project].schema_version must be an integer";
static const char kUnsupportedSchemaVersion[] =
    "unsupported [project].schema_version";
static const char kV2NotImplemented[] =
    "schema_version = 2 loader is not implemented";
static const char kProjectAllocationFailed[] =
    "failed to allocate opaque project handle";

static ConfitStatus confit_dispatch_find_config_root(
    const char *project_root, char *out_root, size_t out_root_size,
    char *out_project_path, size_t out_project_path_size,
    ConfitDiagnostic *diagnostic) {
  char direct_project_path[1024];
  char config_root[1024];
  ConfitStatus status;

  if (project_root == 0 || project_root[0] == '\0') {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, project_root,
                          0, 0, kMissingProjectRoot);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status = confit_host_path_join(direct_project_path,
                                 sizeof(direct_project_path), project_root,
                                 "project.toml", diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }

  if (confit_host_file_exists(direct_project_path)) {
    if (strlen(project_root) + 1U > out_root_size ||
        strlen(direct_project_path) + 1U > out_project_path_size) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                            project_root, 0, 0,
                            "config root buffer is too small");
      return CONFIT_ERR_INVALID_ARGUMENT;
    }
    memcpy(out_root, project_root, strlen(project_root) + 1U);
    memcpy(out_project_path, direct_project_path,
           strlen(direct_project_path) + 1U);
    return CONFIT_OK;
  }

  status = confit_host_path_join(config_root, sizeof(config_root), project_root,
                                 "config", diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_path_join(out_project_path, out_project_path_size,
                                 config_root, "project.toml", diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  if (!confit_host_file_exists(out_project_path)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_PARSE, out_project_path, 0, 0,
                          "failed to open file");
    return CONFIT_ERR_PARSE;
  }
  if (strlen(config_root) + 1U > out_root_size) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, project_root,
                          0, 0, "config root buffer is too small");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  memcpy(out_root, config_root, strlen(config_root) + 1U);
  return CONFIT_OK;
}

static ConfitStatus confit_dispatch_read_schema_version(
    const char *project_path, ConfitSchemaVersion *out_version,
    ConfitDiagnostic *diagnostic) {
  ConfitV2TomlDocument *document;
  const ConfitV2TomlValue *project_table;
  const ConfitV2TomlValue *schema_version;
  const ConfitV2TomlValue *root;
  int64_t raw_version;
  ConfitStatus status;

  if (out_version == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, project_path,
                          0, 0, "missing schema version output pointer");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_version = CONFIT_SCHEMA_VERSION_INVALID;
  document = 0;
  status = confit_v2_toml_parse_file(project_path, &document, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }

  root = confit_v2_toml_document_root(document);
  project_table = confit_v2_toml_table_find(root, "project");
  if (confit_v2_toml_value_type(project_table) != CONFIT_V2_TOML_VALUE_TABLE) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, project_path, 0, 0,
                          kMissingProjectTable);
    confit_v2_toml_document_free(document);
    return CONFIT_ERR_SCHEMA;
  }
  schema_version = confit_v2_toml_table_find(project_table, "schema_version");
  if (schema_version == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, project_path,
                          confit_v2_toml_value_line(project_table),
                          confit_v2_toml_value_column(project_table),
                          kMissingSchemaVersion);
    confit_v2_toml_document_free(document);
    return CONFIT_ERR_SCHEMA;
  }
  if (!confit_v2_toml_value_int64(schema_version, &raw_version)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, project_path,
                          confit_v2_toml_value_line(schema_version),
                          confit_v2_toml_value_column(schema_version),
                          kInvalidSchemaVersion);
    confit_v2_toml_document_free(document);
    return CONFIT_ERR_SCHEMA;
  }
  if (raw_version == (int64_t)CONFIT_SCHEMA_VERSION_V1) {
    *out_version = CONFIT_SCHEMA_VERSION_V1;
  } else if (raw_version == (int64_t)CONFIT_SCHEMA_VERSION_V2) {
    *out_version = CONFIT_SCHEMA_VERSION_V2;
  } else {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_UNSUPPORTED, project_path,
                          confit_v2_toml_value_line(schema_version),
                          confit_v2_toml_value_column(schema_version),
                          kUnsupportedSchemaVersion);
    confit_v2_toml_document_free(document);
    return CONFIT_ERR_UNSUPPORTED;
  }

  confit_v2_toml_document_free(document);
  return CONFIT_OK;
}

ConfitProjectHandle *confit_project_handle_create_v1(ConfitProject *project) {
  ConfitProjectHandle *handle;

  if (project == 0) {
    return 0;
  }
  handle = (ConfitProjectHandle *)calloc(1U, sizeof(*handle));
  if (handle == 0) {
    return 0;
  }
  handle->schema_version = CONFIT_SCHEMA_VERSION_V1;
  handle->implementation.v1 = project;
  return handle;
}

const ConfitProject *
confit_project_handle_borrow_v1(const ConfitProjectHandle *project) {
  if (project == 0 || project->schema_version != CONFIT_SCHEMA_VERSION_V1) {
    return 0;
  }
  return project->implementation.v1;
}

ConfitStatus confit_schema_v1_load_project_handle(
    const char *project_root, ConfitProjectHandle **out_project,
    ConfitDiagnostic *diagnostic) {
  ConfitProject *project;
  ConfitProjectHandle *handle;
  ConfitStatus status;

  if (out_project == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, project_root,
                          0, 0, kMissingProjectOutput);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_project = 0;
  project = 0;
  status = confit_schema_load_project(project_root, &project, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  handle = confit_project_handle_create_v1(project);
  if (handle == 0) {
    confit_project_free(project);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, project_root, 0, 0,
                          kProjectAllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  *out_project = handle;
  return CONFIT_OK;
}

ConfitStatus confit_schema_v2_load_project_handle(
    const char *project_root, ConfitProjectHandle **out_project,
    ConfitDiagnostic *diagnostic) {
  if (out_project == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, project_root,
                          0, 0, kMissingProjectOutput);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_project = 0;
  confit_diagnostic_set(diagnostic, CONFIT_ERR_UNSUPPORTED, project_root, 0, 0,
                        kV2NotImplemented);
  return CONFIT_ERR_UNSUPPORTED;
}

ConfitStatus confit_project_load(const char *project_root,
                                 ConfitProjectHandle **out_project,
                                 ConfitDiagnostic *diagnostic) {
  char config_root[1024];
  char project_path[1024];
  ConfitSchemaVersion version;
  ConfitStatus status;

  if (out_project == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, project_root,
                          0, 0, kMissingProjectOutput);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_project = 0;
  status = confit_dispatch_find_config_root(project_root, config_root,
                                             sizeof(config_root), project_path,
                                             sizeof(project_path), diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_dispatch_read_schema_version(project_path, &version,
                                                diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  if (version == CONFIT_SCHEMA_VERSION_V1) {
    return confit_schema_v1_load_project_handle(config_root, out_project,
                                                 diagnostic);
  }
  return confit_schema_v2_load_project_handle(config_root, out_project,
                                               diagnostic);
}

void confit_project_handle_free(ConfitProjectHandle *project) {
  if (project == 0) {
    return;
  }
  if (project->schema_version == CONFIT_SCHEMA_VERSION_V1) {
    confit_project_free(project->implementation.v1);
  }
  free(project);
}

ConfitSchemaVersion
confit_project_handle_schema_version(const ConfitProjectHandle *project) {
  if (project == 0) {
    return CONFIT_SCHEMA_VERSION_INVALID;
  }
  return project->schema_version;
}
