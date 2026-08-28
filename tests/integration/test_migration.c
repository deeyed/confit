#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#include "confit/migration.h"
#include "confit/schema.h"
#include "confit/snapshot.h"
#include "test_assert.h"
#include "test_fs.h"

#define TEST_PATH_BYTES 2048U

static int write_file(const char *root, const char *relative,
                      const char *text) {
  char path[TEST_PATH_BYTES];
  return confit_test_fs_path_join(path, sizeof(path), root, relative) &&
         confit_test_fs_write_file(path, text);
}

static int write_project(const char *root, int evolved, int reordered) {
  static const char base[] =
      "[menu]\n"
      "prompt = \"Review\"\n"
      "help = \"Review schema evolution.\"\n\n"
      "[[config]]\n"
      "symbol = \"A_BOOL\"\n"
      "type = \"bool\"\n"
      "prompt = \"A bool\"\n"
      "help = \"The original bool help.\"\n"
      "default = false\n\n"
      "[[config]]\n"
      "symbol = \"ENUM_MODE\"\n"
      "type = \"enum\"\n"
      "prompt = \"Enum mode\"\n"
      "help = \"Select the enum mode.\"\n"
      "values = [\"low\", \"high\"]\n"
      "default = \"low\"\n\n"
      "[[config]]\n"
      "symbol = \"RANGE_VALUE\"\n"
      "type = \"int\"\n"
      "prompt = \"Range value\"\n"
      "help = \"Set a bounded integer.\"\n"
      "default = 5\n"
      "range = { min = 1, max = 10 }\n\n"
      "[[config]]\n"
      "symbol = \"REMOVED_VALUE\"\n"
      "type = \"bool\"\n"
      "prompt = \"Removed value\"\n"
      "help = \"This value will be removed.\"\n"
      "default = false\n";
  static const char base_reordered[] =
      "[menu]\n"
      "prompt = \"Review\"\n"
      "help = \"Review schema evolution.\"\n\n"
      "[[config]]\n"
      "symbol = \"REMOVED_VALUE\"\n"
      "type = \"bool\"\n"
      "prompt = \"Removed value\"\n"
      "help = \"This value will be removed.\"\n"
      "default = false\n\n"
      "[[config]]\n"
      "symbol = \"RANGE_VALUE\"\n"
      "type = \"int\"\n"
      "prompt = \"Range value\"\n"
      "help = \"Set a bounded integer.\"\n"
      "default = 5\n"
      "range = { min = 1, max = 10 }\n\n"
      "[[config]]\n"
      "symbol = \"ENUM_MODE\"\n"
      "type = \"enum\"\n"
      "prompt = \"Enum mode\"\n"
      "help = \"Select the enum mode.\"\n"
      "values = [\"low\", \"high\"]\n"
      "default = \"low\"\n\n"
      "[[config]]\n"
      "symbol = \"A_BOOL\"\n"
      "type = \"bool\"\n"
      "prompt = \"A bool\"\n"
      "help = \"The original bool help.\"\n"
      "default = false\n";
  static const char changed[] =
      "[menu]\n"
      "prompt = \"Review\"\n"
      "help = \"Review schema evolution.\"\n\n"
      "[[config]]\n"
      "symbol = \"GATE\"\n"
      "type = \"bool\"\n"
      "prompt = \"Gate\"\n"
      "help = \"A newly added availability gate.\"\n"
      "default = true\n\n"
      "[[config]]\n"
      "symbol = \"A_BOOL\"\n"
      "type = \"bool\"\n"
      "prompt = \"Changed bool prompt\"\n"
      "help = \"Changed bool help.\"\n"
      "default = true\n"
      "depends_on = \"GATE\"\n\n"
      "[[config]]\n"
      "symbol = \"ENUM_MODE\"\n"
      "type = \"enum\"\n"
      "prompt = \"Enum mode\"\n"
      "help = \"Select the enum mode.\"\n"
      "values = [\"low\", \"medium\", \"high\"]\n"
      "default = \"low\"\n\n"
      "[[config]]\n"
      "symbol = \"NEW_STRING\"\n"
      "type = \"string\"\n"
      "prompt = \"New string\"\n"
      "help = \"A genuinely new string value.\"\n"
      "default = \"\"\n\n"
      "[[config]]\n"
      "symbol = \"RANGE_VALUE\"\n"
      "type = \"hex\"\n"
      "prompt = \"Range value\"\n"
      "help = \"Set a bounded hexadecimal value.\"\n"
      "default = 0x5\n"
      "range = { min = 0x1, max = 0x20 }\n";
  return write_file(root, "Confit.toml",
                    "schema_version = 6\n"
                    "mainmenu = \"Migration review\"\n"
                    "source = [\"options.toml\"]\n") &&
         write_file(root, "options.toml",
                    evolved ? changed : (reordered ? base_reordered : base));
}

