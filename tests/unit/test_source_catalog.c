#include "test_assert.h"
#include "test_fs.h"

#include <string.h>

#include "confit/host.h"
#include "confit/source_catalog.h"

static void join_path(char *out, size_t out_size, const char *left,
                      const char *right) {
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(out, out_size, left, right));
}

static void make_directory(const char *root, const char *relative) {
  char path[4096];
  join_path(path, sizeof(path), root, relative);
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(path));
}

static void write_file(const char *root, const char *relative,
                       const char *text) {
  char path[4096];
  join_path(path, sizeof(path), root, relative);
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, text));
}

static void canonicalize_temp_root(char *root, size_t root_size) {
  char canonical[4096];
  ConfitDiagnostic diagnostic;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_host_path_canonicalize(
                         canonical, sizeof(canonical), root, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(strlen(canonical) + 1U <= root_size);
  memcpy(root, canonical, strlen(canonical) + 1U);
}

static void project_init(ConfitV2Project *project, char *root,
                         char **nucleus_roots, size_t nucleus_count,
                         char **test_roots, size_t test_count) {
  memset(project, 0, sizeof(*project));
  project->project_root = root;
  project->span.path = root;
  project->nucleus_roots.items = nucleus_roots;
  project->nucleus_roots.count = nucleus_count;
  project->test_roots.items = test_roots;
  project->test_roots.count = test_count;
}

static void expect_valid_catalogs(void) {
  char root[4096];
  char *nucleus_roots[] = {"nucleus"};
  char *test_roots[] = {"tests"};
  ConfitV2Project project;
  ConfitNucleusCatalog nucleus;
  ConfitTestCatalog tests;
  ConfitComponentCatalog components;
  ConfitDiagnostic diagnostic;

  CONFIT_TEST_ASSERT(
      confit_test_fs_make_temp_dir(root, sizeof(root), "confit-source-valid"));
  canonicalize_temp_root(root, sizeof(root));
  make_directory(root, "nucleus/core");
  make_directory(root, "tests/core");
  write_file(root, "nucleus/Makefile",
             "# mandatory hierarchy owner\n"
             "PARUS_MK_API = 3\n"
             "KERN_SUBDIRS += core\n"
             ".include <parus.kernsubdir.mk>\n");
  write_file(root, "nucleus/core/Makefile",
             "# mandatory source owner\n"
             "PARUS_MK_API = 3\n"
             "KERN_UNIT = kern.fixture.core\n"
             "SRCS += core.c\n"
             "PUBLIC_HEADERS += core.h\n"
             ".include <parus.kernunit.mk>\n");
  write_file(root, "nucleus/core/core.c", "int source_core(void) { return 3; }\n");
  write_file(root, "nucleus/core/core.h", "#define SOURCE_CORE 3\n");
  write_file(root, "tests/core/Makefile",
             "# owner-local test record\n"
             "PARUS_MK_API = 3\n"
             "TEST_ID = test.kern.fixture.core\n"
             "TEST_OWNER = kern.fixture.core\n"
             "TEST_LANE = unit\n"
             "TEST_EVIDENCE_CLASS = host-unit\n"
             "TEST_TIMEOUT_MS = 1000\n"
             "TEST_SRCS += suite.c\n"
             ".include <parus.test.mk>\n");
  write_file(root, "tests/core/suite.c", "int main(void) { return 0; }\n");

  project_init(&project, root, nucleus_roots, 1U, test_roots, 1U);
  memset(&components, 0, sizeof(components));
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_nucleus_catalog_load(&project, &nucleus,
                                                 &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(nucleus.unit_count == 1U);
  CONFIT_TEST_ASSERT(strcmp(nucleus.units[0].id, "kern.fixture.core") == 0);
  CONFIT_TEST_ASSERT(nucleus.units[0].source_count == 1U);
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(
      confit_test_catalog_load(&project, &tests, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(tests.test_count == 1U);
  CONFIT_TEST_ASSERT(strcmp(tests.tests[0].owner, "kern.fixture.core") == 0);
  CONFIT_TEST_ASSERT(tests.tests[0].timeout_ms == 1000U);
  CONFIT_TEST_ASSERT(confit_test_catalog_validate_owners(
                         &tests, &nucleus, &components, &diagnostic) ==
                     CONFIT_OK);
  tests.tests[0].owner[0] = 'x';
  CONFIT_TEST_ASSERT(confit_test_catalog_validate_owners(
                         &tests, &nucleus, &components, &diagnostic) ==
                     CONFIT_ERR_SCHEMA);
  confit_test_catalog_clear(&tests);
  confit_nucleus_catalog_clear(&nucleus);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_leaf_failure(const char *prefix, const char *makefile,
                                const char *extra_path,
                                const char *extra_text,
                                const char *expected_message) {
  char root[4096];
  char *nucleus_roots[] = {"nucleus"};
  ConfitV2Project project;
  ConfitNucleusCatalog nucleus;
  ConfitDiagnostic diagnostic;

  CONFIT_TEST_ASSERT(confit_test_fs_make_temp_dir(root, sizeof(root), prefix));
  canonicalize_temp_root(root, sizeof(root));
  make_directory(root, "nucleus");
  write_file(root, "nucleus/Makefile", makefile);
  if (extra_path != 0) write_file(root, extra_path, extra_text);
  project_init(&project, root, nucleus_roots, 1U, 0, 0U);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_nucleus_catalog_load(&project, &nucleus,
                                                 &diagnostic) != CONFIT_OK);
  CONFIT_TEST_ASSERT(diagnostic.message != 0);
  CONFIT_TEST_ASSERT(strcmp(diagnostic.message, expected_message) == 0);
  confit_nucleus_catalog_clear(&nucleus);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_cycle_failure(void) {
  char root[4096];
  char *nucleus_roots[] = {"nucleus"};
  ConfitV2Project project;
  ConfitNucleusCatalog nucleus;
  ConfitDiagnostic diagnostic;

  CONFIT_TEST_ASSERT(
      confit_test_fs_make_temp_dir(root, sizeof(root), "confit-source-cycle"));
  canonicalize_temp_root(root, sizeof(root));
  make_directory(root, "nucleus/a");
  make_directory(root, "nucleus/b");
  write_file(root, "nucleus/Makefile",
             "PARUS_MK_API = 3\nKERN_SUBDIRS += a b\n"
             ".include <parus.kernsubdir.mk>\n");
  write_file(root, "nucleus/a/Makefile",
             "PARUS_MK_API = 3\nKERN_UNIT = kern.a\nSRCS += a.c\n"
             "KERN_USES += kern.b\n.include <parus.kernunit.mk>\n");
  write_file(root, "nucleus/b/Makefile",
             "PARUS_MK_API = 3\nKERN_UNIT = kern.b\nSRCS += b.c\n"
             "KERN_USES += kern.a\n.include <parus.kernunit.mk>\n");
  write_file(root, "nucleus/a/a.c", "int a(void) { return 0; }\n");
  write_file(root, "nucleus/b/b.c", "int b(void) { return 0; }\n");
  project_init(&project, root, nucleus_roots, 1U, 0, 0U);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_nucleus_catalog_load(&project, &nucleus,
                                                 &diagnostic) ==
                     CONFIT_ERR_SCHEMA);
  CONFIT_TEST_ASSERT(diagnostic.message != 0);
  CONFIT_TEST_ASSERT(strcmp(diagnostic.message,
                            "nucleus KERN_USES graph contains a cycle") == 0);
  confit_nucleus_catalog_clear(&nucleus);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

int main(void) {
  expect_valid_catalogs();
  expect_leaf_failure(
      "confit-source-manifest",
      "PARUS_MK_API = 3\nKERN_UNIT = kern.core\nSRCS += core.c\n"
      ".include <parus.kernunit.mk>\n",
      "nucleus/component.toml", "schema_version = 3\n",
      "mandatory nucleus directory must not contain component.toml");
  expect_leaf_failure(
      "confit-source-recipe",
      "PARUS_MK_API = 3\nKERN_UNIT = kern.core\nSRCS += core.c\n"
      "\t@cc core.c\n.include <parus.kernunit.mk>\n",
      "nucleus/core.c", "int core(void) { return 0; }\n",
      "restricted Makefile contains forbidden syntax");
  expect_leaf_failure(
      "confit-source-missing",
      "PARUS_MK_API = 3\nKERN_UNIT = kern.core\nSRCS += missing.c\n"
      ".include <parus.kernunit.mk>\n",
      0, 0, "declared source is missing, symlinked, or outside its owner");
  expect_leaf_failure(
      "confit-source-duplicate",
      "PARUS_MK_API = 3\nKERN_UNIT = kern.core\n"
      "SRCS += core.c core.c\n.include <parus.kernunit.mk>\n",
      "nucleus/core.c", "int core(void) { return 0; }\n",
      "restricted Make list contains a duplicate token");
  expect_leaf_failure(
      "confit-source-extension",
      "PARUS_MK_API = 3\nKERN_UNIT = kern.core\nSRCS += core.c.evil\n"
      ".include <parus.kernunit.mk>\n",
      "nucleus/core.c.evil", "not a C source\n",
      "restricted Make list contains an unsafe token");
  expect_leaf_failure(
      "confit-source-utf8",
      "# malformed UTF-8: \xc0\x80\n"
      "PARUS_MK_API = 3\nKERN_UNIT = kern.core\nSRCS += core.c\n"
      ".include <parus.kernunit.mk>\n",
      "nucleus/core.c", "int core(void) { return 0; }\n",
      "restricted Makefile violates the byte bound");
  expect_cycle_failure();
  return 0;
}
