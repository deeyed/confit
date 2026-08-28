#include <stdio.h>
#include <string.h>

#include "confit/status.h"
#include "confit/version.h"

static const char *const kPublicCommands[] = {
    "check",          "configure",    "menuconfig", "verify",
    "search",         "explain",      "diff",       "listnewconfig",
    "oldconfig",      "olddefconfig", "savedefconfig",
};

static void confit_cli_print_help(void) {
  (void)fputs(
      "Confit generic configuration tool (schema 6 development skeleton)\n\n"
      "Usage: confit <command> [options]\n\n"
      "Commands:\n"
      "  help             Show this help text\n"
      "  check            Validate configuration inputs\n"
      "  configure        Resolve and publish configuration data\n"
      "  menuconfig       Edit values in the terminal interface\n"
      "  verify           Verify a selected immutable snapshot\n"
      "  search           Search prompts, help text, and symbols\n"
      "  explain          Explain one symbol\n"
      "  diff             Compare two user configurations\n"
      "  listnewconfig    List newly introduced symbols\n"
      "  oldconfig        Prompt for newly introduced symbols\n"
      "  olddefconfig     Accept defaults for new symbols\n"
      "  savedefconfig    Write only values that differ from defaults\n\n"
      "Only help and --version are implemented in this development skeleton.\n"
      "Configuration commands fail without opening project or source files.\n",
      stdout);
}

static int confit_cli_public_command(const char *argument) {
  size_t index;

  for (index = 0U; index < sizeof(kPublicCommands) / sizeof(kPublicCommands[0]);
       ++index) {
    if (strcmp(argument, kPublicCommands[index]) == 0) {
      return 1;
    }
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "help") == 0) {
    confit_cli_print_help();
    return 0;
  }
  if (argc == 2 && strcmp(argv[1], "--version") == 0) {
    (void)printf("%s\nschema_contract=%d\nschema_implementation=unavailable\n",
                 confit_version_string(), CONFIT_SCHEMA_CONTRACT_VERSION);
    return 0;
  }
  if (argc >= 2 && confit_cli_public_command(argv[1])) {
    (void)fprintf(stderr,
                  "confit: %s: schema 6 implementation is unavailable in "
                  "this development skeleton\n",
                  argv[1]);
    return confit_status_exit_code(CONFIT_ERR_USAGE);
  }

  (void)fputs("confit: usage error; run 'confit help'\n", stderr);
  return confit_status_exit_code(CONFIT_ERR_USAGE);
}
