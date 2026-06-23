#include "tui_internal.h"

#include <curses.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>

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

void confit_tui_curses_stop(void) {
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

static void confit_tui_curses_render_subtitle(const char *subtitle, int *row) {
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
                                            char *buffer, size_t buffer_size) {
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

int confit_tui_curses_render(const ConfitTuiScreen *screen) {
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
      const char *marker =
          confit_tui_curses_marker(item, marker_buffer, sizeof(marker_buffer));
      const int selected = item_index == screen->selected_index;

      (void)snprintf(line, sizeof(line), "%c %-10s %-36s  %s",
                     selected ? '>' : ' ', marker,
                     confit_tui_curses_text(item->label),
                     confit_tui_curses_text(item->detail));
      line[sizeof(line) - 1U] = '\0';
      if (selected) {
        attron(A_REVERSE);
      }
      confit_tui_curses_add_clipped(list_top + (int)index, 2, line, width - 4);
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
    confit_tui_curses_add_clipped(
        LINES - 1, 2, confit_tui_curses_text(screen->status), width - 4);
    if (has_colors()) {
      attroff(COLOR_PAIR(1));
    } else {
      attroff(A_REVERSE);
    }
  }

  return refresh() == OK ? 0 : -1;
}

ConfitTuiKey confit_tui_curses_read_key(void) {
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

int confit_tui_curses_read_line(const char *prompt, char *out,
                                size_t out_size) {
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
