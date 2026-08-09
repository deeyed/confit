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
  path_join(path, sizeof(path), base, "base.c");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, "int confit_fixture_base(void) { return 1; }\n"));
  path_join(path, sizeof(path), base, "Makefile");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "# 이 core fixture는 base source 하나만 소유한다.\n"
      "# Driver policy와 test 실행은 이 Makefile이 소유하지 않는다.\n"
      "PARUS_BUILD_API=2\nPARUS_COMPONENT=sys.kern.base\n"
      "SRCS=base.c\n\n.include <parus.kernel.mk>\n"));
  path_join(path, sizeof(path), driver, "component.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, driver_manifest));
  path_join(path, sizeof(path), driver, "driver.c");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, "int confit_fixture_driver(void) { return 2; }\n"));
  path_join(path, sizeof(path), driver, "Makefile");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "# 이 driver fixture는 local driver source만 소유한다.\n"
      "# Hardware 존재와 attach 성공은 이 Makefile의 claim이 아니다.\n"
      "PARUS_BUILD_API=2\nPARUS_COMPONENT=sys.dev.driver\n"
      "SRCS=driver.c\n\n.include <parus.driver.mk>\n"));
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
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "# 이 duplicate fixture는 source ownership만 선언한다.\n"
      "# Duplicate component identity는 catalog가 거부해야 한다.\n"
      "PARUS_BUILD_API=2\nPARUS_COMPONENT=sys.kern.base\n"
      "SRCS=duplicate.c\n\n.include <parus.kernel.mk>\n"));
  path_join(path, sizeof(path), directory, "duplicate.c");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, "int duplicate_source(void) { return 0; }\n"));
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
    path_join(path, sizeof(path), directory, "depth.c");
    CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, "int depth_source(void) { return 0; }\n"));
    path_join(path, sizeof(path), directory, "Makefile");
    {
      char makefile[512];
      int makefile_written = snprintf(
          makefile, sizeof(makefile),
          "# 이 depth fixture는 graph bound 검사용 source만 소유한다.\n"
          "# Dependency 의미와 graph verdict는 manifest resolver가 소유한다.\n"
          "PARUS_BUILD_API=2\nPARUS_COMPONENT=sys.depth.c%02u\n"
          "SRCS=depth.c\n\n.include <parus.kernel.mk>\n",
          index);
      CONFIT_TEST_ASSERT(makefile_written > 0 &&
                         (size_t)makefile_written < sizeof(makefile));
      CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, makefile));
    }
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

