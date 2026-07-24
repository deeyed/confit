#ifndef CONFIT_LINK_V2_H
#define CONFIT_LINK_V2_H

#include <stddef.h>

#include "confit/expression_v2.h"
#include "confit/schema_v2.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief linked expression이 선언된 semantic role이다. */
typedef enum ConfitV2LinkedExpressionRole {
  CONFIT_V2_LINKED_EXPRESSION_COMPUTED = 1,
  CONFIT_V2_LINKED_EXPRESSION_AVAILABLE_IF,
  CONFIT_V2_LINKED_EXPRESSION_VISIBLE_IF,
  CONFIT_V2_LINKED_EXPRESSION_DEFAULT_WHEN,
  CONFIT_V2_LINKED_EXPRESSION_SUGGESTION_WHEN,
  CONFIT_V2_LINKED_EXPRESSION_MENU_VISIBLE_IF,
  CONFIT_V2_LINKED_EXPRESSION_CHOICE_AVAILABLE_IF,
  CONFIT_V2_LINKED_EXPRESSION_CHOICE_VISIBLE_IF,
  CONFIT_V2_LINKED_EXPRESSION_CHOICE_DEFAULT_WHEN,
  CONFIT_V2_LINKED_EXPRESSION_CONSTRAINT_WHEN,
  CONFIT_V2_LINKED_EXPRESSION_CONSTRAINT_REQUIRE,
} ConfitV2LinkedExpressionRole;

/** @brief one AST reference와 canonical v2 symbol의 immutable link다. */
typedef struct ConfitV2LinkedReference {
  const ConfitV2ExpressionNode *node;
  const ConfitV2Symbol *symbol;
} ConfitV2LinkedReference;

/** @brief parse/typecheck/link를 마친 expression record다. */
typedef struct ConfitV2LinkedExpression {
  ConfitV2LinkedExpressionRole role;
  /** source project가 소유하는 option/menu/choice/constraint id. */
  const char *owner_id;
  const ConfitV2Expression *expression;
  const ConfitV2TypedExpression *typed;
  const ConfitV2LinkedReference *references;
  size_t reference_count;
} ConfitV2LinkedExpression;

/** @brief immutable linked v2 project handle이다. */
typedef struct ConfitV2LinkedProject ConfitV2LinkedProject;

/** @brief requested assignment가 들어온 writer lane이다. */
typedef enum ConfitV2AssignmentWriter {
  CONFIT_V2_ASSIGNMENT_WRITER_SCHEMA = 1,
  CONFIT_V2_ASSIGNMENT_WRITER_PROFILE,
  CONFIT_V2_ASSIGNMENT_WRITER_TARGET,
  CONFIT_V2_ASSIGNMENT_WRITER_USER,
  CONFIT_V2_ASSIGNMENT_WRITER_COMPUTED,
} ConfitV2AssignmentWriter;

/** @brief assignment ownership만 검증하기 위한 source record다. */
typedef struct ConfitV2WriteRequest {
  const char *option_id;
  ConfitV2AssignmentWriter writer;
  int is_unset;
  ConfitV2SourceSpan span;
} ConfitV2WriteRequest;

/**
 * @brief parsed v2 project를 canonical symbols와 typed expressions로 link한다.
 *
 * `project`는 caller가 linked handle보다 오래 유지해야 한다. Linker는 project
 * namespace, duplicate canonical symbol, expression reference, expected result
 * type을 hard validate하며 host state를 직접 읽지 않는다.
 */
ConfitStatus confit_v2_schema_link_project(
    const ConfitV2Project *project, ConfitV2LinkedProject **out_linked,
    ConfitDiagnostic *diagnostic);

/** @brief linked project의 owned AST/type/reference records를 해제한다. */
void confit_v2_linked_project_free(ConfitV2LinkedProject *linked);

/** @brief linked project가 borrow하는 parsed v2 project를 반환한다. */
const ConfitV2Project *confit_v2_linked_project_source(
    const ConfitV2LinkedProject *linked);

/** @brief canonical option id lexical order의 linked symbol 개수다. */
size_t confit_v2_linked_project_symbol_count(const ConfitV2LinkedProject *linked);

/** @brief canonical option id로 linked symbol을 찾는다. */
const ConfitV2Symbol *confit_v2_linked_project_find_symbol(
    const ConfitV2LinkedProject *linked, const char *id);

/** @brief linked expression record 수를 반환한다. */
size_t confit_v2_linked_project_expression_count(
    const ConfitV2LinkedProject *linked);

/** @brief index 순서 linked expression record를 반환한다. */
const ConfitV2LinkedExpression *confit_v2_linked_project_expression_at(
    const ConfitV2LinkedProject *linked, size_t index);

/**
 * @brief one requested assignment의 write-domain ownership을 검증한다.
 *
 * Profile/target/user/computed assignment loader는 value precedence를 처리하기
 * 전에 이 API를 호출한다. 이 단계는 value type/range 또는 ledger ordering을
 * 결정하지 않는다.
 */
ConfitStatus confit_v2_linked_project_validate_write(
    const ConfitV2LinkedProject *linked, const ConfitV2WriteRequest *request,
    ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_LINK_V2_H */
