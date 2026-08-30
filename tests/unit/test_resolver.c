#include "confit/resolver.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/digest.h"
#include "confit/limits.h"
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
  int has_range;
  int64_t range_minimum;
  int64_t range_maximum;
} TestDefinition;

typedef struct FailingAllocatorState {
  size_t calls;
  size_t fail_at;
  size_t live;
} FailingAllocatorState;

static const char *const kModes[] = {"quiet", "normal", "verbose"};

static const TestDefinition kDefinitions[] = {
    {"ENABLE_BASE", CONFIT_VALUE_BOOL, 0, 0, 0, 0U, 0, 0, 0U, 0, 0, 0},
    {"COUNT", CONFIT_VALUE_INT, 0, 0, 4, 0U, 0, 0, 0U, 1, 1, 16},
    {"DEVICE_ID", CONFIT_VALUE_HEX, 0, 0, 0, UINT64_C(0x10), 0, 0, 0U,
     1, 0, 255},
    {"LABEL", CONFIT_VALUE_STRING, 0, 0, 0, 0U, "base", 0, 0U, 0, 0, 0},
    {"MODE", CONFIT_VALUE_ENUM, 0, 0, 0, 0U, "normal", kModes, 3U, 0, 0,
     0},
    {"FEATURE", CONFIT_VALUE_BOOL,
     "ENABLE_BASE && COUNT == 7 && MODE == \"verbose\"", 0, 0, 0U, 0, 0,
     0U, 0, 0, 0},
    {"UNAVAILABLE_COUNT", CONFIT_VALUE_INT, "ENABLE_BASE == false", 0, 3,
     0U, 0, 0, 0U, 1, 1, 8},
};

static const TestDefinition kReorderedDefinitions[] = {
    {"UNAVAILABLE_COUNT", CONFIT_VALUE_INT, "ENABLE_BASE == false", 0, 3,
     0U, 0, 0, 0U, 1, 1, 8},
    {"FEATURE", CONFIT_VALUE_BOOL,
     "ENABLE_BASE && COUNT == 7 && MODE == \"verbose\"", 0, 0, 0U, 0, 0,
     0U, 0, 0, 0},
    {"MODE", CONFIT_VALUE_ENUM, 0, 0, 0, 0U, "normal", kModes, 3U, 0, 0,
     0},
    {"LABEL", CONFIT_VALUE_STRING, 0, 0, 0, 0U, "base", 0, 0U, 0, 0, 0},
    {"DEVICE_ID", CONFIT_VALUE_HEX, 0, 0, 0, UINT64_C(0x10), 0, 0, 0U,
     1, 0, 255},
    {"COUNT", CONFIT_VALUE_INT, 0, 0, 4, 0U, 0, 0, 0U, 1, 1, 16},
    {"ENABLE_BASE", CONFIT_VALUE_BOOL, 0, 0, 0, 0U, 0, 0, 0U, 0, 0, 0},
};

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
                         catalog, "Resolver test", &diagnostic) == CONFIT_OK);
  fragment.path = "config/resolver.toml";
  fragment.parent_fragment = CONFIT_INDEX_NONE;
  fragment.source_ordinal = 0U;
  CONFIT_TEST_ASSERT(confit_catalog_add_fragment(catalog, &fragment, 0,
                                                 &diagnostic) == CONFIT_OK);
  for (index = 0U; index < count; ++index) {
    ConfitConfigSpec spec;
    ConfitValue default_value;
    ConfitValue minimum;
    ConfitValue maximum;
    confit_value_init(&default_value);
    confit_value_init(&minimum);
    confit_value_init(&maximum);
    CONFIT_TEST_ASSERT(set_definition_value(&definitions[index],
                                            &default_value,
                                            &diagnostic) == CONFIT_OK);
    memset(&spec, 0, sizeof(spec));
    spec.fragment = 0U;
    spec.menu = CONFIT_INDEX_NONE;
    spec.symbol = definitions[index].symbol;
    spec.kind = definitions[index].kind;
    spec.prompt = definitions[index].symbol;
    spec.help = "Resolve one generic typed configuration value.";
    spec.default_value = &default_value;
    spec.enum_values = definitions[index].enum_values;
    spec.enum_value_count = definitions[index].enum_value_count;
    spec.dependency_text = definitions[index].dependency;
    spec.declaration.path = "config/resolver.toml";
    spec.declaration.line = index + 1U;
    spec.declaration.column = 1U;
    if (definitions[index].has_range) {
      if (definitions[index].kind == CONFIT_VALUE_INT) {
        CONFIT_TEST_ASSERT(confit_value_set_int(
                               &minimum, definitions[index].range_minimum, 0,
                               &diagnostic) == CONFIT_OK);
        CONFIT_TEST_ASSERT(confit_value_set_int(
                               &maximum, definitions[index].range_maximum, 0,
                               &diagnostic) == CONFIT_OK);
      } else {
        CONFIT_TEST_ASSERT(confit_value_set_hex(
                               &minimum,
                               (uint64_t)definitions[index].range_minimum, 0,
                               &diagnostic) == CONFIT_OK);
        CONFIT_TEST_ASSERT(confit_value_set_hex(
                               &maximum,
                               (uint64_t)definitions[index].range_maximum, 0,
                               &diagnostic) == CONFIT_OK);
      }
      spec.range.present = 1;
      spec.range.minimum = &minimum;
      spec.range.maximum = &maximum;
    }
    CONFIT_TEST_ASSERT(confit_catalog_add_config(catalog, &spec, 0,
                                                 &diagnostic) == CONFIT_OK);
    confit_value_destroy(&maximum);
    confit_value_destroy(&minimum);
    confit_value_destroy(&default_value);
  }
  return catalog;
}

