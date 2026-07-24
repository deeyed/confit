#include "confit/expression_v2.h"

#include <stdlib.h>
#include <string.h>

#include "lexer_internal.h"

typedef struct ConfitV2TypecheckContext {
  const ConfitV2Expression *expression;
  const ConfitV2ExpressionEnvironment *environment;
  ConfitV2TypedExpression *typed;
  ConfitDiagnostic *diagnostic;
} ConfitV2TypecheckContext;

static const char kInvalidEnvironment[] = "invalid expression binding environment";
static const char kUnknownReference[] = "unknown expression reference";
static const char kInvalidLiteral[] = "invalid expression literal type";
static const char kBooleanOperator[] = "boolean operator requires bool operands";
static const char kUnaryBoolean[] = "'!' requires a bool operand";
static const char kUnaryNumeric[] = "unary numeric operator requires int or float";
static const char kEqualityTypes[] = "equality requires identical operand types";
static const char kOrderingTypes[] = "ordering requires matching ordered operand types";
static const char kMembershipRight[] = "'in' requires a collection right operand";
static const char kMembershipTypes[] = "'in' element type must match collection type";
static const char kArithmeticTypes[] = "arithmetic requires identical numeric operand types";
static const char kFloatModulo[] = "'%' is not defined for float";
static const char kConditionalCondition[] = "conditional condition must be bool";
static const char kConditionalBranches[] = "conditional branches must have identical types";
static const char kEmptyList[] = "empty list literal has no static element type";
static const char kListElementTypes[] = "list literal supports string, path, or enum elements";
static const char kListElementMismatch[] = "list literal elements must have identical types";
static const char kUnknownBuiltin[] = "unknown expression builtin";
static const char kBuiltinArity[] = "builtin has an invalid argument count";
static const char kBuiltinArgument[] = "builtin argument type is invalid";
static const char kDefinedReference[] = "defined() requires an option reference";
static const char kTypecheckAllocation[] = "failed to allocate expression type data";

static void confit_v2_typecheck_diagnostic(
    const ConfitV2TypecheckContext *context,
    const ConfitV2ExpressionNode *node, ConfitStatus status,
    const char *message) {
  ConfitV2ExpressionText source;

  memset(&source, 0, sizeof(source));
  source.text = context->expression->source;
  source.span = context->expression->source_span;
  confit_v2_expression_diagnostic_at(&source,
                                     node != 0 ? node->start_offset : 0U,
                                     status, message, context->diagnostic);
}

static int confit_v2_typecheck_text_equal(const char *left, const char *right) {
  return left != 0 && right != 0 && strcmp(left, right) == 0;
}

static int confit_v2_typecheck_type_is_valid(const ConfitV2ExpressionType *type) {
  if (type == 0) {
    return 0;
  }
  switch (type->kind) {
  case CONFIT_V2_EXPRESSION_TYPE_BOOL:
  case CONFIT_V2_EXPRESSION_TYPE_TRISTATE:
  case CONFIT_V2_EXPRESSION_TYPE_INT:
  case CONFIT_V2_EXPRESSION_TYPE_UINT:
  case CONFIT_V2_EXPRESSION_TYPE_FLOAT:
  case CONFIT_V2_EXPRESSION_TYPE_STRING:
  case CONFIT_V2_EXPRESSION_TYPE_PATH:
    return type->element_kind == CONFIT_V2_EXPRESSION_TYPE_INVALID &&
           type->enum_id == 0 && type->element_enum_id == 0;
  case CONFIT_V2_EXPRESSION_TYPE_ENUM:
    return type->element_kind == CONFIT_V2_EXPRESSION_TYPE_INVALID &&
           type->enum_id != 0 && type->enum_id[0] != '\0' &&
           type->element_enum_id == 0;
  case CONFIT_V2_EXPRESSION_TYPE_COLLECTION:
    if (type->enum_id != 0 ||
        type->element_kind == CONFIT_V2_EXPRESSION_TYPE_INVALID ||
        type->element_kind == CONFIT_V2_EXPRESSION_TYPE_COLLECTION) {
      return 0;
    }
    if (type->element_kind == CONFIT_V2_EXPRESSION_TYPE_ENUM) {
      return type->element_enum_id != 0 && type->element_enum_id[0] != '\0';
    }
    return type->element_enum_id == 0;
  default:
    return 0;
  }
}

