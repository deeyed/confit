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
  CONFIT_TUI_ROW_MENU = 1,
  CONFIT_TUI_ROW_OPTION = 2,
} ConfitTuiRowKind;

typedef struct ConfitTuiMenuNode {
  char *name;
  char *path;
  size_t parent_index;
  size_t depth;
  size_t child_count;
  size_t option_count;
  size_t visible_count;
} ConfitTuiMenuNode;

typedef struct ConfitTuiRow {
  ConfitTuiListItem item;
  const ConfitOption *option;
  ConfitTuiRowKind kind;
  size_t menu_index;
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
  ConfitTuiMenuNode *menus;
  size_t menu_count;
  size_t current_menu_index;
  ConfitTuiRow *rows;
  size_t row_count;
  size_t *view_indices;
  size_t view_count;
  size_t selected_view_index;
  char search[128];
  size_t search_count;
  size_t search_position;
  char text_filter[128];
  char category[64];
  char tag[64];
  char status[256];
  int flat_view;
  int verbose_inspector;
  int quit_requested;
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

static void confit_tui_set_input_cancelled(ConfitTuiState *state,
                                           ConfitTuiInputMode mode) {
  if (state == 0) {
    return;
  }
  (void)snprintf(state->status, sizeof(state->status), "%s cancelled",
                 confit_tui_input_mode_name(mode));
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

#define CONFIT_TUI_INDEX_NONE ((size_t)-1)

static const char *confit_tui_option_category_name(const ConfitOption *option) {
  if (option != 0 && option->category != 0 && option->category[0] != '\0') {
    return option->category;
  }
  return "(uncategorized)";
}

static const char *confit_tui_option_category_path(const ConfitOption *option) {
  if (option != 0 && option->category != 0 && option->category[0] != '\0') {
    return option->category;
  }
  return "(uncategorized)";
}

static void confit_tui_menu_nodes_free(ConfitTuiMenuNode *menus,
                                       size_t menu_count) {
  size_t index;

  for (index = 0U; index < menu_count; ++index) {
    free(menus[index].name);
    free(menus[index].path);
  }
  free(menus);
}

static void confit_tui_menus_clear(ConfitTuiState *state) {
  if (state == 0) {
    return;
  }
  confit_tui_menu_nodes_free(state->menus, state->menu_count);
  state->menus = 0;
  state->menu_count = 0U;
  state->current_menu_index = 0U;
}

static size_t confit_tui_find_menu_index(const ConfitTuiMenuNode *menus,
                                         size_t menu_count, const char *path) {
  size_t index;

  for (index = 0U; index < menu_count; ++index) {
    if (menus[index].path != 0 && strcmp(menus[index].path, path) == 0) {
      return index;
    }
  }
  return CONFIT_TUI_INDEX_NONE;
}

static ConfitStatus confit_tui_add_menu_node(
    ConfitTuiState *state, const char *name, const char *path,
    size_t parent_index, size_t depth, size_t *out_index,
    ConfitDiagnostic *diagnostic) {
  ConfitTuiMenuNode *new_menus;
  ConfitTuiMenuNode *menu;

  new_menus = (ConfitTuiMenuNode *)realloc(
      state->menus, (state->menu_count + 1U) * sizeof(state->menus[0]));
  if (new_menus == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, path, 0, 0,
                          "failed to allocate tui menu tree");
    return CONFIT_ERR_INTERNAL;
  }
  state->menus = new_menus;
  menu = &state->menus[state->menu_count];
  memset(menu, 0, sizeof(*menu));
  menu->name = confit_tui_copy_string(name);
  menu->path = confit_tui_copy_string(path);
  if (menu->name == 0 || menu->path == 0) {
    free(menu->name);
    free(menu->path);
    memset(menu, 0, sizeof(*menu));
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, path, 0, 0,
                          "failed to copy tui menu path");
    return CONFIT_ERR_INTERNAL;
  }
  menu->parent_index = parent_index;
  menu->depth = depth;
  *out_index = state->menu_count;
  state->menu_count += 1U;
  return CONFIT_OK;
}

static ConfitStatus confit_tui_ensure_menu_path(
    ConfitTuiState *state, const char *path, size_t *out_menu_index,
    ConfitDiagnostic *diagnostic) {
  ConfitCategoryPathInfo info;
  char prefix[CONFIT_CATEGORY_PATH_MAX_LENGTH + 1U];
  size_t parent_index;
  size_t prefix_size;
  size_t index;
  ConfitStatus status;

  if (out_menu_index == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  status = confit_category_path_analyze(path, &info);
  if (status != CONFIT_OK) {
    confit_diagnostic_set(diagnostic, status, path, 0, 0,
                          "invalid tui category path");
    return status;
  }

  parent_index = 0U;
  prefix_size = 0U;
  prefix[0] = '\0';
  for (index = 0U; index < info.depth; ++index) {
    const char *segment_begin;
    char segment[CONFIT_CATEGORY_PATH_MAX_LENGTH + 1U];
    size_t segment_size;
    size_t menu_index;

    status = confit_category_path_segment_at(path, index, &segment_begin,
                                             &segment_size);
    if (status != CONFIT_OK ||
        segment_size >= sizeof(segment) ||
        prefix_size + segment_size + (index > 0U ? 1U : 0U) >=
            sizeof(prefix)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, path, 0, 0,
                            "invalid tui category segment");
      return CONFIT_ERR_SCHEMA;
    }
    memcpy(segment, segment_begin, segment_size);
    segment[segment_size] = '\0';
    if (index > 0U) {
      prefix[prefix_size] = '/';
      prefix_size += 1U;
    }
    memcpy(prefix + prefix_size, segment_begin, segment_size);
    prefix_size += segment_size;
    prefix[prefix_size] = '\0';

    menu_index =
        confit_tui_find_menu_index(state->menus, state->menu_count, prefix);
    if (menu_index == CONFIT_TUI_INDEX_NONE) {
      status = confit_tui_add_menu_node(state, segment, prefix, parent_index,
                                        index + 1U, &menu_index, diagnostic);
      if (status != CONFIT_OK) {
        return status;
      }
    }
    parent_index = menu_index;
  }
  *out_menu_index = parent_index;
  return CONFIT_OK;
}

