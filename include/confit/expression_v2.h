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

/** @brief expression static type의 종류다. */
typedef enum ConfitV2ExpressionTypeKind {
  CONFIT_V2_EXPRESSION_TYPE_INVALID = 0,
  CONFIT_V2_EXPRESSION_TYPE_BOOL,
  CONFIT_V2_EXPRESSION_TYPE_TRISTATE,
  CONFIT_V2_EXPRESSION_TYPE_INT,
  CONFIT_V2_EXPRESSION_TYPE_UINT,
  CONFIT_V2_EXPRESSION_TYPE_FLOAT,
  CONFIT_V2_EXPRESSION_TYPE_STRING,
  CONFIT_V2_EXPRESSION_TYPE_ENUM,
  CONFIT_V2_EXPRESSION_TYPE_PATH,
  CONFIT_V2_EXPRESSION_TYPE_COLLECTION,
} ConfitV2ExpressionTypeKind;

/**
 * @brief expression static type이다.
 *
 * enum identity는 `enum_id`로 구분한다. collection의 element identity는
 * `element_enum_id`에 둔다. 두 pointer의 ownership은 binding environment가
 * 소유하며 typecheck/evaluate 호출 동안 유효해야 한다.
 */
typedef struct ConfitV2ExpressionType {
  ConfitV2ExpressionTypeKind kind;
  ConfitV2ExpressionTypeKind element_kind;
  const char *enum_id;
  const char *element_enum_id;
} ConfitV2ExpressionType;

/** @brief one option reference의 static type과 effective value binding이다. */
typedef struct ConfitV2ExpressionBinding {
  const char *id;
  ConfitV2ExpressionType type;
  /** NULL 또는 UNSET이면 unset reference다. non-NULL payload는 caller가 소유한다. */
  const ConfitV2Value *value;
} ConfitV2ExpressionBinding;

/**
 * @brief expression semantic pass의 explicit input environment다.
 *
 * filesystem, environment variable, clock 같은 host state를 읽지 않는다. v2
 * linker는 후속 단계에서 canonical symbol table을 이 형식으로 제공한다.
 */
typedef struct ConfitV2ExpressionEnvironment {
  const ConfitV2ExpressionBinding *bindings;
  size_t binding_count;
} ConfitV2ExpressionEnvironment;

/** @brief immutable AST node와 그 static type을 묶은 entry다. */
typedef struct ConfitV2ExpressionTypeInfo {
  const ConfitV2ExpressionNode *node;
  ConfitV2ExpressionType type;
} ConfitV2ExpressionTypeInfo;

/** @brief typecheck 결과다. AST와 environment의 ownership을 가져오지 않는다. */
typedef struct ConfitV2TypedExpression {
  const ConfitV2Expression *expression;
  ConfitV2ExpressionTypeInfo *nodes;
  size_t node_count;
  ConfitV2ExpressionType root_type;
} ConfitV2TypedExpression;

/** @brief evaluator의 caller-owned typed value다. */
typedef struct ConfitV2ExpressionValue {
  ConfitV2ExpressionType type;
  int is_set;
  ConfitV2Value value;
} ConfitV2ExpressionValue;

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

/** @brief option type을 expression type으로 deterministic하게 변환한다. */
ConfitV2ExpressionType confit_v2_expression_type_from_option_type(
    ConfitV2OptionType option_type, const char *enum_id);

/** @brief 두 expression type이 implicit conversion 없이 같은지 검사한다. */
int confit_v2_expression_type_equal(const ConfitV2ExpressionType *left,
                                    const ConfitV2ExpressionType *right);

/** @brief static type의 canonical diagnostic name을 반환한다. */
const char *confit_v2_expression_type_name(const ConfitV2ExpressionType *type);

/**
 * @brief parsed AST를 explicit binding type으로 검사한다.
 *
 * Reference 존재, operator matrix, builtin signature, conditional/list type을
 * 검사한다. value를 읽거나 평가하지 않는다. 실패 위치는 문제 AST node의 local
 * source span을 가리킨다.
 */
ConfitStatus confit_v2_expression_type_check(
    const ConfitV2Expression *expression,
    const ConfitV2ExpressionEnvironment *environment,
    ConfitV2TypedExpression **out_typed, ConfitDiagnostic *diagnostic);

/** @brief typecheck 결과를 해제한다. NULL은 허용한다. */
void confit_v2_typed_expression_free(ConfitV2TypedExpression *typed);

/** @brief typecheck 결과에서 특정 AST node의 static type을 조회한다. */
const ConfitV2ExpressionType *confit_v2_typed_expression_node_type(
    const ConfitV2TypedExpression *typed,
    const ConfitV2ExpressionNode *node);

/**
 * @brief typechecked expression을 explicit value binding으로 평가한다.
 *
 * Evaluation은 deterministic하며 filesystem/environment/clock/locale을 읽지
 * 않는다. unset access, overflow, zero division, non-finite float는 hard
 * diagnostic이다. out_value의 owned payload는 clear API로 해제한다.
 */
ConfitStatus confit_v2_expression_evaluate(
    const ConfitV2TypedExpression *typed,
    const ConfitV2ExpressionEnvironment *environment,
    ConfitV2ExpressionValue *out_value, ConfitDiagnostic *diagnostic);

/** @brief evaluator가 만든 owned string/list payload를 해제한다. */
void confit_v2_expression_value_clear(ConfitV2ExpressionValue *value);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_EXPRESSION_V2_H */
