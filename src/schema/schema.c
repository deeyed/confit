#include "confit/schema.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "confit/host.h"
#include "confit/parser.h"

typedef enum ConfitSchemaTableKind {
  CONFIT_SCHEMA_TABLE_NONE = 0,
  CONFIT_SCHEMA_TABLE_PROJECT = 1,
  CONFIT_SCHEMA_TABLE_OPTION = 2,
} ConfitSchemaTableKind;

typedef struct ConfitSchemaImports {
  char **items;
  size_t count;
} ConfitSchemaImports;

typedef struct ConfitSchemaOptionState {
  ConfitOption *option;
  int saw_type;
  size_t type_line;
  char *id;
} ConfitSchemaOptionState;

typedef struct ConfitSchemaLine {
  const char *begin;
  size_t length;
  size_t line;
} ConfitSchemaLine;

static char *confit_schema_copy_bytes(const char *text, size_t size) {
  char *copy;

  copy = (char *)malloc(size + 1U);
  if (copy == 0) {
    return 0;
  }

  if (size > 0U) {
    memcpy(copy, text, size);
  }
  copy[size] = '\0';
  return copy;
}

static char *confit_schema_copy_string(const char *text) {
  return text != 0 ? confit_schema_copy_bytes(text, strlen(text)) : 0;
}

static void confit_schema_set_error(ConfitDiagnostic *diagnostic,
                                    ConfitStatus status, const char *path,
                                    size_t line, size_t column,
                                    const char *message) {
  confit_diagnostic_set(diagnostic, status, path, line, column, message);
}

static int confit_schema_is_space(char value) {
  return value == ' ' || value == '\t';
}

static size_t confit_schema_trim_left(const char *text, size_t length) {
  size_t index;

  index = 0U;
  while (index < length && confit_schema_is_space(text[index])) {
    index += 1U;
  }

  return index;
}

static size_t confit_schema_trim_right(const char *text, size_t begin,
                                       size_t end) {
  while (end > begin && confit_schema_is_space(text[end - 1U])) {
    end -= 1U;
  }

  return end;
}

static size_t confit_schema_strip_comment(const char *text, size_t length) {
  size_t index;
  int in_string;
  char quote;

  in_string = 0;
  quote = '\0';
  for (index = 0U; index < length; ++index) {
    const char value = text[index];

    if (in_string != 0) {
      if (value == quote) {
        in_string = 0;
        quote = '\0';
      } else if (quote == '"' && value == '\\') {
        index += 1U;
      }
      continue;
    }

    if (value == '"' || value == '\'') {
      in_string = 1;
      quote = value;
      continue;
    }
    if (value == '#') {
      return index;
    }
  }

  return length;
}

static int confit_schema_next_line(const char *text, size_t text_size,
                                   size_t *offset, size_t *line_number,
                                   ConfitSchemaLine *out_line) {
  size_t begin;
  size_t length;

  if (*offset >= text_size) {
    return 0;
  }

  begin = *offset;
  while (*offset < text_size && text[*offset] != '\n') {
    *offset += 1U;
  }

  length = *offset - begin;
  if (length > 0U && text[begin + length - 1U] == '\r') {
    length -= 1U;
  }

  out_line->begin = text + begin;
  out_line->length = length;
  out_line->line = *line_number;

  if (*offset < text_size && text[*offset] == '\n') {
    *offset += 1U;
  }
  *line_number += 1U;
  return 1;
}

static int confit_schema_find_equals(const char *text, size_t begin,
                                     size_t end, size_t *out_index) {
  size_t index;
  int in_string;
  char quote;

  in_string = 0;
  quote = '\0';
  for (index = begin; index < end; ++index) {
    const char value = text[index];

    if (in_string != 0) {
      if (value == quote) {
        in_string = 0;
        quote = '\0';
      } else if (quote == '"' && value == '\\') {
        index += 1U;
      }
      continue;
    }

    if (value == '"' || value == '\'') {
      in_string = 1;
      quote = value;
      continue;
    }
    if (value == '=') {
      *out_index = index;
      return 1;
    }
  }

  return 0;
}

static int confit_schema_string_equal(const char *text, size_t begin,
                                      size_t end, const char *literal) {
  const size_t literal_size = strlen(literal);
  return end - begin == literal_size &&
         strncmp(text + begin, literal, literal_size) == 0;
}

static char *confit_schema_parse_bare_token(const char *text, size_t begin,
                                            size_t end) {
  begin = confit_schema_trim_left(text + begin, end - begin) + begin;
  end = confit_schema_trim_right(text, begin, end);
  return begin < end ? confit_schema_copy_bytes(text + begin, end - begin) : 0;
}

static char *confit_schema_parse_quoted_string(const char *text, size_t begin,
                                               size_t end, const char *path,
                                               size_t line,
                                               ConfitDiagnostic *diagnostic) {
  char *out;
  size_t out_size;
  size_t index;
  char quote;

  begin = confit_schema_trim_left(text + begin, end - begin) + begin;
  end = confit_schema_trim_right(text, begin, end);
  if (end < begin + 2U || (text[begin] != '"' && text[begin] != '\'')) {
    confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                            begin + 1U, "expected quoted string");
    return 0;
  }

  quote = text[begin];
  if (text[end - 1U] != quote) {
    confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line, end,
                            "unterminated string");
    return 0;
  }

  out = (char *)malloc(end - begin);
  if (out == 0) {
    confit_schema_set_error(diagnostic, CONFIT_ERR_INTERNAL, path, line,
                            begin + 1U, "failed to allocate string");
    return 0;
  }

  out_size = 0U;
  for (index = begin + 1U; index + 1U < end; ++index) {
    if (quote == '"' && text[index] == '\\') {
      index += 1U;
      if (index + 1U >= end) {
        free(out);
        confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                                index + 1U, "invalid string escape");
        return 0;
      }
      switch (text[index]) {
      case 'n':
        out[out_size] = '\n';
        break;
      case 't':
        out[out_size] = '\t';
        break;
      case 'r':
        out[out_size] = '\r';
        break;
      case '"':
      case '\\':
        out[out_size] = text[index];
        break;
      default:
        free(out);
        confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                                index + 1U, "unsupported string escape");
        return 0;
      }
    } else {
      out[out_size] = text[index];
    }
    out_size += 1U;
  }

  out[out_size] = '\0';
  return out;
}

