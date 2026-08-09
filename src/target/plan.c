#include "confit/target_plan.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/host.h"
#include "confit/generator_v2.h"
#include "confit/parser_v2.h"

enum {
  CONFIT_TARGET_PATH_LIMIT = 4096,
  CONFIT_TARGET_TEXT_LIMIT = 1024,
  CONFIT_TARGET_INCLUDE_LIMIT = 32,
  CONFIT_TARGET_IMAGE_LIMIT_MAX = 1024 * 1024 * 1024,
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

static int confit_target_table_only(const ConfitV2TomlValue *table,
                                    const char *const *fields,
                                    size_t field_count) {
  size_t index;
  if (table == 0 ||
      confit_v2_toml_value_type(table) != CONFIT_V2_TOML_VALUE_TABLE) {
    return 0;
  }
  for (index = 0U; index < confit_v2_toml_table_size(table); ++index) {
    const char *key = confit_v2_toml_table_key_at(table, index);
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
    const ConfitV2TomlValue *table, const char *key, int required,
    char **out, const char *path, ConfitDiagnostic *diagnostic) {
  const ConfitV2TomlValue *value = confit_v2_toml_table_find(table, key);
  const char *text;
  size_t size;
  *out = 0;
  if (value == 0 && !required) return CONFIT_OK;
  if (value == 0 || !confit_v2_toml_value_string(value, &text, &size) ||
      size == 0U || size > CONFIT_TARGET_TEXT_LIMIT) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, path,
                          confit_v2_toml_value_line(table),
                          confit_v2_toml_value_column(table),
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
    const ConfitV2TomlValue *table, const char *key, char ***out_items,
    size_t *out_count, const char *path, ConfitDiagnostic *diagnostic) {
  const ConfitV2TomlValue *value = confit_v2_toml_table_find(table, key);
  size_t index;
  *out_items = 0;
  *out_count = 0U;
  if (value == 0 ||
      confit_v2_toml_value_type(value) != CONFIT_V2_TOML_VALUE_ARRAY ||
      confit_v2_toml_array_size(value) > CONFIT_TARGET_INCLUDE_LIMIT) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, path,
                          confit_v2_toml_value_line(table),
                          confit_v2_toml_value_column(table),
                          "target descriptor string list is missing or invalid");
    return CONFIT_ERR_SCHEMA;
  }
  if (confit_v2_toml_array_size(value) != 0U) {
    *out_items = (char **)calloc(confit_v2_toml_array_size(value),
                                 sizeof(**out_items));
    if (*out_items == 0) return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < confit_v2_toml_array_size(value); ++index) {
    const ConfitV2TomlValue *item = confit_v2_toml_array_at(value, index);
    const char *text;
    size_t size;
    if (!confit_v2_toml_value_string(item, &text, &size) || size == 0U ||
        size > CONFIT_TARGET_TEXT_LIMIT) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, path,
                            confit_v2_toml_value_line(item),
                            confit_v2_toml_value_column(item),
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

static int confit_target_string_list_contains(const ConfitV2TomlValue *array,
                                              const char *expected) {
  size_t index;
  if (array == 0 ||
      confit_v2_toml_value_type(array) != CONFIT_V2_TOML_VALUE_ARRAY) {
    return 0;
  }
  for (index = 0U; index < confit_v2_toml_array_size(array); ++index) {
    const ConfitV2TomlValue *item = confit_v2_toml_array_at(array, index);
    const char *text;
    size_t size;
    if (confit_v2_toml_value_string(item, &text, &size) &&
        strlen(expected) == size && memcmp(text, expected, size) == 0) {
      return 1;
    }
  }
  return 0;
}

static int confit_target_value_matches(const ConfitV2TomlValue *values,
                                       const char *key,
                                       const char *expected) {
  const ConfitV2TomlValue *value = confit_v2_toml_table_find(values, key);
  const char *text;
  size_t size;
  return value != 0 && confit_v2_toml_value_string(value, &text, &size) &&
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
  static const char *const root_fields[] = {"target", "values", "build", "machine"};
  static const char *const build_fields[] = {
      "isa", "abi", "cpu_profile", "entry_profile", "toolchain",
      "linker_script", "image_kind", "package_profile", "machine_profile",
      "expected_component", "expected_capability", "output_stem",
      "required_profile", "private_includes", "max_image_bytes", "dts",
      "dtc", "package_source", "user_artifact_profile"};
  static const char *const machine_fields[] = {
      "runner", "architecture", "executable", "machine", "cpu",
      "memory_mib", "serial", "artifact"};
  char path[CONFIT_TARGET_PATH_LIMIT];
  ConfitV2TomlDocument *document = 0;
  const ConfitV2TomlValue *root;
  const ConfitV2TomlValue *target;
  const ConfitV2TomlValue *values;
  const ConfitV2TomlValue *build;
  const ConfitV2TomlValue *machine;
  const ConfitV2TomlValue *name;
  const ConfitV2TomlValue *schema;
  const ConfitV2TomlValue *capabilities;
  const ConfitV2TomlValue *max_image;
  const ConfitV2TomlValue *machine_memory;
  const char *name_text;
  size_t name_size;
  int64_t schema_version;
  int64_t max_image_bytes;
  int64_t machine_memory_mib = 0;
  char *linker_relative = 0;
  char *dts_relative = 0;
  char *package_relative = 0;
  char **private_relatives = 0;
  size_t private_count = 0U;
  size_t index;
  ConfitStatus status;

  status = confit_target_find_descriptor(project, target_id, path,
                                         sizeof(path), diagnostic);
  if (status == CONFIT_OK) status = confit_v2_toml_parse_file(
      path, &document, diagnostic);
  if (status != CONFIT_OK) return status;
  root = confit_v2_toml_document_root(document);
  target = confit_v2_toml_table_find(root, "target");
  values = confit_v2_toml_table_find(root, "values");
  build = confit_v2_toml_table_find(root, "build");
  machine = confit_v2_toml_table_find(root, "machine");
  if (!confit_target_table_only(root, root_fields,
                                sizeof(root_fields) / sizeof(root_fields[0])) ||
      target == 0 || values == 0 ||
      confit_v2_toml_value_type(values) != CONFIT_V2_TOML_VALUE_TABLE ||
      !confit_target_table_only(build, build_fields,
                                sizeof(build_fields) / sizeof(build_fields[0])) ||
      (machine != 0 &&
       !confit_target_table_only(machine, machine_fields,
                                 sizeof(machine_fields) / sizeof(machine_fields[0])))) {
    status = CONFIT_ERR_SCHEMA;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "target descriptor has unknown or missing tables");
    goto done;
  }
  name = confit_v2_toml_table_find(target, "name");
  schema = confit_v2_toml_table_find(target, "schema_version");
  capabilities = confit_v2_toml_table_find(target, "required_capabilities");
  if (name == 0 || schema == 0 ||
      !confit_v2_toml_value_string(name, &name_text, &name_size) ||
      strlen(target_id) != name_size ||
      memcmp(name_text, target_id, name_size) != 0 ||
      !confit_v2_toml_value_int64(schema, &schema_version) ||
      schema_version != 2) {
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
  CONFIT_TARGET_BUILD_STRING("image_kind", image_kind);
  CONFIT_TARGET_BUILD_STRING("package_profile", package_profile);
  CONFIT_TARGET_BUILD_STRING("machine_profile", machine_profile);
  CONFIT_TARGET_BUILD_STRING("expected_component", expected_component);
  CONFIT_TARGET_BUILD_STRING("expected_capability", expected_capability);
  CONFIT_TARGET_BUILD_STRING("output_stem", output_stem);
  CONFIT_TARGET_BUILD_STRING("required_profile", required_profile);
  CONFIT_TARGET_BUILD_STRING("user_artifact_profile", user_artifact_profile);
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
    machine_memory = confit_v2_toml_table_find(machine, "memory_mib");
    if (status == CONFIT_OK &&
        (machine_memory == 0 ||
         !confit_v2_toml_value_int64(machine_memory, &machine_memory_mib) ||
         machine_memory_mib < 16 || machine_memory_mib > 65536)) {
      status = CONFIT_ERR_SCHEMA;
    }
    if (status == CONFIT_OK) {
      plan->machine_memory_mib = (size_t)machine_memory_mib;
    }
  }
  if (status == CONFIT_OK) status = confit_target_get_string(
      build, "linker_script", 1, &linker_relative, path, diagnostic);
  if (status == CONFIT_OK) status = confit_target_get_string(
      build, "dts", 0, &dts_relative, path, diagnostic);
  if (status == CONFIT_OK) status = confit_target_get_string(
      build, "package_source", 0, &package_relative, path, diagnostic);
  if (status == CONFIT_OK) status = confit_target_get_string_list(
      build, "private_includes", &private_relatives, &private_count, path,
      diagnostic);
#undef CONFIT_TARGET_BUILD_STRING
  max_image = confit_v2_toml_table_find(build, "max_image_bytes");
  if (status == CONFIT_OK &&
      (max_image == 0 || !confit_v2_toml_value_int64(max_image, &max_image_bytes) ||
       max_image_bytes <= 0 ||
       max_image_bytes > CONFIT_TARGET_IMAGE_LIMIT_MAX)) {
    status = CONFIT_ERR_SCHEMA;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "target max_image_bytes is outside the closed limit");
  }
  if (status == CONFIT_OK) plan->max_image_bytes = (size_t)max_image_bytes;
  if (status == CONFIT_OK &&
      (!confit_target_atom_valid(plan->isa) ||
       !confit_target_atom_valid(plan->abi) ||
       !confit_target_atom_valid(plan->cpu_profile) ||
       !confit_target_atom_valid(plan->entry_profile) ||
       !confit_target_atom_valid(plan->toolchain_id) ||
       !confit_target_atom_valid(plan->image_kind) ||
       !confit_target_atom_valid(plan->package_profile) ||
       !confit_target_atom_valid(plan->machine_profile) ||
       !confit_target_atom_valid(plan->expected_component) ||
       !confit_target_atom_valid(plan->expected_capability) ||
       !confit_target_atom_valid(plan->output_stem) ||
       !confit_target_atom_valid(plan->required_profile) ||
       !confit_target_atom_valid(plan->user_artifact_profile) ||
       (machine != 0 &&
        (!confit_target_atom_valid(plan->machine_runner) ||
         !confit_target_atom_valid(plan->machine_architecture) ||
         !confit_target_atom_valid(plan->machine_executable) ||
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
      (!confit_target_value_matches(values, "parus.target.isa", plan->isa) ||
       !confit_target_value_matches(values, "parus.target.cpu", plan->cpu_profile) ||
       !confit_target_value_matches(values, "parus.target.entry_abi", plan->entry_profile) ||
       !confit_target_value_matches(values, "parus.toolchain.id", plan->toolchain_id) ||
       !confit_target_value_matches(values, "parus.target.output_name", plan->output_stem) ||
       !confit_target_string_list_contains(capabilities,
                                           plan->expected_capability))) {
    status = CONFIT_ERR_CONFLICT;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "target build tuple contradicts typed target selection");
  }
  if (status == CONFIT_OK &&
      (strcmp(plan->image_kind, "elf-flat-v1") != 0 ||
       (strcmp(plan->package_profile, "manifest-v1") != 0 &&
        strcmp(plan->package_profile, "rpi5-firmware-v1") != 0) ||
       strcmp(plan->required_profile, "release") != 0 ||
       (strcmp(plan->user_artifact_profile, "none") != 0 &&
        strcmp(plan->user_artifact_profile, "initial-c-v1") != 0))) {
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
  if (status == CONFIT_OK && strcmp(plan->package_profile,
                                    "rpi5-firmware-v1") == 0 &&
      plan->package_source == 0) {
    status = CONFIT_ERR_SCHEMA;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "firmware package profile requires package_source");
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
  } else if (status == CONFIT_OK && confit_v2_toml_table_find(build, "dtc") != 0) {
    status = CONFIT_ERR_SCHEMA;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "dtc executable is forbidden without a dts input");
  }
  if (status == CONFIT_OK && private_count != 0U) {
    plan->private_include_paths =
        (char **)calloc(private_count, sizeof(*plan->private_include_paths));
    if (plan->private_include_paths == 0) status = CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; status == CONFIT_OK && index < private_count; ++index) {
    status = confit_target_repo_path(project, private_relatives[index], 1,
                                     &plan->private_include_paths[index],
                                     diagnostic);
    if (status == CONFIT_OK) plan->private_include_count += 1U;
  }

done:
  free(linker_relative);
  free(dts_relative);
  free(package_relative);
  if (private_relatives != 0) {
    for (index = 0U; index < private_count; ++index) free(private_relatives[index]);
    free(private_relatives);
  }
  confit_v2_toml_document_free(document);
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
  ConfitV2TomlDocument *document = 0;
  const ConfitV2TomlValue *root;
  const ConfitV2TomlValue *toolchain;
  const ConfitV2TomlValue *schema;
  const ConfitV2TomlValue *supported_isas;
  const ConfitV2TomlValue *supported_abis;
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
  if (status == CONFIT_OK) status = confit_v2_toml_parse_file(
      path, &document, diagnostic);
  if (status != CONFIT_OK) return status;
  root = confit_v2_toml_document_root(document);
  toolchain = confit_v2_toml_table_find(root, "toolchain");
  if (!confit_target_table_only(root, root_fields,
                                sizeof(root_fields) / sizeof(root_fields[0])) ||
      !confit_target_table_only(toolchain, fields,
                                sizeof(fields) / sizeof(fields[0]))) {
    status = CONFIT_ERR_SCHEMA;
    confit_diagnostic_set(diagnostic, status, path, 0U, 0U,
                          "toolchain descriptor has an unknown field");
    goto done;
  }
  schema = confit_v2_toml_table_find(toolchain, "schema_version");
  if (schema == 0 || !confit_v2_toml_value_int64(schema, &version) ||
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
  supported_isas = confit_v2_toml_table_find(toolchain, "supported_isas");
  supported_abis = confit_v2_toml_table_find(toolchain, "supported_abis");
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
  confit_v2_toml_document_free(document);
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
  CONFIT_TARGET_FREE(archiver_path);
  CONFIT_TARGET_FREE(linker_path);
  CONFIT_TARGET_FREE(resource_include_path);
  CONFIT_TARGET_FREE(sysroot_path);
  CONFIT_TARGET_FREE(link_emulation);
  CONFIT_TARGET_FREE(linker_script);
  CONFIT_TARGET_FREE(image_kind);
  CONFIT_TARGET_FREE(package_profile);
  CONFIT_TARGET_FREE(machine_profile);
  CONFIT_TARGET_FREE(machine_runner);
  CONFIT_TARGET_FREE(machine_architecture);
  CONFIT_TARGET_FREE(machine_executable);
  CONFIT_TARGET_FREE(machine_executable_path);
  CONFIT_TARGET_FREE(machine_executable_sha256);
  CONFIT_TARGET_FREE(machine_executable_version);
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
  CONFIT_TARGET_FREE(package_source);
  CONFIT_TARGET_FREE(user_artifact_profile);
  CONFIT_TARGET_FREE(target_descriptor_path);
  CONFIT_TARGET_FREE(toolchain_descriptor_path);
#undef CONFIT_TARGET_FREE
  for (index = 0U; index < plan->private_include_count; ++index) {
    free(plan->private_include_paths[index]);
  }
  free(plan->private_include_paths);
  memset(plan, 0, sizeof(*plan));
}

ConfitStatus confit_target_plan_validate_selection(
    const ConfitTargetPlan *plan, const ConfitComponentCatalog *catalog,
    const ConfitComponentClosure *closure, ConfitDiagnostic *diagnostic) {
  const ConfitComponent *component;
  const ConfitComponent *provider;
  size_t index;
  int selected = 0;
  if (plan == 0 || catalog == 0 || closure == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  component = confit_component_catalog_find(catalog, plan->expected_component);
  provider = confit_component_catalog_find_capability_provider(
      catalog, plan->expected_capability);
  for (index = 0U; index < closure->component_count; ++index) {
    if (strcmp(closure->ordered[index]->id, plan->expected_component) == 0) {
      selected = 1;
      break;
    }
  }
  if (component == 0 || component->kind != CONFIT_COMPONENT_KIND_TARGET_IMAGE ||
      provider != component || !selected) {
    confit_diagnostic_set(
        diagnostic, CONFIT_ERR_CONFLICT, "target-plan.selection", 0U, 0U,
        "target expected component/capability is not the selected provider");
    return CONFIT_ERR_CONFLICT;
  }
  return CONFIT_OK;
}
