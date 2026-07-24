#ifndef CONFIT_CONSTRAINT_V2_H
#define CONFIT_CONSTRAINT_V2_H

#include <stddef.h>

#include "confit/diagnostic.h"
#include "confit/link_v2.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief v2 compile 단계가 독립적으로 보존하는 edge graph 종류다. */
typedef enum ConfitV2CompiledGraphKind {
  CONFIT_V2_COMPILED_GRAPH_EVALUATION = 1,
  CONFIT_V2_COMPILED_GRAPH_VISIBILITY,
  CONFIT_V2_COMPILED_GRAPH_CHOICE,
  CONFIT_V2_COMPILED_GRAPH_CONSTRAINT,
} ConfitV2CompiledGraphKind;

/** @brief graph edge의 source owner, target symbol, source 위치다. */
typedef struct ConfitV2CompiledGraphEdge {
  const char *owner_id;
  const ConfitV2Symbol *target;
  const ConfitV2SourceSpan *span;
} ConfitV2CompiledGraphEdge;

/** @brief 서로 섞이지 않는 one semantic graph다. */
typedef struct ConfitV2CompiledGraph {
  ConfitV2CompiledGraphKind kind;
  ConfitV2CompiledGraphEdge *edges;
  size_t edge_count;
} ConfitV2CompiledGraph;

/** @brief parent link와 linked visibility를 가진 compiled menu node다. */
typedef struct ConfitV2CompiledMenu {
  const ConfitV2MenuNode *source;
  const struct ConfitV2CompiledMenu *parent;
  const ConfitV2LinkedExpression *visible_if;
} ConfitV2CompiledMenu;

/** @brief menu의 별도 read-only option 표시다. */
typedef struct ConfitV2CompiledMenuReference {
  const ConfitV2MenuReference *source;
  const ConfitV2CompiledMenu *menu;
  const ConfitV2Symbol *symbol;
} ConfitV2CompiledMenuReference;

/** @brief member link와 conditional rule을 가진 compiled choice다. */
typedef struct ConfitV2CompiledChoice {
  const ConfitV2Choice *source;
  const ConfitV2Symbol **members;
  size_t member_count;
  const ConfitV2LinkedExpression *available_if;
  const ConfitV2LinkedExpression *visible_if;
  const ConfitV2LinkedExpression **default_when;
  size_t default_count;
} ConfitV2CompiledChoice;

/** @brief named constraint의 linked bool expressions다. */
typedef struct ConfitV2CompiledConstraint {
  const ConfitV2Constraint *source;
  const ConfitV2LinkedExpression *when;
  const ConfitV2LinkedExpression *require;
} ConfitV2CompiledConstraint;

/** @brief source/link lifetime을 borrow하는 immutable structure compile handle이다. */
typedef struct ConfitV2CompiledStructure ConfitV2CompiledStructure;

/** @brief final effective value를 constraint evaluator에 전달하는 binding이다. */
typedef struct ConfitV2ConstraintBinding {
  const ConfitV2Symbol *symbol;
  /** unset이면 NULL이다. payload의 lifetime은 report보다 길어야 한다. */
  const ConfitV2Value *value;
  int is_set;
  const char *source_path;
  size_t source_line;
  size_t source_column;
} ConfitV2ConstraintBinding;

/** @brief one expression reference가 읽은 effective value와 causal source다. */
typedef struct ConfitV2ConstraintRead {
  const ConfitV2Symbol *symbol;
  const ConfitV2Value *value;
  int is_set;
  const char *source_path;
  size_t source_line;
  size_t source_column;
  const char *expression_path;
  size_t expression_line;
  size_t expression_column;
} ConfitV2ConstraintRead;

/** @brief named constraint의 final outcome이다. */
typedef enum ConfitV2ConstraintOutcome {
  CONFIT_V2_CONSTRAINT_NOT_APPLICABLE = 0,
  CONFIT_V2_CONSTRAINT_PASSED,
  CONFIT_V2_CONSTRAINT_FAILED,
} ConfitV2ConstraintOutcome;

/** @brief one named constraint의 immutable runtime result다. */
typedef struct ConfitV2ConstraintResult {
  const ConfitV2CompiledConstraint *constraint;
  ConfitV2ConstraintOutcome outcome;
  ConfitV2ConstraintRead *reads;
  size_t read_count;
  ConfitDiagnosticRelatedSpan *related;
  size_t related_count;
} ConfitV2ConstraintResult;

/** @brief suggestion의 runtime state다. resolver는 어느 state도 자동 적용하지 않는다. */
typedef enum ConfitV2SuggestionState {
  CONFIT_V2_SUGGESTION_NOT_APPLICABLE = 0,
  CONFIT_V2_SUGGESTION_SATISFIED,
  CONFIT_V2_SUGGESTION_APPLICABLE,
  CONFIT_V2_SUGGESTION_CONFLICTING,
} ConfitV2SuggestionState;

/** @brief final context에서 평가한 one suggestion이다. */
typedef struct ConfitV2SuggestionResult {
  const ConfitV2Symbol *symbol;
  const ConfitV2Suggestion *suggestion;
  ConfitV2SuggestionState state;
} ConfitV2SuggestionResult;

