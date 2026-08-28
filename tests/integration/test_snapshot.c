#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "confit/config.h"
#include "confit/digest.h"
#include "confit/schema.h"
#include "confit/snapshot.h"

#include "snapshot_internal.h"
#include "test_assert.h"
#include "test_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST_PATH_BYTES 4096U

static const char kEntry[] =
    "schema_version = 6\n"
    "mainmenu = \"Snapshot tests\"\n"
    "source = [\"config/options.toml\"]\n";

static const char kOptions[] =
    "[menu]\n"
    "prompt = \"Options\"\n"
    "help = \"Exercise immutable snapshot publication.\"\n"
    "\n"
    "[[config]]\n"
    "symbol = \"ENABLE_FEATURE\"\n"
    "type = \"bool\"\n"
    "prompt = \"Enable feature\"\n"
    "help = \"Enable one generic feature.\"\n"
    "default = false\n"
    "\n"
    "[[config]]\n"
    "symbol = \"WORKER_COUNT\"\n"
    "type = \"int\"\n"
    "prompt = \"Worker count\"\n"
    "help = \"Set a bounded worker count.\"\n"
    "default = 4\n"
    "range = { min = 1, max = 16 }\n"
    "\n"
    "[[config]]\n"
    "symbol = \"LABEL\"\n"
    "type = \"string\"\n"
    "prompt = \"Label\"\n"
    "help = \"Set a descriptive string.\"\n"
    "default = \"\"\n";

static const char kFirstUser[] =
    "schema_version = 6\n\n"
    "[values]\n"
    "ENABLE_FEATURE = true\n"
    "WORKER_COUNT = 8\n"
    "LABEL = \"line\\nvalue\"\n";

static const char kSecondUser[] =
    "schema_version = 6\n\n"
    "[values]\n"
    "ENABLE_FEATURE = true\n"
    "WORKER_COUNT = 9\n"
    "LABEL = \"line\\nvalue\"\n";

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

static char *read_text(const char *root, const char *relative) {
  char path[TEST_PATH_BYTES];
  join_path(path, root, relative);
  return confit_test_fs_read_file(path);
}

static ConfitHostRoot *open_root(const char *path) {
  ConfitHostRoot *root = 0;
  ConfitDiagnostic diagnostic;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_host_root_open_absolute(path, 0, &root,
                                                    &diagnostic) == CONFIT_OK);
  return root;
}

static ConfitSchemaProject *load_project(ConfitHostRoot *project_root) {
  ConfitSchemaProject *project = 0;
  ConfitDiagnostic diagnostic;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_schema_project_load(
                         project_root, "Confit.toml", 0, &project,
                         &diagnostic) == CONFIT_OK);
  return project;
}

static ConfitUserConfig *load_config(ConfitHostRoot *project_root,
                                     const ConfitSchemaProject *project,
                                     const char *path) {
  ConfitUserConfig *config = 0;
  ConfitDiagnostic diagnostic;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_user_config_load_relative(
                         project_root, path,
                         confit_schema_project_catalog(project), 0, &config,
                         &diagnostic) == CONFIT_OK);
  return config;
}

static ConfitResolution *resolve_config(
    const ConfitSchemaProject *project, const ConfitUserConfig *config) {
  ConfitResolution *resolution = 0;
  ConfitDiagnostic diagnostic;
  const ConfitAssignment *assignments;
  size_t count = 0U;
  confit_diagnostic_init(&diagnostic);
  assignments = confit_user_config_assignments(config, &count);
  CONFIT_TEST_ASSERT(confit_resolve(
                         confit_schema_project_catalog(project),
                         confit_schema_project_dependency_plan(project),
                         assignments, count, 0, &resolution,
                         &diagnostic) == CONFIT_OK);
  return resolution;
}

