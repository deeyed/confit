#include "confit/schema_v2.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "confit/host.h"
#include "confit/parser_v2.h"

#include "model_internal.h"

static const char kV2AllocationFailed[] = "failed to allocate schema v2 model";
static const char kV2MissingProject[] = "[project] table is required";
static const char kV2MissingField[] = "required schema v2 field is missing";
static const char kV2UnknownProjectField[] = "unknown schema v2 project field";
static const char kV2UnknownImportField[] = "unknown schema v2 import field";
static const char kV2UnknownOptionField[] = "unknown schema v2 option field";
static const char kV2UnknownMenuField[] = "unknown schema v2 menu field";
static const char kV2UnknownChoiceField[] = "unknown schema v2 choice field";
static const char kV2UnknownConstraintField[] =
    "unknown schema v2 constraint field";
static const char kV2WrongValueType[] = "schema v2 field has an invalid value type";
static const char kV2InvalidValue[] = "schema v2 field has an invalid value";
static const char kV2DuplicateDefinition[] =
    "duplicate schema v2 semantic definition";
static const char kV2InvalidImportPath[] = "invalid schema v2 import path";
static const char kV2ImportEscapesConfigRoot[] =
    "schema v2 import escapes project config root";
static const char kV2DuplicateImport[] = "duplicate schema v2 canonical import";
static const char kV2ImportCycle[] = "schema v2 import cycle";
static const char kV2InvalidOptionId[] = "invalid schema v2 option id";
static const char kV2InvalidNamespace[] = "option id is outside project namespace";
static const char kV2UnsupportedProjectVersion[] =
    "project does not declare schema_version = 2";
static const char kV2UnsupportedImportVersion[] =
    "import source does not declare schema_version = 2";

static void confit_v2_error(ConfitDiagnostic *diagnostic, ConfitStatus status,
                            const ConfitV2TomlValue *value,
                            const char *message) {
  confit_diagnostic_set(diagnostic, status,
                        value != 0 ? confit_v2_toml_value_source(value) : 0,
                        value != 0 ? confit_v2_toml_value_line(value) : 0U,
                        value != 0 ? confit_v2_toml_value_column(value) : 0U,
                        message);
}

static int confit_v2_key_is_allowed(const char *key,
                                    const char *const *allowed,
                                    size_t allowed_count) {
  size_t index;

  for (index = 0U; index < allowed_count; ++index) {
    if (strcmp(key, allowed[index]) == 0) {
      return 1;
    }
  }
  return 0;
}

static ConfitStatus confit_v2_check_table_keys(
    const ConfitV2TomlValue *table, const char *const *allowed,
    size_t allowed_count, const char *message, ConfitDiagnostic *diagnostic) {
  size_t index;

  if (confit_v2_toml_value_type(table) != CONFIT_V2_TOML_VALUE_TABLE) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, table, kV2WrongValueType);
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < confit_v2_toml_table_size(table); ++index) {
    const char *key = confit_v2_toml_table_key_at(table, index);
    if (key == 0 || !confit_v2_key_is_allowed(key, allowed, allowed_count)) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA,
                      confit_v2_toml_table_value_at(table, index), message);
      return CONFIT_ERR_SCHEMA;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_copy_span(ConfitV2Project *project,
                                         const ConfitV2TomlValue *value,
                                         size_t local_offset,
                                         ConfitV2SourceSpan *out,
                                         ConfitDiagnostic *diagnostic) {
  const char *path;

  memset(out, 0, sizeof(*out));
  if (value == 0) {
    return CONFIT_OK;
  }
  path = confit_v2_toml_value_source(value);
  if (path != 0) {
    out->path = confit_v2_strdup(&project->allocator, path);
    if (out->path == 0) {
      confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, value, kV2AllocationFailed);
      return CONFIT_ERR_INTERNAL;
    }
  }
  out->line = confit_v2_toml_value_line(value);
  out->column = confit_v2_toml_value_column(value);
  out->local_offset = local_offset;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_copy_string(ConfitV2Project *project,
                                           const ConfitV2TomlValue *value,
                                           char **out,
                                           ConfitDiagnostic *diagnostic) {
  const char *text;
  size_t size;
  char *copy;

  *out = 0;
  if (!confit_v2_toml_value_string(value, &text, &size)) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2WrongValueType);
    return CONFIT_ERR_SCHEMA;
  }
  if (memchr(text, '\0', size) != 0) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2InvalidValue);
    return CONFIT_ERR_SCHEMA;
  }
  copy = (char *)confit_v2_allocate(&project->allocator, size + 1U);
  if (copy == 0) {
    confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, value, kV2AllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  if (size > 0U) {
    memcpy(copy, text, size);
  }
  copy[size] = '\0';
  *out = copy;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_append_string(ConfitV2Project *project,
                                             ConfitV2StringList *list,
                                             char *owned_text,
                                             const ConfitV2TomlValue *value,
                                             ConfitDiagnostic *diagnostic) {
  char **grown;

  if (list->count == SIZE_MAX / sizeof(*list->items)) {
    confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, value, kV2AllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  grown = (char **)confit_v2_reallocate(
      &project->allocator, list->items, (list->count + 1U) * sizeof(*grown));
  if (grown == 0) {
    confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, value, kV2AllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  list->items = grown;
  list->items[list->count] = owned_text;
  list->count += 1U;
  return CONFIT_OK;
}

static int confit_v2_string_list_contains(const ConfitV2StringList *list,
                                           const char *text) {
  size_t index;

  for (index = 0U; index < list->count; ++index) {
    if (strcmp(list->items[index], text) == 0) {
      return 1;
    }
  }
  return 0;
}

static int confit_v2_is_identifier_segment(const char *text, size_t size) {
  size_t index;

  if (size == 0U || !((text[0] >= 'a' && text[0] <= 'z') ||
                      (text[0] >= 'A' && text[0] <= 'Z') || text[0] == '_')) {
    return 0;
  }
  for (index = 1U; index < size; ++index) {
    if (!((text[index] >= 'a' && text[index] <= 'z') ||
          (text[index] >= 'A' && text[index] <= 'Z') ||
          (text[index] >= '0' && text[index] <= '9') || text[index] == '_')) {
      return 0;
    }
  }
  return 1;
}

static int confit_v2_is_dotted_identifier(const char *text) {
  const char *segment;
  const char *cursor;

  if (text == 0 || text[0] == '\0') {
    return 0;
  }
  segment = text;
  for (cursor = text;; ++cursor) {
    if (*cursor == '.' || *cursor == '\0') {
      if (!confit_v2_is_identifier_segment(segment, (size_t)(cursor - segment))) {
        return 0;
      }
      if (*cursor == '\0') {
        return 1;
      }
      segment = cursor + 1;
    }
  }
}

static int confit_v2_is_logical_path(const char *path) {
  const char *segment;
  const char *cursor;

  if (path == 0 || path[0] == '\0' || path[0] == '/' || path[0] == '\\' ||
      strchr(path, '\\') != 0) {
    return 0;
  }
  segment = path;
  for (cursor = path;; ++cursor) {
    if (*cursor == '/' || *cursor == '\0') {
      const size_t size = (size_t)(cursor - segment);
      if (size == 0U || (size == 1U && segment[0] == '.') ||
          (size == 2U && segment[0] == '.' && segment[1] == '.')) {
        return 0;
      }
      if (*cursor == '\0') {
        return 1;
      }
      segment = cursor + 1;
    }
  }
}

static int confit_v2_is_host_path_separator(char value) {
  return value == '/' || value == '\\';
}

static int confit_v2_path_is_within_root(const char *root, const char *path) {
  const size_t root_size = strlen(root);

  return strncmp(root, path, root_size) == 0 &&
         (path[root_size] == '\0' || confit_v2_is_host_path_separator(path[root_size]));
}

static ConfitStatus confit_v2_canonicalize_import_path(
    const char *config_root, const char *logical_path, char *out_canonical,
    size_t out_canonical_size, const ConfitV2TomlValue *value,
    ConfitDiagnostic *diagnostic) {
  char joined_path[4096];
  char canonical_root[4096];
  ConfitStatus status;

  status = confit_host_path_join(joined_path, sizeof(joined_path), config_root,
                                 logical_path, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_path_canonicalize(out_canonical, out_canonical_size,
                                          joined_path, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_path_canonicalize(canonical_root, sizeof(canonical_root),
                                          config_root, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  if (!confit_v2_path_is_within_root(canonical_root, out_canonical)) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value,
                    kV2ImportEscapesConfigRoot);
    return CONFIT_ERR_SCHEMA;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_parse_string_list(
    ConfitV2Project *project, const ConfitV2TomlValue *value,
    int require_unique, int require_logical_path, ConfitV2StringList *out,
    ConfitDiagnostic *diagnostic) {
  size_t index;

  memset(out, 0, sizeof(*out));
  if (confit_v2_toml_value_type(value) != CONFIT_V2_TOML_VALUE_ARRAY) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2WrongValueType);
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < confit_v2_toml_array_size(value); ++index) {
    const ConfitV2TomlValue *item = confit_v2_toml_array_at(value, index);
    char *copy;
    ConfitStatus status = confit_v2_copy_string(project, item, &copy, diagnostic);

    if (status != CONFIT_OK) {
      confit_v2_string_list_clear(&project->allocator, out);
      return status;
    }
    if ((require_logical_path && !confit_v2_is_logical_path(copy)) ||
        (require_unique && confit_v2_string_list_contains(out, copy))) {
      confit_v2_deallocate(&project->allocator, copy);
      confit_v2_string_list_clear(&project->allocator, out);
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, item, kV2InvalidValue);
      return CONFIT_ERR_SCHEMA;
    }
    status = confit_v2_append_string(project, out, copy, item, diagnostic);
    if (status != CONFIT_OK) {
      confit_v2_deallocate(&project->allocator, copy);
      confit_v2_string_list_clear(&project->allocator, out);
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_parse_expression(ConfitV2Project *project,
                                                const ConfitV2TomlValue *value,
                                                ConfitV2ExpressionText *out,
                                                ConfitDiagnostic *diagnostic) {
  ConfitStatus status;

  memset(out, 0, sizeof(*out));
  status = confit_v2_copy_string(project, value, &out->text, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_v2_copy_span(project, value, 0U, &out->span, diagnostic);
  if (status != CONFIT_OK) {
    confit_v2_deallocate(&project->allocator, out->text);
    out->text = 0;
  }
  return status;
}

static ConfitStatus confit_v2_parse_type(const ConfitV2TomlValue *value,
                                         ConfitV2OptionType *out,
                                         ConfitDiagnostic *diagnostic) {
  const char *text;
  size_t size;

  if (!confit_v2_toml_value_string(value, &text, &size)) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2WrongValueType);
    return CONFIT_ERR_SCHEMA;
  }
#define CONFIT_V2_TYPE_CASE(name, value_name)                                    \
  if (size == sizeof(name) - 1U && memcmp(text, name, sizeof(name) - 1U) == 0) { \
    *out = value_name;                                                             \
    return CONFIT_OK;                                                              \
  }
  CONFIT_V2_TYPE_CASE("bool", CONFIT_V2_OPTION_TYPE_BOOL)
  CONFIT_V2_TYPE_CASE("tristate", CONFIT_V2_OPTION_TYPE_TRISTATE)
  CONFIT_V2_TYPE_CASE("int", CONFIT_V2_OPTION_TYPE_INT)
  CONFIT_V2_TYPE_CASE("uint", CONFIT_V2_OPTION_TYPE_UINT)
  CONFIT_V2_TYPE_CASE("hex", CONFIT_V2_OPTION_TYPE_HEX)
  CONFIT_V2_TYPE_CASE("float", CONFIT_V2_OPTION_TYPE_FLOAT)
  CONFIT_V2_TYPE_CASE("string", CONFIT_V2_OPTION_TYPE_STRING)
  CONFIT_V2_TYPE_CASE("enum", CONFIT_V2_OPTION_TYPE_ENUM)
  CONFIT_V2_TYPE_CASE("path", CONFIT_V2_OPTION_TYPE_PATH)
  CONFIT_V2_TYPE_CASE("string_list", CONFIT_V2_OPTION_TYPE_STRING_LIST)
  CONFIT_V2_TYPE_CASE("path_list", CONFIT_V2_OPTION_TYPE_PATH_LIST)
  CONFIT_V2_TYPE_CASE("enum_set", CONFIT_V2_OPTION_TYPE_ENUM_SET)
#undef CONFIT_V2_TYPE_CASE
  confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2InvalidValue);
  return CONFIT_ERR_SCHEMA;
}

static ConfitStatus confit_v2_parse_write_domain(
    const ConfitV2TomlValue *value, ConfitV2WriteDomain *out,
    ConfitDiagnostic *diagnostic) {
  const char *text;
  size_t size;

  if (!confit_v2_toml_value_string(value, &text, &size)) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2WrongValueType);
    return CONFIT_ERR_SCHEMA;
  }
#define CONFIT_V2_DOMAIN_CASE(name, value_name)                                  \
  if (size == sizeof(name) - 1U && memcmp(text, name, sizeof(name) - 1U) == 0) { \
    *out = value_name;                                                            \
    return CONFIT_OK;                                                             \
  }
  CONFIT_V2_DOMAIN_CASE("schema", CONFIT_V2_WRITE_DOMAIN_SCHEMA)
  CONFIT_V2_DOMAIN_CASE("profile", CONFIT_V2_WRITE_DOMAIN_PROFILE)
  CONFIT_V2_DOMAIN_CASE("target", CONFIT_V2_WRITE_DOMAIN_TARGET)
  CONFIT_V2_DOMAIN_CASE("computed", CONFIT_V2_WRITE_DOMAIN_COMPUTED)
#undef CONFIT_V2_DOMAIN_CASE
  confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2InvalidValue);
  return CONFIT_ERR_SCHEMA;
}

static ConfitStatus confit_v2_parse_stability(const ConfitV2TomlValue *value,
                                               ConfitV2Stability *out,
                                               ConfitDiagnostic *diagnostic) {
  const char *text;
  size_t size;

  if (!confit_v2_toml_value_string(value, &text, &size)) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2WrongValueType);
    return CONFIT_ERR_SCHEMA;
  }
