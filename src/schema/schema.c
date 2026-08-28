#include "confit/schema.h"

#include <stdint.h>
#include <string.h>

#include "confit/limits.h"

typedef struct ConfitSchemaConfigRecord {
  size_t fragment;
  size_t menu;
  char *symbol;
  char *type_name;
  char *prompt;
  char *help;
  char *dependency_text;
  const ConfitTomlValue *default_candidate;
  const ConfitTomlValue *values_candidate;
  const ConfitTomlValue *range_candidate;
  const char *declaration_path;
  size_t declaration_line;
  size_t declaration_column;
} ConfitSchemaConfigRecord;

struct ConfitSchemaProject {
  ConfitAllocator allocator;
  ConfitSourceGraph *source_graph;
  ConfitCatalog *catalog;
  ConfitSchemaConfigRecord *configs;
  size_t config_count;
  size_t config_capacity;
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
static const char kInvalidTypeName[] = "configuration type must be a bounded string";
static const char kInvalidPrompt[] = "config prompt must be a bounded one-line string";
static const char kInvalidHelp[] = "config help must be a bounded non-empty string";
static const char kInvalidDefaultShape[] = "config default must be a TOML scalar";
static const char kInvalidValuesShape[] = "config values must be an array";
static const char kInvalidRangeShape[] = "config range must be a table";
static const char kInvalidDependencyShape[] = "config depends_on must be a bounded string";
static const char kDuplicateSymbol[] = "configuration symbol is duplicated";
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

static void confit_schema_config_record_destroy(
    ConfitSchemaConfigRecord *record, const ConfitAllocator *allocator) {
  if (record == 0) return;
  if (record->symbol != 0)
    allocator->deallocate(allocator->context, record->symbol);
  if (record->type_name != 0)
    allocator->deallocate(allocator->context, record->type_name);
  if (record->prompt != 0)
    allocator->deallocate(allocator->context, record->prompt);
  if (record->help != 0)
    allocator->deallocate(allocator->context, record->help);
  if (record->dependency_text != 0)
    allocator->deallocate(allocator->context, record->dependency_text);
  memset(record, 0, sizeof(*record));
}

static ConfitStatus confit_schema_grow_configs(
    ConfitSchemaProject *project, ConfitDiagnostic *diagnostic,
    const ConfitTomlValue *location, const char *path) {
  ConfitSchemaConfigRecord *replacement;
  size_t capacity;
  size_t bytes;
  if (project->config_count < project->config_capacity) return CONFIT_OK;
  if (project->config_count >= CONFIT_LIMIT_CONFIG_SYMBOLS) {
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                    location, path, kConfigLimit);
  }
  capacity = project->config_capacity == 0U ? 8U
                                            : project->config_capacity * 2U;
  if (capacity > CONFIT_LIMIT_CONFIG_SYMBOLS)
    capacity = CONFIT_LIMIT_CONFIG_SYMBOLS;
  if (capacity < project->config_count ||
      capacity > SIZE_MAX / sizeof(*replacement)) {
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_INTERNAL, location,
                                    path, kOutOfMemory);
  }
  bytes = capacity * sizeof(*replacement);
  replacement = (ConfitSchemaConfigRecord *)project->allocator.allocate(
      project->allocator.context, bytes);
  if (replacement == 0) {
    return confit_schema_fail_value(diagnostic, CONFIT_ERR_INTERNAL, location,
                                    path, kOutOfMemory);
  }
  memset(replacement, 0, bytes);
  if (project->config_count != 0U) {
    memcpy(replacement, project->configs,
           project->config_count * sizeof(*replacement));
  }
  if (project->configs != 0)
    project->allocator.deallocate(project->allocator.context, project->configs);
  project->configs = replacement;
  project->config_capacity = capacity;
  return CONFIT_OK;
}