static void prepare_project(const char *root) {
  make_directory(root, "project/config");
  make_directory(root, "project/users");
  make_directory(root, "project/unrelated");
  make_directory(root, "project/src");
  make_directory(root, "output");
  make_directory(root, "output-concurrent");
  make_directory(root, "output-symlink");
  make_directory(root, "output-corrupt");
  make_directory(root, "relocated/config");
  make_directory(root, "relocated/users");
  write_text(root, "project/Confit.toml", kEntry);
  write_text(root, "project/config/options.toml", kOptions);
  write_text(root, "project/users/first.toml", kFirstUser);
  write_text(root, "project/users/second.toml", kSecondUser);
  write_text(root, "project/unrelated/invalid.toml", "not = [valid\n");
  write_text(root, "project/src/poison.c", "#error unread poison\n");
  write_text(root, "project/Makefile", ".error unread poison\n");
  write_text(root, "relocated/Confit.toml", kEntry);
  write_text(root, "relocated/config/options.toml", kOptions);
  write_text(root, "relocated/users/second.toml", kSecondUser);
}

static void init_request(ConfitSnapshotPublishRequest *request,
                         const ConfitSchemaProject *project,
                         const ConfitUserConfig *config,
                         const ConfitResolution *resolution,
                         int resolved_printable,
                         const ConfitSnapshotArtifactSpec *optional) {
  memset(request, 0, sizeof(*request));
  request->project = project;
  request->user_config = config;
  request->resolution = resolution;
  request->resolved_values_printable = resolved_printable;
  request->optional_artifacts = optional;
  request->optional_artifact_count = optional != 0 ? 1U : 0U;
}

static void expect_selected(const char *output_path, const char *digest) {
  char expected[66];
  char *selected = read_text(output_path, "selected");
  CONFIT_TEST_ASSERT(selected != 0);
  memcpy(expected, digest, 64U);
  expected[64] = '\n';
  expected[65] = '\0';
  CONFIT_TEST_ASSERT(strcmp(selected, expected) == 0);
  confit_test_fs_free(selected);
}

static void verify_selected(ConfitHostRoot *project_root,
                            ConfitHostRoot *output_root,
                            const char *entry,
                            ConfitSnapshotReadLedger *ledger) {
  ConfitSnapshotVerifyRequest request;
  ConfitDiagnostic diagnostic;
  memset(&request, 0, sizeof(request));
  request.project_root = project_root;
  request.output_root = output_root;
  request.expected_entry_path = entry;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_snapshot_verify_observed(
                         &request, 0, ledger, 0, 0U,
                         &diagnostic) == CONFIT_OK);
}

static void verify_artifact(ConfitHostRoot *project_root,
                            ConfitHostRoot *output_root,
                            const char *entry, const char *name,
                            char out_path[TEST_PATH_BYTES]) {
  ConfitSnapshotVerifyRequest request;
  ConfitDiagnostic diagnostic;
  memset(&request, 0, sizeof(request));
  request.project_root = project_root;
  request.output_root = output_root;
  request.expected_entry_path = entry;
  request.artifact_name = name;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_snapshot_verify(
                         &request, 0, out_path, TEST_PATH_BYTES,
                         &diagnostic) == CONFIT_OK);
}

