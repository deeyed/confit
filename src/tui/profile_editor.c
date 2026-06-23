#include "tui_internal.h"

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
  char detail[224];
  char value[128];
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
} ConfitTuiState;

static ConfitStatus confit_tui_refresh_rows(ConfitTuiState *state,
                                            ConfitDiagnostic *diagnostic);

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

  if (out == 0 || out_size == 0U || text == 0 || text[0] == '\0') {
    return;
  }
  used = strlen(out);
  if (used + 1U >= out_size) {
    return;
  }
  available = out_size - used;
  (void)snprintf(out + used, available, "%s", text);
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
      const char *mark;
      char tags[96];
      char deps[96];
      ConfitTuiRow *row;

      if (strcmp(confit_tui_option_category_name(option),
                 state->categories[category_index].name) != 0) {
        continue;
      }

      resolved = confit_resolved_config_find(state->config, option->id);
      edit = confit_tui_find_const_edit(state, option->id);
      mark = edit != 0 ? "*" : " ";
      confit_tui_format_tag_summary(option, tags, sizeof(tags));
      confit_tui_format_dependency_summary(option, deps, sizeof(deps));

      row = &state->rows[state->row_count];
      row->kind = CONFIT_TUI_ROW_OPTION;
      row->category_index = category_index;
      row->option = option;
      (void)snprintf(row->label, sizeof(row->label), "%s", option->id);
      row->label[sizeof(row->label) - 1U] = '\0';
      (void)snprintf(row->detail, sizeof(row->detail),
                     "%s%s | %s | tags: %s | %s", mark,
                     confit_option_type_name(option->type), deps, tags,
                     confit_tui_text_or_dash(option->prompt));
      row->detail[sizeof(row->detail) - 1U] = '\0';

      confit_tui_format_value(
          option, resolved != 0 ? &resolved->value : &option->default_value,
          row->value, sizeof(row->value));
      row->item.label = row->label;
      row->item.detail = row->detail;
      row->item.value = row->value;
      row->item.depth = 1U;
      row->item.is_heading = 0;
      row->item.expanded = 0;
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
  status =
      confit_tui_resolved_value_for_option(state, option, &current, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  next = option->enum_values[0];
  if (current->kind == CONFIT_VALUE_ENUM ||
      current->kind == CONFIT_VALUE_STRING) {
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
    if (confit_tui_curses_read_line(prompt, input, sizeof(input)) != 0) {
      (void)snprintf(state->status, sizeof(state->status), "cancelled");
      state->status[sizeof(state->status) - 1U] = '\0';
      return CONFIT_OK;
    }
    if (input[0] == '\0') {
      return confit_tui_next_enum(state, option, diagnostic);
    }
  } else {
    char prompt[256];

    (void)snprintf(prompt, sizeof(prompt), "value for %s: ", option->id);
    prompt[sizeof(prompt) - 1U] = '\0';
    if (confit_tui_curses_read_line(prompt, input, sizeof(input)) != 0 ||
        input[0] == '\0') {
      (void)snprintf(state->status, sizeof(state->status), "cancelled");
      state->status[sizeof(state->status) - 1U] = '\0';
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

static ConfitStatus confit_tui_save_profile(ConfitTuiState *state,
                                            ConfitDiagnostic *diagnostic) {
  ConfitProfile *profile;
  char *toml;
  char config_dir[1024];
  char profile_dir[1024];
  char file_name[256];
  char profile_path[1024];
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
    (void)snprintf(state->status, sizeof(state->status), "saved %s",
                   profile_path);
    state->status[sizeof(state->status) - 1U] = '\0';
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
    status = confit_tui_detail_append_dependencies(&builder, option);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_text_append(&builder, "\nHelp\n");
  }
  if (status == CONFIT_OK) {
    status = confit_tui_text_append(
        &builder,
        option->help != 0 && option->help[0] != '\0' ? option->help : "-");
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

static ConfitStatus confit_tui_render_screen(const ConfitTuiState *state,
                                             const char *target_name) {
  ConfitTuiListItem *items;
  ConfitTuiScreen screen;
  char header[320];
  char key_legend[128];
  char status_line[384];
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

  (void)snprintf(
      header, sizeof(header),
      "project=%s profile=%s target=%s search=%s result=%lu/%lu "
      "filter.category=%s filter.tag=%s menus=%lu dirty=%s",
      confit_tui_text_or_dash(state->project->name),
      confit_tui_text_or_dash(state->options->profile_name),
      confit_tui_text_or_dash(target_name),
      confit_tui_text_or_dash(state->search),
      (unsigned long)state->search_position, (unsigned long)state->search_count,
      confit_tui_text_or_dash(state->category),
      confit_tui_text_or_dash(state->tag), (unsigned long)state->category_count,
      state->dirty ? "yes" : "no");
  header[sizeof(header) - 1U] = '\0';
  (void)snprintf(key_legend, sizeof(key_legend),
                 "arrows/jk move PgUp/PgDn Home/End Enter/Space toggle / "
                 "search n/N result c/t filter ?/h detail q quit");
  key_legend[sizeof(key_legend) - 1U] = '\0';
  (void)snprintf(
      status_line, sizeof(status_line), "row %lu/%lu search %lu/%lu | %s",
      state->view_count == 0U
          ? 0UL
          : (unsigned long)(state->selected_view_index + 1U),
      (unsigned long)state->view_count, (unsigned long)state->search_position,
      (unsigned long)state->search_count,
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

  target_name = confit_tui_effective_target_name(state->project,
                                                 state->options->profile_name,
                                                 state->options->target_name);
  (void)snprintf(state->status, sizeof(state->status), "ready");
  state->status[sizeof(state->status) - 1U] = '\0';

  do {
    status = confit_tui_render_screen(state, target_name);
    if (status != CONFIT_OK) {
      return status;
    }
    key = confit_tui_curses_read_key();
    if (key == CONFIT_TUI_KEY_QUIT) {
      break;
    }
    confit_diagnostic_init(&diagnostic);
    status = confit_tui_handle_key(state, key, target_name, &diagnostic);
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
