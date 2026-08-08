#include "test_assert.h"
#include "test_fs.h"

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
      "schema_version = 1\n[component]\nid = \"sys.kern.base\"\n"
      "kind = \"kernel_core\"\nmakefile = \"Makefile\"\n"
      "[provides]\nkapi = [\"parus.base.v1\"]\n";
  const char *const driver_manifest =
      "schema_version = 1\n[component]\nid = \"sys.dev.driver\"\n"
      "kind = \"kernel_driver\"\nmakefile = \"Makefile\"\n"
      "[dependencies]\ncomponents = [\"sys.kern.base\"]\n"
      "kapi = [\"parus.base.v1\"]\n";

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
  CONFIT_TEST_ASSERT(confit_component_catalog_resolve(&catalog, roots, 1U, &closure,
                                                       &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(closure.component_count == 2U);
  CONFIT_TEST_ASSERT(strcmp(closure.ordered[0]->id, "sys.kern.base") == 0);
  CONFIT_TEST_ASSERT(strcmp(closure.ordered[1]->id, "sys.dev.driver") == 0);
  confit_component_closure_clear(&closure);
  confit_component_catalog_clear(&catalog);

  path_join(path, sizeof(path), root, "sys/driver/component.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "schema_version = 1\n[component]\nid = \"sys.dev.driver\"\n"
      "kind = \"kernel_driver\"\nmakefile = \"Makefile\"\n"
      "[dependencies]\ncomponents = [\"sys.dev.driver\"]\n"));
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

static void expect_fail_closed_manifest_edges(void) {
  ConfitV2Project *project = 0;
  ConfitComponentCatalog catalog;
  ConfitComponentClosure closure;
  ConfitDiagnostic diagnostic;
  char root[4096] = {0};
  char path[4096];
  const char *roots[] = {"sys.dev.driver"};

  setup_catalog_fixture(root, sizeof(root));
  path_join(path, sizeof(path), root, "sys/driver/component.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "schema_version = 1\n[component]\nid = \"sys.dev.driver\"\n"
      "kind = \"kernel_driver\"\nmakefile = \"Makefile\"\n"
      "[selection]\nenabled_if = [\"feature.net\"]\n"
      "[dependencies]\ncomponents = [\"sys.kern.base\"]\n"
      "kapi = [\"parus.base.v1\"]\n"));
  confit_diagnostic_init(&diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  memset(&closure, 0, sizeof(closure));
  CONFIT_TEST_ASSERT(confit_v2_schema_load_project(root, &project, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_component_catalog_resolve(&catalog, roots, 1U, &closure,
                                                       &diagnostic) == CONFIT_ERR_UNSUPPORTED);
  confit_component_closure_clear(&closure);
  confit_component_catalog_clear(&catalog);

  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "schema_version = 1\n[component]\nid = \"sys.kern.base\"\n"
      "kind = \"kernel_driver\"\nmakefile = \"Makefile\"\n"));
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog, &diagnostic) != CONFIT_OK);
  confit_component_catalog_clear(&catalog);
  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

int main(void) {
  expect_bounded_catalog();
  expect_symlink_rejection();
  expect_fail_closed_manifest_edges();
  return 0;
}