#define CONFIT_V2_STABILITY_CASE(name, value_name)                               \
  if (size == sizeof(name) - 1U && memcmp(text, name, sizeof(name) - 1U) == 0) { \
    *out = value_name;                                                            \
    return CONFIT_OK;                                                             \
  }
  CONFIT_V2_STABILITY_CASE("experimental", CONFIT_V2_STABILITY_EXPERIMENTAL)
  CONFIT_V2_STABILITY_CASE("stable", CONFIT_V2_STABILITY_STABLE)
  CONFIT_V2_STABILITY_CASE("deprecated", CONFIT_V2_STABILITY_DEPRECATED)
  CONFIT_V2_STABILITY_CASE("internal", CONFIT_V2_STABILITY_INTERNAL)
#undef CONFIT_V2_STABILITY_CASE
  confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2InvalidValue);
  return CONFIT_ERR_SCHEMA;
}

static ConfitStatus confit_v2_parse_bool(const ConfitV2TomlValue *value, int *out,
                                          ConfitDiagnostic *diagnostic) {
  if (!confit_v2_toml_value_bool(value, out)) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2WrongValueType);
    return CONFIT_ERR_SCHEMA;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_parse_value(ConfitV2Project *project,
                                           ConfitV2OptionType type,
                                           const ConfitV2TomlValue *value,
                                           ConfitV2Value *out,
                                           ConfitDiagnostic *diagnostic) {
  const ConfitV2TypeDescriptor *descriptor = confit_v2_type_descriptor(type);
  int64_t signed_value;
  double float_value;
  ConfitStatus status;

  memset(out, 0, sizeof(*out));
  if (descriptor == 0) {
    confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, value, kV2InvalidValue);
    return CONFIT_ERR_INTERNAL;
  }
  out->kind = descriptor->value_kind;
  switch (type) {
  case CONFIT_V2_OPTION_TYPE_BOOL:
    return confit_v2_parse_bool(value, &out->as.bool_value, diagnostic);
  case CONFIT_V2_OPTION_TYPE_TRISTATE: {
    const char *text;
    size_t size;
    if (!confit_v2_toml_value_string(value, &text, &size) || size != 1U ||
        (text[0] != 'n' && text[0] != 'm' && text[0] != 'y')) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2InvalidValue);
      return CONFIT_ERR_SCHEMA;
    }
    out->as.tristate_value = text[0];
    return CONFIT_OK;
  }
  case CONFIT_V2_OPTION_TYPE_INT:
    if (!confit_v2_toml_value_int64(value, &out->as.int_value)) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2WrongValueType);
      return CONFIT_ERR_SCHEMA;
    }
    return CONFIT_OK;
  case CONFIT_V2_OPTION_TYPE_UINT:
  case CONFIT_V2_OPTION_TYPE_HEX:
    if (!confit_v2_toml_value_int64(value, &signed_value) || signed_value < 0) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2InvalidValue);
      return CONFIT_ERR_SCHEMA;
    }
    out->as.uint_value = (uint64_t)signed_value;
    return CONFIT_OK;
  case CONFIT_V2_OPTION_TYPE_FLOAT:
    if (confit_v2_toml_value_float64(value, &float_value)) {
      if (!isfinite(float_value)) {
        confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2InvalidValue);
        return CONFIT_ERR_SCHEMA;
      }
      out->as.float_value = float_value;
      return CONFIT_OK;
    }
    if (confit_v2_toml_value_int64(value, &signed_value)) {
      out->as.float_value = (double)signed_value;
      return CONFIT_OK;
    }
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2WrongValueType);
    return CONFIT_ERR_SCHEMA;
  case CONFIT_V2_OPTION_TYPE_STRING:
  case CONFIT_V2_OPTION_TYPE_ENUM:
  case CONFIT_V2_OPTION_TYPE_PATH:
    status = confit_v2_copy_string(project, value, &out->as.string_value,
                                   diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    if (type == CONFIT_V2_OPTION_TYPE_PATH &&
        !confit_v2_is_logical_path(out->as.string_value)) {
      confit_v2_deallocate(&project->allocator, out->as.string_value);
      out->as.string_value = 0;
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2InvalidValue);
      return CONFIT_ERR_SCHEMA;
    }
    return CONFIT_OK;
  case CONFIT_V2_OPTION_TYPE_STRING_LIST:
  case CONFIT_V2_OPTION_TYPE_PATH_LIST:
  case CONFIT_V2_OPTION_TYPE_ENUM_SET:
    return confit_v2_parse_string_list(
        project, value, type == CONFIT_V2_OPTION_TYPE_ENUM_SET,
        type == CONFIT_V2_OPTION_TYPE_PATH_LIST, &out->as.string_list,
        diagnostic);
  case CONFIT_V2_OPTION_TYPE_INVALID:
  default:
    confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, value, kV2InvalidValue);
    return CONFIT_ERR_INTERNAL;
  }
}

