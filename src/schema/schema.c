#include "confit/schema.h"

#include <stdint.h>
#include <string.h>

#include "confit/limits.h"

struct ConfitSchemaProject {
  ConfitAllocator allocator;
  ConfitSourceGraph *source_graph;
  ConfitCatalog *catalog;
};

typedef struct ConfitUserValueRecord {
  char *symbol;
  const ConfitTomlValue *value_candidate;
  const char *declaration_path;
  size_t declaration_line;
  size_t declaration_column;
} ConfitUserValueRecord;

struct ConfitUserDocument {
  ConfitAllocator allocator;
  ConfitInputImage *input;
  ConfitUserValueRecord *values;
  size_t value_count;
  size_t value_capacity;
};

static const char kInvalidArgument[] = "invalid schema loader argument";
static const char kInvalidAllocator[] = "allocator capability is incomplete";
static const char kOutOfMemory[] = "failed to allocate schema model";
static const char kUnknownEntryField[] = "entry document contains an unknown field";
static const char kUnknownFragmentField[] = "fragment contains an unknown field";
static const char kUnknownMenuField[] = "menu contains an unknown field";
static const char kUnknownConfigField[] = "config declaration contains an unknown field";
static const char kUnknownUserField[] = "user document contains an unknown field";
static const char kEntryVersion[] = "entry schema_version must be integer 6";
static const char kUserVersion[] = "user schema_version must be integer 6";
static const char kMissingMainmenu[] = "entry document requires mainmenu";
static const char kInvalidMainmenu[] = "entry mainmenu must be a bounded one-line string";
static const char kInvalidSource[] = "source must be an array of strings";
static const char kEmptyFragment[] = "fragment must contain a menu or config declaration";
static const char kInvalidMenu[] = "menu must be a table";
static const char kMissingMenuPrompt[] = "menu requires prompt";
static const char kMissingMenuHelp[] = "menu requires help";
static const char kInvalidMenuPrompt[] = "menu prompt must be a bounded one-line string";
static const char kInvalidMenuHelp[] = "menu help must be a bounded non-empty string";
static const char kInvalidConfigArray[] = "config must be an array of tables";
static const char kMissingConfigSymbol[] = "config declaration requires symbol";
static const char kMissingConfigType[] = "config declaration requires type";
static const char kMissingConfigPrompt[] = "config declaration requires prompt";
static const char kMissingConfigHelp[] = "config declaration requires help";
static const char kInvalidSymbol[] = "configuration symbol is invalid";
static const char kInvalidPrompt[] = "config prompt must be a bounded one-line string";
static const char kInvalidHelp[] = "config help must be a bounded non-empty string";
static const char kInvalidDefaultShape[] = "config default must be a TOML scalar";
static const char kInvalidValuesShape[] = "config values must be an array";
static const char kInvalidRangeShape[] = "config range must be a table";
static const char kInvalidDependencyShape[] = "config depends_on must be a bounded string";
static const char kDuplicateSymbol[] = "configuration symbol is duplicated";
static const char kInvalidType[] =
    "configuration type must be bool, int, hex, string, or enum";
static const char kInvalidBoolDefault[] =
    "bool default must be a native TOML boolean";
static const char kInvalidIntDefault[] =
    "int default must be a native TOML integer";
static const char kInvalidHexDefault[] =
    "hex default must use native nonnegative TOML hexadecimal spelling";
static const char kInvalidStringDefault[] =
    "string default must be a bounded safe TOML string";
static const char kMissingEnumDefault[] = "enum requires an explicit default";
static const char kInvalidEnumDefault[] =
    "enum default must be a member of its string atom domain";
static const char kMissingEnumValues[] = "enum requires a values array";
static const char kInvalidEnumValues[] =
    "enum values must be a nonempty bounded array of unique string atoms";
static const char kForbiddenValues[] = "values is valid only for enum";
static const char kForbiddenRange[] = "range is valid only for int and hex";
static const char kInvalidRange[] =
    "range must contain exactly min and max";
static const char kInvalidRangeMinimum[] =
    "range min must use the declared native TOML type";
static const char kInvalidRangeMaximum[] =
    "range max must use the declared native TOML type";
static const char kReversedRange[] = "range min must not exceed max";
static const char kDefaultOutsideRange[] =
    "default must lie within the inclusive range";
static const char kConfigLimit[] = "configuration symbol limit is exceeded";
static const char kInvalidUserValues[] = "user values must be a table";
static const char kInvalidUserSymbol[] = "user value symbol is invalid";
static const char kInvalidUserValue[] = "user value must be a TOML scalar";
static const char kInvalidUserPath[] = "user document path must be relative TOML";

static ConfitStatus confit_schema_fail(ConfitDiagnostic *diagnostic,
                                       ConfitStatus status, const char *path,
                                       size_t line, size_t column,
                                       const char *message) {
  confit_diagnostic_set(diagnostic, status, path, line, column, message);
  return status;
}

static ConfitStatus confit_schema_fail_value(
    ConfitDiagnostic *diagnostic, ConfitStatus status,
    const ConfitTomlValue *value, const char *fallback_path,
    const char *message) {
  const char *path = value != 0 ? confit_toml_value_source(value) : 0;
  return confit_schema_fail(
      diagnostic, status, path != 0 ? path : fallback_path,
      value != 0 ? confit_toml_value_line(value) : 0U,
      value != 0 ? confit_toml_value_column(value) : 0U, message);
}

static int confit_schema_resolve_allocator(const ConfitAllocator *requested,
                                           ConfitAllocator *resolved) {
  if (resolved == 0) return 0;
  if (requested == 0) {
    confit_allocator_default(resolved);
    return 1;
  }
  if (!confit_allocator_is_valid(requested)) return 0;
  *resolved = *requested;
  return 1;
}

static int confit_schema_utf8_continuation(unsigned char byte) {
  return (byte & 0xC0U) == 0x80U;
}