static void test_publication_and_failure(
    const char *root_path, const char *output_path,
    ConfitHostRoot *project_root, ConfitHostRoot *output_root,
    ConfitSchemaProject *project, ConfitUserConfig *first_config,
    ConfitResolution *first_resolution, ConfitUserConfig *second_config,
    ConfitResolution *second_resolution) {
  static const unsigned char optional_bytes[] = "inert optional data\n";
  ConfitSnapshotArtifactSpec optional;
  ConfitSnapshotPublishRequest first_request;
  ConfitSnapshotPublishRequest second_request;
  ConfitSnapshotPublishRequest invalid_request;
  ConfitSnapshotPublication first;
  ConfitSnapshotPublication reuse;
  ConfitSnapshotPublication second;
  ConfitSnapshotPublication failed;
  ConfitSnapshotReadRecord records[3];
  ConfitSnapshotReadLedger ledger;
  ConfitSnapshotVerifyRequest verify;
  ConfitDiagnostic diagnostic;
  ConfitSnapshotFailurePoint failures[] = {
      CONFIT_SNAPSHOT_FAILURE_AFTER_LOCK,
      CONFIT_SNAPSHOT_FAILURE_AFTER_CANDIDATE,
      CONFIT_SNAPSHOT_FAILURE_AFTER_CORE_FILES,
      CONFIT_SNAPSHOT_FAILURE_AFTER_DIRECTORY_PUBLICATION,
      CONFIT_SNAPSHOT_FAILURE_BEFORE_SELECTED,
  };
  char artifact_path[TEST_PATH_BYTES];
  char seal_relative[TEST_PATH_BYTES];
  char seal_absolute[TEST_PATH_BYTES];
  char *seal_text;
  char seal_digest[65];
  size_t index;

  optional.role = "test-data";
  optional.name = "data.bin";
  optional.bytes = optional_bytes;
  optional.size = sizeof(optional_bytes) - 1U;
  optional.printable = 1;
  init_request(&first_request, project, first_config, first_resolution, 0,
               &optional);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_snapshot_publish(
                         output_root, &first_request, 0, &first,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(strlen(first.digest) == 64U && !first.reused_existing);
  expect_selected(output_path, first.digest);
  confit_snapshot_read_ledger_init(&ledger, records, 3U);
  verify_selected(project_root, output_root, "Confit.toml", &ledger);
  CONFIT_TEST_ASSERT(ledger.count == 3U &&
                     strcmp(records[0].path, "Confit.toml") == 0 &&
                     strcmp(records[1].path, "config/options.toml") == 0 &&
                     strcmp(records[2].path, "users/first.toml") == 0);

  verify_artifact(project_root, output_root, "Confit.toml",
                  "user-values.toml", artifact_path);
  CONFIT_TEST_ASSERT(strstr(artifact_path, first.digest) != 0);
  verify_artifact(project_root, output_root, "Confit.toml", "data.bin",
                  artifact_path);
  verify_artifact(project_root, output_root, "Confit.toml", "snapshot.seal",
                  seal_relative);
  join_path(seal_absolute, output_path, seal_relative);
  seal_text = confit_test_fs_read_file(seal_absolute);
  CONFIT_TEST_ASSERT(seal_text != 0);
  confit_sha256_bytes(seal_text, strlen(seal_text), seal_digest);
  CONFIT_TEST_ASSERT(strcmp(seal_digest, first.digest) == 0);
  confit_test_fs_free(seal_text);

  memset(&verify, 0, sizeof(verify));
  verify.project_root = project_root;
  verify.output_root = output_root;
  verify.expected_entry_path = "Confit.toml";
  verify.artifact_name = "resolved-values.json";
  memset(artifact_path, 0x5a, sizeof(artifact_path));
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_snapshot_verify(
                         &verify, 0, artifact_path, sizeof(artifact_path),
                         &diagnostic) == CONFIT_ERR_STALE);
  CONFIT_TEST_ASSERT(artifact_path[0] == '\0');
  verify.artifact_name = "unknown.bin";
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_snapshot_verify(
                         &verify, 0, artifact_path, sizeof(artifact_path),
                         &diagnostic) == CONFIT_ERR_STALE);

  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_snapshot_publish(
                         output_root, &first_request, 0, &reuse,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(reuse.reused_existing &&
                     strcmp(reuse.digest, first.digest) == 0);

  {
    ConfitSnapshotArtifactSpec invalid = optional;
    invalid.name = "user-values.toml";
    init_request(&invalid_request, project, first_config, first_resolution, 0,
                 &invalid);
    confit_diagnostic_init(&diagnostic);
    CONFIT_TEST_ASSERT(confit_snapshot_publish(
                           output_root, &invalid_request, 0, &failed,
                           &diagnostic) == CONFIT_ERR_VALIDATION);
    expect_selected(output_path, first.digest);
  }

  init_request(&second_request, project, second_config, second_resolution, 1,
               &optional);
  for (index = 0U; index < sizeof(failures) / sizeof(failures[0]); ++index) {
    confit_diagnostic_init(&diagnostic);
    CONFIT_TEST_ASSERT(confit_snapshot_publish_with_failure(
                           output_root, &second_request, 0, failures[index],
                           &failed, &diagnostic) == CONFIT_ERR_IO);
    CONFIT_TEST_ASSERT(failed.digest[0] == '\0');
    expect_selected(output_path, first.digest);
  }
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_snapshot_publish(
                         output_root, &second_request, 0, &second,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(strcmp(second.digest, first.digest) != 0 &&
                     second.reused_existing);
  expect_selected(output_path, second.digest);
  verify_artifact(project_root, output_root, "Confit.toml",
                  "resolved-values.json", artifact_path);
  {
    char *json = read_text(output_path, artifact_path);
    CONFIT_TEST_ASSERT(json != 0 && strstr(json, "\\n") != 0 &&
                       strstr(json, "\"WORKER_COUNT\"") != 0 &&
                       strstr(json, "\"value\":9") != 0);
    confit_test_fs_free(json);
  }

  write_text(root_path, "project/unrelated/invalid.toml", "still = [bad\n");
  write_text(root_path, "project/src/poison.c", "#error changed unread\n");
  verify_selected(project_root, output_root, "Confit.toml", 0);
  write_text(root_path, "project/config/options.toml",
             "# changed exact input\n");
  memset(&verify, 0, sizeof(verify));
  verify.project_root = project_root;
  verify.output_root = output_root;
  verify.expected_entry_path = "Confit.toml";
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_snapshot_verify(
                         &verify, 0, 0, 0U,
                         &diagnostic) == CONFIT_ERR_STALE);
  write_text(root_path, "project/config/options.toml", kOptions);
  verify_selected(project_root, output_root, "Confit.toml", 0);
}

