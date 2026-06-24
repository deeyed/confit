#include "tui_internal.h"

#include <ctype.h>
#include <curses.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int confit_tui_curses_started = 0;
static int confit_tui_curses_colors_enabled = 0;
static short confit_tui_curses_default_bg = COLOR_BLACK;

typedef enum ConfitTuiCursesStyle {
  CONFIT_TUI_STYLE_TITLE = 0,
  CONFIT_TUI_STYLE_PATH = 1,
  CONFIT_TUI_STYLE_HEADER = 2,
  CONFIT_TUI_STYLE_LIST = 3,
  CONFIT_TUI_STYLE_SELECTION = 4,
  CONFIT_TUI_STYLE_DISABLED = 5,
  CONFIT_TUI_STYLE_FORCED = 6,
  CONFIT_TUI_STYLE_WARNING = 7,
  CONFIT_TUI_STYLE_STATUS = 8,
  CONFIT_TUI_STYLE_KEY = 9,
  CONFIT_TUI_STYLE_SEARCH_MATCH = 10,
  CONFIT_TUI_STYLE_CATEGORY = 11,
  CONFIT_TUI_STYLE_SEPARATOR = 12,
  CONFIT_TUI_STYLE_HELP = 13,
  CONFIT_TUI_STYLE_DIALOG = 14,
  CONFIT_TUI_STYLE_EDIT = 15,
  CONFIT_TUI_STYLE_COUNT = 16
} ConfitTuiCursesStyle;

typedef struct ConfitTuiCursesStyleDef {
  short pair;
  int color_attr;
  int mono_attr;
} ConfitTuiCursesStyleDef;

static const ConfitTuiCursesStyleDef
    confit_tui_curses_style_defs[CONFIT_TUI_STYLE_COUNT] = {
        {3, A_BOLD, A_BOLD},                 /* title */
        {2, A_BOLD, A_BOLD},                 /* path */
        {2, A_BOLD, A_BOLD},                 /* header */
        {0, A_NORMAL, A_NORMAL},             /* list */
        {4, A_BOLD, A_REVERSE},              /* selection */
        {5, A_DIM, A_DIM},                   /* disabled */
        {8, A_BOLD, A_BOLD},                 /* forced */
        {6, A_BOLD, A_BOLD},                 /* warning */
        {1, A_NORMAL, A_REVERSE},            /* status */
        {1, A_NORMAL, A_REVERSE},            /* key */
        {9, A_BOLD | A_UNDERLINE, A_BOLD},   /* search match */
        {7, A_BOLD, A_BOLD},                 /* category */
        {3, A_BOLD, A_BOLD},                 /* separator */
        {2, A_NORMAL, A_NORMAL},             /* help */
        {10, A_BOLD, A_BOLD},                /* dialog */
        {11, A_NORMAL, A_REVERSE},           /* edit */
};

static const char *confit_tui_curses_text(const char *text) {
  return text != 0 ? text : "";
}

static int confit_tui_curses_contains(const char *text, const char *needle) {
  return strstr(confit_tui_curses_text(text), needle) != 0;
}

static ConfitTuiCursesStyle confit_tui_curses_status_style(const char *text) {
  if (confit_tui_curses_contains(text, "invalid") ||
      confit_tui_curses_contains(text, "error") ||
      confit_tui_curses_contains(text, "failed") ||
      confit_tui_curses_contains(text, "SCHEMA EDIT")) {
    return CONFIT_TUI_STYLE_WARNING;
  }
  if (confit_tui_curses_contains(text, "blocked:") ||
      confit_tui_curses_contains(text, "forced")) {
    return CONFIT_TUI_STYLE_FORCED;
  }
  return CONFIT_TUI_STYLE_STATUS;
}

static int confit_tui_curses_init_pair_safe(short pair, short fg, short bg) {
  if (init_pair(pair, fg, bg) == OK) {
    return 0;
  }
  if (bg == -1 && init_pair(pair, fg, COLOR_BLACK) == OK) {
    return 0;
  }
  return -1;
}

static void confit_tui_curses_init_styles(void) {
  const int no_color = getenv("NO_COLOR") != 0;

  confit_tui_curses_colors_enabled = 0;
  confit_tui_curses_default_bg = COLOR_BLACK;
  if (no_color || !has_colors()) {
    return;
  }
  (void)start_color();
  if (use_default_colors() == OK) {
    confit_tui_curses_default_bg = -1;
  }
  confit_tui_curses_colors_enabled = 1;
  (void)confit_tui_curses_init_pair_safe(1, COLOR_BLACK, COLOR_WHITE);
  (void)confit_tui_curses_init_pair_safe(2, COLOR_CYAN,
                                         confit_tui_curses_default_bg);
  (void)confit_tui_curses_init_pair_safe(3, COLOR_YELLOW,
                                         confit_tui_curses_default_bg);
  (void)confit_tui_curses_init_pair_safe(4, COLOR_BLACK, COLOR_CYAN);
  (void)confit_tui_curses_init_pair_safe(5, COLOR_WHITE,
                                         confit_tui_curses_default_bg);
  (void)confit_tui_curses_init_pair_safe(6, COLOR_RED,
                                         confit_tui_curses_default_bg);
  (void)confit_tui_curses_init_pair_safe(7, COLOR_MAGENTA,
                                         confit_tui_curses_default_bg);
  (void)confit_tui_curses_init_pair_safe(8, COLOR_RED,
                                         confit_tui_curses_default_bg);
  (void)confit_tui_curses_init_pair_safe(9, COLOR_GREEN,
                                         confit_tui_curses_default_bg);
  (void)confit_tui_curses_init_pair_safe(10, COLOR_YELLOW,
                                         confit_tui_curses_default_bg);
  (void)confit_tui_curses_init_pair_safe(11, COLOR_BLACK, COLOR_WHITE);
}

