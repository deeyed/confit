#include <string.h>

#include "confit/diagnostic.h"
#include "confit/expression_v2.h"
#include "confit/host.h"
#include "confit/status.h"

#ifndef CONFIT_TEST_SOURCE_DIR
#define CONFIT_TEST_SOURCE_DIR "."
#endif

static int join_path(char *out, size_t out_size, const char *right) {
  ConfitDiagnostic diagnostic;

  confit_diagnostic_init(&diagnostic);
  return confit_host_path_join(out, out_size, CONFIT_TEST_SOURCE_DIR, right,
                               &diagnostic) == CONFIT_OK;
}

static int expect_sexpr(const char *text, const char *expected) {
  ConfitV2ExpressionText source;
  ConfitV2Expression *expression;
  ConfitDiagnostic diagnostic;
  char *actual;
  int result;

  memset(&source, 0, sizeof(source));
  source.text = (char *)text;
  source.span.path = "expression-fixture";
  source.span.line = 10U;
  source.span.column = 5U;
  confit_diagnostic_init(&diagnostic);
  expression = 0;
  if (confit_v2_expression_parse(&source, 0, &expression, &diagnostic) !=
          CONFIT_OK ||
      expression == 0 ||
      confit_v2_expression_to_sexpr(expression, &actual) != CONFIT_OK) {
    confit_v2_expression_free(expression);
    return 0;
  }
  result = strcmp(actual, expected) == 0;
  confit_v2_expression_string_free(actual);
  confit_v2_expression_free(expression);
  return result;
}

static int expect_golden(void) {
  ConfitDiagnostic diagnostic;
  char path[512];
  char *expected;
  size_t expected_size;
  ConfitV2ExpressionText source;
  ConfitV2Expression *expression;
  char *actual;
  int result;

  if (!join_path(path, sizeof(path),
                 "tests/golden/expression-v2/precedence.txt")) {
    return 0;
  }
  confit_diagnostic_init(&diagnostic);
  expected = 0;
  expected_size = 0U;
  if (confit_host_read_text_file(path, &expected, &expected_size, &diagnostic) !=
      CONFIT_OK) {
    return 0;
  }
  if (expected_size == 0U || expected[expected_size - 1U] != '\n') {
    confit_host_free(expected);
    return 0;
  }
  expected[expected_size - 1U] = '\0';
  memset(&source, 0, sizeof(source));
  source.text = "delos.target.arch == \"armv7m\" || enabled(delos.debug.ddc) && !false";
  source.span.path = "golden-expression";
  source.span.line = 4U;
  source.span.column = 3U;
  expression = 0;
  if (confit_v2_expression_parse(&source, 0, &expression, &diagnostic) !=
          CONFIT_OK ||
      confit_v2_expression_to_sexpr(expression, &actual) != CONFIT_OK) {
    confit_v2_expression_free(expression);
    confit_host_free(expected);
    return 0;
  }
  result = strcmp(actual, expected) == 0;
  confit_v2_expression_string_free(actual);
  confit_v2_expression_free(expression);
  confit_host_free(expected);
  return result;
}

static int expect_error(const char *text, const char *message, size_t line,
                        size_t column) {
  ConfitV2ExpressionText source;
  ConfitV2Expression *expression;
  ConfitDiagnostic diagnostic;

  memset(&source, 0, sizeof(source));
  source.text = (char *)text;
  source.span.path = "expression-error";
  source.span.line = 12U;
  source.span.column = 5U;
  confit_diagnostic_init(&diagnostic);
  expression = 0;
  if (confit_v2_expression_parse(&source, 0, &expression, &diagnostic) !=
          CONFIT_ERR_SCHEMA ||
      expression != 0 || diagnostic.message == 0 ||
      strcmp(diagnostic.message, message) != 0 || diagnostic.line != line ||
      diagnostic.column != column) {
    confit_v2_expression_free(expression);
    return 0;
  }
  return 1;
}

static int expect_limits(void) {
  ConfitV2ExpressionText source;
  ConfitV2ExpressionLimits limits;
  ConfitV2Expression *expression;
  ConfitDiagnostic diagnostic;

  memset(&source, 0, sizeof(source));
  source.text = "((((true))))";
  source.span.path = "limit-expression";
  source.span.line = 1U;
  source.span.column = 1U;
  limits = confit_v2_expression_default_limits();
  limits.max_nesting = 2U;
  confit_diagnostic_init(&diagnostic);
  expression = 0;
  if (confit_v2_expression_parse(&source, &limits, &expression, &diagnostic) !=
          CONFIT_ERR_SCHEMA ||
      diagnostic.message == 0 ||
      strcmp(diagnostic.message, "expression nesting limit exceeded") != 0) {
    confit_v2_expression_free(expression);
    return 0;
  }
  source.text = "true || false || true";
  limits = confit_v2_expression_default_limits();
  limits.max_nodes = 2U;
  confit_diagnostic_clear(&diagnostic);
  expression = 0;
  if (confit_v2_expression_parse(&source, &limits, &expression, &diagnostic) !=
          CONFIT_ERR_SCHEMA ||
      diagnostic.message == 0 ||
      strcmp(diagnostic.message, "expression AST node limit exceeded") != 0) {
    confit_v2_expression_free(expression);
    return 0;
  }
  source.text = "true";
  limits = confit_v2_expression_default_limits();
  limits.max_source_bytes = 2U;
  confit_diagnostic_clear(&diagnostic);
  expression = 0;
  if (confit_v2_expression_parse(&source, &limits, &expression, &diagnostic) !=
          CONFIT_ERR_SCHEMA ||
      diagnostic.message == 0 ||
      strcmp(diagnostic.message, "expression source size limit exceeded") != 0) {
    confit_v2_expression_free(expression);
    return 0;
  }
  return 1;
}

int main(void) {
  if (!expect_sexpr("1 + 2 * 3 == 7 && !false",
                    "(&& (== (+ 1 (* 2 3)) 7) (! false))")) {
    return 2;
  }
  if (!expect_sexpr("true ? [\"a\", \"b\"] : concat(\"c\", \"d\")",
                    "(?: true [\"a\" \"b\"] (concat \"c\" \"d\"))")) {
    return 3;
  }
  if (!expect_sexpr("enabled(delos.driver.usb) && m == m && 0x10 > 4",
                    "(&& (&& (enabled delos.driver.usb) (== m m)) (> 0x10 4))")) {
    return 4;
  }
  if (!expect_sexpr("\"A\\u0042\" == \"AB\"", "(== \"AB\" \"AB\")")) {
    return 5;
  }
  if (!expect_golden()) {
    return 6;
  }
  if (!expect_error("true && )", "expected expression", 12U, 13U)) {
    return 7;
  }
  if (!expect_error("true ? false", "expected ':'", 12U, 17U)) {
    return 8;
  }
  if (!expect_error("delos.debug.ddc @", "invalid expression character", 12U,
                    21U)) {
    return 9;
  }
  if (!expect_error("1 < 2 < 3", "chained relation operators are not allowed",
                    12U, 11U)) {
    return 10;
  }
  if (!expect_limits()) {
    return 11;
  }
  return 0;
}
