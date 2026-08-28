#include "confit/expression.h"

#include "expression_internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "confit/limits.h"

typedef enum ConfitExpressionNodeKind {
  CONFIT_EXPRESSION_NODE_INVALID = 0,
  CONFIT_EXPRESSION_NODE_LITERAL,
  CONFIT_EXPRESSION_NODE_REFERENCE,
  CONFIT_EXPRESSION_NODE_NOT,
  CONFIT_EXPRESSION_NODE_AND,
  CONFIT_EXPRESSION_NODE_OR,
  CONFIT_EXPRESSION_NODE_EQUAL,
  CONFIT_EXPRESSION_NODE_NOT_EQUAL,
} ConfitExpressionNodeKind;

typedef struct ConfitExpressionNode {
  ConfitExpressionNodeKind kind;
  size_t left;
  size_t right;
  size_t reference;
  int compared_reference;
  ConfitValue literal;
} ConfitExpressionNode;

typedef struct ConfitCompiledExpression {
  ConfitExpressionNode *nodes;
  size_t node_count;
  size_t node_capacity;
  size_t root;
  size_t *references;
  size_t reference_count;
  size_t reference_capacity;
} ConfitCompiledExpression;

struct ConfitDependencyPlan {
  ConfitAllocator allocator;
  const ConfitCatalog *catalog;
  ConfitCompiledExpression *expressions;
  size_t *order;
  size_t config_count;
  size_t edge_count;
};

typedef struct ConfitDependencyReasonRecord {
  ConfitReasonKind kind;
  int result;
  const char *subject_symbol;
  const char *detail;
  size_t children[CONFIT_REASON_CHILD_LIMIT];
  size_t child_count;
} ConfitDependencyReasonRecord;

struct ConfitDependencyEvaluation {
  ConfitAllocator allocator;
  const ConfitDependencyPlan *plan;
  ConfitDependencyReasonRecord *reasons;
  size_t reason_count;
  size_t reason_capacity;
  size_t root;
  int available;
};

typedef enum ConfitTokenKind {
  CONFIT_TOKEN_INVALID = 0,
  CONFIT_TOKEN_END,
  CONFIT_TOKEN_SYMBOL,
  CONFIT_TOKEN_DECIMAL,
  CONFIT_TOKEN_HEXADECIMAL,
  CONFIT_TOKEN_STRING,
  CONFIT_TOKEN_NOT,
  CONFIT_TOKEN_AND,
  CONFIT_TOKEN_OR,
  CONFIT_TOKEN_EQUAL,
  CONFIT_TOKEN_NOT_EQUAL,
  CONFIT_TOKEN_LEFT_PAREN,
  CONFIT_TOKEN_RIGHT_PAREN,
} ConfitTokenKind;

typedef struct ConfitToken {
  ConfitTokenKind kind;
  const char *start;
  size_t size;
} ConfitToken;

typedef struct ConfitParser {
  const ConfitCatalog *catalog;
  ConfitCompiledExpression *expression;
  ConfitAllocator allocator;
  ConfitConfigView owner;
  const char *text;
  size_t text_size;
  size_t cursor;
  size_t nesting;
  ConfitToken token;
  ConfitDiagnostic *diagnostic;
} ConfitParser;

static int confit_symbol_less(const ConfitCatalog *catalog, size_t left,
                              size_t right);

static const char kInvalidArgument[] = "invalid dependency expression argument";
static const char kInvalidAllocator[] = "allocator capability is incomplete";
static const char kOutOfMemory[] = "failed to allocate dependency expression";
static const char kInvalidSyntax[] = "dependency expression has invalid syntax";
static const char kNodeLimit[] = "dependency expression AST node limit is exceeded";
static const char kNestingLimit[] = "dependency expression nesting limit is exceeded";
static const char kUnknownSymbol[] = "dependency expression references an unknown symbol";
static const char kBareNonBoolean[] = "bare dependency reference must have bool type";
static const char kComparisonType[] = "dependency comparison literal has the wrong type";
static const char kEnumDomain[] = "dependency enum literal is outside the declared domain";
static const char kCycle[] = "dependency expression graph contains a cycle";
static const char kValueShape[] = "dependency evaluation values do not match the catalog";
static const char kInternalExpression[] = "dependency expression invariant is invalid";
static const char kTrue[] = "true";
static const char kFalse[] = "false";
static const char kEqual[] = "==";
static const char kNotEqual[] = "!=";

static ConfitStatus confit_expression_fail(ConfitDiagnostic *diagnostic,
                                           ConfitStatus status,
                                           const ConfitConfigView *owner,
                                           const char *message) {
  confit_diagnostic_set(diagnostic, status,
                        owner != 0 ? owner->declaration.path : 0,
                        owner != 0 ? owner->declaration.line : 0U,
                        owner != 0 ? owner->declaration.column : 0U, message);
  return status;
}

static int confit_expression_resolve_allocator(
    const ConfitAllocator *allocator, ConfitAllocator *out_allocator) {
  if (out_allocator == 0) return 0;
  if (allocator == 0) {
    confit_allocator_default(out_allocator);
    return 1;
  }
  if (!confit_allocator_is_valid(allocator)) return 0;
  *out_allocator = *allocator;
  return 1;
}

static void confit_expression_node_init(ConfitExpressionNode *node) {
  memset(node, 0, sizeof(*node));
  node->left = CONFIT_INDEX_NONE;
  node->right = CONFIT_INDEX_NONE;
  node->reference = CONFIT_INDEX_NONE;
  confit_value_init(&node->literal);
}

static void confit_compiled_expression_destroy(
    ConfitCompiledExpression *expression, const ConfitAllocator *allocator) {
  size_t index;
  if (expression == 0 || allocator == 0) return;
  for (index = expression->node_count; index > 0U; --index)
    confit_value_destroy(&expression->nodes[index - 1U].literal);
  if (expression->nodes != 0)
    allocator->deallocate(allocator->context, expression->nodes);
  if (expression->references != 0)
    allocator->deallocate(allocator->context, expression->references);
  memset(expression, 0, sizeof(*expression));
}

static ConfitStatus confit_parser_grow_nodes(ConfitParser *parser) {
  ConfitExpressionNode *replacement;
  size_t capacity;
  size_t bytes;
  if (parser->expression->node_count < parser->expression->node_capacity)
    return CONFIT_OK;
  if (parser->expression->node_count >= CONFIT_LIMIT_DEPENDENCY_AST_NODES)
    return confit_expression_fail(parser->diagnostic, CONFIT_ERR_VALIDATION,
                                  &parser->owner, kNodeLimit);
  capacity = parser->expression->node_capacity == 0U
                 ? 8U
                 : parser->expression->node_capacity * 2U;
  if (capacity > CONFIT_LIMIT_DEPENDENCY_AST_NODES)
    capacity = CONFIT_LIMIT_DEPENDENCY_AST_NODES;
  if (capacity > SIZE_MAX / sizeof(*replacement))
    return confit_expression_fail(parser->diagnostic, CONFIT_ERR_INTERNAL,
                                  &parser->owner, kOutOfMemory);
  bytes = capacity * sizeof(*replacement);
  replacement = (ConfitExpressionNode *)parser->allocator.allocate(
      parser->allocator.context, bytes);
  if (replacement == 0)
    return confit_expression_fail(parser->diagnostic, CONFIT_ERR_INTERNAL,
                                  &parser->owner, kOutOfMemory);
  memset(replacement, 0, bytes);
  if (parser->expression->node_count != 0U)
    memcpy(replacement, parser->expression->nodes,
           parser->expression->node_count * sizeof(*replacement));
  if (parser->expression->nodes != 0)
    parser->allocator.deallocate(parser->allocator.context,
                                 parser->expression->nodes);
  parser->expression->nodes = replacement;
  parser->expression->node_capacity = capacity;
  return CONFIT_OK;
}