static ConfitStatus confit_tui_rebuild_menus(ConfitTuiState *state,
                                             ConfitDiagnostic *diagnostic) {
  ConfitTuiMenuNode *old_menus;
  size_t old_menu_count;
  size_t old_current_menu_index;
  char old_path[CONFIT_CATEGORY_PATH_MAX_LENGTH + 1U];
  size_t root_index;
  size_t index;
  ConfitStatus status;

  old_menus = state->menus;
  old_menu_count = state->menu_count;
  old_current_menu_index = state->current_menu_index;
  old_path[0] = '\0';
  if (old_current_menu_index < old_menu_count &&
      old_menus[old_current_menu_index].path != 0) {
    (void)snprintf(old_path, sizeof(old_path), "%s",
                   old_menus[old_current_menu_index].path);
    old_path[sizeof(old_path) - 1U] = '\0';
  }

  state->menus = 0;
  state->menu_count = 0U;
  state->current_menu_index = 0U;
  status = confit_tui_add_menu_node(state, "Main Menu", "", CONFIT_TUI_INDEX_NONE,
                                    0U, &root_index, diagnostic);
  (void)root_index;
  if (status != CONFIT_OK) {
    free(state->menus);
    state->menus = old_menus;
    state->menu_count = old_menu_count;
    state->current_menu_index = old_current_menu_index;
    return status;
  }

  for (index = 0U; index < state->project->option_count; ++index) {
    size_t menu_index;
    const char *path =
        confit_tui_option_category_path(&state->project->options[index]);

    status = confit_tui_ensure_menu_path(state, path, &menu_index, diagnostic);
    if (status != CONFIT_OK) {
      confit_tui_menu_nodes_free(state->menus, state->menu_count);
      state->menus = old_menus;
      state->menu_count = old_menu_count;
      state->current_menu_index = old_current_menu_index;
      return status;
    }
    state->menus[menu_index].option_count += 1U;
  }

  for (index = 1U; index < state->menu_count; ++index) {
    if (state->menus[index].parent_index < state->menu_count) {
      state->menus[state->menus[index].parent_index].child_count += 1U;
    }
  }

  confit_tui_menu_nodes_free(old_menus, old_menu_count);
  if (old_path[0] != '\0') {
    size_t restored =
        confit_tui_find_menu_index(state->menus, state->menu_count, old_path);
    state->current_menu_index =
        restored == CONFIT_TUI_INDEX_NONE ? 0U : restored;
  }
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
  if (state->text_filter[0] != '\0' &&
      !confit_tui_option_matches_search(option, state->text_filter)) {
    return 0;
  }
  if (state->category[0] != '\0' &&
      strcmp(confit_tui_option_category_name(option), state->category) != 0) {
    return 0;
  }
  if (!confit_tui_option_has_tag(option, state->tag)) {
    return 0;
  }
  return 1;
}

static int confit_tui_category_path_in_menu(const char *category_path,
                                            const char *menu_path) {
  size_t menu_size;

  if (menu_path == 0 || menu_path[0] == '\0') {
    return 1;
  }
  if (category_path == 0) {
    return 0;
  }
  menu_size = strlen(menu_path);
  return strcmp(category_path, menu_path) == 0 ||
         (strncmp(category_path, menu_path, menu_size) == 0 &&
          category_path[menu_size] == '/');
}

static int confit_tui_menu_has_visible_descendant(const ConfitTuiState *state,
                                                  size_t menu_index) {
  const char *menu_path;
  size_t index;

  if (menu_index >= state->menu_count) {
    return 0;
  }
  menu_path = state->menus[menu_index].path;
  for (index = 0U; index < state->project->option_count; ++index) {
    const ConfitOption *option = &state->project->options[index];

    if (confit_tui_category_path_in_menu(confit_tui_option_category_path(option),
                                         menu_path) &&
        confit_tui_option_visible(state, option)) {
      return 1;
    }
  }
  return 0;
}

