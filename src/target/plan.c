#include "confit/target_plan.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/host.h"
#include "confit/digest.h"
#include "confit/toml.h"

enum {
  CONFIT_TARGET_PATH_LIMIT = 4096,
  CONFIT_TARGET_TEXT_LIMIT = 1024,
  CONFIT_TARGET_INCLUDE_LIMIT = 32,
  CONFIT_TARGET_IMAGE_LIMIT_MAX = 1024 * 1024 * 1024,
  CONFIT_TARGET_EVIDENCE_LIMIT_MAX = 1024 * 1024,
};

static char *confit_target_strdup(const char *text) {
  char *copy;
  size_t size;
  if (text == 0) return 0;
  size = strlen(text);
  copy = (char *)malloc(size + 1U);
  if (copy != 0) memcpy(copy, text, size + 1U);
  return copy;
}

static int confit_target_atom_valid(const char *text) {
  size_t index;
  if (text == 0 || text[0] == '\0' || strlen(text) > 192U) return 0;
  for (index = 0U; text[index] != '\0'; ++index) {
    const unsigned char value = (unsigned char)text[index];
    if (!((value >= 'a' && value <= 'z') ||
          (value >= '0' && value <= '9') || value == '.' || value == '_' ||
          value == '-' || value == '@')) {
      return 0;
    }
  }
  return 1;
}

/* Compile tuple은 shell text가 아니라 argv atom 목록이다. 새 ISA는 이 generic
 * grammar 안에서 descriptor만 추가할 수 있지만 output/control/plugin option은 어느
 * target도 주입할 수 없다. */
static int confit_target_compile_atom_valid(const char *text) {
  size_t index;
  if (text == 0 || text[0] != '-' || strlen(text) > 128U ||
      strcmp(text, "-o") == 0 || strncmp(text, "-o", 2U) == 0 ||
      strncmp(text, "-M", 2U) == 0 || strncmp(text, "-fplugin", 8U) == 0 ||
      strncmp(text, "-X", 2U) == 0 || strncmp(text, "-B", 2U) == 0 ||
      text[1] == '-' || strchr(text, '@') != 0 || strchr(text, '/') != 0) {
    return 0;
  }
  if (text[1] != 'm' && strncmp(text, "-fno-", 5U) != 0 &&
      text[1] != 'D') {
    return 0;
  }
  for (index = 1U; text[index] != '\0'; ++index) {
    const unsigned char value = (unsigned char)text[index];
    if (!((value >= 'A' && value <= 'Z') ||
          (value >= 'a' && value <= 'z') ||
          (value >= '0' && value <= '9') || value == '_' || value == '-' ||
          value == '.' || value == '+' || value == '=')) return 0;
  }
  return 1;
}

static int confit_target_relative_path_valid(const char *text) {
  const char *segment;
  const char *cursor;
  if (text == 0 || text[0] == '\0' || text[0] == '/' || text[0] == '\\' ||
      strchr(text, '\\') != 0 || strlen(text) >= CONFIT_TARGET_PATH_LIMIT) {
    return 0;
  }
  segment = text;
  cursor = text;
  while (1) {
    if (*cursor == '/' || *cursor == '\0') {
      const size_t size = (size_t)(cursor - segment);
      if (size == 0U || (size == 1U && segment[0] == '.') ||
          (size == 2U && segment[0] == '.' && segment[1] == '.')) {
        return 0;
      }
      if (*cursor == '\0') break;
      segment = cursor + 1;
    } else {
      const unsigned char value = (unsigned char)*cursor;
      if (!((value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') || value == '.' || value == '_' ||
            value == '-' || value == '+')) {
        return 0;
      }
    }
    cursor += 1;
  }
  return 1;
}

static int confit_target_support_facade_valid(const char *text) {
  static const char prefix[] = "sys/include/parus/";
  return confit_target_relative_path_valid(text) &&
         strncmp(text, prefix, sizeof(prefix) - 1U) == 0 &&
         text[sizeof(prefix) - 1U] != '\0';
}

static int confit_target_kapi_valid(const char *text) {
  const char *version;
  size_t index;
  if (!confit_target_atom_valid(text)) return 0;
  version = strstr(text, ".v");
  if (version == 0 || version == text || version[2] == '\0') return 0;
  for (index = 2U; version[index] != '\0'; ++index) {
    if (version[index] < '0' || version[index] > '9') return 0;
  }
  return version[2] != '0';
}

static int confit_target_world_role_valid(const char *text) {
  static const char *const roles[] = {
      "service", "library_private", "install_plan", "terminal"};
  const char *separator = text != 0 ? strchr(text, '=') : 0;
  size_t index;
  if (separator == 0 || separator == text || strchr(separator + 1, '=') != 0 ||
      !confit_target_relative_path_valid(separator + 1)) return 0;
  for (index = 0U; index < sizeof(roles) / sizeof(roles[0]); ++index) {
    const size_t role_size = strlen(roles[index]);
    if ((size_t)(separator - text) == role_size &&
        memcmp(text, roles[index], role_size) == 0) return 1;
  }
  return 0;
}

static int confit_target_kernel_role_valid(const char *text) {
  static const char *const roles[] = {
      "elf", "map", "kapi", "driverdb", "sysinitdb", "release_report",
      "boot_dtb"};
  const char *separator = text != 0 ? strchr(text, '=') : 0;
  size_t role_index;
  if (separator == 0 || separator == text || strchr(separator + 1, '=') != 0 ||
      !confit_target_relative_path_valid(separator + 1)) return 0;
  for (role_index = 0U; role_index < sizeof(roles) / sizeof(roles[0]);
       ++role_index) {
    const size_t size = strlen(roles[role_index]);
    if ((size_t)(separator - text) == size &&
        memcmp(text, roles[role_index], size) == 0) return 1;
  }
  return 0;
}

static int confit_target_image_role_valid(const char *text) {
  static const char *const roles[] = {
      "kernel_payload", "package", "manifest", "terminal",
      "firmware_kernel", "firmware_config", "firmware_cmdline",
      "firmware_dtb"};
  const char *separator = text != 0 ? strchr(text, '=') : 0;
  size_t role_index;
  if (separator == 0 || separator == text || strchr(separator + 1, '=') != 0 ||
      !confit_target_relative_path_valid(separator + 1)) return 0;
  for (role_index = 0U; role_index < sizeof(roles) / sizeof(roles[0]);
       ++role_index) {
    const size_t size = strlen(roles[role_index]);
    if ((size_t)(separator - text) == size &&
        memcmp(text, roles[role_index], size) == 0) return 1;
  }
  return 0;
}

static int confit_target_package_digest_valid(const char *text) {
  static const char *const roles[] = {
      "firmware_config", "firmware_cmdline", "firmware_dtb"};
  const char *separator = text != 0 ? strchr(text, '=') : 0;
  size_t role_index;
  size_t index;
  if (separator == 0 || separator == text || strchr(separator + 1, '=') != 0 ||
      strlen(separator + 1) != 64U) return 0;
  for (index = 0U; index < 64U; ++index) {
    const unsigned char value = (unsigned char)separator[1U + index];
    if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) {
      return 0;
    }
  }
  for (role_index = 0U; role_index < sizeof(roles) / sizeof(roles[0]);
       ++role_index) {
    const size_t size = strlen(roles[role_index]);
    if ((size_t)(separator - text) == size &&
        memcmp(text, roles[role_index], size) == 0) return 1;
  }
  return 0;
}

static int confit_target_table_only(const ConfitTomlValue *table,
                                    const char *const *fields,
                                    size_t field_count) {
  size_t index;
  if (table == 0 ||
      confit_toml_value_type(table) != CONFIT_TOML_VALUE_TABLE) {
    return 0;
  }
  for (index = 0U; index < confit_toml_table_size(table); ++index) {
    const char *key = confit_toml_table_key_at(table, index);
    size_t field_index;
    int found = 0;
    for (field_index = 0U; field_index < field_count; ++field_index) {
      if (strcmp(key, fields[field_index]) == 0) {
        found = 1;
        break;
      }
    }
    if (!found) return 0;
  }
  return 1;
}