static void expect_make_api_v2_rejection(void) {
  static const char *const invalid_makefiles[] = {
      "# old API must fail\nPARUS_BUILD_API=1\nPARUS_COMPONENT=sys.dev.driver\n"
      "SRCS=driver.c\n.include <parus.driver.mk>\n",
      "# conditions must fail\nPARUS_BUILD_API=2\nPARUS_COMPONENT=sys.dev.driver\n"
      ".if 1\nSRCS=driver.c\n.endif\n.include <parus.driver.mk>\n",
      "# raw flags must fail\nPARUS_BUILD_API=2\nPARUS_COMPONENT=sys.dev.driver\n"
      "SRCS=driver.c\nCFLAGS=-O3\n.include <parus.driver.mk>\n",
      "# kind mismatch must fail\nPARUS_BUILD_API=2\nPARUS_COMPONENT=sys.dev.driver\n"
      "SRCS=driver.c\n.include <parus.kernel.mk>\n",
      "# source traversal must fail\nPARUS_BUILD_API=2\nPARUS_COMPONENT=sys.dev.driver\n"
      "SRCS=../base/base.c\n.include <parus.driver.mk>\n",
      "# source glob must fail\nPARUS_BUILD_API=2\nPARUS_COMPONENT=sys.dev.driver\n"
      "SRCS=*.c\n.include <parus.driver.mk>\n",
      "# recipe must fail\nPARUS_BUILD_API=2\nPARUS_COMPONENT=sys.dev.driver\n"
      "SRCS=driver.c\nall:\n\tcc driver.c\n.include <parus.driver.mk>\n",
      "# duplicate source must fail\nPARUS_BUILD_API=2\nPARUS_COMPONENT=sys.dev.driver\n"
      "SRCS=driver.c driver.c\n.include <parus.driver.mk>\n",
      "# directory disguised as source must fail\nPARUS_BUILD_API=2\nPARUS_COMPONENT=sys.dev.driver\n"
      "SRCS=directory.c\n.include <parus.driver.mk>\n",
  };
  ConfitV2Project *project = 0;
  ConfitComponentCatalog catalog;
  ConfitDiagnostic diagnostic;
  char root[4096] = {0};
  char path[4096];
  size_t index;

  setup_catalog_fixture(root, sizeof(root));
  path_join(path, sizeof(path), root, "sys/driver/Makefile");
  {
    char directory_source[4096];
    path_join(directory_source, sizeof(directory_source), root,
              "sys/driver/directory.c");
    CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(directory_source));
  }
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v2_schema_load_project(root, &project, &diagnostic) ==
                     CONFIT_OK);
  for (index = 0U; index < sizeof(invalid_makefiles) / sizeof(invalid_makefiles[0]);
       ++index) {
    CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, invalid_makefiles[index]));
    memset(&catalog, 0, sizeof(catalog));
    confit_diagnostic_clear(&diagnostic);
    CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog,
                                                      &diagnostic) ==
                       CONFIT_ERR_SCHEMA);
    confit_component_catalog_clear(&catalog);
  }
  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_zero_central_sound_driver(void) {
  const char *const roots[] = {"sys.dev.audio.pci.cmi8738"};
  ConfitV2Project *project = 0;
  ConfitComponentCatalog catalog;
  ConfitComponentClosure closure;
  ConfitDiagnostic diagnostic;
  const ConfitComponent *sound;
  char root[4096] = {0};
  char directory[4096];
  char path[4096];

  setup_catalog_fixture(root, sizeof(root));
  path_join(directory, sizeof(directory), root, "sys/sound/cmi8738");
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(directory));
  path_join(path, sizeof(path), directory, "component.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "schema_version = 2\n[component]\n"
      "id = \"sys.dev.audio.pci.cmi8738\"\nkind = \"kernel_driver\"\n"
      "[requires]\ncomponents = []\nkapi = [\"parus.base.v1\"]\n"
      "[provides]\ncapabilities = [\"driver.audio.pci.cmi8738@1\"]\nkapi = []\n"));
  path_join(path, sizeof(path), directory, "driver.c");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path, "int cmi8738_driver_fixture(void) { return 0; }\n"));
  path_join(path, sizeof(path), directory, "stream.c");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path, "int cmi8738_stream_fixture(void) { return 0; }\n"));
  path_join(path, sizeof(path), directory, "Makefile");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "# 이 synthetic leaf는 CMI8738 register와 stream source만 소유한다.\n"
      "# PCI enumeration, DMA policy와 hardware attach claim은 소유하지 않는다.\n"
      "PARUS_BUILD_API=2\nPARUS_COMPONENT=sys.dev.audio.pci.cmi8738\n"
      "SRCS=driver.c stream.c\n\n.include <parus.driver.mk>\n"));

  confit_diagnostic_init(&diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  memset(&closure, 0, sizeof(closure));
  CONFIT_TEST_ASSERT(confit_v2_schema_load_project(root, &project, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog,
                                                    &diagnostic) == CONFIT_OK);
  sound = confit_component_catalog_find(&catalog,
                                         "sys.dev.audio.pci.cmi8738");
  CONFIT_TEST_ASSERT(sound != 0);
  CONFIT_TEST_ASSERT(sound->source_count == 2U);
  CONFIT_TEST_ASSERT(strcmp(sound->build_include, "parus.driver.mk") == 0);
  CONFIT_TEST_ASSERT(confit_component_catalog_resolve(
                         &catalog, roots, 1U, &closure, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(closure.component_count == 2U);
  CONFIT_TEST_ASSERT(strcmp(closure.ordered[0]->id, "sys.kern.base") == 0);
  CONFIT_TEST_ASSERT(strcmp(closure.ordered[1]->id,
                            "sys.dev.audio.pci.cmi8738") == 0);

  confit_component_closure_clear(&closure);
  confit_component_catalog_clear(&catalog);
  confit_v2_project_free(project);
  /* Temp project removal proves the driver did not require a central table edit. */
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void write_test_component(const char *root, const char *owner,
                                 const char *lane,
                                 const char *evidence_class,
                                 unsigned int timeout_ms) {
  char directory[4096];
  char path[4096];
  char manifest[2048];
  int written;
  path_join(directory, sizeof(directory), root, "sys/state-test");
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(directory));
  path_join(path, sizeof(path), directory, "test.c");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path, "int confit_component_owned_test(void) { return 0; }\n"));
  path_join(path, sizeof(path), directory, "Makefile");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "# 이 fixture는 test binary의 local source만 선언한다.\n"
      "# 실행 정책과 timeout은 manifest와 private runner가 소유한다.\n"
      "PARUS_BUILD_API=2\nPARUS_COMPONENT=test.sys.kern.base.state\n"
      "SRCS=test.c\n\n.include <parus.test.mk>\n"));
  written = snprintf(
      manifest, sizeof(manifest),
      "schema_version = 2\n[component]\n"
      "id = \"test.sys.kern.base.state\"\nkind = \"test\"\n"
      "[requires]\ncomponents = []\nkapi = []\n"
      "[provides]\ncapabilities = []\nkapi = []\n"
      "[test]\nowner = \"%s\"\nlane = \"%s\"\n"
      "timeout_ms = %u\nevidence_class = \"%s\"\n",
      owner, lane, timeout_ms, evidence_class);
  CONFIT_TEST_ASSERT(written > 0 && (size_t)written < sizeof(manifest));
  path_join(path, sizeof(path), directory, "component.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, manifest));
}