static ConfitStatus confit_parser_add_node(ConfitParser *parser,
                                           ConfitExpressionNodeKind kind,
                                           size_t left, size_t right,
                                           size_t *out_index) {
  ConfitExpressionNode *node;
  ConfitStatus status = confit_parser_grow_nodes(parser);
  if (status != CONFIT_OK) return status;
  node = &parser->expression->nodes[parser->expression->node_count];
  confit_expression_node_init(node);
  node->kind = kind;
  node->left = left;
  node->right = right;
  *out_index = parser->expression->node_count++;
  return CONFIT_OK;
}

static int confit_ascii_upper_or_underscore(unsigned char byte) {
  return (byte >= (unsigned char)'A' && byte <= (unsigned char)'Z') ||
         byte == (unsigned char)'_';
}

static int confit_ascii_symbol_byte(unsigned char byte) {
  return confit_ascii_upper_or_underscore(byte) ||
         (byte >= (unsigned char)'0' && byte <= (unsigned char)'9');
}

static int confit_ascii_digit(unsigned char byte) {
  return byte >= (unsigned char)'0' && byte <= (unsigned char)'9';
}

static int confit_ascii_hex_digit(unsigned char byte) {
  return confit_ascii_digit(byte) ||
         (byte >= (unsigned char)'a' && byte <= (unsigned char)'f') ||
         (byte >= (unsigned char)'A' && byte <= (unsigned char)'F');
}

static void confit_parser_next(ConfitParser *parser) {
  const char *text = parser->text;
  size_t cursor = parser->cursor;
  size_t start;
  parser->token.kind = CONFIT_TOKEN_INVALID;
  parser->token.start = text + cursor;
  parser->token.size = 0U;
  while (cursor < parser->text_size && text[cursor] == ' ') ++cursor;
  start = cursor;
  if (cursor == parser->text_size) {
    parser->token.kind = CONFIT_TOKEN_END;
    parser->cursor = cursor;
    return;
  }
  if ((cursor + 4U <= parser->text_size &&
       memcmp(text + cursor, "true", 4U) == 0 &&
       (cursor + 4U == parser->text_size ||
        !confit_ascii_symbol_byte((unsigned char)text[cursor + 4U]))) ||
      (cursor + 5U <= parser->text_size &&
       memcmp(text + cursor, "false", 5U) == 0 &&
       (cursor + 5U == parser->text_size ||
        !confit_ascii_symbol_byte((unsigned char)text[cursor + 5U])))) {
    cursor += text[cursor] == 't' ? 4U : 5U;
    parser->token.kind = CONFIT_TOKEN_SYMBOL;
  } else if (confit_ascii_upper_or_underscore((unsigned char)text[cursor])) {
    ++cursor;
    while (cursor < parser->text_size &&
           confit_ascii_symbol_byte((unsigned char)text[cursor]))
      ++cursor;
    parser->token.kind = CONFIT_TOKEN_SYMBOL;
  } else if (text[cursor] == '"') {
    int escaped = 0;
    ++cursor;
    while (cursor < parser->text_size) {
      unsigned char byte = (unsigned char)text[cursor++];
      if (!escaped && byte == (unsigned char)'"') {
        parser->token.kind = CONFIT_TOKEN_STRING;
        break;
      }
      if (!escaped && byte == (unsigned char)'\\') escaped = 1;
      else escaped = 0;
    }
  } else if (text[cursor] == '0' && cursor + 1U < parser->text_size &&
             text[cursor + 1U] == 'x') {
    cursor += 2U;
    while (cursor < parser->text_size &&
           (confit_ascii_hex_digit((unsigned char)text[cursor]) ||
            text[cursor] == '_'))
      ++cursor;
    parser->token.kind = CONFIT_TOKEN_HEXADECIMAL;
  } else if (confit_ascii_digit((unsigned char)text[cursor]) ||
             ((text[cursor] == '+' || text[cursor] == '-') &&
              cursor + 1U < parser->text_size &&
              confit_ascii_digit((unsigned char)text[cursor + 1U]))) {
    ++cursor;
    while (cursor < parser->text_size &&
           (confit_ascii_digit((unsigned char)text[cursor]) ||
            text[cursor] == '_'))
      ++cursor;
    parser->token.kind = CONFIT_TOKEN_DECIMAL;
  } else {
    ++cursor;
    switch (text[start]) {
      case '!':
        if (cursor < parser->text_size && text[cursor] == '=') {
          ++cursor;
          parser->token.kind = CONFIT_TOKEN_NOT_EQUAL;
        } else {
          parser->token.kind = CONFIT_TOKEN_NOT;
        }
        break;
      case '=':
        if (cursor < parser->text_size && text[cursor] == '=') {
          ++cursor;
          parser->token.kind = CONFIT_TOKEN_EQUAL;
        }
        break;
      case '&':
        if (cursor < parser->text_size && text[cursor] == '&') {
          ++cursor;
          parser->token.kind = CONFIT_TOKEN_AND;
        }
        break;
      case '|':
        if (cursor < parser->text_size && text[cursor] == '|') {
          ++cursor;
          parser->token.kind = CONFIT_TOKEN_OR;
        }
        break;
      case '(':
        parser->token.kind = CONFIT_TOKEN_LEFT_PAREN;
        break;
      case ')':
        parser->token.kind = CONFIT_TOKEN_RIGHT_PAREN;
        break;
      default:
        break;
    }
  }
  parser->token.start = text + start;
  parser->token.size = cursor - start;
  parser->cursor = cursor;
}

static int confit_token_is(const ConfitToken *token, const char *text) {
  size_t size = strlen(text);
  return token->size == size && memcmp(token->start, text, size) == 0;
}

static int confit_parse_digits(const ConfitToken *token, unsigned base,
                               size_t prefix, uint64_t limit,
                               uint64_t *out_value) {
  uint64_t value = 0U;
  size_t index = prefix;
  size_t digits = 0U;
  int previous_underscore = 0;
  if (index >= token->size) return 0;
  for (; index < token->size; ++index) {
    unsigned char byte = (unsigned char)token->start[index];
    unsigned digit;
    if (byte == (unsigned char)'_') {
      if (digits == 0U || previous_underscore || index + 1U == token->size)
        return 0;
      previous_underscore = 1;
      continue;
    }
    if (byte >= (unsigned char)'0' && byte <= (unsigned char)'9')
      digit = (unsigned)(byte - (unsigned char)'0');
    else if (byte >= (unsigned char)'a' && byte <= (unsigned char)'f')
      digit = 10U + (unsigned)(byte - (unsigned char)'a');
    else if (byte >= (unsigned char)'A' && byte <= (unsigned char)'F')
      digit = 10U + (unsigned)(byte - (unsigned char)'A');
    else
      return 0;
    if (digit >= base || value > (limit - (uint64_t)digit) / (uint64_t)base)
      return 0;
    value = value * (uint64_t)base + (uint64_t)digit;
    ++digits;
    previous_underscore = 0;
  }
  if (digits == 0U || previous_underscore) return 0;
  *out_value = value;
  return 1;
}

static int confit_parse_decimal(const ConfitToken *token, int64_t *out_value) {
  int negative = 0;
  size_t prefix = 0U;
  size_t first_digit;
  size_t digits = 0U;
  size_t index;
  uint64_t magnitude;
  uint64_t limit;
  if (token->size != 0U &&
      (token->start[0] == '+' || token->start[0] == '-')) {
    negative = token->start[0] == '-';
    prefix = 1U;
  }
  first_digit = prefix;
  while (first_digit < token->size && token->start[first_digit] == '_')
    ++first_digit;
  for (index = prefix; index < token->size; ++index)
    if (token->start[index] != '_') ++digits;
  if (first_digit >= token->size ||
      (digits > 1U && token->start[first_digit] == '0'))
    return 0;
  limit = negative ? (uint64_t)INT64_MAX + UINT64_C(1)
                   : (uint64_t)INT64_MAX;
  if (!confit_parse_digits(token, 10U, prefix, limit, &magnitude)) return 0;
  if (negative && magnitude == (uint64_t)INT64_MAX + UINT64_C(1))
    *out_value = INT64_MIN;
  else if (negative)
    *out_value = -(int64_t)magnitude;
  else
    *out_value = (int64_t)magnitude;
  return 1;
}

