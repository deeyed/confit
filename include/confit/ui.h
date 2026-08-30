#ifndef CONFIT_UI_H
#define CONFIT_UI_H

#include <stddef.h>

#include "confit/diagnostic.h"
#include "confit/expression.h"
#include "confit/model.h"
#include "confit/resolver.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The UI history is intentionally bounded and independent of terminal size. */
#define CONFIT_UI_HISTORY_LIMIT ((size_t)128U)

typedef struct ConfitUiModel ConfitUiModel;

typedef enum ConfitUiState {
  CONFIT_UI_NORMAL = 0,
  CONFIT_UI_EDIT,
  CONFIT_UI_SEARCH,
  CONFIT_UI_COMMAND,
  CONFIT_UI_HELP,
  CONFIT_UI_DIFF,
  CONFIT_UI_ENUM_PICKER,
} ConfitUiState;

typedef enum ConfitUiRowKind {
  CONFIT_UI_ROW_MENU = 1,
  CONFIT_UI_ROW_CONFIG,
} ConfitUiRowKind;

typedef enum ConfitUiAction {
  CONFIT_UI_ACTION_NEXT = 1,
  CONFIT_UI_ACTION_PREVIOUS,
  CONFIT_UI_ACTION_PARENT,
  CONFIT_UI_ACTION_OPEN,
  CONFIT_UI_ACTION_TOGGLE,
  CONFIT_UI_ACTION_BEGIN_EDIT,
  CONFIT_UI_ACTION_BEGIN_SEARCH,
  CONFIT_UI_ACTION_SEARCH_NEXT,
  CONFIT_UI_ACTION_SEARCH_PREVIOUS,
  CONFIT_UI_ACTION_UNDO,
  CONFIT_UI_ACTION_REDO,
  CONFIT_UI_ACTION_SHOW_DIFF,
  CONFIT_UI_ACTION_SHOW_HELP,
  CONFIT_UI_ACTION_BEGIN_COMMAND,
  CONFIT_UI_ACTION_CANCEL,
  CONFIT_UI_ACTION_QUIT_HINT,
} ConfitUiAction;

/** Side effects requested from, but never performed by, the UI model. */
typedef enum ConfitUiEffect {
  CONFIT_UI_EFFECT_NONE = 0,
  CONFIT_UI_EFFECT_REQUEST_SAVE,
  CONFIT_UI_EFFECT_EXIT,
  CONFIT_UI_EFFECT_DISCARD_AND_EXIT,
} ConfitUiEffect;

typedef struct ConfitUiRowView {
  ConfitUiRowKind kind;
  size_t depth;
  size_t menu_index;
  size_t config_index;
  const char *prompt;
  const char *help;
  const char *symbol;
  ConfitValueKind value_kind;
  const ConfitValue *effective_value;
  const ConfitValue *default_value;
  ConfitValueOrigin origin;
  int available;
  int changed;
  int has_range;
  const ConfitValue *range_minimum;
  const ConfitValue *range_maximum;
  const char *const *enum_values;
  size_t enum_value_count;
  const char *dependency_text;
  const char *choice_group;
  const char *reason_detail;
} ConfitUiRowView;

typedef struct ConfitUiDiffView {
  size_t config_index;
  const char *symbol;
  const ConfitValue *saved_value;
  const ConfitValue *working_value;
} ConfitUiDiffView;

/**
 * Construct a terminal-independent working model.
 *
 * The model borrows catalog and plan, which must outlive it. Assignments are
 * copied and normalized to semantic values. No file, terminal, or process API
 * is used. On failure `*out_model` remains null.
 */
ConfitStatus
confit_ui_create(const ConfitCatalog *catalog, const ConfitDependencyPlan *plan,
                 const ConfitAssignment *assignments, size_t assignment_count,
                 const ConfitAllocator *allocator, ConfitUiModel **out_model,
                 ConfitDiagnostic *diagnostic);
void confit_ui_destroy(ConfitUiModel *model);

ConfitUiState confit_ui_state(const ConfitUiModel *model);
int confit_ui_dirty(const ConfitUiModel *model);
int confit_ui_show_unavailable(const ConfitUiModel *model);
const char *confit_ui_notice(const ConfitUiModel *model);

size_t confit_ui_row_count(const ConfitUiModel *model);
size_t confit_ui_cursor(const ConfitUiModel *model);
size_t confit_ui_viewport_offset(const ConfitUiModel *model);
size_t confit_ui_viewport_rows(const ConfitUiModel *model);
ConfitStatus confit_ui_set_viewport_rows(ConfitUiModel *model, size_t rows,
                                         ConfitDiagnostic *diagnostic);
int confit_ui_row_at(const ConfitUiModel *model, size_t row_index,
                     ConfitUiRowView *out_view);

size_t confit_ui_diff_count(const ConfitUiModel *model);
int confit_ui_diff_at(const ConfitUiModel *model, size_t diff_index,
                      ConfitUiDiffView *out_view);

/** Apply one semantic action. Keyboard decoding belongs to the frontend. */
ConfitStatus confit_ui_action(ConfitUiModel *model, ConfitUiAction action,
                              ConfitUiEffect *out_effect,
                              ConfitDiagnostic *diagnostic);

/** Replace the bounded candidate text in EDIT, SEARCH, or COMMAND state. */
ConfitStatus confit_ui_set_input(ConfitUiModel *model, const char *text,
                                 size_t text_size,
                                 ConfitDiagnostic *diagnostic);
const char *confit_ui_input(const ConfitUiModel *model, size_t *out_size);

/** Accept the current edit, search query, command, or enum selection. */
ConfitStatus confit_ui_accept(ConfitUiModel *model, ConfitUiEffect *out_effect,
                              ConfitDiagnostic *diagnostic);

/** Select one enum-domain member without accepting it. */
ConfitStatus confit_ui_enum_select(ConfitUiModel *model, size_t value_index,
                                   ConfitDiagnostic *diagnostic);
size_t confit_ui_enum_selection(const ConfitUiModel *model);

/**
 * Complete a previously requested save.
 *
 * Success advances the saved baseline and emits EXIT only for :wq or dirty
 * :x. Failure preserves all working values and dirty state.
 */
ConfitStatus confit_ui_save_result(ConfitUiModel *model, int succeeded,
                                   ConfitUiEffect *out_effect,
                                   ConfitDiagnostic *diagnostic);

/** Borrow the current immutable resolution until the next successful edit. */
const ConfitResolution *confit_ui_resolution(const ConfitUiModel *model);

/** Deterministic terminal-free semantic view for tests and controllers. */
ConfitStatus confit_ui_format_view(const ConfitUiModel *model, char *buffer,
                                   size_t buffer_size, size_t *out_size,
                                   ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_UI_H */
