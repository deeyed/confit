#include "confit/model.h"

#include <stdlib.h>
#include <string.h>

static char *confit_model_copy_string(const char *text) {
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

static ConfitStatus confit_model_replace_string(char **slot,
                                                const char *text) {
  char *copy;

  copy = confit_model_copy_string(text);
  if (text != 0 && copy == 0) {
    return CONFIT_ERR_INTERNAL;
  }

  free(*slot);
  *slot = copy;
  return CONFIT_OK;
}

static ConfitStatus confit_model_append_string(char ***items,
                                               size_t *item_count,
                                               const char *text) {
  char **new_items;
  char *copy;

  if (items == 0 || item_count == 0 || text == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  copy = confit_model_copy_string(text);
  if (copy == 0) {
    return CONFIT_ERR_INTERNAL;
  }

  new_items =
      (char **)realloc(*items, (*item_count + 1U) * sizeof((*items)[0]));
  if (new_items == 0) {
    free(copy);
    return CONFIT_ERR_INTERNAL;
  }

  new_items[*item_count] = copy;
  *items = new_items;
  *item_count += 1U;
  return CONFIT_OK;
}

static void confit_model_string_array_clear(char **items, size_t item_count) {
  size_t index;

  for (index = 0U; index < item_count; ++index) {
    free(items[index]);
  }
  free(items);
}

static int confit_model_float_is_finite(double value) {
  return value == value && value <= 1.0e308 && value >= -1.0e308;
}

const char *confit_option_type_name(ConfitOptionType type) {
  switch (type) {
  case CONFIT_OPTION_TYPE_INVALID:
    return "invalid";
  case CONFIT_OPTION_TYPE_BOOL:
    return "bool";
  case CONFIT_OPTION_TYPE_INT:
    return "int";
  case CONFIT_OPTION_TYPE_UINT:
    return "uint";
  case CONFIT_OPTION_TYPE_HEX:
    return "hex";
  case CONFIT_OPTION_TYPE_STRING:
    return "string";
  case CONFIT_OPTION_TYPE_ENUM:
    return "enum";
  case CONFIT_OPTION_TYPE_FLOAT:
    return "float";
  case CONFIT_OPTION_TYPE_PATH:
    return "path";
  default:
    return "unknown";
  }
}

const char *confit_value_kind_name(ConfitValueKind kind) {
  switch (kind) {
  case CONFIT_VALUE_EMPTY:
    return "empty";
  case CONFIT_VALUE_BOOL:
    return "bool";
  case CONFIT_VALUE_INT:
    return "int";
  case CONFIT_VALUE_UINT:
    return "uint";
  case CONFIT_VALUE_STRING:
    return "string";
  case CONFIT_VALUE_ENUM:
    return "enum";
  case CONFIT_VALUE_FLOAT:
    return "float";
  case CONFIT_VALUE_PATH:
    return "path";
  default:
    return "unknown";
  }
}

const char *confit_dependency_kind_name(ConfitDependencyKind kind) {
  switch (kind) {
  case CONFIT_DEPENDENCY_REQUIRES:
    return "requires";
  case CONFIT_DEPENDENCY_CONFLICTS:
    return "conflicts";
  case CONFIT_DEPENDENCY_RECOMMENDS:
    return "recommends";
  case CONFIT_DEPENDENCY_FORCES:
    return "forces";
  case CONFIT_DEPENDENCY_VISIBLE_IF:
    return "visible_if";
  default:
    return "unknown";
  }
}

ConfitStatus confit_category_path_analyze(
    const char *path, ConfitCategoryPathInfo *out_info) {
  size_t index;
  size_t size;
  size_t depth;

  if (path == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  size = strlen(path);
  if (size == 0U || size > CONFIT_CATEGORY_PATH_MAX_LENGTH) {
    return CONFIT_ERR_SCHEMA;
  }
  if (path[0] == '/' || path[size - 1U] == '/') {
    return CONFIT_ERR_SCHEMA;
  }

  depth = 1U;
  for (index = 0U; index < size; ++index) {
    if (path[index] != '/') {
      continue;
    }
    if (index + 1U >= size || path[index + 1U] == '/') {
      return CONFIT_ERR_SCHEMA;
    }
    depth += 1U;
  }

  if (out_info != 0) {
    out_info->length = size;
    out_info->depth = depth;
  }
  return CONFIT_OK;
}

ConfitStatus confit_category_path_segment_at(const char *path, size_t index,
                                             const char **out_begin,
                                             size_t *out_size) {
  ConfitCategoryPathInfo info;
  size_t segment_index;
  const char *segment_begin;
  const char *cursor;

  if (out_begin == 0 || out_size == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  if (confit_category_path_analyze(path, &info) != CONFIT_OK ||
      index >= info.depth) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  segment_index = 0U;
  segment_begin = path;
  cursor = path;
  while (*cursor != '\0') {
    if (*cursor == '/') {
      if (segment_index == index) {
        *out_begin = segment_begin;
        *out_size = (size_t)(cursor - segment_begin);
        return CONFIT_OK;
      }
      segment_index += 1U;
      segment_begin = cursor + 1;
    }
    cursor += 1;
  }

  *out_begin = segment_begin;
  *out_size = (size_t)(cursor - segment_begin);
  return CONFIT_OK;
}

void confit_value_init(ConfitValue *value) {
  if (value == 0) {
    return;
  }

  value->kind = CONFIT_VALUE_EMPTY;
  value->as.string_value = 0;
}

void confit_value_clear(ConfitValue *value) {
  if (value == 0) {
    return;
  }

  if (value->kind == CONFIT_VALUE_STRING ||
      value->kind == CONFIT_VALUE_ENUM || value->kind == CONFIT_VALUE_PATH) {
    free(value->as.string_value);
  }
  confit_value_init(value);
}

void confit_value_set_bool(ConfitValue *value, int bool_value) {
  if (value == 0) {
    return;
  }

  confit_value_clear(value);
  value->kind = CONFIT_VALUE_BOOL;
  value->as.bool_value = bool_value != 0;
}

void confit_value_set_int(ConfitValue *value, int64_t int_value) {
  if (value == 0) {
    return;
  }

  confit_value_clear(value);
  value->kind = CONFIT_VALUE_INT;
  value->as.int_value = int_value;
}

void confit_value_set_uint(ConfitValue *value, uint64_t uint_value) {
  if (value == 0) {
    return;
  }

  confit_value_clear(value);
  value->kind = CONFIT_VALUE_UINT;
  value->as.uint_value = uint_value;
}

static ConfitStatus confit_value_set_owned_string(ConfitValue *value,
                                                  ConfitValueKind kind,
                                                  const char *text) {
  char *copy;

  if (value == 0 || text == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  copy = confit_model_copy_string(text);
  if (copy == 0) {
    return CONFIT_ERR_INTERNAL;
  }

  confit_value_clear(value);
  value->kind = kind;
  value->as.string_value = copy;
  return CONFIT_OK;
}

ConfitStatus confit_value_set_string(ConfitValue *value,
                                     const char *string_value) {
  return confit_value_set_owned_string(value, CONFIT_VALUE_STRING,
                                       string_value);
}

ConfitStatus confit_value_set_enum(ConfitValue *value,
                                   const char *enum_value) {
  return confit_value_set_owned_string(value, CONFIT_VALUE_ENUM, enum_value);
}

void confit_value_set_float(ConfitValue *value, double float_value) {
  if (value == 0) {
    return;
  }

  confit_value_clear(value);
  value->kind = CONFIT_VALUE_FLOAT;
  value->as.float_value = float_value;
}

ConfitStatus confit_value_set_path(ConfitValue *value,
                                   const char *path_value) {
  return confit_value_set_owned_string(value, CONFIT_VALUE_PATH, path_value);
}

ConfitStatus confit_value_copy(ConfitValue *out, const ConfitValue *input) {
  if (out == 0 || input == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  switch (input->kind) {
  case CONFIT_VALUE_EMPTY:
    confit_value_clear(out);
    return CONFIT_OK;
  case CONFIT_VALUE_BOOL:
    confit_value_set_bool(out, input->as.bool_value);
    return CONFIT_OK;
  case CONFIT_VALUE_INT:
    confit_value_set_int(out, input->as.int_value);
    return CONFIT_OK;
  case CONFIT_VALUE_UINT:
    confit_value_set_uint(out, input->as.uint_value);
    return CONFIT_OK;
  case CONFIT_VALUE_STRING:
    return confit_value_set_string(out, input->as.string_value);
  case CONFIT_VALUE_ENUM:
    return confit_value_set_enum(out, input->as.string_value);
  case CONFIT_VALUE_FLOAT:
    confit_value_set_float(out, input->as.float_value);
    return CONFIT_OK;
  case CONFIT_VALUE_PATH:
    return confit_value_set_path(out, input->as.string_value);
  default:
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
}

static void confit_named_value_init(ConfitNamedValue *named_value) {
  if (named_value == 0) {
    return;
  }

  named_value->option_id = 0;
  confit_value_init(&named_value->value);
  named_value->source = 0;
}

static void confit_named_value_clear(ConfitNamedValue *named_value) {
  if (named_value == 0) {
    return;
  }

  free(named_value->option_id);
  confit_value_clear(&named_value->value);
  free(named_value->source);
  confit_named_value_init(named_value);
}

static ConfitStatus confit_named_value_set(ConfitNamedValue *named_value,
                                           const char *option_id,
                                           const ConfitValue *value,
                                           const char *source) {
  ConfitStatus status;

  if (named_value == 0 || option_id == 0 || value == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status = confit_model_replace_string(&named_value->option_id, option_id);
  if (status != CONFIT_OK) {
    return status;
  }

  status = confit_value_copy(&named_value->value, value);
  if (status != CONFIT_OK) {
    return status;
  }

  return confit_model_replace_string(&named_value->source, source);
}

static ConfitStatus confit_named_value_append(ConfitNamedValue **values,
                                              size_t *value_count,
                                              const char *option_id,
                                              const ConfitValue *value,
                                              const char *source) {
  ConfitNamedValue *new_values;
  ConfitStatus status;

  if (values == 0 || value_count == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  new_values =
      (ConfitNamedValue *)realloc(*values,
                                  (*value_count + 1U) * sizeof((*values)[0]));
  if (new_values == 0) {
    return CONFIT_ERR_INTERNAL;
  }

  *values = new_values;
  confit_named_value_init(&(*values)[*value_count]);
  status =
      confit_named_value_set(&(*values)[*value_count], option_id, value, source);
  if (status != CONFIT_OK) {
    confit_named_value_clear(&(*values)[*value_count]);
    return status;
  }

  *value_count += 1U;
  return CONFIT_OK;
}

static void confit_named_value_array_clear(ConfitNamedValue *values,
                                           size_t value_count) {
  size_t index;

  for (index = 0U; index < value_count; ++index) {
    confit_named_value_clear(&values[index]);
  }
  free(values);
}

static void confit_dependency_ref_init(ConfitDependencyRef *dependency) {
  if (dependency == 0) {
    return;
  }

  dependency->kind = CONFIT_DEPENDENCY_REQUIRES;
  dependency->option_id = 0;
}

static void confit_dependency_ref_clear(ConfitDependencyRef *dependency) {
  if (dependency == 0) {
    return;
  }

  free(dependency->option_id);
  confit_dependency_ref_init(dependency);
}

static void confit_dependency_ref_array_clear(
    ConfitDependencyRef *dependencies, size_t dependency_count) {
  size_t index;

  for (index = 0U; index < dependency_count; ++index) {
    confit_dependency_ref_clear(&dependencies[index]);
  }
  free(dependencies);
}

static void confit_option_init(ConfitOption *option) {
  if (option == 0) {
    return;
  }

  option->id = 0;
  option->type = CONFIT_OPTION_TYPE_INVALID;
  confit_value_init(&option->default_value);
  option->has_range = 0;
  confit_value_init(&option->range_min);
  confit_value_init(&option->range_max);
  option->enum_values = 0;
  option->enum_value_count = 0;
  option->prompt = 0;
  option->category = 0;
  option->help = 0;
  option->tags = 0;
  option->tag_count = 0;
  option->deprecated_aliases = 0;
  option->deprecated_alias_count = 0;
  option->owner = 0;
  option->since = 0;
  option->stability = 0;
  option->deprecated = 0;
  option->replaced_by = 0;
  option->dependencies = 0;
  option->dependency_count = 0;
}

static void confit_option_clear(ConfitOption *option) {
  if (option == 0) {
    return;
  }

  free(option->id);
  confit_value_clear(&option->default_value);
  confit_option_clear_range(option);
  confit_model_string_array_clear(option->enum_values,
                                  option->enum_value_count);
  free(option->prompt);
  free(option->category);
  free(option->help);
  confit_model_string_array_clear(option->tags, option->tag_count);
  confit_model_string_array_clear(option->deprecated_aliases,
                                  option->deprecated_alias_count);
  free(option->owner);
  free(option->since);
  free(option->stability);
  free(option->replaced_by);
  confit_dependency_ref_array_clear(option->dependencies,
                                    option->dependency_count);
  confit_option_init(option);
}

static void confit_choice_init(ConfitChoice *choice) {
  if (choice == 0) {
    return;
  }

  choice->id = 0;
  choice->options = 0;
  choice->option_count = 0;
  choice->default_option = 0;
}

static void confit_choice_clear(ConfitChoice *choice) {
  if (choice == 0) {
    return;
  }

  free(choice->id);
  confit_model_string_array_clear(choice->options, choice->option_count);
  free(choice->default_option);
  confit_choice_init(choice);
}

static void confit_profile_init(ConfitProfile *profile) {
  if (profile == 0) {
    return;
  }

  profile->name = 0;
  profile->base = 0;
  profile->target = 0;
  profile->values = 0;
  profile->value_count = 0;
}

static void confit_profile_clear(ConfitProfile *profile) {
  if (profile == 0) {
    return;
  }

  free(profile->name);
  free(profile->base);
  free(profile->target);
  confit_named_value_array_clear(profile->values, profile->value_count);
  confit_profile_init(profile);
}

static void confit_target_init(ConfitTarget *target) {
  if (target == 0) {
    return;
  }

  target->name = 0;
  target->arch = 0;
  target->board = 0;
  target->claim_level = 0;
  target->values = 0;
  target->value_count = 0;
}

static void confit_target_clear(ConfitTarget *target) {
  if (target == 0) {
    return;
  }

  free(target->name);
  free(target->arch);
  free(target->board);
  free(target->claim_level);
  confit_named_value_array_clear(target->values, target->value_count);
  confit_target_init(target);
}

ConfitProject *confit_project_create(void) {
  ConfitProject *project;

  project = (ConfitProject *)calloc(1U, sizeof(*project));
  if (project == 0) {
    return 0;
  }

  project->schema_version = 0U;
  return project;
}

void confit_project_free(ConfitProject *project) {
  size_t index;

  if (project == 0) {
    return;
  }

  free(project->name);
  free(project->version);

  for (index = 0U; index < project->option_count; ++index) {
    confit_option_clear(&project->options[index]);
  }
  free(project->options);

  for (index = 0U; index < project->choice_count; ++index) {
    confit_choice_clear(&project->choices[index]);
  }
  free(project->choices);

  for (index = 0U; index < project->profile_count; ++index) {
    confit_profile_clear(&project->profiles[index]);
  }
  free(project->profiles);

  for (index = 0U; index < project->target_count; ++index) {
    confit_target_clear(&project->targets[index]);
  }
  free(project->targets);

  free(project);
}

ConfitStatus confit_project_set_identity(ConfitProject *project,
                                         const char *name,
                                         const char *version,
                                         unsigned schema_version) {
  ConfitStatus status;

  if (project == 0 || name == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status = confit_model_replace_string(&project->name, name);
  if (status != CONFIT_OK) {
    return status;
  }

  status = confit_model_replace_string(&project->version, version);
  if (status != CONFIT_OK) {
    return status;
  }

  project->schema_version = schema_version;
  return CONFIT_OK;
}

ConfitStatus confit_project_add_option(ConfitProject *project,
                                       ConfitOption **out_option) {
  ConfitOption *new_options;

  if (project == 0 || out_option == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  new_options =
      (ConfitOption *)realloc(project->options,
                              (project->option_count + 1U) *
                                  sizeof(project->options[0]));
  if (new_options == 0) {
    return CONFIT_ERR_INTERNAL;
  }

  project->options = new_options;
  *out_option = &project->options[project->option_count];
  confit_option_init(*out_option);
  project->option_count += 1U;
  return CONFIT_OK;
}

ConfitStatus confit_project_add_choice(ConfitProject *project,
                                       ConfitChoice **out_choice) {
  ConfitChoice *new_choices;

  if (project == 0 || out_choice == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  new_choices =
      (ConfitChoice *)realloc(project->choices,
                              (project->choice_count + 1U) *
                                  sizeof(project->choices[0]));
  if (new_choices == 0) {
    return CONFIT_ERR_INTERNAL;
  }

  project->choices = new_choices;
  *out_choice = &project->choices[project->choice_count];
  confit_choice_init(*out_choice);
  project->choice_count += 1U;
  return CONFIT_OK;
}

ConfitStatus confit_project_add_profile(ConfitProject *project,
                                        ConfitProfile **out_profile) {
  ConfitProfile *new_profiles;

  if (project == 0 || out_profile == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  new_profiles =
      (ConfitProfile *)realloc(project->profiles,
                               (project->profile_count + 1U) *
                                   sizeof(project->profiles[0]));
  if (new_profiles == 0) {
    return CONFIT_ERR_INTERNAL;
  }

  project->profiles = new_profiles;
  *out_profile = &project->profiles[project->profile_count];
  confit_profile_init(*out_profile);
  project->profile_count += 1U;
  return CONFIT_OK;
}

ConfitStatus confit_project_add_target(ConfitProject *project,
                                       ConfitTarget **out_target) {
  ConfitTarget *new_targets;

  if (project == 0 || out_target == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  new_targets =
      (ConfitTarget *)realloc(project->targets,
                              (project->target_count + 1U) *
                                  sizeof(project->targets[0]));
  if (new_targets == 0) {
    return CONFIT_ERR_INTERNAL;
  }

  project->targets = new_targets;
  *out_target = &project->targets[project->target_count];
  confit_target_init(*out_target);
  project->target_count += 1U;
  return CONFIT_OK;
}

ConfitOption *confit_project_find_option(ConfitProject *project,
                                         const char *id) {
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

ConfitStatus confit_option_set_identity(ConfitOption *option, const char *id,
                                        ConfitOptionType type) {
  ConfitStatus status;

  if (option == 0 || id == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status = confit_model_replace_string(&option->id, id);
  if (status != CONFIT_OK) {
    return status;
  }

  option->type = type;
  return CONFIT_OK;
}

ConfitStatus confit_option_set_metadata(ConfitOption *option,
                                        const char *prompt,
                                        const char *category,
                                        const char *help) {
  ConfitStatus status;
  const char *effective_category;

  if (option == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  effective_category = (category != 0 && category[0] == '\0') ? 0 : category;
  if (effective_category != 0 &&
      confit_category_path_analyze(effective_category, 0) != CONFIT_OK) {
    return CONFIT_ERR_SCHEMA;
  }

  status = confit_model_replace_string(&option->prompt, prompt);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_model_replace_string(&option->category, effective_category);
  if (status != CONFIT_OK) {
    return status;
  }
  return confit_model_replace_string(&option->help, help);
}

ConfitStatus confit_option_add_tag(ConfitOption *option, const char *tag) {
  if (option == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  return confit_model_append_string(&option->tags, &option->tag_count, tag);
}

ConfitStatus confit_option_add_deprecated_alias(ConfitOption *option,
                                                const char *alias) {
  if (option == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  return confit_model_append_string(&option->deprecated_aliases,
                                    &option->deprecated_alias_count, alias);
}

ConfitStatus confit_option_set_stability_metadata(ConfitOption *option,
                                                  const char *owner,
                                                  const char *since,
                                                  const char *stability) {
  ConfitStatus status;

  if (option == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status = confit_model_replace_string(&option->owner, owner);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_model_replace_string(&option->since, since);
  if (status != CONFIT_OK) {
    return status;
  }
  return confit_model_replace_string(&option->stability, stability);
}

ConfitStatus confit_option_set_deprecation(ConfitOption *option,
                                           int deprecated,
                                           const char *replaced_by) {
  ConfitStatus status;

  if (option == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status = confit_model_replace_string(&option->replaced_by, replaced_by);
  if (status != CONFIT_OK) {
    return status;
  }
  option->deprecated = deprecated != 0;
  return CONFIT_OK;
}

ConfitStatus confit_option_add_dependency(ConfitOption *option,
                                          ConfitDependencyKind kind,
                                          const char *option_id) {
  ConfitDependencyRef *new_dependencies;
  char *copy;

  if (option == 0 || option_id == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  copy = confit_model_copy_string(option_id);
  if (copy == 0) {
    return CONFIT_ERR_INTERNAL;
  }

  new_dependencies =
      (ConfitDependencyRef *)realloc(
          option->dependencies,
          (option->dependency_count + 1U) * sizeof(option->dependencies[0]));
  if (new_dependencies == 0) {
    free(copy);
    return CONFIT_ERR_INTERNAL;
  }

  option->dependencies = new_dependencies;
  confit_dependency_ref_init(&option->dependencies[option->dependency_count]);
  option->dependencies[option->dependency_count].kind = kind;
  option->dependencies[option->dependency_count].option_id = copy;
  option->dependency_count += 1U;
  return CONFIT_OK;
}

ConfitStatus confit_option_set_default(ConfitOption *option,
                                       const ConfitValue *value) {
  if (option == 0 || value == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  return confit_value_copy(&option->default_value, value);
}

ConfitStatus confit_option_set_range(ConfitOption *option,
                                     const ConfitValue *min_value,
                                     const ConfitValue *max_value) {
  ConfitStatus status;

  if (option == 0 || min_value == 0 || max_value == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status = confit_value_copy(&option->range_min, min_value);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_value_copy(&option->range_max, max_value);
  if (status != CONFIT_OK) {
    confit_value_clear(&option->range_min);
    return status;
  }
  option->has_range = 1;
  return CONFIT_OK;
}

void confit_option_clear_range(ConfitOption *option) {
  if (option == 0) {
    return;
  }

  confit_value_clear(&option->range_min);
  confit_value_clear(&option->range_max);
  option->has_range = 0;
}

ConfitStatus confit_option_add_enum_value(ConfitOption *option,
                                          const char *enum_value) {
  if (option == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  return confit_model_append_string(&option->enum_values,
                                    &option->enum_value_count, enum_value);
}

static int confit_option_enum_contains(const ConfitOption *option,
                                       const char *enum_value) {
  size_t index;

  if (option == 0 || enum_value == 0) {
    return 0;
  }

  for (index = 0U; index < option->enum_value_count; ++index) {
    if (strcmp(option->enum_values[index], enum_value) == 0) {
      return 1;
    }
  }
  return 0;
}

static ConfitStatus confit_option_validate_range(const ConfitOption *option) {
  const ConfitValue *value;

  if (option == 0 || !option->has_range ||
      option->default_value.kind == CONFIT_VALUE_EMPTY) {
    return CONFIT_OK;
  }

  value = &option->default_value;
  if (option->type == CONFIT_OPTION_TYPE_INT) {
    if (option->range_min.kind != CONFIT_VALUE_INT ||
        option->range_max.kind != CONFIT_VALUE_INT ||
        value->kind != CONFIT_VALUE_INT ||
        option->range_min.as.int_value > option->range_max.as.int_value ||
        value->as.int_value < option->range_min.as.int_value ||
        value->as.int_value > option->range_max.as.int_value) {
      return CONFIT_ERR_SCHEMA;
    }
    return CONFIT_OK;
  }

  if (option->type == CONFIT_OPTION_TYPE_UINT ||
      option->type == CONFIT_OPTION_TYPE_HEX) {
    if (option->range_min.kind != CONFIT_VALUE_UINT ||
        option->range_max.kind != CONFIT_VALUE_UINT ||
        value->kind != CONFIT_VALUE_UINT ||
        option->range_min.as.uint_value > option->range_max.as.uint_value ||
        value->as.uint_value < option->range_min.as.uint_value ||
        value->as.uint_value > option->range_max.as.uint_value) {
      return CONFIT_ERR_SCHEMA;
    }
    return CONFIT_OK;
  }

  if (option->type == CONFIT_OPTION_TYPE_FLOAT) {
    if (option->range_min.kind != CONFIT_VALUE_FLOAT ||
        option->range_max.kind != CONFIT_VALUE_FLOAT ||
        value->kind != CONFIT_VALUE_FLOAT ||
        !confit_model_float_is_finite(option->range_min.as.float_value) ||
        !confit_model_float_is_finite(option->range_max.as.float_value) ||
        !confit_model_float_is_finite(value->as.float_value) ||
        option->range_min.as.float_value > option->range_max.as.float_value ||
        value->as.float_value < option->range_min.as.float_value ||
        value->as.float_value > option->range_max.as.float_value) {
      return CONFIT_ERR_SCHEMA;
    }
    return CONFIT_OK;
  }

  return CONFIT_ERR_SCHEMA;
}

ConfitStatus confit_option_validate_default(const ConfitOption *option) {
  if (option == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  switch (option->type) {
  case CONFIT_OPTION_TYPE_BOOL:
    if (option->default_value.kind != CONFIT_VALUE_EMPTY &&
        option->default_value.kind != CONFIT_VALUE_BOOL) {
      return CONFIT_ERR_SCHEMA;
    }
    break;
  case CONFIT_OPTION_TYPE_INT:
    if (option->default_value.kind != CONFIT_VALUE_EMPTY &&
        option->default_value.kind != CONFIT_VALUE_INT) {
      return CONFIT_ERR_SCHEMA;
    }
    break;
  case CONFIT_OPTION_TYPE_UINT:
  case CONFIT_OPTION_TYPE_HEX:
    if (option->default_value.kind != CONFIT_VALUE_EMPTY &&
        option->default_value.kind != CONFIT_VALUE_UINT) {
      return CONFIT_ERR_SCHEMA;
    }
    break;
  case CONFIT_OPTION_TYPE_STRING:
    if (option->default_value.kind != CONFIT_VALUE_EMPTY &&
        option->default_value.kind != CONFIT_VALUE_STRING) {
      return CONFIT_ERR_SCHEMA;
    }
    break;
  case CONFIT_OPTION_TYPE_ENUM:
    if (option->default_value.kind != CONFIT_VALUE_EMPTY &&
        option->default_value.kind != CONFIT_VALUE_ENUM) {
      return CONFIT_ERR_SCHEMA;
    }
    if (option->default_value.kind == CONFIT_VALUE_ENUM &&
        option->enum_value_count > 0U &&
        !confit_option_enum_contains(option,
                                     option->default_value.as.string_value)) {
      return CONFIT_ERR_SCHEMA;
    }
    break;
  case CONFIT_OPTION_TYPE_FLOAT:
    if (option->default_value.kind != CONFIT_VALUE_EMPTY &&
        (option->default_value.kind != CONFIT_VALUE_FLOAT ||
         !confit_model_float_is_finite(option->default_value.as.float_value))) {
      return CONFIT_ERR_SCHEMA;
    }
    break;
  case CONFIT_OPTION_TYPE_PATH:
    if (option->default_value.kind != CONFIT_VALUE_EMPTY &&
        option->default_value.kind != CONFIT_VALUE_PATH) {
      return CONFIT_ERR_SCHEMA;
    }
    break;
  default:
    return CONFIT_ERR_SCHEMA;
  }

  return confit_option_validate_range(option);
}

ConfitStatus confit_choice_set_identity(ConfitChoice *choice, const char *id,
                                        const char *default_option) {
  ConfitStatus status;

  if (choice == 0 || id == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status = confit_model_replace_string(&choice->id, id);
  if (status != CONFIT_OK) {
    return status;
  }

  return confit_model_replace_string(&choice->default_option, default_option);
}

ConfitStatus confit_choice_add_option(ConfitChoice *choice,
                                      const char *option) {
  if (choice == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  return confit_model_append_string(&choice->options, &choice->option_count,
                                    option);
}

ConfitStatus confit_profile_set_identity(ConfitProfile *profile,
                                         const char *name, const char *base) {
  ConfitStatus status;

  if (profile == 0 || name == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status = confit_model_replace_string(&profile->name, name);
  if (status != CONFIT_OK) {
    return status;
  }

  return confit_model_replace_string(&profile->base, base);
}

ConfitStatus confit_profile_set_target(ConfitProfile *profile,
                                       const char *target) {
  if (profile == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  return confit_model_replace_string(&profile->target, target);
}

ConfitStatus confit_profile_add_value(ConfitProfile *profile,
                                      const char *option_id,
                                      const ConfitValue *value,
                                      const char *source) {
  if (profile == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  return confit_named_value_append(&profile->values, &profile->value_count,
                                   option_id, value, source);
}

ConfitStatus confit_target_set_identity(ConfitTarget *target, const char *name,
                                        const char *arch, const char *board,
                                        const char *claim_level) {
  ConfitStatus status;

  if (target == 0 || name == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status = confit_model_replace_string(&target->name, name);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_model_replace_string(&target->arch, arch);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_model_replace_string(&target->board, board);
  if (status != CONFIT_OK) {
    return status;
  }
  return confit_model_replace_string(&target->claim_level, claim_level);
}

ConfitStatus confit_target_add_value(ConfitTarget *target,
                                     const char *option_id,
                                     const ConfitValue *value,
                                     const char *source) {
  if (target == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  return confit_named_value_append(&target->values, &target->value_count,
                                   option_id, value, source);
}
