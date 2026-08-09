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
#define CONFIT_COMPONENT_MAX_FILE_BYTES (128U * 1024U)
#define CONFIT_COMPONENT_MAX_ATOM_BYTES 127U
#define CONFIT_COMPONENT_MAX_PATH_BYTES 1024U
#define CONFIT_COMPONENT_MAX_LIST_ITEMS 128U
#define CONFIT_COMPONENT_MAX_TOTAL_EDGES 4096U
#define CONFIT_COMPONENT_MAX_SUGGESTIONS 5U
#define CONFIT_COMPONENT_MAKE_MAX_BYTES (128U * 1024U)
#define CONFIT_COMPONENT_MAKE_MAX_LINE_BYTES 4096U
#define CONFIT_COMPONENT_MAKE_MAX_SOURCES 256U

static const char kManifestName[] = "component.toml";

/* Diagnostic path는 catalog/scan allocation 해제 뒤에도 CLI가 소비한다. */
static _Thread_local char g_component_diagnostic_path[4096];

static void confit_component_diagnostic_set(
    ConfitDiagnostic *diagnostic, ConfitStatus status, const char *path,
    size_t line, size_t column, const char *message) {
  const char *stable_path = 0;
  if (path != 0) {
    size_t size = strlen(path);
    if (size >= sizeof(g_component_diagnostic_path)) {
      size = sizeof(g_component_diagnostic_path) - 1U;
    }
    memcpy(g_component_diagnostic_path, path, size);
    g_component_diagnostic_path[size] = '\0';
    stable_path = g_component_diagnostic_path;
  }
  confit_diagnostic_set(diagnostic, status, stable_path, line, column, message);
}

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
  free(component->build_include);
  confit_component_string_list_clear(component->sources,
                                     component->source_count);
  confit_component_string_list_clear(component->component_dependencies,
                                     component->component_dependency_count);
  free(component->component_dependency_spans);
  confit_component_string_list_clear(component->kapi_requires,
                                     component->kapi_requirement_count);
  free(component->kapi_requirement_spans);
  confit_component_string_list_clear(component->capabilities,
                                     component->capability_count);
  free(component->capability_spans);
  confit_component_string_list_clear(component->kapi_provides,
                                     component->kapi_provide_count);
  free(component->kapi_provide_spans);
  free(component->test_owner);
  free(component->test_lane);
  free(component->test_evidence_class);
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
  size_t index;
  if (closure == 0) return;
  confit_component_string_list_clear(closure->root_ids, closure->root_count);
  for (index = 0U; index < closure->reason_count; ++index) {
    free(closure->reasons[index].component_id);
    free(closure->reasons[index].from_id);
    free(closure->reasons[index].requirement);
    free(closure->reasons[index].source_path);
  }
  free(closure->reasons);
  free(closure->ordered);
  memset(closure, 0, sizeof(*closure));
}

const char *confit_component_reason_kind_name(ConfitComponentReasonKind kind) {
  switch (kind) {
  case CONFIT_COMPONENT_REASON_ROOT: return "root";
  case CONFIT_COMPONENT_REASON_PRIVATE_DEPENDENCY: return "private-dependency";
  case CONFIT_COMPONENT_REASON_KAPI_PROVIDER: return "kapi-provider";
  case CONFIT_COMPONENT_REASON_REQUIRED_CAPABILITY: return "required-capability";
  case CONFIT_COMPONENT_REASON_OPTIONAL_CAPABILITY: return "optional-capability";
  default: return "invalid";
  }
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

static const char *confit_component_kind_build_include(
    ConfitComponentKind kind) {
  switch (kind) {
  case CONFIT_COMPONENT_KIND_KERNEL_CORE: return "parus.kernel.mk";
  case CONFIT_COMPONENT_KIND_KERNEL_DRIVER: return "parus.driver.mk";
  case CONFIT_COMPONENT_KIND_USER_LIBRARY: return "parus.userlib.mk";
  case CONFIT_COMPONENT_KIND_USER_SERVICE: return "parus.service.mk";
  case CONFIT_COMPONENT_KIND_HOST_TOOL: return "parus.host.mk";
  case CONFIT_COMPONENT_KIND_TARGET_IMAGE: return "parus.target.mk";
  case CONFIT_COMPONENT_KIND_TEST: return "parus.test.mk";
  default: return 0;
  }
}

static int confit_component_relative_path_valid(const char *text);
static int confit_component_path_within(const char *root, const char *path);

static int confit_component_source_path_valid(const char *text) {
  const char *suffix;
  const unsigned char *cursor;
  if (!confit_component_relative_path_valid(text)) return 0;
  suffix = strrchr(text, '.');
  if (suffix == 0 || (strcmp(suffix, ".c") != 0 &&
                      strcmp(suffix, ".S") != 0 &&
                      strcmp(suffix, ".s") != 0)) return 0;
  for (cursor = (const unsigned char *)text; *cursor != '\0'; ++cursor) {
    if (!( (*cursor >= 'a' && *cursor <= 'z') ||
           (*cursor >= 'A' && *cursor <= 'Z') ||
           (*cursor >= '0' && *cursor <= '9') || *cursor == '_' ||
           *cursor == '-' || *cursor == '.' || *cursor == '/')) return 0;
  }
  return 1;
}

static ConfitStatus confit_component_append_source(
    ConfitComponent *component, const char *source,
    ConfitDiagnostic *diagnostic, const char *makefile, size_t line) {
  char **grown;
  size_t index;
  if (!confit_component_source_path_valid(source)) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA, makefile, line, 1U,
        "component Makefile has an unsafe source-relative path");
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < component->source_count; ++index) {
    if (strcmp(component->sources[index], source) == 0) {
      confit_component_diagnostic_set(
          diagnostic, CONFIT_ERR_SCHEMA, makefile, line, 1U,
          "component Makefile declares a duplicate source");
      return CONFIT_ERR_SCHEMA;
    }
  }
  if (component->source_count >= CONFIT_COMPONENT_MAKE_MAX_SOURCES) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA, makefile, line, 1U,
        "component Makefile source count exceeds the supported limit");
    return CONFIT_ERR_SCHEMA;
  }
  grown = (char **)realloc(component->sources,
                           (component->source_count + 1U) * sizeof(*grown));
  if (grown == 0) return CONFIT_ERR_INTERNAL;
  component->sources = grown;
  component->sources[component->source_count] = confit_component_strdup(source);
  if (component->sources[component->source_count] == 0) return CONFIT_ERR_INTERNAL;
  component->source_count += 1U;
  return CONFIT_OK;
}

