#include "confit/generator.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct ConfitReportBuilder {
  char *text;
  size_t size;
  size_t capacity;
} ConfitReportBuilder;

static void confit_report_builder_init(ConfitReportBuilder *builder) {
  builder->text = 0;
  builder->size = 0U;
  builder->capacity = 0U;
}

static ConfitStatus confit_report_reserve(ConfitReportBuilder *builder,
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

static ConfitStatus confit_report_append(ConfitReportBuilder *builder,
                                         const char *text) {
  const size_t text_size = strlen(text);
  ConfitStatus status;

  status = confit_report_reserve(builder, text_size);
  if (status != CONFIT_OK) {
    return status;
  }

  memcpy(builder->text + builder->size, text, text_size);
  builder->size += text_size;
  builder->text[builder->size] = '\0';
  return CONFIT_OK;
}

static ConfitStatus confit_report_append_char(ConfitReportBuilder *builder,
                                              char value) {
  char text[2];

  text[0] = value;
  text[1] = '\0';
  return confit_report_append(builder, text);
}

static ConfitStatus confit_report_append_uint64(ConfitReportBuilder *builder,
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
    status = confit_report_append_char(builder, digits[count]);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_report_append_int64(ConfitReportBuilder *builder,
                                               int64_t value) {
  uint64_t magnitude;
  ConfitStatus status;

  if (value < 0) {
    status = confit_report_append(builder, "-");
    if (status != CONFIT_OK) {
      return status;
    }
    magnitude = (uint64_t)(-(value + 1)) + 1U;
  } else {
    magnitude = (uint64_t)value;
  }
  return confit_report_append_uint64(builder, magnitude);
}

static char confit_report_hex_digit(unsigned int value) {
  return (char)(value < 10U ? '0' + value : 'A' + (value - 10U));
}

static ConfitStatus confit_report_append_hex_uint64(
    ConfitReportBuilder *builder, uint64_t value) {
  int shift;
  int started;
  ConfitStatus status;

  status = confit_report_append(builder, "0x");
  if (status != CONFIT_OK) {
    return status;
  }

  started = 0;
  for (shift = 60; shift >= 0; shift -= 4) {
    const unsigned int digit = (unsigned int)((value >> (unsigned int)shift) &
                                             UINT64_C(0xF));

    if (digit != 0U || started || shift == 0) {
      status = confit_report_append_char(builder,
                                         confit_report_hex_digit(digit));
      if (status != CONFIT_OK) {
        return status;
      }
      started = 1;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_report_append_float(ConfitReportBuilder *builder,
                                               double value) {
  uint64_t whole;
  double fraction;
  size_t fraction_begin;
  size_t digit_index;
  ConfitStatus status;

  if (value < 0.0) {
    status = confit_report_append(builder, "-");
    if (status != CONFIT_OK) {
      return status;
    }
    value = -value;
  }

  whole = (uint64_t)value;
  status = confit_report_append_uint64(builder, whole);
  if (status != CONFIT_OK) {
    return status;
  }

  fraction = value - (double)whole;
  if (fraction <= 0.0) {
    return CONFIT_OK;
  }

  status = confit_report_append(builder, ".");
  if (status != CONFIT_OK) {
    return status;
  }

  fraction_begin = builder->size;
  for (digit_index = 0U; digit_index < 6U; ++digit_index) {
    int digit;

    fraction *= 10.0;
    digit = (int)fraction;
    status = confit_report_append_char(builder, (char)('0' + digit));
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
  if (builder->size == fraction_begin) {
    builder->size -= 1U;
    builder->text[builder->size] = '\0';
  }
  return CONFIT_OK;
}

static ConfitStatus confit_report_append_json_escaped(
    ConfitReportBuilder *builder, const char *text) {
  size_t index;
  ConfitStatus status;

  status = confit_report_append(builder, "\"");
  if (status != CONFIT_OK) {
    return status;
  }

  if (text != 0) {
    for (index = 0U; text[index] != '\0'; ++index) {
      switch (text[index]) {
      case '"':
      case '\\':
        status = confit_report_append(builder, "\\");
        if (status == CONFIT_OK) {
          status = confit_report_append_char(builder, text[index]);
        }
        break;
      case '\n':
        status = confit_report_append(builder, "\\n");
        break;
      case '\r':
        status = confit_report_append(builder, "\\r");
        break;
      case '\t':
        status = confit_report_append(builder, "\\t");
        break;
      default:
        status = confit_report_append_char(builder, text[index]);
        break;
      }
      if (status != CONFIT_OK) {
        return status;
      }
    }
  }

  return confit_report_append(builder, "\"");
}

static ConfitStatus confit_report_append_json_value(
    ConfitReportBuilder *builder, const ConfitValue *value) {
  if (value == 0) {
    return confit_report_append(builder, "null");
  }

  switch (value->kind) {
  case CONFIT_VALUE_BOOL:
    return confit_report_append(builder,
                                value->as.bool_value ? "true" : "false");
  case CONFIT_VALUE_INT:
    return confit_report_append_int64(builder, value->as.int_value);
  case CONFIT_VALUE_UINT:
    return confit_report_append_uint64(builder, value->as.uint_value);
  case CONFIT_VALUE_FLOAT:
    return confit_report_append_float(builder, value->as.float_value);
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
  case CONFIT_VALUE_PATH:
    return confit_report_append_json_escaped(builder,
                                             value->as.string_value);
  case CONFIT_VALUE_EMPTY:
  default:
    return confit_report_append(builder, "null");
  }
}

static int confit_report_value_is_active(const ConfitValue *value) {
  if (value == 0) {
    return 0;
  }

  switch (value->kind) {
  case CONFIT_VALUE_BOOL:
    return value->as.bool_value != 0;
  case CONFIT_VALUE_INT:
    return value->as.int_value != 0;
  case CONFIT_VALUE_UINT:
    return value->as.uint_value != 0U;
  case CONFIT_VALUE_FLOAT:
    return value->as.float_value != 0.0;
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
  case CONFIT_VALUE_PATH:
    return value->as.string_value != 0 && value->as.string_value[0] != '\0';
  case CONFIT_VALUE_EMPTY:
  default:
    return 0;
  }
}

static const char *confit_report_activity_reason(const ConfitValue *value) {
  if (value == 0) {
    return "because value is missing";
  }

  switch (value->kind) {
  case CONFIT_VALUE_BOOL:
    return value->as.bool_value ? "because resolved bool is true"
                                : "because resolved bool is false";
  case CONFIT_VALUE_INT:
  case CONFIT_VALUE_UINT:
  case CONFIT_VALUE_FLOAT:
    return confit_report_value_is_active(value)
               ? "because resolved number is non-zero"
               : "because resolved number is zero";
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
  case CONFIT_VALUE_PATH:
    return confit_report_value_is_active(value)
               ? "because resolved text is non-empty"
               : "because resolved text is empty";
  case CONFIT_VALUE_EMPTY:
  default:
    return "because value is empty";
  }
}

static const ConfitOption *confit_report_find_option(
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

static const char *confit_report_profile_name(
    const ConfitReportOptions *options) {
  return options != 0 && options->profile_name != 0 ? options->profile_name
                                                    : "unknown";
}

static const char *confit_report_target_name(
    const ConfitReportOptions *options) {
  return options != 0 && options->target_name != 0 ? options->target_name
                                                   : "unknown";
}

static ConfitStatus confit_report_append_hash_string(
    ConfitReportBuilder *builder, uint64_t source_hash) {
  ConfitStatus status;

  status = confit_report_append(builder, "\"");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_report_append_hex_uint64(builder, source_hash);
  if (status != CONFIT_OK) {
    return status;
  }
  return confit_report_append(builder, "\"");
}

ConfitStatus confit_generate_report_json(
    const ConfitProject *project, const ConfitResolvedConfig *config,
    const ConfitReportOptions *options, char **out_json,
    ConfitDiagnostic *diagnostic) {
  ConfitReportBuilder builder;
  uint64_t source_hash;
  ConfitStatus status;
  size_t index;

  if (out_json == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "missing report output pointer");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_json = 0;

  if (project == 0 || project->name == 0 || config == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid report generator argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  source_hash = 0U;
  status = confit_resolved_config_hash(config, &source_hash);
  if (status != CONFIT_OK) {
    confit_diagnostic_set(diagnostic, status, 0, 0, 0,
                          "failed to hash resolved config");
    return status;
  }

  confit_report_builder_init(&builder);

#define CONFIT_REPORT_APPEND(fragment)                                          \
  do {                                                                          \
    status = confit_report_append(&builder, (fragment));                        \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

#define CONFIT_REPORT_APPEND_JSON(fragment)                                     \
  do {                                                                          \
    status = confit_report_append_json_escaped(&builder, (fragment));           \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

#define CONFIT_REPORT_APPEND_SECTION(call_expr)                                 \
  do {                                                                          \
    status = (call_expr);                                                       \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

  CONFIT_REPORT_APPEND("{\n");
  CONFIT_REPORT_APPEND("  \"schema\": \"confit-report-v1\",\n");
  CONFIT_REPORT_APPEND("  \"project\": ");
  CONFIT_REPORT_APPEND_JSON(project->name);
  CONFIT_REPORT_APPEND(",\n");
  CONFIT_REPORT_APPEND("  \"profile\": ");
  CONFIT_REPORT_APPEND_JSON(confit_report_profile_name(options));
  CONFIT_REPORT_APPEND(",\n");
  CONFIT_REPORT_APPEND("  \"target\": ");
  CONFIT_REPORT_APPEND_JSON(confit_report_target_name(options));
  CONFIT_REPORT_APPEND(",\n");
  CONFIT_REPORT_APPEND("  \"status\": \"ok\",\n");
  CONFIT_REPORT_APPEND("  \"source_hash\": ");
  CONFIT_REPORT_APPEND_SECTION(
      confit_report_append_hash_string(&builder, source_hash));
  CONFIT_REPORT_APPEND(",\n");
  CONFIT_REPORT_APPEND("  \"options\": [\n");

  for (index = 0U; index < config->value_count; ++index) {
    const ConfitResolvedValue *resolved = &config->values[index];
    const ConfitOption *option =
        confit_report_find_option(project, resolved->option_id);

    if (option == 0) {
      free(builder.text);
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, resolved->option_id,
                            0, 0, "unknown report option");
      return CONFIT_ERR_SCHEMA;
    }

    CONFIT_REPORT_APPEND("    {\"id\": ");
    CONFIT_REPORT_APPEND_JSON(resolved->option_id);
    CONFIT_REPORT_APPEND(", \"type\": ");
    CONFIT_REPORT_APPEND_JSON(confit_option_type_name(option->type));
    CONFIT_REPORT_APPEND(", \"value\": ");
    CONFIT_REPORT_APPEND_SECTION(
        confit_report_append_json_value(&builder, &resolved->value));
    CONFIT_REPORT_APPEND(", \"source\": ");
    CONFIT_REPORT_APPEND_JSON(resolved->source != 0 ? resolved->source : "");
    CONFIT_REPORT_APPEND("}");
    CONFIT_REPORT_APPEND(index + 1U < config->value_count ? ",\n" : "\n");
  }

  CONFIT_REPORT_APPEND("  ],\n");
  CONFIT_REPORT_APPEND("  \"compat\": []\n");
  CONFIT_REPORT_APPEND("}\n");

#undef CONFIT_REPORT_APPEND
#undef CONFIT_REPORT_APPEND_JSON
#undef CONFIT_REPORT_APPEND_SECTION

  *out_json = builder.text;
  return CONFIT_OK;
}

static ConfitStatus confit_report_append_explain_group(
    ConfitReportBuilder *builder, const ConfitResolvedConfig *config,
    int want_active) {
  size_t index;
  size_t count;
  ConfitStatus status;

  count = 0U;
  for (index = 0U; index < config->value_count; ++index) {
    const ConfitResolvedValue *resolved = &config->values[index];
    const int active = confit_report_value_is_active(&resolved->value);

    if (active != want_active) {
      continue;
    }

    status = confit_report_append(builder, "  ");
    if (status != CONFIT_OK) {
      return status;
    }
    status = confit_report_append(builder, resolved->option_id);
    if (status != CONFIT_OK) {
      return status;
    }
    status = confit_report_append(builder, "\n    ");
    if (status != CONFIT_OK) {
      return status;
    }
    status =
        confit_report_append(builder,
                             confit_report_activity_reason(&resolved->value));
    if (status != CONFIT_OK) {
      return status;
    }
    status = confit_report_append(builder, "\n    set by ");
    if (status != CONFIT_OK) {
      return status;
    }
    status = confit_report_append(builder,
                                  resolved->source != 0 ? resolved->source
                                                        : "unknown");
    if (status != CONFIT_OK) {
      return status;
    }
    status = confit_report_append(builder, "\n");
    if (status != CONFIT_OK) {
      return status;
    }
    count += 1U;
  }

  if (count == 0U) {
    status = confit_report_append(builder, "  none\n");
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

ConfitStatus confit_generate_explain_report(
    const ConfitProject *project, const ConfitResolvedConfig *config,
    const ConfitReportOptions *options, char **out_text,
    ConfitDiagnostic *diagnostic) {
  ConfitReportBuilder builder;
  ConfitStatus status;

  if (out_text == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "missing explain report output pointer");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_text = 0;

  if (project == 0 || config == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid explain report generator argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  confit_report_builder_init(&builder);

#define CONFIT_EXPLAIN_REPORT_APPEND(fragment)                                  \
  do {                                                                          \
    status = confit_report_append(&builder, (fragment));                        \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

#define CONFIT_EXPLAIN_REPORT_SECTION(call_expr)                                \
  do {                                                                          \
    status = (call_expr);                                                       \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

  CONFIT_EXPLAIN_REPORT_APPEND("profile: ");
  CONFIT_EXPLAIN_REPORT_APPEND(confit_report_profile_name(options));
  CONFIT_EXPLAIN_REPORT_APPEND("\n");
  CONFIT_EXPLAIN_REPORT_APPEND("target: ");
  CONFIT_EXPLAIN_REPORT_APPEND(confit_report_target_name(options));
  CONFIT_EXPLAIN_REPORT_APPEND("\n\n");
  CONFIT_EXPLAIN_REPORT_APPEND("enabled:\n");
  CONFIT_EXPLAIN_REPORT_SECTION(
      confit_report_append_explain_group(&builder, config, 1));
  CONFIT_EXPLAIN_REPORT_APPEND("\n");
  CONFIT_EXPLAIN_REPORT_APPEND("disabled:\n");
  CONFIT_EXPLAIN_REPORT_SECTION(
      confit_report_append_explain_group(&builder, config, 0));

#undef CONFIT_EXPLAIN_REPORT_APPEND
#undef CONFIT_EXPLAIN_REPORT_SECTION

  *out_text = builder.text;
  return CONFIT_OK;
}

static size_t confit_report_find_next_input(
    const ConfitInputFile *files, size_t file_count, const unsigned char *used) {
  size_t index;
  size_t best;

  best = (size_t)-1;
  for (index = 0U; index < file_count; ++index) {
    const char *path = files[index].path != 0 ? files[index].path : "";

    if (used[index]) {
      continue;
    }
    if (best == (size_t)-1 ||
        strcmp(path, files[best].path != 0 ? files[best].path : "") < 0) {
      best = index;
    }
  }
  return best;
}

ConfitStatus confit_generate_inputs_json(const ConfitProject *project,
                                         const ConfitReportOptions *options,
                                         char **out_json,
                                         ConfitDiagnostic *diagnostic) {
  ConfitReportBuilder builder;
  const ConfitInputFile *files;
  size_t file_count;
  unsigned char *used;
  size_t emitted;
  ConfitStatus status;

  if (out_json == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "missing inputs output pointer");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_json = 0;

  if (project == 0 || project->name == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid inputs generator argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  files = options != 0 ? options->input_files : 0;
  file_count = options != 0 ? options->input_file_count : 0U;
  if (file_count > 0U && files == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "missing input file records");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  used = file_count > 0U ? (unsigned char *)calloc(file_count, sizeof(used[0]))
                         : 0;
  if (file_count > 0U && used == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0, 0,
                          "failed to allocate input sort state");
    return CONFIT_ERR_INTERNAL;
  }

  confit_report_builder_init(&builder);

#define CONFIT_INPUTS_APPEND(fragment)                                          \
  do {                                                                          \
    status = confit_report_append(&builder, (fragment));                        \
    if (status != CONFIT_OK) {                                                  \
      free(used);                                                               \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

#define CONFIT_INPUTS_APPEND_JSON(fragment)                                     \
  do {                                                                          \
    status = confit_report_append_json_escaped(&builder, (fragment));           \
    if (status != CONFIT_OK) {                                                  \
      free(used);                                                               \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

  CONFIT_INPUTS_APPEND("{\n");
  CONFIT_INPUTS_APPEND("  \"schema\": \"confit-inputs-v1\",\n");
  CONFIT_INPUTS_APPEND("  \"project\": ");
  CONFIT_INPUTS_APPEND_JSON(project->name);
  CONFIT_INPUTS_APPEND(",\n");
  CONFIT_INPUTS_APPEND("  \"profile\": ");
  CONFIT_INPUTS_APPEND_JSON(confit_report_profile_name(options));
  CONFIT_INPUTS_APPEND(",\n");
  CONFIT_INPUTS_APPEND("  \"files\": [\n");

  for (emitted = 0U; emitted < file_count; ++emitted) {
    const size_t index = confit_report_find_next_input(files, file_count, used);
    const char *path = files[index].path != 0 ? files[index].path : "";
    const char *sha256 = files[index].sha256 != 0 ? files[index].sha256 : "";

    used[index] = 1U;
    CONFIT_INPUTS_APPEND("    {\"path\": ");
    CONFIT_INPUTS_APPEND_JSON(path);
    CONFIT_INPUTS_APPEND(", \"sha256\": ");
    CONFIT_INPUTS_APPEND_JSON(sha256);
    CONFIT_INPUTS_APPEND("}");
    CONFIT_INPUTS_APPEND(emitted + 1U < file_count ? ",\n" : "\n");
  }

  CONFIT_INPUTS_APPEND("  ]\n");
  CONFIT_INPUTS_APPEND("}\n");

#undef CONFIT_INPUTS_APPEND
#undef CONFIT_INPUTS_APPEND_JSON

  free(used);
  *out_json = builder.text;
  return CONFIT_OK;
}
