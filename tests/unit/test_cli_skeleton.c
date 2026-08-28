#include <string.h>

#include "test_fs.h"
#include "test_process.h"

static int run_command(const char *const *arguments, const char *directory,
                       int expected_exit, const char *stdout_contains,
                       const char *stderr_contains, const char *stdout_absent) {
  ConfitTestProcessResult result = {-1, 0, 0};
  char stdout_path[1024];
  char stderr_path[1024];
  int ok;

  if (!confit_test_fs_path_join(stdout_path, sizeof(stdout_path), directory,
                                "stdout.txt") ||
      !confit_test_fs_path_join(stderr_path, sizeof(stderr_path), directory,
                                "stderr.txt")) {
    return 0;
  }
  confit_test_process_result_clear(&result);
  ok = confit_test_process_run(arguments, 0, stdout_path, stderr_path, &result) &&
       result.exit_code == expected_exit && result.stdout_text != 0 &&
       result.stderr_text != 0 &&
       (stdout_contains == 0 ||
        strstr(result.stdout_text, stdout_contains) != 0) &&
       (stderr_contains == 0 ||
        strstr(result.stderr_text, stderr_contains) != 0) &&
       (stdout_absent == 0 || strstr(result.stdout_text, stdout_absent) == 0);
  confit_test_process_result_clear(&result);
  return ok;
}

int main(int argc, char **argv) {
  char directory[1024];
  char legacy_path[1024];
  const char *help[] = {0, "help", 0};
  const char *version[] = {0, "--version", 0};
  const char *legacy[] = {0, "check", "--root", 0, "--project",
                          "legacy.toml", 0};
  static const char *const public_commands[] = {
      "check",     "configure",    "menuconfig",  "verify",
      "search",    "explain",      "diff",        "listnewconfig",
      "oldconfig", "olddefconfig", "savedefconfig"};
  static const char *const removed_commands[] = {
      "doctor", "build-enter", "why-unavailable", "save-minimal",
      "list-new", "tui"};
  size_t index;

  if (argc != 2 || !confit_test_fs_make_temp_dir(directory, sizeof(directory),
                                                  "confit-cli")) {
    return 1;
  }
  help[0] = argv[1];
  version[0] = argv[1];
  legacy[0] = argv[1];
  legacy[3] = directory;
  if (!confit_test_fs_path_join(legacy_path, sizeof(legacy_path), directory,
                                "legacy.toml") ||
      !confit_test_fs_write_file(legacy_path, "schema_version = 5\n")) {
    (void)confit_test_fs_remove_tree(directory);
    return 2;
  }
  if (!run_command(help, directory, 0, "savedefconfig", 0, "build-enter") ||
      !run_command(help, directory, 0, "menuconfig", 0, "why-unavailable") ||
      !run_command(help, directory, 0, "listnewconfig", 0, "save-minimal") ||
      !run_command(help, directory, 0, "olddefconfig", 0, "list-new") ||
      !run_command(help, directory, 0, "menuconfig", 0, "  tui") ||
      !run_command(version, directory, 0, "schema_contract=6", 0,
                   "artifact_abi") ||
      !run_command(legacy, directory, 2, 0,
                   "schema 6 implementation is unavailable", 0)) {
    (void)confit_test_fs_remove_tree(directory);
    return 3;
  }
  for (index = 0U;
       index < sizeof(public_commands) / sizeof(public_commands[0]); ++index) {
    const char *arguments[] = {argv[1], public_commands[index], 0};
    if (!run_command(arguments, directory, 2, 0,
                     "schema 6 implementation is unavailable", 0)) {
      (void)confit_test_fs_remove_tree(directory);
      return 4;
    }
  }
  for (index = 0U;
       index < sizeof(removed_commands) / sizeof(removed_commands[0]); ++index) {
    const char *arguments[] = {argv[1], removed_commands[index], 0};
    if (!run_command(arguments, directory, 2, 0, "usage error", 0)) {
      (void)confit_test_fs_remove_tree(directory);
      return 5;
    }
  }
  return confit_test_fs_remove_tree(directory) ? 0 : 6;
}
