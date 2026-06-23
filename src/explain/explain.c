#include "confit/explain.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct ConfitExplainBuilder {
  char *text;
  size_t size;
  size_t capacity;
} ConfitExplainBuilder;

static void confit_explain_builder_init(ConfitExplainBuilder *builder) {
  builder->text = 0;
  builder->size = 0U;
  builder->capacity = 0U;
}

static ConfitStatus confit_explain_builder_reserve(
    ConfitExplainBuilder *builder, size_t additional_size) {
  size_t required_capacity;
  size_t new_capacity;
  char *new_text;

  required_capacity = builder->size + additional_size + 1U;
  if (required_capacity <= builder->capacity) {
    return CONFIT_OK;
  }

  new_capacity = builder->capacity == 0U ? 512U : builder->capacity;
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

static ConfitStatus confit_explain_append(ConfitExplainBuilder *builder,
                                          const char *text) {
  const size_t text_size = strlen(text);
  ConfitStatus status;

  status = confit_explain_builder_reserve(builder, text_size);
  if (status != CONFIT_OK) {
    return status;
  }

  memcpy(builder->text + builder->size, text, text_size);
  builder->size += text_size;
  builder->text[builder->size] = '\0';
  return CONFIT_OK;
}

static ConfitStatus confit_explain_append_char(ConfitExplainBuilder *builder,
                                               char value) {
  char text[2];

  text[0] = value;
  text[1] = '\0';
  return confit_explain_append(builder, text);
}

static ConfitStatus confit_explain_append_uint64(
    ConfitExplainBuilder *builder, uint64_t value) {
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
    status = confit_explain_append_char(builder, digits[count]);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_explain_append_int64(ConfitExplainBuilder *builder,
                                                int64_t value) {
  uint64_t magnitude;
  ConfitStatus status;

  if (value < 0) {
    status = confit_explain_append(builder, "-");
    if (status != CONFIT_OK) {
      return status;
    }
    magnitude = (uint64_t)(-(value + 1)) + 1U;
  } else {
    magnitude = (uint64_t)value;
  }
  return confit_explain_append_uint64(builder, magnitude);
}

static ConfitStatus confit_explain_append_float(ConfitExplainBuilder *builder,
                                                double value) {
  uint64_t whole;
  double fraction;
  size_t fraction_begin;
  size_t digit_index;
  ConfitStatus status;

  if (value < 0.0) {
    status = confit_explain_append(builder, "-");
    if (status != CONFIT_OK) {
      return status;
    }
    value = -value;
  }

  whole = (uint64_t)value;
  status = confit_explain_append_uint64(builder, whole);
  if (status != CONFIT_OK) {
    return status;
  }

  fraction = value - (double)whole;
  if (fraction <= 0.0) {
    return CONFIT_OK;
  }

  status = confit_explain_append(builder, ".");
  if (status != CONFIT_OK) {
    return status;
  }

  fraction_begin = builder->size;
  for (digit_index = 0U; digit_index < 6U; ++digit_index) {
    int digit;

    fraction *= 10.0;
    digit = (int)fraction;
    status = confit_explain_append_char(builder, (char)('0' + digit));
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

static ConfitStatus confit_explain_append_value(
    ConfitExplainBuilder *builder, const ConfitValue *value) {
  if (value == 0) {
    return confit_explain_append(builder, "null");
  }

  switch (value->kind) {
  case CONFIT_VALUE_BOOL:
    return confit_explain_append(builder,
                                 value->as.bool_value ? "true" : "false");
  case CONFIT_VALUE_INT:
    return confit_explain_append_int64(builder, value->as.int_value);
  case CONFIT_VALUE_UINT:
    return confit_explain_append_uint64(builder, value->as.uint_value);
  case CONFIT_VALUE_FLOAT:
    return confit_explain_append_float(builder, value->as.float_value);
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
  case CONFIT_VALUE_PATH:
    return confit_explain_append(builder, value->as.string_value != 0
                                              ? value->as.string_value
                                              : "");
  case CONFIT_VALUE_EMPTY:
  default:
    return confit_explain_append(builder, "null");
  }
}

static int confit_explain_value_is_active(const ConfitValue *value) {
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

static const char *confit_explain_activity_reason(const ConfitValue *value) {
  if (value == 0) {
    return "disabled because value is missing";
  }

  switch (value->kind) {
  case CONFIT_VALUE_BOOL:
    return value->as.bool_value ? "enabled because resolved bool is true"
                                : "disabled because resolved bool is false";
  case CONFIT_VALUE_INT:
  case CONFIT_VALUE_UINT:
  case CONFIT_VALUE_FLOAT:
    return confit_explain_value_is_active(value)
               ? "enabled because resolved number is non-zero"
               : "disabled because resolved number is zero";
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
  case CONFIT_VALUE_PATH:
    return confit_explain_value_is_active(value)
               ? "enabled because resolved text is non-empty"
               : "disabled because resolved text is empty";
  case CONFIT_VALUE_EMPTY:
  default:
    return "disabled because value is empty";
  }
}

static const ConfitOption *confit_explain_find_option(
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

static ConfitStatus confit_explain_append_trace_value(
    ConfitExplainBuilder *builder, const ConfitResolvedConfig *config,
    const char *option_id) {
  const ConfitResolvedValue *value =
      confit_resolved_config_find(config, option_id);
  ConfitStatus status;

  status = confit_explain_append(builder, option_id);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_explain_append(builder, " = ");
  if (status != CONFIT_OK) {
    return status;
  }
  if (value == 0) {
    status = confit_explain_append(builder, "missing (inactive)");
    return status;
  }
  status = confit_explain_append_value(builder, &value->value);
  if (status != CONFIT_OK) {
    return status;
  }
  return confit_explain_append(builder,
                               confit_explain_value_is_active(&value->value)
                                   ? " (active)"
                                   : " (inactive)");
}

static ConfitStatus confit_explain_append_outgoing_section(
    ConfitExplainBuilder *builder, const ConfitResolvedConfig *config,
    const ConfitOption *option, ConfitDependencyKind kind, const char *label) {
  size_t index;
  size_t count;
  ConfitStatus status;

  status = confit_explain_append(builder, label);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_explain_append(builder, ":\n");
  if (status != CONFIT_OK) {
    return status;
  }

  count = 0U;
  for (index = 0U; index < option->dependency_count; ++index) {
    if (option->dependencies[index].kind == kind) {
      status = confit_explain_append(builder, "  ");
      if (status != CONFIT_OK) {
        return status;
      }
      status = confit_explain_append_trace_value(
          builder, config, option->dependencies[index].option_id);
      if (status != CONFIT_OK) {
        return status;
      }
      status = confit_explain_append(builder, "\n");
      if (status != CONFIT_OK) {
        return status;
      }
      count += 1U;
    }
  }

  if (count == 0U) {
    status = confit_explain_append(builder, "  none\n");
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return confit_explain_append(builder, "\n");
}

static ConfitStatus confit_explain_append_incoming_section(
    ConfitExplainBuilder *builder, const ConfitProject *project,
    const ConfitResolvedConfig *config, const char *option_id,
    ConfitDependencyKind kind, const char *label) {
  size_t option_index;
  size_t count;
  ConfitStatus status;

  status = confit_explain_append(builder, label);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_explain_append(builder, ":\n");
  if (status != CONFIT_OK) {
    return status;
  }

  count = 0U;
  for (option_index = 0U; option_index < project->option_count;
       ++option_index) {
    const ConfitOption *source = &project->options[option_index];
    size_t dependency_index;

    for (dependency_index = 0U; dependency_index < source->dependency_count;
         ++dependency_index) {
      const ConfitDependencyRef *dependency =
          &source->dependencies[dependency_index];

      if (dependency->kind == kind && dependency->option_id != 0 &&
          strcmp(dependency->option_id, option_id) == 0) {
        status = confit_explain_append(builder, "  ");
        if (status != CONFIT_OK) {
          return status;
        }
        status = confit_explain_append_trace_value(builder, config, source->id);
        if (status != CONFIT_OK) {
          return status;
        }
        status = confit_explain_append(builder, "\n");
        if (status != CONFIT_OK) {
          return status;
        }
        count += 1U;
      }
    }
  }

  if (count == 0U) {
    status = confit_explain_append(builder, "  none\n");
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

ConfitStatus confit_explain_option(const ConfitProject *project,
                                   const ConfitResolvedConfig *config,
                                   const char *option_id, char **out_text,
                                   ConfitDiagnostic *diagnostic) {
  ConfitExplainBuilder builder;
  const ConfitOption *option;
  const ConfitResolvedValue *resolved_value;
  const char *source;
  ConfitStatus status;

  if (out_text == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, option_id, 0,
                          0, "missing explanation output pointer");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_text = 0;

  if (project == 0 || config == 0 || option_id == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, option_id, 0,
                          0, "invalid explanation argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  option = confit_explain_find_option(project, option_id);
  if (option == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option_id, 0, 0,
                          "unknown explained option");
    return CONFIT_ERR_SCHEMA;
  }

  resolved_value = confit_resolved_config_find(config, option_id);
  if (resolved_value == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, option_id, 0, 0,
                          "missing resolved option");
    return CONFIT_ERR_INTERNAL;
  }

  source = resolved_value->source != 0 ? resolved_value->source : "unknown";
  confit_explain_builder_init(&builder);

#define CONFIT_EXPLAIN_APPEND(fragment)                                         \
  do {                                                                          \
    status = confit_explain_append(&builder, (fragment));                       \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

#define CONFIT_EXPLAIN_APPEND_VALUE(value)                                      \
  do {                                                                          \
    status = confit_explain_append_value(&builder, (value));                    \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

#define CONFIT_EXPLAIN_APPEND_SECTION(call_expr)                                \
  do {                                                                          \
    status = (call_expr);                                                       \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

  CONFIT_EXPLAIN_APPEND("option: ");
  CONFIT_EXPLAIN_APPEND(option_id);
  CONFIT_EXPLAIN_APPEND("\nstate: ");
  CONFIT_EXPLAIN_APPEND(confit_explain_value_is_active(&resolved_value->value)
                            ? "enabled"
                            : "disabled");
  CONFIT_EXPLAIN_APPEND("\nvalue: ");
  CONFIT_EXPLAIN_APPEND_VALUE(&resolved_value->value);
  CONFIT_EXPLAIN_APPEND("\nset by: ");
  CONFIT_EXPLAIN_APPEND(source);
  CONFIT_EXPLAIN_APPEND("\n\nwhy:\n  ");
  CONFIT_EXPLAIN_APPEND(confit_explain_activity_reason(&resolved_value->value));
  CONFIT_EXPLAIN_APPEND("\n  value comes from ");
  CONFIT_EXPLAIN_APPEND(source);
  CONFIT_EXPLAIN_APPEND("\n\n");

  CONFIT_EXPLAIN_APPEND_SECTION(confit_explain_append_outgoing_section(
      &builder, config, option, CONFIT_DEPENDENCY_REQUIRES, "requires"));
  CONFIT_EXPLAIN_APPEND_SECTION(confit_explain_append_outgoing_section(
      &builder, config, option, CONFIT_DEPENDENCY_CONFLICTS, "conflicts"));
  CONFIT_EXPLAIN_APPEND_SECTION(confit_explain_append_outgoing_section(
      &builder, config, option, CONFIT_DEPENDENCY_RECOMMENDS, "recommends"));
  CONFIT_EXPLAIN_APPEND_SECTION(confit_explain_append_outgoing_section(
      &builder, config, option, CONFIT_DEPENDENCY_FORCES, "forces"));
  CONFIT_EXPLAIN_APPEND_SECTION(confit_explain_append_incoming_section(
      &builder, project, config, option_id, CONFIT_DEPENDENCY_REQUIRES,
      "required by"));
  CONFIT_EXPLAIN_APPEND("\n");
  CONFIT_EXPLAIN_APPEND_SECTION(confit_explain_append_incoming_section(
      &builder, project, config, option_id, CONFIT_DEPENDENCY_CONFLICTS,
      "conflicted by"));
  CONFIT_EXPLAIN_APPEND("\n");
  CONFIT_EXPLAIN_APPEND_SECTION(confit_explain_append_incoming_section(
      &builder, project, config, option_id, CONFIT_DEPENDENCY_RECOMMENDS,
      "recommended by"));
  CONFIT_EXPLAIN_APPEND("\n");
  CONFIT_EXPLAIN_APPEND_SECTION(confit_explain_append_incoming_section(
      &builder, project, config, option_id, CONFIT_DEPENDENCY_FORCES,
      "forced by"));

#undef CONFIT_EXPLAIN_APPEND
#undef CONFIT_EXPLAIN_APPEND_VALUE
#undef CONFIT_EXPLAIN_APPEND_SECTION

  *out_text = builder.text;
  return CONFIT_OK;
}

void confit_explain_string_free(char *text) { free(text); }