static int confit_tui_curses_style_attr(ConfitTuiCursesStyle style) {
  const ConfitTuiCursesStyleDef *def;

  if ((int)style < 0 || style >= CONFIT_TUI_STYLE_COUNT) {
    return A_NORMAL;
  }
  def = &confit_tui_curses_style_defs[style];
  if (confit_tui_curses_colors_enabled && def->pair > 0) {
    return COLOR_PAIR(def->pair) | def->color_attr;
  }
  return def->mono_attr;
}

static void confit_tui_curses_style_on(ConfitTuiCursesStyle style) {
  const int attr = confit_tui_curses_style_attr(style);

  if (attr != A_NORMAL) {
    attron(attr);
  }
}

static void confit_tui_curses_style_off(ConfitTuiCursesStyle style) {
  const int attr = confit_tui_curses_style_attr(style);

  if (attr != A_NORMAL) {
    attroff(attr);
  }
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
  confit_tui_curses_init_styles();
  confit_tui_curses_started = 1;
  return 0;
}

void confit_tui_curses_stop(void) {
  if (confit_tui_curses_started) {
    endwin();
    confit_tui_curses_started = 0;
    confit_tui_curses_colors_enabled = 0;
  }
}

static void confit_tui_curses_add_clipped(int row, int col, const char *text,
                                          int width) {
  if (width <= 0) {
    return;
  }
  mvaddnstr(row, col, confit_tui_curses_text(text), width);
}

static void confit_tui_curses_fill_span(int row, int col, int width) {
  int index;

  for (index = 0; index < width; ++index) {
    mvaddch(row, col + index, ' ');
  }
}

static void confit_tui_curses_add_centered(int row, const char *text) {
  const int width = COLS > 0 ? COLS : 80;
  const int size = (int)strlen(confit_tui_curses_text(text));
  const int col = size < width ? (width - size) / 2 : 0;
  confit_tui_curses_add_clipped(row, col, text, width - col);
}

static void confit_tui_curses_add_centered_in_span(int row, int col, int width,
                                                   const char *text) {
  const int size = (int)strlen(confit_tui_curses_text(text));
  const int offset = size < width ? (width - size) / 2 : 0;
  confit_tui_curses_add_clipped(row, col + offset, text, width - offset);
}

static void confit_tui_curses_render_lines(const char *text, int *row,
                                           int last_row, int col, int width,
                                           ConfitTuiCursesStyle style) {
  const char *cursor;
  const char *line_begin;

  cursor = confit_tui_curses_text(text);
  while (*cursor != '\0' && *row <= last_row) {
    size_t line_size;

    line_begin = cursor;
    while (*cursor != '\0' && *cursor != '\n') {
      cursor += 1;
    }
    line_size = (size_t)(cursor - line_begin);
    confit_tui_curses_style_on(style);
    move(*row, col);
    addnstr(line_begin, line_size < (size_t)width ? (int)line_size : width);
    confit_tui_curses_style_off(style);
    *row += 1;
    if (*cursor == '\n') {
      cursor += 1;
    }
  }
}

static void confit_tui_curses_draw_hline(int row, int col, int width) {
  if (width > 0) {
    mvhline(row, col, ACS_HLINE, width);
  }
}

static void confit_tui_curses_draw_vline(int row, int col, int height) {
  if (height > 0) {
    mvvline(row, col, ACS_VLINE, height);
  }
}

static void confit_tui_curses_draw_box(int top, int left, int height, int width,
                                       const char *title) {
  if (height < 2 || width < 2) {
    return;
  }
  mvaddch(top, left, ACS_ULCORNER);
  mvaddch(top, left + width - 1, ACS_URCORNER);
  mvaddch(top + height - 1, left, ACS_LLCORNER);
  mvaddch(top + height - 1, left + width - 1, ACS_LRCORNER);
  confit_tui_curses_draw_hline(top, left + 1, width - 2);
  confit_tui_curses_draw_hline(top + height - 1, left + 1, width - 2);
  confit_tui_curses_draw_vline(top + 1, left, height - 2);
  confit_tui_curses_draw_vline(top + 1, left + width - 1, height - 2);
  if (title != 0 && title[0] != '\0' && width > 8) {
    char label[128];

    (void)snprintf(label, sizeof(label), " %s ", title);
    label[sizeof(label) - 1U] = '\0';
    confit_tui_curses_add_centered_in_span(top, left + 1, width - 2, label);
  }
}

static void confit_tui_curses_draw_separator(int row, int width,
                                             const char *label,
                                             const char *right_label) {
  if (row < 0 || width <= 0) {
    return;
  }
  confit_tui_curses_style_on(CONFIT_TUI_STYLE_SEPARATOR);
  confit_tui_curses_draw_hline(row, 0, width);
  if (label != 0 && label[0] != '\0' && width > 4) {
    char text[128];

    (void)snprintf(text, sizeof(text), " %s ", label);
    text[sizeof(text) - 1U] = '\0';
    confit_tui_curses_add_clipped(row, 1, text, width - 1);
  }
  if (right_label != 0 && right_label[0] != '\0' && width > 8) {
    char text[96];
    int text_size;
    int col;

    (void)snprintf(text, sizeof(text), " %s ", right_label);
    text[sizeof(text) - 1U] = '\0';
    text_size = (int)strlen(text);
    col = text_size + 1 < width ? width - text_size - 1 : 0;
    confit_tui_curses_add_clipped(row, col, text, width - col);
  }
  confit_tui_curses_style_off(CONFIT_TUI_STYLE_SEPARATOR);
}

