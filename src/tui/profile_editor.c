#include "tui_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/graph.h"
#include "confit/host.h"
#include "confit/resolver.h"
#include "confit/schema.h"

typedef enum ConfitTuiRowKind {
  CONFIT_TUI_ROW_CATEGORY = 1,
  CONFIT_TUI_ROW_OPTION = 2,
} ConfitTuiRowKind;

typedef struct ConfitTuiCategoryState {
  char *name;
  int collapsed;
  size_t option_count;
  size_t visible_count;
} ConfitTuiCategoryState;

typedef struct ConfitTuiRow {
  ConfitTuiListItem item;
  const ConfitOption *option;
  ConfitTuiRowKind kind;
  size_t category_index;
  char label[160];
  char detail[384];
  char value[128];
  char dependency_state[160];
  int is_disabled;
} ConfitTuiRow;

typedef struct ConfitTuiState {
  const ConfitTuiOptions *options;
  ConfitProject *project;
  ConfitGraph *graph;
  ConfitResolvedConfig *config;
  ConfitNamedValue *edits;
  size_t edit_count;
  ConfitTuiCategoryState *categories;
  size_t category_count;
  ConfitTuiRow *rows;
  size_t row_count;
  size_t *view_indices;
  size_t view_count;
  size_t selected_view_index;
  char search[128];
  size_t search_count;
  size_t search_position;
  char category[64];
  char tag[64];
  char status[256];
  int dirty;
  int profile_created;
} ConfitTuiState;

typedef struct ConfitTuiValueValidator {
  const ConfitTuiState *state;
  const ConfitOption *option;
} ConfitTuiValueValidator;

static ConfitStatus confit_tui_refresh_rows(ConfitTuiState *state,
                                            ConfitDiagnostic *diagnostic);

static void
confit_tui_set_status_from_diagnostic(ConfitTuiState *state, const char *prefix,
                                      ConfitStatus status,
                                      const ConfitDiagnostic *diagnostic) {
  const char *message;

  if (state == 0) {
    return;
  }
  message = diagnostic != 0 && diagnostic->message != 0
                ? diagnostic->message
                : confit_status_name(status);
  if (diagnostic != 0 && diagnostic->path != 0 && diagnostic->path[0] != '\0') {
    (void)snprintf(state->status, sizeof(state->status), "%s: %s: %s",
                   confit_tui_text_or_dash(prefix), diagnostic->path, message);
  } else {
    (void)snprintf(state->status, sizeof(state->status), "%s: %s",
                   confit_tui_text_or_dash(prefix), message);
  }
  state->status[sizeof(state->status) - 1U] = '\0';
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

static const char *confit_tui_option_category_name(const ConfitOption *option) {
  if (option != 0 && option->category != 0 && option->category[0] != '\0') {
    return option->category;
  }
  return "(uncategorized)";
}

static void confit_tui_categories_clear(ConfitTuiState *state) {
  size_t index;

  if (state == 0) {
    return;
  }
  for (index = 0U; index < state->category_count; ++index) {
    free(state->categories[index].name);
  }
  free(state->categories);
  state->categories = 0;
  state->category_count = 0U;
}

static int
confit_tui_old_category_collapsed(const ConfitTuiCategoryState *categories,
                                  size_t category_count, const char *name) {
  size_t index;

  for (index = 0U; index < category_count; ++index) {
    if (categories[index].name != 0 &&
        strcmp(categories[index].name, name) == 0) {
      return categories[index].collapsed;
    }
  }
  return 0;
}

static size_t
confit_tui_find_category_index(const ConfitTuiCategoryState *categories,
                               size_t category_count, const char *name) {
  size_t index;

  for (index = 0U; index < category_count; ++index) {
    if (categories[index].name != 0 &&
        strcmp(categories[index].name, name) == 0) {
      return index;
    }
  }
  return (size_t)-1;
}

static ConfitStatus
confit_tui_rebuild_categories(ConfitTuiState *state,
                              ConfitDiagnostic *diagnostic) {
  ConfitTuiCategoryState *old_categories;
  size_t old_category_count;
  size_t index;

  old_categories = state->categories;
  old_category_count = state->category_count;
  state->categories = 0;
  state->category_count = 0U;

  for (index = 0U; index < state->project->option_count; ++index) {
    const char *name =
        confit_tui_option_category_name(&state->project->options[index]);
    size_t category_index = confit_tui_find_category_index(
        state->categories, state->category_count, name);

    if (category_index == (size_t)-1) {
      ConfitTuiCategoryState *new_categories =
          (ConfitTuiCategoryState *)realloc(state->categories,
                                            (state->category_count + 1U) *
                                                sizeof(state->categories[0]));
      if (new_categories == 0) {
        state->categories = old_categories;
        state->category_count = old_category_count;
        confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0, 0,
                              "failed to allocate tui categories");
        return CONFIT_ERR_INTERNAL;
      }
      state->categories = new_categories;
      category_index = state->category_count;
      state->categories[category_index].name = confit_tui_copy_string(name);
      state->categories[category_index].collapsed =
          confit_tui_old_category_collapsed(old_categories, old_category_count,
                                            name);
      state->categories[category_index].option_count = 0U;
      state->categories[category_index].visible_count = 0U;
      if (state->categories[category_index].name == 0) {
        confit_tui_categories_clear(state);
        state->categories = old_categories;
        state->category_count = old_category_count;
        confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, name, 0, 0,
                              "failed to copy tui category");
        return CONFIT_ERR_INTERNAL;
      }
      state->category_count += 1U;
    }
    state->categories[category_index].option_count += 1U;
  }

  for (index = 0U; index < old_category_count; ++index) {
    free(old_categories[index].name);
  }
  free(old_categories);
  return CONFIT_OK;
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