static int confit_v2_value_compare(const ConfitV2Value *left,
                                    const ConfitV2Value *right) {
  if (left->kind == CONFIT_V2_VALUE_INT) {
    return left->as.int_value < right->as.int_value
               ? -1
               : (left->as.int_value > right->as.int_value ? 1 : 0);
  }
  if (left->kind == CONFIT_V2_VALUE_UINT) {
    return left->as.uint_value < right->as.uint_value
               ? -1
               : (left->as.uint_value > right->as.uint_value ? 1 : 0);
  }
  if (left->kind == CONFIT_V2_VALUE_FLOAT) {
    return left->as.float_value < right->as.float_value
               ? -1
               : (left->as.float_value > right->as.float_value ? 1 : 0);
  }
  return 0;
}

static ConfitStatus confit_v2_parse_assignment(
    ConfitV2Project *project, ConfitV2OptionType type,
    const ConfitV2TomlValue *value, ConfitV2Assignment *out,
    ConfitDiagnostic *diagnostic) {
  ConfitStatus status;

  memset(out, 0, sizeof(*out));
  status = confit_v2_parse_value(project, type, value, &out->value, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_v2_copy_span(project, value, 0U, &out->span, diagnostic);
  if (status != CONFIT_OK) {
    confit_v2_value_clear(&project->allocator, &out->value);
    return status;
  }
  out->is_set = 1;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_parse_range(ConfitV2Project *project,
                                           ConfitV2OptionType type,
                                           const ConfitV2TomlValue *value,
                                           ConfitV2NumericRange *out,
                                           ConfitDiagnostic *diagnostic) {
  static const char *const kRangeFields[] = {"min", "max"};
  const ConfitV2TomlValue *min_value;
  const ConfitV2TomlValue *max_value;
  ConfitStatus status;

  memset(out, 0, sizeof(*out));
  if (type != CONFIT_V2_OPTION_TYPE_INT && type != CONFIT_V2_OPTION_TYPE_UINT &&
      type != CONFIT_V2_OPTION_TYPE_HEX && type != CONFIT_V2_OPTION_TYPE_FLOAT) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2InvalidValue);
    return CONFIT_ERR_SCHEMA;
  }
  status = confit_v2_check_table_keys(value, kRangeFields,
                                      sizeof(kRangeFields) / sizeof(kRangeFields[0]),
                                      kV2UnknownOptionField, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  min_value = confit_v2_toml_table_find(value, "min");
  max_value = confit_v2_toml_table_find(value, "max");
  if (min_value == 0 || max_value == 0) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2MissingField);
    return CONFIT_ERR_SCHEMA;
  }
  status = confit_v2_parse_value(project, type, min_value, &out->min_value,
                                  diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_v2_parse_value(project, type, max_value, &out->max_value,
                                  diagnostic);
  if (status != CONFIT_OK) {
    confit_v2_value_clear(&project->allocator, &out->min_value);
    return status;
  }
  if (confit_v2_value_compare(&out->min_value, &out->max_value) > 0) {
    confit_v2_value_clear(&project->allocator, &out->min_value);
    confit_v2_value_clear(&project->allocator, &out->max_value);
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2InvalidValue);
    return CONFIT_ERR_SCHEMA;
  }
  status = confit_v2_copy_span(project, value, 0U, &out->span, diagnostic);
  if (status != CONFIT_OK) {
    confit_v2_value_clear(&project->allocator, &out->min_value);
    confit_v2_value_clear(&project->allocator, &out->max_value);
    return status;
  }
  out->is_set = 1;
  return CONFIT_OK;
}

static int confit_v2_candidate_index(const ConfitV2StringList *candidates,
                                     const char *value, size_t *out_index) {
  size_t index;

  for (index = 0U; index < candidates->count; ++index) {
    if (strcmp(candidates->items[index], value) == 0) {
      if (out_index != 0) {
        *out_index = index;
      }
      return 1;
    }
  }
  return 0;
}