static void test_relocation(ConfitHostRoot *relocated_root,
                            ConfitHostRoot *output_root) {
  verify_selected(relocated_root, output_root, "Confit.toml", 0);
}

static void test_concurrent(ConfitHostRoot *project_root,
                            ConfitHostRoot *output_root,
                            const ConfitSnapshotPublishRequest *request) {
  pid_t children[2];
  size_t index;
  int successes = 0;
  for (index = 0U; index < 2U; ++index) {
    children[index] = fork();
    CONFIT_TEST_ASSERT(children[index] >= 0);
    if (children[index] == 0) {
      ConfitSnapshotPublication publication;
      ConfitDiagnostic diagnostic;
      ConfitStatus status;
      confit_diagnostic_init(&diagnostic);
      status = confit_snapshot_publish(output_root, request, 0, &publication,
                                       &diagnostic);
      _exit(status == CONFIT_OK ? 0 : status == CONFIT_ERR_IO ? 5 : 70);
    }
  }
  for (index = 0U; index < 2U; ++index) {
    int status;
    CONFIT_TEST_ASSERT(waitpid(children[index], &status, 0) == children[index]);
    CONFIT_TEST_ASSERT(WIFEXITED(status));
    CONFIT_TEST_ASSERT(WEXITSTATUS(status) == 0 || WEXITSTATUS(status) == 5);
    if (WEXITSTATUS(status) == 0) successes += 1;
  }
  CONFIT_TEST_ASSERT(successes >= 1);
  verify_selected(project_root, output_root, "Confit.toml", 0);
}

static void test_selected_symlink(const char *root_path,
                                  const char *output_path,
                                  ConfitHostRoot *output_root,
                                  const ConfitSnapshotPublishRequest *request) {
  ConfitSnapshotPublication publication;
  ConfitDiagnostic diagnostic;
  char selected_path[TEST_PATH_BYTES];
  char *victim;
  write_text(root_path, "output-symlink/victim", "untouched\n");
  join_path(selected_path, output_path, "selected");
  CONFIT_TEST_ASSERT(symlink("victim", selected_path) == 0);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_snapshot_publish(
                         output_root, request, 0, &publication,
                         &diagnostic) == CONFIT_ERR_IO);
  victim = read_text(output_path, "victim");
  CONFIT_TEST_ASSERT(victim != 0 && strcmp(victim, "untouched\n") == 0);
  confit_test_fs_free(victim);
  CONFIT_TEST_ASSERT(unlink(selected_path) == 0);
  CONFIT_TEST_ASSERT(mkdir(selected_path, 0700) == 0);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_snapshot_publish(
                         output_root, request, 0, &publication,
                         &diagnostic) == CONFIT_ERR_IO);
}

