#include "confit/toml.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "confit/limits.h"
#include "toml_internal.h"
#include "tomlc17.h"

struct ConfitTomlDocument {
  const char *source_text;
  char *owned_source_text;
  size_t source_size;
  toml_result_t result;
};

static const char kConfitTomlInvalidArgument[] = "invalid TOML argument";
static const char kConfitTomlInvalidUtf8[] = "TOML source is not valid UTF-8";
static const char kConfitTomlEmbeddedNul[] =
    "TOML source contains an embedded NUL byte";
static const char kConfitTomlParseFailed[] = "tomlc17 rejected TOML input";
static const char kConfitTomlOutOfMemory[] = "failed to allocate TOML document";
static const char kConfitTomlTooLarge[] = "TOML source exceeds parser size limit";

static const toml_datum_t *confit_toml_as_datum(
    const ConfitTomlValue *value) {
  return (const toml_datum_t *)(const void *)value;
}

static const ConfitTomlValue *
confit_toml_as_value(const toml_datum_t *datum) {
  return (const ConfitTomlValue *)(const void *)datum;
}

static char *confit_toml_copy_text(const char *text, size_t text_size) {
  char *copy;

  copy = (char *)malloc(text_size + 1U);
  if (copy == 0) {
    return 0;
  }
  if (text_size > 0U) {
    memcpy(copy, text, text_size);
  }
  copy[text_size] = '\0';
  return copy;
}

static int confit_toml_find_nul(const char *text, size_t text_size,
                                size_t *out_line, size_t *out_column) {
  size_t index;
  size_t line = 1U;
  size_t column = 1U;
  for (index = 0U; index < text_size; ++index) {
    if (text[index] == '\0') {
      if (out_line != 0) {
        *out_line = line;
      }
      if (out_column != 0) {
        *out_column = column;
      }
      return 1;
    }
    if (text[index] == '\n') {
      line += 1U;
      column = 1U;
    } else {
      column += 1U;
    }
  }
  return 0;
}

static int confit_toml_utf8_continuation(unsigned char byte) {
  return (byte & 0xC0U) == 0x80U;
}

static int confit_toml_validate_utf8(const char *text, size_t text_size,
                                         size_t *out_line,
                                         size_t *out_column) {
  const unsigned char *bytes;
  size_t column;
  size_t index;
  size_t line;

  bytes = (const unsigned char *)text;
  column = 1U;
  index = 0U;
  line = 1U;
  while (index < text_size) {
    const unsigned char first = bytes[index];
    size_t width;

    if (first < 0x80U) {
      width = 1U;
    } else if (first >= 0xC2U && first <= 0xDFU) {
      width = 2U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
      width = 3U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
      width = 4U;
    } else {
      break;
    }

    if (index + width > text_size ||
        (width >= 2U && !confit_toml_utf8_continuation(bytes[index + 1U])) ||
        (width >= 3U && !confit_toml_utf8_continuation(bytes[index + 2U])) ||
        (width >= 4U && !confit_toml_utf8_continuation(bytes[index + 3U])) ||
        (width == 3U && first == 0xE0U && bytes[index + 1U] < 0xA0U) ||
        (width == 3U && first == 0xEDU && bytes[index + 1U] > 0x9FU) ||
        (width == 4U && first == 0xF0U && bytes[index + 1U] < 0x90U) ||
        (width == 4U && first == 0xF4U && bytes[index + 1U] > 0x8FU)) {
      break;
    }

    if (first == '\n') {
      line += 1U;
      column = 1U;
    } else {
      column += 1U;
    }
    index += width;
  }

  if (index == text_size) {
    return 1;
  }

  if (out_line != 0) {
    *out_line = line;
  }
  if (out_column != 0) {
    *out_column = column;
  }
  return 0;
}

static size_t confit_toml_error_line(const char *message) {
  const char *cursor;

  if (message == 0) {
    return 0U;
  }
  cursor = strstr(message, "line ");
  if (cursor == 0) {
    return 0U;
  }
  cursor += strlen("line ");
  if (!isdigit((unsigned char)*cursor)) {
    return 0U;
  }

  {
    size_t line = 0U;
    while (isdigit((unsigned char)*cursor)) {
      if (line > (SIZE_MAX - 9U) / 10U) {
        return 0U;
      }
      line = line * 10U + (size_t)(*cursor - '0');
      cursor += 1;
    }
    return line;
  }
}

/* tomlc17의 parse error transport는 line만 제공한다. 0-column 진단을 외부
 * schema checker로 흘리지 않도록 그 line의 첫 non-whitespace byte를 bounded하게
 * 계산한다. EOF line처럼 source에 없는 line도 1-based column 1로 봉인한다. */
static size_t confit_toml_error_column(const char *text, size_t text_size,
                                          size_t error_line) {
  size_t index = 0U;
  size_t line = 1U;
  if (text == 0 || error_line == 0U) return 1U;
  while (index < text_size && line < error_line) {
    if (text[index++] == '\n') ++line;
  }
  if (line != error_line) return 1U;
  {
    size_t column = 1U;
    while (index < text_size && text[index] != '\n' &&
           (text[index] == ' ' || text[index] == '\t' || text[index] == '\r')) {
      ++index;
      ++column;
    }
    return column;
  }
}

