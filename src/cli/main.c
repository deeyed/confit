#include <stdio.h>
#include <string.h>

#include "confit/status.h"
#include "confit/version.h"
#include "confit/diagnostic.h"
#include "confit/host.h"

#include "v2_workflow.h"

static void confit_cli_print_help(void) {
  (void)fputs(
      "Confit bmake configuration resolver\n\n"
      "Usage: confit <command> [options]\n\n"
      "Commands:\n"
      "  check, resolve, explain, list, graph, diff, component\n"
      "  build-enter --root ABSOLUTE --repository ABSOLUTE "
      "--invocation DECIMAL --bmake ABSOLUTE --compiler ABSOLUTE\n\n"
      "Configuration publication is provided only by the schema-v4 "
      "Preview/Cancel/Apply transaction API.\n"
      "Only schema_version = 2 project input is accepted.\n",
      stdout);
}

static int confit_cli_is_help(const char *argument) {
  return strcmp(argument, "help") == 0 || strcmp(argument, "--help") == 0 ||
         strcmp(argument, "-h") == 0;
}

int main(int argc, char **argv) {
  const char *command;
  int handled = 0;
  int exit_code;

  if (argc < 2 || confit_cli_is_help(argv[1])) {
    confit_cli_print_help();
    return 0;
  }
  if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "version") == 0) {
    (void)printf("%s\nartifact_abi=%s\n", confit_version_string(),
                 CONFIT_ARTIFACT_ABI_V4);
    return 0;
  }
  if (strcmp(argv[1], "doctor") == 0) {
    (void)printf("doctor ok\nengine=bmake\nschema_version=2\nartifact_abi=%s\n",
                 CONFIT_ARTIFACT_ABI_V4);
    return 0;
  }
  if (strcmp(argv[1], "build-enter") == 0) {
    ConfitDiagnostic diagnostic;
    ConfitStatus status;
    char self_executable[4096];
    confit_diagnostic_init(&diagnostic);
    if (argc != 12 || strcmp(argv[2], "--root") != 0 ||
        strcmp(argv[4], "--repository") != 0 ||
        strcmp(argv[6], "--invocation") != 0 ||
        strcmp(argv[8], "--bmake") != 0 ||
        strcmp(argv[10], "--compiler") != 0) {
      (void)fprintf(stderr,
                    "confit: build-enter requires --root ABSOLUTE "
                    "--repository ABSOLUTE --invocation DECIMAL "
                    "--bmake ABSOLUTE --compiler ABSOLUTE\n");
      return confit_status_exit_code(CONFIT_ERR_INVALID_ARGUMENT);
    }
    status = confit_host_self_executable(self_executable,
                                         sizeof(self_executable), &diagnostic);
    if (status == CONFIT_OK) {
      status = confit_host_prepare_parus_build_root(
          argv[3], argv[5], argv[7], self_executable, argv[9], argv[11],
          &diagnostic);
    }
    if (status != CONFIT_OK) {
      (void)fprintf(stderr, "confit: build-enter failed: %s\n",
                    diagnostic.message == 0 || diagnostic.message[0] == '\0'
                        ? confit_status_name(status)
                        : diagnostic.message);
    }
    return confit_status_exit_code(status);
  }

  command = argv[1];
  exit_code = confit_cli_v2_try_run(command, argc, argv, &handled);
  if (handled != 0) {
    return exit_code;
  }

  (void)fprintf(stderr,
                "confit: unsupported: command or source schema is outside "
                "the bmake artifact ABI v4 contract\n");
  return confit_status_exit_code(CONFIT_ERR_UNSUPPORTED);
}
