#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "confit/config.h"
#include "confit/expression.h"
#include "confit/schema.h"

#include "test_assert.h"
#include "test_fs.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#define TEST_PATH_BYTES 4096U

typedef struct FailingAllocatorState {
  size_t calls;
  size_t fail_at;
  size_t outstanding;
} FailingAllocatorState;

static void *failing_allocate(void *context, size_t size) {
  FailingAllocatorState *state = (FailingAllocatorState *)context;
  void *pointer;
  if (state->calls++ == state->fail_at) return 0;
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

static const char kEntry[] =
    "schema_version = 6\n"
    "mainmenu = \"User configuration tests\"\n"
    "source = [\"config/options.toml\"]\n";

static const char kOptions[] =
    "[menu]\n"
    "prompt = \"Options\"\n"
    "help = \"Exercise every schema 6 user value type.\"\n"
    "\n"
    "[[config]]\n"
    "symbol = \"Z_BOOL\"\n"
    "type = \"bool\"\n"
    "prompt = \"Boolean\"\n"
    "help = \"Set a boolean value.\"\n"
    "default = false\n"
    "\n"
    "[[config]]\n"
    "symbol = \"A_INT\"\n"
    "type = \"int\"\n"
    "prompt = \"Integer\"\n"
    "help = \"Set a bounded integer value.\"\n"
    "default = 4\n"
    "range = { min = 1, max = 10 }\n"
    "\n"
    "[[config]]\n"
    "symbol = \"M_HEX\"\n"
    "type = \"hex\"\n"
    "prompt = \"Hexadecimal\"\n"
    "help = \"Set a bounded hexadecimal value.\"\n"
    "default = 0x10\n"
    "range = { min = 0x0, max = 0xff }\n"
    "\n"
    "[[config]]\n"
    "symbol = \"E_STRING\"\n"
    "type = \"string\"\n"
    "prompt = \"String\"\n"
    "help = \"Set a string value.\"\n"
    "default = \"\"\n"
    "\n"
    "[[config]]\n"
    "symbol = \"Q_ENUM\"\n"
    "type = \"enum\"\n"
    "prompt = \"Enumeration\"\n"
    "help = \"Select one closed enumeration atom.\"\n"
    "values = [\"quiet\", \"verbose\"]\n"
    "default = \"quiet\"\n"
    "depends_on = \"Z_BOOL\"\n";

static const char kUserAllTypes[] =
    "schema_version = 6\n"
    "\n"
    "[values]\n"
    "Q_ENUM = \"verbose\"\n"
    "M_HEX = 0x2A\n"
    "Z_BOOL = true\n"
    "E_STRING = \"line\\n\\t\\\"\\\\snowman \\u2603\"\n"
    "A_INT = 9\n";

static const char kExpectedMinimal[] =
    "schema_version = 6\n"
    "\n"
    "[values]\n"
    "A_INT = 9\n"
    "E_STRING = \"line\\n\\t\\\"\\\\snowman \xE2\x98\x83\"\n"
    "M_HEX = 0x2a\n"
    "Q_ENUM = \"verbose\"\n"
    "Z_BOOL = true\n";

static void join_path(char *out, const char *root, const char *relative) {
  CONFIT_TEST_ASSERT(
      confit_test_fs_path_join(out, TEST_PATH_BYTES, root, relative));
}

static void write_text(const char *root, const char *relative,
                       const char *text) {
  char path[TEST_PATH_BYTES];
  join_path(path, root, relative);
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, text));
}

static void make_directory(const char *root, const char *relative) {
  char path[TEST_PATH_BYTES];
  join_path(path, root, relative);
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(path));
}

static char *read_text(const char *root, const char *relative) {
  char path[TEST_PATH_BYTES];
  join_path(path, root, relative);
  return confit_test_fs_read_file(path);
}

static void write_project(const char *root) {
  make_directory(root, "config");
  make_directory(root, "users");
  make_directory(root, "output");
  write_text(root, "Confit.toml", kEntry);
  write_text(root, "config/options.toml", kOptions);
}

static ConfitSchemaProject *load_project(ConfitHostRoot *root) {
  ConfitDiagnostic diagnostic;
  ConfitSchemaProject *project = 0;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_schema_project_load(
                         root, "Confit.toml", 0, &project,
                         &diagnostic) == CONFIT_OK);
  return project;
}