static ConfitStatus confit_target_get_string(
    const ConfitTomlValue *table, const char *key, int required,
    char **out, const char *path, ConfitDiagnostic *diagnostic) {
  const ConfitTomlValue *value = confit_toml_table_find(table, key);
  const char *text;
  size_t size;
  *out = 0;
  if (value == 0 && !required) return CONFIT_OK;
  if (value == 0 || !confit_toml_value_string(value, &text, &size) ||
      size == 0U || size > CONFIT_TARGET_TEXT_LIMIT) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, path,
                          confit_toml_value_line(table),
                          confit_toml_value_column(table),
                          "target descriptor string field is missing or invalid");
    return CONFIT_ERR_SCHEMA;
  }
  *out = (char *)malloc(size + 1U);
  if (*out == 0) return CONFIT_ERR_INTERNAL;
  memcpy(*out, text, size);
  (*out)[size] = '\0';
  return CONFIT_OK;
}

static ConfitStatus confit_target_get_string_list(
    const ConfitTomlValue *table, const char *key, char ***out_items,
    size_t *out_count, const char *path, ConfitDiagnostic *diagnostic) {
  const ConfitTomlValue *value = confit_toml_table_find(table, key);
  size_t index;
  *out_items = 0;
  *out_count = 0U;
  if (value == 0 ||
      confit_toml_value_type(value) != CONFIT_TOML_VALUE_ARRAY ||
      confit_toml_array_size(value) > CONFIT_TARGET_INCLUDE_LIMIT) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, path,
                          confit_toml_value_line(table),
                          confit_toml_value_column(table),
                          "target descriptor string list is missing or invalid");
    return CONFIT_ERR_SCHEMA;
  }
  if (confit_toml_array_size(value) != 0U) {
    *out_items = (char **)calloc(confit_toml_array_size(value),
                                 sizeof(**out_items));
    if (*out_items == 0) return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < confit_toml_array_size(value); ++index) {
    const ConfitTomlValue *item = confit_toml_array_at(value, index);
    const char *text;
    size_t size;
    if (!confit_toml_value_string(item, &text, &size) || size == 0U ||
        size > CONFIT_TARGET_TEXT_LIMIT) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, path,
                            confit_toml_value_line(item),
                            confit_toml_value_column(item),
                            "target descriptor list item is invalid");
      return CONFIT_ERR_SCHEMA;
    }
    (*out_items)[index] = (char *)malloc(size + 1U);
    if ((*out_items)[index] == 0) return CONFIT_ERR_INTERNAL;
    memcpy((*out_items)[index], text, size);
    (*out_items)[index][size] = '\0';
    *out_count += 1U;
  }
  return CONFIT_OK;
}

static int confit_target_string_list_contains(const ConfitTomlValue *array,
                                              const char *expected) {
  size_t index;
  if (array == 0 ||
      confit_toml_value_type(array) != CONFIT_TOML_VALUE_ARRAY) {
    return 0;
  }
  for (index = 0U; index < confit_toml_array_size(array); ++index) {
    const ConfitTomlValue *item = confit_toml_array_at(array, index);
    const char *text;
    size_t size;
    if (confit_toml_value_string(item, &text, &size) &&
        strlen(expected) == size && memcmp(text, expected, size) == 0) {
      return 1;
    }
  }
  return 0;
}

static int confit_target_value_matches(const ConfitTomlValue *values,
                                       const char *key,
                                       const char *expected) {
  const ConfitTomlValue *value = confit_toml_table_find(values, key);
  const char *text;
  size_t size;
  return value != 0 && confit_toml_value_string(value, &text, &size) &&
         strlen(expected) == size && memcmp(text, expected, size) == 0;
}

static ConfitStatus confit_target_repo_path(
    const ConfitV2Project *project, const char *relative, int directory,
    char **out, ConfitDiagnostic *diagnostic) {
  char joined[CONFIT_TARGET_PATH_LIMIT];
  char canonical[CONFIT_TARGET_PATH_LIMIT];
  const size_t root_size = strlen(project->project_root);
  ConfitStatus status;
  *out = 0;
  if (!confit_target_relative_path_valid(relative)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, relative, 0U, 0U,
                          "target descriptor path is not confined and relative");
    return CONFIT_ERR_SCHEMA;
  }
  status = confit_host_path_join(joined, sizeof(joined), project->project_root,
                                 relative, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_host_path_canonicalize(canonical, sizeof(canonical), joined,
                                           diagnostic);
  }
  if (status != CONFIT_OK || strncmp(canonical, project->project_root, root_size) != 0 ||
      (canonical[root_size] != '/' && canonical[root_size] != '\\')) {
    if (status == CONFIT_OK) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, relative, 0U, 0U,
                            "target descriptor path escapes project root");
      status = CONFIT_ERR_SCHEMA;
    }
    return status;
  }
  if ((directory && !confit_host_directory_exists(canonical)) ||
      (!directory && !confit_host_file_exists(canonical))) {
    confit_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA, relative, 0U, 0U,
        directory ? "target descriptor directory is missing or not a directory"
                  : "target descriptor file is missing or not a regular file");
    return CONFIT_ERR_SCHEMA;
  }
  *out = confit_target_strdup(relative);
  return *out != 0 ? CONFIT_OK : CONFIT_ERR_INTERNAL;
}

static ConfitStatus confit_target_find_descriptor(
    const ConfitV2Project *project, const char *target_id, char *out,
    size_t out_size, ConfitDiagnostic *diagnostic) {
  size_t index;
  size_t found = 0U;
  char name[256];
  if (!confit_target_atom_valid(target_id) ||
      snprintf(name, sizeof(name), "%s.toml", target_id) < 0) {
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < project->target_dirs.count; ++index) {
    char directory[CONFIT_TARGET_PATH_LIMIT];
    char candidate[CONFIT_TARGET_PATH_LIMIT];
    ConfitStatus status = confit_host_path_join(
        directory, sizeof(directory), project->config_root,
        project->target_dirs.items[index], diagnostic);
    if (status == CONFIT_OK) status = confit_host_path_join(
        candidate, sizeof(candidate), directory, name, diagnostic);
    if (status != CONFIT_OK) return status;
    if (confit_host_file_exists(candidate)) {
      if (found != 0U) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, target_id, 0U, 0U,
                              "target descriptor filename is ambiguous");
        return CONFIT_ERR_SCHEMA;
      }
      if (strlen(candidate) + 1U > out_size) return CONFIT_ERR_INTERNAL;
      memcpy(out, candidate, strlen(candidate) + 1U);
      found += 1U;
    }
  }
  if (found != 1U) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, target_id, 0U, 0U,
                          "exact target descriptor file is missing");
    return CONFIT_ERR_SCHEMA;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_target_parse_build(
    const ConfitV2Project *project, const char *target_id,
    ConfitTargetPlan *plan, ConfitDiagnostic *diagnostic) {
  static const char *const root_fields[] = {
      "target", "values", "build", "support", "machine"};
  static const char *const target_fields[] = {"name", "schema_version"};
  static const char *const build_fields[] = {
      "isa", "abi", "cpu_profile", "entry_profile", "toolchain",
      "linker_script", "image_artifact_profile", "image_artifact_roles",
      "package_profile", "machine_profile",
      "expected_component", "expected_capability", "output_stem",
      "required_profile", "max_image_bytes", "dts",
      "dtc", "package_source", "package_input_digests", "kernel_artifact_profile",
      "kernel_artifact_roles", "max_kernel_bytes", "world_artifact_profile",
      "compile_tuple", "world_boot_component", "world_artifact_entry",
      "world_artifact_linker_script", "world_artifact_roles",
      "max_world_bytes"};
  static const char *const machine_fields[] = {
      "runner", "architecture", "executable", "machine", "cpu",
      "memory_mib", "serial", "artifact", "trust_profile",
      "resource_identity", "evidence_transport", "evidence_protocol",
      "evidence_max_bytes"};
  static const char *const support_fields[] = {
      "provider_owner", "consumer_owner", "role", "facade_include_root",
      "required_kapi"};
  char path[CONFIT_TARGET_PATH_LIMIT];
  ConfitTomlDocument *document = 0;
  const ConfitTomlValue *root;
  const ConfitTomlValue *target;
  const ConfitTomlValue *values;
  const ConfitTomlValue *build;
  const ConfitTomlValue *support;
  const ConfitTomlValue *machine;
  const ConfitTomlValue *name;
  const ConfitTomlValue *schema;
  const ConfitTomlValue *max_image;
  const ConfitTomlValue *max_kernel;
  const ConfitTomlValue *max_world;
  const ConfitTomlValue *machine_memory;
  const ConfitTomlValue *machine_evidence_max;
  const char *name_text;
  size_t name_size;
  int64_t schema_version;
  int64_t max_image_bytes;
  int64_t max_kernel_bytes;
  int64_t max_world_bytes;
  int64_t machine_memory_mib = 0;
  int64_t machine_evidence_max_bytes = 0;
  char *linker_relative = 0;
  char *dts_relative = 0;
  char *package_relative = 0;
  char *world_artifact_linker_relative = 0;
  char **compile_tuple = 0;
  size_t compile_tuple_count = 0U;
  char **artifact_roles = 0;
  size_t artifact_role_count = 0U;
  char **kernel_roles = 0;
  size_t kernel_role_count = 0U;
  char **image_roles = 0;
  size_t image_role_count = 0U;
  char **package_input_digests = 0;
  size_t package_input_digest_count = 0U;
  size_t index;
  ConfitStatus status;

  status = confit_target_find_descriptor(project, target_id, path,
                                         sizeof(path), diagnostic);
  if (status == CONFIT_OK) status = confit_toml_parse_file(
      path, &document, diagnostic);
  if (status != CONFIT_OK) return status;
  root = confit_toml_document_root(document);
  target = confit_toml_table_find(root, "target");
  values = confit_toml_table_find(root, "values");
  build = confit_toml_table_find(root, "build");
  support = confit_toml_table_find(root, "support");
  machine = confit_toml_table_find(root, "machine");
  if (!confit_target_table_only(root, root_fields,
                                sizeof(root_fields) / sizeof(root_fields[0])) ||
      target == 0 || values == 0 || support == 0 ||
      !confit_target_table_only(target, target_fields,
                                sizeof(target_fields) / sizeof(target_fields[0])) ||
      confit_toml_value_type(values) != CONFIT_TOML_VALUE_TABLE ||
      !confit_target_table_only(build, build_fields,
                                sizeof(build_fields) / sizeof(build_fields[0])) ||
      !confit_target_table_only(support, support_fields,
                                sizeof(support_fields) / sizeof(support_fields[0])) ||
      (machine != 0 &&
       !confit_target_table_only(machine, machine_fields,
                                 sizeof(machine_fields) / sizeof(machine_fields[0])))) {
    status = CONFIT_ERR_SCHEMA;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "target descriptor has unknown or missing tables");
    goto done;
  }
  name = confit_toml_table_find(target, "name");
  schema = confit_toml_table_find(target, "schema_version");
  if (name == 0 || schema == 0 ||
      !confit_toml_value_string(name, &name_text, &name_size) ||
      strlen(target_id) != name_size ||
      memcmp(name_text, target_id, name_size) != 0 ||
      !confit_toml_value_int64(schema, &schema_version) ||
      schema_version != 3) {
    status = CONFIT_ERR_SCHEMA;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "target descriptor identity or schema is invalid");
    goto done;
  }