static int confit_hex_value(unsigned char byte, unsigned *out_digit) {
  if (byte >= (unsigned char)'0' && byte <= (unsigned char)'9')
    *out_digit = (unsigned)(byte - (unsigned char)'0');
  else if (byte >= (unsigned char)'a' && byte <= (unsigned char)'f')
    *out_digit = 10U + (unsigned)(byte - (unsigned char)'a');
  else if (byte >= (unsigned char)'A' && byte <= (unsigned char)'F')
    *out_digit = 10U + (unsigned)(byte - (unsigned char)'A');
  else
    return 0;
  return 1;
}

static int confit_append_codepoint(char *buffer, size_t capacity,
                                   size_t *size, uint32_t codepoint) {
  size_t needed;
  if (codepoint == 0U || codepoint > UINT32_C(0x10ffff) ||
      (codepoint >= UINT32_C(0xd800) && codepoint <= UINT32_C(0xdfff)))
    return 0;
  needed = codepoint <= UINT32_C(0x7f)
               ? 1U
               : (codepoint <= UINT32_C(0x7ff)
                      ? 2U
                      : (codepoint <= UINT32_C(0xffff) ? 3U : 4U));
  if (*size > capacity || needed > capacity - *size) return 0;
  if (needed == 1U) {
    buffer[(*size)++] = (char)codepoint;
  } else if (needed == 2U) {
    buffer[(*size)++] = (char)(UINT32_C(0xc0) | (codepoint >> 6U));
    buffer[(*size)++] = (char)(UINT32_C(0x80) | (codepoint & UINT32_C(0x3f)));
  } else if (needed == 3U) {
    buffer[(*size)++] = (char)(UINT32_C(0xe0) | (codepoint >> 12U));
    buffer[(*size)++] =
        (char)(UINT32_C(0x80) | ((codepoint >> 6U) & UINT32_C(0x3f)));
    buffer[(*size)++] = (char)(UINT32_C(0x80) | (codepoint & UINT32_C(0x3f)));
  } else {
    buffer[(*size)++] = (char)(UINT32_C(0xf0) | (codepoint >> 18U));
    buffer[(*size)++] =
        (char)(UINT32_C(0x80) | ((codepoint >> 12U) & UINT32_C(0x3f)));
    buffer[(*size)++] =
        (char)(UINT32_C(0x80) | ((codepoint >> 6U) & UINT32_C(0x3f)));
    buffer[(*size)++] = (char)(UINT32_C(0x80) | (codepoint & UINT32_C(0x3f)));
  }
  return 1;
}

static ConfitStatus confit_parser_decode_string(ConfitParser *parser,
                                                const ConfitToken *token,
                                                ConfitValue *out_value) {
  char *buffer;
  size_t capacity = token->size;
  size_t output = 0U;
  size_t index = 1U;
  ConfitStatus status;
  if (token->size < 2U || token->start[token->size - 1U] != '"')
    return confit_expression_fail(parser->diagnostic, CONFIT_ERR_VALIDATION,
                                  &parser->owner, kInvalidSyntax);
  buffer = (char *)parser->allocator.allocate(parser->allocator.context,
                                               capacity == 0U ? 1U : capacity);
  if (buffer == 0)
    return confit_expression_fail(parser->diagnostic, CONFIT_ERR_INTERNAL,
                                  &parser->owner, kOutOfMemory);
  while (index + 1U < token->size) {
    unsigned char byte = (unsigned char)token->start[index++];
    if (byte != (unsigned char)'\\') {
      buffer[output++] = (char)byte;
      continue;
    }
    if (index + 1U > token->size) {
      status = CONFIT_ERR_VALIDATION;
      goto fail;
    }
    byte = (unsigned char)token->start[index++];
    switch (byte) {
      case '"': buffer[output++] = '"'; break;
      case '\\': buffer[output++] = '\\'; break;
      case 'b': buffer[output++] = '\b'; break;
      case 't': buffer[output++] = '\t'; break;
      case 'n': buffer[output++] = '\n'; break;
      case 'f': buffer[output++] = '\f'; break;
      case 'r': buffer[output++] = '\r'; break;
      case 'u':
      case 'U': {
        size_t digits = byte == (unsigned char)'u' ? 4U : 8U;
        size_t digit_index;
        uint32_t codepoint = 0U;
        if (digits > token->size - 1U - index) {
          status = CONFIT_ERR_VALIDATION;
          goto fail;
        }
        for (digit_index = 0U; digit_index < digits; ++digit_index) {
          unsigned digit;
          if (!confit_hex_value((unsigned char)token->start[index++], &digit)) {
            status = CONFIT_ERR_VALIDATION;
            goto fail;
          }
          codepoint = codepoint * UINT32_C(16) + (uint32_t)digit;
        }
        if (!confit_append_codepoint(buffer, capacity, &output, codepoint)) {
          status = CONFIT_ERR_VALIDATION;
          goto fail;
        }
        break;
      }
      default:
        status = CONFIT_ERR_VALIDATION;
        goto fail;
    }
  }
  status = confit_value_set_string(out_value, buffer, output,
                                   &parser->allocator, parser->diagnostic);
  parser->allocator.deallocate(parser->allocator.context, buffer);
  if (status != CONFIT_OK)
    return confit_expression_fail(parser->diagnostic, status, &parser->owner,
                                  kComparisonType);
  return CONFIT_OK;

fail:
  parser->allocator.deallocate(parser->allocator.context, buffer);
  return confit_expression_fail(parser->diagnostic, status, &parser->owner,
                                kInvalidSyntax);
}

static ConfitStatus confit_parser_parse_literal(ConfitParser *parser,
                                                size_t *out_index) {
  ConfitExpressionNode *node;
  ConfitToken token = parser->token;
  ConfitStatus status;
  int64_t integer;
  uint64_t hexadecimal;
  int boolean;
  if (token.kind == CONFIT_TOKEN_SYMBOL &&
      (confit_token_is(&token, "true") || confit_token_is(&token, "false"))) {
    boolean = confit_token_is(&token, "true");
    status = confit_parser_add_node(parser, CONFIT_EXPRESSION_NODE_LITERAL,
                                    CONFIT_INDEX_NONE, CONFIT_INDEX_NONE,
                                    out_index);
    if (status != CONFIT_OK) return status;
    node = &parser->expression->nodes[*out_index];
    status = confit_value_set_bool(&node->literal, boolean, &parser->allocator,
                                   parser->diagnostic);
  } else if (token.kind == CONFIT_TOKEN_DECIMAL &&
             confit_parse_decimal(&token, &integer)) {
    status = confit_parser_add_node(parser, CONFIT_EXPRESSION_NODE_LITERAL,
                                    CONFIT_INDEX_NONE, CONFIT_INDEX_NONE,
                                    out_index);
    if (status != CONFIT_OK) return status;
    node = &parser->expression->nodes[*out_index];
    status = confit_value_set_int(&node->literal, integer, &parser->allocator,
                                  parser->diagnostic);
  } else if (token.kind == CONFIT_TOKEN_HEXADECIMAL &&
             confit_parse_digits(&token, 16U, 2U, (uint64_t)INT64_MAX,
                                 &hexadecimal)) {
    status = confit_parser_add_node(parser, CONFIT_EXPRESSION_NODE_LITERAL,
                                    CONFIT_INDEX_NONE, CONFIT_INDEX_NONE,
                                    out_index);
    if (status != CONFIT_OK) return status;
    node = &parser->expression->nodes[*out_index];
    status = confit_value_set_hex(&node->literal, hexadecimal,
                                  &parser->allocator, parser->diagnostic);
  } else if (token.kind == CONFIT_TOKEN_STRING) {
    status = confit_parser_add_node(parser, CONFIT_EXPRESSION_NODE_LITERAL,
                                    CONFIT_INDEX_NONE, CONFIT_INDEX_NONE,
                                    out_index);
    if (status != CONFIT_OK) return status;
    node = &parser->expression->nodes[*out_index];
    status = confit_parser_decode_string(parser, &token, &node->literal);
  } else {
    return confit_expression_fail(parser->diagnostic, CONFIT_ERR_VALIDATION,
                                  &parser->owner, kInvalidSyntax);
  }
  if (status == CONFIT_OK) confit_parser_next(parser);
  return status;
}