static void test_corrupt_existing(const char *output_path,
                                  ConfitHostRoot *project_root,
                                  ConfitHostRoot *output_root,
                                  const ConfitSnapshotPublishRequest *request) {
  ConfitSnapshotPublication publication;
  ConfitSnapshotPublication retry;
  ConfitSnapshotVerifyRequest verify;
  ConfitDiagnostic diagnostic;
  char directory[TEST_PATH_BYTES];
  char artifact[TEST_PATH_BYTES];
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_snapshot_publish(
                         output_root, request, 0, &publication,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(snprintf(directory, sizeof(directory), "%s/snapshots/%s",
                              output_path, publication.digest) > 0);
  CONFIT_TEST_ASSERT(snprintf(artifact, sizeof(artifact),
                              "%s/user-values.toml", directory) > 0);
  CONFIT_TEST_ASSERT(chmod(directory, 0700) == 0);
  CONFIT_TEST_ASSERT(chmod(artifact, 0600) == 0);
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(artifact, "corrupt\n"));
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_snapshot_publish(
                         output_root, request, 0, &retry,
                         &diagnostic) == CONFIT_ERR_IO);
  expect_selected(output_path, publication.digest);
  memset(&verify, 0, sizeof(verify));
  verify.project_root = project_root;
  verify.output_root = output_root;
  verify.expected_entry_path = "Confit.toml";
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_snapshot_verify(
                         &verify, 0, 0, 0U,
                         &diagnostic) == CONFIT_ERR_STALE);
}

int main(void) {
  char raw_root[TEST_PATH_BYTES];
  char root_path[TEST_PATH_BYTES];
  char project_path[TEST_PATH_BYTES];
  char relocated_path[TEST_PATH_BYTES];
  char output_path[TEST_PATH_BYTES];
  char concurrent_path[TEST_PATH_BYTES];
  char symlink_path[TEST_PATH_BYTES];
  char corrupt_path[TEST_PATH_BYTES];
  ConfitHostRoot *project_root;
  ConfitHostRoot *relocated_root;
  ConfitHostRoot *output_root;
  ConfitHostRoot *concurrent_root;
  ConfitHostRoot *symlink_root;
  ConfitHostRoot *corrupt_root;
  ConfitSchemaProject *project;
  ConfitUserConfig *first_config;
  ConfitUserConfig *second_config;
  ConfitResolution *first_resolution;
  ConfitResolution *second_resolution;
  ConfitSnapshotPublishRequest concurrent_request;

  CONFIT_TEST_ASSERT(confit_test_fs_make_temp_dir(
      raw_root, sizeof(raw_root), "confit-snapshot"));
  CONFIT_TEST_ASSERT(realpath(raw_root, root_path) != 0);
  prepare_project(root_path);
  join_path(project_path, root_path, "project");
  join_path(relocated_path, root_path, "relocated");
  join_path(output_path, root_path, "output");
  join_path(concurrent_path, root_path, "output-concurrent");
  join_path(symlink_path, root_path, "output-symlink");
  join_path(corrupt_path, root_path, "output-corrupt");
  project_root = open_root(project_path);
  relocated_root = open_root(relocated_path);
  output_root = open_root(output_path);
  concurrent_root = open_root(concurrent_path);
  symlink_root = open_root(symlink_path);
  corrupt_root = open_root(corrupt_path);
  project = load_project(project_root);
  first_config = load_config(project_root, project, "users/first.toml");
  second_config = load_config(project_root, project, "users/second.toml");
  first_resolution = resolve_config(project, first_config);
  second_resolution = resolve_config(project, second_config);

  test_publication_and_failure(
      root_path, output_path, project_root, output_root, project, first_config,
      first_resolution, second_config, second_resolution);
  test_relocation(relocated_root, output_root);
  init_request(&concurrent_request, project, second_config, second_resolution,
               1, 0);
  test_concurrent(project_root, concurrent_root, &concurrent_request);
  test_selected_symlink(root_path, symlink_path, symlink_root,
                        &concurrent_request);
  test_corrupt_existing(corrupt_path, project_root, corrupt_root,
                        &concurrent_request);

  confit_resolution_destroy(second_resolution);
  confit_resolution_destroy(first_resolution);
  confit_user_config_destroy(second_config);
  confit_user_config_destroy(first_config);
  confit_schema_project_destroy(project);
  confit_host_root_destroy(corrupt_root);
  confit_host_root_destroy(symlink_root);
  confit_host_root_destroy(concurrent_root);
  confit_host_root_destroy(output_root);
  confit_host_root_destroy(relocated_root);
  confit_host_root_destroy(project_root);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root_path));
  return 0;
}
