#ifndef CONFIT_VENDOR_TUI_H
#define CONFIT_VENDOR_TUI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum CftuiKey {
  CFTUI_KEY_NONE = 0,
  CFTUI_KEY_QUIT = 1,
  CFTUI_KEY_UP = 2,
  CFTUI_KEY_DOWN = 3,
  CFTUI_KEY_ENTER = 4,
  CFTUI_KEY_EDIT = 5,
  CFTUI_KEY_SAVE = 6,
  CFTUI_KEY_SEARCH = 7,
  CFTUI_KEY_CATEGORY = 8,
  CFTUI_KEY_TAG = 9,
  CFTUI_KEY_CLEAR_FILTER = 10,
  CFTUI_KEY_NEW = 11,
  CFTUI_KEY_PROMPT = 12,
  CFTUI_KEY_HELP = 13,
  CFTUI_KEY_RANGE = 14,
  CFTUI_KEY_CHOICES = 15,
} CftuiKey;

typedef struct CftuiListItem {
  const char *label;
  const char *detail;
  const char *value;
} CftuiListItem;

typedef struct CftuiScreen {
  const char *title;
  const char *subtitle;
  const CftuiListItem *items;
  size_t item_count;
  size_t selected_index;
  const char *status;
} CftuiScreen;

int cftui_render(const CftuiScreen *screen);
CftuiKey cftui_read_key(void);
int cftui_read_line(const char *prompt, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_VENDOR_TUI_H */