static ConfitStatus confit_parser_parse_or(ConfitParser *parser,
                                           size_t *out_index);

static ConfitStatus confit_parser_parse_primary(ConfitParser *parser,
                                                size_t *out_index) {
  ConfitStatus status;
  size_t reference;
  size_t literal;
  size_t result;
  ConfitToken symbol;
  ConfitExpressionNodeKind comparison;
  if (parser->token.kind == CONFIT_TOKEN_LEFT_PAREN) {
    if (parser->nesting >= CONFIT_LIMIT_DEPENDENCY_NESTING)
      return confit_expression_fail(parser->diagnostic, CONFIT_ERR_VALIDATION,
                                    &parser->owner, kNestingLimit);
    ++parser->nesting;
    confit_parser_next(parser);
    status = confit_parser_parse_or(parser, out_index);
    if (status == CONFIT_OK &&
        parser->token.kind != CONFIT_TOKEN_RIGHT_PAREN)
      status = confit_expression_fail(parser->diagnostic, CONFIT_ERR_VALIDATION,
                                      &parser->owner, kInvalidSyntax);
    if (status == CONFIT_OK) confit_parser_next(parser);
    --parser->nesting;
    return status;
  }
  {
    /* Token spans are not NUL-terminated; validate without over-reading. */
    size_t index;
    if (parser->token.kind != CONFIT_TOKEN_SYMBOL || parser->token.size == 0U ||
        parser->token.size > 128U ||
        parser->token.start[0] < 'A' || parser->token.start[0] > 'Z')
      return confit_expression_fail(parser->diagnostic, CONFIT_ERR_VALIDATION,
                                    &parser->owner, kInvalidSyntax);
    for (index = 1U; index < parser->token.size; ++index)
      if (!confit_ascii_symbol_byte((unsigned char)parser->token.start[index]))
        return confit_expression_fail(parser->diagnostic, CONFIT_ERR_VALIDATION,
                                      &parser->owner, kInvalidSyntax);
  }
  symbol = parser->token;
  status = confit_parser_add_node(parser, CONFIT_EXPRESSION_NODE_REFERENCE,
                                  CONFIT_INDEX_NONE, CONFIT_INDEX_NONE,
                                  &reference);
  if (status != CONFIT_OK) return status;
  parser->expression->nodes[reference].literal.data.text.data =
      (char *)symbol.start;
  parser->expression->nodes[reference].literal.data.text.size = symbol.size;
  confit_parser_next(parser);
  if (parser->token.kind != CONFIT_TOKEN_EQUAL &&
      parser->token.kind != CONFIT_TOKEN_NOT_EQUAL) {
    *out_index = reference;
    return CONFIT_OK;
  }
  comparison = parser->token.kind == CONFIT_TOKEN_EQUAL
                   ? CONFIT_EXPRESSION_NODE_EQUAL
                   : CONFIT_EXPRESSION_NODE_NOT_EQUAL;
  parser->expression->nodes[reference].compared_reference = 1;
  confit_parser_next(parser);
  status = confit_parser_parse_literal(parser, &literal);
  if (status != CONFIT_OK) return status;
  status = confit_parser_add_node(parser, comparison, reference, literal,
                                  &result);
  if (status == CONFIT_OK) *out_index = result;
  return status;
}

static ConfitStatus confit_parser_parse_unary(ConfitParser *parser,
                                              size_t *out_index) {
  size_t child;
  ConfitStatus status;
  if (parser->token.kind != CONFIT_TOKEN_NOT)
    return confit_parser_parse_primary(parser, out_index);
  if (parser->nesting >= CONFIT_LIMIT_DEPENDENCY_NESTING)
    return confit_expression_fail(parser->diagnostic, CONFIT_ERR_VALIDATION,
                                  &parser->owner, kNestingLimit);
  ++parser->nesting;
  confit_parser_next(parser);
  status = confit_parser_parse_unary(parser, &child);
  --parser->nesting;
  if (status != CONFIT_OK) return status;
  return confit_parser_add_node(parser, CONFIT_EXPRESSION_NODE_NOT, child,
                                CONFIT_INDEX_NONE, out_index);
}

static ConfitStatus confit_parser_parse_and(ConfitParser *parser,
                                            size_t *out_index) {
  size_t left = CONFIT_INDEX_NONE;
  ConfitStatus status = confit_parser_parse_unary(parser, &left);
  while (status == CONFIT_OK && parser->token.kind == CONFIT_TOKEN_AND) {
    size_t right = CONFIT_INDEX_NONE;
    size_t combined = CONFIT_INDEX_NONE;
    confit_parser_next(parser);
    status = confit_parser_parse_unary(parser, &right);
    if (status == CONFIT_OK)
      status = confit_parser_add_node(parser, CONFIT_EXPRESSION_NODE_AND, left,
                                      right, &combined);
    if (status == CONFIT_OK) left = combined;
  }
  if (status == CONFIT_OK) *out_index = left;
  return status;
}

static ConfitStatus confit_parser_parse_or(ConfitParser *parser,
                                           size_t *out_index) {
  size_t left = CONFIT_INDEX_NONE;
  ConfitStatus status = confit_parser_parse_and(parser, &left);
  while (status == CONFIT_OK && parser->token.kind == CONFIT_TOKEN_OR) {
    size_t right = CONFIT_INDEX_NONE;
    size_t combined = CONFIT_INDEX_NONE;
    confit_parser_next(parser);
    status = confit_parser_parse_and(parser, &right);
    if (status == CONFIT_OK)
      status = confit_parser_add_node(parser, CONFIT_EXPRESSION_NODE_OR, left,
                                      right, &combined);
    if (status == CONFIT_OK) left = combined;
  }
  if (status == CONFIT_OK) *out_index = left;
  return status;
}

static int confit_catalog_find_index(const ConfitCatalog *catalog,
                                     const char *text, size_t text_size,
                                     size_t *out_index,
                                     ConfitConfigView *out_view) {
  size_t index;
  for (index = 0U; index < confit_catalog_config_count(catalog); ++index) {
    ConfitConfigView view;
    if (!confit_catalog_config_at(catalog, index, &view)) return 0;
    if (strlen(view.symbol) == text_size &&
        memcmp(view.symbol, text, text_size) == 0) {
      if (out_index != 0) *out_index = index;
      if (out_view != 0) *out_view = view;
      return 1;
    }
  }
  return 0;
}

static int confit_enum_contains(const ConfitConfigView *view,
                                const char *text, size_t text_size) {
  size_t index;
  for (index = 0U; index < view->enum_value_count; ++index)
    if (strlen(view->enum_values[index]) == text_size &&
        memcmp(view->enum_values[index], text, text_size) == 0)
      return 1;
  return 0;
}

static ConfitStatus confit_expression_add_reference(
    ConfitParser *parser, size_t reference) {
  size_t index;
  size_t capacity;
  size_t *replacement;
  for (index = 0U; index < parser->expression->reference_count; ++index)
    if (parser->expression->references[index] == reference) return CONFIT_OK;
  if (parser->expression->reference_count ==
      parser->expression->reference_capacity) {
    capacity = parser->expression->reference_capacity == 0U
                   ? 4U
                   : parser->expression->reference_capacity * 2U;
    if (capacity > CONFIT_LIMIT_DEPENDENCY_AST_NODES)
      capacity = CONFIT_LIMIT_DEPENDENCY_AST_NODES;
    if (capacity < parser->expression->reference_count ||
        capacity > SIZE_MAX / sizeof(*replacement))
      return confit_expression_fail(parser->diagnostic, CONFIT_ERR_INTERNAL,
                                    &parser->owner, kOutOfMemory);
    replacement = (size_t *)parser->allocator.allocate(
        parser->allocator.context, capacity * sizeof(*replacement));
    if (replacement == 0)
      return confit_expression_fail(parser->diagnostic, CONFIT_ERR_INTERNAL,
                                    &parser->owner, kOutOfMemory);
    if (parser->expression->reference_count != 0U)
      memcpy(replacement, parser->expression->references,
             parser->expression->reference_count * sizeof(*replacement));
    if (parser->expression->references != 0)
      parser->allocator.deallocate(parser->allocator.context,
                                   parser->expression->references);
    parser->expression->references = replacement;
    parser->expression->reference_capacity = capacity;
  }
  parser->expression->references[parser->expression->reference_count++] =
      reference;
  return CONFIT_OK;
}

