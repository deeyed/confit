#include <stddef.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "confit/diagnostic.h"
#include "confit/host.h"
#include "confit/status.h"
#include "test_fs.h"

#ifndef CONFIT_TEST_SOURCE_DIR
#define CONFIT_TEST_SOURCE_DIR "."
#endif

#if !defined(_WIN32)
static int confit_test_run_forged_argv0_build_enter(
    const char *executable, const char *root, const char *repository,
    const char *invocation, const char *bmake, const char *compiler) {
  pid_t child;
  int status;
  child = fork();
  if (child < 0) return 0;
  if (child == 0) {
    char *const arguments[] = {
        (char *)"/forged/argv0/confit", (char *)"build-enter",
        (char *)"--root",               (char *)root,
        (char *)"--repository",         (char *)repository,
        (char *)"--invocation",         (char *)invocation,
        (char *)"--bmake",              (char *)bmake,
        (char *)"--compiler",           (char *)compiler,
        0};
    execv(executable, arguments);
    _exit(127);
  }
  while (waitpid(child, &status, 0) < 0) {
    if (errno != EINTR) return 0;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int confit_test_directory_has_exact_entry(const char *directory,
                                                 const char *expected) {
  DIR *stream = opendir(directory);
  struct dirent *entry;
  size_t count = 0U;
  int matched = 0;
  if (stream == 0) return 0;
  while ((entry = readdir(stream)) != 0) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    count += 1U;
    if (strcmp(entry->d_name, expected) == 0) matched = 1;
  }
  return closedir(stream) == 0 && count == 1U && matched;
}
#endif

int main(int argc, char **argv) {
  ConfitDiagnostic diagnostic;
  char fixture_path[512];
  char joined_path[64];
  char expected_path[64];
  char temporary_root[512];
  char temporary_canonical[512];
  char build_root[512];
  char admission_root[512];
  char invocation_root[512];
  char stage0_tool[512];
  char bmake_tool[512];
  char bmake_alias[512];
  char compiler_tool[512];
  char source_repository[512];
  char source_directory[512];
  char source_file[512];
  char self_executable[512];
  char foreign_sentinel[512];
  char root_marker[512];
  char forged_receipt[512];
  char expected_stage0[640];
  char *text;
  size_t text_size;
  char **paths;
  size_t path_count;

  confit_diagnostic_init(&diagnostic);

  if (confit_host_path_separator() == '\0') {
    return 1;
  }
#if defined(_WIN32)
  if (confit_host_path_separator() != '\\') {
    return 19;
  }
#else
  if (confit_host_path_separator() != '/') {
    return 19;
  }
#endif

  if (confit_host_path_join(joined_path, sizeof(joined_path), "left",
                            "right", &diagnostic) != CONFIT_OK) {
    return 2;
  }
  if (strcmp(joined_path, "left/right") != 0 &&
      strcmp(joined_path, "left\\right") != 0) {
    return 3;
  }

  if (confit_host_path_join(joined_path, sizeof(joined_path), "left/", "right",
                            &diagnostic) != CONFIT_OK) {
    return 4;
  }
  if (strcmp(joined_path, "left/right") != 0) {
    return 5;
  }

  if (confit_host_path_join(joined_path, sizeof(joined_path), "left\\",
                            "right", &diagnostic) != CONFIT_OK) {
    return 20;
  }
  if (strcmp(joined_path, "left\\right") != 0) {
    return 21;
  }

  if (confit_host_path_join(joined_path, sizeof(joined_path), "C:\\delos",
                            "config", &diagnostic) != CONFIT_OK) {
    return 22;
  }
  (void)snprintf(expected_path, sizeof(expected_path), "C:\\delos%cconfig",
                 confit_host_path_separator());
  if (strcmp(joined_path, expected_path) != 0) {
    return 23;
  }

  if (confit_host_path_join(joined_path, sizeof(joined_path), "C:\\",
                            "config", &diagnostic) != CONFIT_OK) {
    return 24;
  }
  if (strcmp(joined_path, "C:\\config") != 0) {
    return 25;
  }

  if (confit_host_path_join(joined_path, 4U, "left", "right",
                            &diagnostic) != CONFIT_ERR_INVALID_ARGUMENT) {
    return 6;
  }
  if (!confit_diagnostic_has_error(&diagnostic)) {
    return 7;
  }

  confit_diagnostic_clear(&diagnostic);
  if (confit_host_path_join(fixture_path, sizeof(fixture_path),
                            CONFIT_TEST_SOURCE_DIR,
                            "tests/fixtures/host/read_text.txt",
                            &diagnostic) != CONFIT_OK) {
    return 8;
  }
  if (!confit_host_file_exists(fixture_path) ||
      confit_host_file_exists("") ||
      confit_host_file_exists("tests/fixtures/host/does-not-exist.txt")) {
    return 26;
  }
  if (!confit_host_directory_exists(CONFIT_TEST_SOURCE_DIR) ||
      confit_host_directory_exists(fixture_path) ||
      confit_host_directory_exists("")) {
    return 27;
  }

  text = 0;
  text_size = 0U;
  if (confit_host_read_text_file(fixture_path, &text, &text_size,
                                 &diagnostic) != CONFIT_OK) {
    return 9;
  }
  if (text_size != 20U) {
    confit_host_free(text);
    return 10;
  }
  if (strcmp(text, "confit host fixture\n") != 0) {
    confit_host_free(text);
    return 11;
  }
  confit_host_free(text);

  if (confit_host_read_text_file("", &text, &text_size,
                                 &diagnostic) !=
      CONFIT_ERR_INVALID_ARGUMENT) {
    return 12;
  }

  if (confit_host_path_join(fixture_path, sizeof(fixture_path),
                            CONFIT_TEST_SOURCE_DIR,
                            "tests/fixtures/host/list", &diagnostic) !=
      CONFIT_OK) {
    return 13;
  }
  paths = 0;
  path_count = 0U;
  if (confit_host_list_toml_files(fixture_path, &paths, &path_count,
                                  &diagnostic) != CONFIT_OK) {
    return 14;
  }
  if (path_count != 2U) {
    confit_host_string_list_free(paths, path_count);
    return 15;
  }
  if (strstr(paths[0], "a.toml") == 0 || strstr(paths[1], "b.toml") == 0) {
    confit_host_string_list_free(paths, path_count);
    return 16;
  }
  confit_host_string_list_free(paths, path_count);

  if (confit_host_path_join(fixture_path, sizeof(fixture_path),
                            CONFIT_TEST_SOURCE_DIR,
                            "tests/fixtures/host/missing", &diagnostic) !=
      CONFIT_OK) {
    return 17;
  }
  paths = 0;
  path_count = 99U;
  if (confit_host_list_toml_files(fixture_path, &paths, &path_count,
                                  &diagnostic) != CONFIT_OK ||
      paths != 0 || path_count != 0U) {
    confit_host_string_list_free(paths, path_count);
    return 18;
  }

#if !defined(_WIN32)
  if (argc != 4 ||
      confit_host_self_executable(self_executable, sizeof(self_executable),
                                  &diagnostic) != CONFIT_OK ||
      self_executable[0] != '/' ||
      confit_host_path_canonicalize(stage0_tool, sizeof(stage0_tool), argv[1],
                                    &diagnostic) != CONFIT_OK ||
      confit_host_path_canonicalize(bmake_tool, sizeof(bmake_tool),
                                    argv[2], &diagnostic) != CONFIT_OK ||
      confit_host_path_canonicalize(compiler_tool, sizeof(compiler_tool),
                                    argv[3], &diagnostic) != CONFIT_OK ||
      !confit_test_fs_make_temp_dir(temporary_root, sizeof(temporary_root),
                                    "confit-stage0") ||
      confit_host_path_canonicalize(temporary_canonical,
                                    sizeof(temporary_canonical), temporary_root,
                                    &diagnostic) != CONFIT_OK) {
    return 28;
  }
  if (!confit_test_fs_path_join(source_repository, sizeof(source_repository),
                                temporary_canonical, "repository") ||
      !confit_test_fs_path_join(source_directory, sizeof(source_directory),
                                source_repository, "tools/host/admit") ||
      !confit_test_fs_make_dirs(source_directory) ||
      !confit_test_fs_path_join(source_file, sizeof(source_file),
                                source_directory, "main.c") ||
      confit_host_path_join(fixture_path, sizeof(fixture_path),
                            CONFIT_TEST_SOURCE_DIR,
                            "tests/fixtures/host/admission_stub.c",
                            &diagnostic) != CONFIT_OK ||
      (text = confit_test_fs_read_file(fixture_path)) == 0 ||
      !confit_test_fs_write_file(source_file, text)) {
    confit_test_fs_free(text);
    return 28;
  }
  confit_test_fs_free(text);
  text = 0;
  if (!confit_test_fs_path_join(bmake_alias, sizeof(bmake_alias),
                                temporary_canonical, "bmake-link") ||
      symlink(bmake_tool, bmake_alias) != 0 ||
      !confit_test_fs_path_join(build_root, sizeof(build_root),
                                temporary_canonical,
                                "parus-build") ||
      mkdir(build_root, 0700) != 0 ||
      !confit_test_fs_path_join(foreign_sentinel, sizeof(foreign_sentinel),
                                build_root, "foreign-sentinel") ||
      !confit_test_fs_write_file(foreign_sentinel, "unchanged\n") ||
      confit_host_prepare_parus_build_root(
          build_root, source_repository, "122", stage0_tool, bmake_tool,
          compiler_tool, &diagnostic) !=
          CONFIT_ERR_GENERATION ||
      !confit_test_fs_path_join(root_marker, sizeof(root_marker), build_root,
                                ".parus-root-v1") ||
      confit_host_file_exists(root_marker) ||
      !confit_test_fs_path_join(admission_root, sizeof(admission_root),
                                build_root, ".parus-admission-bootstrap") ||
      confit_host_directory_exists(admission_root) ||
      !confit_test_directory_has_exact_entry(build_root,
                                             "foreign-sentinel") ||
      (text = confit_test_fs_read_file(foreign_sentinel)) == 0 ||
      strcmp(text, "unchanged\n") != 0) {
    confit_test_fs_free(text);
    return 28;
  }
  confit_test_fs_free(text);
  text = 0;
  if (!confit_test_fs_remove_tree(build_root) ||
      confit_host_prepare_parus_build_root(build_root, source_repository,
                                           "123", stage0_tool, bmake_tool,
                                           compiler_tool, &diagnostic) !=
          CONFIT_OK ||
      confit_host_prepare_parus_build_root(build_root, source_repository,
                                           "123", stage0_tool, bmake_tool,
                                           compiler_tool, &diagnostic) !=
          CONFIT_ERR_GENERATION ||
      confit_host_prepare_parus_build_root(build_root, source_repository,
                                           "124", stage0_tool, bmake_alias,
                                           compiler_tool, &diagnostic) !=
          CONFIT_OK ||
      !confit_test_fs_path_join(admission_root, sizeof(admission_root),
                                build_root, ".parus-admission-bootstrap") ||
      !confit_test_fs_path_join(invocation_root, sizeof(invocation_root),
                                admission_root, "123") ||
      !confit_host_directory_exists(invocation_root) ||
      !confit_test_run_forged_argv0_build_enter(
          stage0_tool, build_root, source_repository, "127", bmake_tool,
          compiler_tool) ||
      !confit_test_fs_path_join(forged_receipt, sizeof(forged_receipt),
                                admission_root, "127/.parus-stage0-v1") ||
      (text = confit_test_fs_read_file(forged_receipt)) == 0 ||
      snprintf(expected_stage0, sizeof(expected_stage0), "stage0.path=%s\n",
               stage0_tool) <= 0 ||
      strstr(text, expected_stage0) == 0) {
    confit_test_fs_free(text);
    return 28;
  }
  confit_test_fs_free(text);
  text = 0;
  confit_diagnostic_clear(&diagnostic);
  if (confit_host_prepare_parus_build_root(build_root, source_repository,
                                           "../escape", stage0_tool,
                                           bmake_tool, compiler_tool,
                                           &diagnostic) !=
          CONFIT_ERR_INVALID_ARGUMENT ||
      confit_host_prepare_parus_build_root(build_root, build_root, "125",
                                           stage0_tool, bmake_tool,
                                           compiler_tool, &diagnostic) !=
          CONFIT_ERR_GENERATION ||
      confit_host_prepare_parus_build_root("/tmp/.", source_repository,
                                           "125", stage0_tool, bmake_tool,
                                           compiler_tool, &diagnostic) !=
          CONFIT_ERR_INVALID_ARGUMENT ||
      confit_host_prepare_parus_build_root("/tmp/..", source_repository,
                                           "126", stage0_tool, bmake_tool,
                                           compiler_tool, &diagnostic) !=
          CONFIT_ERR_INVALID_ARGUMENT ||
      chmod(admission_root, 0700) != 0 || chmod(invocation_root, 0700) != 0 ||
      !confit_test_fs_path_join(invocation_root, sizeof(invocation_root),
                                admission_root, "124") ||
      chmod(invocation_root, 0700) != 0 ||
      !confit_test_fs_path_join(invocation_root, sizeof(invocation_root),
                                admission_root, "127") ||
      chmod(invocation_root, 0700) != 0 ||
      !confit_test_fs_remove_tree(temporary_root)) {
    return 29;
  }
#endif

  return 0;
}