static ConfitStatus confit_v2_validate_enum_value(
    ConfitV2Project *project, ConfitV2Symbol *symbol,
    ConfitV2Assignment *assignment, const ConfitV2TomlValue *value,
    ConfitDiagnostic *diagnostic) {
  size_t index;

  if (!assignment->is_set) {
    return CONFIT_OK;
  }
  if (symbol->type == CONFIT_V2_OPTION_TYPE_ENUM) {
    if (!confit_v2_candidate_index(&symbol->values,
                                   assignment->value.as.string_value, 0)) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2InvalidValue);
      return CONFIT_ERR_SCHEMA;
    }
  } else if (symbol->type == CONFIT_V2_OPTION_TYPE_ENUM_SET) {
    for (index = 0U; index < assignment->value.as.string_list.count; ++index) {
      if (!confit_v2_candidate_index(
              &symbol->values, assignment->value.as.string_list.items[index], 0)) {
        confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2InvalidValue);
        return CONFIT_ERR_SCHEMA;
      }
    }
    for (index = 0U; index < assignment->value.as.string_list.count; ++index) {
      size_t cursor;
      for (cursor = index + 1U; cursor < assignment->value.as.string_list.count;
           ++cursor) {
        size_t left_index;
        size_t right_index;
        if (confit_v2_candidate_index(&symbol->values,
                                      assignment->value.as.string_list.items[index],
                                      &left_index) &&
            confit_v2_candidate_index(
                &symbol->values, assignment->value.as.string_list.items[cursor],
                &right_index) &&
            left_index > right_index) {
          char *swap = assignment->value.as.string_list.items[index];
          assignment->value.as.string_list.items[index] =
              assignment->value.as.string_list.items[cursor];
          assignment->value.as.string_list.items[cursor] = swap;
        }
      }
    }
  }
  (void)project;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_append_symbol(ConfitV2Project *project,
                                             ConfitV2Symbol **out,
                                             const ConfitV2TomlValue *value,
                                             ConfitDiagnostic *diagnostic) {
  ConfitV2Symbol *grown;

  if (project->symbol_count == SIZE_MAX / sizeof(*project->symbols)) {
    confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, value, kV2AllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  grown = (ConfitV2Symbol *)confit_v2_reallocate(
      &project->allocator, project->symbols,
      (project->symbol_count + 1U) * sizeof(*project->symbols));
  if (grown == 0) {
    confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, value, kV2AllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  project->symbols = grown;
  *out = &project->symbols[project->symbol_count];
  memset(*out, 0, sizeof(**out));
  project->symbol_count += 1U;
  return CONFIT_OK;
}

static int confit_v2_project_has_symbol(const ConfitV2Project *project,
                                        const char *id) {
  size_t index;
  for (index = 0U; index + 1U < project->symbol_count; ++index) {
    if (strcmp(project->symbols[index].id, id) == 0) {
      return 1;
    }
  }
  return 0;
}

static ConfitStatus confit_v2_parse_defaults(ConfitV2Project *project,
                                              ConfitV2Symbol *symbol,
                                              const ConfitV2TomlValue *value,
                                              ConfitDiagnostic *diagnostic) {
  static const char *const kFields[] = {"when", "value", "priority"};
  size_t index;

  if (confit_v2_toml_value_type(value) != CONFIT_V2_TOML_VALUE_ARRAY) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2WrongValueType);
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < confit_v2_toml_array_size(value); ++index) {
    const ConfitV2TomlValue *entry = confit_v2_toml_array_at(value, index);
    const ConfitV2TomlValue *when;
    const ConfitV2TomlValue *assigned_value;
    const ConfitV2TomlValue *priority;
    ConfitV2ConditionalDefault *grown;
    int64_t priority_value;
    size_t slot;
    ConfitStatus status;

    status = confit_v2_check_table_keys(entry, kFields,
                                        sizeof(kFields) / sizeof(kFields[0]),
                                        kV2UnknownOptionField, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    when = confit_v2_toml_table_find(entry, "when");
    assigned_value = confit_v2_toml_table_find(entry, "value");
    priority = confit_v2_toml_table_find(entry, "priority");
    if (when == 0 || assigned_value == 0 || priority == 0 ||
        !confit_v2_toml_value_int64(priority, &priority_value) ||
        priority_value < INT32_MIN || priority_value > INT32_MAX) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, entry, kV2InvalidValue);
      return CONFIT_ERR_SCHEMA;
    }
    if (symbol->default_count == SIZE_MAX / sizeof(*symbol->defaults)) {
      confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, entry, kV2AllocationFailed);
      return CONFIT_ERR_INTERNAL;
    }
    grown = (ConfitV2ConditionalDefault *)confit_v2_reallocate(
        &project->allocator, symbol->defaults,
        (symbol->default_count + 1U) * sizeof(*symbol->defaults));
    if (grown == 0) {
      confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, entry, kV2AllocationFailed);
      return CONFIT_ERR_INTERNAL;
    }
    symbol->defaults = grown;
    slot = symbol->default_count;
    memset(&symbol->defaults[slot], 0, sizeof(symbol->defaults[slot]));
    symbol->default_count += 1U;
    status = confit_v2_parse_expression(
        project, when, &symbol->defaults[slot].when, diagnostic);
    if (status == CONFIT_OK) {
      status = confit_v2_parse_assignment(
          project, symbol->type, assigned_value,
          &symbol->defaults[slot].assignment, diagnostic);
    }
    if (status == CONFIT_OK) {
      status = confit_v2_validate_enum_value(
          project, symbol, &symbol->defaults[slot].assignment, assigned_value,
          diagnostic);
    }
    if (status == CONFIT_OK) {
      status = confit_v2_copy_span(project, entry, 0U, &symbol->defaults[slot].span,
                                   diagnostic);
    }
    if (status != CONFIT_OK) {
      return status;
    }
    symbol->defaults[slot].priority = (int32_t)priority_value;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_parse_suggestions(
    ConfitV2Project *project, ConfitV2Symbol *symbol,
    const ConfitV2TomlValue *value, ConfitDiagnostic *diagnostic) {
  static const char *const kFields[] = {"when", "value", "message"};
  size_t index;

  if (confit_v2_toml_value_type(value) != CONFIT_V2_TOML_VALUE_ARRAY) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2WrongValueType);
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < confit_v2_toml_array_size(value); ++index) {
    const ConfitV2TomlValue *entry = confit_v2_toml_array_at(value, index);
    const ConfitV2TomlValue *when;
    const ConfitV2TomlValue *assigned_value;
    const ConfitV2TomlValue *message;
    ConfitV2Suggestion *grown;
    ConfitStatus status;
    size_t slot;

    status = confit_v2_check_table_keys(entry, kFields,
                                        sizeof(kFields) / sizeof(kFields[0]),
                                        kV2UnknownOptionField, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    when = confit_v2_toml_table_find(entry, "when");
    assigned_value = confit_v2_toml_table_find(entry, "value");
    message = confit_v2_toml_table_find(entry, "message");
    if (when == 0 || assigned_value == 0 || message == 0) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, entry, kV2MissingField);
      return CONFIT_ERR_SCHEMA;
    }
    if (symbol->suggestion_count == SIZE_MAX / sizeof(*symbol->suggestions)) {
      confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, entry, kV2AllocationFailed);
      return CONFIT_ERR_INTERNAL;
    }
    grown = (ConfitV2Suggestion *)confit_v2_reallocate(
        &project->allocator, symbol->suggestions,
        (symbol->suggestion_count + 1U) * sizeof(*symbol->suggestions));
    if (grown == 0) {
      confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, entry, kV2AllocationFailed);
      return CONFIT_ERR_INTERNAL;
    }
    symbol->suggestions = grown;
    slot = symbol->suggestion_count;
    memset(&symbol->suggestions[slot], 0, sizeof(symbol->suggestions[slot]));
    symbol->suggestion_count += 1U;
    status = confit_v2_parse_expression(
        project, when, &symbol->suggestions[slot].when, diagnostic);
    if (status == CONFIT_OK) {
      status = confit_v2_parse_assignment(
          project, symbol->type, assigned_value,
          &symbol->suggestions[slot].assignment, diagnostic);
    }
    if (status == CONFIT_OK) {
      status = confit_v2_validate_enum_value(
          project, symbol, &symbol->suggestions[slot].assignment, assigned_value,
          diagnostic);
    }
    if (status == CONFIT_OK) {
      status = confit_v2_copy_string(
          project, message, &symbol->suggestions[slot].message, diagnostic);
    }
    if (status == CONFIT_OK) {
      status = confit_v2_copy_span(project, entry, 0U,
                                   &symbol->suggestions[slot].span, diagnostic);
    }
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_parse_emit(const ConfitV2TomlValue *value,
                                         unsigned int *out,
                                         ConfitDiagnostic *diagnostic) {
  size_t index;
  unsigned int mask = 0U;

  if (confit_v2_toml_value_type(value) != CONFIT_V2_TOML_VALUE_ARRAY) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2WrongValueType);
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < confit_v2_toml_array_size(value); ++index) {
    const ConfitV2TomlValue *item = confit_v2_toml_array_at(value, index);
    const char *text;
    size_t size;
    unsigned int bit = 0U;
    if (!confit_v2_toml_value_string(item, &text, &size)) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, item, kV2WrongValueType);
      return CONFIT_ERR_SCHEMA;
    }
    if (size == 6U && memcmp(text, "header", 6U) == 0) {
      bit = CONFIT_V2_EMIT_HEADER;
    } else if (size == 5U && memcmp(text, "cmake", 5U) == 0) {
      bit = CONFIT_V2_EMIT_CMAKE;
    } else if (size == 5U && memcmp(text, "qstar", 5U) == 0) {
      bit = CONFIT_V2_EMIT_QSTAR;
    } else if (size == 9U && memcmp(text, "selection", 9U) == 0) {
      bit = CONFIT_V2_EMIT_SELECTION;
    } else {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, item, kV2InvalidValue);
      return CONFIT_ERR_SCHEMA;
    }
    if ((mask & bit) != 0U) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, item, kV2InvalidValue);
      return CONFIT_ERR_SCHEMA;
    }
    mask |= bit;
  }
  *out = mask;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_parse_symbol(ConfitV2Project *project,
                                            const char *id,
                                            const ConfitV2TomlValue *table,
                                            ConfitDiagnostic *diagnostic) {
  static const char *const kFields[] = {
      "type",        "default",      "required",      "write_domain",
      "user_override", "prompt",     "help",          "menu",
      "tags",        "owner",        "since",         "stability",
      "emit",        "values",       "range",         "computed",
      "available_if", "visible_if",  "defaults",      "suggestions",
  };
  const ConfitV2TomlValue *type_value;
  const ConfitV2TomlValue *domain_value;
  const ConfitV2TomlValue *owner_value;
  const ConfitV2TomlValue *since_value;
  const ConfitV2TomlValue *stability_value;
  const ConfitV2TomlValue *value;
  ConfitV2Symbol *symbol;
  ConfitStatus status;
  size_t namespace_size;

  status = confit_v2_check_table_keys(table, kFields,
                                      sizeof(kFields) / sizeof(kFields[0]),
                                      kV2UnknownOptionField, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  if (!confit_v2_is_dotted_identifier(id)) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, table, kV2InvalidOptionId);
    return CONFIT_ERR_SCHEMA;
  }
  namespace_size = strlen(project->namespace_name);
  if (strncmp(id, project->namespace_name, namespace_size) != 0 ||
      id[namespace_size] != '.') {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, table, kV2InvalidNamespace);
    return CONFIT_ERR_SCHEMA;
  }
  type_value = confit_v2_toml_table_find(table, "type");
  domain_value = confit_v2_toml_table_find(table, "write_domain");
  owner_value = confit_v2_toml_table_find(table, "owner");
  since_value = confit_v2_toml_table_find(table, "since");
  stability_value = confit_v2_toml_table_find(table, "stability");
  if (type_value == 0 || domain_value == 0 || owner_value == 0 ||
      since_value == 0 || stability_value == 0) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, table, kV2MissingField);
    return CONFIT_ERR_SCHEMA;
  }
  status = confit_v2_append_symbol(project, &symbol, table, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  symbol->id = confit_v2_strdup(&project->allocator, id);
  if (symbol->id == 0) {
    confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, table, kV2AllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  if (confit_v2_project_has_symbol(project, id)) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, table, kV2DuplicateDefinition);
    return CONFIT_ERR_SCHEMA;
  }
  status = confit_v2_parse_type(type_value, &symbol->type, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_parse_write_domain(domain_value, &symbol->write_domain,
                                          diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_parse_stability(stability_value, &symbol->stability,
                                       diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_copy_string(project, owner_value, &symbol->owner,
                                   diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_copy_string(project, since_value, &symbol->since,
                                   diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_copy_span(project, table, 0U, &symbol->span, diagnostic);
  }
  if (status != CONFIT_OK) {
    return status;
  }
  value = confit_v2_toml_table_find(table, "required");
  if (value != 0 && (status = confit_v2_parse_bool(value, &symbol->required,
                                                    diagnostic)) != CONFIT_OK) {
    return status;
  }
  value = confit_v2_toml_table_find(table, "user_override");
  if (value != 0 &&
      (status = confit_v2_parse_bool(value, &symbol->user_override, diagnostic)) !=
          CONFIT_OK) {
    return status;
  }
  value = confit_v2_toml_table_find(table, "prompt");
  if (value != 0 &&
      (status = confit_v2_copy_string(project, value, &symbol->prompt,
                                       diagnostic)) != CONFIT_OK) {
    return status;
  }
  value = confit_v2_toml_table_find(table, "help");
  if (value != 0 &&
      (status = confit_v2_copy_string(project, value, &symbol->help,
                                       diagnostic)) != CONFIT_OK) {
    return status;
  }
  value = confit_v2_toml_table_find(table, "menu");
  if (value != 0 &&
      (status = confit_v2_copy_string(project, value, &symbol->menu,
                                       diagnostic)) != CONFIT_OK) {
    return status;
  }
  value = confit_v2_toml_table_find(table, "tags");
  if (value != 0 &&
      (status = confit_v2_parse_string_list(project, value, 0, 0, &symbol->tags,
                                             diagnostic)) != CONFIT_OK) {
    return status;
  }
  value = confit_v2_toml_table_find(table, "emit");
  if (value != 0 &&
      (status = confit_v2_parse_emit(value, &symbol->emit_mask, diagnostic)) !=
          CONFIT_OK) {
    return status;
  }
  value = confit_v2_toml_table_find(table, "values");
  if (value != 0 &&
      (status = confit_v2_parse_string_list(project, value, 1, 0, &symbol->values,
                                             diagnostic)) != CONFIT_OK) {
    return status;
  }
  if (confit_v2_type_descriptor(symbol->type)->requires_values &&
      symbol->values.count == 0U) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, table, kV2MissingField);
    return CONFIT_ERR_SCHEMA;
  }
  if (!confit_v2_type_descriptor(symbol->type)->requires_values &&
      symbol->values.count != 0U) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2InvalidValue);
    return CONFIT_ERR_SCHEMA;
  }
  value = confit_v2_toml_table_find(table, "range");
  if (value != 0 &&
      (status = confit_v2_parse_range(project, symbol->type, value, &symbol->range,
                                      diagnostic)) != CONFIT_OK) {
    return status;
  }
  value = confit_v2_toml_table_find(table, "default");
  if (value != 0 &&
      (status = confit_v2_parse_assignment(project, symbol->type, value,
                                            &symbol->default_value,
                                            diagnostic)) != CONFIT_OK) {
    return status;
  }
  if (value != 0 &&
      (status = confit_v2_validate_enum_value(project, symbol,
                                               &symbol->default_value, value,
                                               diagnostic)) != CONFIT_OK) {
    return status;
  }
  if (symbol->range.is_set && symbol->default_value.is_set &&
      (confit_v2_value_compare(&symbol->default_value.value,
                               &symbol->range.min_value) < 0 ||
       confit_v2_value_compare(&symbol->default_value.value,
                               &symbol->range.max_value) > 0)) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2InvalidValue);
    return CONFIT_ERR_SCHEMA;
  }
  value = confit_v2_toml_table_find(table, "computed");
  if (value != 0 &&
      (status = confit_v2_parse_expression(project, value, &symbol->computed,
                                            diagnostic)) != CONFIT_OK) {
    return status;
  }
  value = confit_v2_toml_table_find(table, "available_if");
  if (value != 0 &&
      (status = confit_v2_parse_expression(project, value, &symbol->available_if,
                                            diagnostic)) != CONFIT_OK) {
    return status;
  }
  value = confit_v2_toml_table_find(table, "visible_if");
  if (value != 0 &&
      (status = confit_v2_parse_expression(project, value, &symbol->visible_if,
                                            diagnostic)) != CONFIT_OK) {
    return status;
  }
  value = confit_v2_toml_table_find(table, "defaults");
  if (value != 0 &&
      (status = confit_v2_parse_defaults(project, symbol, value, diagnostic)) !=
          CONFIT_OK) {
    return status;
  }
  value = confit_v2_toml_table_find(table, "suggestions");
  if (value != 0 &&
      (status = confit_v2_parse_suggestions(project, symbol, value,
                                             diagnostic)) != CONFIT_OK) {
    return status;
  }
  if (symbol->write_domain == CONFIT_V2_WRITE_DOMAIN_COMPUTED) {
    if (symbol->computed.text == 0 || symbol->default_value.is_set ||
        symbol->required || symbol->user_override || symbol->default_count != 0U ||
        symbol->available_if.text != 0) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, table, kV2InvalidValue);
      return CONFIT_ERR_SCHEMA;
    }
  } else if (symbol->computed.text != 0 ||
             (!symbol->default_value.is_set && !symbol->required &&
              symbol->default_count == 0U)) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, table, kV2InvalidValue);
    return CONFIT_ERR_SCHEMA;
  }
  if (symbol->user_override &&
      symbol->write_domain != CONFIT_V2_WRITE_DOMAIN_PROFILE &&
      symbol->write_domain != CONFIT_V2_WRITE_DOMAIN_TARGET) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, table, kV2InvalidValue);
    return CONFIT_ERR_SCHEMA;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_append_menu(ConfitV2Project *project,
                                           ConfitV2MenuNode **out,
                                           const ConfitV2TomlValue *value,
                                           ConfitDiagnostic *diagnostic) {
  ConfitV2MenuNode *grown;
  if (project->menu_count == SIZE_MAX / sizeof(*project->menus)) {
    confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, value, kV2AllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  grown = (ConfitV2MenuNode *)confit_v2_reallocate(
      &project->allocator, project->menus,
      (project->menu_count + 1U) * sizeof(*project->menus));
  if (grown == 0) {
    confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, value, kV2AllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  project->menus = grown;
  *out = &project->menus[project->menu_count];
  memset(*out, 0, sizeof(**out));
  project->menu_count += 1U;
  return CONFIT_OK;
}

static int confit_v2_project_has_menu(const ConfitV2Project *project,
                                      const char *id) {
  size_t index;
  for (index = 0U; index + 1U < project->menu_count; ++index) {
    if (strcmp(project->menus[index].id, id) == 0) {
      return 1;
    }
  }
  return 0;
}

static ConfitStatus confit_v2_parse_menu_references(
    ConfitV2Project *project, ConfitV2MenuNode *menu,
    const ConfitV2TomlValue *value, ConfitDiagnostic *diagnostic) {
  static const char *const kFields[] = {"option", "read_only"};
  size_t index;

  if (confit_v2_toml_value_type(value) != CONFIT_V2_TOML_VALUE_ARRAY) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2WrongValueType);
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < confit_v2_toml_array_size(value); ++index) {
    const ConfitV2TomlValue *entry = confit_v2_toml_array_at(value, index);
    const ConfitV2TomlValue *option;
    const ConfitV2TomlValue *read_only;
    ConfitV2MenuReference *grown;
    ConfitV2MenuReference *reference;
    ConfitStatus status;

    status = confit_v2_check_table_keys(
        entry, kFields, sizeof(kFields) / sizeof(kFields[0]),
        kV2UnknownMenuField, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    option = confit_v2_toml_table_find(entry, "option");
    read_only = confit_v2_toml_table_find(entry, "read_only");
    if (option == 0 || read_only == 0) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, entry, kV2MissingField);
      return CONFIT_ERR_SCHEMA;
    }
    if (menu->reference_count == SIZE_MAX / sizeof(*menu->references)) {
      confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, entry, kV2AllocationFailed);
      return CONFIT_ERR_INTERNAL;
    }
    grown = (ConfitV2MenuReference *)confit_v2_reallocate(
        &project->allocator, menu->references,
        (menu->reference_count + 1U) * sizeof(*menu->references));
    if (grown == 0) {
      confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, entry, kV2AllocationFailed);
      return CONFIT_ERR_INTERNAL;
    }
    menu->references = grown;
    reference = &menu->references[menu->reference_count];
    memset(reference, 0, sizeof(*reference));
    menu->reference_count += 1U;
    status = confit_v2_copy_string(project, option, &reference->option_id,
                                   diagnostic);
    if (status == CONFIT_OK) {
      status = confit_v2_parse_bool(read_only, &reference->read_only, diagnostic);
    }
    if (status == CONFIT_OK) {
      status = confit_v2_copy_span(project, entry, 0U, &reference->span,
                                   diagnostic);
    }
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_parse_menu(ConfitV2Project *project,
                                          const char *id,
                                          const ConfitV2TomlValue *table,
                                          ConfitDiagnostic *diagnostic) {
  static const char *const kFields[] = {"prompt", "parent", "order",
                                         "visible_if", "references"};
  const ConfitV2TomlValue *value;
  ConfitV2MenuNode *menu;
  ConfitStatus status;
  int64_t order;

  status = confit_v2_check_table_keys(table, kFields,
                                      sizeof(kFields) / sizeof(kFields[0]),
                                      kV2UnknownMenuField, diagnostic);
  if (status != CONFIT_OK || !confit_v2_is_dotted_identifier(id)) {
    if (status == CONFIT_OK) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, table, kV2InvalidValue);
      status = CONFIT_ERR_SCHEMA;
    }
    return status;
  }
  value = confit_v2_toml_table_find(table, "prompt");
  if (value == 0) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, table, kV2MissingField);
    return CONFIT_ERR_SCHEMA;
  }
  status = confit_v2_append_menu(project, &menu, table, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  menu->id = confit_v2_strdup(&project->allocator, id);
  if (menu->id == 0) {
    confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, table, kV2AllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  if (confit_v2_project_has_menu(project, id)) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, table, kV2DuplicateDefinition);
    return CONFIT_ERR_SCHEMA;
  }
  status = confit_v2_copy_string(project, value, &menu->prompt, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_copy_span(project, table, 0U, &menu->span, diagnostic);
  }
  value = confit_v2_toml_table_find(table, "parent");
  if (status == CONFIT_OK && value != 0) {
    status = confit_v2_copy_string(project, value, &menu->parent, diagnostic);
  }
  value = confit_v2_toml_table_find(table, "order");
  if (status == CONFIT_OK && value != 0) {
    if (!confit_v2_toml_value_int64(value, &order) || order < INT32_MIN ||
        order > INT32_MAX) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2InvalidValue);
      return CONFIT_ERR_SCHEMA;
    }
    menu->order = (int32_t)order;
  }
  value = confit_v2_toml_table_find(table, "visible_if");
  if (status == CONFIT_OK && value != 0) {
    status = confit_v2_parse_expression(project, value, &menu->visible_if,
                                        diagnostic);
  }
  value = confit_v2_toml_table_find(table, "references");
  if (status == CONFIT_OK && value != 0) {
    status = confit_v2_parse_menu_references(project, menu, value, diagnostic);
  }
  return status;
}

