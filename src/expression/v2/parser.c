#include "confit/expression_v2.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer_internal.h"

typedef struct ConfitV2ExpressionParser {
  const ConfitV2ExpressionText *source;
  ConfitV2ExpressionLexer lexer;
  ConfitV2ExpressionToken current;
  ConfitV2ExpressionLimits limits;
  size_t node_count;
} ConfitV2ExpressionParser;

typedef struct ConfitV2ExpressionWriter {
  char *text;
  size_t length;
  size_t capacity;
} ConfitV2ExpressionWriter;

static const char kExpressionExpectedPrimary[] = "expected expression";
static const char kExpressionExpectedRightParen[] = "expected ')'";
static const char kExpressionExpectedRightBracket[] = "expected ']'";
static const char kExpressionExpectedColon[] = "expected ':'";
static const char kExpressionTrailingToken[] = "unexpected trailing expression token";
static const char kExpressionChainedRelation[] =
    "chained relation operators are not allowed";
static const char kExpressionNestingLimit[] = "expression nesting limit exceeded";
static const char kExpressionNodeLimit[] = "expression AST node limit exceeded";
static const char kExpressionSourceLimit[] = "expression source size limit exceeded";
static const char kExpressionInvalidNumber[] = "invalid expression number literal";
static const char kExpressionAllocationFailed[] = "failed to allocate expression AST";

static void confit_v2_expression_node_free(ConfitV2ExpressionNode *node) {
  size_t index;

  if (node == 0) {
    return;
  }
  switch (node->kind) {
  case CONFIT_V2_EXPRESSION_NODE_LITERAL:
    if (node->as.literal.kind == CONFIT_V2_EXPRESSION_LITERAL_STRING) {
      free(node->as.literal.value.string_value);
    }
    break;
  case CONFIT_V2_EXPRESSION_NODE_REFERENCE:
    free(node->as.reference.option_id);
    break;
  case CONFIT_V2_EXPRESSION_NODE_UNARY:
    confit_v2_expression_node_free(node->as.unary.operand);
    break;
  case CONFIT_V2_EXPRESSION_NODE_BINARY:
    confit_v2_expression_node_free(node->as.binary.left);
    confit_v2_expression_node_free(node->as.binary.right);
    break;
  case CONFIT_V2_EXPRESSION_NODE_CONDITIONAL:
    confit_v2_expression_node_free(node->as.conditional.condition);
    confit_v2_expression_node_free(node->as.conditional.when_true);
    confit_v2_expression_node_free(node->as.conditional.when_false);
    break;
  case CONFIT_V2_EXPRESSION_NODE_CALL:
    free(node->as.call.function_name);
    for (index = 0U; index < node->as.call.argument_count; ++index) {
      confit_v2_expression_node_free(node->as.call.arguments[index]);
    }
    free(node->as.call.arguments);
    break;
  case CONFIT_V2_EXPRESSION_NODE_LIST:
    for (index = 0U; index < node->as.list.item_count; ++index) {
      confit_v2_expression_node_free(node->as.list.items[index]);
    }
    free(node->as.list.items);
    break;
  default:
    break;
  }
  free(node);
}

void confit_v2_expression_free(ConfitV2Expression *expression) {
  if (expression == 0) {
    return;
  }
  free(expression->source);
  free(expression->source_span.path);
  confit_v2_expression_node_free(expression->root);
  free(expression);
}

ConfitV2ExpressionLimits confit_v2_expression_default_limits(void) {
  ConfitV2ExpressionLimits limits;

  limits.max_source_bytes = 65536U;
  limits.max_tokens = 8192U;
  limits.max_nodes = 8192U;
  limits.max_nesting = 128U;
  return limits;
}

static ConfitV2ExpressionLimits confit_v2_expression_effective_limits(
    const ConfitV2ExpressionLimits *requested) {
  ConfitV2ExpressionLimits limits = confit_v2_expression_default_limits();

  if (requested == 0) {
    return limits;
  }
  if (requested->max_source_bytes != 0U) {
    limits.max_source_bytes = requested->max_source_bytes;
  }
  if (requested->max_tokens != 0U) {
    limits.max_tokens = requested->max_tokens;
  }
  if (requested->max_nodes != 0U) {
    limits.max_nodes = requested->max_nodes;
  }
  if (requested->max_nesting != 0U) {
    limits.max_nesting = requested->max_nesting;
  }
  return limits;
}

static ConfitStatus confit_v2_expression_advance(
    ConfitV2ExpressionParser *parser, ConfitDiagnostic *diagnostic) {
  return confit_v2_expression_lexer_next(&parser->lexer, &parser->current,
                                          diagnostic);
}

