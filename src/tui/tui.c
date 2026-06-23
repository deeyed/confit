#include "confit/tui.h"

#include <curses.h>
#include <errno.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/graph.h"
#include "confit/host.h"
#include "confit/model.h"
#include "confit/resolver.h"
#include "confit/schema.h"

typedef enum ConfitTuiKey {
  CONFIT_TUI_KEY_NONE = 0,
  CONFIT_TUI_KEY_QUIT = 1,
  CONFIT_TUI_KEY_UP = 2,
  CONFIT_TUI_KEY_DOWN = 3,
  CONFIT_TUI_KEY_ENTER = 4,
  CONFIT_TUI_KEY_EDIT = 5,
  CONFIT_TUI_KEY_SAVE = 6,
  CONFIT_TUI_KEY_SEARCH = 7,
  CONFIT_TUI_KEY_CATEGORY = 8,
  CONFIT_TUI_KEY_TAG = 9,
  CONFIT_TUI_KEY_CLEAR_FILTER = 10,
  CONFIT_TUI_KEY_NEW = 11,
  CONFIT_TUI_KEY_PROMPT = 12,
  CONFIT_TUI_KEY_HELP = 13,
  CONFIT_TUI_KEY_RANGE = 14,
  CONFIT_TUI_KEY_CHOICES = 15,
} ConfitTuiKey;

typedef struct ConfitTuiListItem {
  const char *label;
  const char *detail;
  const char *value;
} ConfitTuiListItem;

typedef struct ConfitTuiScreen {
  const char *title;
  const char *subtitle;
  const ConfitTuiListItem *items;
  size_t item_count;
  size_t selected_index;
  const char *status;
} ConfitTuiScreen;

static int confit_tui_curses_started = 0;

static const char *confit_tui_curses_text(const char *text) {
  return text != 0 ? text : "";
}

static int confit_tui_curses_start(void) {
  if (confit_tui_curses_started) {
    return 0;
  }
  (void)setlocale(LC_ALL, "");
  if (initscr() == 0) {
    return -1;
  }
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  (void)curs_set(0);
  if (has_colors()) {
    start_color();
    use_default_colors();
    init_pair(1, COLOR_BLACK, COLOR_WHITE);
    init_pair(2, COLOR_CYAN, -1);
    init_pair(3, COLOR_YELLOW, -1);
  }
  confit_tui_curses_started = 1;
  return 0;
}

static void confit_tui_curses_stop(void) {
  if (confit_tui_curses_started) {
    endwin();
    confit_tui_curses_started = 0;
  }
}

static void confit_tui_curses_add_clipped(int row, int col, const char *text,
                                          int width) {
  if (width <= 0) {
    return;
  }
  mvaddnstr(row, col, confit_tui_curses_text(text), width);
}

static void confit_tui_curses_add_centered(int row, const char *text) {
  const int width = COLS > 0 ? COLS : 80;
  const int size = (int)strlen(confit_tui_curses_text(text));
  const int col = size < width ? (width - size) / 2 : 0;
  confit_tui_curses_add_clipped(row, col, text, width - col);
}

static void confit_tui_curses_render_subtitle(const char *subtitle,
                                              int *row) {
  const char *cursor;
  const char *line_begin;
  int width;

  cursor = confit_tui_curses_text(subtitle);
  width = COLS > 4 ? COLS - 4 : 76;
  while (*cursor != '\0' && *row < LINES - 4) {
    size_t line_size;

    line_begin = cursor;
    while (*cursor != '\0' && *cursor != '\n') {
      cursor += 1;
    }
    line_size = (size_t)(cursor - line_begin);
    move(*row, 2);
    if (has_colors()) {
      attron(COLOR_PAIR(2));
    }
    addnstr(line_begin, line_size < (size_t)width ? (int)line_size : width);
    if (has_colors()) {
      attroff(COLOR_PAIR(2));
    }
    *row += 1;
    if (*cursor == '\n') {
      cursor += 1;
    }
  }
}

static const char *confit_tui_curses_marker(const ConfitTuiListItem *item,
                                            char *buffer,
                                            size_t buffer_size) {
  const char *value;

  value = confit_tui_curses_text(item->value);
  if (strcmp(value, "true") == 0) {
    return "[*]";
  }
  if (strcmp(value, "false") == 0) {
    return "[ ]";
  }
  (void)snprintf(buffer, buffer_size, "(%s)", value[0] != '\0' ? value : "-");
  buffer[buffer_size - 1U] = '\0';
  return buffer;
}

