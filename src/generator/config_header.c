#include "confit/generator.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct ConfitHeaderBuilder {
  char *text;
  size_t size;
  size_t capacity;
} ConfitHeaderBuilder;

static void confit_header_builder_init(ConfitHeaderBuilder *builder) {
  builder->text = 0;
  builder->size = 0U;
  builder->capacity = 0U;
}

static ConfitStatus confit_header_reserve(ConfitHeaderBuilder *builder,
                                          size_t additional_size) {
  size_t required_capacity;
  size_t new_capacity;
  char *new_text;

  required_capacity = builder->size + additional_size + 1U;
  if (required_capacity <= builder->capacity) {
    return CONFIT_OK;
  }

  new_capacity = builder->capacity == 0U ? 1024U : builder->capacity;
  while (new_capacity < required_capacity) {
    new_capacity *= 2U;
  }

  new_text = (char *)realloc(builder->text, new_capacity);
  if (new_text == 0) {
    return CONFIT_ERR_INTERNAL;
  }

  builder->text = new_text;
  builder->capacity = new_capacity;
  return CONFIT_OK;
}

static ConfitStatus confit_header_append(ConfitHeaderBuilder *builder,
                                         const char *text) {
  const size_t text_size = strlen(text);
  ConfitStatus status;

  status = confit_header_reserve(builder, text_size);
  if (status != CONFIT_OK) {
    return status;
  }

  memcpy(builder->text + builder->size, text, text_size);
  builder->size += text_size;
  builder->text[builder->size] = '\0';
  return CONFIT_OK;
}

static ConfitStatus confit_header_append_char(ConfitHeaderBuilder *builder,
                                              char value) {
  char text[2];

  text[0] = value;
  text[1] = '\0';
  return confit_header_append(builder, text);
}

static int confit_header_is_lower(char value) {
  return value >= 'a' && value <= 'z';
}

static int confit_header_is_upper(char value) {
  return value >= 'A' && value <= 'Z';
}

static int confit_header_is_digit(char value) {
  return value >= '0' && value <= '9';
}

static int confit_header_is_ident(char value) {
  return confit_header_is_lower(value) || confit_header_is_upper(value) ||
         confit_header_is_digit(value);
}

static char confit_header_to_upper(char value) {
  if (confit_header_is_lower(value)) {
    return (char)(value - 'a' + 'A');
  }
  return value;
}

static ConfitStatus confit_header_append_macro_fragment(
    ConfitHeaderBuilder *builder, const char *text) {
  size_t index;
  int last_was_separator;

  if (text == 0 || text[0] == '\0') {
    return confit_header_append(builder, "UNKNOWN");
  }

  last_was_separator = 1;
  for (index = 0U; text[index] != '\0'; ++index) {
    char value = text[index];

    if (confit_header_is_ident(value)) {
      ConfitStatus status =
          confit_header_append_char(builder, confit_header_to_upper(value));
      if (status != CONFIT_OK) {
        return status;
      }
      last_was_separator = 0;
      continue;
    }

    if (!last_was_separator) {
      ConfitStatus status = confit_header_append_char(builder, '_');
      if (status != CONFIT_OK) {
        return status;
      }
      last_was_separator = 1;
    }
  }

  if (builder->size > 0U && builder->text[builder->size - 1U] == '_') {
    builder->size -= 1U;
    builder->text[builder->size] = '\0';
  }
  return CONFIT_OK;
}

static ConfitStatus confit_header_append_project_prefix(
    ConfitHeaderBuilder *builder, const ConfitProject *project) {
  ConfitStatus status;

  status = confit_header_append_macro_fragment(builder, project->name);
  if (status != CONFIT_OK) {
    return status;
  }
  return confit_header_append(builder, "_CONFIG_");
}

static const char *confit_header_option_tail(const ConfitProject *project,
                                             const char *option_id) {
  const size_t project_size = project->name != 0 ? strlen(project->name) : 0U;

  if (project_size > 0U && strncmp(option_id, project->name, project_size) == 0 &&
      option_id[project_size] == '.') {
    return option_id + project_size + 1U;
  }
  return option_id;
}