static void write_runtime_test_component(const char *root, const char *lane,
                                         const char *evidence_class,
                                         const char *receipt_profile) {
  char directory[4096];
  char path[4096];
  char manifest[2048];
  int written;

  path_join(directory, sizeof(directory), root, "sys/qemu-test");
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(directory));
  path_join(path, sizeof(path), directory, "Makefile");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "# Boot runtime profile test는 local C source를 소유하지 않는다.\n"
      "# 실행 tuple과 receipt policy는 component manifest만 소유한다.\n"
      "PARUS_BUILD_API=2\nPARUS_COMPONENT=test.qemu.synthetic.runtime\n"
      "SRCS=\n\n.include <parus.test.mk>\n"));
  written = snprintf(
      manifest, sizeof(manifest),
      "schema_version = 2\n[component]\n"
      "id = \"test.qemu.synthetic.runtime\"\nkind = \"test\"\n"
      "[requires]\ncomponents = []\nkapi = []\n"
      "[provides]\ncapabilities = []\nkapi = []\n"
      "[test]\nowner = \"sys.kern.base\"\nlane = \"%s\"\n"
      "timeout_ms = 30000\nevidence_class = \"%s\"\n"
      "target = \"qemu-virt-aarch64\"\n"
      "machine_profile = \"qemu-arm64-virt-v1\"\n%s",
      lane, evidence_class, receipt_profile != 0 ?
          "receipt_profile = \"runtime-smoke-v1\"\n" : "");
  CONFIT_TEST_ASSERT(written > 0 && (size_t)written < sizeof(manifest));
  path_join(path, sizeof(path), directory, "component.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, manifest));
}

