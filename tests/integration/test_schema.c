#include "confit/schema.h"

#include "test_assert.h"
#include "test_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_PATH_BYTES 4096U

typedef struct FailingAllocatorState {
  size_t attempt;
  size_t fail_at;
  size_t outstanding;
} FailingAllocatorState;

static void *failing_allocate(void *context, size_t size) {
  FailingAllocatorState *state = (FailingAllocatorState *)context;
  void *pointer;
  if (state->attempt++ == state->fail_at) return 0;
  pointer = malloc(size);
  if (pointer != 0) state->outstanding += 1U;
  return pointer;
}

static void failing_deallocate(void *context, void *pointer) {
  FailingAllocatorState *state = (FailingAllocatorState *)context;
  CONFIT_TEST_ASSERT(pointer != 0 && state->outstanding > 0U);
  state->outstanding -= 1U;
  free(pointer);
}

static void join_path(char *out, const char *root, const char *relative) {
  CONFIT_TEST_ASSERT(
      confit_test_fs_path_join(out, TEST_PATH_BYTES, root, relative));
}

static void make_directory(const char *root, const char *relative) {
  char path[TEST_PATH_BYTES];
  join_path(path, root, relative);
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(path));
}

static void write_text(const char *root, const char *relative,
                       const char *text) {
  char path[TEST_PATH_BYTES];
  join_path(path, root, relative);
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, text));
}

static void expect_project_failure(ConfitHostRoot *root, const char *entry,
                                   const char *message, const char *path,
                                   size_t line, size_t column) {
  ConfitDiagnostic diagnostic;
  ConfitSchemaProject *project = 0;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_schema_project_load(root, entry, 0, &project,
                                                &diagnostic) != CONFIT_OK);
  CONFIT_TEST_ASSERT(project == 0 && diagnostic.message != 0);
  if (strcmp(diagnostic.message, message) != 0 || diagnostic.path == 0 ||
      strcmp(diagnostic.path, path) != 0 ||
      (line != 0U && diagnostic.line != line) ||
      (column != 0U && diagnostic.column != column)) {
    fprintf(stderr,
            "schema diagnostic mismatch for %s: expected %s:%zu:%zu '%s', "
            "actual %s:%zu:%zu '%s'\n",
            entry, path, line, column, message,
            diagnostic.path != 0 ? diagnostic.path : "(null)",
            diagnostic.line, diagnostic.column, diagnostic.message);
  }
  CONFIT_TEST_ASSERT(strcmp(diagnostic.message, message) == 0);
  CONFIT_TEST_ASSERT(diagnostic.path != 0 && strcmp(diagnostic.path, path) == 0);
  if (line != 0U) CONFIT_TEST_ASSERT(diagnostic.line == line);
  if (column != 0U) CONFIT_TEST_ASSERT(diagnostic.column == column);
}

static void expect_user_failure(ConfitHostRoot *root, const char *path,
                                const char *message) {
  ConfitDiagnostic diagnostic;
  ConfitUserDocument *document = 0;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_user_document_load_relative(
                         root, path, 0, &document,
                         &diagnostic) != CONFIT_OK);
  CONFIT_TEST_ASSERT(document == 0 && diagnostic.message != 0);
  CONFIT_TEST_ASSERT(strcmp(diagnostic.message, message) == 0);
  CONFIT_TEST_ASSERT(diagnostic.path != 0 && strcmp(diagnostic.path, path) == 0);
}

