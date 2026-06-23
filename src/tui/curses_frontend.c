#include "tui_internal.h"

#include <ctype.h>
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
                                           int color_pair, int fallback_attr) {
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
    if (has_colors()) {
      attron(COLOR_PAIR(color_pair));
    } else if (fallback_attr != 0) {
      attron(fallback_attr);
    }
    move(*row, col);
    addnstr(line_begin, line_size < (size_t)width ? (int)line_size : width);
    if (has_colors()) {
      attroff(COLOR_PAIR(color_pair));
    } else if (fallback_attr != 0) {
      attroff(fallback_attr);
    }
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

static void confit_tui_curses_render_row(int row, int col, int width,
                                         const ConfitTuiListItem *item,
                                         int selected) {
  char marker_buffer[80];
  char label_buffer[192];
  char line[512];
  const char *marker =
      confit_tui_curses_marker(item, marker_buffer, sizeof(marker_buffer));
  const unsigned depth = item != 0 ? item->depth : 0U;
  const int indent = depth > 8U ? 16 : (int)(depth * 2U);

  (void)snprintf(label_buffer, sizeof(label_buffer), "%*s%s", indent, "",
                 confit_tui_curses_text(item != 0 ? item->label : 0));
  label_buffer[sizeof(label_buffer) - 1U] = '\0';

  if (item != 0 && item->is_heading) {
    (void)snprintf(line, sizeof(line), "%c %-4s %-28s %s", selected ? '>' : ' ',
                   marker, label_buffer, confit_tui_curses_text(item->detail));
  } else {
    (void)snprintf(line, sizeof(line), "%c %-10s %-38s %s",
                   selected ? '>' : ' ', marker, label_buffer,
                   confit_tui_curses_text(item != 0 ? item->detail : 0));
  }
  line[sizeof(line) - 1U] = '\0';
  if (selected) {
    attron(A_REVERSE);
  }
  if (item != 0 && item->is_disabled) {
    attron(A_DIM);
  }
  if (item != 0 && item->is_heading) {
    attron(A_BOLD);
  }
  confit_tui_curses_add_clipped(row, col, line, width);
  if (item != 0 && item->is_heading) {
    attroff(A_BOLD);
  }
  if (item != 0 && item->is_disabled) {
    attroff(A_DIM);
  }
  if (selected) {
    attroff(A_REVERSE);
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
  if (LINES > 1) {
    confit_tui_curses_add_clipped(1, 0, confit_tui_curses_text(screen->status),
                                  width);
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
                                 item_index == screen->selected_index);
  }
  if (screen->item_count == 0U && LINES > 2) {
    confit_tui_curses_add_clipped(2, 0, "(no options)", width);
  }
  if (LINES > 1) {
    if (has_colors()) {
      attron(COLOR_PAIR(1));
    } else {
      attron(A_REVERSE);
    }
    move(LINES - 1, 0);
    clrtoeol();
    confit_tui_curses_add_clipped(
        LINES - 1, 0, confit_tui_curses_text(screen->key_legend), width);
    if (has_colors()) {
      attroff(COLOR_PAIR(1));
    } else {
      attroff(A_REVERSE);
    }
  }
}

int confit_tui_curses_render(const ConfitTuiScreen *screen) {
  size_t index;
  size_t first;
  size_t visible_count;
  size_t last_visible;
  int header_row;
  int menu_top;
  int menu_left;
  int menu_width;
  int menu_height;
  int interior_top;
  int interior_height;
  int max_visible;
  int width;

  if (screen == 0 || confit_tui_curses_start() != 0) {
    return -1;
  }

  erase();
  width = COLS > 0 ? COLS : 80;
  if (LINES < 9 || width < 40) {
    confit_tui_curses_render_compact(screen, width);
    return refresh() == OK ? 0 : -1;
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

  header_row = 1;
  confit_tui_curses_render_lines(screen->header, &header_row, LINES - 7, 2,
                                 width > 4 ? width - 4 : width, 2, A_BOLD);

  menu_top = header_row + 1;
  menu_left = 1;
  menu_width = width > 2 ? width - 2 : width;
  menu_height = LINES - menu_top - 3;
  if (menu_height < 3) {
    confit_tui_curses_render_compact(screen, width);
    return refresh() == OK ? 0 : -1;
  }

  if (has_colors()) {
    attron(COLOR_PAIR(3) | A_BOLD);
  } else {
    attron(A_BOLD);
  }
  confit_tui_curses_draw_box(menu_top, menu_left, menu_height, menu_width,
                             "Menu");
  if (has_colors()) {
    attroff(COLOR_PAIR(3) | A_BOLD);
  } else {
    attroff(A_BOLD);
  }

  interior_top = menu_top + 1;
  interior_height = menu_height - 2;
  max_visible = interior_height > 0 ? interior_height : 0;

  if (screen->item_count == 0U || max_visible <= 0) {
    confit_tui_curses_add_clipped(interior_top, menu_left + 2, "(no options)",
                                  menu_width - 4);
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
      const int selected = item_index == screen->selected_index;

      confit_tui_curses_render_row(interior_top + (int)index, menu_left + 2,
                                   menu_width - 4, item, selected);
    }

    last_visible = first + visible_count;
    if (first > 0U) {
      mvaddch(menu_top, menu_left + 3, ACS_UARROW);
      mvaddch(menu_top, menu_left + 4, ACS_UARROW);
      mvaddch(menu_top, menu_left + 5, ACS_UARROW);
    }
    if (last_visible < screen->item_count) {
      mvaddch(menu_top + menu_height - 1, menu_left + 3, ACS_DARROW);
      mvaddch(menu_top + menu_height - 1, menu_left + 4, ACS_DARROW);
      mvaddch(menu_top + menu_height - 1, menu_left + 5, ACS_DARROW);
    }
    {
      char range[64];

      (void)snprintf(range, sizeof(range), " %lu-%lu/%lu ",
                     (unsigned long)(first + 1U), (unsigned long)last_visible,
                     (unsigned long)screen->item_count);
      range[sizeof(range) - 1U] = '\0';
      confit_tui_curses_add_clipped(
          menu_top, menu_left + menu_width - 2 - (int)strlen(range), range,
          (int)strlen(range));
    }
  }

  if (LINES > 3) {
    move(LINES - 2, 0);
    clrtoeol();
    if (has_colors()) {
      attron(COLOR_PAIR(1));
    } else {
      attron(A_REVERSE);
    }
    confit_tui_curses_add_clipped(
        LINES - 2, 1, confit_tui_curses_text(screen->key_legend), width - 2);
    if (has_colors()) {
      attroff(COLOR_PAIR(1));
    } else {
      attroff(A_REVERSE);
    }
  }

  if (LINES > 2) {
    move(LINES - 1, 0);
    clrtoeol();
    confit_tui_curses_add_clipped(
        LINES - 1, 1, confit_tui_curses_text(screen->status), width - 2);
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
  if (has_colors()) {
    attron(COLOR_PAIR(3) | A_BOLD);
  } else {
    attron(A_BOLD);
  }
  confit_tui_curses_add_centered(0, confit_tui_curses_text(title));
  if (has_colors()) {
    attroff(COLOR_PAIR(3) | A_BOLD);
  } else {
    attroff(A_BOLD);
  }

  if (LINES < 9 || width < 40) {
    if (LINES > 1) {
      confit_tui_curses_add_clipped(1, 0, confit_tui_curses_text(status),
                                    width);
    }
    confit_tui_curses_render_text_body(body, 2, 0, LINES > 4 ? LINES - 4 : 0,
                                       width, first_line);
    if (LINES > 1) {
      if (has_colors()) {
        attron(COLOR_PAIR(1));
      } else {
        attron(A_REVERSE);
      }
      move(LINES - 1, 0);
      clrtoeol();
      confit_tui_curses_add_clipped(LINES - 1, 0,
                                    confit_tui_curses_text(key_legend), width);
      if (has_colors()) {
        attroff(COLOR_PAIR(1));
      } else {
        attroff(A_REVERSE);
      }
    }
    return refresh() == OK ? 0 : -1;
  }

  header_row = 1;
  confit_tui_curses_render_lines(header, &header_row, LINES - 7, 2,
                                 width > 4 ? width - 4 : width, 2, A_BOLD);
  box_top = header_row + 1;
  box_left = 1;
  box_width = width > 2 ? width - 2 : width;
  box_height = LINES - box_top - 3;
  if (box_height < 3) {
    confit_tui_curses_render_text_body(
        body, header_row + 1, 1, LINES - header_row - 3, width - 2, first_line);
    return refresh() == OK ? 0 : -1;
  }

  if (has_colors()) {
    attron(COLOR_PAIR(3) | A_BOLD);
  } else {
    attron(A_BOLD);
  }
  confit_tui_curses_draw_box(box_top, box_left, box_height, box_width, "Help");
  if (has_colors()) {
    attroff(COLOR_PAIR(3) | A_BOLD);
  } else {
    attroff(A_BOLD);
  }

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
    if (has_colors()) {
      attron(COLOR_PAIR(1));
    } else {
      attron(A_REVERSE);
    }
    confit_tui_curses_add_clipped(
        LINES - 2, 1, confit_tui_curses_text(key_legend), width - 2);
    if (has_colors()) {
      attroff(COLOR_PAIR(1));
    } else {
      attroff(A_REVERSE);
    }
  }
  if (LINES > 2) {
    move(LINES - 1, 0);
    clrtoeol();
    confit_tui_curses_add_clipped(LINES - 1, 1, confit_tui_curses_text(status),
                                  width - 2);
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

  if (has_colors()) {
    attron(COLOR_PAIR(3) | A_BOLD);
  } else {
    attron(A_BOLD);
  }
  confit_tui_curses_draw_box(top, left, height, width, title);
  if (has_colors()) {
    attroff(COLOR_PAIR(3) | A_BOLD);
  } else {
    attroff(A_BOLD);
  }

  row = top + 2;
  confit_tui_curses_render_lines(header, &row, top + height - 5, left + 2,
                                 width - 4, 0, A_NORMAL);
  if (has_colors()) {
    attron(COLOR_PAIR(1));
  } else {
    attron(A_REVERSE);
  }
  confit_tui_curses_fill_span(top + height - 3, left + 1, width - 2);
  confit_tui_curses_add_clipped(top + height - 3, left + 2,
                                confit_tui_curses_text(status), width - 4);
  if (has_colors()) {
    attroff(COLOR_PAIR(1));
  } else {
    attroff(A_REVERSE);
  }
  input_col = left + 2 + (int)strlen(confit_tui_curses_text(prompt));
  input_width = width - 4 - (int)strlen(confit_tui_curses_text(prompt));
  if (input_width < 1) {
    input_width = 1;
  }
  confit_tui_curses_add_clipped(top + height - 2, left + 2,
                                confit_tui_curses_text(prompt), width - 4);
  confit_tui_curses_add_clipped(top + height - 2, input_col,
                                confit_tui_curses_text(input), input_width);
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

  if (has_colors()) {
    attron(COLOR_PAIR(3) | A_BOLD);
  } else {
    attron(A_BOLD);
  }
  confit_tui_curses_draw_box(top, left, height, width, title);
  if (has_colors()) {
    attroff(COLOR_PAIR(3) | A_BOLD);
  } else {
    attroff(A_BOLD);
  }

  row = top + 2;
  confit_tui_curses_render_lines(header, &row, top + 3, left + 2, width - 4, 0,
                                 A_NORMAL);
  list_top = top + 4;
  list_height = height - 7;
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
      attron(A_REVERSE);
    }
    confit_tui_curses_add_clipped(list_top + (int)index, left + 2, line,
                                  width - 4);
    if (item_index == selected_index) {
      attroff(A_REVERSE);
    }
  }
  if (has_colors()) {
    attron(COLOR_PAIR(1));
  } else {
    attron(A_REVERSE);
  }
  confit_tui_curses_fill_span(top + height - 2, left + 1, width - 2);
  confit_tui_curses_add_clipped(top + height - 2, left + 2,
                                confit_tui_curses_text(status), width - 4);
  if (has_colors()) {
    attroff(COLOR_PAIR(1));
  } else {
    attroff(A_REVERSE);
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