static ConfitUserConfig *load_config(ConfitHostRoot *root,
                                     const ConfitCatalog *catalog,
                                     const char *path) {
  ConfitDiagnostic diagnostic;
  ConfitUserConfig *config = 0;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_user_config_load_relative(
                         root, path, catalog, 0, &config,
                         &diagnostic) == CONFIT_OK);
  return config;
}

static ConfitUserConfig *load_config_absolute(const ConfitCatalog *catalog,
                                              const char *path) {
  ConfitDiagnostic diagnostic;
  ConfitUserConfig *config = 0;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_user_config_load_absolute(
                         path, catalog, 0, &config,
                         &diagnostic) == CONFIT_OK);
  return config;
}

static ConfitResolution *resolve_config(
    const ConfitSchemaProject *project, const ConfitUserConfig *config) {
  ConfitDiagnostic diagnostic;
  ConfitResolution *resolution = 0;
  const ConfitAssignment *assignments;
  size_t assignment_count = 0U;
  confit_diagnostic_init(&diagnostic);
  assignments = confit_user_config_assignments(config, &assignment_count);
  CONFIT_TEST_ASSERT(confit_resolve(
                         confit_schema_project_catalog(project),
                         confit_schema_project_dependency_plan(project),
                         assignments, assignment_count, 0, &resolution,
                         &diagnostic) == CONFIT_OK);
  return resolution;
}

static char *format_minimal(const ConfitResolution *resolution,
                            size_t *out_size) {
  ConfitDiagnostic diagnostic;
  char *buffer;
  size_t size = 0U;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_user_config_format_minimal(
                         resolution, 0, 0U, &size,
                         &diagnostic) == CONFIT_OK);
  buffer = (char *)malloc(size + 1U);
  CONFIT_TEST_ASSERT(buffer != 0);
  memset(buffer, 0x5a, size + 1U);
  CONFIT_TEST_ASSERT(confit_user_config_format_minimal(
                         resolution, buffer, size + 1U, &size,
                         &diagnostic) == CONFIT_OK);
  *out_size = size;
  return buffer;
}

static void test_load_resolve_serialize_round_trip(const char *root_path,
                                                   ConfitHostRoot *root) {
  ConfitSchemaProject *project = load_project(root);
  const ConfitCatalog *catalog = confit_schema_project_catalog(project);
  ConfitUserConfig *config;
  ConfitUserConfig *absolute_config;
  ConfitUserConfig *reparsed;
  ConfitResolution *resolution;
  ConfitResolution *reparsed_resolution;
  ConfitDiagnostic diagnostic;
  const ConfitAssignment *assignment;
  char *before;
  char *after;
  char *serialized;
  char *again;
  size_t assignment_count;
  size_t serialized_size;
  size_t again_size;
  char absolute_path[TEST_PATH_BYTES];

  write_text(root_path, "users/all-types.toml", kUserAllTypes);
  join_path(absolute_path, root_path, "users/all-types.toml");
  before = read_text(root_path, "users/all-types.toml");
  CONFIT_TEST_ASSERT(before != 0);
  config = load_config(root, catalog, "users/all-types.toml");
  absolute_config = load_config_absolute(catalog, absolute_path);
  CONFIT_TEST_ASSERT(
      confit_user_config_assignment_count(absolute_config) == 5U &&
      strcmp(confit_input_image_path(
                 confit_user_config_input(absolute_config)),
             absolute_path) == 0);
  confit_user_config_destroy(absolute_config);
  CONFIT_TEST_ASSERT(confit_user_config_input(config) != 0);
  assignment_count = confit_user_config_assignment_count(config);
  CONFIT_TEST_ASSERT(assignment_count == 5U);
  CONFIT_TEST_ASSERT(confit_user_config_assignment_at(config, 0U,
                                                       &assignment));
  CONFIT_TEST_ASSERT(strcmp(assignment->symbol, "A_INT") == 0);
  CONFIT_TEST_ASSERT(confit_user_config_assignment_at(config, 4U,
                                                       &assignment));
  CONFIT_TEST_ASSERT(strcmp(assignment->symbol, "Z_BOOL") == 0);
  resolution = resolve_config(project, config);
  serialized = format_minimal(resolution, &serialized_size);
  CONFIT_TEST_ASSERT(serialized_size == sizeof(kExpectedMinimal) - 1U);
  CONFIT_TEST_ASSERT(memcmp(serialized, kExpectedMinimal,
                            serialized_size + 1U) == 0);

  after = read_text(root_path, "users/all-types.toml");
  CONFIT_TEST_ASSERT(after != 0 && strcmp(before, after) == 0);
  confit_test_fs_free(after);
  confit_test_fs_free(before);

  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_user_config_write_minimal(
                         root, "output/minimal.toml", resolution, 0,
                         &diagnostic) == CONFIT_OK);
  reparsed = load_config(root, catalog, "output/minimal.toml");
  reparsed_resolution = resolve_config(project, reparsed);
  again = format_minimal(reparsed_resolution, &again_size);
  CONFIT_TEST_ASSERT(again_size == serialized_size &&
                     memcmp(again, serialized, serialized_size + 1U) == 0);

  free(again);
  free(serialized);
  confit_resolution_destroy(reparsed_resolution);
  confit_user_config_destroy(reparsed);
  confit_resolution_destroy(resolution);
  confit_user_config_destroy(config);
  confit_schema_project_destroy(project);
}