static void test_valid_project_and_user(const char *root_path,
                                        ConfitHostRoot *root) {
  static const char entry[] =
      "schema_version = 6\n"
      "mainmenu = \"Example configuration\"\n"
      "source = [\"valid/runtime.toml\"]\n";
  static const char runtime[] =
      "[menu]\n"
      "prompt = \"Runtime\"\n"
      "help = \"Configure runtime behavior.\"\n"
      "source = [\"valid/plain.toml\", \"valid/logging.toml\"]\n"
      "\n"
      "[[config]]\n"
      "symbol = \"WORKER_COUNT\"\n"
      "type = \"int\"\n"
      "prompt = \"Worker count\"\n"
      "help = \"Set the worker count.\"\n"
      "default = 4\n"
      "range = { min = 1, max = 64 }\n";
  static const char plain[] =
      "[[config]]\n"
      "symbol = \"ENABLE_METRICS\"\n"
      "type = \"bool\"\n"
      "prompt = \"Enable metrics\"\n"
      "help = \"Collect runtime metrics.\"\n";
  static const char logging[] =
      "[menu]\n"
      "prompt = \"Logging\"\n"
      "help = \"Configure logging.\"\n"
      "\n"
      "[[config]]\n"
      "symbol = \"LOG_LEVEL\"\n"
      "type = \"enum\"\n"
      "prompt = \"Log level\"\n"
      "help = \"Select log detail.\"\n"
      "values = [\"quiet\", \"normal\", \"verbose\"]\n"
      "default = \"normal\"\n"
      "depends_on = \"ENABLE_METRICS\"\n";
  static const char user[] =
      "schema_version = 6\n"
      "[values]\n"
      "ENABLE_METRICS = true\n"
      "WORKER_COUNT = 8\n";
  ConfitDiagnostic diagnostic;
  ConfitSchemaProject *project = 0;
  ConfitUserDocument *document = 0;
  ConfitSchemaConfigView config;
  ConfitUserValueView value;
  ConfitMenuView menu;
  const ConfitCatalog *catalog;
  char user_path[] = "valid/user.toml";

  make_directory(root_path, "valid");
  write_text(root_path, "valid/Confit.toml", entry);
  write_text(root_path, "valid/runtime.toml", runtime);
  write_text(root_path, "valid/plain.toml", plain);
  write_text(root_path, "valid/logging.toml", logging);
  write_text(root_path, "valid/user.toml", user);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_schema_project_load(
                         root, "valid/Confit.toml", 0, &project,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(project != 0);
  catalog = confit_schema_project_catalog(project);
  CONFIT_TEST_ASSERT(catalog != 0);
  CONFIT_TEST_ASSERT(strcmp(confit_catalog_mainmenu(catalog),
                            "Example configuration") == 0);
  CONFIT_TEST_ASSERT(confit_catalog_fragment_count(catalog) == 4U);
  CONFIT_TEST_ASSERT(confit_catalog_menu_count(catalog) == 2U);
  CONFIT_TEST_ASSERT(confit_catalog_config_count(catalog) == 0U);
  CONFIT_TEST_ASSERT(confit_schema_project_config_count(project) == 3U);
  CONFIT_TEST_ASSERT(confit_catalog_menu_at(catalog, 1U, &menu));
  CONFIT_TEST_ASSERT(menu.parent_menu == 0U);
  CONFIT_TEST_ASSERT(confit_schema_project_find_config(
      project, "WORKER_COUNT", &config));
  CONFIT_TEST_ASSERT(config.fragment == 1U && config.menu == 0U);
  CONFIT_TEST_ASSERT(strcmp(config.type_name, "int") == 0);
  CONFIT_TEST_ASSERT(config.default_candidate != 0 &&
                     config.range_candidate != 0 &&
                     config.values_candidate == 0);
  CONFIT_TEST_ASSERT(confit_schema_project_find_config(
      project, "ENABLE_METRICS", &config));
  CONFIT_TEST_ASSERT(config.fragment == 2U && config.menu == 0U);
  CONFIT_TEST_ASSERT(confit_schema_project_find_config(project, "LOG_LEVEL",
                                                       &config));
  CONFIT_TEST_ASSERT(config.fragment == 3U && config.menu == 1U);
  CONFIT_TEST_ASSERT(config.values_candidate != 0 &&
                     config.default_candidate != 0 &&
                     strcmp(config.dependency_text, "ENABLE_METRICS") == 0);

  CONFIT_TEST_ASSERT(confit_user_document_load_relative(
                         root, user_path, 0, &document,
                         &diagnostic) == CONFIT_OK);
  user_path[0] = 'X';
  CONFIT_TEST_ASSERT(document != 0 &&
                     confit_user_document_value_count(document) == 2U);
  CONFIT_TEST_ASSERT(confit_user_document_value_at(document, 0U, &value));
  CONFIT_TEST_ASSERT(strcmp(value.symbol, "ENABLE_METRICS") == 0);
  CONFIT_TEST_ASSERT(value.declaration.path != 0 &&
                     strcmp(value.declaration.path, "valid/user.toml") == 0);
  CONFIT_TEST_ASSERT(confit_toml_value_type(value.value_candidate) ==
                     CONFIT_TOML_VALUE_BOOL);
  confit_user_document_destroy(document);
  document = 0;
  write_text(root_path, "valid/defaults.toml", "schema_version = 6\n");
  CONFIT_TEST_ASSERT(confit_user_document_load_relative(
                         root, "valid/defaults.toml", 0, &document,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_user_document_value_count(document) == 0U);
  confit_user_document_destroy(document);
  confit_schema_project_destroy(project);
}

static void test_entry_and_fragment_shapes(const char *root_path,
                                           ConfitHostRoot *root) {
  make_directory(root_path, "shape");
  write_text(root_path, "shape/root-extra.toml",
             "schema_version = 6\nmainmenu = \"X\"\nsource = []\nowner = 1\n");
  expect_project_failure(root, "shape/root-extra.toml",
                         "entry document contains an unknown field",
                         "shape/root-extra.toml", 4U, 9U);
  write_text(root_path, "shape/root-config.toml",
             "schema_version = 6\nmainmenu = \"X\"\nsource = []\n"
             "[[config]]\nsymbol = \"BAD\"\n");
  expect_project_failure(root, "shape/root-config.toml",
                         "entry document contains an unknown field",
                         "shape/root-config.toml", 4U, 3U);
  write_text(root_path, "shape/root.toml",
             "schema_version = 6\nmainmenu = \"X\"\n"
             "source = [\"shape/empty.toml\"]\n");
  write_text(root_path, "shape/empty.toml", "note = \"not allowed\"\n");
  expect_project_failure(root, "shape/root.toml",
                         "fragment contains an unknown field",
                         "shape/empty.toml", 1U, 9U);
  write_text(root_path, "shape/empty.toml", "config = []\n");
  expect_project_failure(root, "shape/root.toml",
                         "fragment must contain a menu or config declaration",
                         "shape/empty.toml", 1U, 10U);
  write_text(root_path, "shape/empty.toml", "config = \"bad\"\n");
  expect_project_failure(root, "shape/root.toml",
                         "config must be an array of tables",
                         "shape/empty.toml", 1U, 11U);
  write_text(root_path, "shape/version.toml",
             "schema_version = 5\nmainmenu = \"X\"\nsource = []\n");
  expect_project_failure(root, "shape/version.toml",
                         "entry schema_version must be integer 6",
                         "shape/version.toml", 1U, 18U);
  write_text(root_path, "shape/missing-mainmenu.toml",
             "schema_version = 6\nsource = []\n");
  expect_project_failure(root, "shape/missing-mainmenu.toml",
                         "entry document requires mainmenu",
                         "shape/missing-mainmenu.toml", 1U, 1U);
  write_text(root_path, "shape/root.toml",
             "schema_version = 6\nmainmenu = \"X\"\n"
             "source = [\"shape/menu-extra.toml\"]\n");
  write_text(root_path, "shape/menu-extra.toml",
             "[menu]\nprompt = \"P\"\nhelp = \"H\"\nowner = \"x\"\n");
  expect_project_failure(root, "shape/root.toml",
                         "menu contains an unknown field",
                         "shape/menu-extra.toml", 4U, 10U);
  write_text(root_path, "shape/menu-extra.toml",
             "[[menu]]\nprompt = \"P\"\nhelp = \"H\"\n");
  expect_project_failure(root, "shape/root.toml",
                         "fragment menu source carrier must be a table",
                         "shape/menu-extra.toml", 0U, 0U);
  write_text(root_path, "shape/menu-extra.toml",
             "[menu]\nprompt = \"P\"\nhelp = \"H\"\n"
             "[menu]\nprompt = \"Q\"\nhelp = \"I\"\n");
  expect_project_failure(root, "shape/root.toml",
                         "tomlc17 rejected TOML input",
                         "shape/menu-extra.toml", 0U, 0U);
  write_text(root_path, "shape/menu-extra.toml",
             "[menu]\nhelp = \"H\"\n");
  expect_project_failure(root, "shape/root.toml", "menu requires prompt",
                         "shape/menu-extra.toml", 1U, 2U);
  write_text(root_path, "shape/menu-extra.toml",
             "[menu]\nprompt = \"P\"\n");
  expect_project_failure(root, "shape/root.toml", "menu requires help",
                         "shape/menu-extra.toml", 1U, 2U);
  write_text(root_path, "shape/duplicate-key.toml",
             "schema_version = 6\nmainmenu = \"A\"\nmainmenu = \"B\"\n"
             "source = []\n");
  expect_project_failure(root, "shape/duplicate-key.toml",
                         "tomlc17 rejected TOML input",
                         "shape/duplicate-key.toml", 3U, 1U);
}

static void write_config_fragment(const char *root_path, const char *path,
                                  const char *extra) {
  char text[2048];
  CONFIT_TEST_ASSERT(snprintf(
                         text, sizeof(text),
                         "[[config]]\n"
                         "symbol = \"VALID_SYMBOL\"\n"
                         "type = \"bool\"\n"
                         "prompt = \"Valid prompt\"\n"
                         "help = \"Valid help.\"\n"
                         "%s",
                         extra) > 0);
  write_text(root_path, path, text);
}

static void test_config_fields_and_legacy(const char *root_path,
                                          ConfitHostRoot *root) {
  static const char *const forbidden[] = {
      "owner", "since", "stability", "tags", "menu_order", "placement",
      "allowed", "enabled_values", "cardinality", "namespace", "target",
      "profile", "selection", "build", "source_file", "object", "provider",
      "driver", "visible_if", "needs", "select", "imply", "choice", "rule",
      "assert", "inherit", "extends", "override", "source_if",
      "conditional_source"};
  size_t index;
  char extra[256];
  make_directory(root_path, "fields");
  write_text(root_path, "fields/root.toml",
             "schema_version = 6\nmainmenu = \"X\"\n"
             "source = [\"fields/leaf.toml\"]\n");
  for (index = 0U; index < sizeof(forbidden) / sizeof(forbidden[0]); ++index) {
    CONFIT_TEST_ASSERT(snprintf(extra, sizeof(extra), "%s = \"x\"\n",
                                forbidden[index]) > 0);
    write_config_fragment(root_path, "fields/leaf.toml", extra);
    expect_project_failure(root, "fields/root.toml",
                           "config declaration contains an unknown field",
                           "fields/leaf.toml", 6U, 0U);
  }
  write_text(root_path, "fields/leaf.toml",
             "[[config]]\ntype = \"bool\"\nprompt = \"P\"\nhelp = \"H\"\n");
  expect_project_failure(root, "fields/root.toml",
                         "config declaration requires symbol",
                         "fields/leaf.toml", 1U, 3U);
  write_text(root_path, "fields/leaf.toml",
             "[[config]]\nsymbol = \"A\"\nprompt = \"P\"\nhelp = \"H\"\n");
  expect_project_failure(root, "fields/root.toml",
                         "config declaration requires type",
                         "fields/leaf.toml", 1U, 3U);
  write_text(root_path, "fields/leaf.toml",
             "[[config]]\nsymbol = \"A\"\ntype = \"bool\"\nhelp = \"H\"\n");
  expect_project_failure(root, "fields/root.toml",
                         "config declaration requires prompt",
                         "fields/leaf.toml", 1U, 3U);
  write_text(root_path, "fields/leaf.toml",
             "[[config]]\nsymbol = \"A\"\ntype = \"bool\"\nprompt = \"P\"\n");
  expect_project_failure(root, "fields/root.toml",
                         "config declaration requires help",
                         "fields/leaf.toml", 1U, 3U);
  write_text(root_path, "fields/leaf.toml",
             "[[config]]\nsymbol = \"bad_symbol\"\ntype = \"bool\"\n"
             "prompt = \"P\"\nhelp = \"H\"\n");
  expect_project_failure(root, "fields/root.toml",
                         "configuration symbol is invalid",
                         "fields/leaf.toml", 2U, 11U);
  write_text(root_path, "fields/leaf.toml",
             "[[config]]\nsymbol = \"A\"\ntype = 1\n"
             "prompt = \"P\"\nhelp = \"H\"\n");
  expect_project_failure(root, "fields/root.toml",
                         "configuration type must be a bounded string",
                         "fields/leaf.toml", 3U, 8U);
  write_text(root_path, "fields/leaf.toml",
             "[[config]]\nsymbol = \"A\"\ntype = \"bool\"\n"
             "prompt = \"Bad\\u001bprompt\"\nhelp = \"H\"\n");
  expect_project_failure(root, "fields/root.toml",
                         "config prompt must be a bounded one-line string",
                         "fields/leaf.toml", 4U, 11U);
  write_text(root_path, "fields/leaf.toml",
             "[[config]]\nsymbol = \"A\"\ntype = \"bool\"\n"
             "prompt = \"P\"\nhelp = \"\"\n");
  expect_project_failure(root, "fields/root.toml",
                         "config help must be a bounded non-empty string",
                         "fields/leaf.toml", 5U, 9U);
  write_config_fragment(root_path, "fields/leaf.toml", "default = []\n");
  expect_project_failure(root, "fields/root.toml",
                         "config default must be a TOML scalar",
                         "fields/leaf.toml", 6U, 11U);
  write_config_fragment(root_path, "fields/leaf.toml", "values = 1\n");
  expect_project_failure(root, "fields/root.toml",
                         "config values must be an array",
                         "fields/leaf.toml", 6U, 10U);
  write_config_fragment(root_path, "fields/leaf.toml", "range = 1\n");
  expect_project_failure(root, "fields/root.toml",
                         "config range must be a table",
                         "fields/leaf.toml", 6U, 9U);
  write_config_fragment(root_path, "fields/leaf.toml", "depends_on = 1\n");
  expect_project_failure(root, "fields/root.toml",
                         "config depends_on must be a bounded string",
                         "fields/leaf.toml", 6U, 14U);
}

static void test_duplicate_depth_and_control(const char *root_path,
                                             ConfitHostRoot *root) {
  make_directory(root_path, "relations");
  write_text(root_path, "relations/root.toml",
             "schema_version = 6\nmainmenu = \"X\"\n"
             "source = [\"relations/a.toml\", \"relations/b.toml\"]\n");
  write_text(root_path, "relations/a.toml",
             "[[config]]\nsymbol = \"DUPLICATE\"\ntype = \"bool\"\n"
             "prompt = \"A\"\nhelp = \"First.\"\n");
  write_text(root_path, "relations/b.toml",
             "[[config]]\nsymbol = \"DUPLICATE\"\ntype = \"int\"\n"
             "prompt = \"B\"\nhelp = \"Second.\"\n");
  expect_project_failure(root, "relations/root.toml",
                         "configuration symbol is duplicated",
                         "relations/b.toml", 2U, 11U);
  write_text(root_path, "relations/control-root.toml",
             "schema_version = 6\nmainmenu = \"X\"\n"
             "source = [\"relations/control.toml\"]\n");
  write_text(root_path, "relations/control.toml",
             "[menu]\nprompt = \"Bad\\u001btitle\"\nhelp = \"Help.\"\n");
  expect_project_failure(root, "relations/control-root.toml",
                         "menu prompt must be a bounded one-line string",
                         "relations/control.toml", 2U, 11U);
  write_text(root_path, "relations/control.toml",
             "[menu]\nprompt = \"Bad\\u2028title\"\nhelp = \"Help.\"\n");
  expect_project_failure(root, "relations/control-root.toml",
                         "menu prompt must be a bounded one-line string",
                         "relations/control.toml", 2U, 11U);

  make_directory(root_path, "depth");
  write_text(root_path, "depth/root.toml",
             "schema_version = 6\nmainmenu = \"X\"\n"
             "source = [\"depth/m1.toml\"]\n");
  write_text(root_path, "depth/m1.toml",
             "[menu]\nprompt = \"M1\"\nhelp = \"H1\"\n"
             "source = [\"depth/m2.toml\"]\n");
  write_text(root_path, "depth/m2.toml",
             "[menu]\nprompt = \"M2\"\nhelp = \"H2\"\n"
             "source = [\"depth/m3.toml\"]\n");
  write_text(root_path, "depth/m3.toml",
             "[menu]\nprompt = \"M3\"\nhelp = \"H3\"\n"
             "source = [\"depth/m4.toml\"]\n");
  write_text(root_path, "depth/m4.toml",
             "[menu]\nprompt = \"M4\"\nhelp = \"H4\"\n");
  expect_project_failure(root, "depth/root.toml",
                         "catalog presentation relation is invalid",
                         "depth/m4.toml", 1U, 2U);
}

static void test_user_shapes(const char *root_path, ConfitHostRoot *root) {
  make_directory(root_path, "user");
  write_text(root_path, "user/extra.toml",
             "schema_version = 6\nprofile = \"x\"\n");
  expect_user_failure(root, "user/extra.toml",
                      "user document contains an unknown field");
  write_text(root_path, "user/version.toml", "schema_version = 5\n");
  expect_user_failure(root, "user/version.toml",
                      "user schema_version must be integer 6");
  write_text(root_path, "user/table.toml",
             "schema_version = 6\nvalues = []\n");
  expect_user_failure(root, "user/table.toml", "user values must be a table");
  write_text(root_path, "user/symbol.toml",
             "schema_version = 6\n[values]\nbad_symbol = true\n");
  expect_user_failure(root, "user/symbol.toml",
                      "user value symbol is invalid");
  write_text(root_path, "user/value.toml",
             "schema_version = 6\n[values]\nGOOD = [1]\n");
  expect_user_failure(root, "user/value.toml",
                      "user value must be a TOML scalar");
  write_text(root_path, "user/duplicate.toml",
             "schema_version = 6\n[values]\nGOOD = 1\nGOOD = 2\n");
  expect_user_failure(root, "user/duplicate.toml",
                      "tomlc17 rejected TOML input");
}

static void test_allocation_cleanup(const char *root_path,
                                    ConfitHostRoot *root) {
  ConfitAllocator allocator;
  ConfitDiagnostic diagnostic;
  ConfitSchemaProject *project = 0;
  FailingAllocatorState state;
  size_t fail_at;
  int success = 0;
  make_directory(root_path, "allocation");
  write_text(root_path, "allocation/root.toml",
             "schema_version = 6\nmainmenu = \"X\"\n"
             "source = [\"allocation/leaf.toml\"]\n");
  write_text(root_path, "allocation/leaf.toml",
             "[[config]]\nsymbol = \"ALLOCATED\"\ntype = \"bool\"\n"
             "prompt = \"Allocated\"\nhelp = \"Exercise allocation cleanup.\"\n");
  allocator.context = &state;
  allocator.allocate = failing_allocate;
  allocator.deallocate = failing_deallocate;
  for (fail_at = 0U; fail_at < 256U; ++fail_at) {
    memset(&state, 0, sizeof(state));
    state.fail_at = fail_at;
    confit_diagnostic_init(&diagnostic);
    if (confit_schema_project_load(root, "allocation/root.toml", &allocator,
                                   &project, &diagnostic) == CONFIT_OK) {
      confit_schema_project_destroy(project);
      project = 0;
      CONFIT_TEST_ASSERT(state.outstanding == 0U);
      success = 1;
      break;
    }
    CONFIT_TEST_ASSERT(project == 0 && state.outstanding == 0U);
  }
  CONFIT_TEST_ASSERT(success);
}

int main(void) {
  char raw_root[TEST_PATH_BYTES];
  char root_path[TEST_PATH_BYTES];
  ConfitDiagnostic diagnostic;
  ConfitHostRoot *root = 0;
  CONFIT_TEST_ASSERT(confit_test_fs_make_temp_dir(raw_root, sizeof(raw_root),
                                                  "confit-r08"));
  CONFIT_TEST_ASSERT(realpath(raw_root, root_path) != 0);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_host_root_open_absolute(
                         root_path, 0, &root, &diagnostic) == CONFIT_OK);
  test_valid_project_and_user(root_path, root);
  test_entry_and_fragment_shapes(root_path, root);
  test_config_fields_and_legacy(root_path, root);
  test_duplicate_depth_and_control(root_path, root);
  test_user_shapes(root_path, root);
  test_allocation_cleanup(root_path, root);
  confit_host_root_destroy(root);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root_path));
  return 0;
}
