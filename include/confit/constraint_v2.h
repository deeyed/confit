#ifndef CONFIT_CONSTRAINT_V2_H
#define CONFIT_CONSTRAINT_V2_H

#include <stddef.h>

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

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_CONSTRAINT_V2_H */
