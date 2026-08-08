#include "confit/component_catalog.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/host.h"
#include "confit/parser_v2.h"

#define CONFIT_COMPONENT_MAX_DEPTH 32U
#define CONFIT_COMPONENT_MAX_ROOTS 32U
#define CONFIT_COMPONENT_MAX_COUNT 512U
#define CONFIT_COMPONENT_MAX_FILE_BYTES (64U * 1024U)
#define CONFIT_COMPONENT_MAX_STRING_BYTES 192U
#define CONFIT_COMPONENT_MAX_LIST_ITEMS 128U
#define CONFIT_COMPONENT_MAX_TOTAL_EDGES 4096U

static const char kManifestName[] = "component.toml";

static char *confit_component_strdup(const char *text) {
  const size_t size = text != 0 ? strlen(text) : 0U;
  char *copy;
  if (text == 0) return 0;
  copy = (char *)malloc(size + 1U);
  if (copy != 0) memcpy(copy, text, size + 1U);
  return copy;
}

static void confit_component_string_list_clear(char **items, size_t count) {
  size_t index;
  for (index = 0U; index < count; ++index) free(items[index]);
  free(items);
}

static void confit_component_clear(ConfitComponent *component) {
  if (component == 0) return;
  free(component->id);
  free(component->manifest_path);
  free(component->makefile_path);
  confit_component_string_list_clear(component->enabled_if,
                                     component->enabled_if_count);
  confit_component_string_list_clear(component->component_dependencies,
                                     component->component_dependency_count);
  confit_component_string_list_clear(component->kapi_requires,
                                     component->kapi_requirement_count);
  confit_component_string_list_clear(component->capabilities,
                                     component->capability_count);
  confit_component_string_list_clear(component->kapi_provides,
                                     component->kapi_provide_count);
  memset(component, 0, sizeof(*component));
}

void confit_component_catalog_clear(ConfitComponentCatalog *catalog) {
  size_t index;
  if (catalog == 0) return;
  for (index = 0U; index < catalog->component_count; ++index) {
    confit_component_clear(&catalog->components[index]);
  }
  free(catalog->components);
  free(catalog->project_root);
  memset(catalog, 0, sizeof(*catalog));
}

void confit_component_closure_clear(ConfitComponentClosure *closure) {
  if (closure == 0) return;
  confit_component_string_list_clear(closure->root_ids, closure->root_count);
  free(closure->ordered);
  memset(closure, 0, sizeof(*closure));
}

const char *confit_component_kind_name(ConfitComponentKind kind) {
  switch (kind) {
  case CONFIT_COMPONENT_KIND_KERNEL_CORE: return "kernel_core";
  case CONFIT_COMPONENT_KIND_KERNEL_DRIVER: return "kernel_driver";
  case CONFIT_COMPONENT_KIND_USER_LIBRARY: return "user_library";
  case CONFIT_COMPONENT_KIND_USER_SERVICE: return "user_service";
  case CONFIT_COMPONENT_KIND_HOST_TOOL: return "host_tool";
  case CONFIT_COMPONENT_KIND_TARGET_IMAGE: return "target_image";
  case CONFIT_COMPONENT_KIND_TEST: return "test";
  default: return "invalid";
  }
}

static ConfitComponentKind confit_component_kind_parse(const char *text) {
  ConfitComponentKind kind;
  for (kind = CONFIT_COMPONENT_KIND_KERNEL_CORE;
       kind <= CONFIT_COMPONENT_KIND_TEST; ++kind) {
    if (strcmp(text, confit_component_kind_name(kind)) == 0) return kind;
  }
  return CONFIT_COMPONENT_KIND_INVALID;
}