static ConfitTomlValueType
confit_toml_map_type(toml_type_t type) {
  switch (type) {
  case TOML_STRING:
    return CONFIT_TOML_VALUE_STRING;
  case TOML_INT64:
    return CONFIT_TOML_VALUE_INT64;
  case TOML_FP64:
    return CONFIT_TOML_VALUE_FLOAT64;
  case TOML_BOOLEAN:
    return CONFIT_TOML_VALUE_BOOL;
  case TOML_DATE:
    return CONFIT_TOML_VALUE_DATE;
  case TOML_TIME:
    return CONFIT_TOML_VALUE_TIME;
  case TOML_DATETIME:
    return CONFIT_TOML_VALUE_DATETIME;
  case TOML_DATETIMETZ:
    return CONFIT_TOML_VALUE_DATETIME_TZ;
  case TOML_ARRAY:
    return CONFIT_TOML_VALUE_ARRAY;
  case TOML_TABLE:
    return CONFIT_TOML_VALUE_TABLE;
  case TOML_UNKNOWN:
  default:
    return CONFIT_TOML_VALUE_UNKNOWN;
  }
}

static ConfitStatus confit_toml_parse_prepared(
    const char *source_name, const char *text, size_t text_size,
    char *owned_text, ConfitTomlDocument **out_document,
    ConfitDiagnostic *diagnostic) {
  ConfitTomlDocument *document;
  size_t column;
  size_t line;
  toml_result_t result;

  if (out_document == 0 || text == 0) {
    free(owned_text);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_USAGE, source_name,
                          0U, 0U, kConfitTomlInvalidArgument);
    return CONFIT_ERR_USAGE;
  }
  *out_document = 0;
  if (text_size > CONFIT_LIMIT_TOML_FILE_BYTES ||
      text_size > (size_t)INT_MAX) {
    free(owned_text);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_VALIDATION, source_name, 0U, 0U,
                          kConfitTomlTooLarge);
    return CONFIT_ERR_VALIDATION;
  }

  if (confit_toml_find_nul(text, text_size, &line, &column)) {
    free(owned_text);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_VALIDATION, source_name, line,
                          column, kConfitTomlEmbeddedNul);
    return CONFIT_ERR_VALIDATION;
  }

  line = 0U;
  column = 0U;
  if (!confit_toml_validate_utf8(text, text_size, &line, &column)) {
    free(owned_text);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_VALIDATION, source_name, line,
                          column, kConfitTomlInvalidUtf8);
    return CONFIT_ERR_VALIDATION;
  }

  result = toml_parse_named(text, (int)text_size, source_name);
  if (!result.ok) {
    line = confit_toml_error_line(result.errmsg);
    column = confit_toml_error_column(text, text_size, line);
    toml_free(result);
    free(owned_text);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_VALIDATION, source_name, line, column,
                          kConfitTomlParseFailed);
    return CONFIT_ERR_VALIDATION;
  }

  document = (ConfitTomlDocument *)calloc(1U, sizeof(*document));
  if (document == 0) {
    toml_free(result);
    free(owned_text);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, source_name, 0U,
                          0U, kConfitTomlOutOfMemory);
    return CONFIT_ERR_INTERNAL;
  }

  document->source_text = text;
  document->owned_source_text = owned_text;
  document->source_size = text_size;
  document->result = result;
  *out_document = document;
  return CONFIT_OK;
}

ConfitStatus confit_toml_parse_text(const char *source_name,
                                    const char *text, size_t text_size,
                                    ConfitTomlDocument **out_document,
                                    ConfitDiagnostic *diagnostic) {
  char *owned_text;
  if (out_document == 0 || text == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_USAGE, source_name, 0U, 0U,
                          kConfitTomlInvalidArgument);
    return CONFIT_ERR_USAGE;
  }
  *out_document = 0;
  if (text_size > CONFIT_LIMIT_TOML_FILE_BYTES ||
      text_size > (size_t)INT_MAX) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_VALIDATION, source_name, 0U,
                          0U, kConfitTomlTooLarge);
    return CONFIT_ERR_VALIDATION;
  }
  owned_text = confit_toml_copy_text(text, text_size);
  if (owned_text == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, source_name, 0U, 0U,
                          kConfitTomlOutOfMemory);
    return CONFIT_ERR_INTERNAL;
  }
  return confit_toml_parse_prepared(source_name, owned_text, text_size,
                                    owned_text, out_document, diagnostic);
}

ConfitStatus confit_toml_parse_borrowed(
    const char *source_name, const char *text, size_t text_size,
    ConfitTomlDocument **out_document, ConfitDiagnostic *diagnostic) {
  return confit_toml_parse_prepared(source_name, text, text_size, 0,
                                    out_document, diagnostic);
}

void confit_toml_document_free(ConfitTomlDocument *document) {
  if (document == 0) {
    return;
  }
  toml_free(document->result);
  free(document->owned_source_text);
  free(document);
}

const char *
confit_toml_document_source_text(const ConfitTomlDocument *document) {
  return document != 0 ? document->source_text : 0;
}