static void expect_test_metadata_security(void) {
  ConfitV2Project *project = 0;
  ConfitComponentCatalog catalog;
  ConfitDiagnostic diagnostic;
  char root[4096] = {0};

  setup_catalog_fixture(root, sizeof(root));
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v2_schema_load_project(root, &project,
                                                    &diagnostic) == CONFIT_OK);

  write_test_component(root, "sys.kern.base", "unit", "host-unit", 5000U);
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog,
                                                    &diagnostic) == CONFIT_OK);
  {
    const ConfitComponent *test = confit_component_catalog_find(
        &catalog, "test.sys.kern.base.state");
    CONFIT_TEST_ASSERT(test != 0);
    CONFIT_TEST_ASSERT(test->kind == CONFIT_COMPONENT_KIND_TEST);
    CONFIT_TEST_ASSERT(strcmp(test->test_owner, "sys.kern.base") == 0);
    CONFIT_TEST_ASSERT(strcmp(test->test_lane, "unit") == 0);
    CONFIT_TEST_ASSERT(strcmp(test->test_evidence_class, "host-unit") == 0);
    CONFIT_TEST_ASSERT(test->test_timeout_ms == 5000U);
  }
  confit_component_catalog_clear(&catalog);

  write_runtime_test_component(root, "qemu", "qemu-runtime",
                               "runtime-smoke-v1");
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog,
                                                    &diagnostic) == CONFIT_OK);
  {
    const ConfitComponent *test = confit_component_catalog_find(
        &catalog, "test.qemu.synthetic.runtime");
    CONFIT_TEST_ASSERT(test != 0);
    CONFIT_TEST_ASSERT(strcmp(test->test_target,
                              "qemu-virt-aarch64") == 0);
    CONFIT_TEST_ASSERT(strcmp(test->test_machine_profile,
                              "qemu-arm64-virt-v1") == 0);
    CONFIT_TEST_ASSERT(strcmp(test->test_receipt_profile,
                              "runtime-smoke-v1") == 0);
    CONFIT_TEST_ASSERT(test->source_count == 0U);
  }
  confit_component_catalog_clear(&catalog);

  write_runtime_test_component(root, "selftest", "booted-selftest",
                               "runtime-smoke-v1");
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog,
                                                    &diagnostic) == CONFIT_OK);
  {
    const ConfitComponent *test = confit_component_catalog_find(
        &catalog, "test.qemu.synthetic.runtime");
    CONFIT_TEST_ASSERT(test != 0);
    CONFIT_TEST_ASSERT(strcmp(test->test_lane, "selftest") == 0);
    CONFIT_TEST_ASSERT(strcmp(test->test_evidence_class,
                              "booted-selftest") == 0);
    CONFIT_TEST_ASSERT(test->source_count == 0U);
  }
  confit_component_catalog_clear(&catalog);

  write_runtime_test_component(root, "qemu", "qemu-runtime", 0);
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog,
                                                    &diagnostic) ==
                     CONFIT_ERR_SCHEMA);
  confit_component_catalog_clear(&catalog);
  write_runtime_test_component(root, "qemu", "qemu-runtime",
                               "runtime-smoke-v1");

  write_test_component(root, "test.sys.kern.base.state", "unit", "host-unit",
                       5000U);
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog,
                                                    &diagnostic) ==
                     CONFIT_ERR_SCHEMA);
  confit_component_catalog_clear(&catalog);

  write_test_component(root, "sys.kern.base", "security", "host-unit", 5000U);
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog,
                                                    &diagnostic) ==
                     CONFIT_ERR_SCHEMA);
  confit_component_catalog_clear(&catalog);

  write_test_component(root, "sys.kern.base", "unit", "host-unit", 0U);
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(confit_component_catalog_load(project, &catalog,
                                                    &diagnostic) ==
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
  expect_make_api_v2_rejection();
  expect_zero_central_sound_driver();
  expect_test_metadata_security();
  return 0;
}
