#include "confit/expression_v2.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lexer_internal.h"

typedef struct ConfitV2EvaluateContext {
  const ConfitV2TypedExpression *typed;
  const ConfitV2ExpressionEnvironment *environment;
  ConfitDiagnostic *diagnostic;
} ConfitV2EvaluateContext;

static const char kInvalidEvaluateArgument[] = "invalid typed expression evaluator argument";
static const char kMissingBinding[] = "runtime expression binding is missing";
static const char kInvalidBindingValue[] = "expression binding value has invalid type";
static const char kUnsetValue[] = "expression value is unset";
static const char kAllocationFailed[] = "expression evaluation allocation failed";
static const char kIntegerOverflow[] = "expression integer overflow";
static const char kUnsignedOverflow[] = "expression unsigned overflow";
static const char kUnsignedUnderflow[] = "expression unsigned underflow";
static const char kDivisionByZero[] = "expression division by zero";
static const char kNonFiniteFloat[] = "expression float result is non-finite";
static const char kInvalidRuntimeType[] = "expression runtime type is invalid";
static const char kUnsupportedValue[] = "expression evaluator received unsupported value";

static void confit_v2_evaluate_diagnostic(
    const ConfitV2EvaluateContext *context,
    const ConfitV2ExpressionNode *node, ConfitStatus status,
    const char *message) {
  ConfitV2ExpressionText source;

  memset(&source, 0, sizeof(source));
  source.text = context->typed->expression->source;
  source.span = context->typed->expression->source_span;
  confit_v2_expression_diagnostic_at(&source,
                                     node != 0 ? node->start_offset : 0U,
                                     status, message, context->diagnostic);
}

void confit_v2_expression_value_clear(ConfitV2ExpressionValue *value) {
  size_t index;

  if (value == 0) {
    return;
  }
  if (value->value.kind == CONFIT_V2_VALUE_STRING) {
    free(value->value.as.string_value);
  } else if (value->value.kind == CONFIT_V2_VALUE_STRING_LIST) {
    for (index = 0U; index < value->value.as.string_list.count; ++index) {
      free(value->value.as.string_list.items[index]);
    }
    free(value->value.as.string_list.items);
  }
  memset(value, 0, sizeof(*value));
}

static const ConfitV2ExpressionBinding *confit_v2_evaluate_find_binding(
    const ConfitV2ExpressionEnvironment *environment, const char *id) {
  size_t index;

  if (environment == 0) {
    return 0;
  }
  for (index = 0U; index < environment->binding_count; ++index) {
    if (environment->bindings[index].id != 0 &&
        strcmp(environment->bindings[index].id, id) == 0) {
      return &environment->bindings[index];
    }
  }
  return 0;
}

static char *confit_v2_evaluate_copy_text(const char *text) {
  const size_t size = strlen(text);
  char *copy = (char *)malloc(size + 1U);

  if (copy != 0) {
    memcpy(copy, text, size + 1U);
  }
  return copy;
}

static int confit_v2_evaluate_tristate_is_valid(char value) {
  return value == 'n' || value == 'm' || value == 'y';
}

static void confit_v2_evaluate_set_bool(ConfitV2ExpressionValue *out,
                                         int value) {
  memset(out, 0, sizeof(*out));
  out->type.kind = CONFIT_V2_EXPRESSION_TYPE_BOOL;
  out->is_set = 1;
  out->value.kind = CONFIT_V2_VALUE_BOOL;
  out->value.as.bool_value = value != 0;
}

static int confit_v2_evaluate_type_value_matches(
    const ConfitV2ExpressionType *type, const ConfitV2Value *value) {
  size_t index;

  if (value == 0 || value->kind == CONFIT_V2_VALUE_UNSET) {
    return 1;
  }
  switch (type->kind) {
  case CONFIT_V2_EXPRESSION_TYPE_BOOL:
    return value->kind == CONFIT_V2_VALUE_BOOL &&
           (value->as.bool_value == 0 || value->as.bool_value == 1);
  case CONFIT_V2_EXPRESSION_TYPE_TRISTATE:
    return value->kind == CONFIT_V2_VALUE_TRISTATE &&
           confit_v2_evaluate_tristate_is_valid(value->as.tristate_value);
  case CONFIT_V2_EXPRESSION_TYPE_INT:
    return value->kind == CONFIT_V2_VALUE_INT;
  case CONFIT_V2_EXPRESSION_TYPE_UINT:
    return value->kind == CONFIT_V2_VALUE_UINT;
  case CONFIT_V2_EXPRESSION_TYPE_FLOAT:
    return value->kind == CONFIT_V2_VALUE_FLOAT && isfinite(value->as.float_value);
  case CONFIT_V2_EXPRESSION_TYPE_STRING:
  case CONFIT_V2_EXPRESSION_TYPE_ENUM:
  case CONFIT_V2_EXPRESSION_TYPE_PATH:
    return value->kind == CONFIT_V2_VALUE_STRING && value->as.string_value != 0;
  case CONFIT_V2_EXPRESSION_TYPE_COLLECTION:
    if (value->kind != CONFIT_V2_VALUE_STRING_LIST ||
        (value->as.string_list.count > 0U && value->as.string_list.items == 0)) {
      return 0;
    }
    for (index = 0U; index < value->as.string_list.count; ++index) {
      if (value->as.string_list.items[index] == 0) {
        return 0;
      }
    }
    return 1;
  default:
    return 0;
  }
}