ConfitV2ExpressionType confit_v2_expression_type_from_option_type(
    ConfitV2OptionType option_type, const char *enum_id) {
  ConfitV2ExpressionType type;

  memset(&type, 0, sizeof(type));
  switch (option_type) {
  case CONFIT_V2_OPTION_TYPE_BOOL:
    type.kind = CONFIT_V2_EXPRESSION_TYPE_BOOL;
    break;
  case CONFIT_V2_OPTION_TYPE_TRISTATE:
    type.kind = CONFIT_V2_EXPRESSION_TYPE_TRISTATE;
    break;
  case CONFIT_V2_OPTION_TYPE_INT:
    type.kind = CONFIT_V2_EXPRESSION_TYPE_INT;
    break;
  case CONFIT_V2_OPTION_TYPE_UINT:
  case CONFIT_V2_OPTION_TYPE_HEX:
    type.kind = CONFIT_V2_EXPRESSION_TYPE_UINT;
    break;
  case CONFIT_V2_OPTION_TYPE_FLOAT:
    type.kind = CONFIT_V2_EXPRESSION_TYPE_FLOAT;
    break;
  case CONFIT_V2_OPTION_TYPE_STRING:
    type.kind = CONFIT_V2_EXPRESSION_TYPE_STRING;
    break;
  case CONFIT_V2_OPTION_TYPE_ENUM:
    type.kind = CONFIT_V2_EXPRESSION_TYPE_ENUM;
    type.enum_id = enum_id;
    break;
  case CONFIT_V2_OPTION_TYPE_PATH:
    type.kind = CONFIT_V2_EXPRESSION_TYPE_PATH;
    break;
  case CONFIT_V2_OPTION_TYPE_STRING_LIST:
    type.kind = CONFIT_V2_EXPRESSION_TYPE_COLLECTION;
    type.element_kind = CONFIT_V2_EXPRESSION_TYPE_STRING;
    break;
  case CONFIT_V2_OPTION_TYPE_PATH_LIST:
    type.kind = CONFIT_V2_EXPRESSION_TYPE_COLLECTION;
    type.element_kind = CONFIT_V2_EXPRESSION_TYPE_PATH;
    break;
  case CONFIT_V2_OPTION_TYPE_ENUM_SET:
    type.kind = CONFIT_V2_EXPRESSION_TYPE_COLLECTION;
    type.element_kind = CONFIT_V2_EXPRESSION_TYPE_ENUM;
    type.element_enum_id = enum_id;
    break;
  default:
    break;
  }
  return type;
}

int confit_v2_expression_type_equal(const ConfitV2ExpressionType *left,
                                    const ConfitV2ExpressionType *right) {
  if (!confit_v2_typecheck_type_is_valid(left) ||
      !confit_v2_typecheck_type_is_valid(right) || left->kind != right->kind) {
    return 0;
  }
  if (left->kind == CONFIT_V2_EXPRESSION_TYPE_ENUM) {
    return confit_v2_typecheck_text_equal(left->enum_id, right->enum_id);
  }
  if (left->kind == CONFIT_V2_EXPRESSION_TYPE_COLLECTION) {
    if (left->element_kind != right->element_kind) {
      return 0;
    }
    if (left->element_kind == CONFIT_V2_EXPRESSION_TYPE_ENUM) {
      return confit_v2_typecheck_text_equal(left->element_enum_id,
                                             right->element_enum_id);
    }
  }
  return 1;
}