static const ConfitNamedValue *
confit_tui_find_const_edit(const ConfitTuiState *state, const char *option_id) {
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

  new_edits = (ConfitNamedValue *)realloc(
      state->edits, (state->edit_count + 1U) * sizeof(state->edits[0]));
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

static const char *
confit_tui_effective_target_name(const ConfitProject *project,
                                 const char *profile_name,
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

static int confit_tui_value_is_active(const ConfitValue *value) {
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

static int confit_tui_option_active(const ConfitTuiState *state,
                                    const char *option_id) {
  const ConfitResolvedValue *resolved;

  if (state == 0 || option_id == 0) {
    return 0;
  }
  resolved = confit_resolved_config_find(state->config, option_id);
  return confit_tui_value_is_active(resolved != 0 ? &resolved->value : 0);
}

static void confit_tui_format_dependency_reason(char *out, size_t out_size,
                                                const char *prefix,
                                                const char *option_id) {
  if (out == 0 || out_size == 0U) {
    return;
  }
  (void)snprintf(out, out_size, "%s %s", prefix,
                 confit_tui_text_or_dash(option_id));
  out[out_size - 1U] = '\0';
}

static const char *
confit_tui_find_inactive_dependency(const ConfitTuiState *state,
                                    const ConfitOption *option,
                                    ConfitDependencyKind kind) {
  size_t index;

  if (state == 0 || option == 0) {
    return 0;
  }
  for (index = 0U; index < option->dependency_count; ++index) {
    const ConfitDependencyRef *dependency = &option->dependencies[index];

    if (dependency->kind == kind &&
        !confit_tui_option_active(state, dependency->option_id)) {
      return dependency->option_id;
    }
  }
  return 0;
}

static const char *
confit_tui_find_active_dependency(const ConfitTuiState *state,
                                  const ConfitOption *option,
                                  ConfitDependencyKind kind) {
  size_t index;

  if (state == 0 || option == 0) {
    return 0;
  }
  for (index = 0U; index < option->dependency_count; ++index) {
    const ConfitDependencyRef *dependency = &option->dependencies[index];

    if (dependency->kind == kind &&
        confit_tui_option_active(state, dependency->option_id)) {
      return dependency->option_id;
    }
  }
  return 0;
}

static const char *
confit_tui_find_active_incoming_dependency(const ConfitTuiState *state,
                                           const char *option_id,
                                           ConfitDependencyKind kind) {
  size_t option_index;

  if (state == 0 || option_id == 0) {
    return 0;
  }
  for (option_index = 0U; option_index < state->project->option_count;
       ++option_index) {
    const ConfitOption *source = &state->project->options[option_index];
    size_t dependency_index;

    if (!confit_tui_option_active(state, source->id)) {
      continue;
    }
    for (dependency_index = 0U; dependency_index < source->dependency_count;
         ++dependency_index) {
      const ConfitDependencyRef *dependency =
          &source->dependencies[dependency_index];

      if (dependency->kind == kind && dependency->option_id != 0 &&
          strcmp(dependency->option_id, option_id) == 0) {
        return source->id;
      }
    }
  }
  return 0;
}

static int confit_tui_candidate_block_reason(const ConfitTuiState *state,
                                             const ConfitOption *option,
                                             const ConfitValue *candidate,
                                             char *reason, size_t reason_size) {
  const int candidate_active = confit_tui_value_is_active(candidate);
  const char *dependency_id;

  dependency_id = confit_tui_find_inactive_dependency(
      state, option, CONFIT_DEPENDENCY_VISIBLE_IF);
  if (dependency_id != 0) {
    confit_tui_format_dependency_reason(
        reason, reason_size, "hidden: visible_if inactive", dependency_id);
    return 1;
  }

  dependency_id = confit_tui_find_active_incoming_dependency(
      state, option->id, CONFIT_DEPENDENCY_FORCES);
  if (dependency_id != 0) {
    confit_tui_format_dependency_reason(reason, reason_size,
                                        "blocked: forced by", dependency_id);
    return 1;
  }

  if (candidate_active) {
    dependency_id = confit_tui_find_inactive_dependency(
        state, option, CONFIT_DEPENDENCY_REQUIRES);
    if (dependency_id != 0) {
      confit_tui_format_dependency_reason(reason, reason_size,
                                          "blocked: requires", dependency_id);
      return 1;
    }
    dependency_id = confit_tui_find_active_dependency(
        state, option, CONFIT_DEPENDENCY_CONFLICTS);
    if (dependency_id != 0) {
      confit_tui_format_dependency_reason(
          reason, reason_size, "blocked: conflicts with", dependency_id);
      return 1;
    }
    dependency_id = confit_tui_find_active_incoming_dependency(
        state, option->id, CONFIT_DEPENDENCY_CONFLICTS);
    if (dependency_id != 0) {
      confit_tui_format_dependency_reason(
          reason, reason_size, "blocked: conflicted by", dependency_id);
      return 1;
    }
  } else {
    dependency_id = confit_tui_find_active_incoming_dependency(
        state, option->id, CONFIT_DEPENDENCY_REQUIRES);
    if (dependency_id != 0) {
      confit_tui_format_dependency_reason(
          reason, reason_size, "blocked: required by", dependency_id);
      return 1;
    }
  }
  return 0;
}

static void confit_tui_format_dependency_state(const ConfitTuiState *state,
                                               const ConfitOption *option,
                                               const ConfitValue *current,
                                               char *out, size_t out_size,
                                               int *out_disabled) {
  ConfitValue candidate;
  const char *dependency_id;
  int disabled;

  if (out == 0 || out_size == 0U) {
    return;
  }
  out[0] = '\0';
  disabled = 0;
  confit_value_init(&candidate);

  dependency_id = confit_tui_find_inactive_dependency(
      state, option, CONFIT_DEPENDENCY_VISIBLE_IF);
  if (dependency_id != 0) {
    confit_tui_format_dependency_reason(
        out, out_size, "hidden: visible_if inactive", dependency_id);
    disabled = 1;
  } else {
    dependency_id = confit_tui_find_active_incoming_dependency(
        state, option->id, CONFIT_DEPENDENCY_FORCES);
    if (dependency_id != 0) {
      confit_tui_format_dependency_reason(out, out_size, "blocked: forced by",
                                          dependency_id);
      disabled = 1;
    }
  }

  if (out[0] == '\0' && current != 0 &&
      confit_value_copy(&candidate, current) == CONFIT_OK) {
    switch (candidate.kind) {
    case CONFIT_VALUE_BOOL:
      candidate.as.bool_value = !candidate.as.bool_value;
      break;
    case CONFIT_VALUE_INT:
      if (candidate.as.int_value != 0) {
        candidate.as.int_value = 0;
      }
      break;
    case CONFIT_VALUE_UINT:
      if (candidate.as.uint_value != 0U) {
        candidate.as.uint_value = 0U;
      }
      break;
    case CONFIT_VALUE_FLOAT:
      if (candidate.as.float_value != 0.0) {
        candidate.as.float_value = 0.0;
      }
      break;
    case CONFIT_VALUE_STRING:
    case CONFIT_VALUE_ENUM:
    case CONFIT_VALUE_PATH:
    case CONFIT_VALUE_EMPTY:
    default:
      break;
    }
    if (confit_tui_candidate_block_reason(state, option, &candidate, out,
                                          out_size)) {
      disabled = 1;
    }
  }
  confit_value_clear(&candidate);

  if (out[0] == '\0') {
    dependency_id = confit_tui_find_active_incoming_dependency(
        state, option->id, CONFIT_DEPENDENCY_RECOMMENDS);
    if (dependency_id != 0) {
      confit_tui_format_dependency_reason(out, out_size, "recommended by",
                                          dependency_id);
    } else {
      (void)snprintf(out, out_size, "deps ok");
      out[out_size - 1U] = '\0';
    }
  }
  if (out_disabled != 0) {
    *out_disabled = disabled;
  }
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
      confit_tui_contains(confit_tui_option_category_name(option), search) ||
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
      strcmp(confit_tui_option_category_name(option), state->category) != 0) {
    return 0;
  }
  if (!confit_tui_option_has_tag(option, state->tag)) {
    return 0;
  }
  return 1;
}

static ConfitStatus confit_tui_rebuild_view(ConfitTuiState *state,
                                            ConfitDiagnostic *diagnostic) {
  size_t index;

  free(state->view_indices);
  state->view_indices = 0;
  state->view_count = 0U;
  for (index = 0U; index < state->category_count; ++index) {
    state->categories[index].visible_count = 0U;
  }
  for (index = 0U; index < state->row_count; ++index) {
    if (state->rows[index].kind == CONFIT_TUI_ROW_OPTION &&
        confit_tui_option_visible(state, state->rows[index].option) &&
        state->rows[index].category_index < state->category_count) {
      state->categories[state->rows[index].category_index].visible_count += 1U;
    }
  }
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
    const ConfitTuiRow *row = &state->rows[index];

    if (row->kind == CONFIT_TUI_ROW_CATEGORY) {
      if (row->category_index < state->category_count &&
          state->categories[row->category_index].visible_count > 0U) {
        state->view_indices[state->view_count] = index;
        state->view_count += 1U;
      }
      continue;
    }

    if (row->kind == CONFIT_TUI_ROW_OPTION &&
        row->category_index < state->category_count &&
        !state->categories[row->category_index].collapsed &&
        confit_tui_option_visible(state, row->option)) {
      state->view_indices[state->view_count] = index;
      state->view_count += 1U;
    }
  }
  if (state->selected_view_index >= state->view_count) {
    state->selected_view_index =
        state->view_count == 0U ? 0U : state->view_count - 1U;
  }
  return CONFIT_OK;
}

static void confit_tui_append_summary_text(char *out, size_t out_size,
                                           const char *text) {
  size_t used;
  size_t available;
  size_t length;

  if (out == 0 || out_size == 0U || text == 0 || text[0] == '\0') {
    return;
  }
  used = strlen(out);
  if (used + 1U >= out_size) {
    return;
  }
  available = out_size - used - 1U;
  length = strlen(text);
  if (length > available) {
    length = available;
  }
  memcpy(out + used, text, length);
  out[used + length] = '\0';
  out[out_size - 1U] = '\0';
}

static void confit_tui_append_unique_tag(char *out, size_t out_size,
                                         const char *tag) {
  if (tag == 0 || tag[0] == '\0' || strstr(out, tag) != 0) {
    return;
  }
  if (out[0] != '\0') {
    confit_tui_append_summary_text(out, out_size, ",");
  }
  confit_tui_append_summary_text(out, out_size, tag);
}

static void confit_tui_count_dependencies(
    const ConfitOption *option, size_t *requires_count, size_t *conflicts_count,
    size_t *recommends_count, size_t *forces_count, size_t *visible_count) {
  size_t index;

  for (index = 0U; index < option->dependency_count; ++index) {
    switch (option->dependencies[index].kind) {
    case CONFIT_DEPENDENCY_REQUIRES:
      *requires_count += 1U;
      break;
    case CONFIT_DEPENDENCY_CONFLICTS:
      *conflicts_count += 1U;
      break;
    case CONFIT_DEPENDENCY_RECOMMENDS:
      *recommends_count += 1U;
      break;
    case CONFIT_DEPENDENCY_FORCES:
      *forces_count += 1U;
      break;
    case CONFIT_DEPENDENCY_VISIBLE_IF:
      *visible_count += 1U;
      break;
    default:
      break;
    }
  }
}

static void confit_tui_format_dependency_summary(const ConfitOption *option,
                                                 char *out, size_t out_size) {
  size_t requires_count;
  size_t conflicts_count;
  size_t recommends_count;
  size_t forces_count;
  size_t visible_count;

  if (out == 0 || out_size == 0U) {
    return;
  }
  out[0] = '\0';
  requires_count = 0U;
  conflicts_count = 0U;
  recommends_count = 0U;
  forces_count = 0U;
  visible_count = 0U;
  confit_tui_count_dependencies(option, &requires_count, &conflicts_count,
                                &recommends_count, &forces_count,
                                &visible_count);
  if (requires_count == 0U && conflicts_count == 0U && recommends_count == 0U &&
      forces_count == 0U && visible_count == 0U) {
    (void)snprintf(out, out_size, "deps: -");
  } else {
    (void)snprintf(out, out_size, "deps: r%lu c%lu v%lu f%lu rec%lu",
                   (unsigned long)requires_count,
                   (unsigned long)conflicts_count, (unsigned long)visible_count,
                   (unsigned long)forces_count,
                   (unsigned long)recommends_count);
  }
  out[out_size - 1U] = '\0';
}

static void confit_tui_format_tag_summary(const ConfitOption *option, char *out,
                                          size_t out_size) {
  size_t index;

  if (out == 0 || out_size == 0U) {
    return;
  }
  out[0] = '\0';
  for (index = 0U; index < option->tag_count; ++index) {
    confit_tui_append_unique_tag(out, out_size, option->tags[index]);
  }
  if (out[0] == '\0') {
    (void)snprintf(out, out_size, "-");
  }
  out[out_size - 1U] = '\0';
}

static void confit_tui_format_category_detail(const ConfitTuiState *state,
                                              size_t category_index, char *out,
                                              size_t out_size) {
  char tags[96];
  size_t requires_count;
  size_t conflicts_count;
  size_t recommends_count;
  size_t forces_count;
  size_t visible_count;
  size_t index;

  if (out == 0 || out_size == 0U || category_index >= state->category_count) {
    return;
  }

  tags[0] = '\0';
  requires_count = 0U;
  conflicts_count = 0U;
  recommends_count = 0U;
  forces_count = 0U;
  visible_count = 0U;
  for (index = 0U; index < state->project->option_count; ++index) {
    const ConfitOption *option = &state->project->options[index];
    size_t tag_index;

    if (strcmp(confit_tui_option_category_name(option),
               state->categories[category_index].name) != 0) {
      continue;
    }
    for (tag_index = 0U; tag_index < option->tag_count; ++tag_index) {
      confit_tui_append_unique_tag(tags, sizeof(tags), option->tags[tag_index]);
    }
    confit_tui_count_dependencies(option, &requires_count, &conflicts_count,
                                  &recommends_count, &forces_count,
                                  &visible_count);
  }
  if (tags[0] == '\0') {
    (void)snprintf(tags, sizeof(tags), "-");
  }
  tags[sizeof(tags) - 1U] = '\0';
  (void)snprintf(out, out_size,
                 "deps r%lu c%lu v%lu f%lu rec%lu | tags: %s | %s | %lu "
                 "options",
                 (unsigned long)requires_count, (unsigned long)conflicts_count,
                 (unsigned long)visible_count, (unsigned long)forces_count,
                 (unsigned long)recommends_count, tags,
                 state->categories[category_index].collapsed ? "collapsed"
                                                             : "expanded",
                 (unsigned long)state->categories[category_index].option_count);
  out[out_size - 1U] = '\0';
}

static ConfitStatus confit_tui_refresh_rows(ConfitTuiState *state,
                                            ConfitDiagnostic *diagnostic) {
  size_t category_index;
  size_t option_index;
  size_t row_capacity;
  ConfitStatus status;

  free(state->rows);
  state->rows = 0;
  state->row_count = 0U;
  status = confit_tui_rebuild_categories(state, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }

  row_capacity = state->project->option_count + state->category_count;
  if (row_capacity > 0U) {
    state->rows = (ConfitTuiRow *)calloc(row_capacity, sizeof(state->rows[0]));
    if (state->rows == 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0, 0,
                            "failed to allocate tui rows");
      return CONFIT_ERR_INTERNAL;
    }
  }

  for (category_index = 0U; category_index < state->category_count;
       ++category_index) {
    ConfitTuiRow *heading = &state->rows[state->row_count];

    heading->kind = CONFIT_TUI_ROW_CATEGORY;
    heading->category_index = category_index;
    (void)snprintf(heading->label, sizeof(heading->label), "%s",
                   state->categories[category_index].name);
    heading->label[sizeof(heading->label) - 1U] = '\0';
    confit_tui_format_category_detail(state, category_index, heading->detail,
                                      sizeof(heading->detail));
    heading->value[0] = '\0';
    heading->item.label = heading->label;
    heading->item.detail = heading->detail;
    heading->item.value = heading->value;
    heading->item.depth = 0U;
    heading->item.is_heading = 1;
    heading->item.expanded = !state->categories[category_index].collapsed;
    state->row_count += 1U;

    for (option_index = 0U; option_index < state->project->option_count;
         ++option_index) {
      const ConfitOption *option = &state->project->options[option_index];
      const ConfitResolvedValue *resolved;
      const ConfitNamedValue *edit;
      const ConfitValue *resolved_value;
      char tags[96];
      char deps[96];
      ConfitTuiRow *row;

      if (strcmp(confit_tui_option_category_name(option),
                 state->categories[category_index].name) != 0) {
        continue;
      }

      resolved = confit_resolved_config_find(state->config, option->id);
      resolved_value =
          resolved != 0 ? &resolved->value : &option->default_value;
      edit = confit_tui_find_const_edit(state, option->id);
      confit_tui_format_tag_summary(option, tags, sizeof(tags));
      confit_tui_format_dependency_summary(option, deps, sizeof(deps));

      row = &state->rows[state->row_count];
      row->kind = CONFIT_TUI_ROW_OPTION;
      row->category_index = category_index;
      row->option = option;
      (void)snprintf(row->label, sizeof(row->label), "%s",
                     option->prompt != 0 && option->prompt[0] != '\0'
                         ? option->prompt
                         : option->id);
      row->label[sizeof(row->label) - 1U] = '\0';
      confit_tui_format_dependency_state(
          state, option, resolved_value, row->dependency_state,
          sizeof(row->dependency_state), &row->is_disabled);
      row->detail[0] = '\0';
      if (edit != 0) {
        confit_tui_append_summary_text(row->detail, sizeof(row->detail),
                                       "dirty | ");
      }
      confit_tui_append_summary_text(row->detail, sizeof(row->detail), "id=");
      confit_tui_append_summary_text(row->detail, sizeof(row->detail),
                                     option->id);
      confit_tui_append_summary_text(row->detail, sizeof(row->detail), " | ");
      confit_tui_append_summary_text(row->detail, sizeof(row->detail),
                                     "type=");
      confit_tui_append_summary_text(row->detail, sizeof(row->detail),
                                     confit_option_type_name(option->type));
      confit_tui_append_summary_text(row->detail, sizeof(row->detail), " | ");
      confit_tui_append_summary_text(row->detail, sizeof(row->detail), deps);
      confit_tui_append_summary_text(row->detail, sizeof(row->detail),
                                     " | tags: ");
      confit_tui_append_summary_text(row->detail, sizeof(row->detail), tags);
      confit_tui_append_summary_text(row->detail, sizeof(row->detail), " | ");
      confit_tui_append_summary_text(row->detail, sizeof(row->detail),
                                     "state=");
      confit_tui_append_summary_text(row->detail, sizeof(row->detail),
                                     confit_tui_text_or_dash(
                                         row->dependency_state));

      confit_tui_format_value(option, resolved_value, row->value,
                              sizeof(row->value));
      row->item.label = row->label;
      row->item.detail = row->detail;
      row->item.value = row->value;
      row->item.depth = 1U;
      row->item.is_heading = 0;
      row->item.expanded = 0;
      row->item.is_disabled = row->is_disabled;
      state->row_count += 1U;
    }
  }
  return confit_tui_rebuild_view(state, diagnostic);
}