static ConfitStatus confit_expression_link(ConfitParser *parser) {
  size_t index;
  for (index = 0U; index < parser->expression->node_count; ++index) {
    ConfitExpressionNode *node = &parser->expression->nodes[index];
    ConfitConfigView referenced;
    size_t reference;
    if (node->kind != CONFIT_EXPRESSION_NODE_REFERENCE) continue;
    if (!confit_catalog_find_index(parser->catalog,
                                   node->literal.data.text.data,
                                   node->literal.data.text.size, &reference,
                                   &referenced))
      return confit_expression_fail(parser->diagnostic, CONFIT_ERR_VALIDATION,
                                    &parser->owner, kUnknownSymbol);
    node->literal.data.text.data = 0;
    node->literal.data.text.size = 0U;
    node->reference = reference;
    if (!node->compared_reference && referenced.kind != CONFIT_VALUE_BOOL)
      return confit_expression_fail(parser->diagnostic, CONFIT_ERR_VALIDATION,
                                    &parser->owner, kBareNonBoolean);
    {
      ConfitStatus status = confit_expression_add_reference(parser, reference);
      if (status != CONFIT_OK) return status;
    }
  }
  for (index = 0U; index < parser->expression->node_count; ++index) {
    ConfitExpressionNode *node = &parser->expression->nodes[index];
    ConfitExpressionNode *reference_node;
    ConfitExpressionNode *literal_node;
    ConfitConfigView referenced;
    const char *text;
    size_t text_size;
    ConfitValue enumeration;
    ConfitStatus status;
    if (node->kind != CONFIT_EXPRESSION_NODE_EQUAL &&
        node->kind != CONFIT_EXPRESSION_NODE_NOT_EQUAL)
      continue;
    reference_node = &parser->expression->nodes[node->left];
    literal_node = &parser->expression->nodes[node->right];
    if (!confit_catalog_config_at(parser->catalog, reference_node->reference,
                                  &referenced))
      return confit_expression_fail(parser->diagnostic, CONFIT_ERR_INTERNAL,
                                    &parser->owner, kInternalExpression);
    if (referenced.kind == CONFIT_VALUE_ENUM &&
        literal_node->literal.kind == CONFIT_VALUE_STRING) {
      if (!confit_value_text(&literal_node->literal, &text, &text_size) ||
          !confit_enum_contains(&referenced, text, text_size))
        return confit_expression_fail(parser->diagnostic,
                                      CONFIT_ERR_VALIDATION, &parser->owner,
                                      kEnumDomain);
      confit_value_init(&enumeration);
      status = confit_value_set_enum(&enumeration, text, text_size,
                                     &parser->allocator, parser->diagnostic);
      if (status != CONFIT_OK) {
        confit_value_destroy(&enumeration);
        return confit_expression_fail(parser->diagnostic, status,
                                      &parser->owner, kComparisonType);
      }
      confit_value_destroy(&literal_node->literal);
      literal_node->literal = enumeration;
    }
    if (literal_node->literal.kind != referenced.kind)
      return confit_expression_fail(parser->diagnostic, CONFIT_ERR_VALIDATION,
                                    &parser->owner, kComparisonType);
  }
  for (index = 1U; index < parser->expression->reference_count; ++index) {
    size_t value = parser->expression->references[index];
    size_t cursor = index;
    while (cursor != 0U &&
           confit_symbol_less(parser->catalog, value,
                              parser->expression->references[cursor - 1U])) {
      parser->expression->references[cursor] =
          parser->expression->references[cursor - 1U];
      --cursor;
    }
    parser->expression->references[cursor] = value;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_expression_compile(
    const ConfitCatalog *catalog, size_t config_index,
    ConfitCompiledExpression *expression, const ConfitAllocator *allocator,
    ConfitDiagnostic *diagnostic) {
  ConfitParser parser;
  ConfitStatus status;
  const char *text;
  memset(&parser, 0, sizeof(parser));
  parser.catalog = catalog;
  parser.expression = expression;
  parser.allocator = *allocator;
  parser.diagnostic = diagnostic;
  if (!confit_catalog_config_at(catalog, config_index, &parser.owner))
    return confit_expression_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                  kInvalidArgument);
  text = parser.owner.dependency_text;
  if (text == 0) {
    expression->root = CONFIT_INDEX_NONE;
    return CONFIT_OK;
  }
  parser.text = text;
  parser.text_size = strlen(text);
  confit_parser_next(&parser);
  status = confit_parser_parse_or(&parser, &expression->root);
  if (status == CONFIT_OK && parser.token.kind != CONFIT_TOKEN_END)
    status = confit_expression_fail(diagnostic, CONFIT_ERR_VALIDATION, &parser.owner,
                                    kInvalidSyntax);
  if (status == CONFIT_OK) status = confit_expression_link(&parser);
  return status;
}

static int confit_symbol_less(const ConfitCatalog *catalog, size_t left,
                              size_t right) {
  ConfitConfigView left_view;
  ConfitConfigView right_view;
  if (!confit_catalog_config_at(catalog, left, &left_view) ||
      !confit_catalog_config_at(catalog, right, &right_view))
    return left < right;
  return strcmp(left_view.symbol, right_view.symbol) < 0;
}

static void confit_heap_push(const ConfitCatalog *catalog, size_t *heap,
                             size_t *size, size_t value) {
  size_t child = (*size)++;
  while (child != 0U) {
    size_t parent = (child - 1U) / 2U;
    if (!confit_symbol_less(catalog, value, heap[parent])) break;
    heap[child] = heap[parent];
    child = parent;
  }
  heap[child] = value;
}

static size_t confit_heap_pop(const ConfitCatalog *catalog, size_t *heap,
                              size_t *size) {
  size_t result = heap[0];
  size_t value = heap[--(*size)];
  size_t parent = 0U;
  while (parent < *size) {
    size_t left = parent * 2U + 1U;
    size_t right = left + 1U;
    size_t child;
    if (left >= *size) break;
    child = right < *size && confit_symbol_less(catalog, heap[right], heap[left])
                ? right
                : left;
    if (!confit_symbol_less(catalog, heap[child], value)) break;
    heap[parent] = heap[child];
    parent = child;
  }
  if (*size != 0U) heap[parent] = value;
  return result;
}

static ConfitStatus confit_dependency_reject_cycle(
    const ConfitDependencyPlan *plan, ConfitDiagnostic *diagnostic) {
  unsigned char *state;
  size_t *stack_nodes;
  size_t *stack_edges;
  size_t *heap;
  size_t heap_size = 0U;
  size_t bytes;
  size_t index;
  ConfitAllocator allocator = plan->allocator;
  if (plan->config_count == 0U) return CONFIT_OK;
  bytes = plan->config_count * sizeof(size_t);
  state = (unsigned char *)allocator.allocate(allocator.context,
                                               plan->config_count);
  stack_nodes = (size_t *)allocator.allocate(allocator.context, bytes);
  stack_edges = (size_t *)allocator.allocate(allocator.context, bytes);
  heap = (size_t *)allocator.allocate(allocator.context, bytes);
  if (state == 0 || stack_nodes == 0 || stack_edges == 0 || heap == 0) {
    if (state != 0) allocator.deallocate(allocator.context, state);
    if (stack_nodes != 0)
      allocator.deallocate(allocator.context, stack_nodes);
    if (stack_edges != 0)
      allocator.deallocate(allocator.context, stack_edges);
    if (heap != 0) allocator.deallocate(allocator.context, heap);
    return confit_expression_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                  kOutOfMemory);
  }
  memset(state, 0, plan->config_count);
  for (index = 0U; index < plan->config_count; ++index)
    confit_heap_push(plan->catalog, heap, &heap_size, index);
  while (heap_size != 0U) {
    size_t start = confit_heap_pop(plan->catalog, heap, &heap_size);
    size_t depth;
    if (state[start] != 0U) continue;
    depth = 1U;
    stack_nodes[0] = start;
    stack_edges[0] = 0U;
    state[start] = 1U;
    while (depth != 0U) {
      size_t node = stack_nodes[depth - 1U];
      const ConfitCompiledExpression *expression = &plan->expressions[node];
      if (stack_edges[depth - 1U] >= expression->reference_count) {
        state[node] = 2U;
        --depth;
        continue;
      }
      {
        size_t reference =
            expression->references[stack_edges[depth - 1U]++];
        if (state[reference] == 0U) {
          stack_nodes[depth] = reference;
          stack_edges[depth] = 0U;
          state[reference] = 1U;
          ++depth;
        } else if (state[reference] == 1U) {
          size_t cycle_start = 0U;
          size_t cycle_member;
          ConfitConfigView owner;
          while (cycle_start < depth &&
                 stack_nodes[cycle_start] != reference)
            ++cycle_start;
          cycle_member = reference;
          for (index = cycle_start; index < depth; ++index)
            if (confit_symbol_less(plan->catalog, stack_nodes[index],
                                   cycle_member))
              cycle_member = stack_nodes[index];
          allocator.deallocate(allocator.context, state);
          allocator.deallocate(allocator.context, stack_nodes);
          allocator.deallocate(allocator.context, stack_edges);
          allocator.deallocate(allocator.context, heap);
          if (confit_catalog_config_at(plan->catalog, cycle_member, &owner))
            return confit_expression_fail(diagnostic, CONFIT_ERR_VALIDATION,
                                          &owner, kCycle);
          return confit_expression_fail(diagnostic, CONFIT_ERR_VALIDATION, 0,
                                        kCycle);
        }
      }
    }
  }
  allocator.deallocate(allocator.context, state);
  allocator.deallocate(allocator.context, stack_nodes);
  allocator.deallocate(allocator.context, stack_edges);
  allocator.deallocate(allocator.context, heap);
  return CONFIT_OK;
}

