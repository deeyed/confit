#include "confit/component_catalog.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/host.h"
#include "confit/toml.h"

#define CONFIT_COMPONENT_MAX_DEPTH 32U
#define CONFIT_COMPONENT_MAX_ROOTS 32U
#define CONFIT_COMPONENT_MAX_COUNT 512U
#define CONFIT_COMPONENT_MAX_FILE_BYTES (128U * 1024U)
#define CONFIT_COMPONENT_MAX_ATOM_BYTES 127U
#define CONFIT_COMPONENT_MAX_SUMMARY_BYTES 512U
#define CONFIT_COMPONENT_MAX_PATH_BYTES 1023U
#define CONFIT_COMPONENT_MAX_LIST_ITEMS 128U
#define CONFIT_COMPONENT_MAX_TOTAL_EDGES 4096U
#define CONFIT_COMPONENT_MAX_SUGGESTIONS 5U
#define CONFIT_COMPONENT_MAKE_MAX_BYTES (128U * 1024U)
#define CONFIT_COMPONENT_MAKE_MAX_LINE_BYTES 4096U
#define CONFIT_COMPONENT_MAKE_MAX_SOURCES 256U
#define CONFIT_COMPONENT_MAKE_MAX_HEADERS 128U

static const char kManifestName[] = "component.toml";
static _Thread_local char g_component_diagnostic_path[4096];

typedef enum ConfitComponentAtomKind {
  CONFIT_COMPONENT_ATOM_FEATURE = 1,
  CONFIT_COMPONENT_ATOM_KAPI,
} ConfitComponentAtomKind;

typedef struct ConfitComponentResolveContext {
  const ConfitComponentCatalog *catalog;
  const ConfitComponentProviderChoice *choices;
  size_t choice_count;
  unsigned char *state;
  ConfitComponentClosure *closure;
  ConfitDiagnostic *diagnostic;
} ConfitComponentResolveContext;

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
  size_t size;
  char *copy;
  if (text == 0) return 0;
  size = strlen(text);
  copy = (char *)malloc(size + 1U);
  if (copy != 0) memcpy(copy, text, size + 1U);
  return copy;
}

static void confit_component_string_list_clear(char **items, size_t count) {
  size_t index;
  if (items == 0) return;
  for (index = 0U; index < count; ++index) free(items[index]);
  free(items);
}

static void confit_component_clear(ConfitComponent *component) {
  if (component == 0) return;
  free(component->id);
  free(component->summary);
  free(component->owner);
  free(component->manifest_path);
  free(component->makefile_path);
  free(component->build_include);
  confit_component_string_list_clear(component->sources,
                                     component->source_count);
  confit_component_string_list_clear(component->public_headers,
                                     component->public_header_count);
  confit_component_string_list_clear(component->link_uses,
                                     component->link_use_count);
  confit_component_string_list_clear(component->feature_requires,
                                     component->feature_requirement_count);
  free(component->feature_requirement_spans);
  confit_component_string_list_clear(component->feature_provides,
                                     component->feature_provide_count);
  free(component->feature_provide_spans);
  confit_component_string_list_clear(component->feature_conflicts,
                                     component->feature_conflict_count);
  free(component->feature_conflict_spans);
  confit_component_string_list_clear(component->kapi_requires,
                                     component->kapi_requirement_count);
  free(component->kapi_requirement_spans);
  confit_component_string_list_clear(component->kapi_provides,
                                     component->kapi_provide_count);
  free(component->kapi_provide_spans);
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
  confit_component_string_list_clear(closure->root_features,
                                     closure->root_feature_count);
  confit_component_string_list_clear(closure->absent_optional_features,
                                     closure->absent_optional_feature_count);
  for (index = 0U; index < closure->reason_count; ++index) {
    free(closure->reasons[index].component_id);
    free(closure->reasons[index].from_id);
    free(closure->reasons[index].requirement);
    free(closure->reasons[index].source_path);
  }
  free(closure->reasons);
  free(closure->ordered);
  confit_component_string_list_clear(closure->kapi_requires,
                                     closure->kapi_requirement_count);
  confit_component_string_list_clear(closure->kapi_provides,
                                     closure->kapi_provide_count);
  memset(closure, 0, sizeof(*closure));
}

const char *confit_component_kind_name(ConfitComponentKind kind) {
  switch (kind) {
  case CONFIT_COMPONENT_KIND_KERNEL_FEATURE: return "kernel_feature";
  case CONFIT_COMPONENT_KIND_KERNEL_PROVIDER: return "kernel_provider";
  case CONFIT_COMPONENT_KIND_WORLD_FEATURE: return "world_feature";
  case CONFIT_COMPONENT_KIND_WORLD_SERVICE: return "world_service";
  default: return "invalid";
  }
}

const char *confit_component_provider_selection_name(
    ConfitComponentProviderSelection selection) {
  switch (selection) {
  case CONFIT_COMPONENT_PROVIDER_SELECTION_UNIQUE: return "unique";
  case CONFIT_COMPONENT_PROVIDER_SELECTION_EXPLICIT: return "explicit";
  default: return "none";
  }
}

const char *confit_component_reason_kind_name(ConfitComponentReasonKind kind) {
  switch (kind) {
  case CONFIT_COMPONENT_REASON_ROOT_FEATURE: return "root-feature";
  case CONFIT_COMPONENT_REASON_FEATURE_REQUIREMENT: return "feature-requirement";
  case CONFIT_COMPONENT_REASON_KAPI_REQUIREMENT: return "kapi-requirement";
  default: return "invalid";
  }
}

static ConfitComponentKind confit_component_kind_parse(const char *text) {
  ConfitComponentKind kind;
  for (kind = CONFIT_COMPONENT_KIND_KERNEL_FEATURE;
       kind <= CONFIT_COMPONENT_KIND_WORLD_SERVICE; ++kind) {
    if (strcmp(text, confit_component_kind_name(kind)) == 0) return kind;
  }
  return CONFIT_COMPONENT_KIND_INVALID;
}

static int confit_component_relative_path_valid(const char *text) {
  const char *segment;
  const char *cursor;
  if (text == 0 || text[0] == '\0' || text[0] == '/' || text[0] == '\\' ||
      strchr(text, '\\') != 0 || strlen(text) > CONFIT_COMPONENT_MAX_PATH_BYTES) {
    return 0;
  }
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
         (path[root_size] == '/' || path[root_size] == '\\' ||
          path[root_size] == '\0');
}

static int confit_component_identifier_valid(const char *text) {
  size_t index;
  int segment_start = 1;
  if (text == 0 || text[0] == '\0' ||
      strlen(text) > CONFIT_COMPONENT_MAX_ATOM_BYTES) return 0;
  for (index = 0U; text[index] != '\0'; ++index) {
    const unsigned char value = (unsigned char)text[index];
    if (value == '.') {
      if (segment_start) return 0;
      segment_start = 1;
    } else if ((value >= 'a' && value <= 'z') ||
               (value >= '0' && value <= '9') || value == '-') {
      if (segment_start && !(value >= 'a' && value <= 'z')) return 0;
      segment_start = 0;
    } else {
      return 0;
    }
  }
  return !segment_start;
}

static int confit_component_version_valid(const char *text) {
  uint64_t value = 0U;
  const char *cursor;
  if (text == 0 || text[0] == '\0' ||
      (text[0] == '0' && text[1] != '\0')) return 0;
  for (cursor = text; *cursor != '\0'; ++cursor) {
    if (*cursor < '0' || *cursor > '9') return 0;
    value = value * 10U + (uint64_t)(*cursor - '0');
    if (value > UINT32_MAX) return 0;
  }
  return value > 0U;
}

static int confit_component_feature_valid(const char *text) {
  const char *version;
  char name[CONFIT_COMPONENT_MAX_ATOM_BYTES + 1U];
  size_t name_size;
  if (text == 0 || strlen(text) > CONFIT_COMPONENT_MAX_ATOM_BYTES) return 0;
  version = strrchr(text, '@');
  if (version == 0 || version == text || !confit_component_version_valid(version + 1)) {
    return 0;
  }
  name_size = (size_t)(version - text);
  if (name_size >= sizeof(name)) return 0;
  memcpy(name, text, name_size);
  name[name_size] = '\0';
  return confit_component_identifier_valid(name);
}

static int confit_component_kapi_valid(const char *text) {
  const char *version;
  char name[CONFIT_COMPONENT_MAX_ATOM_BYTES + 1U];
  size_t name_size;
  if (text == 0 || strlen(text) > CONFIT_COMPONENT_MAX_ATOM_BYTES) return 0;
  version = strrchr(text, '.');
  if (version == 0 || version == text || version[1] != 'v' ||
      !confit_component_version_valid(version + 2)) return 0;
  name_size = (size_t)(version - text);
  if (name_size >= sizeof(name)) return 0;
  memcpy(name, text, name_size);
  name[name_size] = '\0';
  return confit_component_identifier_valid(name);
}

static int confit_component_link_valid(const char *text) {
  return confit_component_identifier_valid(text) ||
         confit_component_feature_valid(text);
}

