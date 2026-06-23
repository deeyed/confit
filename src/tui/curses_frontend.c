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
  if (item != 0 && item->is_heading) {
    attron(A_BOLD);
  }
  confit_tui_curses_add_clipped(row, col, line, width);
  if (item != 0 && item->is_heading) {
    attroff(A_BOLD);
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