#define CONFIT_TARGET_BUILD_STRING(field, member)                              \
  if (status == CONFIT_OK)                                                     \
    status = confit_target_get_string(build, field, 1, &plan->member, path,    \
                                      diagnostic)
  plan->target_id = confit_target_strdup(target_id);
  plan->target_descriptor_path = confit_target_strdup(path);
  status = plan->target_id != 0 && plan->target_descriptor_path != 0
               ? CONFIT_OK
               : CONFIT_ERR_INTERNAL;
  CONFIT_TARGET_BUILD_STRING("isa", isa);
  CONFIT_TARGET_BUILD_STRING("abi", abi);
  CONFIT_TARGET_BUILD_STRING("cpu_profile", cpu_profile);
  CONFIT_TARGET_BUILD_STRING("entry_profile", entry_profile);
  CONFIT_TARGET_BUILD_STRING("toolchain", toolchain_id);
  CONFIT_TARGET_BUILD_STRING("image_artifact_profile", image_artifact_profile);
  CONFIT_TARGET_BUILD_STRING("package_profile", package_profile);
  CONFIT_TARGET_BUILD_STRING("machine_profile", machine_profile);
  CONFIT_TARGET_BUILD_STRING("expected_component", expected_component);
  CONFIT_TARGET_BUILD_STRING("expected_capability", expected_capability);
  CONFIT_TARGET_BUILD_STRING("output_stem", output_stem);
  CONFIT_TARGET_BUILD_STRING("required_profile", required_profile);
  CONFIT_TARGET_BUILD_STRING("kernel_artifact_profile", kernel_artifact_profile);
  CONFIT_TARGET_BUILD_STRING("world_artifact_profile", world_artifact_profile);
  if (status == CONFIT_OK) status = confit_target_get_string(
      build, "world_boot_component", 0, &plan->world_boot_component, path,
      diagnostic);
  if (status == CONFIT_OK) status = confit_target_get_string(
      build, "world_artifact_entry", 0, &plan->world_artifact_entry, path,
      diagnostic);
  if (status == CONFIT_OK) status = confit_target_get_string(
      build, "world_artifact_linker_script", 0,
      &world_artifact_linker_relative, path, diagnostic);
  if (status == CONFIT_OK && machine != 0) {
    status = confit_target_get_string(machine, "runner", 1,
                                      &plan->machine_runner, path, diagnostic);
    if (status == CONFIT_OK) status = confit_target_get_string(
        machine, "architecture", 1, &plan->machine_architecture, path,
        diagnostic);
    if (status == CONFIT_OK) status = confit_target_get_string(
        machine, "executable", 1, &plan->machine_executable, path,
        diagnostic);
    if (status == CONFIT_OK) status = confit_target_get_string(
        machine, "machine", 1, &plan->machine_name, path, diagnostic);
    if (status == CONFIT_OK) status = confit_target_get_string(
        machine, "cpu", 1, &plan->machine_cpu, path, diagnostic);
    if (status == CONFIT_OK) status = confit_target_get_string(
        machine, "serial", 1, &plan->machine_serial, path, diagnostic);
    if (status == CONFIT_OK) status = confit_target_get_string(
        machine, "artifact", 1, &plan->machine_artifact, path, diagnostic);
    if (status == CONFIT_OK) status = confit_target_get_string(
        machine, "trust_profile", 1, &plan->machine_trust_profile, path,
        diagnostic);
    if (status == CONFIT_OK) status = confit_target_get_string(
        machine, "resource_identity", 1, &plan->machine_resource_identity,
        path, diagnostic);
    if (status == CONFIT_OK) status = confit_target_get_string(
        machine, "evidence_transport", 1,
        &plan->machine_evidence_transport, path, diagnostic);
    if (status == CONFIT_OK) status = confit_target_get_string(
        machine, "evidence_protocol", 1,
        &plan->machine_evidence_protocol, path, diagnostic);
    machine_memory = confit_toml_table_find(machine, "memory_mib");
    if (status == CONFIT_OK &&
        (machine_memory == 0 ||
         !confit_toml_value_int64(machine_memory, &machine_memory_mib) ||
         machine_memory_mib < 16 || machine_memory_mib > 65536)) {
      status = CONFIT_ERR_SCHEMA;
    }
    if (status == CONFIT_OK) {
      plan->machine_memory_mib = (size_t)machine_memory_mib;
    }
    machine_evidence_max =
        confit_toml_table_find(machine, "evidence_max_bytes");
    if (status == CONFIT_OK &&
        (machine_evidence_max == 0 ||
         !confit_toml_value_int64(machine_evidence_max,
                                     &machine_evidence_max_bytes) ||
         machine_evidence_max_bytes <= 0 ||
         machine_evidence_max_bytes > CONFIT_TARGET_EVIDENCE_LIMIT_MAX)) {
      status = CONFIT_ERR_SCHEMA;
    }
    if (status == CONFIT_OK) {
      plan->machine_evidence_max_bytes =
          (size_t)machine_evidence_max_bytes;
    }
  }
  if (status == CONFIT_OK) status = confit_target_get_string(
      support, "provider_owner", 1, &plan->support_provider_owner, path,
      diagnostic);
  if (status == CONFIT_OK) status = confit_target_get_string(
      support, "consumer_owner", 1, &plan->support_consumer_owner, path,
      diagnostic);
  if (status == CONFIT_OK) status = confit_target_get_string(
      support, "role", 1, &plan->support_role, path, diagnostic);
  if (status == CONFIT_OK) status = confit_target_get_string(
      support, "facade_include_root", 1,
      &plan->support_facade_include_root, path, diagnostic);
  if (status == CONFIT_OK) status = confit_target_get_string(
      support, "required_kapi", 1, &plan->support_required_kapi, path,
      diagnostic);
  if (status == CONFIT_OK) status = confit_target_get_string(
      build, "linker_script", 1, &linker_relative, path, diagnostic);
  if (status == CONFIT_OK) status = confit_target_get_string(
      build, "dts", 0, &dts_relative, path, diagnostic);
  if (status == CONFIT_OK) status = confit_target_get_string(
      build, "package_source", 0, &package_relative, path, diagnostic);
  if (status == CONFIT_OK) status = confit_target_get_string_list(
      build, "compile_tuple", &compile_tuple, &compile_tuple_count, path,
      diagnostic);
  if (status == CONFIT_OK) status = confit_target_get_string_list(
      build, "kernel_artifact_roles", &kernel_roles, &kernel_role_count, path,
      diagnostic);
  if (status == CONFIT_OK) status = confit_target_get_string_list(
      build, "image_artifact_roles", &image_roles, &image_role_count, path,
      diagnostic);
  if (status == CONFIT_OK) status = confit_target_get_string_list(
      build, "package_input_digests", &package_input_digests,
      &package_input_digest_count, path, diagnostic);
  if (status == CONFIT_OK) status = confit_target_get_string_list(
      build, "world_artifact_roles", &artifact_roles, &artifact_role_count,
      path, diagnostic);