static int confit_component_id_valid(const char *text) {
  size_t index;
  int segment_start = 1;
  if (text == 0 || text[0] == '\0' || strlen(text) > CONFIT_COMPONENT_MAX_STRING_BYTES) {
    return 0;
  }
  for (index = 0U; text[index] != '\0'; ++index) {
    const unsigned char value = (unsigned char)text[index];
    if (value == '.') {
      if (segment_start) return 0;
      segment_start = 1;
    } else if ((value >= 'a' && value <= 'z') ||
               (value >= '0' && value <= '9')) {
      if (segment_start && !(value >= 'a' && value <= 'z')) return 0;
      segment_start = 0;
    } else {
      return 0;
    }
  }
  return !segment_start;
}

static int confit_component_atom_valid(const char *text) {
  size_t index;
  if (text == 0 || text[0] == '\0' || strlen(text) > CONFIT_COMPONENT_MAX_STRING_BYTES) {
    return 0;
  }
  for (index = 0U; text[index] != '\0'; ++index) {
    const unsigned char value = (unsigned char)text[index];
    if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
          value == '.' || value == '_' || value == '-' || value == '@')) {
      return 0;
    }
  }
  return text[0] != '.' && text[strlen(text) - 1U] != '.';
}

static int confit_component_relative_path_valid(const char *text) {
  const char *segment;
  const char *cursor;
  if (text == 0 || text[0] == '\0' || text[0] == '/' || text[0] == '\\' ||
      strchr(text, '\\') != 0 ||
      strlen(text) > CONFIT_COMPONENT_MAX_STRING_BYTES) return 0;
  segment = text;
  for (cursor = text;; ++cursor) {
    if (*cursor == '/' || *cursor == '\0') {
      const size_t size = (size_t)(cursor - segment);
      if (size == 0U || (size == 1U && segment[0] == '.') ||
          (size == 2U && segment[0] == '.' && segment[1] == '.')) return 0;
      if (*cursor == '\0') return 1;
      segment = cursor + 1U;
    }
  }
}

static int confit_component_path_within(const char *root, const char *path) {
  const size_t root_size = strlen(root);
  return strncmp(root, path, root_size) == 0 &&
         (path[root_size] == '/' || path[root_size] == '\\' || path[root_size] == '\0');
}

static ConfitStatus confit_component_copy_toml_string(const ConfitV2TomlValue *value,
                                                       char **out,
                                                       ConfitDiagnostic *diagnostic) {
  const char *text;
  size_t size;
  char *copy;
  *out = 0;
  if (value == 0 || !confit_v2_toml_value_string(value, &text, &size) ||
      size == 0U || size > CONFIT_COMPONENT_MAX_STRING_BYTES ||
      memchr(text, '\0', size) != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                          value != 0 ? confit_v2_toml_value_source(value) : 0,
                          value != 0 ? confit_v2_toml_value_line(value) : 0U,
                          value != 0 ? confit_v2_toml_value_column(value) : 0U,
                          "component manifest has an invalid string value");
    return CONFIT_ERR_SCHEMA;
  }
  copy = (char *)malloc(size + 1U);
  if (copy == 0) return CONFIT_ERR_INTERNAL;
  memcpy(copy, text, size);
  copy[size] = '\0';
  *out = copy;
  return CONFIT_OK;
}

static int confit_component_table_keys(const ConfitV2TomlValue *table,
                                       const char *const *allowed,
                                       size_t allowed_count) {
  size_t index;
  if (table == 0 || confit_v2_toml_value_type(table) != CONFIT_V2_TOML_VALUE_TABLE) {
    return 0;
  }
  for (index = 0U; index < confit_v2_toml_table_size(table); ++index) {
    size_t allowed_index;
    const char *key = confit_v2_toml_table_key_at(table, index);
    for (allowed_index = 0U; allowed_index < allowed_count; ++allowed_index) {
      if (strcmp(key, allowed[allowed_index]) == 0) break;
    }
    if (allowed_index == allowed_count) return 0;
  }
  return 1;
}