const char *confit_v2_expression_type_name(const ConfitV2ExpressionType *type) {
  if (type == 0) {
    return "invalid";
  }
  switch (type->kind) {
  case CONFIT_V2_EXPRESSION_TYPE_BOOL:
    return "bool";
  case CONFIT_V2_EXPRESSION_TYPE_TRISTATE:
    return "tristate";
  case CONFIT_V2_EXPRESSION_TYPE_INT:
    return "int";
  case CONFIT_V2_EXPRESSION_TYPE_UINT:
    return "uint";
  case CONFIT_V2_EXPRESSION_TYPE_FLOAT:
    return "float";
  case CONFIT_V2_EXPRESSION_TYPE_STRING:
    return "string";
  case CONFIT_V2_EXPRESSION_TYPE_ENUM:
    return "enum";
  case CONFIT_V2_EXPRESSION_TYPE_PATH:
    return "path";
  case CONFIT_V2_EXPRESSION_TYPE_COLLECTION:
    return "collection";
  default:
    return "invalid";
  }
}

static int confit_v2_typecheck_is_numeric(const ConfitV2ExpressionType *type) {
  return type->kind == CONFIT_V2_EXPRESSION_TYPE_INT ||
         type->kind == CONFIT_V2_EXPRESSION_TYPE_UINT ||
         type->kind == CONFIT_V2_EXPRESSION_TYPE_FLOAT;
}

static int confit_v2_typecheck_is_ordered(const ConfitV2ExpressionType *type) {
  return confit_v2_typecheck_is_numeric(type) ||
         type->kind == CONFIT_V2_EXPRESSION_TYPE_STRING ||
         type->kind == CONFIT_V2_EXPRESSION_TYPE_PATH;
}

static const ConfitV2ExpressionBinding *confit_v2_typecheck_find_binding(
    const ConfitV2ExpressionEnvironment *environment, const char *id) {
  size_t index;

  if (environment == 0) {
    return 0;
  }
  for (index = 0U; index < environment->binding_count; ++index) {
    if (strcmp(environment->bindings[index].id, id) == 0) {
      return &environment->bindings[index];
    }
  }
  return 0;
}

static int confit_v2_typecheck_environment_is_valid(
    const ConfitV2ExpressionEnvironment *environment) {
  size_t index;
  size_t other;

  if (environment == 0) {
    return 1;
  }
  if (environment->binding_count > 0U && environment->bindings == 0) {
    return 0;
  }
  for (index = 0U; index < environment->binding_count; ++index) {
    const ConfitV2ExpressionBinding *binding = &environment->bindings[index];
    if (binding->id == 0 || binding->id[0] == '\0' ||
        !confit_v2_typecheck_type_is_valid(&binding->type)) {
      return 0;
    }
    for (other = 0U; other < index; ++other) {
      if (strcmp(binding->id, environment->bindings[other].id) == 0) {
        return 0;
      }
    }
  }
  return 1;
}

