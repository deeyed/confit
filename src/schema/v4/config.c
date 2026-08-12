#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "config_internal.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "confit/toml.h"
#include "confit/host.h"

static _Thread_local char g_v4_diagnostic_path[CONFIT_V4_MAX_PATH_BYTES + 1U];
static const char kSchemaDefaultPath[] = "confit://schema-v4/defaults";
static const char kConfigName[] = "Config.toml";
static const char kOwnersName[] = "OWNERS.toml";

typedef struct ConfitV4DiscoveryContext {
  ConfitV4Catalog *catalog;
  ConfitV4Role role;
  const char *role_root;
  char **folded_paths;
  size_t folded_count;
  char **config_paths;
  size_t config_count;
  ConfitDiagnostic *diagnostic;
} ConfitV4DiscoveryContext;

static const char *const kRoleNames[CONFIT_V4_ROLE_COUNT] = {
    "options", "menus", "choices", "constraints", "profiles", "targets",
    "selections", "products"};

char *confit_v4_copy(const char *text) {
  size_t size;
  char *copy;
  if (text == 0) return 0;
  size = strlen(text);
  copy = (char *)malloc(size + 1U);
  if (copy != 0) memcpy(copy, text, size + 1U);
  return copy;
}

void confit_v4_owned_span_clear(ConfitV4OwnedSpan *span) {
  if (span == 0) return;
  free(span->path);
  memset(span, 0, sizeof(*span));
}

int confit_v4_owned_span_set(ConfitV4OwnedSpan *span, const char *path,
                            size_t line, size_t column) {
  char *copy;
  if (span == 0 || path == 0) return 0;
  copy = confit_v4_copy(path);
  if (copy == 0) return 0;
  confit_v4_owned_span_clear(span);
  span->path = copy;
  span->line = line != 0U ? line : 1U;
  span->column = column != 0U ? column : 1U;
  return 1;
}

void confit_v4_string_list_clear(ConfitV4StringList *list) {
  size_t index;
  if (list == 0) return;
  for (index = 0U; index < list->count; ++index) {
    free(list->items[index]);
    confit_v4_owned_span_clear(&list->spans[index]);
  }
  free(list->items);
  free(list->spans);
  memset(list, 0, sizeof(*list));
}

void confit_v4_set_diagnostic(ConfitDiagnostic *diagnostic,
                              ConfitStatus status,
                              const ConfitV4OwnedSpan *source,
                              const char *message) {
  const char *path = 0;
  size_t line = 0U;
  size_t column = 0U;
  if (source != 0 && source->path != 0) {
    size_t size = strlen(source->path);
    if (size > CONFIT_V4_MAX_PATH_BYTES) size = CONFIT_V4_MAX_PATH_BYTES;
    memcpy(g_v4_diagnostic_path, source->path, size);
    g_v4_diagnostic_path[size] = '\0';
    path = g_v4_diagnostic_path;
    line = source->line;
    column = source->column;
  }
  confit_diagnostic_set(diagnostic, status, path, line, column, message);
}

static void confit_v4_set_value_diagnostic(ConfitDiagnostic *diagnostic,
                                           ConfitStatus status,
                                           const ConfitTomlValue *value,
                                           const char *fallback_path,
                                           const char *message) {
  ConfitV4OwnedSpan span;
  memset(&span, 0, sizeof(span));
  span.path = (char *)(value != 0 ? confit_toml_value_source(value)
                                 : fallback_path);
  span.line = value != 0 ? confit_toml_value_line(value) : 1U;
  span.column = value != 0 ? confit_toml_value_column(value) : 1U;
  confit_v4_set_diagnostic(diagnostic, status, &span, message);
}