static ConfitStatus confit_component_parse_atom_list(
    const ConfitV2TomlValue *value, int require_component_id, char ***out_items,
    size_t *out_count, size_t *total_edges, ConfitDiagnostic *diagnostic) {
  size_t count;
  size_t index;
  char **items = 0;
  if (value == 0) return CONFIT_OK;
  if (confit_v2_toml_value_type(value) != CONFIT_V2_TOML_VALUE_ARRAY ||
      (count = confit_v2_toml_array_size(value)) > CONFIT_COMPONENT_MAX_LIST_ITEMS) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                          confit_v2_toml_value_source(value),
                          confit_v2_toml_value_line(value),
                          confit_v2_toml_value_column(value),
                          "component manifest has an invalid bounded list");
    return CONFIT_ERR_SCHEMA;
  }
  if (count > 0U) {
    items = (char **)calloc(count, sizeof(*items));
    if (items == 0) return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < count; ++index) {
    size_t other;
    ConfitStatus status = confit_component_copy_toml_string(
        confit_v2_toml_array_at(value, index), &items[index], diagnostic);
    if (status != CONFIT_OK ||
        !(require_component_id ? confit_component_id_valid(items[index])
                               : confit_component_atom_valid(items[index]))) {
      if (status == CONFIT_OK) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                              confit_v2_toml_value_source(value),
                              confit_v2_toml_value_line(value),
                              confit_v2_toml_value_column(value),
                              "component manifest has an unsafe atom");
      }
      confit_component_string_list_clear(items, count);
      return status == CONFIT_OK ? CONFIT_ERR_SCHEMA : status;
    }
    for (other = 0U; other < index; ++other) {
      if (strcmp(items[other], items[index]) == 0) {
        confit_component_string_list_clear(items, count);
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                              confit_v2_toml_value_source(value),
                              confit_v2_toml_value_line(value),
                              confit_v2_toml_value_column(value),
                              "component manifest list contains a duplicate atom");
        return CONFIT_ERR_SCHEMA;
      }
    }
  }
  if (total_edges != 0) {
    if (*total_edges > CONFIT_COMPONENT_MAX_TOTAL_EDGES - count) {
      confit_component_string_list_clear(items, count);
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                            confit_v2_toml_value_source(value),
                            confit_v2_toml_value_line(value),
                            confit_v2_toml_value_column(value),
                            "component manifest edge budget exceeds the supported limit");
      return CONFIT_ERR_SCHEMA;
    }
    *total_edges += count;
  }
  *out_items = items;
  *out_count = count;
  return CONFIT_OK;
}