static int confit_component_text_safe(const char *text, size_t maximum) {
  const unsigned char *cursor;
  size_t size;
  if (text == 0 || text[0] == '\0') return 0;
  size = strlen(text);
  if (size > maximum) return 0;
  for (cursor = (const unsigned char *)text; *cursor != '\0'; ++cursor) {
    if (*cursor < 0x20U || *cursor == 0x7fU) return 0;
  }
  return 1;
}

static int confit_component_list_contains(char *const *items, size_t count,
                                          const char *needle) {
  size_t index;
  for (index = 0U; index < count; ++index) {
    if (strcmp(items[index], needle) == 0) return 1;
  }
  return 0;
}

static ConfitStatus confit_component_validate_table_keys(
    const ConfitTomlValue *table, const char *const *allowed,
    size_t allowed_count, const char *fallback_path,
    ConfitDiagnostic *diagnostic) {
  size_t index;
  if (table == 0 ||
      confit_toml_value_type(table) != CONFIT_TOML_VALUE_TABLE) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA,
        table != 0 ? confit_toml_value_source(table) : fallback_path,
        table != 0 ? confit_toml_value_line(table) : 1U,
        table != 0 ? confit_toml_value_column(table) : 1U,
        "component schema requires a closed TOML table");
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < confit_toml_table_size(table); ++index) {
    size_t allowed_index;
    const char *key = confit_toml_table_key_at(table, index);
    for (allowed_index = 0U; allowed_index < allowed_count; ++allowed_index) {
      if (strcmp(key, allowed[allowed_index]) == 0) break;
    }
    if (allowed_index == allowed_count) {
      const ConfitTomlValue *value =
          confit_toml_table_value_at(table, index);
      confit_component_diagnostic_set(
          diagnostic, CONFIT_ERR_SCHEMA,
          value != 0 ? confit_toml_value_source(value) : fallback_path,
          value != 0 ? confit_toml_value_line(value) : 1U,
          value != 0 ? confit_toml_value_column(value) : 1U,
          "component schema contains an unknown field or table");
      return CONFIT_ERR_SCHEMA;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_component_copy_toml_string(
    const ConfitTomlValue *value, size_t maximum, char **out,
    ConfitDiagnostic *diagnostic) {
  const char *text;
  size_t size;
  char *copy;
  *out = 0;
  if (value == 0) return CONFIT_ERR_SCHEMA;
  if (!confit_toml_value_string(value, &text, &size) ||
      size == 0U || size > maximum || memchr(text, '\0', size) != 0) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA,
        confit_toml_value_source(value),
        confit_toml_value_line(value),
        confit_toml_value_column(value),
        "component manifest has an invalid bounded string");
    return CONFIT_ERR_SCHEMA;
  }
  copy = (char *)malloc(size + 1U);
  if (copy == 0) return CONFIT_ERR_INTERNAL;
  memcpy(copy, text, size);
  copy[size] = '\0';
  *out = copy;
  return CONFIT_OK;
}

static ConfitStatus confit_component_parse_atom_list(
    const ConfitTomlValue *value, ConfitComponentAtomKind atom_kind,
    char ***out_items, ConfitComponentSourceSpan **out_spans,
    size_t *out_count, size_t *total_edges, ConfitDiagnostic *diagnostic) {
  size_t count;
  size_t index;
  char **items = 0;
  ConfitComponentSourceSpan *spans = 0;
  if (value == 0 ||
      confit_toml_value_type(value) != CONFIT_TOML_VALUE_ARRAY ||
      (count = confit_toml_array_size(value)) >
          CONFIT_COMPONENT_MAX_LIST_ITEMS) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA,
        value != 0 ? confit_toml_value_source(value) : 0,
        value != 0 ? confit_toml_value_line(value) : 0U,
        value != 0 ? confit_toml_value_column(value) : 0U,
        "component manifest has an invalid bounded list");
    return CONFIT_ERR_SCHEMA;
  }
  if (*total_edges > CONFIT_COMPONENT_MAX_TOTAL_EDGES - count) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA, confit_toml_value_source(value),
        confit_toml_value_line(value), confit_toml_value_column(value),
        "component manifest edge budget exceeds the supported limit");
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
    const ConfitTomlValue *atom = confit_toml_array_at(value, index);
    ConfitStatus status = confit_component_copy_toml_string(
        atom, CONFIT_COMPONENT_MAX_ATOM_BYTES, &items[index], diagnostic);
    if (status != CONFIT_OK ||
        !((atom_kind == CONFIT_COMPONENT_ATOM_FEATURE &&
           confit_component_feature_valid(items[index])) ||
          (atom_kind == CONFIT_COMPONENT_ATOM_KAPI &&
           confit_component_kapi_valid(items[index])))) {
      if (status == CONFIT_OK) {
        confit_component_diagnostic_set(
            diagnostic, CONFIT_ERR_SCHEMA, confit_toml_value_source(atom),
            confit_toml_value_line(atom), confit_toml_value_column(atom),
            "component manifest has an invalid feature or KAPI token");
      }
      confit_component_string_list_clear(items, count);
      free(spans);
      return status == CONFIT_OK ? CONFIT_ERR_SCHEMA : status;
    }
    spans[index].line = confit_toml_value_line(atom);
    spans[index].column = confit_toml_value_column(atom);
    for (other = 0U; other < index; ++other) {
      if (strcmp(items[other], items[index]) == 0) {
        confit_component_string_list_clear(items, count);
        free(spans);
        confit_component_diagnostic_set(
            diagnostic, CONFIT_ERR_SCHEMA, confit_toml_value_source(atom),
            confit_toml_value_line(atom), confit_toml_value_column(atom),
            "component manifest list contains a duplicate token");
        return CONFIT_ERR_SCHEMA;
      }
    }
  }
  *total_edges += count;
  *out_items = items;
  *out_spans = spans;
  *out_count = count;
  return CONFIT_OK;
}

static int confit_component_source_path_valid(const char *text, int header) {
  const char *suffix;
  const unsigned char *cursor;
  if (!confit_component_relative_path_valid(text)) return 0;
  suffix = strrchr(text, '.');
  if (suffix == 0 ||
      (header ? strcmp(suffix, ".h") != 0
              : (strcmp(suffix, ".c") != 0 && strcmp(suffix, ".S") != 0 &&
                 strcmp(suffix, ".s") != 0))) return 0;
  for (cursor = (const unsigned char *)text; *cursor != '\0'; ++cursor) {
    if (!((*cursor >= 'a' && *cursor <= 'z') ||
          (*cursor >= 'A' && *cursor <= 'Z') ||
          (*cursor >= '0' && *cursor <= '9') || *cursor == '_' ||
          *cursor == '-' || *cursor == '.' || *cursor == '/')) return 0;
  }
  return 1;
}

static ConfitStatus confit_component_append_owned_file(
    char ***items, size_t *count, size_t maximum, const char *relative,
    int header, const char *project_root, const char *directory,
    const char *makefile, size_t line, ConfitDiagnostic *diagnostic) {
  char **grown;
  char physical[4096];
  char canonical[4096];
  size_t index;
  ConfitStatus status;
  if (!confit_component_source_path_valid(relative, header)) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA, makefile, line, 1U,
        "component Makefile has an unsafe owner-relative source/header path");
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < *count; ++index) {
    if (strcmp((*items)[index], relative) == 0) {
      confit_component_diagnostic_set(
          diagnostic, CONFIT_ERR_SCHEMA, makefile, line, 1U,
          "component Makefile declares a duplicate source/header");
      return CONFIT_ERR_SCHEMA;
    }
  }
  if (*count >= maximum) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA, makefile, line, 1U,
        "component Makefile source/header count exceeds the supported limit");
    return CONFIT_ERR_SCHEMA;
  }
  status = confit_host_path_join(physical, sizeof(physical), directory,
                                 relative, diagnostic);
  if (status != CONFIT_OK ||
      confit_host_path_canonicalize(canonical, sizeof(canonical), physical,
                                    diagnostic) != CONFIT_OK ||
      strcmp(canonical, physical) != 0 ||
      !confit_component_path_within(directory, canonical) ||
      !confit_component_path_within(project_root, canonical) ||
      !confit_host_file_exists(canonical)) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA, makefile, line, 1U,
        "component source/header is missing, symlinked, or outside its owner directory");
    return CONFIT_ERR_SCHEMA;
  }
  grown = (char **)realloc(*items, (*count + 1U) * sizeof(*grown));
  if (grown == 0) return CONFIT_ERR_INTERNAL;
  *items = grown;
  (*items)[*count] = confit_component_strdup(relative);
  if ((*items)[*count] == 0) return CONFIT_ERR_INTERNAL;
  *count += 1U;
  return CONFIT_OK;
}

static int confit_component_include_allowed(ConfitComponentKind kind,
                                            const char *include_name) {
  if (kind == CONFIT_COMPONENT_KIND_KERNEL_FEATURE) {
    return strcmp(include_name, "parus.component.mk") == 0;
  }
  if (kind == CONFIT_COMPONENT_KIND_KERNEL_PROVIDER) {
    return strcmp(include_name, "parus.component.mk") == 0 ||
           strcmp(include_name, "parus.driver.mk") == 0;
  }
  return (kind == CONFIT_COMPONENT_KIND_WORLD_FEATURE ||
          kind == CONFIT_COMPONENT_KIND_WORLD_SERVICE) &&
         strcmp(include_name, "parus.world.mk") == 0;
}