#undef CONFIT_TARGET_BUILD_STRING
  max_image = confit_toml_table_find(build, "max_image_bytes");
  max_kernel = confit_toml_table_find(build, "max_kernel_bytes");
  max_world = confit_toml_table_find(build, "max_world_bytes");
  if (status == CONFIT_OK &&
      (max_image == 0 || !confit_toml_value_int64(max_image, &max_image_bytes) ||
       max_image_bytes <= 0 ||
       max_image_bytes > CONFIT_TARGET_IMAGE_LIMIT_MAX)) {
    status = CONFIT_ERR_SCHEMA;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "target max_image_bytes is outside the closed limit");
  }
  if (status == CONFIT_OK) plan->max_image_bytes = (size_t)max_image_bytes;
  if (status == CONFIT_OK &&
      (max_kernel == 0 ||
       !confit_toml_value_int64(max_kernel, &max_kernel_bytes) ||
       max_kernel_bytes <= 0 || max_kernel_bytes > CONFIT_TARGET_IMAGE_LIMIT_MAX)) {
    status = CONFIT_ERR_SCHEMA;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "target max_kernel_bytes is outside the closed limit");
  }
  if (status == CONFIT_OK) plan->max_kernel_bytes = (size_t)max_kernel_bytes;
  if (status == CONFIT_OK &&
      (max_world == 0 ||
       !confit_toml_value_int64(max_world, &max_world_bytes) ||
       max_world_bytes <= 0 || max_world_bytes > CONFIT_TARGET_IMAGE_LIMIT_MAX)) {
    status = CONFIT_ERR_SCHEMA;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "target max_world_bytes is outside the closed limit");
  }
  if (status == CONFIT_OK) plan->max_world_bytes = (size_t)max_world_bytes;
  if (status == CONFIT_OK &&
      (!confit_target_atom_valid(plan->isa) ||
       !confit_target_atom_valid(plan->abi) ||
       !confit_target_atom_valid(plan->cpu_profile) ||
       !confit_target_atom_valid(plan->entry_profile) ||
       !confit_target_atom_valid(plan->toolchain_id) ||
       !confit_target_atom_valid(plan->image_artifact_profile) ||
       !confit_target_atom_valid(plan->package_profile) ||
       !confit_target_atom_valid(plan->machine_profile) ||
       !confit_target_atom_valid(plan->expected_component) ||
       !confit_target_atom_valid(plan->expected_capability) ||
       !confit_target_atom_valid(plan->output_stem) ||
       !confit_target_atom_valid(plan->required_profile) ||
       !confit_target_atom_valid(plan->kernel_artifact_profile) ||
       !confit_target_atom_valid(plan->world_artifact_profile) ||
       !confit_target_atom_valid(plan->support_provider_owner) ||
       !confit_target_atom_valid(plan->support_consumer_owner) ||
       !confit_target_atom_valid(plan->support_role) ||
       !confit_target_kapi_valid(plan->support_required_kapi) ||
       !confit_target_support_facade_valid(
           plan->support_facade_include_root) ||
       (machine != 0 &&
        (!confit_target_atom_valid(plan->machine_runner) ||
         !confit_target_atom_valid(plan->machine_architecture) ||
         !confit_target_atom_valid(plan->machine_executable) ||
         !confit_target_atom_valid(plan->machine_trust_profile) ||
         !confit_target_atom_valid(plan->machine_resource_identity) ||
         !confit_target_atom_valid(plan->machine_evidence_transport) ||
         !confit_target_atom_valid(plan->machine_evidence_protocol) ||
         !confit_target_atom_valid(plan->machine_name) ||
         !confit_target_atom_valid(plan->machine_cpu) ||
         !confit_target_atom_valid(plan->machine_serial) ||
         !confit_target_atom_valid(plan->machine_artifact))))) {
    status = CONFIT_ERR_SCHEMA;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "target descriptor contains an unsafe atom");
  }
  if (status == CONFIT_OK && machine != 0 &&
      (strcmp(plan->machine_runner, "qemu-v1") != 0 ||
       strcmp(plan->machine_trust_profile, "qemu-executable-v1") != 0 ||
       strcmp(plan->machine_evidence_transport,
              "qemu-fwcfg-challenge-v1") != 0 ||
       strcmp(plan->machine_evidence_protocol,
              "parus-qemu-terminal-v1") != 0 ||
       strcmp(plan->machine_architecture, plan->isa) != 0 ||
       strcmp(plan->machine_serial, "stdio-v1") != 0 ||
       strcmp(plan->machine_artifact, "flat-image-v1") != 0 ||
       ((strcmp(plan->isa, "arm64") == 0 &&
         strcmp(plan->machine_executable, "qemu-system-aarch64") != 0) ||
        (strcmp(plan->isa, "amd64") == 0 &&
         strcmp(plan->machine_executable, "qemu-system-x86_64") != 0) ||
        (strcmp(plan->isa, "riscv64") == 0 &&
         strcmp(plan->machine_executable, "qemu-system-riscv64") != 0)))) {
    status = CONFIT_ERR_UNSUPPORTED;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "target machine tuple uses an unknown QEMU profile");
  }
  if (status == CONFIT_OK &&
      (strcmp(plan->support_consumer_owner, plan->expected_component) != 0 ||
       strcmp(plan->support_role, "architecture.facade.v1") != 0)) {
    status = CONFIT_ERR_CONFLICT;
    confit_diagnostic_set(
        diagnostic, status, path, 0U, 0U,
        "target support edge is not the exact board-to-architecture facade");
  }
  if (status == CONFIT_OK &&
      (!confit_target_value_matches(values, "parus.target.isa", plan->isa) ||
       !confit_target_value_matches(values, "parus.target.cpu", plan->cpu_profile) ||
       !confit_target_value_matches(values, "parus.target.entry_abi", plan->entry_profile) ||
       !confit_target_value_matches(values, "parus.toolchain.id", plan->toolchain_id) ||
       !confit_target_value_matches(values, "parus.target.output_name", plan->output_stem))) {
    status = CONFIT_ERR_CONFLICT;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "target build tuple contradicts typed target selection");
  }
  if (status == CONFIT_OK &&
      (strcmp(plan->image_artifact_profile, "imagegen-v1") != 0 ||
       (strcmp(plan->package_profile, "container-v1") != 0 &&
        strcmp(plan->package_profile, "firmware-directory-v1") != 0) ||
       strcmp(plan->required_profile, "release") != 0 ||
       strcmp(plan->kernel_artifact_profile, "elf-v1") != 0 ||
       (strcmp(plan->world_artifact_profile, "none") != 0 &&
        strcmp(plan->world_artifact_profile, "world-v1") != 0))) {
    status = CONFIT_ERR_UNSUPPORTED;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "target descriptor uses an unknown image or package kind");
  }
  if (status == CONFIT_OK) status = confit_target_repo_path(
      project, linker_relative, 0, &plan->linker_script, diagnostic);
  if (status == CONFIT_OK && dts_relative != 0) status = confit_target_repo_path(
      project, dts_relative, 0, &plan->dts_path, diagnostic);
  if (status == CONFIT_OK && package_relative != 0) status = confit_target_repo_path(
      project, package_relative, 1, &plan->package_source, diagnostic);
  if (status == CONFIT_OK && world_artifact_linker_relative != 0) {
    status = confit_target_repo_path(
        project, world_artifact_linker_relative, 0,
        &plan->world_artifact_linker_script, diagnostic);
  }
  if (status == CONFIT_OK && strcmp(plan->package_profile,
                                    "firmware-directory-v1") == 0 &&
      plan->package_source == 0) {
    status = CONFIT_ERR_SCHEMA;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "firmware package profile requires package_source");
  }
  if (status == CONFIT_OK &&
      strcmp(plan->package_profile, "firmware-directory-v1") == 0 &&
      package_input_digest_count != 3U) {
    status = CONFIT_ERR_SCHEMA;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "firmware package profile requires three sealed input digests");
  }
  if (status == CONFIT_OK && strcmp(plan->package_profile, "container-v1") == 0 &&
      package_input_digest_count != 0U) {
    status = CONFIT_ERR_SCHEMA;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "container package profile forbids firmware input digests");
  }
  if (status == CONFIT_OK && plan->dts_path != 0) {
    char *dtc_name = 0;
    status = confit_target_get_string(build, "dtc", 1, &dtc_name, path,
                                      diagnostic);
    if (status == CONFIT_OK && !confit_target_atom_valid(dtc_name)) {
      status = CONFIT_ERR_SCHEMA;
    }
    if (status == CONFIT_OK) {
      char resolved[CONFIT_TARGET_PATH_LIMIT];
      status = confit_host_resolve_executable(resolved, sizeof(resolved),
                                              dtc_name, diagnostic);
      if (status == CONFIT_OK) plan->dtc_path = confit_target_strdup(resolved);
      if (status == CONFIT_OK && plan->dtc_path == 0) status = CONFIT_ERR_INTERNAL;
    }
    free(dtc_name);
  } else if (status == CONFIT_OK && confit_toml_table_find(build, "dtc") != 0) {
    status = CONFIT_ERR_SCHEMA;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "dtc executable is forbidden without a dts input");
  }
  if (status == CONFIT_OK && compile_tuple_count == 0U) {
    status = CONFIT_ERR_SCHEMA;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "target compile tuple must not be empty");
  }
  for (index = 0U; status == CONFIT_OK && index < compile_tuple_count; ++index) {
    size_t prior;
    if (!confit_target_compile_atom_valid(compile_tuple[index])) {
      status = CONFIT_ERR_SCHEMA;
      confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                            "target compile tuple contains an unsafe argv atom");
    }
    for (prior = 0U; status == CONFIT_OK && prior < index; ++prior) {
      if (strcmp(compile_tuple[prior], compile_tuple[index]) == 0) {
        status = CONFIT_ERR_SCHEMA;
        confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                              "target compile tuple contains a duplicate atom");
      }
    }
  }
  if (status == CONFIT_OK &&
      kernel_role_count != 6U + (dts_relative != 0 ? 1U : 0U)) {
    status = CONFIT_ERR_SCHEMA;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "kernel artifact profile has an incomplete exact role set");
  }
  for (index = 0U; status == CONFIT_OK && index < kernel_role_count; ++index) {
    size_t prior;
    const char *separator;
    if (!confit_target_kernel_role_valid(kernel_roles[index])) {
      status = CONFIT_ERR_SCHEMA;
      confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                            "kernel artifact role is unknown or has an unsafe path");
      break;
    }
    separator = strchr(kernel_roles[index], '=');
    for (prior = 0U; prior < index; ++prior) {
      const char *prior_separator = strchr(kernel_roles[prior], '=');
      if ((size_t)(separator - kernel_roles[index]) ==
              (size_t)(prior_separator - kernel_roles[prior]) &&
          memcmp(kernel_roles[index], kernel_roles[prior],
                 (size_t)(separator - kernel_roles[index])) == 0) {
        status = CONFIT_ERR_SCHEMA;
        confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                              "kernel artifact role name is duplicated");
        break;
      }
    }
  }
  {
    const size_t required = 4U +
        (strcmp(plan->package_profile, "firmware-directory-v1") == 0 ? 4U : 0U);
    if (status == CONFIT_OK && image_role_count != required) {
      status = CONFIT_ERR_SCHEMA;
      confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                            "ImageGEN profile has an incomplete exact role set");
    }
  }
  for (index = 0U; status == CONFIT_OK && index < image_role_count; ++index) {
    size_t prior;
    const char *separator;
    if (!confit_target_image_role_valid(image_roles[index])) {
      status = CONFIT_ERR_SCHEMA;
      confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                            "ImageGEN artifact role is unknown or unsafe");
      break;
    }
    separator = strchr(image_roles[index], '=');
    for (prior = 0U; prior < index; ++prior) {
      const char *prior_separator = strchr(image_roles[prior], '=');
      if ((size_t)(separator - image_roles[index]) ==
              (size_t)(prior_separator - image_roles[prior]) &&
          memcmp(image_roles[index], image_roles[prior],
                 (size_t)(separator - image_roles[index])) == 0) {
        status = CONFIT_ERR_SCHEMA;
        confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                              "ImageGEN artifact role name is duplicated");
        break;
      }
    }
  }
  if (status == CONFIT_OK) {
    static const char *const base_roles[] = {
        "kernel_payload", "package", "manifest", "terminal"};
    size_t required_index;
    for (required_index = 0U; status == CONFIT_OK &&
                              required_index < sizeof(base_roles) / sizeof(base_roles[0]);
         ++required_index) {
      int found = 0;
      for (index = 0U; index < image_role_count; ++index) {
        const char *separator = strchr(image_roles[index], '=');
        if ((size_t)(separator - image_roles[index]) == strlen(base_roles[required_index]) &&
            memcmp(image_roles[index], base_roles[required_index],
                   strlen(base_roles[required_index])) == 0) found = 1;
      }
      if (!found) {
        status = CONFIT_ERR_SCHEMA;
        confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                              "ImageGEN profile omits a required base role");
      }
    }
  }
  if (status == CONFIT_OK && dts_relative != 0) {
    int found = 0;
    for (index = 0U; index < kernel_role_count; ++index) {
      if (strncmp(kernel_roles[index], "boot_dtb=", 9U) == 0) found = 1;
    }
    if (!found) status = CONFIT_ERR_SCHEMA;
  }
  if (status == CONFIT_OK &&
      strcmp(plan->package_profile, "firmware-directory-v1") == 0) {
    static const char *const firmware_roles[] = {
        "firmware_kernel", "firmware_config", "firmware_cmdline",
        "firmware_dtb"};
    size_t required_index;
    for (required_index = 0U; status == CONFIT_OK &&
                              required_index < sizeof(firmware_roles) / sizeof(firmware_roles[0]);
         ++required_index) {
      int found = 0;
      for (index = 0U; index < image_role_count; ++index) {
        const char *separator = strchr(image_roles[index], '=');
        if ((size_t)(separator - image_roles[index]) == strlen(firmware_roles[required_index]) &&
            memcmp(image_roles[index], firmware_roles[required_index],
                   strlen(firmware_roles[required_index])) == 0) found = 1;
      }
      if (!found) status = CONFIT_ERR_SCHEMA;
    }
    for (index = 0U; status == CONFIT_OK &&
                      index < package_input_digest_count; ++index) {
      const char *separator;
      size_t prior;
      if (!confit_target_package_digest_valid(package_input_digests[index])) {
        status = CONFIT_ERR_SCHEMA;
        confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                              "firmware input digest is not role=lowercase-sha256");
        break;
      }
      separator = strchr(package_input_digests[index], '=');
      for (prior = 0U; prior < index; ++prior) {
        const char *prior_separator = strchr(package_input_digests[prior], '=');
        if ((size_t)(separator - package_input_digests[index]) ==
                (size_t)(prior_separator - package_input_digests[prior]) &&
            memcmp(package_input_digests[index], package_input_digests[prior],
                   (size_t)(separator - package_input_digests[index])) == 0) {
          status = CONFIT_ERR_SCHEMA;
          confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                                "firmware input digest role is duplicated");
          break;
        }
      }
    }
    for (index = 0U; status == CONFIT_OK && index < 3U; ++index) {
      static const char *const digest_roles[] = {
          "firmware_config", "firmware_cmdline", "firmware_dtb"};
      size_t candidate;
      int found = 0;
      for (candidate = 0U; candidate < package_input_digest_count; ++candidate) {
        const char *separator = strchr(package_input_digests[candidate], '=');
        if ((size_t)(separator - package_input_digests[candidate]) ==
                strlen(digest_roles[index]) &&
            memcmp(package_input_digests[candidate], digest_roles[index],
                   strlen(digest_roles[index])) == 0) found = 1;
      }
      if (!found) status = CONFIT_ERR_SCHEMA;
    }
  }
  if (status == CONFIT_OK && strcmp(plan->world_artifact_profile, "none") == 0 &&
      (plan->world_boot_component != 0 || plan->world_artifact_entry != 0 ||
       plan->world_artifact_linker_script != 0 || artifact_role_count != 0U)) {
    status = CONFIT_ERR_SCHEMA;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "none World artifact profile must not publish roles");
  }
  if (status == CONFIT_OK && strcmp(plan->world_artifact_profile, "world-v1") == 0 &&
      (plan->world_boot_component == 0 || plan->world_artifact_entry == 0 ||
       plan->world_artifact_linker_script == 0 || artifact_role_count != 4U ||
       !confit_target_atom_valid(plan->world_boot_component) ||
       !confit_target_atom_valid(plan->world_artifact_entry))) {
    status = CONFIT_ERR_SCHEMA;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "World artifact profile has no boot service, entry, or four roles");
  }
  for (index = 0U; status == CONFIT_OK && index < artifact_role_count; ++index) {
    size_t prior;
    const char *separator;
    if (!confit_target_world_role_valid(artifact_roles[index])) {
      status = CONFIT_ERR_SCHEMA;
      confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                            "World artifact role is not role=relative-path");
      break;
    }
    separator = strchr(artifact_roles[index], '=');
    for (prior = 0U; prior < index; ++prior) {
      const char *prior_separator = strchr(artifact_roles[prior], '=');
      if ((size_t)(separator - artifact_roles[index]) ==
              (size_t)(prior_separator - artifact_roles[prior]) &&
          memcmp(artifact_roles[index], artifact_roles[prior],
                 (size_t)(separator - artifact_roles[index])) == 0) {
        status = CONFIT_ERR_SCHEMA;
        confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                              "World artifact role name is duplicated");
        break;
      }
    }
  }
  if (status == CONFIT_OK) {
    plan->kernel_artifact_roles = kernel_roles;
    plan->kernel_artifact_role_count = kernel_role_count;
    kernel_roles = 0;
    kernel_role_count = 0U;
    plan->image_artifact_roles = image_roles;
    plan->image_artifact_role_count = image_role_count;
    image_roles = 0;
    image_role_count = 0U;
    plan->package_input_digests = package_input_digests;
    plan->package_input_digest_count = package_input_digest_count;
    package_input_digests = 0;
    package_input_digest_count = 0U;
    plan->compile_tuple = compile_tuple;
    plan->compile_tuple_count = compile_tuple_count;
    compile_tuple = 0;
    compile_tuple_count = 0U;
    plan->world_artifact_roles = artifact_roles;
    plan->world_artifact_role_count = artifact_role_count;
    artifact_roles = 0;
    artifact_role_count = 0U;
  }

