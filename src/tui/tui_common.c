#include "tui_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *confit_tui_text_or_dash(const char *text) {
  return text != 0 && text[0] != '\0' ? text : "-";
}

const char *confit_tui_input_mode_name(ConfitTuiInputMode mode) {
  switch (mode) {
  case CONFIT_TUI_INPUT_NORMAL:
    return "normal";
  case CONFIT_TUI_INPUT_COMMAND:
    return "command";
  case CONFIT_TUI_INPUT_SEARCH:
    return "search";
  case CONFIT_TUI_INPUT_FILTER:
    return "filter";
  case CONFIT_TUI_INPUT_DIALOG:
    return "dialog";
  default:
    return "input";
  }
}

int confit_tui_input_cancelled(int result) {
  return result == CONFIT_TUI_INPUT_CANCELLED;
}

int confit_tui_input_error(int result) {
  return result == CONFIT_TUI_INPUT_ERROR;
}

ConfitStatus confit_tui_parse_int64(const char *text, int64_t *out) {
  char *end;
  long long value;

  errno = 0;
  value = strtoll(text, &end, 0);
  if (text == end || *end != '\0' || errno != 0) {
    return CONFIT_ERR_SCHEMA;
  }
  *out = (int64_t)value;
  return CONFIT_OK;
}

ConfitStatus confit_tui_parse_uint64(const char *text, uint64_t *out) {
  char *end;
  unsigned long long value;

  if (text[0] == '-') {
    return CONFIT_ERR_SCHEMA;
  }
  errno = 0;
  value = strtoull(text, &end, 0);
  if (text == end || *end != '\0' || errno != 0) {
    return CONFIT_ERR_SCHEMA;
  }
  *out = (uint64_t)value;
  return CONFIT_OK;
}

ConfitStatus confit_tui_parse_double(const char *text, double *out) {
  char *end;
  double value;

  errno = 0;
  value = strtod(text, &end);
  if (text == end || *end != '\0' || errno != 0 || value != value ||
      value > 1.0e308 || value < -1.0e308) {
    return CONFIT_ERR_SCHEMA;
  }
  *out = value;
  return CONFIT_OK;
}

void confit_tui_text_builder_init(ConfitTuiTextBuilder *builder) {
  builder->text = 0;
  builder->size = 0U;
  builder->capacity = 0U;
}

static ConfitStatus confit_tui_text_reserve(ConfitTuiTextBuilder *builder,
                                            size_t additional_size) {
  size_t required;
  size_t capacity;
  char *text;

  required = builder->size + additional_size + 1U;
  if (required <= builder->capacity) {
    return CONFIT_OK;
  }
  capacity = builder->capacity == 0U ? 512U : builder->capacity;
  while (capacity < required) {
    capacity *= 2U;
  }
  text = (char *)realloc(builder->text, capacity);
  if (text == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  builder->text = text;
  builder->capacity = capacity;
  return CONFIT_OK;
}

ConfitStatus confit_tui_text_append(ConfitTuiTextBuilder *builder,
                                    const char *text) {
  const size_t size = strlen(text);
  ConfitStatus status;

  status = confit_tui_text_reserve(builder, size);
  if (status != CONFIT_OK) {
    return status;
  }
  memcpy(builder->text + builder->size, text, size);
  builder->size += size;
  builder->text[builder->size] = '\0';
  return CONFIT_OK;
}

ConfitStatus confit_tui_text_append_char(ConfitTuiTextBuilder *builder,
                                         char value) {
  char text[2];

  text[0] = value;
  text[1] = '\0';
  return confit_tui_text_append(builder, text);
}

ConfitStatus confit_tui_text_append_quoted(ConfitTuiTextBuilder *builder,
                                           const char *text) {
  ConfitStatus status;
  size_t index;

  status = confit_tui_text_append(builder, "\"");
  if (status != CONFIT_OK) {
    return status;
  }
  for (index = 0U; text != 0 && text[index] != '\0'; ++index) {
    if (text[index] == '"' || text[index] == '\\') {
      status = confit_tui_text_append(builder, "\\");
      if (status != CONFIT_OK) {
        return status;
      }
    }
    status = confit_tui_text_append_char(builder, text[index]);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return confit_tui_text_append(builder, "\"");
}

ConfitStatus confit_tui_text_append_value(ConfitTuiTextBuilder *builder,
                                          const ConfitOption *option,
                                          const ConfitValue *value) {
  char buffer[128];

  switch (value->kind) {
  case CONFIT_VALUE_BOOL:
    return confit_tui_text_append(builder,
                                  value->as.bool_value ? "true" : "false");
  case CONFIT_VALUE_INT:
    (void)snprintf(buffer, sizeof(buffer), "%lld",
                   (long long)value->as.int_value);
    return confit_tui_text_append(builder, buffer);
  case CONFIT_VALUE_UINT:
    if (option != 0 && option->type == CONFIT_OPTION_TYPE_HEX) {
      (void)snprintf(buffer, sizeof(buffer), "0x%llX",
                     (unsigned long long)value->as.uint_value);
    } else {
      (void)snprintf(buffer, sizeof(buffer), "%llu",
                     (unsigned long long)value->as.uint_value);
    }
    return confit_tui_text_append(builder, buffer);
  case CONFIT_VALUE_FLOAT:
    (void)snprintf(buffer, sizeof(buffer), "%.17g", value->as.float_value);
    return confit_tui_text_append(builder, buffer);
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
  case CONFIT_VALUE_PATH:
    return confit_tui_text_append_quoted(builder, value->as.string_value);
  default:
    return CONFIT_ERR_SCHEMA;
  }
}
