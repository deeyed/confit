#include "confit_tui.h"

#include <stdio.h>
#include <string.h>

static const char *cftui_text_or_empty(const char *text) {
  return text != 0 ? text : "";
}

int cftui_render(const CftuiScreen *screen) {
  size_t index;

  if (screen == 0) {
    return -1;
  }

  if (fputs("\n== ", stdout) == EOF ||
      fputs(cftui_text_or_empty(screen->title), stdout) == EOF ||
      fputs(" ==\n", stdout) == EOF) {
    return -1;
  }
  if (screen->subtitle != 0 && screen->subtitle[0] != '\0') {
    if (fputs(screen->subtitle, stdout) == EOF || fputs("\n", stdout) == EOF) {
      return -1;
    }
  }

  if (screen->item_count == 0U) {
    if (fputs("  no options\n", stdout) == EOF) {
      return -1;
    }
  }
  for (index = 0U; index < screen->item_count; ++index) {
    const CftuiListItem *item = &screen->items[index];
    const char marker = index == screen->selected_index ? '>' : ' ';

    if (fputc(marker, stdout) == EOF || fputs(" ", stdout) == EOF ||
        fputs(cftui_text_or_empty(item->label), stdout) == EOF ||
        fputs(" = ", stdout) == EOF ||
        fputs(cftui_text_or_empty(item->value), stdout) == EOF ||
        fputs("\n", stdout) == EOF) {
      return -1;
    }
    if (item->detail != 0 && item->detail[0] != '\0') {
      if (fputs("    ", stdout) == EOF || fputs(item->detail, stdout) == EOF ||
          fputs("\n", stdout) == EOF) {
        return -1;
      }
    }
  }

  if (fputs("[status] ", stdout) == EOF ||
      fputs(cftui_text_or_empty(screen->status), stdout) == EOF ||
      fputs("\n", stdout) == EOF) {
    return -1;
  }
  return fflush(stdout) == 0 ? 0 : -1;
}

CftuiKey cftui_read_key(void) {
  int ch;

  ch = getchar();
  if (ch == EOF || ch == 'q' || ch == 'Q') {
    return CFTUI_KEY_QUIT;
  }
  if (ch == 'k' || ch == 'K') {
    return CFTUI_KEY_UP;
  }
  if (ch == 'j' || ch == 'J') {
    return CFTUI_KEY_DOWN;
  }
  if (ch == '\n' || ch == '\r') {
    return CFTUI_KEY_ENTER;
  }
  if (ch == 'e' || ch == 'E') {
    return CFTUI_KEY_EDIT;
  }
  if (ch == 's' || ch == 'S') {
    return CFTUI_KEY_SAVE;
  }
  if (ch == '/') {
    return CFTUI_KEY_SEARCH;
  }
  if (ch == 'c' || ch == 'C') {
    return CFTUI_KEY_CATEGORY;
  }
  if (ch == 't' || ch == 'T') {
    return CFTUI_KEY_TAG;
  }
  if (ch == 'x' || ch == 'X') {
    return CFTUI_KEY_CLEAR_FILTER;
  }
  if (ch == 27) {
    const int bracket = getchar();
    const int arrow = bracket == '[' ? getchar() : EOF;

    if (arrow == 'A') {
      return CFTUI_KEY_UP;
    }
    if (arrow == 'B') {
      return CFTUI_KEY_DOWN;
    }
  }
  return CFTUI_KEY_NONE;
}

int cftui_read_line(const char *prompt, char *out, size_t out_size) {
  size_t size;

  if (out == 0 || out_size == 0U) {
    return -1;
  }
  out[0] = '\0';

  if (prompt != 0 && prompt[0] != '\0') {
    if (fputs(prompt, stdout) == EOF || fflush(stdout) != 0) {
      return -1;
    }
  }
  if (fgets(out, (int)out_size, stdin) == 0) {
    return -1;
  }

  size = strlen(out);
  while (size > 0U && (out[size - 1U] == '\n' || out[size - 1U] == '\r')) {
    out[size - 1U] = '\0';
    size -= 1U;
  }
  return 0;
}