static ConfitStatus confit_v2_append_choice(ConfitV2Project *project,
                                             ConfitV2Choice **out,
                                             const ConfitV2TomlValue *value,
                                             ConfitDiagnostic *diagnostic) {
  ConfitV2Choice *grown;
  if (project->choice_count == SIZE_MAX / sizeof(*project->choices)) {
    confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, value, kV2AllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  grown = (ConfitV2Choice *)confit_v2_reallocate(
      &project->allocator, project->choices,
      (project->choice_count + 1U) * sizeof(*project->choices));
  if (grown == 0) {
    confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, value, kV2AllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  project->choices = grown;
  *out = &project->choices[project->choice_count];
  memset(*out, 0, sizeof(**out));
  project->choice_count += 1U;
  return CONFIT_OK;
}

static int confit_v2_project_has_choice(const ConfitV2Project *project,
                                        const char *id) {
  size_t index;
  for (index = 0U; index + 1U < project->choice_count; ++index) {
    if (strcmp(project->choices[index].id, id) == 0) {
      return 1;
    }
  }
  return 0;
}

static ConfitStatus confit_v2_parse_choice_defaults(
    ConfitV2Project *project, ConfitV2Choice *choice,
    const ConfitV2TomlValue *value, ConfitDiagnostic *diagnostic) {
  static const char *const kFields[] = {"when", "member", "priority"};
  size_t index;

  if (confit_v2_toml_value_type(value) != CONFIT_V2_TOML_VALUE_ARRAY) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2WrongValueType);
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < confit_v2_toml_array_size(value); ++index) {
    const ConfitV2TomlValue *entry = confit_v2_toml_array_at(value, index);
    const ConfitV2TomlValue *when;
    const ConfitV2TomlValue *member;
    const ConfitV2TomlValue *priority;
    ConfitV2ChoiceDefault *grown;
    int64_t priority_value;
    ConfitStatus status;
    size_t slot;

    status = confit_v2_check_table_keys(entry, kFields,
                                        sizeof(kFields) / sizeof(kFields[0]),
                                        kV2UnknownChoiceField, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    when = confit_v2_toml_table_find(entry, "when");
    member = confit_v2_toml_table_find(entry, "member");
    priority = confit_v2_toml_table_find(entry, "priority");
    if (when == 0 || member == 0 || priority == 0 ||
        !confit_v2_toml_value_int64(priority, &priority_value) ||
        priority_value < INT32_MIN || priority_value > INT32_MAX) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, entry, kV2InvalidValue);
      return CONFIT_ERR_SCHEMA;
    }
    if (choice->default_count == SIZE_MAX / sizeof(*choice->defaults)) {
      confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, entry, kV2AllocationFailed);
      return CONFIT_ERR_INTERNAL;
    }
    grown = (ConfitV2ChoiceDefault *)confit_v2_reallocate(
        &project->allocator, choice->defaults,
        (choice->default_count + 1U) * sizeof(*choice->defaults));
    if (grown == 0) {
      confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, entry, kV2AllocationFailed);
      return CONFIT_ERR_INTERNAL;
    }
    choice->defaults = grown;
    slot = choice->default_count;
    memset(&choice->defaults[slot], 0, sizeof(choice->defaults[slot]));
    choice->default_count += 1U;
    status = confit_v2_parse_expression(
        project, when, &choice->defaults[slot].when, diagnostic);
    if (status == CONFIT_OK) {
      status = confit_v2_copy_string(project, member, &choice->defaults[slot].member,
                                     diagnostic);
    }
    if (status == CONFIT_OK) {
      status = confit_v2_copy_span(project, entry, 0U, &choice->defaults[slot].span,
                                   diagnostic);
    }
    if (status != CONFIT_OK) {
      return status;
    }
    choice->defaults[slot].priority = (int32_t)priority_value;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_parse_choice(ConfitV2Project *project,
                                            const char *id,
                                            const ConfitV2TomlValue *table,
                                            ConfitDiagnostic *diagnostic) {
  static const char *const kFields[] = {"member_type", "members", "cardinality",
                                         "available_if", "visible_if", "defaults"};
  const ConfitV2TomlValue *member_type;
  const ConfitV2TomlValue *members;
  const ConfitV2TomlValue *cardinality;
  const ConfitV2TomlValue *value;
  ConfitV2Choice *choice;
  const char *text;
  size_t size;
  ConfitStatus status;

  status = confit_v2_check_table_keys(table, kFields,
                                      sizeof(kFields) / sizeof(kFields[0]),
                                      kV2UnknownChoiceField, diagnostic);
  if (status != CONFIT_OK || !confit_v2_is_dotted_identifier(id)) {
    if (status == CONFIT_OK) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, table, kV2InvalidValue);
      status = CONFIT_ERR_SCHEMA;
    }
    return status;
  }
  member_type = confit_v2_toml_table_find(table, "member_type");
  members = confit_v2_toml_table_find(table, "members");
  cardinality = confit_v2_toml_table_find(table, "cardinality");
  if (member_type == 0 || members == 0 || cardinality == 0) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, table, kV2MissingField);
    return CONFIT_ERR_SCHEMA;
  }
  status = confit_v2_append_choice(project, &choice, table, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  choice->id = confit_v2_strdup(&project->allocator, id);
  if (choice->id == 0) {
    confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, table, kV2AllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  if (confit_v2_project_has_choice(project, id)) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, table, kV2DuplicateDefinition);
    return CONFIT_ERR_SCHEMA;
  }
  status = confit_v2_parse_type(member_type, &choice->member_type, diagnostic);
  if (status != CONFIT_OK || (choice->member_type != CONFIT_V2_OPTION_TYPE_BOOL &&
                              choice->member_type != CONFIT_V2_OPTION_TYPE_TRISTATE)) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, member_type, kV2InvalidValue);
    return CONFIT_ERR_SCHEMA;
  }
  status = confit_v2_parse_string_list(project, members, 1, 0, &choice->members,
                                       diagnostic);
  if (status != CONFIT_OK || choice->members.count == 0U) {
    if (status == CONFIT_OK) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, members, kV2InvalidValue);
      status = CONFIT_ERR_SCHEMA;
    }
    return status;
  }
  if (!confit_v2_toml_value_string(cardinality, &text, &size)) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, cardinality, kV2WrongValueType);
    return CONFIT_ERR_SCHEMA;
  }
  if (size == 11U && memcmp(text, "exactly-one", 11U) == 0) {
    choice->cardinality = CONFIT_V2_CHOICE_CARDINALITY_EXACTLY_ONE;
  } else if (size == 11U && memcmp(text, "zero-or-one", 11U) == 0) {
    choice->cardinality = CONFIT_V2_CHOICE_CARDINALITY_ZERO_OR_ONE;
  } else if (size == 11U && memcmp(text, "one-or-more", 11U) == 0) {
    choice->cardinality = CONFIT_V2_CHOICE_CARDINALITY_ONE_OR_MORE;
  } else {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, cardinality, kV2InvalidValue);
    return CONFIT_ERR_SCHEMA;
  }
  status = confit_v2_copy_span(project, table, 0U, &choice->span, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  value = confit_v2_toml_table_find(table, "available_if");
  if (value != 0 &&
      (status = confit_v2_parse_expression(project, value, &choice->available_if,
                                            diagnostic)) != CONFIT_OK) {
    return status;
  }
  value = confit_v2_toml_table_find(table, "visible_if");
  if (value != 0 &&
      (status = confit_v2_parse_expression(project, value, &choice->visible_if,
                                            diagnostic)) != CONFIT_OK) {
    return status;
  }
  value = confit_v2_toml_table_find(table, "defaults");
  if (value != 0 &&
      (status = confit_v2_parse_choice_defaults(project, choice, value,
                                                 diagnostic)) != CONFIT_OK) {
    return status;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_append_constraint(
    ConfitV2Project *project, ConfitV2Constraint **out,
    const ConfitV2TomlValue *value, ConfitDiagnostic *diagnostic) {
  ConfitV2Constraint *grown;
  if (project->constraint_count == SIZE_MAX / sizeof(*project->constraints)) {
    confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, value, kV2AllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  grown = (ConfitV2Constraint *)confit_v2_reallocate(
      &project->allocator, project->constraints,
      (project->constraint_count + 1U) * sizeof(*project->constraints));
  if (grown == 0) {
    confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, value, kV2AllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  project->constraints = grown;
  *out = &project->constraints[project->constraint_count];
  memset(*out, 0, sizeof(**out));
  project->constraint_count += 1U;
  return CONFIT_OK;
}

static int confit_v2_project_has_constraint(const ConfitV2Project *project,
                                            const char *id) {
  size_t index;
  for (index = 0U; index + 1U < project->constraint_count; ++index) {
    if (strcmp(project->constraints[index].id, id) == 0) {
      return 1;
    }
  }
  return 0;
}

static ConfitStatus confit_v2_parse_constraint(ConfitV2Project *project,
                                                const ConfitV2TomlValue *table,
                                                ConfitDiagnostic *diagnostic) {
  static const char *const kFields[] = {"id", "when", "require", "message"};
  const ConfitV2TomlValue *id;
  const ConfitV2TomlValue *when;
  const ConfitV2TomlValue *require;
  const ConfitV2TomlValue *message;
  ConfitV2Constraint *constraint;
  ConfitStatus status;

  status = confit_v2_check_table_keys(table, kFields,
                                      sizeof(kFields) / sizeof(kFields[0]),
                                      kV2UnknownConstraintField, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  id = confit_v2_toml_table_find(table, "id");
  when = confit_v2_toml_table_find(table, "when");
  require = confit_v2_toml_table_find(table, "require");
  message = confit_v2_toml_table_find(table, "message");
  if (id == 0 || when == 0 || require == 0 || message == 0) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, table, kV2MissingField);
    return CONFIT_ERR_SCHEMA;
  }
  status = confit_v2_append_constraint(project, &constraint, table, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_v2_copy_string(project, id, &constraint->id, diagnostic);
  if (status == CONFIT_OK &&
      (!confit_v2_is_dotted_identifier(constraint->id) ||
       confit_v2_project_has_constraint(project, constraint->id))) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, id,
                    confit_v2_project_has_constraint(project, constraint->id)
                        ? kV2DuplicateDefinition
                        : kV2InvalidValue);
    return CONFIT_ERR_SCHEMA;
  }
  if (status == CONFIT_OK) {
    status = confit_v2_parse_expression(project, when, &constraint->when,
                                        diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_parse_expression(project, require, &constraint->require,
                                        diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_copy_string(project, message, &constraint->message,
                                   diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_copy_span(project, table, 0U, &constraint->span,
                                 diagnostic);
  }
  return status;
}

static ConfitStatus confit_v2_append_import(
    ConfitV2Project *project, const char *config_root, char *path,
    const ConfitV2TomlValue *value, size_t *out_index,
    ConfitDiagnostic *diagnostic);

static ConfitStatus confit_v2_visit_import(
    ConfitV2Project *project, const char *config_root, size_t import_index,
    ConfitDiagnostic *diagnostic);

static ConfitStatus confit_v2_parse_import_document(
    ConfitV2Project *project, const char *config_root, const char *path,
    ConfitDiagnostic *diagnostic) {
  static const char *const kRootFields[] = {"schema_version", "imports", "option",
                                             "menu", "choice", "constraint"};
  ConfitV2TomlDocument *document;
  const ConfitV2TomlValue *root;
  const ConfitV2TomlValue *declarations;
  const ConfitV2TomlValue *value;
  ConfitStatus status;
  int64_t schema_version;
  size_t index;

  document = 0;
  status = confit_v2_toml_parse_file(path, &document, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  root = confit_v2_toml_document_root(document);
  status = confit_v2_check_table_keys(root, kRootFields,
                                      sizeof(kRootFields) / sizeof(kRootFields[0]),
                                      kV2UnknownImportField, diagnostic);
  if (status != CONFIT_OK) {
    confit_v2_toml_document_free(document);
    return status;
  }
  value = confit_v2_toml_table_find(root, "schema_version");
  if (value == 0 || !confit_v2_toml_value_int64(value, &schema_version) ||
      schema_version != 2) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value != 0 ? value : root,
                    kV2UnsupportedImportVersion);
    confit_v2_toml_document_free(document);
    return CONFIT_ERR_SCHEMA;
  }
  value = confit_v2_toml_table_find(root, "imports");
  if (value != 0) {
    if (confit_v2_toml_value_type(value) != CONFIT_V2_TOML_VALUE_ARRAY) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2WrongValueType);
      confit_v2_toml_document_free(document);
      return CONFIT_ERR_SCHEMA;
    }
    for (index = 0U; index < confit_v2_toml_array_size(value); ++index) {
      const ConfitV2TomlValue *item = confit_v2_toml_array_at(value, index);
      char *import_path;
      size_t import_index;

      status = confit_v2_copy_string(project, item, &import_path, diagnostic);
      if (status != CONFIT_OK) {
        confit_v2_toml_document_free(document);
        return status;
      }
      if (!confit_v2_is_logical_path(import_path)) {
        confit_v2_deallocate(&project->allocator, import_path);
        confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, item, kV2InvalidImportPath);
        confit_v2_toml_document_free(document);
        return CONFIT_ERR_SCHEMA;
      }
      status = confit_v2_append_import(project, config_root, import_path, item,
                                        &import_index, diagnostic);
      if (status != CONFIT_OK) {
        confit_v2_deallocate(&project->allocator, import_path);
        confit_v2_toml_document_free(document);
        return status;
      }
      (void)import_index;
      status = confit_v2_visit_import(project, config_root, import_index,
                                      diagnostic);
      if (status != CONFIT_OK) {
        confit_v2_toml_document_free(document);
        return status;
      }
    }
  }
  declarations = confit_v2_toml_table_find(root, "option");
  if (declarations != 0) {
    if (confit_v2_toml_value_type(declarations) != CONFIT_V2_TOML_VALUE_TABLE) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, declarations, kV2WrongValueType);
      confit_v2_toml_document_free(document);
      return CONFIT_ERR_SCHEMA;
    }
    for (index = 0U; index < confit_v2_toml_table_size(declarations); ++index) {
      status = confit_v2_parse_symbol(
          project, confit_v2_toml_table_key_at(declarations, index),
          confit_v2_toml_table_value_at(declarations, index), diagnostic);
      if (status != CONFIT_OK) {
        confit_v2_toml_document_free(document);
        return status;
      }
    }
  }
  declarations = confit_v2_toml_table_find(root, "menu");
  if (declarations != 0) {
    if (confit_v2_toml_value_type(declarations) != CONFIT_V2_TOML_VALUE_TABLE) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, declarations, kV2WrongValueType);
      confit_v2_toml_document_free(document);
      return CONFIT_ERR_SCHEMA;
    }
    for (index = 0U; index < confit_v2_toml_table_size(declarations); ++index) {
      status = confit_v2_parse_menu(
          project, confit_v2_toml_table_key_at(declarations, index),
          confit_v2_toml_table_value_at(declarations, index), diagnostic);
      if (status != CONFIT_OK) {
        confit_v2_toml_document_free(document);
        return status;
      }
    }
  }
  declarations = confit_v2_toml_table_find(root, "choice");
  if (declarations != 0) {
    if (confit_v2_toml_value_type(declarations) != CONFIT_V2_TOML_VALUE_TABLE) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, declarations, kV2WrongValueType);
      confit_v2_toml_document_free(document);
      return CONFIT_ERR_SCHEMA;
    }
    for (index = 0U; index < confit_v2_toml_table_size(declarations); ++index) {
      status = confit_v2_parse_choice(
          project, confit_v2_toml_table_key_at(declarations, index),
          confit_v2_toml_table_value_at(declarations, index), diagnostic);
      if (status != CONFIT_OK) {
        confit_v2_toml_document_free(document);
        return status;
      }
    }
  }
  declarations = confit_v2_toml_table_find(root, "constraint");
  if (declarations != 0) {
    if (confit_v2_toml_value_type(declarations) != CONFIT_V2_TOML_VALUE_ARRAY) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, declarations, kV2WrongValueType);
      confit_v2_toml_document_free(document);
      return CONFIT_ERR_SCHEMA;
    }
    for (index = 0U; index < confit_v2_toml_array_size(declarations); ++index) {
      status = confit_v2_parse_constraint(
          project, confit_v2_toml_array_at(declarations, index), diagnostic);
      if (status != CONFIT_OK) {
        confit_v2_toml_document_free(document);
        return status;
      }
    }
  }
  confit_v2_toml_document_free(document);
  return CONFIT_OK;
}