static ConfitStatus confit_v2_evaluate_copy_bound_value(
    const ConfitV2EvaluateContext *context, const ConfitV2ExpressionNode *node,
    const ConfitV2ExpressionType *type, const ConfitV2Value *input,
    ConfitV2ExpressionValue *out) {
  size_t index;

  memset(out, 0, sizeof(*out));
  out->type = *type;
  if (input == 0 || input->kind == CONFIT_V2_VALUE_UNSET) {
    return CONFIT_OK;
  }
  if (!confit_v2_evaluate_type_value_matches(type, input)) {
    confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                  kInvalidBindingValue);
    return CONFIT_ERR_SCHEMA;
  }
  out->is_set = 1;
  out->value.kind = input->kind;
  switch (input->kind) {
  case CONFIT_V2_VALUE_BOOL:
    out->value.as.bool_value = input->as.bool_value;
    break;
  case CONFIT_V2_VALUE_TRISTATE:
    out->value.as.tristate_value = input->as.tristate_value;
    break;
  case CONFIT_V2_VALUE_INT:
    out->value.as.int_value = input->as.int_value;
    break;
  case CONFIT_V2_VALUE_UINT:
    out->value.as.uint_value = input->as.uint_value;
    break;
  case CONFIT_V2_VALUE_FLOAT:
    out->value.as.float_value = input->as.float_value;
    break;
  case CONFIT_V2_VALUE_STRING:
    out->value.as.string_value =
        confit_v2_evaluate_copy_text(input->as.string_value);
    if (out->value.as.string_value == 0) {
      confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_INTERNAL,
                                    kAllocationFailed);
      confit_v2_expression_value_clear(out);
      return CONFIT_ERR_INTERNAL;
    }
    break;
  case CONFIT_V2_VALUE_STRING_LIST:
    out->value.as.string_list.count = input->as.string_list.count;
    if (input->as.string_list.count == 0U) {
      break;
    }
    out->value.as.string_list.items = (char **)calloc(
        input->as.string_list.count, sizeof(*out->value.as.string_list.items));
    if (out->value.as.string_list.items == 0) {
      confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_INTERNAL,
                                    kAllocationFailed);
      confit_v2_expression_value_clear(out);
      return CONFIT_ERR_INTERNAL;
    }
    for (index = 0U; index < input->as.string_list.count; ++index) {
      out->value.as.string_list.items[index] =
          confit_v2_evaluate_copy_text(input->as.string_list.items[index]);
      if (out->value.as.string_list.items[index] == 0) {
        confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_INTERNAL,
                                      kAllocationFailed);
        confit_v2_expression_value_clear(out);
        return CONFIT_ERR_INTERNAL;
      }
    }
    break;
  default:
    confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                  kUnsupportedValue);
    confit_v2_expression_value_clear(out);
    return CONFIT_ERR_SCHEMA;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_evaluate_require_set(
    const ConfitV2EvaluateContext *context, const ConfitV2ExpressionNode *node,
    const ConfitV2ExpressionValue *value) {
  if (value->is_set) {
    return CONFIT_OK;
  }
  confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA, kUnsetValue);
  return CONFIT_ERR_SCHEMA;
}

static int confit_v2_evaluate_values_equal(
    const ConfitV2ExpressionValue *left, const ConfitV2ExpressionValue *right) {
  size_t index;

  switch (left->type.kind) {
  case CONFIT_V2_EXPRESSION_TYPE_BOOL:
    return left->value.as.bool_value == right->value.as.bool_value;
  case CONFIT_V2_EXPRESSION_TYPE_TRISTATE:
    return left->value.as.tristate_value == right->value.as.tristate_value;
  case CONFIT_V2_EXPRESSION_TYPE_INT:
    return left->value.as.int_value == right->value.as.int_value;
  case CONFIT_V2_EXPRESSION_TYPE_UINT:
    return left->value.as.uint_value == right->value.as.uint_value;
  case CONFIT_V2_EXPRESSION_TYPE_FLOAT:
    return left->value.as.float_value == right->value.as.float_value;
  case CONFIT_V2_EXPRESSION_TYPE_STRING:
  case CONFIT_V2_EXPRESSION_TYPE_ENUM:
  case CONFIT_V2_EXPRESSION_TYPE_PATH:
    return strcmp(left->value.as.string_value, right->value.as.string_value) == 0;
  case CONFIT_V2_EXPRESSION_TYPE_COLLECTION:
    if (left->value.as.string_list.count != right->value.as.string_list.count) {
      return 0;
    }
    for (index = 0U; index < left->value.as.string_list.count; ++index) {
      if (strcmp(left->value.as.string_list.items[index],
                 right->value.as.string_list.items[index]) != 0) {
        return 0;
      }
    }
    return 1;
  default:
    return 0;
  }
}

