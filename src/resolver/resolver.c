#include "confit/resolver.h"

#include <stdlib.h>
#include <string.h>

typedef struct ConfitResolverJsonBuilder {
  char *text;
  size_t size;
  size_t capacity;
} ConfitResolverJsonBuilder;

static char *confit_resolver_copy_string(const char *text) {
  char *copy;
  size_t size;

  if (text == 0) {
    return 0;
  }

  size = strlen(text);
  copy = (char *)malloc(size + 1U);
  if (copy == 0) {
    return 0;
  }
  memcpy(copy, text, size + 1U);
  return copy;
}

static ConfitStatus confit_resolver_replace_string(char **slot,
                                                   const char *text) {
  char *copy;

  copy = confit_resolver_copy_string(text);
  if (text != 0 && copy == 0) {
    return CONFIT_ERR_INTERNAL;
  }

  free(*slot);
  *slot = copy;
  return CONFIT_OK;
}

static ConfitResolvedConfig *confit_resolved_config_create(size_t value_count) {
  ConfitResolvedConfig *config;

  config = (ConfitResolvedConfig *)calloc(1U, sizeof(*config));
  if (config == 0) {
    return 0;
  }

  if (value_count > 0U) {
    config->values =
        (ConfitResolvedValue *)calloc(value_count, sizeof(config->values[0]));
    if (config->values == 0) {
      free(config);
      return 0;
    }
  }
  config->value_count = value_count;
  return config;
}

static void confit_resolved_value_clear(ConfitResolvedValue *value) {
  if (value == 0) {
    return;
  }

  free(value->option_id);
  confit_value_clear(&value->value);
  free(value->source);
  value->option_id = 0;
  value->source = 0;
}

void confit_resolved_config_free(ConfitResolvedConfig *config) {
  size_t index;

  if (config == 0) {
    return;
  }

  for (index = 0U; index < config->value_count; ++index) {
    confit_resolved_value_clear(&config->values[index]);
  }
  free(config->values);
  free(config);
}

static const ConfitOption *confit_resolver_find_option(
    const ConfitProject *project, const char *id) {
  size_t index;

  if (project == 0 || id == 0) {
    return 0;
  }

  for (index = 0U; index < project->option_count; ++index) {
    if (project->options[index].id != 0 &&
        strcmp(project->options[index].id, id) == 0) {
      return &project->options[index];
    }
  }
  return 0;
}

static const ConfitProfile *confit_resolver_find_profile(
    const ConfitProject *project, const char *name) {
  size_t index;

  if (project == 0 || name == 0) {
    return 0;
  }

  for (index = 0U; index < project->profile_count; ++index) {
    if (project->profiles[index].name != 0 &&
        strcmp(project->profiles[index].name, name) == 0) {
      return &project->profiles[index];
    }
  }
  return 0;
}

static const ConfitTarget *confit_resolver_find_target(
    const ConfitProject *project, const char *name) {
  size_t index;

  if (project == 0 || name == 0) {
    return 0;
  }

  for (index = 0U; index < project->target_count; ++index) {
    if (project->targets[index].name != 0 &&
        strcmp(project->targets[index].name, name) == 0) {
      return &project->targets[index];
    }
  }
  return 0;
}

const ConfitResolvedValue *confit_resolved_config_find(
    const ConfitResolvedConfig *config, const char *option_id) {
  size_t index;

  if (config == 0 || option_id == 0) {
    return 0;
  }

  for (index = 0U; index < config->value_count; ++index) {
    if (config->values[index].option_id != 0 &&
        strcmp(config->values[index].option_id, option_id) == 0) {
      return &config->values[index];
    }
  }
  return 0;
}

static ConfitResolvedValue *confit_resolver_find_mutable_value(
    ConfitResolvedConfig *config, const char *option_id) {
  size_t index;

  if (config == 0 || option_id == 0) {
    return 0;
  }

  for (index = 0U; index < config->value_count; ++index) {
    if (config->values[index].option_id != 0 &&
        strcmp(config->values[index].option_id, option_id) == 0) {
      return &config->values[index];
    }
  }
  return 0;
}