static int confit_schema_text_valid(const char *text, size_t size,
                                    size_t limit, int allow_empty,
                                    int allow_layout) {
  const unsigned char *bytes = (const unsigned char *)text;
  size_t index = 0U;
  if (text == 0 || size > limit || (!allow_empty && size == 0U)) return 0;
  while (index < size) {
    const unsigned char first = bytes[index];
    size_t width;
    if (first == 0U || first == 0x1BU || first == 0x7FU ||
        (first < 0x20U &&
         !(allow_layout &&
           (first == (unsigned char)'\t' || first == (unsigned char)'\n' ||
            first == (unsigned char)'\r')))) {
      return 0;
    }
    if (first < 0x80U) {
      width = 1U;
    } else if (first >= 0xC2U && first <= 0xDFU) {
      width = 2U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
      width = 3U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
      width = 4U;
    } else {
      return 0;
    }
    if (index + width > size ||
        (width >= 2U && !confit_schema_utf8_continuation(bytes[index + 1U])) ||
        (width >= 3U && !confit_schema_utf8_continuation(bytes[index + 2U])) ||
        (width >= 4U && !confit_schema_utf8_continuation(bytes[index + 3U])) ||
        (width == 3U && first == 0xE0U && bytes[index + 1U] < 0xA0U) ||
        (width == 3U && first == 0xEDU && bytes[index + 1U] > 0x9FU) ||
        (width == 4U && first == 0xF0U && bytes[index + 1U] < 0x90U) ||
        (width == 4U && first == 0xF4U && bytes[index + 1U] > 0x8FU) ||
        (width == 2U && first == 0xC2U && bytes[index + 1U] >= 0x80U &&
         bytes[index + 1U] <= 0x9FU) ||
        (!allow_layout && width == 3U && first == 0xE2U &&
         bytes[index + 1U] == 0x80U &&
         (bytes[index + 2U] == 0xA8U || bytes[index + 2U] == 0xA9U))) {
      return 0;
    }
    index += width;
  }
  return 1;
}

static ConfitStatus confit_schema_copy_bytes(
    const char *text, size_t size, size_t limit, int allow_empty,
    int allow_layout, const ConfitAllocator *allocator, char **out,
    ConfitDiagnostic *diagnostic, const ConfitTomlValue *value,
    const char *fallback_path, const char *message) {
  char *copy;
  if (out == 0 || !confit_schema_text_valid(text, size, limit, allow_empty,
                                             allow_layout)) {
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, value,
                                    fallback_path, message);
  }
  copy = (char *)allocator->allocate(allocator->context, size + 1U);
  if (copy == 0) {
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_INTERNAL, value,
                                    fallback_path, kOutOfMemory);
  }
  if (size != 0U) memcpy(copy, text, size);
  copy[size] = '\0';
  *out = copy;
  return CONFIT_OK;
}

static ConfitStatus confit_schema_copy_string(
    const ConfitTomlValue *value, size_t limit, int allow_empty,
    int allow_layout, const ConfitAllocator *allocator, char **out,
    ConfitDiagnostic *diagnostic, const char *fallback_path,
    const char *message) {
  const char *text = 0;
  size_t size = 0U;
  if (!confit_toml_value_string(value, &text, &size)) {
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, value,
                                    fallback_path, message);
  }
  return confit_schema_copy_bytes(text, size, limit, allow_empty, allow_layout,
                                  allocator, out, diagnostic, value,
                                  fallback_path, message);
}

static int confit_schema_key_equals(const ConfitTomlValue *table,
                                    size_t index, const char *expected) {
  const char *key = confit_toml_table_key_at(table, index);
  const size_t size = confit_toml_table_key_size_at(table, index);
  const size_t expected_size = strlen(expected);
  return key != 0 && size == expected_size &&
         memcmp(key, expected, expected_size) == 0;
}

static int confit_schema_key_allowed(const ConfitTomlValue *table,
                                     size_t index,
                                     const char *const *allowed,
                                     size_t allowed_count) {
  size_t candidate;
  for (candidate = 0U; candidate < allowed_count; ++candidate) {
    if (confit_schema_key_equals(table, index, allowed[candidate])) return 1;
  }
  return 0;
}

static ConfitStatus confit_schema_validate_keys(
    const ConfitTomlValue *table, const char *const *allowed,
    size_t allowed_count, const char *path, const char *message,
    ConfitDiagnostic *diagnostic) {
  size_t index;
  if (confit_toml_value_type(table) != CONFIT_TOML_VALUE_TABLE) {
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, table,
                                    path, message);
  }
  for (index = 0U; index < confit_toml_table_size(table); ++index) {
    if (!confit_schema_key_allowed(table, index, allowed, allowed_count)) {
      return confit_schema_fail_value(
          diagnostic, CONFIT_ERR_VALIDATION,
          confit_toml_table_value_at(table, index), path, message);
    }
  }
  return CONFIT_OK;
}

static int confit_schema_scalar(const ConfitTomlValue *value) {
  const ConfitTomlValueType type = confit_toml_value_type(value);
  return type != CONFIT_TOML_VALUE_UNKNOWN &&
         type != CONFIT_TOML_VALUE_ARRAY &&
         type != CONFIT_TOML_VALUE_TABLE;
}

static ConfitStatus confit_schema_validate_source(
    const ConfitTomlValue *source, const char *path,
    ConfitDiagnostic *diagnostic) {
  size_t index;
  if (source == 0 ||
      confit_toml_value_type(source) != CONFIT_TOML_VALUE_ARRAY) {
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, source,
                                    path, kInvalidSource);
  }
  for (index = 0U; index < confit_toml_array_size(source); ++index) {
    if (confit_toml_value_type(confit_toml_array_at(source, index)) !=
        CONFIT_TOML_VALUE_STRING) {
      return confit_schema_fail_value(
          diagnostic, CONFIT_ERR_VALIDATION,
          confit_toml_array_at(source, index), path, kInvalidSource);
    }
  }
  return CONFIT_OK;
}

static void confit_schema_anchor_model_error(ConfitDiagnostic *diagnostic,
                                             const ConfitTomlValue *value,
                                             const char *path) {
  if (diagnostic == 0) return;
  diagnostic->path = value != 0 && confit_toml_value_source(value) != 0
                         ? confit_toml_value_source(value)
                         : path;
  diagnostic->line = value != 0 ? confit_toml_value_line(value) : 0U;
  diagnostic->column = value != 0 ? confit_toml_value_column(value) : 0U;
}

