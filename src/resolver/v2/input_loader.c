#include "ledger_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "confit/host.h"

static const char kAllocationFailed[] = "failed to allocate v2 ledger input";
static const char kInvalidInputDocument[] = "invalid schema v2 input document";
static const char kUnknownInputField[] = "unknown schema v2 input field";
static const char kMissingInputField[] = "required schema v2 input field is missing";
static const char kWrongInputType[] = "schema v2 input value has wrong type";
static const char kUnknownInputOption[] = "schema v2 input references unknown option";
static const char kDuplicateInputAssignment[] =
    "schema v2 input has duplicate option assignment";
static const char kInvalidInputValue[] = "invalid schema v2 input value";
static const char kRequiredUnset[] = "schema v2 required option cannot be unset";
static const char kDuplicateInputName[] = "duplicate schema v2 input name";
static const char kUnknownInputName[] = "unknown schema v2 profile or target";
static const char kInputInheritanceCycle[] = "schema v2 input inheritance cycle";

static void confit_v2_input_root_list_clear(ConfitV2StringList *list) {
  size_t index;
  if (list == 0) return;
  for (index = 0U; index < list->count; ++index) free(list->items[index]);
  free(list->items);
  memset(list, 0, sizeof(*list));
}

static ConfitStatus confit_v2_input_root_list_append(ConfitV2StringList *list,
                                                      const char *text) {
  char **grown;
  char *copy;
  size_t index;
  const size_t size = text != 0 ? strlen(text) : 0U;
  if (size == 0U || size > 192U || list->count >= 128U) return CONFIT_ERR_SCHEMA;
  for (index = 0U; index < list->count; ++index) {
    if (strcmp(list->items[index], text) == 0) return CONFIT_OK;
  }
  copy = (char *)malloc(size + 1U);
  if (copy == 0) return CONFIT_ERR_INTERNAL;
  memcpy(copy, text, size + 1U);
  grown = (char **)realloc(list->items, (list->count + 1U) * sizeof(*list->items));
  if (grown == 0) {
    free(copy);
    return CONFIT_ERR_INTERNAL;
  }
  list->items = grown;
  list->items[list->count++] = copy;
  return CONFIT_OK;
}

static int confit_v2_input_component_id_valid(const char *text) {
  size_t index;
  int segment_start = 1;
  if (text == 0 || text[0] == '\0') return 0;
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

static int confit_v2_input_capability_valid(const char *text) {
  size_t index;
  if (text == 0 || text[0] == '\0' || text[0] == '.') return 0;
  for (index = 0U; text[index] != '\0'; ++index) {
    const unsigned char value = (unsigned char)text[index];
    if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
          value == '.' || value == '_' || value == '-' || value == '@')) {
      return 0;
    }
  }
  return text[index - 1U] != '.';
}