static ConfitStatus confit_resolver_validate_value_for_option(
    const ConfitOption *option, const ConfitValue *value) {
  ConfitOption validation_option;

  if (option == 0 || value == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  validation_option = *option;
  validation_option.default_value = *value;
  return confit_option_validate_default(&validation_option);
}

static ConfitStatus confit_resolver_set_value(ConfitResolvedConfig *config,
                                              const ConfitOption *option,
                                              const ConfitValue *value,
                                              const char *source,
                                              ConfitDiagnostic *diagnostic) {
  ConfitResolvedValue *resolved_value;
  ConfitStatus status;

  if (config == 0 || option == 0 || value == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid resolved value argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status = confit_resolver_validate_value_for_option(option, value);
  if (status != CONFIT_OK) {
    confit_diagnostic_set(diagnostic, status, option->id, 0, 0,
                          "invalid resolved value");
    return status;
  }

  resolved_value = confit_resolver_find_mutable_value(config, option->id);
  if (resolved_value == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, option->id, 0, 0,
                          "missing resolved value slot");
    return CONFIT_ERR_INTERNAL;
  }

  status = confit_value_copy(&resolved_value->value, value);
  if (status != CONFIT_OK) {
    confit_diagnostic_set(diagnostic, status, option->id, 0, 0,
                          "failed to copy resolved value");
    return status;
  }

  status = confit_resolver_replace_string(&resolved_value->source, source);
  if (status != CONFIT_OK) {
    confit_diagnostic_set(diagnostic, status, option->id, 0, 0,
                          "failed to copy resolved source");
  }
  return status;
}

static ConfitStatus confit_resolver_apply_named_values(
    ConfitResolvedConfig *config, const ConfitProject *project,
    const ConfitNamedValue *values, size_t value_count,
    const char *fallback_source, ConfitDiagnostic *diagnostic) {
  size_t index;

  for (index = 0U; index < value_count; ++index) {
    const ConfitOption *option =
        confit_resolver_find_option(project, values[index].option_id);
    const char *source =
        values[index].source != 0 ? values[index].source : fallback_source;
    ConfitStatus status;

    if (option == 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                            values[index].option_id, 0, 0,
                            "unknown resolved option");
      return CONFIT_ERR_SCHEMA;
    }

    status = confit_resolver_set_value(config, option, &values[index].value,
                                       source, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
  }

  return CONFIT_OK;
}

static int confit_resolver_value_is_active(const ConfitValue *value) {
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
  default:
    return 0;
  }
}

static ConfitStatus confit_resolver_validate_dependencies(
    const ConfitResolvedConfig *config, const ConfitProject *project,
    ConfitDiagnostic *diagnostic) {
  size_t option_index;

  for (option_index = 0U; option_index < project->option_count;
       ++option_index) {
    const ConfitOption *option = &project->options[option_index];
    const ConfitResolvedValue *source_value =
        confit_resolved_config_find(config, option->id);
    size_t dependency_index;

    if (!confit_resolver_value_is_active(source_value != 0 ? &source_value->value
                                                           : 0)) {
      continue;
    }

    for (dependency_index = 0U; dependency_index < option->dependency_count;
         ++dependency_index) {
      const ConfitDependencyRef *dependency =
          &option->dependencies[dependency_index];
      const ConfitResolvedValue *target_value =
          confit_resolved_config_find(config, dependency->option_id);
      const int target_active = confit_resolver_value_is_active(
          target_value != 0 ? &target_value->value : 0);

      if (dependency->kind == CONFIT_DEPENDENCY_REQUIRES && !target_active) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_DEPENDENCY, option->id, 0,
                              0, "dependency not satisfied");
        return CONFIT_ERR_DEPENDENCY;
      }
      if (dependency->kind == CONFIT_DEPENDENCY_CONFLICTS && target_active) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_CONFLICT, option->id, 0,
                              0, "conflict active");
        return CONFIT_ERR_CONFLICT;
      }
    }
  }

  return CONFIT_OK;
}

static int confit_resolver_resolved_value_compare(const void *left,
                                                  const void *right) {
  const ConfitResolvedValue *left_value = (const ConfitResolvedValue *)left;
  const ConfitResolvedValue *right_value = (const ConfitResolvedValue *)right;
  return strcmp(left_value->option_id, right_value->option_id);
}

static void confit_resolver_sort_values(ConfitResolvedConfig *config) {
  if (config != 0 && config->value_count > 1U) {
    qsort(config->values, config->value_count, sizeof(config->values[0]),
          confit_resolver_resolved_value_compare);
  }
}

static ConfitStatus confit_resolver_seed_defaults(
    ConfitResolvedConfig *config, const ConfitProject *project,
    ConfitDiagnostic *diagnostic) {
  size_t index;

  for (index = 0U; index < project->option_count; ++index) {
    ConfitStatus status;

    config->values[index].option_id =
        confit_resolver_copy_string(project->options[index].id);
    if (config->values[index].option_id == 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL,
                            project->options[index].id, 0, 0,
                            "failed to copy resolved option id");
      return CONFIT_ERR_INTERNAL;
    }
    confit_value_init(&config->values[index].value);
    status = confit_resolver_set_value(config, &project->options[index],
                                       &project->options[index].default_value,
                                       "default", diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
  }

  return CONFIT_OK;
}