static int confit_schema_parse_uint(const char *text, size_t begin, size_t end,
                                    uint64_t *out_value) {
  uint64_t value;
  size_t index;

  begin = confit_schema_trim_left(text + begin, end - begin) + begin;
  end = confit_schema_trim_right(text, begin, end);
  if (begin >= end) {
    return 0;
  }

  value = 0U;
  for (index = begin; index < end; ++index) {
    if (!isdigit((unsigned char)text[index])) {
      return 0;
    }
    value = value * 10U + (uint64_t)(text[index] - '0');
  }

  *out_value = value;
  return 1;
}

static int confit_schema_parse_hex_uint(const char *text, size_t begin,
                                        size_t end, uint64_t *out_value) {
  uint64_t value;
  size_t index;

  begin = confit_schema_trim_left(text + begin, end - begin) + begin;
  end = confit_schema_trim_right(text, begin, end);
  if (end < begin + 3U || text[begin] != '0' ||
      (text[begin + 1U] != 'x' && text[begin + 1U] != 'X')) {
    return confit_schema_parse_uint(text, begin, end, out_value);
  }

  value = 0U;
  for (index = begin + 2U; index < end; ++index) {
    const char ch = text[index];
    unsigned digit;

    if (ch >= '0' && ch <= '9') {
      digit = (unsigned)(ch - '0');
    } else if (ch >= 'a' && ch <= 'f') {
      digit = (unsigned)(ch - 'a') + 10U;
    } else if (ch >= 'A' && ch <= 'F') {
      digit = (unsigned)(ch - 'A') + 10U;
    } else {
      return 0;
    }
    value = value * 16U + (uint64_t)digit;
  }

  *out_value = value;
  return 1;
}

static int confit_schema_parse_int(const char *text, size_t begin, size_t end,
                                   int64_t *out_value) {
  int negative;
  uint64_t magnitude;

  begin = confit_schema_trim_left(text + begin, end - begin) + begin;
  end = confit_schema_trim_right(text, begin, end);
  negative = 0;
  if (begin < end && text[begin] == '-') {
    negative = 1;
    begin += 1U;
  }

  if (!confit_schema_parse_uint(text, begin, end, &magnitude)) {
    return 0;
  }

  *out_value = negative ? -(int64_t)magnitude : (int64_t)magnitude;
  return 1;
}

static int confit_schema_parse_float(const char *text, size_t begin,
                                     size_t end, double *out_value) {
  int negative;
  int saw_digit;
  double value;
  double place;
  int exponent_negative;
  int exponent;
  size_t index;

  begin = confit_schema_trim_left(text + begin, end - begin) + begin;
  end = confit_schema_trim_right(text, begin, end);
  if (begin >= end) {
    return 0;
  }

  index = begin;
  negative = 0;
  if (text[index] == '-' || text[index] == '+') {
    negative = text[index] == '-';
    index += 1U;
  }

  value = 0.0;
  saw_digit = 0;
  while (index < end && isdigit((unsigned char)text[index])) {
    if (value > 1.0e307) {
      return 0;
    }
    value = value * 10.0 + (double)(text[index] - '0');
    saw_digit = 1;
    index += 1U;
  }

  if (index < end && text[index] == '.') {
    index += 1U;
    place = 0.1;
    while (index < end && isdigit((unsigned char)text[index])) {
      value += (double)(text[index] - '0') * place;
      place *= 0.1;
      saw_digit = 1;
      index += 1U;
    }
  }

  if (!saw_digit) {
    return 0;
  }

  if (index < end && (text[index] == 'e' || text[index] == 'E')) {
    index += 1U;
    exponent_negative = 0;
    exponent = 0;
    if (index < end && (text[index] == '-' || text[index] == '+')) {
      exponent_negative = text[index] == '-';
      index += 1U;
    }
    if (index >= end || !isdigit((unsigned char)text[index])) {
      return 0;
    }
    while (index < end && isdigit((unsigned char)text[index])) {
      exponent = exponent * 10 + (int)(text[index] - '0');
      if (exponent > 308) {
        return 0;
      }
      index += 1U;
    }
    while (exponent > 0) {
      value = exponent_negative ? value / 10.0 : value * 10.0;
      if (value > 1.0e308 || value < -1.0e308) {
        return 0;
      }
      exponent -= 1;
    }
  }

  if (index != end) {
    return 0;
  }

  *out_value = negative ? -value : value;
  return *out_value == *out_value && *out_value <= 1.0e308 &&
         *out_value >= -1.0e308;
}

static int confit_schema_parse_string_array(const char *text, size_t begin,
                                            size_t end, char ***out_items,
                                            size_t *out_count,
                                            const char *path, size_t line,
                                            ConfitDiagnostic *diagnostic) {
  char **items;
  size_t count;
  size_t index;

  begin = confit_schema_trim_left(text + begin, end - begin) + begin;
  end = confit_schema_trim_right(text, begin, end);
  if (begin >= end || text[begin] != '[' || text[end - 1U] != ']') {
    confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                            begin + 1U, "expected string array");
    return 0;
  }

  items = 0;
  count = 0U;
  index = begin + 1U;
  while (index + 1U < end) {
    size_t value_begin;
    size_t value_end;
    char *value;
    char **new_items;

    while (index + 1U < end &&
           (confit_schema_is_space(text[index]) || text[index] == ',')) {
      index += 1U;
    }
    if (index + 1U >= end) {
      break;
    }

    value_begin = index;
    if (text[index] != '"' && text[index] != '\'') {
      confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                              index + 1U, "expected quoted array item");
      goto fail;
    }
    index += 1U;
    while (index + 1U < end && text[index] != text[value_begin]) {
      if (text[value_begin] == '"' && text[index] == '\\') {
        index += 1U;
      }
      index += 1U;
    }
    if (index + 1U >= end) {
      confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                              value_begin + 1U, "unterminated array string");
      goto fail;
    }
    value_end = index + 1U;
    index += 1U;

    value = confit_schema_parse_quoted_string(text, value_begin, value_end,
                                              path, line, diagnostic);
    if (value == 0) {
      goto fail;
    }

    new_items = (char **)realloc(items, (count + 1U) * sizeof(items[0]));
    if (new_items == 0) {
      free(value);
      confit_schema_set_error(diagnostic, CONFIT_ERR_INTERNAL, path, line,
                              value_begin + 1U,
                              "failed to allocate string array");
      goto fail;
    }
    items = new_items;
    items[count] = value;
    count += 1U;
  }

  *out_items = items;
  *out_count = count;
  return 1;

