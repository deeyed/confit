#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "confit/diagnostic.h"
#include "confit/expression_v2.h"
#include "confit/status.h"

static ConfitV2ExpressionType make_type(ConfitV2ExpressionTypeKind kind) {
  ConfitV2ExpressionType type;

  memset(&type, 0, sizeof(type));
  type.kind = kind;
  return type;
}

static ConfitStatus compile_expression(
    const char *text, const ConfitV2ExpressionEnvironment *environment,
    ConfitV2Expression **out_expression, ConfitV2TypedExpression **out_typed,
    ConfitDiagnostic *diagnostic) {
  ConfitV2ExpressionText source;
  ConfitStatus status;

  memset(&source, 0, sizeof(source));
  source.text = (char *)text;
  source.span.path = "expression-semantics";
  source.span.line = 7U;
  source.span.column = 3U;
  *out_expression = 0;
  *out_typed = 0;
  status = confit_v2_expression_parse(&source, 0, out_expression, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_v2_expression_type_check(*out_expression, environment, out_typed,
                                            diagnostic);
  if (status != CONFIT_OK) {
    confit_v2_expression_free(*out_expression);
    *out_expression = 0;
  }
  return status;
}

static int expect_bool(const char *text,
                       const ConfitV2ExpressionEnvironment *environment,
                       int expected) {
  ConfitV2Expression *expression;
  ConfitV2TypedExpression *typed;
  ConfitV2ExpressionValue value;
  ConfitDiagnostic diagnostic;
  ConfitStatus status;

  confit_diagnostic_init(&diagnostic);
  memset(&value, 0, sizeof(value));
  status = compile_expression(text, environment, &expression, &typed, &diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_expression_evaluate(typed, environment, &value, &diagnostic);
  }
  if (status != CONFIT_OK || !value.is_set ||
      value.type.kind != CONFIT_V2_EXPRESSION_TYPE_BOOL ||
      value.value.kind != CONFIT_V2_VALUE_BOOL ||
      value.value.as.bool_value != expected) {
    confit_v2_expression_value_clear(&value);
    confit_v2_typed_expression_free(typed);
    confit_v2_expression_free(expression);
    return 0;
  }
  confit_v2_expression_value_clear(&value);
  confit_v2_typed_expression_free(typed);
  confit_v2_expression_free(expression);
  return 1;
}

static int expect_string(const char *text,
                         const ConfitV2ExpressionEnvironment *environment,
                         const char *expected) {
  ConfitV2Expression *expression;
  ConfitV2TypedExpression *typed;
  ConfitV2ExpressionValue value;
  ConfitDiagnostic diagnostic;
  ConfitStatus status;

  confit_diagnostic_init(&diagnostic);
  memset(&value, 0, sizeof(value));
  status = compile_expression(text, environment, &expression, &typed, &diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_expression_evaluate(typed, environment, &value, &diagnostic);
  }
  if (status != CONFIT_OK || !value.is_set ||
      value.type.kind != CONFIT_V2_EXPRESSION_TYPE_STRING ||
      value.value.kind != CONFIT_V2_VALUE_STRING ||
      strcmp(value.value.as.string_value, expected) != 0) {
    confit_v2_expression_value_clear(&value);
    confit_v2_typed_expression_free(typed);
    confit_v2_expression_free(expression);
    return 0;
  }
  confit_v2_expression_value_clear(&value);
  confit_v2_typed_expression_free(typed);
  confit_v2_expression_free(expression);
  return 1;
}

static int expect_type_error(const char *text,
                             const ConfitV2ExpressionEnvironment *environment,
                             const char *message, size_t line, size_t column) {
  ConfitV2Expression *expression;
  ConfitV2TypedExpression *typed;
  ConfitDiagnostic diagnostic;

  confit_diagnostic_init(&diagnostic);
  if (compile_expression(text, environment, &expression, &typed, &diagnostic) !=
          CONFIT_ERR_SCHEMA ||
      expression != 0 || typed != 0 || diagnostic.message == 0 ||
      strcmp(diagnostic.message, message) != 0 || diagnostic.line != line ||
      diagnostic.column != column) {
    confit_v2_typed_expression_free(typed);
    confit_v2_expression_free(expression);
    return 0;
  }
  return 1;
}

static int expect_evaluate_error(
    const char *text, const ConfitV2ExpressionEnvironment *environment,
    const char *message) {
  ConfitV2Expression *expression;
  ConfitV2TypedExpression *typed;
  ConfitV2ExpressionValue value;
  ConfitDiagnostic diagnostic;

  confit_diagnostic_init(&diagnostic);
  memset(&value, 0, sizeof(value));
  if (compile_expression(text, environment, &expression, &typed, &diagnostic) !=
          CONFIT_OK ||
      confit_v2_expression_evaluate(typed, environment, &value, &diagnostic) !=
          CONFIT_ERR_SCHEMA ||
      diagnostic.message == 0 || strcmp(diagnostic.message, message) != 0) {
    confit_v2_expression_value_clear(&value);
    confit_v2_typed_expression_free(typed);
    confit_v2_expression_free(expression);
    return 0;
  }
  confit_v2_expression_value_clear(&value);
  confit_v2_typed_expression_free(typed);
  confit_v2_expression_free(expression);
  return 1;
}

static int evaluate_float(const char *text, double *out_result) {
  ConfitV2Expression *expression;
  ConfitV2TypedExpression *typed;
  ConfitV2ExpressionValue value;
  ConfitV2ExpressionEnvironment environment;
  ConfitDiagnostic diagnostic;
  ConfitStatus status;

  confit_diagnostic_init(&diagnostic);
  memset(&environment, 0, sizeof(environment));
  memset(&value, 0, sizeof(value));
  status = compile_expression(text, &environment, &expression, &typed, &diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_expression_evaluate(typed, &environment, &value, &diagnostic);
  }
  if (status != CONFIT_OK || !value.is_set ||
      value.type.kind != CONFIT_V2_EXPRESSION_TYPE_FLOAT ||
      value.value.kind != CONFIT_V2_VALUE_FLOAT) {
    confit_v2_expression_value_clear(&value);
    confit_v2_typed_expression_free(typed);
    confit_v2_expression_free(expression);
    return 0;
  }
  *out_result = value.value.as.float_value;
  confit_v2_expression_value_clear(&value);
  confit_v2_typed_expression_free(typed);
  confit_v2_expression_free(expression);
  return 1;
}

static int test_type_matrix_and_builtins(void) {
  ConfitV2Value driver;
  ConfitV2Value target_kind;
  ConfitV2Value mode;
  ConfitV2ExpressionBinding bindings[3];
  ConfitV2ExpressionEnvironment environment;

  memset(&driver, 0, sizeof(driver));
  driver.kind = CONFIT_V2_VALUE_TRISTATE;
  driver.as.tristate_value = 'y';
  memset(&target_kind, 0, sizeof(target_kind));
  target_kind.kind = CONFIT_V2_VALUE_STRING;
  target_kind.as.string_value = "sim";
  memset(&mode, 0, sizeof(mode));
  mode.kind = CONFIT_V2_VALUE_STRING;
  mode.as.string_value = "fast";
  memset(bindings, 0, sizeof(bindings));
  bindings[0].id = "delos.driver.usb";
  bindings[0].type = make_type(CONFIT_V2_EXPRESSION_TYPE_TRISTATE);
  bindings[0].value = &driver;
  bindings[1].id = "delos.target.kind";
  bindings[1].type = make_type(CONFIT_V2_EXPRESSION_TYPE_STRING);
  bindings[1].value = &target_kind;
  bindings[2].id = "delos.mode";
  bindings[2].type = make_type(CONFIT_V2_EXPRESSION_TYPE_ENUM);
  bindings[2].type.enum_id = "delos.mode";
  bindings[2].value = &mode;
  environment.bindings = bindings;
  environment.binding_count = 3U;
  return expect_bool(
             "enabled(delos.driver.usb) && delos.target.kind in [\"sim\", \"hw\"] && len([\"a\", \"b\"]) == 0x2 && contains([\"sim\"], delos.target.kind) && starts_with(\"delos\", \"de\") && ends_with(\"delos\", \"os\") && enum_name(delos.mode) == \"fast\"",
             &environment, 1) &&
         expect_string("concat(\"de\", \"los\")", &environment, "delos");
}

static int test_hard_type_errors(void) {
  ConfitV2Value count;
  ConfitV2Value first_enum;
  ConfitV2Value second_enum;
  ConfitV2ExpressionBinding bindings[3];
  ConfitV2ExpressionEnvironment environment;

  memset(&count, 0, sizeof(count));
  count.kind = CONFIT_V2_VALUE_INT;
  count.as.int_value = 1;
  memset(&first_enum, 0, sizeof(first_enum));
  first_enum.kind = CONFIT_V2_VALUE_STRING;
  first_enum.as.string_value = "a";
  memset(&second_enum, 0, sizeof(second_enum));
  second_enum.kind = CONFIT_V2_VALUE_STRING;
  second_enum.as.string_value = "a";
  memset(bindings, 0, sizeof(bindings));
  bindings[0].id = "delos.count";
  bindings[0].type = make_type(CONFIT_V2_EXPRESSION_TYPE_INT);
  bindings[0].value = &count;
  bindings[1].id = "delos.first";
  bindings[1].type = make_type(CONFIT_V2_EXPRESSION_TYPE_ENUM);
  bindings[1].type.enum_id = "first";
  bindings[1].value = &first_enum;
  bindings[2].id = "delos.second";
  bindings[2].type = make_type(CONFIT_V2_EXPRESSION_TYPE_ENUM);
  bindings[2].type.enum_id = "second";
  bindings[2].value = &second_enum;
  environment.bindings = bindings;
  environment.binding_count = 3U;
  return expect_type_error("delos.count && true", &environment,
                           "boolean operator requires bool operands", 7U, 3U) &&
         expect_type_error("delos.count == 1.0", &environment,
                           "equality requires identical operand types", 7U, 3U) &&
         expect_type_error("delos.first == delos.second", &environment,
                           "equality requires identical operand types", 7U, 3U) &&
         expect_type_error("delos.first == \"x\"", &environment,
                           "equality requires identical operand types", 7U, 3U) &&
         expect_type_error("builtin(delos.count)", &environment,
                           "builtin argument type is invalid", 7U, 3U) &&
         expect_type_error("len([])", &environment,
                           "empty list literal has no static element type", 7U, 7U);
}

static int test_unset_and_runtime_failures(void) {
  ConfitV2Value unset;
  ConfitV2Value maximum;
  ConfitV2Value count;
  ConfitV2ExpressionBinding bindings[3];
  ConfitV2ExpressionEnvironment environment;

  memset(&unset, 0, sizeof(unset));
  unset.kind = CONFIT_V2_VALUE_UNSET;
  memset(&maximum, 0, sizeof(maximum));
  maximum.kind = CONFIT_V2_VALUE_INT;
  maximum.as.int_value = INT64_MAX;
  memset(&count, 0, sizeof(count));
  count.kind = CONFIT_V2_VALUE_INT;
  count.as.int_value = 3;
  memset(bindings, 0, sizeof(bindings));
  bindings[0].id = "delos.output.name";
  bindings[0].type = make_type(CONFIT_V2_EXPRESSION_TYPE_STRING);
  bindings[0].value = &unset;
  bindings[1].id = "delos.maximum";
  bindings[1].type = make_type(CONFIT_V2_EXPRESSION_TYPE_INT);
  bindings[1].value = &maximum;
  bindings[2].id = "delos.count";
  bindings[2].type = make_type(CONFIT_V2_EXPRESSION_TYPE_INT);
  bindings[2].value = &count;
  environment.bindings = bindings;
  environment.binding_count = 3U;
  return expect_bool("!defined(delos.output.name) || delos.output.name != \"\"",
                     &environment, 1) &&
         expect_evaluate_error("delos.output.name != \"\"", &environment,
                               "expression value is unset") &&
         expect_evaluate_error("delos.maximum + 1", &environment,
                               "expression integer overflow") &&
         expect_evaluate_error("delos.count / 0", &environment,
                               "expression division by zero") &&
         expect_evaluate_error("1e308 * 1e308", &environment,
                               "expression float result is non-finite");
}

static int test_type_conversion_helper(void) {
  ConfitV2ExpressionType enum_set = confit_v2_expression_type_from_option_type(
      CONFIT_V2_OPTION_TYPE_ENUM_SET, "delos.feature");
  ConfitV2ExpressionType expected;

  memset(&expected, 0, sizeof(expected));
  expected.kind = CONFIT_V2_EXPRESSION_TYPE_COLLECTION;
  expected.element_kind = CONFIT_V2_EXPRESSION_TYPE_ENUM;
  expected.element_enum_id = "delos.feature";
  return confit_v2_expression_type_equal(&enum_set, &expected) &&
         strcmp(confit_v2_expression_type_name(&enum_set), "collection") == 0;
}

static int test_locale_independent_float_evaluation(void) {
  static const char *const candidates[] = {"fr_FR.UTF-8", "de_DE.UTF-8",
                                             "ko_KR.UTF-8"};
  const char *current = setlocale(LC_NUMERIC, 0);
  char *saved = current != 0 ? (char *)malloc(strlen(current) + 1U) : 0;
  double baseline;
  size_t index;

  if (current != 0 && saved == 0) {
    return 0;
  }
  if (saved != 0) {
    memcpy(saved, current, strlen(current) + 1U);
  }
  if (setlocale(LC_NUMERIC, "C") == 0 ||
      !evaluate_float("1.5 + 2.25", &baseline) || fabs(baseline - 3.75) > 1e-12) {
    free(saved);
    return 0;
  }
  for (index = 0U; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
    double observed;
    if (setlocale(LC_NUMERIC, candidates[index]) != 0 &&
        (!evaluate_float("1.5 + 2.25", &observed) ||
         fabs(observed - baseline) > 1e-12)) {
      if (saved != 0) {
        (void)setlocale(LC_NUMERIC, saved);
      }
      free(saved);
      return 0;
    }
  }
  if (saved != 0) {
    (void)setlocale(LC_NUMERIC, saved);
  }
  free(saved);
  return 1;
}

int main(void) {
  if (!test_type_matrix_and_builtins()) {
    return 2;
  }
  if (!test_hard_type_errors()) {
    return 3;
  }
  if (!test_unset_and_runtime_failures()) {
    return 4;
  }
  if (!test_type_conversion_helper()) {
    return 5;
  }
  if (!test_locale_independent_float_evaluation()) {
    return 6;
  }
  return 0;
}