done:
  free(linker_relative);
  free(dts_relative);
  free(package_relative);
  free(world_artifact_linker_relative);
  if (compile_tuple != 0) {
    for (index = 0U; index < compile_tuple_count; ++index) free(compile_tuple[index]);
    free(compile_tuple);
  }
  if (artifact_roles != 0) {
    for (index = 0U; index < artifact_role_count; ++index) free(artifact_roles[index]);
    free(artifact_roles);
  }
  if (kernel_roles != 0) {
    for (index = 0U; index < kernel_role_count; ++index) free(kernel_roles[index]);
    free(kernel_roles);
  }
  if (image_roles != 0) {
    for (index = 0U; index < image_role_count; ++index) free(image_roles[index]);
    free(image_roles);
  }
  if (package_input_digests != 0) {
    for (index = 0U; index < package_input_digest_count; ++index) {
      free(package_input_digests[index]);
    }
    free(package_input_digests);
  }
  confit_toml_document_free(document);
  return status;
}

static ConfitStatus confit_target_parse_toolchain(
    const ConfitV2Project *project, ConfitTargetPlan *plan,
    ConfitDiagnostic *diagnostic) {
  static const char *const root_fields[] = {"toolchain"};
  static const char *const fields[] = {
      "id", "schema_version", "kind", "compiler", "archiver", "linker",
      "resource_headers", "target_triple", "sysroot", "link_emulation",
      "supported_isas", "supported_abis"};
  char relative[256];
  char path[CONFIT_TARGET_PATH_LIMIT];
  ConfitTomlDocument *document = 0;
  const ConfitTomlValue *root;
  const ConfitTomlValue *toolchain;
  const ConfitTomlValue *schema;
  const ConfitTomlValue *supported_isas;
  const ConfitTomlValue *supported_abis;
  int64_t version;
  char *id = 0;
  char *compiler = 0;
  char *archiver = 0;
  char *linker = 0;
  char *resource_headers = 0;
  char *sysroot_relative = 0;
  char resolved[CONFIT_TARGET_PATH_LIMIT];
  char resource_root[CONFIT_TARGET_PATH_LIMIT];
  char resource_include[CONFIT_TARGET_PATH_LIMIT];
  ConfitStatus status;

  if (snprintf(relative, sizeof(relative), "toolchains/%s.toml",
               plan->toolchain_id) < 0) {
    return CONFIT_ERR_INTERNAL;
  }
  status = confit_host_path_join(path, sizeof(path), project->config_root,
                                 relative, diagnostic);
  if (status == CONFIT_OK && !confit_host_file_exists(path)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, path, 0U, 0U,
                          "selected toolchain descriptor is missing");
    status = CONFIT_ERR_SCHEMA;
  }
  if (status == CONFIT_OK) status = confit_toml_parse_file(
      path, &document, diagnostic);
  if (status != CONFIT_OK) return status;
  root = confit_toml_document_root(document);
  toolchain = confit_toml_table_find(root, "toolchain");
  if (!confit_target_table_only(root, root_fields,
                                sizeof(root_fields) / sizeof(root_fields[0])) ||
      !confit_target_table_only(toolchain, fields,
                                sizeof(fields) / sizeof(fields[0]))) {
    status = CONFIT_ERR_SCHEMA;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "toolchain descriptor has an unknown field");
    goto done;
  }
  schema = confit_toml_table_find(toolchain, "schema_version");
  if (schema == 0 || !confit_toml_value_int64(schema, &version) ||
      version != 1) {
    status = CONFIT_ERR_SCHEMA;
    goto done;
  }