static ConfitStatus confit_v2_expression_make_node(
    ConfitV2ExpressionParser *parser, ConfitV2ExpressionNodeKind kind,
    size_t start_offset, size_t end_offset, ConfitV2ExpressionNode **out_node,
    ConfitDiagnostic *diagnostic) {
  ConfitV2ExpressionNode *node;

  if (parser->node_count >= parser->limits.max_nodes) {
    confit_v2_expression_diagnostic_at(parser->source, start_offset,
                                       CONFIT_ERR_SCHEMA, kExpressionNodeLimit,
                                       diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  node = (ConfitV2ExpressionNode *)calloc(1U, sizeof(*node));
  if (node == 0) {
    confit_v2_expression_diagnostic_at(parser->source, start_offset,
                                       CONFIT_ERR_INTERNAL,
                                       kExpressionAllocationFailed, diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  parser->node_count += 1U;
  node->kind = kind;
  node->start_offset = start_offset;
  node->end_offset = end_offset;
  *out_node = node;
  return CONFIT_OK;
}

static char *confit_v2_expression_token_copy(
    const ConfitV2ExpressionParser *parser, const ConfitV2ExpressionToken *token,
    ConfitDiagnostic *diagnostic) {
  const size_t size = token->end_offset - token->start_offset;
  char *copy = (char *)malloc(size + 1U);

  if (copy == 0) {
    confit_v2_expression_diagnostic_at(parser->source, token->start_offset,
                                       CONFIT_ERR_INTERNAL,
                                       kExpressionAllocationFailed, diagnostic);
    return 0;
  }
  if (size > 0U) {
    memcpy(copy, parser->source->text + token->start_offset, size);
  }
  copy[size] = '\0';
  return copy;
}

static int confit_v2_expression_parse_uint64(const char *text, size_t size,
                                              unsigned int base,
                                              uint64_t *out_value) {
  uint64_t value = 0U;
  size_t index;

  if (size == 0U) {
    return 0;
  }
  for (index = 0U; index < size; ++index) {
    unsigned int digit;
    const char current = text[index];
    if (current >= '0' && current <= '9') {
      digit = (unsigned int)(current - '0');
    } else if (current >= 'a' && current <= 'f') {
      digit = (unsigned int)(current - 'a' + 10);
    } else if (current >= 'A' && current <= 'F') {
      digit = (unsigned int)(current - 'A' + 10);
    } else {
      return 0;
    }
    if (digit >= base || value > (UINT64_MAX - digit) / base) {
      return 0;
    }
    value = value * base + digit;
  }
  *out_value = value;
  return 1;
}

static int confit_v2_expression_parse_float(const char *text, size_t size,
                                             double *out_value) {
  double value = 0.0;
  double fraction_scale = 1.0;
  int exponent = 0;
  int exponent_negative = 0;
  size_t cursor = 0U;

  while (cursor < size && text[cursor] >= '0' && text[cursor] <= '9') {
    value = value * 10.0 + (double)(text[cursor] - '0');
    cursor += 1U;
  }
  if (cursor < size && text[cursor] == '.') {
    cursor += 1U;
    while (cursor < size && text[cursor] >= '0' && text[cursor] <= '9') {
      fraction_scale *= 0.1;
      value += (double)(text[cursor] - '0') * fraction_scale;
      cursor += 1U;
    }
  }
  if (cursor < size && (text[cursor] == 'e' || text[cursor] == 'E')) {
    cursor += 1U;
    if (cursor < size && (text[cursor] == '+' || text[cursor] == '-')) {
      exponent_negative = text[cursor] == '-';
      cursor += 1U;
    }
    while (cursor < size && text[cursor] >= '0' && text[cursor] <= '9') {
      if (exponent > 10000) {
        return 0;
      }
      exponent = exponent * 10 + (text[cursor] - '0');
      cursor += 1U;
    }
  }
  if (cursor != size || exponent > 308) {
    return 0;
  }
  while (exponent > 0) {
    value = exponent_negative ? value * 0.1 : value * 10.0;
    exponent -= 1;
  }
  if (!isfinite(value)) {
    return 0;
  }
  *out_value = value;
  return 1;
}

static ConfitStatus confit_v2_expression_parse_conditional(
    ConfitV2ExpressionParser *parser, size_t nesting,
    ConfitV2ExpressionNode **out_node, ConfitDiagnostic *diagnostic);

static ConfitStatus confit_v2_expression_parse_list(
    ConfitV2ExpressionParser *parser, size_t nesting,
    ConfitV2ExpressionNode **out_node, ConfitDiagnostic *diagnostic) {
  const size_t start = parser->current.start_offset;
  ConfitV2ExpressionNode **items = 0;
  size_t item_count = 0U;
  ConfitStatus status;

  status = confit_v2_expression_advance(parser, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  if (parser->current.kind != CONFIT_V2_TOKEN_RIGHT_BRACKET) {
    for (;;) {
      ConfitV2ExpressionNode *item = 0;
      ConfitV2ExpressionNode **grown;
      status = confit_v2_expression_parse_conditional(parser, nesting + 1U,
                                                       &item, diagnostic);
      if (status != CONFIT_OK) {
        size_t index;
        for (index = 0U; index < item_count; ++index) {
          confit_v2_expression_node_free(items[index]);
        }
        free(items);
        return status;
      }
      if (item_count == SIZE_MAX / sizeof(*items)) {
        confit_v2_expression_node_free(item);
        free(items);
        confit_v2_expression_diagnostic_at(parser->source, start,
                                           CONFIT_ERR_INTERNAL,
                                           kExpressionAllocationFailed,
                                           diagnostic);
        return CONFIT_ERR_INTERNAL;
      }
      grown = (ConfitV2ExpressionNode **)realloc(
          items, (item_count + 1U) * sizeof(*items));
      if (grown == 0) {
        size_t index;
        confit_v2_expression_node_free(item);
        for (index = 0U; index < item_count; ++index) {
          confit_v2_expression_node_free(items[index]);
        }
        free(items);
        confit_v2_expression_diagnostic_at(parser->source, start,
                                           CONFIT_ERR_INTERNAL,
                                           kExpressionAllocationFailed,
                                           diagnostic);
        return CONFIT_ERR_INTERNAL;
      }
      items = grown;
      items[item_count++] = item;
      if (parser->current.kind != CONFIT_V2_TOKEN_COMMA) {
        break;
      }
      status = confit_v2_expression_advance(parser, diagnostic);
      if (status != CONFIT_OK) {
        size_t index;
        for (index = 0U; index < item_count; ++index) {
          confit_v2_expression_node_free(items[index]);
        }
        free(items);
        return status;
      }
    }
  }
  if (parser->current.kind != CONFIT_V2_TOKEN_RIGHT_BRACKET) {
    size_t index;
    for (index = 0U; index < item_count; ++index) {
      confit_v2_expression_node_free(items[index]);
    }
    free(items);
    confit_v2_expression_diagnostic_at(parser->source, parser->current.start_offset,
                                       CONFIT_ERR_SCHEMA, kExpressionExpectedRightBracket,
                                       diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  {
    const size_t end = parser->current.end_offset;
    ConfitV2ExpressionNode *node;
    status = confit_v2_expression_advance(parser, diagnostic);
    if (status != CONFIT_OK) {
      size_t index;
      for (index = 0U; index < item_count; ++index) {
        confit_v2_expression_node_free(items[index]);
      }
      free(items);
      return status;
    }
    status = confit_v2_expression_make_node(parser, CONFIT_V2_EXPRESSION_NODE_LIST,
                                             start, end, &node, diagnostic);
    if (status != CONFIT_OK) {
      size_t index;
      for (index = 0U; index < item_count; ++index) {
        confit_v2_expression_node_free(items[index]);
      }
      free(items);
      return status;
    }
    node->as.list.items = items;
    node->as.list.item_count = item_count;
    *out_node = node;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_expression_parse_call(
    ConfitV2ExpressionParser *parser, const ConfitV2ExpressionToken *name_token,
    size_t nesting, ConfitV2ExpressionNode **out_node,
    ConfitDiagnostic *diagnostic) {
  char *name = confit_v2_expression_token_copy(parser, name_token, diagnostic);
  ConfitV2ExpressionNode **arguments = 0;
  size_t argument_count = 0U;
  ConfitStatus status;

  if (name == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  status = confit_v2_expression_advance(parser, diagnostic);
  if (status != CONFIT_OK) {
    free(name);
    return status;
  }
  if (parser->current.kind != CONFIT_V2_TOKEN_RIGHT_PAREN) {
    for (;;) {
      ConfitV2ExpressionNode *argument = 0;
      ConfitV2ExpressionNode **grown;
      status = confit_v2_expression_parse_conditional(parser, nesting + 1U,
                                                       &argument, diagnostic);
      if (status != CONFIT_OK) {
        size_t index;
        free(name);
        for (index = 0U; index < argument_count; ++index) {
          confit_v2_expression_node_free(arguments[index]);
        }
        free(arguments);
        return status;
      }
      grown = (ConfitV2ExpressionNode **)realloc(
          arguments, (argument_count + 1U) * sizeof(*arguments));
      if (grown == 0) {
        size_t index;
        free(name);
        confit_v2_expression_node_free(argument);
        for (index = 0U; index < argument_count; ++index) {
          confit_v2_expression_node_free(arguments[index]);
        }
        free(arguments);
        confit_v2_expression_diagnostic_at(parser->source, name_token->start_offset,
                                           CONFIT_ERR_INTERNAL,
                                           kExpressionAllocationFailed,
                                           diagnostic);
        return CONFIT_ERR_INTERNAL;
      }
      arguments = grown;
      arguments[argument_count++] = argument;
      if (parser->current.kind != CONFIT_V2_TOKEN_COMMA) {
        break;
      }
      status = confit_v2_expression_advance(parser, diagnostic);
      if (status != CONFIT_OK) {
        size_t index;
        free(name);
        for (index = 0U; index < argument_count; ++index) {
          confit_v2_expression_node_free(arguments[index]);
        }
        free(arguments);
        return status;
      }
    }
  }
  if (parser->current.kind != CONFIT_V2_TOKEN_RIGHT_PAREN) {
    size_t index;
    free(name);
    for (index = 0U; index < argument_count; ++index) {
      confit_v2_expression_node_free(arguments[index]);
    }
    free(arguments);
    confit_v2_expression_diagnostic_at(parser->source, parser->current.start_offset,
                                       CONFIT_ERR_SCHEMA, kExpressionExpectedRightParen,
                                       diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  {
    const size_t end = parser->current.end_offset;
    ConfitV2ExpressionNode *node;
    status = confit_v2_expression_advance(parser, diagnostic);
    if (status != CONFIT_OK) {
      size_t index;
      free(name);
      for (index = 0U; index < argument_count; ++index) {
        confit_v2_expression_node_free(arguments[index]);
      }
      free(arguments);
      return status;
    }
    status = confit_v2_expression_make_node(parser, CONFIT_V2_EXPRESSION_NODE_CALL,
                                             name_token->start_offset, end, &node,
                                             diagnostic);
    if (status != CONFIT_OK) {
      size_t index;
      free(name);
      for (index = 0U; index < argument_count; ++index) {
        confit_v2_expression_node_free(arguments[index]);
      }
      free(arguments);
      return status;
    }
    node->as.call.function_name = name;
    node->as.call.arguments = arguments;
    node->as.call.argument_count = argument_count;
    *out_node = node;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_expression_parse_primary(
    ConfitV2ExpressionParser *parser, size_t nesting,
    ConfitV2ExpressionNode **out_node, ConfitDiagnostic *diagnostic) {
  const ConfitV2ExpressionToken token = parser->current;
  ConfitStatus status;
  ConfitV2ExpressionNode *node;

  if (nesting > parser->limits.max_nesting) {
    confit_v2_expression_diagnostic_at(parser->source, token.start_offset,
                                       CONFIT_ERR_SCHEMA, kExpressionNestingLimit,
                                       diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  if (token.kind == CONFIT_V2_TOKEN_LEFT_PAREN) {
    status = confit_v2_expression_advance(parser, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    status = confit_v2_expression_parse_conditional(parser, nesting + 1U, out_node,
                                                     diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    if (parser->current.kind != CONFIT_V2_TOKEN_RIGHT_PAREN) {
      confit_v2_expression_node_free(*out_node);
      *out_node = 0;
      confit_v2_expression_diagnostic_at(parser->source, parser->current.start_offset,
                                         CONFIT_ERR_SCHEMA,
                                         kExpressionExpectedRightParen,
                                         diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    (*out_node)->start_offset = token.start_offset;
    (*out_node)->end_offset = parser->current.end_offset;
    return confit_v2_expression_advance(parser, diagnostic);
  }
  if (token.kind == CONFIT_V2_TOKEN_LEFT_BRACKET) {
    return confit_v2_expression_parse_list(parser, nesting, out_node, diagnostic);
  }
  if (token.kind == CONFIT_V2_TOKEN_IDENTIFIER) {
    status = confit_v2_expression_advance(parser, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    if (parser->current.kind == CONFIT_V2_TOKEN_LEFT_PAREN) {
      return confit_v2_expression_parse_call(parser, &token, nesting, out_node,
                                             diagnostic);
    }
    if (memchr(parser->source->text + token.start_offset, '.',
              token.end_offset - token.start_offset) == 0) {
      confit_v2_expression_diagnostic_at(parser->source, token.start_offset,
                                         CONFIT_ERR_SCHEMA, kExpressionExpectedPrimary,
                                         diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    status = confit_v2_expression_make_node(parser, CONFIT_V2_EXPRESSION_NODE_REFERENCE,
                                             token.start_offset, token.end_offset,
                                             &node, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    node->as.reference.option_id = confit_v2_expression_token_copy(parser, &token,
                                                                      diagnostic);
    if (node->as.reference.option_id == 0) {
      confit_v2_expression_node_free(node);
      return CONFIT_ERR_INTERNAL;
    }
    *out_node = node;
    return CONFIT_OK;
  }
  if (token.kind == CONFIT_V2_TOKEN_TRUE || token.kind == CONFIT_V2_TOKEN_FALSE ||
      token.kind == CONFIT_V2_TOKEN_TRISTATE ||
      token.kind == CONFIT_V2_TOKEN_INTEGER || token.kind == CONFIT_V2_TOKEN_HEX ||
      token.kind == CONFIT_V2_TOKEN_FLOAT || token.kind == CONFIT_V2_TOKEN_STRING) {
    status = confit_v2_expression_make_node(parser, CONFIT_V2_EXPRESSION_NODE_LITERAL,
                                             token.start_offset, token.end_offset,
                                             &node, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    if (token.kind == CONFIT_V2_TOKEN_TRUE || token.kind == CONFIT_V2_TOKEN_FALSE) {
      node->as.literal.kind = CONFIT_V2_EXPRESSION_LITERAL_BOOL;
      node->as.literal.value.bool_value = token.kind == CONFIT_V2_TOKEN_TRUE;
    } else if (token.kind == CONFIT_V2_TOKEN_TRISTATE) {
      node->as.literal.kind = CONFIT_V2_EXPRESSION_LITERAL_TRISTATE;
      node->as.literal.value.tristate_value =
          parser->source->text[token.start_offset];
    } else if (token.kind == CONFIT_V2_TOKEN_INTEGER) {
      uint64_t raw_value;
      if (!confit_v2_expression_parse_uint64(
              parser->source->text + token.start_offset,
              token.end_offset - token.start_offset, 10U, &raw_value) ||
          raw_value > (uint64_t)INT64_MAX) {
        confit_v2_expression_node_free(node);
        confit_v2_expression_diagnostic_at(parser->source, token.start_offset,
                                           CONFIT_ERR_SCHEMA,
                                           kExpressionInvalidNumber, diagnostic);
        return CONFIT_ERR_SCHEMA;
      }
      node->as.literal.kind = CONFIT_V2_EXPRESSION_LITERAL_INT;
      node->as.literal.value.int_value = (int64_t)raw_value;
    } else if (token.kind == CONFIT_V2_TOKEN_HEX) {
      if (!confit_v2_expression_parse_uint64(
              parser->source->text + token.start_offset + 2U,
              token.end_offset - token.start_offset - 2U, 16U,
              &node->as.literal.value.hex_value)) {
        confit_v2_expression_node_free(node);
        confit_v2_expression_diagnostic_at(parser->source, token.start_offset,
                                           CONFIT_ERR_SCHEMA,
                                           kExpressionInvalidNumber, diagnostic);
        return CONFIT_ERR_SCHEMA;
      }
      node->as.literal.kind = CONFIT_V2_EXPRESSION_LITERAL_HEX;
    } else if (token.kind == CONFIT_V2_TOKEN_FLOAT) {
      if (!confit_v2_expression_parse_float(
              parser->source->text + token.start_offset,
              token.end_offset - token.start_offset,
              &node->as.literal.value.float_value)) {
        confit_v2_expression_node_free(node);
        confit_v2_expression_diagnostic_at(parser->source, token.start_offset,
                                           CONFIT_ERR_SCHEMA,
                                           kExpressionInvalidNumber, diagnostic);
        return CONFIT_ERR_SCHEMA;
      }
      node->as.literal.kind = CONFIT_V2_EXPRESSION_LITERAL_FLOAT;
    } else {
      status = confit_v2_expression_token_string(parser->source, &token,
                                                  &node->as.literal.value.string_value,
                                                  diagnostic);
      if (status != CONFIT_OK) {
        confit_v2_expression_node_free(node);
        return status;
      }
      node->as.literal.kind = CONFIT_V2_EXPRESSION_LITERAL_STRING;
    }
    status = confit_v2_expression_advance(parser, diagnostic);
    if (status != CONFIT_OK) {
      confit_v2_expression_node_free(node);
      return status;
    }
    *out_node = node;
    return CONFIT_OK;
  }
  confit_v2_expression_diagnostic_at(parser->source, token.start_offset,
                                     CONFIT_ERR_SCHEMA, kExpressionExpectedPrimary,
                                     diagnostic);
  return CONFIT_ERR_SCHEMA;
}

static ConfitStatus confit_v2_expression_parse_unary(
    ConfitV2ExpressionParser *parser, size_t nesting,
    ConfitV2ExpressionNode **out_node, ConfitDiagnostic *diagnostic) {
  ConfitV2ExpressionToken token = parser->current;
  ConfitV2ExpressionUnaryOperator operator_kind;
  ConfitV2ExpressionNode *operand;
  ConfitV2ExpressionNode *node;
  ConfitStatus status;

  if (token.kind != CONFIT_V2_TOKEN_NOT && token.kind != CONFIT_V2_TOKEN_MINUS &&
      token.kind != CONFIT_V2_TOKEN_PLUS) {
    return confit_v2_expression_parse_primary(parser, nesting, out_node, diagnostic);
  }
  operator_kind = token.kind == CONFIT_V2_TOKEN_NOT
                      ? CONFIT_V2_EXPRESSION_UNARY_NOT
                      : (token.kind == CONFIT_V2_TOKEN_MINUS
                             ? CONFIT_V2_EXPRESSION_UNARY_NEGATE
                             : CONFIT_V2_EXPRESSION_UNARY_PLUS);
  status = confit_v2_expression_advance(parser, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  operand = 0;
  status = confit_v2_expression_parse_unary(parser, nesting + 1U, &operand,
                                            diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_v2_expression_make_node(parser, CONFIT_V2_EXPRESSION_NODE_UNARY,
                                           token.start_offset, operand->end_offset,
                                           &node, diagnostic);
  if (status != CONFIT_OK) {
    confit_v2_expression_node_free(operand);
    return status;
  }
  node->as.unary.operator_kind = operator_kind;
  node->as.unary.operand = operand;
  *out_node = node;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_expression_parse_binary_chain(
    ConfitV2ExpressionParser *parser, size_t nesting,
    ConfitV2ExpressionNode **out_node, ConfitDiagnostic *diagnostic,
    int precedence);

static int confit_v2_expression_operator_precedence(
    ConfitV2ExpressionTokenKind kind, ConfitV2ExpressionBinaryOperator *out) {
  switch (kind) {
  case CONFIT_V2_TOKEN_OR:
    *out = CONFIT_V2_EXPRESSION_BINARY_OR;
    return 1;
  case CONFIT_V2_TOKEN_AND:
    *out = CONFIT_V2_EXPRESSION_BINARY_AND;
    return 2;
  case CONFIT_V2_TOKEN_EQUAL:
    *out = CONFIT_V2_EXPRESSION_BINARY_EQUAL;
    return 3;
  case CONFIT_V2_TOKEN_NOT_EQUAL:
    *out = CONFIT_V2_EXPRESSION_BINARY_NOT_EQUAL;
    return 3;
  case CONFIT_V2_TOKEN_LESS:
    *out = CONFIT_V2_EXPRESSION_BINARY_LESS;
    return 4;
  case CONFIT_V2_TOKEN_LESS_EQUAL:
    *out = CONFIT_V2_EXPRESSION_BINARY_LESS_EQUAL;
    return 4;
  case CONFIT_V2_TOKEN_GREATER:
    *out = CONFIT_V2_EXPRESSION_BINARY_GREATER;
    return 4;
  case CONFIT_V2_TOKEN_GREATER_EQUAL:
    *out = CONFIT_V2_EXPRESSION_BINARY_GREATER_EQUAL;
    return 4;
  case CONFIT_V2_TOKEN_IN:
    *out = CONFIT_V2_EXPRESSION_BINARY_IN;
    return 4;
  case CONFIT_V2_TOKEN_PLUS:
    *out = CONFIT_V2_EXPRESSION_BINARY_ADD;
    return 5;
  case CONFIT_V2_TOKEN_MINUS:
    *out = CONFIT_V2_EXPRESSION_BINARY_SUBTRACT;
    return 5;
  case CONFIT_V2_TOKEN_STAR:
    *out = CONFIT_V2_EXPRESSION_BINARY_MULTIPLY;
    return 6;
  case CONFIT_V2_TOKEN_SLASH:
    *out = CONFIT_V2_EXPRESSION_BINARY_DIVIDE;
    return 6;
  case CONFIT_V2_TOKEN_PERCENT:
    *out = CONFIT_V2_EXPRESSION_BINARY_MODULO;
    return 6;
  default:
    return 0;
  }
}

static ConfitStatus confit_v2_expression_parse_binary_chain(
    ConfitV2ExpressionParser *parser, size_t nesting,
    ConfitV2ExpressionNode **out_node, ConfitDiagnostic *diagnostic,
    int precedence) {
  ConfitV2ExpressionNode *left;
  ConfitStatus status;
  int saw_relation;

  if (precedence > 6) {
    return confit_v2_expression_parse_unary(parser, nesting, out_node, diagnostic);
  }
  left = 0;
  status = confit_v2_expression_parse_binary_chain(parser, nesting, &left,
                                                    diagnostic, precedence + 1);
  if (status != CONFIT_OK) {
    return status;
  }
  saw_relation = 0;
  for (;;) {
    ConfitV2ExpressionBinaryOperator operator_kind;
    const int token_precedence =
        confit_v2_expression_operator_precedence(parser->current.kind,
                                                  &operator_kind);
    ConfitV2ExpressionToken operator_token;
    ConfitV2ExpressionNode *right;
    ConfitV2ExpressionNode *node;
    if (token_precedence != precedence) {
      break;
    }
    if (precedence == 4 && saw_relation) {
      confit_v2_expression_diagnostic_at(
          parser->source, parser->current.start_offset, CONFIT_ERR_SCHEMA,
          kExpressionChainedRelation, diagnostic);
      confit_v2_expression_node_free(left);
      return CONFIT_ERR_SCHEMA;
    }
    operator_token = parser->current;
    status = confit_v2_expression_advance(parser, diagnostic);
    if (status != CONFIT_OK) {
      confit_v2_expression_node_free(left);
      return status;
    }
    right = 0;
    status = confit_v2_expression_parse_binary_chain(parser, nesting, &right,
                                                      diagnostic, precedence + 1);
    if (status != CONFIT_OK) {
      confit_v2_expression_node_free(left);
      return status;
    }
    status = confit_v2_expression_make_node(parser, CONFIT_V2_EXPRESSION_NODE_BINARY,
                                             left->start_offset, right->end_offset,
                                             &node, diagnostic);
    if (status != CONFIT_OK) {
      confit_v2_expression_node_free(left);
      confit_v2_expression_node_free(right);
      return status;
    }
    node->as.binary.operator_kind = operator_kind;
    node->as.binary.left = left;
    node->as.binary.right = right;
    left = node;
    if (precedence == 4) {
      saw_relation = 1;
    }
    (void)operator_token;
  }
  *out_node = left;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_expression_parse_conditional(
    ConfitV2ExpressionParser *parser, size_t nesting,
    ConfitV2ExpressionNode **out_node, ConfitDiagnostic *diagnostic) {
  ConfitV2ExpressionNode *condition;
  ConfitStatus status;

  condition = 0;
  status = confit_v2_expression_parse_binary_chain(parser, nesting, &condition,
                                                    diagnostic, 1);
  if (status != CONFIT_OK) {
    return status;
  }
  if (parser->current.kind == CONFIT_V2_TOKEN_QUESTION) {
    ConfitV2ExpressionNode *when_true;
    ConfitV2ExpressionNode *when_false;
    ConfitV2ExpressionNode *node;
    const size_t start = condition->start_offset;

    status = confit_v2_expression_advance(parser, diagnostic);
    if (status != CONFIT_OK) {
      confit_v2_expression_node_free(condition);
      return status;
    }
    when_true = 0;
    status = confit_v2_expression_parse_conditional(parser, nesting + 1U,
                                                     &when_true, diagnostic);
    if (status != CONFIT_OK) {
      confit_v2_expression_node_free(condition);
      return status;
    }
    if (parser->current.kind != CONFIT_V2_TOKEN_COLON) {
      confit_v2_expression_node_free(condition);
      confit_v2_expression_node_free(when_true);
      confit_v2_expression_diagnostic_at(parser->source, parser->current.start_offset,
                                         CONFIT_ERR_SCHEMA, kExpressionExpectedColon,
                                         diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    status = confit_v2_expression_advance(parser, diagnostic);
    if (status != CONFIT_OK) {
      confit_v2_expression_node_free(condition);
      confit_v2_expression_node_free(when_true);
      return status;
    }
    when_false = 0;
    status = confit_v2_expression_parse_conditional(parser, nesting + 1U,
                                                     &when_false, diagnostic);
    if (status != CONFIT_OK) {
      confit_v2_expression_node_free(condition);
      confit_v2_expression_node_free(when_true);
      return status;
    }
    status = confit_v2_expression_make_node(
        parser, CONFIT_V2_EXPRESSION_NODE_CONDITIONAL, start,
        when_false->end_offset, &node, diagnostic);
    if (status != CONFIT_OK) {
      confit_v2_expression_node_free(condition);
      confit_v2_expression_node_free(when_true);
      confit_v2_expression_node_free(when_false);
      return status;
    }
    node->as.conditional.condition = condition;
    node->as.conditional.when_true = when_true;
    node->as.conditional.when_false = when_false;
    *out_node = node;
    return CONFIT_OK;
  }
  *out_node = condition;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_expression_copy_span(
    const ConfitV2ExpressionText *source, ConfitV2SourceSpan *out,
    ConfitDiagnostic *diagnostic) {
  memset(out, 0, sizeof(*out));
  if (source->span.path != 0) {
    const size_t size = strlen(source->span.path);
    out->path = (char *)malloc(size + 1U);
    if (out->path == 0) {
      confit_v2_expression_diagnostic_at(source, 0U, CONFIT_ERR_INTERNAL,
                                         kExpressionAllocationFailed, diagnostic);
      return CONFIT_ERR_INTERNAL;
    }
    memcpy(out->path, source->span.path, size + 1U);
  }
  out->line = source->span.line;
  out->column = source->span.column;
  out->local_offset = source->span.local_offset;
  return CONFIT_OK;
}

ConfitStatus confit_v2_expression_parse(
    const ConfitV2ExpressionText *source,
    const ConfitV2ExpressionLimits *requested_limits,
    ConfitV2Expression **out_expression, ConfitDiagnostic *diagnostic) {
  ConfitV2ExpressionParser parser;
  ConfitV2Expression *expression;
  const size_t source_size = source != 0 && source->text != 0 ? strlen(source->text) : 0U;
  ConfitStatus status;

  if (source == 0 || source->text == 0 || out_expression == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                          source != 0 ? source->span.path : 0, 0, 0,
                          "invalid expression parser argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_expression = 0;
  memset(&parser, 0, sizeof(parser));
  parser.source = source;
  parser.limits = confit_v2_expression_effective_limits(requested_limits);
  if (source_size > parser.limits.max_source_bytes) {
    confit_v2_expression_diagnostic_at(source, parser.limits.max_source_bytes,
                                       CONFIT_ERR_SCHEMA, kExpressionSourceLimit,
                                       diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  confit_v2_expression_lexer_init(&parser.lexer, source, parser.limits.max_tokens);
  status = confit_v2_expression_advance(&parser, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  expression = (ConfitV2Expression *)calloc(1U, sizeof(*expression));
  if (expression == 0) {
    confit_v2_expression_diagnostic_at(source, 0U, CONFIT_ERR_INTERNAL,
                                       kExpressionAllocationFailed, diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  expression->source = (char *)malloc(source_size + 1U);
  if (expression->source == 0) {
    free(expression);
    confit_v2_expression_diagnostic_at(source, 0U, CONFIT_ERR_INTERNAL,
                                       kExpressionAllocationFailed, diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  memcpy(expression->source, source->text, source_size + 1U);
  status = confit_v2_expression_copy_span(source, &expression->source_span,
                                          diagnostic);
  if (status != CONFIT_OK) {
    confit_v2_expression_free(expression);
    return status;
  }
  status = confit_v2_expression_parse_conditional(&parser, 1U, &expression->root,
                                                   diagnostic);
  if (status != CONFIT_OK) {
    confit_v2_expression_free(expression);
    return status;
  }
  if (parser.current.kind != CONFIT_V2_TOKEN_EOF) {
    confit_v2_expression_diagnostic_at(source, parser.current.start_offset,
                                       CONFIT_ERR_SCHEMA, kExpressionTrailingToken,
                                       diagnostic);
    confit_v2_expression_free(expression);
    return CONFIT_ERR_SCHEMA;
  }
  *out_expression = expression;
  return CONFIT_OK;
}

static int confit_v2_expression_writer_reserve(ConfitV2ExpressionWriter *writer,
                                                size_t additional) {
  size_t needed;
  size_t capacity;
  char *grown;

  if (additional > SIZE_MAX - writer->length - 1U) {
    return 0;
  }
  needed = writer->length + additional + 1U;
  if (needed <= writer->capacity) {
    return 1;
  }
  capacity = writer->capacity == 0U ? 64U : writer->capacity;
  while (capacity < needed) {
    if (capacity > SIZE_MAX / 2U) {
      capacity = needed;
      break;
    }
    capacity *= 2U;
  }
  grown = (char *)realloc(writer->text, capacity);
  if (grown == 0) {
    return 0;
  }
  writer->text = grown;
  writer->capacity = capacity;
  return 1;
}

static int confit_v2_expression_writer_text(ConfitV2ExpressionWriter *writer,
                                             const char *text) {
  const size_t size = strlen(text);
  if (!confit_v2_expression_writer_reserve(writer, size)) {
    return 0;
  }
  memcpy(writer->text + writer->length, text, size);
  writer->length += size;
  writer->text[writer->length] = '\0';
  return 1;
}

static const char *confit_v2_expression_unary_name(
    ConfitV2ExpressionUnaryOperator operator_kind) {
  switch (operator_kind) {
  case CONFIT_V2_EXPRESSION_UNARY_NOT:
    return "!";
  case CONFIT_V2_EXPRESSION_UNARY_NEGATE:
    return "-";
  case CONFIT_V2_EXPRESSION_UNARY_PLUS:
    return "+";
  default:
    return "?";
  }
}

static const char *confit_v2_expression_binary_name(
    ConfitV2ExpressionBinaryOperator operator_kind) {
  static const char *const names[] = {
      "?",  "||", "&&", "==", "!=", "<",  "<=", ">",
      ">=", "in", "+",  "-",  "*",  "/",  "%",
  };
  if ((size_t)operator_kind >= sizeof(names) / sizeof(names[0])) {
    return "?";
  }
  return names[operator_kind];
}

static int confit_v2_expression_writer_quoted(ConfitV2ExpressionWriter *writer,
                                               const char *text) {
  const unsigned char *cursor = (const unsigned char *)text;
  if (!confit_v2_expression_writer_text(writer, "\"")) {
    return 0;
  }
  while (*cursor != '\0') {
    char escaped[3];
    if (*cursor == '"' || *cursor == '\\') {
      escaped[0] = '\\';
      escaped[1] = (char)*cursor;
      escaped[2] = '\0';
      if (!confit_v2_expression_writer_text(writer, escaped)) {
        return 0;
      }
    } else if (*cursor == '\n') {
      if (!confit_v2_expression_writer_text(writer, "\\n")) {
        return 0;
      }
    } else {
      escaped[0] = (char)*cursor;
      escaped[1] = '\0';
      if (!confit_v2_expression_writer_text(writer, escaped)) {
        return 0;
      }
    }
    cursor += 1U;
  }
  return confit_v2_expression_writer_text(writer, "\"");
}

static int confit_v2_expression_write_node(ConfitV2ExpressionWriter *writer,
                                            const ConfitV2ExpressionNode *node) {
  size_t index;
  char number[64];

  if (node == 0) {
    return confit_v2_expression_writer_text(writer, "<null>");
  }
  switch (node->kind) {
  case CONFIT_V2_EXPRESSION_NODE_LITERAL:
    switch (node->as.literal.kind) {
    case CONFIT_V2_EXPRESSION_LITERAL_BOOL:
      return confit_v2_expression_writer_text(
          writer, node->as.literal.value.bool_value ? "true" : "false");
    case CONFIT_V2_EXPRESSION_LITERAL_TRISTATE:
      number[0] = node->as.literal.value.tristate_value;
      number[1] = '\0';
      return confit_v2_expression_writer_text(writer, number);
    case CONFIT_V2_EXPRESSION_LITERAL_INT:
      (void)snprintf(number, sizeof(number), "%lld",
                     (long long)node->as.literal.value.int_value);
      return confit_v2_expression_writer_text(writer, number);
    case CONFIT_V2_EXPRESSION_LITERAL_HEX:
      (void)snprintf(number, sizeof(number), "0x%llx",
                     (unsigned long long)node->as.literal.value.hex_value);
      return confit_v2_expression_writer_text(writer, number);
    case CONFIT_V2_EXPRESSION_LITERAL_FLOAT:
      (void)snprintf(number, sizeof(number), "%.17g",
                     node->as.literal.value.float_value);
      return confit_v2_expression_writer_text(writer, number);
    case CONFIT_V2_EXPRESSION_LITERAL_STRING:
      return confit_v2_expression_writer_quoted(writer,
                                                 node->as.literal.value.string_value);
    default:
      return 0;
    }
  case CONFIT_V2_EXPRESSION_NODE_REFERENCE:
    return confit_v2_expression_writer_text(writer, node->as.reference.option_id);
  case CONFIT_V2_EXPRESSION_NODE_UNARY:
    return confit_v2_expression_writer_text(writer, "(") &&
           confit_v2_expression_writer_text(
               writer, confit_v2_expression_unary_name(node->as.unary.operator_kind)) &&
           confit_v2_expression_writer_text(writer, " ") &&
           confit_v2_expression_write_node(writer, node->as.unary.operand) &&
           confit_v2_expression_writer_text(writer, ")");
  case CONFIT_V2_EXPRESSION_NODE_BINARY:
    return confit_v2_expression_writer_text(writer, "(") &&
           confit_v2_expression_writer_text(
               writer, confit_v2_expression_binary_name(node->as.binary.operator_kind)) &&
           confit_v2_expression_writer_text(writer, " ") &&
           confit_v2_expression_write_node(writer, node->as.binary.left) &&
           confit_v2_expression_writer_text(writer, " ") &&
           confit_v2_expression_write_node(writer, node->as.binary.right) &&
           confit_v2_expression_writer_text(writer, ")");
  case CONFIT_V2_EXPRESSION_NODE_CONDITIONAL:
    return confit_v2_expression_writer_text(writer, "(?: ") &&
           confit_v2_expression_write_node(writer, node->as.conditional.condition) &&
           confit_v2_expression_writer_text(writer, " ") &&
           confit_v2_expression_write_node(writer, node->as.conditional.when_true) &&
           confit_v2_expression_writer_text(writer, " ") &&
           confit_v2_expression_write_node(writer, node->as.conditional.when_false) &&
           confit_v2_expression_writer_text(writer, ")");
  case CONFIT_V2_EXPRESSION_NODE_CALL:
    if (!confit_v2_expression_writer_text(writer, "(") ||
        !confit_v2_expression_writer_text(writer, node->as.call.function_name)) {
      return 0;
    }
    for (index = 0U; index < node->as.call.argument_count; ++index) {
      if (!confit_v2_expression_writer_text(writer, " ") ||
          !confit_v2_expression_write_node(writer, node->as.call.arguments[index])) {
        return 0;
      }
    }
    return confit_v2_expression_writer_text(writer, ")");
  case CONFIT_V2_EXPRESSION_NODE_LIST:
    if (!confit_v2_expression_writer_text(writer, "[")) {
      return 0;
    }
    for (index = 0U; index < node->as.list.item_count; ++index) {
      if ((index != 0U && !confit_v2_expression_writer_text(writer, " ")) ||
          !confit_v2_expression_write_node(writer, node->as.list.items[index])) {
        return 0;
      }
    }
    return confit_v2_expression_writer_text(writer, "]");
  default:
    return 0;
  }
}

ConfitStatus confit_v2_expression_to_sexpr(const ConfitV2Expression *expression,
                                            char **out_text) {
  ConfitV2ExpressionWriter writer;

  if (expression == 0 || expression->root == 0 || out_text == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_text = 0;
  memset(&writer, 0, sizeof(writer));
  if (!confit_v2_expression_write_node(&writer, expression->root)) {
    free(writer.text);
    return CONFIT_ERR_INTERNAL;
  }
  *out_text = writer.text;
  return CONFIT_OK;
}

void confit_v2_expression_string_free(char *text) { free(text); }