static void assignment_init_value(ConfitAssignment *assignment,
                                  const char *symbol, ConfitValueKind kind,
                                  int64_t number, const char *text) {
  ConfitDiagnostic diagnostic;
  ConfitValue value;
  confit_diagnostic_init(&diagnostic);
  confit_assignment_init(assignment);
  confit_value_init(&value);
  switch (kind) {
    case CONFIT_VALUE_BOOL:
      CONFIT_TEST_ASSERT(confit_value_set_bool(&value, number != 0, 0,
                                               &diagnostic) == CONFIT_OK);
      break;
    case CONFIT_VALUE_INT:
      CONFIT_TEST_ASSERT(confit_value_set_int(&value, number, 0,
                                              &diagnostic) == CONFIT_OK);
      break;
    case CONFIT_VALUE_HEX:
      CONFIT_TEST_ASSERT(confit_value_set_hex(&value, (uint64_t)number, 0,
                                              &diagnostic) == CONFIT_OK);
      break;
    case CONFIT_VALUE_STRING:
      CONFIT_TEST_ASSERT(confit_value_set_string(
                             &value, text, strlen(text), 0,
                             &diagnostic) == CONFIT_OK);
      break;
    case CONFIT_VALUE_ENUM:
      CONFIT_TEST_ASSERT(confit_value_set_enum(
                             &value, text, strlen(text), 0,
                             &diagnostic) == CONFIT_OK);
      break;
    default:
      CONFIT_TEST_ASSERT(0);
  }
  CONFIT_TEST_ASSERT(confit_assignment_set(assignment, symbol, &value, 0,
                                           &diagnostic) == CONFIT_OK);
  confit_value_destroy(&value);
}

static void destroy_assignments(ConfitAssignment *assignments, size_t count) {
  size_t index;
  for (index = count; index > 0U; --index)
    confit_assignment_destroy(&assignments[index - 1U]);
}

static char *canonical_resolution(const ConfitResolution *resolution,
                                  size_t *out_size) {
  ConfitDiagnostic diagnostic;
  ConfitStatus status;
  char *text;
  confit_diagnostic_init(&diagnostic);
  status = confit_resolution_format_canonical(resolution, 0, 0U, out_size,
                                               &diagnostic);
  CONFIT_TEST_ASSERT(status == CONFIT_ERR_USAGE && *out_size != 0U);
  text = (char *)malloc(*out_size + 1U);
  CONFIT_TEST_ASSERT(text != 0);
  CONFIT_TEST_ASSERT(confit_resolution_format_canonical(
                         resolution, text, *out_size + 1U, out_size,
                         &diagnostic) == CONFIT_OK);
  return text;
}

