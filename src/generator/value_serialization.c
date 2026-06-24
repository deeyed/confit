#include "confit/generator.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ConfitValueSerializer {
  char *text;
  size_t size;
  size_t capacity;
} ConfitValueSerializer;

static void confit_value_serializer_init(ConfitValueSerializer *serializer) {
  serializer->text = 0;
  serializer->size = 0U;
  serializer->capacity = 0U;
}

static ConfitStatus confit_value_serializer_reserve(
    ConfitValueSerializer *serializer, size_t additional_size) {
  size_t required_capacity;
  size_t new_capacity;
  char *new_text;

  required_capacity = serializer->size + additional_size + 1U;
  if (required_capacity <= serializer->capacity) {
    return CONFIT_OK;
  }

  new_capacity = serializer->capacity == 0U ? 128U : serializer->capacity;
  while (new_capacity < required_capacity) {
    new_capacity *= 2U;
  }

  new_text = (char *)realloc(serializer->text, new_capacity);
  if (new_text == 0) {
    return CONFIT_ERR_INTERNAL;
  }

  serializer->text = new_text;
  serializer->capacity = new_capacity;
  return CONFIT_OK;
}

static ConfitStatus confit_value_serializer_append(
    ConfitValueSerializer *serializer, const char *text) {
  const size_t text_size = strlen(text);
  ConfitStatus status;

  status = confit_value_serializer_reserve(serializer, text_size);
  if (status != CONFIT_OK) {
    return status;
  }

  memcpy(serializer->text + serializer->size, text, text_size);
  serializer->size += text_size;
  serializer->text[serializer->size] = '\0';
  return CONFIT_OK;
}

static ConfitStatus confit_value_serializer_append_char(
    ConfitValueSerializer *serializer, char value) {
  char text[2];

  text[0] = value;
  text[1] = '\0';
  return confit_value_serializer_append(serializer, text);
}