static void test_empty_omitted_and_default_equal(const char *root_path,
                                                 ConfitHostRoot *root) {
  static const char empty[] = "schema_version = 6\n\n[values]\n";
  static const char omitted[] = "schema_version = 6\n";
  static const char default_equal[] =
      "schema_version = 6\n\n[values]\n"
      "A_INT = 4\nM_HEX = 0x10\nZ_BOOL = false\n"
      "E_STRING = \"\"\nQ_ENUM = \"quiet\"\n";
  static const char canonical_empty[] =
      "schema_version = 6\n\n[values]\n";
  const char *paths[] = {"users/empty.toml", "users/omitted.toml",
                         "users/default-equal.toml"};
  const char *inputs[] = {empty, omitted, default_equal};
  ConfitSchemaProject *project = load_project(root);
  const ConfitCatalog *catalog = confit_schema_project_catalog(project);
  size_t index;

  for (index = 0U; index < 3U; ++index) {
    ConfitUserConfig *config;
    ConfitResolution *resolution;
    char *serialized;
    size_t size;
    write_text(root_path, paths[index], inputs[index]);
    config = load_config(root, catalog, paths[index]);
    resolution = resolve_config(project, config);
    serialized = format_minimal(resolution, &size);
    CONFIT_TEST_ASSERT(size == sizeof(canonical_empty) - 1U &&
                       memcmp(serialized, canonical_empty, size + 1U) == 0);
    free(serialized);
    confit_resolution_destroy(resolution);
    confit_user_config_destroy(config);
  }
  confit_schema_project_destroy(project);
}

static void expect_config_failure(ConfitHostRoot *root,
                                  const ConfitCatalog *catalog,
                                  const char *path, const char *message) {
  ConfitDiagnostic diagnostic;
  ConfitUserConfig *config = (ConfitUserConfig *)(uintptr_t)1U;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_user_config_load_relative(
                         root, path, catalog, 0, &config,
                         &diagnostic) != CONFIT_OK);
  if (diagnostic.message == 0 || strcmp(diagnostic.message, message) != 0)
    fprintf(stderr, "expected '%s', got '%s'\n", message,
            diagnostic.message != 0 ? diagnostic.message : "(null)");
  CONFIT_TEST_ASSERT(config == 0 && diagnostic.message != 0 &&
                     strcmp(diagnostic.message, message) == 0);
  CONFIT_TEST_ASSERT(diagnostic.path != 0 && strcmp(diagnostic.path, path) == 0);
}