static ConfitStatus confit_component_parse_manifest(
    const char *project_root, const char *manifest_physical,
    const char *manifest_logical, size_t *total_edges, ConfitComponent *out,
    ConfitDiagnostic *diagnostic) {
  static const char *const root_keys[] = {"schema_version", "component", "selection",
                                           "dependencies", "provides"};
  static const char *const component_keys[] = {"id", "kind", "makefile"};
  static const char *const selection_keys[] = {"enabled_if"};
  static const char *const dependency_keys[] = {"components", "kapi"};
  static const char *const provide_keys[] = {"capabilities", "kapi"};
  ConfitV2TomlDocument *document = 0;
  const ConfitV2TomlValue *root;
  const ConfitV2TomlValue *component_table;
  const ConfitV2TomlValue *selection_table;
  const ConfitV2TomlValue *dependency_table;
  const ConfitV2TomlValue *provide_table;
  const ConfitV2TomlValue *value;
  char directory[4096];
  char makefile_physical[4096];
  char makefile_canonical[4096];
  const char *separator;
  int64_t schema_version;
  ConfitStatus status;

  memset(out, 0, sizeof(*out));
  status = confit_v2_toml_parse_file(manifest_physical, &document, diagnostic);
  if (status != CONFIT_OK) return status;
  root = confit_v2_toml_document_root(document);
  if (!confit_component_table_keys(root, root_keys,
                                   sizeof(root_keys) / sizeof(root_keys[0])) ||
      (value = confit_v2_toml_table_find(root, "schema_version")) == 0 ||
      !confit_v2_toml_value_int64(value, &schema_version) || schema_version != 1 ||
      (component_table = confit_v2_toml_table_find(root, "component")) == 0 ||
      !confit_component_table_keys(component_table, component_keys,
                                   sizeof(component_keys) / sizeof(component_keys[0]))) {
    status = CONFIT_ERR_SCHEMA;
    goto invalid;
  }
  status = confit_component_copy_toml_string(
      confit_v2_toml_table_find(component_table, "id"), &out->id, diagnostic);
  if (status != CONFIT_OK || !confit_component_id_valid(out->id)) goto invalid;
  {
    char *kind_text = 0;
    status = confit_component_copy_toml_string(
        confit_v2_toml_table_find(component_table, "kind"), &kind_text, diagnostic);
    if (status != CONFIT_OK) {
      free(kind_text);
      goto invalid;
    }
    out->kind = confit_component_kind_parse(kind_text);
    free(kind_text);
    if (out->kind == CONFIT_COMPONENT_KIND_INVALID) goto invalid;
  }
  {
    char *makefile_relative = 0;
    status = confit_component_copy_toml_string(
        confit_v2_toml_table_find(component_table, "makefile"), &makefile_relative,
        diagnostic);
    if (status != CONFIT_OK || !confit_component_relative_path_valid(makefile_relative)) {
      free(makefile_relative);
      goto invalid;
    }
    separator = strrchr(manifest_physical, '/');
    if (separator == 0 || (size_t)(separator - manifest_physical) >= sizeof(directory)) {
      free(makefile_relative);
      status = CONFIT_ERR_SCHEMA;
      goto invalid;
    }
    memcpy(directory, manifest_physical, (size_t)(separator - manifest_physical));
    directory[separator - manifest_physical] = '\0';
    status = confit_host_path_join(makefile_physical, sizeof(makefile_physical),
                                   directory, makefile_relative, diagnostic);
    free(makefile_relative);
    if (status != CONFIT_OK ||
        confit_host_path_canonicalize(makefile_canonical, sizeof(makefile_canonical),
                                      makefile_physical, diagnostic) != CONFIT_OK ||
        strcmp(makefile_canonical, makefile_physical) != 0 ||
        !confit_component_path_within(directory, makefile_canonical) ||
        !confit_component_path_within(project_root, makefile_canonical)) {
      status = CONFIT_ERR_SCHEMA;
      goto invalid;
    }
    out->makefile_path = confit_component_strdup(makefile_canonical + strlen(project_root) + 1U);
    if (out->makefile_path == 0) {
      status = CONFIT_ERR_INTERNAL;
      goto invalid;
    }
  }
  selection_table = confit_v2_toml_table_find(root, "selection");
  if (selection_table != 0 &&
      !confit_component_table_keys(selection_table, selection_keys,
                                   sizeof(selection_keys) / sizeof(selection_keys[0]))) {
    status = CONFIT_ERR_SCHEMA;
    goto invalid;
  }
  if (selection_table != 0) status = confit_component_parse_atom_list(
      confit_v2_toml_table_find(selection_table, "enabled_if"), 0,
      &out->enabled_if, &out->enabled_if_count, total_edges, diagnostic);
  dependency_table = confit_v2_toml_table_find(root, "dependencies");
  if (status == CONFIT_OK && dependency_table != 0 &&
      !confit_component_table_keys(dependency_table, dependency_keys,
                                   sizeof(dependency_keys) / sizeof(dependency_keys[0]))) {
    status = CONFIT_ERR_SCHEMA;
  }
  if (status == CONFIT_OK && dependency_table != 0) status = confit_component_parse_atom_list(
      confit_v2_toml_table_find(dependency_table, "components"), 1,
      &out->component_dependencies, &out->component_dependency_count, total_edges,
      diagnostic);
  if (status == CONFIT_OK && dependency_table != 0) status = confit_component_parse_atom_list(
      confit_v2_toml_table_find(dependency_table, "kapi"), 0, &out->kapi_requires,
      &out->kapi_requirement_count, total_edges, diagnostic);
  provide_table = confit_v2_toml_table_find(root, "provides");
  if (status == CONFIT_OK && provide_table != 0 &&
      !confit_component_table_keys(provide_table, provide_keys,
                                   sizeof(provide_keys) / sizeof(provide_keys[0]))) {
    status = CONFIT_ERR_SCHEMA;
  }
  if (status == CONFIT_OK && provide_table != 0) status = confit_component_parse_atom_list(
      confit_v2_toml_table_find(provide_table, "capabilities"), 0,
      &out->capabilities, &out->capability_count, total_edges, diagnostic);
  if (status == CONFIT_OK && provide_table != 0) status = confit_component_parse_atom_list(
      confit_v2_toml_table_find(provide_table, "kapi"), 0, &out->kapi_provides,
      &out->kapi_provide_count, total_edges, diagnostic);
  if (status != CONFIT_OK) goto invalid;
  out->manifest_path = confit_component_strdup(manifest_logical);
  if (out->manifest_path == 0) {
    status = CONFIT_ERR_INTERNAL;
    goto invalid;
  }
  confit_v2_toml_document_free(document);
  return CONFIT_OK;

invalid:
  if (status == CONFIT_OK) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, manifest_physical, 0U, 0U,
                          "component.toml has an unknown field or invalid schema");
    status = CONFIT_ERR_SCHEMA;
  }
  confit_v2_toml_document_free(document);
  confit_component_clear(out);
  return status;
}