static ConfitStatus confit_dependency_build_order(
    ConfitDependencyPlan *plan, ConfitDiagnostic *diagnostic) {
  size_t *indegree;
  size_t *reverse_count;
  size_t *reverse_offset;
  size_t *reverse_cursor;
  size_t *dependents;
  size_t *heap;
  size_t heap_size = 0U;
  size_t emitted = 0U;
  size_t index;
  size_t edge;
  size_t bytes;
  ConfitAllocator *allocator = &plan->allocator;
  if (plan->config_count == 0U) return CONFIT_OK;
  if (plan->config_count > SIZE_MAX / sizeof(size_t))
    return confit_expression_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                  kOutOfMemory);
  bytes = plan->config_count * sizeof(size_t);
  indegree = (size_t *)allocator->allocate(allocator->context, bytes);
  reverse_count = (size_t *)allocator->allocate(allocator->context, bytes);
  reverse_offset = (size_t *)allocator->allocate(
      allocator->context, (plan->config_count + 1U) * sizeof(size_t));
  reverse_cursor = (size_t *)allocator->allocate(allocator->context, bytes);
  heap = (size_t *)allocator->allocate(allocator->context, bytes);
  dependents = plan->edge_count == 0U
                   ? 0
                   : (size_t *)allocator->allocate(
                         allocator->context,
                         plan->edge_count * sizeof(size_t));
  if (indegree == 0 || reverse_count == 0 || reverse_offset == 0 ||
      reverse_cursor == 0 || heap == 0 ||
      (plan->edge_count != 0U && dependents == 0)) {
    if (indegree != 0) allocator->deallocate(allocator->context, indegree);
    if (reverse_count != 0)
      allocator->deallocate(allocator->context, reverse_count);
    if (reverse_offset != 0)
      allocator->deallocate(allocator->context, reverse_offset);
    if (reverse_cursor != 0)
      allocator->deallocate(allocator->context, reverse_cursor);
    if (heap != 0) allocator->deallocate(allocator->context, heap);
    if (dependents != 0)
      allocator->deallocate(allocator->context, dependents);
    return confit_expression_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                  kOutOfMemory);
  }
  memset(reverse_count, 0, bytes);
  for (index = 0U; index < plan->config_count; ++index) {
    indegree[index] = plan->expressions[index].reference_count;
    for (edge = 0U; edge < plan->expressions[index].reference_count; ++edge)
      ++reverse_count[plan->expressions[index].references[edge]];
  }
  reverse_offset[0] = 0U;
  for (index = 0U; index < plan->config_count; ++index) {
    reverse_offset[index + 1U] = reverse_offset[index] + reverse_count[index];
    reverse_cursor[index] = reverse_offset[index];
  }
  for (index = 0U; index < plan->config_count; ++index)
    for (edge = 0U; edge < plan->expressions[index].reference_count; ++edge) {
      size_t prerequisite = plan->expressions[index].references[edge];
      dependents[reverse_cursor[prerequisite]++] = index;
    }
  for (index = 0U; index < plan->config_count; ++index)
    if (indegree[index] == 0U)
      confit_heap_push(plan->catalog, heap, &heap_size, index);
  while (heap_size != 0U) {
    size_t prerequisite = confit_heap_pop(plan->catalog, heap, &heap_size);
    plan->order[emitted++] = prerequisite;
    if (dependents != 0)
      for (edge = reverse_offset[prerequisite];
           edge < reverse_offset[prerequisite + 1U]; ++edge) {
        size_t dependent = dependents[edge];
        if (--indegree[dependent] == 0U)
          confit_heap_push(plan->catalog, heap, &heap_size, dependent);
      }
  }
  allocator->deallocate(allocator->context, indegree);
  allocator->deallocate(allocator->context, reverse_count);
  allocator->deallocate(allocator->context, reverse_offset);
  allocator->deallocate(allocator->context, reverse_cursor);
  allocator->deallocate(allocator->context, heap);
  if (dependents != 0) allocator->deallocate(allocator->context, dependents);
  if (emitted != plan->config_count)
    return confit_expression_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                  kInternalExpression);
  return CONFIT_OK;
}