static void test_defaults_and_all_typed_overrides(void) {
  const size_t definition_count =
      sizeof(kDefinitions) / sizeof(kDefinitions[0]);
  ConfitCatalog *catalog = make_catalog(kDefinitions, definition_count);
  ConfitDependencyPlan *plan = 0;
  ConfitResolution *defaults = 0;
  ConfitResolution *configured = 0;
  ConfitDiagnostic diagnostic;
  ConfitAssignment assignments[7];
  const ConfitResolvedValue *value;
  const ConfitReasonNode *reason;
  size_t index;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_dependency_plan_create(catalog, 0, &plan,
                                                   &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_dependency_plan_matches_catalog(plan, catalog));
  CONFIT_TEST_ASSERT(confit_resolve(catalog, plan, 0, 0U, 0, &defaults,
                                   &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_resolution_catalog(defaults) == catalog);
  CONFIT_TEST_ASSERT(confit_resolution_value_count(defaults) ==
                     definition_count);
  for (index = 0U; index < definition_count; ++index) {
    CONFIT_TEST_ASSERT(confit_resolution_value_at(defaults, index, &value));
    if (index != 0U) {
      const ConfitResolvedValue *previous;
      CONFIT_TEST_ASSERT(confit_resolution_value_at(defaults, index - 1U,
                                                    &previous));
      CONFIT_TEST_ASSERT(strcmp(previous->symbol, value->symbol) < 0);
    }
    CONFIT_TEST_ASSERT(value->origin == CONFIT_ORIGIN_DEFAULT);
  }
  CONFIT_TEST_ASSERT(confit_resolution_find_value(defaults, "FEATURE",
                                                  &value));
  CONFIT_TEST_ASSERT(value->available == 0 && value->reason != CONFIT_INDEX_NONE);

  assignment_init_value(&assignments[0], "MODE", CONFIT_VALUE_ENUM, 0,
                        "verbose");
  assignment_init_value(&assignments[1], "FEATURE", CONFIT_VALUE_BOOL, 1, 0);
  assignment_init_value(&assignments[2], "DEVICE_ID", CONFIT_VALUE_HEX, 0x20,
                        0);
  assignment_init_value(&assignments[3], "ENABLE_BASE", CONFIT_VALUE_BOOL, 1,
                        0);
  assignment_init_value(&assignments[4], "LABEL", CONFIT_VALUE_STRING, 0,
                        "development");
  assignment_init_value(&assignments[5], "COUNT", CONFIT_VALUE_INT, 7, 0);
  assignment_init_value(&assignments[6], "UNAVAILABLE_COUNT",
                        CONFIT_VALUE_INT, 3, 0);
  CONFIT_TEST_ASSERT(confit_resolve(catalog, plan, assignments, 7U, 0,
                                   &configured, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_resolution_find_value(configured, "ENABLE_BASE",
                                                  &value));
  CONFIT_TEST_ASSERT(value->available == 1 &&
                     value->effective_value.data.boolean == 1 &&
                     value->origin == CONFIT_ORIGIN_USER);
  CONFIT_TEST_ASSERT(confit_resolution_find_value(configured, "COUNT",
                                                  &value));
  CONFIT_TEST_ASSERT(value->effective_value.data.integer == 7);
  CONFIT_TEST_ASSERT(confit_resolution_find_value(configured, "DEVICE_ID",
                                                  &value));
  CONFIT_TEST_ASSERT(value->effective_value.data.hexadecimal == UINT64_C(0x20));
  CONFIT_TEST_ASSERT(confit_resolution_find_value(configured, "LABEL", &value));
  CONFIT_TEST_ASSERT(strcmp(value->effective_value.data.text.data,
                            "development") == 0);
  CONFIT_TEST_ASSERT(confit_resolution_find_value(configured, "MODE", &value));
  CONFIT_TEST_ASSERT(strcmp(value->effective_value.data.text.data,
                            "verbose") == 0);
  CONFIT_TEST_ASSERT(confit_resolution_find_value(configured, "FEATURE",
                                                  &value));
  CONFIT_TEST_ASSERT(value->available == 1 &&
                     value->effective_value.data.boolean == 1);
  CONFIT_TEST_ASSERT(confit_resolution_find_value(
                         configured, "UNAVAILABLE_COUNT", &value));
  CONFIT_TEST_ASSERT(value->available == 0 &&
                     value->effective_value.data.integer == 3 &&
                     value->origin == CONFIT_ORIGIN_USER);
  CONFIT_TEST_ASSERT(confit_resolution_reason_at(configured, value->reason,
                                                 &reason));
  CONFIT_TEST_ASSERT(reason->kind == CONFIT_REASON_UNAVAILABLE &&
                     reason->result == 0 && reason->child_count == 1U);
  CONFIT_TEST_ASSERT(confit_resolution_reason_at(
                         configured, reason->children[0], &reason));
  CONFIT_TEST_ASSERT(reason->kind == CONFIT_REASON_COMPARISON &&
                     reason->result == 0 &&
                     strcmp(reason->subject_symbol, "ENABLE_BASE") == 0 &&
                     strcmp(reason->detail, "==") == 0);

  destroy_assignments(assignments, 7U);
  confit_resolution_destroy(configured);
  confit_resolution_destroy(defaults);
  confit_dependency_plan_destroy(plan);
  confit_catalog_destroy(catalog);
}

static void expect_resolution_failure(const ConfitCatalog *catalog,
                                      const ConfitDependencyPlan *plan,
                                      const ConfitAssignment *assignments,
                                      size_t count, const char *message) {
  ConfitDiagnostic diagnostic;
  ConfitResolution *resolution = (ConfitResolution *)(uintptr_t)1U;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_resolve(catalog, plan, assignments, count, 0,
                                   &resolution, &diagnostic) != CONFIT_OK);
  if (diagnostic.message == 0 || strcmp(diagnostic.message, message) != 0)
    fprintf(stderr, "expected '%s', got '%s'\n", message,
            diagnostic.message != 0 ? diagnostic.message : "(null)");
  CONFIT_TEST_ASSERT(resolution == 0 && diagnostic.message != 0 &&
                     strcmp(diagnostic.message, message) == 0);
}

static void test_fail_closed_assignments_and_plan_identity(void) {
  const size_t definition_count =
      sizeof(kDefinitions) / sizeof(kDefinitions[0]);
  ConfitCatalog *catalog = make_catalog(kDefinitions, definition_count);
  ConfitCatalog *other_catalog = make_catalog(kDefinitions, definition_count);
  ConfitDependencyPlan *plan = 0;
  ConfitDependencyPlan *other_plan = 0;
  ConfitAssignment assignments[2];
  ConfitDiagnostic diagnostic;
  ConfitResolution *resolution = (ConfitResolution *)(uintptr_t)1U;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_dependency_plan_create(catalog, 0, &plan,
                                                   &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_dependency_plan_create(other_catalog, 0,
                                                   &other_plan,
                                                   &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(!confit_dependency_plan_matches_catalog(other_plan,
                                                            catalog));
  CONFIT_TEST_ASSERT(confit_resolve(catalog, other_plan, 0, 0U, 0,
                                   &resolution, &diagnostic) ==
                     CONFIT_ERR_USAGE);
  CONFIT_TEST_ASSERT(resolution == 0 &&
                     strcmp(diagnostic.message,
                            "dependency plan does not belong to the catalog") ==
                         0);

  assignment_init_value(&assignments[0], "UNKNOWN", CONFIT_VALUE_BOOL, 1, 0);
  expect_resolution_failure(catalog, plan, assignments, 1U,
                            "user assignment names an unknown symbol");
  confit_assignment_destroy(&assignments[0]);

  assignment_init_value(&assignments[0], "COUNT", CONFIT_VALUE_INT, 7, 0);
  assignment_init_value(&assignments[1], "COUNT", CONFIT_VALUE_INT, 8, 0);
  expect_resolution_failure(catalog, plan, assignments, 2U,
                            "user assignment is duplicated");
  destroy_assignments(assignments, 2U);

  assignment_init_value(&assignments[0], "COUNT", CONFIT_VALUE_BOOL, 1, 0);
  expect_resolution_failure(catalog, plan, assignments, 1U,
                            "user assignment has the wrong value type");
  confit_assignment_destroy(&assignments[0]);

  assignment_init_value(&assignments[0], "COUNT", CONFIT_VALUE_INT, 99, 0);
  expect_resolution_failure(catalog, plan, assignments, 1U,
                            "user assignment is outside the declared range");
  confit_assignment_destroy(&assignments[0]);

  assignment_init_value(&assignments[0], "MODE", CONFIT_VALUE_ENUM, 0,
                        "broken");
  expect_resolution_failure(catalog, plan, assignments, 1U,
                            "user assignment is outside the enum domain");
  confit_assignment_destroy(&assignments[0]);

  assignment_init_value(&assignments[0], "ENABLE_BASE", CONFIT_VALUE_BOOL, 1,
                        0);
  assignment_init_value(&assignments[1], "UNAVAILABLE_COUNT",
                        CONFIT_VALUE_INT, 4, 0);
  expect_resolution_failure(
      catalog, plan, assignments, 2U,
      "unavailable option has a non-default user value");
  destroy_assignments(assignments, 2U);

  confit_dependency_plan_destroy(other_plan);
  confit_dependency_plan_destroy(plan);
  confit_catalog_destroy(other_catalog);
  confit_catalog_destroy(catalog);
}

static void test_order_independent_canonical_identity(void) {
  const size_t count = sizeof(kDefinitions) / sizeof(kDefinitions[0]);
  ConfitCatalog *first_catalog = make_catalog(kDefinitions, count);
  ConfitCatalog *second_catalog = make_catalog(kReorderedDefinitions, count);
  ConfitDependencyPlan *first_plan = 0;
  ConfitDependencyPlan *second_plan = 0;
  ConfitResolution *first = 0;
  ConfitResolution *second = 0;
  ConfitDiagnostic diagnostic;
  char *first_text;
  char *second_text;
  char first_digest[65];
  char second_digest[65];
  char guard[4] = {'s', 'a', 'f', 'e'};
  size_t first_size;
  size_t second_size;
  const ConfitResolvedValue *first_value;
  const ConfitResolvedValue *second_value;
  const ConfitReasonNode *first_reason;
  const ConfitReasonNode *second_reason;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_dependency_plan_create(
                         first_catalog, 0, &first_plan,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_dependency_plan_create(
                         second_catalog, 0, &second_plan,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_resolve(first_catalog, first_plan, 0, 0U, 0,
                                   &first, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_resolve(second_catalog, second_plan, 0, 0U, 0,
                                   &second, &diagnostic) == CONFIT_OK);
  first_size = 0U;
  CONFIT_TEST_ASSERT(confit_resolution_format_canonical(
                         first, guard, sizeof(guard), &first_size,
                         &diagnostic) == CONFIT_ERR_USAGE);
  CONFIT_TEST_ASSERT(memcmp(guard, "safe", sizeof(guard)) == 0 &&
                     first_size > sizeof(guard));
  first_text = canonical_resolution(first, &first_size);
  second_text = canonical_resolution(second, &second_size);
  confit_sha256_bytes(first_text, first_size, first_digest);
  confit_sha256_bytes(second_text, second_size, second_digest);
  CONFIT_TEST_ASSERT(first_size == second_size &&
                     memcmp(first_text, second_text, first_size) == 0 &&
                     strcmp(first_digest, second_digest) == 0);
  CONFIT_TEST_ASSERT(confit_resolution_find_value(first, "FEATURE",
                                                  &first_value));
  CONFIT_TEST_ASSERT(confit_resolution_find_value(second, "FEATURE",
                                                  &second_value));
  CONFIT_TEST_ASSERT(confit_resolution_reason_at(first, first_value->reason,
                                                 &first_reason));
  CONFIT_TEST_ASSERT(confit_resolution_reason_at(second, second_value->reason,
                                                 &second_reason));
  CONFIT_TEST_ASSERT(first_reason->kind == second_reason->kind &&
                     first_reason->result == second_reason->result &&
                     strcmp(first_reason->subject_symbol,
                            second_reason->subject_symbol) == 0);
  free(second_text);
  free(first_text);
  confit_resolution_destroy(second);
  confit_resolution_destroy(first);
  confit_dependency_plan_destroy(second_plan);
  confit_dependency_plan_destroy(first_plan);
  confit_catalog_destroy(second_catalog);
  confit_catalog_destroy(first_catalog);
}

static void test_allocation_failure_is_transactional(void) {
  static const TestDefinition definitions[] = {
      {"BASE", CONFIT_VALUE_BOOL, 0, 1, 0, 0U, 0, 0, 0U, 0, 0, 0},
      {"FEATURE", CONFIT_VALUE_BOOL, "BASE", 0, 0, 0U, 0, 0, 0U, 0, 0,
       0},
  };
  ConfitCatalog *catalog = make_catalog(definitions, 2U);
  ConfitDependencyPlan *plan = 0;
  ConfitDiagnostic diagnostic;
  size_t fail_at;
  int observed_success = 0;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_dependency_plan_create(catalog, 0, &plan,
                                                   &diagnostic) == CONFIT_OK);
  for (fail_at = 0U; fail_at < 256U; ++fail_at) {
    FailingAllocatorState state;
    ConfitAllocator allocator;
    ConfitResolution *resolution = (ConfitResolution *)(uintptr_t)1U;
    ConfitStatus status;
    memset(&state, 0, sizeof(state));
    state.fail_at = fail_at;
    allocator.context = &state;
    allocator.allocate = failing_allocate;
    allocator.deallocate = failing_deallocate;
    confit_diagnostic_init(&diagnostic);
    status = confit_resolve(catalog, plan, 0, 0U, &allocator, &resolution,
                            &diagnostic);
    if (status == CONFIT_OK) {
      observed_success = 1;
      confit_resolution_destroy(resolution);
      CONFIT_TEST_ASSERT(state.live == 0U);
      break;
    }
    CONFIT_TEST_ASSERT(resolution == 0 && state.live == 0U);
  }
  CONFIT_TEST_ASSERT(observed_success);
  confit_dependency_plan_destroy(plan);
  confit_catalog_destroy(catalog);
}

static void test_maximum_symbol_graph(void) {
  ConfitCatalog *catalog = 0;
  ConfitDependencyPlan *plan = 0;
  ConfitResolution *resolution = 0;
  ConfitDiagnostic diagnostic;
  ConfitSourceFragmentSpec fragment;
  ConfitConfigSpec spec;
  ConfitValue default_value;
  const ConfitResolvedValue *first;
  const ConfitResolvedValue *last;
  size_t index;
  char dependency[16];
  char symbol[16];
  confit_diagnostic_init(&diagnostic);
  confit_value_init(&default_value);
  CONFIT_TEST_ASSERT(confit_value_set_bool(&default_value, 0, 0,
                                           &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_catalog_create(0, &catalog, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_catalog_set_mainmenu(
                         catalog, "Maximum resolver graph",
                         &diagnostic) == CONFIT_OK);
  fragment.path = "config/maximum.toml";
  fragment.parent_fragment = CONFIT_INDEX_NONE;
  fragment.source_ordinal = 0U;
  CONFIT_TEST_ASSERT(confit_catalog_add_fragment(catalog, &fragment, 0,
                                                 &diagnostic) == CONFIT_OK);
  memset(&spec, 0, sizeof(spec));
  spec.fragment = 0U;
  spec.menu = CONFIT_INDEX_NONE;
  spec.kind = CONFIT_VALUE_BOOL;
  spec.help = "Exercise the exact public configuration-symbol ceiling.";
  spec.default_value = &default_value;
  spec.declaration.path = "config/maximum.toml";
  spec.declaration.column = 1U;
  for (index = 0U; index < CONFIT_LIMIT_CONFIG_SYMBOLS; ++index) {
    CONFIT_TEST_ASSERT(snprintf(symbol, sizeof(symbol), "C%05zu", index) > 0);
    if (index == 0U) {
      spec.dependency_text = 0;
    } else {
      CONFIT_TEST_ASSERT(snprintf(dependency, sizeof(dependency), "C%05zu",
                                  index - 1U) > 0);
      spec.dependency_text = dependency;
    }
    spec.symbol = symbol;
    spec.prompt = symbol;
    spec.declaration.line = index + 1U;
    CONFIT_TEST_ASSERT(confit_catalog_add_config(catalog, &spec, 0,
                                                 &diagnostic) == CONFIT_OK);
  }
  CONFIT_TEST_ASSERT(confit_dependency_plan_create(catalog, 0, &plan,
                                                   &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_dependency_plan_edge_count(plan) ==
                     CONFIT_LIMIT_CONFIG_SYMBOLS - 1U);
  CONFIT_TEST_ASSERT(confit_resolve(catalog, plan, 0, 0U, 0, &resolution,
                                   &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_resolution_value_count(resolution) ==
                     CONFIT_LIMIT_CONFIG_SYMBOLS);
  CONFIT_TEST_ASSERT(confit_resolution_value_at(resolution, 0U, &first));
  CONFIT_TEST_ASSERT(confit_resolution_value_at(
                         resolution, CONFIT_LIMIT_CONFIG_SYMBOLS - 1U,
                         &last));
  CONFIT_TEST_ASSERT(strcmp(first->symbol, "C00000") == 0 &&
                     strcmp(last->symbol, "C16383") == 0);
  confit_resolution_destroy(resolution);
  confit_dependency_plan_destroy(plan);
  confit_catalog_destroy(catalog);
  confit_value_destroy(&default_value);
}

static void add_choice_member(ConfitCatalog *catalog, const char *symbol,
                              int selected) {
  ConfitConfigSpec spec;
  ConfitDiagnostic diagnostic;
  ConfitValue value;
  confit_diagnostic_init(&diagnostic);
  confit_value_init(&value);
  memset(&spec, 0, sizeof(spec));
  CONFIT_TEST_ASSERT(confit_value_set_bool(&value, selected, 0,
                                           &diagnostic) == CONFIT_OK);
  spec.fragment = 0U;
  spec.menu = CONFIT_INDEX_NONE;
  spec.symbol = symbol;
  spec.kind = CONFIT_VALUE_BOOL;
  spec.prompt = symbol;
  spec.help = "Select exactly one resolver choice member.";
  spec.default_value = &value;
  spec.choice_group = "resolver-board";
  spec.declaration.path = "config/choice.toml";
  spec.declaration.line = selected ? 1U : 2U;
  spec.declaration.column = 1U;
  CONFIT_TEST_ASSERT(confit_catalog_add_config(catalog, &spec, 0,
                                               &diagnostic) == CONFIT_OK);
  confit_value_destroy(&value);
}

static void test_choice_exact_one_resolution(void) {
  ConfitCatalog *catalog = 0;
  ConfitDependencyPlan *plan = 0;
  ConfitResolution *resolution = 0;
  ConfitDiagnostic diagnostic;
  ConfitSourceFragmentSpec fragment = {
      .path = "config/choice.toml",
      .parent_fragment = CONFIT_INDEX_NONE,
      .source_ordinal = 0U,
  };
  ConfitAssignment assignments[2];
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_catalog_create(0, &catalog, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_catalog_set_mainmenu(
                         catalog, "Choice resolver", &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_catalog_add_fragment(catalog, &fragment, 0,
                                                 &diagnostic) == CONFIT_OK);
  add_choice_member(catalog, "BOARD_A", 1);
  add_choice_member(catalog, "BOARD_B", 0);
  CONFIT_TEST_ASSERT(confit_dependency_plan_create(catalog, 0, &plan,
                                                   &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_resolve(catalog, plan, 0, 0U, 0, &resolution,
                                   &diagnostic) == CONFIT_OK);
  confit_resolution_destroy(resolution);
  resolution = 0;

  assignment_init_value(&assignments[0], "BOARD_A", CONFIT_VALUE_BOOL, 1, 0);
  assignment_init_value(&assignments[1], "BOARD_B", CONFIT_VALUE_BOOL, 1, 0);
  CONFIT_TEST_ASSERT(confit_resolve(catalog, plan, assignments, 2U, 0,
                                   &resolution, &diagnostic) ==
                     CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT_CONTAINS(diagnostic.message,
                              "choice group must resolve to exactly one true value");
  destroy_assignments(assignments, 2U);
  confit_diagnostic_clear(&diagnostic);

  assignment_init_value(&assignments[0], "BOARD_A", CONFIT_VALUE_BOOL, 0, 0);
  CONFIT_TEST_ASSERT(confit_resolve(catalog, plan, assignments, 1U, 0,
                                   &resolution, &diagnostic) ==
                     CONFIT_ERR_VALIDATION);
  confit_assignment_destroy(&assignments[0]);
  confit_diagnostic_clear(&diagnostic);

  assignment_init_value(&assignments[0], "BOARD_A", CONFIT_VALUE_BOOL, 0, 0);
  assignment_init_value(&assignments[1], "BOARD_B", CONFIT_VALUE_BOOL, 1, 0);
  CONFIT_TEST_ASSERT(confit_resolve(catalog, plan, assignments, 2U, 0,
                                   &resolution, &diagnostic) == CONFIT_OK);
  destroy_assignments(assignments, 2U);
  confit_resolution_destroy(resolution);
  confit_dependency_plan_destroy(plan);
  confit_catalog_destroy(catalog);
}

int main(void) {
  test_defaults_and_all_typed_overrides();
  test_fail_closed_assignments_and_plan_identity();
  test_order_independent_canonical_identity();
  test_allocation_failure_is_transactional();
  test_maximum_symbol_graph();
  test_choice_exact_one_resolution();
  return 0;
}
