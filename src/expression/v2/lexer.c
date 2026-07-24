#include "lexer_internal.h"

#include <stdlib.h>
#include <string.h>

static const char kExpressionInvalidCharacter[] = "invalid expression character";
static const char kExpressionInvalidString[] = "invalid expression string literal";
static const char kExpressionInvalidNumber[] = "invalid expression number literal";
static const char kExpressionTokenLimit[] = "expression token limit exceeded";
static const char kExpressionAllocationFailed[] =
    "failed to allocate expression string literal";

static int confit_v2_is_space(char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static int confit_v2_is_lower_alpha(char value) {
  return value >= 'a' && value <= 'z';
}

static int confit_v2_is_digit(char value) {
  return value >= '0' && value <= '9';
}

static int confit_v2_is_identifier_tail(char value) {
  return confit_v2_is_lower_alpha(value) || confit_v2_is_digit(value) ||
         value == '_';
}

static int confit_v2_is_hex(char value) {
  return confit_v2_is_digit(value) || (value >= 'a' && value <= 'f') ||
         (value >= 'A' && value <= 'F');
}

static int confit_v2_hex_value(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

void confit_v2_expression_diagnostic_at(const ConfitV2ExpressionText *source,
                                        size_t offset, ConfitStatus status,
                                        const char *message,
                                        ConfitDiagnostic *diagnostic) {
  const char *text;
  size_t column;
  size_t cursor;
  size_t line;

  text = source != 0 ? source->text : 0;
  line = source != 0 ? source->span.line : 0U;
  column = source != 0 ? source->span.column : 0U;
  if (text != 0) {
    for (cursor = 0U; cursor < offset && text[cursor] != '\0'; ++cursor) {
      if (text[cursor] == '\n') {
        line += 1U;
        column = 1U;
      } else if (column != 0U) {
        column += 1U;
      }
    }
  }
  confit_diagnostic_set(diagnostic, status,
                        source != 0 ? source->span.path : 0, line, column,
                        message);
}

void confit_v2_expression_lexer_init(ConfitV2ExpressionLexer *lexer,
                                      const ConfitV2ExpressionText *source,
                                      size_t max_tokens) {
  lexer->source = source;
  lexer->offset = 0U;
  lexer->token_count = 0U;
  lexer->max_tokens = max_tokens;
}

static int confit_v2_token_matches(const char *text, size_t start, size_t end,
                                   const char *expected) {
  const size_t expected_size = strlen(expected);
  return end - start == expected_size &&
         memcmp(text + start, expected, expected_size) == 0;
}

static ConfitStatus confit_v2_lex_string(ConfitV2ExpressionLexer *lexer,
                                          ConfitV2ExpressionToken *token,
                                          ConfitDiagnostic *diagnostic) {
  const char *text = lexer->source->text;
  const size_t start = lexer->offset;
  size_t cursor = start + 1U;

  while (text[cursor] != '\0') {
    const unsigned char current = (unsigned char)text[cursor];
    if (current == '"') {
      lexer->offset = cursor + 1U;
      token->kind = CONFIT_V2_TOKEN_STRING;
      token->start_offset = start;
      token->end_offset = cursor + 1U;
      return CONFIT_OK;
    }
    if (current < 0x20U) {
      break;
    }
    if (current == '\\') {
      const char escape = text[cursor + 1U];
      if (escape == '\0' ||
          (escape != '"' && escape != '\\' && escape != '/' && escape != 'b' &&
           escape != 'f' && escape != 'n' && escape != 'r' && escape != 't' &&
           escape != 'u')) {
        break;
      }
      if (escape == 'u') {
        size_t index;
        for (index = 0U; index < 4U; ++index) {
          if (confit_v2_hex_value(text[cursor + 2U + index]) < 0) {
            break;
          }
        }
        if (index != 4U) {
          break;
        }
        cursor += 6U;
      } else {
        cursor += 2U;
      }
    } else {
      cursor += 1U;
    }
  }
  confit_v2_expression_diagnostic_at(lexer->source, start, CONFIT_ERR_SCHEMA,
                                     kExpressionInvalidString, diagnostic);
  return CONFIT_ERR_SCHEMA;
}

static ConfitStatus confit_v2_lex_identifier(ConfitV2ExpressionLexer *lexer,
                                              ConfitV2ExpressionToken *token,
                                              ConfitDiagnostic *diagnostic) {
  const char *text = lexer->source->text;
  const size_t start = lexer->offset;
  size_t cursor = start;

  while (confit_v2_is_identifier_tail(text[cursor])) {
    cursor += 1U;
  }
  while (text[cursor] == '.') {
    const size_t segment_start = cursor + 1U;
    if (!confit_v2_is_lower_alpha(text[segment_start])) {
      confit_v2_expression_diagnostic_at(lexer->source, cursor,
                                         CONFIT_ERR_SCHEMA,
                                         kExpressionInvalidCharacter,
                                         diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    cursor = segment_start + 1U;
    while (confit_v2_is_identifier_tail(text[cursor])) {
      cursor += 1U;
    }
  }
  lexer->offset = cursor;
  token->start_offset = start;
  token->end_offset = cursor;
  token->kind = CONFIT_V2_TOKEN_IDENTIFIER;
  if (confit_v2_token_matches(text, start, cursor, "true")) {
    token->kind = CONFIT_V2_TOKEN_TRUE;
  } else if (confit_v2_token_matches(text, start, cursor, "false")) {
    token->kind = CONFIT_V2_TOKEN_FALSE;
  } else if (confit_v2_token_matches(text, start, cursor, "n") ||
             confit_v2_token_matches(text, start, cursor, "m") ||
             confit_v2_token_matches(text, start, cursor, "y")) {
    token->kind = CONFIT_V2_TOKEN_TRISTATE;
  } else if (confit_v2_token_matches(text, start, cursor, "in")) {
    token->kind = CONFIT_V2_TOKEN_IN;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_lex_number(ConfitV2ExpressionLexer *lexer,
                                          ConfitV2ExpressionToken *token,
                                          ConfitDiagnostic *diagnostic) {
  const char *text = lexer->source->text;
  const size_t start = lexer->offset;
  size_t cursor = start;
  int is_float = 0;

  if (text[cursor] == '0' && (text[cursor + 1U] == 'x' || text[cursor + 1U] == 'X')) {
    cursor += 2U;
    if (!confit_v2_is_hex(text[cursor])) {
      confit_v2_expression_diagnostic_at(lexer->source, start, CONFIT_ERR_SCHEMA,
                                         kExpressionInvalidNumber, diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    while (confit_v2_is_hex(text[cursor])) {
      cursor += 1U;
    }
    token->kind = CONFIT_V2_TOKEN_HEX;
  } else {
    while (confit_v2_is_digit(text[cursor])) {
      cursor += 1U;
    }
    if (text[cursor] == '.') {
      is_float = 1;
      cursor += 1U;
      if (!confit_v2_is_digit(text[cursor])) {
        confit_v2_expression_diagnostic_at(lexer->source, start,
                                           CONFIT_ERR_SCHEMA,
                                           kExpressionInvalidNumber,
                                           diagnostic);
        return CONFIT_ERR_SCHEMA;
      }
      while (confit_v2_is_digit(text[cursor])) {
        cursor += 1U;
      }
    }
    if (text[cursor] == 'e' || text[cursor] == 'E') {
      is_float = 1;
      cursor += 1U;
      if (text[cursor] == '+' || text[cursor] == '-') {
        cursor += 1U;
      }
      if (!confit_v2_is_digit(text[cursor])) {
        confit_v2_expression_diagnostic_at(lexer->source, start,
                                           CONFIT_ERR_SCHEMA,
                                           kExpressionInvalidNumber,
                                           diagnostic);
        return CONFIT_ERR_SCHEMA;
      }
      while (confit_v2_is_digit(text[cursor])) {
        cursor += 1U;
      }
    }
    token->kind = is_float ? CONFIT_V2_TOKEN_FLOAT : CONFIT_V2_TOKEN_INTEGER;
  }
  lexer->offset = cursor;
  token->start_offset = start;
  token->end_offset = cursor;
  return CONFIT_OK;
}

ConfitStatus confit_v2_expression_lexer_next(
    ConfitV2ExpressionLexer *lexer, ConfitV2ExpressionToken *out_token,
    ConfitDiagnostic *diagnostic) {
  const char *text;
  size_t start;
  char current;

  if (lexer == 0 || lexer->source == 0 || lexer->source->text == 0 ||
      out_token == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid expression lexer argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  text = lexer->source->text;
  while (confit_v2_is_space(text[lexer->offset])) {
    lexer->offset += 1U;
  }
  if (lexer->token_count >= lexer->max_tokens) {
    confit_v2_expression_diagnostic_at(lexer->source, lexer->offset,
                                       CONFIT_ERR_SCHEMA, kExpressionTokenLimit,
                                       diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  lexer->token_count += 1U;
  current = text[lexer->offset];
  out_token->start_offset = lexer->offset;
  out_token->end_offset = lexer->offset + 1U;
  if (current == '\0') {
    out_token->kind = CONFIT_V2_TOKEN_EOF;
    return CONFIT_OK;
  }
  if (confit_v2_is_lower_alpha(current)) {
    return confit_v2_lex_identifier(lexer, out_token, diagnostic);
  }
  if (confit_v2_is_digit(current)) {
    return confit_v2_lex_number(lexer, out_token, diagnostic);
  }
  start = lexer->offset;
  lexer->offset += 1U;
  switch (current) {
  case '"':
    lexer->offset = start;
    return confit_v2_lex_string(lexer, out_token, diagnostic);
  case '(':
    out_token->kind = CONFIT_V2_TOKEN_LEFT_PAREN;
    return CONFIT_OK;
  case ')':
    out_token->kind = CONFIT_V2_TOKEN_RIGHT_PAREN;
    return CONFIT_OK;
  case '[':
    out_token->kind = CONFIT_V2_TOKEN_LEFT_BRACKET;
    return CONFIT_OK;
  case ']':
    out_token->kind = CONFIT_V2_TOKEN_RIGHT_BRACKET;
    return CONFIT_OK;
  case ',':
    out_token->kind = CONFIT_V2_TOKEN_COMMA;
    return CONFIT_OK;
  case '?':
    out_token->kind = CONFIT_V2_TOKEN_QUESTION;
    return CONFIT_OK;
  case ':':
    out_token->kind = CONFIT_V2_TOKEN_COLON;
    return CONFIT_OK;
  case '+':
    out_token->kind = CONFIT_V2_TOKEN_PLUS;
    return CONFIT_OK;
  case '-':
    out_token->kind = CONFIT_V2_TOKEN_MINUS;
    return CONFIT_OK;
  case '*':
    out_token->kind = CONFIT_V2_TOKEN_STAR;
    return CONFIT_OK;
  case '/':
    out_token->kind = CONFIT_V2_TOKEN_SLASH;
    return CONFIT_OK;
  case '%':
    out_token->kind = CONFIT_V2_TOKEN_PERCENT;
    return CONFIT_OK;
  case '!':
    if (text[lexer->offset] == '=') {
      lexer->offset += 1U;
      out_token->end_offset += 1U;
      out_token->kind = CONFIT_V2_TOKEN_NOT_EQUAL;
    } else {
      out_token->kind = CONFIT_V2_TOKEN_NOT;
    }
    return CONFIT_OK;
  case '=':
    if (text[lexer->offset] == '=') {
      lexer->offset += 1U;
      out_token->end_offset += 1U;
      out_token->kind = CONFIT_V2_TOKEN_EQUAL;
      return CONFIT_OK;
    }
    break;
  case '<':
    if (text[lexer->offset] == '=') {
      lexer->offset += 1U;
      out_token->end_offset += 1U;
      out_token->kind = CONFIT_V2_TOKEN_LESS_EQUAL;
    } else {
      out_token->kind = CONFIT_V2_TOKEN_LESS;
    }
    return CONFIT_OK;
  case '>':
    if (text[lexer->offset] == '=') {
      lexer->offset += 1U;
      out_token->end_offset += 1U;
      out_token->kind = CONFIT_V2_TOKEN_GREATER_EQUAL;
    } else {
      out_token->kind = CONFIT_V2_TOKEN_GREATER;
    }
    return CONFIT_OK;
  case '&':
    if (text[lexer->offset] == '&') {
      lexer->offset += 1U;
      out_token->end_offset += 1U;
      out_token->kind = CONFIT_V2_TOKEN_AND;
      return CONFIT_OK;
    }
    break;
  case '|':
    if (text[lexer->offset] == '|') {
      lexer->offset += 1U;
      out_token->end_offset += 1U;
      out_token->kind = CONFIT_V2_TOKEN_OR;
      return CONFIT_OK;
    }
    break;
  default:
    break;
  }
  confit_v2_expression_diagnostic_at(lexer->source, start, CONFIT_ERR_SCHEMA,
                                     kExpressionInvalidCharacter, diagnostic);
  return CONFIT_ERR_SCHEMA;
}

static int confit_v2_emit_utf8(char *out, size_t *offset, uint32_t scalar) {
  if (scalar <= 0x7FU) {
    out[(*offset)++] = (char)scalar;
  } else if (scalar <= 0x7FFU) {
    out[(*offset)++] = (char)(0xC0U | (scalar >> 6U));
    out[(*offset)++] = (char)(0x80U | (scalar & 0x3FU));
  } else if (scalar <= 0xFFFFU) {
    out[(*offset)++] = (char)(0xE0U | (scalar >> 12U));
    out[(*offset)++] = (char)(0x80U | ((scalar >> 6U) & 0x3FU));
    out[(*offset)++] = (char)(0x80U | (scalar & 0x3FU));
  } else if (scalar <= 0x10FFFFU) {
    out[(*offset)++] = (char)(0xF0U | (scalar >> 18U));
    out[(*offset)++] = (char)(0x80U | ((scalar >> 12U) & 0x3FU));
    out[(*offset)++] = (char)(0x80U | ((scalar >> 6U) & 0x3FU));
    out[(*offset)++] = (char)(0x80U | (scalar & 0x3FU));
  } else {
    return 0;
  }
  return 1;
}

static int confit_v2_parse_unicode_scalar(const char *text, size_t *cursor,
                                           uint32_t *out_scalar) {
  uint32_t scalar = 0U;
  size_t index;

  for (index = 0U; index < 4U; ++index) {
    const int digit = confit_v2_hex_value(text[*cursor + index]);
    if (digit < 0) {
      return 0;
    }
    scalar = (scalar << 4U) | (uint32_t)digit;
  }
  *cursor += 4U;
  if (scalar >= 0xD800U && scalar <= 0xDBFFU) {
    uint32_t low = 0U;
    if (text[*cursor] != '\\' || text[*cursor + 1U] != 'u') {
      return 0;
    }
    *cursor += 2U;
    for (index = 0U; index < 4U; ++index) {
      const int digit = confit_v2_hex_value(text[*cursor + index]);
      if (digit < 0) {
        return 0;
      }
      low = (low << 4U) | (uint32_t)digit;
    }
    *cursor += 4U;
    if (low < 0xDC00U || low > 0xDFFFU) {
      return 0;
    }
    scalar = 0x10000U + ((scalar - 0xD800U) << 10U) + (low - 0xDC00U);
  } else if (scalar >= 0xDC00U && scalar <= 0xDFFFU) {
    return 0;
  }
  *out_scalar = scalar;
  return 1;
}

ConfitStatus confit_v2_expression_token_string(
    const ConfitV2ExpressionText *source, const ConfitV2ExpressionToken *token,
    char **out_text, ConfitDiagnostic *diagnostic) {
  const char *text;
  char *out;
  size_t cursor;
  size_t output_offset;

  if (source == 0 || source->text == 0 || token == 0 || out_text == 0 ||
      token->kind != CONFIT_V2_TOKEN_STRING ||
      token->end_offset < token->start_offset + 2U) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid expression string token");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_text = 0;
  text = source->text;
  out = (char *)malloc(token->end_offset - token->start_offset);
  if (out == 0) {
    confit_v2_expression_diagnostic_at(source, token->start_offset,
                                       CONFIT_ERR_INTERNAL,
                                       kExpressionAllocationFailed, diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  cursor = token->start_offset + 1U;
  output_offset = 0U;
  while (cursor + 1U < token->end_offset) {
    if (text[cursor] == '\\') {
      const char escape = text[cursor + 1U];
      cursor += 2U;
      switch (escape) {
      case '"':
      case '\\':
      case '/':
        out[output_offset++] = escape;
        break;
      case 'b':
        out[output_offset++] = '\b';
        break;
      case 'f':
        out[output_offset++] = '\f';
        break;
      case 'n':
        out[output_offset++] = '\n';
        break;
      case 'r':
        out[output_offset++] = '\r';
        break;
      case 't':
        out[output_offset++] = '\t';
        break;
      case 'u': {
        uint32_t scalar;
        if (!confit_v2_parse_unicode_scalar(text, &cursor, &scalar) ||
            !confit_v2_emit_utf8(out, &output_offset, scalar)) {
          free(out);
          confit_v2_expression_diagnostic_at(source, token->start_offset,
                                             CONFIT_ERR_SCHEMA,
                                             kExpressionInvalidString,
                                             diagnostic);
          return CONFIT_ERR_SCHEMA;
        }
        break;
      }
      default:
        free(out);
        confit_v2_expression_diagnostic_at(source, token->start_offset,
                                           CONFIT_ERR_SCHEMA,
                                           kExpressionInvalidString,
                                           diagnostic);
        return CONFIT_ERR_SCHEMA;
      }
    } else {
      out[output_offset++] = text[cursor++];
    }
  }
  out[output_offset] = '\0';
  *out_text = out;
  return CONFIT_OK;
}