static ConfitStatus confit_schema_parse_entry(ConfitSchemaProject *project,
                                              const ConfitSourceNodeView *node,
                                              ConfitDiagnostic *diagnostic) {
  static const char *const allowed[] = {"schema_version", "mainmenu", "source"};
  const ConfitTomlValue *root =
      confit_toml_document_root(confit_input_image_document(node->input));
  const ConfitTomlValue *version;
  const ConfitTomlValue *mainmenu;
  const ConfitTomlValue *source;
  int64_t version_number = 0;
  char *title = 0;
  ConfitStatus status;
  status = confit_schema_validate_keys(root, allowed, 3U, node->path,
                                       kUnknownEntryField, diagnostic);
  if (status != CONFIT_OK) return status;
  version = confit_toml_table_find(root, "schema_version");
  if (!confit_toml_value_int64(version, &version_number) ||
      version_number != 6) {
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, version,
                                    node->path, kEntryVersion);
  }
  mainmenu = confit_toml_table_find(root, "mainmenu");
  if (mainmenu == 0) {
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, root,
                                    node->path, kMissingMainmenu);
  }
  status = confit_schema_copy_string(
      mainmenu, CONFIT_LIMIT_PROMPT_BYTES, 0, 0, &project->allocator, &title,
      diagnostic, node->path, kInvalidMainmenu);
  if (status != CONFIT_OK) return status;
  status = confit_catalog_set_mainmenu(project->catalog, title, diagnostic);
  project->allocator.deallocate(project->allocator.context, title);
  if (status != CONFIT_OK) {
    confit_schema_anchor_model_error(diagnostic, mainmenu, node->path);
    return status;
  }
  source = confit_toml_table_find(root, "source");
  return confit_schema_validate_source(source, node->path, diagnostic);
}

static ConfitStatus confit_schema_add_menu(
    ConfitSchemaProject *project, size_t fragment, size_t parent_menu,
    const ConfitTomlValue *menu, const char *path, size_t *out_menu,
    ConfitDiagnostic *diagnostic) {
  static const char *const allowed[] = {"prompt", "help", "source"};
  const ConfitTomlValue *prompt;
  const ConfitTomlValue *help;
  const ConfitTomlValue *source;
  ConfitMenuSpec spec;
  char *prompt_text = 0;
  char *help_text = 0;
  ConfitStatus status;
  status = confit_schema_validate_keys(menu, allowed, 3U, path,
                                       kUnknownMenuField, diagnostic);
  if (status != CONFIT_OK) return status;
  prompt = confit_toml_table_find(menu, "prompt");
  if (prompt == 0)
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, menu,
                                    path, kMissingMenuPrompt);
  help = confit_toml_table_find(menu, "help");
  if (help == 0)
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, menu,
                                    path, kMissingMenuHelp);
  status = confit_schema_copy_string(
      prompt, CONFIT_LIMIT_PROMPT_BYTES, 0, 0, &project->allocator,
      &prompt_text, diagnostic, path, kInvalidMenuPrompt);
  if (status == CONFIT_OK)
    status = confit_schema_copy_string(
        help, CONFIT_LIMIT_HELP_BYTES, 0, 1, &project->allocator, &help_text,
        diagnostic, path, kInvalidMenuHelp);
  if (status == CONFIT_OK) {
    source = confit_toml_table_find(menu, "source");
    if (source != 0)
      status = confit_schema_validate_source(source, path, diagnostic);
  }
  if (status == CONFIT_OK) {
    memset(&spec, 0, sizeof(spec));
    spec.fragment = fragment;
    spec.parent_menu = parent_menu;
    spec.prompt = prompt_text;
    spec.help = help_text;
    spec.declaration.path = path;
    spec.declaration.line = confit_toml_value_line(menu);
    spec.declaration.column = confit_toml_value_column(menu);
    status = confit_catalog_add_menu(project->catalog, &spec, out_menu,
                                     diagnostic);
    if (status != CONFIT_OK)
      confit_schema_anchor_model_error(diagnostic, menu, path);
  }
  if (help_text != 0)
    project->allocator.deallocate(project->allocator.context, help_text);
  if (prompt_text != 0)
    project->allocator.deallocate(project->allocator.context, prompt_text);
  return status;
}

static int confit_schema_type_kind(const ConfitTomlValue *type_name,
                                   ConfitValueKind *out_kind) {
  const char *text = 0;
  size_t size = 0U;
  if (out_kind == 0 ||
      !confit_toml_value_string(type_name, &text, &size)) {
    return 0;
  }
  if (size == 4U && memcmp(text, "bool", 4U) == 0) {
    *out_kind = CONFIT_VALUE_BOOL;
  } else if (size == 3U && memcmp(text, "int", 3U) == 0) {
    *out_kind = CONFIT_VALUE_INT;
  } else if (size == 3U && memcmp(text, "hex", 3U) == 0) {
    *out_kind = CONFIT_VALUE_HEX;
  } else if (size == 6U && memcmp(text, "string", 6U) == 0) {
    *out_kind = CONFIT_VALUE_STRING;
  } else if (size == 4U && memcmp(text, "enum", 4U) == 0) {
    *out_kind = CONFIT_VALUE_ENUM;
  } else {
    return 0;
  }
  return 1;
}

