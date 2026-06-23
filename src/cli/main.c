#include <stdio.h>
#include <string.h>

#include "confit/status.h"
#include "confit/version.h"

static void confit_cli_print_help(FILE *out) {
  fputs("Confit host configuration tool\n", out);
  fputs("\n", out);
  fputs("Usage:\n", out);
  fputs("  confit --version\n", out);
  fputs("  confit help\n", out);
  fputs("\n", out);
  fputs("Round 1 commands:\n", out);
  fputs("  help        Show this help text.\n", out);
  fputs("  --version   Show Confit version.\n", out);
}

int main(int argc, char **argv) {
  if (argc <= 1) {
    confit_cli_print_help(stdout);
    return confit_status_exit_code(CONFIT_OK);
  }

  if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "version") == 0) {
    puts(confit_version_string());
    return confit_status_exit_code(CONFIT_OK);
  }

  if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0 ||
      strcmp(argv[1], "-h") == 0) {
    confit_cli_print_help(stdout);
    return confit_status_exit_code(CONFIT_OK);
  }

  fprintf(stderr, "confit: unknown command or option: %s\n", argv[1]);
  fputs("try 'confit help'\n", stderr);
  return confit_status_exit_code(CONFIT_ERR_INVALID_ARGUMENT);
}