static ConfitStatus confit_resolver_build_profile_stack(
    const ConfitProject *project, const char *profile_name,
    const ConfitProfile ***out_stack, size_t *out_count,
    ConfitDiagnostic *diagnostic) {
  const ConfitProfile **stack;
  const ConfitProfile *profile;
  size_t count;

  *out_stack = 0;
  *out_count = 0U;
  if (profile_name == 0) {
    return CONFIT_OK;
  }

  profile = confit_resolver_find_profile(project, profile_name);
  if (profile == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, profile_name, 0, 0,
                          "unknown profile");
    return CONFIT_ERR_SCHEMA;
  }

  stack = (const ConfitProfile **)calloc(project->profile_count,
                                         sizeof(stack[0]));
  if (project->profile_count > 0U && stack == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, profile_name, 0, 0,
                          "failed to allocate profile stack");
    return CONFIT_ERR_INTERNAL;
  }

  count = 0U;
  while (profile != 0) {
    size_t index;

    for (index = 0U; index < count; ++index) {
      if (stack[index] == profile) {
        free(stack);
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, profile->name, 0,
                              0, "profile base cycle");
        return CONFIT_ERR_SCHEMA;
      }
    }
    if (count >= project->profile_count) {
      free(stack);
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, profile->name, 0, 0,
                            "profile base cycle");
      return CONFIT_ERR_SCHEMA;
    }
    stack[count] = profile;
    count += 1U;

    if (profile->base != 0) {
      const ConfitProfile *base_profile =
          confit_resolver_find_profile(project, profile->base);
      if (base_profile == 0) {
        free(stack);
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, profile->base, 0,
                              0, "unknown base profile");
        return CONFIT_ERR_SCHEMA;
      }
      profile = base_profile;
    } else {
      profile = 0;
    }
  }

  *out_stack = stack;
  *out_count = count;
  return CONFIT_OK;
}

static const char *confit_resolver_select_target_name(
    const ConfitProfile *const *profile_stack, size_t profile_count,
    const char *target_name) {
  size_t index;

  if (target_name != 0) {
    return target_name;
  }

  for (index = 0U; index < profile_count; ++index) {
    if (profile_stack[index]->target != 0) {
      return profile_stack[index]->target;
    }
  }
  return 0;
}

ConfitStatus confit_resolver_resolve(
    const ConfitProject *project, const char *profile_name,
    const char *target_name, const ConfitNamedValue *user_values,
    size_t user_value_count, ConfitResolvedConfig **out_config,
    ConfitDiagnostic *diagnostic) {
  ConfitResolvedConfig *config;
  const ConfitProfile **profile_stack;
  size_t profile_count;
  const char *selected_target_name;
  const ConfitTarget *target;
  ConfitStatus status;
  size_t reverse_index;

  if (out_config == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "missing resolver output pointer");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_config = 0;

  if (project == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "missing project");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  if (user_value_count > 0U && user_values == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "missing user override values");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  config = confit_resolved_config_create(project->option_count);
  if (config == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0, 0,
                          "failed to allocate resolved config");
    return CONFIT_ERR_INTERNAL;
  }

  status = confit_resolver_seed_defaults(config, project, diagnostic);
  if (status != CONFIT_OK) {
    confit_resolved_config_free(config);
    return status;
  }

  profile_stack = 0;
  profile_count = 0U;
  status = confit_resolver_build_profile_stack(
      project, profile_name, &profile_stack, &profile_count, diagnostic);
  if (status != CONFIT_OK) {
    confit_resolved_config_free(config);
    return status;
  }

  reverse_index = profile_count;
  while (reverse_index > 1U) {
    const ConfitProfile *base_profile;

    reverse_index -= 1U;
    base_profile = profile_stack[reverse_index];
    status = confit_resolver_apply_named_values(
        config, project, base_profile->values, base_profile->value_count,
        base_profile->name, diagnostic);
    if (status != CONFIT_OK) {
      free(profile_stack);
      confit_resolved_config_free(config);
      return status;
    }
  }

  selected_target_name =
      confit_resolver_select_target_name(profile_stack, profile_count,
                                         target_name);
  target = selected_target_name != 0
               ? confit_resolver_find_target(project, selected_target_name)
               : 0;
  if (selected_target_name != 0 && target == 0) {
    free(profile_stack);
    confit_resolved_config_free(config);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, selected_target_name,
                          0, 0, "unknown target");
    return CONFIT_ERR_SCHEMA;
  }
  if (target != 0) {
    status = confit_resolver_apply_named_values(
        config, project, target->values, target->value_count, target->name,
        diagnostic);
    if (status != CONFIT_OK) {
      free(profile_stack);
      confit_resolved_config_free(config);
      return status;
    }
  }

  if (profile_count > 0U) {
    const ConfitProfile *selected_profile = profile_stack[0];

    status = confit_resolver_apply_named_values(
        config, project, selected_profile->values,
        selected_profile->value_count, selected_profile->name, diagnostic);
    if (status != CONFIT_OK) {
      free(profile_stack);
      confit_resolved_config_free(config);
      return status;
    }
  }

  free(profile_stack);
  status = confit_resolver_apply_named_values(config, project, user_values,
                                              user_value_count, "user",
                                              diagnostic);
  if (status != CONFIT_OK) {
    confit_resolved_config_free(config);
    return status;
  }

  confit_resolver_sort_values(config);
  status = confit_resolver_validate_dependencies(config, project, diagnostic);
  if (status != CONFIT_OK) {
    confit_resolved_config_free(config);
    return status;
  }

  *out_config = config;
  return CONFIT_OK;
}