fail:
  while (count > 0U) {
    count -= 1U;
    free(items[count]);
  }
  free(items);
  return 0;
}

static void confit_schema_string_array_free(char **items, size_t count) {
  size_t index;

  for (index = 0U; index < count; ++index) {
    free(items[index]);
  }
  free(items);
}

static int confit_schema_find_range_comma(const char *text, size_t begin,
                                          size_t end, size_t *out_comma) {
  size_t index;

  for (index = begin; index < end; ++index) {
    if (text[index] == ',') {
      *out_comma = index;
      return 1;
    }
  }
  return 0;
}

static ConfitStatus confit_schema_parse_range_value(
    ConfitOptionType type, const char *text, size_t begin, size_t end,
    ConfitValue *out_value, const char *path, size_t line,
    ConfitDiagnostic *diagnostic) {
  int64_t int_value;
  uint64_t uint_value;
  double float_value;

  confit_value_init(out_value);
  switch (type) {
  case CONFIT_OPTION_TYPE_INT:
    if (!confit_schema_parse_int(text, begin, end, &int_value)) {
      confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                              begin + 1U, "invalid int range value");
      return CONFIT_ERR_SCHEMA;
    }
    confit_value_set_int(out_value, int_value);
    return CONFIT_OK;
  case CONFIT_OPTION_TYPE_UINT:
    if (!confit_schema_parse_uint(text, begin, end, &uint_value)) {
      confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                              begin + 1U, "invalid uint range value");
      return CONFIT_ERR_SCHEMA;
    }
    confit_value_set_uint(out_value, uint_value);
    return CONFIT_OK;
  case CONFIT_OPTION_TYPE_HEX:
    if (!confit_schema_parse_hex_uint(text, begin, end, &uint_value)) {
      confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                              begin + 1U, "invalid hex range value");
      return CONFIT_ERR_SCHEMA;
    }
    confit_value_set_uint(out_value, uint_value);
    return CONFIT_OK;
  case CONFIT_OPTION_TYPE_FLOAT:
    if (!confit_schema_parse_float(text, begin, end, &float_value)) {
      confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                              begin + 1U, "invalid float range value");
      return CONFIT_ERR_SCHEMA;
    }
    confit_value_set_float(out_value, float_value);
    return CONFIT_OK;
  default:
    confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                            begin + 1U,
                            "range is only valid for numeric options");
    return CONFIT_ERR_SCHEMA;
  }
}

static ConfitStatus confit_schema_parse_range(
    ConfitOption *option, const char *line_text, size_t value_begin,
    size_t value_end, const char *path, size_t line,
    ConfitDiagnostic *diagnostic) {
  ConfitValue min_value;
  ConfitValue max_value;
  ConfitStatus status;
  size_t comma;

  value_begin =
      confit_schema_trim_left(line_text + value_begin, value_end - value_begin) +
      value_begin;
  value_end = confit_schema_trim_right(line_text, value_begin, value_end);
  if (value_begin >= value_end || line_text[value_begin] != '[' ||
      line_text[value_end - 1U] != ']') {
    confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                            value_begin + 1U, "expected range array");
    return CONFIT_ERR_SCHEMA;
  }
  if (!confit_schema_find_range_comma(line_text, value_begin + 1U,
                                      value_end - 1U, &comma)) {
    confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                            value_begin + 1U, "range must have two values");
    return CONFIT_ERR_SCHEMA;
  }

  status = confit_schema_parse_range_value(option->type, line_text,
                                           value_begin + 1U, comma, &min_value,
                                           path, line, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_schema_parse_range_value(option->type, line_text, comma + 1U,
                                           value_end - 1U, &max_value, path,
                                           line, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_option_set_range(option, &min_value, &max_value);
  }
  confit_value_clear(&min_value);
  confit_value_clear(&max_value);
  return status;
}

static int confit_schema_parse_table(const char *text, size_t begin, size_t end,
                                     char **out_name) {
  begin = confit_schema_trim_left(text + begin, end - begin) + begin;
  end = confit_schema_trim_right(text, begin, end);
  if (begin + 2U > end || text[begin] != '[' || text[end - 1U] != ']') {
    return 0;
  }
  *out_name = confit_schema_copy_bytes(text + begin + 1U, end - begin - 2U);
  return *out_name != 0;
}

static int confit_schema_parse_option_table_id(const char *table_name,
                                               char **out_id) {
  const char *prefix;
  const char *begin;
  size_t size;

  prefix = "option.\"";
  size = strlen(prefix);
  if (strncmp(table_name, prefix, size) != 0) {
    return 0;
  }
  begin = table_name + size;
  size = strlen(begin);
  if (size < 2U || begin[size - 1U] != '"') {
    return 0;
  }

  *out_id = confit_schema_copy_bytes(begin, size - 1U);
  return *out_id != 0;
}

static int confit_schema_valid_id_char(char value) {
  return isalnum((unsigned char)value) || value == '_' || value == '-';
}

static int confit_schema_validate_option_id(const char *id) {
  size_t index;
  int saw_dot;
  int segment_has_char;

  if (id == 0 || id[0] == '\0' || id[0] == '.' ||
      strcmp(id, "system") == 0 || strncmp(id, "system.", 7U) == 0) {
    return 0;
  }

  saw_dot = 0;
  segment_has_char = 0;
  for (index = 0U; id[index] != '\0'; ++index) {
    if (id[index] == '.') {
      if (!segment_has_char) {
        return 0;
      }
      saw_dot = 1;
      segment_has_char = 0;
      continue;
    }
    if (!confit_schema_valid_id_char(id[index])) {
      return 0;
    }
    segment_has_char = 1;
  }

  return saw_dot && segment_has_char;
}