#define CONFIT_TOOLCHAIN_STRING(field, output)                                 \
  if (status == CONFIT_OK)                                                     \
    status = confit_target_get_string(toolchain, field, 1, output, path,       \
                                      diagnostic)
  status = CONFIT_OK;
  CONFIT_TOOLCHAIN_STRING("id", &id);
  CONFIT_TOOLCHAIN_STRING("kind", &plan->toolchain_kind);
  CONFIT_TOOLCHAIN_STRING("compiler", &compiler);
  CONFIT_TOOLCHAIN_STRING("archiver", &archiver);
  CONFIT_TOOLCHAIN_STRING("linker", &linker);
  CONFIT_TOOLCHAIN_STRING("resource_headers", &resource_headers);
  CONFIT_TOOLCHAIN_STRING("target_triple", &plan->target_triple);
  CONFIT_TOOLCHAIN_STRING("sysroot", &sysroot_relative);
  CONFIT_TOOLCHAIN_STRING("link_emulation", &plan->link_emulation);
#undef CONFIT_TOOLCHAIN_STRING
  supported_isas = confit_toml_table_find(toolchain, "supported_isas");
  supported_abis = confit_toml_table_find(toolchain, "supported_abis");
  if (status == CONFIT_OK &&
      (strcmp(id, plan->toolchain_id) != 0 ||
       strcmp(plan->toolchain_kind, "clang-lld-v1") != 0 ||
       strcmp(resource_headers, "clang-print-resource-dir") != 0 ||
       !confit_target_string_list_contains(supported_isas, plan->isa) ||
       !confit_target_string_list_contains(supported_abis, plan->abi) ||
       !confit_target_atom_valid(compiler) ||
       !confit_target_atom_valid(archiver) ||
       !confit_target_atom_valid(linker) ||
       !confit_target_atom_valid(plan->target_triple) ||
       !confit_target_atom_valid(plan->link_emulation))) {
    status = CONFIT_ERR_CONFLICT;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "toolchain capability contradicts selected target");
  }
  if (status == CONFIT_OK) {
    status = confit_host_resolve_executable(resolved, sizeof(resolved), compiler,
                                            diagnostic);
    if (status == CONFIT_OK) plan->compiler_path = confit_target_strdup(resolved);
    if (status == CONFIT_OK && plan->compiler_path == 0) status = CONFIT_ERR_INTERNAL;
  }
  if (status == CONFIT_OK) {
    status = confit_host_resolve_executable(resolved, sizeof(resolved), archiver,
                                            diagnostic);
    if (status == CONFIT_OK) plan->archiver_path = confit_target_strdup(resolved);
    if (status == CONFIT_OK && plan->archiver_path == 0) status = CONFIT_ERR_INTERNAL;
  }
  if (status == CONFIT_OK) {
    status = confit_host_resolve_executable(resolved, sizeof(resolved), linker,
                                            diagnostic);
    if (status == CONFIT_OK) plan->linker_path = confit_target_strdup(resolved);
    if (status == CONFIT_OK && plan->linker_path == 0) status = CONFIT_ERR_INTERNAL;
  }
  if (status == CONFIT_OK && strcmp(sysroot_relative, "none") != 0) {
    status = confit_target_repo_path(project, sysroot_relative, 1,
                                     &plan->sysroot_path, diagnostic);
  }
  if (status == CONFIT_OK && plan->compiler_path != 0) {
    status = confit_host_capture_one_argument(
        resource_root, sizeof(resource_root), plan->compiler_path,
        "-print-resource-dir", diagnostic);
    if (status == CONFIT_OK) status = confit_host_path_join(
        resource_include, sizeof(resource_include), resource_root, "include",
        diagnostic);
    if (status == CONFIT_OK) status = confit_host_path_canonicalize(
        resolved, sizeof(resolved), resource_include, diagnostic);
    if (status == CONFIT_OK) {
      char stdatomic_path[CONFIT_TARGET_PATH_LIMIT];
      status = confit_host_path_join(stdatomic_path, sizeof(stdatomic_path),
                                     resolved, "stdatomic.h", diagnostic);
      if (status == CONFIT_OK && !confit_host_file_exists(stdatomic_path)) {
        status = CONFIT_ERR_UNSUPPORTED;
        confit_diagnostic_set(diagnostic, status, stdatomic_path, 0U, 0U,
                              "compiler resource headers omit stdatomic.h");
      }
    }
    if (status == CONFIT_OK) {
      plan->resource_include_path = confit_target_strdup(resolved);
      if (plan->resource_include_path == 0) status = CONFIT_ERR_INTERNAL;
    }
  }
  if (status == CONFIT_OK) {
    plan->toolchain_descriptor_path = confit_target_strdup(path);
    if (plan->toolchain_descriptor_path == 0) status = CONFIT_ERR_INTERNAL;
  }