static ConfitStatus confit_component_parse_makefile(
    const char *project_root, const char *directory, const char *makefile,
    ConfitComponent *component, ConfitDiagnostic *diagnostic) {
  static const char kApiPrefix[] = "PARUS_BUILD_API=";
  static const char kComponentPrefix[] = "PARUS_COMPONENT=";
  static const char kSourcesPrefix[] = "SRCS=";
  char expected_include[96];
  char *text = 0;
  size_t size = 0U;
  size_t offset = 0U;
  size_t line = 1U;
  int seen_api = 0;
  int seen_component = 0;
  int seen_sources = 0;
  int seen_include = 0;
  ConfitStatus status;
  const char *include_name = confit_component_kind_build_include(component->kind);

  if (include_name == 0 || snprintf(expected_include, sizeof(expected_include),
                                    ".include <%s>", include_name) <= 0) {
    return CONFIT_ERR_INTERNAL;
  }
  status = confit_host_read_text_file(makefile, &text, &size, diagnostic);
  if (status != CONFIT_OK) return status;
  if (size == 0U || size > CONFIT_COMPONENT_MAKE_MAX_BYTES ||
      memchr(text, '\0', size) != 0) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA, makefile, 0U, 0U,
        "component Makefile violates the bounded text size contract");
    status = CONFIT_ERR_SCHEMA;
    goto done;
  }

  while (offset < size) {
    size_t end = offset;
    size_t length;
    char saved;
    char *statement;
    while (end < size && text[end] != '\n') ++end;
    length = end - offset;
    if (length > 0U && text[offset + length - 1U] == '\r') --length;
    if (length > CONFIT_COMPONENT_MAKE_MAX_LINE_BYTES) {
      confit_component_diagnostic_set(
          diagnostic, CONFIT_ERR_SCHEMA, makefile, line, 1U,
          "component Makefile line exceeds the supported limit");
      status = CONFIT_ERR_SCHEMA;
      goto done;
    }
    saved = text[offset + length];
    text[offset + length] = '\0';
    statement = text + offset;
    if (length == 0U || statement[0] == '#') {
      /* Korean responsibility comments are author guidance, never parser authority. */
    } else if (strchr(statement, '\t') != 0 || strchr(statement, '$') != 0 ||
               strchr(statement, '\\') != 0 || strchr(statement, '#') != 0 ||
               seen_include) {
      confit_component_diagnostic_set(
          diagnostic, CONFIT_ERR_SCHEMA, makefile, line, 1U,
          "component Makefile contains a recipe, expansion, inline comment, or statement after the public include");
      status = CONFIT_ERR_SCHEMA;
      text[offset + length] = saved;
      goto done;
    } else if (strncmp(statement, kApiPrefix, sizeof(kApiPrefix) - 1U) == 0) {
      if (seen_api || strcmp(statement + sizeof(kApiPrefix) - 1U, "2") != 0) {
        status = CONFIT_ERR_SCHEMA;
      } else {
        seen_api = 1;
      }
    } else if (strncmp(statement, kComponentPrefix,
                       sizeof(kComponentPrefix) - 1U) == 0) {
      if (seen_component || strcmp(statement + sizeof(kComponentPrefix) - 1U,
                                   component->id) != 0) {
        status = CONFIT_ERR_SCHEMA;
      } else {
        seen_component = 1;
      }
    } else if (strncmp(statement, kSourcesPrefix,
                       sizeof(kSourcesPrefix) - 1U) == 0) {
      char *cursor = statement + sizeof(kSourcesPrefix) - 1U;
      if (seen_sources || component->kind == CONFIT_COMPONENT_KIND_TARGET_IMAGE ||
          cursor[0] == '\0' || cursor[0] == ' ' || cursor[strlen(cursor) - 1U] == ' ') {
        status = CONFIT_ERR_SCHEMA;
      } else {
        seen_sources = 1;
        while (status == CONFIT_OK && *cursor != '\0') {
          char *separator = strchr(cursor, ' ');
          char separator_saved = '\0';
          char physical[4096];
          char canonical[4096];
          if (separator != 0) {
            separator_saved = *separator;
            *separator = '\0';
          }
          if (cursor[0] == '\0') {
            status = CONFIT_ERR_SCHEMA;
          } else {
            status = confit_component_append_source(component, cursor, diagnostic,
                                                     makefile, line);
          }
          if (status == CONFIT_OK) {
            status = confit_host_path_join(physical, sizeof(physical), directory,
                                           cursor, diagnostic);
          }
          if (status == CONFIT_OK &&
              (confit_host_path_canonicalize(canonical, sizeof(canonical), physical,
                                             diagnostic) != CONFIT_OK ||
               strcmp(canonical, physical) != 0 ||
               !confit_component_path_within(directory, canonical) ||
               !confit_component_path_within(project_root, canonical) ||
               !confit_host_file_exists(canonical))) {
            confit_component_diagnostic_set(
                diagnostic, CONFIT_ERR_SCHEMA, makefile, line, 1U,
                "component source is missing, symlinked, or outside its owner directory");
            status = CONFIT_ERR_SCHEMA;
          }
          if (separator != 0) {
            *separator = separator_saved;
            cursor = separator + 1U;
            if (*cursor == ' ') status = CONFIT_ERR_SCHEMA;
          } else {
            cursor += strlen(cursor);
          }
        }
      }
    } else if (strcmp(statement, expected_include) == 0) {
      seen_include = 1;
    } else {
      status = CONFIT_ERR_SCHEMA;
    }
    text[offset + length] = saved;
    if (status != CONFIT_OK) {
      if (!confit_diagnostic_has_error(diagnostic)) {
        confit_component_diagnostic_set(
            diagnostic, CONFIT_ERR_SCHEMA, makefile, line, 1U,
            "component Makefile contains an unknown variable, syntax, API version, identity, or public include");
      }
      goto done;
    }
    offset = end < size ? end + 1U : end;
    ++line;
  }

  if (!seen_api || !seen_component || !seen_include ||
      (component->kind == CONFIT_COMPONENT_KIND_TARGET_IMAGE && seen_sources) ||
      (component->kind != CONFIT_COMPONENT_KIND_TARGET_IMAGE && !seen_sources)) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA, makefile, 0U, 0U,
        "component Makefile is missing its exact Build API v2 declaration, source list, or final public include");
    status = CONFIT_ERR_SCHEMA;
    goto done;
  }
  component->build_include = confit_component_strdup(include_name);
  if (component->build_include == 0) status = CONFIT_ERR_INTERNAL;