static ConfitStatus confit_v2_append_import(
    ConfitV2Project *project, const char *config_root, char *path,
    const ConfitV2TomlValue *value, size_t *out_index,
    ConfitDiagnostic *diagnostic) {
  ConfitV2Import *grown;
  ConfitStatus status;
  char canonical_path[4096];
  char *canonical_copy;
  size_t index;

  if (out_index == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, config_root,
                          0, 0, "missing schema v2 import index output");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_index = 0U;
  status = confit_v2_canonicalize_import_path(config_root, path, canonical_path,
                                              sizeof(canonical_path), value,
                                              diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  for (index = 0U; index < project->import_count; ++index) {
    if (strcmp(project->imports[index].canonical_path, canonical_path) == 0) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value,
                      project->imports[index].state ==
                              CONFIT_V2_IMPORT_STATE_VISITING
                          ? kV2ImportCycle
                          : kV2DuplicateImport);
      return CONFIT_ERR_SCHEMA;
    }
  }
  if (project->import_count == SIZE_MAX / sizeof(*project->imports)) {
    confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, value, kV2AllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  grown = (ConfitV2Import *)confit_v2_reallocate(
      &project->allocator, project->imports,
      (project->import_count + 1U) * sizeof(*project->imports));
  if (grown == 0) {
    confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, value, kV2AllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  project->imports = grown;
  memset(&project->imports[project->import_count], 0,
         sizeof(project->imports[project->import_count]));
  canonical_copy = confit_v2_strdup(&project->allocator, canonical_path);
  if (canonical_copy == 0) {
    confit_v2_error(diagnostic, CONFIT_ERR_INTERNAL, value, kV2AllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  project->imports[project->import_count].path = path;
  project->imports[project->import_count].canonical_path = canonical_copy;
  status = confit_v2_copy_span(project, value, 0U,
                               &project->imports[project->import_count].span,
                               diagnostic);
  if (status != CONFIT_OK) {
    project->imports[project->import_count].path = 0;
    confit_v2_deallocate(&project->allocator, canonical_copy);
    project->imports[project->import_count].canonical_path = 0;
    return status;
  }
  project->imports[project->import_count].state = CONFIT_V2_IMPORT_STATE_DECLARED;
  *out_index = project->import_count;
  project->import_count += 1U;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_visit_import(
    ConfitV2Project *project, const char *config_root, size_t import_index,
    ConfitDiagnostic *diagnostic) {
  ConfitV2Import *import;
  ConfitStatus status;

  if (import_index >= project->import_count) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, config_root, 0, 0,
                          "schema v2 import index is invalid");
    return CONFIT_ERR_INTERNAL;
  }
  import = &project->imports[import_index];
  if (import->state == CONFIT_V2_IMPORT_STATE_COMPLETE) {
    return CONFIT_OK;
  }
  if (import->state == CONFIT_V2_IMPORT_STATE_VISITING) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, import->span.path,
                          import->span.line, import->span.column,
                          kV2ImportCycle);
    return CONFIT_ERR_SCHEMA;
  }
  import->state = CONFIT_V2_IMPORT_STATE_VISITING;
  status = confit_v2_parse_import_document(project, config_root,
                                            import->canonical_path, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  project->imports[import_index].state = CONFIT_V2_IMPORT_STATE_COMPLETE;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_find_config_root(
    const char *project_root, char *out_root, size_t out_root_size,
    char *out_project_path, size_t out_project_path_size,
    ConfitDiagnostic *diagnostic) {
  char direct[1024];
  char config_root[1024];
  ConfitStatus status;

  if (project_root == 0 || project_root[0] == '\0') {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, project_root,
                          0, 0, "missing project root");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  status = confit_host_path_join(direct, sizeof(direct), project_root,
                                 "project.toml", diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  if (confit_host_file_exists(direct)) {
    if (strlen(project_root) + 1U > out_root_size ||
        strlen(direct) + 1U > out_project_path_size) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, project_root,
                            0, 0, "config root buffer is too small");
      return CONFIT_ERR_INVALID_ARGUMENT;
    }
    memcpy(out_root, project_root, strlen(project_root) + 1U);
    memcpy(out_project_path, direct, strlen(direct) + 1U);
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

static ConfitStatus confit_v2_parse_project_document(
    ConfitV2Project *project, const char *config_root, const char *project_path,
    ConfitDiagnostic *diagnostic) {
  static const char *const kFields[] = {"name", "namespace", "version",
                                         "default_target", "schema_version", "imports",
                                         "profile_dirs", "target_dirs",
                                         "selection_dirs"};
  ConfitV2TomlDocument *document;
  const ConfitV2TomlValue *root;
  const ConfitV2TomlValue *table;
  const ConfitV2TomlValue *value;
  int64_t schema_version;
  ConfitStatus status;
  size_t index;

  document = 0;
  status = confit_v2_toml_parse_file(project_path, &document, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  root = confit_v2_toml_document_root(document);
  if (confit_v2_toml_table_size(root) != 1U ||
      (table = confit_v2_toml_table_find(root, "project")) == 0) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, root, kV2MissingProject);
    confit_v2_toml_document_free(document);
    return CONFIT_ERR_SCHEMA;
  }
  status = confit_v2_check_table_keys(table, kFields,
                                      sizeof(kFields) / sizeof(kFields[0]),
                                      kV2UnknownProjectField, diagnostic);
  if (status != CONFIT_OK) {
    confit_v2_toml_document_free(document);
    return status;
  }
  value = confit_v2_toml_table_find(table, "name");
  if (value == 0 ||
      (status = confit_v2_copy_string(project, value, &project->name,
                                       diagnostic)) != CONFIT_OK ||
      project->name[0] == '\0') {
    if (status == CONFIT_OK) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, table, kV2InvalidValue);
      status = CONFIT_ERR_SCHEMA;
    }
    confit_v2_toml_document_free(document);
    return status;
  }
  value = confit_v2_toml_table_find(table, "namespace");
  if (value == 0 ||
      (status = confit_v2_copy_string(project, value, &project->namespace_name,
                                       diagnostic)) != CONFIT_OK ||
      !confit_v2_is_identifier_segment(project->namespace_name,
                                       strlen(project->namespace_name))) {
    if (status == CONFIT_OK) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, table, kV2InvalidValue);
      status = CONFIT_ERR_SCHEMA;
    }
    confit_v2_toml_document_free(document);
    return status;
  }
  value = confit_v2_toml_table_find(table, "schema_version");
  if (value == 0 || !confit_v2_toml_value_int64(value, &schema_version) ||
      schema_version != 2) {
    confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA,
                    value != 0 ? value : table, kV2UnsupportedProjectVersion);
    confit_v2_toml_document_free(document);
    return CONFIT_ERR_SCHEMA;
  }
  value = confit_v2_toml_table_find(table, "version");
  if (value != 0 &&
      (status = confit_v2_copy_string(project, value, &project->version,
                                      diagnostic)) != CONFIT_OK) {
    confit_v2_toml_document_free(document);
    return status;
  }
  value = confit_v2_toml_table_find(table, "default_target");
  if (value != 0 &&
      (status = confit_v2_copy_string(project, value, &project->default_target,
                                      diagnostic)) != CONFIT_OK) {
    confit_v2_toml_document_free(document);
    return status;
  }
  if (value != 0 &&
      (status = confit_v2_copy_span(project, value, 0U,
                                    &project->default_target_span,
                                    diagnostic)) != CONFIT_OK) {
    confit_v2_toml_document_free(document);
    return status;
  }
  status = confit_v2_copy_span(project, table, 0U, &project->span, diagnostic);
  if (status != CONFIT_OK) {
    confit_v2_toml_document_free(document);
    return status;
  }
  value = confit_v2_toml_table_find(table, "profile_dirs");
  if (value != 0 &&
      (status = confit_v2_parse_string_list(project, value, 1, 1,
                                             &project->profile_dirs,
                                             diagnostic)) != CONFIT_OK) {
    confit_v2_toml_document_free(document);
    return status;
  }
  value = confit_v2_toml_table_find(table, "target_dirs");
  if (value != 0 &&
      (status = confit_v2_parse_string_list(project, value, 1, 1,
                                             &project->target_dirs,
                                             diagnostic)) != CONFIT_OK) {
    confit_v2_toml_document_free(document);
    return status;
  }
  value = confit_v2_toml_table_find(table, "selection_dirs");
  if (value != 0 &&
      (status = confit_v2_parse_string_list(project, value, 1, 1,
                                             &project->selection_dirs,
                                             diagnostic)) != CONFIT_OK) {
    confit_v2_toml_document_free(document);
    return status;
  }
  value = confit_v2_toml_table_find(table, "imports");
  if (value != 0) {
    if (confit_v2_toml_value_type(value) != CONFIT_V2_TOML_VALUE_ARRAY) {
      confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, value, kV2WrongValueType);
      confit_v2_toml_document_free(document);
      return CONFIT_ERR_SCHEMA;
    }
    for (index = 0U; index < confit_v2_toml_array_size(value); ++index) {
      const ConfitV2TomlValue *item = confit_v2_toml_array_at(value, index);
      char *import_path;
      size_t import_index;
      status = confit_v2_copy_string(project, item, &import_path, diagnostic);
      if (status != CONFIT_OK) {
        confit_v2_toml_document_free(document);
        return status;
      }
      if (!confit_v2_is_logical_path(import_path)) {
        confit_v2_deallocate(&project->allocator, import_path);
        confit_v2_error(diagnostic, CONFIT_ERR_SCHEMA, item, kV2InvalidImportPath);
        confit_v2_toml_document_free(document);
        return CONFIT_ERR_SCHEMA;
      }
      status = confit_v2_append_import(project, config_root, import_path, item,
                                        &import_index, diagnostic);
      if (status != CONFIT_OK) {
        confit_v2_deallocate(&project->allocator, import_path);
        confit_v2_toml_document_free(document);
        return status;
      }
    }
  }
  confit_v2_toml_document_free(document);
  return CONFIT_OK;
}

