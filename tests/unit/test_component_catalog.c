#include "test_assert.h"
#include "test_fs.h"

#include <stdio.h>
#include <string.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "confit/component_catalog.h"

static void path_join(char *out, size_t out_size, const char *left,
                      const char *right) {
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(out, out_size, left, right));
}

static void setup_catalog_fixture(char *root, size_t root_size) {
  char config[4096];
  char sys[4096];
  char base[4096];
  char driver[4096];
  char path[4096];
  const char *const project =
      "[project]\nname = \"catalog\"\nnamespace = \"catalog\"\n"
      "schema_version = 2\ncomponent_roots = [\"sys\"]\n";
  const char *const base_manifest =
      "schema_version = 2\n[component]\nid = \"sys.kern.base\"\n"
      "kind = \"kernel_core\"\n"
      "[requires]\ncomponents = []\nkapi = []\n"
      "[provides]\ncapabilities = [\"kernel.base@1\"]\n"
      "kapi = [\"parus.base.v1\"]\n";
  const char *const driver_manifest =
      "schema_version = 2\n[component]\nid = \"sys.dev.driver\"\n"
      "kind = \"kernel_driver\"\n"
      "[requires]\ncomponents = []\n"
      "kapi = [\"parus.base.v1\"]\n"
      "[provides]\ncapabilities = [\"driver.example@1\"]\nkapi = []\n";

  CONFIT_TEST_ASSERT(confit_test_fs_make_temp_dir(root, root_size, "confit-component"));
  path_join(config, sizeof(config), root, "config");
  path_join(sys, sizeof(sys), root, "sys");
  path_join(base, sizeof(base), sys, "base");
  path_join(driver, sizeof(driver), sys, "driver");
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(config));
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(base));
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(driver));
  path_join(path, sizeof(path), config, "project.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, project));
  path_join(path, sizeof(path), base, "component.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, base_manifest));
  path_join(path, sizeof(path), base, "Makefile");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, "# unparsed\n"));
  path_join(path, sizeof(path), driver, "component.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, driver_manifest));
  path_join(path, sizeof(path), driver, "Makefile");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, "# unparsed\n"));
}