static ConfitOptionType confit_schema_parse_option_type(const char *value) {
  if (strcmp(value, "bool") == 0) {
    return CONFIT_OPTION_TYPE_BOOL;
  }
  if (strcmp(value, "int") == 0) {
    return CONFIT_OPTION_TYPE_INT;
  }
  if (strcmp(value, "uint") == 0) {
    return CONFIT_OPTION_TYPE_UINT;
  }
  if (strcmp(value, "hex") == 0) {
    return CONFIT_OPTION_TYPE_HEX;
  }
  if (strcmp(value, "string") == 0) {
    return CONFIT_OPTION_TYPE_STRING;
  }
  if (strcmp(value, "enum") == 0) {
    return CONFIT_OPTION_TYPE_ENUM;
  }
  if (strcmp(value, "float") == 0) {
    return CONFIT_OPTION_TYPE_FLOAT;
  }
  if (strcmp(value, "path") == 0) {
    return CONFIT_OPTION_TYPE_PATH;
  }
  return CONFIT_OPTION_TYPE_INVALID;
}

static int confit_schema_is_known_project_field(const char *key) {
  return strcmp(key, "name") == 0 || strcmp(key, "version") == 0 ||
         strcmp(key, "schema_version") == 0 || strcmp(key, "imports") == 0 ||
         strncmp(key, "x_", 2U) == 0;
}

static int confit_schema_is_known_option_field(const char *key) {
  static const char *const known_fields[] = {
      "type",       "default",    "prompt",    "category", "tags",
      "help",       "requires",   "conflicts", "recommends",
      "forces",     "visible_if", "range",     "choices",  "deprecated",
      "replaced_by"};
  size_t index;

  if (strncmp(key, "x_", 2U) == 0) {
    return 1;
  }

  for (index = 0U; index < sizeof(known_fields) / sizeof(known_fields[0]);
       ++index) {
    if (strcmp(key, known_fields[index]) == 0) {
      return 1;
    }
  }
  return 0;
}

static int confit_schema_slice_has_char(const char *text, size_t begin,
                                        size_t end, char needle) {
  size_t index;

  for (index = begin; index < end; ++index) {
    if (text[index] == needle) {
      return 1;
    }
  }
  return 0;
}

static ConfitStatus confit_schema_append_slice(char **buffer,
                                               size_t *buffer_size,
                                               const char *text, size_t begin,
                                               size_t end, const char *path,
                                               size_t line,
                                               ConfitDiagnostic *diagnostic) {
  char *new_buffer;
  size_t slice_size;
  size_t extra_space;

  slice_size = end - begin;
  extra_space = *buffer_size > 0U ? 1U : 0U;
  new_buffer =
      (char *)realloc(*buffer, *buffer_size + extra_space + slice_size + 1U);
  if (new_buffer == 0) {
    confit_schema_set_error(diagnostic, CONFIT_ERR_INTERNAL, path, line, 1U,
                            "failed to allocate multi-line array");
    return CONFIT_ERR_INTERNAL;
  }

  *buffer = new_buffer;
  if (extra_space != 0U) {
    (*buffer)[*buffer_size] = ' ';
    *buffer_size += 1U;
  }
  if (slice_size > 0U) {
    memcpy(*buffer + *buffer_size, text + begin, slice_size);
    *buffer_size += slice_size;
  }
  (*buffer)[*buffer_size] = '\0';
  return CONFIT_OK;
}

static ConfitStatus confit_schema_collect_array_value(
    const char *text, size_t text_size, size_t *offset, size_t *line_number,
    const ConfitSchemaLine *first_line, size_t value_begin, size_t value_end,
    char **out_value, const char *path, ConfitDiagnostic *diagnostic) {
  char *buffer;
  size_t buffer_size;
  ConfitStatus status;
  ConfitSchemaLine line;

  *out_value = 0;
  buffer = 0;
  buffer_size = 0U;
  status = confit_schema_append_slice(&buffer, &buffer_size, first_line->begin,
                                      value_begin, value_end, path,
                                      first_line->line, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  if (confit_schema_slice_has_char(first_line->begin, value_begin, value_end,
                                   ']')) {
    *out_value = buffer;
    return CONFIT_OK;
  }

  while (confit_schema_next_line(text, text_size, offset, line_number, &line)) {
    size_t begin;
    size_t end;

    end = confit_schema_strip_comment(line.begin, line.length);
    begin = confit_schema_trim_left(line.begin, end);
    end = confit_schema_trim_right(line.begin, begin, end);
    status = confit_schema_append_slice(&buffer, &buffer_size, line.begin,
                                        begin, end, path, line.line,
                                        diagnostic);
    if (status != CONFIT_OK) {
      free(buffer);
      return status;
    }
    if (confit_schema_slice_has_char(line.begin, begin, end, ']')) {
      *out_value = buffer;
      return CONFIT_OK;
    }
  }

  free(buffer);
  confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path,
                          first_line->line, value_begin + 1U,
                          "unterminated string array");
  return CONFIT_ERR_SCHEMA;
}

static ConfitStatus confit_schema_join(char *out, size_t out_size,
                                       const char *left, const char *right,
                                       ConfitDiagnostic *diagnostic) {
  return confit_host_path_join(out, out_size, left, right, diagnostic);
}