ConfitStatus confit_v2_schema_load_project_with_allocator(
    const char *project_root, const ConfitV2Allocator *allocator,
    ConfitV2Project **out_project, ConfitDiagnostic *diagnostic) {
  char config_root[1024];
  char project_path[1024];
  char canonical_config_root[4096];
  ConfitV2Project *project;
  ConfitStatus status;
  size_t index;

  if (out_project == 0 || !confit_v2_allocator_is_valid(allocator)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, project_root,
                          0, 0, "invalid schema v2 loader argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_project = 0;
  status = confit_v2_find_config_root(project_root, config_root,
                                      sizeof(config_root), project_path,
                                      sizeof(project_path), diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_path_canonicalize(canonical_config_root,
                                          sizeof(canonical_config_root),
                                          config_root, diagnostic);
  if (status != CONFIT_OK ||
      strlen(canonical_config_root) + 1U > sizeof(config_root)) {
    if (status == CONFIT_OK) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, project_root,
                            0, 0, "canonical config root is too long");
      status = CONFIT_ERR_INVALID_ARGUMENT;
    }
    return status;
  }
  memcpy(config_root, canonical_config_root, strlen(canonical_config_root) + 1U);
  status = confit_host_path_join(project_path, sizeof(project_path), config_root,
                                 "project.toml", diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  project = (ConfitV2Project *)confit_v2_allocate(allocator, sizeof(*project));
  if (project == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, project_path, 0, 0,
                          kV2AllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  memset(project, 0, sizeof(*project));
  project->allocator = *allocator;
  project->config_root = confit_v2_strdup(allocator, config_root);
  if (project->config_root == 0) {
    confit_v2_project_free(project);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, project_path, 0, 0,
                          kV2AllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  status = confit_v2_parse_project_document(project, config_root, project_path,
                                             diagnostic);
  if (status != CONFIT_OK) {
    confit_v2_project_free(project);
    return status;
  }
  for (index = 0U; index < project->import_count; ++index) {
    status = confit_v2_visit_import(project, config_root, index, diagnostic);
    if (status != CONFIT_OK) {
      confit_v2_project_free(project);
      return status;
    }
  }
  *out_project = project;
  return CONFIT_OK;
}

ConfitStatus confit_v2_schema_load_project(const char *project_root,
                                            ConfitV2Project **out_project,
                                            ConfitDiagnostic *diagnostic) {
  return confit_v2_schema_load_project_with_allocator(
      project_root, confit_v2_default_allocator(), out_project, diagnostic);
}