static ConfitStatus confit_schema_typed_value(
    const ConfitTomlDocument *document, const ConfitTomlValue *value,
    ConfitValueKind kind, const ConfitAllocator *allocator, ConfitValue *out,
    ConfitDiagnostic *diagnostic, const char *path,
    const char *invalid_message) {
  const char *text = 0;
  size_t text_size = 0U;
  int boolean = 0;
  int64_t integer = 0;
  ConfitTomlIntegerBase base = CONFIT_TOML_INTEGER_BASE_UNKNOWN;
  ConfitStatus status;

  switch (kind) {
  case CONFIT_VALUE_BOOL:
    if (!confit_toml_value_bool(value, &boolean))
      return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, value,
                                      path, invalid_message != 0
                                                ? invalid_message
                                                : kInvalidBoolDefault);
    return confit_value_set_bool(out, boolean, allocator, diagnostic);
  case CONFIT_VALUE_INT:
    if (!confit_toml_value_int64(value, &integer))
      return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, value,
                                      path, invalid_message != 0
                                                ? invalid_message
                                                : kInvalidIntDefault);
    return confit_value_set_int(out, integer, allocator, diagnostic);
  case CONFIT_VALUE_HEX:
    if (!confit_toml_value_int64(value, &integer) || integer < 0 ||
        !confit_toml_value_integer_base(document, value, &base) ||
        base != CONFIT_TOML_INTEGER_BASE_HEXADECIMAL)
      return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, value,
                                      path, invalid_message != 0
                                                ? invalid_message
                                                : kInvalidHexDefault);
    return confit_value_set_hex(out, (uint64_t)integer, allocator, diagnostic);
  case CONFIT_VALUE_STRING:
    if (!confit_toml_value_string(value, &text, &text_size) ||
        !confit_schema_text_valid(text, text_size, CONFIT_LIMIT_STRING_BYTES, 1,
                                  1))
      return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, value,
                                      path, invalid_message != 0
                                                ? invalid_message
                                                : kInvalidStringDefault);
    status = confit_value_set_string(out, text, text_size, allocator,
                                     diagnostic);
    break;
  case CONFIT_VALUE_ENUM:
    if (!confit_toml_value_string(value, &text, &text_size))
      return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, value,
                                      path, invalid_message != 0
                                                ? invalid_message
                                                : kInvalidEnumDefault);
    status = confit_value_set_enum(out, text, text_size, allocator, diagnostic);
    if (status == CONFIT_ERR_VALIDATION)
      return confit_schema_fail_value(diagnostic, status, value, path,
                                      invalid_message != 0
                                          ? invalid_message
                                          : kInvalidEnumDefault);
    break;
  case CONFIT_VALUE_INVALID:
  default:
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_INTERNAL, value,
                                    path, kInvalidType);
  }
  if (status != CONFIT_OK)
    confit_schema_anchor_model_error(diagnostic, value, path);
  return status;
}

static ConfitStatus confit_schema_default_value(
    const ConfitTomlDocument *document, const ConfitTomlValue *candidate,
    ConfitValueKind kind, const ConfitAllocator *allocator, ConfitValue *out,
    ConfitDiagnostic *diagnostic, const char *path) {
  if (candidate != 0)
    return confit_schema_typed_value(document, candidate, kind, allocator, out,
                                     diagnostic, path, 0);
  switch (kind) {
  case CONFIT_VALUE_BOOL:
    return confit_value_set_bool(out, 0, allocator, diagnostic);
  case CONFIT_VALUE_INT:
    return confit_value_set_int(out, INT64_C(0), allocator, diagnostic);
  case CONFIT_VALUE_HEX:
    return confit_value_set_hex(out, UINT64_C(0), allocator, diagnostic);
  case CONFIT_VALUE_STRING:
    return confit_value_set_string(out, "", 0U, allocator, diagnostic);
  case CONFIT_VALUE_ENUM:
    return confit_schema_fail(diagnostic, CONFIT_ERR_VALIDATION, path, 0U, 0U,
                              kMissingEnumDefault);
  case CONFIT_VALUE_INVALID:
  default:
    return confit_schema_fail(diagnostic, CONFIT_ERR_INTERNAL, path, 0U, 0U,
                              kInvalidType);
  }
}

static void confit_schema_enum_values_destroy(
    char **values, size_t count, const ConfitAllocator *allocator) {
  size_t index;
  if (values == 0) return;
  for (index = 0U; index < count; ++index) {
    if (values[index] != 0)
      allocator->deallocate(allocator->context, values[index]);
  }
  allocator->deallocate(allocator->context, values);
}

static ConfitStatus confit_schema_enum_values(
    const ConfitTomlValue *candidate, const ConfitAllocator *allocator,
    char ***out_values, size_t *out_count, ConfitDiagnostic *diagnostic,
    const char *path) {
  char **values;
  size_t count;
  size_t index;
  ConfitStatus status = CONFIT_OK;
  if (candidate == 0)
    return confit_schema_fail(diagnostic, CONFIT_ERR_VALIDATION, path, 0U, 0U,
                              kMissingEnumValues);
  count = confit_toml_array_size(candidate);
  if (confit_toml_value_type(candidate) != CONFIT_TOML_VALUE_ARRAY ||
      count == 0U || count > CONFIT_LIMIT_ENUM_VALUES)
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                    candidate, path, kInvalidEnumValues);
  if (count > SIZE_MAX / sizeof(*values))
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_INTERNAL, candidate,
                                    path, kOutOfMemory);
  values = (char **)allocator->allocate(allocator->context,
                                        count * sizeof(*values));
  if (values == 0)
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_INTERNAL, candidate,
                                    path, kOutOfMemory);
  memset(values, 0, count * sizeof(*values));
  for (index = 0U; status == CONFIT_OK && index < count; ++index) {
    const ConfitTomlValue *atom = confit_toml_array_at(candidate, index);
    status = confit_schema_copy_string(
        atom, CONFIT_LIMIT_ENUM_ATOM_BYTES, 0, 0, allocator, &values[index],
        diagnostic, path, kInvalidEnumValues);
  }
  if (status == CONFIT_OK)
    status = confit_enum_domain_validate((const char *const *)values, count,
                                         diagnostic);
  if (status != CONFIT_OK) {
    if (status == CONFIT_ERR_VALIDATION)
      (void)confit_schema_fail_value(diagnostic, status, candidate, path,
                                     kInvalidEnumValues);
    confit_schema_enum_values_destroy(values, count, allocator);
    return status;
  }
  *out_values = values;
  *out_count = count;
  return CONFIT_OK;
}

static int confit_schema_enum_contains(char *const *values, size_t count,
                                       const ConfitValue *value) {
  size_t index;
  if (value == 0 || value->kind != CONFIT_VALUE_ENUM) return 0;
  for (index = 0U; index < count; ++index) {
    const size_t size = strlen(values[index]);
    if (size == value->data.text.size &&
        memcmp(values[index], value->data.text.data, size) == 0)
      return 1;
  }
  return 0;
}

static int confit_schema_range_contains(const ConfitValue *minimum,
                                        const ConfitValue *value,
                                        const ConfitValue *maximum) {
  if (minimum->kind == CONFIT_VALUE_INT && value->kind == CONFIT_VALUE_INT &&
      maximum->kind == CONFIT_VALUE_INT)
    return minimum->data.integer <= value->data.integer &&
           value->data.integer <= maximum->data.integer;
  if (minimum->kind == CONFIT_VALUE_HEX && value->kind == CONFIT_VALUE_HEX &&
      maximum->kind == CONFIT_VALUE_HEX)
    return minimum->data.hexadecimal <= value->data.hexadecimal &&
           value->data.hexadecimal <= maximum->data.hexadecimal;
  return 0;
}

