#include "confit/expression.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"

typedef struct TestDefinition {
  const char *symbol;
  ConfitValueKind kind;
  const char *dependency;
  int boolean;
  int64_t integer;
  uint64_t hexadecimal;
  const char *text;
  const char *const *enum_values;
  size_t enum_value_count;
} TestDefinition;

typedef struct FailingAllocatorState {
  size_t calls;
  size_t fail_at;
  size_t live;
} FailingAllocatorState;

static void *failing_allocate(void *context, size_t size) {
  FailingAllocatorState *state = (FailingAllocatorState *)context;
  void *pointer;
  if (state->calls++ == state->fail_at) return 0;
  pointer = malloc(size);
  if (pointer != 0) ++state->live;
  return pointer;
}

static void failing_deallocate(void *context, void *pointer) {
  FailingAllocatorState *state = (FailingAllocatorState *)context;
  CONFIT_TEST_ASSERT(pointer != 0 && state->live != 0U);
  --state->live;
  free(pointer);
}

static ConfitStatus set_definition_value(const TestDefinition *definition,
                                         ConfitValue *value,
                                         ConfitDiagnostic *diagnostic) {
  switch (definition->kind) {
    case CONFIT_VALUE_BOOL:
      return confit_value_set_bool(value, definition->boolean, 0, diagnostic);
    case CONFIT_VALUE_INT:
      return confit_value_set_int(value, definition->integer, 0, diagnostic);
    case CONFIT_VALUE_HEX:
      return confit_value_set_hex(value, definition->hexadecimal, 0,
                                  diagnostic);
    case CONFIT_VALUE_STRING:
      return confit_value_set_string(value, definition->text,
                                     strlen(definition->text), 0, diagnostic);
    case CONFIT_VALUE_ENUM:
      return confit_value_set_enum(value, definition->text,
                                   strlen(definition->text), 0, diagnostic);
    default:
      return CONFIT_ERR_INTERNAL;
  }
}

static ConfitCatalog *make_catalog(const TestDefinition *definitions,
                                   size_t count) {
  ConfitCatalog *catalog = 0;
  ConfitDiagnostic diagnostic;
  ConfitSourceFragmentSpec fragment;
  size_t index;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_catalog_create(0, &catalog, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_catalog_set_mainmenu(
                         catalog, "Expression test", &diagnostic) == CONFIT_OK);
  fragment.path = "config/expression.toml";
  fragment.parent_fragment = CONFIT_INDEX_NONE;
  fragment.source_ordinal = 0U;
  CONFIT_TEST_ASSERT(confit_catalog_add_fragment(catalog, &fragment, 0,
                                                 &diagnostic) == CONFIT_OK);
  for (index = 0U; index < count; ++index) {
    ConfitConfigSpec spec;
    ConfitValue value;
    confit_value_init(&value);
    CONFIT_TEST_ASSERT(set_definition_value(&definitions[index], &value,
                                            &diagnostic) == CONFIT_OK);
    memset(&spec, 0, sizeof(spec));
    spec.fragment = 0U;
    spec.menu = CONFIT_INDEX_NONE;
    spec.symbol = definitions[index].symbol;
    spec.kind = definitions[index].kind;
    spec.prompt = definitions[index].symbol;
    spec.help = "Test one bounded dependency expression.";
    spec.default_value = &value;
    spec.enum_values = definitions[index].enum_values;
    spec.enum_value_count = definitions[index].enum_value_count;
    spec.dependency_text = definitions[index].dependency;
    spec.declaration.path = "config/expression.toml";
    spec.declaration.line = index + 1U;
    spec.declaration.column = 1U;
    CONFIT_TEST_ASSERT(confit_catalog_add_config(catalog, &spec, 0,
                                                 &diagnostic) == CONFIT_OK);
    confit_value_destroy(&value);
  }
  return catalog;
}