static int confit_v2_evaluate_compare(const ConfitV2ExpressionValue *left,
                                      const ConfitV2ExpressionValue *right) {
  switch (left->type.kind) {
  case CONFIT_V2_EXPRESSION_TYPE_INT:
    return left->value.as.int_value < right->value.as.int_value
               ? -1
               : left->value.as.int_value > right->value.as.int_value;
  case CONFIT_V2_EXPRESSION_TYPE_UINT:
    return left->value.as.uint_value < right->value.as.uint_value
               ? -1
               : left->value.as.uint_value > right->value.as.uint_value;
  case CONFIT_V2_EXPRESSION_TYPE_FLOAT:
    return left->value.as.float_value < right->value.as.float_value
               ? -1
               : left->value.as.float_value > right->value.as.float_value;
  case CONFIT_V2_EXPRESSION_TYPE_STRING:
  case CONFIT_V2_EXPRESSION_TYPE_PATH:
    return strcmp(left->value.as.string_value, right->value.as.string_value);
  default:
    return 0;
  }
}

static int confit_v2_evaluate_int_add(int64_t left, int64_t right, int64_t *out) {
  if ((right > 0 && left > INT64_MAX - right) ||
      (right < 0 && left < INT64_MIN - right)) {
    return 0;
  }
  *out = left + right;
  return 1;
}

static int confit_v2_evaluate_int_subtract(int64_t left, int64_t right,
                                            int64_t *out) {
  if ((right < 0 && left > INT64_MAX + right) ||
      (right > 0 && left < INT64_MIN + right)) {
    return 0;
  }
  *out = left - right;
  return 1;
}

static int confit_v2_evaluate_int_multiply(int64_t left, int64_t right,
                                            int64_t *out) {
  if (left == 0 || right == 0) {
    *out = 0;
    return 1;
  }
  if ((left == INT64_MIN && right == -1) ||
      (right == INT64_MIN && left == -1)) {
    return 0;
  }
  if (left > 0) {
    if (right > 0 ? left > INT64_MAX / right : right < INT64_MIN / left) {
      return 0;
    }
  } else if (right > 0) {
    if (left < INT64_MIN / right) {
      return 0;
    }
  } else if (left < INT64_MAX / right) {
    return 0;
  }
  *out = left * right;
  return 1;
}

static ConfitStatus confit_v2_evaluate_node(
    const ConfitV2EvaluateContext *context, const ConfitV2ExpressionNode *node,
    ConfitV2ExpressionValue *out);

static ConfitStatus confit_v2_evaluate_literal(
    const ConfitV2EvaluateContext *context, const ConfitV2ExpressionNode *node,
    ConfitV2ExpressionValue *out) {
  const ConfitV2ExpressionType *type =
      confit_v2_typed_expression_node_type(context->typed, node);

  if (type == 0) {
    confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_INTERNAL,
                                  kInvalidRuntimeType);
    return CONFIT_ERR_INTERNAL;
  }
  memset(out, 0, sizeof(*out));
  out->type = *type;
  out->is_set = 1;
  switch (node->as.literal.kind) {
  case CONFIT_V2_EXPRESSION_LITERAL_BOOL:
    out->value.kind = CONFIT_V2_VALUE_BOOL;
    out->value.as.bool_value = node->as.literal.value.bool_value;
    return CONFIT_OK;
  case CONFIT_V2_EXPRESSION_LITERAL_TRISTATE:
    out->value.kind = CONFIT_V2_VALUE_TRISTATE;
    out->value.as.tristate_value = node->as.literal.value.tristate_value;
    return CONFIT_OK;
  case CONFIT_V2_EXPRESSION_LITERAL_INT:
    out->value.kind = CONFIT_V2_VALUE_INT;
    out->value.as.int_value = node->as.literal.value.int_value;
    return CONFIT_OK;
  case CONFIT_V2_EXPRESSION_LITERAL_HEX:
    out->value.kind = CONFIT_V2_VALUE_UINT;
    out->value.as.uint_value = node->as.literal.value.hex_value;
    return CONFIT_OK;
  case CONFIT_V2_EXPRESSION_LITERAL_FLOAT:
    out->value.kind = CONFIT_V2_VALUE_FLOAT;
    out->value.as.float_value = node->as.literal.value.float_value;
    return CONFIT_OK;
  case CONFIT_V2_EXPRESSION_LITERAL_STRING:
    out->value.kind = CONFIT_V2_VALUE_STRING;
    out->value.as.string_value =
        confit_v2_evaluate_copy_text(node->as.literal.value.string_value);
    if (out->value.as.string_value != 0) {
      return CONFIT_OK;
    }
    confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_INTERNAL,
                                  kAllocationFailed);
    confit_v2_expression_value_clear(out);
    return CONFIT_ERR_INTERNAL;
  default:
    confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                  kUnsupportedValue);
    return CONFIT_ERR_SCHEMA;
  }
}

static ConfitStatus confit_v2_evaluate_reference(
    const ConfitV2EvaluateContext *context, const ConfitV2ExpressionNode *node,
    ConfitV2ExpressionValue *out) {
  const ConfitV2ExpressionBinding *binding = confit_v2_evaluate_find_binding(
      context->environment, node->as.reference.option_id);
  const ConfitV2ExpressionType *type =
      confit_v2_typed_expression_node_type(context->typed, node);

  if (binding == 0 || type == 0) {
    confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA, kMissingBinding);
    return CONFIT_ERR_SCHEMA;
  }
  if (!confit_v2_expression_type_equal(type, &binding->type)) {
    confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                  kInvalidRuntimeType);
    return CONFIT_ERR_SCHEMA;
  }
  return confit_v2_evaluate_copy_bound_value(context, node, type, binding->value,
                                             out);
}