static char *confit_component_trim(char *text) {
  char *end;
  while (*text == ' ') ++text;
  end = text + strlen(text);
  while (end > text && end[-1] == ' ') --end;
  *end = '\0';
  return text;
}

static ConfitStatus confit_component_parse_make_tokens(
    char *tokens, int header, const char *project_root, const char *directory,
    const char *makefile, size_t line, ConfitComponent *component,
    ConfitDiagnostic *diagnostic) {
  char *cursor = tokens;
  ConfitStatus status = CONFIT_OK;
  if (cursor[0] == '\0' || cursor[0] == ' ' ||
      cursor[strlen(cursor) - 1U] == ' ') return CONFIT_ERR_SCHEMA;
  while (status == CONFIT_OK && *cursor != '\0') {
    char *separator = strchr(cursor, ' ');
    if (separator != 0) *separator = '\0';
    status = confit_component_append_owned_file(
        header ? &component->public_headers : &component->sources,
        header ? &component->public_header_count : &component->source_count,
        header ? CONFIT_COMPONENT_MAKE_MAX_HEADERS
               : CONFIT_COMPONENT_MAKE_MAX_SOURCES,
        cursor, header, project_root, directory, makefile, line, diagnostic);
    if (separator != 0) {
      *separator = ' ';
      cursor = separator + 1U;
      if (*cursor == ' ') status = CONFIT_ERR_SCHEMA;
    } else {
      cursor += strlen(cursor);
    }
  }
  return status;
}

static ConfitStatus confit_component_parse_link_tokens(
    char *tokens, const char *makefile, size_t line,
    ConfitComponent *component, ConfitDiagnostic *diagnostic) {
  char *cursor = tokens;
  if (cursor[0] == '\0' || cursor[0] == ' ' ||
      cursor[strlen(cursor) - 1U] == ' ') return CONFIT_ERR_SCHEMA;
  while (*cursor != '\0') {
    char *separator = strchr(cursor, ' ');
    char **grown;
    size_t index;
    if (separator != 0) *separator = '\0';
    if (!confit_component_link_valid(cursor) ||
        component->link_use_count >= CONFIT_COMPONENT_MAX_LIST_ITEMS) {
      if (separator != 0) *separator = ' ';
      confit_component_diagnostic_set(
          diagnostic, CONFIT_ERR_SCHEMA, makefile, line, 1U,
          "LINK_USES contains an unsafe or excessive owner identity");
      return CONFIT_ERR_SCHEMA;
    }
    for (index = 0U; index < component->link_use_count; ++index) {
      if (strcmp(component->link_uses[index], cursor) == 0) {
        if (separator != 0) *separator = ' ';
        confit_component_diagnostic_set(
            diagnostic, CONFIT_ERR_SCHEMA, makefile, line, 1U,
            "LINK_USES contains a duplicate owner identity");
        return CONFIT_ERR_SCHEMA;
      }
    }
    grown = (char **)realloc(
        component->link_uses,
        (component->link_use_count + 1U) * sizeof(*grown));
    if (grown == 0) return CONFIT_ERR_INTERNAL;
    component->link_uses = grown;
    component->link_uses[component->link_use_count] =
        confit_component_strdup(cursor);
    if (component->link_uses[component->link_use_count] == 0)
      return CONFIT_ERR_INTERNAL;
    ++component->link_use_count;
    if (separator == 0) break;
    *separator = ' ';
    cursor = separator + 1U;
    if (*cursor == ' ') return CONFIT_ERR_SCHEMA;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_component_parse_make_statement(
    char *statement, const char *project_root, const char *directory,
    const char *makefile, size_t line, ConfitComponent *component,
    int *seen_api, int *seen_include, ConfitDiagnostic *diagnostic) {
  static const char kApi[] = "PARUS_MK_API = ";
  static const char kSources[] = "SRCS += ";
  static const char kHeaders[] = "PUBLIC_HEADERS += ";
  static const char kLinkUses[] = "LINK_USES += ";
  static const char kIncludePrefix[] = ".include <";
  size_t size = strlen(statement);
  ConfitStatus status = CONFIT_OK;
  if (statement[0] == '\0' || statement[0] == '#') return CONFIT_OK;
  if (*seen_include || strchr(statement, '\t') != 0 ||
      strchr(statement, '$') != 0 || strchr(statement, '#') != 0 ||
      strchr(statement, ':') != 0 || strchr(statement, '!') != 0 ||
      strchr(statement, '?') != 0 || strchr(statement, '*') != 0 ||
      strchr(statement, '[') != 0 || strchr(statement, ']') != 0) {
    status = CONFIT_ERR_SCHEMA;
  } else if (strncmp(statement, kApi, sizeof(kApi) - 1U) == 0) {
    if (*seen_api || strcmp(statement + sizeof(kApi) - 1U, "3") != 0) {
      status = CONFIT_ERR_SCHEMA;
    } else {
      *seen_api = 1;
    }
  } else if (strncmp(statement, kSources, sizeof(kSources) - 1U) == 0) {
    status = confit_component_parse_make_tokens(
        statement + sizeof(kSources) - 1U, 0, project_root, directory,
        makefile, line, component, diagnostic);
  } else if (strncmp(statement, kHeaders, sizeof(kHeaders) - 1U) == 0) {
    status = confit_component_parse_make_tokens(
        statement + sizeof(kHeaders) - 1U, 1, project_root, directory,
        makefile, line, component, diagnostic);
  } else if (strncmp(statement, kLinkUses, sizeof(kLinkUses) - 1U) == 0) {
    status = confit_component_parse_link_tokens(
        statement + sizeof(kLinkUses) - 1U, makefile, line, component,
        diagnostic);
  } else if (size > sizeof(kIncludePrefix) &&
             strncmp(statement, kIncludePrefix,
                     sizeof(kIncludePrefix) - 1U) == 0 &&
             statement[size - 1U] == '>') {
    char include_name[96];
    size_t include_size = size - (sizeof(kIncludePrefix) - 1U) - 1U;
    if (include_size == 0U || include_size >= sizeof(include_name)) {
      status = CONFIT_ERR_SCHEMA;
    } else {
      memcpy(include_name, statement + sizeof(kIncludePrefix) - 1U,
             include_size);
      include_name[include_size] = '\0';
      if (!confit_component_include_allowed(component->kind, include_name)) {
        status = CONFIT_ERR_SCHEMA;
      } else {
        component->build_include = confit_component_strdup(include_name);
        if (component->build_include == 0) return CONFIT_ERR_INTERNAL;
        *seen_include = 1;
      }
    }
  } else {
    status = CONFIT_ERR_SCHEMA;
  }
  if (status != CONFIT_OK && !confit_diagnostic_has_error(diagnostic)) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA, makefile, line, 1U,
        "component Makefile violates restricted Build API v3 grammar");
  }
  return status;
}

static ConfitStatus confit_component_parse_makefile(
    const char *project_root, const char *directory, const char *makefile,
    ConfitComponent *component, ConfitDiagnostic *diagnostic) {
  char *text = 0;
  size_t size = 0U;
  size_t offset = 0U;
  size_t line = 1U;
  size_t statement_line = 1U;
  char logical[CONFIT_COMPONENT_MAKE_MAX_LINE_BYTES + 1U];
  size_t logical_size = 0U;
  int seen_api = 0;
  int seen_include = 0;
  ConfitStatus status = confit_host_read_text_file(makefile, &text, &size,
                                                    diagnostic);
  logical[0] = '\0';
  if (status != CONFIT_OK) return status;
  if (size == 0U || size > CONFIT_COMPONENT_MAKE_MAX_BYTES ||
      memchr(text, '\0', size) != 0) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA, makefile, 0U, 0U,
        "component Makefile violates the bounded text size contract");
    status = CONFIT_ERR_SCHEMA;
    goto done;
  }
  while (offset < size && status == CONFIT_OK) {
    size_t end = offset;
    size_t length;
    int continuation = 0;
    char saved;
    char *part;
    while (end < size && text[end] != '\n') ++end;
    length = end - offset;
    if (length > 0U && text[offset + length - 1U] == '\r') --length;
    if (length > CONFIT_COMPONENT_MAKE_MAX_LINE_BYTES) {
      confit_component_diagnostic_set(
          diagnostic, CONFIT_ERR_SCHEMA, makefile, line, 1U,
          "component Makefile physical line exceeds the supported limit");
      status = CONFIT_ERR_SCHEMA;
      break;
    }
    saved = text[offset + length];
    text[offset + length] = '\0';
    part = confit_component_trim(text + offset);
    if (strchr(part, '\t') != 0) status = CONFIT_ERR_SCHEMA;
    if (status == CONFIT_OK && part[0] != '\0' &&
        part[strlen(part) - 1U] == '\\') {
      continuation = 1;
      part[strlen(part) - 1U] = '\0';
      part = confit_component_trim(part);
    } else if (status == CONFIT_OK && strchr(part, '\\') != 0) {
      status = CONFIT_ERR_SCHEMA;
    }
    if (status == CONFIT_OK && logical_size != 0U &&
        (part[0] == '\0' || part[0] == '#')) status = CONFIT_ERR_SCHEMA;
    if (status == CONFIT_OK && part[0] != '\0') {
      const size_t part_size = strlen(part);
      const size_t separator = logical_size != 0U ? 1U : 0U;
      if (logical_size + separator + part_size >
          CONFIT_COMPONENT_MAKE_MAX_LINE_BYTES) {
        status = CONFIT_ERR_SCHEMA;
      } else {
        if (separator != 0U) logical[logical_size++] = ' ';
        memcpy(logical + logical_size, part, part_size + 1U);
        logical_size += part_size;
      }
    }
    text[offset + length] = saved;
    if (status == CONFIT_OK && !continuation) {
      status = confit_component_parse_make_statement(
          logical, project_root, directory, makefile, statement_line, component,
          &seen_api, &seen_include, diagnostic);
      logical_size = 0U;
      logical[0] = '\0';
      statement_line = line + 1U;
    }
    offset = end < size ? end + 1U : end;
    ++line;
  }
  if (status == CONFIT_OK && logical_size != 0U) status = CONFIT_ERR_SCHEMA;
  if (status == CONFIT_OK &&
      (!seen_api || !seen_include || component->build_include == 0 ||
       ((component->kind == CONFIT_COMPONENT_KIND_KERNEL_PROVIDER ||
         component->kind == CONFIT_COMPONENT_KIND_WORLD_SERVICE) &&
        component->source_count == 0U))) {
    status = CONFIT_ERR_SCHEMA;
  }
  if (status != CONFIT_OK && !confit_diagnostic_has_error(diagnostic)) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA, makefile, line, 1U,
        "component Makefile is incomplete or uses forbidden Build API v3 syntax");
  }