static ConfitStatus confit_value_serializer_append_uint64(
    ConfitValueSerializer *serializer, uint64_t value) {
  char digits[32];
  size_t count;

  count = 0U;
  do {
    digits[count] = (char)('0' + (value % 10U));
    value /= 10U;
    count += 1U;
  } while (value != 0U);

  while (count > 0U) {
    ConfitStatus status;

    count -= 1U;
    status = confit_value_serializer_append_char(serializer, digits[count]);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_value_serializer_append_int64(
    ConfitValueSerializer *serializer, int64_t value) {
  uint64_t magnitude;
  ConfitStatus status;

  if (value < 0) {
    status = confit_value_serializer_append(serializer, "-");
    if (status != CONFIT_OK) {
      return status;
    }
    magnitude = (uint64_t)(-(value + 1)) + 1U;
  } else {
    magnitude = (uint64_t)value;
  }
  return confit_value_serializer_append_uint64(serializer, magnitude);
}

static char confit_value_hex_digit(unsigned int value) {
  return (char)(value < 10U ? '0' + value : 'A' + (value - 10U));
}

static ConfitStatus confit_value_serializer_append_hex_uint64(
    ConfitValueSerializer *serializer, uint64_t value) {
  int shift;
  int started;
  ConfitStatus status;

  status = confit_value_serializer_append(serializer, "0x");
  if (status != CONFIT_OK) {
    return status;
  }

  started = 0;
  for (shift = 60; shift >= 0; shift -= 4) {
    const unsigned int digit = (unsigned int)((value >> (unsigned int)shift) &
                                             UINT64_C(0xF));

    if (digit != 0U || started || shift == 0 || shift < 32) {
      status = confit_value_serializer_append_char(
          serializer, confit_value_hex_digit(digit));
      if (status != CONFIT_OK) {
        return status;
      }
      started = 1;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_value_serializer_append_float(
    ConfitValueSerializer *serializer, double value) {
  char text[64];

  (void)snprintf(text, sizeof(text), "%.17g", value);
  return confit_value_serializer_append(serializer, text);
}

static int confit_value_is_printable_ascii(unsigned char value) {
  return value >= 32U && value < 127U;
}

static ConfitStatus confit_value_serializer_append_text_byte(
    ConfitValueSerializer *serializer, unsigned char value) {
  ConfitStatus status;

  switch (value) {
  case '\\':
    return confit_value_serializer_append(serializer, "\\\\");
  case '"':
    return confit_value_serializer_append(serializer, "\\\"");
  case '\n':
    return confit_value_serializer_append(serializer, "\\n");
  case '\r':
    return confit_value_serializer_append(serializer, "\\r");
  case '\t':
    return confit_value_serializer_append(serializer, "\\t");
  default:
    if (confit_value_is_printable_ascii(value)) {
      return confit_value_serializer_append_char(serializer, (char)value);
    }
    status = confit_value_serializer_append(serializer, "\\x");
    if (status == CONFIT_OK) {
      status = confit_value_serializer_append_char(
          serializer, confit_value_hex_digit((unsigned int)(value >> 4U)));
    }
    if (status == CONFIT_OK) {
      status = confit_value_serializer_append_char(
          serializer, confit_value_hex_digit((unsigned int)(value & 0xFU)));
    }
    return status;
  }
}

static ConfitStatus confit_value_serializer_append_text_payload(
    ConfitValueSerializer *serializer, const char *text) {
  size_t index;

  if (text == 0) {
    return CONFIT_OK;
  }

  for (index = 0U; text[index] != '\0'; ++index) {
    ConfitStatus status = confit_value_serializer_append_text_byte(
        serializer, (unsigned char)text[index]);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_value_serializer_append_lua_string(
    ConfitValueSerializer *serializer, const char *text) {
  ConfitStatus status;

  status = confit_value_serializer_append_char(serializer, '"');
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_value_serializer_append_text_payload(serializer, text);
  if (status != CONFIT_OK) {
    return status;
  }
  return confit_value_serializer_append_char(serializer, '"');
}

static ConfitStatus confit_value_serializer_append_cmake_string(
    ConfitValueSerializer *serializer, const char *text) {
  size_t index;
  ConfitStatus status;

  status = confit_value_serializer_append_char(serializer, '"');
  if (status != CONFIT_OK) {
    return status;
  }

  if (text != 0) {
    for (index = 0U; text[index] != '\0'; ++index) {
      const unsigned char value = (unsigned char)text[index];

      switch (value) {
      case '\\':
      case '"':
      case '$':
      case ';':
        status = confit_value_serializer_append_char(serializer, '\\');
        if (status == CONFIT_OK) {
          status = confit_value_serializer_append_char(serializer, (char)value);
        }
        break;
      case '\n':
        status = confit_value_serializer_append(serializer, "\\n");
        break;
      case '\r':
        status = confit_value_serializer_append(serializer, "\\r");
        break;
      case '\t':
        status = confit_value_serializer_append(serializer, "\\t");
        break;
      default:
        if (confit_value_is_printable_ascii(value)) {
          status = confit_value_serializer_append_char(serializer, (char)value);
        } else {
          status = confit_value_serializer_append_char(serializer, '?');
        }
        break;
      }
      if (status != CONFIT_OK) {
        return status;
      }
    }
  }

  return confit_value_serializer_append_char(serializer, '"');
}

static ConfitStatus confit_value_serializer_append_text_value(
    ConfitValueSerializer *serializer, const ConfitValue *value,
    ConfitOptionType option_type) {
  if (value == 0) {
    return confit_value_serializer_append(serializer, "");
  }

  switch (value->kind) {
  case CONFIT_VALUE_BOOL:
    return confit_value_serializer_append(
        serializer, value->as.bool_value ? "true" : "false");
  case CONFIT_VALUE_INT:
    return confit_value_serializer_append_int64(serializer, value->as.int_value);
  case CONFIT_VALUE_UINT:
    if (option_type == CONFIT_OPTION_TYPE_HEX) {
      return confit_value_serializer_append_hex_uint64(serializer,
                                                       value->as.uint_value);
    }
    return confit_value_serializer_append_uint64(serializer,
                                                 value->as.uint_value);
  case CONFIT_VALUE_FLOAT:
    return confit_value_serializer_append_float(serializer,
                                                value->as.float_value);
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
  case CONFIT_VALUE_PATH:
    return confit_value_serializer_append_text_payload(serializer,
                                                       value->as.string_value);
  case CONFIT_VALUE_EMPTY:
  default:
    return confit_value_serializer_append(serializer, "");
  }
}

static ConfitStatus confit_value_serializer_append_lua_value(
    ConfitValueSerializer *serializer, const ConfitValue *value,
    ConfitOptionType option_type) {
  if (value == 0) {
    return confit_value_serializer_append(serializer, "nil");
  }

  switch (value->kind) {
  case CONFIT_VALUE_BOOL:
    return confit_value_serializer_append(
        serializer, value->as.bool_value ? "true" : "false");
  case CONFIT_VALUE_INT:
    return confit_value_serializer_append_int64(serializer, value->as.int_value);
  case CONFIT_VALUE_UINT:
    (void)option_type;
    return confit_value_serializer_append_uint64(serializer,
                                                 value->as.uint_value);
  case CONFIT_VALUE_FLOAT:
    return confit_value_serializer_append_float(serializer,
                                                value->as.float_value);
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
  case CONFIT_VALUE_PATH:
    return confit_value_serializer_append_lua_string(serializer,
                                                    value->as.string_value);
  case CONFIT_VALUE_EMPTY:
  default:
    return confit_value_serializer_append(serializer, "nil");
  }
}

static ConfitStatus confit_value_serializer_append_cmake_value(
    ConfitValueSerializer *serializer, const ConfitValue *value,
    ConfitOptionType option_type) {
  ConfitValueSerializer text_serializer;
  ConfitStatus status;

  if (value != 0 &&
      (value->kind == CONFIT_VALUE_STRING || value->kind == CONFIT_VALUE_ENUM ||
       value->kind == CONFIT_VALUE_PATH)) {
    return confit_value_serializer_append_cmake_string(
        serializer, value->as.string_value);
  }

  confit_value_serializer_init(&text_serializer);
  status = confit_value_serializer_append_text_value(&text_serializer, value,
                                                     option_type);
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append_cmake_string(
        serializer, text_serializer.text != 0 ? text_serializer.text : "");
  }
  free(text_serializer.text);
  return status;
}

static ConfitStatus confit_value_serializer_append_cmake_raw_value_text(
    ConfitValueSerializer *serializer, const ConfitValue *value,
    ConfitOptionType option_type) {
  if (value != 0 &&
      (value->kind == CONFIT_VALUE_STRING || value->kind == CONFIT_VALUE_ENUM ||
       value->kind == CONFIT_VALUE_PATH)) {
    return confit_value_serializer_append(
        serializer, value->as.string_value != 0 ? value->as.string_value : "");
  }
  return confit_value_serializer_append_text_value(serializer, value,
                                                  option_type);
}

static ConfitStatus confit_value_serializer_finish_value(
    ConfitValueSerializer *serializer, const ConfitValue *value,
    ConfitOptionType option_type, ConfitGeneratorValueFormat format) {
  switch (format) {
  case CONFIT_GENERATOR_VALUE_TEXT:
    return confit_value_serializer_append_text_value(serializer, value,
                                                     option_type);
  case CONFIT_GENERATOR_VALUE_LUA:
    return confit_value_serializer_append_lua_value(serializer, value,
                                                    option_type);
  case CONFIT_GENERATOR_VALUE_CMAKE:
    return confit_value_serializer_append_cmake_value(serializer, value,
                                                     option_type);
  default:
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
}

static int confit_value_is_lower(char value) {
  return value >= 'a' && value <= 'z';
}

static int confit_value_is_upper(char value) {
  return value >= 'A' && value <= 'Z';
}

static int confit_value_is_digit(char value) {
  return value >= '0' && value <= '9';
}

static int confit_value_is_ident(char value) {
  return confit_value_is_lower(value) || confit_value_is_upper(value) ||
         confit_value_is_digit(value);
}

static char confit_value_to_upper(char value) {
  if (confit_value_is_lower(value)) {
    return (char)(value - 'a' + 'A');
  }
  return value;
}

static ConfitStatus confit_value_serializer_append_variable_fragment(
    ConfitValueSerializer *serializer, const char *text) {
  size_t index;
  int last_was_separator;

  if (text == 0 || text[0] == '\0') {
    return confit_value_serializer_append(serializer, "UNKNOWN");
  }

  last_was_separator = 1;
  for (index = 0U; text[index] != '\0'; ++index) {
    ConfitStatus status;

    if (confit_value_is_ident(text[index])) {
      status = confit_value_serializer_append_char(
          serializer, confit_value_to_upper(text[index]));
      if (status != CONFIT_OK) {
        return status;
      }
      last_was_separator = 0;
      continue;
    }

    if (!last_was_separator) {
      status = confit_value_serializer_append_char(serializer, '_');
      if (status != CONFIT_OK) {
        return status;
      }
      last_was_separator = 1;
    }
  }

  if (serializer->size > 0U && serializer->text[serializer->size - 1U] == '_') {
    serializer->size -= 1U;
    serializer->text[serializer->size] = '\0';
  }
  return CONFIT_OK;
}

static ConfitStatus confit_value_serializer_append_cmake_option_prefix(
    ConfitValueSerializer *serializer, const char *option_id) {
  const char *dot;
  char namespace_text[128];
  ConfitStatus status;

  if (option_id == 0 || option_id[0] == '\0') {
    return confit_value_serializer_append(serializer, "UNKNOWN_CONFIG");
  }

  dot = strchr(option_id, '.');
  if (dot == 0 || dot == option_id) {
    status = confit_value_serializer_append_variable_fragment(serializer,
                                                              option_id);
    if (status != CONFIT_OK) {
      return status;
    }
    return confit_value_serializer_append(serializer, "_CONFIG");
  }

  if ((size_t)(dot - option_id) >= sizeof(namespace_text)) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  memcpy(namespace_text, option_id, (size_t)(dot - option_id));
  namespace_text[dot - option_id] = '\0';

  status = confit_value_serializer_append_variable_fragment(serializer,
                                                            namespace_text);
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append(serializer, "_CONFIG_");
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append_variable_fragment(serializer,
                                                              dot + 1);
  }
  return status;
}

static ConfitStatus confit_value_serializer_append_lua_record(
    ConfitValueSerializer *serializer, const ConfitResolvedValue *value,
    ConfitOptionType option_type) {
  ConfitValueSerializer text_serializer;
  ConfitStatus status;

  confit_value_serializer_init(&text_serializer);
  status = confit_value_serializer_append_text_value(
      &text_serializer, &value->value, option_type);
  if (status != CONFIT_OK) {
    free(text_serializer.text);
    return status;
  }

  status = confit_value_serializer_append(serializer, "{ type = ");
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append_lua_string(
        serializer, confit_option_type_name(option_type));
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append(serializer, ", value = ");
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append_lua_value(serializer, &value->value,
                                                     option_type);
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append(serializer, ", text = ");
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append_lua_string(
        serializer, text_serializer.text != 0 ? text_serializer.text : "");
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append(serializer, ", source = ");
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append_lua_string(
        serializer, value->source != 0 ? value->source : "");
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append(serializer, " }");
  }
  free(text_serializer.text);
  return status;
}

static ConfitStatus confit_value_serializer_append_cmake_set(
    ConfitValueSerializer *serializer, const char *prefix, const char *suffix,
    const char *value) {
  ConfitStatus status;

  status = confit_value_serializer_append(serializer, "set(");
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append(serializer, prefix);
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append(serializer, suffix);
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append(serializer, " ");
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append_cmake_string(serializer, value);
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append(serializer, ")\n");
  }
  return status;
}

static ConfitStatus confit_value_serializer_append_cmake_record(
    ConfitValueSerializer *serializer, const ConfitResolvedValue *value,
    ConfitOptionType option_type) {
  ConfitValueSerializer prefix_serializer;
  ConfitValueSerializer value_serializer;
  ConfitValueSerializer text_serializer;
  ConfitStatus status;

  confit_value_serializer_init(&prefix_serializer);
  confit_value_serializer_init(&value_serializer);
  confit_value_serializer_init(&text_serializer);

  status = confit_value_serializer_append_cmake_option_prefix(
      &prefix_serializer, value->option_id);
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append_cmake_raw_value_text(
        &value_serializer, &value->value, option_type);
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append_text_value(&text_serializer,
                                                       &value->value,
                                                       option_type);
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append_cmake_set(
        serializer, prefix_serializer.text, "",
        text_serializer.text != 0 ? text_serializer.text : "");
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append_cmake_set(
        serializer, prefix_serializer.text, "_TYPE",
        confit_option_type_name(option_type));
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append_cmake_set(
        serializer, prefix_serializer.text, "_VALUE",
        value_serializer.text != 0 ? value_serializer.text : "");
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append_cmake_set(
        serializer, prefix_serializer.text, "_TEXT",
        text_serializer.text != 0 ? text_serializer.text : "");
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append_cmake_set(
        serializer, prefix_serializer.text, "_SOURCE",
        value->source != 0 ? value->source : "");
  }

  free(prefix_serializer.text);
  free(value_serializer.text);
  free(text_serializer.text);
  return status;
}

static ConfitStatus confit_value_serializer_append_text_record(
    ConfitValueSerializer *serializer, const ConfitResolvedValue *value,
    ConfitOptionType option_type) {
  ConfitValueSerializer text_serializer;
  ConfitStatus status;

  confit_value_serializer_init(&text_serializer);
  status = confit_value_serializer_append_text_value(&text_serializer,
                                                     &value->value,
                                                     option_type);
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append(serializer,
                                            value->option_id != 0
                                                ? value->option_id
                                                : "(unknown)");
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append(serializer, " type=");
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append(serializer,
                                            confit_option_type_name(option_type));
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append(serializer, " value=");
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append(
        serializer, text_serializer.text != 0 ? text_serializer.text : "");
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append(serializer, " text=");
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append(
        serializer, text_serializer.text != 0 ? text_serializer.text : "");
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append(serializer, " source=");
  }
  if (status == CONFIT_OK) {
    status = confit_value_serializer_append_text_payload(
        serializer, value->source != 0 ? value->source : "");
  }

  free(text_serializer.text);
  return status;
}

ConfitStatus confit_generator_serialize_value(
    const ConfitValue *value, ConfitOptionType option_type,
    ConfitGeneratorValueFormat format, char **out_text,
    ConfitDiagnostic *diagnostic) {
  ConfitValueSerializer serializer;
  ConfitStatus status;

  if (out_text == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "missing serialized value output pointer");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_text = 0;
  if (value == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid serialized value argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  confit_value_serializer_init(&serializer);
  status = confit_value_serializer_finish_value(&serializer, value, option_type,
                                                format);
  if (status != CONFIT_OK) {
    free(serializer.text);
    confit_diagnostic_set(diagnostic, status, 0, 0, 0,
                          "failed to serialize value");
    return status;
  }

  *out_text = serializer.text;
  return CONFIT_OK;
}

ConfitStatus confit_generator_serialize_resolved_value(
    const ConfitResolvedValue *value, ConfitOptionType option_type,
    ConfitGeneratorValueFormat format, char **out_text,
    ConfitDiagnostic *diagnostic) {
  ConfitValueSerializer serializer;
  ConfitStatus status;

  if (out_text == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "missing serialized resolved value output pointer");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_text = 0;
  if (value == 0 || value->option_id == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid serialized resolved value argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  confit_value_serializer_init(&serializer);
  switch (format) {
  case CONFIT_GENERATOR_VALUE_TEXT:
    status = confit_value_serializer_append_text_record(&serializer, value,
                                                        option_type);
    break;
  case CONFIT_GENERATOR_VALUE_LUA:
    status = confit_value_serializer_append_lua_record(&serializer, value,
                                                       option_type);
    break;
  case CONFIT_GENERATOR_VALUE_CMAKE:
    status = confit_value_serializer_append_cmake_record(&serializer, value,
                                                         option_type);
    break;
  default:
    status = CONFIT_ERR_INVALID_ARGUMENT;
    break;
  }
  if (status != CONFIT_OK) {
    free(serializer.text);
    confit_diagnostic_set(diagnostic, status, value->option_id, 0, 0,
                          "failed to serialize resolved value");
    return status;
  }

  *out_text = serializer.text;
  return CONFIT_OK;
}