done:
  free(id);
  free(compiler);
  free(archiver);
  free(linker);
  free(resource_headers);
  free(sysroot_relative);
  confit_toml_document_free(document);
  return status;
}

static ConfitStatus confit_target_measure_machine_identity(
    ConfitTargetPlan *plan, ConfitDiagnostic *diagnostic) {
  static const char prefix[] = "QEMU emulator version ";
  char discovered[CONFIT_TARGET_PATH_LIMIT];
  char canonical[CONFIT_TARGET_PATH_LIMIT];
  char banner[512];
  char digest[65];
  const char *version;
  ConfitStatus status;

  if (plan->machine_executable == 0) return CONFIT_OK;
  status = confit_host_resolve_executable(discovered, sizeof(discovered),
                                          plan->machine_executable, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_host_path_canonicalize(canonical, sizeof(canonical),
                                           discovered, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v4_sha256_file(canonical, digest, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_capture_first_line_argument(
        banner, sizeof(banner), canonical, "--version", diagnostic);
  }
  if (status != CONFIT_OK) return status;
  if (strncmp(banner, prefix, sizeof(prefix) - 1U) != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, canonical, 0U, 0U,
                          "machine executable returned an unknown version banner");
    return CONFIT_ERR_SCHEMA;
  }
  version = banner + sizeof(prefix) - 1U;
  if (!confit_target_atom_valid(version)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, canonical, 0U, 0U,
                          "machine executable version is not a bounded atom");
    return CONFIT_ERR_SCHEMA;
  }
  plan->machine_executable_path = confit_target_strdup(canonical);
  plan->machine_executable_sha256 = confit_target_strdup(digest);
  plan->machine_executable_version = confit_target_strdup(version);
  if (plan->machine_executable_path == 0 ||
      plan->machine_executable_sha256 == 0 ||
      plan->machine_executable_version == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  return CONFIT_OK;
}

static int confit_target_extract_numeric_version(const char *banner,
                                                 char output[64]) {
  size_t start = 0U;
  size_t length = 0U;
  while (banner[start] != '\0' &&
         (banner[start] < '0' || banner[start] > '9')) ++start;
  while (banner[start + length] != '\0' &&
         ((banner[start + length] >= '0' && banner[start + length] <= '9') ||
          banner[start + length] == '.')) {
    if (length + 1U >= 64U) return 0;
    ++length;
  }
  if (length == 0U || banner[start + length - 1U] == '.') return 0;
  memcpy(output, banner + start, length);
  output[length] = '\0';
  return confit_target_atom_valid(output);
}

static ConfitStatus confit_target_measure_tool_identity(
    const char *path, char **out_digest, char **out_version,
    ConfitDiagnostic *diagnostic) {
  char digest[65];
  char banner[512];
  char version[64];
  ConfitStatus status;
  if (path == 0 || out_digest == 0 || out_version == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  status = confit_v4_sha256_file(path, digest, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_host_capture_first_line_argument(
        banner, sizeof(banner), path, "--version", diagnostic);
  }
  if (status == CONFIT_OK && !confit_target_extract_numeric_version(banner, version)) {
    status = CONFIT_ERR_SCHEMA;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "tool returned no bounded numeric version token");
  }
  if (status == CONFIT_OK) {
    *out_digest = confit_target_strdup(digest);
    *out_version = confit_target_strdup(version);
    if (*out_digest == 0 || *out_version == 0) status = CONFIT_ERR_INTERNAL;
  }
  return status;
}

ConfitStatus confit_target_plan_load(const ConfitV2Project *project,
                                     const char *target_id,
                                     ConfitTargetPlan *out_plan,
                                     ConfitDiagnostic *diagnostic) {
  ConfitStatus status;
  if (project == 0 || target_id == 0 || out_plan == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  memset(out_plan, 0, sizeof(*out_plan));
  status = confit_target_parse_build(project, target_id, out_plan, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_target_parse_toolchain(project, out_plan, diagnostic);
  }
  if (status == CONFIT_OK) status = confit_target_measure_tool_identity(
      out_plan->compiler_path, &out_plan->compiler_sha256,
      &out_plan->compiler_version, diagnostic);
  if (status == CONFIT_OK) status = confit_target_measure_tool_identity(
      out_plan->archiver_path, &out_plan->archiver_sha256,
      &out_plan->archiver_version, diagnostic);
  if (status == CONFIT_OK) status = confit_target_measure_tool_identity(
      out_plan->linker_path, &out_plan->linker_sha256,
      &out_plan->linker_version, diagnostic);
  if (status == CONFIT_OK && out_plan->dtc_path != 0) {
    status = confit_target_measure_tool_identity(
        out_plan->dtc_path, &out_plan->dtc_sha256,
        &out_plan->dtc_version, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_target_measure_machine_identity(out_plan, diagnostic);
  }
  if (status != CONFIT_OK) {
    /* Parser와 host probe의 path는 document buffer, local path 또는 plan-owned
     * executable일 수 있다. Public return 전에 plan을 지우므로 borrowed diagnostic이
     * dangling pointer가 되지 않도록 stable API boundary label로 다시 결속한다. */
    const char *message = diagnostic != 0 && diagnostic->message != 0
                              ? diagnostic->message
                              : "failed to load the sealed target plan";
    confit_target_plan_clear(out_plan);
    confit_diagnostic_set(diagnostic, status, "target-plan", 0U, 0U, message);
  }
  return status;
}

void confit_target_plan_clear(ConfitTargetPlan *plan) {
  size_t index;
  if (plan == 0) return;
#define CONFIT_TARGET_FREE(member) free(plan->member)
  CONFIT_TARGET_FREE(target_id);
  CONFIT_TARGET_FREE(isa);
  CONFIT_TARGET_FREE(abi);
  CONFIT_TARGET_FREE(cpu_profile);
  CONFIT_TARGET_FREE(entry_profile);
  CONFIT_TARGET_FREE(toolchain_id);
  CONFIT_TARGET_FREE(toolchain_kind);
  CONFIT_TARGET_FREE(target_triple);
  CONFIT_TARGET_FREE(compiler_path);
  CONFIT_TARGET_FREE(compiler_sha256);
  CONFIT_TARGET_FREE(compiler_version);
  CONFIT_TARGET_FREE(archiver_path);
  CONFIT_TARGET_FREE(archiver_sha256);
  CONFIT_TARGET_FREE(archiver_version);
  CONFIT_TARGET_FREE(linker_path);
  CONFIT_TARGET_FREE(linker_sha256);
  CONFIT_TARGET_FREE(linker_version);
  CONFIT_TARGET_FREE(resource_include_path);
  CONFIT_TARGET_FREE(sysroot_path);
  CONFIT_TARGET_FREE(link_emulation);
  CONFIT_TARGET_FREE(linker_script);
  CONFIT_TARGET_FREE(image_artifact_profile);
  CONFIT_TARGET_FREE(package_profile);
  CONFIT_TARGET_FREE(machine_profile);
  CONFIT_TARGET_FREE(machine_runner);
  CONFIT_TARGET_FREE(machine_architecture);
  CONFIT_TARGET_FREE(machine_executable);
  CONFIT_TARGET_FREE(machine_executable_path);
  CONFIT_TARGET_FREE(machine_executable_sha256);
  CONFIT_TARGET_FREE(machine_executable_version);
  CONFIT_TARGET_FREE(machine_trust_profile);
  CONFIT_TARGET_FREE(machine_resource_identity);
  CONFIT_TARGET_FREE(machine_evidence_transport);
  CONFIT_TARGET_FREE(machine_evidence_protocol);
  CONFIT_TARGET_FREE(machine_name);
  CONFIT_TARGET_FREE(machine_cpu);
  CONFIT_TARGET_FREE(machine_serial);
  CONFIT_TARGET_FREE(machine_artifact);
  CONFIT_TARGET_FREE(expected_component);
  CONFIT_TARGET_FREE(expected_capability);
  CONFIT_TARGET_FREE(output_stem);
  CONFIT_TARGET_FREE(required_profile);
  CONFIT_TARGET_FREE(dts_path);
  CONFIT_TARGET_FREE(dtc_path);
  CONFIT_TARGET_FREE(dtc_sha256);
  CONFIT_TARGET_FREE(dtc_version);
  CONFIT_TARGET_FREE(package_source);
  CONFIT_TARGET_FREE(kernel_artifact_profile);
  CONFIT_TARGET_FREE(world_artifact_profile);
  CONFIT_TARGET_FREE(world_boot_component);
  CONFIT_TARGET_FREE(world_artifact_entry);
  CONFIT_TARGET_FREE(world_artifact_linker_script);
  CONFIT_TARGET_FREE(support_provider_owner);
  CONFIT_TARGET_FREE(support_consumer_owner);
  CONFIT_TARGET_FREE(support_role);
  CONFIT_TARGET_FREE(support_facade_include_root);
  CONFIT_TARGET_FREE(support_required_kapi);
  CONFIT_TARGET_FREE(target_descriptor_path);
  CONFIT_TARGET_FREE(toolchain_descriptor_path);
#undef CONFIT_TARGET_FREE
  for (index = 0U; index < plan->compile_tuple_count; ++index) {
    free(plan->compile_tuple[index]);
  }
  free(plan->compile_tuple);
  for (index = 0U; index < plan->kernel_artifact_role_count; ++index) {
    free(plan->kernel_artifact_roles[index]);
  }
  free(plan->kernel_artifact_roles);
  for (index = 0U; index < plan->image_artifact_role_count; ++index) {
    free(plan->image_artifact_roles[index]);
  }
  free(plan->image_artifact_roles);
  for (index = 0U; index < plan->package_input_digest_count; ++index) {
    free(plan->package_input_digests[index]);
  }
  free(plan->package_input_digests);
  for (index = 0U; index < plan->world_artifact_role_count; ++index) {
    free(plan->world_artifact_roles[index]);
  }
  free(plan->world_artifact_roles);
  memset(plan, 0, sizeof(*plan));
}

ConfitStatus confit_target_plan_validate_selection(
    const ConfitTargetPlan *plan, const ConfitComponentCatalog *catalog,
    const ConfitComponentClosure *closure, ConfitDiagnostic *diagnostic) {
  const ConfitComponent *component;
  const ConfitComponent *provider = 0;
  const ConfitComponent *support_provider = 0;
  const ConfitComponent *kapi_provider;
  size_t provider_count;
  size_t index;
  int selected = 0;
  int support_selected = 0;
  int support_required = 0;
  if (plan == 0 || catalog == 0 || closure == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  component = confit_component_catalog_find(catalog, plan->expected_component);
  provider_count = confit_component_catalog_find_feature_providers(
      catalog, plan->expected_capability, &provider, 1U);
  for (index = 0U; index < closure->component_count; ++index) {
    if (strcmp(closure->ordered[index]->id, plan->expected_component) == 0) {
      selected = 1;
      break;
    }
  }
  if (component == 0 || provider_count != 1U || provider != component ||
      !selected) {
    confit_diagnostic_set(
        diagnostic, CONFIT_ERR_CONFLICT, "target-plan.selection", 0U, 0U,
        "target expected component/capability is not the selected provider");
    return CONFIT_ERR_CONFLICT;
  }
  for (index = 0U; index < closure->component_count; ++index) {
    const ConfitComponent *candidate = closure->ordered[index];
    size_t requirement;
    if (strcmp(candidate->owner, plan->support_provider_owner) == 0) {
      if (support_provider != 0) {
        confit_diagnostic_set(
            diagnostic, CONFIT_ERR_CONFLICT, "target-plan.support", 0U, 0U,
            "target support owner identifies multiple selected components");
        return CONFIT_ERR_CONFLICT;
      }
      support_provider = candidate;
      support_selected = 1;
    }
    if (candidate == component) {
      for (requirement = 0U;
           requirement < candidate->kapi_requirement_count; ++requirement) {
        if (strcmp(candidate->kapi_requires[requirement],
                   plan->support_required_kapi) == 0) {
          support_required = 1;
          break;
        }
      }
    }
  }
  kapi_provider = confit_component_closure_find_kapi_provider(
      closure, plan->support_required_kapi);
  if (!support_selected || support_provider == 0 ||
      support_provider == component || kapi_provider != support_provider ||
      strcmp(component->owner, plan->support_consumer_owner) != 0 ||
      strcmp(plan->support_consumer_owner, plan->expected_component) != 0 ||
      !support_required) {
    confit_diagnostic_set(
        diagnostic, CONFIT_ERR_CONFLICT, "target-plan.support", 0U, 0U,
        "target support facade is not the selected consumer-to-KAPI-provider edge");
    return CONFIT_ERR_CONFLICT;
  }
  if (strcmp(plan->world_artifact_profile, "world-v1") == 0) {
    const ConfitComponent *role_component = confit_component_catalog_find(
        catalog, plan->world_boot_component);
    size_t selected_index;
    int role_selected = 0;
    for (selected_index = 0U; role_component != 0 &&
         selected_index < closure->component_count; ++selected_index) {
      if (closure->ordered[selected_index] == role_component) {
        role_selected = 1;
        break;
      }
    }
    if (!role_selected ||
        role_component->kind != CONFIT_COMPONENT_KIND_WORLD_SERVICE) {
      confit_diagnostic_set(
          diagnostic, CONFIT_ERR_CONFLICT, "target-plan.world-artifact", 0U, 0U,
          "World boot component is not one selected World service");
      return CONFIT_ERR_CONFLICT;
    }
  }
  return CONFIT_OK;
}
