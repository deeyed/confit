#include "confit/tui.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/graph.h"
#include "confit/host.h"
#include "confit/model.h"
#include "confit/resolver.h"
#include "confit/schema.h"
#include "confit_tui.h"

typedef struct ConfitTuiRow {
  CftuiListItem item;
  const ConfitOption *option;
  char detail[224];
  char value[128];
} ConfitTuiRow;

typedef struct ConfitTuiTextBuilder {
  char *text;
  size_t size;
  size_t capacity;
} ConfitTuiTextBuilder;

typedef struct ConfitTuiState {
  const ConfitTuiOptions *options;
  ConfitProject *project;
  ConfitGraph *graph;
  ConfitResolvedConfig *config;
  ConfitNamedValue *edits;
  size_t edit_count;
  ConfitTuiRow *rows;
  size_t row_count;
  size_t *view_indices;
  size_t view_count;
  size_t selected_view_index;
  char search[128];
  char category[64];
  char tag[64];
  char status[256];
  int dirty;
} ConfitTuiState;

static const char *confit_tui_text_or_dash(const char *text) {
  return text != 0 && text[0] != '\0' ? text : "-";
}

static char *confit_tui_copy_string(const char *text) {
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

static void confit_tui_named_value_clear(ConfitNamedValue *value) {
  if (value == 0) {
    return;
  }
  free(value->option_id);
  confit_value_clear(&value->value);
  free(value->source);
  value->option_id = 0;
  value->source = 0;
}

static void confit_tui_edits_clear(ConfitTuiState *state) {
  size_t index;

  if (state == 0) {
    return;
  }
  for (index = 0U; index < state->edit_count; ++index) {
    confit_tui_named_value_clear(&state->edits[index]);
  }
  free(state->edits);
  state->edits = 0;
  state->edit_count = 0U;
}

static ConfitNamedValue *confit_tui_find_edit(ConfitTuiState *state,
                                              const char *option_id) {
  size_t index;

  for (index = 0U; index < state->edit_count; ++index) {
    if (state->edits[index].option_id != 0 &&
        strcmp(state->edits[index].option_id, option_id) == 0) {
      return &state->edits[index];
    }
  }
  return 0;
}

static const ConfitNamedValue *confit_tui_find_const_edit(
    const ConfitTuiState *state, const char *option_id) {
  size_t index;

  for (index = 0U; index < state->edit_count; ++index) {
    if (state->edits[index].option_id != 0 &&
        strcmp(state->edits[index].option_id, option_id) == 0) {
      return &state->edits[index];
    }
  }
  return 0;
}

static ConfitStatus confit_tui_set_edit(ConfitTuiState *state,
                                        const char *option_id,
                                        const ConfitValue *value,
                                        ConfitDiagnostic *diagnostic) {
  ConfitNamedValue *edit;
  ConfitNamedValue *new_edits;

  edit = confit_tui_find_edit(state, option_id);
  if (edit != 0) {
    return confit_value_copy(&edit->value, value);
  }

  new_edits =
      (ConfitNamedValue *)realloc(state->edits,
                                  (state->edit_count + 1U) *
                                      sizeof(state->edits[0]));
  if (new_edits == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, option_id, 0, 0,
                          "failed to allocate tui edit");
    return CONFIT_ERR_INTERNAL;
  }
  state->edits = new_edits;
  edit = &state->edits[state->edit_count];
  edit->option_id = confit_tui_copy_string(option_id);
  confit_value_init(&edit->value);
  edit->source = 0;
  if (edit->option_id == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, option_id, 0, 0,
                          "failed to copy tui edit id");
    return CONFIT_ERR_INTERNAL;
  }
  if (confit_value_copy(&edit->value, value) != CONFIT_OK) {
    confit_tui_named_value_clear(edit);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, option_id, 0, 0,
                          "failed to copy tui edit value");
    return CONFIT_ERR_INTERNAL;
  }
  state->edit_count += 1U;
  return CONFIT_OK;
}