static const char *confit_tui_curses_marker(const ConfitTuiListItem *item,
                                            char *buffer, size_t buffer_size) {
  const char *value;

  if (item != 0 && item->is_heading) {
    return item->expanded ? "[-]" : "[+]";
  }
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

static void confit_tui_curses_copy_clipped(char *out, size_t out_size,
                                           const char *text,
                                           size_t max_chars) {
  size_t length;

  if (out == 0 || out_size == 0U) {
    return;
  }
  out[0] = '\0';
  if (text == 0 || max_chars == 0U) {
    return;
  }
  length = strlen(text);
  if (length > max_chars) {
    length = max_chars;
  }
  if (length + 1U > out_size) {
    length = out_size - 1U;
  }
  if (length > 0U) {
    memcpy(out, text, length);
  }
  out[length] = '\0';
  if (text[length] != '\0' && length > 1U) {
    out[length - 1U] = '~';
  }
}

static int confit_tui_curses_detail_field(const char *detail,
                                          const char *name, char *out,
                                          size_t out_size) {
  const char *begin;
  const char *end;
  size_t name_size;
  size_t size;

  if (out == 0 || out_size == 0U) {
    return 0;
  }
  out[0] = '\0';
  detail = confit_tui_curses_text(detail);
  name = confit_tui_curses_text(name);
  name_size = strlen(name);
  if (name_size == 0U) {
    return 0;
  }
  begin = strstr(detail, name);
  if (begin == 0) {
    return 0;
  }
  begin += name_size;
  end = strstr(begin, " | ");
  if (end == 0) {
    end = begin + strlen(begin);
  }
  size = (size_t)(end - begin);
  if (size >= out_size) {
    size = out_size - 1U;
  }
  memcpy(out, begin, size);
  out[size] = '\0';
  return out[0] != '\0';
}

static int confit_tui_curses_row_has_detail(const ConfitTuiListItem *item,
                                            int selected, int width) {
  return selected && item != 0 && !item->is_heading && width >= 96 &&
         confit_tui_curses_text(item->detail)[0] != '\0';
}

static int confit_tui_curses_row_height(const ConfitTuiListItem *item,
                                        int selected, int width) {
  return confit_tui_curses_row_has_detail(item, selected, width) ? 2 : 1;
}

static void confit_tui_curses_render_detail_row(int row, int col, int width,
                                                const ConfitTuiListItem *item) {
  char id[128];
  char type[32];
  char deps[96];
  char tags[96];
  char state[128];
  char line[512];
  int indent;

  if (item == 0 || width <= 0) {
    return;
  }
  (void)confit_tui_curses_detail_field(item->detail, "id=", id, sizeof(id));
  (void)confit_tui_curses_detail_field(item->detail, "type=", type,
                                       sizeof(type));
  (void)confit_tui_curses_detail_field(item->detail, "deps: ", deps,
                                       sizeof(deps));
  (void)confit_tui_curses_detail_field(item->detail, "tags: ", tags,
                                       sizeof(tags));
  (void)confit_tui_curses_detail_field(item->detail, "state=", state,
                                       sizeof(state));
  indent = width >= 120 ? 6 : 4;
  if (width >= 140) {
    (void)snprintf(line, sizeof(line),
                   "%*sid=%s | type=%s | deps: %s | tags: %s | state=%s",
                   indent, "", confit_tui_curses_text(id),
                   confit_tui_curses_text(type), confit_tui_curses_text(deps),
                   confit_tui_curses_text(tags), confit_tui_curses_text(state));
  } else if (width >= 120) {
    (void)snprintf(line, sizeof(line),
                   "%*sid=%s | type=%s | deps: %s | state=%s", indent, "",
                   confit_tui_curses_text(id), confit_tui_curses_text(type),
                   confit_tui_curses_text(deps), confit_tui_curses_text(state));
  } else {
    (void)snprintf(line, sizeof(line), "%*sid=%s | type=%s | state=%s", indent,
                   "", confit_tui_curses_text(id),
                   confit_tui_curses_text(type), confit_tui_curses_text(state));
  }
  line[sizeof(line) - 1U] = '\0';
  confit_tui_curses_style_on(item->is_disabled ? CONFIT_TUI_STYLE_DISABLED
                                               : CONFIT_TUI_STYLE_HELP);
  confit_tui_curses_add_clipped(row, col, line, width);
  confit_tui_curses_style_off(item->is_disabled ? CONFIT_TUI_STYLE_DISABLED
                                                : CONFIT_TUI_STYLE_HELP);
}

static void confit_tui_curses_render_row(int row, int col, int width,
                                         const ConfitTuiListItem *item,
                                         int selected, int show_detail) {
  char marker_buffer[80];
  char marker_text[32];
  char label_buffer[192];
  char label_text[224];
  char id[128];
  char badge[32];
  char suffix[192];
  char line[512];
  ConfitTuiCursesStyle style;
  int selected_disabled;
  int marker_width;
  int fixed_width;
  int suffix_width;
  int label_width;
  int dirty;
  int blocked;
  int forced;
  const char *marker =
      confit_tui_curses_marker(item, marker_buffer, sizeof(marker_buffer));
  const unsigned depth = item != 0 ? item->depth : 0U;
  const int indent = depth > 8U ? 16 : (int)(depth * 2U);

  if (item != 0 && item->is_heading) {
    (void)snprintf(label_buffer, sizeof(label_buffer), "%*s%s", indent, "",
                   confit_tui_curses_text(item->label));
    label_buffer[sizeof(label_buffer) - 1U] = '\0';
    (void)snprintf(line, sizeof(line), "%c %-4s %-28s %s", selected ? '>' : ' ',
                   marker, label_buffer, confit_tui_curses_text(item->detail));
  } else {
    dirty = item != 0 && confit_tui_curses_contains(item->detail, "dirty");
    blocked = item != 0 && (item->is_disabled ||
                            confit_tui_curses_contains(item->detail,
                                                       "blocked:"));
    forced = item != 0 && confit_tui_curses_contains(item->detail, "forced");
    (void)confit_tui_curses_detail_field(item != 0 ? item->detail : 0, "id=",
                                         id, sizeof(id));
    badge[0] = '\0';
    if (forced) {
      (void)snprintf(badge, sizeof(badge), " [forced]");
    } else if (blocked) {
      (void)snprintf(badge, sizeof(badge), " [blocked]");
    }
    suffix[0] = '\0';
    if (width >= 72 && id[0] != '\0') {
      (void)snprintf(suffix, sizeof(suffix), " <%s>%s", id, badge);
    } else if (badge[0] != '\0') {
      (void)snprintf(suffix, sizeof(suffix), "%s", badge);
    }
    suffix[sizeof(suffix) - 1U] = '\0';
    marker_width = width >= 100 ? 12 : 8;
    fixed_width = 2 + marker_width + 3;
    suffix_width = (int)strlen(suffix);
    label_width = width - fixed_width - suffix_width;
    if (label_width < 8) {
      label_width = width - fixed_width;
      suffix[0] = '\0';
      suffix_width = 0;
    }
    if (label_width < 1) {
      label_width = 1;
    }
    confit_tui_curses_copy_clipped(marker_text, sizeof(marker_text), marker,
                                   (size_t)marker_width);
    (void)snprintf(label_buffer, sizeof(label_buffer), "%*s%s", indent, "",
                   confit_tui_curses_text(item != 0 ? item->label : 0));
    label_buffer[sizeof(label_buffer) - 1U] = '\0';
    confit_tui_curses_copy_clipped(label_text, sizeof(label_text), label_buffer,
                                   (size_t)label_width);
    (void)snprintf(line, sizeof(line), "%c %-*s %c %s%s", selected ? '>' : ' ',
                   marker_width, marker_text, dirty ? '*' : ' ', label_text,
                   suffix);
  }
  line[sizeof(line) - 1U] = '\0';
  if (selected) {
    style = CONFIT_TUI_STYLE_SELECTION;
  } else if (item != 0 && item->is_heading) {
    style = CONFIT_TUI_STYLE_CATEGORY;
  } else if (item != 0 &&
             confit_tui_curses_contains(item->detail, "forced")) {
    style = CONFIT_TUI_STYLE_FORCED;
  } else if (item != 0 && item->is_disabled) {
    style = CONFIT_TUI_STYLE_DISABLED;
  } else {
    style = CONFIT_TUI_STYLE_LIST;
  }
  selected_disabled = selected && item != 0 && item->is_disabled;
  confit_tui_curses_style_on(style);
  if (selected_disabled) {
    attron(A_DIM);
  }
  if (selected) {
    confit_tui_curses_fill_span(row, col, width);
  }
  confit_tui_curses_add_clipped(row, col, line, width);
  if (selected_disabled) {
    attroff(A_DIM);
  }
  confit_tui_curses_style_off(style);
  if (show_detail && confit_tui_curses_row_has_detail(item, selected, width)) {
    confit_tui_curses_render_detail_row(row + 1, col, width, item);
  }
}

static void confit_tui_curses_render_compact(const ConfitTuiScreen *screen,
                                             int width) {
  size_t index;
  size_t first;
  size_t visible_count;
  int row;
  int list_height;

  erase();
  confit_tui_curses_style_on(CONFIT_TUI_STYLE_TITLE);
  confit_tui_curses_add_centered(0, confit_tui_curses_text(screen->title));
  confit_tui_curses_style_off(CONFIT_TUI_STYLE_TITLE);
  if (LINES > 1) {
    char compact_status[384];

    (void)snprintf(compact_status, sizeof(compact_status),
                   "compact terminal fallback; %s",
                   confit_tui_curses_text(screen->status));
    compact_status[sizeof(compact_status) - 1U] = '\0';
    confit_tui_curses_style_on(confit_tui_curses_status_style(screen->status));
    confit_tui_curses_add_clipped(1, 0, compact_status, width);
    confit_tui_curses_style_off(confit_tui_curses_status_style(screen->status));
  }

  list_height = LINES > 4 ? LINES - 4 : 0;
  first = 0U;
  visible_count = list_height > 0 ? (size_t)list_height : 0U;
  if (visible_count > screen->item_count) {
    visible_count = screen->item_count;
  }
  if (visible_count > 0U && screen->selected_index >= visible_count) {
    first = screen->selected_index - visible_count + 1U;
  }
  if (first + visible_count > screen->item_count) {
    first = screen->item_count - visible_count;
  }
  for (index = 0U, row = 2; index < visible_count; ++index, ++row) {
    const size_t item_index = first + index;

    confit_tui_curses_render_row(row, 0, width, &screen->items[item_index],
                                 item_index == screen->selected_index, 0);
  }
  if (screen->item_count == 0U && LINES > 2) {
    confit_tui_curses_add_clipped(2, 0, "(no options)", width);
  }
  if (LINES > 1) {
    confit_tui_curses_style_on(CONFIT_TUI_STYLE_KEY);
    move(LINES - 1, 0);
    clrtoeol();
    confit_tui_curses_add_clipped(
        LINES - 1, 0, confit_tui_curses_text(screen->key_legend), width);
    confit_tui_curses_style_off(CONFIT_TUI_STYLE_KEY);
  }
}

int confit_tui_curses_render(const ConfitTuiScreen *screen) {
  size_t index;
  size_t first;
  size_t visible_count;
  size_t last_visible;
  size_t selected_index;
  int header_row;
  int separator_row;
  int list_top;
  int list_left;
  int list_width;
  int list_height;
  int footer_separator_row;
  int max_visible;
  int render_row;
  int selected_extra_lines;
  int width;
  char range[64];

  if (screen == 0 || confit_tui_curses_start() != 0) {
    return -1;
  }

  erase();
  width = COLS > 0 ? COLS : 80;
  if (LINES < 9 || width < 40) {
    confit_tui_curses_render_compact(screen, width);
    return refresh() == OK ? 0 : -1;
  }

  confit_tui_curses_style_on(CONFIT_TUI_STYLE_TITLE);
  confit_tui_curses_add_centered(0, confit_tui_curses_text(screen->title));
  confit_tui_curses_style_off(CONFIT_TUI_STYLE_TITLE);

  header_row = 1;
  confit_tui_curses_render_lines(screen->header, &header_row, LINES - 7, 2,
                                 width > 4 ? width - 4 : width,
                                 CONFIT_TUI_STYLE_HEADER);

  separator_row = header_row;
  list_top = separator_row + 1;
  footer_separator_row = LINES - 3;
  list_left = 1;
  list_width = width > 2 ? width - 2 : width;
  list_height = footer_separator_row - list_top;
  if (list_height < 1) {
    confit_tui_curses_render_compact(screen, width);
    return refresh() == OK ? 0 : -1;
  }

  selected_index = screen->selected_index;
  if (screen->item_count > 0U && selected_index >= screen->item_count) {
    selected_index = screen->item_count - 1U;
  }
  selected_extra_lines = 0;
  if (screen->item_count > 0U && list_height >= 2 &&
      confit_tui_curses_row_height(&screen->items[selected_index], 1,
                                   list_width) > 1) {
    selected_extra_lines = 1;
  }
  max_visible = list_height > selected_extra_lines
                    ? list_height - selected_extra_lines
                    : 1;
  visible_count = 0U;
  first = 0U;
  last_visible = 0U;
  range[0] = '\0';
  confit_tui_curses_draw_separator(footer_separator_row, width, "", "");

  if (screen->item_count == 0U || max_visible <= 0) {
    (void)snprintf(range, sizeof(range), "0/0");
    range[sizeof(range) - 1U] = '\0';
    confit_tui_curses_draw_separator(separator_row, width, "Options", range);
    confit_tui_curses_add_clipped(list_top, list_left, "(no options)",
                                  list_width);
  } else {
    visible_count = (size_t)max_visible;
    if (visible_count > screen->item_count) {
      visible_count = screen->item_count;
    }
    if (selected_index >= visible_count / 2U) {
      first = selected_index - visible_count / 2U;
    }
    if (first + visible_count > screen->item_count) {
      first = screen->item_count - visible_count;
    }
    last_visible = first + visible_count;
    (void)snprintf(range, sizeof(range), "%lu-%lu/%lu",
                   (unsigned long)(first + 1U), (unsigned long)last_visible,
                   (unsigned long)screen->item_count);
    range[sizeof(range) - 1U] = '\0';
    confit_tui_curses_draw_separator(separator_row, width, "Options", range);

    render_row = list_top;
    for (index = 0U; index < visible_count; ++index) {
      const size_t item_index = first + index;
      const ConfitTuiListItem *item = &screen->items[item_index];
      const int selected = item_index == selected_index;
      const int show_detail = selected && selected_extra_lines > 0;

      confit_tui_curses_render_row(render_row, list_left, list_width, item,
                                   selected, show_detail);
      render_row += show_detail ? 2 : 1;
    }

    if (first > 0U) {
      mvaddch(separator_row, 2, ACS_UARROW);
      mvaddch(separator_row, 3, ACS_UARROW);
      mvaddch(separator_row, 4, ACS_UARROW);
    }
    if (last_visible < screen->item_count) {
      mvaddch(footer_separator_row, 2, ACS_DARROW);
      mvaddch(footer_separator_row, 3, ACS_DARROW);
      mvaddch(footer_separator_row, 4, ACS_DARROW);
    }
  }

  if (LINES > 3) {
    move(LINES - 2, 0);
    clrtoeol();
    confit_tui_curses_style_on(CONFIT_TUI_STYLE_KEY);
    confit_tui_curses_add_clipped(
        LINES - 2, 1, confit_tui_curses_text(screen->key_legend), width - 2);
    confit_tui_curses_style_off(CONFIT_TUI_STYLE_KEY);
  }

  if (LINES > 2) {
    const ConfitTuiCursesStyle status_style =
        confit_tui_curses_status_style(screen->status);

    move(LINES - 1, 0);
    clrtoeol();
    confit_tui_curses_style_on(status_style);
    confit_tui_curses_add_clipped(
        LINES - 1, 1, confit_tui_curses_text(screen->status), width - 2);
    confit_tui_curses_style_off(status_style);
  }

  return refresh() == OK ? 0 : -1;
}

static size_t confit_tui_curses_count_text_lines(const char *text) {
  size_t count;
  const char *cursor;

  text = confit_tui_curses_text(text);
  if (text[0] == '\0') {
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

static const char *confit_tui_curses_skip_text_lines(const char *text,
                                                     size_t line_count) {
  const char *cursor;
  size_t index;

  cursor = confit_tui_curses_text(text);
  for (index = 0U; index < line_count && *cursor != '\0'; ++index) {
    while (*cursor != '\0' && *cursor != '\n') {
      cursor += 1;
    }
    if (*cursor == '\n') {
      cursor += 1;
    }
  }
  return cursor;
}

static void confit_tui_curses_render_text_body(const char *body, int top,
                                               int left, int height, int width,
                                               size_t first_line) {
  const char *cursor;
  int row;

  if (height <= 0 || width <= 0) {
    return;
  }
  if (confit_tui_curses_text(body)[0] == '\0') {
    confit_tui_curses_add_clipped(top, left, "(no detail)", width);
    return;
  }

  cursor = confit_tui_curses_skip_text_lines(body, first_line);
  for (row = 0; row < height && *cursor != '\0'; ++row) {
    const char *line_begin = cursor;
    size_t line_size;

    while (*cursor != '\0' && *cursor != '\n') {
      cursor += 1;
    }
    line_size = (size_t)(cursor - line_begin);
    move(top + row, left);
    addnstr(line_begin, line_size < (size_t)width ? (int)line_size : width);
    if (*cursor == '\n') {
      cursor += 1;
    }
  }
}

int confit_tui_curses_render_text(const char *title, const char *header,
                                  const char *body, const char *key_legend,
                                  const char *status, size_t first_line) {
  size_t body_lines;
  size_t last_visible;
  int header_row;
  int box_top;
  int box_left;
  int box_width;
  int box_height;
  int body_top;
  int body_height;
  int width;

  if (confit_tui_curses_start() != 0) {
    return -1;
  }

  erase();
  width = COLS > 0 ? COLS : 80;
  confit_tui_curses_style_on(CONFIT_TUI_STYLE_TITLE);
  confit_tui_curses_add_centered(0, confit_tui_curses_text(title));
  confit_tui_curses_style_off(CONFIT_TUI_STYLE_TITLE);

  if (LINES < 9 || width < 40) {
    if (LINES > 1) {
      confit_tui_curses_add_clipped(1, 0, confit_tui_curses_text(status),
                                    width);
    }
    confit_tui_curses_render_text_body(body, 2, 0, LINES > 4 ? LINES - 4 : 0,
                                       width, first_line);
    if (LINES > 1) {
      confit_tui_curses_style_on(CONFIT_TUI_STYLE_KEY);
      move(LINES - 1, 0);
      clrtoeol();
      confit_tui_curses_add_clipped(LINES - 1, 0,
                                    confit_tui_curses_text(key_legend), width);
      confit_tui_curses_style_off(CONFIT_TUI_STYLE_KEY);
    }
    return refresh() == OK ? 0 : -1;
  }

  header_row = 1;
  confit_tui_curses_render_lines(header, &header_row, LINES - 7, 2,
                                 width > 4 ? width - 4 : width,
                                 CONFIT_TUI_STYLE_HEADER);
  box_top = header_row + 1;
  box_left = 1;
  box_width = width > 2 ? width - 2 : width;
  box_height = LINES - box_top - 3;
  if (box_height < 3) {
    confit_tui_curses_render_text_body(
        body, header_row + 1, 1, LINES - header_row - 3, width - 2, first_line);
    return refresh() == OK ? 0 : -1;
  }

  confit_tui_curses_style_on(CONFIT_TUI_STYLE_DIALOG);
  confit_tui_curses_draw_box(box_top, box_left, box_height, box_width, "Help");
  confit_tui_curses_style_off(CONFIT_TUI_STYLE_DIALOG);

  body_top = box_top + 1;
  body_height = box_height - 2;
  confit_tui_curses_render_text_body(body, body_top, box_left + 2, body_height,
                                     box_width - 4, first_line);

  body_lines = confit_tui_curses_count_text_lines(body);
  last_visible = first_line + (body_height > 0 ? (size_t)body_height : 0U);
  if (last_visible > body_lines) {
    last_visible = body_lines;
  }
  if (first_line > 0U) {
    mvaddch(box_top, box_left + 3, ACS_UARROW);
  }
  if (last_visible < body_lines) {
    mvaddch(box_top + box_height - 1, box_left + 3, ACS_DARROW);
  }
  {
    char range[64];

    (void)snprintf(range, sizeof(range), " %lu-%lu/%lu ",
                   body_lines == 0U ? 0UL : (unsigned long)(first_line + 1U),
                   (unsigned long)last_visible, (unsigned long)body_lines);
    range[sizeof(range) - 1U] = '\0';
    confit_tui_curses_add_clipped(box_top,
                                  box_left + box_width - 2 - (int)strlen(range),
                                  range, (int)strlen(range));
  }

  if (LINES > 3) {
    move(LINES - 2, 0);
    clrtoeol();
    confit_tui_curses_style_on(CONFIT_TUI_STYLE_KEY);
    confit_tui_curses_add_clipped(
        LINES - 2, 1, confit_tui_curses_text(key_legend), width - 2);
    confit_tui_curses_style_off(CONFIT_TUI_STYLE_KEY);
  }
  if (LINES > 2) {
    const ConfitTuiCursesStyle status_style =
        confit_tui_curses_status_style(status);

    move(LINES - 1, 0);
    clrtoeol();
    confit_tui_curses_style_on(status_style);
    confit_tui_curses_add_clipped(LINES - 1, 1, confit_tui_curses_text(status),
                                  width - 2);
    confit_tui_curses_style_off(status_style);
  }
  return refresh() == OK ? 0 : -1;
}

ConfitTuiKey confit_tui_curses_read_key(void) {
  const int ch = getch();

  if (ch == ERR || ch == 'q' || ch == 'Q') {
    return CONFIT_TUI_KEY_QUIT;
  }
  if (ch == 27) {
    return CONFIT_TUI_KEY_CANCEL;
  }
  if (ch == KEY_UP || ch == 'k' || ch == 'K') {
    return CONFIT_TUI_KEY_UP;
  }
  if (ch == KEY_DOWN || ch == 'j' || ch == 'J') {
    return CONFIT_TUI_KEY_DOWN;
  }
  if (ch == KEY_PPAGE) {
    return CONFIT_TUI_KEY_PAGE_UP;
  }
  if (ch == KEY_NPAGE) {
    return CONFIT_TUI_KEY_PAGE_DOWN;
  }
  if (ch == KEY_HOME) {
    return CONFIT_TUI_KEY_HOME;
  }
  if (ch == KEY_END) {
    return CONFIT_TUI_KEY_END;
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
  if (ch == 'd' || ch == 'D') {
    return CONFIT_TUI_KEY_DEFAULT;
  }
  if (ch == 't' || ch == 'T') {
    return CONFIT_TUI_KEY_TAG;
  }
  if (ch == 'y' || ch == 'Y') {
    return CONFIT_TUI_KEY_TYPE;
  }
  if (ch == 'x' || ch == 'X') {
    return CONFIT_TUI_KEY_CLEAR_FILTER;
  }
  if (ch == 'n') {
    return CONFIT_TUI_KEY_NEW;
  }
  if (ch == 'N') {
    return CONFIT_TUI_KEY_SEARCH_PREVIOUS;
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
  if (ch == '?') {
    return CONFIT_TUI_KEY_KEYMAP_HELP;
  }
  return CONFIT_TUI_KEY_NONE;
}

size_t confit_tui_curses_page_step(void) {
  if (!confit_tui_curses_started) {
    return 10U;
  }
  if (LINES > 8) {
    return (size_t)(LINES - 8);
  }
  return 1U;
}

int confit_tui_curses_read_line(const char *prompt, char *out,
                                size_t out_size) {
  size_t input_size;
  int row;

  if (out == 0 || out_size == 0U || confit_tui_curses_start() != 0) {
    return -1;
  }
  out[0] = '\0';
  row = LINES > 2 ? LINES - 1 : 0;
  move(row, 1);
  clrtoeol();
  confit_tui_curses_style_on(CONFIT_TUI_STYLE_EDIT);
  confit_tui_curses_add_clipped(row, 2, confit_tui_curses_text(prompt),
                                COLS > 4 ? COLS - 4 : 76);
  confit_tui_curses_style_off(CONFIT_TUI_STYLE_EDIT);
  (void)curs_set(1);
  input_size = 0U;
  out[0] = '\0';
  move(row, 2 + (int)strlen(confit_tui_curses_text(prompt)));
  do {
    const int ch = getch();

    if (ch == ERR) {
      (void)curs_set(0);
      out[0] = '\0';
      return -1;
    }
    if (ch == 27) {
      (void)curs_set(0);
      out[0] = '\0';
      return 1;
    }
    if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
      break;
    }
    if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
      if (input_size > 0U) {
        input_size -= 1U;
        out[input_size] = '\0';
        move(row,
             2 + (int)strlen(confit_tui_curses_text(prompt)) + (int)input_size);
        delch();
      }
      continue;
    }
    if (ch == 21) {
      input_size = 0U;
      out[0] = '\0';
      confit_tui_curses_add_clipped(row, 2, confit_tui_curses_text(prompt),
                                    COLS > 4 ? COLS - 4 : 76);
      move(row, 2 + (int)strlen(confit_tui_curses_text(prompt)));
      clrtoeol();
      continue;
    }
    if (ch >= 0 && ch <= 255 && isprint((unsigned char)ch) &&
        input_size + 1U < out_size) {
      out[input_size] = (char)ch;
      input_size += 1U;
      out[input_size] = '\0';
      addch(ch);
    }
  } while (1);

  (void)curs_set(0);
  out[out_size - 1U] = '\0';
  return 0;
}

static void confit_tui_curses_dialog_rect(int *top, int *left, int *height,
                                          int *width, int preferred_height) {
  const int screen_width = COLS > 0 ? COLS : 80;
  const int screen_height = LINES > 0 ? LINES : 24;

  *width = screen_width > 78 ? 76 : screen_width - 2;
  if (*width < 36) {
    *width = screen_width;
  }
  *height = preferred_height;
  if (*height > screen_height - 2) {
    *height = screen_height - 2;
  }
  if (*height < 7) {
    *height = screen_height;
  }
  *left = screen_width > *width ? (screen_width - *width) / 2 : 0;
  *top = screen_height > *height ? (screen_height - *height) / 2 : 0;
}

static void confit_tui_curses_render_value_dialog(const char *title,
                                                  const char *header,
                                                  const char *prompt,
                                                  const char *input,
                                                  const char *status) {
  int top;
  int left;
  int height;
  int width;
  int row;
  int input_col;
  int input_width;

  erase();
  confit_tui_curses_dialog_rect(&top, &left, &height, &width, 10);
  if (height < 7 || width < 36) {
    confit_tui_curses_add_clipped(0, 0, confit_tui_curses_text(title),
                                  COLS > 0 ? COLS : 80);
    confit_tui_curses_add_clipped(1, 0, confit_tui_curses_text(status),
                                  COLS > 0 ? COLS : 80);
    confit_tui_curses_add_clipped(2, 0, confit_tui_curses_text(prompt),
                                  COLS > 0 ? COLS : 80);
    addnstr(confit_tui_curses_text(input), COLS > 0 ? COLS : 80);
    (void)refresh();
    return;
  }

  confit_tui_curses_style_on(CONFIT_TUI_STYLE_DIALOG);
  confit_tui_curses_draw_box(top, left, height, width, title);
  confit_tui_curses_style_off(CONFIT_TUI_STYLE_DIALOG);

  row = top + 2;
  confit_tui_curses_render_lines(header, &row, top + height - 5, left + 2,
                                 width - 4, CONFIT_TUI_STYLE_HELP);
  {
    const ConfitTuiCursesStyle status_style =
        confit_tui_curses_status_style(status);

    confit_tui_curses_style_on(status_style);
    confit_tui_curses_fill_span(top + height - 3, left + 1, width - 2);
    confit_tui_curses_add_clipped(top + height - 3, left + 2,
                                  confit_tui_curses_text(status), width - 4);
    confit_tui_curses_style_off(status_style);
  }
  input_col = left + 2 + (int)strlen(confit_tui_curses_text(prompt));
  input_width = width - 4 - (int)strlen(confit_tui_curses_text(prompt));
  if (input_width < 1) {
    input_width = 1;
  }
  confit_tui_curses_add_clipped(top + height - 2, left + 2,
                                confit_tui_curses_text(prompt), width - 4);
  confit_tui_curses_style_on(CONFIT_TUI_STYLE_EDIT);
  confit_tui_curses_add_clipped(top + height - 2, input_col,
                                confit_tui_curses_text(input), input_width);
  confit_tui_curses_style_off(CONFIT_TUI_STYLE_EDIT);
  move(top + height - 2,
       input_col + (int)strlen(confit_tui_curses_text(input)));
  (void)refresh();
}

int confit_tui_curses_read_value_dialog(const char *title, const char *header,
                                        const char *prompt,
                                        const char *initial_status,
                                        ConfitTuiInputValidator validator,
                                        void *validator_user, char *out,
                                        size_t out_size) {
  char status[160];
  size_t input_size;

  if (out == 0 || out_size == 0U || confit_tui_curses_start() != 0) {
    return -1;
  }
  out[0] = '\0';
  input_size = 0U;
  (void)snprintf(status, sizeof(status), "%s",
                 confit_tui_curses_text(initial_status));
  status[sizeof(status) - 1U] = '\0';
  (void)curs_set(1);

  do {
    int ch;

    confit_tui_curses_render_value_dialog(title, header, prompt, out, status);
    ch = getch();
    if (ch == ERR) {
      (void)curs_set(0);
      out[0] = '\0';
      return -1;
    }
    if (ch == 27) {
      (void)curs_set(0);
      out[0] = '\0';
      return 1;
    }
    if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
      char message[160];

      message[0] = '\0';
      if (validator == 0 ||
          validator(out, message, sizeof(message), validator_user) == 0) {
        (void)curs_set(0);
        out[out_size - 1U] = '\0';
        return 0;
      }
      (void)snprintf(status, sizeof(status), "%s",
                     message[0] != '\0' ? message : "invalid value");
      status[sizeof(status) - 1U] = '\0';
      input_size = 0U;
      out[0] = '\0';
      continue;
    }
    if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
      if (input_size > 0U) {
        input_size -= 1U;
        out[input_size] = '\0';
      }
      continue;
    }
    if (ch == 21) {
      input_size = 0U;
      out[0] = '\0';
      continue;
    }
    if (ch >= 0 && ch <= 255 && isprint((unsigned char)ch) &&
        input_size + 1U < out_size) {
      out[input_size] = (char)ch;
      input_size += 1U;
      out[input_size] = '\0';
    }
  } while (1);
}

static void confit_tui_curses_render_choice_dialog(
    const char *title, const char *header, const char *const *items,
    size_t item_count, size_t selected_index, const char *status) {
  int top;
  int left;
  int height;
  int width;
  int row;
  int list_top;
  int list_height;
  int header_last;
  size_t first;
  size_t index;

  erase();
  confit_tui_curses_dialog_rect(&top, &left, &height, &width, 14);
  if (height < 8 || width < 36) {
    confit_tui_curses_add_clipped(0, 0, confit_tui_curses_text(title),
                                  COLS > 0 ? COLS : 80);
    for (index = 0U; index < item_count && (int)index + 2 < LINES; ++index) {
      confit_tui_curses_add_clipped((int)index + 2, 0,
                                    confit_tui_curses_text(items[index]),
                                    COLS > 0 ? COLS : 80);
    }
    (void)refresh();
    return;
  }

  confit_tui_curses_style_on(CONFIT_TUI_STYLE_DIALOG);
  confit_tui_curses_draw_box(top, left, height, width, title);
  confit_tui_curses_style_off(CONFIT_TUI_STYLE_DIALOG);

  row = top + 2;
  header_last = top + height - 5 - (int)item_count;
  if (header_last < top + 3) {
    header_last = top + 3;
  }
  if (header_last > top + height - 5) {
    header_last = top + height - 5;
  }
  confit_tui_curses_render_lines(header, &row, header_last, left + 2,
                                 width - 4, CONFIT_TUI_STYLE_HELP);
  list_top = row + 1;
  list_height = top + height - 3 - list_top;
  if (list_height < 1) {
    list_top = top + 4;
    list_height = height - 7;
  }
  first = 0U;
  if (list_height > 0 && selected_index >= (size_t)list_height) {
    first = selected_index - (size_t)list_height + 1U;
  }
  for (index = 0U; index < (size_t)list_height && first + index < item_count;
       ++index) {
    const size_t item_index = first + index;
    char line[256];

    (void)snprintf(line, sizeof(line), "%c %s",
                   item_index == selected_index ? '>' : ' ',
                   confit_tui_curses_text(items[item_index]));
    line[sizeof(line) - 1U] = '\0';
    if (item_index == selected_index) {
      confit_tui_curses_style_on(CONFIT_TUI_STYLE_SELECTION);
    }
    confit_tui_curses_add_clipped(list_top + (int)index, left + 2, line,
                                  width - 4);
    if (item_index == selected_index) {
      confit_tui_curses_style_off(CONFIT_TUI_STYLE_SELECTION);
    }
  }
  {
    const ConfitTuiCursesStyle status_style =
        confit_tui_curses_status_style(status);

    confit_tui_curses_style_on(status_style);
    confit_tui_curses_fill_span(top + height - 2, left + 1, width - 2);
    confit_tui_curses_add_clipped(top + height - 2, left + 2,
                                  confit_tui_curses_text(status), width - 4);
    confit_tui_curses_style_off(status_style);
  }
  (void)refresh();
}

int confit_tui_curses_select_dialog(const char *title, const char *header,
                                    const char *const *items, size_t item_count,
                                    size_t selected_index,
                                    size_t *out_selected_index) {
  char status[128];

  if (items == 0 || item_count == 0U || out_selected_index == 0 ||
      confit_tui_curses_start() != 0) {
    return -1;
  }
  if (selected_index >= item_count) {
    selected_index = 0U;
  }
  (void)snprintf(status, sizeof(status), "Enter selects, Esc cancels");
  status[sizeof(status) - 1U] = '\0';

  do {
    int ch;

    confit_tui_curses_render_choice_dialog(title, header, items, item_count,
                                           selected_index, status);
    ch = getch();
    if (ch == ERR) {
      return -1;
    }
    if (ch == 27 || ch == 'q' || ch == 'Q') {
      return 1;
    }
    if ((ch == KEY_UP || ch == 'k' || ch == 'K') && selected_index > 0U) {
      selected_index -= 1U;
      continue;
    }
    if ((ch == KEY_DOWN || ch == 'j' || ch == 'J') &&
        selected_index + 1U < item_count) {
      selected_index += 1U;
      continue;
    }
    if (ch == KEY_HOME) {
      selected_index = 0U;
      continue;
    }
    if (ch == KEY_END) {
      selected_index = item_count - 1U;
      continue;
    }
    if (ch == KEY_PPAGE) {
      const size_t step = confit_tui_curses_page_step();
      selected_index = selected_index > step ? selected_index - step : 0U;
      continue;
    }
    if (ch == KEY_NPAGE) {
      const size_t step = confit_tui_curses_page_step();
      if (selected_index + step < item_count) {
        selected_index += step;
      } else {
        selected_index = item_count - 1U;
      }
      continue;
    }
    if (ch == '\n' || ch == '\r' || ch == KEY_ENTER || ch == ' ') {
      *out_selected_index = selected_index;
      return 0;
    }
  } while (1);
}