static ConfitStatus confit_schema_find_config_root(const char *project_root,
                                                   char *out, size_t out_size,
                                                   ConfitDiagnostic *diagnostic) {
  char candidate[1024];
  ConfitParserDocument *document;
  ConfitStatus status;

  if (project_root == 0 || project_root[0] == '\0') {
    confit_schema_set_error(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                            "missing project root");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status = confit_schema_join(candidate, sizeof(candidate), project_root,
                              "project.toml", diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }

  document = 0;
  status = confit_parser_load_file(candidate, &document, diagnostic);
  if (status == CONFIT_OK) {
    confit_parser_document_free(document);
    if (strlen(project_root) + 1U > out_size) {
      confit_schema_set_error(diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                              project_root, 0, 0,
                              "config root buffer is too small");
      return CONFIT_ERR_INVALID_ARGUMENT;
    }
    memcpy(out, project_root, strlen(project_root) + 1U);
    return CONFIT_OK;
  }

  status = confit_schema_join(out, out_size, project_root, "config",
                              diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_schema_load_document(const char *path,
                                                ConfitParserDocument **document,
                                                ConfitDiagnostic *diagnostic) {
  ConfitStatus status;

  status = confit_parser_load_file(path, document, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_schema_imports_append(ConfitSchemaImports *imports,
                                                 const char *item,
                                                 const char *path,
                                                 size_t line,
                                                 ConfitDiagnostic *diagnostic) {
  char **new_items;
  char *copy;

  copy = confit_schema_copy_string(item);
  if (copy == 0) {
    confit_schema_set_error(diagnostic, CONFIT_ERR_INTERNAL, path, line, 1U,
                            "failed to allocate import");
    return CONFIT_ERR_INTERNAL;
  }

  new_items =
      (char **)realloc(imports->items,
                       (imports->count + 1U) * sizeof(imports->items[0]));
  if (new_items == 0) {
    free(copy);
    confit_schema_set_error(diagnostic, CONFIT_ERR_INTERNAL, path, line, 1U,
                            "failed to allocate imports");
    return CONFIT_ERR_INTERNAL;
  }

  imports->items = new_items;
  imports->items[imports->count] = copy;
  imports->count += 1U;
  return CONFIT_OK;
}

static void confit_schema_imports_clear(ConfitSchemaImports *imports) {
  size_t index;

  if (imports == 0) {
    return;
  }

  for (index = 0U; index < imports->count; ++index) {
    free(imports->items[index]);
  }
  free(imports->items);
  imports->items = 0;
  imports->count = 0U;
}

static ConfitStatus confit_schema_set_project_string(char **slot,
                                                     const char *value,
                                                     const char *path,
                                                     size_t line,
                                                     ConfitDiagnostic *diagnostic) {
  char *copy;

  copy = confit_schema_copy_string(value);
  if (copy == 0) {
    confit_schema_set_error(diagnostic, CONFIT_ERR_INTERNAL, path, line, 1U,
                            "failed to allocate project field");
    return CONFIT_ERR_INTERNAL;
  }

  free(*slot);
  *slot = copy;
  return CONFIT_OK;
}

static ConfitStatus confit_schema_parse_project_value(
    ConfitProject *project, ConfitSchemaImports *imports, const char *key,
    const char *line_text, size_t value_begin, size_t value_end,
    const char *path, size_t line, ConfitDiagnostic *diagnostic) {
  ConfitStatus status;
  char *string_value;
  char **array_items;
  size_t array_count;
  size_t index;
  uint64_t uint_value;

  if (!confit_schema_is_known_project_field(key)) {
    confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line, 1U,
                            "unknown project field");
    return CONFIT_ERR_SCHEMA;
  }
  if (strncmp(key, "x_", 2U) == 0) {
    return CONFIT_OK;
  }

  if (strcmp(key, "name") == 0 || strcmp(key, "version") == 0) {
    string_value = confit_schema_parse_quoted_string(
        line_text, value_begin, value_end, path, line, diagnostic);
    if (string_value == 0) {
      return CONFIT_ERR_SCHEMA;
    }
    status = strcmp(key, "name") == 0
                 ? confit_schema_set_project_string(&project->name,
                                                    string_value, path, line,
                                                    diagnostic)
                 : confit_schema_set_project_string(&project->version,
                                                    string_value, path, line,
                                                    diagnostic);
    free(string_value);
    return status;
  }

  if (strcmp(key, "schema_version") == 0) {
    if (!confit_schema_parse_uint(line_text, value_begin, value_end,
                                  &uint_value)) {
      confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                              value_begin + 1U,
                              "schema_version must be an unsigned integer");
      return CONFIT_ERR_SCHEMA;
    }
    if (uint_value != 1U) {
      confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                              value_begin + 1U,
                              "unsupported schema_version");
      return CONFIT_ERR_SCHEMA;
    }
    project->schema_version = 1U;
    return CONFIT_OK;
  }

  if (strcmp(key, "imports") == 0) {
    array_items = 0;
    array_count = 0U;
    if (!confit_schema_parse_string_array(line_text, value_begin, value_end,
                                          &array_items, &array_count, path,
                                          line, diagnostic)) {
      return CONFIT_ERR_SCHEMA;
    }
    for (index = 0U; index < array_count; ++index) {
      status = confit_schema_imports_append(imports, array_items[index], path,
                                            line, diagnostic);
      if (status != CONFIT_OK) {
        confit_schema_string_array_free(array_items, array_count);
        return status;
      }
    }
    confit_schema_string_array_free(array_items, array_count);
    return CONFIT_OK;
  }

  return CONFIT_OK;
}

static ConfitStatus confit_schema_parse_option_default(
    ConfitOption *option, const char *line_text, size_t value_begin,
    size_t value_end, const char *path, size_t line,
    ConfitDiagnostic *diagnostic) {
  ConfitValue value;
  ConfitStatus status;
  char *string_value;
  int64_t int_value;
  uint64_t uint_value;
  double float_value;

  confit_value_init(&value);
  switch (option->type) {
  case CONFIT_OPTION_TYPE_BOOL:
    value_begin =
        confit_schema_trim_left(line_text + value_begin, value_end - value_begin) +
        value_begin;
    value_end = confit_schema_trim_right(line_text, value_begin, value_end);
    if (confit_schema_string_equal(line_text, value_begin, value_end, "true")) {
      confit_value_set_bool(&value, 1);
    } else if (confit_schema_string_equal(line_text, value_begin, value_end,
                                          "false")) {
      confit_value_set_bool(&value, 0);
    } else {
      confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                              value_begin + 1U,
                              "bool default must be true or false");
      return CONFIT_ERR_SCHEMA;
    }
    break;
  case CONFIT_OPTION_TYPE_INT:
    if (!confit_schema_parse_int(line_text, value_begin, value_end,
                                 &int_value)) {
      confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                              value_begin + 1U,
                              "int default must be an integer");
      return CONFIT_ERR_SCHEMA;
    }
    confit_value_set_int(&value, int_value);
    break;
  case CONFIT_OPTION_TYPE_UINT:
    if (!confit_schema_parse_uint(line_text, value_begin, value_end,
                                  &uint_value)) {
      confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                              value_begin + 1U,
                              "uint default must be an unsigned integer");
      return CONFIT_ERR_SCHEMA;
    }
    confit_value_set_uint(&value, uint_value);
    break;
  case CONFIT_OPTION_TYPE_HEX:
    if (!confit_schema_parse_hex_uint(line_text, value_begin, value_end,
                                      &uint_value)) {
      confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                              value_begin + 1U,
                              "hex default must be an unsigned integer");
      return CONFIT_ERR_SCHEMA;
    }
    confit_value_set_uint(&value, uint_value);
    break;
  case CONFIT_OPTION_TYPE_STRING:
    string_value = confit_schema_parse_quoted_string(
        line_text, value_begin, value_end, path, line, diagnostic);
    if (string_value == 0) {
      return CONFIT_ERR_SCHEMA;
    }
    status = confit_value_set_string(&value, string_value);
    free(string_value);
    if (status != CONFIT_OK) {
      return status;
    }
    break;
  case CONFIT_OPTION_TYPE_PATH:
    string_value = confit_schema_parse_quoted_string(
        line_text, value_begin, value_end, path, line, diagnostic);
    if (string_value == 0) {
      return CONFIT_ERR_SCHEMA;
    }
    status = confit_value_set_path(&value, string_value);
    free(string_value);
    if (status != CONFIT_OK) {
      return status;
    }
    break;
  case CONFIT_OPTION_TYPE_ENUM:
    string_value = confit_schema_parse_quoted_string(
        line_text, value_begin, value_end, path, line, diagnostic);
    if (string_value == 0) {
      return CONFIT_ERR_SCHEMA;
    }
    status = confit_value_set_enum(&value, string_value);
    free(string_value);
    if (status != CONFIT_OK) {
      return status;
    }
    break;
  case CONFIT_OPTION_TYPE_FLOAT:
    if (!confit_schema_parse_float(line_text, value_begin, value_end,
                                   &float_value)) {
      confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                              value_begin + 1U,
                              "float default must be finite");
      return CONFIT_ERR_SCHEMA;
    }
    confit_value_set_float(&value, float_value);
    break;
  default:
    confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                            value_begin + 1U,
                            "option type must be set before default");
    return CONFIT_ERR_SCHEMA;
  }

  status = confit_option_set_default(option, &value);
  confit_value_clear(&value);
  return status;
}