static int confit_tui_curses_render(const ConfitTuiScreen *screen) {
  size_t index;
  size_t first;
  size_t visible_count;
  int row;
  int list_top;
  int list_bottom;
  int max_visible;
  int width;

  if (screen == 0 || confit_tui_curses_start() != 0) {
    return -1;
  }

  erase();
  width = COLS > 0 ? COLS : 80;
  if (LINES > 2 && COLS > 2) {
    box(stdscr, 0, 0);
  }
  if (has_colors()) {
    attron(COLOR_PAIR(3) | A_BOLD);
  } else {
    attron(A_BOLD);
  }
  confit_tui_curses_add_centered(0, confit_tui_curses_text(screen->title));
  if (has_colors()) {
    attroff(COLOR_PAIR(3) | A_BOLD);
  } else {
    attroff(A_BOLD);
  }

  row = 2;
  confit_tui_curses_render_subtitle(screen->subtitle, &row);
  list_top = row + 1;
  list_bottom = LINES > 4 ? LINES - 3 : list_top;
  max_visible = list_bottom > list_top ? list_bottom - list_top : 0;

  if (screen->item_count == 0U || max_visible <= 0) {
    confit_tui_curses_add_clipped(list_top, 2, "(no options)", width - 4);
  } else {
    visible_count = (size_t)max_visible;
    if (visible_count > screen->item_count) {
      visible_count = screen->item_count;
    }
    first = 0U;
    if (screen->selected_index >= visible_count / 2U) {
      first = screen->selected_index - visible_count / 2U;
    }
    if (first + visible_count > screen->item_count) {
      first = screen->item_count - visible_count;
    }

    for (index = 0U; index < visible_count; ++index) {
      const size_t item_index = first + index;
      const ConfitTuiListItem *item = &screen->items[item_index];
      char marker_buffer[80];
      char line[512];
      const char *marker = confit_tui_curses_marker(
          item, marker_buffer, sizeof(marker_buffer));
      const int selected = item_index == screen->selected_index;

      (void)snprintf(line, sizeof(line), "%c %-10s %-36s  %s",
                     selected ? '>' : ' ', marker,
                     confit_tui_curses_text(item->label),
                     confit_tui_curses_text(item->detail));
      line[sizeof(line) - 1U] = '\0';
      if (selected) {
        attron(A_REVERSE);
      }
      confit_tui_curses_add_clipped(list_top + (int)index, 2, line,
                                    width - 4);
      if (selected) {
        attroff(A_REVERSE);
      }
    }
  }

  if (LINES > 2) {
    move(LINES - 2, 1);
    hline(ACS_HLINE, width > 2 ? width - 2 : width);
    if (has_colors()) {
      attron(COLOR_PAIR(1));
    } else {
      attron(A_REVERSE);
    }
    move(LINES - 1, 1);
    clrtoeol();
    confit_tui_curses_add_clipped(LINES - 1, 2,
                                  confit_tui_curses_text(screen->status),
                                  width - 4);
    if (has_colors()) {
      attroff(COLOR_PAIR(1));
    } else {
      attroff(A_REVERSE);
    }
  }

  return refresh() == OK ? 0 : -1;
}

static ConfitTuiKey confit_tui_curses_read_key(void) {
  const int ch = getch();

  if (ch == ERR || ch == 'q' || ch == 'Q') {
    return CONFIT_TUI_KEY_QUIT;
  }
  if (ch == KEY_UP || ch == 'k' || ch == 'K') {
    return CONFIT_TUI_KEY_UP;
  }
  if (ch == KEY_DOWN || ch == 'j' || ch == 'J') {
    return CONFIT_TUI_KEY_DOWN;
  }
  if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
    return CONFIT_TUI_KEY_ENTER;
  }
  if (ch == ' ') {
    return CONFIT_TUI_KEY_ENTER;
  }
  if (ch == 'e' || ch == 'E') {
    return CONFIT_TUI_KEY_EDIT;
  }
  if (ch == 's' || ch == 'S') {
    return CONFIT_TUI_KEY_SAVE;
  }
  if (ch == '/') {
    return CONFIT_TUI_KEY_SEARCH;
  }
  if (ch == 'c' || ch == 'C') {
    return CONFIT_TUI_KEY_CATEGORY;
  }
  if (ch == 't' || ch == 'T') {
    return CONFIT_TUI_KEY_TAG;
  }
  if (ch == 'x' || ch == 'X') {
    return CONFIT_TUI_KEY_CLEAR_FILTER;
  }
  if (ch == 'n' || ch == 'N') {
    return CONFIT_TUI_KEY_NEW;
  }
  if (ch == 'p' || ch == 'P') {
    return CONFIT_TUI_KEY_PROMPT;
  }
  if (ch == 'h' || ch == 'H') {
    return CONFIT_TUI_KEY_HELP;
  }
  if (ch == 'r' || ch == 'R') {
    return CONFIT_TUI_KEY_RANGE;
  }
  if (ch == 'o' || ch == 'O') {
    return CONFIT_TUI_KEY_CHOICES;
  }
  return CONFIT_TUI_KEY_NONE;
}