static int confit_v4_text_safe(const char *text, size_t maximum) {
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

int confit_v4_symbol_valid(const char *text) {
  size_t index;
  if (text == 0 || text[0] < 'A' || text[0] > 'Z' ||
      strlen(text) > CONFIT_V4_MAX_SYMBOL_BYTES) return 0;
  for (index = 1U; text[index] != '\0'; ++index) {
    const unsigned char value = (unsigned char)text[index];
    if (!((value >= 'A' && value <= 'Z') ||
          (value >= '0' && value <= '9') || value == '_')) return 0;
  }
  return 1;
}

int confit_v4_namespace_valid(const char *text) {
  size_t index;
  int segment_start = 1;
  if (text == 0 || text[0] == '\0' ||
      strlen(text) > CONFIT_V4_MAX_SYMBOL_BYTES) return 0;
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

static int confit_v4_tag_valid(const char *text) {
  size_t index;
  if (text == 0 || text[0] < 'a' || text[0] > 'z' ||
      strlen(text) > 63U) return 0;
  for (index = 1U; text[index] != '\0'; ++index) {
    const unsigned char value = (unsigned char)text[index];
    if (!((value >= 'a' && value <= 'z') ||
          (value >= '0' && value <= '9') || value == '-')) return 0;
  }
  return 1;
}

static int confit_v4_relative_path_valid(const char *text) {
  const char *segment;
  const char *cursor;
  if (text == 0 || text[0] == '\0' || text[0] == '/' || text[0] == '\\' ||
      strchr(text, '\\') != 0 || strlen(text) > CONFIT_V4_MAX_PATH_BYTES) {
    return 0;
  }
  for (cursor = text; *cursor != '\0'; ++cursor) {
    const unsigned char value = (unsigned char)*cursor;
    if (!((value >= 'A' && value <= 'Z') ||
          (value >= 'a' && value <= 'z') ||
          (value >= '0' && value <= '9') || value == '_' || value == '-' ||
          value == '.' || value == '/')) {
      return 0;
    }
  }
  segment = text;
  for (cursor = text;; ++cursor) {
    if (*cursor == '/' || *cursor == '\0') {
      const size_t size = (size_t)(cursor - segment);
      if (size == 0U || (size == 1U && segment[0] == '.') ||
          (size == 2U && segment[0] == '.' && segment[1] == '.')) {
        return 0;
      }
      if (*cursor == '\0') return 1;
      segment = cursor + 1U;
    }
  }
}

static int confit_v4_ascii_case_equal(const char *left, const char *right) {
  size_t index;
  if (left == 0 || right == 0) return 0;
  for (index = 0U; left[index] != '\0' && right[index] != '\0'; ++index) {
    unsigned char left_value = (unsigned char)left[index];
    unsigned char right_value = (unsigned char)right[index];
    if (left_value >= 'A' && left_value <= 'Z')
      left_value = (unsigned char)(left_value - 'A' + 'a');
    if (right_value >= 'A' && right_value <= 'Z')
      right_value = (unsigned char)(right_value - 'A' + 'a');
    if (left_value != right_value) return 0;
  }
  return left[index] == right[index];
}

static int confit_v4_join(char *out, size_t out_size, const char *left,
                          const char *right) {
  ConfitDiagnostic diagnostic;
  confit_diagnostic_init(&diagnostic);
  return confit_host_path_join(out, out_size, left, right, &diagnostic) ==
         CONFIT_OK;
}

static ConfitStatus confit_v4_validate_keys(
    const ConfitTomlValue *table, const char *const *allowed,
    size_t allowed_count, const char *path, ConfitDiagnostic *diagnostic) {
  size_t index;
  if (table == 0 ||
      confit_toml_value_type(table) != CONFIT_TOML_VALUE_TABLE) {
    confit_v4_set_value_diagnostic(diagnostic, CONFIT_ERR_SCHEMA, table, path,
                                   "Config v4 requires a closed TOML table");
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < confit_toml_table_size(table); ++index) {
    const char *key = confit_toml_table_key_at(table, index);
    size_t allowed_index;
    for (allowed_index = 0U; allowed_index < allowed_count; ++allowed_index) {
      if (strcmp(key, allowed[allowed_index]) == 0) break;
    }
    if (allowed_index == allowed_count) {
      confit_v4_set_value_diagnostic(
          diagnostic, CONFIT_ERR_SCHEMA,
          confit_toml_table_value_at(table, index), path,
          "Config v4 contains an unknown or retired field");
      return CONFIT_ERR_SCHEMA;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v4_copy_string(const ConfitTomlValue *value,
                                          size_t maximum, char **out,
                                          ConfitDiagnostic *diagnostic) {
  const char *text;
  size_t size;
  char *copy;
  *out = 0;
  if (value == 0 || !confit_toml_value_string(value, &text, &size) ||
      size == 0U || size > maximum || memchr(text, '\0', size) != 0) {
    confit_v4_set_value_diagnostic(diagnostic, CONFIT_ERR_SCHEMA, value, 0,
                                   "Config v4 requires a bounded string");
    return CONFIT_ERR_SCHEMA;
  }
  copy = (char *)malloc(size + 1U);
  if (copy == 0) return CONFIT_ERR_INTERNAL;
  memcpy(copy, text, size);
  copy[size] = '\0';
  if (!confit_v4_text_safe(copy, maximum)) {
    free(copy);
    confit_v4_set_value_diagnostic(diagnostic, CONFIT_ERR_SCHEMA, value, 0,
                                   "Config v4 string contains unsafe bytes");
    return CONFIT_ERR_SCHEMA;
  }
  *out = copy;
  return CONFIT_OK;
}

static ConfitStatus confit_v4_span_from_value(ConfitV4OwnedSpan *out,
                                              const ConfitTomlValue *value,
                                              const char *fallback_path) {
  const char *path = value != 0 ? confit_toml_value_source(value)
                                : fallback_path;
  return confit_v4_owned_span_set(
             out, path,
             value != 0 ? confit_toml_value_line(value) : 1U,
             value != 0 ? confit_toml_value_column(value) : 1U)
             ? CONFIT_OK
             : CONFIT_ERR_INTERNAL;
}

static int confit_v4_list_contains(const ConfitV4StringList *list,
                                   const char *text) {
  size_t index;
  for (index = 0U; index < list->count; ++index) {
    if (strcmp(list->items[index], text) == 0) return 1;
  }
  return 0;
}

typedef int (*ConfitV4StringValidator)(const char *text);

static ConfitStatus confit_v4_parse_string_list(
    const ConfitTomlValue *value, size_t maximum_count,
    ConfitV4StringValidator validator, ConfitV4StringList *out,
    ConfitDiagnostic *diagnostic) {
  size_t count;
  size_t index;
  memset(out, 0, sizeof(*out));
  if (value == 0 ||
      confit_toml_value_type(value) != CONFIT_TOML_VALUE_ARRAY ||
      (count = confit_toml_array_size(value)) > maximum_count) {
    confit_v4_set_value_diagnostic(diagnostic, CONFIT_ERR_SCHEMA, value, 0,
                                   "Config v4 list exceeds its closed bound");
    return CONFIT_ERR_SCHEMA;
  }
  if (count == 0U) return CONFIT_OK;
  out->items = (char **)calloc(count, sizeof(out->items[0]));
  out->spans = (ConfitV4OwnedSpan *)calloc(count, sizeof(out->spans[0]));
  if (out->items == 0 || out->spans == 0) {
    confit_v4_string_list_clear(out);
    return CONFIT_ERR_INTERNAL;
  }
  out->count = count;
  for (index = 0U; index < count; ++index) {
    const ConfitTomlValue *item = confit_toml_array_at(value, index);
    ConfitStatus status = confit_v4_copy_string(
        item, CONFIT_V4_MAX_SYMBOL_BYTES, &out->items[index], diagnostic);
    if (status != CONFIT_OK ||
        (validator != 0 && !validator(out->items[index]))) {
      if (status == CONFIT_OK) {
        confit_v4_set_value_diagnostic(
            diagnostic, CONFIT_ERR_SCHEMA, item, 0,
            "Config v4 list contains an invalid semantic atom");
      }
      confit_v4_string_list_clear(out);
      return status != CONFIT_OK ? status : CONFIT_ERR_SCHEMA;
    }
    if (confit_v4_span_from_value(&out->spans[index], item, 0) != CONFIT_OK) {
      confit_v4_string_list_clear(out);
      return CONFIT_ERR_INTERNAL;
    }
    for (size_t other = 0U; other < index; ++other) {
      if (strcmp(out->items[other], out->items[index]) == 0) {
        confit_v4_set_value_diagnostic(
            diagnostic, CONFIT_ERR_SCHEMA, item, 0,
            "Config v4 list contains a duplicate atom");
        confit_v4_string_list_clear(out);
        return CONFIT_ERR_SCHEMA;
      }
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v4_copy_list(ConfitV4StringList *out,
                                        const ConfitV4StringList *source) {
  size_t index;
  memset(out, 0, sizeof(*out));
  if (source->count == 0U) return CONFIT_OK;
  out->items = (char **)calloc(source->count, sizeof(out->items[0]));
  out->spans =
      (ConfitV4OwnedSpan *)calloc(source->count, sizeof(out->spans[0]));
  if (out->items == 0 || out->spans == 0) {
    confit_v4_string_list_clear(out);
    return CONFIT_ERR_INTERNAL;
  }
  out->count = source->count;
  for (index = 0U; index < source->count; ++index) {
    out->items[index] = confit_v4_copy(source->items[index]);
    if (out->items[index] == 0 ||
        !confit_v4_owned_span_set(&out->spans[index], source->spans[index].path,
                                  source->spans[index].line,
                                  source->spans[index].column)) {
      confit_v4_string_list_clear(out);
      return CONFIT_ERR_INTERNAL;
    }
  }
  return CONFIT_OK;
}

static void confit_v4_defaults_clear(ConfitV4Defaults *defaults) {
  if (defaults == 0) return;
  free(defaults->owner);
  free(defaults->since);
  free(defaults->stability);
  confit_v4_string_list_clear(&defaults->tags);
  confit_v4_owned_span_clear(&defaults->owner_source);
  confit_v4_owned_span_clear(&defaults->since_source);
  confit_v4_owned_span_clear(&defaults->stability_source);
  confit_v4_owned_span_clear(&defaults->tags_source);
  confit_v4_owned_span_clear(&defaults->menu_order_source);
  memset(defaults, 0, sizeof(*defaults));
}

static ConfitStatus confit_v4_defaults_copy(ConfitV4Defaults *out,
                                            const ConfitV4Defaults *source) {
  memset(out, 0, sizeof(*out));
#define COPY_DEFAULT_TEXT(field)                                               \
  do {                                                                         \
    if (source->field != 0) {                                                   \
      out->field = confit_v4_copy(source->field);                               \
      if (out->field == 0 ||                                                    \
          !confit_v4_owned_span_set(&out->field##_source,                       \
                                    source->field##_source.path,                \
                                    source->field##_source.line,                \
                                    source->field##_source.column)) {           \
        confit_v4_defaults_clear(out);                                          \
        return CONFIT_ERR_INTERNAL;                                             \
      }                                                                         \
    }                                                                           \
  } while (0)
  COPY_DEFAULT_TEXT(owner);
  COPY_DEFAULT_TEXT(since);
  COPY_DEFAULT_TEXT(stability);
#undef COPY_DEFAULT_TEXT
  if (confit_v4_copy_list(&out->tags, &source->tags) != CONFIT_OK) {
    confit_v4_defaults_clear(out);
    return CONFIT_ERR_INTERNAL;
  }
  if (source->tags_source.path != 0 &&
      !confit_v4_owned_span_set(&out->tags_source, source->tags_source.path,
                                source->tags_source.line,
                                source->tags_source.column)) {
    confit_v4_defaults_clear(out);
    return CONFIT_ERR_INTERNAL;
  }
  out->has_menu_order = source->has_menu_order;
  out->menu_order = source->menu_order;
  if (source->has_menu_order &&
      !confit_v4_owned_span_set(&out->menu_order_source,
                                source->menu_order_source.path,
                                source->menu_order_source.line,
                                source->menu_order_source.column)) {
    confit_v4_defaults_clear(out);
    return CONFIT_ERR_INTERNAL;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v4_parse_defaults_table(
    const ConfitTomlValue *table, const char *path,
    ConfitV4Defaults *defaults, ConfitDiagnostic *diagnostic) {
  static const char *const allowed[] = {
      "owner", "since", "stability", "tags", "menu_order"};
  const ConfitTomlValue *value;
  ConfitStatus status;
  if ((status = confit_v4_validate_keys(table, allowed,
                                         sizeof(allowed) / sizeof(allowed[0]),
                                         path, diagnostic)) != CONFIT_OK) {
    return status;
  }
#define PARSE_DEFAULT_TEXT(field, validator, message)                           \
  do {                                                                          \
    value = confit_toml_table_find(table, #field);                            \
    if (value != 0) {                                                            \
      char *parsed = 0;                                                          \
      status = confit_v4_copy_string(value, CONFIT_V4_MAX_TEXT_BYTES, &parsed,  \
                                     diagnostic);                                \
      if (status != CONFIT_OK || !(validator)(parsed)) {                         \
        free(parsed);                                                            \
        if (status == CONFIT_OK)                                                  \
          confit_v4_set_value_diagnostic(diagnostic, CONFIT_ERR_SCHEMA, value,  \
                                         path, message);                         \
        return status != CONFIT_OK ? status : CONFIT_ERR_SCHEMA;                \
      }                                                                          \
      free(defaults->field);                                                     \
      defaults->field = parsed;                                                  \
      if (confit_v4_span_from_value(&defaults->field##_source, value, path) !=   \
          CONFIT_OK) return CONFIT_ERR_INTERNAL;                                 \
    }                                                                            \
  } while (0)
  PARSE_DEFAULT_TEXT(owner, confit_v4_namespace_valid,
                     "owner must be a lower-case dotted responsibility");
#undef PARSE_DEFAULT_TEXT
  value = confit_toml_table_find(table, "since");
  if (value != 0) {
    char *parsed = 0;
    status = confit_v4_copy_string(value, 64U, &parsed, diagnostic);
    if (status != CONFIT_OK) return status;
    free(defaults->since);
    defaults->since = parsed;
    if (confit_v4_span_from_value(&defaults->since_source, value, path) !=
        CONFIT_OK) return CONFIT_ERR_INTERNAL;
  }
  value = confit_toml_table_find(table, "stability");
  if (value != 0) {
    char *parsed = 0;
    status = confit_v4_copy_string(value, 32U, &parsed, diagnostic);
    if (status != CONFIT_OK ||
        (strcmp(parsed, "experimental") != 0 &&
         strcmp(parsed, "stable") != 0 &&
         strcmp(parsed, "deprecated") != 0)) {
      free(parsed);
      if (status == CONFIT_OK)
        confit_v4_set_value_diagnostic(
            diagnostic, CONFIT_ERR_SCHEMA, value, path,
            "stability must be experimental, stable, or deprecated");
      return status != CONFIT_OK ? status : CONFIT_ERR_SCHEMA;
    }
    free(defaults->stability);
    defaults->stability = parsed;
    if (confit_v4_span_from_value(&defaults->stability_source, value, path) !=
        CONFIT_OK) return CONFIT_ERR_INTERNAL;
  }
  value = confit_toml_table_find(table, "tags");
  if (value != 0) {
    ConfitV4StringList parsed;
    status = confit_v4_parse_string_list(value, CONFIT_V4_MAX_TAGS,
                                         confit_v4_tag_valid, &parsed,
                                         diagnostic);
    if (status != CONFIT_OK) return status;
    confit_v4_string_list_clear(&defaults->tags);
    defaults->tags = parsed;
    if (confit_v4_span_from_value(&defaults->tags_source, value, path) !=
        CONFIT_OK) return CONFIT_ERR_INTERNAL;
  }
  value = confit_toml_table_find(table, "menu_order");
  if (value != 0) {
    int64_t order;
    if (!confit_toml_value_int64(value, &order) || order < -1000000 ||
        order > 1000000) {
      confit_v4_set_value_diagnostic(
          diagnostic, CONFIT_ERR_SCHEMA, value, path,
          "menu_order must be a bounded integer");
      return CONFIT_ERR_SCHEMA;
    }
    defaults->has_menu_order = 1;
    defaults->menu_order = order;
    if (confit_v4_span_from_value(&defaults->menu_order_source, value, path) !=
        CONFIT_OK) return CONFIT_ERR_INTERNAL;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v4_require_schema(
    const ConfitTomlValue *root, const char *path,
    ConfitDiagnostic *diagnostic) {
  const ConfitTomlValue *value =
      confit_toml_table_find(root, "schema_version");
  int64_t version;
  if (value == 0 || !confit_toml_value_int64(value, &version) ||
      version != 4) {
    confit_v4_set_value_diagnostic(
        diagnostic, CONFIT_ERR_SCHEMA, value, path,
        "Config catalog accepts schema_version = 4 only");
    return CONFIT_ERR_SCHEMA;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v4_parse_root_list(
    const ConfitTomlValue *value, ConfitV4RoleRoots *roots,
    ConfitDiagnostic *diagnostic) {
  ConfitV4StringList parsed;
  ConfitStatus status = confit_v4_parse_string_list(
      value, 32U, confit_v4_relative_path_valid, &parsed, diagnostic);
  if (status != CONFIT_OK) return status;
  roots->items = parsed.items;
  roots->count = parsed.count;
  free(parsed.spans);
  return CONFIT_OK;
}

static ConfitStatus confit_v4_parse_project(ConfitV4Catalog *catalog,
                                            ConfitDiagnostic *diagnostic) {
  static const char *const root_allowed[] = {
      "schema_version", "project", "discovery", "defaults"};
  static const char *const project_allowed[] = {"name", "namespace"};
  ConfitTomlDocument *document = 0;
  const ConfitTomlValue *root;
  const ConfitTomlValue *project;
  const ConfitTomlValue *discovery;
  const ConfitTomlValue *defaults;
  ConfitStatus status;
#if !defined(_WIN32)
  struct stat metadata;
  if (lstat(catalog->project_path, &metadata) != 0 ||
      S_ISLNK(metadata.st_mode) || !S_ISREG(metadata.st_mode) ||
      metadata.st_size < 0 ||
      (uintmax_t)metadata.st_size > CONFIT_V4_MAX_FILE_BYTES) {
    confit_v4_set_value_diagnostic(
        diagnostic, CONFIT_ERR_SCHEMA, 0, catalog->project_path,
        "Config v4 project must be a bounded regular non-symlink file");
    return CONFIT_ERR_SCHEMA;
  }
#endif
  status = confit_toml_parse_file(catalog->project_path, &document,
                                  diagnostic);
  if (status != CONFIT_OK) return status;
  root = confit_toml_document_root(document);
  status = confit_v4_validate_keys(root, root_allowed,
                                   sizeof(root_allowed) / sizeof(root_allowed[0]),
                                   catalog->project_path, diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_require_schema(root, catalog->project_path, diagnostic);
  project = confit_toml_table_find(root, "project");
  discovery = confit_toml_table_find(root, "discovery");
  defaults = confit_toml_table_find(root, "defaults");
  if (status == CONFIT_OK)
    status = confit_v4_validate_keys(
        project, project_allowed,
        sizeof(project_allowed) / sizeof(project_allowed[0]),
        catalog->project_path, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v4_copy_string(confit_toml_table_find(project, "name"),
                                   127U, &catalog->project_name, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v4_copy_string(
        confit_toml_table_find(project, "namespace"), 127U,
        &catalog->project_namespace, diagnostic);
    if (status == CONFIT_OK &&
        !confit_v4_namespace_valid(catalog->project_namespace)) {
      confit_v4_set_value_diagnostic(
          diagnostic, CONFIT_ERR_SCHEMA,
          confit_toml_table_find(project, "namespace"),
          catalog->project_path, "project namespace is invalid");
      status = CONFIT_ERR_SCHEMA;
    }
  }
  if (status == CONFIT_OK) {
    status = confit_v4_validate_keys(
        discovery, kRoleNames, CONFIT_V4_ROLE_COUNT, catalog->project_path,
        diagnostic);
  }
  for (size_t role = 0U; status == CONFIT_OK && role < CONFIT_V4_ROLE_COUNT;
       ++role) {
    const ConfitTomlValue *value =
        confit_toml_table_find(discovery, kRoleNames[role]);
    if (value == 0) {
      confit_v4_set_value_diagnostic(
          diagnostic, CONFIT_ERR_SCHEMA, discovery, catalog->project_path,
          "project discovery must declare every closed role root");
      status = CONFIT_ERR_SCHEMA;
      break;
    }
    status = confit_v4_parse_root_list(value, &catalog->roots[role], diagnostic);
  }
  for (size_t role = 0U; status == CONFIT_OK && role < CONFIT_V4_ROLE_COUNT;
       ++role) {
    for (size_t root_index = 0U;
         status == CONFIT_OK && root_index < catalog->roots[role].count;
         ++root_index) {
      for (size_t other_role = 0U; other_role <= role; ++other_role) {
        const size_t limit = other_role == role ? root_index
                                                : catalog->roots[other_role].count;
        for (size_t other_root = 0U; other_root < limit; ++other_root) {
          if (confit_v4_ascii_case_equal(
                  catalog->roots[role].items[root_index],
                  catalog->roots[other_role].items[other_root])) {
            confit_v4_set_value_diagnostic(
                diagnostic, CONFIT_ERR_SCHEMA, discovery,
                catalog->project_path,
                "one role root cannot be duplicated or case-fold aliased");
            status = CONFIT_ERR_SCHEMA;
            break;
          }
        }
        if (status != CONFIT_OK) break;
      }
    }
  }
  if (status == CONFIT_OK && defaults != 0) {
    status = confit_v4_parse_defaults_table(defaults, catalog->project_path,
                                            &catalog->defaults, diagnostic);
  }
  if (status == CONFIT_OK && catalog->defaults.stability == 0) {
    catalog->defaults.stability = confit_v4_copy("experimental");
    if (catalog->defaults.stability == 0 ||
        !confit_v4_owned_span_set(&catalog->defaults.stability_source,
                                  kSchemaDefaultPath, 1U, 1U)) {
      status = CONFIT_ERR_INTERNAL;
    }
  }
  if (status == CONFIT_OK && catalog->defaults.tags_source.path == 0 &&
      !confit_v4_owned_span_set(&catalog->defaults.tags_source,
                                kSchemaDefaultPath, 1U, 1U)) {
    status = CONFIT_ERR_INTERNAL;
  }
  confit_toml_document_free(document);
  return status;
}

static ConfitStatus confit_v4_parse_owners_file(
    const char *path, ConfitV4Defaults *defaults,
    ConfitDiagnostic *diagnostic) {
  static const char *const root_allowed[] = {"schema_version", "defaults"};
  ConfitTomlDocument *document = 0;
  const ConfitTomlValue *root;
  const ConfitTomlValue *table;
  ConfitStatus status =
      confit_toml_parse_file(path, &document, diagnostic);
  if (status != CONFIT_OK) return status;
  root = confit_toml_document_root(document);
  status = confit_v4_validate_keys(
      root, root_allowed, sizeof(root_allowed) / sizeof(root_allowed[0]), path,
      diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_require_schema(root, path, diagnostic);
  table = confit_toml_table_find(root, "defaults");
  if (status == CONFIT_OK && table == 0) {
    confit_v4_set_value_diagnostic(
        diagnostic, CONFIT_ERR_SCHEMA, root, path,
        "OWNERS.toml requires one defaults table");
    status = CONFIT_ERR_SCHEMA;
  }
  if (status == CONFIT_OK)
    status = confit_v4_parse_defaults_table(table, path, defaults, diagnostic);
  confit_toml_document_free(document);
  return status;
}

static ConfitStatus confit_v4_collect_defaults(
    const ConfitV4Catalog *catalog, const char *role_root,
    const char *document_path, ConfitV4Defaults *out,
    ConfitDiagnostic *diagnostic) {
  char current[CONFIT_V4_MAX_PATH_BYTES + 1U];
  const char *relative;
  ConfitStatus status = confit_v4_defaults_copy(out, &catalog->defaults);
  if (status != CONFIT_OK) return status;
  if (strncmp(document_path, role_root, strlen(role_root)) != 0 ||
      (document_path[strlen(role_root)] != '/' &&
       document_path[strlen(role_root)] != '\0')) {
    confit_v4_defaults_clear(out);
    return CONFIT_ERR_INTERNAL;
  }
  if (strlen(role_root) >= sizeof(current)) {
    confit_v4_defaults_clear(out);
    return CONFIT_ERR_SCHEMA;
  }
  memcpy(current, role_root, strlen(role_root) + 1U);
  relative = document_path + strlen(role_root);
  if (*relative == '/') ++relative;
  for (;;) {
    char owners[CONFIT_V4_MAX_PATH_BYTES + 1U];
    const char *separator = strchr(relative, '/');
    if (!confit_v4_join(owners, sizeof(owners), current, kOwnersName)) {
      confit_v4_defaults_clear(out);
      return CONFIT_ERR_INTERNAL;
    }
    if (confit_host_file_exists(owners)) {
      status = confit_v4_parse_owners_file(owners, out, diagnostic);
      if (status != CONFIT_OK) {
        confit_v4_defaults_clear(out);
        return status;
      }
    }
    if (separator == 0) break;
    {
      const size_t segment_size = (size_t)(separator - relative);
      const size_t current_size = strlen(current);
      if (segment_size == 0U ||
          current_size + 1U + segment_size + 1U > sizeof(current)) {
        confit_v4_defaults_clear(out);
        return CONFIT_ERR_SCHEMA;
      }
      current[current_size] = '/';
      memcpy(current + current_size + 1U, relative, segment_size);
      current[current_size + 1U + segment_size] = '\0';
      relative = separator + 1U;
    }
  }
  return CONFIT_OK;
}

static ConfitV4OptionType confit_v4_option_type_parse(const char *text) {
  if (strcmp(text, "bool") == 0) return CONFIT_V4_OPTION_BOOL;
  if (strcmp(text, "placement") == 0) return CONFIT_V4_OPTION_PLACEMENT;
  if (strcmp(text, "enum") == 0) return CONFIT_V4_OPTION_ENUM;
  if (strcmp(text, "integer") == 0) return CONFIT_V4_OPTION_INTEGER;
  if (strcmp(text, "string") == 0) return CONFIT_V4_OPTION_STRING;
  return CONFIT_V4_OPTION_INVALID;
}

static void confit_v4_option_clear(ConfitV4Option *option) {
  if (option == 0) return;
  free(option->symbol);
  free(option->projection);
  free(option->prompt);
  free(option->help);
  free(option->menu);
  free(option->owner);
  free(option->since);
  free(option->stability);
  free(option->default_value);
  confit_v4_string_list_clear(&option->tags);
  confit_v4_string_list_clear(&option->allowed);
  confit_v4_string_list_clear(&option->values);
  confit_v4_string_list_clear(&option->enabled_values);
  confit_v4_string_list_clear(&option->prerequisites);
  confit_v4_string_list_clear(&option->visible_all);
  for (size_t index = 0U; index < option->provider_count; ++index) {
    free(option->providers[index].namespace_name);
    confit_v4_owned_span_clear(&option->providers[index].source);
  }
  free(option->providers);
  confit_v4_owned_span_clear(&option->declaration);
  confit_v4_owned_span_clear(&option->owner_source);
  confit_v4_owned_span_clear(&option->since_source);
  confit_v4_owned_span_clear(&option->stability_source);
  confit_v4_owned_span_clear(&option->tags_source);
  confit_v4_owned_span_clear(&option->menu_order_source);
  confit_v4_owned_span_clear(&option->default_source);
  memset(option, 0, sizeof(*option));
}

static ConfitStatus confit_v4_copy_inherited_text(
    char **out, ConfitV4OwnedSpan *out_span, const char *direct_name,
    const ConfitTomlValue *table, const char *path, const char *inherited,
    const ConfitV4OwnedSpan *inherited_span, size_t maximum,
    ConfitV4StringValidator validator, ConfitDiagnostic *diagnostic) {
  const ConfitTomlValue *value =
      confit_toml_table_find(table, direct_name);
  ConfitStatus status;
  if (value != 0) {
    status = confit_v4_copy_string(value, maximum, out, diagnostic);
    if (status != CONFIT_OK) return status;
    if (validator != 0 && !validator(*out)) {
      confit_v4_set_value_diagnostic(
          diagnostic, CONFIT_ERR_SCHEMA, value, path,
          "Config v4 inherited field has an invalid semantic value");
      free(*out);
      *out = 0;
      return CONFIT_ERR_SCHEMA;
    }
    return confit_v4_span_from_value(out_span, value, path);
  }
  if (inherited == 0 || inherited_span == 0 || inherited_span->path == 0) {
    confit_v4_set_value_diagnostic(
        diagnostic, CONFIT_ERR_SCHEMA, table, path,
        "Config v4 required provenance has no direct or inherited source");
    return CONFIT_ERR_SCHEMA;
  }
  *out = confit_v4_copy(inherited);
  if (*out == 0 ||
      !confit_v4_owned_span_set(out_span, inherited_span->path,
                                inherited_span->line,
                                inherited_span->column)) {
    free(*out);
    *out = 0;
    return CONFIT_ERR_INTERNAL;
  }
  return CONFIT_OK;
}

static int confit_v4_stability_valid(const char *text) {
  return text != 0 &&
         (strcmp(text, "experimental") == 0 || strcmp(text, "stable") == 0 ||
          strcmp(text, "deprecated") == 0);
}

static ConfitStatus confit_v4_parse_option_values(
    const ConfitTomlValue *option_table, const char *path,
    int external_product, ConfitV4Option *option,
    ConfitDiagnostic *diagnostic) {
  const ConfitTomlValue *allowed =
      confit_toml_table_find(option_table, "allowed");
  const ConfitTomlValue *values =
      confit_toml_table_find(option_table, "values");
  const ConfitTomlValue *enabled =
      confit_toml_table_find(option_table, "enabled_values");
  const ConfitTomlValue *minimum =
      confit_toml_table_find(option_table, "minimum");
  const ConfitTomlValue *maximum =
      confit_toml_table_find(option_table, "maximum");
  const ConfitTomlValue *default_value =
      confit_toml_table_find(option_table, "default");
  ConfitStatus status;
  int default_enabled = 0;

  if (option->type == CONFIT_V4_OPTION_BOOL) {
    int value = 0;
    if (allowed != 0 || values != 0 || enabled != 0 || minimum != 0 ||
        maximum != 0) goto invalid_fields;
    if (default_value != 0 &&
        !confit_toml_value_bool(default_value, &value)) goto invalid_default;
    option->default_value = confit_v4_copy(value ? "true" : "false");
    default_enabled = value;
  } else if (option->type == CONFIT_V4_OPTION_PLACEMENT) {
    if (allowed == 0 || values != 0 || enabled != 0 || minimum != 0 ||
        maximum != 0) goto invalid_fields;
    status = confit_v4_parse_string_list(allowed, 3U, 0, &option->allowed,
                                         diagnostic);
    if (status != CONFIT_OK) return status;
    if (!confit_v4_list_contains(&option->allowed, "off") ||
        (!confit_v4_list_contains(&option->allowed, "kernel") &&
         !confit_v4_list_contains(&option->allowed, "service"))) {
      goto invalid_fields;
    }
    for (size_t index = 0U; index < option->allowed.count; ++index) {
      if (strcmp(option->allowed.items[index], "off") != 0 &&
          strcmp(option->allowed.items[index], "kernel") != 0 &&
          strcmp(option->allowed.items[index], "service") != 0) {
        goto invalid_fields;
      }
    }
    if (default_value == 0) {
      option->default_value = confit_v4_copy("off");
    } else {
      status = confit_v4_copy_string(default_value, 16U,
                                     &option->default_value, diagnostic);
      if (status != CONFIT_OK) return status;
    }
    if (!confit_v4_list_contains(&option->allowed, option->default_value))
      goto invalid_default;
    default_enabled = strcmp(option->default_value, "off") != 0;
  } else if (option->type == CONFIT_V4_OPTION_ENUM) {
    if (values == 0 || enabled == 0 || default_value == 0 || allowed != 0 ||
        minimum != 0 || maximum != 0) goto invalid_fields;
    status = confit_v4_parse_string_list(values, CONFIT_V4_MAX_VALUES,
                                         confit_v4_tag_valid, &option->values,
                                         diagnostic);
    if (status != CONFIT_OK || option->values.count == 0U) return status != CONFIT_OK ? status : CONFIT_ERR_SCHEMA;
    status = confit_v4_parse_string_list(
        enabled, CONFIT_V4_MAX_VALUES, confit_v4_tag_valid,
        &option->enabled_values, diagnostic);
    if (status != CONFIT_OK || option->enabled_values.count == 0U)
      return status != CONFIT_OK ? status : CONFIT_ERR_SCHEMA;
    for (size_t index = 0U; index < option->enabled_values.count; ++index) {
      if (!confit_v4_list_contains(&option->values,
                                   option->enabled_values.items[index])) {
        goto invalid_fields;
      }
    }
    status = confit_v4_copy_string(default_value, CONFIT_V4_MAX_SYMBOL_BYTES,
                                   &option->default_value, diagnostic);
    if (status != CONFIT_OK) return status;
    if (!confit_v4_list_contains(&option->values, option->default_value))
      goto invalid_default;
    default_enabled = confit_v4_list_contains(&option->enabled_values,
                                              option->default_value);
  } else if (option->type == CONFIT_V4_OPTION_INTEGER) {
    int64_t min_value;
    int64_t max_value;
    int64_t selected;
    char buffer[64];
    if (minimum == 0 || maximum == 0 || default_value == 0 || allowed != 0 ||
        values != 0 || enabled != 0 ||
        !confit_toml_value_int64(minimum, &min_value) ||
        !confit_toml_value_int64(maximum, &max_value) ||
        !confit_toml_value_int64(default_value, &selected) ||
        min_value > max_value || selected < min_value || selected > max_value) {
      goto invalid_fields;
    }
    option->minimum = min_value;
    option->maximum = max_value;
    if (snprintf(buffer, sizeof(buffer), "%lld", (long long)selected) <= 0)
      return CONFIT_ERR_INTERNAL;
    option->default_value = confit_v4_copy(buffer);
  } else if (option->type == CONFIT_V4_OPTION_STRING) {
    if (default_value == 0 || allowed != 0 || values != 0 || enabled != 0 ||
        minimum != 0 || maximum != 0) goto invalid_fields;
    status = confit_v4_copy_string(default_value, CONFIT_V4_MAX_TEXT_BYTES,
                                   &option->default_value, diagnostic);
    if (status != CONFIT_OK) return status;
  }
  if (option->default_value == 0) return CONFIT_ERR_INTERNAL;
  if (external_product && default_enabled) {
    confit_v4_set_value_diagnostic(
        diagnostic, CONFIT_ERR_SCHEMA, default_value, path,
        "external product Config.toml cannot enable itself by default");
    return CONFIT_ERR_SCHEMA;
  }
  if (default_value != 0) {
    return confit_v4_span_from_value(&option->default_source, default_value,
                                     path);
  }
  return confit_v4_owned_span_set(&option->default_source, kSchemaDefaultPath,
                                  1U, 1U)
             ? CONFIT_OK
             : CONFIT_ERR_INTERNAL;

invalid_fields:
  confit_v4_set_value_diagnostic(
      diagnostic, CONFIT_ERR_SCHEMA, option_table, path,
      "option type uses a missing or forbidden value-domain field");
  return CONFIT_ERR_SCHEMA;
invalid_default:
  confit_v4_set_value_diagnostic(
      diagnostic, CONFIT_ERR_SCHEMA, default_value, path,
      "option default is outside its declared value domain");
  return CONFIT_ERR_SCHEMA;
}

static ConfitStatus confit_v4_parse_provider_array(
    const ConfitTomlValue *interfaces, const char *path,
    ConfitV4Option *option, ConfitDiagnostic *diagnostic) {
  static const char *const interface_allowed[] = {"provides"};
  static const char *const provider_allowed[] = {
      "namespace", "major", "cardinality", "absence"};
  const ConfitTomlValue *providers;
  size_t count;
  ConfitStatus status;
  if (interfaces == 0) return CONFIT_OK;
  status = confit_v4_validate_keys(
      interfaces, interface_allowed,
      sizeof(interface_allowed) / sizeof(interface_allowed[0]), path,
      diagnostic);
  if (status != CONFIT_OK) return status;
  providers = confit_toml_table_find(interfaces, "provides");
  if (providers == 0 ||
      confit_toml_value_type(providers) != CONFIT_TOML_VALUE_ARRAY ||
      (count = confit_toml_array_size(providers)) == 0U ||
      count > CONFIT_V4_MAX_OPTION_EDGES) {
    confit_v4_set_value_diagnostic(
        diagnostic, CONFIT_ERR_SCHEMA, providers, path,
        "interfaces.provides must be a nonempty bounded table array");
    return CONFIT_ERR_SCHEMA;
  }
  option->providers =
      (ConfitV4Provider *)calloc(count, sizeof(option->providers[0]));
  if (option->providers == 0) return CONFIT_ERR_INTERNAL;
  option->provider_count = count;
  for (size_t index = 0U; index < count; ++index) {
    ConfitV4Provider *provider = &option->providers[index];
    const ConfitTomlValue *table =
        confit_toml_array_at(providers, index);
    const ConfitTomlValue *major_value;
    char *cardinality = 0;
    char *absence = 0;
    int64_t major;
    status = confit_v4_validate_keys(
        table, provider_allowed,
        sizeof(provider_allowed) / sizeof(provider_allowed[0]), path,
        diagnostic);
    if (status != CONFIT_OK) return status;
    status = confit_v4_copy_string(
        confit_toml_table_find(table, "namespace"),
        CONFIT_V4_MAX_SYMBOL_BYTES, &provider->namespace_name, diagnostic);
    if (status != CONFIT_OK ||
        !confit_v4_namespace_valid(provider->namespace_name)) {
      if (status == CONFIT_OK)
        confit_v4_set_value_diagnostic(
            diagnostic, CONFIT_ERR_SCHEMA, table, path,
            "provider namespace is not a lower-case dotted name");
      return status != CONFIT_OK ? status : CONFIT_ERR_SCHEMA;
    }
    major_value = confit_toml_table_find(table, "major");
    if (!confit_toml_value_int64(major_value, &major) || major <= 0 ||
        (uint64_t)major > UINT32_MAX) {
      confit_v4_set_value_diagnostic(
          diagnostic, CONFIT_ERR_SCHEMA, major_value, path,
          "provider major must be a positive uint32");
      return CONFIT_ERR_SCHEMA;
    }
    provider->major = (uint32_t)major;
    status = confit_v4_copy_string(
        confit_toml_table_find(table, "cardinality"), 16U,
        &cardinality, diagnostic);
    if (status == CONFIT_OK)
      status = confit_v4_copy_string(
          confit_toml_table_find(table, "absence"), 16U, &absence,
          diagnostic);
    if (status != CONFIT_OK) {
      free(cardinality);
      free(absence);
      return status;
    }
    provider->cardinality =
        strcmp(cardinality, "single") == 0
            ? CONFIT_V4_PROVIDER_CARDINALITY_SINGLE
            : strcmp(cardinality, "multiple") == 0
                  ? CONFIT_V4_PROVIDER_CARDINALITY_MULTIPLE
                  : CONFIT_V4_PROVIDER_CARDINALITY_INVALID;
    provider->absence =
        strcmp(absence, "allowed") == 0
            ? CONFIT_V4_PROVIDER_ABSENCE_ALLOWED
            : strcmp(absence, "forbidden") == 0
                  ? CONFIT_V4_PROVIDER_ABSENCE_FORBIDDEN
                  : CONFIT_V4_PROVIDER_ABSENCE_INVALID;
    free(cardinality);
    free(absence);
    if (provider->cardinality == CONFIT_V4_PROVIDER_CARDINALITY_INVALID ||
        provider->absence == CONFIT_V4_PROVIDER_ABSENCE_INVALID) {
      confit_v4_set_value_diagnostic(
          diagnostic, CONFIT_ERR_SCHEMA, table, path,
          "provider cardinality or absence is outside the closed vocabulary");
      return CONFIT_ERR_SCHEMA;
    }
    if (confit_v4_span_from_value(&provider->source, table, path) != CONFIT_OK)
      return CONFIT_ERR_INTERNAL;
    for (size_t other = 0U; other < index; ++other) {
      if (strcmp(option->providers[other].namespace_name,
                 provider->namespace_name) == 0 &&
          option->providers[other].major == provider->major) {
        confit_v4_set_value_diagnostic(
            diagnostic, CONFIT_ERR_SCHEMA, table, path,
            "one option cannot duplicate a provider namespace and major");
        return CONFIT_ERR_SCHEMA;
      }
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v4_parse_option_document(
    ConfitV4Catalog *catalog, const char *role_root, const char *path,
    int external_product, ConfitDiagnostic *diagnostic) {
  static const char *const root_allowed[] = {
      "schema_version", "option", "constraints", "ui", "interfaces"};
  static const char *const option_allowed[] = {
      "symbol",       "type",       "prompt",  "help",  "menu",
      "menu_order",   "owner",      "since",   "stability",
      "tags",         "allowed",    "values",  "enabled_values",
      "default",      "minimum",    "maximum"};
  static const char *const constraints_allowed[] = {"all"};
  static const char *const ui_allowed[] = {"visible_all"};
  ConfitTomlDocument *document = 0;
  const ConfitTomlValue *root;
  const ConfitTomlValue *table;
  const ConfitTomlValue *constraints;
  const ConfitTomlValue *ui;
  const ConfitTomlValue *interfaces;
  const ConfitTomlValue *value;
  ConfitV4Defaults defaults;
  ConfitV4Option option;
  ConfitStatus status;
  char *type = 0;
  memset(&defaults, 0, sizeof(defaults));
  memset(&option, 0, sizeof(option));
  if (catalog->option_count >= CONFIT_V4_MAX_OPTIONS) {
    confit_v4_set_value_diagnostic(
        diagnostic, CONFIT_ERR_SCHEMA, 0, path,
        "Config v4 option count exceeds the supported limit");
    return CONFIT_ERR_SCHEMA;
  }
  status = confit_v4_collect_defaults(catalog, role_root, path, &defaults,
                                      diagnostic);
  if (status != CONFIT_OK) return status;
  status = confit_toml_parse_file(path, &document, diagnostic);
  if (status != CONFIT_OK) goto done;
  root = confit_toml_document_root(document);
  status = confit_v4_validate_keys(
      root, root_allowed, sizeof(root_allowed) / sizeof(root_allowed[0]), path,
      diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_require_schema(root, path, diagnostic);
  table = confit_toml_table_find(root, "option");
  constraints = confit_toml_table_find(root, "constraints");
  ui = confit_toml_table_find(root, "ui");
  interfaces = confit_toml_table_find(root, "interfaces");
  if (status == CONFIT_OK)
    status = confit_v4_validate_keys(
        table, option_allowed,
        sizeof(option_allowed) / sizeof(option_allowed[0]), path, diagnostic);
  if (status != CONFIT_OK) goto done;

  value = confit_toml_table_find(table, "symbol");
  status = confit_v4_copy_string(value, CONFIT_V4_MAX_SYMBOL_BYTES,
                                 &option.symbol, diagnostic);
  if (status != CONFIT_OK || !confit_v4_symbol_valid(option.symbol)) {
    if (status == CONFIT_OK)
      confit_v4_set_value_diagnostic(
          diagnostic, CONFIT_ERR_SCHEMA, value, path,
          "option symbol must be uppercase ASCII with digits or underscore");
    status = status != CONFIT_OK ? status : CONFIT_ERR_SCHEMA;
    goto done;
  }
  {
    const size_t projection_size = strlen(option.symbol) + strlen("CONFIG_");
    if (projection_size > CONFIT_V4_MAX_SYMBOL_BYTES) {
      confit_v4_set_value_diagnostic(
          diagnostic, CONFIT_ERR_SCHEMA, value, path,
          "CONFIG_ projection exceeds the supported identifier bound");
      status = CONFIT_ERR_SCHEMA;
      goto done;
    }
    option.projection = (char *)malloc(projection_size + 1U);
    if (option.projection == 0) {
      status = CONFIT_ERR_INTERNAL;
      goto done;
    }
    memcpy(option.projection, "CONFIG_", strlen("CONFIG_"));
    memcpy(option.projection + strlen("CONFIG_"), option.symbol,
           strlen(option.symbol) + 1U);
  }
  for (size_t index = 0U; index < catalog->option_count; ++index) {
    if (strcmp(catalog->options[index].symbol, option.symbol) == 0 ||
        strcmp(catalog->options[index].projection, option.projection) == 0) {
      confit_v4_set_value_diagnostic(
          diagnostic, CONFIT_ERR_SCHEMA, value, path,
          "duplicate option symbol or CONFIG_ projection collision");
      status = CONFIT_ERR_SCHEMA;
      goto done;
    }
  }
  value = confit_toml_table_find(table, "type");
  status = confit_v4_copy_string(value, 32U, &type, diagnostic);
  if (status != CONFIT_OK ||
      (option.type = confit_v4_option_type_parse(type)) ==
          CONFIT_V4_OPTION_INVALID) {
    if (status == CONFIT_OK)
      confit_v4_set_value_diagnostic(
          diagnostic, CONFIT_ERR_SCHEMA, value, path,
          "option type is outside the closed v4 vocabulary");
    status = status != CONFIT_OK ? status : CONFIT_ERR_SCHEMA;
    goto done;
  }
  free(type);
  type = 0;
  status = confit_v4_copy_string(confit_toml_table_find(table, "prompt"),
                                 CONFIT_V4_MAX_TEXT_BYTES, &option.prompt,
                                 diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_copy_string(confit_toml_table_find(table, "help"),
                                   CONFIT_V4_MAX_TEXT_BYTES, &option.help,
                                   diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_copy_string(confit_toml_table_find(table, "menu"),
                                   CONFIT_V4_MAX_SYMBOL_BYTES, &option.menu,
                                   diagnostic);
  if (status != CONFIT_OK || !confit_v4_namespace_valid(option.menu)) {
    if (status == CONFIT_OK)
      confit_v4_set_value_diagnostic(diagnostic, CONFIT_ERR_SCHEMA, table,
                                     path, "option menu id is invalid");
    status = status != CONFIT_OK ? status : CONFIT_ERR_SCHEMA;
    goto done;
  }
  status = confit_v4_copy_inherited_text(
      &option.owner, &option.owner_source, "owner", table, path,
      defaults.owner, &defaults.owner_source, CONFIT_V4_MAX_SYMBOL_BYTES,
      confit_v4_namespace_valid, diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_copy_inherited_text(
        &option.since, &option.since_source, "since", table, path,
        defaults.since, &defaults.since_source, 64U, 0, diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_copy_inherited_text(
        &option.stability, &option.stability_source, "stability", table, path,
        defaults.stability, &defaults.stability_source, 32U,
        confit_v4_stability_valid, diagnostic);
  if (status != CONFIT_OK) goto done;
  value = confit_toml_table_find(table, "tags");
  if (value != 0) {
    status = confit_v4_parse_string_list(value, CONFIT_V4_MAX_TAGS,
                                         confit_v4_tag_valid, &option.tags,
                                         diagnostic);
    if (status == CONFIT_OK)
      status = confit_v4_span_from_value(&option.tags_source, value, path);
  } else {
    status = confit_v4_copy_list(&option.tags, &defaults.tags);
    if (status == CONFIT_OK && defaults.tags_source.path != 0 &&
        !confit_v4_owned_span_set(&option.tags_source,
                                  defaults.tags_source.path,
                                  defaults.tags_source.line,
                                  defaults.tags_source.column)) {
      status = CONFIT_ERR_INTERNAL;
    }
  }
  if (status != CONFIT_OK || option.tags_source.path == 0) {
    if (status == CONFIT_OK)
      confit_v4_set_value_diagnostic(
          diagnostic, CONFIT_ERR_SCHEMA, table, path,
          "option tags have no direct or inherited provenance");
    status = status != CONFIT_OK ? status : CONFIT_ERR_SCHEMA;
    goto done;
  }
  value = confit_toml_table_find(table, "menu_order");
  if (value != 0) {
    if (!confit_toml_value_int64(value, &option.menu_order) ||
        option.menu_order < -1000000 || option.menu_order > 1000000) {
      confit_v4_set_value_diagnostic(
          diagnostic, CONFIT_ERR_SCHEMA, value, path,
          "menu_order must be a bounded integer");
      status = CONFIT_ERR_SCHEMA;
      goto done;
    }
    status = confit_v4_span_from_value(&option.menu_order_source, value, path);
  } else if (defaults.has_menu_order) {
    option.menu_order = defaults.menu_order;
    status = confit_v4_owned_span_set(
                 &option.menu_order_source, defaults.menu_order_source.path,
                 defaults.menu_order_source.line,
                 defaults.menu_order_source.column)
                 ? CONFIT_OK
                 : CONFIT_ERR_INTERNAL;
  } else {
    confit_v4_set_value_diagnostic(
        diagnostic, CONFIT_ERR_SCHEMA, table, path,
        "menu_order has no direct or inherited provenance");
    status = CONFIT_ERR_SCHEMA;
  }
  if (status != CONFIT_OK) goto done;
  status = confit_v4_parse_option_values(table, path, external_product,
                                         &option, diagnostic);
  if (status != CONFIT_OK) goto done;
  if (constraints != 0) {
    status = confit_v4_validate_keys(
        constraints, constraints_allowed,
        sizeof(constraints_allowed) / sizeof(constraints_allowed[0]), path,
        diagnostic);
    value = confit_toml_table_find(constraints, "all");
    if (status == CONFIT_OK && value == 0) {
      confit_v4_set_value_diagnostic(
          diagnostic, CONFIT_ERR_SCHEMA, constraints, path,
          "empty constraints table must be omitted");
      status = CONFIT_ERR_SCHEMA;
    }
    if (status == CONFIT_OK)
      status = confit_v4_parse_string_list(
          value, CONFIT_V4_MAX_OPTION_EDGES, confit_v4_symbol_valid,
          &option.prerequisites, diagnostic);
  }
  if (status != CONFIT_OK) goto done;
  if (ui != 0) {
    status = confit_v4_validate_keys(
        ui, ui_allowed, sizeof(ui_allowed) / sizeof(ui_allowed[0]), path,
        diagnostic);
    value = confit_toml_table_find(ui, "visible_all");
    if (status == CONFIT_OK && value == 0) {
      confit_v4_set_value_diagnostic(
          diagnostic, CONFIT_ERR_SCHEMA, ui, path,
          "empty ui table must be omitted");
      status = CONFIT_ERR_SCHEMA;
    }
    if (status == CONFIT_OK)
      status = confit_v4_parse_string_list(
          value, CONFIT_V4_MAX_OPTION_EDGES, confit_v4_symbol_valid,
          &option.visible_all, diagnostic);
  }
  if (status != CONFIT_OK) goto done;
  status = confit_v4_parse_provider_array(interfaces, path, &option,
                                          diagnostic);
  if (status != CONFIT_OK) goto done;
  if (catalog->total_edges > CONFIT_V4_MAX_TOTAL_EDGES -
                                 option.prerequisites.count -
                                 option.visible_all.count -
                                 option.provider_count) {
    confit_v4_set_value_diagnostic(
        diagnostic, CONFIT_ERR_SCHEMA, root, path,
        "Config v4 total edge budget is exhausted");
    status = CONFIT_ERR_SCHEMA;
    goto done;
  }
  catalog->total_edges += option.prerequisites.count +
                          option.visible_all.count + option.provider_count;
  status = confit_v4_span_from_value(&option.declaration, table, path);
  if (status != CONFIT_OK) goto done;
  {
    ConfitV4Option *grown = (ConfitV4Option *)realloc(
        catalog->options,
        (catalog->option_count + 1U) * sizeof(catalog->options[0]));
    if (grown == 0) {
      status = CONFIT_ERR_INTERNAL;
      goto done;
    }
    catalog->options = grown;
    catalog->options[catalog->option_count++] = option;
    memset(&option, 0, sizeof(option));
  }

done:
  free(type);
  confit_v4_option_clear(&option);
  confit_v4_defaults_clear(&defaults);
  confit_toml_document_free(document);
  return status;
}

static void confit_v4_menu_clear(ConfitV4Menu *menu) {
  free(menu->id);
  free(menu->prompt);
  free(menu->help);
  free(menu->parent);
  confit_v4_owned_span_clear(&menu->source);
  memset(menu, 0, sizeof(*menu));
}

static ConfitStatus confit_v4_parse_menu_document(
    ConfitV4Catalog *catalog, const char *path,
    ConfitDiagnostic *diagnostic) {
  static const char *const root_allowed[] = {"schema_version", "menu"};
  static const char *const menu_allowed[] = {
      "id", "prompt", "help", "parent", "order"};
  ConfitTomlDocument *document = 0;
  const ConfitTomlValue *root;
  const ConfitTomlValue *table;
  const ConfitTomlValue *value;
  ConfitV4Menu menu;
  ConfitStatus status;
  memset(&menu, 0, sizeof(menu));
  if (catalog->menu_count >= CONFIT_V4_MAX_MENUS) return CONFIT_ERR_SCHEMA;
  status = confit_toml_parse_file(path, &document, diagnostic);
  if (status != CONFIT_OK) return status;
  root = confit_toml_document_root(document);
  status = confit_v4_validate_keys(
      root, root_allowed, sizeof(root_allowed) / sizeof(root_allowed[0]), path,
      diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_require_schema(root, path, diagnostic);
  table = confit_toml_table_find(root, "menu");
  if (status == CONFIT_OK)
    status = confit_v4_validate_keys(
        table, menu_allowed, sizeof(menu_allowed) / sizeof(menu_allowed[0]),
        path, diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_copy_string(confit_toml_table_find(table, "id"),
                                   CONFIT_V4_MAX_SYMBOL_BYTES, &menu.id,
                                   diagnostic);
  if (status == CONFIT_OK && !confit_v4_namespace_valid(menu.id))
    status = CONFIT_ERR_SCHEMA;
  if (status == CONFIT_OK)
    status = confit_v4_copy_string(
        confit_toml_table_find(table, "prompt"),
        CONFIT_V4_MAX_TEXT_BYTES, &menu.prompt, diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_copy_string(confit_toml_table_find(table, "help"),
                                   CONFIT_V4_MAX_TEXT_BYTES, &menu.help,
                                   diagnostic);
  value = confit_toml_table_find(table, "parent");
  if (status == CONFIT_OK && value != 0) {
    status = confit_v4_copy_string(value, CONFIT_V4_MAX_SYMBOL_BYTES,
                                   &menu.parent, diagnostic);
    if (status == CONFIT_OK && !confit_v4_namespace_valid(menu.parent))
      status = CONFIT_ERR_SCHEMA;
  }
  value = confit_toml_table_find(table, "order");
  if (status == CONFIT_OK &&
      (!confit_toml_value_int64(value, &menu.order) ||
       menu.order < -1000000 || menu.order > 1000000)) {
    status = CONFIT_ERR_SCHEMA;
  }
  if (status == CONFIT_OK)
    status = confit_v4_span_from_value(&menu.source, table, path);
  if (status == CONFIT_OK) {
    for (size_t index = 0U; index < catalog->menu_count; ++index) {
      if (strcmp(catalog->menus[index].id, menu.id) == 0) {
        status = CONFIT_ERR_SCHEMA;
        break;
      }
    }
  }
  if (status == CONFIT_OK) {
    ConfitV4Menu *grown = (ConfitV4Menu *)realloc(
        catalog->menus,
        (catalog->menu_count + 1U) * sizeof(catalog->menus[0]));
    if (grown == 0) status = CONFIT_ERR_INTERNAL;
    else {
      catalog->menus = grown;
      catalog->menus[catalog->menu_count++] = menu;
      memset(&menu, 0, sizeof(menu));
    }
  }
  if (status != CONFIT_OK &&
      (diagnostic == 0 || !confit_diagnostic_has_error(diagnostic))) {
    confit_v4_set_value_diagnostic(diagnostic, CONFIT_ERR_SCHEMA, table, path,
                                   "menu document is invalid or duplicate");
  }
  confit_v4_menu_clear(&menu);
  confit_toml_document_free(document);
  return status;
}

static void confit_v4_choice_clear(ConfitV4Choice *choice) {
  free(choice->symbol);
  free(choice->prompt);
  free(choice->help);
  confit_v4_string_list_clear(&choice->members);
  confit_v4_owned_span_clear(&choice->source);
  memset(choice, 0, sizeof(*choice));
}

static ConfitStatus confit_v4_parse_choice_document(
    ConfitV4Catalog *catalog, const char *path,
    ConfitDiagnostic *diagnostic) {
  static const char *const root_allowed[] = {"schema_version", "choice"};
  static const char *const choice_allowed[] = {
      "symbol", "prompt", "help", "members", "cardinality"};
  ConfitTomlDocument *document = 0;
  const ConfitTomlValue *root;
  const ConfitTomlValue *table;
  ConfitV4Choice choice;
  ConfitStatus status;
  char *cardinality = 0;
  memset(&choice, 0, sizeof(choice));
  if (catalog->choice_count >= CONFIT_V4_MAX_CHOICES)
    return CONFIT_ERR_SCHEMA;
  status = confit_toml_parse_file(path, &document, diagnostic);
  if (status != CONFIT_OK) return status;
  root = confit_toml_document_root(document);
  status = confit_v4_validate_keys(
      root, root_allowed, sizeof(root_allowed) / sizeof(root_allowed[0]), path,
      diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_require_schema(root, path, diagnostic);
  table = confit_toml_table_find(root, "choice");
  if (status == CONFIT_OK)
    status = confit_v4_validate_keys(
        table, choice_allowed,
        sizeof(choice_allowed) / sizeof(choice_allowed[0]), path, diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_copy_string(
        confit_toml_table_find(table, "symbol"),
        CONFIT_V4_MAX_SYMBOL_BYTES, &choice.symbol, diagnostic);
  if (status == CONFIT_OK && !confit_v4_symbol_valid(choice.symbol))
    status = CONFIT_ERR_SCHEMA;
  if (status == CONFIT_OK)
    status = confit_v4_copy_string(
        confit_toml_table_find(table, "prompt"),
        CONFIT_V4_MAX_TEXT_BYTES, &choice.prompt, diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_copy_string(confit_toml_table_find(table, "help"),
                                   CONFIT_V4_MAX_TEXT_BYTES, &choice.help,
                                   diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_parse_string_list(
        confit_toml_table_find(table, "members"),
        CONFIT_V4_MAX_OPTION_EDGES, confit_v4_symbol_valid, &choice.members,
        diagnostic);
  if (status == CONFIT_OK && choice.members.count == 0U)
    status = CONFIT_ERR_SCHEMA;
  if (status == CONFIT_OK)
    status = confit_v4_copy_string(
        confit_toml_table_find(table, "cardinality"), 32U, &cardinality,
        diagnostic);
  if (status == CONFIT_OK) {
    choice.cardinality =
        strcmp(cardinality, "at_most_one") == 0
            ? CONFIT_V4_CHOICE_AT_MOST_ONE
            : strcmp(cardinality, "exactly_one") == 0
                  ? CONFIT_V4_CHOICE_EXACTLY_ONE
                  : CONFIT_V4_CHOICE_CARDINALITY_INVALID;
    if (choice.cardinality == CONFIT_V4_CHOICE_CARDINALITY_INVALID)
      status = CONFIT_ERR_SCHEMA;
  }
  if (status == CONFIT_OK)
    status = confit_v4_span_from_value(&choice.source, table, path);
  if (status == CONFIT_OK) {
    for (size_t index = 0U; index < catalog->choice_count; ++index) {
      if (strcmp(catalog->choices[index].symbol, choice.symbol) == 0) {
        status = CONFIT_ERR_SCHEMA;
        break;
      }
    }
  }
  if (status == CONFIT_OK) {
    ConfitV4Choice *grown = (ConfitV4Choice *)realloc(
        catalog->choices,
        (catalog->choice_count + 1U) * sizeof(catalog->choices[0]));
    if (grown == 0) status = CONFIT_ERR_INTERNAL;
    else {
      catalog->choices = grown;
      catalog->choices[catalog->choice_count++] = choice;
      memset(&choice, 0, sizeof(choice));
    }
  }
  if (status != CONFIT_OK &&
      (diagnostic == 0 || !confit_diagnostic_has_error(diagnostic))) {
    confit_v4_set_value_diagnostic(
        diagnostic, CONFIT_ERR_SCHEMA, table, path,
        "choice document is invalid, empty, or duplicate");
  }
  free(cardinality);
  confit_v4_choice_clear(&choice);
  confit_toml_document_free(document);
  return status;
}

static void confit_v4_rule_clear(ConfitV4Rule *rule) {
  confit_v4_string_list_clear(&rule->if_all);
  confit_v4_string_list_clear(&rule->require_all);
  free(rule->message);
  confit_v4_owned_span_clear(&rule->source);
  memset(rule, 0, sizeof(*rule));
}

static ConfitStatus confit_v4_parse_rule_document(
    ConfitV4Catalog *catalog, const char *path,
    ConfitDiagnostic *diagnostic) {
  static const char *const root_allowed[] = {"schema_version", "rule"};
  static const char *const rule_allowed[] = {
      "if_all", "require_all", "message"};
  ConfitTomlDocument *document = 0;
  const ConfitTomlValue *root;
  const ConfitTomlValue *rules;
  ConfitStatus status;
  size_t count;
  status = confit_toml_parse_file(path, &document, diagnostic);
  if (status != CONFIT_OK) return status;
  root = confit_toml_document_root(document);
  status = confit_v4_validate_keys(
      root, root_allowed, sizeof(root_allowed) / sizeof(root_allowed[0]), path,
      diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_require_schema(root, path, diagnostic);
  rules = confit_toml_table_find(root, "rule");
  if (status == CONFIT_OK &&
      (rules == 0 ||
       confit_toml_value_type(rules) != CONFIT_TOML_VALUE_ARRAY ||
       (count = confit_toml_array_size(rules)) == 0U ||
       catalog->rule_count > CONFIT_V4_MAX_RULES - count)) {
    status = CONFIT_ERR_SCHEMA;
  }
  for (size_t index = 0U; status == CONFIT_OK && index < count; ++index) {
    const ConfitTomlValue *table = confit_toml_array_at(rules, index);
    ConfitV4Rule rule;
    memset(&rule, 0, sizeof(rule));
    status = confit_v4_validate_keys(
        table, rule_allowed, sizeof(rule_allowed) / sizeof(rule_allowed[0]),
        path, diagnostic);
    if (status == CONFIT_OK)
      status = confit_v4_parse_string_list(
          confit_toml_table_find(table, "if_all"),
          CONFIT_V4_MAX_OPTION_EDGES, confit_v4_symbol_valid, &rule.if_all,
          diagnostic);
    if (status == CONFIT_OK)
      status = confit_v4_parse_string_list(
          confit_toml_table_find(table, "require_all"),
          CONFIT_V4_MAX_OPTION_EDGES, confit_v4_symbol_valid,
          &rule.require_all, diagnostic);
    if (status == CONFIT_OK &&
        (rule.if_all.count == 0U || rule.require_all.count == 0U))
      status = CONFIT_ERR_SCHEMA;
    if (status == CONFIT_OK)
      status = confit_v4_copy_string(
          confit_toml_table_find(table, "message"),
          CONFIT_V4_MAX_TEXT_BYTES, &rule.message, diagnostic);
    if (status == CONFIT_OK)
      status = confit_v4_span_from_value(&rule.source, table, path);
    if (status == CONFIT_OK &&
        catalog->total_edges > CONFIT_V4_MAX_TOTAL_EDGES -
                                   rule.if_all.count -
                                   rule.require_all.count) {
      status = CONFIT_ERR_SCHEMA;
    }
    if (status == CONFIT_OK) {
      ConfitV4Rule *grown = (ConfitV4Rule *)realloc(
          catalog->rules,
          (catalog->rule_count + 1U) * sizeof(catalog->rules[0]));
      if (grown == 0) status = CONFIT_ERR_INTERNAL;
      else {
        catalog->rules = grown;
        catalog->rules[catalog->rule_count++] = rule;
        catalog->total_edges += rule.if_all.count + rule.require_all.count;
        memset(&rule, 0, sizeof(rule));
      }
    }
    confit_v4_rule_clear(&rule);
  }
  if (status != CONFIT_OK &&
      (diagnostic == 0 || !confit_diagnostic_has_error(diagnostic))) {
    confit_v4_set_value_diagnostic(
        diagnostic, CONFIT_ERR_SCHEMA, rules, path,
        "global rule document is malformed or exceeds its budget");
  }
  confit_toml_document_free(document);
  return status;
}

static ConfitStatus confit_v4_parse_document(
    ConfitV4DiscoveryContext *context, const char *path) {
  switch (context->role) {
  case CONFIT_V4_ROLE_OPTIONS:
    return confit_v4_parse_option_document(
        context->catalog, context->role_root, path, 0, context->diagnostic);
  case CONFIT_V4_ROLE_PRODUCTS:
    return confit_v4_parse_option_document(
        context->catalog, context->role_root, path, 1, context->diagnostic);
  case CONFIT_V4_ROLE_MENUS:
    return confit_v4_parse_menu_document(context->catalog, path,
                                         context->diagnostic);
  case CONFIT_V4_ROLE_CHOICES:
    return confit_v4_parse_choice_document(context->catalog, path,
                                           context->diagnostic);
  case CONFIT_V4_ROLE_CONSTRAINTS:
    return confit_v4_parse_rule_document(context->catalog, path,
                                         context->diagnostic);
  case CONFIT_V4_ROLE_PROFILES:
  case CONFIT_V4_ROLE_TARGETS:
  case CONFIT_V4_ROLE_SELECTIONS:
    confit_v4_set_value_diagnostic(
        context->diagnostic, CONFIT_ERR_UNSUPPORTED, 0, path,
        "this configure-only resolver does not accept standalone profile, target, or selection documents");
    return CONFIT_ERR_UNSUPPORTED;
  default:
    return CONFIT_ERR_INTERNAL;
  }
}

static int confit_v4_compare_strings(const void *left, const void *right) {
  const char *const *left_text = (const char *const *)left;
  const char *const *right_text = (const char *const *)right;
  return strcmp(*left_text, *right_text);
}

static char *confit_v4_ascii_fold(const char *text) {
  char *copy = confit_v4_copy(text);
  if (copy == 0) return 0;
  for (size_t index = 0U; copy[index] != '\0'; ++index) {
    if (copy[index] >= 'A' && copy[index] <= 'Z')
      copy[index] = (char)(copy[index] - 'A' + 'a');
  }
  return copy;
}

static ConfitStatus confit_v4_append_path(char ***items, size_t *count,
                                          size_t maximum, const char *path) {
  char **grown;
  char *copy;
  if (*count >= maximum) return CONFIT_ERR_SCHEMA;
  copy = confit_v4_copy(path);
  if (copy == 0) return CONFIT_ERR_INTERNAL;
  grown = (char **)realloc(*items, (*count + 1U) * sizeof((*items)[0]));
  if (grown == 0) {
    free(copy);
    return CONFIT_ERR_INTERNAL;
  }
  *items = grown;
  (*items)[(*count)++] = copy;
  return CONFIT_OK;
}

static ConfitStatus confit_v4_record_document(
    ConfitV4DiscoveryContext *context, const char *path, size_t size,
    int is_config) {
  char *folded;
  ConfitStatus status;
  if (context->catalog->document_count >= CONFIT_V4_MAX_DOCUMENTS ||
      size > CONFIT_V4_MAX_FILE_BYTES ||
      context->catalog->total_bytes > CONFIT_V4_MAX_TOTAL_BYTES - size) {
    confit_v4_set_value_diagnostic(
        context->diagnostic, CONFIT_ERR_SCHEMA, 0, path,
        "Config v4 discovery file or aggregate byte budget is exhausted");
    return CONFIT_ERR_SCHEMA;
  }
  folded = confit_v4_ascii_fold(path);
  if (folded == 0) return CONFIT_ERR_INTERNAL;
  for (size_t index = 0U; index < context->catalog->document_count; ++index) {
    char *existing_folded =
        confit_v4_ascii_fold(context->catalog->documents[index]);
    if (existing_folded == 0) {
      free(folded);
      return CONFIT_ERR_INTERNAL;
    }
    if (strcmp(existing_folded, folded) == 0) {
      free(existing_folded);
      free(folded);
      confit_v4_set_value_diagnostic(
          context->diagnostic, CONFIT_ERR_SCHEMA, 0, path,
          "Config v4 discovery found a cross-root case-fold collision");
      return CONFIT_ERR_SCHEMA;
    }
    free(existing_folded);
  }
  for (size_t index = 0U; index < context->folded_count; ++index) {
    if (strcmp(context->folded_paths[index], folded) == 0) {
      free(folded);
      confit_v4_set_value_diagnostic(
          context->diagnostic, CONFIT_ERR_SCHEMA, 0, path,
          "Config v4 discovery found a duplicate or case-fold collision");
      return CONFIT_ERR_SCHEMA;
    }
  }
  status = confit_v4_append_path(&context->folded_paths,
                                 &context->folded_count,
                                 CONFIT_V4_MAX_DOCUMENTS, folded);
  free(folded);
  if (status != CONFIT_OK) return status;
  for (size_t index = 0U; index < context->catalog->document_count; ++index) {
    if (strcmp(context->catalog->documents[index], path) == 0) {
      confit_v4_set_value_diagnostic(
          context->diagnostic, CONFIT_ERR_SCHEMA, 0, path,
          "one discovery document belongs to more than one role root");
      return CONFIT_ERR_SCHEMA;
    }
  }
  status = confit_v4_append_path(&context->catalog->documents,
                                 &context->catalog->document_count,
                                 CONFIT_V4_MAX_DOCUMENTS, path);
  if (status != CONFIT_OK) return status;
  context->catalog->total_bytes += size;
  if (is_config)
    status = confit_v4_append_path(&context->config_paths,
                                   &context->config_count,
                                   CONFIT_V4_MAX_DOCUMENTS, path);
  return status;
}

#if !defined(_WIN32)
static ConfitStatus confit_v4_discover_directory(
    ConfitV4DiscoveryContext *context, const char *directory, size_t depth) {
  DIR *stream;
  struct dirent *entry;
  struct stat metadata;
  char **names = 0;
  size_t name_count = 0U;
  ConfitStatus status = CONFIT_OK;
  if (depth > CONFIT_V4_MAX_DISCOVERY_DEPTH) {
    confit_v4_set_value_diagnostic(
        context->diagnostic, CONFIT_ERR_SCHEMA, 0, directory,
        "Config v4 discovery depth exceeds the supported limit");
    return CONFIT_ERR_SCHEMA;
  }
  if (lstat(directory, &metadata) != 0 || S_ISLNK(metadata.st_mode) ||
      !S_ISDIR(metadata.st_mode)) {
    confit_v4_set_value_diagnostic(
        context->diagnostic, CONFIT_ERR_SCHEMA, 0, directory,
        "Config v4 role root must be a real directory, not a symlink");
    return CONFIT_ERR_SCHEMA;
  }
  stream = opendir(directory);
  if (stream == 0) {
    confit_v4_set_value_diagnostic(context->diagnostic, CONFIT_ERR_PARSE, 0,
                                   directory,
                                   "failed to open Config v4 role root");
    return CONFIT_ERR_PARSE;
  }
  while ((entry = readdir(stream)) != 0) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    status = confit_v4_append_path(&names, &name_count,
                                   CONFIT_V4_MAX_DISCOVERY_ENTRIES,
                                   entry->d_name);
    if (status != CONFIT_OK) break;
  }
  if (closedir(stream) != 0 && status == CONFIT_OK) status = CONFIT_ERR_PARSE;
  if (status != CONFIT_OK) goto done;
  qsort(names, name_count, sizeof(names[0]), confit_v4_compare_strings);
  for (size_t index = 0U; index < name_count; ++index) {
    char child[CONFIT_V4_MAX_PATH_BYTES + 1U];
    if (context->catalog->discovery_entry_count++ >=
        CONFIT_V4_MAX_DISCOVERY_ENTRIES) {
      confit_v4_set_value_diagnostic(
          context->diagnostic, CONFIT_ERR_SCHEMA, 0, directory,
          "Config v4 traversal entry budget is exhausted");
      status = CONFIT_ERR_SCHEMA;
      break;
    }
    if (!confit_v4_join(child, sizeof(child), directory, names[index]) ||
        lstat(child, &metadata) != 0) {
      status = CONFIT_ERR_PARSE;
      break;
    }
    if (S_ISLNK(metadata.st_mode)) {
      confit_v4_set_value_diagnostic(
          context->diagnostic, CONFIT_ERR_SCHEMA, 0, child,
          "Config v4 discovery rejects every symlink entry");
      status = CONFIT_ERR_SCHEMA;
      break;
    }
    if (S_ISDIR(metadata.st_mode)) {
      status = confit_v4_discover_directory(context, child, depth + 1U);
      if (status != CONFIT_OK) break;
      continue;
    }
    if (!S_ISREG(metadata.st_mode)) continue;
    if (strcmp(names[index], kConfigName) == 0 ||
        strcmp(names[index], kOwnersName) == 0) {
      if (metadata.st_size < 0) {
        status = CONFIT_ERR_SCHEMA;
        break;
      }
      status = confit_v4_record_document(
          context, child, (size_t)metadata.st_size,
          strcmp(names[index], kConfigName) == 0);
      if (status != CONFIT_OK) break;
    }
  }

done:
  for (size_t index = 0U; index < name_count; ++index) free(names[index]);
  free(names);
  return status;
}
#endif

static ConfitStatus confit_v4_discover_root(ConfitV4Catalog *catalog,
                                            ConfitV4Role role,
                                            const char *relative_root,
                                            ConfitDiagnostic *diagnostic) {
  char absolute[CONFIT_V4_MAX_PATH_BYTES + 1U];
  ConfitV4DiscoveryContext context;
  ConfitStatus status;
  memset(&context, 0, sizeof(context));
  if (!confit_v4_relative_path_valid(relative_root) ||
      !confit_v4_join(absolute, sizeof(absolute), catalog->repository_root,
                      relative_root)) {
    confit_v4_set_value_diagnostic(
        diagnostic, CONFIT_ERR_SCHEMA, 0, relative_root,
        "Config v4 discovery root escapes the repository or is malformed");
    return CONFIT_ERR_SCHEMA;
  }
  context.catalog = catalog;
  context.role = role;
  context.role_root = absolute;
  context.diagnostic = diagnostic;
#if defined(_WIN32)
  confit_v4_set_value_diagnostic(
      diagnostic, CONFIT_ERR_UNSUPPORTED, 0, absolute,
      "bounded Config v4 discovery is unsupported on this host");
  status = CONFIT_ERR_UNSUPPORTED;
#else
  status = confit_v4_discover_directory(&context, absolute, 0U);
#endif
  if (status == CONFIT_OK) {
    qsort(context.config_paths, context.config_count,
          sizeof(context.config_paths[0]), confit_v4_compare_strings);
    for (size_t index = 0U; index < context.config_count; ++index) {
      status = confit_v4_parse_document(&context, context.config_paths[index]);
      if (status != CONFIT_OK) break;
    }
  }
  for (size_t index = 0U; index < context.folded_count; ++index)
    free(context.folded_paths[index]);
  free(context.folded_paths);
  for (size_t index = 0U; index < context.config_count; ++index)
    free(context.config_paths[index]);
  free(context.config_paths);
  return status;
}

const ConfitV4Option *confit_v4_find_option(const ConfitV4Catalog *catalog,
                                            const char *symbol) {
  if (catalog == 0 || symbol == 0) return 0;
  for (size_t index = 0U; index < catalog->option_count; ++index) {
    if (strcmp(catalog->options[index].symbol, symbol) == 0)
      return &catalog->options[index];
  }
  return 0;
}

static const ConfitV4Menu *confit_v4_find_menu(const ConfitV4Catalog *catalog,
                                               const char *id) {
  for (size_t index = 0U; index < catalog->menu_count; ++index) {
    if (strcmp(catalog->menus[index].id, id) == 0) return &catalog->menus[index];
  }
  return 0;
}

static ConfitStatus confit_v4_visit_menu(
    const ConfitV4Catalog *catalog, size_t index, unsigned char *state,
    size_t depth, ConfitDiagnostic *diagnostic) {
  const ConfitV4Menu *menu = &catalog->menus[index];
  if (depth > CONFIT_V4_MAX_GRAPH_DEPTH) {
    confit_v4_set_diagnostic(
        diagnostic, CONFIT_ERR_DEPENDENCY, &menu->source,
        "Config v4 menu depth exceeds the supported limit");
    return CONFIT_ERR_DEPENDENCY;
  }
  state[index] = 1U;
  if (menu->parent != 0) {
    const ConfitV4Menu *parent = confit_v4_find_menu(catalog, menu->parent);
    size_t parent_index;
    if (parent == 0) {
      confit_v4_set_diagnostic(diagnostic, CONFIT_ERR_DEPENDENCY,
                               &menu->source,
                               "menu references an unknown parent");
      return CONFIT_ERR_DEPENDENCY;
    }
    parent_index = (size_t)(parent - catalog->menus);
    if (parent_index == index || state[parent_index] == 1U) {
      confit_v4_set_diagnostic(diagnostic, CONFIT_ERR_DEPENDENCY,
                               &menu->source,
                               "Config v4 menu hierarchy contains a cycle");
      return CONFIT_ERR_DEPENDENCY;
    }
    if (state[parent_index] == 0U) {
      ConfitStatus status = confit_v4_visit_menu(
          catalog, parent_index, state, depth + 1U, diagnostic);
      if (status != CONFIT_OK) return status;
    }
  }
  state[index] = 2U;
  return CONFIT_OK;
}

static int confit_v4_prerequisite_compatible(const ConfitV4Option *option) {
  return option != 0 && option->type != CONFIT_V4_OPTION_INTEGER &&
         option->type != CONFIT_V4_OPTION_STRING;
}

static ConfitStatus confit_v4_validate_symbol_list(
    const ConfitV4Catalog *catalog, const ConfitV4StringList *list,
    const char *self, ConfitDiagnostic *diagnostic) {
  for (size_t index = 0U; index < list->count; ++index) {
    const ConfitV4Option *target =
        confit_v4_find_option(catalog, list->items[index]);
    if (target == 0 || !confit_v4_prerequisite_compatible(target) ||
        (self != 0 && strcmp(self, list->items[index]) == 0)) {
      confit_v4_set_diagnostic(
          diagnostic, CONFIT_ERR_DEPENDENCY, &list->spans[index],
          target == 0
              ? "Config v4 reason edge references an unknown option"
              : !confit_v4_prerequisite_compatible(target)
                    ? "integer/string option cannot be a boolean prerequisite"
                    : "Config v4 option cannot depend on itself");
      return CONFIT_ERR_DEPENDENCY;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v4_visit_dependency(
    const ConfitV4Catalog *catalog, size_t index, unsigned char *state,
    size_t depth, ConfitDiagnostic *diagnostic) {
  const ConfitV4Option *option = &catalog->options[index];
  if (depth > CONFIT_V4_MAX_GRAPH_DEPTH) {
    confit_v4_set_diagnostic(
        diagnostic, CONFIT_ERR_DEPENDENCY, &option->declaration,
        "Config v4 dependency depth exceeds the supported limit");
    return CONFIT_ERR_DEPENDENCY;
  }
  state[index] = 1U;
  for (size_t edge = 0U; edge < option->prerequisites.count; ++edge) {
    const ConfitV4Option *target =
        confit_v4_find_option(catalog, option->prerequisites.items[edge]);
    size_t target_index;
    if (target == 0) return CONFIT_ERR_DEPENDENCY;
    target_index = (size_t)(target - catalog->options);
    if (state[target_index] == 1U) {
      confit_v4_set_diagnostic(
          diagnostic, CONFIT_ERR_DEPENDENCY,
          &option->prerequisites.spans[edge],
          "Config v4 dependency cycle preserves this closing edge span");
      return CONFIT_ERR_DEPENDENCY;
    }
    if (state[target_index] == 0U) {
      ConfitStatus status = confit_v4_visit_dependency(
          catalog, target_index, state, depth + 1U, diagnostic);
      if (status != CONFIT_OK) return status;
    }
  }
  state[index] = 2U;
  return CONFIT_OK;
}

static ConfitStatus confit_v4_validate_catalog(
    ConfitV4Catalog *catalog, ConfitDiagnostic *diagnostic) {
  unsigned char *state;
  ConfitStatus status = CONFIT_OK;
  state = (unsigned char *)calloc(catalog->menu_count, sizeof(state[0]));
  if (state == 0 && catalog->menu_count != 0U) return CONFIT_ERR_INTERNAL;
  for (size_t index = 0U; status == CONFIT_OK && index < catalog->menu_count;
       ++index) {
    if (state[index] == 0U)
      status = confit_v4_visit_menu(catalog, index, state, 1U, diagnostic);
  }
  free(state);
  if (status != CONFIT_OK) return status;
  for (size_t index = 0U; index < catalog->option_count; ++index) {
    ConfitV4Option *option = &catalog->options[index];
    if (confit_v4_find_menu(catalog, option->menu) == 0) {
      confit_v4_set_diagnostic(diagnostic, CONFIT_ERR_DEPENDENCY,
                               &option->declaration,
                               "option references an unknown TUI menu");
      return CONFIT_ERR_DEPENDENCY;
    }
    status = confit_v4_validate_symbol_list(
        catalog, &option->prerequisites, option->symbol, diagnostic);
    if (status == CONFIT_OK)
      status = confit_v4_validate_symbol_list(
          catalog, &option->visible_all, option->symbol, diagnostic);
    if (status != CONFIT_OK) return status;
  }
  for (size_t index = 0U; index < catalog->choice_count; ++index) {
    if (confit_v4_find_option(catalog, catalog->choices[index].symbol) != 0) {
      confit_v4_set_diagnostic(
          diagnostic, CONFIT_ERR_CONFLICT, &catalog->choices[index].source,
          "choice symbol collides with an option symbol");
      return CONFIT_ERR_CONFLICT;
    }
    status = confit_v4_validate_symbol_list(
        catalog, &catalog->choices[index].members, 0, diagnostic);
    if (status != CONFIT_OK) return status;
  }
  for (size_t index = 0U; index < catalog->rule_count; ++index) {
    status = confit_v4_validate_symbol_list(catalog,
                                            &catalog->rules[index].if_all, 0,
                                            diagnostic);
    if (status == CONFIT_OK)
      status = confit_v4_validate_symbol_list(
          catalog, &catalog->rules[index].require_all, 0, diagnostic);
    if (status != CONFIT_OK) return status;
  }
  state = (unsigned char *)calloc(catalog->option_count, sizeof(state[0]));
  if (state == 0 && catalog->option_count != 0U) return CONFIT_ERR_INTERNAL;
  for (size_t index = 0U; status == CONFIT_OK &&
                         index < catalog->option_count;
       ++index) {
    if (state[index] == 0U)
      status = confit_v4_visit_dependency(catalog, index, state, 1U,
                                          diagnostic);
  }
  free(state);
  if (status != CONFIT_OK) return status;
  for (size_t option_index = 0U; option_index < catalog->option_count;
       ++option_index) {
    const ConfitV4Option *option = &catalog->options[option_index];
    for (size_t provider_index = 0U;
         provider_index < option->provider_count; ++provider_index) {
      const ConfitV4Provider *provider = &option->providers[provider_index];
      for (size_t other_option = 0U; other_option < option_index;
           ++other_option) {
        const ConfitV4Option *other = &catalog->options[other_option];
        for (size_t other_provider = 0U;
             other_provider < other->provider_count; ++other_provider) {
          const ConfitV4Provider *candidate =
              &other->providers[other_provider];
          if (candidate->major == provider->major &&
              strcmp(candidate->namespace_name,
                     provider->namespace_name) == 0 &&
              (candidate->cardinality != provider->cardinality ||
               candidate->absence != provider->absence)) {
            confit_v4_set_diagnostic(
                diagnostic, CONFIT_ERR_CONFLICT, &provider->source,
                "providers for one namespace disagree on cardinality or absence");
            return CONFIT_ERR_CONFLICT;
          }
        }
      }
    }
  }
  return CONFIT_OK;
}

static int confit_v4_compare_options(const void *left, const void *right) {
  const ConfitV4Option *left_option = (const ConfitV4Option *)left;
  const ConfitV4Option *right_option = (const ConfitV4Option *)right;
  return strcmp(left_option->symbol, right_option->symbol);
}

static void confit_v4_catalog_clear(ConfitV4Catalog *catalog) {
  if (catalog == 0) return;
  free(catalog->repository_root);
  free(catalog->project_path);
  free(catalog->project_name);
  free(catalog->project_namespace);
  for (size_t role = 0U; role < CONFIT_V4_ROLE_COUNT; ++role) {
    for (size_t index = 0U; index < catalog->roots[role].count; ++index)
      free(catalog->roots[role].items[index]);
    free(catalog->roots[role].items);
  }
  confit_v4_defaults_clear(&catalog->defaults);
  for (size_t index = 0U; index < catalog->option_count; ++index)
    confit_v4_option_clear(&catalog->options[index]);
  free(catalog->options);
  for (size_t index = 0U; index < catalog->menu_count; ++index)
    confit_v4_menu_clear(&catalog->menus[index]);
  free(catalog->menus);
  for (size_t index = 0U; index < catalog->choice_count; ++index)
    confit_v4_choice_clear(&catalog->choices[index]);
  free(catalog->choices);
  for (size_t index = 0U; index < catalog->rule_count; ++index)
    confit_v4_rule_clear(&catalog->rules[index]);
  free(catalog->rules);
  for (size_t index = 0U; index < catalog->document_count; ++index)
    free(catalog->documents[index]);
  free(catalog->documents);
  memset(catalog, 0, sizeof(*catalog));
}

ConfitStatus confit_v4_catalog_load(const char *repository_root,
                                    ConfitV4Catalog **out_catalog,
                                    ConfitDiagnostic *diagnostic) {
  ConfitV4Catalog *catalog;
  ConfitStatus status;
  char canonical[CONFIT_V4_MAX_PATH_BYTES + 1U];
#if !defined(_WIN32)
  struct stat metadata;
#endif
  if (repository_root == 0 || out_catalog == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                          repository_root, 0U, 0U,
                          "invalid Config v4 catalog argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_catalog = 0;
#if defined(_WIN32)
  confit_diagnostic_set(diagnostic, CONFIT_ERR_UNSUPPORTED, repository_root,
                        0U, 0U,
                        "Config v4 candidate discovery is unsupported on Windows");
  return CONFIT_ERR_UNSUPPORTED;
#else
  if (repository_root[0] != '/' ||
      realpath(repository_root, canonical) == 0 ||
      strcmp(repository_root, canonical) != 0 ||
      lstat(repository_root, &metadata) != 0 || S_ISLNK(metadata.st_mode) ||
      !S_ISDIR(metadata.st_mode)) {
    confit_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA, repository_root, 0U, 0U,
        "Config v4 repository root must be canonical, absolute, and non-symlink");
    return CONFIT_ERR_SCHEMA;
  }
#endif
  catalog = (ConfitV4Catalog *)calloc(1U, sizeof(*catalog));
  if (catalog == 0) return CONFIT_ERR_INTERNAL;
  catalog->repository_root = confit_v4_copy(repository_root);
  catalog->project_path =
      (char *)malloc(strlen(repository_root) + strlen("/config/project.toml") +
                     1U);
  if (catalog->repository_root == 0 || catalog->project_path == 0) {
    confit_v4_catalog_clear(catalog);
    free(catalog);
    return CONFIT_ERR_INTERNAL;
  }
  (void)snprintf(catalog->project_path,
                 strlen(repository_root) + strlen("/config/project.toml") + 1U,
                 "%s/config/project.toml", repository_root);
  status = confit_v4_parse_project(catalog, diagnostic);
  if (status == CONFIT_OK) {
#if !defined(_WIN32)
    if (lstat(catalog->project_path, &metadata) != 0 ||
        !S_ISREG(metadata.st_mode) || metadata.st_size < 0 ||
        (uintmax_t)metadata.st_size > CONFIT_V4_MAX_FILE_BYTES) {
      status = CONFIT_ERR_SCHEMA;
    } else {
      status = confit_v4_append_path(
          &catalog->documents, &catalog->document_count,
          CONFIT_V4_MAX_DOCUMENTS, catalog->project_path);
      if (status == CONFIT_OK) catalog->total_bytes = (size_t)metadata.st_size;
    }
#endif
  }
  for (size_t role = 0U; status == CONFIT_OK && role < CONFIT_V4_ROLE_COUNT;
       ++role) {
    for (size_t root = 0U;
         status == CONFIT_OK && root < catalog->roots[role].count; ++root) {
      status = confit_v4_discover_root(catalog, (ConfitV4Role)role,
                                       catalog->roots[role].items[root],
                                       diagnostic);
    }
  }
  if (status == CONFIT_OK) {
    qsort(catalog->options, catalog->option_count, sizeof(catalog->options[0]),
          confit_v4_compare_options);
    qsort(catalog->documents, catalog->document_count,
          sizeof(catalog->documents[0]), confit_v4_compare_strings);
    status = confit_v4_validate_catalog(catalog, diagnostic);
  }
  if (status != CONFIT_OK) {
    if (diagnostic != 0 && !confit_diagnostic_has_error(diagnostic)) {
      confit_diagnostic_set(diagnostic, status, catalog->project_path, 1U, 1U,
                            "Config v4 catalog validation failed");
    }
    confit_v4_catalog_clear(catalog);
    free(catalog);
    return status;
  }
  *out_catalog = catalog;
  return CONFIT_OK;
}

void confit_v4_catalog_free(ConfitV4Catalog *catalog) {
  if (catalog == 0) return;
  confit_v4_catalog_clear(catalog);
  free(catalog);
}

size_t confit_v4_catalog_document_count(const ConfitV4Catalog *catalog) {
  return catalog != 0 ? catalog->document_count : 0U;
}

size_t confit_v4_catalog_option_count(const ConfitV4Catalog *catalog) {
  return catalog != 0 ? catalog->option_count : 0U;
}

size_t confit_v4_catalog_menu_count(const ConfitV4Catalog *catalog) {
  return catalog != 0 ? catalog->menu_count : 0U;
}

size_t confit_v4_catalog_choice_count(const ConfitV4Catalog *catalog) {
  return catalog != 0 ? catalog->choice_count : 0U;
}

size_t confit_v4_catalog_rule_count(const ConfitV4Catalog *catalog) {
  return catalog != 0 ? catalog->rule_count : 0U;
}

static ConfitV4SourceSpan confit_v4_source_view(
    const ConfitV4OwnedSpan *span) {
  ConfitV4SourceSpan view;
  view.path = span != 0 ? span->path : 0;
  view.line = span != 0 ? span->line : 0U;
  view.column = span != 0 ? span->column : 0U;
  return view;
}

int confit_v4_catalog_option(const ConfitV4Catalog *catalog,
                             const char *symbol,
                             ConfitV4OptionView *out_option) {
  const ConfitV4Option *option = confit_v4_find_option(catalog, symbol);
  if (option == 0 || out_option == 0) return 0;
  memset(out_option, 0, sizeof(*out_option));
  out_option->symbol = option->symbol;
  out_option->projection = option->projection;
  out_option->type = option->type;
  out_option->prompt = option->prompt;
  out_option->help = option->help;
  out_option->menu = option->menu;
  out_option->menu_order = option->menu_order;
  out_option->owner = option->owner;
  out_option->since = option->since;
  out_option->stability = option->stability;
  out_option->default_value = option->default_value;
  out_option->minimum = option->minimum;
  out_option->maximum = option->maximum;
  out_option->domain_count = option->type == CONFIT_V4_OPTION_PLACEMENT
                                 ? option->allowed.count
                                 : option->values.count;
  out_option->domain_values = (const char *const *)(
      option->type == CONFIT_V4_OPTION_PLACEMENT ? option->allowed.items
                                                 : option->values.items);
  out_option->enabled_value_count = option->enabled_values.count;
  out_option->enabled_values =
      (const char *const *)option->enabled_values.items;
  out_option->tag_count = option->tags.count;
  out_option->tags = (const char *const *)option->tags.items;
  out_option->prerequisite_count = option->prerequisites.count;
  out_option->prerequisites =
      (const char *const *)option->prerequisites.items;
  out_option->visible_count = option->visible_all.count;
  out_option->visible_all = (const char *const *)option->visible_all.items;
  out_option->provider_count = option->provider_count;
  out_option->declaration = confit_v4_source_view(&option->declaration);
  out_option->owner_source = confit_v4_source_view(&option->owner_source);
  out_option->since_source = confit_v4_source_view(&option->since_source);
  out_option->stability_source =
      confit_v4_source_view(&option->stability_source);
  out_option->tags_source = confit_v4_source_view(&option->tags_source);
  out_option->menu_order_source =
      confit_v4_source_view(&option->menu_order_source);
  out_option->default_source = confit_v4_source_view(&option->default_source);
  return 1;
}
