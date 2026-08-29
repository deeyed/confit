#include "confit/ui.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/expression.h"
#include "confit/resolver.h"
#include "test_assert.h"

typedef struct Fixture {
  ConfitCatalog *catalog;
  ConfitDependencyPlan *plan;
  ConfitUiModel *ui;
} Fixture;

static const char *const kModes[] = {"quiet", "normal", "verbose"};

static void add_fragment(ConfitCatalog *catalog, const char *path,
                         size_t parent) {
  ConfitSourceFragmentSpec spec;
  ConfitDiagnostic diagnostic;
  confit_diagnostic_init(&diagnostic);
  spec.path = path;
  spec.parent_fragment = parent;
  spec.source_ordinal = 0U;
  CONFIT_TEST_ASSERT(
      confit_catalog_add_fragment(catalog, &spec, 0, &diagnostic) == CONFIT_OK);
}

static size_t add_menu(ConfitCatalog *catalog, size_t fragment, size_t parent,
                       const char *prompt) {
  ConfitMenuSpec spec;
  ConfitDiagnostic diagnostic;
  size_t index = CONFIT_INDEX_NONE;
  confit_diagnostic_init(&diagnostic);
  memset(&spec, 0, sizeof(spec));
  spec.fragment = fragment;
  spec.parent_menu = parent;
  spec.prompt = prompt;
  spec.help = "Generic configuration menu.";
  CONFIT_TEST_ASSERT(confit_catalog_add_menu(catalog, &spec, &index,
                                             &diagnostic) == CONFIT_OK);
  return index;
}

static void add_config(ConfitCatalog *catalog, size_t fragment, size_t menu,
                       const char *symbol, ConfitValueKind kind,
                       const char *dependency) {
  ConfitConfigSpec spec;
  ConfitDiagnostic diagnostic;
  ConfitValue value;
  ConfitValue minimum;
  ConfitValue maximum;
  confit_diagnostic_init(&diagnostic);
  confit_value_init(&value);
  confit_value_init(&minimum);
  confit_value_init(&maximum);
  memset(&spec, 0, sizeof(spec));
  spec.fragment = fragment;
  spec.menu = menu;
  spec.symbol = symbol;
  spec.kind = kind;
  spec.prompt = symbol;
  spec.help = "Configure one generic typed value for terminal-free tests.";
  spec.dependency_text = dependency;
  if (kind == CONFIT_VALUE_BOOL) {
    CONFIT_TEST_ASSERT(confit_value_set_bool(&value, 0, 0, &diagnostic) ==
                       CONFIT_OK);
  } else if (kind == CONFIT_VALUE_INT) {
    CONFIT_TEST_ASSERT(confit_value_set_int(&value, 4, 0, &diagnostic) ==
                       CONFIT_OK);
    CONFIT_TEST_ASSERT(confit_value_set_int(&minimum, 1, 0, &diagnostic) ==
                       CONFIT_OK);
    CONFIT_TEST_ASSERT(confit_value_set_int(&maximum, 64, 0, &diagnostic) ==
                       CONFIT_OK);
    spec.range.present = 1;
    spec.range.minimum = &minimum;
    spec.range.maximum = &maximum;
  } else if (kind == CONFIT_VALUE_HEX) {
    CONFIT_TEST_ASSERT(confit_value_set_hex(&value, 0x10U, 0, &diagnostic) ==
                       CONFIT_OK);
    CONFIT_TEST_ASSERT(confit_value_set_hex(&minimum, 0U, 0, &diagnostic) ==
                       CONFIT_OK);
    CONFIT_TEST_ASSERT(
        confit_value_set_hex(&maximum, 0xffffU, 0, &diagnostic) == CONFIT_OK);
    spec.range.present = 1;
    spec.range.minimum = &minimum;
    spec.range.maximum = &maximum;
  } else if (kind == CONFIT_VALUE_STRING) {
    CONFIT_TEST_ASSERT(confit_value_set_string(&value, "base", 4U, 0,
                                               &diagnostic) == CONFIT_OK);
  } else {
    CONFIT_TEST_ASSERT(confit_value_set_enum(&value, "normal", 6U, 0,
                                             &diagnostic) == CONFIT_OK);
    spec.enum_values = kModes;
    spec.enum_value_count = 3U;
  }
  spec.default_value = &value;
  CONFIT_TEST_ASSERT(
      confit_catalog_add_config(catalog, &spec, 0, &diagnostic) == CONFIT_OK);
  confit_value_destroy(&maximum);
  confit_value_destroy(&minimum);
  confit_value_destroy(&value);
}