done:
  confit_host_free(text);
  return status;
}

static ConfitStatus confit_component_parse_manifest(
    const char *project_root, const char *manifest_physical,
    const char *manifest_logical, size_t *total_edges, ConfitComponent *out,
    ConfitDiagnostic *diagnostic) {
  static const char *const root_keys[] = {
      "schema_version", "component", "selection", "interfaces"};
  static const char *const component_keys[] = {
      "id", "kind", "summary", "owner"};
  static const char *const selection_keys[] = {
      "requires", "provides", "conflicts", "default"};
  static const char *const interface_keys[] = {
      "kapi_requires", "kapi_provides"};
  ConfitTomlDocument *document = 0;
  const ConfitTomlValue *root;
  const ConfitTomlValue *component_table;
  const ConfitTomlValue *selection_table;
  const ConfitTomlValue *interface_table;
  const ConfitTomlValue *value;
  char directory[4096];
  char makefile_physical[4096];
  char makefile_canonical[4096];
  const char *separator;
  int64_t schema_version = 0;
  int default_value = 1;
  ConfitStatus status;
  memset(out, 0, sizeof(*out));
  status = confit_toml_parse_file(manifest_physical, &document, diagnostic);
  if (status != CONFIT_OK) return status;
  root = confit_toml_document_root(document);
  component_table = confit_toml_table_find(root, "component");
  selection_table = confit_toml_table_find(root, "selection");
  interface_table = confit_toml_table_find(root, "interfaces");
  value = confit_toml_table_find(root, "schema_version");
  status = confit_component_validate_table_keys(
      root, root_keys, sizeof(root_keys) / sizeof(root_keys[0]),
      manifest_physical, diagnostic);
  if (status == CONFIT_OK) status = confit_component_validate_table_keys(
      component_table, component_keys,
      sizeof(component_keys) / sizeof(component_keys[0]), manifest_physical,
      diagnostic);
  if (status == CONFIT_OK) status = confit_component_validate_table_keys(
      selection_table, selection_keys,
      sizeof(selection_keys) / sizeof(selection_keys[0]), manifest_physical,
      diagnostic);
  if (status == CONFIT_OK) status = confit_component_validate_table_keys(
      interface_table, interface_keys,
      sizeof(interface_keys) / sizeof(interface_keys[0]), manifest_physical,
      diagnostic);
  if (status != CONFIT_OK) goto invalid;
  if (value == 0 || !confit_toml_value_int64(value, &schema_version) ||
      schema_version != 3) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA,
        value != 0 ? confit_toml_value_source(value) : manifest_physical,
        value != 0 ? confit_toml_value_line(value) : 1U,
        value != 0 ? confit_toml_value_column(value) : 1U,
        "component manifest schema_version must be exactly 3");
    status = CONFIT_ERR_SCHEMA;
    goto invalid;
  }
  status = confit_component_copy_toml_string(
      confit_toml_table_find(component_table, "id"),
      CONFIT_COMPONENT_MAX_ATOM_BYTES, &out->id, diagnostic);
  if (status == CONFIT_OK) status = confit_component_copy_toml_string(
      confit_toml_table_find(component_table, "summary"),
      CONFIT_COMPONENT_MAX_SUMMARY_BYTES, &out->summary, diagnostic);
  if (status == CONFIT_OK) status = confit_component_copy_toml_string(
      confit_toml_table_find(component_table, "owner"),
      CONFIT_COMPONENT_MAX_ATOM_BYTES, &out->owner, diagnostic);
  if (status != CONFIT_OK || !confit_component_identifier_valid(out->id) ||
      !confit_component_identifier_valid(out->owner) ||
      !confit_component_text_safe(out->summary,
                                  CONFIT_COMPONENT_MAX_SUMMARY_BYTES)) {
    status = CONFIT_ERR_SCHEMA;
    goto invalid;
  }
  {
    char *kind_text = 0;
    status = confit_component_copy_toml_string(
        confit_toml_table_find(component_table, "kind"),
        CONFIT_COMPONENT_MAX_ATOM_BYTES, &kind_text, diagnostic);
    if (status == CONFIT_OK) out->kind = confit_component_kind_parse(kind_text);
    free(kind_text);
    if (status != CONFIT_OK || out->kind == CONFIT_COMPONENT_KIND_INVALID) {
      status = CONFIT_ERR_SCHEMA;
      goto invalid;
    }
  }
  value = confit_toml_table_find(selection_table, "default");
  if (value == 0 || !confit_toml_value_bool(value, &default_value) ||
      default_value != 0) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA,
        value != 0 ? confit_toml_value_source(value) : manifest_physical,
        value != 0 ? confit_toml_value_line(value) : 1U,
        value != 0 ? confit_toml_value_column(value) : 1U,
        "component selection.default must be the boolean false");
    status = CONFIT_ERR_SCHEMA;
    goto invalid;
  }
  status = confit_component_parse_atom_list(
      confit_toml_table_find(selection_table, "requires"),
      CONFIT_COMPONENT_ATOM_FEATURE, &out->feature_requires,
      &out->feature_requirement_spans, &out->feature_requirement_count,
      total_edges, diagnostic);
  if (status == CONFIT_OK) status = confit_component_parse_atom_list(
      confit_toml_table_find(selection_table, "provides"),
      CONFIT_COMPONENT_ATOM_FEATURE, &out->feature_provides,
      &out->feature_provide_spans, &out->feature_provide_count, total_edges,
      diagnostic);
  if (status == CONFIT_OK) status = confit_component_parse_atom_list(
      confit_toml_table_find(selection_table, "conflicts"),
      CONFIT_COMPONENT_ATOM_FEATURE, &out->feature_conflicts,
      &out->feature_conflict_spans, &out->feature_conflict_count, total_edges,
      diagnostic);
  if (status == CONFIT_OK) status = confit_component_parse_atom_list(
      confit_toml_table_find(interface_table, "kapi_requires"),
      CONFIT_COMPONENT_ATOM_KAPI, &out->kapi_requires,
      &out->kapi_requirement_spans, &out->kapi_requirement_count, total_edges,
      diagnostic);
  if (status == CONFIT_OK) status = confit_component_parse_atom_list(
      confit_toml_table_find(interface_table, "kapi_provides"),
      CONFIT_COMPONENT_ATOM_KAPI, &out->kapi_provides,
      &out->kapi_provide_spans, &out->kapi_provide_count, total_edges,
      diagnostic);
  if (status != CONFIT_OK) goto invalid;
  separator = strrchr(manifest_physical, '/');
  if (separator == 0 ||
      (size_t)(separator - manifest_physical) >= sizeof(directory)) {
    status = CONFIT_ERR_SCHEMA;
    goto invalid;
  }
  memcpy(directory, manifest_physical,
         (size_t)(separator - manifest_physical));
  directory[separator - manifest_physical] = '\0';
  status = confit_host_path_join(makefile_physical, sizeof(makefile_physical),
                                 directory, "Makefile", diagnostic);
  if (status != CONFIT_OK ||
      confit_host_path_canonicalize(makefile_canonical,
                                    sizeof(makefile_canonical),
                                    makefile_physical, diagnostic) != CONFIT_OK ||
      strcmp(makefile_canonical, makefile_physical) != 0 ||
      !confit_component_path_within(directory, makefile_canonical) ||
      !confit_component_path_within(project_root, makefile_canonical)) {
    status = CONFIT_ERR_SCHEMA;
    goto invalid;
  }
  out->makefile_path = confit_component_strdup(
      makefile_canonical + strlen(project_root) + 1U);
  if (out->makefile_path == 0) {
    status = CONFIT_ERR_INTERNAL;
    goto invalid;
  }
  status = confit_component_parse_makefile(
      project_root, directory, makefile_canonical, out, diagnostic);
  if (status != CONFIT_OK) goto invalid;
  out->manifest_path = confit_component_strdup(manifest_logical);
  if (out->manifest_path == 0) {
    status = CONFIT_ERR_INTERNAL;
    goto invalid;
  }
  confit_toml_document_free(document);
  return CONFIT_OK;