static int confit_schema_range_ordered(const ConfitValue *minimum,
                                       const ConfitValue *maximum) {
  if (minimum->kind == CONFIT_VALUE_INT && maximum->kind == CONFIT_VALUE_INT)
    return minimum->data.integer <= maximum->data.integer;
  if (minimum->kind == CONFIT_VALUE_HEX && maximum->kind == CONFIT_VALUE_HEX)
    return minimum->data.hexadecimal <= maximum->data.hexadecimal;
  return 0;
}

static ConfitStatus confit_schema_range(
    const ConfitTomlDocument *document, const ConfitTomlValue *candidate,
    ConfitValueKind kind, const ConfitValue *default_value,
    const ConfitAllocator *allocator, ConfitValue *minimum,
    ConfitValue *maximum, int *out_present, ConfitDiagnostic *diagnostic,
    const char *path) {
  static const char *const allowed[] = {"min", "max"};
  const ConfitTomlValue *min_value;
  const ConfitTomlValue *max_value;
  ConfitStatus status;
  if (candidate == 0) return CONFIT_OK;
  if (kind != CONFIT_VALUE_INT && kind != CONFIT_VALUE_HEX)
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                    candidate, path, kForbiddenRange);
  status = confit_schema_validate_keys(candidate, allowed, 2U, path,
                                       kInvalidRange, diagnostic);
  min_value = status == CONFIT_OK ? confit_toml_table_find(candidate, "min") : 0;
  max_value = status == CONFIT_OK ? confit_toml_table_find(candidate, "max") : 0;
  if (status == CONFIT_OK && (min_value == 0 || max_value == 0))
    status = confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                      candidate, path, kInvalidRange);
  if (status == CONFIT_OK)
    status = confit_schema_typed_value(document, min_value, kind, allocator,
                                       minimum, diagnostic, path,
                                       kInvalidRangeMinimum);
  if (status == CONFIT_OK)
    status = confit_schema_typed_value(document, max_value, kind, allocator,
                                       maximum, diagnostic, path,
                                       kInvalidRangeMaximum);
  if (status == CONFIT_OK &&
      !confit_schema_range_ordered(minimum, maximum))
    status = confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                      candidate, path, kReversedRange);
  if (status == CONFIT_OK &&
      !confit_schema_range_contains(minimum, default_value, maximum))
    status = confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                      candidate, path, kDefaultOutsideRange);
  if (status == CONFIT_OK) *out_present = 1;
  return status;
}

