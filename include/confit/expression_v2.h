#ifndef CONFIT_EXPRESSION_V2_H
#define CONFIT_EXPRESSION_V2_H

#include <stddef.h>
#include <stdint.h>

#include "confit/schema_v2.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief v2 expression parser가 허용하는 resource 상한이다. */
typedef struct ConfitV2ExpressionLimits {
  /** UTF-8 expression source의 최대 byte 수. 0이면 default를 사용한다. */
  size_t max_source_bytes;
  /** lexer가 생산할 최대 token 수. 0이면 default를 사용한다. */
  size_t max_tokens;
  /** AST node 최대 개수. 0이면 default를 사용한다. */
  size_t max_nodes;
  /** recursive grouping/list/call/unary nesting 최대값. 0이면 default를 사용한다. */
  size_t max_nesting;
} ConfitV2ExpressionLimits;

/** @brief expression literal node의 payload kind다. */
typedef enum ConfitV2ExpressionLiteralKind {
  CONFIT_V2_EXPRESSION_LITERAL_BOOL = 1,
  CONFIT_V2_EXPRESSION_LITERAL_TRISTATE,
  CONFIT_V2_EXPRESSION_LITERAL_INT,
  CONFIT_V2_EXPRESSION_LITERAL_HEX,
  CONFIT_V2_EXPRESSION_LITERAL_FLOAT,
  CONFIT_V2_EXPRESSION_LITERAL_STRING,
} ConfitV2ExpressionLiteralKind;

/** @brief expression unary operator다. */
typedef enum ConfitV2ExpressionUnaryOperator {
  CONFIT_V2_EXPRESSION_UNARY_NOT = 1,
  CONFIT_V2_EXPRESSION_UNARY_NEGATE,
  CONFIT_V2_EXPRESSION_UNARY_PLUS,
} ConfitV2ExpressionUnaryOperator;

/** @brief expression binary operator다. */
typedef enum ConfitV2ExpressionBinaryOperator {
  CONFIT_V2_EXPRESSION_BINARY_OR = 1,
  CONFIT_V2_EXPRESSION_BINARY_AND,
  CONFIT_V2_EXPRESSION_BINARY_EQUAL,
  CONFIT_V2_EXPRESSION_BINARY_NOT_EQUAL,
  CONFIT_V2_EXPRESSION_BINARY_LESS,
  CONFIT_V2_EXPRESSION_BINARY_LESS_EQUAL,
  CONFIT_V2_EXPRESSION_BINARY_GREATER,
  CONFIT_V2_EXPRESSION_BINARY_GREATER_EQUAL,
  CONFIT_V2_EXPRESSION_BINARY_IN,
  CONFIT_V2_EXPRESSION_BINARY_ADD,
  CONFIT_V2_EXPRESSION_BINARY_SUBTRACT,
  CONFIT_V2_EXPRESSION_BINARY_MULTIPLY,
  CONFIT_V2_EXPRESSION_BINARY_DIVIDE,
  CONFIT_V2_EXPRESSION_BINARY_MODULO,
} ConfitV2ExpressionBinaryOperator;

/** @brief v2 expression AST node 종류다. */
typedef enum ConfitV2ExpressionNodeKind {
  CONFIT_V2_EXPRESSION_NODE_LITERAL = 1,
  CONFIT_V2_EXPRESSION_NODE_REFERENCE,
  CONFIT_V2_EXPRESSION_NODE_UNARY,
  CONFIT_V2_EXPRESSION_NODE_BINARY,
  CONFIT_V2_EXPRESSION_NODE_CONDITIONAL,
  CONFIT_V2_EXPRESSION_NODE_CALL,
  CONFIT_V2_EXPRESSION_NODE_LIST,
} ConfitV2ExpressionNodeKind;

/** @brief expression AST node다. 모든 string/child allocation은 expression이 소유한다. */
typedef struct ConfitV2ExpressionNode ConfitV2ExpressionNode;

struct ConfitV2ExpressionNode {
  /** node kind. */
  ConfitV2ExpressionNodeKind kind;
  /** expression string 내부 0-based 시작 byte offset. */
  size_t start_offset;
  /** expression string 내부 exclusive end byte offset. */
  size_t end_offset;
  union {
    struct {
      ConfitV2ExpressionLiteralKind kind;
      union {
        int bool_value;
        char tristate_value;
        int64_t int_value;
        uint64_t hex_value;
        double float_value;
        char *string_value;
      } value;
    } literal;
    struct {
      char *option_id;
    } reference;
    struct {
      ConfitV2ExpressionUnaryOperator operator_kind;
      ConfitV2ExpressionNode *operand;
    } unary;
    struct {
      ConfitV2ExpressionBinaryOperator operator_kind;
      ConfitV2ExpressionNode *left;
      ConfitV2ExpressionNode *right;
    } binary;
    struct {
      ConfitV2ExpressionNode *condition;
      ConfitV2ExpressionNode *when_true;
      ConfitV2ExpressionNode *when_false;
    } conditional;
    struct {
      char *function_name;
      ConfitV2ExpressionNode **arguments;
      size_t argument_count;
    } call;
    struct {
      ConfitV2ExpressionNode **items;
      size_t item_count;
    } list;
  } as;
};

/**
 * @brief parsed expression과 AST ownership root다.
 *
 * `source`와 모든 AST subnode는 expression이 소유한다. `source_span`은 source
 * TOML value의 path/line/column을 별도로 복사해 original v2 project model이
 * 해제된 뒤에도 diagnostic origin을 보존한다.
 */
typedef struct ConfitV2Expression {
  char *source;
  ConfitV2SourceSpan source_span;
  ConfitV2ExpressionNode *root;
} ConfitV2Expression;

/** @brief parser default resource limit을 반환한다. */
ConfitV2ExpressionLimits confit_v2_expression_default_limits(void);

/**
 * @brief TOML expression text를 independent AST로 parse한다.
 *
 * Parser는 option reference의 존재나 operator type을 검사하지 않는다. syntax와
 * resource limit만 검사하며, 실패 diagnostic은 `source->span`의 source location에
 * expression-local offset을 합성해 기록한다.
 *
 * @param source v2 model이 보유한 expression text/source span.
 * @param limits optional resource limit. NULL 또는 0 field는 default를 사용한다.
 * @param out_expression 성공 시 caller-owned parsed AST output.
 * @param diagnostic syntax/resource 실패 위치와 원인.
 * @return 성공하면 CONFIT_OK, syntax 오류면 CONFIT_ERR_SCHEMA.
 */
ConfitStatus confit_v2_expression_parse(
    const ConfitV2ExpressionText *source,
    const ConfitV2ExpressionLimits *limits, ConfitV2Expression **out_expression,
    ConfitDiagnostic *diagnostic);

/** @brief expression AST와 source ownership tree를 해제한다. NULL은 허용한다. */
void confit_v2_expression_free(ConfitV2Expression *expression);

/**
 * @brief AST를 deterministic S-expression으로 직렬화한다.
 *
 * 테스트와 diagnostic tooling을 위한 canonical form이며 evaluation output이 아니다.
 * 반환 문자열은 caller가 `confit_v2_expression_string_free()`로 해제한다.
 */
ConfitStatus confit_v2_expression_to_sexpr(const ConfitV2Expression *expression,
                                            char **out_text);

/** @brief `confit_v2_expression_to_sexpr()` 반환 문자열을 해제한다. */
void confit_v2_expression_string_free(char *text);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_EXPRESSION_V2_H */