invalid:
  if (status == CONFIT_OK || !confit_diagnostic_has_error(diagnostic)) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA, manifest_physical, 1U, 1U,
        "component.toml is not the closed selectable schema v3");
  }
  confit_toml_document_free(document);
  confit_component_clear(out);
  return status == CONFIT_OK ? CONFIT_ERR_SCHEMA : status;
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

size_t confit_component_catalog_find_feature_providers(
    const ConfitComponentCatalog *catalog, const char *feature,
    const ConfitComponent **out_candidates, size_t capacity) {
  size_t index;
  size_t count = 0U;
  if (catalog == 0 || feature == 0) return 0U;
  for (index = 0U; index < catalog->component_count; ++index) {
    const ConfitComponent *component = &catalog->components[index];
    if (confit_component_list_contains(component->feature_provides,
                                       component->feature_provide_count,
                                       feature)) {
      if (out_candidates != 0 && count < capacity) {
        out_candidates[count] = component;
      }
      ++count;
    }
  }
  return count;
}

static int confit_component_owned_logical_path(
    const ConfitComponent *component, const char *relative, char *out,
    size_t out_size) {
  const char *separator = strrchr(component->makefile_path, '/');
  size_t directory_size;
  size_t relative_size;
  if (separator == 0) return 0;
  directory_size = (size_t)(separator - component->makefile_path);
  relative_size = strlen(relative);
  if (directory_size + 1U + relative_size + 1U > out_size) return 0;
  memcpy(out, component->makefile_path, directory_size);
  out[directory_size] = '/';
  memcpy(out + directory_size + 1U, relative, relative_size + 1U);
  return 1;
}