static ConfitStatus confit_schema_parse_option_value(
    ConfitOption *option, ConfitSchemaOptionState *state, const char *key,
    const char *line_text, size_t value_begin, size_t value_end,
    const char *path, size_t line, ConfitDiagnostic *diagnostic) {
  ConfitStatus status;
  char *string_value;
  char **array_items;
  size_t array_count;
  size_t index;
  ConfitOptionType option_type;

  if (!confit_schema_is_known_option_field(key)) {
    confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line, 1U,
                            "unknown option field");
    return CONFIT_ERR_SCHEMA;
  }

  if (strncmp(key, "x_", 2U) == 0 || strcmp(key, "requires") == 0 ||
      strcmp(key, "conflicts") == 0 || strcmp(key, "recommends") == 0 ||
      strcmp(key, "forces") == 0 || strcmp(key, "visible_if") == 0 ||
      strcmp(key, "deprecated") == 0 || strcmp(key, "replaced_by") == 0) {
    return CONFIT_OK;
  }

  if (strcmp(key, "type") == 0) {
    string_value = confit_schema_parse_quoted_string(
        line_text, value_begin, value_end, path, line, diagnostic);
    if (string_value == 0) {
      return CONFIT_ERR_SCHEMA;
    }
    option_type = confit_schema_parse_option_type(string_value);
    free(string_value);
    if (option_type == CONFIT_OPTION_TYPE_INVALID) {
      confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                              value_begin + 1U, "unknown option type");
      return CONFIT_ERR_SCHEMA;
    }
    option->type = option_type;
    state->saw_type = 1;
    state->type_line = line;
    return CONFIT_OK;
  }

  if (strcmp(key, "default") == 0) {
    return confit_schema_parse_option_default(option, line_text, value_begin,
                                             value_end, path, line, diagnostic);
  }

  if (strcmp(key, "range") == 0) {
    return confit_schema_parse_range(option, line_text, value_begin, value_end,
                                    path, line, diagnostic);
  }

  if (strcmp(key, "choices") == 0) {
    if (option->type != CONFIT_OPTION_TYPE_ENUM) {
      confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line,
                              value_begin + 1U,
                              "choices are only valid for enum options");
      return CONFIT_ERR_SCHEMA;
    }
    array_items = 0;
    array_count = 0U;
    if (!confit_schema_parse_string_array(line_text, value_begin, value_end,
                                          &array_items, &array_count, path,
                                          line, diagnostic)) {
      return CONFIT_ERR_SCHEMA;
    }
    for (index = 0U; index < array_count; ++index) {
      status = confit_option_add_enum_value(option, array_items[index]);
      if (status != CONFIT_OK) {
        confit_schema_string_array_free(array_items, array_count);
        return status;
      }
    }
    confit_schema_string_array_free(array_items, array_count);
    return CONFIT_OK;
  }

  if (strcmp(key, "prompt") == 0 || strcmp(key, "category") == 0 ||
      strcmp(key, "help") == 0) {
    const char *prompt = option->prompt;
    const char *category = option->category;
    const char *help = option->help;

    string_value = confit_schema_parse_quoted_string(
        line_text, value_begin, value_end, path, line, diagnostic);
    if (string_value == 0) {
      return CONFIT_ERR_SCHEMA;
    }

    if (strcmp(key, "prompt") == 0) {
      prompt = string_value;
    } else if (strcmp(key, "category") == 0) {
      category = string_value;
    } else {
      help = string_value;
    }
    status = confit_option_set_metadata(option, prompt, category, help);
    free(string_value);
    return status;
  }

  if (strcmp(key, "tags") == 0) {
    array_items = 0;
    array_count = 0U;
    if (!confit_schema_parse_string_array(line_text, value_begin, value_end,
                                          &array_items, &array_count, path,
                                          line, diagnostic)) {
      return CONFIT_ERR_SCHEMA;
    }
    for (index = 0U; index < array_count; ++index) {
      status = confit_option_add_tag(option, array_items[index]);
      if (status != CONFIT_OK) {
        confit_schema_string_array_free(array_items, array_count);
        return status;
      }
    }
    confit_schema_string_array_free(array_items, array_count);
    return CONFIT_OK;
  }

  return CONFIT_OK;
}