static ConfitStatus confit_v2_evaluate_unary(
    const ConfitV2EvaluateContext *context, const ConfitV2ExpressionNode *node,
    ConfitV2ExpressionValue *out) {
  ConfitV2ExpressionValue operand;
  ConfitStatus status;

  memset(&operand, 0, sizeof(operand));
  status = confit_v2_evaluate_node(context, node->as.unary.operand, &operand);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_v2_evaluate_require_set(context, node->as.unary.operand, &operand);
  if (status != CONFIT_OK) {
    confit_v2_expression_value_clear(&operand);
    return status;
  }
  if (node->as.unary.operator_kind == CONFIT_V2_EXPRESSION_UNARY_NOT) {
    confit_v2_evaluate_set_bool(out, !operand.value.as.bool_value);
  } else if (operand.type.kind == CONFIT_V2_EXPRESSION_TYPE_INT) {
    if (node->as.unary.operator_kind == CONFIT_V2_EXPRESSION_UNARY_NEGATE &&
        operand.value.as.int_value == INT64_MIN) {
      confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                    kIntegerOverflow);
      confit_v2_expression_value_clear(&operand);
      return CONFIT_ERR_SCHEMA;
    }
    memset(out, 0, sizeof(*out));
    out->type = operand.type;
    out->is_set = 1;
    out->value.kind = CONFIT_V2_VALUE_INT;
    out->value.as.int_value = node->as.unary.operator_kind ==
                                       CONFIT_V2_EXPRESSION_UNARY_NEGATE
                                   ? -operand.value.as.int_value
                                   : operand.value.as.int_value;
  } else if (operand.type.kind == CONFIT_V2_EXPRESSION_TYPE_FLOAT) {
    const double value = node->as.unary.operator_kind ==
                                 CONFIT_V2_EXPRESSION_UNARY_NEGATE
                             ? -operand.value.as.float_value
                             : operand.value.as.float_value;
    if (!isfinite(value)) {
      confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                    kNonFiniteFloat);
      confit_v2_expression_value_clear(&operand);
      return CONFIT_ERR_SCHEMA;
    }
    memset(out, 0, sizeof(*out));
    out->type = operand.type;
    out->is_set = 1;
    out->value.kind = CONFIT_V2_VALUE_FLOAT;
    out->value.as.float_value = value;
  } else {
    confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                  kInvalidRuntimeType);
    confit_v2_expression_value_clear(&operand);
    return CONFIT_ERR_SCHEMA;
  }
  confit_v2_expression_value_clear(&operand);
  return CONFIT_OK;
}

