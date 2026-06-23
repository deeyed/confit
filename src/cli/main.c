#include <string.h>

#include "confit/host.h"
#include "confit/status.h"
#include "confit/version.h"

static ConfitStatus confit_cli_print_help(void) {
  ConfitStatus status;

  status = confit_host_stdout_write("Confit host configuration tool\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("Usage:\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("  confit --version\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("  confit help\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("Round 1 commands:\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("  help        Show this help text.\n");
  if (status != CONFIT_OK) {
    return status;
  }
  return confit_host_stdout_write("  --version   Show Confit version.\n");
}

int main(int argc, char **argv) {
  ConfitStatus status;

  if (argc <= 1) {
    return confit_status_exit_code(confit_cli_print_help());
  }

  if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "version") == 0) {
    return confit_status_exit_code(
        confit_host_stdout_write_line(confit_version_string()));
  }

  if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0 ||
      strcmp(argv[1], "-h") == 0) {
    return confit_status_exit_code(confit_cli_print_help());
  }

  status = confit_host_stderr_write("confit: unknown command or option: ");
  if (status == CONFIT_OK) {
    status = confit_host_stderr_write(argv[1]);
  }
  if (status == CONFIT_OK) {
    status = confit_host_stderr_write("\n");
  }
  if (status == CONFIT_OK) {
    status = confit_host_stderr_write_line("try 'confit help'");
  }
  if (status != CONFIT_OK) {
    return confit_status_exit_code(status);
  }

  return confit_status_exit_code(CONFIT_ERR_INVALID_ARGUMENT);
}