static ConfitStatus confit_v2_typecheck_append_type(
    ConfitV2TypecheckContext *context, const ConfitV2ExpressionNode *node,
    ConfitV2ExpressionType type) {
  ConfitV2ExpressionTypeInfo *grown;
  size_t count;

  count = context->typed->node_count;
  if (count == SIZE_MAX / sizeof(*grown)) {
    confit_v2_typecheck_diagnostic(context, node, CONFIT_ERR_INTERNAL,
                                   kTypecheckAllocation);
    return CONFIT_ERR_INTERNAL;
  }
  grown = (ConfitV2ExpressionTypeInfo *)realloc(
      context->typed->nodes, (count + 1U) * sizeof(*grown));
  if (grown == 0) {
    confit_v2_typecheck_diagnostic(context, node, CONFIT_ERR_INTERNAL,
                                   kTypecheckAllocation);
    return CONFIT_ERR_INTERNAL;
  }
  context->typed->nodes = grown;
  context->typed->nodes[count].node = node;
  context->typed->nodes[count].type = type;
  context->typed->node_count = count + 1U;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_typecheck_node(
    ConfitV2TypecheckContext *context, const ConfitV2ExpressionNode *node,
    ConfitV2ExpressionType *out_type);

static ConfitStatus confit_v2_typecheck_call(
    ConfitV2TypecheckContext *context, const ConfitV2ExpressionNode *node,
    ConfitV2ExpressionType *out_type) {
  ConfitV2ExpressionType *arguments;
  size_t index;
  ConfitStatus status;
  const char *name = node->as.call.function_name;

  arguments = 0;
  if (node->as.call.argument_count > 0U) {
    arguments = (ConfitV2ExpressionType *)calloc(node->as.call.argument_count,
                                                  sizeof(*arguments));
    if (arguments == 0) {
      confit_v2_typecheck_diagnostic(context, node, CONFIT_ERR_INTERNAL,
                                     kTypecheckAllocation);
      return CONFIT_ERR_INTERNAL;
    }
  }
  for (index = 0U; index < node->as.call.argument_count; ++index) {
    status = confit_v2_typecheck_node(context, node->as.call.arguments[index],
                                      &arguments[index]);
    if (status != CONFIT_OK) {
      free(arguments);
      return status;
    }
  }
  memset(out_type, 0, sizeof(*out_type));
  if (strcmp(name, "enabled") == 0) {
    if (node->as.call.argument_count != 1U) {
      status = CONFIT_ERR_SCHEMA;
      confit_v2_typecheck_diagnostic(context, node, status, kBuiltinArity);
    } else if (arguments[0].kind != CONFIT_V2_EXPRESSION_TYPE_BOOL &&
               arguments[0].kind != CONFIT_V2_EXPRESSION_TYPE_TRISTATE) {
      status = CONFIT_ERR_SCHEMA;
      confit_v2_typecheck_diagnostic(context, node, status, kBuiltinArgument);
    } else {
      out_type->kind = CONFIT_V2_EXPRESSION_TYPE_BOOL;
      status = CONFIT_OK;
    }
  } else if (strcmp(name, "builtin") == 0 || strcmp(name, "module") == 0) {
    if (node->as.call.argument_count != 1U) {
      status = CONFIT_ERR_SCHEMA;
      confit_v2_typecheck_diagnostic(context, node, status, kBuiltinArity);
    } else if (arguments[0].kind != CONFIT_V2_EXPRESSION_TYPE_TRISTATE) {
      status = CONFIT_ERR_SCHEMA;
      confit_v2_typecheck_diagnostic(context, node, status, kBuiltinArgument);
    } else {
      out_type->kind = CONFIT_V2_EXPRESSION_TYPE_BOOL;
      status = CONFIT_OK;
    }
  } else if (strcmp(name, "defined") == 0) {
    if (node->as.call.argument_count != 1U) {
      status = CONFIT_ERR_SCHEMA;
      confit_v2_typecheck_diagnostic(context, node, status, kBuiltinArity);
    } else if (node->as.call.arguments[0]->kind !=
               CONFIT_V2_EXPRESSION_NODE_REFERENCE) {
      status = CONFIT_ERR_SCHEMA;
      confit_v2_typecheck_diagnostic(context, node, status, kDefinedReference);
    } else {
      out_type->kind = CONFIT_V2_EXPRESSION_TYPE_BOOL;
      status = CONFIT_OK;
    }
  } else if (strcmp(name, "len") == 0) {
    if (node->as.call.argument_count != 1U) {
      status = CONFIT_ERR_SCHEMA;
      confit_v2_typecheck_diagnostic(context, node, status, kBuiltinArity);
    } else if (arguments[0].kind != CONFIT_V2_EXPRESSION_TYPE_STRING &&
               arguments[0].kind != CONFIT_V2_EXPRESSION_TYPE_COLLECTION) {
      status = CONFIT_ERR_SCHEMA;
      confit_v2_typecheck_diagnostic(context, node, status, kBuiltinArgument);
    } else {
      out_type->kind = CONFIT_V2_EXPRESSION_TYPE_UINT;
      status = CONFIT_OK;
    }
  } else if (strcmp(name, "contains") == 0) {
    if (node->as.call.argument_count != 2U) {
      status = CONFIT_ERR_SCHEMA;
      confit_v2_typecheck_diagnostic(context, node, status, kBuiltinArity);
    } else if (arguments[0].kind != CONFIT_V2_EXPRESSION_TYPE_COLLECTION ||
               arguments[0].element_kind != arguments[1].kind ||
               (arguments[1].kind == CONFIT_V2_EXPRESSION_TYPE_ENUM &&
                !confit_v2_typecheck_text_equal(arguments[0].element_enum_id,
                                                 arguments[1].enum_id))) {
      status = CONFIT_ERR_SCHEMA;
      confit_v2_typecheck_diagnostic(context, node, status, kBuiltinArgument);
    } else {
      out_type->kind = CONFIT_V2_EXPRESSION_TYPE_BOOL;
      status = CONFIT_OK;
    }
  } else if (strcmp(name, "starts_with") == 0 ||
             strcmp(name, "ends_with") == 0) {
    if (node->as.call.argument_count != 2U) {
      status = CONFIT_ERR_SCHEMA;
      confit_v2_typecheck_diagnostic(context, node, status, kBuiltinArity);
    } else if (arguments[0].kind != CONFIT_V2_EXPRESSION_TYPE_STRING ||
               arguments[1].kind != CONFIT_V2_EXPRESSION_TYPE_STRING) {
      status = CONFIT_ERR_SCHEMA;
      confit_v2_typecheck_diagnostic(context, node, status, kBuiltinArgument);
    } else {
      out_type->kind = CONFIT_V2_EXPRESSION_TYPE_BOOL;
      status = CONFIT_OK;
    }
  } else if (strcmp(name, "concat") == 0) {
    if (node->as.call.argument_count == 0U) {
      status = CONFIT_ERR_SCHEMA;
      confit_v2_typecheck_diagnostic(context, node, status, kBuiltinArity);
    } else {
      status = CONFIT_OK;
      for (index = 0U; index < node->as.call.argument_count; ++index) {
        if (arguments[index].kind != CONFIT_V2_EXPRESSION_TYPE_STRING) {
          status = CONFIT_ERR_SCHEMA;
          confit_v2_typecheck_diagnostic(context, node, status, kBuiltinArgument);
          break;
        }
      }
      if (status == CONFIT_OK) {
        out_type->kind = CONFIT_V2_EXPRESSION_TYPE_STRING;
      }
    }
  } else if (strcmp(name, "enum_name") == 0) {
    if (node->as.call.argument_count != 1U) {
      status = CONFIT_ERR_SCHEMA;
      confit_v2_typecheck_diagnostic(context, node, status, kBuiltinArity);
    } else if (arguments[0].kind != CONFIT_V2_EXPRESSION_TYPE_ENUM) {
      status = CONFIT_ERR_SCHEMA;
      confit_v2_typecheck_diagnostic(context, node, status, kBuiltinArgument);
    } else {
      out_type->kind = CONFIT_V2_EXPRESSION_TYPE_STRING;
      status = CONFIT_OK;
    }
  } else {
    status = CONFIT_ERR_SCHEMA;
    confit_v2_typecheck_diagnostic(context, node, status, kUnknownBuiltin);
  }
  free(arguments);
  return status;
}

static ConfitStatus confit_v2_typecheck_node(
    ConfitV2TypecheckContext *context, const ConfitV2ExpressionNode *node,
    ConfitV2ExpressionType *out_type) {
  ConfitV2ExpressionType left;
  ConfitV2ExpressionType right;
  ConfitStatus status;
  size_t index;

  if (node == 0 || out_type == 0) {
    confit_v2_typecheck_diagnostic(context, node, CONFIT_ERR_INTERNAL,
                                   kInvalidLiteral);
    return CONFIT_ERR_INTERNAL;
  }
  memset(out_type, 0, sizeof(*out_type));
  memset(&left, 0, sizeof(left));
  memset(&right, 0, sizeof(right));
  switch (node->kind) {
  case CONFIT_V2_EXPRESSION_NODE_LITERAL:
    switch (node->as.literal.kind) {
    case CONFIT_V2_EXPRESSION_LITERAL_BOOL:
      out_type->kind = CONFIT_V2_EXPRESSION_TYPE_BOOL;
      break;
    case CONFIT_V2_EXPRESSION_LITERAL_TRISTATE:
      out_type->kind = CONFIT_V2_EXPRESSION_TYPE_TRISTATE;
      break;
    case CONFIT_V2_EXPRESSION_LITERAL_INT:
      out_type->kind = CONFIT_V2_EXPRESSION_TYPE_INT;
      break;
    case CONFIT_V2_EXPRESSION_LITERAL_HEX:
      out_type->kind = CONFIT_V2_EXPRESSION_TYPE_UINT;
      break;
    case CONFIT_V2_EXPRESSION_LITERAL_FLOAT:
      out_type->kind = CONFIT_V2_EXPRESSION_TYPE_FLOAT;
      break;
    case CONFIT_V2_EXPRESSION_LITERAL_STRING:
      out_type->kind = CONFIT_V2_EXPRESSION_TYPE_STRING;
      break;
    default:
      confit_v2_typecheck_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                     kInvalidLiteral);
      return CONFIT_ERR_SCHEMA;
    }
    break;
  case CONFIT_V2_EXPRESSION_NODE_REFERENCE: {
    const ConfitV2ExpressionBinding *binding = confit_v2_typecheck_find_binding(
        context->environment, node->as.reference.option_id);
    if (binding == 0) {
      confit_v2_typecheck_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                     kUnknownReference);
      return CONFIT_ERR_SCHEMA;
    }
    *out_type = binding->type;
    break;
  }
  case CONFIT_V2_EXPRESSION_NODE_UNARY:
    status = confit_v2_typecheck_node(context, node->as.unary.operand, &left);
    if (status != CONFIT_OK) {
      return status;
    }
    if (node->as.unary.operator_kind == CONFIT_V2_EXPRESSION_UNARY_NOT) {
      if (left.kind != CONFIT_V2_EXPRESSION_TYPE_BOOL) {
        confit_v2_typecheck_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                       kUnaryBoolean);
        return CONFIT_ERR_SCHEMA;
      }
    } else if (left.kind != CONFIT_V2_EXPRESSION_TYPE_INT &&
               left.kind != CONFIT_V2_EXPRESSION_TYPE_FLOAT) {
      confit_v2_typecheck_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                     kUnaryNumeric);
      return CONFIT_ERR_SCHEMA;
    }
    *out_type = left;
    break;
  case CONFIT_V2_EXPRESSION_NODE_BINARY:
    status = confit_v2_typecheck_node(context, node->as.binary.left, &left);
    if (status != CONFIT_OK) {
      return status;
    }
    status = confit_v2_typecheck_node(context, node->as.binary.right, &right);
    if (status != CONFIT_OK) {
      return status;
    }
    switch (node->as.binary.operator_kind) {
    case CONFIT_V2_EXPRESSION_BINARY_AND:
    case CONFIT_V2_EXPRESSION_BINARY_OR:
      if (left.kind != CONFIT_V2_EXPRESSION_TYPE_BOOL ||
          right.kind != CONFIT_V2_EXPRESSION_TYPE_BOOL) {
        confit_v2_typecheck_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                       kBooleanOperator);
        return CONFIT_ERR_SCHEMA;
      }
      out_type->kind = CONFIT_V2_EXPRESSION_TYPE_BOOL;
      break;
    case CONFIT_V2_EXPRESSION_BINARY_EQUAL:
    case CONFIT_V2_EXPRESSION_BINARY_NOT_EQUAL:
      if (!confit_v2_expression_type_equal(&left, &right)) {
        confit_v2_typecheck_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                       kEqualityTypes);
        return CONFIT_ERR_SCHEMA;
      }
      out_type->kind = CONFIT_V2_EXPRESSION_TYPE_BOOL;
      break;
    case CONFIT_V2_EXPRESSION_BINARY_IN:
      if (right.kind != CONFIT_V2_EXPRESSION_TYPE_COLLECTION) {
        confit_v2_typecheck_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                       kMembershipRight);
        return CONFIT_ERR_SCHEMA;
      }
      if (right.element_kind != left.kind ||
          (left.kind == CONFIT_V2_EXPRESSION_TYPE_ENUM &&
           !confit_v2_typecheck_text_equal(left.enum_id, right.element_enum_id))) {
        confit_v2_typecheck_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                       kMembershipTypes);
        return CONFIT_ERR_SCHEMA;
      }
      out_type->kind = CONFIT_V2_EXPRESSION_TYPE_BOOL;
      break;
    case CONFIT_V2_EXPRESSION_BINARY_LESS:
    case CONFIT_V2_EXPRESSION_BINARY_LESS_EQUAL:
    case CONFIT_V2_EXPRESSION_BINARY_GREATER:
    case CONFIT_V2_EXPRESSION_BINARY_GREATER_EQUAL:
      if (!confit_v2_expression_type_equal(&left, &right) ||
          !confit_v2_typecheck_is_ordered(&left)) {
        confit_v2_typecheck_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                       kOrderingTypes);
        return CONFIT_ERR_SCHEMA;
      }
      out_type->kind = CONFIT_V2_EXPRESSION_TYPE_BOOL;
      break;
    case CONFIT_V2_EXPRESSION_BINARY_ADD:
    case CONFIT_V2_EXPRESSION_BINARY_SUBTRACT:
    case CONFIT_V2_EXPRESSION_BINARY_MULTIPLY:
    case CONFIT_V2_EXPRESSION_BINARY_DIVIDE:
    case CONFIT_V2_EXPRESSION_BINARY_MODULO:
      if (!confit_v2_expression_type_equal(&left, &right) ||
          !confit_v2_typecheck_is_numeric(&left)) {
        confit_v2_typecheck_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                       kArithmeticTypes);
        return CONFIT_ERR_SCHEMA;
      }
      if (node->as.binary.operator_kind == CONFIT_V2_EXPRESSION_BINARY_MODULO &&
          left.kind == CONFIT_V2_EXPRESSION_TYPE_FLOAT) {
        confit_v2_typecheck_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                       kFloatModulo);
        return CONFIT_ERR_SCHEMA;
      }
      *out_type = left;
      break;
    default:
      confit_v2_typecheck_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                     kInvalidLiteral);
      return CONFIT_ERR_SCHEMA;
    }
    break;
  case CONFIT_V2_EXPRESSION_NODE_CONDITIONAL:
    status = confit_v2_typecheck_node(context, node->as.conditional.condition,
                                      &left);
    if (status != CONFIT_OK) {
      return status;
    }
    if (left.kind != CONFIT_V2_EXPRESSION_TYPE_BOOL) {
      confit_v2_typecheck_diagnostic(context, node->as.conditional.condition,
                                     CONFIT_ERR_SCHEMA, kConditionalCondition);
      return CONFIT_ERR_SCHEMA;
    }
    status = confit_v2_typecheck_node(context, node->as.conditional.when_true,
                                      &left);
    if (status != CONFIT_OK) {
      return status;
    }
    status = confit_v2_typecheck_node(context, node->as.conditional.when_false,
                                      &right);
    if (status != CONFIT_OK) {
      return status;
    }
    if (!confit_v2_expression_type_equal(&left, &right)) {
      confit_v2_typecheck_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                     kConditionalBranches);
      return CONFIT_ERR_SCHEMA;
    }
    *out_type = left;
    break;
  case CONFIT_V2_EXPRESSION_NODE_CALL:
    status = confit_v2_typecheck_call(context, node, out_type);
    if (status != CONFIT_OK) {
      return status;
    }
    break;
  case CONFIT_V2_EXPRESSION_NODE_LIST:
    if (node->as.list.item_count == 0U) {
      confit_v2_typecheck_diagnostic(context, node, CONFIT_ERR_SCHEMA, kEmptyList);
      return CONFIT_ERR_SCHEMA;
    }
    status = confit_v2_typecheck_node(context, node->as.list.items[0], &left);
    if (status != CONFIT_OK) {
      return status;
    }
    if (left.kind != CONFIT_V2_EXPRESSION_TYPE_STRING &&
        left.kind != CONFIT_V2_EXPRESSION_TYPE_PATH &&
        left.kind != CONFIT_V2_EXPRESSION_TYPE_ENUM) {
      confit_v2_typecheck_diagnostic(context, node->as.list.items[0],
                                     CONFIT_ERR_SCHEMA, kListElementTypes);
      return CONFIT_ERR_SCHEMA;
    }
    for (index = 1U; index < node->as.list.item_count; ++index) {
      status = confit_v2_typecheck_node(context, node->as.list.items[index], &right);
      if (status != CONFIT_OK) {
        return status;
      }
      if (!confit_v2_expression_type_equal(&left, &right)) {
        confit_v2_typecheck_diagnostic(context, node->as.list.items[index],
                                       CONFIT_ERR_SCHEMA, kListElementMismatch);
        return CONFIT_ERR_SCHEMA;
      }
    }
    out_type->kind = CONFIT_V2_EXPRESSION_TYPE_COLLECTION;
    out_type->element_kind = left.kind;
    if (left.kind == CONFIT_V2_EXPRESSION_TYPE_ENUM) {
      out_type->element_enum_id = left.enum_id;
    }
    break;
  default:
    confit_v2_typecheck_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                   kInvalidLiteral);
    return CONFIT_ERR_SCHEMA;
  }
  if (!confit_v2_typecheck_type_is_valid(out_type)) {
    confit_v2_typecheck_diagnostic(context, node, CONFIT_ERR_SCHEMA,
                                   kInvalidLiteral);
    return CONFIT_ERR_SCHEMA;
  }
  return confit_v2_typecheck_append_type(context, node, *out_type);
}