static ConfitStatus confit_schema_finish_option(
    const ConfitSchemaOptionState *state, const char *path,
    ConfitDiagnostic *diagnostic) {
  if (state->option == 0) {
    return CONFIT_OK;
  }

  if (!state->saw_type) {
    confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path,
                            state->type_line, 1U, "missing option type");
    return CONFIT_ERR_SCHEMA;
  }

  if (confit_option_validate_default(state->option) != CONFIT_OK) {
    const char *message;

    message = "invalid option default";
    if (state->option->type == CONFIT_OPTION_TYPE_ENUM &&
        state->option->default_value.kind == CONFIT_VALUE_ENUM &&
        state->option->enum_value_count > 0U) {
      message = "enum default is not a candidate";
    } else if (state->option->has_range) {
      message = "default outside range";
    }
    confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path,
                            state->type_line, 1U, message);
    return CONFIT_ERR_SCHEMA;
  }

  return CONFIT_OK;
}

static void confit_schema_option_state_clear(ConfitSchemaOptionState *state) {
  if (state == 0) {
    return;
  }

  free(state->id);
  state->id = 0;
  state->option = 0;
  state->saw_type = 0;
  state->type_line = 0U;
}

static ConfitStatus confit_schema_begin_option(
    ConfitProject *project, ConfitSchemaOptionState *state, char *id,
    const char *path, size_t line, ConfitDiagnostic *diagnostic) {
  ConfitStatus status;
  ConfitOption *option;

  if (!confit_schema_validate_option_id(id)) {
    confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line, 1U,
                            "invalid option id");
    free(id);
    return CONFIT_ERR_SCHEMA;
  }
  if (confit_project_find_option(project, id) != 0) {
    confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line, 1U,
                            "duplicate option id");
    free(id);
    return CONFIT_ERR_SCHEMA;
  }

  status = confit_project_add_option(project, &option);
  if (status != CONFIT_OK) {
    free(id);
    return status;
  }
  status = confit_option_set_identity(option, id, CONFIT_OPTION_TYPE_INVALID);
  if (status != CONFIT_OK) {
    free(id);
    return status;
  }

  state->option = option;
  state->saw_type = 0;
  state->type_line = line;
  state->id = id;
  return CONFIT_OK;
}

static ConfitStatus confit_schema_parse_option_file(
    ConfitProject *project, const char *path, ConfitDiagnostic *diagnostic) {
  ConfitParserDocument *document;
  const char *text;
  size_t text_size;
  size_t offset;
  size_t line_number;
  ConfitSchemaLine line;
  ConfitSchemaTableKind table_kind;
  ConfitSchemaOptionState option_state;
  ConfitStatus status;

  status = confit_schema_load_document(path, &document, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }

  text = confit_parser_document_source_text(document);
  text_size = confit_parser_document_source_size(document);
  offset = 0U;
  line_number = 1U;
  table_kind = CONFIT_SCHEMA_TABLE_NONE;
  option_state.option = 0;
  option_state.saw_type = 0;
  option_state.type_line = 0U;
  option_state.id = 0;

  while (confit_schema_next_line(text, text_size, &offset, &line_number,
                                 &line)) {
    size_t begin;
    size_t end;
    size_t equals_index;
    char *key;

    end = confit_schema_strip_comment(line.begin, line.length);
    begin = confit_schema_trim_left(line.begin, end);
    end = confit_schema_trim_right(line.begin, begin, end);
    if (begin >= end) {
      continue;
    }

    if (line.begin[begin] == '[') {
      char *table_name;
      char *option_id;

      status = confit_schema_finish_option(&option_state, path, diagnostic);
      if (status != CONFIT_OK) {
        confit_schema_option_state_clear(&option_state);
        confit_parser_document_free(document);
        return status;
      }
      confit_schema_option_state_clear(&option_state);

      table_name = 0;
      option_id = 0;
      if (!confit_schema_parse_table(line.begin, begin, end, &table_name)) {
        confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line.line,
                                begin + 1U, "invalid table header");
        confit_parser_document_free(document);
        return CONFIT_ERR_SCHEMA;
      }
      if (confit_schema_parse_option_table_id(table_name, &option_id)) {
        table_kind = CONFIT_SCHEMA_TABLE_OPTION;
        status = confit_schema_begin_option(project, &option_state, option_id,
                                            path, line.line, diagnostic);
        free(table_name);
        if (status != CONFIT_OK) {
          confit_parser_document_free(document);
          return status;
        }
        continue;
      }
      free(table_name);
      table_kind = CONFIT_SCHEMA_TABLE_NONE;
      continue;
    }

    if (table_kind != CONFIT_SCHEMA_TABLE_OPTION ||
        option_state.option == 0) {
      confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line.line,
                              begin + 1U, "expected option table");
      confit_schema_option_state_clear(&option_state);
      confit_parser_document_free(document);
      return CONFIT_ERR_SCHEMA;
    }

    if (!confit_schema_find_equals(line.begin, begin, end, &equals_index)) {
      confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line.line,
                              begin + 1U, "expected key/value entry");
      confit_schema_option_state_clear(&option_state);
      confit_parser_document_free(document);
      return CONFIT_ERR_SCHEMA;
    }

    key = confit_schema_parse_bare_token(line.begin, begin, equals_index);
    if (key == 0) {
      confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line.line,
                              begin + 1U, "missing key");
      confit_schema_option_state_clear(&option_state);
      confit_parser_document_free(document);
      return CONFIT_ERR_SCHEMA;
    }
    status = confit_schema_parse_option_value(
        option_state.option, &option_state, key, line.begin, equals_index + 1U,
        end, path, line.line, diagnostic);
    free(key);
    if (status != CONFIT_OK) {
      confit_schema_option_state_clear(&option_state);
      confit_parser_document_free(document);
      return status;
    }
  }

  status = confit_schema_finish_option(&option_state, path, diagnostic);
  confit_schema_option_state_clear(&option_state);
  confit_parser_document_free(document);
  return status;
}