static ConfitStatus confit_v2_input_parse_component_roots(
    const ConfitTomlValue *value, ConfitV2StringList *out,
    const char *path, ConfitDiagnostic *diagnostic) {
  size_t index;
  if (value == 0) return CONFIT_OK;
  if (confit_toml_value_type(value) != CONFIT_TOML_VALUE_ARRAY ||
      confit_toml_array_size(value) > 128U) {
    confit_v2_ledger_diagnostic(path, confit_toml_value_line(value),
                                confit_toml_value_column(value),
                                CONFIT_ERR_SCHEMA, kWrongInputType, diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < confit_toml_array_size(value); ++index) {
    const ConfitTomlValue *item = confit_toml_array_at(value, index);
    const char *text;
    size_t size;
    char buffer[193];
    ConfitStatus status;
    if (!confit_toml_value_string(item, &text, &size) || size == 0U ||
        size >= sizeof(buffer)) {
      confit_v2_ledger_diagnostic(path, confit_toml_value_line(item),
                                  confit_toml_value_column(item),
                                  CONFIT_ERR_SCHEMA, kWrongInputType, diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    memcpy(buffer, text, size);
    buffer[size] = '\0';
    if (!confit_v2_input_component_id_valid(buffer)) {
      confit_v2_ledger_diagnostic(path, confit_toml_value_line(item),
                                  confit_toml_value_column(item),
                                  CONFIT_ERR_SCHEMA, kInvalidInputValue, diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    status = confit_v2_input_root_list_append(out, buffer);
    if (status != CONFIT_OK) {
      confit_v2_ledger_diagnostic(path, confit_toml_value_line(item),
                                  confit_toml_value_column(item), status,
                                  kInvalidInputValue, diagnostic);
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_input_parse_capability_requests(
    const ConfitTomlValue *value, ConfitV2StringList *out, const char *path,
    ConfitDiagnostic *diagnostic) {
  size_t index;
  if (value == 0) return CONFIT_OK;
  if (confit_toml_value_type(value) != CONFIT_TOML_VALUE_ARRAY ||
      confit_toml_array_size(value) > 128U) {
    confit_v2_ledger_diagnostic(path, confit_toml_value_line(value),
                                confit_toml_value_column(value),
                                CONFIT_ERR_SCHEMA, kWrongInputType, diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < confit_toml_array_size(value); ++index) {
    const ConfitTomlValue *item = confit_toml_array_at(value, index);
    const char *text;
    size_t size;
    char buffer[193];
    ConfitStatus status;
    if (!confit_toml_value_string(item, &text, &size) || size == 0U ||
        size >= sizeof(buffer)) {
      confit_v2_ledger_diagnostic(path, confit_toml_value_line(item),
                                  confit_toml_value_column(item),
                                  CONFIT_ERR_SCHEMA, kWrongInputType, diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    memcpy(buffer, text, size);
    buffer[size] = '\0';
    if (!confit_v2_input_capability_valid(buffer)) {
      confit_v2_ledger_diagnostic(path, confit_toml_value_line(item),
                                  confit_toml_value_column(item),
                                  CONFIT_ERR_SCHEMA, kInvalidInputValue, diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    status = confit_v2_input_root_list_append(out, buffer);
    if (status != CONFIT_OK) {
      confit_v2_ledger_diagnostic(path, confit_toml_value_line(item),
                                  confit_toml_value_column(item), status,
                                  kInvalidInputValue, diagnostic);
      return status;
    }
  }
  return CONFIT_OK;
}

char *confit_v2_ledger_strdup(const char *text) {
  char *copy;
  size_t size;

  if (text == 0) {
    return 0;
  }
  size = strlen(text);
  copy = (char *)malloc(size + 1U);
  if (copy != 0) {
    memcpy(copy, text, size + 1U);
  }
  return copy;
}

void confit_v2_ledger_diagnostic(const char *path, size_t line, size_t column,
                                 ConfitStatus status, const char *message,
                                 ConfitDiagnostic *diagnostic) {
  confit_diagnostic_set(diagnostic, status, path, line, column, message);
}

void confit_v2_ledger_value_clear(ConfitV2Value *value) {
  size_t index;

  if (value == 0) {
    return;
  }
  if (value->kind == CONFIT_V2_VALUE_STRING) {
    free(value->as.string_value);
  } else if (value->kind == CONFIT_V2_VALUE_STRING_LIST) {
    for (index = 0U; index < value->as.string_list.count; ++index) {
      free(value->as.string_list.items[index]);
    }
    free(value->as.string_list.items);
  }
  memset(value, 0, sizeof(*value));
}

ConfitStatus confit_v2_ledger_value_copy(ConfitV2Value *out,
                                          const ConfitV2Value *value) {
  size_t index;

  if (out == 0 || value == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  memset(out, 0, sizeof(*out));
  out->kind = value->kind;
  if (value->kind == CONFIT_V2_VALUE_STRING) {
    out->as.string_value = confit_v2_ledger_strdup(value->as.string_value);
    return value->as.string_value == 0 || out->as.string_value != 0
               ? CONFIT_OK
               : CONFIT_ERR_INTERNAL;
  }
  if (value->kind == CONFIT_V2_VALUE_STRING_LIST) {
    out->as.string_list.count = value->as.string_list.count;
    if (out->as.string_list.count > 0U) {
      out->as.string_list.items = (char **)calloc(
          out->as.string_list.count, sizeof(*out->as.string_list.items));
      if (out->as.string_list.items == 0) {
        confit_v2_ledger_value_clear(out);
        return CONFIT_ERR_INTERNAL;
      }
    }
    for (index = 0U; index < out->as.string_list.count; ++index) {
      out->as.string_list.items[index] =
          confit_v2_ledger_strdup(value->as.string_list.items[index]);
      if (out->as.string_list.items[index] == 0) {
        confit_v2_ledger_value_clear(out);
        return CONFIT_ERR_INTERNAL;
      }
    }
    return CONFIT_OK;
  }
  out->as = value->as;
  return CONFIT_OK;
}

static int confit_v2_ledger_logical_path(const char *path) {
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

static int confit_v2_ledger_candidate_index(const ConfitV2StringList *values,
                                             const char *text,
                                             size_t *out_index) {
  size_t index;

  for (index = 0U; index < values->count; ++index) {
    if (strcmp(values->items[index], text) == 0) {
      if (out_index != 0) {
        *out_index = index;
      }
      return 1;
    }
  }
  return 0;
}

static int confit_v2_ledger_range_contains(const ConfitV2Symbol *symbol,
                                            const ConfitV2Value *value) {
  if (!symbol->range.is_set) {
    return 1;
  }
  if (value->kind == CONFIT_V2_VALUE_INT) {
    return value->as.int_value >= symbol->range.min_value.as.int_value &&
           value->as.int_value <= symbol->range.max_value.as.int_value;
  }
  if (value->kind == CONFIT_V2_VALUE_UINT) {
    return value->as.uint_value >= symbol->range.min_value.as.uint_value &&
           value->as.uint_value <= symbol->range.max_value.as.uint_value;
  }
  if (value->kind == CONFIT_V2_VALUE_FLOAT) {
    return isfinite(value->as.float_value) &&
           value->as.float_value >= symbol->range.min_value.as.float_value &&
           value->as.float_value <= symbol->range.max_value.as.float_value;
  }
  return 0;
}

ConfitStatus confit_v2_ledger_validate_value(
    const ConfitV2Symbol *symbol, ConfitV2Value *value,
    const char *path, size_t line, size_t column, ConfitDiagnostic *diagnostic) {
  size_t index;

  if (symbol == 0 || value == 0 ||
      confit_v2_type_descriptor(symbol->type) == 0 ||
      value->kind != confit_v2_type_descriptor(symbol->type)->value_kind) {
    confit_v2_ledger_diagnostic(path, line, column, CONFIT_ERR_SCHEMA,
                                kInvalidInputValue, diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  if (symbol->type == CONFIT_V2_OPTION_TYPE_FLOAT &&
      !isfinite(value->as.float_value)) {
    confit_v2_ledger_diagnostic(path, line, column, CONFIT_ERR_SCHEMA,
                                kInvalidInputValue, diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  if ((symbol->type == CONFIT_V2_OPTION_TYPE_PATH ||
       symbol->type == CONFIT_V2_OPTION_TYPE_PATH_LIST) &&
      ((value->kind == CONFIT_V2_VALUE_STRING &&
        !confit_v2_ledger_logical_path(value->as.string_value)))) {
    confit_v2_ledger_diagnostic(path, line, column, CONFIT_ERR_SCHEMA,
                                kInvalidInputValue, diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  if (value->kind == CONFIT_V2_VALUE_STRING_LIST &&
      symbol->type == CONFIT_V2_OPTION_TYPE_PATH_LIST) {
    for (index = 0U; index < value->as.string_list.count; ++index) {
      if (!confit_v2_ledger_logical_path(value->as.string_list.items[index])) {
        confit_v2_ledger_diagnostic(path, line, column, CONFIT_ERR_SCHEMA,
                                    kInvalidInputValue, diagnostic);
        return CONFIT_ERR_SCHEMA;
      }
    }
  }
  if (symbol->type == CONFIT_V2_OPTION_TYPE_ENUM &&
      !confit_v2_ledger_candidate_index(&symbol->values,
                                        value->as.string_value, 0)) {
    confit_v2_ledger_diagnostic(path, line, column, CONFIT_ERR_SCHEMA,
                                kInvalidInputValue, diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  if (symbol->type == CONFIT_V2_OPTION_TYPE_ENUM_SET) {
    for (index = 0U; index < value->as.string_list.count; ++index) {
      size_t other;
      if (!confit_v2_ledger_candidate_index(
              &symbol->values, value->as.string_list.items[index], 0)) {
        confit_v2_ledger_diagnostic(path, line, column, CONFIT_ERR_SCHEMA,
                                    kInvalidInputValue, diagnostic);
        return CONFIT_ERR_SCHEMA;
      }
      for (other = index + 1U; other < value->as.string_list.count; ++other) {
        if (strcmp(value->as.string_list.items[index],
                   value->as.string_list.items[other]) == 0) {
          confit_v2_ledger_diagnostic(path, line, column, CONFIT_ERR_SCHEMA,
                                      kInvalidInputValue, diagnostic);
          return CONFIT_ERR_SCHEMA;
        }
      }
    }
    for (index = 0U; index < value->as.string_list.count; ++index) {
      size_t other;
      for (other = index + 1U; other < value->as.string_list.count; ++other) {
        size_t left_index = 0U;
        size_t right_index = 0U;
        if (!confit_v2_ledger_candidate_index(
                &symbol->values, value->as.string_list.items[index],
                &left_index) ||
            !confit_v2_ledger_candidate_index(
                &symbol->values, value->as.string_list.items[other],
                &right_index)) {
          confit_v2_ledger_diagnostic(path, line, column, CONFIT_ERR_SCHEMA,
                                      kInvalidInputValue, diagnostic);
          return CONFIT_ERR_SCHEMA;
        }
        if (left_index > right_index) {
          char *swap = value->as.string_list.items[index];
          value->as.string_list.items[index] = value->as.string_list.items[other];
          value->as.string_list.items[other] = swap;
        }
      }
    }
  }
  if (!confit_v2_ledger_range_contains(symbol, value)) {
    confit_v2_ledger_diagnostic(path, line, column, CONFIT_ERR_SCHEMA,
                                kInvalidInputValue, diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_ledger_parse_string_list(
    const ConfitTomlValue *source, ConfitV2Value *out,
    const char *path, ConfitDiagnostic *diagnostic) {
  size_t index;

  if (confit_toml_value_type(source) != CONFIT_TOML_VALUE_ARRAY) {
    confit_v2_ledger_diagnostic(path, confit_toml_value_line(source),
                                confit_toml_value_column(source),
                                CONFIT_ERR_SCHEMA, kWrongInputType, diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  out->kind = CONFIT_V2_VALUE_STRING_LIST;
  out->as.string_list.count = confit_toml_array_size(source);
  if (out->as.string_list.count > 0U) {
    out->as.string_list.items = (char **)calloc(
        out->as.string_list.count, sizeof(*out->as.string_list.items));
    if (out->as.string_list.items == 0) {
      return CONFIT_ERR_INTERNAL;
    }
  }
  for (index = 0U; index < out->as.string_list.count; ++index) {
    const char *text;
    size_t size;
    if (!confit_toml_value_string(confit_toml_array_at(source, index),
                                      &text, &size)) {
      confit_v2_ledger_value_clear(out);
      confit_v2_ledger_diagnostic(path, confit_toml_value_line(source),
                                  confit_toml_value_column(source),
                                  CONFIT_ERR_SCHEMA, kWrongInputType, diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    out->as.string_list.items[index] = (char *)malloc(size + 1U);
    if (out->as.string_list.items[index] == 0) {
      confit_v2_ledger_value_clear(out);
      return CONFIT_ERR_INTERNAL;
    }
    memcpy(out->as.string_list.items[index], text, size);
    out->as.string_list.items[index][size] = '\0';
  }
  return CONFIT_OK;
}

ConfitStatus confit_v2_ledger_parse_toml_value(
    const ConfitV2Symbol *symbol, const ConfitTomlValue *source,
    ConfitV2Value *out, ConfitDiagnostic *diagnostic) {
  const char *path = confit_toml_value_source(source);
  const ConfitV2TypeDescriptor *descriptor;
  int64_t integer;
  double floating;
  const char *text;
  size_t size;
  ConfitStatus status;

  if (symbol == 0 || source == 0 || out == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  memset(out, 0, sizeof(*out));
  descriptor = confit_v2_type_descriptor(symbol->type);
  if (descriptor == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  out->kind = descriptor->value_kind;
  switch (symbol->type) {
  case CONFIT_V2_OPTION_TYPE_BOOL:
    if (!confit_toml_value_bool(source, &out->as.bool_value)) {
      status = CONFIT_ERR_SCHEMA;
      break;
    }
    status = CONFIT_OK;
    break;
  case CONFIT_V2_OPTION_TYPE_TRISTATE:
    if (!confit_toml_value_string(source, &text, &size) || size != 1U ||
        (text[0] != 'n' && text[0] != 'm' && text[0] != 'y')) {
      status = CONFIT_ERR_SCHEMA;
      break;
    }
    out->as.tristate_value = text[0];
    status = CONFIT_OK;
    break;
  case CONFIT_V2_OPTION_TYPE_INT:
    if (!confit_toml_value_int64(source, &integer)) {
      status = CONFIT_ERR_SCHEMA;
      break;
    }
    out->as.int_value = integer;
    status = CONFIT_OK;
    break;
  case CONFIT_V2_OPTION_TYPE_UINT:
  case CONFIT_V2_OPTION_TYPE_HEX:
    if (!confit_toml_value_int64(source, &integer) || integer < 0) {
      status = CONFIT_ERR_SCHEMA;
      break;
    }
    out->as.uint_value = (uint64_t)integer;
    status = CONFIT_OK;
    break;
  case CONFIT_V2_OPTION_TYPE_FLOAT:
    if (confit_toml_value_float64(source, &floating)) {
      out->as.float_value = floating;
      status = CONFIT_OK;
    } else if (confit_toml_value_int64(source, &integer)) {
      out->as.float_value = (double)integer;
      status = CONFIT_OK;
    } else {
      status = CONFIT_ERR_SCHEMA;
    }
    break;
  case CONFIT_V2_OPTION_TYPE_STRING:
  case CONFIT_V2_OPTION_TYPE_ENUM:
  case CONFIT_V2_OPTION_TYPE_PATH:
    if (!confit_toml_value_string(source, &text, &size)) {
      status = CONFIT_ERR_SCHEMA;
      break;
    }
    out->as.string_value = (char *)malloc(size + 1U);
    if (out->as.string_value == 0) {
      return CONFIT_ERR_INTERNAL;
    }
    memcpy(out->as.string_value, text, size);
    out->as.string_value[size] = '\0';
    status = CONFIT_OK;
    break;
  case CONFIT_V2_OPTION_TYPE_STRING_LIST:
  case CONFIT_V2_OPTION_TYPE_PATH_LIST:
  case CONFIT_V2_OPTION_TYPE_ENUM_SET:
    status = confit_v2_ledger_parse_string_list(source, out, path, diagnostic);
    break;
  case CONFIT_V2_OPTION_TYPE_INVALID:
  default:
    return CONFIT_ERR_INTERNAL;
  }
  if (status != CONFIT_OK) {
    confit_v2_ledger_value_clear(out);
    confit_v2_ledger_diagnostic(path, confit_toml_value_line(source),
                                confit_toml_value_column(source),
                                CONFIT_ERR_SCHEMA, kWrongInputType, diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  return confit_v2_ledger_validate_value(
      symbol, out, path, confit_toml_value_line(source),
      confit_toml_value_column(source), diagnostic);
}

ConfitStatus confit_v2_ledger_parse_user_value(
    const ConfitV2Symbol *symbol, const char *text, ConfitV2Value *out,
    ConfitDiagnostic *diagnostic) {
  ConfitTomlDocument *document = 0;
  const ConfitTomlValue *source;
  char *toml;
  size_t size;
  ConfitStatus status;

  if (symbol == 0 || text == 0 || out == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  if (symbol->type == CONFIT_V2_OPTION_TYPE_STRING ||
      symbol->type == CONFIT_V2_OPTION_TYPE_PATH ||
      symbol->type == CONFIT_V2_OPTION_TYPE_ENUM) {
    memset(out, 0, sizeof(*out));
    out->kind = CONFIT_V2_VALUE_STRING;
    out->as.string_value = confit_v2_ledger_strdup(text);
    if (out->as.string_value == 0) {
      return CONFIT_ERR_INTERNAL;
    }
    return confit_v2_ledger_validate_value(symbol, out, "cli --set", 0U, 0U,
                                           diagnostic);
  }
  if (symbol->type == CONFIT_V2_OPTION_TYPE_TRISTATE && strlen(text) == 1U &&
      (text[0] == 'n' || text[0] == 'm' || text[0] == 'y')) {
    memset(out, 0, sizeof(*out));
    out->kind = CONFIT_V2_VALUE_TRISTATE;
    out->as.tristate_value = text[0];
    return CONFIT_OK;
  }
  size = strlen(text);
  toml = (char *)malloc(size + 9U);
  if (toml == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  memcpy(toml, "value = ", 8U);
  memcpy(toml + 8U, text, size + 1U);
  status = confit_toml_parse_text("cli --set", toml, size + 8U, &document,
                                      diagnostic);
  free(toml);
  if (status != CONFIT_OK) {
    return status;
  }
  source = confit_toml_table_find(confit_toml_document_root(document),
                                     "value");
  status = source != 0 ? confit_v2_ledger_parse_toml_value(symbol, source, out,
                                                             diagnostic)
                       : CONFIT_ERR_INTERNAL;
  confit_toml_document_free(document);
  return status;
}

static int confit_v2_input_table_only(const ConfitTomlValue *table,
                                      const char *const *fields,
                                      size_t field_count) {
  size_t index;

  if (confit_toml_value_type(table) != CONFIT_TOML_VALUE_TABLE) {
    return 0;
  }
  for (index = 0U; index < confit_toml_table_size(table); ++index) {
    const char *key = confit_toml_table_key_at(table, index);
    size_t field_index;
    int known = 0;
    for (field_index = 0U; field_index < field_count; ++field_index) {
      if (strcmp(key, fields[field_index]) == 0) {
        known = 1;
        break;
      }
    }
    if (!known) {
      return 0;
    }
  }
  return 1;
}

static void confit_v2_input_document_clear(ConfitV2InputDocument *document) {
  size_t index;

  if (document == 0) {
    return;
  }
  free(document->name);
  free(document->base);
  free(document->target);
  free(document->path);
  confit_v2_input_root_list_clear(&document->root_components);
  confit_v2_input_root_list_clear(&document->required_capabilities);
  confit_v2_input_root_list_clear(&document->optional_capabilities);
  for (index = 0U; index < document->assignment_count; ++index) {
    confit_v2_ledger_value_clear(&document->assignments[index].value);
  }
  free(document->assignments);
  memset(document, 0, sizeof(*document));
}

void confit_v2_input_catalog_clear(ConfitV2InputCatalog *catalog) {
  size_t index;

  if (catalog == 0) {
    return;
  }
  for (index = 0U; index < catalog->document_count; ++index) {
    confit_v2_input_document_clear(&catalog->documents[index]);
  }
  free(catalog->documents);
  memset(catalog, 0, sizeof(*catalog));
}

const ConfitV2InputDocument *confit_v2_input_catalog_find(
    const ConfitV2InputCatalog *catalog, const char *name) {
  size_t index;

  if (catalog == 0 || name == 0) {
    return 0;
  }
  for (index = 0U; index < catalog->document_count; ++index) {
    if (strcmp(catalog->documents[index].name, name) == 0) {
      return &catalog->documents[index];
    }
  }
  return 0;
}

static ConfitStatus confit_v2_input_append_assignment(
    ConfitV2InputDocument *document, const ConfitV2Symbol *symbol,
    const ConfitV2Value *value, int is_unset, size_t line, size_t column,
    ConfitDiagnostic *diagnostic) {
  ConfitV2InputAssignment *grown;
  size_t index;

  for (index = 0U; index < document->assignment_count; ++index) {
    if (document->assignments[index].symbol == symbol) {
      confit_v2_ledger_diagnostic(document->path, line, column, CONFIT_ERR_SCHEMA,
                                  kDuplicateInputAssignment, diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
  }
  if (document->assignment_count == SIZE_MAX / sizeof(*document->assignments)) {
    return CONFIT_ERR_INTERNAL;
  }
  grown = (ConfitV2InputAssignment *)realloc(
      document->assignments,
      (document->assignment_count + 1U) * sizeof(*document->assignments));
  if (grown == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  document->assignments = grown;
  memset(&document->assignments[document->assignment_count], 0,
         sizeof(*document->assignments));
  document->assignments[document->assignment_count].symbol = symbol;
  document->assignments[document->assignment_count].is_unset = is_unset;
  document->assignments[document->assignment_count].line = line;
  document->assignments[document->assignment_count].column = column;
  document->assignments[document->assignment_count].declaration_order =
      document->assignment_count;
  if (!is_unset && confit_v2_ledger_value_copy(
                       &document->assignments[document->assignment_count].value,
                       value) != CONFIT_OK) {
    return CONFIT_ERR_INTERNAL;
  }
  document->assignment_count += 1U;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_input_validate_writer(
    const ConfitV2LinkedProject *linked, ConfitV2InputKind kind,
    const ConfitV2Symbol *symbol, const char *path, size_t line, size_t column,
    int is_unset, ConfitDiagnostic *diagnostic) {
  ConfitV2WriteRequest request;
  ConfitStatus status;

  memset(&request, 0, sizeof(request));
  request.option_id = symbol->id;
  request.writer = kind == CONFIT_V2_INPUT_KIND_PROFILE
                       ? CONFIT_V2_ASSIGNMENT_WRITER_PROFILE
                       : CONFIT_V2_ASSIGNMENT_WRITER_TARGET;
  request.is_unset = is_unset;
  request.span.path = (char *)path;
  request.span.line = line;
  request.span.column = column;
  status = confit_v2_linked_project_validate_write(linked, &request, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  if (is_unset && symbol->required) {
    confit_v2_ledger_diagnostic(path, line, column, CONFIT_ERR_SCHEMA,
                                kRequiredUnset, diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_input_parse_values(
    const ConfitV2LinkedProject *linked, ConfitV2InputKind kind,
    ConfitV2InputDocument *document, const ConfitTomlValue *values,
    ConfitDiagnostic *diagnostic) {
  size_t index;

  if (values == 0) {
    return CONFIT_OK;
  }
  if (confit_toml_value_type(values) != CONFIT_TOML_VALUE_TABLE) {
    confit_v2_ledger_diagnostic(document->path, confit_toml_value_line(values),
                                confit_toml_value_column(values),
                                CONFIT_ERR_SCHEMA, kWrongInputType, diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < confit_toml_table_size(values); ++index) {
    const char *id = confit_toml_table_key_at(values, index);
    const ConfitTomlValue *value =
        confit_toml_table_value_at(values, index);
    const ConfitV2Symbol *symbol =
        confit_v2_linked_project_find_symbol(linked, id);
    ConfitV2Value parsed;
    ConfitStatus status;

    if (symbol == 0) {
      confit_v2_ledger_diagnostic(document->path, confit_toml_value_line(value),
                                  confit_toml_value_column(value),
                                  CONFIT_ERR_SCHEMA, kUnknownInputOption,
                                  diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    status = confit_v2_input_validate_writer(
        linked, kind, symbol, document->path, confit_toml_value_line(value),
        confit_toml_value_column(value), 0, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    memset(&parsed, 0, sizeof(parsed));
    status = confit_v2_ledger_parse_toml_value(symbol, value, &parsed, diagnostic);
    if (status == CONFIT_OK) {
      status = confit_v2_input_append_assignment(
          document, symbol, &parsed, 0, confit_toml_value_line(value),
          confit_toml_value_column(value), diagnostic);
    }
    confit_v2_ledger_value_clear(&parsed);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_input_parse_unset(
    const ConfitV2LinkedProject *linked, ConfitV2InputKind kind,
    ConfitV2InputDocument *document, const ConfitTomlValue *unset,
    ConfitDiagnostic *diagnostic) {
  static const char *const kUnsetFields[] = {"options"};
  const ConfitTomlValue *options;
  size_t index;

  if (unset == 0) {
    return CONFIT_OK;
  }
  if (!confit_v2_input_table_only(unset, kUnsetFields,
                                  sizeof(kUnsetFields) / sizeof(kUnsetFields[0]))) {
    confit_v2_ledger_diagnostic(document->path, confit_toml_value_line(unset),
                                confit_toml_value_column(unset),
                                CONFIT_ERR_SCHEMA, kUnknownInputField, diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  options = confit_toml_table_find(unset, "options");
  if (options == 0 || confit_toml_value_type(options) !=
                          CONFIT_TOML_VALUE_ARRAY) {
    confit_v2_ledger_diagnostic(document->path, confit_toml_value_line(unset),
                                confit_toml_value_column(unset),
                                CONFIT_ERR_SCHEMA, kMissingInputField, diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < confit_toml_array_size(options); ++index) {
    const ConfitTomlValue *item = confit_toml_array_at(options, index);
    const char *id;
    size_t size;
    const ConfitV2Symbol *symbol;
    ConfitStatus status;

    if (!confit_toml_value_string(item, &id, &size) || size == 0U) {
      confit_v2_ledger_diagnostic(document->path, confit_toml_value_line(item),
                                  confit_toml_value_column(item),
                                  CONFIT_ERR_SCHEMA, kWrongInputType, diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    symbol = confit_v2_linked_project_find_symbol(linked, id);
    if (symbol == 0) {
      confit_v2_ledger_diagnostic(document->path, confit_toml_value_line(item),
                                  confit_toml_value_column(item),
                                  CONFIT_ERR_SCHEMA, kUnknownInputOption,
                                  diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    status = confit_v2_input_validate_writer(
        linked, kind, symbol, document->path, confit_toml_value_line(item),
        confit_toml_value_column(item), 1, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    status = confit_v2_input_append_assignment(
        document, symbol, 0, 1, confit_toml_value_line(item),
        confit_toml_value_column(item), diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_input_parse_document(
    const ConfitV2LinkedProject *linked, ConfitV2InputKind kind,
    const char *path, ConfitV2InputDocument *out_document,
    ConfitDiagnostic *diagnostic) {
  static const char *const kProfileFields[] = {
      "name", "schema_version", "base", "target", "root_components",
      "required_capabilities", "optional_capabilities"};
  static const char *const kTargetFields[] = {
      "name", "schema_version", "base", "claim", "root_components",
      "required_capabilities", "optional_capabilities"};
  ConfitTomlDocument *document = 0;
  const ConfitTomlValue *root;
  const ConfitTomlValue *section;
  const ConfitTomlValue *name;
  const ConfitTomlValue *schema_version;
  const ConfitTomlValue *base;
  const ConfitTomlValue *target;
  int64_t version;
  const char *text;
  size_t text_size;
  ConfitStatus status;

  memset(out_document, 0, sizeof(*out_document));
  out_document->path = confit_v2_ledger_strdup(path);
  if (out_document->path == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  status = confit_toml_parse_file(path, &document, diagnostic);
  if (status != CONFIT_OK) {
    confit_v2_input_document_clear(out_document);
    return status;
  }
  root = confit_toml_document_root(document);
  section = confit_toml_table_find(root,
                                      kind == CONFIT_V2_INPUT_KIND_PROFILE
                                          ? "profile"
                                          : "target");
  if (section == 0 ||
      !confit_v2_input_table_only(
          section, kind == CONFIT_V2_INPUT_KIND_PROFILE ? kProfileFields
                                                         : kTargetFields,
          kind == CONFIT_V2_INPUT_KIND_PROFILE
              ? sizeof(kProfileFields) / sizeof(kProfileFields[0])
              : sizeof(kTargetFields) / sizeof(kTargetFields[0]))) {
    confit_v2_ledger_diagnostic(path, confit_toml_value_line(root),
                                confit_toml_value_column(root),
                                CONFIT_ERR_SCHEMA, kInvalidInputDocument,
                                diagnostic);
    confit_toml_document_free(document);
    confit_v2_input_document_clear(out_document);
    return CONFIT_ERR_SCHEMA;
  }
  name = confit_toml_table_find(section, "name");
  schema_version = confit_toml_table_find(section, "schema_version");
  if (name == 0 || schema_version == 0 ||
      !confit_toml_value_string(name, &text, &text_size) || text_size == 0U ||
      !confit_toml_value_int64(schema_version, &version) ||
      (kind == CONFIT_V2_INPUT_KIND_PROFILE
           ? version != 2
           : (version != 2 && version != 3))) {
    confit_v2_ledger_diagnostic(path, confit_toml_value_line(section),
                                confit_toml_value_column(section),
                                CONFIT_ERR_SCHEMA, kMissingInputField, diagnostic);
    confit_toml_document_free(document);
    confit_v2_input_document_clear(out_document);
    return CONFIT_ERR_SCHEMA;
  }
  out_document->name = (char *)malloc(text_size + 1U);
  if (out_document->name == 0) {
    confit_toml_document_free(document);
    confit_v2_input_document_clear(out_document);
    return CONFIT_ERR_INTERNAL;
  }
  memcpy(out_document->name, text, text_size);
  out_document->name[text_size] = '\0';
  base = confit_toml_table_find(section, "base");
  if (base != 0) {
    if (!confit_toml_value_string(base, &text, &text_size) || text_size == 0U) {
      status = CONFIT_ERR_SCHEMA;
      goto fail;
    }
    out_document->base = (char *)malloc(text_size + 1U);
    if (out_document->base == 0) {
      status = CONFIT_ERR_INTERNAL;
      goto fail;
    }
    memcpy(out_document->base, text, text_size);
    out_document->base[text_size] = '\0';
  }
  target = kind == CONFIT_V2_INPUT_KIND_PROFILE
               ? confit_toml_table_find(section, "target")
               : 0;
  if (target != 0) {
    if (!confit_toml_value_string(target, &text, &text_size) || text_size == 0U) {
      status = CONFIT_ERR_SCHEMA;
      goto fail;
    }
    out_document->target = (char *)malloc(text_size + 1U);
    if (out_document->target == 0) {
      status = CONFIT_ERR_INTERNAL;
      goto fail;
    }
    memcpy(out_document->target, text, text_size);
    out_document->target[text_size] = '\0';
    out_document->target_line = confit_toml_value_line(target);
    out_document->target_column = confit_toml_value_column(target);
  }
  status = confit_v2_input_parse_component_roots(
      confit_toml_table_find(section, "root_components"),
      &out_document->root_components, path, diagnostic);
  if (status == CONFIT_OK) status = confit_v2_input_parse_capability_requests(
      confit_toml_table_find(section, "required_capabilities"),
      &out_document->required_capabilities, path, diagnostic);
  if (status == CONFIT_OK) status = confit_v2_input_parse_capability_requests(
      confit_toml_table_find(section, "optional_capabilities"),
      &out_document->optional_capabilities, path, diagnostic);
  if (status != CONFIT_OK) goto fail;
  status = confit_v2_input_parse_values(
      linked, kind, out_document, confit_toml_table_find(root, "values"),
      diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_input_parse_unset(
        linked, kind, out_document, confit_toml_table_find(root, "unset"),
        diagnostic);
  }
  if (status == CONFIT_OK) {
    size_t root_index;
    for (root_index = 0U; root_index < confit_toml_table_size(root);
         ++root_index) {
      const char *key = confit_toml_table_key_at(root, root_index);
      if (strcmp(key, kind == CONFIT_V2_INPUT_KIND_PROFILE ? "profile" : "target") !=
              0 &&
          strcmp(key, "values") != 0 && strcmp(key, "unset") != 0 &&
          !(kind == CONFIT_V2_INPUT_KIND_TARGET &&
            (strcmp(key, "build") == 0 || strcmp(key, "support") == 0 ||
             strcmp(key, "machine") == 0))) {
        status = CONFIT_ERR_SCHEMA;
        confit_v2_ledger_diagnostic(
            path, confit_toml_value_line(root),
            confit_toml_value_column(root), CONFIT_ERR_SCHEMA,
            kUnknownInputField, diagnostic);
        break;
      }
    }
  }
fail:
  confit_toml_document_free(document);
  if (status != CONFIT_OK) {
    if (diagnostic != 0 && diagnostic->status == CONFIT_OK) {
      confit_v2_ledger_diagnostic(path, 0U, 0U, status,
                                  status == CONFIT_ERR_INTERNAL
                                      ? kAllocationFailed
                                      : kInvalidInputDocument,
                                  diagnostic);
    }
    /*
     * out_document->path는 아래 clear에서 해제된다. Diagnostic은 borrowed
     * pointer이므로 실패 위치가 document-owned copy를 가리키면 CLI가 반환 뒤
     * use-after-free 문자열을 출력한다. 호출자가 보유한 canonical path로 다시
     * 결속한 다음 document storage를 해제한다.
     */
    if (diagnostic != 0 && diagnostic->path == out_document->path) {
      diagnostic->path = path;
    }
    confit_v2_input_document_clear(out_document);
  }
  return status;
}

static int confit_v2_input_compare_string_ptrs(const void *left, const void *right) {
  const char *const *a = (const char *const *)left;
  const char *const *b = (const char *const *)right;
  return strcmp(*a, *b);
}

ConfitStatus confit_v2_input_collect_component_roots(
    const ConfitV2CompiledStructure *compiled, const char *profile_name,
    const char *target_name, char ***out_roots, size_t *out_count,
    ConfitDiagnostic *diagnostic) {
  ConfitV2InputCatalog profiles;
  ConfitV2InputCatalog targets;
  const ConfitV2InputDocument **profile_chain = 0;
  const ConfitV2InputDocument **target_chain = 0;
  size_t profile_count = 0U;
  size_t target_count = 0U;
  ConfitV2StringList roots;
  ConfitStatus status;
  size_t chain_index;

  if (compiled == 0 || out_roots == 0 || out_count == 0) return CONFIT_ERR_INVALID_ARGUMENT;
  *out_roots = 0;
  *out_count = 0U;
  memset(&profiles, 0, sizeof(profiles));
  memset(&targets, 0, sizeof(targets));
  memset(&roots, 0, sizeof(roots));
  status = confit_v2_input_catalog_load(compiled, CONFIT_V2_INPUT_KIND_PROFILE,
                                         &profiles, diagnostic);
  if (status == CONFIT_OK) status = confit_v2_input_catalog_load(
      compiled, CONFIT_V2_INPUT_KIND_TARGET, &targets, diagnostic);
  if (status == CONFIT_OK && target_name != 0) status =
      confit_v2_input_catalog_build_chain(&targets, target_name, &target_chain,
                                          &target_count, diagnostic);
  if (status == CONFIT_OK && profile_name != 0) status =
      confit_v2_input_catalog_build_chain(&profiles, profile_name, &profile_chain,
                                          &profile_count, diagnostic);
  for (chain_index = 0U; status == CONFIT_OK && chain_index < target_count;
       ++chain_index) {
    size_t root_index;
    for (root_index = 0U; status == CONFIT_OK &&
                           root_index < target_chain[chain_index]->root_components.count;
         ++root_index) status = confit_v2_input_root_list_append(
             &roots, target_chain[chain_index]->root_components.items[root_index]);
  }
  for (chain_index = 0U; status == CONFIT_OK && chain_index < profile_count;
       ++chain_index) {
    size_t root_index;
    for (root_index = 0U; status == CONFIT_OK &&
                           root_index < profile_chain[chain_index]->root_components.count;
         ++root_index) status = confit_v2_input_root_list_append(
             &roots, profile_chain[chain_index]->root_components.items[root_index]);
  }
  if (status == CONFIT_OK && roots.count > 1U) {
    qsort(roots.items, roots.count, sizeof(*roots.items),
          confit_v2_input_compare_string_ptrs);
  }
  free(profile_chain);
  free(target_chain);
  confit_v2_input_catalog_clear(&profiles);
  confit_v2_input_catalog_clear(&targets);
  if (status != CONFIT_OK) {
    confit_v2_input_root_list_clear(&roots);
    return status;
  }
  *out_roots = roots.items;
  *out_count = roots.count;
  return CONFIT_OK;
}

ConfitStatus confit_v2_snapshot_collect_component_roots(
    const ConfitV2CompiledStructure *compiled, const ConfitV2Snapshot *snapshot,
    char ***out_roots, size_t *out_count, ConfitDiagnostic *diagnostic) {
  if (snapshot == 0) return CONFIT_ERR_INVALID_ARGUMENT;
  return confit_v2_input_collect_component_roots(
      compiled, confit_v2_snapshot_profile_name(snapshot),
      confit_v2_snapshot_target_name(snapshot), out_roots, out_count, diagnostic);
}

ConfitStatus confit_v2_input_collect_component_capability_requests(
    const ConfitV2CompiledStructure *compiled, const char *profile_name,
    const char *target_name, char ***out_required, size_t *out_required_count,
    char ***out_optional, size_t *out_optional_count, ConfitDiagnostic *diagnostic) {
  ConfitV2InputCatalog profiles;
  ConfitV2InputCatalog targets;
  const ConfitV2InputDocument **profile_chain = 0;
  const ConfitV2InputDocument **target_chain = 0;
  size_t profile_count = 0U;
  size_t target_count = 0U;
  ConfitV2StringList required;
  ConfitV2StringList optional;
  ConfitStatus status;
  size_t chain_index;

  if (compiled == 0 || out_required == 0 || out_required_count == 0 ||
      out_optional == 0 || out_optional_count == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_required = 0;
  *out_required_count = 0U;
  *out_optional = 0;
  *out_optional_count = 0U;
  memset(&profiles, 0, sizeof(profiles));
  memset(&targets, 0, sizeof(targets));
  memset(&required, 0, sizeof(required));
  memset(&optional, 0, sizeof(optional));
  status = confit_v2_input_catalog_load(compiled, CONFIT_V2_INPUT_KIND_PROFILE,
                                         &profiles, diagnostic);
  if (status == CONFIT_OK) status = confit_v2_input_catalog_load(
      compiled, CONFIT_V2_INPUT_KIND_TARGET, &targets, diagnostic);
  if (status == CONFIT_OK && target_name != 0) status =
      confit_v2_input_catalog_build_chain(&targets, target_name, &target_chain,
                                          &target_count, diagnostic);
  if (status == CONFIT_OK && profile_name != 0) status =
      confit_v2_input_catalog_build_chain(&profiles, profile_name, &profile_chain,
                                          &profile_count, diagnostic);
  for (chain_index = 0U; status == CONFIT_OK && chain_index < target_count;
       ++chain_index) {
    size_t item_index;
    for (item_index = 0U; status == CONFIT_OK &&
                         item_index < target_chain[chain_index]->required_capabilities.count;
         ++item_index) {
      status = confit_v2_input_root_list_append(
          &required, target_chain[chain_index]->required_capabilities.items[item_index]);
    }
    for (item_index = 0U; status == CONFIT_OK &&
                         item_index < target_chain[chain_index]->optional_capabilities.count;
         ++item_index) {
      status = confit_v2_input_root_list_append(
          &optional, target_chain[chain_index]->optional_capabilities.items[item_index]);
    }
  }
  for (chain_index = 0U; status == CONFIT_OK && chain_index < profile_count;
       ++chain_index) {
    size_t item_index;
    for (item_index = 0U; status == CONFIT_OK &&
                         item_index < profile_chain[chain_index]->required_capabilities.count;
         ++item_index) {
      status = confit_v2_input_root_list_append(
          &required, profile_chain[chain_index]->required_capabilities.items[item_index]);
    }
    for (item_index = 0U; status == CONFIT_OK &&
                         item_index < profile_chain[chain_index]->optional_capabilities.count;
         ++item_index) {
      status = confit_v2_input_root_list_append(
          &optional, profile_chain[chain_index]->optional_capabilities.items[item_index]);
    }
  }
  if (status == CONFIT_OK && required.count > 1U) {
    qsort(required.items, required.count, sizeof(*required.items),
          confit_v2_input_compare_string_ptrs);
  }
  if (status == CONFIT_OK && optional.count > 1U) {
    qsort(optional.items, optional.count, sizeof(*optional.items),
          confit_v2_input_compare_string_ptrs);
  }
  free(profile_chain);
  free(target_chain);
  confit_v2_input_catalog_clear(&profiles);
  confit_v2_input_catalog_clear(&targets);
  if (status != CONFIT_OK) {
    confit_v2_input_root_list_clear(&required);
    confit_v2_input_root_list_clear(&optional);
    return status;
  }
  *out_required = required.items;
  *out_required_count = required.count;
  *out_optional = optional.items;
  *out_optional_count = optional.count;
  return CONFIT_OK;
}

ConfitStatus confit_v2_snapshot_collect_component_capability_requests(
    const ConfitV2CompiledStructure *compiled, const ConfitV2Snapshot *snapshot,
    char ***out_required, size_t *out_required_count, char ***out_optional,
    size_t *out_optional_count, ConfitDiagnostic *diagnostic) {
  if (snapshot == 0) return CONFIT_ERR_INVALID_ARGUMENT;
  return confit_v2_input_collect_component_capability_requests(
      compiled, confit_v2_snapshot_profile_name(snapshot),
      confit_v2_snapshot_target_name(snapshot), out_required, out_required_count,
      out_optional, out_optional_count, diagnostic);
}

ConfitStatus confit_v2_input_catalog_load(
    const ConfitV2CompiledStructure *compiled, ConfitV2InputKind kind,
    ConfitV2InputCatalog *out_catalog, ConfitDiagnostic *diagnostic) {
  const ConfitV2LinkedProject *linked;
  const ConfitV2Project *project;
  const ConfitV2StringList *directories;
  size_t directory_index;

  if (compiled == 0 || out_catalog == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  memset(out_catalog, 0, sizeof(*out_catalog));
  out_catalog->kind = kind;
  linked = confit_v2_compiled_structure_source(compiled);
  project = confit_v2_linked_project_source(linked);
  directories = kind == CONFIT_V2_INPUT_KIND_PROFILE ? &project->profile_dirs
                                                       : &project->target_dirs;
  for (directory_index = 0U; directory_index < directories->count;
       ++directory_index) {
    char directory[4096];
    char **paths = 0;
    size_t path_count = 0U;
    size_t path_index;
    ConfitStatus status = confit_host_path_join(
        directory, sizeof(directory), project->config_root,
        directories->items[directory_index], diagnostic);
    if (status != CONFIT_OK) {
      confit_v2_input_catalog_clear(out_catalog);
      return status;
    }
    status = confit_host_list_toml_files(directory, &paths, &path_count,
                                         diagnostic);
    if (status != CONFIT_OK) {
      confit_v2_input_catalog_clear(out_catalog);
      return status;
    }
    for (path_index = 0U; path_index < path_count; ++path_index) {
      ConfitV2InputDocument parsed;
      ConfitV2InputDocument *grown;
      size_t existing;

      status = confit_v2_input_parse_document(linked, kind, paths[path_index],
                                               &parsed, diagnostic);
      if (status != CONFIT_OK) {
        confit_host_string_list_free(paths, path_count);
        confit_v2_input_catalog_clear(out_catalog);
        return status;
      }
      for (existing = 0U; existing < out_catalog->document_count; ++existing) {
        if (strcmp(out_catalog->documents[existing].name, parsed.name) == 0) {
          confit_v2_ledger_diagnostic(parsed.path, 0U, 0U, CONFIT_ERR_SCHEMA,
                                      kDuplicateInputName, diagnostic);
          confit_v2_input_document_clear(&parsed);
          confit_host_string_list_free(paths, path_count);
          confit_v2_input_catalog_clear(out_catalog);
          return CONFIT_ERR_SCHEMA;
        }
      }
      grown = (ConfitV2InputDocument *)realloc(
          out_catalog->documents,
          (out_catalog->document_count + 1U) * sizeof(*out_catalog->documents));
      if (grown == 0) {
        confit_v2_input_document_clear(&parsed);
        confit_host_string_list_free(paths, path_count);
        confit_v2_input_catalog_clear(out_catalog);
        return CONFIT_ERR_INTERNAL;
      }
      out_catalog->documents = grown;
      out_catalog->documents[out_catalog->document_count] = parsed;
      out_catalog->document_count += 1U;
    }
    confit_host_string_list_free(paths, path_count);
  }
  return CONFIT_OK;
}

ConfitStatus confit_v2_input_catalog_build_chain(
    const ConfitV2InputCatalog *catalog, const char *leaf_name,
    const ConfitV2InputDocument ***out_chain, size_t *out_count,
    ConfitDiagnostic *diagnostic) {
  const ConfitV2InputDocument **reversed;
  const ConfitV2InputDocument **chain;
  const ConfitV2InputDocument *cursor;
  size_t count = 0U;
  size_t index;

  if (catalog == 0 || out_chain == 0 || out_count == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_chain = 0;
  *out_count = 0U;
  if (leaf_name == 0) {
    return CONFIT_OK;
  }
  cursor = confit_v2_input_catalog_find(catalog, leaf_name);
  if (cursor == 0) {
    confit_v2_ledger_diagnostic(leaf_name, 0U, 0U, CONFIT_ERR_SCHEMA,
                                kUnknownInputName, diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  reversed = (const ConfitV2InputDocument **)calloc(
      catalog->document_count, sizeof(*reversed));
  if (reversed == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  while (cursor != 0) {
    for (index = 0U; index < count; ++index) {
      if (cursor == reversed[index]) {
        confit_v2_ledger_diagnostic(cursor->path, 0U, 0U, CONFIT_ERR_SCHEMA,
                                    kInputInheritanceCycle, diagnostic);
        free(reversed);
        return CONFIT_ERR_SCHEMA;
      }
    }
    if (count == catalog->document_count) {
      free(reversed);
      return CONFIT_ERR_INTERNAL;
    }
    reversed[count] = cursor;
    count += 1U;
    cursor = cursor->base != 0
                 ? confit_v2_input_catalog_find(catalog, cursor->base)
                 : 0;
    if (cursor == 0 && reversed[count - 1U]->base != 0) {
      confit_v2_ledger_diagnostic(reversed[count - 1U]->path, 0U, 0U,
                                  CONFIT_ERR_SCHEMA, kUnknownInputName,
                                  diagnostic);
      free(reversed);
      return CONFIT_ERR_SCHEMA;
    }
  }
  chain = (const ConfitV2InputDocument **)calloc(count, sizeof(*chain));
  if (chain == 0) {
    free(reversed);
    return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < count; ++index) {
    chain[index] = reversed[count - index - 1U];
  }
  free(reversed);
  *out_chain = chain;
  *out_count = count;
  return CONFIT_OK;
}

void confit_v2_input_chain_free(const ConfitV2InputDocument **chain) {
  free(chain);
}
