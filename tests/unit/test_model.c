#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "confit/limits.h"
#include "confit/model.h"
#include "test_assert.h"

typedef struct FailingAllocator {
  size_t calls;
  size_t fail_at;
  size_t live;
} FailingAllocator;

static void *failing_allocate(void *context, size_t size) {
  FailingAllocator *state = (FailingAllocator *)context;
  void *pointer;
  const size_t call = state->calls++;
  if (call == state->fail_at) {
    return 0;
  }
  pointer = malloc(size);
  if (pointer != 0) {
    state->live += 1U;
  }
  return pointer;
}

static void failing_deallocate(void *context, void *pointer) {
  FailingAllocator *state = (FailingAllocator *)context;
  if (pointer != 0) {
    CONFIT_TEST_ASSERT(state->live != 0U);
    state->live -= 1U;
    free(pointer);
  }
}

static ConfitAllocator make_failing_allocator(FailingAllocator *state) {
  ConfitAllocator allocator;
  allocator.context = state;
  allocator.allocate = failing_allocate;
  allocator.deallocate = failing_deallocate;
  return allocator;
}

static void expect_format(const ConfitValue *value, const char *expected) {
  ConfitDiagnostic diagnostic;
  char buffer[256];
  size_t output_size = 0U;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_value_format_canonical(
                         value, buffer, sizeof(buffer), &output_size,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(output_size == strlen(expected));
  CONFIT_TEST_ASSERT(strcmp(buffer, expected) == 0);
}

static void test_limits(void) {
  CONFIT_TEST_ASSERT(CONFIT_LIMIT_TOML_FILE_BYTES == 1024U * 1024U);
  CONFIT_TEST_ASSERT(CONFIT_LIMIT_TOTAL_INPUT_BYTES == 64U * 1024U * 1024U);
  CONFIT_TEST_ASSERT(CONFIT_LIMIT_SOURCE_FRAGMENTS == 4096U);
  CONFIT_TEST_ASSERT(CONFIT_LIMIT_SOURCE_EDGES == 16384U);
  CONFIT_TEST_ASSERT(CONFIT_LIMIT_INCLUDE_DEPTH == 64U);
  CONFIT_TEST_ASSERT(CONFIT_LIMIT_MENUS == 4096U);
  CONFIT_TEST_ASSERT(CONFIT_LIMIT_VISIBLE_MENU_DEPTH == 3U);
  CONFIT_TEST_ASSERT(CONFIT_LIMIT_CONFIG_SYMBOLS == 16384U);
  CONFIT_TEST_ASSERT(CONFIT_LIMIT_SOURCE_PATH_BYTES == 1024U);
  CONFIT_TEST_ASSERT(CONFIT_LIMIT_PROMPT_BYTES == 256U);
  CONFIT_TEST_ASSERT(CONFIT_LIMIT_HELP_BYTES == 8192U);
  CONFIT_TEST_ASSERT(CONFIT_LIMIT_STRING_BYTES == 4096U);
  CONFIT_TEST_ASSERT(CONFIT_LIMIT_ENUM_VALUES == 256U);
  CONFIT_TEST_ASSERT(CONFIT_LIMIT_ENUM_ATOM_BYTES == 128U);
  CONFIT_TEST_ASSERT(CONFIT_LIMIT_DEPENDENCY_TEXT_BYTES == 4096U);
  CONFIT_TEST_ASSERT(CONFIT_LIMIT_DEPENDENCY_AST_NODES == 512U);
  CONFIT_TEST_ASSERT(CONFIT_LIMIT_DEPENDENCY_NESTING == 32U);
  CONFIT_TEST_ASSERT(CONFIT_LIMIT_DIAGNOSTICS == 1024U);
  CONFIT_TEST_ASSERT(CONFIT_LIMIT_RENDER_COLUMNS == 512U);
  CONFIT_TEST_ASSERT(CONFIT_LIMIT_RENDER_ROWS == 256U);
}

static void test_values(void) {
  static const char invalid_utf8[] = {(char)0xFF};
  static const char escaped_text[] = "line one\nline two";
  ConfitDiagnostic diagnostic;
  ConfitValue boolean;
  ConfitValue copy;
  ConfitValue enumeration;
  ConfitValue hexadecimal;
  ConfitValue integer;
  ConfitValue string;
  char *maximum;
  size_t output_size;
  char guard[4] = {'s', 'a', 'f', 'e'};

  confit_diagnostic_init(&diagnostic);
  confit_value_init(&boolean);
  confit_value_init(&copy);
  confit_value_init(&enumeration);
  confit_value_init(&hexadecimal);
  confit_value_init(&integer);
  confit_value_init(&string);

  CONFIT_TEST_ASSERT(confit_value_set_bool(&boolean, 12, 0, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_value_set_int(&integer, INT64_MIN, 0,
                                          &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_value_set_hex(&hexadecimal, (uint64_t)INT64_MAX, 0,
                                          &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_value_set_string(
                         &string, escaped_text, sizeof(escaped_text) - 1U, 0,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_value_set_enum(&enumeration, "verbose", 7U, 0,
                                           &diagnostic) == CONFIT_OK);

  expect_format(&boolean, "bool:true");
  expect_format(&integer, "int:-9223372036854775808");
  expect_format(&hexadecimal, "hex:0x7fffffffffffffff");
  expect_format(&string, "string:17:line one\nline two");
  expect_format(&enumeration, "enum:7:verbose");

  CONFIT_TEST_ASSERT(confit_value_copy(&copy, &boolean, 0, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_value_equal(&copy, &boolean));
  CONFIT_TEST_ASSERT(confit_value_copy(&copy, &integer, 0, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_value_equal(&copy, &integer));
  CONFIT_TEST_ASSERT(confit_value_copy(&copy, &hexadecimal, 0, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_value_equal(&copy, &hexadecimal));
  CONFIT_TEST_ASSERT(confit_value_copy(&copy, &string, 0, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_value_equal(&copy, &string));
  CONFIT_TEST_ASSERT(!confit_value_equal(&copy, &enumeration));
  CONFIT_TEST_ASSERT(confit_value_copy(&copy, &enumeration, 0, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_value_equal(&copy, &enumeration));
  CONFIT_TEST_ASSERT(confit_value_copy(&copy, &string, 0, &diagnostic) ==
                     CONFIT_OK);

  output_size = 0U;
  CONFIT_TEST_ASSERT(confit_value_format_canonical(
                         &boolean, guard, sizeof(guard), &output_size,
                         &diagnostic) == CONFIT_ERR_USAGE);
  CONFIT_TEST_ASSERT(memcmp(guard, "safe", sizeof(guard)) == 0);
  CONFIT_TEST_ASSERT(output_size == strlen("bool:true"));

  CONFIT_TEST_ASSERT(confit_value_set_hex(&hexadecimal,
                                          (uint64_t)INT64_MAX + UINT64_C(1), 0,
                                          &diagnostic) ==
                     CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(hexadecimal.data.hexadecimal == (uint64_t)INT64_MAX);
  CONFIT_TEST_ASSERT(confit_value_set_string(&copy, invalid_utf8,
                                             sizeof(invalid_utf8), 0,
                                             &diagnostic) ==
                     CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(confit_value_equal(&copy, &string));
  CONFIT_TEST_ASSERT(confit_value_set_string(&copy, "bad\033", 4U, 0,
                                             &diagnostic) ==
                     CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(confit_value_equal(&copy, &string));

  maximum = (char *)malloc(CONFIT_LIMIT_STRING_BYTES + 2U);
  CONFIT_TEST_ASSERT(maximum != 0);
  memset(maximum, 'x', CONFIT_LIMIT_STRING_BYTES + 1U);
  maximum[CONFIT_LIMIT_STRING_BYTES + 1U] = '\0';
  CONFIT_TEST_ASSERT(confit_value_set_string(
                         &copy, maximum, CONFIT_LIMIT_STRING_BYTES, 0,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_value_set_string(
                         &copy, maximum, CONFIT_LIMIT_STRING_BYTES + 1U, 0,
                         &diagnostic) == CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(copy.data.text.size == CONFIT_LIMIT_STRING_BYTES);
  free(maximum);

  confit_value_destroy(&boolean);
  confit_value_destroy(&copy);
  confit_value_destroy(&enumeration);
  confit_value_destroy(&hexadecimal);
  confit_value_destroy(&integer);
  confit_value_destroy(&string);
}

static void test_symbol_and_enum_validation(void) {
  const char *duplicate[] = {"quiet", "quiet"};
  const char *valid[] = {"quiet", "normal", "verbose"};
  const char *maximum_values[256];
  const char *too_many[257];
  char maximum_storage[256][5];
  char overlong_atom[130];
  char maximum_symbol[129];
  char overlong_symbol[130];
  size_t index;
  ConfitDiagnostic diagnostic;

  confit_diagnostic_init(&diagnostic);
  maximum_symbol[0] = 'A';
  for (index = 1U; index < sizeof(maximum_symbol) - 1U; ++index)
    maximum_symbol[index] = '_';
  maximum_symbol[sizeof(maximum_symbol) - 1U] = '\0';
  memcpy(overlong_symbol, maximum_symbol, sizeof(maximum_symbol));
  overlong_symbol[sizeof(maximum_symbol) - 1U] = '_';
  overlong_symbol[sizeof(overlong_symbol) - 1U] = '\0';
  memset(overlong_atom, 'a', sizeof(overlong_atom) - 1U);
  overlong_atom[sizeof(overlong_atom) - 1U] = '\0';
  for (index = 0U; index < 256U; ++index) {
    maximum_storage[index][0] = 'v';
    maximum_storage[index][1] = (char)('0' + (index / 100U));
    maximum_storage[index][2] = (char)('0' + ((index / 10U) % 10U));
    maximum_storage[index][3] = (char)('0' + (index % 10U));
    maximum_storage[index][4] = '\0';
    maximum_values[index] = maximum_storage[index];
    too_many[index] = maximum_storage[index];
  }
  too_many[256] = "v256";

  CONFIT_TEST_ASSERT(confit_symbol_is_valid("ENABLE_METRICS"));
  CONFIT_TEST_ASSERT(confit_symbol_is_valid(maximum_symbol));
  CONFIT_TEST_ASSERT(!confit_symbol_is_valid("enable_metrics"));
  CONFIT_TEST_ASSERT(!confit_symbol_is_valid("9INVALID"));
  CONFIT_TEST_ASSERT(!confit_symbol_is_valid(overlong_symbol));
  CONFIT_TEST_ASSERT(confit_enum_domain_validate(valid, 3U, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_enum_domain_validate(maximum_values, 256U,
                                                 &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_enum_domain_validate(duplicate, 2U, &diagnostic) ==
                     CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(confit_enum_domain_validate(too_many, 257U, &diagnostic) ==
                     CONFIT_ERR_VALIDATION);
  too_many[0] = overlong_atom;
  CONFIT_TEST_ASSERT(confit_enum_domain_validate(too_many, 1U, &diagnostic) ==
                     CONFIT_ERR_VALIDATION);
}

static void test_value_allocation_failure(void) {
  ConfitAllocator allocator;
  ConfitDiagnostic diagnostic;
  ConfitValue destination;
  ConfitValue source;
  FailingAllocator state;
  const char *text;
  size_t text_size;
  size_t baseline_live;

  memset(&state, 0, sizeof(state));
  state.fail_at = SIZE_MAX;
  allocator = make_failing_allocator(&state);
  confit_diagnostic_init(&diagnostic);
  confit_value_init(&destination);
  confit_value_init(&source);
  CONFIT_TEST_ASSERT(confit_value_set_string(&destination, "old", 3U,
                                             &allocator, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_value_set_string(&source, "replacement", 11U,
                                             &allocator, &diagnostic) ==
                     CONFIT_OK);
  baseline_live = state.live;
  state.fail_at = state.calls;
  CONFIT_TEST_ASSERT(confit_value_copy(&destination, &source, &allocator,
                                       &diagnostic) == CONFIT_ERR_INTERNAL);
  CONFIT_TEST_ASSERT(state.live == baseline_live);
  CONFIT_TEST_ASSERT(confit_value_text(&destination, &text, &text_size));
  CONFIT_TEST_ASSERT(text_size == 3U && memcmp(text, "old", 3U) == 0);
  state.fail_at = SIZE_MAX;
  confit_value_destroy(&destination);
  confit_value_destroy(&source);
  CONFIT_TEST_ASSERT(state.live == 0U);
}

static void init_int_config(ConfitConfigSpec *spec, size_t fragment,
                            size_t menu, const ConfitValue *default_value,
                            const ConfitValue *minimum,
                            const ConfitValue *maximum) {
  memset(spec, 0, sizeof(*spec));
  spec->fragment = fragment;
  spec->menu = menu;
  spec->symbol = "WORKER_COUNT";
  spec->kind = CONFIT_VALUE_INT;
  spec->prompt = "Worker count";
  spec->help = "Set the maximum number of worker contexts.";
  spec->default_value = default_value;
  spec->range.present = 1;
  spec->range.minimum = minimum;
  spec->range.maximum = maximum;
  spec->dependency_text = "ENABLE_RUNTIME";
  spec->declaration.path = "config/runtime.toml";
  spec->declaration.line = 7U;
  spec->declaration.column = 1U;
}

static void test_catalog(void) {
  const char *levels[] = {"quiet", "normal", "verbose"};
  ConfitCatalog *catalog = 0;
  ConfitConfigSpec config;
  ConfitConfigView config_view;
  ConfitDiagnostic diagnostic;
  ConfitMenuSpec menu;
  ConfitMenuView menu_view;
  ConfitSourceFragmentSpec fragment;
  ConfitSourceFragmentView fragment_view;
  ConfitValue default_int;
  ConfitValue default_level;
  ConfitValue maximum;
  ConfitValue minimum;
  char *overlong_help;
  char *overlong_prompt;
  size_t child_fragment;
  size_t child_menu;
  size_t index;
  size_t root_fragment;
  size_t root_menu;
  size_t third_fragment;
  size_t third_menu;
  size_t fourth_fragment;

  confit_diagnostic_init(&diagnostic);
  confit_value_init(&default_int);
  confit_value_init(&default_level);
  confit_value_init(&maximum);
  confit_value_init(&minimum);
  CONFIT_TEST_ASSERT(confit_value_set_int(&default_int, 4, 0, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_value_set_int(&minimum, 1, 0, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_value_set_int(&maximum, 64, 0, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_value_set_enum(&default_level, "normal", 6U, 0,
                                           &diagnostic) == CONFIT_OK);

  CONFIT_TEST_ASSERT(confit_catalog_create(0, &catalog, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_catalog_set_mainmenu(
                         catalog, "Example configuration", &diagnostic) ==
                     CONFIT_OK);

  fragment.path = "Confit.toml";
  fragment.parent_fragment = CONFIT_INDEX_NONE;
  fragment.source_ordinal = 0U;
  CONFIT_TEST_ASSERT(confit_catalog_add_fragment(
                         catalog, &fragment, &root_fragment, &diagnostic) ==
                     CONFIT_OK);
  fragment.path = "config/runtime.toml";
  fragment.parent_fragment = root_fragment;
  fragment.source_ordinal = 0U;
  CONFIT_TEST_ASSERT(confit_catalog_add_fragment(
                         catalog, &fragment, &child_fragment, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_catalog_add_fragment(catalog, &fragment, 0,
                                                 &diagnostic) ==
                     CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(confit_catalog_fragment_count(catalog) == 2U);

  memset(&menu, 0, sizeof(menu));
  menu.fragment = root_fragment;
  menu.parent_menu = CONFIT_INDEX_NONE;
  menu.prompt = "Root options";
  menu.help = "Configure root-level behavior.";
  menu.declaration.path = "Confit.toml";
  CONFIT_TEST_ASSERT(confit_catalog_add_menu(catalog, &menu, &root_menu,
                                             &diagnostic) == CONFIT_OK);
  menu.fragment = child_fragment;
  menu.parent_menu = root_menu;
  menu.prompt = "Runtime";
  menu.help = "Configure runtime behavior.";
  menu.declaration.path = "config/runtime.toml";
  CONFIT_TEST_ASSERT(confit_catalog_add_menu(catalog, &menu, &child_menu,
                                             &diagnostic) == CONFIT_OK);

  init_int_config(&config, child_fragment, child_menu, &default_int, &minimum,
                  &maximum);
  CONFIT_TEST_ASSERT(confit_catalog_add_config(catalog, &config, &index,
                                               &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(index == 0U);
  CONFIT_TEST_ASSERT(confit_catalog_add_config(catalog, &config, 0,
                                               &diagnostic) ==
                     CONFIT_ERR_VALIDATION);

  memset(&config, 0, sizeof(config));
  config.fragment = child_fragment;
  config.menu = child_menu;
  config.symbol = "LOG_LEVEL";
  config.kind = CONFIT_VALUE_ENUM;
  config.prompt = "Log level";
  config.help = "Select the amount of log output.";
  config.default_value = &default_level;
  config.enum_values = levels;
  config.enum_value_count = 3U;
  config.declaration.path = "config/runtime.toml";
  CONFIT_TEST_ASSERT(confit_catalog_add_config(catalog, &config, 0,
                                               &diagnostic) == CONFIT_OK);

  CONFIT_TEST_ASSERT(strcmp(confit_catalog_mainmenu(catalog),
                            "Example configuration") == 0);
  CONFIT_TEST_ASSERT(confit_catalog_menu_count(catalog) == 2U);
  CONFIT_TEST_ASSERT(confit_catalog_config_count(catalog) == 2U);
  CONFIT_TEST_ASSERT(confit_catalog_fragment_at(catalog, child_fragment,
                                                &fragment_view));
  CONFIT_TEST_ASSERT(fragment_view.parent_fragment == root_fragment);
  CONFIT_TEST_ASSERT(confit_catalog_menu_at(catalog, child_menu, &menu_view));
  CONFIT_TEST_ASSERT(strcmp(menu_view.prompt, "Runtime") == 0);
  CONFIT_TEST_ASSERT(confit_catalog_find_config(catalog, "WORKER_COUNT",
                                                &config_view) != 0);
  CONFIT_TEST_ASSERT(config_view.has_range);
  CONFIT_TEST_ASSERT(config_view.range_minimum->data.integer == 1);
  CONFIT_TEST_ASSERT(config_view.range_maximum->data.integer == 64);
  CONFIT_TEST_ASSERT(strcmp(config_view.dependency_text,
                            "ENABLE_RUNTIME") == 0);
  CONFIT_TEST_ASSERT(confit_catalog_find_config(catalog, "LOG_LEVEL",
                                                &config_view) != 0);
  CONFIT_TEST_ASSERT(config_view.enum_value_count == 3U);
  CONFIT_TEST_ASSERT(strcmp(config_view.enum_values[2], "verbose") == 0);

  fragment.path = "config/third.toml";
  fragment.parent_fragment = child_fragment;
  fragment.source_ordinal = 0U;
  CONFIT_TEST_ASSERT(confit_catalog_add_fragment(
                         catalog, &fragment, &third_fragment, &diagnostic) ==
                     CONFIT_OK);
  menu.fragment = third_fragment;
  menu.parent_menu = child_menu;
  menu.prompt = "Third";
  menu.help = "Third-level options.";
  menu.declaration.path = "config/third.toml";
  CONFIT_TEST_ASSERT(confit_catalog_add_menu(catalog, &menu, &third_menu,
                                             &diagnostic) == CONFIT_OK);
  fragment.path = "config/fourth.toml";
  fragment.parent_fragment = third_fragment;
  CONFIT_TEST_ASSERT(confit_catalog_add_fragment(
                         catalog, &fragment, &fourth_fragment, &diagnostic) ==
                     CONFIT_OK);
  overlong_prompt = (char *)malloc(CONFIT_LIMIT_PROMPT_BYTES + 2U);
  overlong_help = (char *)malloc(CONFIT_LIMIT_HELP_BYTES + 2U);
  CONFIT_TEST_ASSERT(overlong_prompt != 0 && overlong_help != 0);
  memset(overlong_prompt, 'p', CONFIT_LIMIT_PROMPT_BYTES + 1U);
  overlong_prompt[CONFIT_LIMIT_PROMPT_BYTES + 1U] = '\0';
  memset(overlong_help, 'h', CONFIT_LIMIT_HELP_BYTES + 1U);
  overlong_help[CONFIT_LIMIT_HELP_BYTES + 1U] = '\0';
  menu.fragment = fourth_fragment;
  menu.parent_menu = CONFIT_INDEX_NONE;
  menu.prompt = overlong_prompt;
  menu.help = "Valid help.";
  menu.declaration.path = "config/fourth.toml";
  CONFIT_TEST_ASSERT(confit_catalog_add_menu(catalog, &menu, 0, &diagnostic) ==
                     CONFIT_ERR_VALIDATION);
  menu.prompt = "Fourth";
  menu.help = overlong_help;
  CONFIT_TEST_ASSERT(confit_catalog_add_menu(catalog, &menu, 0, &diagnostic) ==
                     CONFIT_ERR_VALIDATION);
  menu.fragment = fourth_fragment;
  menu.parent_menu = third_menu;
  menu.prompt = "Fourth";
  menu.help = "This menu exceeds the visible depth.";
  menu.declaration.path = "config/fourth.toml";
  CONFIT_TEST_ASSERT(confit_catalog_add_menu(catalog, &menu, 0, &diagnostic) ==
                     CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(confit_catalog_menu_count(catalog) == 3U);
  free(overlong_prompt);
  free(overlong_help);

  config.range.minimum = &maximum;
  config.range.maximum = &minimum;
  config.range.present = 1;
  config.kind = CONFIT_VALUE_INT;
  config.symbol = "INVALID_RANGE";
  config.default_value = &default_int;
  config.enum_values = 0;
  config.enum_value_count = 0U;
  CONFIT_TEST_ASSERT(confit_catalog_add_config(catalog, &config, 0,
                                               &diagnostic) ==
                     CONFIT_ERR_VALIDATION);

  confit_catalog_reset(catalog);
  CONFIT_TEST_ASSERT(confit_catalog_fragment_count(catalog) == 0U);
  CONFIT_TEST_ASSERT(confit_catalog_menu_count(catalog) == 0U);
  CONFIT_TEST_ASSERT(confit_catalog_config_count(catalog) == 0U);
  CONFIT_TEST_ASSERT(confit_catalog_mainmenu(catalog) == 0);
  confit_catalog_destroy(catalog);
  confit_value_destroy(&default_int);
  confit_value_destroy(&default_level);
  confit_value_destroy(&maximum);
  confit_value_destroy(&minimum);
}

static void test_assignment_reason_resolved(void) {
  ConfitAllocator allocator;
  ConfitAssignment assignment;
  ConfitDiagnostic diagnostic;
  ConfitReasonNode reason;
  ConfitResolvedValue resolved;
  ConfitValue default_value;
  ConfitValue effective_value;
  FailingAllocator state;
  size_t children[2] = {3U, 5U};

  memset(&state, 0, sizeof(state));
  state.fail_at = SIZE_MAX;
  allocator = make_failing_allocator(&state);
  confit_assignment_init(&assignment);
  confit_reason_node_init(&reason);
  confit_resolved_value_init(&resolved);
  confit_value_init(&default_value);
  confit_value_init(&effective_value);
  confit_diagnostic_init(&diagnostic);

  CONFIT_TEST_ASSERT(confit_value_set_bool(&default_value, 0, &allocator,
                                           &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_value_set_bool(&effective_value, 1, &allocator,
                                           &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_assignment_set(&assignment, "ENABLE_LOGGING",
                                           &effective_value, &allocator,
                                           &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(strcmp(assignment.symbol, "ENABLE_LOGGING") == 0);
  CONFIT_TEST_ASSERT(confit_reason_node_set(
                         &reason, CONFIT_REASON_AND, "ENABLE_LOGGING",
                         "RUNTIME_SUPPORT", "both inputs are required",
                         children, 2U, &allocator, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(reason.child_count == 2U && reason.children[1] == 5U);
  CONFIT_TEST_ASSERT(confit_resolved_value_set(
                         &resolved, "ENABLE_LOGGING", &default_value,
                         &effective_value, CONFIT_ORIGIN_USER, 1,
                         CONFIT_INDEX_NONE, &allocator, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(resolved.origin == CONFIT_ORIGIN_USER);
  CONFIT_TEST_ASSERT(resolved.available == 1);
  CONFIT_TEST_ASSERT(resolved.effective_value.data.boolean == 1);

  confit_assignment_destroy(&assignment);
  confit_reason_node_destroy(&reason);
  confit_resolved_value_destroy(&resolved);
  confit_value_destroy(&default_value);
  confit_value_destroy(&effective_value);
  CONFIT_TEST_ASSERT(state.live == 0U);
}

static void test_transactional_allocation_failure(void) {
  size_t offset;
  size_t failure_count = 0U;
  int observed_success = 0;
  for (offset = 0U; offset < 12U; ++offset) {
    ConfitAllocator allocator;
    ConfitCatalog *catalog = 0;
    ConfitConfigSpec config;
    ConfitDiagnostic diagnostic;
    ConfitSourceFragmentSpec fragment;
    ConfitValue default_value;
    FailingAllocator state;
    size_t baseline_live;
    ConfitStatus status;

    memset(&state, 0, sizeof(state));
    state.fail_at = SIZE_MAX;
    allocator = make_failing_allocator(&state);
    confit_diagnostic_init(&diagnostic);
    confit_value_init(&default_value);
    CONFIT_TEST_ASSERT(confit_value_set_bool(&default_value, 0, &allocator,
                                             &diagnostic) == CONFIT_OK);
    CONFIT_TEST_ASSERT(confit_catalog_create(&allocator, &catalog,
                                             &diagnostic) == CONFIT_OK);
    fragment.path = "Confit.toml";
    fragment.parent_fragment = CONFIT_INDEX_NONE;
    fragment.source_ordinal = 0U;
    CONFIT_TEST_ASSERT(confit_catalog_add_fragment(catalog, &fragment, 0,
                                                   &diagnostic) == CONFIT_OK);
    baseline_live = state.live;

    memset(&config, 0, sizeof(config));
    config.fragment = 0U;
    config.menu = CONFIT_INDEX_NONE;
    config.symbol = "ENABLE_RUNTIME";
    config.kind = CONFIT_VALUE_BOOL;
    config.prompt = "Enable runtime";
    config.help = "Enable the optional runtime implementation.";
    config.default_value = &default_value;
    config.dependency_text = "PLATFORM_SUPPORT";
    config.declaration.path = "Confit.toml";

    state.fail_at = state.calls + offset;
    status = confit_catalog_add_config(catalog, &config, 0, &diagnostic);
    if (status == CONFIT_OK) {
      observed_success = 1;
      CONFIT_TEST_ASSERT(confit_catalog_config_count(catalog) == 1U);
    } else {
      failure_count += 1U;
      CONFIT_TEST_ASSERT(status == CONFIT_ERR_INTERNAL);
      CONFIT_TEST_ASSERT(confit_catalog_config_count(catalog) == 0U);
      CONFIT_TEST_ASSERT(state.live == baseline_live);
    }
    state.fail_at = SIZE_MAX;
    confit_catalog_destroy(catalog);
    confit_value_destroy(&default_value);
    CONFIT_TEST_ASSERT(state.live == 0U);
  }
  CONFIT_TEST_ASSERT(observed_success);
  CONFIT_TEST_ASSERT(failure_count == 6U);
}

int main(void) {
  test_limits();
  test_values();
  test_symbol_and_enum_validation();
  test_value_allocation_failure();
  test_catalog();
  test_assignment_reason_resolved();
  test_transactional_allocation_failure();
  return 0;
}