ConfitStatus confit_v2_expression_type_check(
    const ConfitV2Expression *expression,
    const ConfitV2ExpressionEnvironment *environment,
    ConfitV2TypedExpression **out_typed, ConfitDiagnostic *diagnostic) {
  ConfitV2TypecheckContext context;
  ConfitV2TypedExpression *typed;
  ConfitStatus status;

  if (out_typed == 0 || expression == 0 || expression->source == 0 ||
      expression->root == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid expression typecheck argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_typed = 0;
  if (!confit_v2_typecheck_environment_is_valid(environment)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                          expression->source_span.path,
                          expression->source_span.line,
                          expression->source_span.column, kInvalidEnvironment);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  typed = (ConfitV2TypedExpression *)calloc(1U, sizeof(*typed));
  if (typed == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL,
                          expression->source_span.path,
                          expression->source_span.line,
                          expression->source_span.column, kTypecheckAllocation);
    return CONFIT_ERR_INTERNAL;
  }
  typed->expression = expression;
  memset(&context, 0, sizeof(context));
  context.expression = expression;
  context.environment = environment;
  context.typed = typed;
  context.diagnostic = diagnostic;
  status = confit_v2_typecheck_node(&context, expression->root,
                                    &typed->root_type);
  if (status != CONFIT_OK) {
    confit_v2_typed_expression_free(typed);
    return status;
  }
  *out_typed = typed;
  return CONFIT_OK;
}

void confit_v2_typed_expression_free(ConfitV2TypedExpression *typed) {
  if (typed == 0) {
    return;
  }
  free(typed->nodes);
  free(typed);
}

const ConfitV2ExpressionType *confit_v2_typed_expression_node_type(
    const ConfitV2TypedExpression *typed,
    const ConfitV2ExpressionNode *node) {
  size_t index;

  if (typed == 0 || node == 0) {
    return 0;
  }
  for (index = 0U; index < typed->node_count; ++index) {
    if (typed->nodes[index].node == node) {
      return &typed->nodes[index].type;
    }
  }
  return 0;
}
