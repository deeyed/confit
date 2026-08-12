#ifndef CONFIT_WORKFLOW_V5_H
#define CONFIT_WORKFLOW_V5_H

#include <stddef.h>

#include "confit/config_v5.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIT_V5_WORKFLOW_TEXT_MAX (256U * 1024U)
#define CONFIT_V5_WORKFLOW_QUERY_MAX 127U

typedef enum ConfitV5ValueOrigin {
  CONFIT_V5_VALUE_ORIGIN_INVALID = 0,
  CONFIT_V5_VALUE_ORIGIN_DEFAULT,
  CONFIT_V5_VALUE_ORIGIN_USER,
  CONFIT_V5_VALUE_ORIGIN_DERIVED,
} ConfitV5ValueOrigin;

typedef struct ConfitV5WorkflowRow {
  ConfitV5OptionView option;
  const char *value;
  ConfitV5ValueOrigin origin;
  int available;
} ConfitV5WorkflowRow;

typedef struct ConfitV5Workflow ConfitV5Workflow;

typedef enum ConfitV5TuiActionKind {
  CONFIT_V5_TUI_ACTION_INVALID = 0,
  CONFIT_V5_TUI_ACTION_NONE,
  CONFIT_V5_TUI_ACTION_UP,
  CONFIT_V5_TUI_ACTION_DOWN,
  CONFIT_V5_TUI_ACTION_SEARCH,
  CONFIT_V5_TUI_ACTION_SET,
  CONFIT_V5_TUI_ACTION_PREVIEW,
  CONFIT_V5_TUI_ACTION_APPLY,
  CONFIT_V5_TUI_ACTION_CANCEL,
  CONFIT_V5_TUI_ACTION_HELP,
} ConfitV5TuiActionKind;

typedef struct ConfitV5TuiAction {
  ConfitV5TuiActionKind kind;
  char symbol[128];
  char value[513];
} ConfitV5TuiAction;

/** @brief Config v5 catalog와 동일 resolver 결과를 소유하는 UX model을 연다. */
ConfitStatus confit_v5_workflow_open(const ConfitV5CatalogRequest *request,
                                     ConfitV5Workflow **out_workflow,
                                     ConfitDiagnostic *diagnostic);
void confit_v5_workflow_free(ConfitV5Workflow *workflow);

size_t confit_v5_workflow_row_count(const ConfitV5Workflow *workflow);
int confit_v5_workflow_row(const ConfitV5Workflow *workflow, size_t index,
                           ConfitV5WorkflowRow *out_row);
/** @brief bounded ASCII case-insensitive search의 n번째 결과 row index를 반환한다. */
int confit_v5_workflow_search(const ConfitV5Workflow *workflow,
                              const char *query, size_t match_index,
                              size_t *out_row_index);
/** @brief option 값을 바꾸고 동일 resolver로 전체 graph를 다시 평가한다. */
ConfitStatus confit_v5_workflow_set(ConfitV5Workflow *workflow,
                                    const char *symbol, const char *value,
                                    ConfitDiagnostic *diagnostic);
/** @brief default와 다른 명시 선택만 담은 deterministic KERNCONF를 만든다. */
ConfitStatus confit_v5_workflow_minimal(const ConfitV5Workflow *workflow,
                                        char **out_text, size_t *out_size,
                                        ConfitDiagnostic *diagnostic);
/** @brief option metadata와 동일 reason graph를 bounded text로 직렬화한다. */
ConfitStatus confit_v5_workflow_explain(const ConfitV5Workflow *workflow,
                                        const char *symbol, int only_blockers,
                                        char **out_text, size_t *out_size,
                                        ConfitDiagnostic *diagnostic);
/** @brief shallow menu/rows/fixed detail pane를 terminal-independent text로 렌더한다. */
ConfitStatus confit_v5_workflow_render(const ConfitV5Workflow *workflow,
                                       size_t selected_row,
                                       const char *query, size_t width,
                                       size_t height, char **out_text,
                                       size_t *out_size,
                                       ConfitDiagnostic *diagnostic);
/** @brief terminal byte string을 arbitrary command 없이 closed TUI action으로 낮춘다. */
ConfitStatus confit_v5_tui_decode(const char *input,
                                  ConfitV5TuiAction *out_action,
                                  ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_WORKFLOW_V5_H */