static int confit_component_compare(const void *left, const void *right) {
  const ConfitComponent *a = (const ConfitComponent *)left;
  const ConfitComponent *b = (const ConfitComponent *)right;
  return strcmp(a->id, b->id);
}

const ConfitComponent *confit_component_catalog_find(
    const ConfitComponentCatalog *catalog, const char *id) {
  size_t low = 0U;
  size_t high;
  if (catalog == 0 || id == 0) return 0;
  high = catalog->component_count;
  while (low < high) {
    const size_t middle = low + (high - low) / 2U;
    const int compare = strcmp(id, catalog->components[middle].id);
    if (compare == 0) return &catalog->components[middle];
    if (compare < 0) high = middle;
    else low = middle + 1U;
  }
  return 0;
}

static int confit_component_list_contains(char *const *items, size_t count,
                                          const char *needle) {
  size_t index;
  for (index = 0U; index < count; ++index) {
    if (strcmp(items[index], needle) == 0) return 1;
  }
  return 0;
}

static ConfitStatus confit_component_catalog_validate(
    const ConfitComponentCatalog *catalog, ConfitDiagnostic *diagnostic) {
  unsigned char *emitted;
  size_t emitted_count = 0U;
  size_t index;
  if (catalog->component_count == 0U) return CONFIT_OK;
  emitted = (unsigned char *)calloc(catalog->component_count, sizeof(*emitted));
  if (emitted == 0) return CONFIT_ERR_INTERNAL;
  for (index = 0U; index < catalog->component_count; ++index) {
    const ConfitComponent *component = &catalog->components[index];
    size_t dependency_index;
    size_t other;
    for (dependency_index = 0U; dependency_index < component->component_dependency_count;
         ++dependency_index) {
      if (strcmp(component->id, component->component_dependencies[dependency_index]) == 0 ||
          confit_component_catalog_find(catalog,
                                        component->component_dependencies[dependency_index]) == 0) {
        free(emitted);
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, component->manifest_path, 0U, 0U,
                              "component dependency is missing or self-referential");
        return CONFIT_ERR_SCHEMA;
      }
    }
    for (dependency_index = 0U; dependency_index < component->kapi_requirement_count;
         ++dependency_index) {
      size_t providers = 0U;
      for (other = 0U; other < catalog->component_count; ++other) {
        if (confit_component_list_contains(catalog->components[other].kapi_provides,
                                           catalog->components[other].kapi_provide_count,
                                           component->kapi_requires[dependency_index])) ++providers;
      }
      if (providers != 1U) {
        free(emitted);
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, component->manifest_path, 0U, 0U,
                              "component KAPI requirement does not have one exact provider");
        return CONFIT_ERR_SCHEMA;
      }
    }
    for (dependency_index = 0U; dependency_index < component->capability_count;
         ++dependency_index) {
      for (other = index + 1U; other < catalog->component_count; ++other) {
        if (confit_component_list_contains(catalog->components[other].capabilities,
                                           catalog->components[other].capability_count,
                                           component->capabilities[dependency_index])) {
          free(emitted);
          confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, component->manifest_path, 0U, 0U,
                                "component capability has multiple exact providers");
          return CONFIT_ERR_SCHEMA;
        }
      }
    }
    for (dependency_index = 0U; dependency_index < component->kapi_provide_count;
         ++dependency_index) {
      for (other = index + 1U; other < catalog->component_count; ++other) {
        if (confit_component_list_contains(catalog->components[other].kapi_provides,
                                           catalog->components[other].kapi_provide_count,
                                           component->kapi_provides[dependency_index])) {
          free(emitted);
          confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, component->manifest_path, 0U, 0U,
                                "component KAPI has multiple exact providers");
          return CONFIT_ERR_SCHEMA;
        }
      }
    }
  }
  while (emitted_count < catalog->component_count) {
    int progressed = 0;
    for (index = 0U; index < catalog->component_count; ++index) {
      const ConfitComponent *component = &catalog->components[index];
      size_t dependency_index;
      int ready = !emitted[index];
      if (!ready) continue;
      for (dependency_index = 0U; dependency_index < component->component_dependency_count;
           ++dependency_index) {
        const ConfitComponent *dependency = confit_component_catalog_find(
            catalog, component->component_dependencies[dependency_index]);
        const size_t dependency_slot = (size_t)(dependency - catalog->components);
        if (!emitted[dependency_slot]) {
          ready = 0;
          break;
        }
      }
      if (ready) {
        emitted[index] = 1U;
        emitted_count += 1U;
        progressed = 1;
      }
    }
    if (!progressed) {
      free(emitted);
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, catalog->project_root, 0U, 0U,
                            "component dependency graph contains a cycle");
      return CONFIT_ERR_SCHEMA;
    }
  }
  free(emitted);
  return CONFIT_OK;
}