static ConfitStatus confit_schema_add_config(
    ConfitSchemaProject *project, size_t fragment, size_t menu,
    const ConfitTomlDocument *document, const ConfitTomlValue *table,
    const char *path,
    ConfitDiagnostic *diagnostic) {
  static const char *const allowed[] = {
      "symbol", "type", "prompt", "help", "default", "depends_on",
      "values", "range"};
  ConfitConfigSpec spec;
  ConfitValue default_value;
  ConfitValue range_minimum;
  ConfitValue range_maximum;
  const ConfitTomlValue *symbol;
  const ConfitTomlValue *type_name;
  const ConfitTomlValue *prompt;
  const ConfitTomlValue *help;
  const ConfitTomlValue *dependency;
  const ConfitTomlValue *default_candidate;
  const ConfitTomlValue *values_candidate;
  const ConfitTomlValue *range_candidate;
  ConfitStatus status;
  const char *symbol_bytes = 0;
  size_t symbol_size = 0U;
  char *symbol_text = 0;
  char *prompt_text = 0;
  char *help_text = 0;
  char *dependency_text = 0;
  char **enum_values = 0;
  size_t enum_value_count = 0U;
  ConfitValueKind kind = CONFIT_VALUE_INVALID;
  int has_range = 0;
  memset(&spec, 0, sizeof(spec));
  confit_value_init(&default_value);
  confit_value_init(&range_minimum);
  confit_value_init(&range_maximum);
  status = confit_schema_validate_keys(table, allowed, 8U, path,
                                       kUnknownConfigField, diagnostic);
  if (status != CONFIT_OK) return status;
  symbol = confit_toml_table_find(table, "symbol");
  type_name = confit_toml_table_find(table, "type");
  prompt = confit_toml_table_find(table, "prompt");
  help = confit_toml_table_find(table, "help");
  if (symbol == 0)
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, table,
                                    path, kMissingConfigSymbol);
  if (type_name == 0)
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, table,
                                    path, kMissingConfigType);
  if (prompt == 0)
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, table,
                                    path, kMissingConfigPrompt);
  if (help == 0)
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, table,
                                    path, kMissingConfigHelp);
  if (!confit_toml_value_string(symbol, &symbol_bytes, &symbol_size)) {
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, symbol,
                                    path, kInvalidSymbol);
  }
  status = confit_schema_copy_bytes(
      symbol_bytes, symbol_size, 128U, 0, 0, &project->allocator,
      &symbol_text, diagnostic, symbol, path, kInvalidSymbol);
  if (status == CONFIT_OK && !confit_symbol_is_valid(symbol_text))
    status = confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, symbol,
                                      path, kInvalidSymbol);
  if (status == CONFIT_OK) {
    ConfitConfigView existing;
    if (confit_catalog_find_config(project->catalog, symbol_text, &existing))
      status = confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                        symbol, path, kDuplicateSymbol);
  }
  if (status == CONFIT_OK && !confit_schema_type_kind(type_name, &kind))
    status = confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                      type_name, path, kInvalidType);
  if (status == CONFIT_OK)
    status = confit_schema_copy_string(
        prompt, CONFIT_LIMIT_PROMPT_BYTES, 0, 0, &project->allocator,
        &prompt_text, diagnostic, path, kInvalidPrompt);
  if (status == CONFIT_OK)
    status = confit_schema_copy_string(
        help, CONFIT_LIMIT_HELP_BYTES, 0, 1, &project->allocator,
        &help_text, diagnostic, path, kInvalidHelp);
  default_candidate = confit_toml_table_find(table, "default");
  values_candidate = confit_toml_table_find(table, "values");
  range_candidate = confit_toml_table_find(table, "range");
  if (status == CONFIT_OK && default_candidate != 0 &&
      !confit_schema_scalar(default_candidate))
    status = confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                      default_candidate, path,
                                      kInvalidDefaultShape);
  if (status == CONFIT_OK && values_candidate != 0 &&
      confit_toml_value_type(values_candidate) !=
          CONFIT_TOML_VALUE_ARRAY)
    status = confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                      values_candidate, path,
                                      kInvalidValuesShape);
  if (status == CONFIT_OK && range_candidate != 0 &&
      confit_toml_value_type(range_candidate) !=
          CONFIT_TOML_VALUE_TABLE)
    status = confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                      range_candidate, path,
                                      kInvalidRangeShape);
  dependency = confit_toml_table_find(table, "depends_on");
  if (status == CONFIT_OK && dependency != 0)
    status = confit_schema_copy_string(
        dependency, CONFIT_LIMIT_DEPENDENCY_TEXT_BYTES, 1, 0,
        &project->allocator, &dependency_text, diagnostic, path,
        kInvalidDependencyShape);
  if (status == CONFIT_OK)
    status = confit_schema_default_value(
        document, default_candidate, kind, &project->allocator, &default_value,
        diagnostic, path);
  if (status == CONFIT_OK && kind == CONFIT_VALUE_ENUM)
    status = confit_schema_enum_values(values_candidate, &project->allocator,
                                       &enum_values, &enum_value_count,
                                       diagnostic, path);
  if (status == CONFIT_OK && kind == CONFIT_VALUE_ENUM &&
      !confit_schema_enum_contains(enum_values, enum_value_count,
                                   &default_value))
    status = confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                      default_candidate, path,
                                      kInvalidEnumDefault);
  if (status == CONFIT_OK && kind != CONFIT_VALUE_ENUM &&
      values_candidate != 0)
    status = confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                      values_candidate, path, kForbiddenValues);
  if (status == CONFIT_OK)
    status = confit_schema_range(
        document, range_candidate, kind, &default_value, &project->allocator,
        &range_minimum, &range_maximum, &has_range, diagnostic, path);
  if (status == CONFIT_OK) {
    spec.fragment = fragment;
    spec.menu = menu;
    spec.symbol = symbol_text;
    spec.kind = kind;
    spec.prompt = prompt_text;
    spec.help = help_text;
    spec.default_value = &default_value;
    spec.range.present = has_range;
    spec.range.minimum = has_range ? &range_minimum : 0;
    spec.range.maximum = has_range ? &range_maximum : 0;
    spec.enum_values = (const char *const *)enum_values;
    spec.enum_value_count = enum_value_count;
    spec.dependency_text = dependency_text;
    spec.declaration.path = path;
    spec.declaration.line = confit_toml_value_line(table);
    spec.declaration.column = confit_toml_value_column(table);
    status = confit_catalog_add_config(project->catalog, &spec, 0, diagnostic);
    if (status != CONFIT_OK)
      confit_schema_anchor_model_error(diagnostic, table, path);
  }
  confit_schema_enum_values_destroy(enum_values, enum_value_count,
                                    &project->allocator);
  confit_value_destroy(&range_maximum);
  confit_value_destroy(&range_minimum);
  confit_value_destroy(&default_value);
  if (dependency_text != 0)
    project->allocator.deallocate(project->allocator.context, dependency_text);
  if (help_text != 0)
    project->allocator.deallocate(project->allocator.context, help_text);
  if (prompt_text != 0)
    project->allocator.deallocate(project->allocator.context, prompt_text);
  if (symbol_text != 0)
    project->allocator.deallocate(project->allocator.context, symbol_text);
  return status;
}

static ConfitStatus confit_schema_parse_fragment(
    ConfitSchemaProject *project, size_t fragment,
    const ConfitSourceNodeView *node, size_t parent_menu,
    size_t *out_attachment, ConfitDiagnostic *diagnostic) {
  static const char *const allowed[] = {"menu", "config"};
  const ConfitTomlValue *root =
      confit_toml_document_root(confit_input_image_document(node->input));
  const ConfitTomlDocument *document =
      confit_input_image_document(node->input);
  const ConfitTomlValue *menu = confit_toml_table_find(root, "menu");
  const ConfitTomlValue *configs = confit_toml_table_find(root, "config");
  size_t attachment = parent_menu;
  size_t index;
  ConfitStatus status;
  status = confit_schema_validate_keys(root, allowed, 2U, node->path,
                                       kUnknownFragmentField, diagnostic);
  if (status != CONFIT_OK) return status;
  if (menu == 0 && configs == 0)
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, root,
                                    node->path, kEmptyFragment);
  if (menu != 0) {
    if (confit_toml_value_type(menu) != CONFIT_TOML_VALUE_TABLE)
      return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, menu,
                                      node->path, kInvalidMenu);
    status = confit_schema_add_menu(project, fragment, parent_menu, menu,
                                    node->path, &attachment, diagnostic);
    if (status != CONFIT_OK) return status;
  }
  if (configs != 0) {
    if (confit_toml_value_type(configs) != CONFIT_TOML_VALUE_ARRAY)
      return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                      configs, node->path,
                                      kInvalidConfigArray);
    if (menu == 0 && confit_toml_array_size(configs) == 0U)
      return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                      configs, node->path, kEmptyFragment);
    for (index = 0U; index < confit_toml_array_size(configs); ++index) {
      const ConfitTomlValue *config = confit_toml_array_at(configs, index);
      if (confit_toml_value_type(config) != CONFIT_TOML_VALUE_TABLE)
        return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                        config, node->path,
                                        kInvalidConfigArray);
      status = confit_schema_add_config(project, fragment, attachment,
                                        document, config, node->path,
                                        diagnostic);
      if (status != CONFIT_OK) return status;
    }
  }
  *out_attachment = attachment;
  return CONFIT_OK;
}

void confit_schema_project_destroy(ConfitSchemaProject *project) {
  ConfitAllocator allocator;
  if (project == 0) return;
  allocator = project->allocator;
  confit_catalog_destroy(project->catalog);
  confit_source_graph_destroy(project->source_graph);
  memset(project, 0, sizeof(*project));
  allocator.deallocate(allocator.context, project);
}