static int confit_tui_curses_read_line(const char *prompt, char *out, size_t out_size) {
  int row;

  if (out == 0 || out_size == 0U || confit_tui_curses_start() != 0) {
    return -1;
  }
  out[0] = '\0';
  row = LINES > 2 ? LINES - 2 : 0;
  move(row, 1);
  clrtoeol();
  if (has_colors()) {
    attron(COLOR_PAIR(1));
  } else {
    attron(A_REVERSE);
  }
  confit_tui_curses_add_clipped(row, 2, confit_tui_curses_text(prompt),
                                COLS > 4 ? COLS - 4 : 76);
  if (has_colors()) {
    attroff(COLOR_PAIR(1));
  } else {
    attroff(A_REVERSE);
  }
  echo();
  (void)curs_set(1);
  move(row, 2 + (int)strlen(confit_tui_curses_text(prompt)));
  if (getnstr(out, (int)out_size - 1) == ERR) {
    noecho();
    (void)curs_set(0);
    out[0] = '\0';
    return -1;
  }
  noecho();
  (void)curs_set(0);
  out[out_size - 1U] = '\0';
  return 0;
}

typedef struct ConfitTuiRow {
  ConfitTuiListItem item;
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

static ConfitStatus confit_tui_load_checked_project(
    ConfitTuiState *state, ConfitDiagnostic *diagnostic) {
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
    if (confit_tui_curses_read_line(prompt, input, sizeof(input)) != 0) {
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

  if (confit_tui_curses_read_line(prompt, input, sizeof(input)) != 0) {
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
  ConfitTuiListItem *items;
  ConfitTuiScreen screen;
  char subtitle[320];
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
  if (confit_tui_curses_render(&screen) != 0) {
    free(items);
    return CONFIT_ERR_INTERNAL;
  }
  free(items);
  return CONFIT_OK;
}

static ConfitStatus confit_tui_handle_key(ConfitTuiState *state, ConfitTuiKey key,
                                          ConfitDiagnostic *diagnostic) {
  const ConfitOption *option;

  if (key == CONFIT_TUI_KEY_DOWN && state->selected_view_index + 1U < state->view_count) {
    state->selected_view_index += 1U;
    return CONFIT_OK;
  }
  if (key == CONFIT_TUI_KEY_UP && state->selected_view_index > 0U) {
    state->selected_view_index -= 1U;
    return CONFIT_OK;
  }
  if (key == CONFIT_TUI_KEY_SEARCH) {
    return confit_tui_set_filter(state->search, sizeof(state->search),
                                 "search: ", state, diagnostic);
  }
  if (key == CONFIT_TUI_KEY_CATEGORY) {
    return confit_tui_set_filter(state->category, sizeof(state->category),
                                 "category: ", state, diagnostic);
  }
  if (key == CONFIT_TUI_KEY_TAG) {
    return confit_tui_set_filter(state->tag, sizeof(state->tag), "tag: ",
                                 state, diagnostic);
  }
  if (key == CONFIT_TUI_KEY_CLEAR_FILTER) {
    confit_tui_clear_filters(state);
    return confit_tui_rebuild_view(state, diagnostic);
  }
  if (key == CONFIT_TUI_KEY_SAVE) {
    return confit_tui_save_profile(state, diagnostic);
  }
  if (key == CONFIT_TUI_KEY_EDIT || key == CONFIT_TUI_KEY_ENTER) {
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
  ConfitTuiKey key;
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
    key = confit_tui_curses_read_key();
    if (key == CONFIT_TUI_KEY_QUIT) {
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

typedef struct ConfitTuiSchemaOption {
  char id[128];
  char type[16];
  char default_value[128];
  char prompt[128];
  char help[256];
  char category[64];
  char tags[128];
  char range[128];
  char choices[128];
} ConfitTuiSchemaOption;

typedef struct ConfitTuiSchemaState {
  const ConfitTuiOptions *options;
  ConfitProject *project;
  ConfitGraph *graph;
  ConfitTuiSchemaOption *options_list;
  size_t option_count;
  size_t selected_index;
  char status[256];
  int dirty;
} ConfitTuiSchemaState;

static int confit_tui_schema_valid_id_char(char value) {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
         (value >= '0' && value <= '9') || value == '_' || value == '-';
}

static int confit_tui_schema_valid_option_id(const char *id) {
  size_t index;
  int saw_dot;
  int segment_has_char;

  if (id == 0 || id[0] == '\0' || id[0] == '.' ||
      strcmp(id, "system") == 0 || strncmp(id, "system.", 7U) == 0) {
    return 0;
  }

  saw_dot = 0;
  segment_has_char = 0;
  for (index = 0U; id[index] != '\0'; ++index) {
    if (id[index] == '.') {
      if (!segment_has_char) {
        return 0;
      }
      saw_dot = 1;
      segment_has_char = 0;
      continue;
    }
    if (!confit_tui_schema_valid_id_char(id[index])) {
      return 0;
    }
    segment_has_char = 1;
  }
  return saw_dot && segment_has_char;
}

static int confit_tui_schema_type_is_valid(const char *type) {
  return strcmp(type, "bool") == 0 || strcmp(type, "int") == 0 ||
         strcmp(type, "uint") == 0 || strcmp(type, "hex") == 0 ||
         strcmp(type, "string") == 0 || strcmp(type, "enum") == 0 ||
         strcmp(type, "float") == 0 || strcmp(type, "path") == 0;
}

static void confit_tui_schema_default_for_type(ConfitTuiSchemaOption *option) {
  if (strcmp(option->type, "bool") == 0) {
    (void)snprintf(option->default_value, sizeof(option->default_value),
                   "false");
  } else if (strcmp(option->type, "int") == 0 ||
             strcmp(option->type, "uint") == 0) {
    (void)snprintf(option->default_value, sizeof(option->default_value), "0");
  } else if (strcmp(option->type, "hex") == 0) {
    (void)snprintf(option->default_value, sizeof(option->default_value),
                   "0x0");
  } else if (strcmp(option->type, "float") == 0) {
    (void)snprintf(option->default_value, sizeof(option->default_value),
                   "0.0");
  } else {
    option->default_value[0] = '\0';
  }
}

static ConfitTuiSchemaOption *confit_tui_schema_selected(
    ConfitTuiSchemaState *state) {
  if (state->option_count == 0U || state->selected_index >= state->option_count) {
    return 0;
  }
  return &state->options_list[state->selected_index];
}

static ConfitStatus confit_tui_schema_load(ConfitTuiSchemaState *state,
                                           ConfitDiagnostic *diagnostic) {
  ConfitStatus status;

  status = confit_schema_load_project(state->options->project_root,
                                      &state->project, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_graph_build(state->project, &state->graph, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_graph_validate(state->graph, diagnostic);
  }
  return status;
}

static ConfitStatus confit_tui_schema_add_option(
    ConfitTuiSchemaState *state, ConfitDiagnostic *diagnostic) {
  char id[128];
  char type[32];
  char prompt[128];
  ConfitTuiSchemaOption *new_options;
  ConfitTuiSchemaOption *option;
  size_t index;

  if (confit_tui_curses_read_line("option id: ", id, sizeof(id)) != 0 || id[0] == '\0') {
    return CONFIT_OK;
  }
  if (!confit_tui_schema_valid_option_id(id)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, id, 0, 0,
                          "invalid schema option id");
    return CONFIT_ERR_SCHEMA;
  }
  if (confit_project_find_option(state->project, id) != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, id, 0, 0,
                          "schema option already exists");
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < state->option_count; ++index) {
    if (strcmp(state->options_list[index].id, id) == 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, id, 0, 0,
                            "schema draft already exists");
      return CONFIT_ERR_SCHEMA;
    }
  }
  if (confit_tui_curses_read_line("type: ", type, sizeof(type)) != 0 || type[0] == '\0') {
    return CONFIT_OK;
  }
  if (!confit_tui_schema_type_is_valid(type)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, type, 0, 0,
                          "invalid schema option type");
    return CONFIT_ERR_SCHEMA;
  }
  if (confit_tui_curses_read_line("prompt: ", prompt, sizeof(prompt)) != 0) {
    prompt[0] = '\0';
  }

  new_options = (ConfitTuiSchemaOption *)realloc(
      state->options_list,
      (state->option_count + 1U) * sizeof(state->options_list[0]));
  if (new_options == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, id, 0, 0,
                          "failed to allocate schema option");
    return CONFIT_ERR_INTERNAL;
  }
  state->options_list = new_options;
  option = &state->options_list[state->option_count];
  memset(option, 0, sizeof(*option));
  (void)snprintf(option->id, sizeof(option->id), "%s", id);
  (void)snprintf(option->type, sizeof(option->type), "%s", type);
  (void)snprintf(option->prompt, sizeof(option->prompt), "%s", prompt);
  confit_tui_schema_default_for_type(option);
  state->selected_index = state->option_count;
  state->option_count += 1U;
  state->dirty = 1;
  (void)snprintf(state->status, sizeof(state->status), "created %s", id);
  state->status[sizeof(state->status) - 1U] = '\0';
  return CONFIT_OK;
}

static ConfitStatus confit_tui_schema_set_string(char *slot, size_t slot_size,
                                                 const char *prompt,
                                                 ConfitTuiSchemaState *state) {
  char input[256];

  if (confit_tui_curses_read_line(prompt, input, sizeof(input)) != 0) {
    return CONFIT_OK;
  }
  (void)snprintf(slot, slot_size, "%s", input);
  slot[slot_size - 1U] = '\0';
  state->dirty = 1;
  (void)snprintf(state->status, sizeof(state->status), "schema field updated");
  state->status[sizeof(state->status) - 1U] = '\0';
  return CONFIT_OK;
}

static ConfitStatus confit_tui_schema_validate_range(
    const ConfitTuiSchemaOption *option, ConfitDiagnostic *diagnostic) {
  char scratch[128];
  char *comma;
  uint64_t uint_min;
  uint64_t uint_max;
  uint64_t uint_default;
  int64_t int_min;
  int64_t int_max;
  int64_t int_default;
  double float_min;
  double float_max;
  double float_default;

  if (option->range[0] == '\0') {
    return CONFIT_OK;
  }
  if (strcmp(option->type, "int") != 0 && strcmp(option->type, "uint") != 0 &&
      strcmp(option->type, "hex") != 0 && strcmp(option->type, "float") != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                          "range is only valid for numeric schema options");
    return CONFIT_ERR_SCHEMA;
  }
  (void)snprintf(scratch, sizeof(scratch), "%s", option->range);
  scratch[sizeof(scratch) - 1U] = '\0';
  comma = strchr(scratch, ',');
  if (comma == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                          "schema range must use min,max");
    return CONFIT_ERR_SCHEMA;
  }
  *comma = '\0';
  if (strcmp(option->type, "int") == 0) {
    if (confit_tui_parse_int64(scratch, &int_min) == CONFIT_OK &&
        confit_tui_parse_int64(comma + 1, &int_max) == CONFIT_OK &&
        confit_tui_parse_int64(option->default_value, &int_default) ==
            CONFIT_OK &&
        int_min <= int_max && int_default >= int_min &&
        int_default <= int_max) {
      return CONFIT_OK;
    }
  }
  if (strcmp(option->type, "float") == 0 &&
      confit_tui_parse_double(scratch, &float_min) == CONFIT_OK &&
      confit_tui_parse_double(comma + 1, &float_max) == CONFIT_OK &&
      confit_tui_parse_double(option->default_value, &float_default) ==
          CONFIT_OK &&
      float_min <= float_max && float_default >= float_min &&
      float_default <= float_max) {
    return CONFIT_OK;
  }
  if ((strcmp(option->type, "uint") == 0 || strcmp(option->type, "hex") == 0) &&
      confit_tui_parse_uint64(scratch, &uint_min) == CONFIT_OK &&
      confit_tui_parse_uint64(comma + 1, &uint_max) == CONFIT_OK &&
      confit_tui_parse_uint64(option->default_value, &uint_default) ==
          CONFIT_OK &&
      uint_min <= uint_max && uint_default >= uint_min &&
      uint_default <= uint_max) {
    return CONFIT_OK;
  }
  confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                        "schema range does not contain the default");
  return CONFIT_ERR_SCHEMA;
}