size_t confit_toml_document_source_size(
    const ConfitTomlDocument *document) {
  return document != 0 ? document->source_size : 0U;
}

const ConfitTomlValue *
confit_toml_document_root(const ConfitTomlDocument *document) {
  return document != 0 ? confit_toml_as_value(&document->result.toptab)
                       : 0;
}

ConfitTomlValueType
confit_toml_value_type(const ConfitTomlValue *value) {
  const toml_datum_t *datum = confit_toml_as_datum(value);
  return datum != 0 ? confit_toml_map_type(datum->type)
                    : CONFIT_TOML_VALUE_UNKNOWN;
}

size_t confit_toml_value_line(const ConfitTomlValue *value) {
  const toml_datum_t *datum = confit_toml_as_datum(value);
  return datum != 0 && datum->lineno > 0 ? (size_t)datum->lineno : 0U;
}

size_t confit_toml_value_column(const ConfitTomlValue *value) {
  const toml_datum_t *datum = confit_toml_as_datum(value);
  return datum != 0 && datum->colno > 0 ? (size_t)datum->colno : 0U;
}

const char *confit_toml_value_source(const ConfitTomlValue *value) {
  const toml_datum_t *datum = confit_toml_as_datum(value);
  return datum != 0 ? datum->source : 0;
}

int confit_toml_value_string(const ConfitTomlValue *value,
                                const char **out_text, size_t *out_size) {
  const toml_datum_t *datum = confit_toml_as_datum(value);

  if (datum == 0 || datum->type != TOML_STRING || out_text == 0 ||
      out_size == 0 || datum->u.str.len < 0) {
    return 0;
  }
  *out_text = datum->u.str.ptr;
  *out_size = (size_t)datum->u.str.len;
  return 1;
}

int confit_toml_value_int64(const ConfitTomlValue *value,
                                int64_t *out_value) {
  const toml_datum_t *datum = confit_toml_as_datum(value);

  if (datum == 0 || datum->type != TOML_INT64 || out_value == 0) {
    return 0;
  }
  *out_value = datum->u.int64;
  return 1;
}

int confit_toml_value_float64(const ConfitTomlValue *value,
                                  double *out_value) {
  const toml_datum_t *datum = confit_toml_as_datum(value);

  if (datum == 0 || datum->type != TOML_FP64 || out_value == 0) {
    return 0;
  }
  *out_value = datum->u.fp64;
  return 1;
}

int confit_toml_value_bool(const ConfitTomlValue *value,
                               int *out_value) {
  const toml_datum_t *datum = confit_toml_as_datum(value);

  if (datum == 0 || datum->type != TOML_BOOLEAN || out_value == 0) {
    return 0;
  }
  *out_value = datum->u.boolean ? 1 : 0;
  return 1;
}

size_t confit_toml_table_size(const ConfitTomlValue *table) {
  const toml_datum_t *datum = confit_toml_as_datum(table);
  return datum != 0 && datum->type == TOML_TABLE && datum->u.tab.size > 0
             ? (size_t)datum->u.tab.size
             : 0U;
}

const char *confit_toml_table_key_at(const ConfitTomlValue *table,
                                         size_t index) {
  const toml_datum_t *datum = confit_toml_as_datum(table);

  if (datum == 0 || datum->type != TOML_TABLE || datum->u.tab.size < 0 ||
      index >= (size_t)datum->u.tab.size) {
    return 0;
  }
  return datum->u.tab.key[index];
}

const ConfitTomlValue *
confit_toml_table_value_at(const ConfitTomlValue *table, size_t index) {
  const toml_datum_t *datum = confit_toml_as_datum(table);

  if (datum == 0 || datum->type != TOML_TABLE || datum->u.tab.size < 0 ||
      index >= (size_t)datum->u.tab.size) {
    return 0;
  }
  return confit_toml_as_value(&datum->u.tab.value[index]);
}

const ConfitTomlValue *
confit_toml_table_find(const ConfitTomlValue *table, const char *key) {
  size_t index;
  size_t size;

  if (key == 0) {
    return 0;
  }
  size = confit_toml_table_size(table);
  for (index = 0U; index < size; ++index) {
    const char *candidate = confit_toml_table_key_at(table, index);
    if (candidate != 0 && strcmp(candidate, key) == 0) {
      return confit_toml_table_value_at(table, index);
    }
  }
  return 0;
}

size_t confit_toml_array_size(const ConfitTomlValue *array) {
  const toml_datum_t *datum = confit_toml_as_datum(array);
  return datum != 0 && datum->type == TOML_ARRAY && datum->u.arr.size > 0
             ? (size_t)datum->u.arr.size
             : 0U;
}

const ConfitTomlValue *
confit_toml_array_at(const ConfitTomlValue *array, size_t index) {
  const toml_datum_t *datum = confit_toml_as_datum(array);

  if (datum == 0 || datum->type != TOML_ARRAY || datum->u.arr.size < 0 ||
      index >= (size_t)datum->u.arr.size) {
    return 0;
  }
  return confit_toml_as_value(&datum->u.arr.elem[index]);
}