static ConfitStatus confit_component_catalog_validate(
    const ConfitComponentCatalog *catalog, ConfitDiagnostic *diagnostic) {
  size_t index;
  for (index = 0U; index < catalog->component_count; ++index) {
    const ConfitComponent *component = &catalog->components[index];
    size_t source_index;
    size_t other;
    for (source_index = 0U; source_index < component->source_count;
         ++source_index) {
      char source_path[2048];
      if (!confit_component_owned_logical_path(
              component, component->sources[source_index], source_path,
              sizeof(source_path))) return CONFIT_ERR_INTERNAL;
      for (other = index + 1U; other < catalog->component_count; ++other) {
        const ConfitComponent *candidate = &catalog->components[other];
        size_t candidate_index;
        for (candidate_index = 0U;
             candidate_index < candidate->source_count; ++candidate_index) {
          char candidate_path[2048];
          if (!confit_component_owned_logical_path(
                  candidate, candidate->sources[candidate_index], candidate_path,
                  sizeof(candidate_path))) return CONFIT_ERR_INTERNAL;
          if (strcmp(source_path, candidate_path) == 0) {
            confit_component_diagnostic_set(
                diagnostic, CONFIT_ERR_SCHEMA, component->manifest_path, 0U,
                0U, "one source file has multiple component owners");
            return CONFIT_ERR_SCHEMA;
          }
        }
      }
    }
  }
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
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA, project->config_root, 0U, 0U,
        "component root count exceeds the supported limit");
    return CONFIT_ERR_SCHEMA;
  }
  memset(out_catalog, 0, sizeof(*out_catalog));
  out_catalog->project_root = confit_component_strdup(project->project_root);
  if (out_catalog->project_root == 0) return CONFIT_ERR_INTERNAL;
  for (root_index = 0U;
       status == CONFIT_OK && root_index < project->component_roots.count;
       ++root_index) {
    char root_path[4096];
    char **paths = 0;
    size_t path_count = 0U;
    size_t path_index;
    if (!confit_component_relative_path_valid(
            project->component_roots.items[root_index])) {
      confit_component_diagnostic_set(
          diagnostic, CONFIT_ERR_SCHEMA, project->config_root, 0U, 0U,
          "project component_roots contains an unsafe path");
      status = CONFIT_ERR_SCHEMA;
      break;
    }
    status = confit_host_path_join(
        root_path, sizeof(root_path), project->project_root,
        project->component_roots.items[root_index], diagnostic);
    if (status == CONFIT_OK) {
      status = confit_host_list_named_files_recursive(
          root_path, kManifestName, CONFIT_COMPONENT_MAX_DEPTH,
          CONFIT_COMPONENT_MAX_COUNT, CONFIT_COMPONENT_MAX_FILE_BYTES, &paths,
          &path_count, diagnostic);
    }
    for (path_index = 0U;
         status == CONFIT_OK && path_index < path_count; ++path_index) {
      const char *logical;
      ConfitComponent parsed;
      ConfitComponent *grown;
      size_t existing;
      if (!confit_component_path_within(project->project_root,
                                        paths[path_index])) {
        confit_component_diagnostic_set(
            diagnostic, CONFIT_ERR_SCHEMA, paths[path_index], 0U, 0U,
            "component manifest escapes the project root");
        status = CONFIT_ERR_SCHEMA;
        break;
      }
      logical = paths[path_index] + strlen(project->project_root) + 1U;
      status = confit_component_parse_manifest(
          project->project_root, paths[path_index], logical, &total_edges,
          &parsed, diagnostic);
      if (status != CONFIT_OK) break;
      if (out_catalog->component_count >= CONFIT_COMPONENT_MAX_COUNT) {
        confit_component_clear(&parsed);
        confit_component_diagnostic_set(
            diagnostic, CONFIT_ERR_SCHEMA, paths[path_index], 0U, 0U,
            "component catalog count exceeds the supported limit");
        status = CONFIT_ERR_SCHEMA;
        break;
      }
      for (existing = 0U; existing < out_catalog->component_count; ++existing) {
        if (strcmp(out_catalog->components[existing].id, parsed.id) == 0 ||
            strcmp(out_catalog->components[existing].manifest_path,
                   parsed.manifest_path) == 0) {
          confit_component_clear(&parsed);
          confit_component_diagnostic_set(
              diagnostic, CONFIT_ERR_SCHEMA, paths[path_index], 0U, 0U,
              "component catalog has a duplicate ID or canonical path");
          status = CONFIT_ERR_SCHEMA;
          break;
        }
      }
      if (status != CONFIT_OK) break;
      grown = (ConfitComponent *)realloc(
          out_catalog->components,
          (out_catalog->component_count + 1U) *
              sizeof(*out_catalog->components));
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
  if (status == CONFIT_OK) {
    status = confit_component_catalog_validate(out_catalog, diagnostic);
  }
  if (status != CONFIT_OK) confit_component_catalog_clear(out_catalog);
  return status;
}

static ConfitStatus confit_component_closure_append_string(
    char ***items, size_t *count, const char *text) {
  char **grown;
  char *copy;
  size_t index;
  for (index = 0U; index < *count; ++index) {
    if (strcmp((*items)[index], text) == 0) return CONFIT_OK;
  }
  if (*count >= CONFIT_COMPONENT_MAX_TOTAL_EDGES) return CONFIT_ERR_SCHEMA;
  copy = confit_component_strdup(text);
  if (copy == 0) return CONFIT_ERR_INTERNAL;
  grown = (char **)realloc(*items, (*count + 1U) * sizeof(*grown));
  if (grown == 0) {
    free(copy);
    return CONFIT_ERR_INTERNAL;
  }
  grown[*count] = copy;
  *items = grown;
  *count += 1U;
  return CONFIT_OK;
}

static ConfitStatus confit_component_reason_append(
    ConfitComponentClosure *closure, ConfitComponentReasonKind kind,
    ConfitComponentProviderSelection provider_selection,
    const char *component_id, const char *from_id, const char *requirement,
    const char *source_path, size_t source_line, size_t source_column) {
  ConfitComponentReason *grown;
  ConfitComponentReason *reason;
  if (closure->reason_count >= CONFIT_COMPONENT_MAX_TOTAL_EDGES +
                                   CONFIT_COMPONENT_MAX_LIST_ITEMS) {
    return CONFIT_ERR_SCHEMA;
  }
  grown = (ConfitComponentReason *)realloc(
      closure->reasons,
      (closure->reason_count + 1U) * sizeof(*closure->reasons));
  if (grown == 0) return CONFIT_ERR_INTERNAL;
  closure->reasons = grown;
  reason = &closure->reasons[closure->reason_count];
  memset(reason, 0, sizeof(*reason));
  reason->kind = kind;
  reason->provider_selection = provider_selection;
  reason->component_id = confit_component_strdup(component_id);
  reason->from_id = from_id != 0 ? confit_component_strdup(from_id) : 0;
  reason->requirement = confit_component_strdup(requirement);
  reason->source_path = confit_component_strdup(
      source_path != 0 ? source_path : requirement);
  reason->source_line = source_line;
  reason->source_column = source_column;
  if (reason->component_id == 0 ||
      (from_id != 0 && reason->from_id == 0) ||
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

static const ConfitComponentProviderChoice *confit_component_choice_find(
    const ConfitComponentResolveContext *context, const char *feature) {
  size_t index;
  for (index = 0U; index < context->choice_count; ++index) {
    if (strcmp(context->choices[index].feature, feature) == 0) {
      return &context->choices[index];
    }
  }
  return 0;
}

static ConfitStatus confit_component_select_component(
    ConfitComponentResolveContext *context, const ConfitComponent *component,
    size_t depth);

static ConfitStatus confit_component_select_feature(
    ConfitComponentResolveContext *context, const char *feature,
    const ConfitComponent *from, ConfitComponentReasonKind reason_kind,
    const char *source_path, size_t source_line, size_t source_column,
    size_t depth, int optional) {
  const ConfitComponentProviderChoice *choice =
      confit_component_choice_find(context, feature);
  const ConfitComponent *candidate = 0;
  ConfitComponentProviderSelection selection =
      CONFIT_COMPONENT_PROVIDER_SELECTION_NONE;
  size_t candidate_count;
  ConfitStatus status;
  if (!confit_component_feature_valid(feature)) {
    confit_component_diagnostic_set(
        context->diagnostic, CONFIT_ERR_SCHEMA, source_path, source_line,
        source_column, "selection contains an invalid versioned feature");
    return CONFIT_ERR_SCHEMA;
  }
  candidate_count = confit_component_catalog_find_feature_providers(
      context->catalog, feature, &candidate, 1U);
  if (choice != 0) {
    candidate = confit_component_catalog_find(context->catalog,
                                               choice->component_id);
    if (candidate == 0 ||
        !confit_component_list_contains(candidate->feature_provides,
                                        candidate->feature_provide_count,
                                        feature)) {
      confit_component_diagnostic_set(
          context->diagnostic, CONFIT_ERR_SCHEMA, choice->source_path,
          choice->source_line, choice->source_column,
          "explicit provider does not provide the requested feature");
      return CONFIT_ERR_SCHEMA;
    }
    selection = CONFIT_COMPONENT_PROVIDER_SELECTION_EXPLICIT;
  } else if (candidate_count == 0U) {
    if (optional) {
      return confit_component_closure_append_string(
          &context->closure->absent_optional_features,
          &context->closure->absent_optional_feature_count, feature);
    }
    confit_component_diagnostic_set(
        context->diagnostic, CONFIT_ERR_SCHEMA, source_path, source_line,
        source_column, "required feature has no available provider");
    return CONFIT_ERR_SCHEMA;
  } else if (candidate_count != 1U) {
    confit_component_diagnostic_set(
        context->diagnostic, CONFIT_ERR_SCHEMA, source_path, source_line,
        source_column,
        "feature provider is ambiguous and requires an explicit profile mapping");
    return CONFIT_ERR_SCHEMA;
  } else {
    selection = CONFIT_COMPONENT_PROVIDER_SELECTION_UNIQUE;
  }
  status = confit_component_reason_append(
      context->closure, reason_kind, selection, candidate->id,
      from != 0 ? from->id : 0, feature,
      choice != 0 ? choice->source_path
                  : (source_path != 0 ? source_path
                                      : candidate->manifest_path),
      choice != 0 ? choice->source_line : source_line,
      choice != 0 ? choice->source_column : source_column);
  if (status != CONFIT_OK) return status;
  return confit_component_select_component(context, candidate, depth);
}

static ConfitStatus confit_component_select_component(
    ConfitComponentResolveContext *context, const ConfitComponent *component,
    size_t depth) {
  size_t slot = (size_t)(component - context->catalog->components);
  size_t index;
  ConfitStatus status = CONFIT_OK;
  if (depth > CONFIT_COMPONENT_MAX_DEPTH) {
    confit_component_diagnostic_set(
        context->diagnostic, CONFIT_ERR_SCHEMA, component->manifest_path, 0U,
        0U, "component feature graph exceeds the supported depth");
    return CONFIT_ERR_SCHEMA;
  }
  if (context->state[slot] == 2U) return CONFIT_OK;
  if (context->state[slot] == 1U) {
    confit_component_diagnostic_set(
        context->diagnostic, CONFIT_ERR_SCHEMA, component->manifest_path, 0U,
        0U, "component feature graph contains a cycle");
    return CONFIT_ERR_SCHEMA;
  }
  context->state[slot] = 1U;
  for (index = 0U;
       status == CONFIT_OK && index < component->feature_requirement_count;
       ++index) {
    status = confit_component_select_feature(
        context, component->feature_requires[index], component,
        CONFIT_COMPONENT_REASON_FEATURE_REQUIREMENT, component->manifest_path,
        component->feature_requirement_spans[index].line,
        component->feature_requirement_spans[index].column, depth + 1U, 0);
  }
  if (status == CONFIT_OK) {
    const ConfitComponent **grown = (const ConfitComponent **)realloc(
        context->closure->ordered,
        (context->closure->component_count + 1U) *
            sizeof(*context->closure->ordered));
    if (grown == 0) status = CONFIT_ERR_INTERNAL;
    else {
      context->closure->ordered = grown;
      context->closure->ordered[context->closure->component_count++] =
          component;
      context->state[slot] = 2U;
    }
  }
  if (status != CONFIT_OK) context->state[slot] = 0U;
  return status;
}

const ConfitComponent *confit_component_closure_find_kapi_provider(
    const ConfitComponentClosure *closure, const char *kapi) {
  const ConfitComponent *provider = 0;
  size_t index;
  if (closure == 0 || kapi == 0) return 0;
  for (index = 0U; index < closure->component_count; ++index) {
    const ConfitComponent *component = closure->ordered[index];
    if (confit_component_list_contains(component->kapi_provides,
                                       component->kapi_provide_count, kapi)) {
      if (provider != 0) return 0;
      provider = component;
    }
  }
  return provider;
}

static size_t confit_component_closure_kapi_provider_count(
    const ConfitComponentClosure *closure, const char *kapi,
    const ConfitComponent **out_provider) {
  size_t count = 0U;
  size_t index;
  if (out_provider != 0) *out_provider = 0;
  for (index = 0U; index < closure->component_count; ++index) {
    const ConfitComponent *component = closure->ordered[index];
    if (confit_component_list_contains(component->kapi_provides,
                                       component->kapi_provide_count, kapi)) {
      if (out_provider != 0 && count == 0U) *out_provider = component;
      ++count;
    }
  }
  return count;
}

static ConfitStatus confit_component_validate_selected_closure(
    ConfitComponentClosure *closure, ConfitDiagnostic *diagnostic) {
  size_t index;
  ConfitStatus status = CONFIT_OK;
  for (index = 0U; status == CONFIT_OK && index < closure->component_count;
       ++index) {
    const ConfitComponent *component = closure->ordered[index];
    size_t item;
    for (item = 0U; status == CONFIT_OK &&
                    item < component->feature_provide_count; ++item) {
      size_t provider_count = 0U;
      size_t other;
      for (other = 0U; other < closure->component_count; ++other) {
        if (confit_component_list_contains(
                closure->ordered[other]->feature_provides,
                closure->ordered[other]->feature_provide_count,
                component->feature_provides[item])) ++provider_count;
      }
      if (provider_count != 1U) {
        confit_component_diagnostic_set(
            diagnostic, CONFIT_ERR_SCHEMA, component->manifest_path,
            component->feature_provide_spans[item].line,
            component->feature_provide_spans[item].column,
            "selected closure contains multiple providers for one feature");
        status = CONFIT_ERR_SCHEMA;
      }
    }
    for (item = 0U; status == CONFIT_OK &&
                    item < component->feature_conflict_count; ++item) {
      size_t other;
      for (other = 0U; other < closure->component_count; ++other) {
        if (confit_component_list_contains(
                closure->ordered[other]->feature_provides,
                closure->ordered[other]->feature_provide_count,
                component->feature_conflicts[item])) {
          confit_component_diagnostic_set(
              diagnostic, CONFIT_ERR_SCHEMA, component->manifest_path,
              component->feature_conflict_spans[item].line,
              component->feature_conflict_spans[item].column,
              "selected closure violates a component feature conflict");
          status = CONFIT_ERR_SCHEMA;
          break;
        }
      }
    }
    for (item = 0U; status == CONFIT_OK &&
                    item < component->kapi_provide_count; ++item) {
      status = confit_component_closure_append_string(
          &closure->kapi_provides, &closure->kapi_provide_count,
          component->kapi_provides[item]);
    }
    for (item = 0U; status == CONFIT_OK &&
                    item < component->kapi_requirement_count; ++item) {
      const ConfitComponent *provider = 0;
      size_t provider_count = confit_component_closure_kapi_provider_count(
          closure, component->kapi_requires[item], &provider);
      if (provider_count > 1U) {
        confit_component_diagnostic_set(
            diagnostic, CONFIT_ERR_SCHEMA, component->manifest_path,
            component->kapi_requirement_spans[item].line,
            component->kapi_requirement_spans[item].column,
            "selected component KAPI requirement has multiple selected providers");
        status = CONFIT_ERR_SCHEMA;
      } else {
        status = confit_component_closure_append_string(
            &closure->kapi_requires, &closure->kapi_requirement_count,
            component->kapi_requires[item]);
        if (status == CONFIT_OK && provider != 0) {
          status = confit_component_reason_append(
              closure, CONFIT_COMPONENT_REASON_KAPI_REQUIREMENT,
              CONFIT_COMPONENT_PROVIDER_SELECTION_NONE, provider->id,
              component->id, component->kapi_requires[item],
              component->manifest_path,
              component->kapi_requirement_spans[item].line,
              component->kapi_requirement_spans[item].column);
        }
      }
    }
  }
  return status;
}

static int confit_component_string_ptr_compare(const void *left,
                                               const void *right) {
  const char *const *a = (const char *const *)left;
  const char *const *b = (const char *const *)right;
  return strcmp(*a, *b);
}

static ConfitStatus confit_component_copy_feature_set(
    const char *const *features, size_t count, char ***out,
    ConfitDiagnostic *diagnostic) {
  char **items = 0;
  size_t index;
  if (count > CONFIT_COMPONENT_MAX_LIST_ITEMS ||
      (count > 0U && features == 0)) return CONFIT_ERR_INVALID_ARGUMENT;
  if (count > 0U) {
    items = (char **)calloc(count, sizeof(*items));
    if (items == 0) return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < count; ++index) {
    if (!confit_component_feature_valid(features[index])) {
      confit_component_diagnostic_set(
          diagnostic, CONFIT_ERR_SCHEMA, features[index], 0U, 0U,
          "profile contains an invalid versioned feature");
      confit_component_string_list_clear(items, count);
      return CONFIT_ERR_SCHEMA;
    }
    items[index] = confit_component_strdup(features[index]);
    if (items[index] == 0) {
      confit_component_string_list_clear(items, count);
      return CONFIT_ERR_INTERNAL;
    }
  }
  if (count > 1U) qsort(items, count, sizeof(*items),
                        confit_component_string_ptr_compare);
  for (index = 1U; index < count; ++index) {
    if (strcmp(items[index - 1U], items[index]) == 0) {
      confit_component_diagnostic_set(
          diagnostic, CONFIT_ERR_SCHEMA, items[index], 0U, 0U,
          "profile feature list contains a duplicate");
      confit_component_string_list_clear(items, count);
      return CONFIT_ERR_SCHEMA;
    }
  }
  *out = items;
  return CONFIT_OK;
}

static ConfitStatus confit_component_validate_choices(
    const ConfitComponentCatalog *catalog,
    const ConfitComponentProviderChoice *choices, size_t choice_count,
    ConfitDiagnostic *diagnostic) {
  size_t index;
  if (choice_count > CONFIT_COMPONENT_MAX_LIST_ITEMS ||
      (choice_count > 0U && choices == 0)) return CONFIT_ERR_INVALID_ARGUMENT;
  for (index = 0U; index < choice_count; ++index) {
    const ConfitComponent *component;
    size_t other;
    if (!confit_component_feature_valid(choices[index].feature) ||
        !confit_component_identifier_valid(choices[index].component_id)) {
      confit_component_diagnostic_set(
          diagnostic, CONFIT_ERR_SCHEMA, choices[index].source_path,
          choices[index].source_line, choices[index].source_column,
          "profile provider mapping has an invalid feature or component ID");
      return CONFIT_ERR_SCHEMA;
    }
    for (other = 0U; other < index; ++other) {
      if (strcmp(choices[other].feature, choices[index].feature) == 0) {
        confit_component_diagnostic_set(
            diagnostic, CONFIT_ERR_SCHEMA, choices[index].source_path,
            choices[index].source_line, choices[index].source_column,
            "profile has duplicate provider mappings for one feature");
        return CONFIT_ERR_SCHEMA;
      }
    }
    component = confit_component_catalog_find(catalog,
                                               choices[index].component_id);
    if (component == 0 ||
        !confit_component_list_contains(component->feature_provides,
                                        component->feature_provide_count,
                                        choices[index].feature)) {
      confit_component_diagnostic_set(
          diagnostic, CONFIT_ERR_SCHEMA, choices[index].source_path,
          choices[index].source_line, choices[index].source_column,
          "profile provider mapping names a non-provider component");
      return CONFIT_ERR_SCHEMA;
    }
  }
  return CONFIT_OK;
}

ConfitStatus confit_component_catalog_resolve_features(
    const ConfitComponentCatalog *catalog,
    const char *const *required_features, size_t required_feature_count,
    const char *const *optional_features, size_t optional_feature_count,
    const ConfitComponentProviderChoice *provider_choices,
    size_t provider_choice_count, ConfitComponentClosure *out_closure,
    ConfitDiagnostic *diagnostic) {
  ConfitComponentResolveContext context;
  char **required = 0;
  char **optional = 0;
  size_t index;
  ConfitStatus status;
  if (catalog == 0 || out_closure == 0) return CONFIT_ERR_INVALID_ARGUMENT;
  memset(out_closure, 0, sizeof(*out_closure));
  status = confit_component_copy_feature_set(
      required_features, required_feature_count, &required, diagnostic);
  if (status == CONFIT_OK) status = confit_component_copy_feature_set(
      optional_features, optional_feature_count, &optional, diagnostic);
  if (status == CONFIT_OK) status = confit_component_validate_choices(
      catalog, provider_choices, provider_choice_count, diagnostic);
  if (status != CONFIT_OK) goto done;
  out_closure->root_features = required;
  out_closure->root_feature_count = required_feature_count;
  required = 0;
  memset(&context, 0, sizeof(context));
  context.catalog = catalog;
  context.choices = provider_choices;
  context.choice_count = provider_choice_count;
  context.closure = out_closure;
  context.diagnostic = diagnostic;
  context.state = (unsigned char *)calloc(catalog->component_count,
                                           sizeof(*context.state));
  if (catalog->component_count > 0U && context.state == 0) {
    status = CONFIT_ERR_INTERNAL;
    goto done;
  }
  for (index = 0U; status == CONFIT_OK && index < required_feature_count;
       ++index) {
    status = confit_component_select_feature(
        &context, out_closure->root_features[index], 0,
        CONFIT_COMPONENT_REASON_ROOT_FEATURE, 0, 0U, 0U, 1U, 0);
  }
  for (index = 0U; status == CONFIT_OK && index < optional_feature_count;
       ++index) {
    status = confit_component_select_feature(
        &context, optional[index], 0, CONFIT_COMPONENT_REASON_ROOT_FEATURE,
        0, 0U, 0U, 1U, 1);
  }
  if (status == CONFIT_OK) status = confit_component_validate_selected_closure(
      out_closure, diagnostic);
  free(context.state);
done:
  confit_component_string_list_clear(required, required_feature_count);
  confit_component_string_list_clear(optional, optional_feature_count);
  if (status != CONFIT_OK) confit_component_closure_clear(out_closure);
  return status;
}

static int confit_component_provider_choice_compare(const void *left,
                                                    const void *right) {
  const ConfitComponentProviderChoice *a =
      (const ConfitComponentProviderChoice *)left;
  const ConfitComponentProviderChoice *b =
      (const ConfitComponentProviderChoice *)right;
  return strcmp(a->feature, b->feature);
}

static void confit_component_provider_choices_clear(
    ConfitComponentProviderChoice *choices, size_t count) {
  size_t index;
  for (index = 0U; index < count; ++index) {
    free((char *)choices[index].feature);
    free((char *)choices[index].component_id);
  }
  free(choices);
}

ConfitStatus confit_component_catalog_resolve_profile_file(
    const ConfitComponentCatalog *catalog, const char *profile_path,
    ConfitComponentClosure *out_closure, ConfitDiagnostic *diagnostic) {
  static const char *const root_keys[] = {
      "schema_version", "profile", "features", "providers"};
  static const char *const profile_keys[] = {"id", "target"};
  static const char *const feature_keys[] = {"enable"};
  char canonical[4096];
  ConfitTomlDocument *document = 0;
  const ConfitTomlValue *root;
  const ConfitTomlValue *profile;
  const ConfitTomlValue *features;
  const ConfitTomlValue *providers;
  const ConfitTomlValue *value;
  char *profile_id = 0;
  char *target_id = 0;
  char **required = 0;
  ConfitComponentSourceSpan *required_spans = 0;
  size_t required_count = 0U;
  size_t total_edges = 0U;
  ConfitComponentProviderChoice *choices = 0;
  size_t choice_count = 0U;
  int64_t schema_version = 0;
  size_t index;
  ConfitStatus status;

  if (catalog == 0 || profile_path == 0 || out_closure == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  memset(out_closure, 0, sizeof(*out_closure));
  status = confit_host_path_canonicalize(canonical, sizeof(canonical),
                                         profile_path, diagnostic);
  if (status != CONFIT_OK || strcmp(canonical, profile_path) != 0 ||
      !confit_component_path_within(catalog->project_root, canonical)) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA, profile_path, 0U, 0U,
        "schema v3 profile is symlinked or escapes the project root");
    return CONFIT_ERR_SCHEMA;
  }
  status = confit_toml_parse_file(profile_path, &document, diagnostic);
  if (status != CONFIT_OK) return status;
  root = confit_toml_document_root(document);
  profile = confit_toml_table_find(root, "profile");
  features = confit_toml_table_find(root, "features");
  providers = confit_toml_table_find(root, "providers");
  value = confit_toml_table_find(root, "schema_version");
  status = confit_component_validate_table_keys(
      root, root_keys, sizeof(root_keys) / sizeof(root_keys[0]), profile_path,
      diagnostic);
  if (status == CONFIT_OK) status = confit_component_validate_table_keys(
      profile, profile_keys, sizeof(profile_keys) / sizeof(profile_keys[0]),
      profile_path, diagnostic);
  if (status == CONFIT_OK) status = confit_component_validate_table_keys(
      features, feature_keys, sizeof(feature_keys) / sizeof(feature_keys[0]),
      profile_path, diagnostic);
  if (status != CONFIT_OK) goto invalid;
  if (value == 0 || !confit_toml_value_int64(value, &schema_version) ||
      schema_version != 3) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA,
        value != 0 ? confit_toml_value_source(value) : profile_path,
        value != 0 ? confit_toml_value_line(value) : 1U,
        value != 0 ? confit_toml_value_column(value) : 1U,
        "component profile schema_version must be exactly 3");
    status = CONFIT_ERR_SCHEMA;
    goto invalid;
  }
  if (providers == 0 ||
      confit_toml_value_type(providers) != CONFIT_TOML_VALUE_TABLE ||
      confit_toml_table_size(providers) > CONFIT_COMPONENT_MAX_LIST_ITEMS) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA,
        providers != 0 ? confit_toml_value_source(providers) : profile_path,
        providers != 0 ? confit_toml_value_line(providers) : 1U,
        providers != 0 ? confit_toml_value_column(providers) : 1U,
        "component profile has an invalid bounded providers table");
    status = CONFIT_ERR_SCHEMA;
    goto invalid;
  }
  status = confit_component_copy_toml_string(
      confit_toml_table_find(profile, "id"),
      CONFIT_COMPONENT_MAX_ATOM_BYTES, &profile_id, diagnostic);
  if (status == CONFIT_OK) status = confit_component_copy_toml_string(
      confit_toml_table_find(profile, "target"),
      CONFIT_COMPONENT_MAX_ATOM_BYTES, &target_id, diagnostic);
  if (status != CONFIT_OK ||
      !confit_component_identifier_valid(profile_id) ||
      !confit_component_identifier_valid(target_id)) {
    status = CONFIT_ERR_SCHEMA;
    goto invalid;
  }
  status = confit_component_parse_atom_list(
      confit_toml_table_find(features, "enable"),
      CONFIT_COMPONENT_ATOM_FEATURE, &required, &required_spans,
      &required_count, &total_edges, diagnostic);
  if (status != CONFIT_OK) goto invalid;
  choice_count = confit_toml_table_size(providers);
  if (choice_count > 0U) {
    choices = (ConfitComponentProviderChoice *)calloc(choice_count,
                                                       sizeof(*choices));
    if (choices == 0) {
      status = CONFIT_ERR_INTERNAL;
      goto invalid;
    }
  }
  for (index = 0U; index < choice_count; ++index) {
    const char *feature = confit_toml_table_key_at(providers, index);
    const ConfitTomlValue *provider =
        confit_toml_table_value_at(providers, index);
    char *component_id = 0;
    if (!confit_component_feature_valid(feature)) {
      status = CONFIT_ERR_SCHEMA;
      goto invalid;
    }
    status = confit_component_copy_toml_string(
        provider, CONFIT_COMPONENT_MAX_ATOM_BYTES, &component_id, diagnostic);
    if (status != CONFIT_OK ||
        !confit_component_identifier_valid(component_id)) {
      free(component_id);
      status = CONFIT_ERR_SCHEMA;
      goto invalid;
    }
    choices[index].feature = confit_component_strdup(feature);
    choices[index].component_id = component_id;
    choices[index].source_path = profile_path;
    choices[index].source_line = confit_toml_value_line(provider);
    choices[index].source_column = confit_toml_value_column(provider);
    if (choices[index].feature == 0) {
      status = CONFIT_ERR_INTERNAL;
      goto invalid;
    }
  }
  if (choice_count > 1U) {
    qsort(choices, choice_count, sizeof(*choices),
          confit_component_provider_choice_compare);
  }
  status = confit_component_catalog_resolve_features(
      catalog, (const char *const *)required, required_count, 0, 0U, choices,
      choice_count, out_closure, diagnostic);
  for (index = 0U; status == CONFIT_OK &&
                   index < out_closure->reason_count; ++index) {
    ConfitComponentReason *reason = &out_closure->reasons[index];
    size_t feature_index;
    if (reason->kind != CONFIT_COMPONENT_REASON_ROOT_FEATURE ||
        reason->provider_selection ==
            CONFIT_COMPONENT_PROVIDER_SELECTION_EXPLICIT) {
      continue;
    }
    for (feature_index = 0U; feature_index < required_count; ++feature_index) {
      if (strcmp(reason->requirement, required[feature_index]) == 0) {
        char *source = confit_component_strdup(profile_path);
        if (source == 0) {
          status = CONFIT_ERR_INTERNAL;
          break;
        }
        free(reason->source_path);
        reason->source_path = source;
        reason->source_line = required_spans[feature_index].line;
        reason->source_column = required_spans[feature_index].column;
        break;
      }
    }
  }
  for (index = 0U; status == CONFIT_OK && index < choice_count; ++index) {
    size_t reason_index;
    int used = 0;
    for (reason_index = 0U; reason_index < out_closure->reason_count;
         ++reason_index) {
      if (strcmp(out_closure->reasons[reason_index].requirement,
                 choices[index].feature) == 0) {
        used = 1;
        break;
      }
    }
    if (!used) {
      confit_component_diagnostic_set(
          diagnostic, CONFIT_ERR_SCHEMA, choices[index].source_path,
          choices[index].source_line, choices[index].source_column,
          "profile contains an unused provider mapping");
      status = CONFIT_ERR_SCHEMA;
    }
  }
  if (status != CONFIT_OK) confit_component_closure_clear(out_closure);
  goto done;