static void confit_resolver_json_builder_init(
    ConfitResolverJsonBuilder *builder) {
  builder->text = 0;
  builder->size = 0U;
  builder->capacity = 0U;
}

static ConfitStatus confit_resolver_json_builder_reserve(
    ConfitResolverJsonBuilder *builder, size_t additional_size) {
  size_t required_capacity;
  size_t new_capacity;
  char *new_text;

  required_capacity = builder->size + additional_size + 1U;
  if (required_capacity <= builder->capacity) {
    return CONFIT_OK;
  }

  new_capacity = builder->capacity == 0U ? 256U : builder->capacity;
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

static ConfitStatus confit_resolver_json_append(
    ConfitResolverJsonBuilder *builder, const char *text) {
  const size_t size = strlen(text);
  ConfitStatus status;

  status = confit_resolver_json_builder_reserve(builder, size);
  if (status != CONFIT_OK) {
    return status;
  }

  memcpy(builder->text + builder->size, text, size);
  builder->size += size;
  builder->text[builder->size] = '\0';
  return CONFIT_OK;
}

static ConfitStatus confit_resolver_json_append_char(
    ConfitResolverJsonBuilder *builder, char value) {
  char text[2];

  text[0] = value;
  text[1] = '\0';
  return confit_resolver_json_append(builder, text);
}

static ConfitStatus confit_resolver_json_append_escaped(
    ConfitResolverJsonBuilder *builder, const char *text) {
  ConfitStatus status;
  size_t index;

  status = confit_resolver_json_append(builder, "\"");
  if (status != CONFIT_OK) {
    return status;
  }

  for (index = 0U; text[index] != '\0'; ++index) {
    switch (text[index]) {
    case '"':
    case '\\':
      status = confit_resolver_json_append(builder, "\\");
      if (status != CONFIT_OK) {
        return status;
      }
      status = confit_resolver_json_append_char(builder, text[index]);
      break;
    case '\n':
      status = confit_resolver_json_append(builder, "\\n");
      break;
    case '\r':
      status = confit_resolver_json_append(builder, "\\r");
      break;
    case '\t':
      status = confit_resolver_json_append(builder, "\\t");
      break;
    default:
      status = confit_resolver_json_append_char(builder, text[index]);
      break;
    }
    if (status != CONFIT_OK) {
      return status;
    }
  }

  return confit_resolver_json_append(builder, "\"");
}

static ConfitStatus confit_resolver_json_append_uint64(
    ConfitResolverJsonBuilder *builder, uint64_t value) {
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
    status = confit_resolver_json_append_char(builder, digits[count]);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_resolver_json_append_int64(
    ConfitResolverJsonBuilder *builder, int64_t value) {
  uint64_t magnitude;
  ConfitStatus status;

  if (value < 0) {
    status = confit_resolver_json_append(builder, "-");
    if (status != CONFIT_OK) {
      return status;
    }
    magnitude = (uint64_t)(-(value + 1)) + 1U;
  } else {
    magnitude = (uint64_t)value;
  }
  return confit_resolver_json_append_uint64(builder, magnitude);
}

static ConfitStatus confit_resolver_json_append_float(
    ConfitResolverJsonBuilder *builder, double value) {
  uint64_t whole;
  double fraction;
  size_t fraction_begin;
  size_t digit_index;
  ConfitStatus status;

  if (value < 0.0) {
    status = confit_resolver_json_append(builder, "-");
    if (status != CONFIT_OK) {
      return status;
    }
    value = -value;
  }

  whole = (uint64_t)value;
  status = confit_resolver_json_append_uint64(builder, whole);
  if (status != CONFIT_OK) {
    return status;
  }

  fraction = value - (double)whole;
  if (fraction <= 0.0) {
    return CONFIT_OK;
  }

  status = confit_resolver_json_append(builder, ".");
  if (status != CONFIT_OK) {
    return status;
  }

  fraction_begin = builder->size;
  for (digit_index = 0U; digit_index < 6U; ++digit_index) {
    int digit;

    fraction *= 10.0;
    digit = (int)fraction;
    status = confit_resolver_json_append_char(builder, (char)('0' + digit));
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

static ConfitStatus confit_resolver_json_append_value(
    ConfitResolverJsonBuilder *builder, const ConfitValue *value) {
  if (value == 0) {
    return confit_resolver_json_append(builder, "null");
  }

  switch (value->kind) {
  case CONFIT_VALUE_BOOL:
    return confit_resolver_json_append(builder,
                                       value->as.bool_value ? "true" : "false");
  case CONFIT_VALUE_INT:
    return confit_resolver_json_append_int64(builder, value->as.int_value);
  case CONFIT_VALUE_UINT:
    return confit_resolver_json_append_uint64(builder, value->as.uint_value);
  case CONFIT_VALUE_FLOAT:
    return confit_resolver_json_append_float(builder, value->as.float_value);
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
  case CONFIT_VALUE_PATH:
    return confit_resolver_json_append_escaped(builder,
                                               value->as.string_value);
  case CONFIT_VALUE_EMPTY:
  default:
    return confit_resolver_json_append(builder, "null");
  }
}

ConfitStatus confit_resolved_config_to_json(const ConfitResolvedConfig *config,
                                            char **out_json) {
  ConfitResolverJsonBuilder builder;
  ConfitStatus status;
  size_t index;

  if (config == 0 || out_json == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  *out_json = 0;
  confit_resolver_json_builder_init(&builder);

#define CONFIT_JSON_APPEND(fragment)                                            \
  do {                                                                          \
    status = confit_resolver_json_append(&builder, (fragment));                 \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

#define CONFIT_JSON_APPEND_ESCAPED(fragment)                                    \
  do {                                                                          \
    status = confit_resolver_json_append_escaped(&builder, (fragment));         \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

#define CONFIT_JSON_APPEND_VALUE(value)                                         \
  do {                                                                          \
    status = confit_resolver_json_append_value(&builder, (value));              \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

  CONFIT_JSON_APPEND("{\n");
  CONFIT_JSON_APPEND("  \"schema\": \"confit-resolved-v1\",\n");
  CONFIT_JSON_APPEND("  \"values\": [\n");
  for (index = 0U; index < config->value_count; ++index) {
    CONFIT_JSON_APPEND("    {\"id\": ");
    CONFIT_JSON_APPEND_ESCAPED(config->values[index].option_id);
    CONFIT_JSON_APPEND(", \"value\": ");
    CONFIT_JSON_APPEND_VALUE(&config->values[index].value);
    CONFIT_JSON_APPEND(", \"source\": ");
    CONFIT_JSON_APPEND_ESCAPED(config->values[index].source != 0
                                   ? config->values[index].source
                                   : "");
    CONFIT_JSON_APPEND("}");
    CONFIT_JSON_APPEND(index + 1U < config->value_count ? ",\n" : "\n");
  }
  CONFIT_JSON_APPEND("  ]\n");
  CONFIT_JSON_APPEND("}\n");

#undef CONFIT_JSON_APPEND
#undef CONFIT_JSON_APPEND_ESCAPED
#undef CONFIT_JSON_APPEND_VALUE

  *out_json = builder.text;
  return CONFIT_OK;
}

ConfitStatus confit_resolved_config_hash(const ConfitResolvedConfig *config,
                                         uint64_t *out_hash) {
  static const uint64_t offset_basis = UINT64_C(14695981039346656037);
  static const uint64_t prime = UINT64_C(1099511628211);
  char *json;
  uint64_t hash;
  size_t index;
  ConfitStatus status;

  if (out_hash == 0 || config == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  json = 0;
  status = confit_resolved_config_to_json(config, &json);
  if (status != CONFIT_OK) {
    return status;
  }

  hash = offset_basis;
  for (index = 0U; json[index] != '\0'; ++index) {
    hash ^= (unsigned char)json[index];
    hash *= prime;
  }
  confit_resolver_string_free(json);
  *out_hash = hash;
  return CONFIT_OK;
}

void confit_resolver_string_free(char *text) { free(text); }