static ConfitStatus confit_v2_evaluate_arithmetic(
    const ConfitV2EvaluateContext *context, const ConfitV2ExpressionNode *node,
    const ConfitV2ExpressionValue *left, const ConfitV2ExpressionValue *right,
    ConfitV2ExpressionValue *out) {
  const ConfitV2ExpressionBinaryOperator operator_kind =
      node->as.binary.operator_kind;

  memset(out, 0, sizeof(*out));
  out->type = left->type;
  out->is_set = 1;
  if (left->type.kind == CONFIT_V2_EXPRESSION_TYPE_INT) {
    int64_t value;
    int valid = 1;
    out->value.kind = CONFIT_V2_VALUE_INT;
    switch (operator_kind) {
    case CONFIT_V2_EXPRESSION_BINARY_ADD:
      valid = confit_v2_evaluate_int_add(left->value.as.int_value,
                                         right->value.as.int_value, &value);
      break;
    case CONFIT_V2_EXPRESSION_BINARY_SUBTRACT:
      valid = confit_v2_evaluate_int_subtract(left->value.as.int_value,
                                              right->value.as.int_value, &value);
      break;
    case CONFIT_V2_EXPRESSION_BINARY_MULTIPLY:
      valid = confit_v2_evaluate_int_multiply(left->value.as.int_value,
                                              right->value.as.int_value, &value);
      break;
    case CONFIT_V2_EXPRESSION_BINARY_DIVIDE:
    case CONFIT_V2_EXPRESSION_BINARY_MODULO:
      if (right->value.as.int_value == 0) {
        confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                      kDivisionByZero);
        return CONFIT_ERR_SCHEMA;
      }
      if (left->value.as.int_value == INT64_MIN &&
          right->value.as.int_value == -1) {
        confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                      kIntegerOverflow);
        return CONFIT_ERR_SCHEMA;
      }
      value = operator_kind == CONFIT_V2_EXPRESSION_BINARY_DIVIDE
                  ? left->value.as.int_value / right->value.as.int_value
                  : left->value.as.int_value % right->value.as.int_value;
      break;
    default:
      valid = 0;
      break;
    }
    if (!valid) {
      confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                    kIntegerOverflow);
      return CONFIT_ERR_SCHEMA;
    }
    out->value.as.int_value = value;
    return CONFIT_OK;
  }
  if (left->type.kind == CONFIT_V2_EXPRESSION_TYPE_UINT) {
    uint64_t value;
    out->value.kind = CONFIT_V2_VALUE_UINT;
    switch (operator_kind) {
    case CONFIT_V2_EXPRESSION_BINARY_ADD:
      if (UINT64_MAX - left->value.as.uint_value < right->value.as.uint_value) {
        confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                      kUnsignedOverflow);
        return CONFIT_ERR_SCHEMA;
      }
      value = left->value.as.uint_value + right->value.as.uint_value;
      break;
    case CONFIT_V2_EXPRESSION_BINARY_SUBTRACT:
      if (left->value.as.uint_value < right->value.as.uint_value) {
        confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                      kUnsignedUnderflow);
        return CONFIT_ERR_SCHEMA;
      }
      value = left->value.as.uint_value - right->value.as.uint_value;
      break;
    case CONFIT_V2_EXPRESSION_BINARY_MULTIPLY:
      if (right->value.as.uint_value != 0U &&
          left->value.as.uint_value >
              UINT64_MAX / right->value.as.uint_value) {
        confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                      kUnsignedOverflow);
        return CONFIT_ERR_SCHEMA;
      }
      value = left->value.as.uint_value * right->value.as.uint_value;
      break;
    case CONFIT_V2_EXPRESSION_BINARY_DIVIDE:
    case CONFIT_V2_EXPRESSION_BINARY_MODULO:
      if (right->value.as.uint_value == 0U) {
        confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                      kDivisionByZero);
        return CONFIT_ERR_SCHEMA;
      }
      value = operator_kind == CONFIT_V2_EXPRESSION_BINARY_DIVIDE
                  ? left->value.as.uint_value / right->value.as.uint_value
                  : left->value.as.uint_value % right->value.as.uint_value;
      break;
    default:
      confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                    kInvalidRuntimeType);
      return CONFIT_ERR_SCHEMA;
    }
    out->value.as.uint_value = value;
    return CONFIT_OK;
  }
  if (left->type.kind == CONFIT_V2_EXPRESSION_TYPE_FLOAT) {
    double value;
    out->value.kind = CONFIT_V2_VALUE_FLOAT;
    if ((operator_kind == CONFIT_V2_EXPRESSION_BINARY_DIVIDE ||
         operator_kind == CONFIT_V2_EXPRESSION_BINARY_MODULO) &&
        right->value.as.float_value == 0.0) {
      confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                    kDivisionByZero);
      return CONFIT_ERR_SCHEMA;
    }
    switch (operator_kind) {
    case CONFIT_V2_EXPRESSION_BINARY_ADD:
      value = left->value.as.float_value + right->value.as.float_value;
      break;
    case CONFIT_V2_EXPRESSION_BINARY_SUBTRACT:
      value = left->value.as.float_value - right->value.as.float_value;
      break;
    case CONFIT_V2_EXPRESSION_BINARY_MULTIPLY:
      value = left->value.as.float_value * right->value.as.float_value;
      break;
    case CONFIT_V2_EXPRESSION_BINARY_DIVIDE:
      value = left->value.as.float_value / right->value.as.float_value;
      break;
    default:
      confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                    kInvalidRuntimeType);
      return CONFIT_ERR_SCHEMA;
    }
    if (!isfinite(value)) {
      confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                    kNonFiniteFloat);
      return CONFIT_ERR_SCHEMA;
    }
    out->value.as.float_value = value == 0.0 ? 0.0 : value;
    return CONFIT_OK;
  }
  confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                kInvalidRuntimeType);
  return CONFIT_ERR_SCHEMA;
}