invalid:
  if (status == CONFIT_OK || !confit_diagnostic_has_error(diagnostic)) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA, profile_path, 1U, 1U,
        "profile is not the closed feature/provider schema v3");
  }
  status = status == CONFIT_OK ? CONFIT_ERR_SCHEMA : status;
done:
  free(profile_id);
  free(target_id);
  confit_component_string_list_clear(required, required_count);
  free(required_spans);
  confit_component_provider_choices_clear(choices, choice_count);
  confit_toml_document_free(document);
  return status;
}

ConfitStatus confit_component_catalog_resolve_component_diagnostic(
    const ConfitComponentCatalog *catalog, const char *component_id,
    const ConfitComponentProviderChoice *provider_choices,
    size_t provider_choice_count, ConfitComponentClosure *out_closure,
    ConfitDiagnostic *diagnostic) {
  ConfitComponentResolveContext context;
  const ConfitComponent *component;
  ConfitStatus status;
  if (catalog == 0 || component_id == 0 || out_closure == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  memset(out_closure, 0, sizeof(*out_closure));
  component = confit_component_catalog_find(catalog, component_id);
  if (component == 0) {
    confit_component_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA, component_id, 0U, 0U,
        "diagnostic component ID is unavailable");
    return CONFIT_ERR_SCHEMA;
  }
  status = confit_component_validate_choices(
      catalog, provider_choices, provider_choice_count, diagnostic);
  if (status != CONFIT_OK) return status;
  memset(&context, 0, sizeof(context));
  context.catalog = catalog;
  context.choices = provider_choices;
  context.choice_count = provider_choice_count;
  context.closure = out_closure;
  context.diagnostic = diagnostic;
  context.state = (unsigned char *)calloc(catalog->component_count,
                                           sizeof(*context.state));
  if (catalog->component_count > 0U && context.state == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  status = confit_component_select_component(&context, component, 1U);
  if (status == CONFIT_OK) status = confit_component_validate_selected_closure(
      out_closure, diagnostic);
  free(context.state);
  if (status != CONFIT_OK) confit_component_closure_clear(out_closure);
  return status;
}

static size_t confit_component_edit_distance(const char *left,
                                             const char *right) {
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
    memcpy(previous, current,
           (right_size + 1U) * sizeof(previous[0]));
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