static int confit_schema_symbol_exists(const ConfitSchemaProject *project,
                                       const char *symbol) {
  size_t index;
  for (index = 0U; index < project->config_count; ++index) {
    if (strcmp(project->configs[index].symbol, symbol) == 0) return 1;
  }
  return 0;
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

static ConfitStatus confit_schema_add_config(
    ConfitSchemaProject *project, size_t fragment, size_t menu,
    const ConfitTomlValue *table, const char *path,
    ConfitDiagnostic *diagnostic) {
  static const char *const allowed[] = {
      "symbol", "type", "prompt", "help", "default", "depends_on",
      "values", "range"};
  ConfitSchemaConfigRecord candidate;
  const ConfitTomlValue *symbol;
  const ConfitTomlValue *type_name;
  const ConfitTomlValue *prompt;
  const ConfitTomlValue *help;
  const ConfitTomlValue *dependency;
  ConfitStatus status;
  const char *symbol_bytes = 0;
  size_t symbol_size = 0U;
  memset(&candidate, 0, sizeof(candidate));
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
      &candidate.symbol, diagnostic, symbol, path, kInvalidSymbol);
  if (status == CONFIT_OK && !confit_symbol_is_valid(candidate.symbol))
    status = confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, symbol,
                                      path, kInvalidSymbol);
  if (status == CONFIT_OK &&
      confit_schema_symbol_exists(project, candidate.symbol))
    status = confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION, symbol,
                                      path, kDuplicateSymbol);
  if (status == CONFIT_OK)
    status = confit_schema_copy_string(
        type_name, 128U, 0, 0, &project->allocator, &candidate.type_name,
        diagnostic, path, kInvalidTypeName);
  if (status == CONFIT_OK)
    status = confit_schema_copy_string(
        prompt, CONFIT_LIMIT_PROMPT_BYTES, 0, 0, &project->allocator,
        &candidate.prompt, diagnostic, path, kInvalidPrompt);
  if (status == CONFIT_OK)
    status = confit_schema_copy_string(
        help, CONFIT_LIMIT_HELP_BYTES, 0, 1, &project->allocator,
        &candidate.help, diagnostic, path, kInvalidHelp);
  candidate.default_candidate = confit_toml_table_find(table, "default");
  candidate.values_candidate = confit_toml_table_find(table, "values");
  candidate.range_candidate = confit_toml_table_find(table, "range");
  if (status == CONFIT_OK && candidate.default_candidate != 0 &&
      !confit_schema_scalar(candidate.default_candidate))
    status = confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                      candidate.default_candidate, path,
                                      kInvalidDefaultShape);
  if (status == CONFIT_OK && candidate.values_candidate != 0 &&
      confit_toml_value_type(candidate.values_candidate) !=
          CONFIT_TOML_VALUE_ARRAY)
    status = confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                      candidate.values_candidate, path,
                                      kInvalidValuesShape);
  if (status == CONFIT_OK && candidate.range_candidate != 0 &&
      confit_toml_value_type(candidate.range_candidate) !=
          CONFIT_TOML_VALUE_TABLE)
    status = confit_schema_fail_value(diagnostic, CONFIT_ERR_VALIDATION,
                                      candidate.range_candidate, path,
                                      kInvalidRangeShape);
  dependency = confit_toml_table_find(table, "depends_on");
  if (status == CONFIT_OK && dependency != 0)
    status = confit_schema_copy_string(
        dependency, CONFIT_LIMIT_DEPENDENCY_TEXT_BYTES, 1, 0,
        &project->allocator, &candidate.dependency_text, diagnostic, path,
        kInvalidDependencyShape);
  candidate.fragment = fragment;
  candidate.menu = menu;
  candidate.declaration_path = path;
  candidate.declaration_line = confit_toml_value_line(table);
  candidate.declaration_column = confit_toml_value_column(table);
  if (status == CONFIT_OK)
    status = confit_schema_grow_configs(project, diagnostic, table, path);
  if (status != CONFIT_OK) {
    confit_schema_config_record_destroy(&candidate, &project->allocator);
    return status;
  }
  project->configs[project->config_count++] = candidate;
  return CONFIT_OK;
}

static ConfitStatus confit_schema_parse_fragment(
    ConfitSchemaProject *project, size_t fragment,
    const ConfitSourceNodeView *node, size_t parent_menu,
    size_t *out_attachment, ConfitDiagnostic *diagnostic) {
  static const char *const allowed[] = {"menu", "config"};
  const ConfitTomlValue *root =
      confit_toml_document_root(confit_input_image_document(node->input));
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
      status = confit_schema_add_config(project, fragment, attachment, config,
                                        node->path, diagnostic);
      if (status != CONFIT_OK) return status;
    }
  }
  *out_attachment = attachment;
  return CONFIT_OK;
}

void confit_schema_project_destroy(ConfitSchemaProject *project) {
  ConfitAllocator allocator;
  size_t index;
  if (project == 0) return;
  allocator = project->allocator;
  for (index = project->config_count; index > 0U; --index)
    confit_schema_config_record_destroy(&project->configs[index - 1U],
                                        &allocator);
  if (project->configs != 0)
    allocator.deallocate(allocator.context, project->configs);
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
  return project != 0 ? project->config_count : 0U;
}

int confit_schema_project_config_at(const ConfitSchemaProject *project,
                                    size_t index,
                                    ConfitSchemaConfigView *out_view) {
  const ConfitSchemaConfigRecord *record;
  if (project == 0 || out_view == 0 || index >= project->config_count) return 0;
  record = &project->configs[index];
  out_view->fragment = record->fragment;
  out_view->menu = record->menu;
  out_view->symbol = record->symbol;
  out_view->type_name = record->type_name;
  out_view->prompt = record->prompt;
  out_view->help = record->help;
  out_view->dependency_text = record->dependency_text;
  out_view->default_candidate = record->default_candidate;
  out_view->values_candidate = record->values_candidate;
  out_view->range_candidate = record->range_candidate;
  out_view->declaration.path = record->declaration_path;
  out_view->declaration.line = record->declaration_line;
  out_view->declaration.column = record->declaration_column;
  return 1;
}

int confit_schema_project_find_config(const ConfitSchemaProject *project,
                                      const char *symbol,
                                      ConfitSchemaConfigView *out_view) {
  size_t index;
  if (project == 0 || symbol == 0 || out_view == 0) return 0;
  for (index = 0U; index < project->config_count; ++index) {
    if (strcmp(project->configs[index].symbol, symbol) == 0)
      return confit_schema_project_config_at(project, index, out_view);
  }
  return 0;
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