static ConfitStatus confit_v2_evaluate_binary(
    const ConfitV2EvaluateContext *context, const ConfitV2ExpressionNode *node,
    ConfitV2ExpressionValue *out) {
  const ConfitV2ExpressionBinaryOperator operator_kind =
      node->as.binary.operator_kind;
  ConfitV2ExpressionValue left;
  ConfitV2ExpressionValue right;
  ConfitStatus status;

  memset(&left, 0, sizeof(left));
  memset(&right, 0, sizeof(right));
  status = confit_v2_evaluate_node(context, node->as.binary.left, &left);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_v2_evaluate_require_set(context, node->as.binary.left, &left);
  if (status != CONFIT_OK) {
    confit_v2_expression_value_clear(&left);
    return status;
  }
  if (operator_kind == CONFIT_V2_EXPRESSION_BINARY_AND &&
      !left.value.as.bool_value) {
    confit_v2_evaluate_set_bool(out, 0);
    confit_v2_expression_value_clear(&left);
    return CONFIT_OK;
  }
  if (operator_kind == CONFIT_V2_EXPRESSION_BINARY_OR &&
      left.value.as.bool_value) {
    confit_v2_evaluate_set_bool(out, 1);
    confit_v2_expression_value_clear(&left);
    return CONFIT_OK;
  }
  status = confit_v2_evaluate_node(context, node->as.binary.right, &right);
  if (status != CONFIT_OK) {
    confit_v2_expression_value_clear(&left);
    return status;
  }
  status = confit_v2_evaluate_require_set(context, node->as.binary.right, &right);
  if (status != CONFIT_OK) {
    confit_v2_expression_value_clear(&left);
    confit_v2_expression_value_clear(&right);
    return status;
  }
  switch (operator_kind) {
  case CONFIT_V2_EXPRESSION_BINARY_AND:
    confit_v2_evaluate_set_bool(out, left.value.as.bool_value &&
                                      right.value.as.bool_value);
    status = CONFIT_OK;
    break;
  case CONFIT_V2_EXPRESSION_BINARY_OR:
    confit_v2_evaluate_set_bool(out, left.value.as.bool_value ||
                                      right.value.as.bool_value);
    status = CONFIT_OK;
    break;
  case CONFIT_V2_EXPRESSION_BINARY_EQUAL:
    confit_v2_evaluate_set_bool(out, confit_v2_evaluate_values_equal(&left, &right));
    status = CONFIT_OK;
    break;
  case CONFIT_V2_EXPRESSION_BINARY_NOT_EQUAL:
    confit_v2_evaluate_set_bool(out, !confit_v2_evaluate_values_equal(&left, &right));
    status = CONFIT_OK;
    break;
  case CONFIT_V2_EXPRESSION_BINARY_LESS:
    confit_v2_evaluate_set_bool(out, confit_v2_evaluate_compare(&left, &right) < 0);
    status = CONFIT_OK;
    break;
  case CONFIT_V2_EXPRESSION_BINARY_LESS_EQUAL:
    confit_v2_evaluate_set_bool(out, confit_v2_evaluate_compare(&left, &right) <= 0);
    status = CONFIT_OK;
    break;
  case CONFIT_V2_EXPRESSION_BINARY_GREATER:
    confit_v2_evaluate_set_bool(out, confit_v2_evaluate_compare(&left, &right) > 0);
    status = CONFIT_OK;
    break;
  case CONFIT_V2_EXPRESSION_BINARY_GREATER_EQUAL:
    confit_v2_evaluate_set_bool(out, confit_v2_evaluate_compare(&left, &right) >= 0);
    status = CONFIT_OK;
    break;
  case CONFIT_V2_EXPRESSION_BINARY_IN: {
    size_t index;
    int found = 0;
    for (index = 0U; index < right.value.as.string_list.count; ++index) {
      if (strcmp(left.value.as.string_value,
                 right.value.as.string_list.items[index]) == 0) {
        found = 1;
        break;
      }
    }
    confit_v2_evaluate_set_bool(out, found);
    status = CONFIT_OK;
    break;
  }
  default:
    status = confit_v2_evaluate_arithmetic(context, node, &left, &right, out);
    break;
  }
  confit_v2_expression_value_clear(&left);
  confit_v2_expression_value_clear(&right);
  return status;
}

static ConfitStatus confit_v2_evaluate_list(
    const ConfitV2EvaluateContext *context, const ConfitV2ExpressionNode *node,
    ConfitV2ExpressionValue *out) {
  const ConfitV2ExpressionType *type =
      confit_v2_typed_expression_node_type(context->typed, node);
  size_t index;

  if (type == 0 || type->kind != CONFIT_V2_EXPRESSION_TYPE_COLLECTION) {
    confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_INTERNAL,
                                  kInvalidRuntimeType);
    return CONFIT_ERR_INTERNAL;
  }
  memset(out, 0, sizeof(*out));
  out->type = *type;
  out->is_set = 1;
  out->value.kind = CONFIT_V2_VALUE_STRING_LIST;
  out->value.as.string_list.count = node->as.list.item_count;
  if (node->as.list.item_count == 0U) {
    return CONFIT_OK;
  }
  out->value.as.string_list.items =
      (char **)calloc(node->as.list.item_count, sizeof(*out->value.as.string_list.items));
  if (out->value.as.string_list.items == 0) {
    confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_INTERNAL,
                                  kAllocationFailed);
    confit_v2_expression_value_clear(out);
    return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < node->as.list.item_count; ++index) {
    ConfitV2ExpressionValue item;
    ConfitStatus status;

    memset(&item, 0, sizeof(item));
    status = confit_v2_evaluate_node(context, node->as.list.items[index], &item);
    if (status == CONFIT_OK) {
      status = confit_v2_evaluate_require_set(context, node->as.list.items[index],
                                              &item);
    }
    if (status != CONFIT_OK) {
      confit_v2_expression_value_clear(&item);
      confit_v2_expression_value_clear(out);
      return status;
    }
    out->value.as.string_list.items[index] =
        confit_v2_evaluate_copy_text(item.value.as.string_value);
    confit_v2_expression_value_clear(&item);
    if (out->value.as.string_list.items[index] == 0) {
      confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_INTERNAL,
                                    kAllocationFailed);
      confit_v2_expression_value_clear(out);
      return CONFIT_ERR_INTERNAL;
    }
  }
  return CONFIT_OK;
}

static int confit_v2_evaluate_string_prefix(const char *text, const char *prefix) {
  const size_t prefix_size = strlen(prefix);
  return strncmp(text, prefix, prefix_size) == 0;
}