static const char *confit_tui_effective_target_name(
    const ConfitProject *project, const char *profile_name,
    const char *target_name) {
  size_t index;

  if (target_name != 0) {
    return target_name;
  }
  if (project == 0 || profile_name == 0) {
    return 0;
  }
  for (index = 0U; index < project->profile_count; ++index) {
    const ConfitProfile *profile = &project->profiles[index];
    if (profile->name != 0 && strcmp(profile->name, profile_name) == 0) {
      return profile->target;
    }
  }
  return 0;
}

static ConfitProfile *confit_tui_find_profile(ConfitProject *project,
                                              const char *name) {
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

static void confit_tui_format_value(const ConfitOption *option,
                                    const ConfitValue *value, char *out,
                                    size_t out_size) {
  if (out == 0 || out_size == 0U) {
    return;
  }

  switch (value != 0 ? value->kind : CONFIT_VALUE_EMPTY) {
  case CONFIT_VALUE_BOOL:
    (void)snprintf(out, out_size, "%s",
                   value->as.bool_value ? "true" : "false");
    break;
  case CONFIT_VALUE_INT:
    (void)snprintf(out, out_size, "%lld", (long long)value->as.int_value);
    break;
  case CONFIT_VALUE_UINT:
    if (option != 0 && option->type == CONFIT_OPTION_TYPE_HEX) {
      (void)snprintf(out, out_size, "0x%llX",
                     (unsigned long long)value->as.uint_value);
    } else {
      (void)snprintf(out, out_size, "%llu",
                     (unsigned long long)value->as.uint_value);
    }
    break;
  case CONFIT_VALUE_FLOAT:
    (void)snprintf(out, out_size, "%.6g", value->as.float_value);
    break;
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
  case CONFIT_VALUE_PATH:
    (void)snprintf(out, out_size, "%s",
                   confit_tui_text_or_dash(value->as.string_value));
    break;
  case CONFIT_VALUE_EMPTY:
  default:
    (void)snprintf(out, out_size, "-");
    break;
  }
  out[out_size - 1U] = '\0';
}

static int confit_tui_option_has_tag(const ConfitOption *option,
                                     const char *tag) {
  size_t index;

  if (tag == 0 || tag[0] == '\0') {
    return 1;
  }
  for (index = 0U; index < option->tag_count; ++index) {
    if (option->tags[index] != 0 && strcmp(option->tags[index], tag) == 0) {
      return 1;
    }
  }
  return 0;
}

static int confit_tui_contains(const char *haystack, const char *needle) {
  return needle == 0 || needle[0] == '\0' ||
         (haystack != 0 && strstr(haystack, needle) != 0);
}

static int confit_tui_option_matches_search(const ConfitOption *option,
                                            const char *search) {
  size_t index;

  if (search == 0 || search[0] == '\0') {
    return 1;
  }
  if (confit_tui_contains(option->id, search) ||
      confit_tui_contains(option->prompt, search) ||
      confit_tui_contains(option->category, search) ||
      confit_tui_contains(option->help, search)) {
    return 1;
  }
  for (index = 0U; index < option->tag_count; ++index) {
    if (confit_tui_contains(option->tags[index], search)) {
      return 1;
    }
  }
  return 0;
}

static int confit_tui_option_visible(const ConfitTuiState *state,
                                     const ConfitOption *option) {
  if (state->category[0] != '\0' &&
      (option->category == 0 ||
       strcmp(option->category, state->category) != 0)) {
    return 0;
  }
  if (!confit_tui_option_has_tag(option, state->tag)) {
    return 0;
  }
  return confit_tui_option_matches_search(option, state->search);
}

static ConfitStatus confit_tui_rebuild_view(ConfitTuiState *state,
                                            ConfitDiagnostic *diagnostic) {
  size_t index;

  free(state->view_indices);
  state->view_indices = 0;
  state->view_count = 0U;
  if (state->row_count > 0U) {
    state->view_indices =
        (size_t *)calloc(state->row_count, sizeof(state->view_indices[0]));
    if (state->view_indices == 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0, 0,
                            "failed to allocate tui filtered view");
      return CONFIT_ERR_INTERNAL;
    }
  }

  for (index = 0U; index < state->row_count; ++index) {
    if (confit_tui_option_visible(state, state->rows[index].option)) {
      state->view_indices[state->view_count] = index;
      state->view_count += 1U;
    }
  }
  if (state->selected_view_index >= state->view_count) {
    state->selected_view_index = state->view_count == 0U
                                     ? 0U
                                     : state->view_count - 1U;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_tui_refresh_rows(ConfitTuiState *state,
                                            ConfitDiagnostic *diagnostic) {
  size_t index;

  free(state->rows);
  state->rows = 0;
  state->row_count = 0U;
  if (state->project->option_count > 0U) {
    state->rows = (ConfitTuiRow *)calloc(state->project->option_count,
                                         sizeof(state->rows[0]));
    if (state->rows == 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0, 0,
                            "failed to allocate tui rows");
      return CONFIT_ERR_INTERNAL;
    }
  }

  for (index = 0U; index < state->project->option_count; ++index) {
    const ConfitOption *option = &state->project->options[index];
    const ConfitResolvedValue *resolved =
        confit_resolved_config_find(state->config, option->id);
    const ConfitNamedValue *edit = confit_tui_find_const_edit(state, option->id);
    const char *mark = edit != 0 ? "*" : " ";

    (void)snprintf(state->rows[index].detail,
                   sizeof(state->rows[index].detail), "%s%s | %s | %s", mark,
                   confit_option_type_name(option->type),
                   confit_tui_text_or_dash(option->category),
                   confit_tui_text_or_dash(option->prompt));
    state->rows[index].detail[sizeof(state->rows[index].detail) - 1U] = '\0';

    confit_tui_format_value(
        option, resolved != 0 ? &resolved->value : &option->default_value,
        state->rows[index].value, sizeof(state->rows[index].value));
    state->rows[index].option = option;
    state->rows[index].item.label = option->id;
    state->rows[index].item.detail = state->rows[index].detail;
    state->rows[index].item.value = state->rows[index].value;
  }
  state->row_count = state->project->option_count;
  return confit_tui_rebuild_view(state, diagnostic);
}

static ConfitStatus confit_tui_refresh_resolution(ConfitTuiState *state,
                                                  ConfitDiagnostic *diagnostic) {
  ConfitResolvedConfig *config;
  ConfitStatus status;

  config = 0;
  status = confit_resolver_resolve(
      state->project, state->options->profile_name, state->options->target_name,
      state->edits, state->edit_count, &config, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  confit_resolved_config_free(state->config);
  state->config = config;
  return confit_tui_refresh_rows(state, diagnostic);
}

static ConfitStatus confit_tui_load_checked_project(
    ConfitTuiState *state, ConfitDiagnostic *diagnostic) {
  ConfitStatus status;

  status = confit_schema_load_project(state->options->project_root,
                                      &state->project, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_graph_build(state->project, &state->graph, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_graph_validate(state->graph, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_refresh_resolution(state, diagnostic);
  }
  return status;
}

static ConfitStatus confit_tui_parse_int64(const char *text, int64_t *out) {
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

static ConfitStatus confit_tui_parse_uint64(const char *text, uint64_t *out) {
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

static ConfitStatus confit_tui_parse_double(const char *text, double *out) {
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

static ConfitStatus confit_tui_parse_value(const ConfitOption *option,
                                           const char *text,
                                           ConfitValue *out_value,
                                           ConfitDiagnostic *diagnostic) {
  ConfitStatus status;

  confit_value_init(out_value);
  switch (option->type) {
  case CONFIT_OPTION_TYPE_BOOL:
    if (strcmp(text, "true") == 0 || strcmp(text, "1") == 0) {
      confit_value_set_bool(out_value, 1);
      return CONFIT_OK;
    }
    if (strcmp(text, "false") == 0 || strcmp(text, "0") == 0) {
      confit_value_set_bool(out_value, 0);
      return CONFIT_OK;
    }
    break;
  case CONFIT_OPTION_TYPE_INT: {
    int64_t value;
    status = confit_tui_parse_int64(text, &value);
    if (status == CONFIT_OK) {
      confit_value_set_int(out_value, value);
    }
    return status;
  }
  case CONFIT_OPTION_TYPE_UINT:
  case CONFIT_OPTION_TYPE_HEX: {
    uint64_t value;
    status = confit_tui_parse_uint64(text, &value);
    if (status == CONFIT_OK) {
      confit_value_set_uint(out_value, value);
    }
    return status;
  }
  case CONFIT_OPTION_TYPE_STRING:
    return confit_value_set_string(out_value, text);
  case CONFIT_OPTION_TYPE_ENUM: {
    size_t index;
    for (index = 0U; index < option->enum_value_count; ++index) {
      if (strcmp(option->enum_values[index], text) == 0) {
        return confit_value_set_enum(out_value, text);
      }
    }
    break;
  }
  case CONFIT_OPTION_TYPE_FLOAT: {
    double value;
    status = confit_tui_parse_double(text, &value);
    if (status == CONFIT_OK) {
      confit_value_set_float(out_value, value);
    }
    return status;
  }
  case CONFIT_OPTION_TYPE_PATH:
    return confit_value_set_path(out_value, text);
  default:
    break;
  }

  confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                        "invalid tui value");
  return CONFIT_ERR_SCHEMA;
}

static ConfitStatus confit_tui_resolved_value_for_option(
    const ConfitTuiState *state, const ConfitOption *option,
    const ConfitValue **out_value, ConfitDiagnostic *diagnostic) {
  const ConfitResolvedValue *resolved;

  resolved = confit_resolved_config_find(state->config, option->id);
  if (resolved == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, option->id, 0, 0,
                          "missing resolved tui option");
    return CONFIT_ERR_INTERNAL;
  }
  *out_value = &resolved->value;
  return CONFIT_OK;
}

static ConfitStatus confit_tui_apply_value_edit(ConfitTuiState *state,
                                                const ConfitOption *option,
                                                const ConfitValue *value,
                                                ConfitDiagnostic *diagnostic) {
  ConfitStatus status;

  status = confit_tui_set_edit(state, option->id, value, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_tui_refresh_resolution(state, diagnostic);
  if (status == CONFIT_OK) {
    state->dirty = 1;
    (void)snprintf(state->status, sizeof(state->status), "edited %s",
                   option->id);
    state->status[sizeof(state->status) - 1U] = '\0';
  }
  return status;
}

static ConfitStatus confit_tui_toggle_bool(ConfitTuiState *state,
                                           const ConfitOption *option,
                                           ConfitDiagnostic *diagnostic) {
  const ConfitValue *current;
  ConfitValue value;
  ConfitStatus status;

  status = confit_tui_resolved_value_for_option(state, option, &current,
                                                diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  confit_value_init(&value);
  confit_value_set_bool(&value,
                        current->kind == CONFIT_VALUE_BOOL
                            ? !current->as.bool_value
                            : 1);
  status = confit_tui_apply_value_edit(state, option, &value, diagnostic);
  confit_value_clear(&value);
  return status;
}

static ConfitStatus confit_tui_next_enum(ConfitTuiState *state,
                                         const ConfitOption *option,
                                         ConfitDiagnostic *diagnostic) {
  const ConfitValue *current;
  const char *next;
  size_t index;
  ConfitValue value;
  ConfitStatus status;

  if (option->enum_value_count == 0U) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                          "enum option has no candidates");
    return CONFIT_ERR_SCHEMA;
  }
  status = confit_tui_resolved_value_for_option(state, option, &current,
                                                diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  next = option->enum_values[0];
  if (current->kind == CONFIT_VALUE_ENUM || current->kind == CONFIT_VALUE_STRING) {
    for (index = 0U; index < option->enum_value_count; ++index) {
      if (current->as.string_value != 0 &&
          strcmp(option->enum_values[index], current->as.string_value) == 0) {
        next = option->enum_values[(index + 1U) % option->enum_value_count];
        break;
      }
    }
  }

  confit_value_init(&value);
  status = confit_value_set_enum(&value, next);
  if (status == CONFIT_OK) {
    status = confit_tui_apply_value_edit(state, option, &value, diagnostic);
  }
  confit_value_clear(&value);
  return status;
}

static ConfitStatus confit_tui_prompt_edit(ConfitTuiState *state,
                                           const ConfitOption *option,
                                           ConfitDiagnostic *diagnostic) {
  char input[256];
  ConfitValue value;
  ConfitStatus status;

  if (option->type == CONFIT_OPTION_TYPE_BOOL) {
    return confit_tui_toggle_bool(state, option, diagnostic);
  }
  if (option->type == CONFIT_OPTION_TYPE_ENUM) {
    char prompt[256];

    (void)snprintf(prompt, sizeof(prompt),
                   "enum value for %s (empty = next): ", option->id);
    prompt[sizeof(prompt) - 1U] = '\0';
    if (cftui_read_line(prompt, input, sizeof(input)) != 0) {
      return CONFIT_OK;
    }
    if (input[0] == '\0') {
      return confit_tui_next_enum(state, option, diagnostic);
    }
  } else {
    char prompt[256];

    (void)snprintf(prompt, sizeof(prompt), "value for %s: ", option->id);
    prompt[sizeof(prompt) - 1U] = '\0';
    if (cftui_read_line(prompt, input, sizeof(input)) != 0 ||
        input[0] == '\0') {
      return CONFIT_OK;
    }
  }

  confit_value_init(&value);
  status = confit_tui_parse_value(option, input, &value, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_tui_apply_value_edit(state, option, &value, diagnostic);
  }
  confit_value_clear(&value);
  return status;
}

static const ConfitOption *confit_tui_selected_option(
    const ConfitTuiState *state) {
  if (state->view_count == 0U ||
      state->selected_view_index >= state->view_count) {
    return 0;
  }
  return state->rows[state->view_indices[state->selected_view_index]].option;
}

static void confit_tui_text_builder_init(ConfitTuiTextBuilder *builder) {
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

static ConfitStatus confit_tui_text_append(ConfitTuiTextBuilder *builder,
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

static ConfitStatus confit_tui_text_append_char(ConfitTuiTextBuilder *builder,
                                                char value) {
  char text[2];

  text[0] = value;
  text[1] = '\0';
  return confit_tui_text_append(builder, text);
}

static ConfitStatus confit_tui_text_append_quoted(
    ConfitTuiTextBuilder *builder, const char *text) {
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

static ConfitStatus confit_tui_text_append_value(
    ConfitTuiTextBuilder *builder, const ConfitOption *option,
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

static int confit_tui_profile_has_value(const ConfitProfile *profile,
                                        const char *option_id) {
  size_t index;

  if (profile == 0 || option_id == 0) {
    return 0;
  }
  for (index = 0U; index < profile->value_count; ++index) {
    if (profile->values[index].option_id != 0 &&
        strcmp(profile->values[index].option_id, option_id) == 0) {
      return 1;
    }
  }
  return 0;
}

static const ConfitValue *confit_tui_saved_value_for(
    const ConfitTuiState *state, const ConfitNamedValue *profile_value) {
  const ConfitNamedValue *edit =
      confit_tui_find_const_edit(state, profile_value->option_id);
  return edit != 0 ? &edit->value : &profile_value->value;
}

static ConfitStatus confit_tui_append_profile_value(
    ConfitTuiTextBuilder *builder, const ConfitTuiState *state,
    const char *option_id, const ConfitValue *value,
    ConfitDiagnostic *diagnostic) {
  ConfitOption *option;
  ConfitStatus status;

  option = confit_project_find_option(state->project, option_id);
  if (option == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option_id, 0, 0,
                          "cannot save unknown profile option");
    return CONFIT_ERR_SCHEMA;
  }
  status = confit_tui_text_append_quoted(builder, option_id);
  if (status == CONFIT_OK) {
    status = confit_tui_text_append(builder, " = ");
  }
  if (status == CONFIT_OK) {
    status = confit_tui_text_append_value(builder, option, value);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_text_append(builder, "\n");
  }
  return status;
}

static ConfitStatus confit_tui_build_profile_toml(
    const ConfitTuiState *state, const ConfitProfile *profile, char **out_text,
    ConfitDiagnostic *diagnostic) {
  ConfitTuiTextBuilder builder;
  ConfitStatus status;
  const char *target_name;
  size_t index;

  *out_text = 0;
  confit_tui_text_builder_init(&builder);
  status = confit_tui_text_append(&builder, "[profile]\nname = ");
  if (status == CONFIT_OK) {
    status = confit_tui_text_append_quoted(&builder, profile->name);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_text_append(&builder, "\nschema_version = 1\n");
  }
  if (status == CONFIT_OK && profile->base != 0) {
    status = confit_tui_text_append(&builder, "base = ");
    if (status == CONFIT_OK) {
      status = confit_tui_text_append_quoted(&builder, profile->base);
    }
    if (status == CONFIT_OK) {
      status = confit_tui_text_append(&builder, "\n");
    }
  }
  target_name = profile->target != 0 ? profile->target : state->options->target_name;
  if (status == CONFIT_OK && target_name != 0) {
    status = confit_tui_text_append(&builder, "target = ");
    if (status == CONFIT_OK) {
      status = confit_tui_text_append_quoted(&builder, target_name);
    }
    if (status == CONFIT_OK) {
      status = confit_tui_text_append(&builder, "\n");
    }
  }
  if (status == CONFIT_OK) {
    status = confit_tui_text_append(&builder, "\n[values]\n");
  }

  for (index = 0U; status == CONFIT_OK && index < profile->value_count;
       ++index) {
    status = confit_tui_append_profile_value(
        &builder, state, profile->values[index].option_id,
        confit_tui_saved_value_for(state, &profile->values[index]),
        diagnostic);
  }
  for (index = 0U; status == CONFIT_OK && index < state->edit_count; ++index) {
    if (!confit_tui_profile_has_value(profile, state->edits[index].option_id)) {
      status = confit_tui_append_profile_value(
          &builder, state, state->edits[index].option_id,
          &state->edits[index].value, diagnostic);
    }
  }

  if (status != CONFIT_OK) {
    free(builder.text);
    return status;
  }
  *out_text = builder.text;
  return CONFIT_OK;
}

static ConfitStatus confit_tui_save_profile(ConfitTuiState *state,
                                            ConfitDiagnostic *diagnostic) {
  ConfitProfile *profile;
  char *toml;
  char config_dir[1024];
  char profile_dir[1024];
  char file_name[256];
  char profile_path[1024];
  ConfitStatus status;

  profile = confit_tui_find_profile(state->project, state->options->profile_name);
  if (profile == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                          state->options->profile_name, 0, 0,
                          "cannot save unknown profile");
    return CONFIT_ERR_SCHEMA;
  }

  status = confit_tui_refresh_resolution(state, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }

  toml = 0;
  status = confit_tui_build_profile_toml(state, profile, &toml, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_host_path_join(config_dir, sizeof(config_dir),
                                   state->options->project_root, "config",
                                   diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_path_join(profile_dir, sizeof(profile_dir), config_dir,
                                   "profiles", diagnostic);
  }
  if (status == CONFIT_OK) {
    if (snprintf(file_name, sizeof(file_name), "%s.toml", profile->name) >=
        (int)sizeof(file_name)) {
      status = CONFIT_ERR_INVALID_ARGUMENT;
      confit_diagnostic_set(diagnostic, status, profile->name, 0, 0,
                            "profile name is too long");
    }
  }
  if (status == CONFIT_OK) {
    status = confit_host_path_join(profile_path, sizeof(profile_path),
                                   profile_dir, file_name, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_make_directories(profile_dir, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_write_text_file(profile_path, toml, diagnostic);
  }
  free(toml);
  if (status == CONFIT_OK) {
    state->dirty = 0;
    (void)snprintf(state->status, sizeof(state->status),
                   "saved %s", profile_path);
    state->status[sizeof(state->status) - 1U] = '\0';
  }
  return status;
}

static ConfitStatus confit_tui_set_filter(char *slot, size_t slot_size,
                                          const char *prompt,
                                          ConfitTuiState *state,
                                          ConfitDiagnostic *diagnostic) {
  char input[128];

  if (cftui_read_line(prompt, input, sizeof(input)) != 0) {
    return CONFIT_OK;
  }
  (void)snprintf(slot, slot_size, "%s", input);
  slot[slot_size - 1U] = '\0';
  state->selected_view_index = 0U;
  (void)snprintf(state->status, sizeof(state->status), "filter updated");
  state->status[sizeof(state->status) - 1U] = '\0';
  return confit_tui_rebuild_view(state, diagnostic);
}

static void confit_tui_clear_filters(ConfitTuiState *state) {
  state->search[0] = '\0';
  state->category[0] = '\0';
  state->tag[0] = '\0';
  state->selected_view_index = 0U;
  (void)snprintf(state->status, sizeof(state->status), "filters cleared");
  state->status[sizeof(state->status) - 1U] = '\0';
}

static ConfitStatus confit_tui_render_screen(const ConfitTuiState *state,
                                             const char *target_name) {
  CftuiListItem *items;
  CftuiScreen screen;
  char subtitle[320];
  char status_line[384];
  size_t index;

  items = 0;
  if (state->view_count > 0U) {
    items = (CftuiListItem *)calloc(state->view_count, sizeof(items[0]));
    if (items == 0) {
      return CONFIT_ERR_INTERNAL;
    }
    for (index = 0U; index < state->view_count; ++index) {
      items[index] = state->rows[state->view_indices[index]].item;
    }
  }

  (void)snprintf(subtitle, sizeof(subtitle),
                 "project=%s profile=%s target=%s search=%s category=%s "
                 "tag=%s dirty=%s",
                 confit_tui_text_or_dash(state->project->name),
                 confit_tui_text_or_dash(state->options->profile_name),
                 confit_tui_text_or_dash(target_name),
                 confit_tui_text_or_dash(state->search),
                 confit_tui_text_or_dash(state->category),
                 confit_tui_text_or_dash(state->tag),
                 state->dirty ? "yes" : "no");
  subtitle[sizeof(subtitle) - 1U] = '\0';
  (void)snprintf(status_line, sizeof(status_line),
                 "option %lu/%lu | / search c category t tag x clear e edit "
                 "s save q quit | %s",
                 state->view_count == 0U
                     ? 0UL
                     : (unsigned long)(state->selected_view_index + 1U),
                 (unsigned long)state->view_count,
                 state->status[0] != '\0' ? state->status : "ready");
  status_line[sizeof(status_line) - 1U] = '\0';

  screen.title = "Confit TUI";
  screen.subtitle = subtitle;
  screen.items = items;
  screen.item_count = state->view_count;
  screen.selected_index = state->selected_view_index;
  screen.status = status_line;
  if (cftui_render(&screen) != 0) {
    free(items);
    return CONFIT_ERR_INTERNAL;
  }
  free(items);
  return CONFIT_OK;
}

static ConfitStatus confit_tui_handle_key(ConfitTuiState *state, CftuiKey key,
                                          ConfitDiagnostic *diagnostic) {
  const ConfitOption *option;

  if (key == CFTUI_KEY_DOWN && state->selected_view_index + 1U < state->view_count) {
    state->selected_view_index += 1U;
    return CONFIT_OK;
  }
  if (key == CFTUI_KEY_UP && state->selected_view_index > 0U) {
    state->selected_view_index -= 1U;
    return CONFIT_OK;
  }
  if (key == CFTUI_KEY_SEARCH) {
    return confit_tui_set_filter(state->search, sizeof(state->search),
                                 "search: ", state, diagnostic);
  }
  if (key == CFTUI_KEY_CATEGORY) {
    return confit_tui_set_filter(state->category, sizeof(state->category),
                                 "category: ", state, diagnostic);
  }
  if (key == CFTUI_KEY_TAG) {
    return confit_tui_set_filter(state->tag, sizeof(state->tag), "tag: ",
                                 state, diagnostic);
  }
  if (key == CFTUI_KEY_CLEAR_FILTER) {
    confit_tui_clear_filters(state);
    return confit_tui_rebuild_view(state, diagnostic);
  }
  if (key == CFTUI_KEY_SAVE) {
    return confit_tui_save_profile(state, diagnostic);
  }
  if (key == CFTUI_KEY_EDIT || key == CFTUI_KEY_ENTER) {
    option = confit_tui_selected_option(state);
    if (option == 0) {
      (void)snprintf(state->status, sizeof(state->status),
                     "no visible option selected");
      state->status[sizeof(state->status) - 1U] = '\0';
      return CONFIT_OK;
    }
    return confit_tui_prompt_edit(state, option, diagnostic);
  }
  return CONFIT_OK;
}

static ConfitStatus confit_tui_render_loop(ConfitTuiState *state) {
  const char *target_name;
  CftuiKey key;
  ConfitDiagnostic diagnostic;
  ConfitStatus status;

  target_name = confit_tui_effective_target_name(
      state->project, state->options->profile_name, state->options->target_name);
  (void)snprintf(state->status, sizeof(state->status), "ready");
  state->status[sizeof(state->status) - 1U] = '\0';

  do {
    status = confit_tui_render_screen(state, target_name);
    if (status != CONFIT_OK) {
      return status;
    }
    key = cftui_read_key();
    if (key == CFTUI_KEY_QUIT) {
      break;
    }
    confit_diagnostic_init(&diagnostic);
    status = confit_tui_handle_key(state, key, &diagnostic);
    if (status != CONFIT_OK) {
      (void)snprintf(state->status, sizeof(state->status), "error: %s",
                     diagnostic.message != 0 ? diagnostic.message
                                             : confit_status_name(status));
      state->status[sizeof(state->status) - 1U] = '\0';
    }
  } while (1);

  return CONFIT_OK;
}

static void confit_tui_state_clear(ConfitTuiState *state) {
  if (state == 0) {
    return;
  }
  confit_tui_edits_clear(state);
  free(state->rows);
  free(state->view_indices);
  confit_resolved_config_free(state->config);
  confit_graph_free(state->graph);
  confit_project_free(state->project);
}

ConfitStatus confit_tui_run(const ConfitTuiOptions *options,
                            ConfitDiagnostic *diagnostic) {
  ConfitTuiState state;
  ConfitStatus status;

  if (options == 0 || options->project_root == 0 ||
      options->profile_name == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid tui options");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  memset(&state, 0, sizeof(state));
  state.options = options;
  status = confit_tui_load_checked_project(&state, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_tui_render_loop(&state);
  }
  confit_tui_state_clear(&state);
  return status;
}