static void publish_current(ConfitHostRoot *project_root,
                            ConfitHostRoot *output_root) {
  ConfitSchemaProject *project = 0;
  ConfitResolution *resolution = 0;
  ConfitSnapshotPublishRequest request;
  ConfitSnapshotPublication publication;
  ConfitDiagnostic diagnostic;
  memset(&request, 0, sizeof(request));
  memset(&publication, 0, sizeof(publication));
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_schema_project_load(
                         project_root, "Confit.toml", 0, &project,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_resolve(
                         confit_schema_project_catalog(project),
                         confit_schema_project_dependency_plan(project), 0,
                         0U, 0, &resolution, &diagnostic) == CONFIT_OK);
  request.project = project;
  request.resolution = resolution;
  CONFIT_TEST_ASSERT(confit_snapshot_publish(
                         output_root, &request, 0, &publication,
                         &diagnostic) == CONFIT_OK);
  confit_resolution_destroy(resolution);
  confit_schema_project_destroy(project);
}

static unsigned find_changes(const ConfitMigrationReview *review,
                             const char *symbol) {
  size_t index;
  for (index = 0U; index < confit_migration_review_change_count(review);
       ++index) {
    ConfitMigrationChangeView view;
    CONFIT_TEST_ASSERT(
        confit_migration_review_change_at(review, index, &view));
    if (strcmp(view.symbol, symbol) == 0) return view.changes;
  }
  return 0U;
}