static int confit_v2_evaluate_string_suffix(const char *text, const char *suffix) {
  const size_t text_size = strlen(text);
  const size_t suffix_size = strlen(suffix);
  return text_size >= suffix_size &&
         memcmp(text + text_size - suffix_size, suffix, suffix_size) == 0;
}

static ConfitStatus confit_v2_evaluate_call(
    const ConfitV2EvaluateContext *context, const ConfitV2ExpressionNode *node,
    ConfitV2ExpressionValue *out) {
  const char *name = node->as.call.function_name;
  ConfitV2ExpressionValue *arguments;
  size_t index;
  ConfitStatus status;

  if (strcmp(name, "defined") == 0) {
    const ConfitV2ExpressionNode *argument = node->as.call.arguments[0];
    const ConfitV2ExpressionBinding *binding = confit_v2_evaluate_find_binding(
        context->environment, argument->as.reference.option_id);
    const ConfitV2ExpressionType *argument_type =
        confit_v2_typed_expression_node_type(context->typed, argument);
    if (binding == 0) {
      confit_v2_evaluate_diagnostic(context, argument, CONFIT_ERR_SCHEMA,
                                    kMissingBinding);
      return CONFIT_ERR_SCHEMA;
    }
    if (argument_type == 0 ||
        !confit_v2_expression_type_equal(argument_type, &binding->type)) {
      confit_v2_evaluate_diagnostic(context, argument, CONFIT_ERR_SCHEMA,
                                    kInvalidRuntimeType);
      return CONFIT_ERR_SCHEMA;
    }
    confit_v2_evaluate_set_bool(out, binding->value != 0 &&
                                      binding->value->kind != CONFIT_V2_VALUE_UNSET);
    return CONFIT_OK;
  }
  arguments = (ConfitV2ExpressionValue *)calloc(node->as.call.argument_count,
                                                  sizeof(*arguments));
  if (arguments == 0) {
    confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_INTERNAL,
                                  kAllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < node->as.call.argument_count; ++index) {
    status = confit_v2_evaluate_node(context, node->as.call.arguments[index],
                                     &arguments[index]);
    if (status == CONFIT_OK) {
      status = confit_v2_evaluate_require_set(context, node->as.call.arguments[index],
                                              &arguments[index]);
    }
    if (status != CONFIT_OK) {
      size_t clear_index;
      for (clear_index = 0U; clear_index <= index; ++clear_index) {
        confit_v2_expression_value_clear(&arguments[clear_index]);
      }
      free(arguments);
      return status;
    }
  }
  if (strcmp(name, "enabled") == 0) {
    const int enabled = arguments[0].type.kind == CONFIT_V2_EXPRESSION_TYPE_BOOL
                            ? arguments[0].value.as.bool_value
                            : arguments[0].value.as.tristate_value != 'n';
    confit_v2_evaluate_set_bool(out, enabled);
    status = CONFIT_OK;
  } else if (strcmp(name, "builtin") == 0) {
    confit_v2_evaluate_set_bool(out, arguments[0].value.as.tristate_value == 'y');
    status = CONFIT_OK;
  } else if (strcmp(name, "module") == 0) {
    confit_v2_evaluate_set_bool(out, arguments[0].value.as.tristate_value == 'm');
    status = CONFIT_OK;
  } else if (strcmp(name, "len") == 0) {
    memset(out, 0, sizeof(*out));
    out->type.kind = CONFIT_V2_EXPRESSION_TYPE_UINT;
    out->is_set = 1;
    out->value.kind = CONFIT_V2_VALUE_UINT;
    out->value.as.uint_value = arguments[0].type.kind ==
                                        CONFIT_V2_EXPRESSION_TYPE_STRING
                                    ? (uint64_t)strlen(arguments[0].value.as.string_value)
                                    : (uint64_t)arguments[0].value.as.string_list.count;
    status = CONFIT_OK;
  } else if (strcmp(name, "contains") == 0) {
    int found = 0;
    for (index = 0U; index < arguments[0].value.as.string_list.count; ++index) {
      if (strcmp(arguments[0].value.as.string_list.items[index],
                 arguments[1].value.as.string_value) == 0) {
        found = 1;
        break;
      }
    }
    confit_v2_evaluate_set_bool(out, found);
    status = CONFIT_OK;
  } else if (strcmp(name, "starts_with") == 0) {
    confit_v2_evaluate_set_bool(
        out, confit_v2_evaluate_string_prefix(arguments[0].value.as.string_value,
                                               arguments[1].value.as.string_value));
    status = CONFIT_OK;
  } else if (strcmp(name, "ends_with") == 0) {
    confit_v2_evaluate_set_bool(
        out, confit_v2_evaluate_string_suffix(arguments[0].value.as.string_value,
                                               arguments[1].value.as.string_value));
    status = CONFIT_OK;
  } else if (strcmp(name, "concat") == 0) {
    size_t total = 0U;
    char *joined;
    size_t offset = 0U;
    for (index = 0U; index < node->as.call.argument_count; ++index) {
      const size_t size = strlen(arguments[index].value.as.string_value);
      if (size > SIZE_MAX - total - 1U) {
        status = CONFIT_ERR_INTERNAL;
        confit_v2_evaluate_diagnostic(context, node, status, kAllocationFailed);
        goto clear_arguments;
      }
      total += size;
    }
    joined = (char *)malloc(total + 1U);
    if (joined == 0) {
      status = CONFIT_ERR_INTERNAL;
      confit_v2_evaluate_diagnostic(context, node, status, kAllocationFailed);
      goto clear_arguments;
    }
    for (index = 0U; index < node->as.call.argument_count; ++index) {
      const size_t size = strlen(arguments[index].value.as.string_value);
      memcpy(joined + offset, arguments[index].value.as.string_value, size);
      offset += size;
    }
    joined[offset] = '\0';
    memset(out, 0, sizeof(*out));
    out->type.kind = CONFIT_V2_EXPRESSION_TYPE_STRING;
    out->is_set = 1;
    out->value.kind = CONFIT_V2_VALUE_STRING;
    out->value.as.string_value = joined;
    status = CONFIT_OK;
  } else if (strcmp(name, "enum_name") == 0) {
    memset(out, 0, sizeof(*out));
    out->type.kind = CONFIT_V2_EXPRESSION_TYPE_STRING;
    out->is_set = 1;
    out->value.kind = CONFIT_V2_VALUE_STRING;
    out->value.as.string_value =
        confit_v2_evaluate_copy_text(arguments[0].value.as.string_value);
    if (out->value.as.string_value == 0) {
      status = CONFIT_ERR_INTERNAL;
      confit_v2_evaluate_diagnostic(context, node, status, kAllocationFailed);
    } else {
      status = CONFIT_OK;
    }
  } else {
    status = CONFIT_ERR_INTERNAL;
    confit_v2_evaluate_diagnostic(context, node, status, kInvalidRuntimeType);
  }

clear_arguments:
  for (index = 0U; index < node->as.call.argument_count; ++index) {
    confit_v2_expression_value_clear(&arguments[index]);
  }
  free(arguments);
  if (status != CONFIT_OK) {
    confit_v2_expression_value_clear(out);
  }
  return status;
}