static ConfitStatus
confit_tui_refresh_resolution(ConfitTuiState *state,
                              ConfitDiagnostic *diagnostic) {
  ConfitResolvedConfig *config;
  ConfitStatus status;

  config = 0;
  status = confit_resolver_resolve(state->project, state->options->profile_name,
                                   state->options->target_name, state->edits,
                                   state->edit_count, &config, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  confit_resolved_config_free(state->config);
  state->config = config;
  return confit_tui_refresh_rows(state, diagnostic);
}

static ConfitStatus confit_tui_ensure_profile(ConfitTuiState *state,
                                              ConfitDiagnostic *diagnostic) {
  ConfitProfile *profile;
  ConfitStatus status;

  if (state->options->schema_edit || state->options->profile_name == 0 ||
      confit_tui_find_profile(state->project, state->options->profile_name) !=
          0) {
    return CONFIT_OK;
  }

  status = confit_project_add_profile(state->project, &profile);
  if (status == CONFIT_OK) {
    status =
        confit_profile_set_identity(profile, state->options->profile_name, 0);
  }
  if (status == CONFIT_OK && state->options->target_name != 0) {
    status = confit_profile_set_target(profile, state->options->target_name);
  }
  if (status == CONFIT_OK) {
    state->dirty = 1;
    state->profile_created = 1;
    (void)snprintf(state->status, sizeof(state->status),
                   "created new profile %s; press s to save",
                   state->options->profile_name);
    state->status[sizeof(state->status) - 1U] = '\0';
  }
  if (status != CONFIT_OK) {
    confit_diagnostic_set(diagnostic, status, state->options->profile_name, 0,
                          0, "failed to create tui profile");
  }
  return status;
}

static ConfitStatus
confit_tui_load_checked_project(ConfitTuiState *state,
                                ConfitDiagnostic *diagnostic) {
  ConfitStatus status;

  status = confit_schema_load_project(state->options->project_root,
                                      &state->project, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_tui_ensure_profile(state, diagnostic);
  }
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

static void confit_tui_validation_message(char *message, size_t message_size,
                                          const char *text) {
  if (message == 0 || message_size == 0U) {
    return;
  }
  (void)snprintf(message, message_size, "%s", text);
  message[message_size - 1U] = '\0';
}

static int confit_tui_text_has_control(const char *text) {
  size_t index;

  for (index = 0U; text != 0 && text[index] != '\0'; ++index) {
    if (iscntrl((unsigned char)text[index])) {
      return 1;
    }
  }
  return 0;
}

static int confit_tui_path_is_absolute(const char *text) {
  if (text == 0 || text[0] == '\0') {
    return 0;
  }
  if (text[0] == '/' || text[0] == '\\') {
    return 1;
  }
  return isalpha((unsigned char)text[0]) && text[1] == ':';
}

static int confit_tui_option_value_in_range(const ConfitOption *option,
                                            const ConfitValue *value) {
  if (option == 0 || value == 0 || !option->has_range) {
    return 1;
  }
  if (option->type == CONFIT_OPTION_TYPE_INT &&
      value->kind == CONFIT_VALUE_INT &&
      option->range_min.kind == CONFIT_VALUE_INT &&
      option->range_max.kind == CONFIT_VALUE_INT) {
    return value->as.int_value >= option->range_min.as.int_value &&
           value->as.int_value <= option->range_max.as.int_value;
  }
  if ((option->type == CONFIT_OPTION_TYPE_UINT ||
       option->type == CONFIT_OPTION_TYPE_HEX) &&
      value->kind == CONFIT_VALUE_UINT &&
      option->range_min.kind == CONFIT_VALUE_UINT &&
      option->range_max.kind == CONFIT_VALUE_UINT) {
    return value->as.uint_value >= option->range_min.as.uint_value &&
           value->as.uint_value <= option->range_max.as.uint_value;
  }
  if (option->type == CONFIT_OPTION_TYPE_FLOAT &&
      value->kind == CONFIT_VALUE_FLOAT &&
      option->range_min.kind == CONFIT_VALUE_FLOAT &&
      option->range_max.kind == CONFIT_VALUE_FLOAT) {
    return value->as.float_value >= option->range_min.as.float_value &&
           value->as.float_value <= option->range_max.as.float_value;
  }
  return 0;
}

static void confit_tui_format_range_message(const ConfitOption *option,
                                            char *message,
                                            size_t message_size) {
  if (message == 0 || message_size == 0U) {
    return;
  }
  if (option->type == CONFIT_OPTION_TYPE_INT &&
      option->range_min.kind == CONFIT_VALUE_INT &&
      option->range_max.kind == CONFIT_VALUE_INT) {
    (void)snprintf(message, message_size,
                   "invalid int: outside range [%lld, %lld]",
                   (long long)option->range_min.as.int_value,
                   (long long)option->range_max.as.int_value);
  } else if (option->type == CONFIT_OPTION_TYPE_UINT &&
             option->range_min.kind == CONFIT_VALUE_UINT &&
             option->range_max.kind == CONFIT_VALUE_UINT) {
    (void)snprintf(message, message_size,
                   "invalid uint: outside range [%llu, %llu]",
                   (unsigned long long)option->range_min.as.uint_value,
                   (unsigned long long)option->range_max.as.uint_value);
  } else if (option->type == CONFIT_OPTION_TYPE_HEX &&
             option->range_min.kind == CONFIT_VALUE_UINT &&
             option->range_max.kind == CONFIT_VALUE_UINT) {
    (void)snprintf(message, message_size,
                   "invalid hex: outside range [0x%llX, 0x%llX]",
                   (unsigned long long)option->range_min.as.uint_value,
                   (unsigned long long)option->range_max.as.uint_value);
  } else if (option->type == CONFIT_OPTION_TYPE_FLOAT &&
             option->range_min.kind == CONFIT_VALUE_FLOAT &&
             option->range_max.kind == CONFIT_VALUE_FLOAT) {
    (void)snprintf(
        message, message_size, "invalid float: outside range [%.6g, %.6g]",
        option->range_min.as.float_value, option->range_max.as.float_value);
  } else {
    (void)snprintf(message, message_size, "invalid value: outside range");
  }
  message[message_size - 1U] = '\0';
}

static ConfitStatus confit_tui_validate_parsed_value(const ConfitOption *option,
                                                     const ConfitValue *value,
                                                     char *message,
                                                     size_t message_size) {
  ConfitOption validation_option;

  if (!confit_tui_option_value_in_range(option, value)) {
    confit_tui_format_range_message(option, message, message_size);
    return CONFIT_ERR_SCHEMA;
  }

  validation_option = *option;
  validation_option.default_value = *value;
  if (confit_option_validate_default(&validation_option) != CONFIT_OK) {
    confit_tui_validation_message(message, message_size,
                                  "invalid value: schema validation failed");
    return CONFIT_ERR_SCHEMA;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_tui_parse_value(const ConfitOption *option,
                                           const char *text,
                                           ConfitValue *out_value,
                                           char *message, size_t message_size) {
  ConfitStatus status;

  confit_value_init(out_value);
  switch (option->type) {
  case CONFIT_OPTION_TYPE_BOOL:
    if (strcmp(text, "true") == 0 || strcmp(text, "1") == 0) {
      confit_value_set_bool(out_value, 1);
      return confit_tui_validate_parsed_value(option, out_value, message,
                                              message_size);
    }
    if (strcmp(text, "false") == 0 || strcmp(text, "0") == 0) {
      confit_value_set_bool(out_value, 0);
      return confit_tui_validate_parsed_value(option, out_value, message,
                                              message_size);
    }
    confit_tui_validation_message(message, message_size,
                                  "invalid bool: expected true or false");
    break;
  case CONFIT_OPTION_TYPE_INT: {
    int64_t value;
    status = confit_tui_parse_int64(text, &value);
    if (status != CONFIT_OK) {
      confit_tui_validation_message(message, message_size,
                                    "invalid int: expected integer");
      return status;
    }
    confit_value_set_int(out_value, value);
    return confit_tui_validate_parsed_value(option, out_value, message,
                                            message_size);
  }
  case CONFIT_OPTION_TYPE_UINT:
  case CONFIT_OPTION_TYPE_HEX: {
    uint64_t value;
    status = confit_tui_parse_uint64(text, &value);
    if (status != CONFIT_OK) {
      confit_tui_validation_message(
          message, message_size,
          option->type == CONFIT_OPTION_TYPE_HEX
              ? "invalid hex: expected unsigned integer or 0x value"
              : "invalid uint: expected unsigned integer");
      return status;
    }
    confit_value_set_uint(out_value, value);
    return confit_tui_validate_parsed_value(option, out_value, message,
                                            message_size);
  }
  case CONFIT_OPTION_TYPE_STRING:
    if (text == 0 || text[0] == '\0') {
      confit_tui_validation_message(message, message_size,
                                    "invalid string: value required");
      return CONFIT_ERR_SCHEMA;
    }
    if (confit_tui_text_has_control(text)) {
      confit_tui_validation_message(message, message_size,
                                    "invalid string: control characters");
      return CONFIT_ERR_SCHEMA;
    }
    status = confit_value_set_string(out_value, text);
    if (status != CONFIT_OK) {
      confit_tui_validation_message(message, message_size,
                                    "invalid string: allocation failed");
      return status;
    }
    return confit_tui_validate_parsed_value(option, out_value, message,
                                            message_size);
  case CONFIT_OPTION_TYPE_ENUM: {
    size_t index;
    for (index = 0U; index < option->enum_value_count; ++index) {
      if (strcmp(option->enum_values[index], text) == 0) {
        status = confit_value_set_enum(out_value, text);
        if (status != CONFIT_OK) {
          confit_tui_validation_message(message, message_size,
                                        "invalid enum: allocation failed");
          return status;
        }
        return confit_tui_validate_parsed_value(option, out_value, message,
                                                message_size);
      }
    }
    confit_tui_validation_message(message, message_size,
                                  "invalid enum: not a candidate");
    break;
  }
  case CONFIT_OPTION_TYPE_FLOAT: {
    double value;
    status = confit_tui_parse_double(text, &value);
    if (status != CONFIT_OK) {
      confit_tui_validation_message(message, message_size,
                                    "invalid float: expected finite value");
      return status;
    }
    confit_value_set_float(out_value, value);
    return confit_tui_validate_parsed_value(option, out_value, message,
                                            message_size);
  }
  case CONFIT_OPTION_TYPE_PATH:
    if (text == 0 || text[0] == '\0') {
      confit_tui_validation_message(message, message_size,
                                    "invalid path: value required");
      return CONFIT_ERR_SCHEMA;
    }
    if (confit_tui_text_has_control(text)) {
      confit_tui_validation_message(message, message_size,
                                    "invalid path: control characters");
      return CONFIT_ERR_SCHEMA;
    }
    if (confit_tui_path_is_absolute(text)) {
      confit_tui_validation_message(message, message_size,
                                    "invalid path: expected relative path");
      return CONFIT_ERR_SCHEMA;
    }
    status = confit_value_set_path(out_value, text);
    if (status != CONFIT_OK) {
      confit_tui_validation_message(message, message_size,
                                    "invalid path: allocation failed");
      return status;
    }
    return confit_tui_validate_parsed_value(option, out_value, message,
                                            message_size);
  default:
    break;
  }

  confit_tui_validation_message(message, message_size, "invalid tui value");
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
  char reason[160];

  reason[0] = '\0';
  if (confit_tui_candidate_block_reason(state, option, value, reason,
                                        sizeof(reason))) {
    (void)snprintf(state->status, sizeof(state->status), "%s", reason);
    state->status[sizeof(state->status) - 1U] = '\0';
    (void)diagnostic;
    return CONFIT_OK;
  }

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

  status =
      confit_tui_resolved_value_for_option(state, option, &current, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  confit_value_init(&value);
  confit_value_set_bool(
      &value, current->kind == CONFIT_VALUE_BOOL ? !current->as.bool_value : 1);
  status = confit_tui_apply_value_edit(state, option, &value, diagnostic);
  confit_value_clear(&value);
  return status;
}

static int confit_tui_value_dialog_validator(const char *text, char *message,
                                             size_t message_size, void *user) {
  ConfitTuiValueValidator *validator = (ConfitTuiValueValidator *)user;
  ConfitValue value;
  ConfitStatus status;

  if (validator == 0 || validator->option == 0) {
    confit_tui_validation_message(message, message_size,
                                  "invalid value validator");
    return 1;
  }
  confit_value_init(&value);
  status = confit_tui_parse_value(validator->option, text, &value, message,
                                  message_size);
  if (status == CONFIT_OK &&
      confit_tui_candidate_block_reason(validator->state, validator->option,
                                        &value, message, message_size)) {
    status = CONFIT_ERR_DEPENDENCY;
  }
  confit_value_clear(&value);
  return status == CONFIT_OK ? 0 : 1;
}

static size_t confit_tui_current_enum_index(const ConfitOption *option,
                                            const ConfitValue *current) {
  size_t index;

  if (option == 0 || current == 0 ||
      (current->kind != CONFIT_VALUE_ENUM &&
       current->kind != CONFIT_VALUE_STRING)) {
    return 0U;
  }
  for (index = 0U; index < option->enum_value_count; ++index) {
    if (current->as.string_value != 0 &&
        strcmp(option->enum_values[index], current->as.string_value) == 0) {
      return index;
    }
  }
  return 0U;
}

static ConfitStatus confit_tui_prompt_enum(ConfitTuiState *state,
                                           const ConfitOption *option,
                                           ConfitDiagnostic *diagnostic) {
  const ConfitValue *current;
  const char **choices;
  size_t selected_index;
  size_t index;
  char header[384];
  char message[160];
  ConfitValue value;
  ConfitStatus status;
  int select_status;

  if (option->enum_value_count == 0U) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                          "enum option has no candidates");
    return CONFIT_ERR_SCHEMA;
  }
  status =
      confit_tui_resolved_value_for_option(state, option, &current, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }

  choices = (const char **)calloc(option->enum_value_count, sizeof(choices[0]));
  if (choices == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, option->id, 0, 0,
                          "failed to allocate enum choices");
    return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < option->enum_value_count; ++index) {
    choices[index] = option->enum_values[index];
  }
  selected_index = confit_tui_current_enum_index(option, current);
  (void)snprintf(header, sizeof(header), "option: %s\nprompt: %s\ncurrent: %s",
                 confit_tui_text_or_dash(option->id),
                 confit_tui_text_or_dash(option->prompt),
                 current->kind == CONFIT_VALUE_ENUM ||
                         current->kind == CONFIT_VALUE_STRING
                     ? confit_tui_text_or_dash(current->as.string_value)
                     : "-");
  header[sizeof(header) - 1U] = '\0';
  select_status = confit_tui_curses_select_dialog(
      "Confit Choice", header, choices, option->enum_value_count,
      selected_index, &selected_index);
  free(choices);
  if (select_status != 0) {
    (void)snprintf(state->status, sizeof(state->status), "cancelled");
    state->status[sizeof(state->status) - 1U] = '\0';
    return CONFIT_OK;
  }

  confit_value_init(&value);
  status = confit_tui_parse_value(option, option->enum_values[selected_index],
                                  &value, message, sizeof(message));
  if (status == CONFIT_OK) {
    status = confit_tui_apply_value_edit(state, option, &value, diagnostic);
    if (status == CONFIT_OK) {
      (void)snprintf(state->status, sizeof(state->status), "selected %s = %s",
                     option->id, option->enum_values[selected_index]);
      state->status[sizeof(state->status) - 1U] = '\0';
    }
  } else {
    confit_diagnostic_set(diagnostic, status, option->id, 0, 0,
                          message[0] != '\0' ? message : "invalid enum value");
  }
  confit_value_clear(&value);
  return status;
}

static ConfitStatus
confit_tui_prompt_typed_value(ConfitTuiState *state, const ConfitOption *option,
                              ConfitDiagnostic *diagnostic) {
  ConfitTuiValueValidator validator;
  char input[256];
  char header[512];
  char prompt[64];
  char message[160];
  ConfitValue value;
  ConfitStatus status;
  int input_status;

  (void)snprintf(
      header, sizeof(header),
      "option: %s\ntype: %s\nprompt: %s\nEnter commits a valid value. Esc "
      "cancels.",
      confit_tui_text_or_dash(option->id),
      confit_option_type_name(option->type),
      confit_tui_text_or_dash(option->prompt));
  header[sizeof(header) - 1U] = '\0';
  (void)snprintf(prompt, sizeof(prompt),
                 "%s value: ", confit_option_type_name(option->type));
  prompt[sizeof(prompt) - 1U] = '\0';
  validator.state = state;
  validator.option = option;
  input_status = confit_tui_curses_read_value_dialog(
      "Confit Value", header, prompt, "Enter a value",
      confit_tui_value_dialog_validator, &validator, input, sizeof(input));
  if (input_status != 0) {
    (void)snprintf(state->status, sizeof(state->status), "cancelled");
    state->status[sizeof(state->status) - 1U] = '\0';
    return CONFIT_OK;
  }

  confit_value_init(&value);
  message[0] = '\0';
  status =
      confit_tui_parse_value(option, input, &value, message, sizeof(message));
  if (status == CONFIT_OK) {
    status = confit_tui_apply_value_edit(state, option, &value, diagnostic);
    if (status == CONFIT_OK) {
      state->status[0] = '\0';
      confit_tui_append_summary_text(state->status, sizeof(state->status),
                                     "accepted ");
      confit_tui_append_summary_text(state->status, sizeof(state->status),
                                     option->id);
      confit_tui_append_summary_text(state->status, sizeof(state->status),
                                     " = ");
      confit_tui_append_summary_text(state->status, sizeof(state->status),
                                     input);
    }
  } else {
    confit_diagnostic_set(diagnostic, status, option->id, 0, 0,
                          message[0] != '\0' ? message : "invalid tui value");
  }
  confit_value_clear(&value);
  return status;
}

static ConfitStatus confit_tui_prompt_edit(ConfitTuiState *state,
                                           const ConfitOption *option,
                                           ConfitDiagnostic *diagnostic) {
  if (option->type == CONFIT_OPTION_TYPE_BOOL) {
    return confit_tui_toggle_bool(state, option, diagnostic);
  }
  if (option->type == CONFIT_OPTION_TYPE_ENUM) {
    return confit_tui_prompt_enum(state, option, diagnostic);
  }
  return confit_tui_prompt_typed_value(state, option, diagnostic);
}

static const ConfitOption *
confit_tui_selected_option(const ConfitTuiState *state) {
  if (state->view_count == 0U ||
      state->selected_view_index >= state->view_count) {
    return 0;
  }
  return state->rows[state->view_indices[state->selected_view_index]].option;
}

static ConfitTuiRow *confit_tui_selected_row(ConfitTuiState *state) {
  if (state->view_count == 0U ||
      state->selected_view_index >= state->view_count) {
    return 0;
  }
  return &state->rows[state->view_indices[state->selected_view_index]];
}

static void confit_tui_select_first_option(ConfitTuiState *state) {
  size_t index;

  for (index = 0U; index < state->view_count; ++index) {
    ConfitTuiRow *row = &state->rows[state->view_indices[index]];

    if (row->kind == CONFIT_TUI_ROW_OPTION) {
      state->selected_view_index = index;
      return;
    }
  }
  state->selected_view_index = 0U;
}

static int confit_tui_row_matches_search(const ConfitTuiState *state,
                                         const ConfitTuiRow *row) {
  return row != 0 && row->kind == CONFIT_TUI_ROW_OPTION &&
         row->category_index < state->category_count &&
         confit_tui_option_visible(state, row->option) &&
         confit_tui_option_matches_search(row->option, state->search);
}

static size_t confit_tui_selected_row_index(const ConfitTuiState *state) {
  if (state->view_count == 0U ||
      state->selected_view_index >= state->view_count) {
    return (size_t)-1;
  }
  return state->view_indices[state->selected_view_index];
}

static void confit_tui_refresh_search_position(ConfitTuiState *state) {
  const size_t selected_row_index = confit_tui_selected_row_index(state);
  size_t index;

  state->search_count = 0U;
  state->search_position = 0U;
  if (state->search[0] == '\0') {
    return;
  }
  for (index = 0U; index < state->row_count; ++index) {
    if (!confit_tui_row_matches_search(state, &state->rows[index])) {
      continue;
    }
    state->search_count += 1U;
    if (index == selected_row_index) {
      state->search_position = state->search_count;
    }
  }
}

static ConfitStatus confit_tui_select_row_index(ConfitTuiState *state,
                                                size_t row_index,
                                                ConfitDiagnostic *diagnostic) {
  ConfitStatus status;
  size_t index;

  if (row_index >= state->row_count) {
    return CONFIT_OK;
  }
  if (state->rows[row_index].kind == CONFIT_TUI_ROW_OPTION &&
      state->rows[row_index].category_index < state->category_count &&
      state->categories[state->rows[row_index].category_index].collapsed) {
    state->categories[state->rows[row_index].category_index].collapsed = 0;
    status = confit_tui_refresh_rows(state, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  for (index = 0U; index < state->view_count; ++index) {
    if (state->view_indices[index] == row_index) {
      state->selected_view_index = index;
      confit_tui_refresh_search_position(state);
      return CONFIT_OK;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_tui_search_jump(ConfitTuiState *state, int direction,
                                           const char *status_prefix,
                                           ConfitDiagnostic *diagnostic) {
  const size_t selected_row_index = confit_tui_selected_row_index(state);
  size_t first_match;
  size_t best_match;
  size_t match_count;
  size_t match_position;
  size_t index;
  int have_first;
  int have_best;

  first_match = 0U;
  best_match = 0U;
  match_count = 0U;
  match_position = 0U;
  have_first = 0;
  have_best = 0;
  if (state->search[0] == '\0') {
    (void)snprintf(state->status, sizeof(state->status), "no search query");
    state->status[sizeof(state->status) - 1U] = '\0';
    confit_tui_refresh_search_position(state);
    return CONFIT_OK;
  }

  for (index = 0U; index < state->row_count; ++index) {
    if (!confit_tui_row_matches_search(state, &state->rows[index])) {
      continue;
    }
    match_count += 1U;
    if (!have_first) {
      first_match = index;
      have_first = 1;
    }
    if (direction >= 0) {
      if (selected_row_index == (size_t)-1 || index > selected_row_index) {
        if (!have_best) {
          best_match = index;
          match_position = match_count;
          have_best = 1;
        }
      }
    } else if (selected_row_index == (size_t)-1 || index < selected_row_index) {
      best_match = index;
      match_position = match_count;
      have_best = 1;
    }
  }

  if (match_count == 0U) {
    state->search_count = 0U;
    state->search_position = 0U;
    (void)snprintf(state->status, sizeof(state->status), "search 0/0: %s",
                   state->search);
    state->status[sizeof(state->status) - 1U] = '\0';
    return CONFIT_OK;
  }
  if (!have_best) {
    best_match = direction >= 0 ? first_match : first_match;
    if (direction < 0) {
      for (index = 0U; index < state->row_count; ++index) {
        if (confit_tui_row_matches_search(state, &state->rows[index])) {
          best_match = index;
          match_position += 1U;
        }
      }
    } else {
      match_position = 1U;
    }
  }

  if (confit_tui_select_row_index(state, best_match, diagnostic) != CONFIT_OK) {
    return CONFIT_ERR_INTERNAL;
  }
  state->search_count = match_count;
  state->search_position = match_position;
  (void)snprintf(state->status, sizeof(state->status), "%s %lu/%lu: %s",
                 status_prefix, (unsigned long)state->search_position,
                 (unsigned long)state->search_count,
                 confit_tui_text_or_dash(state->rows[best_match].option->id));
  state->status[sizeof(state->status) - 1U] = '\0';
  return CONFIT_OK;
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

static const ConfitValue *
confit_tui_saved_value_for(const ConfitTuiState *state,
                           const ConfitNamedValue *profile_value) {
  const ConfitNamedValue *edit =
      confit_tui_find_const_edit(state, profile_value->option_id);
  return edit != 0 ? &edit->value : &profile_value->value;
}

static ConfitStatus
confit_tui_append_profile_value(ConfitTuiTextBuilder *builder,
                                const ConfitTuiState *state,
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

static ConfitStatus
confit_tui_build_profile_toml(const ConfitTuiState *state,
                              const ConfitProfile *profile, char **out_text,
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
  target_name =
      profile->target != 0 ? profile->target : state->options->target_name;
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
        confit_tui_saved_value_for(state, &profile->values[index]), diagnostic);
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

static ConfitStatus confit_tui_profile_path(const ConfitTuiState *state,
                                            const ConfitProfile *profile,
                                            char *out_dir, size_t out_dir_size,
                                            char *out_path,
                                            size_t out_path_size,
                                            ConfitDiagnostic *diagnostic) {
  char config_dir[1024];
  char file_name[256];
  ConfitStatus status;

  if (state == 0 || profile == 0 || out_dir == 0 || out_path == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid profile path argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  status =
      confit_host_path_join(config_dir, sizeof(config_dir),
                            state->options->project_root, "config", diagnostic);
  if (status == CONFIT_OK) {
    status = confit_host_path_join(out_dir, out_dir_size, config_dir,
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
    status = confit_host_path_join(out_path, out_path_size, out_dir, file_name,
                                   diagnostic);
  }
  return status;
}

static int confit_tui_profile_file_exists(const char *profile_path) {
  char *existing;
  ConfitStatus status;

  existing = 0;
  status = confit_host_read_text_file(profile_path, &existing, 0, 0);
  if (status == CONFIT_OK) {
    confit_host_free(existing);
    return 1;
  }
  return 0;
}

static int confit_tui_confirm_overwrite_profile(ConfitTuiState *state,
                                                const char *profile_path) {
  static const char *items[] = {"Overwrite profile", "Cancel"};
  char header[512];
  size_t selected_index;
  int select_status;

  (void)snprintf(header, sizeof(header),
                 "profile=%s\npath=%s\nExisting profile TOML will be "
                 "replaced.",
                 confit_tui_text_or_dash(state->options->profile_name),
                 confit_tui_text_or_dash(profile_path));
  header[sizeof(header) - 1U] = '\0';
  selected_index = 0U;
  select_status = confit_tui_curses_select_dialog(
      "Overwrite Profile", header, items, 2U, selected_index, &selected_index);
  if (select_status != 0 || selected_index != 0U) {
    (void)snprintf(state->status, sizeof(state->status), "save cancelled");
    state->status[sizeof(state->status) - 1U] = '\0';
    return 0;
  }
  return 1;
}

static ConfitStatus
confit_tui_reload_saved_project(ConfitTuiState *state,
                                ConfitDiagnostic *diagnostic) {
  ConfitProject *project;
  ConfitGraph *graph;
  ConfitResolvedConfig *config;
  ConfitStatus status;

  project = 0;
  graph = 0;
  config = 0;
  status = confit_schema_load_project(state->options->project_root, &project,
                                      diagnostic);
  if (status == CONFIT_OK) {
    status = confit_graph_build(project, &graph, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_graph_validate(graph, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_resolver_resolve(project, state->options->profile_name,
                                     state->options->target_name, 0, 0, &config,
                                     diagnostic);
  }
  if (status != CONFIT_OK) {
    confit_resolved_config_free(config);
    confit_graph_free(graph);
    confit_project_free(project);
    return status;
  }

  confit_project_free(state->project);
  confit_graph_free(state->graph);
  confit_resolved_config_free(state->config);
  confit_tui_edits_clear(state);
  state->project = project;
  state->graph = graph;
  state->config = config;
  state->dirty = 0;
  state->profile_created = 0;
  return confit_tui_refresh_rows(state, diagnostic);
}

static ConfitStatus confit_tui_save_profile(ConfitTuiState *state,
                                            ConfitDiagnostic *diagnostic) {
  ConfitProfile *profile;
  char *toml;
  char profile_dir[1024];
  char profile_path[1024];
  ConfitDiagnostic reload_diagnostic;
  ConfitStatus status;

  profile =
      confit_tui_find_profile(state->project, state->options->profile_name);
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
  status =
      confit_tui_profile_path(state, profile, profile_dir, sizeof(profile_dir),
                              profile_path, sizeof(profile_path), diagnostic);
  if (status == CONFIT_OK) {
    if (confit_tui_profile_file_exists(profile_path) &&
        !confit_tui_confirm_overwrite_profile(state, profile_path)) {
      return CONFIT_OK;
    }
  }
  if (status == CONFIT_OK) {
    status = confit_tui_build_profile_toml(state, profile, &toml, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_make_directories(profile_dir, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_write_text_file(profile_path, toml, diagnostic);
  }
  free(toml);
  if (status == CONFIT_OK) {
    confit_diagnostic_init(&reload_diagnostic);
    status = confit_tui_reload_saved_project(state, &reload_diagnostic);
    if (status == CONFIT_OK) {
      state->status[0] = '\0';
      confit_tui_append_summary_text(
          state->status, sizeof(state->status),
          "saved and reloaded; full validation ok: ");
      confit_tui_append_summary_text(state->status, sizeof(state->status),
                                     profile_path);
    } else {
      confit_tui_set_status_from_diagnostic(state, "saved but reload failed",
                                            status, &reload_diagnostic);
      return CONFIT_OK;
    }
  }
  return status;
}

static ConfitStatus confit_tui_set_filter(char *slot, size_t slot_size,
                                          const char *prompt,
                                          ConfitTuiState *state,
                                          ConfitDiagnostic *diagnostic) {
  char input[128];
  int input_status;
  ConfitStatus status;

  input_status = confit_tui_curses_read_line(prompt, input, sizeof(input));
  if (input_status != 0) {
    if (input_status > 0) {
      (void)snprintf(state->status, sizeof(state->status), "cancelled");
      state->status[sizeof(state->status) - 1U] = '\0';
    }
    return CONFIT_OK;
  }
  (void)snprintf(slot, slot_size, "%s", input);
  slot[slot_size - 1U] = '\0';
  state->selected_view_index = 0U;
  (void)snprintf(state->status, sizeof(state->status), "filter updated");
  state->status[sizeof(state->status) - 1U] = '\0';
  status = confit_tui_rebuild_view(state, diagnostic);
  if (status == CONFIT_OK) {
    confit_tui_select_first_option(state);
    confit_tui_refresh_search_position(state);
  }
  return status;
}

static ConfitStatus confit_tui_set_search(ConfitTuiState *state,
                                          ConfitDiagnostic *diagnostic) {
  char input[128];
  int input_status;

  input_status = confit_tui_curses_read_line("search: ", input, sizeof(input));
  if (input_status != 0) {
    if (input_status > 0) {
      (void)snprintf(state->status, sizeof(state->status), "cancelled");
      state->status[sizeof(state->status) - 1U] = '\0';
    }
    return CONFIT_OK;
  }
  (void)snprintf(state->search, sizeof(state->search), "%s", input);
  state->search[sizeof(state->search) - 1U] = '\0';
  if (state->search[0] == '\0') {
    state->search_count = 0U;
    state->search_position = 0U;
    (void)snprintf(state->status, sizeof(state->status), "search cleared");
    state->status[sizeof(state->status) - 1U] = '\0';
    return CONFIT_OK;
  }
  return confit_tui_search_jump(state, 1, "search", diagnostic);
}

static void confit_tui_clear_filters(ConfitTuiState *state) {
  state->search[0] = '\0';
  state->search_count = 0U;
  state->search_position = 0U;
  state->category[0] = '\0';
  state->tag[0] = '\0';
  state->selected_view_index = 0U;
  (void)snprintf(state->status, sizeof(state->status), "filters cleared");
  state->status[sizeof(state->status) - 1U] = '\0';
}

static ConfitStatus confit_tui_detail_append_line(ConfitTuiTextBuilder *builder,
                                                  const char *label,
                                                  const char *value) {
  ConfitStatus status;

  status = confit_tui_text_append(builder, label);
  if (status == CONFIT_OK) {
    status = confit_tui_text_append(builder, value != 0 ? value : "-");
  }
  if (status == CONFIT_OK) {
    status = confit_tui_text_append(builder, "\n");
  }
  return status;
}

static ConfitStatus confit_tui_detail_append_wrapped_word(
    ConfitTuiTextBuilder *builder, const char *word, size_t word_size,
    size_t wrap_width, size_t *line_size) {
  ConfitStatus status;

  if (wrap_width == 0U) {
    wrap_width = 78U;
  }
  while (word_size > 0U) {
    size_t room;
    size_t take;
    size_t index;

    if (*line_size > 0U && *line_size + 1U + word_size > wrap_width) {
      status = confit_tui_text_append(builder, "\n");
      if (status != CONFIT_OK) {
        return status;
      }
      *line_size = 0U;
    }
    if (*line_size > 0U) {
      status = confit_tui_text_append(builder, " ");
      if (status != CONFIT_OK) {
        return status;
      }
      *line_size += 1U;
    }

    room = wrap_width > *line_size ? wrap_width - *line_size : 1U;
    take = word_size < room ? word_size : room;
    for (index = 0U; index < take; ++index) {
      status = confit_tui_text_append_char(builder, word[index]);
      if (status != CONFIT_OK) {
        return status;
      }
    }
    *line_size += take;
    word += take;
    word_size -= take;
    if (word_size > 0U) {
      status = confit_tui_text_append(builder, "\n");
      if (status != CONFIT_OK) {
        return status;
      }
      *line_size = 0U;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_tui_detail_append_wrapped(
    ConfitTuiTextBuilder *builder, const char *text, size_t wrap_width) {
  const char *cursor;
  size_t line_size;
  ConfitStatus status;

  if (text == 0 || text[0] == '\0') {
    return confit_tui_text_append(builder, "-");
  }

  cursor = text;
  line_size = 0U;
  while (*cursor != '\0') {
    const char *word;
    size_t word_size;

    if (*cursor == '\n') {
      status = confit_tui_text_append(builder, "\n");
      if (status != CONFIT_OK) {
        return status;
      }
      line_size = 0U;
      cursor += 1;
      continue;
    }
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r') {
      cursor += 1;
    }
    if (*cursor == '\0') {
      break;
    }
    word = cursor;
    while (*cursor != '\0' && *cursor != '\n' && *cursor != ' ' &&
           *cursor != '\t' && *cursor != '\r') {
      cursor += 1;
    }
    word_size = (size_t)(cursor - word);
    status = confit_tui_detail_append_wrapped_word(
        builder, word, word_size, wrap_width, &line_size);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_tui_detail_append_tags(ConfitTuiTextBuilder *builder,
                                                  const ConfitOption *option) {
  ConfitStatus status;
  size_t index;

  status = confit_tui_text_append(builder, "tags: ");
  if (status == CONFIT_OK && option->tag_count == 0U) {
    status = confit_tui_text_append(builder, "-");
  }
  for (index = 0U; status == CONFIT_OK && index < option->tag_count; ++index) {
    if (index > 0U) {
      status = confit_tui_text_append(builder, ", ");
    }
    if (status == CONFIT_OK) {
      status = confit_tui_text_append(
          builder, confit_tui_text_or_dash(option->tags[index]));
    }
  }
  if (status == CONFIT_OK) {
    status = confit_tui_text_append(builder, "\n");
  }
  return status;
}

static ConfitStatus confit_tui_detail_append_dependency_kind(
    ConfitTuiTextBuilder *builder, const ConfitOption *option,
    ConfitDependencyKind kind, const char *label) {
  ConfitStatus status;
  size_t index;
  size_t written;

  status = confit_tui_text_append(builder, label);
  written = 0U;
  for (index = 0U; status == CONFIT_OK && index < option->dependency_count;
       ++index) {
    if (option->dependencies[index].kind != kind) {
      continue;
    }
    if (written > 0U) {
      status = confit_tui_text_append(builder, ", ");
    }
    if (status == CONFIT_OK) {
      status = confit_tui_text_append(
          builder,
          confit_tui_text_or_dash(option->dependencies[index].option_id));
    }
    written += 1U;
  }
  if (status == CONFIT_OK && written == 0U) {
    status = confit_tui_text_append(builder, "-");
  }
  if (status == CONFIT_OK) {
    status = confit_tui_text_append(builder, "\n");
  }
  return status;
}

static ConfitStatus
confit_tui_detail_append_dependencies(ConfitTuiTextBuilder *builder,
                                      const ConfitOption *option) {
  ConfitStatus status;

  status = confit_tui_text_append(builder, "\nDependencies\n");
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_dependency_kind(
        builder, option, CONFIT_DEPENDENCY_REQUIRES, "requires: ");
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_dependency_kind(
        builder, option, CONFIT_DEPENDENCY_CONFLICTS, "conflicts: ");
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_dependency_kind(
        builder, option, CONFIT_DEPENDENCY_FORCES, "forces: ");
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_dependency_kind(
        builder, option, CONFIT_DEPENDENCY_RECOMMENDS, "recommends: ");
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_dependency_kind(
        builder, option, CONFIT_DEPENDENCY_VISIBLE_IF, "visible_if: ");
  }
  return status;
}

static ConfitStatus confit_tui_detail_append_runtime_dependency_state(
    ConfitTuiTextBuilder *builder, const ConfitTuiState *state,
    const ConfitOption *option, const ConfitValue *current) {
  char state_line[160];
  const char *dependency_id;
  int disabled;
  ConfitStatus status;

  disabled = 0;
  confit_tui_format_dependency_state(state, option, current, state_line,
                                     sizeof(state_line), &disabled);
  status = confit_tui_text_append(builder, "\nDependency State\n");
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(
        builder, "display policy: ",
        "show dimmed, not hidden, so blocked reasons remain inspectable");
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(
        builder, "row state: ", state_line[0] != '\0' ? state_line : "deps ok");
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(
        builder, "edit policy: ", disabled ? "blocked or guarded" : "editable");
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(
        builder, "edit block: ", disabled ? state_line : "-");
  }
  dependency_id = confit_tui_find_inactive_dependency(
      state, option, CONFIT_DEPENDENCY_VISIBLE_IF);
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(
        builder,
        "visible_if inactive: ", dependency_id != 0 ? dependency_id : "-");
  }
  dependency_id = confit_tui_find_inactive_dependency(
      state, option, CONFIT_DEPENDENCY_REQUIRES);
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(
        builder,
        "requires inactive: ", dependency_id != 0 ? dependency_id : "-");
  }
  dependency_id = confit_tui_find_active_dependency(
      state, option, CONFIT_DEPENDENCY_CONFLICTS);
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(
        builder,
        "conflicts active: ", dependency_id != 0 ? dependency_id : "-");
  }
  dependency_id = confit_tui_find_active_incoming_dependency(
      state, option->id, CONFIT_DEPENDENCY_FORCES);
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(
        builder,
        "forced by active: ", dependency_id != 0 ? dependency_id : "-");
  }
  dependency_id = confit_tui_find_active_incoming_dependency(
      state, option->id, CONFIT_DEPENDENCY_REQUIRES);
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(
        builder,
        "required by active: ", dependency_id != 0 ? dependency_id : "-");
  }
  dependency_id = confit_tui_find_active_incoming_dependency(
      state, option->id, CONFIT_DEPENDENCY_CONFLICTS);
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(
        builder,
        "conflicted by active: ", dependency_id != 0 ? dependency_id : "-");
  }
  dependency_id = confit_tui_find_active_incoming_dependency(
      state, option->id, CONFIT_DEPENDENCY_RECOMMENDS);
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(
        builder,
        "recommended by active: ", dependency_id != 0 ? dependency_id : "-");
  }
  return status;
}

static ConfitStatus
confit_tui_build_option_detail(const ConfitTuiState *state,
                               const ConfitOption *option, char **out_text,
                               ConfitDiagnostic *diagnostic) {
  ConfitTuiTextBuilder builder;
  const ConfitResolvedValue *resolved;
  char current_value[128];
  char default_value[128];
  ConfitStatus status;

  *out_text = 0;
  if (option == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0, 0,
                          "missing detail option");
    return CONFIT_ERR_INTERNAL;
  }

  resolved = confit_resolved_config_find(state->config, option->id);
  confit_tui_format_value(
      option, resolved != 0 ? &resolved->value : &option->default_value,
      current_value, sizeof(current_value));
  confit_tui_format_value(option, &option->default_value, default_value,
                          sizeof(default_value));

  confit_tui_text_builder_init(&builder);
  status = confit_tui_detail_append_line(
      &builder, "prompt: ", confit_tui_text_or_dash(option->prompt));
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(&builder, "id: ", option->id);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(
        &builder, "type: ", confit_option_type_name(option->type));
  }
  if (status == CONFIT_OK) {
    status =
        confit_tui_detail_append_line(&builder, "current: ", current_value);
  }
  if (status == CONFIT_OK) {
    status =
        confit_tui_detail_append_line(&builder, "default: ", default_value);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(
        &builder, "source: ",
        resolved != 0 ? confit_tui_text_or_dash(resolved->source) : "default");
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(
        &builder, "category: ", confit_tui_option_category_name(option));
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_tags(&builder, option);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_runtime_dependency_state(
        &builder, state, option,
        resolved != 0 ? &resolved->value : &option->default_value);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_dependencies(&builder, option);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_text_append(&builder, "\nHelp\n");
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_wrapped(
        &builder,
        option->help != 0 && option->help[0] != '\0' ? option->help : "-",
        78U);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_text_append(&builder, "\n");
  }
  if (status != CONFIT_OK) {
    free(builder.text);
    confit_diagnostic_set(diagnostic, status, option->id, 0, 0,
                          "failed to build tui detail");
    return status;
  }
  *out_text = builder.text;
  return CONFIT_OK;
}

static ConfitStatus
confit_tui_build_category_detail(const ConfitTuiState *state,
                                 const ConfitTuiRow *row, char **out_text,
                                 ConfitDiagnostic *diagnostic) {
  ConfitTuiTextBuilder builder;
  ConfitStatus status;
  char count_line[128];
  const char *name;

  *out_text = 0;
  if (row == 0 || row->category_index >= state->category_count) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0, 0,
                          "missing detail category");
    return CONFIT_ERR_INTERNAL;
  }
  name = state->categories[row->category_index].name;
  (void)snprintf(
      count_line, sizeof(count_line), "%lu visible, %lu total",
      (unsigned long)state->categories[row->category_index].visible_count,
      (unsigned long)state->categories[row->category_index].option_count);
  count_line[sizeof(count_line) - 1U] = '\0';

  confit_tui_text_builder_init(&builder);
  status = confit_tui_detail_append_line(&builder, "menu: ", name);
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(
        &builder, "state: ",
        state->categories[row->category_index].collapsed ? "collapsed"
                                                         : "expanded");
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(&builder, "options: ", count_line);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(&builder, "summary: ", row->detail);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_text_append(&builder,
                                    "\nSelect an option inside this menu to "
                                    "inspect current value, default, "
                                    "source, dependencies, and help text.\n");
  }
  if (status != CONFIT_OK) {
    free(builder.text);
    confit_diagnostic_set(diagnostic, status, name, 0, 0,
                          "failed to build tui category detail");
    return status;
  }
  *out_text = builder.text;
  return CONFIT_OK;
}

static size_t confit_tui_detail_line_count(const char *text) {
  size_t count;
  const char *cursor;

  if (text == 0 || text[0] == '\0') {
    return 0U;
  }
  count = 1U;
  for (cursor = text; *cursor != '\0'; ++cursor) {
    if (*cursor == '\n') {
      count += 1U;
    }
  }
  return count;
}

static void confit_tui_detail_scroll(size_t line_count, size_t *first_line,
                                     ConfitTuiKey key) {
  size_t step;

  switch (key) {
  case CONFIT_TUI_KEY_DOWN:
    if (*first_line + 1U < line_count) {
      *first_line += 1U;
    }
    break;
  case CONFIT_TUI_KEY_UP:
    if (*first_line > 0U) {
      *first_line -= 1U;
    }
    break;
  case CONFIT_TUI_KEY_PAGE_DOWN:
    step = confit_tui_curses_page_step();
    if (*first_line + step < line_count) {
      *first_line += step;
    } else if (line_count > 0U) {
      *first_line = line_count - 1U;
    }
    break;
  case CONFIT_TUI_KEY_PAGE_UP:
    step = confit_tui_curses_page_step();
    if (*first_line > step) {
      *first_line -= step;
    } else {
      *first_line = 0U;
    }
    break;
  case CONFIT_TUI_KEY_HOME:
    *first_line = 0U;
    break;
  case CONFIT_TUI_KEY_END:
    *first_line = line_count > 0U ? line_count - 1U : 0U;
    break;
  default:
    break;
  }
}

static ConfitStatus confit_tui_show_detail(ConfitTuiState *state,
                                           const char *target_name,
                                           ConfitDiagnostic *diagnostic) {
  ConfitTuiRow *row;
  char *body;
  char header[320];
  char status_line[256];
  size_t first_line;
  size_t line_count;
  ConfitStatus status;

  row = confit_tui_selected_row(state);
  if (row == 0) {
    (void)snprintf(state->status, sizeof(state->status), "no detail selected");
    state->status[sizeof(state->status) - 1U] = '\0';
    return CONFIT_OK;
  }

  body = 0;
  if (row->kind == CONFIT_TUI_ROW_CATEGORY) {
    status = confit_tui_build_category_detail(state, row, &body, diagnostic);
  } else {
    status =
        confit_tui_build_option_detail(state, row->option, &body, diagnostic);
  }
  if (status != CONFIT_OK) {
    return status;
  }

  (void)snprintf(header, sizeof(header),
                 "project=%s profile=%s target=%s selected=%s",
                 confit_tui_text_or_dash(state->project->name),
                 confit_tui_text_or_dash(state->options->profile_name),
                 confit_tui_text_or_dash(target_name),
                 confit_tui_text_or_dash(row->item.label));
  header[sizeof(header) - 1U] = '\0';
  first_line = 0U;
  line_count = confit_tui_detail_line_count(body);

  do {
    ConfitTuiKey key;

    (void)snprintf(status_line, sizeof(status_line), "detail line %lu/%lu",
                   line_count == 0U ? 0UL : (unsigned long)(first_line + 1U),
                   (unsigned long)line_count);
    status_line[sizeof(status_line) - 1U] = '\0';
    if (confit_tui_curses_render_text(
            "Confit Help", header, body,
            "arrows/jk scroll PgUp/PgDn Home/End Esc/q/h/? close", status_line,
            first_line) != 0) {
      free(body);
      return CONFIT_ERR_INTERNAL;
    }

    key = confit_tui_curses_read_key();
    if (key == CONFIT_TUI_KEY_QUIT || key == CONFIT_TUI_KEY_CANCEL ||
        key == CONFIT_TUI_KEY_HELP || key == CONFIT_TUI_KEY_KEYMAP_HELP ||
        key == CONFIT_TUI_KEY_ENTER) {
      break;
    }
    confit_tui_detail_scroll(line_count, &first_line, key);
  } while (1);

  free(body);
  (void)snprintf(state->status, sizeof(state->status), "closed detail");
  state->status[sizeof(state->status) - 1U] = '\0';
  return CONFIT_OK;
}

static void confit_tui_profile_format_key_legend(
    const ConfitTuiState *state, const ConfitTuiRow *selected, char *out,
    size_t out_size) {
  const int has_filter =
      state != 0 && (state->search[0] != '\0' || state->category[0] != '\0' ||
                     state->tag[0] != '\0');

  if (out == 0 || out_size == 0U) {
    return;
  }
  if (selected != 0 && selected->kind == CONFIT_TUI_ROW_CATEGORY) {
    if (state != 0 && state->dirty) {
      (void)snprintf(out, out_size,
                     "keys: move jk/arrows Pg/Home/End | enter collapse | s "
                     "save | / search | c/t filter%s | ? help | q quit",
                     has_filter ? " x clear" : "");
    } else {
      (void)snprintf(out, out_size,
                     "keys: move jk/arrows Pg/Home/End | enter collapse | / "
                     "search | c/t filter%s | ? help | q quit",
                     has_filter ? " x clear" : "");
    }
  } else {
    if (state != 0 && state->dirty) {
      (void)snprintf(out, out_size,
                     "keys: move jk/arrows Pg/Home/End | enter/e edit | s "
                     "save | / search n/N | c/t filter%s | ? help | q quit",
                     has_filter ? " x clear" : "");
    } else {
      (void)snprintf(out, out_size,
                     "keys: move jk/arrows Pg/Home/End | enter/e edit | / "
                     "search n/N | c/t filter%s | ? help | q quit",
                     has_filter ? " x clear" : "");
    }
  }
  out[out_size - 1U] = '\0';
}

static ConfitStatus confit_tui_render_screen(const ConfitTuiState *state,
                                             const char *target_name) {
  ConfitTuiListItem *items;
  ConfitTuiScreen screen;
  char header[320];
  char key_legend[128];
  char status_line[384];
  const ConfitTuiRow *selected;
  size_t index;

  items = 0;
  if (state->view_count > 0U) {
    items = (ConfitTuiListItem *)calloc(state->view_count, sizeof(items[0]));
    if (items == 0) {
      return CONFIT_ERR_INTERNAL;
    }
    for (index = 0U; index < state->view_count; ++index) {
      items[index] = state->rows[state->view_indices[index]].item;
    }
  }
  selected = state->view_count > 0U &&
                     state->selected_view_index < state->view_count
                 ? &state->rows[state->view_indices[state->selected_view_index]]
                 : 0;

  (void)snprintf(
      header, sizeof(header),
      "mode=profile project=%s profile=%s target=%s dirty=%s | row %lu/%lu | "
      "search=%s %lu/%lu | filter=%s/%s",
      confit_tui_text_or_dash(state->project->name),
      confit_tui_text_or_dash(state->options->profile_name),
      confit_tui_text_or_dash(target_name), state->dirty ? "yes" : "no",
      state->view_count == 0U
          ? 0UL
          : (unsigned long)(state->selected_view_index + 1U),
      (unsigned long)state->view_count,
      confit_tui_text_or_dash(state->search),
      (unsigned long)state->search_position, (unsigned long)state->search_count,
      confit_tui_text_or_dash(state->category),
      confit_tui_text_or_dash(state->tag));
  header[sizeof(header) - 1U] = '\0';
  confit_tui_profile_format_key_legend(state, selected, key_legend,
                                       sizeof(key_legend));
  (void)snprintf(status_line, sizeof(status_line), "%s",
                 state->status[0] != '\0' ? state->status : "ready");
  status_line[sizeof(status_line) - 1U] = '\0';

  screen.title = "Confit TUI - menuconfig profile";
  screen.header = header;
  screen.key_legend = key_legend;
  screen.items = items;
  screen.item_count = state->view_count;
  screen.selected_index = state->selected_view_index;
  screen.status = status_line;
  if (confit_tui_curses_render(&screen) != 0) {
    free(items);
    return CONFIT_ERR_INTERNAL;
  }
  free(items);
  return CONFIT_OK;
}

static int confit_tui_profile_move_selection(ConfitTuiState *state,
                                             ConfitTuiKey key) {
  size_t step;

  switch (key) {
  case CONFIT_TUI_KEY_DOWN:
    if (state->view_count == 0U) {
      state->selected_view_index = 0U;
      return 1;
    }
    if (state->selected_view_index + 1U < state->view_count) {
      state->selected_view_index += 1U;
    }
    return 1;
  case CONFIT_TUI_KEY_UP:
    if (state->view_count == 0U) {
      state->selected_view_index = 0U;
      return 1;
    }
    if (state->selected_view_index > 0U) {
      state->selected_view_index -= 1U;
    }
    return 1;
  case CONFIT_TUI_KEY_PAGE_DOWN:
    if (state->view_count == 0U) {
      state->selected_view_index = 0U;
      return 1;
    }
    step = confit_tui_curses_page_step();
    if (state->selected_view_index + step < state->view_count) {
      state->selected_view_index += step;
    } else {
      state->selected_view_index = state->view_count - 1U;
    }
    (void)snprintf(state->status, sizeof(state->status), "moved PageDown");
    state->status[sizeof(state->status) - 1U] = '\0';
    return 1;
  case CONFIT_TUI_KEY_PAGE_UP:
    if (state->view_count == 0U) {
      state->selected_view_index = 0U;
      return 1;
    }
    step = confit_tui_curses_page_step();
    if (state->selected_view_index > step) {
      state->selected_view_index -= step;
    } else {
      state->selected_view_index = 0U;
    }
    (void)snprintf(state->status, sizeof(state->status), "moved PageUp");
    state->status[sizeof(state->status) - 1U] = '\0';
    return 1;
  case CONFIT_TUI_KEY_HOME:
    state->selected_view_index = 0U;
    (void)snprintf(state->status, sizeof(state->status), "moved Home");
    state->status[sizeof(state->status) - 1U] = '\0';
    return 1;
  case CONFIT_TUI_KEY_END:
    if (state->view_count == 0U) {
      state->selected_view_index = 0U;
      return 1;
    }
    state->selected_view_index = state->view_count - 1U;
    (void)snprintf(state->status, sizeof(state->status), "moved End");
    state->status[sizeof(state->status) - 1U] = '\0';
    return 1;
  default:
    break;
  }
  return 0;
}

static ConfitStatus
confit_tui_confirm_dirty_quit(ConfitTuiState *state, int *out_quit,
                              ConfitDiagnostic *diagnostic) {
  static const char *items[] = {"Save profile", "Discard changes", "Cancel"};
  char header[512];
  size_t selected_index;
  int select_status;
  ConfitStatus status;

  *out_quit = 0;
  if (!state->dirty) {
    *out_quit = 1;
    return CONFIT_OK;
  }

  (void)snprintf(header, sizeof(header),
                 "profile=%s\nproject=%s\nUnsaved profile changes are "
                 "pending.",
                 confit_tui_text_or_dash(state->options->profile_name),
                 confit_tui_text_or_dash(state->project->name));
  header[sizeof(header) - 1U] = '\0';
  selected_index = 0U;
  select_status =
      confit_tui_curses_select_dialog("Unsaved Profile Changes", header, items,
                                      3U, selected_index, &selected_index);
  if (select_status != 0 || selected_index == 2U) {
    (void)snprintf(state->status, sizeof(state->status), "quit cancelled");
    state->status[sizeof(state->status) - 1U] = '\0';
    return CONFIT_OK;
  }
  if (selected_index == 1U) {
    (void)snprintf(state->status, sizeof(state->status),
                   "discarded unsaved profile changes");
    state->status[sizeof(state->status) - 1U] = '\0';
    *out_quit = 1;
    return CONFIT_OK;
  }

  status = confit_tui_save_profile(state, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  *out_quit = state->dirty ? 0 : 1;
  return CONFIT_OK;
}

static ConfitStatus confit_tui_handle_key(ConfitTuiState *state,
                                          ConfitTuiKey key,
                                          const char *target_name,
                                          ConfitDiagnostic *diagnostic) {
  const ConfitOption *option;
  ConfitTuiRow *row;

  if (confit_tui_profile_move_selection(state, key)) {
    confit_tui_refresh_search_position(state);
    return CONFIT_OK;
  }
  if (key == CONFIT_TUI_KEY_KEYMAP_HELP || key == CONFIT_TUI_KEY_HELP) {
    return confit_tui_show_detail(state, target_name, diagnostic);
  }
  if (key == CONFIT_TUI_KEY_SEARCH) {
    return confit_tui_set_search(state, diagnostic);
  }
  if (key == CONFIT_TUI_KEY_NEW) {
    return confit_tui_search_jump(state, 1, "next result", diagnostic);
  }
  if (key == CONFIT_TUI_KEY_SEARCH_PREVIOUS) {
    return confit_tui_search_jump(state, -1, "previous result", diagnostic);
  }
  if (key == CONFIT_TUI_KEY_CATEGORY) {
    return confit_tui_set_filter(state->category, sizeof(state->category),
                                 "category: ", state, diagnostic);
  }
  if (key == CONFIT_TUI_KEY_TAG) {
    return confit_tui_set_filter(state->tag, sizeof(state->tag), "tag: ", state,
                                 diagnostic);
  }
  if (key == CONFIT_TUI_KEY_CLEAR_FILTER) {
    confit_tui_clear_filters(state);
    return confit_tui_rebuild_view(state, diagnostic);
  }
  if (key == CONFIT_TUI_KEY_CANCEL) {
    if (state->search[0] != '\0' || state->category[0] != '\0' ||
        state->tag[0] != '\0') {
      confit_tui_clear_filters(state);
      return confit_tui_rebuild_view(state, diagnostic);
    }
    (void)snprintf(state->status, sizeof(state->status), "cancelled");
    state->status[sizeof(state->status) - 1U] = '\0';
    return CONFIT_OK;
  }
  if (key == CONFIT_TUI_KEY_SAVE) {
    return confit_tui_save_profile(state, diagnostic);
  }
  if (key == CONFIT_TUI_KEY_EDIT || key == CONFIT_TUI_KEY_ENTER) {
    row = confit_tui_selected_row(state);
    if (row != 0 && row->kind == CONFIT_TUI_ROW_CATEGORY &&
        row->category_index < state->category_count) {
      const char *category_name = state->categories[row->category_index].name;

      state->categories[row->category_index].collapsed =
          !state->categories[row->category_index].collapsed;
      (void)snprintf(state->status, sizeof(state->status), "%s menu %s",
                     state->categories[row->category_index].collapsed
                         ? "collapsed"
                         : "expanded",
                     confit_tui_text_or_dash(category_name));
      state->status[sizeof(state->status) - 1U] = '\0';
      return confit_tui_refresh_rows(state, diagnostic);
    }
    option = confit_tui_selected_option(state);
    if (option == 0) {
      (void)snprintf(state->status, sizeof(state->status),
                     "select an option or category");
      state->status[sizeof(state->status) - 1U] = '\0';
      return CONFIT_OK;
    }
    return confit_tui_prompt_edit(state, option, diagnostic);
  }
  return CONFIT_OK;
}

static ConfitStatus confit_tui_render_loop(ConfitTuiState *state) {
  const char *target_name;
  ConfitTuiKey key;
  ConfitDiagnostic diagnostic;
  ConfitStatus status;

  if (state->status[0] == '\0') {
    (void)snprintf(state->status, sizeof(state->status), "ready");
    state->status[sizeof(state->status) - 1U] = '\0';
  }

  do {
    target_name = confit_tui_effective_target_name(state->project,
                                                   state->options->profile_name,
                                                   state->options->target_name);
    status = confit_tui_render_screen(state, target_name);
    if (status != CONFIT_OK) {
      return status;
    }
    key = confit_tui_curses_read_key();
    if (key == CONFIT_TUI_KEY_QUIT) {
      int should_quit;

      confit_diagnostic_init(&diagnostic);
      should_quit = 0;
      status = confit_tui_confirm_dirty_quit(state, &should_quit, &diagnostic);
      if (status != CONFIT_OK) {
        confit_tui_set_status_from_diagnostic(state, "error", status,
                                              &diagnostic);
        continue;
      }
      if (should_quit) {
        break;
      }
      continue;
    }
    confit_diagnostic_init(&diagnostic);
    status = confit_tui_handle_key(state, key, target_name, &diagnostic);
    if (status != CONFIT_OK) {
      confit_tui_set_status_from_diagnostic(state, "error", status,
                                            &diagnostic);
    }
  } while (1);

  return CONFIT_OK;
}

static void confit_tui_state_clear(ConfitTuiState *state) {
  if (state == 0) {
    return;
  }
  confit_tui_edits_clear(state);
  confit_tui_categories_clear(state);
  free(state->rows);
  free(state->view_indices);
  confit_resolved_config_free(state->config);
  confit_graph_free(state->graph);
  confit_project_free(state->project);
}

ConfitStatus confit_tui_run_profile_editor(const ConfitTuiOptions *options,
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
  confit_tui_curses_stop();
  return status;
}
