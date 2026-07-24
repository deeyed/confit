#ifndef CONFIT_EXPRESSION_V2_LEXER_INTERNAL_H
#define CONFIT_EXPRESSION_V2_LEXER_INTERNAL_H

#include "confit/expression_v2.h"

typedef enum ConfitV2ExpressionTokenKind {
  CONFIT_V2_TOKEN_EOF = 0,
  CONFIT_V2_TOKEN_IDENTIFIER,
  CONFIT_V2_TOKEN_TRUE,
  CONFIT_V2_TOKEN_FALSE,
  CONFIT_V2_TOKEN_TRISTATE,
  CONFIT_V2_TOKEN_INTEGER,
  CONFIT_V2_TOKEN_HEX,
  CONFIT_V2_TOKEN_FLOAT,
  CONFIT_V2_TOKEN_STRING,
  CONFIT_V2_TOKEN_LEFT_PAREN,
  CONFIT_V2_TOKEN_RIGHT_PAREN,
  CONFIT_V2_TOKEN_LEFT_BRACKET,
  CONFIT_V2_TOKEN_RIGHT_BRACKET,
  CONFIT_V2_TOKEN_COMMA,
  CONFIT_V2_TOKEN_QUESTION,
  CONFIT_V2_TOKEN_COLON,
  CONFIT_V2_TOKEN_NOT,
  CONFIT_V2_TOKEN_OR,
  CONFIT_V2_TOKEN_AND,
  CONFIT_V2_TOKEN_EQUAL,
  CONFIT_V2_TOKEN_NOT_EQUAL,
  CONFIT_V2_TOKEN_LESS,
  CONFIT_V2_TOKEN_LESS_EQUAL,
  CONFIT_V2_TOKEN_GREATER,
  CONFIT_V2_TOKEN_GREATER_EQUAL,
  CONFIT_V2_TOKEN_IN,
  CONFIT_V2_TOKEN_PLUS,
  CONFIT_V2_TOKEN_MINUS,
  CONFIT_V2_TOKEN_STAR,
  CONFIT_V2_TOKEN_SLASH,
  CONFIT_V2_TOKEN_PERCENT,
} ConfitV2ExpressionTokenKind;

typedef struct ConfitV2ExpressionToken {
  ConfitV2ExpressionTokenKind kind;
  size_t start_offset;
  size_t end_offset;
} ConfitV2ExpressionToken;

typedef struct ConfitV2ExpressionLexer {
  const ConfitV2ExpressionText *source;
  size_t offset;
  size_t token_count;
  size_t max_tokens;
} ConfitV2ExpressionLexer;

void confit_v2_expression_lexer_init(ConfitV2ExpressionLexer *lexer,
                                      const ConfitV2ExpressionText *source,
                                      size_t max_tokens);
ConfitStatus confit_v2_expression_lexer_next(
    ConfitV2ExpressionLexer *lexer, ConfitV2ExpressionToken *out_token,
    ConfitDiagnostic *diagnostic);
ConfitStatus confit_v2_expression_token_string(
    const ConfitV2ExpressionText *source, const ConfitV2ExpressionToken *token,
    char **out_text, ConfitDiagnostic *diagnostic);
void confit_v2_expression_diagnostic_at(const ConfitV2ExpressionText *source,
                                        size_t offset, ConfitStatus status,
                                        const char *message,
                                        ConfitDiagnostic *diagnostic);

#endif /* CONFIT_EXPRESSION_V2_LEXER_INTERNAL_H */