static void test_invalid_link_and_type(const char *root_path,
                                       ConfitHostRoot *root) {
  ConfitSchemaProject *project = load_project(root);
  const ConfitCatalog *catalog = confit_schema_project_catalog(project);
  ConfitDiagnostic diagnostic;
  ConfitUserConfig *config;
  ConfitResolution *resolution = 0;
  const ConfitAssignment *assignments;
  size_t assignment_count;

  write_text(root_path, "users/stale.toml",
             "schema_version = 6\n[values]\nREMOVED = true\n");
  expect_config_failure(
      root, catalog, "users/stale.toml",
      "user value names an unknown or stale configuration symbol");
  write_text(root_path, "users/wrong-int.toml",
             "schema_version = 6\n[values]\nA_INT = true\n");
  expect_config_failure(
      root, catalog, "users/wrong-int.toml",
      "user value does not match the declared native TOML type");
  write_text(root_path, "users/decimal-hex.toml",
             "schema_version = 6\n[values]\nM_HEX = 42\n");
  expect_config_failure(
      root, catalog, "users/decimal-hex.toml",
      "user value does not match the declared native TOML type");

  write_text(root_path, "users/outside.toml",
             "schema_version = 6\n[values]\nA_INT = 99\n");
  config = load_config(root, catalog, "users/outside.toml");
  assignments = confit_user_config_assignments(config, &assignment_count);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_resolve(
                         catalog,
                         confit_schema_project_dependency_plan(project),
                         assignments, assignment_count, 0, &resolution,
                         &diagnostic) == CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(resolution == 0 && diagnostic.message != 0 &&
                     strcmp(diagnostic.message,
                            "user assignment is outside the declared range") ==
                         0);
  confit_user_config_destroy(config);

  write_text(root_path, "users/unavailable.toml",
             "schema_version = 6\n[values]\nQ_ENUM = \"verbose\"\n");
  config = load_config(root, catalog, "users/unavailable.toml");
  assignments = confit_user_config_assignments(config, &assignment_count);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_resolve(
                         catalog,
                         confit_schema_project_dependency_plan(project),
                         assignments, assignment_count, 0, &resolution,
                         &diagnostic) == CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(resolution == 0 && diagnostic.message != 0 &&
                     strcmp(diagnostic.message,
                            "unavailable option has a non-default user value") ==
                         0);
  confit_user_config_destroy(config);
  confit_schema_project_destroy(project);
}

static void test_stable_input_order_and_small_buffer(const char *root_path,
                                                     ConfitHostRoot *root) {
  static const char first[] =
      "schema_version = 6\n[values]\nZ_BOOL = true\nA_INT = 8\n";
  static const char second[] =
      "schema_version = 6\n[values]\nA_INT = 8\nZ_BOOL = true\n";
  ConfitSchemaProject *project = load_project(root);
  const ConfitCatalog *catalog = confit_schema_project_catalog(project);
  ConfitUserConfig *first_config;
  ConfitUserConfig *second_config;
  ConfitResolution *first_resolution;
  ConfitResolution *second_resolution;
  ConfitDiagnostic diagnostic;
  char *first_text;
  char *second_text;
  char guard[16];
  size_t first_size;
  size_t second_size;
  size_t required = 0U;

  write_text(root_path, "users/order-one.toml", first);
  write_text(root_path, "users/order-two.toml", second);
  first_config = load_config(root, catalog, "users/order-one.toml");
  second_config = load_config(root, catalog, "users/order-two.toml");
  first_resolution = resolve_config(project, first_config);
  second_resolution = resolve_config(project, second_config);
  first_text = format_minimal(first_resolution, &first_size);
  second_text = format_minimal(second_resolution, &second_size);
  CONFIT_TEST_ASSERT(first_size == second_size &&
                     memcmp(first_text, second_text, first_size + 1U) == 0);
  memset(guard, 0x5a, sizeof(guard));
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_user_config_format_minimal(
                         first_resolution, guard, sizeof(guard), &required,
                         &diagnostic) == CONFIT_ERR_USAGE);
  CONFIT_TEST_ASSERT(required == first_size);
  {
    size_t index;
    for (index = 0U; index < sizeof(guard); ++index)
      CONFIT_TEST_ASSERT((unsigned char)guard[index] == 0x5aU);
  }
  free(second_text);
  free(first_text);
  confit_resolution_destroy(second_resolution);
  confit_resolution_destroy(first_resolution);
  confit_user_config_destroy(second_config);
  confit_user_config_destroy(first_config);
  confit_schema_project_destroy(project);
}