ConfitStatus confit_component_catalog_load(const ConfitV2Project *project,
                                           ConfitComponentCatalog *out_catalog,
                                           ConfitDiagnostic *diagnostic) {
  size_t root_index;
  size_t total_edges = 0U;
  ConfitStatus status = CONFIT_OK;
  if (project == 0 || project->project_root == 0 || out_catalog == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  if (project->component_roots.count > CONFIT_COMPONENT_MAX_ROOTS) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, project->config_root,
                          0U, 0U,
                          "component root count exceeds the supported limit");
    return CONFIT_ERR_SCHEMA;
  }
  memset(out_catalog, 0, sizeof(*out_catalog));
  out_catalog->project_root = confit_component_strdup(project->project_root);
  if (out_catalog->project_root == 0) return CONFIT_ERR_INTERNAL;
  for (root_index = 0U; status == CONFIT_OK && root_index < project->component_roots.count;
       ++root_index) {
    char root_path[4096];
    char **paths = 0;
    size_t path_count = 0U;
    size_t path_index;
    if (!confit_component_relative_path_valid(project->component_roots.items[root_index])) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, project->config_root, 0U, 0U,
                            "project component_roots contains an unsafe path");
      status = CONFIT_ERR_SCHEMA;
      break;
    }
    status = confit_host_path_join(root_path, sizeof(root_path), project->project_root,
                                   project->component_roots.items[root_index], diagnostic);
    if (status == CONFIT_OK) status = confit_host_list_named_files_recursive(
        root_path, kManifestName, CONFIT_COMPONENT_MAX_DEPTH, CONFIT_COMPONENT_MAX_COUNT,
        CONFIT_COMPONENT_MAX_FILE_BYTES, &paths, &path_count, diagnostic);
    for (path_index = 0U; status == CONFIT_OK && path_index < path_count; ++path_index) {
      const char *logical;
      ConfitComponent parsed;
      ConfitComponent *grown;
      size_t existing;
      if (!confit_component_path_within(project->project_root, paths[path_index])) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, paths[path_index], 0U, 0U,
                              "component manifest escapes the project root");
        status = CONFIT_ERR_SCHEMA;
        break;
      }
      logical = paths[path_index] + strlen(project->project_root) + 1U;
      status = confit_component_parse_manifest(project->project_root, paths[path_index],
                                                logical, &total_edges, &parsed, diagnostic);
      if (status != CONFIT_OK) break;
      if (out_catalog->component_count >= CONFIT_COMPONENT_MAX_COUNT) {
        confit_component_clear(&parsed);
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, paths[path_index],
                              0U, 0U,
                              "component catalog count exceeds the supported limit");
        status = CONFIT_ERR_SCHEMA;
        break;
      }
      for (existing = 0U; existing < out_catalog->component_count; ++existing) {
        if (strcmp(out_catalog->components[existing].id, parsed.id) == 0 ||
            strcmp(out_catalog->components[existing].manifest_path, parsed.manifest_path) == 0) {
          confit_component_clear(&parsed);
          confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, paths[path_index], 0U, 0U,
                                "component catalog has a duplicate ID or path");
          status = CONFIT_ERR_SCHEMA;
          break;
        }
      }
      if (status != CONFIT_OK) break;
      grown = (ConfitComponent *)realloc(out_catalog->components,
          (out_catalog->component_count + 1U) * sizeof(*out_catalog->components));
      if (grown == 0) {
        confit_component_clear(&parsed);
        status = CONFIT_ERR_INTERNAL;
        break;
      }
      out_catalog->components = grown;
      out_catalog->components[out_catalog->component_count++] = parsed;
    }
    confit_host_string_list_free(paths, path_count);
  }
  if (status == CONFIT_OK && out_catalog->component_count > 1U) {
    qsort(out_catalog->components, out_catalog->component_count,
          sizeof(*out_catalog->components), confit_component_compare);
  }
  if (status == CONFIT_OK) status = confit_component_catalog_validate(out_catalog, diagnostic);
  if (status != CONFIT_OK) confit_component_catalog_clear(out_catalog);
  return status;
}