static ConfitStatus confit_schema_parse_project_file(
    ConfitProject *project, const char *path, ConfitSchemaImports *imports,
    ConfitDiagnostic *diagnostic) {
  ConfitParserDocument *document;
  const char *text;
  size_t text_size;
  size_t offset;
  size_t line_number;
  ConfitSchemaLine line;
  ConfitSchemaTableKind table_kind;
  ConfitStatus status;

  status = confit_schema_load_document(path, &document, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }

  text = confit_parser_document_source_text(document);
  text_size = confit_parser_document_source_size(document);
  offset = 0U;
  line_number = 1U;
  table_kind = CONFIT_SCHEMA_TABLE_NONE;

  while (confit_schema_next_line(text, text_size, &offset, &line_number,
                                 &line)) {
    size_t begin;
    size_t end;
    size_t equals_index;
    char *key;

    end = confit_schema_strip_comment(line.begin, line.length);
    begin = confit_schema_trim_left(line.begin, end);
    end = confit_schema_trim_right(line.begin, begin, end);
    if (begin >= end) {
      continue;
    }

    if (line.begin[begin] == '[') {
      char *table_name;

      table_name = 0;
      if (!confit_schema_parse_table(line.begin, begin, end, &table_name)) {
        confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line.line,
                                begin + 1U, "invalid table header");
        confit_parser_document_free(document);
        return CONFIT_ERR_SCHEMA;
      }
      table_kind = strcmp(table_name, "project") == 0
                       ? CONFIT_SCHEMA_TABLE_PROJECT
                       : CONFIT_SCHEMA_TABLE_NONE;
      free(table_name);
      continue;
    }

    if (table_kind != CONFIT_SCHEMA_TABLE_PROJECT) {
      confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line.line,
                              begin + 1U, "expected project table");
      confit_parser_document_free(document);
      return CONFIT_ERR_SCHEMA;
    }

    if (!confit_schema_find_equals(line.begin, begin, end, &equals_index)) {
      confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line.line,
                              begin + 1U, "expected key/value entry");
      confit_parser_document_free(document);
      return CONFIT_ERR_SCHEMA;
    }

    key = confit_schema_parse_bare_token(line.begin, begin, equals_index);
    if (key == 0) {
      confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, line.line,
                              begin + 1U, "missing key");
      confit_parser_document_free(document);
      return CONFIT_ERR_SCHEMA;
    }
    if (strcmp(key, "imports") == 0) {
      char *value_text;

      value_text = 0;
      status = confit_schema_collect_array_value(
          text, text_size, &offset, &line_number, &line, equals_index + 1U, end,
          &value_text, path, diagnostic);
      if (status == CONFIT_OK) {
        status = confit_schema_parse_project_value(
            project, imports, key, value_text, 0U, strlen(value_text), path,
            line.line, diagnostic);
      }
      free(value_text);
      free(key);
      if (status != CONFIT_OK) {
        confit_parser_document_free(document);
        return status;
      }
      continue;
    }
    status = confit_schema_parse_project_value(
        project, imports, key, line.begin, equals_index + 1U, end, path,
        line.line, diagnostic);
    free(key);
    if (status != CONFIT_OK) {
      confit_parser_document_free(document);
      return status;
    }
  }

  confit_parser_document_free(document);
  if (project->name == 0 || project->schema_version != 1U) {
    confit_schema_set_error(diagnostic, CONFIT_ERR_SCHEMA, path, 1U, 1U,
                            "project name and schema_version are required");
    return CONFIT_ERR_SCHEMA;
  }
  return CONFIT_OK;
}

ConfitStatus confit_schema_load_project(const char *project_root,
                                        ConfitProject **out_project,
                                        ConfitDiagnostic *diagnostic) {
  ConfitProject *project;
  ConfitSchemaImports imports;
  char config_root[1024];
  char project_path[1024];
  ConfitStatus status;
  size_t index;

  if (out_project == 0) {
    confit_schema_set_error(diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                            project_root, 0, 0,
                            "missing project output pointer");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  *out_project = 0;
  imports.items = 0;
  imports.count = 0U;

  status = confit_schema_find_config_root(project_root, config_root,
                                          sizeof(config_root), diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_schema_join(project_path, sizeof(project_path), config_root,
                              "project.toml", diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }

  project = confit_project_create();
  if (project == 0) {
    confit_schema_set_error(diagnostic, CONFIT_ERR_INTERNAL, project_path, 0, 0,
                            "failed to allocate project");
    return CONFIT_ERR_INTERNAL;
  }

  status =
      confit_schema_parse_project_file(project, project_path, &imports,
                                       diagnostic);
  if (status != CONFIT_OK) {
    confit_schema_imports_clear(&imports);
    confit_project_free(project);
    return status;
  }

  for (index = 0U; index < imports.count; ++index) {
    char option_path[1024];

    status = confit_schema_join(option_path, sizeof(option_path), config_root,
                                imports.items[index], diagnostic);
    if (status == CONFIT_OK) {
      status = confit_schema_parse_option_file(project, option_path,
                                               diagnostic);
    }
    if (status != CONFIT_OK) {
      confit_schema_imports_clear(&imports);
      confit_project_free(project);
      return status;
    }
  }

  confit_schema_imports_clear(&imports);
  *out_project = project;
  return CONFIT_OK;
}
