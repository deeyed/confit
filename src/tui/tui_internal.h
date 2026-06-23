#ifndef CONFIT_TUI_INTERNAL_H
#define CONFIT_TUI_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "confit/diagnostic.h"
#include "confit/model.h"
#include "confit/status.h"
#include "confit/tui.h"

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
  CONFIT_TUI_KEY_PAGE_UP = 16,
  CONFIT_TUI_KEY_PAGE_DOWN = 17,
  CONFIT_TUI_KEY_HOME = 18,
  CONFIT_TUI_KEY_END = 19,
  CONFIT_TUI_KEY_CANCEL = 20,
  CONFIT_TUI_KEY_KEYMAP_HELP = 21,
  CONFIT_TUI_KEY_SEARCH_PREVIOUS = 22,
} ConfitTuiKey;

typedef struct ConfitTuiListItem {
  const char *label;
  const char *detail;
  const char *value;
  unsigned depth;
  int is_heading;
  int expanded;
  int is_disabled;
} ConfitTuiListItem;

typedef struct ConfitTuiScreen {
  const char *title;
  const char *header;
  const char *key_legend;
  const ConfitTuiListItem *items;
  size_t item_count;
  size_t selected_index;
  const char *status;
} ConfitTuiScreen;

typedef struct ConfitTuiTextBuilder {
  char *text;
  size_t size;
  size_t capacity;
} ConfitTuiTextBuilder;

typedef int (*ConfitTuiInputValidator)(const char *text, char *message,
                                       size_t message_size, void *user);

const char *confit_tui_text_or_dash(const char *text);

ConfitStatus confit_tui_parse_int64(const char *text, int64_t *out);
ConfitStatus confit_tui_parse_uint64(const char *text, uint64_t *out);
ConfitStatus confit_tui_parse_double(const char *text, double *out);

void confit_tui_text_builder_init(ConfitTuiTextBuilder *builder);
ConfitStatus confit_tui_text_append(ConfitTuiTextBuilder *builder,
                                    const char *text);
ConfitStatus confit_tui_text_append_char(ConfitTuiTextBuilder *builder,
                                         char value);
ConfitStatus confit_tui_text_append_quoted(ConfitTuiTextBuilder *builder,
                                           const char *text);
ConfitStatus confit_tui_text_append_value(ConfitTuiTextBuilder *builder,
                                          const ConfitOption *option,
                                          const ConfitValue *value);

int confit_tui_curses_render(const ConfitTuiScreen *screen);
int confit_tui_curses_render_text(const char *title, const char *header,
                                  const char *body, const char *key_legend,
                                  const char *status, size_t first_line);
ConfitTuiKey confit_tui_curses_read_key(void);
size_t confit_tui_curses_page_step(void);
int confit_tui_curses_read_line(const char *prompt, char *out, size_t out_size);
int confit_tui_curses_read_value_dialog(const char *title, const char *header,
                                        const char *prompt,
                                        const char *initial_status,
                                        ConfitTuiInputValidator validator,
                                        void *validator_user, char *out,
                                        size_t out_size);
int confit_tui_curses_select_dialog(const char *title, const char *header,
                                    const char *const *items, size_t item_count,
                                    size_t selected_index,
                                    size_t *out_selected_index);
void confit_tui_curses_stop(void);

ConfitStatus confit_tui_run_profile_editor(const ConfitTuiOptions *options,
                                           ConfitDiagnostic *diagnostic);
ConfitStatus confit_tui_run_schema_editor(const ConfitTuiOptions *options,
                                          ConfitDiagnostic *diagnostic);

#endif /* CONFIT_TUI_INTERNAL_H */