ConfitStatus confit_dependency_plan_create(
    const ConfitCatalog *catalog, const ConfitAllocator *allocator,
    ConfitDependencyPlan **out_plan, ConfitDiagnostic *diagnostic) {
  ConfitAllocator resolved;
  ConfitDependencyPlan *plan;
  size_t index;
  ConfitStatus status = CONFIT_OK;
  if (catalog == 0 || out_plan == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_USAGE, 0, 0U, 0U,
                          kInvalidArgument);
    return CONFIT_ERR_USAGE;
  }
  *out_plan = 0;
  if (!confit_expression_resolve_allocator(allocator, &resolved)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_USAGE, 0, 0U, 0U,
                          kInvalidAllocator);
    return CONFIT_ERR_USAGE;
  }
  plan = (ConfitDependencyPlan *)resolved.allocate(resolved.context,
                                                    sizeof(*plan));
  if (plan == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0U, 0U,
                          kOutOfMemory);
    return CONFIT_ERR_INTERNAL;
  }
  memset(plan, 0, sizeof(*plan));
  plan->allocator = resolved;
  plan->catalog = catalog;
  plan->config_count = confit_catalog_config_count(catalog);
  if (plan->config_count != 0U) {
    if (plan->config_count > SIZE_MAX / sizeof(*plan->expressions) ||
        plan->config_count > SIZE_MAX / sizeof(*plan->order)) {
      status = confit_expression_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                      kOutOfMemory);
    } else {
      plan->expressions = (ConfitCompiledExpression *)resolved.allocate(
          resolved.context, plan->config_count * sizeof(*plan->expressions));
      plan->order = (size_t *)resolved.allocate(
          resolved.context, plan->config_count * sizeof(*plan->order));
      if (plan->expressions != 0)
        memset(plan->expressions, 0,
               plan->config_count * sizeof(*plan->expressions));
      if (plan->expressions == 0 || plan->order == 0)
        status = confit_expression_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                        kOutOfMemory);
    }
  }
  for (index = 0U; status == CONFIT_OK && index < plan->config_count; ++index) {
    status = confit_expression_compile(catalog, index,
                                       &plan->expressions[index], &resolved,
                                       diagnostic);
    if (status == CONFIT_OK) {
      if (plan->edge_count > SIZE_MAX -
                                 plan->expressions[index].reference_count)
        status = confit_expression_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                        kOutOfMemory);
      else
        plan->edge_count += plan->expressions[index].reference_count;
    }
  }
  if (status == CONFIT_OK)
    status = confit_dependency_reject_cycle(plan, diagnostic);
  if (status == CONFIT_OK)
    status = confit_dependency_build_order(plan, diagnostic);
  if (status != CONFIT_OK) {
    confit_dependency_plan_destroy(plan);
    return status;
  }
  *out_plan = plan;
  return CONFIT_OK;
}

void confit_dependency_plan_destroy(ConfitDependencyPlan *plan) {
  ConfitAllocator allocator;
  size_t index;
  if (plan == 0) return;
  allocator = plan->allocator;
  if (plan->expressions != 0)
    for (index = plan->config_count; index > 0U; --index)
      confit_compiled_expression_destroy(&plan->expressions[index - 1U],
                                         &allocator);
  if (plan->expressions != 0)
    allocator.deallocate(allocator.context, plan->expressions);
  if (plan->order != 0) allocator.deallocate(allocator.context, plan->order);
  memset(plan, 0, sizeof(*plan));
  allocator.deallocate(allocator.context, plan);
}

size_t confit_dependency_plan_config_count(const ConfitDependencyPlan *plan) {
  return plan != 0 ? plan->config_count : 0U;
}

size_t confit_dependency_plan_edge_count(const ConfitDependencyPlan *plan) {
  return plan != 0 ? plan->edge_count : 0U;
}

int confit_dependency_plan_matches_catalog(const ConfitDependencyPlan *plan,
                                           const ConfitCatalog *catalog) {
  return plan != 0 && catalog != 0 && plan->catalog == catalog;
}

int confit_dependency_plan_has_expression(const ConfitDependencyPlan *plan,
                                          size_t config_index) {
  return plan != 0 && config_index < plan->config_count &&
         plan->expressions[config_index].root != CONFIT_INDEX_NONE;
}

int confit_dependency_plan_order_at(const ConfitDependencyPlan *plan,
                                    size_t order_index,
                                    size_t *out_config_index) {
  if (plan == 0 || out_config_index == 0 || order_index >= plan->config_count)
    return 0;
  *out_config_index = plan->order[order_index];
  return 1;
}

static ConfitStatus confit_evaluation_add_reason(
    ConfitDependencyEvaluation *evaluation, ConfitReasonKind kind, int result,
    const char *subject_symbol, const char *detail, const size_t *children,
    size_t child_count, size_t *out_index, ConfitDiagnostic *diagnostic) {
  ConfitDependencyReasonRecord *reason;
  if (child_count > CONFIT_REASON_CHILD_LIMIT ||
      evaluation->reason_count >= evaluation->reason_capacity)
    return confit_expression_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                  kInternalExpression);
  reason = &evaluation->reasons[evaluation->reason_count];
  memset(reason, 0, sizeof(*reason));
  reason->kind = kind;
  reason->result = result != 0;
  reason->subject_symbol = subject_symbol;
  reason->detail = detail;
  reason->child_count = child_count;
  if (child_count != 0U) memcpy(reason->children, children,
                                child_count * sizeof(*children));
  *out_index = evaluation->reason_count++;
  return CONFIT_OK;
}

typedef struct ConfitEvaluationFrame {
  size_t node;
  unsigned state;
} ConfitEvaluationFrame;

static ConfitStatus confit_dependency_evaluate_expression(
    ConfitDependencyEvaluation *evaluation,
    const ConfitCompiledExpression *expression, size_t owner_index,
    const ConfitValue *values, ConfitDiagnostic *diagnostic) {
  ConfitEvaluationFrame frames[CONFIT_LIMIT_DEPENDENCY_AST_NODES];
  int results[CONFIT_LIMIT_DEPENDENCY_AST_NODES];
  size_t reason_indexes[CONFIT_LIMIT_DEPENDENCY_AST_NODES];
  size_t frame_count = 1U;
  ConfitConfigView owner;
  if (!confit_catalog_config_at(evaluation->plan->catalog, owner_index, &owner))
    return confit_expression_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                  kInternalExpression);
  frames[0].node = expression->root;
  frames[0].state = 0U;
  while (frame_count != 0U) {
    ConfitEvaluationFrame *frame = &frames[frame_count - 1U];
    const ConfitExpressionNode *node = &expression->nodes[frame->node];
    ConfitStatus status;
    size_t reason;
    size_t children[2];
    int result;
    const char *subject = owner.symbol;
    if (node->kind == CONFIT_EXPRESSION_NODE_REFERENCE) {
      ConfitConfigView referenced;
      if (!confit_catalog_config_at(evaluation->plan->catalog, node->reference,
                                    &referenced) ||
          values[node->reference].kind != CONFIT_VALUE_BOOL)
        return confit_expression_fail(diagnostic, CONFIT_ERR_INTERNAL, &owner,
                                      kInternalExpression);
      result = values[node->reference].data.boolean != 0;
      status = confit_evaluation_add_reason(
          evaluation, CONFIT_REASON_REFERENCE, result, referenced.symbol,
          result ? kTrue : kFalse, 0, 0U, &reason, diagnostic);
      if (status != CONFIT_OK) return status;
      results[frame->node] = result;
      reason_indexes[frame->node] = reason;
      --frame_count;
      continue;
    }
    if (node->kind == CONFIT_EXPRESSION_NODE_EQUAL ||
        node->kind == CONFIT_EXPRESSION_NODE_NOT_EQUAL) {
      const ConfitExpressionNode *reference = &expression->nodes[node->left];
      const ConfitExpressionNode *literal = &expression->nodes[node->right];
      ConfitConfigView referenced;
      if (!confit_catalog_config_at(evaluation->plan->catalog,
                                    reference->reference, &referenced))
        return confit_expression_fail(diagnostic, CONFIT_ERR_INTERNAL, &owner,
                                      kInternalExpression);
      result = confit_value_equal(&values[reference->reference],
                                  &literal->literal);
      if (node->kind == CONFIT_EXPRESSION_NODE_NOT_EQUAL) result = !result;
      status = confit_evaluation_add_reason(
          evaluation, CONFIT_REASON_COMPARISON, result, referenced.symbol,
          node->kind == CONFIT_EXPRESSION_NODE_EQUAL ? kEqual : kNotEqual, 0,
          0U, &reason, diagnostic);
      if (status != CONFIT_OK) return status;
      results[frame->node] = result;
      reason_indexes[frame->node] = reason;
      --frame_count;
      continue;
    }
    if (node->kind == CONFIT_EXPRESSION_NODE_NOT) {
      if (frame->state == 0U) {
        frame->state = 1U;
        frames[frame_count].node = node->left;
        frames[frame_count++].state = 0U;
        continue;
      }
      result = !results[node->left];
      children[0] = reason_indexes[node->left];
      status = confit_evaluation_add_reason(
          evaluation, CONFIT_REASON_NOT, result, subject, 0, children, 1U,
          &reason, diagnostic);
    } else if (node->kind == CONFIT_EXPRESSION_NODE_AND) {
      if (frame->state == 0U) {
        frame->state = 1U;
        frames[frame_count].node = node->left;
        frames[frame_count++].state = 0U;
        continue;
      }
      if (frame->state == 1U && !results[node->left]) {
        result = 0;
        children[0] = reason_indexes[node->left];
        status = confit_evaluation_add_reason(
            evaluation, CONFIT_REASON_AND, result, subject, 0, children, 1U,
            &reason, diagnostic);
      } else if (frame->state == 1U) {
        frame->state = 2U;
        frames[frame_count].node = node->right;
        frames[frame_count++].state = 0U;
        continue;
      } else {
        result = results[node->right];
        children[0] = reason_indexes[node->left];
        children[1] = reason_indexes[node->right];
        status = confit_evaluation_add_reason(
            evaluation, CONFIT_REASON_AND, result, subject, 0, children, 2U,
            &reason, diagnostic);
      }
    } else if (node->kind == CONFIT_EXPRESSION_NODE_OR) {
      if (frame->state == 0U) {
        frame->state = 1U;
        frames[frame_count].node = node->left;
        frames[frame_count++].state = 0U;
        continue;
      }
      if (frame->state == 1U && results[node->left]) {
        result = 1;
        children[0] = reason_indexes[node->left];
        status = confit_evaluation_add_reason(
            evaluation, CONFIT_REASON_OR, result, subject, 0, children, 1U,
            &reason, diagnostic);
      } else if (frame->state == 1U) {
        frame->state = 2U;
        frames[frame_count].node = node->right;
        frames[frame_count++].state = 0U;
        continue;
      } else {
        result = results[node->right];
        children[0] = reason_indexes[node->left];
        children[1] = reason_indexes[node->right];
        status = confit_evaluation_add_reason(
            evaluation, CONFIT_REASON_OR, result, subject, 0, children, 2U,
            &reason, diagnostic);
      }
    } else {
      return confit_expression_fail(diagnostic, CONFIT_ERR_INTERNAL, &owner,
                                    kInternalExpression);
    }
    if (status != CONFIT_OK) return status;
    results[frame->node] = result;
    reason_indexes[frame->node] = reason;
    --frame_count;
  }
  evaluation->root = reason_indexes[expression->root];
  evaluation->available = results[expression->root];
  return CONFIT_OK;
}