static void expect_bounded_catalog(void) {
  ConfitV2Project *project = 0;
  ConfitComponentCatalog catalog;
  ConfitComponentClosure closure;
  ConfitDiagnostic diagnostic;
  char root[4096] = {0};
  char path[4096];
  const char *roots[] = {"sys.dev.driver"};

  setup_catalog_fixture(root, sizeof(root));
  confit_diagnostic_init(&diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  memset(&closure, 0, sizeof(closure));
  CONFIT_TEST_ASSERT(confit_v2_schema_load_project(root, &project, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(catalog.component_count == 2U);
  {
    const ConfitComponent *candidates[5];
    size_t candidate_count = confit_component_catalog_suggest(
        &catalog, "sys.dev.drivr", candidates, 5U);
    CONFIT_TEST_ASSERT(candidate_count == 2U);
    CONFIT_TEST_ASSERT(strcmp(candidates[0]->id, "sys.dev.driver") == 0);
  }
  CONFIT_TEST_ASSERT(confit_component_catalog_resolve(&catalog, roots, 1U, &closure,
                                                       &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(closure.component_count == 2U);
  CONFIT_TEST_ASSERT(strcmp(closure.ordered[0]->id, "sys.kern.base") == 0);
  CONFIT_TEST_ASSERT(strcmp(closure.ordered[1]->id, "sys.dev.driver") == 0);
  CONFIT_TEST_ASSERT(closure.reason_count == 2U);
  CONFIT_TEST_ASSERT(closure.reasons[0].kind == CONFIT_COMPONENT_REASON_ROOT);
  CONFIT_TEST_ASSERT(closure.reasons[1].kind == CONFIT_COMPONENT_REASON_KAPI_PROVIDER);
  CONFIT_TEST_ASSERT(strcmp(closure.reasons[1].component_id, "sys.kern.base") == 0);
  CONFIT_TEST_ASSERT(strcmp(closure.reasons[1].from_id, "sys.dev.driver") == 0);
  CONFIT_TEST_ASSERT(strcmp(closure.reasons[1].requirement, "parus.base.v1") == 0);
  CONFIT_TEST_ASSERT(closure.reasons[1].source_path != 0);
  CONFIT_TEST_ASSERT(closure.reasons[1].source_line == 7U);
  CONFIT_TEST_ASSERT(closure.reasons[1].source_column > 0U);
  confit_component_closure_clear(&closure);

  {
    const char *required[] = {"driver.example@1"};
    CONFIT_TEST_ASSERT(confit_component_catalog_resolve_selection(
        &catalog, 0, 0U, required, 1U, 0, 0U, &closure, &diagnostic) == CONFIT_OK);
    CONFIT_TEST_ASSERT(closure.component_count == 2U);
    CONFIT_TEST_ASSERT(closure.reason_count == 2U);
    CONFIT_TEST_ASSERT(closure.reasons[0].kind == CONFIT_COMPONENT_REASON_KAPI_PROVIDER);
    CONFIT_TEST_ASSERT(closure.reasons[1].kind == CONFIT_COMPONENT_REASON_REQUIRED_CAPABILITY);
    confit_component_closure_clear(&closure);
  }
  confit_component_catalog_clear(&catalog);

  path_join(path, sizeof(path), root, "sys/driver/component.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "schema_version = 2\n[component]\nid = \"sys.dev.driver\"\n"
      "kind = \"kernel_driver\"\n"
      "[requires]\ncomponents = [\"sys.dev.driver\"]\nkapi = []\n"
      "[provides]\ncapabilities = []\nkapi = []\n"));
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog, &diagnostic) != CONFIT_OK);
  confit_component_catalog_clear(&catalog);

  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_symlink_rejection(void) {
#if !defined(_WIN32)
  ConfitV2Project *project = 0;
  ConfitComponentCatalog catalog;
  ConfitDiagnostic diagnostic;
  char root[4096] = {0};
  char link_path[4096];

  setup_catalog_fixture(root, sizeof(root));
  path_join(link_path, sizeof(link_path), root, "sys/escape");
  CONFIT_TEST_ASSERT(symlink("/tmp", link_path) == 0);
  confit_diagnostic_init(&diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_v2_schema_load_project(root, &project, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog, &diagnostic) != CONFIT_OK);
  confit_component_catalog_clear(&catalog);
  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
#endif
}

static void expect_fail_closed_manifest_schema(void) {
  ConfitV2Project *project = 0;
  ConfitComponentCatalog catalog;
  ConfitDiagnostic diagnostic;
  char root[4096] = {0};
  char path[4096];

  setup_catalog_fixture(root, sizeof(root));
  path_join(path, sizeof(path), root, "sys/driver/component.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "schema_version = 2\n[component]\nid = \"sys.dev.driver\"\n"
      "kind = \"kernel_driver\"\nmakefile = \"Makefile\"\n"
      "[selection]\nenabled_if = [\"feature.net\"]\n"
      "[requires]\ncomponents = []\nkapi = [\"parus.base.v1\"]\n"
      "[provides]\ncapabilities = [\"driver.example@1\"]\nkapi = []\n"));
  confit_diagnostic_init(&diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_v2_schema_load_project(root, &project, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog, &diagnostic) == CONFIT_ERR_SCHEMA);
  confit_component_catalog_clear(&catalog);

  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "schema_version = 1\n[component]\nid = \"sys.kern.base\"\n"
      "kind = \"kernel_driver\"\n[requires]\ncomponents = []\nkapi = []\n"
      "[provides]\ncapabilities = []\nkapi = []\n"));
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog, &diagnostic) != CONFIT_OK);
  confit_component_catalog_clear(&catalog);

  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "schema_version = 2\n[component]\nid = \"sys.dev.driver\"\n"
      "kind = \"kernel_driver\"\n[requires]\ncomponents = []\n"
      "[provides]\ncapabilities = []\nkapi = []\n"));
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog, &diagnostic) ==
                     CONFIT_ERR_SCHEMA);
  confit_component_catalog_clear(&catalog);
  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_provider_and_version_rejection(void) {
  ConfitV2Project *project = 0;
  ConfitComponentCatalog catalog;
  ConfitDiagnostic diagnostic;
  char root[4096] = {0};
  char path[4096];

  setup_catalog_fixture(root, sizeof(root));
  path_join(path, sizeof(path), root, "sys/driver/component.toml");
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v2_schema_load_project(root, &project, &diagnostic) == CONFIT_OK);

  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "schema_version = 2\n[component]\nid = \"sys.dev.driver\"\n"
      "kind = \"kernel_driver\"\n[requires]\ncomponents = []\nkapi = []\n"
      "[provides]\ncapabilities = [\"driver.example@1\"]\n"
      "kapi = [\"parus.base.v1\"]\n"));
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog, &diagnostic) == CONFIT_ERR_SCHEMA);
  confit_component_catalog_clear(&catalog);

  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "schema_version = 2\n[component]\nid = \"sys.dev.driver\"\n"
      "kind = \"kernel_driver\"\n[requires]\ncomponents = []\nkapi = []\n"
      "[provides]\ncapabilities = [\"kernel.base@1\"]\nkapi = []\n"));
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog, &diagnostic) ==
                     CONFIT_ERR_SCHEMA);
  confit_component_catalog_clear(&catalog);

  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "schema_version = 2\n[component]\nid = \"sys.dev.driver\"\n"
      "kind = \"kernel_driver\"\n[requires]\ncomponents = []\n"
      "kapi = [\"parus.missing.v1\"]\n"
      "[provides]\ncapabilities = [\"driver.example@1\"]\nkapi = []\n"));
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog, &diagnostic) == CONFIT_ERR_SCHEMA);
  confit_component_catalog_clear(&catalog);

  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "schema_version = 2\n[component]\nid = \"sys.dev.driver\"\n"
      "kind = \"kernel_driver\"\n[requires]\ncomponents = []\nkapi = []\n"
      "[provides]\ncapabilities = [\"driver.unversioned\"]\nkapi = []\n"));
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog, &diagnostic) == CONFIT_ERR_SCHEMA);
  confit_component_catalog_clear(&catalog);

  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_duplicate_id_and_atom_rejection(void) {
  ConfitV2Project *project = 0;
  ConfitComponentCatalog catalog;
  ConfitDiagnostic diagnostic;
  char root[4096] = {0};
  char directory[4096];
  char path[4096];
  char oversized_id[130];
  char oversized_manifest[1024];

  setup_catalog_fixture(root, sizeof(root));
  path_join(directory, sizeof(directory), root, "sys/duplicate");
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(directory));
  path_join(path, sizeof(path), directory, "Makefile");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, "# unparsed\n"));
  path_join(path, sizeof(path), directory, "component.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "schema_version = 2\n[component]\nid = \"sys.kern.base\"\n"
      "kind = \"kernel_core\"\n[requires]\ncomponents = []\nkapi = []\n"
      "[provides]\ncapabilities = []\nkapi = []\n"));
  confit_diagnostic_init(&diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_v2_schema_load_project(root, &project, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog, &diagnostic) ==
                     CONFIT_ERR_SCHEMA);
  confit_component_catalog_clear(&catalog);

  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(directory));
  path_join(path, sizeof(path), root, "sys/driver/component.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "schema_version = 2\n[component]\nid = \"sys.dev.driver\"\n"
      "kind = \"kernel_driver\"\n[requires]\n"
      "components = [\"sys.kern.base\", \"sys.kern.base\"]\nkapi = []\n"
      "[provides]\ncapabilities = []\nkapi = []\n"));
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog, &diagnostic) ==
                     CONFIT_ERR_SCHEMA);
  confit_component_catalog_clear(&catalog);

  memset(oversized_id, 'a', 128U);
  oversized_id[128] = '\0';
  CONFIT_TEST_ASSERT(snprintf(
      oversized_manifest, sizeof(oversized_manifest),
      "schema_version = 2\n[component]\nid = \"%s\"\n"
      "kind = \"kernel_driver\"\n[requires]\ncomponents = []\nkapi = []\n"
      "[provides]\ncapabilities = []\nkapi = []\n",
      oversized_id) > 0);
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, oversized_manifest));
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog, &diagnostic) ==
                     CONFIT_ERR_SCHEMA);
  confit_component_catalog_clear(&catalog);
  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_graph_depth_rejection(void) {
  ConfitV2Project *project = 0;
  ConfitComponentCatalog catalog;
  ConfitDiagnostic diagnostic;
  char root[4096] = {0};
  char config[4096];
  char sys[4096];
  char path[4096];
  unsigned int index;

  CONFIT_TEST_ASSERT(confit_test_fs_make_temp_dir(root, sizeof(root), "confit-depth"));
  path_join(config, sizeof(config), root, "config");
  path_join(sys, sizeof(sys), root, "sys");
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(config));
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(sys));
  path_join(path, sizeof(path), config, "project.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path, "[project]\nname = \"depth\"\nnamespace = \"depth\"\n"
            "schema_version = 2\ncomponent_roots = [\"sys\"]\n"));
  for (index = 0U; index < 33U; ++index) {
    char directory[4096];
    char leaf[32];
    char manifest[1024];
    int written;
    (void)snprintf(leaf, sizeof(leaf), "c%02u", index);
    path_join(directory, sizeof(directory), sys, leaf);
    CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(directory));
    path_join(path, sizeof(path), directory, "component.toml");
    written = snprintf(
        manifest, sizeof(manifest),
        "schema_version = 2\n[component]\nid = \"sys.depth.c%02u\"\n"
        "kind = \"kernel_core\"\n[requires]\ncomponents = %s\nkapi = []\n"
        "[provides]\ncapabilities = []\nkapi = []\n",
        index, index == 0U ? "[]" : "[\"sys.depth.c00\"]");
    if (index > 1U) {
      written = snprintf(
          manifest, sizeof(manifest),
          "schema_version = 2\n[component]\nid = \"sys.depth.c%02u\"\n"
          "kind = \"kernel_core\"\n[requires]\ncomponents = [\"sys.depth.c%02u\"]\nkapi = []\n"
          "[provides]\ncapabilities = []\nkapi = []\n",
          index, index - 1U);
    }
    CONFIT_TEST_ASSERT(written > 0 && (size_t)written < sizeof(manifest));
    CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, manifest));
    path_join(path, sizeof(path), directory, "Makefile");
    CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, "# unparsed\n"));
  }
  confit_diagnostic_init(&diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_v2_schema_load_project(root, &project, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog, &diagnostic) == CONFIT_ERR_SCHEMA);
  CONFIT_TEST_ASSERT(diagnostic.message != 0 && strstr(diagnostic.message, "depth") != 0);
  confit_component_catalog_clear(&catalog);
  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_list_bound_rejection(void) {
  ConfitV2Project *project = 0;
  ConfitComponentCatalog catalog;
  ConfitDiagnostic diagnostic;
  char root[4096] = {0};
  char path[4096];
  char manifest[16384];
  size_t used;
  unsigned int index;

  setup_catalog_fixture(root, sizeof(root));
  path_join(path, sizeof(path), root, "sys/driver/component.toml");
  used = (size_t)snprintf(
      manifest, sizeof(manifest),
      "schema_version = 2\n[component]\nid = \"sys.dev.driver\"\n"
      "kind = \"kernel_driver\"\n[requires]\ncomponents = []\nkapi = []\n"
      "[provides]\ncapabilities = [");
  CONFIT_TEST_ASSERT(used < sizeof(manifest));
  for (index = 0U; index < 129U; ++index) {
    int written = snprintf(manifest + used, sizeof(manifest) - used,
                           "%s\"driver.bound.%03u@1\"",
                           index == 0U ? "" : ",", index);
    CONFIT_TEST_ASSERT(written > 0 && (size_t)written < sizeof(manifest) - used);
    used += (size_t)written;
  }
  CONFIT_TEST_ASSERT(used + sizeof("]\nkapi = []\n") <= sizeof(manifest));
  memcpy(manifest + used, "]\nkapi = []\n", sizeof("]\nkapi = []\n"));
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, manifest));

  confit_diagnostic_init(&diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_v2_schema_load_project(root, &project, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog, &diagnostic) ==
                     CONFIT_ERR_SCHEMA);
  confit_component_catalog_clear(&catalog);
  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

int main(void) {
  expect_bounded_catalog();
  expect_symlink_rejection();
  expect_fail_closed_manifest_schema();
  expect_provider_and_version_rejection();
  expect_duplicate_id_and_atom_rejection();
  expect_graph_depth_rejection();
  expect_list_bound_rejection();
  return 0;
}