ConfitStatus confit_schema_project_load(
    ConfitHostRoot *project_root, const char *entry_path,
    const ConfitAllocator *allocator, ConfitSchemaProject **out_project,
    ConfitDiagnostic *diagnostic) {
  ConfitAllocator resolved;
  ConfitSchemaProject *project;
  ConfitSourceNodeView node;
  size_t *attachment = 0;
  size_t node_count;
  size_t index;
  ConfitStatus status;
  if (project_root == 0 || entry_path == 0 || out_project == 0) {
    return confit_schema_fail(diagnostic, CONFIT_ERR_USAGE, entry_path, 0U, 0U,
                              kInvalidArgument);
  }
  *out_project = 0;
  if (!confit_schema_resolve_allocator(allocator, &resolved))
    return confit_schema_fail(diagnostic, CONFIT_ERR_USAGE, entry_path, 0U, 0U,
                              kInvalidAllocator);
  project = (ConfitSchemaProject *)resolved.allocate(resolved.context,
                                                      sizeof(*project));
  if (project == 0)
    return confit_schema_fail(diagnostic, CONFIT_ERR_INTERNAL, entry_path, 0U,
                              0U, kOutOfMemory);
  memset(project, 0, sizeof(*project));
  project->allocator = resolved;
  status = confit_source_graph_load(project_root, entry_path, &resolved,
                                    &project->source_graph, diagnostic);
  if (status == CONFIT_OK)
    status = confit_catalog_create(&resolved, &project->catalog, diagnostic);
  node_count = status == CONFIT_OK
                   ? confit_source_graph_node_count(project->source_graph)
                   : 0U;
  if (status == CONFIT_OK) {
    if (node_count == 0U || node_count > SIZE_MAX / sizeof(*attachment)) {
      status = confit_schema_fail(diagnostic, CONFIT_ERR_INTERNAL, entry_path,
                                  0U, 0U, kOutOfMemory);
    } else {
      attachment = (size_t *)resolved.allocate(
          resolved.context, node_count * sizeof(*attachment));
      if (attachment == 0)
        status = confit_schema_fail(diagnostic, CONFIT_ERR_INTERNAL, entry_path,
                                    0U, 0U, kOutOfMemory);
    }
  }
  for (index = 0U; status == CONFIT_OK && index < node_count; ++index) {
    ConfitSourceFragmentSpec spec;
    if (!confit_source_graph_node_at(project->source_graph, index, &node)) {
      status = confit_schema_fail(diagnostic, CONFIT_ERR_INTERNAL, entry_path,
                                  0U, 0U, kInvalidArgument);
      break;
    }
    spec.path = node.path;
    spec.parent_fragment = node.parent_node;
    spec.source_ordinal = node.source_ordinal;
    status = confit_catalog_add_fragment(project->catalog, &spec, 0,
                                         diagnostic);
    if (status != CONFIT_OK)
      confit_schema_anchor_model_error(diagnostic, 0, node.path);
    attachment[index] = CONFIT_INDEX_NONE;
  }
  if (status == CONFIT_OK &&
      confit_source_graph_node_at(project->source_graph, 0U, &node))
    status = confit_schema_parse_entry(project, &node, diagnostic);
  for (index = 1U; status == CONFIT_OK && index < node_count; ++index) {
    size_t parent_menu;
    if (!confit_source_graph_node_at(project->source_graph, index, &node)) {
      status = confit_schema_fail(diagnostic, CONFIT_ERR_INTERNAL, entry_path,
                                  0U, 0U, kInvalidArgument);
      break;
    }
    parent_menu = node.parent_node != CONFIT_INDEX_NONE
                      ? attachment[node.parent_node]
                      : CONFIT_INDEX_NONE;
    status = confit_schema_parse_fragment(project, index, &node, parent_menu,
                                          &attachment[index], diagnostic);
  }
  if (attachment != 0) resolved.deallocate(resolved.context, attachment);
  if (status != CONFIT_OK) {
    (void)confit_diagnostic_stabilize_path(diagnostic);
    confit_schema_project_destroy(project);
    return status;
  }
  *out_project = project;
  return CONFIT_OK;
}

const ConfitSourceGraph *
confit_schema_project_source_graph(const ConfitSchemaProject *project) {
  return project != 0 ? project->source_graph : 0;
}

const ConfitCatalog *
confit_schema_project_catalog(const ConfitSchemaProject *project) {
  return project != 0 ? project->catalog : 0;
}

size_t confit_schema_project_config_count(const ConfitSchemaProject *project) {
  return project != 0 ? confit_catalog_config_count(project->catalog) : 0U;
}

int confit_schema_project_config_at(const ConfitSchemaProject *project,
                                    size_t index,
                                    ConfitSchemaConfigView *out_view) {
  return project != 0 &&
         confit_catalog_config_at(project->catalog, index, out_view);
}

int confit_schema_project_find_config(const ConfitSchemaProject *project,
                                      const char *symbol,
                                      ConfitSchemaConfigView *out_view) {
  return project != 0 &&
         confit_catalog_find_config(project->catalog, symbol, out_view);
}

static void confit_user_value_record_destroy(
    ConfitUserValueRecord *record, const ConfitAllocator *allocator) {
  if (record != 0 && record->symbol != 0)
    allocator->deallocate(allocator->context, record->symbol);
  if (record != 0) memset(record, 0, sizeof(*record));
}

static ConfitStatus confit_user_grow_values(
    ConfitUserDocument *document, const ConfitTomlValue *location,
    const char *path, ConfitDiagnostic *diagnostic) {
  ConfitUserValueRecord *replacement;
  size_t capacity;
  size_t bytes;
  if (document->value_count < document->value_capacity) return CONFIT_OK;
  if (document->value_count >= CONFIT_LIMIT_CONFIG_SYMBOLS)
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, location,
                                    path, kConfigLimit);
  capacity = document->value_capacity == 0U ? 8U
                                            : document->value_capacity * 2U;
  if (capacity > CONFIT_LIMIT_CONFIG_SYMBOLS)
    capacity = CONFIT_LIMIT_CONFIG_SYMBOLS;
  if (capacity < document->value_count ||
      capacity > SIZE_MAX / sizeof(*replacement))
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_INTERNAL, location,
                                    path, kOutOfMemory);
  bytes = capacity * sizeof(*replacement);
  replacement = (ConfitUserValueRecord *)document->allocator.allocate(
      document->allocator.context, bytes);
  if (replacement == 0)
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_INTERNAL, location,
                                    path, kOutOfMemory);
  memset(replacement, 0, bytes);
  if (document->value_count != 0U)
    memcpy(replacement, document->values,
           document->value_count * sizeof(*replacement));
  if (document->values != 0)
    document->allocator.deallocate(document->allocator.context,
                                   document->values);
  document->values = replacement;
  document->value_capacity = capacity;
  return CONFIT_OK;
}