static Fixture fixture_create(void) {
  Fixture fixture;
  ConfitDiagnostic diagnostic;
  size_t runtime;
  size_t logging;
  memset(&fixture, 0, sizeof(fixture));
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_catalog_create(0, &fixture.catalog, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_catalog_set_mainmenu(fixture.catalog,
                                                 "UI test configuration",
                                                 &diagnostic) == CONFIT_OK);
  add_fragment(fixture.catalog, "Confit.toml", CONFIT_INDEX_NONE);
  add_fragment(fixture.catalog, "config/runtime.toml", 0U);
  add_fragment(fixture.catalog, "config/logging.toml", 1U);
  runtime = add_menu(fixture.catalog, 1U, CONFIT_INDEX_NONE, "Runtime");
  logging = add_menu(fixture.catalog, 2U, runtime, "Logging");
  add_config(fixture.catalog, 1U, runtime, "ENABLE_BASE", CONFIT_VALUE_BOOL, 0);
  add_config(fixture.catalog, 1U, runtime, "COUNT", CONFIT_VALUE_INT, 0);
  add_config(fixture.catalog, 1U, runtime, "DEVICE_ID", CONFIT_VALUE_HEX, 0);
  add_config(fixture.catalog, 1U, runtime, "LABEL", CONFIT_VALUE_STRING, 0);
  add_config(fixture.catalog, 2U, logging, "MODE", CONFIT_VALUE_ENUM, 0);
  add_config(fixture.catalog, 1U, runtime, "ONLY_WHEN_BASE", CONFIT_VALUE_INT,
             "ENABLE_BASE");
  CONFIT_TEST_ASSERT(confit_dependency_plan_create(fixture.catalog, 0,
                                                   &fixture.plan,
                                                   &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_ui_create(fixture.catalog, fixture.plan, 0, 0U, 0,
                                      &fixture.ui, &diagnostic) == CONFIT_OK);
  return fixture;
}

static void fixture_destroy(Fixture *fixture) {
  confit_ui_destroy(fixture->ui);
  confit_dependency_plan_destroy(fixture->plan);
  confit_catalog_destroy(fixture->catalog);
}

static ConfitStatus act(ConfitUiModel *ui, ConfitUiAction action,
                        ConfitUiEffect *effect, ConfitDiagnostic *diagnostic) {
  confit_diagnostic_clear(diagnostic);
  return confit_ui_action(ui, action, effect, diagnostic);
}

static void open_runtime(Fixture *fixture) {
  ConfitDiagnostic diagnostic;
  ConfitUiEffect effect;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(act(fixture->ui, CONFIT_UI_ACTION_OPEN, &effect,
                         &diagnostic) == CONFIT_OK);
}

static void select_symbol(ConfitUiModel *ui, const char *symbol) {
  ConfitDiagnostic diagnostic;
  ConfitUiEffect effect;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(act(ui, CONFIT_UI_ACTION_BEGIN_SEARCH, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_ui_set_input(ui, symbol, strlen(symbol),
                                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_ui_accept(ui, &effect, &diagnostic) == CONFIT_OK);
}

static void edit(ConfitUiModel *ui, const char *text) {
  ConfitDiagnostic diagnostic;
  ConfitUiEffect effect;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(
      act(ui, CONFIT_UI_ACTION_BEGIN_EDIT, &effect, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_ui_set_input(ui, text, strlen(text), &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_ui_accept(ui, &effect, &diagnostic) == CONFIT_OK);
}

static ConfitStatus command(ConfitUiModel *ui, const char *text,
                            ConfitUiEffect *effect,
                            ConfitDiagnostic *diagnostic) {
  ConfitStatus status =
      act(ui, CONFIT_UI_ACTION_BEGIN_COMMAND, effect, diagnostic);
  if (status == CONFIT_OK)
    status = confit_ui_set_input(ui, text, strlen(text), diagnostic);
  if (status == CONFIT_OK)
    status = confit_ui_accept(ui, effect, diagnostic);
  return status;
}

static char *resolution_text(const ConfitUiModel *ui) {
  ConfitDiagnostic diagnostic;
  size_t size = 0U;
  char *text;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(
      confit_resolution_format_canonical(confit_ui_resolution(ui), 0, 0U, &size,
                                         &diagnostic) == CONFIT_ERR_USAGE);
  text = (char *)malloc(size + 1U);
  CONFIT_TEST_ASSERT(text != 0);
  CONFIT_TEST_ASSERT(confit_resolution_format_canonical(
                         confit_ui_resolution(ui), text, size + 1U, &size,
                         &diagnostic) == CONFIT_OK);
  return text;
}

static void test_rows_and_view(void) {
  Fixture fixture = fixture_create();
  ConfitDiagnostic diagnostic;
  ConfitUiEffect effect;
  ConfitUiRowView row;
  char view[2048];
  size_t size;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_ui_row_count(fixture.ui) == 1U);
  CONFIT_TEST_ASSERT(confit_ui_row_at(fixture.ui, 0U, &row) &&
                     row.kind == CONFIT_UI_ROW_MENU && row.depth == 1U);
  CONFIT_TEST_ASSERT(confit_ui_format_view(fixture.ui, view, sizeof(view),
                                           &size, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT_CONTAINS(view, "state=0 dirty=0 menu=root cursor=0");
  CONFIT_TEST_ASSERT_CONTAINS(view, ">menu 1 Runtime");
  open_runtime(&fixture);
  CONFIT_TEST_ASSERT(confit_ui_row_count(fixture.ui) == 6U);
  CONFIT_TEST_ASSERT(confit_ui_set_viewport_rows(fixture.ui, 2U, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_NEXT, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_NEXT, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_ui_cursor(fixture.ui) == 2U &&
                     confit_ui_viewport_offset(fixture.ui) == 1U);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_PARENT, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_ui_row_count(fixture.ui) == 1U);
  fixture_destroy(&fixture);
}

static void test_edits_history_and_transaction(void) {
  Fixture fixture = fixture_create();
  ConfitDiagnostic diagnostic;
  ConfitUiEffect effect;
  ConfitUiDiffView diff;
  char *before;
  char *after;
  confit_diagnostic_init(&diagnostic);
  open_runtime(&fixture);
  select_symbol(fixture.ui, "ENABLE_BASE");
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_TOGGLE, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_ui_diff_at(fixture.ui, 0U, &diff) &&
                     strcmp(diff.symbol, "ENABLE_BASE") == 0);
  select_symbol(fixture.ui, "COUNT");
  edit(fixture.ui, "9");
  select_symbol(fixture.ui, "DEVICE_ID");
  edit(fixture.ui, "0x2a");
  select_symbol(fixture.ui, "LABEL");
  edit(fixture.ui, "development");
  select_symbol(fixture.ui, "MODE");
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_BEGIN_EDIT, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_ui_state(fixture.ui) == CONFIT_UI_ENUM_PICKER);
  CONFIT_TEST_ASSERT(confit_ui_enum_select(fixture.ui, 2U, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_ui_accept(fixture.ui, &effect, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_ui_diff_count(fixture.ui) == 5U);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_UNDO, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_REDO, &effect,
                         &diagnostic) == CONFIT_OK);
  select_symbol(fixture.ui, "COUNT");
  before = resolution_text(fixture.ui);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_BEGIN_EDIT, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_ui_set_input(fixture.ui, "999", 3U, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_ui_accept(fixture.ui, &effect, &diagnostic) ==
                     CONFIT_ERR_VALIDATION);
  after = resolution_text(fixture.ui);
  CONFIT_TEST_ASSERT(strcmp(before, after) == 0);
  free(after);
  free(before);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_CANCEL, &effect,
                         &diagnostic) == CONFIT_OK);
  fixture_destroy(&fixture);

  fixture = fixture_create();
  open_runtime(&fixture);
  select_symbol(fixture.ui, "ONLY_WHEN_BASE");
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_BEGIN_EDIT, &effect,
                         &diagnostic) == CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(!confit_ui_dirty(fixture.ui));
  fixture_destroy(&fixture);
}

static void test_row_render_metadata(void) {
  Fixture fixture = fixture_create();
  ConfitDiagnostic diagnostic;
  ConfitUiEffect effect;
  ConfitUiRowView row;
  confit_diagnostic_init(&diagnostic);
  open_runtime(&fixture);
  select_symbol(fixture.ui, "COUNT");
  CONFIT_TEST_ASSERT(
      confit_ui_row_at(fixture.ui, confit_ui_cursor(fixture.ui), &row));
  CONFIT_TEST_ASSERT(row.has_range && row.range_minimum != 0 &&
                     row.range_maximum != 0 && !row.changed);
  select_symbol(fixture.ui, "ONLY_WHEN_BASE");
  CONFIT_TEST_ASSERT(
      confit_ui_row_at(fixture.ui, confit_ui_cursor(fixture.ui), &row));
  CONFIT_TEST_ASSERT(!row.available && row.dependency_text != 0 &&
                     strcmp(row.dependency_text, "ENABLE_BASE") == 0 &&
                     row.reason_detail != 0);
  select_symbol(fixture.ui, "ENABLE_BASE");
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_TOGGLE, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(
      confit_ui_row_at(fixture.ui, confit_ui_cursor(fixture.ui), &row) &&
      row.changed);
  select_symbol(fixture.ui, "MODE");
  CONFIT_TEST_ASSERT(
      confit_ui_row_at(fixture.ui, confit_ui_cursor(fixture.ui), &row));
  CONFIT_TEST_ASSERT(row.enum_value_count == 3U && row.enum_values != 0 &&
                     strcmp(row.enum_values[2], "verbose") == 0);
  fixture_destroy(&fixture);
}

static void test_search_modes_and_commands(void) {
  Fixture fixture = fixture_create();
  ConfitDiagnostic diagnostic;
  ConfitUiEffect effect;
  confit_diagnostic_init(&diagnostic);
  open_runtime(&fixture);
  select_symbol(fixture.ui, "typed value");
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_SEARCH_NEXT, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_SEARCH_PREVIOUS, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(command(fixture.ui, ":set nounavailable", &effect,
                             &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(!confit_ui_show_unavailable(fixture.ui) &&
                     confit_ui_row_count(fixture.ui) == 5U);
  CONFIT_TEST_ASSERT(command(fixture.ui, ":set unavailable", &effect,
                             &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_SHOW_HELP, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_CANCEL, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_BEGIN_SEARCH, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_CANCEL, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_BEGIN_COMMAND, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_CANCEL, &effect,
                         &diagnostic) == CONFIT_OK);
  select_symbol(fixture.ui, "COUNT");
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_BEGIN_EDIT, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_CANCEL, &effect,
                         &diagnostic) == CONFIT_OK);
  select_symbol(fixture.ui, "MODE");
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_BEGIN_EDIT, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_CANCEL, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_SHOW_DIFF, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_CANCEL, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(command(fixture.ui, ":help", &effect, &diagnostic) ==
                         CONFIT_OK &&
                     confit_ui_state(fixture.ui) == CONFIT_UI_HELP);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_CANCEL, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_QUIT_HINT, &effect,
                         &diagnostic) == CONFIT_OK &&
                     effect == CONFIT_UI_EFFECT_NONE &&
                     confit_ui_notice(fixture.ui) != 0);
  select_symbol(fixture.ui, "ENABLE_BASE");
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_TOGGLE, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(command(fixture.ui, ":q", &effect, &diagnostic) ==
                     CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_CANCEL, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(command(fixture.ui, ":!", &effect, &diagnostic) ==
                     CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_CANCEL, &effect,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(command(fixture.ui, ":wq", &effect, &diagnostic) ==
                         CONFIT_OK &&
                     effect == CONFIT_UI_EFFECT_REQUEST_SAVE);
  CONFIT_TEST_ASSERT(
      confit_ui_save_result(fixture.ui, 0, &effect, &diagnostic) == CONFIT_OK &&
      effect == CONFIT_UI_EFFECT_NONE && confit_ui_dirty(fixture.ui));
  CONFIT_TEST_ASSERT(command(fixture.ui, ":wq", &effect, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(
      confit_ui_save_result(fixture.ui, 1, &effect, &diagnostic) == CONFIT_OK &&
      effect == CONFIT_UI_EFFECT_EXIT && !confit_ui_dirty(fixture.ui));
  CONFIT_TEST_ASSERT(command(fixture.ui, ":w", &effect, &diagnostic) ==
                         CONFIT_OK &&
                     effect == CONFIT_UI_EFFECT_REQUEST_SAVE);
  CONFIT_TEST_ASSERT(
      confit_ui_save_result(fixture.ui, 1, &effect, &diagnostic) == CONFIT_OK &&
      effect == CONFIT_UI_EFFECT_NONE);
  CONFIT_TEST_ASSERT(command(fixture.ui, ":q", &effect, &diagnostic) ==
                         CONFIT_OK &&
                     effect == CONFIT_UI_EFFECT_EXIT);
  CONFIT_TEST_ASSERT(command(fixture.ui, ":x", &effect, &diagnostic) ==
                         CONFIT_OK &&
                     effect == CONFIT_UI_EFFECT_EXIT);
  CONFIT_TEST_ASSERT(command(fixture.ui, ":q!", &effect, &diagnostic) ==
                         CONFIT_OK &&
                     effect == CONFIT_UI_EFFECT_DISCARD_AND_EXIT);
  fixture_destroy(&fixture);
}

static void test_history_bound_and_random_actions(void) {
  Fixture fixture = fixture_create();
  ConfitDiagnostic diagnostic;
  ConfitUiEffect effect;
  uint32_t random = UINT32_C(0x91e10da5);
  size_t index;
  confit_diagnostic_init(&diagnostic);
  open_runtime(&fixture);
  select_symbol(fixture.ui, "ENABLE_BASE");
  for (index = 0U; index < CONFIT_UI_HISTORY_LIMIT + 32U; ++index)
    CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_TOGGLE, &effect,
                           &diagnostic) == CONFIT_OK);
  for (index = 0U; index < CONFIT_UI_HISTORY_LIMIT; ++index)
    CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_UNDO, &effect,
                           &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(act(fixture.ui, CONFIT_UI_ACTION_UNDO, &effect,
                         &diagnostic) == CONFIT_ERR_VALIDATION);
  for (index = 0U; index < 5000U; ++index) {
    ConfitUiAction semantic;
    random = random * UINT32_C(1664525) + UINT32_C(1013904223);
    semantic = (ConfitUiAction)(CONFIT_UI_ACTION_NEXT + random % 15U);
    (void)act(fixture.ui, semantic, &effect, &diagnostic);
    if (confit_ui_state(fixture.ui) != CONFIT_UI_NORMAL)
      (void)act(fixture.ui, CONFIT_UI_ACTION_CANCEL, &effect, &diagnostic);
    CONFIT_TEST_ASSERT(confit_ui_row_count(fixture.ui) == 0U ||
                       confit_ui_cursor(fixture.ui) <
                           confit_ui_row_count(fixture.ui));
  }
  fixture_destroy(&fixture);
}

int main(void) {
  test_rows_and_view();
  test_edits_history_and_transaction();
  test_row_render_metadata();
  test_search_modes_and_commands();
  test_history_bound_and_random_actions();
  return 0;
}