static void test_categories(const char *root) {
  char output_path[TEST_PATH_BYTES];
  ConfitHostRoot *project_root = 0;
  ConfitHostRoot *output_root = 0;
  ConfitSchemaProject *current = 0;
  ConfitMigrationReview *review = 0;
  ConfitDiagnostic diagnostic;
  unsigned changes;
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      output_path, sizeof(output_path), root, "output-categories"));
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(output_path));
  CONFIT_TEST_ASSERT(write_project(root, 0, 0));
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_host_root_open_absolute(
                         root, 0, &project_root, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_host_root_open_absolute(
                         output_path, 0, &output_root, &diagnostic) ==
                     CONFIT_OK);
  publish_current(project_root, output_root);
  CONFIT_TEST_ASSERT(write_project(root, 1, 0));
  CONFIT_TEST_ASSERT(confit_schema_project_load(
                         project_root, "Confit.toml", 0, &current,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_migration_review_selected(
                         output_root, confit_schema_project_catalog(current),
                         0, &review, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_migration_review_has_semantic_changes(review));
  changes = find_changes(review, "A_BOOL");
  CONFIT_TEST_ASSERT((changes & CONFIT_MIGRATION_CHANGE_DEFAULT) != 0U);
  CONFIT_TEST_ASSERT((changes & CONFIT_MIGRATION_CHANGE_DEPENDENCY) != 0U);
  CONFIT_TEST_ASSERT((changes & CONFIT_MIGRATION_CHANGE_PROMPT) != 0U);
  CONFIT_TEST_ASSERT((changes & CONFIT_MIGRATION_CHANGE_HELP) != 0U);
  changes = find_changes(review, "ENUM_MODE");
  CONFIT_TEST_ASSERT((changes & CONFIT_MIGRATION_CHANGE_DOMAIN) != 0U);
  changes = find_changes(review, "RANGE_VALUE");
  CONFIT_TEST_ASSERT((changes & CONFIT_MIGRATION_CHANGE_TYPE) != 0U);
  CONFIT_TEST_ASSERT((changes & CONFIT_MIGRATION_CHANGE_DOMAIN) != 0U);
  CONFIT_TEST_ASSERT((find_changes(review, "REMOVED_VALUE") &
                      CONFIT_MIGRATION_CHANGE_REMOVED) != 0U);
  CONFIT_TEST_ASSERT((find_changes(review, "GATE") &
                      CONFIT_MIGRATION_CHANGE_NEW) != 0U);
  CONFIT_TEST_ASSERT((find_changes(review, "NEW_STRING") &
                      CONFIT_MIGRATION_CHANGE_NEW) != 0U);
  confit_migration_review_destroy(review);
  confit_schema_project_destroy(current);
  confit_host_root_destroy(output_root);
  confit_host_root_destroy(project_root);
}

static void test_reorder_and_corruption(const char *root) {
  char output_path[TEST_PATH_BYTES];
  char selected_path[TEST_PATH_BYTES];
  char summary_relative[TEST_PATH_BYTES];
  char summary_path[TEST_PATH_BYTES];
  char *selected;
  char *newline;
  ConfitHostRoot *project_root = 0;
  ConfitHostRoot *output_root = 0;
  ConfitSchemaProject *current = 0;
  ConfitMigrationReview *review = 0;
  ConfitDiagnostic diagnostic;
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      output_path, sizeof(output_path), root, "output-reorder"));
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(output_path));
  CONFIT_TEST_ASSERT(write_project(root, 0, 0));
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_host_root_open_absolute(
                         root, 0, &project_root, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_host_root_open_absolute(
                         output_path, 0, &output_root, &diagnostic) ==
                     CONFIT_OK);
  publish_current(project_root, output_root);
  CONFIT_TEST_ASSERT(write_project(root, 0, 1));
  CONFIT_TEST_ASSERT(confit_schema_project_load(
                         project_root, "Confit.toml", 0, &current,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_migration_review_selected(
                         output_root, confit_schema_project_catalog(current),
                         0, &review, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_migration_review_change_count(review) == 0U);
  confit_migration_review_destroy(review);
  review = 0;
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      selected_path, sizeof(selected_path), output_path, "selected"));
  selected = confit_test_fs_read_file(selected_path);
  CONFIT_TEST_ASSERT(selected != 0);
  newline = strchr(selected, '\n');
  CONFIT_TEST_ASSERT(newline != 0);
  *newline = '\0';
  CONFIT_TEST_ASSERT(snprintf(summary_relative, sizeof(summary_relative),
                              "snapshots/%s/catalog.summary", selected) > 0);
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      summary_path, sizeof(summary_path), output_path, summary_relative));
#if !defined(_WIN32)
  CONFIT_TEST_ASSERT(chmod(summary_path, 0644) == 0);
#endif
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(summary_path, "corrupt\n"));
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_migration_review_selected(
                         output_root, confit_schema_project_catalog(current),
                         0, &review, &diagnostic) == CONFIT_ERR_STALE);
  CONFIT_TEST_ASSERT(review == 0);
  confit_test_fs_free(selected);
  confit_schema_project_destroy(current);
  confit_host_root_destroy(output_root);
  confit_host_root_destroy(project_root);
}

int main(void) {
  char raw_root[TEST_PATH_BYTES];
  char root[TEST_PATH_BYTES];
  if (!confit_test_fs_make_temp_dir(raw_root, sizeof(raw_root),
                                    "confit-migration"))
    return 1;
#if defined(_WIN32)
  if (strlen(raw_root) + 1U > sizeof(root)) return 2;
  memcpy(root, raw_root, strlen(raw_root) + 1U);
#else
  if (realpath(raw_root, root) == 0) return 2;
#endif
  test_categories(root);
  test_reorder_and_corruption(root);
  return confit_test_fs_remove_tree(root) ? 0 : 3;
}
