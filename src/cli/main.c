#include <stdio.h>
#include <string.h>

#include "confit/status.h"
#include "confit/version.h"

#include "v2_workflow.h"

static void confit_cli_print_help(void) {
  (void)fputs(
      "Confit bmake configuration resolver\n\n"
      "Usage: confit <command> [options]\n\n"
      "Commands:\n"
      "  check, resolve, gen, explain, list, graph, diff, component\n\n"
      "`gen` accepts only --artifact bundle and publishes the sealed ABI v3 "
      "bundle.\n"
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
                 CONFIT_ARTIFACT_ABI_V3);
    return 0;
  }
  if (strcmp(argv[1], "doctor") == 0) {
    (void)printf("doctor ok\nengine=bmake\nschema_version=2\nartifact_abi=%s\n",
                 CONFIT_ARTIFACT_ABI_V3);
    return 0;
  }

  command = argv[1];
  exit_code = confit_cli_v2_try_run(command, argc, argv, &handled);
  if (handled != 0) {
    return exit_code;
  }

  (void)fprintf(stderr,
                "confit: unsupported: command or source schema is outside "
                "the bmake artifact ABI v3 contract\n");
  return confit_status_exit_code(CONFIT_ERR_UNSUPPORTED);
}