static ConfitStatus confit_header_append_macro_name(
    ConfitHeaderBuilder *builder, const ConfitProject *project,
    const char *option_id) {
  ConfitStatus status;

  status = confit_header_append_project_prefix(builder, project);
  if (status != CONFIT_OK) {
    return status;
  }
  return confit_header_append_macro_fragment(
      builder, confit_header_option_tail(project, option_id));
}

static ConfitStatus confit_header_append_uint64(ConfitHeaderBuilder *builder,
                                                uint64_t value) {
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
    status = confit_header_append_char(builder, digits[count]);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_header_append_int64(ConfitHeaderBuilder *builder,
                                               int64_t value) {
  uint64_t magnitude;
  ConfitStatus status;

  if (value < 0) {
    status = confit_header_append(builder, "-");
    if (status != CONFIT_OK) {
      return status;
    }
    magnitude = (uint64_t)(-(value + 1)) + 1U;
  } else {
    magnitude = (uint64_t)value;
  }
  return confit_header_append_uint64(builder, magnitude);
}

static char confit_header_hex_digit(unsigned int value) {
  return (char)(value < 10U ? '0' + value : 'A' + (value - 10U));
}

static ConfitStatus confit_header_append_hex_uint64(
    ConfitHeaderBuilder *builder, uint64_t value, int force_width) {
  int shift;
  int started;
  ConfitStatus status;

  status = confit_header_append(builder, "0x");
  if (status != CONFIT_OK) {
    return status;
  }

  started = 0;
  for (shift = 60; shift >= 0; shift -= 4) {
    const unsigned int digit = (unsigned int)((value >> (unsigned int)shift) &
                                             UINT64_C(0xF));

    if (digit != 0U || started || shift == 0 ||
        (force_width && shift < 32)) {
      status = confit_header_append_char(builder,
                                         confit_header_hex_digit(digit));
      if (status != CONFIT_OK) {
        return status;
      }
      started = 1;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_header_append_float(ConfitHeaderBuilder *builder,
                                               double value) {
  uint64_t whole;
  double fraction;
  size_t fraction_begin;
  size_t digit_index;
  ConfitStatus status;

  if (value < 0.0) {
    status = confit_header_append(builder, "-");
    if (status != CONFIT_OK) {
      return status;
    }
    value = -value;
  }

  whole = (uint64_t)value;
  status = confit_header_append_uint64(builder, whole);
  if (status != CONFIT_OK) {
    return status;
  }

  fraction = value - (double)whole;
  status = confit_header_append(builder, ".");
  if (status != CONFIT_OK) {
    return status;
  }
  if (fraction <= 0.0) {
    return confit_header_append(builder, "0");
  }

  fraction_begin = builder->size;
  for (digit_index = 0U; digit_index < 6U; ++digit_index) {
    int digit;

    fraction *= 10.0;
    digit = (int)fraction;
    status = confit_header_append_char(builder, (char)('0' + digit));
    if (status != CONFIT_OK) {
      return status;
    }
    fraction -= (double)digit;
  }

  while (builder->size > fraction_begin &&
         builder->text[builder->size - 1U] == '0') {
    builder->size -= 1U;
    builder->text[builder->size] = '\0';
  }
  return CONFIT_OK;
}

static ConfitStatus confit_header_append_octal_escape(
    ConfitHeaderBuilder *builder, unsigned char value) {
  ConfitStatus status;

  status = confit_header_append_char(builder, '\\');
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_header_append_char(builder,
                                     (char)('0' + ((value >> 6U) & 7U)));
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_header_append_char(builder,
                                     (char)('0' + ((value >> 3U) & 7U)));
  if (status != CONFIT_OK) {
    return status;
  }
  return confit_header_append_char(builder, (char)('0' + (value & 7U)));
}

static ConfitStatus confit_header_append_string_literal(
    ConfitHeaderBuilder *builder, const char *text) {
  size_t index;
  ConfitStatus status;

  status = confit_header_append_char(builder, '"');
  if (status != CONFIT_OK) {
    return status;
  }

  if (text != 0) {
    for (index = 0U; text[index] != '\0'; ++index) {
      const unsigned char value = (unsigned char)text[index];

      switch (value) {
      case '"':
      case '\\':
        status = confit_header_append_char(builder, '\\');
        if (status == CONFIT_OK) {
          status = confit_header_append_char(builder, (char)value);
        }
        break;
      case '\n':
        status = confit_header_append(builder, "\\n");
        break;
      case '\r':
        status = confit_header_append(builder, "\\r");
        break;
      case '\t':
        status = confit_header_append(builder, "\\t");
        break;
      default:
        if (value < 32U || value >= 127U) {
          status = confit_header_append_octal_escape(builder, value);
        } else {
          status = confit_header_append_char(builder, (char)value);
        }
        break;
      }
      if (status != CONFIT_OK) {
        return status;
      }
    }
  }

  return confit_header_append_char(builder, '"');
}

static const ConfitOption *confit_header_find_option(
    const ConfitProject *project, const char *option_id) {
  size_t index;

  if (project == 0 || option_id == 0) {
    return 0;
  }

  for (index = 0U; index < project->option_count; ++index) {
    if (project->options[index].id != 0 &&
        strcmp(project->options[index].id, option_id) == 0) {
      return &project->options[index];
    }
  }
  return 0;
}

static ConfitStatus confit_header_append_value(
    ConfitHeaderBuilder *builder, const ConfitOption *option,
    const ConfitValue *value) {
  if (value == 0) {
    return confit_header_append(builder, "0");
  }

  switch (value->kind) {
  case CONFIT_VALUE_BOOL:
    return confit_header_append(builder, value->as.bool_value ? "1" : "0");
  case CONFIT_VALUE_INT:
    return confit_header_append_int64(builder, value->as.int_value);
  case CONFIT_VALUE_UINT: {
    ConfitStatus status;

    if (option != 0 && option->type == CONFIT_OPTION_TYPE_HEX) {
      status = confit_header_append_hex_uint64(builder, value->as.uint_value,
                                               1);
    } else {
      status = confit_header_append_uint64(builder, value->as.uint_value);
    }
    if (status != CONFIT_OK) {
      return status;
    }
    return confit_header_append(builder, "U");
  }
  case CONFIT_VALUE_FLOAT:
    return confit_header_append_float(builder, value->as.float_value);
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
  case CONFIT_VALUE_PATH:
    return confit_header_append_string_literal(builder,
                                               value->as.string_value);
  case CONFIT_VALUE_EMPTY:
  default:
    return confit_header_append(builder, "0");
  }
}

static ConfitStatus confit_header_append_define(
    ConfitHeaderBuilder *builder, const ConfitProject *project,
    const ConfitResolvedValue *resolved_value) {
  const ConfitOption *option =
      confit_header_find_option(project, resolved_value->option_id);
  ConfitStatus status;

  if (option == 0) {
    return CONFIT_ERR_SCHEMA;
  }

  status = confit_header_append(builder, "#define ");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_header_append_macro_name(builder, project,
                                           resolved_value->option_id);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_header_append(builder, " ");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_header_append_value(builder, option, &resolved_value->value);
  if (status != CONFIT_OK) {
    return status;
  }
  return confit_header_append(builder, "\n");
}

static ConfitStatus confit_header_append_source_hash_define(
    ConfitHeaderBuilder *builder, const ConfitProject *project,
    uint64_t source_hash) {
  ConfitStatus status;

  status = confit_header_append(builder, "#define ");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_header_append_project_prefix(builder, project);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_header_append(builder, "SOURCE_HASH ");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_header_append_hex_uint64(builder, source_hash, 0);
  if (status != CONFIT_OK) {
    return status;
  }
  return confit_header_append(builder, "ULL\n");
}

ConfitStatus confit_generate_config_header(
    const ConfitProject *project, const ConfitResolvedConfig *config,
    const ConfitConfigHeaderOptions *options, char **out_text,
    ConfitDiagnostic *diagnostic) {
  ConfitHeaderBuilder builder;
  uint64_t source_hash;
  const char *profile_name;
  const char *target_name;
  ConfitStatus status;
  size_t index;

  if (out_text == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "missing generated header output pointer");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_text = 0;

  if (project == 0 || project->name == 0 || config == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid config header generator argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  source_hash = 0U;
  status = confit_resolved_config_hash(config, &source_hash);
  if (status != CONFIT_OK) {
    confit_diagnostic_set(diagnostic, status, 0, 0, 0,
                          "failed to hash resolved config");
    return status;
  }

  profile_name = options != 0 && options->profile_name != 0
                     ? options->profile_name
                     : "unknown";
  target_name = options != 0 && options->target_name != 0
                    ? options->target_name
                    : "unknown";

  confit_header_builder_init(&builder);

#define CONFIT_HEADER_APPEND(fragment)                                          \
  do {                                                                          \
    status = confit_header_append(&builder, (fragment));                        \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

#define CONFIT_HEADER_APPEND_SECTION(call_expr)                                 \
  do {                                                                          \
    status = (call_expr);                                                       \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

  CONFIT_HEADER_APPEND("/* Generated by Confit. Do not edit. */\n");
  CONFIT_HEADER_APPEND("/* project: ");
  CONFIT_HEADER_APPEND(project->name);
  CONFIT_HEADER_APPEND(" */\n");
  CONFIT_HEADER_APPEND("/* profile: ");
  CONFIT_HEADER_APPEND(profile_name);
  CONFIT_HEADER_APPEND(" */\n");
  CONFIT_HEADER_APPEND("/* target: ");
  CONFIT_HEADER_APPEND(target_name);
  CONFIT_HEADER_APPEND(" */\n");
  CONFIT_HEADER_APPEND("/* source hash: ");
  CONFIT_HEADER_APPEND_SECTION(
      confit_header_append_hex_uint64(&builder, source_hash, 0));
  CONFIT_HEADER_APPEND(" */\n\n");

  CONFIT_HEADER_APPEND("#ifndef ");
  CONFIT_HEADER_APPEND_SECTION(confit_header_append_macro_fragment(
      &builder, project->name));
  CONFIT_HEADER_APPEND("_GENERATED_CONFIG_H\n");
  CONFIT_HEADER_APPEND("#define ");
  CONFIT_HEADER_APPEND_SECTION(confit_header_append_macro_fragment(
      &builder, project->name));
  CONFIT_HEADER_APPEND("_GENERATED_CONFIG_H\n\n");

  for (index = 0U; index < config->value_count; ++index) {
    const ConfitResolvedValue *resolved_value = &config->values[index];

    status = confit_header_append_define(&builder, project, resolved_value);
    if (status != CONFIT_OK) {
      free(builder.text);
      confit_diagnostic_set(diagnostic, status, resolved_value->option_id, 0, 0,
                            "failed to emit config define");
      return status;
    }
  }

  CONFIT_HEADER_APPEND("\n");
  CONFIT_HEADER_APPEND_SECTION(
      confit_header_append_source_hash_define(&builder, project, source_hash));
  CONFIT_HEADER_APPEND("\n#endif /* ");
  CONFIT_HEADER_APPEND_SECTION(confit_header_append_macro_fragment(
      &builder, project->name));
  CONFIT_HEADER_APPEND("_GENERATED_CONFIG_H */\n");

#undef CONFIT_HEADER_APPEND
#undef CONFIT_HEADER_APPEND_SECTION

  *out_text = builder.text;
  return CONFIT_OK;
}

void confit_generator_string_free(char *text) { free(text); }