static void test_atomic_write_failure(const char *root_path,
                                      ConfitHostRoot *root) {
#if !defined(_WIN32)
  ConfitSchemaProject *project = load_project(root);
  const ConfitCatalog *catalog = confit_schema_project_catalog(project);
  ConfitUserConfig *config;
  ConfitResolution *resolution;
  ConfitDiagnostic diagnostic;
  char link_path[TEST_PATH_BYTES];
  char *victim;

  write_text(root_path, "users/write.toml",
             "schema_version = 6\n[values]\nZ_BOOL = true\n");
  write_text(root_path, "output/victim.toml", "untouched\n");
  join_path(link_path, root_path, "output/destination.toml");
  CONFIT_TEST_ASSERT(symlink("victim.toml", link_path) == 0);
  config = load_config(root, catalog, "users/write.toml");
  resolution = resolve_config(project, config);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_user_config_write_minimal(
                         root, "output/not-toml", resolution, 0,
                         &diagnostic) == CONFIT_ERR_VALIDATION);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_user_config_write_minimal(
                         root, "output/destination.toml", resolution, 0,
                         &diagnostic) == CONFIT_ERR_IO);
  victim = read_text(root_path, "output/victim.toml");
  CONFIT_TEST_ASSERT(victim != 0 && strcmp(victim, "untouched\n") == 0);
  confit_test_fs_free(victim);
  confit_resolution_destroy(resolution);
  confit_user_config_destroy(config);
  confit_schema_project_destroy(project);
#else
  (void)root_path;
  (void)root;
#endif
}

static void test_load_allocation_failure(ConfitHostRoot *root,
                                         const ConfitCatalog *catalog) {
  size_t fail_at;
  int reached_success = 0;
  for (fail_at = 0U; fail_at < 256U && !reached_success; ++fail_at) {
    FailingAllocatorState state;
    ConfitAllocator allocator;
    ConfitDiagnostic diagnostic;
    ConfitUserConfig *config = (ConfitUserConfig *)(uintptr_t)1U;
    ConfitStatus status;
    memset(&state, 0, sizeof(state));
    state.fail_at = fail_at;
    allocator.context = &state;
    allocator.allocate = failing_allocate;
    allocator.deallocate = failing_deallocate;
    confit_diagnostic_init(&diagnostic);
    status = confit_user_config_load_relative(
        root, "users/all-types.toml", catalog, &allocator, &config,
        &diagnostic);
    if (status == CONFIT_OK) {
      CONFIT_TEST_ASSERT(config != 0);
      confit_user_config_destroy(config);
      reached_success = 1;
    } else {
      CONFIT_TEST_ASSERT(config == 0);
      CONFIT_TEST_ASSERT(diagnostic.path == 0 ||
                         strcmp(diagnostic.path, "users/all-types.toml") == 0);
    }
    CONFIT_TEST_ASSERT(state.outstanding == 0U);
  }
  CONFIT_TEST_ASSERT(reached_success);
}