static ConfitStatus confit_tui_rebuild_view(ConfitTuiState *state,
                                            ConfitDiagnostic *diagnostic) {
  size_t index;

  free(state->view_indices);
  state->view_indices = 0;
  state->view_count = 0U;
  for (index = 0U; index < state->menu_count; ++index) {
    state->menus[index].visible_count = 0U;
  }
  for (index = 0U; index < state->row_count; ++index) {
    if (state->rows[index].kind == CONFIT_TUI_ROW_OPTION &&
        state->rows[index].menu_index < state->menu_count &&
        confit_tui_option_visible(state, state->rows[index].option)) {
      state->menus[state->rows[index].menu_index].visible_count += 1U;
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

    if (state->flat_view) {
      if (row->kind == CONFIT_TUI_ROW_OPTION &&
          confit_tui_option_visible(state, row->option)) {
        state->view_indices[state->view_count] = index;
        state->view_count += 1U;
      }
      continue;
    }

    if (row->kind == CONFIT_TUI_ROW_MENU) {
      if (row->menu_index < state->menu_count &&
          state->menus[row->menu_index].parent_index ==
              state->current_menu_index &&
          confit_tui_menu_has_visible_descendant(state, row->menu_index)) {
        state->view_indices[state->view_count] = index;
        state->view_count += 1U;
      }
      continue;
    }

    if (row->kind == CONFIT_TUI_ROW_OPTION &&
        row->menu_index == state->current_menu_index &&
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
  if (option != 0) {
    for (index = 0U; index < option->tag_count; ++index) {
      confit_tui_append_unique_tag(out, out_size, option->tags[index]);
    }
  }
  if (out[0] == '\0') {
    (void)snprintf(out, out_size, "-");
  }
  out[out_size - 1U] = '\0';
}

static void confit_tui_format_menu_detail(const ConfitTuiState *state,
                                          size_t menu_index, char *out,
                                          size_t out_size) {
  char tags[96];
  size_t requires_count;
  size_t conflicts_count;
  size_t recommends_count;
  size_t forces_count;
  size_t visible_count;
  size_t descendant_count;
  size_t index;

  if (out == 0 || out_size == 0U || menu_index >= state->menu_count) {
    return;
  }

  tags[0] = '\0';
  requires_count = 0U;
  conflicts_count = 0U;
  recommends_count = 0U;
  forces_count = 0U;
  visible_count = 0U;
  descendant_count = 0U;
  for (index = 0U; index < state->project->option_count; ++index) {
    const ConfitOption *option = &state->project->options[index];
    size_t tag_index;

    if (!confit_tui_category_path_in_menu(confit_tui_option_category_path(option),
                                          state->menus[menu_index].path)) {
      continue;
    }
    descendant_count += 1U;
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
  (void)snprintf(
      out, out_size,
      "path: %s | menus: %lu | options: %lu direct/%lu total | deps r%lu c%lu "
      "v%lu f%lu rec%lu | tags: %s",
      state->menus[menu_index].path[0] != '\0' ? state->menus[menu_index].path
                                               : "Main Menu",
      (unsigned long)state->menus[menu_index].child_count,
      (unsigned long)state->menus[menu_index].option_count,
      (unsigned long)descendant_count, (unsigned long)requires_count,
      (unsigned long)conflicts_count, (unsigned long)visible_count,
      (unsigned long)forces_count, (unsigned long)recommends_count, tags);
  out[out_size - 1U] = '\0';
}

static ConfitStatus confit_tui_refresh_rows(ConfitTuiState *state,
                                            ConfitDiagnostic *diagnostic) {
  size_t menu_index;
  size_t option_index;
  size_t row_capacity;
  ConfitStatus status;

  free(state->rows);
  state->rows = 0;
  state->row_count = 0U;
  status = confit_tui_rebuild_menus(state, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }

  row_capacity = state->project->option_count +
                 (state->menu_count > 0U ? state->menu_count - 1U : 0U);
  if (row_capacity > 0U) {
    state->rows = (ConfitTuiRow *)calloc(row_capacity, sizeof(state->rows[0]));
    if (state->rows == 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0, 0,
                            "failed to allocate tui rows");
      return CONFIT_ERR_INTERNAL;
    }
  }

  for (menu_index = 1U; menu_index < state->menu_count; ++menu_index) {
    ConfitTuiRow *heading = &state->rows[state->row_count];

    heading->kind = CONFIT_TUI_ROW_MENU;
    heading->menu_index = menu_index;
    (void)snprintf(heading->label, sizeof(heading->label), "%s",
                   state->menus[menu_index].name);
    heading->label[sizeof(heading->label) - 1U] = '\0';
    confit_tui_format_menu_detail(state, menu_index, heading->detail,
                                  sizeof(heading->detail));
    heading->value[0] = '\0';
    heading->item.label = heading->label;
    heading->item.detail = heading->detail;
    heading->item.value = heading->value;
    heading->item.depth = 0U;
    heading->item.is_heading = 1;
    heading->item.expanded = 0;
    state->row_count += 1U;
  }

  for (option_index = 0U; option_index < state->project->option_count;
       ++option_index) {
    const ConfitOption *option = &state->project->options[option_index];
    const ConfitResolvedValue *resolved;
    const ConfitNamedValue *edit;
    const ConfitValue *resolved_value;
    char deps[96];
    ConfitTuiRow *row;
    size_t option_menu_index;

    option_menu_index = confit_tui_find_menu_index(
        state->menus, state->menu_count, confit_tui_option_category_path(option));
    if (option_menu_index == CONFIT_TUI_INDEX_NONE) {
      continue;
    }

    resolved = confit_resolved_config_find(state->config, option->id);
    resolved_value = resolved != 0 ? &resolved->value : &option->default_value;
    edit = confit_tui_find_const_edit(state, option->id);
    confit_tui_format_dependency_summary(option, deps, sizeof(deps));

    row = &state->rows[state->row_count];
    row->kind = CONFIT_TUI_ROW_OPTION;
    row->menu_index = option_menu_index;
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
    confit_tui_append_summary_text(row->detail, sizeof(row->detail), option->id);
    confit_tui_append_summary_text(row->detail, sizeof(row->detail), " | ");
    confit_tui_append_summary_text(row->detail, sizeof(row->detail), "type=");
    confit_tui_append_summary_text(row->detail, sizeof(row->detail),
                                   confit_option_type_name(option->type));
    confit_tui_append_summary_text(row->detail, sizeof(row->detail), " | ");
    confit_tui_append_summary_text(row->detail, sizeof(row->detail), deps);
    confit_tui_append_summary_text(row->detail, sizeof(row->detail), " | ");
    confit_tui_append_summary_text(row->detail, sizeof(row->detail), "state=");
    confit_tui_append_summary_text(
        row->detail, sizeof(row->detail),
        confit_tui_text_or_dash(row->dependency_state));

    confit_tui_format_value(option, resolved_value, row->value,
                            sizeof(row->value));
    row->item.label = row->label;
    row->item.detail = row->detail;
    row->item.value = row->value;
    row->item.depth = 0U;
    row->item.is_heading = 0;
    row->item.expanded = 0;
    row->item.is_disabled = row->is_disabled;
    state->row_count += 1U;
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

static void confit_tui_format_option_range(const ConfitOption *option,
                                           char *out, size_t out_size) {
  if (out == 0 || out_size == 0U) {
    return;
  }
  if (option == 0 || !option->has_range) {
    (void)snprintf(out, out_size, "-");
  } else if (option->type == CONFIT_OPTION_TYPE_INT &&
             option->range_min.kind == CONFIT_VALUE_INT &&
             option->range_max.kind == CONFIT_VALUE_INT) {
    (void)snprintf(out, out_size, "[%lld, %lld]",
                   (long long)option->range_min.as.int_value,
                   (long long)option->range_max.as.int_value);
  } else if (option->type == CONFIT_OPTION_TYPE_UINT &&
             option->range_min.kind == CONFIT_VALUE_UINT &&
             option->range_max.kind == CONFIT_VALUE_UINT) {
    (void)snprintf(out, out_size, "[%llu, %llu]",
                   (unsigned long long)option->range_min.as.uint_value,
                   (unsigned long long)option->range_max.as.uint_value);
  } else if (option->type == CONFIT_OPTION_TYPE_HEX &&
             option->range_min.kind == CONFIT_VALUE_UINT &&
             option->range_max.kind == CONFIT_VALUE_UINT) {
    (void)snprintf(out, out_size, "[0x%llX, 0x%llX]",
                   (unsigned long long)option->range_min.as.uint_value,
                   (unsigned long long)option->range_max.as.uint_value);
  } else if (option->type == CONFIT_OPTION_TYPE_FLOAT &&
             option->range_min.kind == CONFIT_VALUE_FLOAT &&
             option->range_max.kind == CONFIT_VALUE_FLOAT) {
    (void)snprintf(out, out_size, "[%.6g, %.6g]",
                   option->range_min.as.float_value,
                   option->range_max.as.float_value);
  } else {
    (void)snprintf(out, out_size, "-");
  }
  out[out_size - 1U] = '\0';
}

static void confit_tui_format_enum_candidates(const ConfitOption *option,
                                              char *out, size_t out_size) {
  size_t index;

  if (out == 0 || out_size == 0U) {
    return;
  }
  out[0] = '\0';
  if (option == 0 || option->enum_value_count == 0U) {
    (void)snprintf(out, out_size, "-");
    out[out_size - 1U] = '\0';
    return;
  }
  for (index = 0U; index < option->enum_value_count; ++index) {
    if (index > 0U) {
      confit_tui_append_summary_text(out, out_size, ", ");
    }
    confit_tui_append_summary_text(
        out, out_size, confit_tui_text_or_dash(option->enum_values[index]));
  }
  out[out_size - 1U] = '\0';
}

static void confit_tui_format_input_policy(const ConfitOption *option,
                                           char *out, size_t out_size) {
  if (out == 0 || out_size == 0U) {
    return;
  }
  switch (option != 0 ? option->type : CONFIT_OPTION_TYPE_STRING) {
  case CONFIT_OPTION_TYPE_INT:
    (void)snprintf(out, out_size, "integer only");
    break;
  case CONFIT_OPTION_TYPE_UINT:
    (void)snprintf(out, out_size, "unsigned integer only");
    break;
  case CONFIT_OPTION_TYPE_HEX:
    (void)snprintf(out, out_size, "unsigned integer or 0x hex value");
    break;
  case CONFIT_OPTION_TYPE_FLOAT:
    (void)snprintf(out, out_size, "finite floating point number");
    break;
  case CONFIT_OPTION_TYPE_STRING:
    (void)snprintf(out, out_size,
                   "non-empty text; control characters rejected");
    break;
  case CONFIT_OPTION_TYPE_PATH:
    (void)snprintf(out, out_size,
                   "relative path; absolute/control paths rejected");
    break;
  case CONFIT_OPTION_TYPE_ENUM:
    (void)snprintf(out, out_size, "choose one listed candidate");
    break;
  case CONFIT_OPTION_TYPE_BOOL:
  default:
    (void)snprintf(out, out_size, "bool toggles immediately");
    break;
  }
  out[out_size - 1U] = '\0';
}

static void confit_tui_format_input_status(const ConfitOption *option,
                                           char *out, size_t out_size) {
  char range[96];

  if (out == 0 || out_size == 0U) {
    return;
  }
  confit_tui_format_option_range(option, range, sizeof(range));
  switch (option != 0 ? option->type : CONFIT_OPTION_TYPE_STRING) {
  case CONFIT_OPTION_TYPE_INT:
    (void)snprintf(out, out_size, "required: integer");
    break;
  case CONFIT_OPTION_TYPE_UINT:
    (void)snprintf(out, out_size, "required: unsigned integer");
    break;
  case CONFIT_OPTION_TYPE_HEX:
    (void)snprintf(out, out_size, "required: unsigned integer or 0x hex");
    break;
  case CONFIT_OPTION_TYPE_FLOAT:
    (void)snprintf(out, out_size, "required: finite float");
    break;
  case CONFIT_OPTION_TYPE_STRING:
    (void)snprintf(out, out_size, "required: non-empty text");
    break;
  case CONFIT_OPTION_TYPE_PATH:
    (void)snprintf(out, out_size, "required: relative path");
    break;
  default:
    (void)snprintf(out, out_size, "required: valid value");
    break;
  }
  out[out_size - 1U] = '\0';
  if (range[0] != '-' || range[1] != '\0') {
    confit_tui_append_summary_text(out, out_size, " in range ");
    confit_tui_append_summary_text(out, out_size, range);
  }
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
    {
      char candidates[128];

      confit_tui_format_enum_candidates(option, candidates,
                                        sizeof(candidates));
      confit_tui_validation_message(message, message_size,
                                    "invalid enum: not a candidate");
      if (candidates[0] != '-' || candidates[1] != '\0') {
        confit_tui_append_summary_text(message, message_size, " (");
        confit_tui_append_summary_text(message, message_size, candidates);
        confit_tui_append_summary_text(message, message_size, ")");
      }
    }
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

static int confit_tui_value_edit_was_applied(const ConfitTuiState *state,
                                             const ConfitOption *option) {
  char expected[256];

  if (state == 0 || option == 0 || option->id == 0) {
    return 0;
  }
  (void)snprintf(expected, sizeof(expected), "edited %s", option->id);
  expected[sizeof(expected) - 1U] = '\0';
  return strcmp(state->status, expected) == 0;
}

static ConfitStatus confit_tui_toggle_bool(ConfitTuiState *state,
                                           const ConfitOption *option,
                                           ConfitDiagnostic *diagnostic) {
  const ConfitValue *current;
  ConfitValue value;
  ConfitStatus status;
  char value_text[64];

  status =
      confit_tui_resolved_value_for_option(state, option, &current, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  confit_value_init(&value);
  confit_value_set_bool(
      &value, current->kind == CONFIT_VALUE_BOOL ? !current->as.bool_value : 1);
  status = confit_tui_apply_value_edit(state, option, &value, diagnostic);
  if (status == CONFIT_OK && confit_tui_value_edit_was_applied(state, option)) {
    confit_tui_format_value(option, &value, value_text, sizeof(value_text));
    (void)snprintf(state->status, sizeof(state->status), "toggled %s = %s",
                   option->id, value_text);
    state->status[sizeof(state->status) - 1U] = '\0';
  }
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
  char header[768];
  char message[160];
  char current_value[128];
  char default_value[128];
  char candidates[192];
  char policy[128];
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
  confit_tui_format_value(option, current, current_value,
                          sizeof(current_value));
  confit_tui_format_value(option, &option->default_value, default_value,
                          sizeof(default_value));
  confit_tui_format_enum_candidates(option, candidates, sizeof(candidates));
  confit_tui_format_input_policy(option, policy, sizeof(policy));
  (void)snprintf(header, sizeof(header),
                 "option: %s\nprompt: %s\ntype: enum\ncurrent: %s\ndefault: "
                 "%s\nchoices: %s\npolicy: %s\nkeys: j/k move, Enter/Space "
                 "select, Esc cancel",
                 confit_tui_text_or_dash(option->id),
                 confit_tui_text_or_dash(option->prompt), current_value,
                 default_value, candidates, policy);
  header[sizeof(header) - 1U] = '\0';
  select_status = confit_tui_curses_select_dialog(
      "Confit Choice", header, choices, option->enum_value_count,
      selected_index, &selected_index);
  free(choices);
  if (select_status != CONFIT_TUI_INPUT_ACCEPTED) {
    if (confit_tui_input_cancelled(select_status)) {
      confit_tui_set_input_cancelled(state, CONFIT_TUI_INPUT_DIALOG);
    }
    return CONFIT_OK;
  }

  confit_value_init(&value);
  status = confit_tui_parse_value(option, option->enum_values[selected_index],
                                  &value, message, sizeof(message));
  if (status == CONFIT_OK) {
    status = confit_tui_apply_value_edit(state, option, &value, diagnostic);
    if (status == CONFIT_OK &&
        confit_tui_value_edit_was_applied(state, option)) {
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
  const ConfitValue *current;
  char input[256];
  char header[768];
  char prompt[64];
  char message[160];
  char current_value[128];
  char default_value[128];
  char range[96];
  char policy[160];
  char initial_status[160];
  ConfitValue value;
  ConfitStatus status;
  int input_status;

  status =
      confit_tui_resolved_value_for_option(state, option, &current, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  confit_tui_format_value(option, current, current_value,
                          sizeof(current_value));
  confit_tui_format_value(option, &option->default_value, default_value,
                          sizeof(default_value));
  confit_tui_format_option_range(option, range, sizeof(range));
  confit_tui_format_input_policy(option, policy, sizeof(policy));
  confit_tui_format_input_status(option, initial_status,
                                 sizeof(initial_status));
  (void)snprintf(
      header, sizeof(header),
      "option: %s\nprompt: %s\ntype: %s\ncurrent: %s\ndefault: %s\npolicy: "
      "%s\nrange: %s\nkeys: Enter validates, Ctrl-U clears, Esc cancels",
      confit_tui_text_or_dash(option->id),
      confit_tui_text_or_dash(option->prompt),
      confit_option_type_name(option->type),
      current_value, default_value, policy, range);
  header[sizeof(header) - 1U] = '\0';
  (void)snprintf(prompt, sizeof(prompt),
                 "%s value: ", confit_option_type_name(option->type));
  prompt[sizeof(prompt) - 1U] = '\0';
  validator.state = state;
  validator.option = option;
  input_status = confit_tui_curses_read_value_dialog(
      "Confit Value", header, prompt, initial_status,
      confit_tui_value_dialog_validator, &validator, input, sizeof(input));
  if (input_status != CONFIT_TUI_INPUT_ACCEPTED) {
    if (confit_tui_input_cancelled(input_status)) {
      confit_tui_set_input_cancelled(state, CONFIT_TUI_INPUT_DIALOG);
    }
    return CONFIT_OK;
  }

  confit_value_init(&value);
  message[0] = '\0';
  status =
      confit_tui_parse_value(option, input, &value, message, sizeof(message));
  if (status == CONFIT_OK) {
    status = confit_tui_apply_value_edit(state, option, &value, diagnostic);
    if (status == CONFIT_OK &&
        confit_tui_value_edit_was_applied(state, option)) {
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
         row->menu_index < state->menu_count &&
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
  if (!state->flat_view &&
      state->rows[row_index].kind == CONFIT_TUI_ROW_OPTION &&
      state->rows[row_index].menu_index < state->menu_count &&
      state->rows[row_index].menu_index != state->current_menu_index) {
    state->current_menu_index = state->rows[row_index].menu_index;
    status = confit_tui_rebuild_view(state, diagnostic);
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
  (void)snprintf(
      state->status, sizeof(state->status), "%s %lu/%lu: %s <%s> category=%s",
      status_prefix, (unsigned long)state->search_position,
      (unsigned long)state->search_count,
      confit_tui_text_or_dash(state->rows[best_match].option->prompt),
      confit_tui_text_or_dash(state->rows[best_match].option->id),
      confit_tui_option_category_name(state->rows[best_match].option));
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
  if (select_status != CONFIT_TUI_INPUT_ACCEPTED || selected_index != 0U) {
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

  input_status = confit_tui_curses_read_mode_line(CONFIT_TUI_INPUT_FILTER,
                                                  prompt, input, sizeof(input));
  if (input_status != CONFIT_TUI_INPUT_ACCEPTED) {
    if (confit_tui_input_cancelled(input_status)) {
      confit_tui_set_input_cancelled(state, CONFIT_TUI_INPUT_FILTER);
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

  input_status = confit_tui_curses_read_mode_line(
      CONFIT_TUI_INPUT_SEARCH, "search: ", input, sizeof(input));
  if (input_status != CONFIT_TUI_INPUT_ACCEPTED) {
    if (confit_tui_input_cancelled(input_status)) {
      confit_tui_set_input_cancelled(state, CONFIT_TUI_INPUT_SEARCH);
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
  state->text_filter[0] = '\0';
  state->category[0] = '\0';
  state->tag[0] = '\0';
  state->selected_view_index = 0U;
  (void)snprintf(state->status, sizeof(state->status), "cleared filters");
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

static ConfitStatus
confit_tui_detail_append_section(ConfitTuiTextBuilder *builder,
                                 const char *title) {
  ConfitStatus status;

  if (builder->size > 0U) {
    status = confit_tui_text_append(builder, "\n");
    if (status != CONFIT_OK) {
      return status;
    }
  }
  status = confit_tui_text_append(builder, title);
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

  status = confit_tui_detail_append_section(builder, "Declared Dependencies");
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
  status = confit_tui_detail_append_section(builder, "Dependency State");
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
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_section(builder, "Blocked Reason");
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(
        builder, "reason: ", disabled ? state_line : "-");
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
  status = confit_tui_detail_append_section(&builder, "Overview");
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(
      &builder, "prompt: ", confit_tui_text_or_dash(option->prompt));
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(&builder, "id: ", option->id);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(
        &builder, "type: ", confit_option_type_name(option->type));
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_section(&builder, "Value");
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
    status = confit_tui_detail_append_section(&builder, "Location");
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
    status = confit_tui_detail_append_section(&builder, "Help Text");
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

static void confit_tui_format_breadcrumb(const ConfitTuiState *state, char *out,
                                         size_t out_size) {
  size_t stack[16];
  size_t stack_count;
  size_t index;

  if (out == 0 || out_size == 0U) {
    return;
  }
  out[0] = '\0';
  if (state != 0 && state->flat_view) {
    (void)snprintf(out, out_size, "Flat List");
    out[out_size - 1U] = '\0';
    return;
  }
  if (state == 0 || state->menu_count == 0U ||
      state->current_menu_index >= state->menu_count) {
    (void)snprintf(out, out_size, "Main Menu");
    out[out_size - 1U] = '\0';
    return;
  }
  stack_count = 0U;
  index = state->current_menu_index;
  while (index < state->menu_count && stack_count < 16U) {
    stack[stack_count] = index;
    stack_count += 1U;
    if (state->menus[index].parent_index == CONFIT_TUI_INDEX_NONE) {
      break;
    }
    index = state->menus[index].parent_index;
  }
  while (stack_count > 0U) {
    stack_count -= 1U;
    if (out[0] != '\0') {
      confit_tui_append_summary_text(out, out_size, " > ");
    }
    confit_tui_append_summary_text(out, out_size,
                                   state->menus[stack[stack_count]].name);
  }
  if (out[0] == '\0') {
    (void)snprintf(out, out_size, "Main Menu");
  }
  out[out_size - 1U] = '\0';
}

static ConfitStatus
confit_tui_build_menu_detail(const ConfitTuiState *state, const ConfitTuiRow *row,
                             char **out_text, ConfitDiagnostic *diagnostic) {
  ConfitTuiTextBuilder builder;
  ConfitStatus status;
  char count_line[128];
  char breadcrumb[256];
  const char *name;

  *out_text = 0;
  if (row == 0 || row->menu_index >= state->menu_count) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0, 0,
                          "missing detail menu");
    return CONFIT_ERR_INTERNAL;
  }
  name = state->menus[row->menu_index].name;
  (void)snprintf(
      count_line, sizeof(count_line), "%lu visible direct, %lu direct total",
      (unsigned long)state->menus[row->menu_index].visible_count,
      (unsigned long)state->menus[row->menu_index].option_count);
  count_line[sizeof(count_line) - 1U] = '\0';
  confit_tui_format_breadcrumb(state, breadcrumb, sizeof(breadcrumb));

  confit_tui_text_builder_init(&builder);
  status = confit_tui_detail_append_section(&builder, "Menu");
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(&builder, "menu: ", name);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(&builder, "path: ",
                                           state->menus[row->menu_index].path);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(&builder, "current view: ",
                                           breadcrumb);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(&builder, "direct options: ",
                                           count_line);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_section(&builder, "Summary");
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_line(&builder, "summary: ", row->detail);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_detail_append_section(&builder, "Help Text");
  }
  if (status == CONFIT_OK) {
    status = confit_tui_text_append(&builder,
                                    "Enter opens this menu. Left or Esc returns "
                                    "to the parent menu. Root Esc follows the "
                                    "normal exit flow.\n");
  }
  if (status != CONFIT_OK) {
    free(builder.text);
    confit_diagnostic_set(diagnostic, status, name, 0, 0,
                          "failed to build tui menu detail");
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
  char header[512];
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
  if (row->kind == CONFIT_TUI_ROW_MENU) {
    status = confit_tui_build_menu_detail(state, row, &body, diagnostic);
  } else {
    status =
        confit_tui_build_option_detail(state, row->option, &body, diagnostic);
  }
  if (status != CONFIT_OK) {
    return status;
  }

  (void)snprintf(header, sizeof(header),
                 "mode=help project=%s profile=%s target=%s selected=%s",
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
            "keys: scroll jk/arrows Pg/Home/End | Esc/q close", status_line,
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
      state != 0 && (state->category[0] != '\0' || state->tag[0] != '\0' ||
                     state->text_filter[0] != '\0');
  const char *filter_suffix = has_filter ? " x clear" : "";
  const char *inspector_mode =
      state != 0 && state->verbose_inspector ? "compact" : "verbose";

  if (out == 0 || out_size == 0U) {
    return;
  }
  if (selected != 0 && selected->kind == CONFIT_TUI_ROW_MENU) {
    if (state != 0 && state->dirty) {
      (void)snprintf(out, out_size,
                     "keys: move | enter menu | Esc back/exit | s save | / "
                     "search | c/t filter%s | : cmd | ? help | v %s",
                     filter_suffix, inspector_mode);
    } else {
      (void)snprintf(out, out_size,
                     "keys: move | enter menu | Esc back/exit | / search | "
                     "c/t filter%s | : cmd | ? help | v %s",
                     filter_suffix, inspector_mode);
    }
  } else {
    if (state != 0 && state->dirty) {
      (void)snprintf(out, out_size,
                     "keys: move | enter/e edit | Esc exit | s save | / "
                     "search n/N | c/t filter%s | : cmd | ? help | v %s",
                     filter_suffix, inspector_mode);
    } else {
      (void)snprintf(out, out_size,
                     "keys: move | enter/e edit | Esc exit | / search n/N | "
                     "c/t filter%s | : cmd | ? help | v %s",
                     filter_suffix, inspector_mode);
    }
  }
  out[out_size - 1U] = '\0';
}

static const char *
confit_tui_profile_row_source(const ConfitTuiState *state,
                              const ConfitTuiRow *selected) {
  const ConfitResolvedValue *resolved;

  if (state == 0 || selected == 0 || selected->option == 0) {
    return "-";
  }
  if (confit_tui_find_const_edit(state, selected->option->id) != 0) {
    return "pending edit";
  }
  resolved = confit_resolved_config_find(state->config, selected->option->id);
  if (resolved != 0 && resolved->source != 0 && resolved->source[0] != '\0') {
    return resolved->source;
  }
  return "default";
}

static const char *confit_tui_profile_blocked_reason(
    const ConfitTuiRow *selected) {
  const char *state;

  if (selected == 0) {
    return "-";
  }
  state = confit_tui_text_or_dash(selected->dependency_state);
  if (strncmp(state, "blocked:", 8U) == 0 ||
      strncmp(state, "hidden:", 7U) == 0) {
    return state;
  }
  return "-";
}

static void confit_tui_profile_format_compact_inspector(
    const ConfitTuiRow *selected, char *out, size_t out_size) {
  if (out == 0 || out_size == 0U) {
    return;
  }
  if (selected == 0) {
    (void)snprintf(out, out_size, "no selection");
  } else if (selected->kind == CONFIT_TUI_ROW_MENU) {
    (void)snprintf(out, out_size, "%s menu %s",
                   confit_tui_text_or_dash(selected->item.label),
                   confit_tui_text_or_dash(selected->detail));
  } else {
    (void)snprintf(
        out, out_size, "%s <%s> %s %s%s",
        confit_tui_text_or_dash(selected->item.label),
        confit_tui_text_or_dash(selected->option != 0 ? selected->option->id
                                                      : 0),
        confit_tui_text_or_dash(selected->option != 0
                                    ? confit_option_type_name(
                                          selected->option->type)
                                    : 0),
        confit_tui_text_or_dash(selected->dependency_state),
        selected->detail[0] != '\0' && strstr(selected->detail, "dirty") != 0
            ? " dirty"
            : "");
  }
  out[out_size - 1U] = '\0';
}

static void confit_tui_profile_format_verbose_inspector(
    const ConfitTuiState *state, const ConfitTuiRow *selected, char *out,
    size_t out_size) {
  char deps[96];
  char tags[128];

  if (out == 0 || out_size == 0U) {
    return;
  }
  if (selected == 0) {
    (void)snprintf(out, out_size, "verbose: no selection");
  } else if (selected->kind == CONFIT_TUI_ROW_MENU) {
    (void)snprintf(out, out_size, "verbose: %s",
                   confit_tui_text_or_dash(selected->detail));
  } else {
    confit_tui_format_dependency_summary(selected->option, deps, sizeof(deps));
    confit_tui_format_tag_summary(selected->option, tags, sizeof(tags));
    (void)snprintf(
        out, out_size,
        "verbose: type:%s | source:%s | %s | tags:%s | id:%s | "
        "blocked_reason:%s | state:%s",
        confit_tui_text_or_dash(selected->option != 0
                                    ? confit_option_type_name(
                                          selected->option->type)
                                    : 0),
        confit_tui_profile_row_source(state, selected), deps, tags,
        confit_tui_text_or_dash(selected->option != 0 ? selected->option->id
                                                      : 0),
        confit_tui_profile_blocked_reason(selected),
        confit_tui_text_or_dash(selected->dependency_state));
  }
  out[out_size - 1U] = '\0';
}

static void confit_tui_profile_format_inspector(const ConfitTuiState *state,
                                                const ConfitTuiRow *selected,
                                                char *out, size_t out_size) {
  if (state != 0 && state->verbose_inspector) {
    confit_tui_profile_format_verbose_inspector(state, selected, out, out_size);
  } else {
    confit_tui_profile_format_compact_inspector(selected, out, out_size);
  }
}

static ConfitStatus confit_tui_render_screen(const ConfitTuiState *state,
                                             const char *target_name) {
  ConfitTuiListItem *items;
  ConfitTuiScreen screen;
  char breadcrumb[256];
  char header[320];
  char inspector[512];
  char key_legend[192];
  char status_line[384];
  char number[32];
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

  confit_tui_format_breadcrumb(state, breadcrumb, sizeof(breadcrumb));
  header[0] = '\0';
  confit_tui_append_summary_text(header, sizeof(header),
                                 "mode=profile project=");
  confit_tui_append_summary_text(
      header, sizeof(header), confit_tui_text_or_dash(state->project->name));
  confit_tui_append_summary_text(header, sizeof(header), " profile=");
  confit_tui_append_summary_text(
      header, sizeof(header),
      confit_tui_text_or_dash(state->options->profile_name));
  confit_tui_append_summary_text(header, sizeof(header), " target=");
  confit_tui_append_summary_text(header, sizeof(header),
                                 confit_tui_text_or_dash(target_name));
  confit_tui_append_summary_text(header, sizeof(header), " dirty=");
  confit_tui_append_summary_text(header, sizeof(header),
                                 state->dirty ? "yes" : "no");
  confit_tui_append_summary_text(header, sizeof(header), "\nbreadcrumb=");
  confit_tui_append_summary_text(header, sizeof(header), breadcrumb);
  confit_tui_append_summary_text(header, sizeof(header), " | row ");
  (void)snprintf(number, sizeof(number), "%lu",
                 state->view_count == 0U
                     ? 0UL
                     : (unsigned long)(state->selected_view_index + 1U));
  confit_tui_append_summary_text(header, sizeof(header), number);
  confit_tui_append_summary_text(header, sizeof(header), "/");
  (void)snprintf(number, sizeof(number), "%lu",
                 (unsigned long)state->view_count);
  confit_tui_append_summary_text(header, sizeof(header), number);
  confit_tui_append_summary_text(header, sizeof(header), " | search=");
  confit_tui_append_summary_text(header, sizeof(header),
                                 confit_tui_text_or_dash(state->search));
  confit_tui_append_summary_text(header, sizeof(header), " ");
  (void)snprintf(number, sizeof(number), "%lu",
                 (unsigned long)state->search_position);
  confit_tui_append_summary_text(header, sizeof(header), number);
  confit_tui_append_summary_text(header, sizeof(header), "/");
  (void)snprintf(number, sizeof(number), "%lu",
                 (unsigned long)state->search_count);
  confit_tui_append_summary_text(header, sizeof(header), number);
  confit_tui_append_summary_text(header, sizeof(header), " | filter=");
  confit_tui_append_summary_text(header, sizeof(header),
                                 confit_tui_text_or_dash(state->category));
  confit_tui_append_summary_text(header, sizeof(header), "/");
  confit_tui_append_summary_text(header, sizeof(header),
                                 confit_tui_text_or_dash(state->tag));
  confit_tui_append_summary_text(header, sizeof(header), " | text=");
  confit_tui_append_summary_text(header, sizeof(header),
                                 confit_tui_text_or_dash(state->text_filter));
  confit_tui_profile_format_inspector(state, selected, inspector,
                                      sizeof(inspector));
  confit_tui_profile_format_key_legend(state, selected, key_legend,
                                       sizeof(key_legend));
  (void)snprintf(status_line, sizeof(status_line), "%s",
                 state->status[0] != '\0' ? state->status : "ready");
  status_line[sizeof(status_line) - 1U] = '\0';

  screen.title = "Confit TUI - menuconfig profile";
  screen.header = header;
  screen.inspector = inspector;
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
  if (select_status != CONFIT_TUI_INPUT_ACCEPTED || selected_index == 2U) {
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

static ConfitStatus confit_tui_request_exit(ConfitTuiState *state,
                                            int *out_quit,
                                            ConfitDiagnostic *diagnostic) {
  return confit_tui_confirm_dirty_quit(state, out_quit, diagnostic);
}

static ConfitStatus confit_tui_enter_menu(ConfitTuiState *state,
                                          size_t menu_index,
                                          ConfitDiagnostic *diagnostic) {
  char breadcrumb[256];
  ConfitStatus status;

  if (menu_index >= state->menu_count) {
    (void)snprintf(state->status, sizeof(state->status), "missing menu");
    state->status[sizeof(state->status) - 1U] = '\0';
    return CONFIT_OK;
  }
  state->current_menu_index = menu_index;
  state->selected_view_index = 0U;
  status = confit_tui_rebuild_view(state, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  confit_tui_format_breadcrumb(state, breadcrumb, sizeof(breadcrumb));
  state->status[0] = '\0';
  confit_tui_append_summary_text(state->status, sizeof(state->status),
                                 "entered menu ");
  confit_tui_append_summary_text(state->status, sizeof(state->status),
                                 breadcrumb);
  return CONFIT_OK;
}

static ConfitStatus confit_tui_go_parent_menu(ConfitTuiState *state,
                                              ConfitDiagnostic *diagnostic) {
  size_t parent_index;
  char breadcrumb[256];
  ConfitStatus status;

  if (state->current_menu_index >= state->menu_count ||
      state->current_menu_index == 0U) {
    (void)snprintf(state->status, sizeof(state->status), "at Main Menu");
    state->status[sizeof(state->status) - 1U] = '\0';
    return CONFIT_OK;
  }
  parent_index = state->menus[state->current_menu_index].parent_index;
  state->current_menu_index =
      parent_index < state->menu_count ? parent_index : 0U;
  state->selected_view_index = 0U;
  status = confit_tui_rebuild_view(state, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  confit_tui_format_breadcrumb(state, breadcrumb, sizeof(breadcrumb));
  state->status[0] = '\0';
  confit_tui_append_summary_text(state->status, sizeof(state->status),
                                 "back to menu ");
  confit_tui_append_summary_text(state->status, sizeof(state->status),
                                 breadcrumb);
  return CONFIT_OK;
}

static char *confit_tui_command_trim(char *text) {
  char *end;

  if (text == 0) {
    return 0;
  }
  while (*text != '\0' && isspace((unsigned char)*text)) {
    text += 1;
  }
  end = text + strlen(text);
  while (end > text && isspace((unsigned char)end[-1])) {
    end -= 1;
  }
  *end = '\0';
  if (text[0] == ':') {
    text += 1;
    while (*text != '\0' && isspace((unsigned char)*text)) {
      text += 1;
    }
  }
  return text;
}

static int confit_tui_command_starts_with(const char *command,
                                          const char *name) {
  size_t index;

  if (command == 0 || name == 0) {
    return 0;
  }
  for (index = 0U; name[index] != '\0'; ++index) {
    if (tolower((unsigned char)command[index]) !=
        tolower((unsigned char)name[index])) {
      return 0;
    }
  }
  return command[index] == '\0' || isspace((unsigned char)command[index]);
}

static const char *confit_tui_command_arg(const char *command,
                                          const char *name) {
  const char *arg;

  if (!confit_tui_command_starts_with(command, name)) {
    return 0;
  }
  arg = command + strlen(name);
  while (*arg != '\0' && isspace((unsigned char)*arg)) {
    arg += 1;
  }
  return arg;
}

static ConfitStatus confit_tui_command_rebuild(ConfitTuiState *state,
                                               ConfitDiagnostic *diagnostic) {
  ConfitStatus status;

  state->selected_view_index = 0U;
  status = confit_tui_rebuild_view(state, diagnostic);
  if (status == CONFIT_OK) {
    confit_tui_select_first_option(state);
    confit_tui_refresh_search_position(state);
  }
  return status;
}

static ConfitStatus confit_tui_dispatch_command(ConfitTuiState *state,
                                                const char *command,
                                                ConfitDiagnostic *diagnostic) {
  const char *arg;

  if (command == 0 || command[0] == '\0') {
    (void)snprintf(state->status, sizeof(state->status), "empty command");
    state->status[sizeof(state->status) - 1U] = '\0';
    return CONFIT_OK;
  }
  if (confit_tui_command_starts_with(command, "verbose")) {
    state->verbose_inspector = 1;
    (void)snprintf(state->status, sizeof(state->status),
                   "verbose inspector mode");
    state->status[sizeof(state->status) - 1U] = '\0';
    return CONFIT_OK;
  }
  if (confit_tui_command_starts_with(command, "noverbose")) {
    state->verbose_inspector = 0;
    (void)snprintf(state->status, sizeof(state->status),
                   "compact inspector mode");
    state->status[sizeof(state->status) - 1U] = '\0';
    return CONFIT_OK;
  }
  if (confit_tui_command_starts_with(command, "tree")) {
    state->flat_view = 0;
    state->current_menu_index = 0U;
    (void)snprintf(state->status, sizeof(state->status), "tree view");
    state->status[sizeof(state->status) - 1U] = '\0';
    return confit_tui_command_rebuild(state, diagnostic);
  }
  if (confit_tui_command_starts_with(command, "flat")) {
    state->flat_view = 1;
    state->current_menu_index = 0U;
    (void)snprintf(state->status, sizeof(state->status), "flat view");
    state->status[sizeof(state->status) - 1U] = '\0';
    return confit_tui_command_rebuild(state, diagnostic);
  }
  arg = confit_tui_command_arg(command, "filter");
  if (arg != 0) {
    if (arg[0] == '\0') {
      (void)snprintf(state->status, sizeof(state->status),
                     "usage: :filter <text>");
      state->status[sizeof(state->status) - 1U] = '\0';
      return CONFIT_OK;
    }
    (void)snprintf(state->text_filter, sizeof(state->text_filter), "%s", arg);
    state->text_filter[sizeof(state->text_filter) - 1U] = '\0';
    (void)snprintf(state->status, sizeof(state->status), "filter: %s",
                   state->text_filter);
    state->status[sizeof(state->status) - 1U] = '\0';
    return confit_tui_command_rebuild(state, diagnostic);
  }
  if (confit_tui_command_starts_with(command, "clear")) {
    confit_tui_clear_filters(state);
    return confit_tui_command_rebuild(state, diagnostic);
  }
  if (confit_tui_command_starts_with(command, "help")) {
    (void)snprintf(state->status, sizeof(state->status),
                   "commands: verbose noverbose tree flat filter <text> clear "
                   "help quit");
    state->status[sizeof(state->status) - 1U] = '\0';
    return CONFIT_OK;
  }
  if (confit_tui_command_starts_with(command, "quit")) {
    state->quit_requested = 1;
    (void)snprintf(state->status, sizeof(state->status), "quit requested");
    state->status[sizeof(state->status) - 1U] = '\0';
    return CONFIT_OK;
  }

  (void)snprintf(state->status, sizeof(state->status), "unknown command: %s",
                 command);
  state->status[sizeof(state->status) - 1U] = '\0';
  return CONFIT_OK;
}

static ConfitStatus confit_tui_run_command(ConfitTuiState *state,
                                           ConfitDiagnostic *diagnostic) {
  char input[160];
  char *command;
  int input_status;

  input_status = confit_tui_curses_read_mode_line(CONFIT_TUI_INPUT_COMMAND, ":",
                                                  input, sizeof(input));
  if (input_status != CONFIT_TUI_INPUT_ACCEPTED) {
    if (confit_tui_input_cancelled(input_status)) {
      confit_tui_set_input_cancelled(state, CONFIT_TUI_INPUT_COMMAND);
    }
    return CONFIT_OK;
  }
  command = confit_tui_command_trim(input);
  return confit_tui_dispatch_command(state, command, diagnostic);
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
  if (key == CONFIT_TUI_KEY_COMMAND) {
    return confit_tui_run_command(state, diagnostic);
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
  if (key == CONFIT_TUI_KEY_VERBOSE_INSPECTOR) {
    state->verbose_inspector = !state->verbose_inspector;
    (void)snprintf(state->status, sizeof(state->status),
                   "%s inspector mode",
                   state->verbose_inspector ? "verbose" : "compact");
    state->status[sizeof(state->status) - 1U] = '\0';
    return CONFIT_OK;
  }
  if (key == CONFIT_TUI_KEY_LEFT) {
    return confit_tui_go_parent_menu(state, diagnostic);
  }
  if (key == CONFIT_TUI_KEY_CANCEL) {
    if (state->current_menu_index != 0U) {
      return confit_tui_go_parent_menu(state, diagnostic);
    }
    if (state->search[0] != '\0' || state->category[0] != '\0' ||
        state->tag[0] != '\0' || state->text_filter[0] != '\0') {
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
    if (row != 0 && row->kind == CONFIT_TUI_ROW_MENU) {
      return confit_tui_enter_menu(state, row->menu_index, diagnostic);
    }
    option = confit_tui_selected_option(state);
    if (option == 0) {
      (void)snprintf(state->status, sizeof(state->status),
                     "select an option or menu");
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
    if (key == CONFIT_TUI_KEY_CANCEL && state->current_menu_index != 0U) {
      confit_diagnostic_init(&diagnostic);
      status = confit_tui_go_parent_menu(state, &diagnostic);
      if (status != CONFIT_OK) {
        confit_tui_set_status_from_diagnostic(state, "error", status,
                                              &diagnostic);
      }
      continue;
    }
    if (key == CONFIT_TUI_KEY_QUIT ||
        (key == CONFIT_TUI_KEY_CANCEL && state->current_menu_index == 0U)) {
      int should_quit;

      confit_diagnostic_init(&diagnostic);
      should_quit = 0;
      status = confit_tui_request_exit(state, &should_quit, &diagnostic);
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
    if (state->quit_requested) {
      int should_quit;

      state->quit_requested = 0;
      confit_diagnostic_init(&diagnostic);
      should_quit = 0;
      status = confit_tui_request_exit(state, &should_quit, &diagnostic);
      if (status != CONFIT_OK) {
        confit_tui_set_status_from_diagnostic(state, "error", status,
                                              &diagnostic);
        continue;
      }
      if (should_quit) {
        break;
      }
    }
  } while (1);

  return CONFIT_OK;
}

static void confit_tui_state_clear(ConfitTuiState *state) {
  if (state == 0) {
    return;
  }
  confit_tui_edits_clear(state);
  confit_tui_menus_clear(state);
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