void confit_user_document_destroy(ConfitUserDocument *document) {
  ConfitAllocator allocator;
  size_t index;
  if (document == 0) return;
  allocator = document->allocator;
  for (index = document->value_count; index > 0U; --index)
    confit_user_value_record_destroy(&document->values[index - 1U], &allocator);
  if (document->values != 0)
    allocator.deallocate(allocator.context, document->values);
  confit_input_image_destroy(document->input);
  memset(document, 0, sizeof(*document));
  allocator.deallocate(allocator.context, document);
}

static int confit_schema_toml_suffix(const char *path) {
  const size_t size = path != 0 ? strlen(path) : 0U;
  return size >= 5U && memcmp(path + size - 5U, ".toml", 5U) == 0;
}

ConfitStatus confit_user_document_load_relative(
    ConfitHostRoot *project_root, const char *path,
    const ConfitAllocator *allocator, ConfitUserDocument **out_document,
    ConfitDiagnostic *diagnostic) {
  static const char *const allowed[] = {"schema_version", "values"};
  ConfitAllocator resolved;
  ConfitUserDocument *document;
  const ConfitTomlValue *root;
  const ConfitTomlValue *version;
  const ConfitTomlValue *values;
  int64_t version_number = 0;
  size_t index;
  ConfitStatus status;
  if (project_root == 0 || path == 0 || out_document == 0) {
    return confit_schema_fail(diagnostic, CONFIT_ERR_USAGE, path, 0U, 0U,
                              kInvalidArgument);
  }
  *out_document = 0;
  if (!confit_host_relative_path_is_valid(path) ||
      !confit_schema_toml_suffix(path))
    return confit_schema_fail(diagnostic, CONFIT_ERR_VALIDATION, path, 0U, 0U,
                              kInvalidUserPath);
  if (!confit_schema_resolve_allocator(allocator, &resolved))
    return confit_schema_fail(diagnostic, CONFIT_ERR_USAGE, path, 0U, 0U,
                              kInvalidAllocator);
  document = (ConfitUserDocument *)resolved.allocate(resolved.context,
                                                      sizeof(*document));
  if (document == 0)
    return confit_schema_fail(diagnostic, CONFIT_ERR_INTERNAL, path, 0U, 0U,
                              kOutOfMemory);
  memset(document, 0, sizeof(*document));
  document->allocator = resolved;
  status = confit_input_load_toml(project_root, path, &resolved,
                                  &document->input, diagnostic);
  root = status == CONFIT_OK
             ? confit_toml_document_root(
                   confit_input_image_document(document->input))
             : 0;
  if (status == CONFIT_OK)
    status = confit_schema_validate_keys(root, allowed, 2U, path,
                                         kUnknownUserField, diagnostic);
  version = status == CONFIT_OK
                ? confit_toml_table_find(root, "schema_version")
                : 0;
  if (status == CONFIT_OK &&
      (!confit_toml_value_int64(version, &version_number) ||
       version_number != 6))
    status = confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                      version, path, kUserVersion);
  values = status == CONFIT_OK ? confit_toml_table_find(root, "values") : 0;
  if (status == CONFIT_OK && values != 0 &&
      confit_toml_value_type(values) != CONFIT_TOML_VALUE_TABLE)
    status = confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, values,
                                      path, kInvalidUserValues);
  for (index = 0U; status == CONFIT_OK && values != 0 &&
                  index < confit_toml_table_size(values);
       ++index) {
    ConfitUserValueRecord candidate;
    const char *key = confit_toml_table_key_at(values, index);
    const size_t key_size = confit_toml_table_key_size_at(values, index);
    const ConfitTomlValue *value = confit_toml_table_value_at(values, index);
    memset(&candidate, 0, sizeof(candidate));
    status = confit_schema_copy_bytes(
        key, key_size, 128U, 0, 0, &resolved, &candidate.symbol, diagnostic,
        value, path, kInvalidUserSymbol);
    if (status == CONFIT_OK && !confit_symbol_is_valid(candidate.symbol))
      status = confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                        value, path, kInvalidUserSymbol);
    if (status == CONFIT_OK && !confit_schema_scalar(value))
      status = confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                        value, path, kInvalidUserValue);
    candidate.value_candidate = value;
    candidate.declaration_path = confit_input_image_path(document->input);
    candidate.declaration_line = confit_toml_value_line(value);
    candidate.declaration_column = confit_toml_value_column(value);
    if (status == CONFIT_OK)
      status = confit_user_grow_values(document, value, path, diagnostic);
    if (status == CONFIT_OK) {
      document->values[document->value_count++] = candidate;
    } else {
      confit_user_value_record_destroy(&candidate, &resolved);
    }
  }
  if (status != CONFIT_OK) {
    (void)confit_diagnostic_stabilize_path(diagnostic);
    confit_user_document_destroy(document);
    return status;
  }
  *out_document = document;
  return CONFIT_OK;
}

const ConfitInputImage *
confit_user_document_input(const ConfitUserDocument *document) {
  return document != 0 ? document->input : 0;
}

size_t confit_user_document_value_count(const ConfitUserDocument *document) {
  return document != 0 ? document->value_count : 0U;
}

int confit_user_document_value_at(const ConfitUserDocument *document,
                                  size_t index,
                                  ConfitUserValueView *out_view) {
  const ConfitUserValueRecord *record;
  if (document == 0 || out_view == 0 || index >= document->value_count)
    return 0;
  record = &document->values[index];
  out_view->symbol = record->symbol;
  out_view->value_candidate = record->value_candidate;
  out_view->declaration.path = record->declaration_path;
  out_view->declaration.line = record->declaration_line;
  out_view->declaration.column = record->declaration_column;
  return 1;
}
