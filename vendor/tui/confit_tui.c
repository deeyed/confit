#include "confit_tui.h"

#include <stdio.h>

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