static ConfitStatus confit_dependency_plan_evaluate_impl(
    const ConfitDependencyPlan *plan, size_t config_index,
    const ConfitValue *values, size_t value_count,
    const ConfitAllocator *allocator,
    ConfitDependencyEvaluation **out_evaluation,
    ConfitDiagnostic *diagnostic, int validate_values) {
  ConfitAllocator resolved;
  ConfitDependencyEvaluation *evaluation;
  ConfitConfigView owner;
  size_t index;
  size_t capacity;
  ConfitStatus status;
  if (plan == 0 || out_evaluation == 0 || config_index >= plan->config_count ||
      value_count != plan->config_count ||
      (value_count != 0U && values == 0)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_USAGE, 0, 0U, 0U,
                          kInvalidArgument);
    return CONFIT_ERR_USAGE;
  }
  *out_evaluation = 0;
  if (!confit_expression_resolve_allocator(allocator, &resolved)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_USAGE, 0, 0U, 0U,
                          kInvalidAllocator);
    return CONFIT_ERR_USAGE;
  }
  if (!confit_catalog_config_at(plan->catalog, config_index, &owner))
    return confit_expression_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                  kInternalExpression);
  for (index = 0U; validate_values && index < value_count; ++index) {
    ConfitConfigView view;
    if (!confit_catalog_config_at(plan->catalog, index, &view) ||
        values[index].kind != view.kind)
      return confit_expression_fail(diagnostic, CONFIT_ERR_VALIDATION, &owner,
                                    kValueShape);
  }
  evaluation = (ConfitDependencyEvaluation *)resolved.allocate(
      resolved.context, sizeof(*evaluation));
  if (evaluation == 0)
    return confit_expression_fail(diagnostic, CONFIT_ERR_INTERNAL, &owner,
                                  kOutOfMemory);
  memset(evaluation, 0, sizeof(*evaluation));
  evaluation->allocator = resolved;
  evaluation->plan = plan;
  capacity = plan->expressions[config_index].node_count;
  if (capacity == 0U) capacity = 1U;
  evaluation->reasons = (ConfitDependencyReasonRecord *)resolved.allocate(
      resolved.context, capacity * sizeof(*evaluation->reasons));
  if (evaluation->reasons == 0) {
    confit_dependency_evaluation_destroy(evaluation);
    return confit_expression_fail(diagnostic, CONFIT_ERR_INTERNAL, &owner,
                                  kOutOfMemory);
  }
  evaluation->reason_capacity = capacity;
  if (plan->expressions[config_index].root == CONFIT_INDEX_NONE) {
    status = confit_evaluation_add_reason(
        evaluation, CONFIT_REASON_LITERAL, 1, owner.symbol, kTrue, 0, 0U,
        &evaluation->root, diagnostic);
    evaluation->available = 1;
  } else {
    status = confit_dependency_evaluate_expression(
        evaluation, &plan->expressions[config_index], config_index, values,
        diagnostic);
  }
  if (status != CONFIT_OK) {
    confit_dependency_evaluation_destroy(evaluation);
    return status;
  }
  *out_evaluation = evaluation;
  return CONFIT_OK;
}

ConfitStatus confit_dependency_plan_evaluate(
    const ConfitDependencyPlan *plan, size_t config_index,
    const ConfitValue *values, size_t value_count,
    const ConfitAllocator *allocator,
    ConfitDependencyEvaluation **out_evaluation,
    ConfitDiagnostic *diagnostic) {
  return confit_dependency_plan_evaluate_impl(
      plan, config_index, values, value_count, allocator, out_evaluation,
      diagnostic, 1);
}

ConfitStatus confit_dependency_plan_evaluate_prevalidated(
    const ConfitDependencyPlan *plan, size_t config_index,
    const ConfitValue *values, size_t value_count,
    const ConfitAllocator *allocator,
    ConfitDependencyEvaluation **out_evaluation,
    ConfitDiagnostic *diagnostic) {
  return confit_dependency_plan_evaluate_impl(
      plan, config_index, values, value_count, allocator, out_evaluation,
      diagnostic, 0);
}

void confit_dependency_evaluation_destroy(
    ConfitDependencyEvaluation *evaluation) {
  ConfitAllocator allocator;
  if (evaluation == 0) return;
  allocator = evaluation->allocator;
  if (evaluation->reasons != 0)
    allocator.deallocate(allocator.context, evaluation->reasons);
  memset(evaluation, 0, sizeof(*evaluation));
  allocator.deallocate(allocator.context, evaluation);
}

int confit_dependency_evaluation_available(
    const ConfitDependencyEvaluation *evaluation) {
  return evaluation != 0 && evaluation->available;
}

size_t confit_dependency_evaluation_reason_count(
    const ConfitDependencyEvaluation *evaluation) {
  return evaluation != 0 ? evaluation->reason_count : 0U;
}

size_t confit_dependency_evaluation_reason_root(
    const ConfitDependencyEvaluation *evaluation) {
  return evaluation != 0 ? evaluation->root : CONFIT_INDEX_NONE;
}

int confit_dependency_evaluation_reason_at(
    const ConfitDependencyEvaluation *evaluation, size_t index,
    ConfitDependencyReasonView *out_view) {
  const ConfitDependencyReasonRecord *reason;
  if (evaluation == 0 || out_view == 0 || index >= evaluation->reason_count)
    return 0;
  reason = &evaluation->reasons[index];
  out_view->kind = reason->kind;
  out_view->result = reason->result;
  out_view->subject_symbol = reason->subject_symbol;
  out_view->detail = reason->detail;
  out_view->child_count = reason->child_count;
  out_view->children[0] = reason->children[0];
  out_view->children[1] = reason->children[1];
  return 1;
}