ConfitStatus confit_component_catalog_resolve(
    const ConfitComponentCatalog *catalog, const char *const *root_ids,
    size_t root_count, ConfitComponentClosure *out_closure,
    ConfitDiagnostic *diagnostic) {
  unsigned char *selected = 0;
  unsigned char *emitted = 0;
  size_t root_index;
  size_t selected_count = 0U;
  if (catalog == 0 || out_closure == 0 ||
      (root_count > 0U && root_ids == 0) || root_count > CONFIT_COMPONENT_MAX_LIST_ITEMS) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  memset(out_closure, 0, sizeof(*out_closure));
  if (root_count > 0U) {
    out_closure->root_ids = (char **)calloc(root_count, sizeof(*out_closure->root_ids));
    if (out_closure->root_ids == 0) return CONFIT_ERR_INTERNAL;
  }
  for (root_index = 0U; root_index < root_count; ++root_index) {
    size_t other;
    if (!confit_component_id_valid(root_ids[root_index]) ||
        confit_component_catalog_find(catalog, root_ids[root_index]) == 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, root_ids[root_index], 0U, 0U,
                            "selected component root is unavailable");
      confit_component_closure_clear(out_closure);
      return CONFIT_ERR_SCHEMA;
    }
    for (other = 0U; other < root_index; ++other) {
      if (strcmp(root_ids[other], root_ids[root_index]) == 0) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, root_ids[root_index], 0U, 0U,
                              "selected component root is duplicated");
        confit_component_closure_clear(out_closure);
        return CONFIT_ERR_SCHEMA;
      }
    }
    out_closure->root_ids[root_index] = confit_component_strdup(root_ids[root_index]);
    if (out_closure->root_ids[root_index] == 0) {
      confit_component_closure_clear(out_closure);
      return CONFIT_ERR_INTERNAL;
    }
  }
  out_closure->root_count = root_count;
  selected = (unsigned char *)calloc(catalog->component_count, sizeof(*selected));
  emitted = (unsigned char *)calloc(catalog->component_count, sizeof(*emitted));
  if ((catalog->component_count > 0U) && (selected == 0 || emitted == 0)) {
    free(selected);
    free(emitted);
    confit_component_closure_clear(out_closure);
    return CONFIT_ERR_INTERNAL;
  }
  for (root_index = 0U; root_index < root_count; ++root_index) {
    const ConfitComponent *root = confit_component_catalog_find(catalog, root_ids[root_index]);
    selected[(size_t)(root - catalog->components)] = 1U;
  }
  for (;;) {
    int changed = 0;
    size_t index;
    for (index = 0U; index < catalog->component_count; ++index) {
      const ConfitComponent *component = &catalog->components[index];
      size_t dependency_index;
      if (!selected[index]) continue;
      if (component->enabled_if_count != 0U) {
        free(selected);
        free(emitted);
        confit_diagnostic_set(diagnostic, CONFIT_ERR_UNSUPPORTED, component->manifest_path, 0U, 0U,
                              "component enabled_if requires a typed selection evaluator");
        confit_component_closure_clear(out_closure);
        return CONFIT_ERR_UNSUPPORTED;
      }
      for (dependency_index = 0U; dependency_index < component->component_dependency_count;
           ++dependency_index) {
        const ConfitComponent *dependency = confit_component_catalog_find(
            catalog, component->component_dependencies[dependency_index]);
        const size_t dependency_slot = (size_t)(dependency - catalog->components);
        if (!selected[dependency_slot]) {
          selected[dependency_slot] = 1U;
          changed = 1;
        }
      }
    }
    if (!changed) break;
  }
  for (root_index = 0U; root_index < catalog->component_count; ++root_index) {
    if (selected[root_index]) selected_count += 1U;
  }
  if (selected_count > 0U) {
    out_closure->ordered = (const ConfitComponent **)calloc(
        selected_count, sizeof(*out_closure->ordered));
    if (out_closure->ordered == 0) {
      free(selected);
      free(emitted);
      confit_component_closure_clear(out_closure);
      return CONFIT_ERR_INTERNAL;
    }
  }
  while (out_closure->component_count < selected_count) {
    int progressed = 0;
    size_t index;
    for (index = 0U; index < catalog->component_count; ++index) {
      const ConfitComponent *component = &catalog->components[index];
      size_t dependency_index;
      int ready = selected[index] && !emitted[index];
      if (!ready) continue;
      for (dependency_index = 0U; dependency_index < component->component_dependency_count;
           ++dependency_index) {
        const ConfitComponent *dependency = confit_component_catalog_find(
            catalog, component->component_dependencies[dependency_index]);
        if (!emitted[(size_t)(dependency - catalog->components)]) {
          ready = 0;
          break;
        }
      }
      if (ready) {
        emitted[index] = 1U;
        out_closure->ordered[out_closure->component_count++] = component;
        progressed = 1;
      }
    }
    if (!progressed) {
      free(selected);
      free(emitted);
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, catalog->project_root, 0U, 0U,
                            "selected component closure contains a cycle");
      confit_component_closure_clear(out_closure);
      return CONFIT_ERR_SCHEMA;
    }
  }
  free(selected);
  free(emitted);
  return CONFIT_OK;
}