static void init_large_assignment(ConfitAssignment *assignment,
                                  const char *symbol, const char *text,
                                  size_t text_size,
                                  ConfitDiagnostic *diagnostic) {
  ConfitValue value;
  confit_assignment_init(assignment);
  confit_value_init(&value);
  CONFIT_TEST_ASSERT(confit_value_set_string(&value, text, text_size, 0,
                                             diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_assignment_set(assignment, symbol, &value, 0,
                                           diagnostic) == CONFIT_OK);
  confit_value_destroy(&value);
}

static void test_serialized_file_limit(void) {
  enum { kCount = 128 };
  ConfitCatalog *catalog = 0;
  ConfitDependencyPlan *plan = 0;
  ConfitResolution *within = 0;
  ConfitResolution *over = 0;
  ConfitAssignment *assignments;
  ConfitDiagnostic diagnostic;
  ConfitSourceFragmentSpec fragment;
  ConfitValue default_value;
  char *large_text;
  char symbols[kCount][16];
  size_t index;
  size_t size = 0U;

  confit_diagnostic_init(&diagnostic);
  confit_value_init(&default_value);
  CONFIT_TEST_ASSERT(confit_value_set_string(&default_value, "", 0U, 0,
                                             &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_catalog_create(0, &catalog, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_catalog_set_mainmenu(catalog, "Large", &diagnostic) ==
                     CONFIT_OK);
  fragment.path = "Confit.toml";
  fragment.parent_fragment = CONFIT_INDEX_NONE;
  fragment.source_ordinal = 0U;
  CONFIT_TEST_ASSERT(confit_catalog_add_fragment(catalog, &fragment, 0,
                                                 &diagnostic) == CONFIT_OK);
  assignments = (ConfitAssignment *)calloc(kCount, sizeof(*assignments));
  large_text = (char *)malloc(CONFIT_LIMIT_STRING_BYTES);
  CONFIT_TEST_ASSERT(assignments != 0 && large_text != 0);
  memset(large_text, '\n', CONFIT_LIMIT_STRING_BYTES);
  for (index = 0U; index < kCount; ++index) {
    ConfitConfigSpec spec;
    const int written = snprintf(symbols[index], sizeof(symbols[index]),
                                 "BIG_%03zu", index);
    CONFIT_TEST_ASSERT(written > 0 && (size_t)written < sizeof(symbols[index]));
    memset(&spec, 0, sizeof(spec));
    spec.fragment = 0U;
    spec.menu = CONFIT_INDEX_NONE;
    spec.symbol = symbols[index];
    spec.kind = CONFIT_VALUE_STRING;
    spec.prompt = "Large string";
    spec.help = "Exercise the canonical TOML output byte ceiling.";
    spec.default_value = &default_value;
    spec.declaration.path = "Confit.toml";
    spec.declaration.line = index + 1U;
    spec.declaration.column = 1U;
    CONFIT_TEST_ASSERT(confit_catalog_add_config(catalog, &spec, 0,
                                                 &diagnostic) == CONFIT_OK);
    init_large_assignment(&assignments[index], symbols[index], large_text,
                          CONFIT_LIMIT_STRING_BYTES, &diagnostic);
  }
  CONFIT_TEST_ASSERT(confit_dependency_plan_create(catalog, 0, &plan,
                                                   &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_resolve(catalog, plan, assignments, kCount - 1U,
                                   0, &within, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_user_config_format_minimal(
                         within, 0, 0U, &size, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(size <= CONFIT_LIMIT_TOML_FILE_BYTES);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_resolve(catalog, plan, assignments, kCount, 0,
                                   &over, &diagnostic) == CONFIT_OK);
  size = 123U;
  CONFIT_TEST_ASSERT(confit_user_config_format_minimal(
                         over, 0, 0U, &size,
                         &diagnostic) == CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(size == 0U && diagnostic.message != 0 &&
                     strcmp(diagnostic.message,
                            "minimal user configuration exceeds the TOML file limit") ==
                         0);

  confit_resolution_destroy(over);
  confit_resolution_destroy(within);
  confit_dependency_plan_destroy(plan);
  for (index = kCount; index > 0U; --index)
    confit_assignment_destroy(&assignments[index - 1U]);
  free(large_text);
  free(assignments);
  confit_catalog_destroy(catalog);
  confit_value_destroy(&default_value);
}

int main(void) {
  char raw_root[TEST_PATH_BYTES];
  char root_path[TEST_PATH_BYTES];
  ConfitHostRoot *root = 0;
  ConfitDiagnostic diagnostic;

  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_test_fs_make_temp_dir(
      raw_root, sizeof(raw_root), "confit-config"));
#if !defined(_WIN32)
  CONFIT_TEST_ASSERT(realpath(raw_root, root_path) != 0);
#else
  CONFIT_TEST_ASSERT(strlen(raw_root) + 1U <= sizeof(root_path));
  memcpy(root_path, raw_root, strlen(raw_root) + 1U);
#endif
  write_project(root_path);
  CONFIT_TEST_ASSERT(confit_host_root_open_absolute(
                         root_path, 0, &root, &diagnostic) == CONFIT_OK);
  test_load_resolve_serialize_round_trip(root_path, root);
  {
    ConfitSchemaProject *project = load_project(root);
    test_load_allocation_failure(root,
                                 confit_schema_project_catalog(project));
    confit_schema_project_destroy(project);
  }
  test_empty_omitted_and_default_equal(root_path, root);
  test_invalid_link_and_type(root_path, root);
  test_stable_input_order_and_small_buffer(root_path, root);
  test_atomic_write_failure(root_path, root);
  test_serialized_file_limit();
  confit_host_root_destroy(root);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root_path));
  return 0;
}