static ConfitStatus confit_tui_schema_validate_drafts(
    const ConfitTuiSchemaState *state, ConfitDiagnostic *diagnostic) {
  size_t index;

  for (index = 0U; index < state->option_count; ++index) {
    const ConfitTuiSchemaOption *option = &state->options_list[index];
    size_t next_index;

    if (!confit_tui_schema_valid_option_id(option->id) ||
        !confit_tui_schema_type_is_valid(option->type)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                            "invalid schema draft");
      return CONFIT_ERR_SCHEMA;
    }
    for (next_index = index + 1U; next_index < state->option_count;
         ++next_index) {
      if (strcmp(option->id, state->options_list[next_index].id) == 0) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                              "duplicate schema draft id");
        return CONFIT_ERR_SCHEMA;
      }
    }
    if (strcmp(option->type, "enum") == 0 && option->choices[0] == '\0') {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                            "enum schema option needs choices");
      return CONFIT_ERR_SCHEMA;
    }
    if (confit_tui_schema_validate_range(option, diagnostic) != CONFIT_OK) {
      return CONFIT_ERR_SCHEMA;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_tui_text_append_csv_quoted_array(
    ConfitTuiTextBuilder *builder, const char *csv) {
  char scratch[256];
  char *cursor;
  char *token;
  int first;
  ConfitStatus status;

  status = confit_tui_text_append(builder, "[");
  if (status != CONFIT_OK) {
    return status;
  }
  (void)snprintf(scratch, sizeof(scratch), "%s", csv != 0 ? csv : "");
  scratch[sizeof(scratch) - 1U] = '\0';
  cursor = scratch;
  first = 1;
  while ((token = strtok(cursor, ",")) != 0) {
    cursor = 0;
    while (*token == ' ' || *token == '\t') {
      token += 1;
    }
    if (!first) {
      status = confit_tui_text_append(builder, ", ");
      if (status != CONFIT_OK) {
        return status;
      }
    }
    status = confit_tui_text_append_quoted(builder, token);
    if (status != CONFIT_OK) {
      return status;
    }
    first = 0;
  }
  return confit_tui_text_append(builder, "]");
}

static ConfitStatus confit_tui_schema_append_default(
    ConfitTuiTextBuilder *builder, const ConfitTuiSchemaOption *option) {
  if (strcmp(option->type, "enum") == 0) {
    char first_choice[128];
    char *comma;

    (void)snprintf(first_choice, sizeof(first_choice), "%s", option->choices);
    first_choice[sizeof(first_choice) - 1U] = '\0';
    comma = strchr(first_choice, ',');
    if (comma != 0) {
      *comma = '\0';
    }
    return confit_tui_text_append_quoted(builder, first_choice);
  }
  if (strcmp(option->type, "string") == 0 || strcmp(option->type, "path") == 0) {
    return confit_tui_text_append_quoted(builder, option->default_value);
  }
  return confit_tui_text_append(builder, option->default_value);
}

static ConfitStatus confit_tui_schema_build_toml(
    const ConfitTuiSchemaState *state, char **out_text,
    ConfitDiagnostic *diagnostic) {
  ConfitTuiTextBuilder builder;
  ConfitStatus status;
  size_t index;

  *out_text = 0;
  status = confit_tui_schema_validate_drafts(state, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  confit_tui_text_builder_init(&builder);
  for (index = 0U; status == CONFIT_OK && index < state->option_count;
       ++index) {
    const ConfitTuiSchemaOption *option = &state->options_list[index];

    status = confit_tui_text_append(&builder, "[option.");
    if (status == CONFIT_OK) {
      status = confit_tui_text_append_quoted(&builder, option->id);
    }
    if (status == CONFIT_OK) {
      status = confit_tui_text_append(&builder, "]\ntype = ");
    }
    if (status == CONFIT_OK) {
      status = confit_tui_text_append_quoted(&builder, option->type);
    }
    if (status == CONFIT_OK && option->choices[0] != '\0') {
      status = confit_tui_text_append(&builder, "\nchoices = ");
      if (status == CONFIT_OK) {
        status = confit_tui_text_append_csv_quoted_array(&builder,
                                                         option->choices);
      }
    }
    if (status == CONFIT_OK) {
      status = confit_tui_text_append(&builder, "\ndefault = ");
    }
    if (status == CONFIT_OK) {
      status = confit_tui_schema_append_default(&builder, option);
    }
    if (status == CONFIT_OK && option->prompt[0] != '\0') {
      status = confit_tui_text_append(&builder, "\nprompt = ");
      if (status == CONFIT_OK) {
        status = confit_tui_text_append_quoted(&builder, option->prompt);
      }
    }
    if (status == CONFIT_OK && option->category[0] != '\0') {
      status = confit_tui_text_append(&builder, "\ncategory = ");
      if (status == CONFIT_OK) {
        status = confit_tui_text_append_quoted(&builder, option->category);
      }
    }
    if (status == CONFIT_OK && option->tags[0] != '\0') {
      status = confit_tui_text_append(&builder, "\ntags = ");
      if (status == CONFIT_OK) {
        status = confit_tui_text_append_csv_quoted_array(&builder,
                                                         option->tags);
      }
    }
    if (status == CONFIT_OK && option->help[0] != '\0') {
      status = confit_tui_text_append(&builder, "\nhelp = ");
      if (status == CONFIT_OK) {
        status = confit_tui_text_append_quoted(&builder, option->help);
      }
    }
    if (status == CONFIT_OK && option->range[0] != '\0') {
      status = confit_tui_text_append(&builder, "\nrange = [");
      if (status == CONFIT_OK) {
        status = confit_tui_text_append(&builder, option->range);
      }
      if (status == CONFIT_OK) {
        status = confit_tui_text_append(&builder, "]");
      }
    }
    if (status == CONFIT_OK) {
      status = confit_tui_text_append(
          &builder, index + 1U < state->option_count ? "\n\n" : "\n");
    }
  }
  if (status != CONFIT_OK) {
    free(builder.text);
    return status;
  }
  *out_text = builder.text;
  return CONFIT_OK;
}

static ConfitStatus confit_tui_schema_path(const ConfitTuiOptions *options,
                                           char *out_path, size_t out_size,
                                           char *out_dir, size_t out_dir_size,
                                           ConfitDiagnostic *diagnostic) {
  char config_dir[1024];
  ConfitStatus status;

  status = confit_host_path_join(config_dir, sizeof(config_dir),
                                 options->project_root, "config", diagnostic);
  if (status == CONFIT_OK) {
    status = confit_host_path_join(out_dir, out_dir_size, config_dir,
                                   "options", diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_path_join(out_path, out_size, out_dir,
                                   "tui-schema.toml", diagnostic);
  }
  return status;
}

static ConfitStatus confit_tui_schema_save(ConfitTuiSchemaState *state,
                                           ConfitDiagnostic *diagnostic) {
  char *toml;
  char schema_dir[1024];
  char schema_path[1024];
  ConfitProject *check_project;
  ConfitGraph *check_graph;
  ConfitStatus status;

  toml = 0;
  check_project = 0;
  check_graph = 0;
  status = confit_tui_schema_build_toml(state, &toml, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_tui_schema_path(state->options, schema_path,
                                    sizeof(schema_path), schema_dir,
                                    sizeof(schema_dir), diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_make_directories(schema_dir, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_write_text_file(schema_path, toml, diagnostic);
  }
  free(toml);
  if (status == CONFIT_OK) {
    status = confit_schema_load_project(state->options->project_root,
                                        &check_project, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_graph_build(check_project, &check_graph, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_graph_validate(check_graph, diagnostic);
  }
  confit_graph_free(check_graph);
  confit_project_free(check_project);
  if (status == CONFIT_OK) {
    state->dirty = 0;
    (void)snprintf(state->status, sizeof(state->status),
                   "schema saved %s", schema_path);
    state->status[sizeof(state->status) - 1U] = '\0';
  }
  return status;
}

static ConfitStatus confit_tui_schema_render(
    const ConfitTuiSchemaState *state) {
  ConfitTuiListItem *items;
  ConfitTuiScreen screen;
  char status_line[384];
  char subtitle[256];
  char(*details)[224];
  char(*values)[64];
  size_t index;

  items = 0;
  details = 0;
  values = 0;
  if (state->option_count > 0U) {
    items = (ConfitTuiListItem *)calloc(state->option_count, sizeof(items[0]));
    details = (char(*)[224])calloc(state->option_count, sizeof(details[0]));
    values = (char(*)[64])calloc(state->option_count, sizeof(values[0]));
    if (items == 0 || details == 0 || values == 0) {
      free(items);
      free(details);
      free(values);
      return CONFIT_ERR_INTERNAL;
    }
  }
  for (index = 0U; index < state->option_count; ++index) {
    const ConfitTuiSchemaOption *option = &state->options_list[index];

    (void)snprintf(details[index], sizeof(details[index]),
                   "%s | %s | %s | tags=%s range=%s choices=%s",
                   confit_tui_text_or_dash(option->category),
                   confit_tui_text_or_dash(option->prompt),
                   confit_tui_text_or_dash(option->help),
                   confit_tui_text_or_dash(option->tags),
                   confit_tui_text_or_dash(option->range),
                   confit_tui_text_or_dash(option->choices));
    details[index][sizeof(details[index]) - 1U] = '\0';
    (void)snprintf(values[index], sizeof(values[index]), "%s", option->type);
    values[index][sizeof(values[index]) - 1U] = '\0';
    items[index].label = option->id;
    items[index].detail = details[index];
    items[index].value = values[index];
  }

  (void)snprintf(subtitle, sizeof(subtitle),
                 "Schema edit mode changes project configuration semantics.\n"
                 "Prefer code review for schema changes.\n"
                 "project=%s dirty=%s",
                 confit_tui_text_or_dash(
                     state->project != 0 ? state->project->name : 0),
                 state->dirty ? "yes" : "no");
  subtitle[sizeof(subtitle) - 1U] = '\0';
  (void)snprintf(status_line, sizeof(status_line),
                 "schema %lu/%lu | n new p prompt h help c category t tag "
                 "r range o choices s save q quit | %s",
                 state->option_count == 0U
                     ? 0UL
                     : (unsigned long)(state->selected_index + 1U),
                 (unsigned long)state->option_count,
                 state->status[0] != '\0' ? state->status : "guarded");
  status_line[sizeof(status_line) - 1U] = '\0';
  screen.title = "Confit Schema Editor";
  screen.subtitle = subtitle;
  screen.items = items;
  screen.item_count = state->option_count;
  screen.selected_index = state->selected_index;
  screen.status = status_line;
  if (confit_tui_curses_render(&screen) != 0) {
    free(items);
    free(details);
    free(values);
    return CONFIT_ERR_INTERNAL;
  }
  free(items);
  free(details);
  free(values);
  return CONFIT_OK;
}

static ConfitStatus confit_tui_schema_handle_key(
    ConfitTuiSchemaState *state, ConfitTuiKey key, ConfitDiagnostic *diagnostic) {
  ConfitTuiSchemaOption *option;

  if (key == CONFIT_TUI_KEY_DOWN && state->selected_index + 1U < state->option_count) {
    state->selected_index += 1U;
    return CONFIT_OK;
  }
  if (key == CONFIT_TUI_KEY_UP && state->selected_index > 0U) {
    state->selected_index -= 1U;
    return CONFIT_OK;
  }
  if (key == CONFIT_TUI_KEY_NEW) {
    return confit_tui_schema_add_option(state, diagnostic);
  }
  if (key == CONFIT_TUI_KEY_SAVE) {
    return confit_tui_schema_save(state, diagnostic);
  }

  option = confit_tui_schema_selected(state);
  if (option == 0) {
    (void)snprintf(state->status, sizeof(state->status),
                   "create an option first");
    state->status[sizeof(state->status) - 1U] = '\0';
    return CONFIT_OK;
  }
  if (key == CONFIT_TUI_KEY_PROMPT) {
    return confit_tui_schema_set_string(option->prompt,
                                        sizeof(option->prompt), "prompt: ",
                                        state);
  }
  if (key == CONFIT_TUI_KEY_HELP) {
    return confit_tui_schema_set_string(option->help, sizeof(option->help),
                                        "help: ", state);
  }
  if (key == CONFIT_TUI_KEY_CATEGORY) {
    return confit_tui_schema_set_string(option->category,
                                        sizeof(option->category),
                                        "category: ", state);
  }
  if (key == CONFIT_TUI_KEY_TAG) {
    return confit_tui_schema_set_string(option->tags, sizeof(option->tags),
                                        "tags comma-list: ", state);
  }
  if (key == CONFIT_TUI_KEY_RANGE) {
    return confit_tui_schema_set_string(option->range, sizeof(option->range),
                                        "range min,max: ", state);
  }
  if (key == CONFIT_TUI_KEY_CHOICES) {
    return confit_tui_schema_set_string(option->choices,
                                        sizeof(option->choices),
                                        "choices comma-list: ", state);
  }
  return CONFIT_OK;
}

static ConfitStatus confit_tui_run_schema_editor(
    const ConfitTuiOptions *options, ConfitDiagnostic *diagnostic) {
  ConfitTuiSchemaState state;
  ConfitStatus status;
  ConfitTuiKey key;

  memset(&state, 0, sizeof(state));
  state.options = options;
  status = confit_tui_schema_load(&state, diagnostic);
  if (status != CONFIT_OK) {
    confit_graph_free(state.graph);
    confit_project_free(state.project);
    return status;
  }
  (void)snprintf(state.status, sizeof(state.status), "guarded schema editing");
  do {
    status = confit_tui_schema_render(&state);
    if (status != CONFIT_OK) {
      break;
    }
    key = confit_tui_curses_read_key();
    if (key == CONFIT_TUI_KEY_QUIT) {
      break;
    }
    confit_diagnostic_init(diagnostic);
    status = confit_tui_schema_handle_key(&state, key, diagnostic);
    if (status != CONFIT_OK) {
      (void)snprintf(state.status, sizeof(state.status), "error: %s",
                     diagnostic->message != 0 ? diagnostic->message
                                              : confit_status_name(status));
      state.status[sizeof(state.status) - 1U] = '\0';
      status = CONFIT_OK;
    }
  } while (1);

  free(state.options_list);
  confit_graph_free(state.graph);
  confit_project_free(state.project);
  confit_tui_curses_stop();
  return status;
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

  if (options == 0 || options->project_root == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid tui options");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  if (options->schema_edit) {
    return confit_tui_run_schema_editor(options, diagnostic);
  }
  if (options->profile_name == 0) {
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