static ConfitStatus confit_v2_evaluate_conditional(
    const ConfitV2EvaluateContext *context, const ConfitV2ExpressionNode *node,
    ConfitV2ExpressionValue *out) {
  ConfitV2ExpressionValue condition;
  ConfitStatus status;

  memset(&condition, 0, sizeof(condition));
  status = confit_v2_evaluate_node(context, node->as.conditional.condition,
                                   &condition);
  if (status == CONFIT_OK) {
    status = confit_v2_evaluate_require_set(context, node->as.conditional.condition,
                                            &condition);
  }
  if (status != CONFIT_OK) {
    confit_v2_expression_value_clear(&condition);
    return status;
  }
  status = confit_v2_evaluate_node(
      context, condition.value.as.bool_value ? node->as.conditional.when_true
                                              : node->as.conditional.when_false,
      out);
  confit_v2_expression_value_clear(&condition);
  return status;
}

static ConfitStatus confit_v2_evaluate_node(
    const ConfitV2EvaluateContext *context, const ConfitV2ExpressionNode *node,
    ConfitV2ExpressionValue *out) {
  if (node == 0 || out == 0) {
    confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_INTERNAL,
                                  kInvalidRuntimeType);
    return CONFIT_ERR_INTERNAL;
  }
  switch (node->kind) {
  case CONFIT_V2_EXPRESSION_NODE_LITERAL:
    return confit_v2_evaluate_literal(context, node, out);
  case CONFIT_V2_EXPRESSION_NODE_REFERENCE:
    return confit_v2_evaluate_reference(context, node, out);
  case CONFIT_V2_EXPRESSION_NODE_UNARY:
    return confit_v2_evaluate_unary(context, node, out);
  case CONFIT_V2_EXPRESSION_NODE_BINARY:
    return confit_v2_evaluate_binary(context, node, out);
  case CONFIT_V2_EXPRESSION_NODE_CONDITIONAL:
    return confit_v2_evaluate_conditional(context, node, out);
  case CONFIT_V2_EXPRESSION_NODE_CALL:
    return confit_v2_evaluate_call(context, node, out);
  case CONFIT_V2_EXPRESSION_NODE_LIST:
    return confit_v2_evaluate_list(context, node, out);
  default:
    confit_v2_evaluate_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                  kUnsupportedValue);
    return CONFIT_ERR_SCHEMA;
  }
}

ConfitStatus confit_v2_expression_evaluate(
    const ConfitV2TypedExpression *typed,
    const ConfitV2ExpressionEnvironment *environment,
    ConfitV2ExpressionValue *out_value, ConfitDiagnostic *diagnostic) {
  ConfitV2EvaluateContext context;

  if (typed == 0 || typed->expression == 0 || typed->expression->root == 0 ||
      typed->nodes == 0 || typed->node_count == 0U || out_value == 0 ||
      (environment != 0 && environment->binding_count > 0U &&
       environment->bindings == 0)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          kInvalidEvaluateArgument);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  memset(&context, 0, sizeof(context));
  context.typed = typed;
  context.environment = environment;
  context.diagnostic = diagnostic;
  memset(out_value, 0, sizeof(*out_value));
  return confit_v2_evaluate_node(&context, typed->expression->root, out_value);
}