static void make_values(const ConfitCatalog *catalog, ConfitValue *values,
                        size_t count) {
  ConfitDiagnostic diagnostic;
  size_t index;
  confit_diagnostic_init(&diagnostic);
  for (index = 0U; index < count; ++index) {
    ConfitConfigView view;
    confit_value_init(&values[index]);
    CONFIT_TEST_ASSERT(confit_catalog_config_at(catalog, index, &view));
    CONFIT_TEST_ASSERT(confit_value_copy(&values[index], view.default_value, 0,
                                         &diagnostic) == CONFIT_OK);
  }
}

static void destroy_values(ConfitValue *values, size_t count) {
  size_t index;
  for (index = count; index > 0U; --index)
    confit_value_destroy(&values[index - 1U]);
}

static size_t find_index(const ConfitCatalog *catalog, const char *symbol) {
  size_t index;
  for (index = 0U; index < confit_catalog_config_count(catalog); ++index) {
    ConfitConfigView view;
    CONFIT_TEST_ASSERT(confit_catalog_config_at(catalog, index, &view));
    if (strcmp(view.symbol, symbol) == 0) return index;
  }
  CONFIT_TEST_ASSERT(0);
  return 0U;
}

static int evaluate(const ConfitDependencyPlan *plan, size_t config_index,
                    const ConfitValue *values, size_t value_count,
                    size_t *out_reason_count,
                    ConfitDependencyReasonView *out_root) {
  ConfitDependencyEvaluation *evaluation = 0;
  ConfitDiagnostic diagnostic;
  size_t root;
  int available;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_dependency_plan_evaluate(
                         plan, config_index, values, value_count, 0,
                         &evaluation, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(evaluation != 0);
  available = confit_dependency_evaluation_available(evaluation);
  *out_reason_count =
      confit_dependency_evaluation_reason_count(evaluation);
  root = confit_dependency_evaluation_reason_root(evaluation);
  CONFIT_TEST_ASSERT(confit_dependency_evaluation_reason_at(evaluation, root,
                                                            out_root));
  confit_dependency_evaluation_destroy(evaluation);
  return available;
}

static void test_precedence_types_and_reasons(void) {
  static const char *const modes[] = {"quiet", "normal", "verbose"};
  static const TestDefinition definitions[] = {
      {"BOOL_A", CONFIT_VALUE_BOOL, 0, 1, 0, 0U, 0, 0, 0U},
      {"BOOL_B", CONFIT_VALUE_BOOL, 0, 0, 0, 0U, 0, 0, 0U},
      {"COUNT", CONFIT_VALUE_INT, 0, 0, 4, 0U, 0, 0, 0U},
      {"DEVICE_ID", CONFIT_VALUE_HEX, 0, 0, 0, UINT64_C(0x10), 0, 0, 0U},
      {"NAME", CONFIT_VALUE_STRING, 0, 0, 0, 0U, "alpha", 0, 0U},
      {"MODE", CONFIT_VALUE_ENUM, 0, 0, 0, 0U, "normal", modes, 3U},
      {"PRECEDENCE", CONFIT_VALUE_BOOL, "BOOL_A || BOOL_B && !BOOL_A", 0,
       0, 0U, 0, 0, 0U},
      {"PARENTHESES", CONFIT_VALUE_BOOL, "(BOOL_A || BOOL_B) && !BOOL_A", 0,
       0, 0U, 0, 0, 0U},
      {"ALL_TYPES", CONFIT_VALUE_BOOL,
       "BOOL_A && COUNT == 4 && DEVICE_ID != 0x20 && NAME == \"alpha\" && "
       "MODE == \"normal\"",
       0, 0, 0U, 0, 0, 0U},
      {"BOOL_COMPARE", CONFIT_VALUE_BOOL, "BOOL_B == false", 0, 0, 0U, 0, 0,
       0U},
      {"ALL_NOT_EQUAL", CONFIT_VALUE_BOOL,
       "BOOL_A != false && COUNT != 5 && DEVICE_ID == 0x10 && NAME != "
       "\"beta\" && MODE != \"quiet\"",
       0, 0, 0U, 0, 0, 0U},
      {"NO_DEPENDENCY", CONFIT_VALUE_BOOL, 0, 0, 0, 0U, 0, 0, 0U},
  };
  const size_t count = sizeof(definitions) / sizeof(definitions[0]);
  ConfitCatalog *catalog = make_catalog(definitions, count);
  ConfitDependencyPlan *plan = 0;
  ConfitDiagnostic diagnostic;
  ConfitValue values[sizeof(definitions) / sizeof(definitions[0])];
  ConfitDependencyReasonView root;
  size_t reason_count;
  char before[64];
  char after[64];
  size_t before_size;
  size_t after_size;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_dependency_plan_create(catalog, 0, &plan,
                                                   &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(plan != 0);
  CONFIT_TEST_ASSERT(confit_dependency_plan_config_count(plan) == count);
  CONFIT_TEST_ASSERT(confit_dependency_plan_edge_count(plan) == 15U);
  make_values(catalog, values, count);
  CONFIT_TEST_ASSERT(confit_value_format_canonical(
                         &values[0], before, sizeof(before), &before_size,
                         &diagnostic) == CONFIT_OK);

  CONFIT_TEST_ASSERT(evaluate(plan, find_index(catalog, "PRECEDENCE"), values,
                              count, &reason_count, &root));
  CONFIT_TEST_ASSERT(root.kind == CONFIT_REASON_OR && root.result == 1);
  CONFIT_TEST_ASSERT(root.child_count == 1U && reason_count == 2U);
  CONFIT_TEST_ASSERT(!evaluate(plan, find_index(catalog, "PARENTHESES"), values,
                               count, &reason_count, &root));
  CONFIT_TEST_ASSERT(root.kind == CONFIT_REASON_AND && root.result == 0);
  CONFIT_TEST_ASSERT(evaluate(plan, find_index(catalog, "ALL_TYPES"), values,
                              count, &reason_count, &root));
  CONFIT_TEST_ASSERT(root.kind == CONFIT_REASON_AND && root.result == 1);
  CONFIT_TEST_ASSERT(evaluate(plan, find_index(catalog, "BOOL_COMPARE"), values,
                              count, &reason_count, &root));
  CONFIT_TEST_ASSERT(root.kind == CONFIT_REASON_COMPARISON &&
                     strcmp(root.subject_symbol, "BOOL_B") == 0 &&
                     strcmp(root.detail, "==") == 0);
  CONFIT_TEST_ASSERT(evaluate(plan, find_index(catalog, "ALL_NOT_EQUAL"),
                              values, count, &reason_count, &root));
  CONFIT_TEST_ASSERT(root.kind == CONFIT_REASON_AND && root.result == 1);
  CONFIT_TEST_ASSERT(evaluate(plan, find_index(catalog, "NO_DEPENDENCY"), values,
                              count, &reason_count, &root));
  CONFIT_TEST_ASSERT(root.kind == CONFIT_REASON_LITERAL && reason_count == 1U);
  CONFIT_TEST_ASSERT(values[0].data.boolean == 1 &&
                     values[1].data.boolean == 0 &&
                     values[2].data.integer == 4 &&
                     values[3].data.hexadecimal == UINT64_C(0x10));

  CONFIT_TEST_ASSERT(confit_value_format_canonical(
                         &values[0], after, sizeof(after), &after_size,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(before_size == after_size &&
                     memcmp(before, after, before_size + 1U) == 0);
  destroy_values(values, count);
  confit_dependency_plan_destroy(plan);
  confit_catalog_destroy(catalog);
}

static void expect_plan_failure(const TestDefinition *definitions,
                                size_t count, const char *message) {
  ConfitCatalog *catalog = make_catalog(definitions, count);
  ConfitDependencyPlan *plan = 0;
  ConfitDiagnostic diagnostic;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_dependency_plan_create(catalog, 0, &plan,
                                                   &diagnostic) != CONFIT_OK);
  if (diagnostic.message == 0 || strcmp(diagnostic.message, message) != 0)
    fprintf(stderr, "expected '%s', got '%s'\n", message,
            diagnostic.message != 0 ? diagnostic.message : "(null)");
  CONFIT_TEST_ASSERT(plan == 0 && diagnostic.message != 0 &&
                     strcmp(diagnostic.message, message) == 0);
  confit_catalog_destroy(catalog);
}

static void test_fail_closed_linking(void) {
  static const char *const modes[] = {"quiet", "normal"};
  static const TestDefinition unknown[] = {
      {"ALWAYS", CONFIT_VALUE_BOOL, 0, 1, 0, 0U, 0, 0, 0U},
      {"FEATURE", CONFIT_VALUE_BOOL, "MISSING || ALWAYS", 0, 0, 0U, 0, 0,
       0U}};
  static const TestDefinition bare_int[] = {
      {"COUNT", CONFIT_VALUE_INT, 0, 0, 1, 0U, 0, 0, 0U},
      {"FEATURE", CONFIT_VALUE_BOOL, "COUNT", 0, 0, 0U, 0, 0, 0U}};
  static const TestDefinition wrong_type[] = {
      {"COUNT", CONFIT_VALUE_INT, 0, 0, 1, 0U, 0, 0, 0U},
      {"FEATURE", CONFIT_VALUE_BOOL, "COUNT == 0x1", 0, 0, 0U, 0, 0, 0U}};
  static const TestDefinition enum_domain[] = {
      {"MODE", CONFIT_VALUE_ENUM, 0, 0, 0, 0U, "normal", modes, 2U},
      {"FEATURE", CONFIT_VALUE_BOOL, "MODE == \"verbose\"", 0, 0, 0U, 0, 0,
       0U}};
  static const TestDefinition self_cycle[] = {
      {"FEATURE", CONFIT_VALUE_BOOL, "FEATURE", 0, 0, 0U, 0, 0, 0U}};
  static const TestDefinition multi_cycle[] = {
      {"ALPHA", CONFIT_VALUE_BOOL, "BRAVO", 0, 0, 0U, 0, 0, 0U},
      {"BRAVO", CONFIT_VALUE_BOOL, "CHARLIE", 0, 0, 0U, 0, 0, 0U},
      {"CHARLIE", CONFIT_VALUE_BOOL, "ALPHA", 0, 0, 0U, 0, 0, 0U}};
  static const TestDefinition two_cycle[] = {
      {"ALPHA", CONFIT_VALUE_BOOL, "BRAVO", 0, 0, 0U, 0, 0, 0U},
      {"BRAVO", CONFIT_VALUE_BOOL, "ALPHA", 0, 0, 0U, 0, 0, 0U}};
  static const TestDefinition invalid_operator[] = {
      {"ALPHA", CONFIT_VALUE_BOOL, 0, 1, 0, 0U, 0, 0, 0U},
      {"FEATURE", CONFIT_VALUE_BOOL, "ALPHA + 1", 0, 0, 0U, 0, 0, 0U}};
  static const TestDefinition empty[] = {
      {"FEATURE", CONFIT_VALUE_BOOL, "", 0, 0, 0U, 0, 0, 0U}};
  expect_plan_failure(unknown, 2U,
                      "dependency expression references an unknown symbol");
  expect_plan_failure(bare_int, 2U,
                      "bare dependency reference must have bool type");
  expect_plan_failure(wrong_type, 2U,
                      "dependency comparison literal has the wrong type");
  expect_plan_failure(enum_domain, 2U,
                      "dependency enum literal is outside the declared domain");
  expect_plan_failure(self_cycle, 1U,
                      "dependency expression graph contains a cycle");
  expect_plan_failure(two_cycle, 2U,
                      "dependency expression graph contains a cycle");
  expect_plan_failure(multi_cycle, 3U,
                      "dependency expression graph contains a cycle");
  expect_plan_failure(invalid_operator, 2U,
                      "dependency expression has invalid syntax");
  expect_plan_failure(empty, 1U, "dependency expression has invalid syntax");
}

static void test_long_cycle(void) {
  TestDefinition definitions[64];
  char symbols[64][8];
  size_t index;
  ConfitCatalog *catalog;
  ConfitDependencyPlan *plan = 0;
  ConfitDiagnostic diagnostic;
  for (index = 0U; index < 64U; ++index) {
    CONFIT_TEST_ASSERT(snprintf(symbols[index], sizeof(symbols[index]),
                                "NODE_%02zu", index) > 0);
    memset(&definitions[index], 0, sizeof(definitions[index]));
    definitions[index].symbol = symbols[index];
    definitions[index].kind = CONFIT_VALUE_BOOL;
    definitions[index].dependency = symbols[(index + 1U) % 64U];
  }
  catalog = make_catalog(definitions, 64U);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_dependency_plan_create(catalog, 0, &plan,
                                                   &diagnostic) ==
                     CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(plan == 0 && diagnostic.line == 1U &&
                     strcmp(diagnostic.message,
                            "dependency expression graph contains a cycle") ==
                         0);
  confit_catalog_destroy(catalog);
}

static void test_stable_order(void) {
  static const TestDefinition first[] = {
      {"ZULU", CONFIT_VALUE_BOOL, "ALPHA && MIKE", 0, 0, 0U, 0, 0, 0U},
      {"MIKE", CONFIT_VALUE_BOOL, 0, 1, 0, 0U, 0, 0, 0U},
      {"ALPHA", CONFIT_VALUE_BOOL, 0, 1, 0, 0U, 0, 0, 0U}};
  static const TestDefinition second[] = {
      {"ALPHA", CONFIT_VALUE_BOOL, 0, 1, 0, 0U, 0, 0, 0U},
      {"ZULU", CONFIT_VALUE_BOOL, "ALPHA && MIKE", 0, 0, 0U, 0, 0, 0U},
      {"MIKE", CONFIT_VALUE_BOOL, 0, 1, 0, 0U, 0, 0, 0U}};
  ConfitCatalog *catalogs[2];
  ConfitDependencyPlan *plans[2] = {0, 0};
  ConfitDiagnostic diagnostic;
  size_t project;
  size_t order;
  const char *observed[2][3];
  ConfitDependencyReasonView roots[2];
  size_t reason_counts[2];
  catalogs[0] = make_catalog(first, 3U);
  catalogs[1] = make_catalog(second, 3U);
  confit_diagnostic_init(&diagnostic);
  for (project = 0U; project < 2U; ++project) {
    CONFIT_TEST_ASSERT(confit_dependency_plan_create(
                           catalogs[project], 0, &plans[project],
                           &diagnostic) == CONFIT_OK);
    for (order = 0U; order < 3U; ++order) {
      size_t config_index;
      ConfitConfigView view;
      CONFIT_TEST_ASSERT(confit_dependency_plan_order_at(
          plans[project], order, &config_index));
      CONFIT_TEST_ASSERT(
          confit_catalog_config_at(catalogs[project], config_index, &view));
      observed[project][order] = view.symbol;
    }
    {
      ConfitValue values[3];
      make_values(catalogs[project], values, 3U);
      CONFIT_TEST_ASSERT(evaluate(
          plans[project], find_index(catalogs[project], "ZULU"), values, 3U,
          &reason_counts[project], &roots[project]));
      destroy_values(values, 3U);
    }
  }
  for (order = 0U; order < 3U; ++order)
    CONFIT_TEST_ASSERT(strcmp(observed[0][order], observed[1][order]) == 0);
  CONFIT_TEST_ASSERT(strcmp(observed[0][0], "ALPHA") == 0);
  CONFIT_TEST_ASSERT(strcmp(observed[0][1], "MIKE") == 0);
  CONFIT_TEST_ASSERT(strcmp(observed[0][2], "ZULU") == 0);
  CONFIT_TEST_ASSERT(reason_counts[0] == reason_counts[1] &&
                     roots[0].kind == roots[1].kind &&
                     roots[0].result == roots[1].result &&
                     roots[0].child_count == roots[1].child_count);
  confit_dependency_plan_destroy(plans[0]);
  confit_dependency_plan_destroy(plans[1]);
  confit_catalog_destroy(catalogs[0]);
  confit_catalog_destroy(catalogs[1]);
}

static void test_stable_cycle_anchor(void) {
  static const TestDefinition first[] = {
      {"CHARLIE", CONFIT_VALUE_BOOL, "ALPHA", 0, 0, 0U, 0, 0, 0U},
      {"ALPHA", CONFIT_VALUE_BOOL, "BRAVO", 0, 0, 0U, 0, 0, 0U},
      {"BRAVO", CONFIT_VALUE_BOOL, "CHARLIE", 0, 0, 0U, 0, 0, 0U}};
  static const TestDefinition second[] = {
      {"BRAVO", CONFIT_VALUE_BOOL, "CHARLIE", 0, 0, 0U, 0, 0, 0U},
      {"CHARLIE", CONFIT_VALUE_BOOL, "ALPHA", 0, 0, 0U, 0, 0, 0U},
      {"ALPHA", CONFIT_VALUE_BOOL, "BRAVO", 0, 0, 0U, 0, 0, 0U}};
  const TestDefinition *orders[2] = {first, second};
  const size_t expected_lines[2] = {2U, 3U};
  size_t index;
  for (index = 0U; index < 2U; ++index) {
    ConfitCatalog *catalog = make_catalog(orders[index], 3U);
    ConfitDependencyPlan *plan = 0;
    ConfitDiagnostic diagnostic;
    confit_diagnostic_init(&diagnostic);
    CONFIT_TEST_ASSERT(confit_dependency_plan_create(
                           catalog, 0, &plan, &diagnostic) ==
                       CONFIT_ERR_VALIDATION);
    CONFIT_TEST_ASSERT(plan == 0 &&
                       strcmp(diagnostic.message,
                              "dependency expression graph contains a cycle") ==
                           0 &&
                       diagnostic.line == expected_lines[index]);
    confit_catalog_destroy(catalog);
  }
}

static void make_repeated_expression(char *buffer, size_t capacity,
                                     size_t operands, int add_not) {
  size_t used = 0U;
  size_t index;
  for (index = 0U; index < operands; ++index) {
    const char *piece = index == 0U ? (add_not ? "!ALPHA" : "ALPHA")
                                    : " && ALPHA";
    size_t size = strlen(piece);
    CONFIT_TEST_ASSERT(size < capacity - used);
    memcpy(buffer + used, piece, size);
    used += size;
  }
  buffer[used] = '\0';
}

static void test_resource_bounds(void) {
  char expression[4097];
  TestDefinition definitions[2] = {
      {"ALPHA", CONFIT_VALUE_BOOL, 0, 1, 0, 0U, 0, 0, 0U},
      {"FEATURE", CONFIT_VALUE_BOOL, expression, 0, 0, 0U, 0, 0, 0U}};
  ConfitCatalog *catalog;
  ConfitDependencyPlan *plan = 0;
  ConfitDiagnostic diagnostic;
  size_t index;

  for (index = 0U; index < CONFIT_LIMIT_DEPENDENCY_NESTING; ++index)
    expression[index] = '!';
  memcpy(expression + CONFIT_LIMIT_DEPENDENCY_NESTING, "ALPHA", 6U);
  catalog = make_catalog(definitions, 2U);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_dependency_plan_create(catalog, 0, &plan,
                                                   &diagnostic) == CONFIT_OK);
  confit_dependency_plan_destroy(plan);
  confit_catalog_destroy(catalog);

  for (index = 0U; index < CONFIT_LIMIT_DEPENDENCY_NESTING + 1U; ++index)
    expression[index] = '!';
  memcpy(expression + CONFIT_LIMIT_DEPENDENCY_NESTING + 1U, "ALPHA", 6U);
  expect_plan_failure(definitions, 2U,
                      "dependency expression nesting limit is exceeded");

  make_repeated_expression(expression, sizeof(expression), 256U, 1);
  catalog = make_catalog(definitions, 2U);
  confit_diagnostic_init(&diagnostic);
  plan = 0;
  CONFIT_TEST_ASSERT(confit_dependency_plan_create(catalog, 0, &plan,
                                                   &diagnostic) == CONFIT_OK);
  confit_dependency_plan_destroy(plan);
  confit_catalog_destroy(catalog);

  make_repeated_expression(expression, sizeof(expression), 257U, 0);
  expect_plan_failure(definitions, 2U,
                      "dependency expression AST node limit is exceeded");
}

static void test_allocation_failure_cleanup(void) {
  static const TestDefinition definitions[] = {
      {"ALPHA", CONFIT_VALUE_BOOL, 0, 1, 0, 0U, 0, 0, 0U},
      {"COUNT", CONFIT_VALUE_INT, 0, 0, 7, 0U, 0, 0, 0U},
      {"FEATURE", CONFIT_VALUE_BOOL, "ALPHA && COUNT == 7", 0, 0, 0U, 0, 0,
       0U}};
  ConfitCatalog *catalog = make_catalog(definitions, 3U);
  ConfitDependencyPlan *plan = 0;
  ConfitAllocator allocator;
  ConfitDiagnostic diagnostic;
  FailingAllocatorState state;
  size_t fail_at;
  int succeeded = 0;
  allocator.context = &state;
  allocator.allocate = failing_allocate;
  allocator.deallocate = failing_deallocate;
  for (fail_at = 0U; fail_at < 128U; ++fail_at) {
    memset(&state, 0, sizeof(state));
    state.fail_at = fail_at;
    confit_diagnostic_init(&diagnostic);
    if (confit_dependency_plan_create(catalog, &allocator, &plan,
                                      &diagnostic) == CONFIT_OK) {
      confit_dependency_plan_destroy(plan);
      plan = 0;
      CONFIT_TEST_ASSERT(state.live == 0U);
      succeeded = 1;
      break;
    }
    CONFIT_TEST_ASSERT(plan == 0 && state.live == 0U);
  }
  CONFIT_TEST_ASSERT(succeeded);

  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_dependency_plan_create(catalog, 0, &plan,
                                                   &diagnostic) == CONFIT_OK);
  {
    ConfitValue values[3];
    make_values(catalog, values, 3U);
    succeeded = 0;
    for (fail_at = 0U; fail_at < 16U; ++fail_at) {
      ConfitDependencyEvaluation *evaluation = 0;
      memset(&state, 0, sizeof(state));
      state.fail_at = fail_at;
      confit_diagnostic_init(&diagnostic);
      if (confit_dependency_plan_evaluate(
              plan, 2U, values, 3U, &allocator, &evaluation,
              &diagnostic) == CONFIT_OK) {
        confit_dependency_evaluation_destroy(evaluation);
        CONFIT_TEST_ASSERT(state.live == 0U);
        succeeded = 1;
        break;
      }
      CONFIT_TEST_ASSERT(evaluation == 0 && state.live == 0U);
    }
    CONFIT_TEST_ASSERT(succeeded);
    destroy_values(values, 3U);
  }
  confit_dependency_plan_destroy(plan);
  confit_catalog_destroy(catalog);
}

int main(void) {
  test_precedence_types_and_reasons();
  test_fail_closed_linking();
  test_long_cycle();
  test_stable_order();
  test_stable_cycle_anchor();
  test_resource_bounds();
  test_allocation_failure_cleanup();
  return 0;
}