done:
  confit_host_free(text);
  return status;
}

static int confit_component_id_valid(const char *text) {
  size_t index;
  int segment_start = 1;
  if (text == 0 || text[0] == '\0' || strlen(text) > CONFIT_COMPONENT_MAX_ATOM_BYTES) {
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
  if (text == 0 || text[0] == '\0' || strlen(text) > CONFIT_COMPONENT_MAX_ATOM_BYTES) {
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
      strlen(text) > CONFIT_COMPONENT_MAX_PATH_BYTES) return 0;
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
      size == 0U || size > CONFIT_COMPONENT_MAX_ATOM_BYTES ||
      memchr(text, '\0', size) != 0) {
    confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
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

static int confit_component_capability_valid(const char *text) {
  const char *version;
  const char *cursor;
  if (!confit_component_atom_valid(text)) return 0;
  version = strrchr(text, '@');
  if (version == 0 || version == text || version[1] == '\0') return 0;
  for (cursor = version + 1; *cursor != '\0'; ++cursor) {
    if (*cursor < '0' || *cursor > '9') return 0;
  }
  return version[1] != '0' || version[2] != '\0';
}

static int confit_component_kapi_valid(const char *text) {
  const char *version;
  const char *cursor;
  if (!confit_component_atom_valid(text)) return 0;
  version = strrchr(text, '.');
  if (version == 0 || version[1] != 'v' || version[2] == '\0') return 0;
  for (cursor = version + 2; *cursor != '\0'; ++cursor) {
    if (*cursor < '0' || *cursor > '9') return 0;
  }
  return version[2] != '0' || version[3] != '\0';
}

static int confit_component_string_in_closed_set(
    const char *text, const char *const *items, size_t count) {
  size_t index;
  for (index = 0U; index < count; ++index) {
    if (strcmp(text, items[index]) == 0) return 1;
  }
  return 0;
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

typedef enum ConfitComponentAtomKind {
  CONFIT_COMPONENT_ATOM_ID = 1,
  CONFIT_COMPONENT_ATOM_KAPI,
  CONFIT_COMPONENT_ATOM_CAPABILITY,
} ConfitComponentAtomKind;

static ConfitStatus confit_component_parse_atom_list(
    const ConfitV2TomlValue *value, ConfitComponentAtomKind atom_kind, char ***out_items,
    ConfitComponentSourceSpan **out_spans, size_t *out_count, size_t *total_edges,
    ConfitDiagnostic *diagnostic) {
  size_t count;
  size_t index;
  char **items = 0;
  ConfitComponentSourceSpan *spans = 0;
  if (value == 0) return CONFIT_OK;
  if (confit_v2_toml_value_type(value) != CONFIT_V2_TOML_VALUE_ARRAY ||
      (count = confit_v2_toml_array_size(value)) > CONFIT_COMPONENT_MAX_LIST_ITEMS) {
    confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                          confit_v2_toml_value_source(value),
                          confit_v2_toml_value_line(value),
                          confit_v2_toml_value_column(value),
                          "component manifest has an invalid bounded list");
    return CONFIT_ERR_SCHEMA;
  }
  if (count > 0U) {
    items = (char **)calloc(count, sizeof(*items));
    spans = (ConfitComponentSourceSpan *)calloc(count, sizeof(*spans));
    if (items == 0 || spans == 0) {
      free(items);
      free(spans);
      return CONFIT_ERR_INTERNAL;
    }
  }
  for (index = 0U; index < count; ++index) {
    size_t other;
    const ConfitV2TomlValue *atom = confit_v2_toml_array_at(value, index);
    ConfitStatus status = confit_component_copy_toml_string(
        atom, &items[index], diagnostic);
    spans[index].line = confit_v2_toml_value_line(atom);
    spans[index].column = confit_v2_toml_value_column(atom);
    if (status != CONFIT_OK ||
        !((atom_kind == CONFIT_COMPONENT_ATOM_ID && confit_component_id_valid(items[index])) ||
          (atom_kind == CONFIT_COMPONENT_ATOM_KAPI && confit_component_kapi_valid(items[index])) ||
          (atom_kind == CONFIT_COMPONENT_ATOM_CAPABILITY && confit_component_capability_valid(items[index])))) {
      if (status == CONFIT_OK) {
        confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                              confit_v2_toml_value_source(value),
                              confit_v2_toml_value_line(value),
                              confit_v2_toml_value_column(value),
                              "component manifest has an unsafe atom");
      }
      confit_component_string_list_clear(items, count);
      free(spans);
      return status == CONFIT_OK ? CONFIT_ERR_SCHEMA : status;
    }
    for (other = 0U; other < index; ++other) {
      if (strcmp(items[other], items[index]) == 0) {
        confit_component_string_list_clear(items, count);
        free(spans);
        confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
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
      free(spans);
      confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                            confit_v2_toml_value_source(value),
                            confit_v2_toml_value_line(value),
                            confit_v2_toml_value_column(value),
                            "component manifest edge budget exceeds the supported limit");
      return CONFIT_ERR_SCHEMA;
    }
    *total_edges += count;
  }
  *out_items = items;
  *out_spans = spans;
  *out_count = count;
  return CONFIT_OK;
}

static ConfitStatus confit_component_parse_manifest(
    const char *project_root, const char *manifest_physical,
    const char *manifest_logical, size_t *total_edges, ConfitComponent *out,
    ConfitDiagnostic *diagnostic) {
  static const char *const root_keys[] = {"schema_version", "component", "requires",
                                           "provides", "test"};
  static const char *const component_keys[] = {"id", "kind"};
  static const char *const requirement_keys[] = {"components", "kapi"};
  static const char *const provide_keys[] = {"capabilities", "kapi"};
  static const char *const test_keys[] = {"owner", "lane", "timeout_ms",
                                          "evidence_class", "target", "machine"};
  ConfitV2TomlDocument *document = 0;
  const ConfitV2TomlValue *root;
  const ConfitV2TomlValue *component_table;
  const ConfitV2TomlValue *requirement_table;
  const ConfitV2TomlValue *provide_table;
  const ConfitV2TomlValue *test_table;
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
      !confit_v2_toml_value_int64(value, &schema_version) || schema_version != 2 ||
      (component_table = confit_v2_toml_table_find(root, "component")) == 0 ||
      !confit_component_table_keys(component_table, component_keys,
                                   sizeof(component_keys) / sizeof(component_keys[0])) ||
      (requirement_table = confit_v2_toml_table_find(root, "requires")) == 0 ||
      !confit_component_table_keys(requirement_table, requirement_keys,
                                   sizeof(requirement_keys) / sizeof(requirement_keys[0])) ||
      confit_v2_toml_table_find(requirement_table, "components") == 0 ||
      confit_v2_toml_table_find(requirement_table, "kapi") == 0 ||
      (provide_table = confit_v2_toml_table_find(root, "provides")) == 0 ||
      !confit_component_table_keys(provide_table, provide_keys,
                                   sizeof(provide_keys) / sizeof(provide_keys[0])) ||
      confit_v2_toml_table_find(provide_table, "capabilities") == 0 ||
      confit_v2_toml_table_find(provide_table, "kapi") == 0) {
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
  separator = strrchr(manifest_physical, '/');
  if (separator == 0 || (size_t)(separator - manifest_physical) >= sizeof(directory)) {
    status = CONFIT_ERR_SCHEMA;
    goto invalid;
  }
  memcpy(directory, manifest_physical, (size_t)(separator - manifest_physical));
  directory[separator - manifest_physical] = '\0';
  status = confit_host_path_join(makefile_physical, sizeof(makefile_physical),
                                 directory, "Makefile", diagnostic);
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
  status = confit_component_parse_makefile(project_root, directory,
                                            makefile_canonical, out,
                                            diagnostic);
  if (status != CONFIT_OK) goto invalid;
  status = confit_component_parse_atom_list(
      confit_v2_toml_table_find(requirement_table, "components"), CONFIT_COMPONENT_ATOM_ID,
      &out->component_dependencies, &out->component_dependency_spans,
      &out->component_dependency_count, total_edges, diagnostic);
  if (status == CONFIT_OK) status = confit_component_parse_atom_list(
      confit_v2_toml_table_find(requirement_table, "kapi"), CONFIT_COMPONENT_ATOM_KAPI,
      &out->kapi_requires, &out->kapi_requirement_spans,
      &out->kapi_requirement_count, total_edges, diagnostic);
  if (status == CONFIT_OK) status = confit_component_parse_atom_list(
      confit_v2_toml_table_find(provide_table, "capabilities"), CONFIT_COMPONENT_ATOM_CAPABILITY,
      &out->capabilities, &out->capability_spans, &out->capability_count,
      total_edges, diagnostic);
  if (status == CONFIT_OK) status = confit_component_parse_atom_list(
      confit_v2_toml_table_find(provide_table, "kapi"), CONFIT_COMPONENT_ATOM_KAPI,
      &out->kapi_provides, &out->kapi_provide_spans,
      &out->kapi_provide_count, total_edges, diagnostic);
  test_table = confit_v2_toml_table_find(root, "test");
  if (status == CONFIT_OK &&
      ((out->kind == CONFIT_COMPONENT_KIND_TEST && test_table == 0) ||
       (out->kind != CONFIT_COMPONENT_KIND_TEST && test_table != 0) ||
       (test_table != 0 && !confit_component_table_keys(
           test_table, test_keys, sizeof(test_keys) / sizeof(test_keys[0]))))) {
    status = CONFIT_ERR_SCHEMA;
  }
  if (status == CONFIT_OK && test_table != 0) {
    static const char *const lanes[] = {"unit", "selftest", "security", "qemu",
                                        "package", "documentation", "hardware-manual"};
    static const char *const evidence[] = {"host-unit", "booted-selftest",
        "host-security", "qemu-smoke", "qemu-runtime", "package",
        "documentation", "physical-hardware"};
    int64_t timeout_ms = 0;
    status = confit_component_copy_toml_string(
        confit_v2_toml_table_find(test_table, "owner"), &out->test_owner, diagnostic);
    if (status == CONFIT_OK) status = confit_component_copy_toml_string(
        confit_v2_toml_table_find(test_table, "lane"), &out->test_lane, diagnostic);
    if (status == CONFIT_OK) status = confit_component_copy_toml_string(
        confit_v2_toml_table_find(test_table, "evidence_class"),
        &out->test_evidence_class, diagnostic);
    value = confit_v2_toml_table_find(test_table, "timeout_ms");
    if (status == CONFIT_OK &&
        (!confit_component_id_valid(out->test_owner) ||
         !confit_component_string_in_closed_set(
             out->test_lane, lanes, sizeof(lanes) / sizeof(lanes[0])) ||
         !confit_component_string_in_closed_set(
             out->test_evidence_class, evidence, sizeof(evidence) / sizeof(evidence[0])) ||
         value == 0 || !confit_v2_toml_value_int64(value, &timeout_ms) ||
         timeout_ms < 1 || timeout_ms > 120000)) {
      status = CONFIT_ERR_SCHEMA;
    }
    if (status == CONFIT_OK) out->test_timeout_ms = (unsigned int)timeout_ms;
  }
  if (status != CONFIT_OK) goto invalid;
  out->manifest_path = confit_component_strdup(manifest_logical);
  if (out->manifest_path == 0) {
    status = CONFIT_ERR_INTERNAL;
    goto invalid;
  }
  confit_v2_toml_document_free(document);
  return CONFIT_OK;

invalid:
  if (status == CONFIT_OK || !confit_diagnostic_has_error(diagnostic)) {
    confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, manifest_physical, 0U, 0U,
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

static int confit_component_source_logical_path(
    const ConfitComponent *component, const char *source, char *out,
    size_t out_size) {
  const char *separator;
  size_t directory_size;
  size_t source_size;
  if (component == 0 || component->makefile_path == 0 || source == 0 ||
      out == 0 || out_size == 0U) return 0;
  separator = strrchr(component->makefile_path, '/');
  if (separator == 0) return 0;
  directory_size = (size_t)(separator - component->makefile_path);
  source_size = strlen(source);
  if (directory_size + 1U + source_size + 1U > out_size) return 0;
  memcpy(out, component->makefile_path, directory_size);
  out[directory_size] = '/';
  memcpy(out + directory_size + 1U, source, source_size + 1U);
  return 1;
}

static ConfitStatus confit_component_catalog_validate(
    const ConfitComponentCatalog *catalog, ConfitDiagnostic *diagnostic) {
  unsigned char *emitted;
  unsigned char *depths;
  size_t emitted_count = 0U;
  size_t index;
  if (catalog->component_count == 0U) return CONFIT_OK;
  emitted = (unsigned char *)calloc(catalog->component_count, sizeof(*emitted));
  depths = (unsigned char *)calloc(catalog->component_count, sizeof(*depths));
  if (emitted == 0 || depths == 0) {
    free(depths);
    free(emitted);
    return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < catalog->component_count; ++index) {
    const ConfitComponent *component = &catalog->components[index];
    size_t dependency_index;
    size_t other;
    size_t source_index;
    for (source_index = 0U; source_index < component->source_count;
         ++source_index) {
      char source_path[4096];
      if (!confit_component_source_logical_path(
              component, component->sources[source_index], source_path,
              sizeof(source_path))) {
        free(emitted);
        free(depths);
        confit_component_diagnostic_set(
            diagnostic, CONFIT_ERR_SCHEMA, component->makefile_path, 0U, 0U,
            "component source identity cannot be represented safely");
        return CONFIT_ERR_SCHEMA;
      }
      for (other = index + 1U; other < catalog->component_count; ++other) {
        size_t other_source_index;
        for (other_source_index = 0U;
             other_source_index < catalog->components[other].source_count;
             ++other_source_index) {
          char other_source_path[4096];
          if (!confit_component_source_logical_path(
                  &catalog->components[other],
                  catalog->components[other].sources[other_source_index],
                  other_source_path, sizeof(other_source_path))) {
            free(emitted);
            free(depths);
            return CONFIT_ERR_SCHEMA;
          }
          if (strcmp(source_path, other_source_path) == 0) {
            free(emitted);
            free(depths);
            confit_component_diagnostic_set(
                diagnostic, CONFIT_ERR_SCHEMA, component->makefile_path, 0U,
                0U, "one source file is owned by multiple components");
            return CONFIT_ERR_SCHEMA;
          }
        }
      }
    }
    if (component->kind == CONFIT_COMPONENT_KIND_TEST) {
      const ConfitComponent *test_owner =
          confit_component_catalog_find(catalog, component->test_owner);
      int lane_matches_evidence = 0;
      if (test_owner == 0 || test_owner->kind == CONFIT_COMPONENT_KIND_TEST) {
        free(emitted);
        free(depths);
        confit_component_diagnostic_set(
            diagnostic, CONFIT_ERR_SCHEMA, component->manifest_path, 0U, 0U,
            "test owner must be one existing non-test component");
        return CONFIT_ERR_SCHEMA;
      }
      lane_matches_evidence =
          (strcmp(component->test_lane, "unit") == 0 &&
           strcmp(component->test_evidence_class, "host-unit") == 0) ||
          (strcmp(component->test_lane, "selftest") == 0 &&
           strcmp(component->test_evidence_class, "booted-selftest") == 0) ||
          (strcmp(component->test_lane, "security") == 0 &&
           strcmp(component->test_evidence_class, "host-security") == 0) ||
          (strcmp(component->test_lane, "qemu") == 0 &&
           (strcmp(component->test_evidence_class, "qemu-smoke") == 0 ||
            strcmp(component->test_evidence_class, "qemu-runtime") == 0)) ||
          (strcmp(component->test_lane, "package") == 0 &&
           strcmp(component->test_evidence_class, "package") == 0) ||
          (strcmp(component->test_lane, "documentation") == 0 &&
           strcmp(component->test_evidence_class, "documentation") == 0) ||
          (strcmp(component->test_lane, "hardware-manual") == 0 &&
           strcmp(component->test_evidence_class, "physical-hardware") == 0);
      if (!lane_matches_evidence) {
        free(emitted);
        free(depths);
        confit_component_diagnostic_set(
            diagnostic, CONFIT_ERR_SCHEMA, component->manifest_path, 0U, 0U,
            "test lane and evidence_class are incompatible");
        return CONFIT_ERR_SCHEMA;
      }
    }
    for (dependency_index = 0U; dependency_index < component->component_dependency_count;
         ++dependency_index) {
      if (strcmp(component->id, component->component_dependencies[dependency_index]) == 0 ||
          confit_component_catalog_find(catalog,
                                        component->component_dependencies[dependency_index]) == 0) {
        free(emitted);
        free(depths);
        confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, component->manifest_path, 0U, 0U,
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
        free(depths);
        confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, component->manifest_path, 0U, 0U,
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
          free(depths);
          confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, component->manifest_path, 0U, 0U,
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
          free(depths);
          confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, component->manifest_path, 0U, 0U,
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
      unsigned char depth = 1U;
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
        if ((unsigned int)depths[dependency_slot] + 1U > depth) {
          depth = (unsigned char)(depths[dependency_slot] + 1U);
        }
      }
      for (dependency_index = 0U; ready &&
           dependency_index < component->kapi_requirement_count; ++dependency_index) {
        const ConfitComponent *dependency = confit_component_catalog_find_kapi_provider(
            catalog, component->kapi_requires[dependency_index]);
        const size_t dependency_slot = (size_t)(dependency - catalog->components);
        if (!emitted[dependency_slot]) ready = 0;
        else if ((unsigned int)depths[dependency_slot] + 1U > depth) {
          depth = (unsigned char)(depths[dependency_slot] + 1U);
        }
      }
      if (ready) {
        if (depth > CONFIT_COMPONENT_MAX_DEPTH) {
          free(depths);
          free(emitted);
          confit_component_diagnostic_set(
              diagnostic, CONFIT_ERR_SCHEMA, component->manifest_path, 0U, 0U,
              "component dependency graph exceeds the supported depth");
          return CONFIT_ERR_SCHEMA;
        }
        emitted[index] = 1U;
        depths[index] = depth;
        emitted_count += 1U;
        progressed = 1;
      }
    }
    if (!progressed) {
      free(emitted);
      free(depths);
      confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, catalog->project_root, 0U, 0U,
                            "component dependency graph contains a cycle");
      return CONFIT_ERR_SCHEMA;
    }
  }
  free(emitted);
  free(depths);
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
    confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, project->config_root,
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
      confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, project->config_root, 0U, 0U,
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
        confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, paths[path_index], 0U, 0U,
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
        confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, paths[path_index],
                              0U, 0U,
                              "component catalog count exceeds the supported limit");
        status = CONFIT_ERR_SCHEMA;
        break;
      }
      for (existing = 0U; existing < out_catalog->component_count; ++existing) {
        if (strcmp(out_catalog->components[existing].id, parsed.id) == 0 ||
            strcmp(out_catalog->components[existing].manifest_path, parsed.manifest_path) == 0) {
          confit_component_clear(&parsed);
          confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, paths[path_index], 0U, 0U,
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
      confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, root_ids[root_index], 0U, 0U,
                            "selected component root is unavailable");
      confit_component_closure_clear(out_closure);
      return CONFIT_ERR_SCHEMA;
    }
    for (other = 0U; other < root_index; ++other) {
      if (strcmp(root_ids[other], root_ids[root_index]) == 0) {
        confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, root_ids[root_index], 0U, 0U,
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
      for (dependency_index = 0U; dependency_index < component->kapi_requirement_count;
           ++dependency_index) {
        const ConfitComponent *dependency = confit_component_catalog_find_kapi_provider(
            catalog, component->kapi_requires[dependency_index]);
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
      for (dependency_index = 0U; ready &&
           dependency_index < component->kapi_requirement_count; ++dependency_index) {
        const ConfitComponent *dependency = confit_component_catalog_find_kapi_provider(
            catalog, component->kapi_requires[dependency_index]);
        if (!emitted[(size_t)(dependency - catalog->components)]) ready = 0;
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
      confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, catalog->project_root, 0U, 0U,
                            "selected component closure contains a cycle");
      confit_component_closure_clear(out_closure);
      return CONFIT_ERR_SCHEMA;
    }
  }
  free(selected);
  free(emitted);
  {
    size_t index;
    size_t reason_capacity = root_count;
    for (index = 0U; index < out_closure->component_count; ++index) {
      reason_capacity += out_closure->ordered[index]->component_dependency_count;
      reason_capacity += out_closure->ordered[index]->kapi_requirement_count;
    }
    if (reason_capacity > CONFIT_COMPONENT_MAX_TOTAL_EDGES + CONFIT_COMPONENT_MAX_LIST_ITEMS) {
      confit_component_closure_clear(out_closure);
      return CONFIT_ERR_SCHEMA;
    }
    if (reason_capacity > 0U) {
      out_closure->reasons = (ConfitComponentReason *)calloc(
          reason_capacity, sizeof(*out_closure->reasons));
      if (out_closure->reasons == 0) {
        confit_component_closure_clear(out_closure);
        return CONFIT_ERR_INTERNAL;
      }
    }
    for (index = 0U; index < root_count; ++index) {
      const ConfitComponent *root_component =
          confit_component_catalog_find(catalog, root_ids[index]);
      ConfitComponentReason *reason = &out_closure->reasons[out_closure->reason_count++];
      reason->kind = CONFIT_COMPONENT_REASON_ROOT;
      reason->component_id = confit_component_strdup(root_ids[index]);
      reason->requirement = confit_component_strdup(root_ids[index]);
      reason->source_path = confit_component_strdup(root_component->manifest_path);
      reason->source_line = 1U;
      reason->source_column = 1U;
      if (reason->component_id == 0 || reason->requirement == 0 ||
          reason->source_path == 0) {
        confit_component_closure_clear(out_closure);
        return CONFIT_ERR_INTERNAL;
      }
    }
    for (index = 0U; index < out_closure->component_count; ++index) {
      const ConfitComponent *component = out_closure->ordered[index];
      size_t edge;
      for (edge = 0U; edge < component->component_dependency_count; ++edge) {
        ConfitComponentReason *reason = &out_closure->reasons[out_closure->reason_count++];
        reason->kind = CONFIT_COMPONENT_REASON_PRIVATE_DEPENDENCY;
        reason->component_id = confit_component_strdup(component->component_dependencies[edge]);
        reason->from_id = confit_component_strdup(component->id);
        reason->requirement = confit_component_strdup(component->component_dependencies[edge]);
        reason->source_path = confit_component_strdup(component->manifest_path);
        reason->source_line = component->component_dependency_spans[edge].line;
        reason->source_column = component->component_dependency_spans[edge].column;
        if (reason->component_id == 0 || reason->from_id == 0 || reason->requirement == 0 ||
            reason->source_path == 0) {
          confit_component_closure_clear(out_closure);
          return CONFIT_ERR_INTERNAL;
        }
      }
      for (edge = 0U; edge < component->kapi_requirement_count; ++edge) {
        const ConfitComponent *provider = confit_component_catalog_find_kapi_provider(
            catalog, component->kapi_requires[edge]);
        ConfitComponentReason *reason = &out_closure->reasons[out_closure->reason_count++];
        reason->kind = CONFIT_COMPONENT_REASON_KAPI_PROVIDER;
        reason->component_id = confit_component_strdup(provider->id);
        reason->from_id = confit_component_strdup(component->id);
        reason->requirement = confit_component_strdup(component->kapi_requires[edge]);
        reason->source_path = confit_component_strdup(component->manifest_path);
        reason->source_line = component->kapi_requirement_spans[edge].line;
        reason->source_column = component->kapi_requirement_spans[edge].column;
        if (reason->component_id == 0 || reason->from_id == 0 || reason->requirement == 0 ||
            reason->source_path == 0) {
          confit_component_closure_clear(out_closure);
          return CONFIT_ERR_INTERNAL;
        }
      }
    }
  }
  return CONFIT_OK;
}

static char *confit_component_duplicate_text(const char *text) {
  const size_t size = text != 0 ? strlen(text) : 0U;
  char *copy;
  if (size == 0U) return 0;
  copy = (char *)malloc(size + 1U);
  if (copy != 0) memcpy(copy, text, size + 1U);
  return copy;
}

static void confit_component_root_list_clear(char **roots, size_t root_count) {
  size_t index;
  for (index = 0U; index < root_count; ++index) free(roots[index]);
  free(roots);
}

static int confit_component_id_in_list(const char *id,
                                       const char *const *items,
                                       size_t count) {
  size_t index;
  for (index = 0U; index < count; ++index) {
    if (strcmp(id, items[index]) == 0) return 1;
  }
  return 0;
}

static ConfitStatus confit_component_reason_append(
    ConfitComponentClosure *closure, ConfitComponentReasonKind kind,
    const char *component_id, const char *from_id, const char *requirement,
    const char *source_path, size_t source_line, size_t source_column) {
  ConfitComponentReason *grown = (ConfitComponentReason *)realloc(
      closure->reasons, (closure->reason_count + 1U) * sizeof(*grown));
  ConfitComponentReason *reason;
  if (grown == 0) return CONFIT_ERR_INTERNAL;
  closure->reasons = grown;
  reason = &closure->reasons[closure->reason_count];
  memset(reason, 0, sizeof(*reason));
  reason->kind = kind;
  reason->component_id = confit_component_strdup(component_id);
  reason->from_id = from_id != 0 ? confit_component_strdup(from_id) : 0;
  reason->requirement = confit_component_strdup(requirement);
  reason->source_path = confit_component_strdup(source_path);
  reason->source_line = source_line;
  reason->source_column = source_column;
  if (reason->component_id == 0 || (from_id != 0 && reason->from_id == 0) ||
      reason->requirement == 0 || reason->source_path == 0) {
    free(reason->component_id);
    free(reason->from_id);
    free(reason->requirement);
    free(reason->source_path);
    memset(reason, 0, sizeof(*reason));
    return CONFIT_ERR_INTERNAL;
  }
  closure->reason_count += 1U;
  return CONFIT_OK;
}

static ConfitStatus confit_component_root_list_append(char ***roots,
                                                       size_t *root_count,
                                                       const char *id) {
  char **grown;
  size_t index;
  char *copy;
  if (roots == 0 || root_count == 0 || !confit_component_id_valid(id)) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  for (index = 0U; index < *root_count; ++index) {
    if (strcmp((*roots)[index], id) == 0) return CONFIT_OK;
  }
  if (*root_count >= CONFIT_COMPONENT_MAX_LIST_ITEMS) return CONFIT_ERR_SCHEMA;
  copy = confit_component_duplicate_text(id);
  if (copy == 0) return CONFIT_ERR_INTERNAL;
  grown = (char **)realloc(*roots, (*root_count + 1U) * sizeof(*grown));
  if (grown == 0) {
    free(copy);
    return CONFIT_ERR_INTERNAL;
  }
  grown[*root_count] = copy;
  *roots = grown;
  *root_count += 1U;
  return CONFIT_OK;
}

const ConfitComponent *confit_component_catalog_find_capability_provider(
    const ConfitComponentCatalog *catalog, const char *capability) {
  size_t index;
  for (index = 0U; index < catalog->component_count; ++index) {
    const ConfitComponent *component = &catalog->components[index];
    if (confit_component_list_contains(component->capabilities,
                                        component->capability_count, capability)) {
      return component;
    }
  }
  return 0;
}

const ConfitComponent *confit_component_catalog_find_kapi_provider(
    const ConfitComponentCatalog *catalog, const char *kapi) {
  size_t index;
  if (catalog == 0 || kapi == 0) return 0;
  for (index = 0U; index < catalog->component_count; ++index) {
    const ConfitComponent *component = &catalog->components[index];
    if (confit_component_list_contains(component->kapi_provides,
                                        component->kapi_provide_count, kapi)) {
      return component;
    }
  }
  return 0;
}

static size_t confit_component_edit_distance(const char *left, const char *right) {
  size_t previous[CONFIT_COMPONENT_MAX_ATOM_BYTES + 1U];
  size_t current[CONFIT_COMPONENT_MAX_ATOM_BYTES + 1U];
  size_t left_size = strlen(left);
  size_t right_size = strlen(right);
  size_t row;
  size_t column;
  if (left_size > CONFIT_COMPONENT_MAX_ATOM_BYTES ||
      right_size > CONFIT_COMPONENT_MAX_ATOM_BYTES) return SIZE_MAX;
  for (column = 0U; column <= right_size; ++column) previous[column] = column;
  for (row = 1U; row <= left_size; ++row) {
    current[0] = row;
    for (column = 1U; column <= right_size; ++column) {
      size_t deletion = previous[column] + 1U;
      size_t insertion = current[column - 1U] + 1U;
      size_t substitution = previous[column - 1U] +
          (left[row - 1U] == right[column - 1U] ? 0U : 1U);
      size_t best = deletion < insertion ? deletion : insertion;
      if (substitution < best) best = substitution;
      current[column] = best;
    }
    memcpy(previous, current, (right_size + 1U) * sizeof(previous[0]));
  }
  return previous[right_size];
}

size_t confit_component_catalog_suggest(const ConfitComponentCatalog *catalog,
                                        const char *id,
                                        const ConfitComponent **out_candidates,
                                        size_t capacity) {
  size_t distances[CONFIT_COMPONENT_MAX_SUGGESTIONS];
  size_t count = 0U;
  size_t index;
  size_t limit = capacity < CONFIT_COMPONENT_MAX_SUGGESTIONS
                     ? capacity : CONFIT_COMPONENT_MAX_SUGGESTIONS;
  if (catalog == 0 || id == 0 || out_candidates == 0 || limit == 0U) return 0U;
  for (index = 0U; index < catalog->component_count; ++index) {
    const ConfitComponent *candidate = &catalog->components[index];
    size_t distance = confit_component_edit_distance(id, candidate->id);
    size_t slot = count;
    while (slot > 0U &&
           (distance < distances[slot - 1U] ||
            (distance == distances[slot - 1U] &&
             strcmp(candidate->id, out_candidates[slot - 1U]->id) < 0))) {
      if (slot < limit) {
        distances[slot] = distances[slot - 1U];
        out_candidates[slot] = out_candidates[slot - 1U];
      }
      --slot;
    }
    if (slot < limit) {
      distances[slot] = distance;
      out_candidates[slot] = candidate;
      if (count < limit) ++count;
    }
  }
  return count;
}

ConfitStatus confit_component_catalog_resolve_selection(
    const ConfitComponentCatalog *catalog, const char *const *component_roots,
    size_t component_root_count, const char *const *required_capabilities,
    size_t required_capability_count, const char *const *optional_capabilities,
    size_t optional_capability_count, ConfitComponentClosure *out_closure,
    ConfitDiagnostic *diagnostic) {
  char **effective_roots = 0;
  size_t effective_root_count = 0U;
  size_t index;
  ConfitStatus status = CONFIT_OK;
  if (catalog == 0 || out_closure == 0 ||
      (component_root_count > 0U && component_roots == 0) ||
      (required_capability_count > 0U && required_capabilities == 0) ||
      (optional_capability_count > 0U && optional_capabilities == 0) ||
      component_root_count > CONFIT_COMPONENT_MAX_LIST_ITEMS ||
      required_capability_count > CONFIT_COMPONENT_MAX_LIST_ITEMS ||
      optional_capability_count > CONFIT_COMPONENT_MAX_LIST_ITEMS) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  for (index = 0U; status == CONFIT_OK && index < component_root_count; ++index) {
    status = confit_component_root_list_append(&effective_roots, &effective_root_count,
                                               component_roots[index]);
  }
  for (index = 0U; status == CONFIT_OK && index < required_capability_count; ++index) {
    const ConfitComponent *provider;
    if (!confit_component_capability_valid(required_capabilities[index])) {
      confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, required_capabilities[index],
                            0U, 0U, "required component capability is unsafe");
      status = CONFIT_ERR_SCHEMA;
      break;
    }
    provider = confit_component_catalog_find_capability_provider(
        catalog, required_capabilities[index]);
    if (provider == 0) {
      confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, required_capabilities[index],
                            0U, 0U, "required component capability is unavailable");
      status = CONFIT_ERR_SCHEMA;
      break;
    }
    status = confit_component_root_list_append(&effective_roots, &effective_root_count,
                                               provider->id);
  }
  for (index = 0U; status == CONFIT_OK && index < optional_capability_count; ++index) {
    const ConfitComponent *provider;
    if (!confit_component_capability_valid(optional_capabilities[index])) {
      confit_component_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, optional_capabilities[index],
                            0U, 0U, "optional component capability is unsafe");
      status = CONFIT_ERR_SCHEMA;
      break;
    }
    provider = confit_component_catalog_find_capability_provider(
        catalog, optional_capabilities[index]);
    if (provider != 0) {
      status = confit_component_root_list_append(&effective_roots, &effective_root_count,
                                                 provider->id);
    }
  }
  if (status == CONFIT_OK) {
    status = confit_component_catalog_resolve(
        catalog, (const char *const *)effective_roots, effective_root_count,
        out_closure, diagnostic);
  }
  if (status == CONFIT_OK) {
    size_t read_index;
    size_t write_index = 0U;
    for (read_index = 0U; read_index < out_closure->reason_count; ++read_index) {
      ConfitComponentReason *reason = &out_closure->reasons[read_index];
      if (reason->kind == CONFIT_COMPONENT_REASON_ROOT &&
          !confit_component_id_in_list(reason->component_id, component_roots,
                                       component_root_count)) {
        free(reason->component_id);
        free(reason->from_id);
        free(reason->requirement);
        free(reason->source_path);
        continue;
      }
      if (write_index != read_index) out_closure->reasons[write_index] = *reason;
      ++write_index;
    }
    out_closure->reason_count = write_index;
  }
  for (index = 0U; status == CONFIT_OK && index < required_capability_count; ++index) {
    const ConfitComponent *provider = confit_component_catalog_find_capability_provider(
        catalog, required_capabilities[index]);
    size_t capability_index = 0U;
    while (capability_index < provider->capability_count &&
           strcmp(provider->capabilities[capability_index],
                  required_capabilities[index]) != 0) {
      ++capability_index;
    }
    status = confit_component_reason_append(
        out_closure, CONFIT_COMPONENT_REASON_REQUIRED_CAPABILITY, provider->id,
        0, required_capabilities[index], provider->manifest_path,
        provider->capability_spans[capability_index].line,
        provider->capability_spans[capability_index].column);
  }
  for (index = 0U; status == CONFIT_OK && index < optional_capability_count; ++index) {
    const ConfitComponent *provider = confit_component_catalog_find_capability_provider(
        catalog, optional_capabilities[index]);
    if (provider != 0) {
      size_t capability_index = 0U;
      while (capability_index < provider->capability_count &&
             strcmp(provider->capabilities[capability_index],
                    optional_capabilities[index]) != 0) {
        ++capability_index;
      }
      status = confit_component_reason_append(
          out_closure, CONFIT_COMPONENT_REASON_OPTIONAL_CAPABILITY, provider->id,
          0, optional_capabilities[index], provider->manifest_path,
          provider->capability_spans[capability_index].line,
          provider->capability_spans[capability_index].column);
    }
  }
  if (status != CONFIT_OK) confit_component_closure_clear(out_closure);
  confit_component_root_list_clear(effective_roots, effective_root_count);
  return status;
}