/** @brief complete named-constraint/suggestion validation report다. */
typedef struct ConfitV2ConstraintReport ConfitV2ConstraintReport;

/**
 * @brief linked v2 project의 menu/choice/constraint 구조를 compile한다.
 *
 * Menu parent/order/reference, choice member/cardinality/default, named
 * constraint source를 hard validate한다. Evaluation, visibility, choice,
 * constraint edge는 같은 container나 cycle policy를 공유하지 않는다. `linked`와
 * 그 source project는 반환 handle보다 오래 유지해야 한다.
 */
ConfitStatus confit_v2_compile_structure(
    const ConfitV2LinkedProject *linked, ConfitV2CompiledStructure **out_compiled,
    ConfitDiagnostic *diagnostic);

/** @brief compiled structure가 소유한 index와 graph edge array를 해제한다. */
void confit_v2_compiled_structure_free(ConfitV2CompiledStructure *compiled);

/** @brief compiled structure가 borrow하는 linked project를 반환한다. */
const ConfitV2LinkedProject *confit_v2_compiled_structure_source(
    const ConfitV2CompiledStructure *compiled);

/** @brief canonical menu id lexical order의 compiled menu 개수다. */
size_t confit_v2_compiled_structure_menu_count(
    const ConfitV2CompiledStructure *compiled);

/** @brief canonical menu id lexical index의 menu를 반환한다. */
const ConfitV2CompiledMenu *confit_v2_compiled_structure_menu_at(
    const ConfitV2CompiledStructure *compiled, size_t index);

/** @brief canonical menu id로 compiled menu를 찾는다. */
const ConfitV2CompiledMenu *confit_v2_compiled_structure_find_menu(
    const ConfitV2CompiledStructure *compiled, const char *id);

/** @brief read-only menu reference 개수다. */
size_t confit_v2_compiled_structure_menu_reference_count(
    const ConfitV2CompiledStructure *compiled);

/** @brief source declaration lexical order의 read-only menu reference를 반환한다. */
const ConfitV2CompiledMenuReference *
confit_v2_compiled_structure_menu_reference_at(
    const ConfitV2CompiledStructure *compiled, size_t index);

/** @brief canonical choice id lexical order의 compiled choice 개수다. */
size_t confit_v2_compiled_structure_choice_count(
    const ConfitV2CompiledStructure *compiled);

/** @brief canonical choice id lexical index의 choice를 반환한다. */
const ConfitV2CompiledChoice *confit_v2_compiled_structure_choice_at(
    const ConfitV2CompiledStructure *compiled, size_t index);

/** @brief canonical constraint id lexical order의 constraint 개수다. */
size_t confit_v2_compiled_structure_constraint_count(
    const ConfitV2CompiledStructure *compiled);

/** @brief canonical constraint id lexical index의 constraint를 반환한다. */
const ConfitV2CompiledConstraint *confit_v2_compiled_structure_constraint_at(
    const ConfitV2CompiledStructure *compiled, size_t index);

/** @brief requested semantic graph를 반환한다. 반환 graph는 compiled handle이 소유한다. */
const ConfitV2CompiledGraph *confit_v2_compiled_structure_graph(
    const ConfitV2CompiledStructure *compiled, ConfitV2CompiledGraphKind kind);

/**
 * @brief final effective context에서 named constraint와 suggestion을 평가한다.
 *
 * Constraint failure가 있으면 `CONFIT_ERR_SCHEMA`를 반환하지만 `out_report`에는
 * 모든 deterministic failure와 suggestion state를 계속 반환한다. 이 API는 value를
 * 변경하거나 suggestion을 적용하지 않는다.
 */
ConfitStatus confit_v2_constraint_validate(
    const ConfitV2CompiledStructure *compiled,
    const ConfitV2ConstraintBinding *bindings, size_t binding_count,
    ConfitV2ConstraintReport **out_report, ConfitDiagnostic *diagnostic);

/** @brief report와 report가 소유한 read/related record를 해제한다. */
void confit_v2_constraint_report_free(ConfitV2ConstraintReport *report);

/** @brief report가 borrow하는 compiled structure를 반환한다. */
const ConfitV2CompiledStructure *confit_v2_constraint_report_source(
    const ConfitV2ConstraintReport *report);

/** @brief source span/id 정렬 결과의 constraint record 개수다. */
size_t confit_v2_constraint_report_result_count(
    const ConfitV2ConstraintReport *report);

/** @brief sorted constraint runtime result를 반환한다. */
const ConfitV2ConstraintResult *confit_v2_constraint_report_result_at(
    const ConfitV2ConstraintReport *report, size_t index);

/** @brief final failure 수를 반환한다. */
size_t confit_v2_constraint_report_failure_count(
    const ConfitV2ConstraintReport *report);

/** @brief canonical option id와 declaration order의 suggestion result 수다. */
size_t confit_v2_constraint_report_suggestion_count(
    const ConfitV2ConstraintReport *report);

/** @brief suggestion result를 반환한다. */
const ConfitV2SuggestionResult *confit_v2_constraint_report_suggestion_at(
    const ConfitV2ConstraintReport *report, size_t index);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_CONSTRAINT_V2_H */
