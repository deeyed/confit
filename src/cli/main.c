#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/compat.h"
#include "confit/diagnostic.h"
#include "confit/explain.h"
#include "confit/generator.h"
#include "confit/graph.h"
#include "confit/host.h"
#include "confit/resolver.h"
#include "confit/schema.h"
#include "confit/status.h"
#include "confit/tui.h"
#include "confit/version.h"

typedef struct ConfitCliProjectArgs {
  const char *project_root;
  const char *profile_name;
  const char *target_name;
} ConfitCliProjectArgs;

typedef struct ConfitCliCheckArgs {
  ConfitCliProjectArgs project;
  int strict;
} ConfitCliCheckArgs;

typedef struct ConfitCliResolveArgs {
  ConfitCliProjectArgs project;
  const char **sets;
  size_t set_count;
  const char *format;
} ConfitCliResolveArgs;

typedef struct ConfitCliGenArgs {
  ConfitCliProjectArgs project;
  const char *out_dir;
  unsigned artifact_mask;
  int artifact_seen;
  int dry_run;
  int force;
} ConfitCliGenArgs;

typedef struct ConfitCliExplainArgs {
  ConfitCliProjectArgs project;
  const char **sets;
  size_t set_count;
  const char *option_id;
} ConfitCliExplainArgs;

typedef struct ConfitCliCompatArgs {
  const char *parus_root;
  const char *delos_root;
  const char *profile_name;
  const char *target_name;
  const char *compat_root;
  const char *format;
} ConfitCliCompatArgs;

typedef struct ConfitCliListArgs {
  const char *project_root;
  const char *category;
  const char *tag;
} ConfitCliListArgs;

typedef struct ConfitCliGraphArgs {
  ConfitCliProjectArgs project;
  const char *format;
} ConfitCliGraphArgs;

typedef struct ConfitCliDiffArgs {
  const char *project_root;
  const char *profile_name;
  const char *base_name;
  const char *target_name;
  const char *format;
} ConfitCliDiffArgs;

typedef struct ConfitCliCompletionArgs {
  const char *shell;
} ConfitCliCompletionArgs;

typedef enum ConfitCliProfileCommand {
  CONFIT_CLI_PROFILE_INVALID = 0,
  CONFIT_CLI_PROFILE_LIST = 1,
  CONFIT_CLI_PROFILE_SHOW = 2,
  CONFIT_CLI_PROFILE_NEW = 3,
  CONFIT_CLI_PROFILE_SET = 4,
  CONFIT_CLI_PROFILE_UNSET = 5,
  CONFIT_CLI_PROFILE_VALIDATE = 6,
} ConfitCliProfileCommand;

typedef struct ConfitCliProfileArgs {
  ConfitCliProfileCommand command;
  const char *project_root;
  const char *profile_name;
  const char *base_name;
  const char *target_name;
  const char *assignment;
  const char *option_id;
  int force;
} ConfitCliProfileArgs;

typedef struct ConfitCliDoctorArgs {
  const char *project_root;
} ConfitCliDoctorArgs;

typedef struct ConfitCliInitArgs {
  const char *project_root;
  const char *template_name;
  int dry_run;
  int force;
} ConfitCliInitArgs;

typedef struct ConfitCliTemplateFile {
  const char *relative_path;
  const char *text;
} ConfitCliTemplateFile;

typedef struct ConfitCliTemplateSpec {
  const char *name;
  const ConfitCliTemplateFile *files;
  size_t file_count;
} ConfitCliTemplateSpec;

typedef struct ConfitCliTextBuilder {
  char *text;
  size_t size;
  size_t capacity;
} ConfitCliTextBuilder;

typedef struct ConfitCliInputFiles {
  ConfitInputFile *items;
  size_t count;
} ConfitCliInputFiles;

typedef struct ConfitCliSha256 {
  uint32_t state[8];
  uint64_t bit_count;
  unsigned char buffer[64];
  size_t buffer_size;
} ConfitCliSha256;

typedef struct ConfitCliGlobalArgs {
  const char *color;
  int quiet;
  int verbose;
  int command_index;
} ConfitCliGlobalArgs;

typedef int (*ConfitCliCommandHandler)(int argc, char **argv);

typedef struct ConfitCliCommandSpec {
  const char *name;
  const char *summary;
  const char *usage;
  const char *options;
  ConfitCliCommandHandler handler;
} ConfitCliCommandSpec;

static int confit_cli_run_doctor(int argc, char **argv);
static int confit_cli_run_init(int argc, char **argv);
static int confit_cli_run_check(int argc, char **argv);
static int confit_cli_run_resolve(int argc, char **argv);
static int confit_cli_run_gen(int argc, char **argv);
static int confit_cli_run_explain(int argc, char **argv);
static int confit_cli_run_compat(int argc, char **argv);
static int confit_cli_run_list(int argc, char **argv);
static int confit_cli_run_graph(int argc, char **argv);
static int confit_cli_run_diff(int argc, char **argv);
static int confit_cli_run_profile(int argc, char **argv);
static int confit_cli_run_completion(int argc, char **argv);
static int confit_cli_run_tui(int argc, char **argv);

static ConfitStatus confit_cli_find_config_root(const char *project_root,
                                                char *out, size_t out_size,
                                                ConfitDiagnostic *diagnostic);
static char *confit_cli_copy_bytes(const char *text, size_t size);

#define CONFIT_CLI_ARTIFACT_HEADER 0x01U
#define CONFIT_CLI_ARTIFACT_REPORTS 0x02U
#define CONFIT_CLI_ARTIFACT_CMAKE 0x04U
#define CONFIT_CLI_ARTIFACT_QSTAR 0x08U
#define CONFIT_CLI_ARTIFACT_ALL                                                \
  (CONFIT_CLI_ARTIFACT_HEADER | CONFIT_CLI_ARTIFACT_REPORTS |                 \
   CONFIT_CLI_ARTIFACT_CMAKE | CONFIT_CLI_ARTIFACT_QSTAR)

static const ConfitCliCommandSpec confit_cli_help_spec = {
    "help", "Show global help or command-specific help.",
    "confit help [command]", "Use `confit help <command>` for details.", 0};

static const ConfitCliCommandSpec confit_cli_commands[] = {
    {"doctor",
     "Check host installation, platform support, project layout, and "
     "generators.",
     "confit doctor [--project <path>]",
     "--project <path>", confit_cli_run_doctor},
    {"init", "Create a Confit project skeleton from a named template.",
     "confit init --project <path> --template minimal|delos|parus [--force] "
     "[--dry-run]",
     "--project <path>\n  --template minimal|delos|parus\n  --force\n  "
     "--dry-run",
     confit_cli_run_init},
    {"check",
     "Parse project TOML, validate schema and graph, and resolve a profile.",
     "confit check --project <path> --profile <name> [--target <name>] "
     "[--strict]",
     "--project <path>\n  --profile <name>\n  --target <name>\n  --strict",
     confit_cli_run_check},
    {"resolve", "Emit the resolved configuration without writing artifacts.",
     "confit resolve --project <path> --profile <name> [--target <name>] "
     "[--set <id=value>] [--format text|json|toml]",
     "--project <path>\n  --profile <name>\n  --target <name>\n  "
     "--set <id=value>\n  --format text|json|toml",
     confit_cli_run_resolve},
    {"gen", "Generate deterministic configuration artifacts.",
     "confit gen --project <path> --profile <name> [--target <name>] --out "
     "<path> [--artifact header|reports|cmake|qstar|all] [--force] "
     "[--dry-run]",
     "--project <path>\n  --profile <name>\n  --target <name>\n  --out <path>"
     "\n  --artifact header|reports|cmake|qstar|all\n  --force\n  --dry-run",
     confit_cli_run_gen},
    {"explain", "Explain one resolved option value.",
     "confit explain --project <path> --profile <name> [--target <name>] "
     "[--set <id=value>] <option-id>",
     "--project <path>\n  --profile <name>\n  --target <name>\n  "
     "--set <id=value>",
     confit_cli_run_explain},
    {"list", "List schema entities.",
     "confit list --project <path> [--kind options|profiles|targets|"
     "categories|tags] [--category <name>] [--tag <name>] [--query <text>] "
     "[--show-hidden]",
     "--project <path>\n  --category <name>\n  --tag <name>",
     confit_cli_run_list},
    {"graph", "Emit the option dependency graph as JSON or DOT.",
     "confit graph --project <path> [--profile <name>] [--target <name>] "
     "[--format json|dot]",
     "--project <path>\n  --profile <name>\n  --target <name>\n  --format "
     "json|dot",
     confit_cli_run_graph},
    {"diff", "Compare resolved configurations.",
     "confit diff --project <path> --profile <name> --base <profile> "
     "[--target <name>] [--format text|json]",
     "--project <path>\n  --profile <name>\n  --base <profile>\n  "
     "--target <name>\n  --format text|json",
     confit_cli_run_diff},
    {"compat", "Check compatibility assertions across project roots.",
     "confit compat --parus <path> --delos <path> --profile <name> "
     "[--target <name>] [--compat <path>] [--format text|json]",
     "--parus <path>\n  --delos <path>\n  --profile <name>\n  --target "
     "<name>\n  --compat <path>\n  --format text|json",
     confit_cli_run_compat},
    {"profile", "Manage profile TOML without opening the TUI.",
     "confit profile list --project <path>\n"
     "confit profile show --project <path> <name>\n"
     "confit profile new --project <path> <name> [--base <profile>] "
     "[--target <target>] [--force]\n"
     "confit profile set --project <path> <name> <option-id=value>\n"
     "confit profile unset --project <path> <name> <option-id>\n"
     "confit profile validate --project <path> <name>",
     "--project <path>\n  --base <profile>\n  --target <target>\n  --force",
     confit_cli_run_profile},
    {"tui", "Open the terminal UI where supported.",
     "confit tui --project <path> --profile <name> [--target <name>]\n"
     "confit tui --project <path> --schema-edit",
     "--project <path>\n  --profile <name>\n  --target <name>\n  "
     "--schema-edit",
     confit_cli_run_tui},
    {"completion", "Emit shell completion text.",
     "confit completion --shell bash|zsh|fish",
     "--shell bash|zsh|fish", confit_cli_run_completion},
};

static const size_t confit_cli_command_count =
    sizeof(confit_cli_commands) / sizeof(confit_cli_commands[0]);

static const char *confit_cli_executable_path = "confit";
static ConfitCliGlobalArgs confit_cli_global_args = {"auto", 0, 0, 1};

static const ConfitCliTemplateFile confit_cli_template_minimal_files[] = {
    {"config/project.toml",
     "[project]\n"
     "name = \"minimal\"\n"
     "version = \"0.1.0\"\n"
     "schema_version = 1\n"
     "imports = [\n"
     "  \"options/core.toml\",\n"
     "]\n"},
    {"config/options/core.toml",
     "[option.\"minimal.enabled\"]\n"
     "type = \"bool\"\n"
     "default = true\n"
     "prompt = \"Enable minimal configuration\"\n"
     "category = \"general\"\n"
     "tags = [\"minimal\"]\n"
     "help = \"Starter option created by confit init.\"\n"
     "\n"
     "[option.\"minimal.output.name\"]\n"
     "type = \"string\"\n"
     "default = \"minimal\"\n"
     "prompt = \"Output name\"\n"
     "category = \"build\"\n"
     "tags = [\"generated\"]\n"
     "help = \"Generated configuration output label.\"\n"},
    {"config/profiles/default.toml",
     "[profile]\n"
     "name = \"default\"\n"
     "schema_version = 1\n"
     "target = \"host\"\n"
     "\n"
     "[values]\n"
     "\"minimal.enabled\" = true\n"
     "\"minimal.output.name\" = \"minimal-host\"\n"},
    {"config/targets/host.toml",
     "[target]\n"
     "name = \"host\"\n"
     "schema_version = 1\n"
     "arch = \"host\"\n"
     "board = \"host\"\n"
     "\n"
     "[target.claim]\n"
     "level = \"development\"\n"
     "\n"
     "[values]\n"
     "\"minimal.output.name\" = \"minimal-host\"\n"}};

static const ConfitCliTemplateFile confit_cli_template_delos_files[] = {
    {"config/project.toml",
     "[project]\n"
     "name = \"delos\"\n"
     "version = \"0.1.0\"\n"
     "schema_version = 1\n"
     "imports = [\n"
     "  \"options/core.toml\",\n"
     "]\n"},
    {"config/options/core.toml",
     "[option.\"delos.dcg.enabled\"]\n"
     "type = \"bool\"\n"
     "default = false\n"
     "prompt = \"Enable Delos DCG\"\n"
     "category = \"runtime\"\n"
     "tags = [\"delos\", \"dcg\"]\n"
     "help = \"Starter switch for the Delos DCG path.\"\n"
     "\n"
     "[option.\"delos.debug.ddc\"]\n"
     "type = \"bool\"\n"
     "default = false\n"
     "prompt = \"Enable Delos Debug Console\"\n"
     "category = \"debug\"\n"
     "tags = [\"debug\"]\n"
     "help = \"Include the development DDC command parser.\"\n"
     "\n"
     "[option.\"delos.debug.dsh\"]\n"
     "type = \"bool\"\n"
     "default = false\n"
     "prompt = \"Enable Delos Shell\"\n"
     "category = \"debug\"\n"
     "tags = [\"debug\", \"shell\"]\n"
     "requires = [\"delos.debug.ddc\"]\n"
     "help = \"Include the development DSH command surface.\"\n"
     "\n"
     "[option.\"delos.output.name\"]\n"
     "type = \"string\"\n"
     "default = \"delos\"\n"
     "prompt = \"Delos output name\"\n"
     "category = \"build\"\n"
     "tags = [\"generated\"]\n"
     "help = \"Generated Delos configuration output label.\"\n"},
    {"config/profiles/default.toml",
     "[profile]\n"
     "name = \"default\"\n"
     "schema_version = 1\n"
     "target = \"host\"\n"
     "\n"
     "[values]\n"
     "\"delos.dcg.enabled\" = true\n"
     "\"delos.output.name\" = \"delos-host\"\n"},
    {"config/targets/host.toml",
     "[target]\n"
     "name = \"host\"\n"
     "schema_version = 1\n"
     "arch = \"host\"\n"
     "board = \"host\"\n"
     "\n"
     "[target.claim]\n"
     "level = \"portability-probe\"\n"
     "\n"
     "[values]\n"
     "\"delos.output.name\" = \"delos-host\"\n"}};

static const ConfitCliTemplateFile confit_cli_template_parus_files[] = {
    {"config/project.toml",
     "[project]\n"
     "name = \"parus\"\n"
     "version = \"0.1.0\"\n"
     "schema_version = 1\n"
     "imports = [\n"
     "  \"options/core.toml\",\n"
     "]\n"},
    {"config/options/core.toml",
     "[option.\"parus.rt_executor.delos\"]\n"
     "type = \"bool\"\n"
     "default = false\n"
     "prompt = \"Route RT executor through Delos\"\n"
     "category = \"runtime\"\n"
     "tags = [\"parus\", \"delos\"]\n"
     "help = \"Starter switch for a Parus and Delos compatibility path.\"\n"
     "\n"
     "[option.\"parus.debug.release\"]\n"
     "type = \"bool\"\n"
     "default = false\n"
     "prompt = \"Use release debug boundary\"\n"
     "category = \"debug\"\n"
     "tags = [\"release\", \"debug\"]\n"
     "help = \"Starter option for release/debug compatibility checks.\"\n"
     "\n"
     "[option.\"parus.output.name\"]\n"
     "type = \"string\"\n"
     "default = \"parus\"\n"
     "prompt = \"Parus output name\"\n"
     "category = \"build\"\n"
     "tags = [\"generated\"]\n"
     "help = \"Generated Parus configuration output label.\"\n"},
    {"config/profiles/default.toml",
     "[profile]\n"
     "name = \"default\"\n"
     "schema_version = 1\n"
     "target = \"host\"\n"
     "\n"
     "[values]\n"
     "\"parus.rt_executor.delos\" = false\n"
     "\"parus.output.name\" = \"parus-host\"\n"},
    {"config/targets/host.toml",
     "[target]\n"
     "name = \"host\"\n"
     "schema_version = 1\n"
     "arch = \"host\"\n"
     "board = \"host\"\n"
     "\n"
     "[target.claim]\n"
     "level = \"portability-probe\"\n"
     "\n"
     "[values]\n"
     "\"parus.output.name\" = \"parus-host\"\n"}};

static const ConfitCliTemplateSpec confit_cli_templates[] = {
    {"minimal", confit_cli_template_minimal_files,
     sizeof(confit_cli_template_minimal_files) /
         sizeof(confit_cli_template_minimal_files[0])},
    {"delos", confit_cli_template_delos_files,
     sizeof(confit_cli_template_delos_files) /
         sizeof(confit_cli_template_delos_files[0])},
    {"parus", confit_cli_template_parus_files,
     sizeof(confit_cli_template_parus_files) /
         sizeof(confit_cli_template_parus_files[0])}};

static const size_t confit_cli_template_count =
    sizeof(confit_cli_templates) / sizeof(confit_cli_templates[0]);

static const char *const confit_cli_init_directories[] = {
    "config", "config/options", "config/profiles", "config/targets",
    "config/compat"};

static const size_t confit_cli_init_directory_count =
    sizeof(confit_cli_init_directories) /
    sizeof(confit_cli_init_directories[0]);

static ConfitStatus confit_cli_write_error(const char *message) {
  ConfitStatus status;

  status = confit_host_stderr_write("confit: ");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stderr_write_line(message);
  if (status != CONFIT_OK) {
    return status;
  }
  return CONFIT_ERR_INVALID_ARGUMENT;
}

static int confit_cli_return_error(ConfitStatus status,
                                   const ConfitDiagnostic *diagnostic) {
  ConfitStatus write_status;

  write_status = confit_host_stderr_write("confit: ");
  if (write_status == CONFIT_OK && diagnostic != 0 &&
      diagnostic->path != 0) {
    write_status = confit_host_stderr_write(diagnostic->path);
    if (write_status == CONFIT_OK) {
      write_status = confit_host_stderr_write(": ");
    }
  }
  if (write_status == CONFIT_OK) {
    write_status = confit_host_stderr_write(confit_status_name(status));
  }
  if (write_status == CONFIT_OK && diagnostic != 0 &&
      diagnostic->message != 0) {
    write_status = confit_host_stderr_write(": ");
  }
  if (write_status == CONFIT_OK && diagnostic != 0 &&
      diagnostic->message != 0) {
    write_status = confit_host_stderr_write(diagnostic->message);
  }
  if (write_status == CONFIT_OK) {
    write_status = confit_host_stderr_write("\n");
  }
  if (write_status != CONFIT_OK) {
    return confit_status_exit_code(write_status);
  }
  return confit_status_exit_code(status);
}

static const ConfitCliCommandSpec *confit_cli_find_command(const char *name) {
  size_t index;

  for (index = 0U; index < confit_cli_command_count; ++index) {
    if (strcmp(confit_cli_commands[index].name, name) == 0) {
      return &confit_cli_commands[index];
    }
  }
  return 0;
}

static int confit_cli_is_help_arg(const char *arg) {
  return strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0 ||
         strcmp(arg, "help") == 0;
}

static ConfitStatus confit_cli_print_indented_lines(const char *text,
                                                    const char *indent) {
  ConfitStatus status;
  size_t begin;
  size_t index;

  if (text == 0) {
    return CONFIT_OK;
  }

  begin = 0U;
  status = CONFIT_OK;
  while (status == CONFIT_OK) {
    index = begin;
    while (text[index] != '\0' && text[index] != '\n') {
      index += 1U;
    }
    status = confit_host_stdout_write(
        index > begin && isspace((unsigned char)text[begin]) ? "" : indent);
    if (status == CONFIT_OK) {
      char *line = confit_cli_copy_bytes(text + begin, index - begin);
      if (line == 0) {
        return CONFIT_ERR_INTERNAL;
      }
      status = confit_host_stdout_write_line(line);
      free(line);
    }
    if (text[index] == '\0') {
      break;
    }
    begin = index + 1U;
  }
  return status;
}

static ConfitStatus confit_cli_print_command_help(
    const ConfitCliCommandSpec *command) {
  ConfitStatus status;

  if (command == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status = confit_host_stdout_write("Confit command: ");
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line(command->name);
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write("\nSummary:\n  ");
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line(command->summary);
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write("\nUsage:\n");
  }
  if (status == CONFIT_OK) {
    status = confit_cli_print_indented_lines(command->usage, "  ");
  }
  if (status == CONFIT_OK && command->options != 0) {
    status = confit_host_stdout_write("\nOptions:\n");
  }
  if (status == CONFIT_OK && command->options != 0) {
    status = confit_cli_print_indented_lines(command->options, "  ");
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write("\nGlobal options:\n");
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line(
        "  --color auto|always|never");
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line("  --quiet");
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line("  --verbose");
  }
  if (status == CONFIT_OK && command->handler == 0) {
    status = confit_host_stdout_write("\nStatus:\n  ");
  }
  if (status == CONFIT_OK && command->handler == 0) {
    status = confit_host_stdout_write_line(
        "Command is recognized but not implemented in this build.");
  }
  return status;
}

static ConfitStatus confit_cli_write_spaces(size_t count) {
  ConfitStatus status;
  size_t index;

  status = CONFIT_OK;
  for (index = 0U; status == CONFIT_OK && index < count; ++index) {
    status = confit_host_stdout_write(" ");
  }
  return status;
}

static ConfitStatus confit_cli_print_help(void) {
  ConfitStatus status;
  size_t index;

  status = confit_host_stdout_write("Confit host configuration tool\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("\nUsage:\n");
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
  status = confit_host_stdout_write("  confit help <command>\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("  confit <command> [options]\n");
  if (status != CONFIT_OK) {
    return status;
  }

  status = confit_host_stdout_write("\nCommands:\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("  help        ");
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line(confit_cli_help_spec.summary);
  }
  for (index = 0U; status == CONFIT_OK && index < confit_cli_command_count;
       ++index) {
    status = confit_host_stdout_write("  ");
    if (status == CONFIT_OK) {
      status = confit_host_stdout_write(confit_cli_commands[index].name);
    }
    if (status == CONFIT_OK &&
        strlen(confit_cli_commands[index].name) < 10U) {
      status =
          confit_cli_write_spaces(10U - strlen(confit_cli_commands[index].name));
    }
    if (status == CONFIT_OK) {
      status = confit_host_stdout_write("  ");
    }
    if (status == CONFIT_OK) {
      status = confit_host_stdout_write_line(
          confit_cli_commands[index].summary);
    }
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write("\nGlobal options:\n");
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line("  --help                  Show help.");
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line("  --version               Show Confit version.");
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line("  --color auto|always|never");
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line("  --quiet");
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line("  --verbose");
  }
  return status;
}

static ConfitStatus confit_cli_parse_global_args(int argc, char **argv,
                                                 ConfitCliGlobalArgs *args) {
  int index;

  args->color = "auto";
  args->quiet = 0;
  args->verbose = 0;
  args->command_index = 1;

  for (index = 1; index < argc; ++index) {
    const char *arg = argv[index];

    if (strcmp(arg, "--color") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --color");
      }
      index += 1;
      if (strcmp(argv[index], "auto") != 0 &&
          strcmp(argv[index], "always") != 0 &&
          strcmp(argv[index], "never") != 0) {
        return confit_cli_write_error(
            "--color must be auto, always, or never");
      }
      args->color = argv[index];
      args->command_index = index + 1;
      continue;
    }
    if (strcmp(arg, "--quiet") == 0) {
      args->quiet = 1;
      args->command_index = index + 1;
      continue;
    }
    if (strcmp(arg, "--verbose") == 0) {
      args->verbose = 1;
      args->command_index = index + 1;
      continue;
    }
    break;
  }

  if (args->quiet && args->verbose) {
    return confit_cli_write_error("--quiet and --verbose cannot be combined");
  }
  return CONFIT_OK;
}

static void confit_cli_global_args_init(ConfitCliGlobalArgs *args) {
  args->color = "auto";
  args->quiet = 0;
  args->verbose = 0;
  args->command_index = 1;
}

static ConfitStatus confit_cli_parse_global_option(
    int argc, char **argv, int *index, ConfitCliGlobalArgs *args,
    int *out_consumed) {
  const char *arg;

  *out_consumed = 0;
  arg = argv[*index];
  if (strcmp(arg, "--color") == 0) {
    if (*index + 1 >= argc) {
      return confit_cli_write_error("missing value for --color");
    }
    *index += 1;
    if (strcmp(argv[*index], "auto") != 0 &&
        strcmp(argv[*index], "always") != 0 &&
        strcmp(argv[*index], "never") != 0) {
      return confit_cli_write_error("--color must be auto, always, or never");
    }
    args->color = argv[*index];
    *out_consumed = 1;
    return CONFIT_OK;
  }
  if (strcmp(arg, "--quiet") == 0) {
    args->quiet = 1;
    *out_consumed = 1;
    return CONFIT_OK;
  }
  if (strcmp(arg, "--verbose") == 0) {
    args->verbose = 1;
    *out_consumed = 1;
    return CONFIT_OK;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_cli_normalize_args(int argc, char **argv,
                                              ConfitCliGlobalArgs *args,
                                              int *out_argc,
                                              char ***out_argv) {
  char **normalized;
  int normalized_argc;
  int index;
  ConfitStatus status;

  status = confit_cli_parse_global_args(argc, argv, args);
  if (status != CONFIT_OK) {
    return status;
  }

  confit_cli_global_args_init(args);
  normalized = (char **)malloc(((size_t)argc + 1U) * sizeof(normalized[0]));
  if (normalized == 0) {
    return CONFIT_ERR_INTERNAL;
  }

  normalized_argc = 0;
  if (argc > 0) {
    normalized[normalized_argc] = argv[0];
    normalized_argc += 1;
  }
  for (index = 1; index < argc; ++index) {
    int consumed;

    consumed = 0;
    status =
        confit_cli_parse_global_option(argc, argv, &index, args, &consumed);
    if (status != CONFIT_OK) {
      free(normalized);
      return status;
    }
    if (consumed) {
      continue;
    }
    normalized[normalized_argc] = argv[index];
    normalized_argc += 1;
  }

  if (args->quiet && args->verbose) {
    free(normalized);
    return confit_cli_write_error("--quiet and --verbose cannot be combined");
  }

  normalized[normalized_argc] = 0;
  args->command_index = 1;
  *out_argc = normalized_argc;
  *out_argv = normalized;
  return CONFIT_OK;
}

static int confit_cli_is_quiet(void) { return confit_cli_global_args.quiet; }

static int confit_cli_is_verbose(void) {
  return confit_cli_global_args.verbose;
}

static ConfitStatus confit_cli_print_verbose_project(
    const char *command, const ConfitCliProjectArgs *project) {
  ConfitStatus status;

  if (!confit_cli_is_verbose()) {
    return CONFIT_OK;
  }

  status = confit_host_stderr_write("confit: verbose: command=");
  if (status == CONFIT_OK) {
    status = confit_host_stderr_write(command);
  }
  if (status == CONFIT_OK && project != 0 && project->project_root != 0) {
    status = confit_host_stderr_write(" project=");
    if (status == CONFIT_OK) {
      status = confit_host_stderr_write(project->project_root);
    }
  }
  if (status == CONFIT_OK && project != 0 && project->profile_name != 0) {
    status = confit_host_stderr_write(" profile=");
    if (status == CONFIT_OK) {
      status = confit_host_stderr_write(project->profile_name);
    }
  }
  if (status == CONFIT_OK && project != 0 && project->target_name != 0) {
    status = confit_host_stderr_write(" target=");
    if (status == CONFIT_OK) {
      status = confit_host_stderr_write(project->target_name);
    }
  }
  if (status == CONFIT_OK) {
    status = confit_host_stderr_write("\n");
  }
  return status;
}

static int confit_cli_run_help(int argc, char **argv) {
  const ConfitCliCommandSpec *command;
  ConfitStatus status;

  if (argc == 2) {
    return confit_status_exit_code(confit_cli_print_help());
  }
  if (argc == 3 && confit_cli_is_help_arg(argv[2])) {
    return confit_status_exit_code(
        confit_cli_print_command_help(&confit_cli_help_spec));
  }
  if (argc == 3) {
    command = strcmp(argv[2], "help") == 0 ? &confit_cli_help_spec
                                            : confit_cli_find_command(argv[2]);
    if (command == 0) {
      status = confit_host_stderr_write("confit: unknown help command: ");
      if (status == CONFIT_OK) {
        status = confit_host_stderr_write_line(argv[2]);
      }
      if (status == CONFIT_OK) {
        status = confit_host_stderr_write_line("try 'confit help'");
      }
      if (status != CONFIT_OK) {
        return confit_status_exit_code(status);
      }
      return confit_status_exit_code(CONFIT_ERR_INVALID_ARGUMENT);
    }
    return confit_status_exit_code(confit_cli_print_command_help(command));
  }
  return confit_status_exit_code(
      confit_cli_write_error("help accepts at most one command name"));
}

static int confit_cli_run_unsupported_command(
    const ConfitCliCommandSpec *command) {
  ConfitStatus status;

  status = confit_host_stderr_write("confit: unsupported command: ");
  if (status == CONFIT_OK) {
    status = confit_host_stderr_write_line(command->name);
  }
  if (status == CONFIT_OK) {
    status = confit_host_stderr_write("try 'confit help ");
  }
  if (status == CONFIT_OK) {
    status = confit_host_stderr_write(command->name);
  }
  if (status == CONFIT_OK) {
    status = confit_host_stderr_write_line("'");
  }
  if (status != CONFIT_OK) {
    return confit_status_exit_code(status);
  }
  return confit_status_exit_code(CONFIT_ERR_UNSUPPORTED);
}

#ifndef CONFIT_BUILD_SYSTEM_NAME
#define CONFIT_BUILD_SYSTEM_NAME "unknown"
#endif

#ifndef CONFIT_BUILD_C_COMPILER_ID
#define CONFIT_BUILD_C_COMPILER_ID "unknown"
#endif

#ifndef CONFIT_BUILD_C_COMPILER_VERSION
#define CONFIT_BUILD_C_COMPILER_VERSION "unknown"
#endif

#ifndef CONFIT_BUILD_HAS_CURSES
#define CONFIT_BUILD_HAS_CURSES 1
#endif

static ConfitStatus confit_cli_write_doctor_kv(const char *key,
                                               const char *value) {
  ConfitStatus status;

  status = confit_host_stdout_write("  ");
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write(key);
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write(": ");
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line(value);
  }
  return status;
}

static ConfitStatus confit_cli_write_doctor_size(const char *key,
                                                 size_t value) {
  char buffer[64];

  (void)snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)value);
  return confit_cli_write_doctor_kv(key, buffer);
}

static const char *confit_cli_doctor_platform_note(void) {
#if defined(_WIN32)
  return "windows clang-only CLI lane; TUI unsupported";
#elif defined(__APPLE__)
  return "macOS CLI and curses TUI lane";
#elif defined(__linux__)
  return "Linux CLI and curses TUI lane";
#else
  return "portable CLI lane; platform is not release-gated yet";
#endif
}

static const char *confit_cli_doctor_install_rule(void) {
#if defined(_WIN32)
  return "single executable artifact: <prefix>/bin/confit.exe";
#else
  return "single executable artifact: <prefix>/bin/confit";
#endif
}

static ConfitStatus confit_cli_parse_doctor_args(int argc, char **argv,
                                                 ConfitCliDoctorArgs *args) {
  int index;

  args->project_root = 0;
  for (index = 2; index < argc; ++index) {
    const char *arg = argv[index];

    if (strcmp(arg, "--project") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --project");
      }
      index += 1;
      args->project_root = argv[index];
      continue;
    }
    if (arg[0] == '-') {
      return confit_cli_write_error("unknown doctor option");
    }
    return confit_cli_write_error("doctor does not accept positional arguments");
  }
  return CONFIT_OK;
}

static int confit_cli_run_doctor(int argc, char **argv) {
  ConfitCliDoctorArgs args;
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitStatus status;

  status = confit_cli_parse_doctor_args(argc, argv, &args);
  if (status != CONFIT_OK) {
    return confit_status_exit_code(status);
  }

  status = confit_host_stdout_write_line("Confit doctor");
  if (status == CONFIT_OK) {
    status = confit_cli_write_doctor_kv("version", confit_version_string());
  }
  if (status == CONFIT_OK) {
    status = confit_cli_write_doctor_kv("executable",
                                        confit_cli_executable_path);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_write_doctor_kv("platform",
                                        CONFIT_BUILD_SYSTEM_NAME);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_write_doctor_kv("platform note",
                                        confit_cli_doctor_platform_note());
  }
  if (status == CONFIT_OK) {
    status = confit_cli_write_doctor_kv("compiler",
                                        CONFIT_BUILD_C_COMPILER_ID " "
                                        CONFIT_BUILD_C_COMPILER_VERSION);
  }
  if (status == CONFIT_OK) {
#if CONFIT_BUILD_HAS_CURSES
    status = confit_cli_write_doctor_kv("curses", "available; TUI enabled");
#else
    status = confit_cli_write_doctor_kv("curses",
                                        "not available; TUI unsupported");
#endif
  }
  if (status == CONFIT_OK) {
    status = confit_cli_write_doctor_kv("install rule",
                                        confit_cli_doctor_install_rule());
  }
  if (status == CONFIT_OK) {
    status = confit_cli_write_doctor_kv(
        "generators", "header, reports, cmake, qstar manifest");
  }
  if (status == CONFIT_OK) {
    status = confit_cli_write_doctor_kv(
        "deferred generators", "none in this build");
  }

  if (status != CONFIT_OK) {
    return confit_status_exit_code(status);
  }

  if (args.project_root == 0) {
    status = confit_cli_write_doctor_kv("project", "not checked");
    if (status == CONFIT_OK) {
      status = confit_host_stdout_write_line("doctor ok");
    }
    return confit_status_exit_code(status);
  }

  confit_diagnostic_init(&diagnostic);
  project = 0;
  status = confit_schema_load_project(args.project_root, &project, &diagnostic);
  if (status != CONFIT_OK) {
    return confit_cli_return_error(status, &diagnostic);
  }

  status = confit_cli_write_doctor_kv("project", args.project_root);
  if (status == CONFIT_OK) {
    status = confit_cli_write_doctor_size("options", project->option_count);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_write_doctor_size("profiles", project->profile_count);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_write_doctor_size("targets", project->target_count);
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line("doctor ok");
  }

  confit_project_free(project);
  return confit_status_exit_code(status);
}

static const ConfitCliTemplateSpec *confit_cli_find_template(
    const char *name) {
  size_t index;

  for (index = 0U; index < confit_cli_template_count; ++index) {
    if (strcmp(confit_cli_templates[index].name, name) == 0) {
      return &confit_cli_templates[index];
    }
  }
  return 0;
}

static ConfitStatus confit_cli_parse_init_args(int argc, char **argv,
                                               ConfitCliInitArgs *args) {
  int index;

  args->project_root = 0;
  args->template_name = 0;
  args->dry_run = 0;
  args->force = 0;

  for (index = 2; index < argc; ++index) {
    const char *arg = argv[index];

    if (strcmp(arg, "--project") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --project");
      }
      index += 1;
      args->project_root = argv[index];
      continue;
    }
    if (strcmp(arg, "--template") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --template");
      }
      index += 1;
      args->template_name = argv[index];
      continue;
    }
    if (strcmp(arg, "--dry-run") == 0) {
      args->dry_run = 1;
      continue;
    }
    if (strcmp(arg, "--force") == 0) {
      args->force = 1;
      continue;
    }
    if (arg[0] == '-') {
      return confit_cli_write_error("unknown init option");
    }
    return confit_cli_write_error("init does not accept positional arguments");
  }

  if (args->project_root == 0) {
    return confit_cli_write_error("init requires --project");
  }
  if (args->template_name == 0) {
    return confit_cli_write_error("init requires --template");
  }
  if (confit_cli_find_template(args->template_name) == 0) {
    return confit_cli_write_error(
        "init --template must be minimal, delos, or parus");
  }
  return CONFIT_OK;
}

static ConfitStatus confit_cli_join_relative_path(char *out, size_t out_size,
                                                  const char *root,
                                                  const char *relative,
                                                  ConfitDiagnostic *diagnostic) {
  char current[1024];
  char segment[256];
  size_t relative_index;
  size_t segment_size;
  ConfitStatus status;

  if (root == 0 || relative == 0 || out == 0 || out_size == 0U) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid relative path join argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  if (strlen(root) + 1U > sizeof(current)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, root, 0, 0,
                          "project path is too long");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  memcpy(current, root, strlen(root) + 1U);

  relative_index = 0U;
  while (relative[relative_index] != '\0') {
    segment_size = 0U;
    while (relative[relative_index] != '\0' &&
           relative[relative_index] != '/') {
      if (segment_size + 1U >= sizeof(segment)) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, relative,
                              0, 0, "relative path segment is too long");
        return CONFIT_ERR_INVALID_ARGUMENT;
      }
      segment[segment_size] = relative[relative_index];
      segment_size += 1U;
      relative_index += 1U;
    }
    segment[segment_size] = '\0';
    if (relative[relative_index] == '/') {
      relative_index += 1U;
    }
    if (segment_size == 0U) {
      continue;
    }
    status =
        confit_host_path_join(out, out_size, current, segment, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    if (strlen(out) + 1U > sizeof(current)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, out, 0, 0,
                            "joined path is too long");
      return CONFIT_ERR_INVALID_ARGUMENT;
    }
    memcpy(current, out, strlen(out) + 1U);
  }

  if (strlen(current) + 1U > out_size) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, current, 0,
                          0, "joined path buffer is too small");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  memcpy(out, current, strlen(current) + 1U);
  return CONFIT_OK;
}

static int confit_cli_path_has_file(const char *path) {
  ConfitDiagnostic diagnostic;
  char *text;
  ConfitStatus status;

  confit_diagnostic_init(&diagnostic);
  text = 0;
  status = confit_host_read_text_file(path, &text, 0, &diagnostic);
  if (status == CONFIT_OK) {
    confit_host_free(text);
    return 1;
  }
  return 0;
}

static ConfitStatus confit_cli_write_init_line(const char *verb,
                                               const char *path) {
  ConfitStatus status;

  status = confit_host_stdout_write(verb);
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write(": ");
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line(path);
  }
  return status;
}

static ConfitStatus confit_cli_init_preflight(
    const ConfitCliInitArgs *args, const ConfitCliTemplateSpec *template_spec,
    ConfitDiagnostic *diagnostic) {
  size_t index;

  for (index = 0U; index < template_spec->file_count; ++index) {
    char path[1024];
    ConfitStatus status;

    status = confit_cli_join_relative_path(
        path, sizeof(path), args->project_root,
        template_spec->files[index].relative_path, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    if (confit_cli_path_has_file(path) && !args->force) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, path, 0, 0,
                            "init refuses to overwrite existing file");
      return CONFIT_ERR_INVALID_ARGUMENT;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_cli_init_manifest(
    const ConfitCliInitArgs *args, const ConfitCliTemplateSpec *template_spec,
    ConfitDiagnostic *diagnostic) {
  ConfitStatus status;
  size_t index;

  status = confit_host_stdout_write("init template: ");
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line(template_spec->name);
  }
  for (index = 0U; status == CONFIT_OK &&
                   index < confit_cli_init_directory_count;
       ++index) {
    char path[1024];

    status = confit_cli_join_relative_path(
        path, sizeof(path), args->project_root,
        confit_cli_init_directories[index], diagnostic);
    if (status == CONFIT_OK) {
      status = confit_cli_write_init_line("create dir", path);
    }
  }
  for (index = 0U; status == CONFIT_OK && index < template_spec->file_count;
       ++index) {
    char path[1024];
    const char *verb;

    status = confit_cli_join_relative_path(
        path, sizeof(path), args->project_root,
        template_spec->files[index].relative_path, diagnostic);
    if (status == CONFIT_OK) {
      verb = confit_cli_path_has_file(path) ? "overwrite file" : "create file";
      status = confit_cli_write_init_line(verb, path);
    }
  }
  return status;
}

static ConfitStatus confit_cli_init_write(
    const ConfitCliInitArgs *args, const ConfitCliTemplateSpec *template_spec,
    ConfitDiagnostic *diagnostic) {
  ConfitStatus status;
  size_t index;

  status = confit_host_make_directories(args->project_root, diagnostic);
  for (index = 0U; status == CONFIT_OK &&
                   index < confit_cli_init_directory_count;
       ++index) {
    char path[1024];

    status = confit_cli_join_relative_path(
        path, sizeof(path), args->project_root,
        confit_cli_init_directories[index], diagnostic);
    if (status == CONFIT_OK) {
      status = confit_host_make_directories(path, diagnostic);
    }
  }

  for (index = 0U; status == CONFIT_OK && index < template_spec->file_count;
       ++index) {
    char path[1024];

    status = confit_cli_join_relative_path(
        path, sizeof(path), args->project_root,
        template_spec->files[index].relative_path, diagnostic);
    if (status == CONFIT_OK) {
      status = confit_host_write_text_file(path, template_spec->files[index].text,
                                           diagnostic);
    }
  }
  return status;
}

static int confit_cli_run_init(int argc, char **argv) {
  ConfitCliInitArgs args;
  const ConfitCliTemplateSpec *template_spec;
  ConfitDiagnostic diagnostic;
  ConfitStatus status;

  status = confit_cli_parse_init_args(argc, argv, &args);
  if (status != CONFIT_OK) {
    return confit_status_exit_code(status);
  }

  template_spec = confit_cli_find_template(args.template_name);
  confit_diagnostic_init(&diagnostic);
  status = confit_cli_init_preflight(&args, template_spec, &diagnostic);
  if (status == CONFIT_OK) {
    status = confit_cli_init_manifest(&args, template_spec, &diagnostic);
  }
  if (status == CONFIT_OK && !args.dry_run) {
    status = confit_cli_init_write(&args, template_spec, &diagnostic);
  }
  if (status != CONFIT_OK) {
    return confit_cli_return_error(status, &diagnostic);
  }

  if (args.dry_run) {
    status = confit_host_stdout_write("init dry-run ok: ");
  } else {
    status = confit_host_stdout_write("init ok: ");
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line(args.project_root);
  }
  return confit_status_exit_code(status);
}

static void confit_cli_project_args_init(ConfitCliProjectArgs *args) {
  args->project_root = 0;
  args->profile_name = 0;
  args->target_name = 0;
}

static char *confit_cli_copy_bytes(const char *text, size_t size) {
  char *copy;

  copy = (char *)malloc(size + 1U);
  if (copy == 0) {
    return 0;
  }
  if (size > 0U) {
    memcpy(copy, text, size);
  }
  copy[size] = '\0';
  return copy;
}

static char *confit_cli_copy_string(const char *text) {
  return text != 0 ? confit_cli_copy_bytes(text, strlen(text)) : 0;
}

static size_t confit_cli_trim_left(const char *text, size_t begin,
                                   size_t end) {
  while (begin < end && isspace((unsigned char)text[begin])) {
    begin += 1U;
  }
  return begin;
}

static size_t confit_cli_trim_right(const char *text, size_t begin,
                                    size_t end) {
  while (end > begin && isspace((unsigned char)text[end - 1U])) {
    end -= 1U;
  }
  return end;
}

static int confit_cli_slice_equals_case(const char *text, size_t begin,
                                        size_t end, const char *word) {
  size_t index;
  size_t word_size;

  word_size = strlen(word);
  if (end - begin != word_size) {
    return 0;
  }
  for (index = 0U; index < word_size; ++index) {
    if (tolower((unsigned char)text[begin + index]) !=
        tolower((unsigned char)word[index])) {
      return 0;
    }
  }
  return 1;
}

static void confit_cli_text_builder_init(ConfitCliTextBuilder *builder) {
  builder->text = 0;
  builder->size = 0U;
  builder->capacity = 0U;
}

static ConfitStatus confit_cli_text_reserve(ConfitCliTextBuilder *builder,
                                            size_t additional_size) {
  size_t required;
  size_t capacity;
  char *text;

  required = builder->size + additional_size + 1U;
  if (required <= builder->capacity) {
    return CONFIT_OK;
  }
  capacity = builder->capacity == 0U ? 512U : builder->capacity;
  while (capacity < required) {
    capacity *= 2U;
  }
  text = (char *)realloc(builder->text, capacity);
  if (text == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  builder->text = text;
  builder->capacity = capacity;
  return CONFIT_OK;
}

static ConfitStatus confit_cli_text_append(ConfitCliTextBuilder *builder,
                                           const char *text) {
  const size_t size = strlen(text);
  ConfitStatus status;

  status = confit_cli_text_reserve(builder, size);
  if (status != CONFIT_OK) {
    return status;
  }
  if (size > 0U) {
    memcpy(builder->text + builder->size, text, size);
  }
  builder->size += size;
  builder->text[builder->size] = '\0';
  return CONFIT_OK;
}

static ConfitStatus confit_cli_text_append_char(ConfitCliTextBuilder *builder,
                                                char value) {
  char text[2];

  text[0] = value;
  text[1] = '\0';
  return confit_cli_text_append(builder, text);
}

static ConfitStatus confit_cli_text_append_quoted(
    ConfitCliTextBuilder *builder, const char *text) {
  ConfitStatus status;
  size_t index;

  status = confit_cli_text_append(builder, "\"");
  if (status != CONFIT_OK) {
    return status;
  }
  for (index = 0U; text != 0 && text[index] != '\0'; ++index) {
    switch (text[index]) {
    case '"':
    case '\\':
      status = confit_cli_text_append(builder, "\\");
      if (status == CONFIT_OK) {
        status = confit_cli_text_append_char(builder, text[index]);
      }
      break;
    case '\n':
      status = confit_cli_text_append(builder, "\\n");
      break;
    case '\r':
      status = confit_cli_text_append(builder, "\\r");
      break;
    case '\t':
      status = confit_cli_text_append(builder, "\\t");
      break;
    default:
      status = confit_cli_text_append_char(builder, text[index]);
      break;
    }
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return confit_cli_text_append(builder, "\"");
}

static ConfitStatus confit_cli_text_append_value(
    ConfitCliTextBuilder *builder, const ConfitOption *option,
    const ConfitValue *value) {
  char buffer[128];

  switch (value->kind) {
  case CONFIT_VALUE_BOOL:
    return confit_cli_text_append(builder,
                                  value->as.bool_value ? "true" : "false");
  case CONFIT_VALUE_INT:
    (void)snprintf(buffer, sizeof(buffer), "%lld",
                   (long long)value->as.int_value);
    return confit_cli_text_append(builder, buffer);
  case CONFIT_VALUE_UINT:
    if (option != 0 && option->type == CONFIT_OPTION_TYPE_HEX) {
      (void)snprintf(buffer, sizeof(buffer), "0x%llX",
                     (unsigned long long)value->as.uint_value);
    } else {
      (void)snprintf(buffer, sizeof(buffer), "%llu",
                     (unsigned long long)value->as.uint_value);
    }
    return confit_cli_text_append(builder, buffer);
  case CONFIT_VALUE_FLOAT:
    (void)snprintf(buffer, sizeof(buffer), "%.17g", value->as.float_value);
    return confit_cli_text_append(builder, buffer);
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
  case CONFIT_VALUE_PATH:
    return confit_cli_text_append_quoted(builder, value->as.string_value);
  case CONFIT_VALUE_EMPTY:
  default:
    return CONFIT_ERR_SCHEMA;
  }
}

static uint32_t confit_cli_sha256_rotr(uint32_t value, unsigned int bits) {
  return (value >> bits) | (value << (32U - bits));
}

static uint32_t confit_cli_sha256_load_be32(const unsigned char *data) {
  return ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
         ((uint32_t)data[2] << 8U) | (uint32_t)data[3];
}

static void confit_cli_sha256_store_be32(unsigned char *out,
                                         uint32_t value) {
  out[0] = (unsigned char)(value >> 24U);
  out[1] = (unsigned char)(value >> 16U);
  out[2] = (unsigned char)(value >> 8U);
  out[3] = (unsigned char)value;
}

static void confit_cli_sha256_init(ConfitCliSha256 *sha) {
  sha->state[0] = UINT32_C(0x6A09E667);
  sha->state[1] = UINT32_C(0xBB67AE85);
  sha->state[2] = UINT32_C(0x3C6EF372);
  sha->state[3] = UINT32_C(0xA54FF53A);
  sha->state[4] = UINT32_C(0x510E527F);
  sha->state[5] = UINT32_C(0x9B05688C);
  sha->state[6] = UINT32_C(0x1F83D9AB);
  sha->state[7] = UINT32_C(0x5BE0CD19);
  sha->bit_count = 0U;
  sha->buffer_size = 0U;
}

static void confit_cli_sha256_transform(ConfitCliSha256 *sha,
                                        const unsigned char block[64]) {
  static const uint32_t constants[64] = {
      UINT32_C(0x428A2F98), UINT32_C(0x71374491), UINT32_C(0xB5C0FBCF),
      UINT32_C(0xE9B5DBA5), UINT32_C(0x3956C25B), UINT32_C(0x59F111F1),
      UINT32_C(0x923F82A4), UINT32_C(0xAB1C5ED5), UINT32_C(0xD807AA98),
      UINT32_C(0x12835B01), UINT32_C(0x243185BE), UINT32_C(0x550C7DC3),
      UINT32_C(0x72BE5D74), UINT32_C(0x80DEB1FE), UINT32_C(0x9BDC06A7),
      UINT32_C(0xC19BF174), UINT32_C(0xE49B69C1), UINT32_C(0xEFBE4786),
      UINT32_C(0x0FC19DC6), UINT32_C(0x240CA1CC), UINT32_C(0x2DE92C6F),
      UINT32_C(0x4A7484AA), UINT32_C(0x5CB0A9DC), UINT32_C(0x76F988DA),
      UINT32_C(0x983E5152), UINT32_C(0xA831C66D), UINT32_C(0xB00327C8),
      UINT32_C(0xBF597FC7), UINT32_C(0xC6E00BF3), UINT32_C(0xD5A79147),
      UINT32_C(0x06CA6351), UINT32_C(0x14292967), UINT32_C(0x27B70A85),
      UINT32_C(0x2E1B2138), UINT32_C(0x4D2C6DFC), UINT32_C(0x53380D13),
      UINT32_C(0x650A7354), UINT32_C(0x766A0ABB), UINT32_C(0x81C2C92E),
      UINT32_C(0x92722C85), UINT32_C(0xA2BFE8A1), UINT32_C(0xA81A664B),
      UINT32_C(0xC24B8B70), UINT32_C(0xC76C51A3), UINT32_C(0xD192E819),
      UINT32_C(0xD6990624), UINT32_C(0xF40E3585), UINT32_C(0x106AA070),
      UINT32_C(0x19A4C116), UINT32_C(0x1E376C08), UINT32_C(0x2748774C),
      UINT32_C(0x34B0BCB5), UINT32_C(0x391C0CB3), UINT32_C(0x4ED8AA4A),
      UINT32_C(0x5B9CCA4F), UINT32_C(0x682E6FF3), UINT32_C(0x748F82EE),
      UINT32_C(0x78A5636F), UINT32_C(0x84C87814), UINT32_C(0x8CC70208),
      UINT32_C(0x90BEFFFA), UINT32_C(0xA4506CEB), UINT32_C(0xBEF9A3F7),
      UINT32_C(0xC67178F2)};
  uint32_t words[64];
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  uint32_t e;
  uint32_t f;
  uint32_t g;
  uint32_t h;
  size_t index;

  for (index = 0U; index < 16U; ++index) {
    words[index] = confit_cli_sha256_load_be32(block + index * 4U);
  }
  for (index = 16U; index < 64U; ++index) {
    const uint32_t s0 = confit_cli_sha256_rotr(words[index - 15U], 7U) ^
                        confit_cli_sha256_rotr(words[index - 15U], 18U) ^
                        (words[index - 15U] >> 3U);
    const uint32_t s1 = confit_cli_sha256_rotr(words[index - 2U], 17U) ^
                        confit_cli_sha256_rotr(words[index - 2U], 19U) ^
                        (words[index - 2U] >> 10U);
    words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
  }

  a = sha->state[0];
  b = sha->state[1];
  c = sha->state[2];
  d = sha->state[3];
  e = sha->state[4];
  f = sha->state[5];
  g = sha->state[6];
  h = sha->state[7];

  for (index = 0U; index < 64U; ++index) {
    const uint32_t s1 = confit_cli_sha256_rotr(e, 6U) ^
                        confit_cli_sha256_rotr(e, 11U) ^
                        confit_cli_sha256_rotr(e, 25U);
    const uint32_t ch = (e & f) ^ ((~e) & g);
    const uint32_t temp1 = h + s1 + ch + constants[index] + words[index];
    const uint32_t s0 = confit_cli_sha256_rotr(a, 2U) ^
                        confit_cli_sha256_rotr(a, 13U) ^
                        confit_cli_sha256_rotr(a, 22U);
    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temp2 = s0 + maj;

    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  sha->state[0] += a;
  sha->state[1] += b;
  sha->state[2] += c;
  sha->state[3] += d;
  sha->state[4] += e;
  sha->state[5] += f;
  sha->state[6] += g;
  sha->state[7] += h;
}

static void confit_cli_sha256_update(ConfitCliSha256 *sha,
                                     const unsigned char *data,
                                     size_t data_size) {
  size_t offset;

  sha->bit_count += (uint64_t)data_size * 8U;
  offset = 0U;
  while (offset < data_size) {
    const size_t space = sizeof(sha->buffer) - sha->buffer_size;
    const size_t chunk =
        data_size - offset < space ? data_size - offset : space;

    memcpy(sha->buffer + sha->buffer_size, data + offset, chunk);
    sha->buffer_size += chunk;
    offset += chunk;
    if (sha->buffer_size == sizeof(sha->buffer)) {
      confit_cli_sha256_transform(sha, sha->buffer);
      sha->buffer_size = 0U;
    }
  }
}

static void confit_cli_sha256_final(ConfitCliSha256 *sha,
                                    unsigned char out_digest[32]) {
  uint64_t bit_count;
  size_t index;

  bit_count = sha->bit_count;
  sha->buffer[sha->buffer_size] = 0x80U;
  sha->buffer_size += 1U;

  if (sha->buffer_size > 56U) {
    while (sha->buffer_size < sizeof(sha->buffer)) {
      sha->buffer[sha->buffer_size] = 0U;
      sha->buffer_size += 1U;
    }
    confit_cli_sha256_transform(sha, sha->buffer);
    sha->buffer_size = 0U;
  }

  while (sha->buffer_size < 56U) {
    sha->buffer[sha->buffer_size] = 0U;
    sha->buffer_size += 1U;
  }
  for (index = 0U; index < 8U; ++index) {
    sha->buffer[63U - index] = (unsigned char)(bit_count >> (index * 8U));
  }
  confit_cli_sha256_transform(sha, sha->buffer);

  for (index = 0U; index < 8U; ++index) {
    confit_cli_sha256_store_be32(out_digest + index * 4U, sha->state[index]);
  }
}

static void confit_cli_sha256_hex(const char *text, size_t text_size,
                                  char out_hex[65]) {
  static const char digits[] = "0123456789abcdef";
  ConfitCliSha256 sha;
  unsigned char digest[32];
  size_t index;

  confit_cli_sha256_init(&sha);
  confit_cli_sha256_update(&sha, (const unsigned char *)text, text_size);
  confit_cli_sha256_final(&sha, digest);
  for (index = 0U; index < sizeof(digest); ++index) {
    out_hex[index * 2U] = digits[digest[index] >> 4U];
    out_hex[index * 2U + 1U] = digits[digest[index] & 0x0FU];
  }
  out_hex[64] = '\0';
}

static void confit_cli_input_files_init(ConfitCliInputFiles *files) {
  files->items = 0;
  files->count = 0U;
}

static void confit_cli_input_files_clear(ConfitCliInputFiles *files) {
  size_t index;

  if (files == 0) {
    return;
  }
  for (index = 0U; index < files->count; ++index) {
    free((void *)files->items[index].path);
    free((void *)files->items[index].sha256);
  }
  free(files->items);
  confit_cli_input_files_init(files);
}

static ConfitStatus confit_cli_input_files_append(
    ConfitCliInputFiles *files, const char *path, const char *sha256,
    ConfitDiagnostic *diagnostic) {
  ConfitInputFile *new_items;
  char *path_copy;
  char *sha_copy;

  path_copy = confit_cli_copy_string(path);
  sha_copy = confit_cli_copy_string(sha256);
  if (path_copy == 0 || sha_copy == 0) {
    free(path_copy);
    free(sha_copy);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, path, 0, 0,
                          "failed to allocate input manifest record");
    return CONFIT_ERR_INTERNAL;
  }

  new_items =
      (ConfitInputFile *)realloc(files->items,
                                 (files->count + 1U) * sizeof(files->items[0]));
  if (new_items == 0) {
    free(path_copy);
    free(sha_copy);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, path, 0, 0,
                          "failed to grow input manifest");
    return CONFIT_ERR_INTERNAL;
  }

  files->items = new_items;
  files->items[files->count].path = path_copy;
  files->items[files->count].sha256 = sha_copy;
  files->count += 1U;
  return CONFIT_OK;
}

static const char *confit_cli_basename(const char *path) {
  size_t index;
  size_t begin;

  begin = 0U;
  for (index = 0U; path != 0 && path[index] != '\0'; ++index) {
    if (path[index] == '/' || path[index] == '\\') {
      begin = index + 1U;
    }
  }
  return path + begin;
}

static ConfitStatus confit_cli_collect_input_file(
    ConfitCliInputFiles *files, const char *relative_path,
    const char *absolute_path, ConfitDiagnostic *diagnostic) {
  char *text;
  size_t text_size;
  char digest[65];
  ConfitStatus status;

  text = 0;
  text_size = 0U;
  status =
      confit_host_read_text_file(absolute_path, &text, &text_size, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  confit_cli_sha256_hex(text, text_size, digest);
  status =
      confit_cli_input_files_append(files, relative_path, digest, diagnostic);
  confit_host_free(text);
  return status;
}

static ConfitStatus confit_cli_collect_input_dir(
    ConfitCliInputFiles *files, const char *directory,
    const char *relative_directory, ConfitDiagnostic *diagnostic) {
  char **paths;
  size_t path_count;
  size_t index;
  ConfitStatus status;

  paths = 0;
  path_count = 0U;
  status =
      confit_host_list_toml_files(directory, &paths, &path_count, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }

  for (index = 0U; status == CONFIT_OK && index < path_count; ++index) {
    char relative_path[512];
    const char *name = confit_cli_basename(paths[index]);

    if (snprintf(relative_path, sizeof(relative_path), "%s/%s",
                 relative_directory, name) >= (int)sizeof(relative_path)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                            relative_directory, 0, 0,
                            "input manifest path is too long");
      status = CONFIT_ERR_INVALID_ARGUMENT;
    } else {
      status =
          confit_cli_collect_input_file(files, relative_path, paths[index],
                                        diagnostic);
    }
  }

  confit_host_string_list_free(paths, path_count);
  return status;
}

static ConfitStatus confit_cli_collect_input_files(
    const char *project_root, ConfitCliInputFiles *files,
    ConfitDiagnostic *diagnostic) {
  char config_root[1024];
  char path[1024];
  char relative[512];
  const char *prefix;
  ConfitStatus status;

  confit_cli_input_files_init(files);
  status = confit_cli_find_config_root(project_root, config_root,
                                       sizeof(config_root), diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }

  prefix = strcmp(config_root, project_root) == 0 ? "" : "config/";
  status = confit_host_path_join(path, sizeof(path), config_root,
                                 "project.toml", diagnostic);
  if (status == CONFIT_OK) {
    if (snprintf(relative, sizeof(relative), "%sproject.toml", prefix) >=
        (int)sizeof(relative)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                            project_root, 0, 0,
                            "input manifest path is too long");
      status = CONFIT_ERR_INVALID_ARGUMENT;
    }
  }
  if (status == CONFIT_OK) {
    status = confit_cli_collect_input_file(files, relative, path, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_path_join(path, sizeof(path), config_root, "options",
                                   diagnostic);
  }
  if (status == CONFIT_OK) {
    (void)snprintf(relative, sizeof(relative), "%soptions", prefix);
    status = confit_cli_collect_input_dir(files, path, relative, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_path_join(path, sizeof(path), config_root, "profiles",
                                   diagnostic);
  }
  if (status == CONFIT_OK) {
    (void)snprintf(relative, sizeof(relative), "%sprofiles", prefix);
    status = confit_cli_collect_input_dir(files, path, relative, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_path_join(path, sizeof(path), config_root, "targets",
                                   diagnostic);
  }
  if (status == CONFIT_OK) {
    (void)snprintf(relative, sizeof(relative), "%stargets", prefix);
    status = confit_cli_collect_input_dir(files, path, relative, diagnostic);
  }

  if (status != CONFIT_OK) {
    confit_cli_input_files_clear(files);
  }
  return status;
}

static ConfitStatus confit_cli_add_raw_set(const char ***sets,
                                           size_t *set_count,
                                           const char *value) {
  const char **new_sets;

  new_sets =
      (const char **)realloc(*sets, (*set_count + 1U) * sizeof((*sets)[0]));
  if (new_sets == 0) {
    return CONFIT_ERR_INTERNAL;
  }

  *sets = new_sets;
  (*sets)[*set_count] = value;
  *set_count += 1U;
  return CONFIT_OK;
}

static void confit_cli_clear_raw_sets(const char ***sets, size_t *set_count) {
  if (sets == 0 || set_count == 0) {
    return;
  }

  free((void *)*sets);
  *sets = 0;
  *set_count = 0U;
}

static void confit_cli_named_value_init(ConfitNamedValue *value) {
  if (value == 0) {
    return;
  }

  value->option_id = 0;
  confit_value_init(&value->value);
  value->source = 0;
}

static void confit_cli_named_value_clear(ConfitNamedValue *value) {
  if (value == 0) {
    return;
  }

  free(value->option_id);
  confit_value_clear(&value->value);
  free(value->source);
  confit_cli_named_value_init(value);
}

static void confit_cli_named_values_clear(ConfitNamedValue *values,
                                          size_t value_count) {
  size_t index;

  for (index = 0U; index < value_count; ++index) {
    confit_cli_named_value_clear(&values[index]);
  }
  free(values);
}

static ConfitStatus confit_cli_append_override(ConfitNamedValue **values,
                                               size_t *value_count,
                                               const char *option_id,
                                               const ConfitValue *value,
                                               const char *source) {
  ConfitNamedValue *new_values;
  ConfitStatus status;

  new_values =
      (ConfitNamedValue *)realloc(*values,
                                  (*value_count + 1U) * sizeof((*values)[0]));
  if (new_values == 0) {
    return CONFIT_ERR_INTERNAL;
  }

  *values = new_values;
  confit_cli_named_value_init(&(*values)[*value_count]);
  (*values)[*value_count].option_id = confit_cli_copy_string(option_id);
  (*values)[*value_count].source = confit_cli_copy_string(source);
  if ((*values)[*value_count].option_id == 0 ||
      (*values)[*value_count].source == 0) {
    confit_cli_named_value_clear(&(*values)[*value_count]);
    return CONFIT_ERR_INTERNAL;
  }
  status = confit_value_copy(&(*values)[*value_count].value, value);
  if (status != CONFIT_OK) {
    confit_cli_named_value_clear(&(*values)[*value_count]);
    return status;
  }

  *value_count += 1U;
  return CONFIT_OK;
}

static ConfitOption *confit_cli_find_option_by_id_or_alias(
    ConfitProject *project, const char *id, int *out_alias_used,
    int *out_ambiguous) {
  ConfitOption *match;
  size_t option_index;

  *out_alias_used = 0;
  *out_ambiguous = 0;
  match = confit_project_find_option(project, id);
  if (match != 0) {
    return match;
  }

  for (option_index = 0U; option_index < project->option_count;
       ++option_index) {
    ConfitOption *option = &project->options[option_index];
    size_t alias_index;

    for (alias_index = 0U; alias_index < option->deprecated_alias_count;
         ++alias_index) {
      if (strcmp(option->deprecated_aliases[alias_index], id) != 0) {
        continue;
      }
      if (match != 0) {
        *out_ambiguous = 1;
        return match;
      }
      match = option;
      *out_alias_used = 1;
    }
  }
  return match;
}

static int confit_cli_parse_bool_value(const char *text, size_t begin,
                                       size_t end, int *out_value) {
  if (confit_cli_slice_equals_case(text, begin, end, "true") ||
      confit_cli_slice_equals_case(text, begin, end, "1")) {
    *out_value = 1;
    return 1;
  }
  if (confit_cli_slice_equals_case(text, begin, end, "false") ||
      confit_cli_slice_equals_case(text, begin, end, "0")) {
    *out_value = 0;
    return 1;
  }
  return 0;
}

static int confit_cli_parse_int_value(const char *text, size_t begin,
                                      size_t end, int64_t *out_value) {
  char *copy;
  char *tail;
  long long value;

  copy = confit_cli_copy_bytes(text + begin, end - begin);
  if (copy == 0) {
    return 0;
  }
  errno = 0;
  tail = 0;
  value = strtoll(copy, &tail, 0);
  if (errno != 0 || tail == copy || *tail != '\0') {
    free(copy);
    return 0;
  }
  free(copy);
  *out_value = (int64_t)value;
  return 1;
}

static int confit_cli_parse_uint_value(const char *text, size_t begin,
                                       size_t end, uint64_t *out_value) {
  char *copy;
  char *tail;
  unsigned long long value;

  if (begin < end && text[begin] == '-') {
    return 0;
  }
  copy = confit_cli_copy_bytes(text + begin, end - begin);
  if (copy == 0) {
    return 0;
  }
  errno = 0;
  tail = 0;
  value = strtoull(copy, &tail, 0);
  if (errno != 0 || tail == copy || *tail != '\0') {
    free(copy);
    return 0;
  }
  free(copy);
  *out_value = (uint64_t)value;
  return 1;
}

static int confit_cli_parse_float_value(const char *text, size_t begin,
                                        size_t end, double *out_value) {
  char *copy;
  char *tail;
  double value;

  copy = confit_cli_copy_bytes(text + begin, end - begin);
  if (copy == 0) {
    return 0;
  }
  errno = 0;
  tail = 0;
  value = strtod(copy, &tail);
  if (errno != 0 || tail == copy || *tail != '\0' || !isfinite(value)) {
    free(copy);
    return 0;
  }
  free(copy);
  *out_value = value;
  return 1;
}

static char *confit_cli_parse_string_slice(const char *text, size_t begin,
                                           size_t end) {
  char quote;
  char *copy;
  size_t index;
  size_t out_index;

  if (end > begin + 1U &&
      (text[begin] == '"' || text[begin] == '\'') &&
      text[end - 1U] == text[begin]) {
    quote = text[begin];
    copy = (char *)malloc(end - begin - 1U);
    if (copy == 0) {
      return 0;
    }
    out_index = 0U;
    for (index = begin + 1U; index + 1U < end; ++index) {
      if (quote == '"' && text[index] == '\\' && index + 2U < end) {
        index += 1U;
        switch (text[index]) {
        case 'n':
          copy[out_index] = '\n';
          break;
        case 'r':
          copy[out_index] = '\r';
          break;
        case 't':
          copy[out_index] = '\t';
          break;
        default:
          copy[out_index] = text[index];
          break;
        }
      } else {
        copy[out_index] = text[index];
      }
      out_index += 1U;
    }
    copy[out_index] = '\0';
    return copy;
  }

  return confit_cli_copy_bytes(text + begin, end - begin);
}

static ConfitStatus confit_cli_parse_override_typed_value(
    const ConfitOption *option, const char *text, size_t begin, size_t end,
    ConfitValue *out_value, ConfitDiagnostic *diagnostic) {
  int bool_value;
  int64_t int_value;
  uint64_t uint_value;
  double float_value;
  char *string_value;
  ConfitStatus status;

  confit_value_init(out_value);
  switch (option->type) {
  case CONFIT_OPTION_TYPE_BOOL:
    if (!confit_cli_parse_bool_value(text, begin, end, &bool_value)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                            "invalid --set bool value");
      return CONFIT_ERR_SCHEMA;
    }
    confit_value_set_bool(out_value, bool_value);
    return CONFIT_OK;
  case CONFIT_OPTION_TYPE_INT:
    if (!confit_cli_parse_int_value(text, begin, end, &int_value)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                            "invalid --set int value");
      return CONFIT_ERR_SCHEMA;
    }
    confit_value_set_int(out_value, int_value);
    return CONFIT_OK;
  case CONFIT_OPTION_TYPE_UINT:
  case CONFIT_OPTION_TYPE_HEX:
    if (!confit_cli_parse_uint_value(text, begin, end, &uint_value)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                            "invalid --set unsigned value");
      return CONFIT_ERR_SCHEMA;
    }
    confit_value_set_uint(out_value, uint_value);
    return CONFIT_OK;
  case CONFIT_OPTION_TYPE_FLOAT:
    if (!confit_cli_parse_float_value(text, begin, end, &float_value)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                            "invalid --set float value");
      return CONFIT_ERR_SCHEMA;
    }
    confit_value_set_float(out_value, float_value);
    return CONFIT_OK;
  case CONFIT_OPTION_TYPE_STRING:
  case CONFIT_OPTION_TYPE_ENUM:
  case CONFIT_OPTION_TYPE_PATH:
    string_value = confit_cli_parse_string_slice(text, begin, end);
    if (string_value == 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, option->id, 0, 0,
                            "failed to allocate --set value");
      return CONFIT_ERR_INTERNAL;
    }
    if (option->type == CONFIT_OPTION_TYPE_STRING) {
      status = confit_value_set_string(out_value, string_value);
    } else if (option->type == CONFIT_OPTION_TYPE_ENUM) {
      status = confit_value_set_enum(out_value, string_value);
    } else {
      status = confit_value_set_path(out_value, string_value);
    }
    free(string_value);
    if (status != CONFIT_OK) {
      confit_diagnostic_set(diagnostic, status, option->id, 0, 0,
                            "failed to copy --set value");
    }
    return status;
  case CONFIT_OPTION_TYPE_INVALID:
  default:
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                          "invalid --set option type");
    return CONFIT_ERR_SCHEMA;
  }
}

static ConfitStatus confit_cli_parse_override(
    ConfitProject *project, const char *assignment, ConfitNamedValue **values,
    size_t *value_count, ConfitDiagnostic *diagnostic) {
  size_t equals_index;
  size_t key_begin;
  size_t key_end;
  size_t value_begin;
  size_t value_end;
  char *option_id;
  ConfitOption *option;
  ConfitValue value;
  ConfitOption validation_option;
  int alias_used;
  int ambiguous;
  ConfitStatus status;

  equals_index = 0U;
  while (assignment[equals_index] != '\0' && assignment[equals_index] != '=') {
    equals_index += 1U;
  }
  if (assignment[equals_index] != '=') {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, assignment,
                          0, 0, "--set must be option-id=value");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  key_begin = confit_cli_trim_left(assignment, 0U, equals_index);
  key_end = confit_cli_trim_right(assignment, key_begin, equals_index);
  value_begin = confit_cli_trim_left(assignment, equals_index + 1U,
                                     strlen(assignment));
  value_end = confit_cli_trim_right(assignment, value_begin,
                                    strlen(assignment));
  if (key_begin == key_end) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, assignment,
                          0, 0, "--set option id is empty");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  option_id = confit_cli_copy_bytes(assignment + key_begin,
                                    key_end - key_begin);
  if (option_id == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, assignment, 0, 0,
                          "failed to allocate --set option id");
    return CONFIT_ERR_INTERNAL;
  }

  alias_used = 0;
  ambiguous = 0;
  option = confit_cli_find_option_by_id_or_alias(project, option_id,
                                                 &alias_used, &ambiguous);
  if (ambiguous) {
    free(option_id);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, assignment, 0, 0,
                          "ambiguous --set deprecated alias");
    return CONFIT_ERR_SCHEMA;
  }
  if (option == 0) {
    free(option_id);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, assignment, 0, 0,
                          "unknown --set option");
    return CONFIT_ERR_SCHEMA;
  }

  status = confit_cli_parse_override_typed_value(option, assignment,
                                                value_begin, value_end, &value,
                                                diagnostic);
  if (status != CONFIT_OK) {
    free(option_id);
    return status;
  }

  validation_option = *option;
  validation_option.default_value = value;
  if (confit_option_validate_default(&validation_option) != CONFIT_OK) {
    confit_value_clear(&value);
    free(option_id);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                          "invalid --set value");
    return CONFIT_ERR_SCHEMA;
  }

  status = confit_cli_append_override(values, value_count, option->id, &value,
                                      "cli --set");
  confit_value_clear(&value);
  free(option_id);
  if (status != CONFIT_OK) {
    confit_diagnostic_set(diagnostic, status, assignment, 0, 0,
                          "failed to record --set override");
  }
  (void)alias_used;
  return status;
}

static ConfitStatus confit_cli_parse_overrides(
    ConfitProject *project, const char *const *sets, size_t set_count,
    ConfitNamedValue **out_values, size_t *out_value_count,
    ConfitDiagnostic *diagnostic) {
  size_t index;
  ConfitStatus status;

  *out_values = 0;
  *out_value_count = 0U;
  for (index = 0U; index < set_count; ++index) {
    status = confit_cli_parse_override(project, sets[index], out_values,
                                       out_value_count, diagnostic);
    if (status != CONFIT_OK) {
      confit_cli_named_values_clear(*out_values, *out_value_count);
      *out_values = 0;
      *out_value_count = 0U;
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_cli_join3(char *out, size_t out_size,
                                     const char *first, const char *second,
                                     const char *third,
                                     ConfitDiagnostic *diagnostic) {
  char scratch[1024];
  ConfitStatus status;

  status =
      confit_host_path_join(scratch, sizeof(scratch), first, second,
                            diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  return confit_host_path_join(out, out_size, scratch, third, diagnostic);
}

static ConfitStatus confit_cli_load_project_graph(
    const char *project_root, ConfitProject **out_project,
    ConfitGraph **out_graph, ConfitSchemaAudit *audit,
    ConfitDiagnostic *diagnostic) {
  ConfitProject *project;
  ConfitGraph *graph;
  ConfitStatus status;

  if (out_project == 0 || out_graph == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                          project_root, 0, 0,
                          "missing checked project output");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  *out_project = 0;
  *out_graph = 0;
  project = 0;
  graph = 0;

  if (audit != 0) {
    status = confit_schema_load_project_with_audit(project_root, &project,
                                                   audit, diagnostic);
  } else {
    status = confit_schema_load_project(project_root, &project, diagnostic);
  }
  if (status != CONFIT_OK) {
    goto fail;
  }
  status = confit_graph_build(project, &graph, diagnostic);
  if (status != CONFIT_OK) {
    goto fail;
  }
  status = confit_graph_validate(graph, diagnostic);
  if (status != CONFIT_OK) {
    goto fail;
  }

  *out_project = project;
  *out_graph = graph;
  return CONFIT_OK;

fail:
  confit_graph_free(graph);
  confit_project_free(project);
  return status;
}

static ConfitStatus confit_cli_load_checked_project(
    const char *project_root, const char *profile_name, const char *target_name,
    int resolve_profile, ConfitProject **out_project, ConfitGraph **out_graph,
    ConfitResolvedConfig **out_config, ConfitSchemaAudit *audit,
    ConfitDiagnostic *diagnostic) {
  ConfitProject *project;
  ConfitGraph *graph;
  ConfitResolvedConfig *config;
  ConfitStatus status;

  if (out_project == 0 || out_graph == 0 || out_config == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, project_root,
                          0, 0, "missing checked project output");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  *out_project = 0;
  *out_graph = 0;
  *out_config = 0;
  project = 0;
  graph = 0;
  config = 0;

  status = confit_cli_load_project_graph(project_root, &project, &graph, audit,
                                         diagnostic);
  if (status != CONFIT_OK) {
    goto fail;
  }
  if (resolve_profile) {
    status = confit_resolver_resolve(project, profile_name, target_name, 0, 0U,
                                     &config, diagnostic);
    if (status != CONFIT_OK) {
      goto fail;
    }
  }

  *out_project = project;
  *out_graph = graph;
  *out_config = config;
  return CONFIT_OK;

fail:
  confit_resolved_config_free(config);
  confit_graph_free(graph);
  confit_project_free(project);
  return status;
}

static const char *confit_cli_effective_target_name(
    const ConfitProject *project, const char *profile_name,
    const char *target_name) {
  size_t index;

  if (target_name != 0) {
    return target_name;
  }
  if (project == 0 || profile_name == 0) {
    return 0;
  }
  for (index = 0U; index < project->profile_count; ++index) {
    const ConfitProfile *profile = &project->profiles[index];
    if (profile->name != 0 && strcmp(profile->name, profile_name) == 0) {
      return profile->target;
    }
  }
  return 0;
}

static ConfitStatus confit_cli_print_schema_warnings(
    const ConfitSchemaAudit *audit) {
  ConfitStatus status;
  size_t index;

  if (audit == 0) {
    return CONFIT_OK;
  }

  status = CONFIT_OK;
  for (index = 0U; status == CONFIT_OK && index < audit->warning_count;
       ++index) {
    const ConfitSchemaWarning *warning = &audit->warnings[index];

    status = confit_host_stderr_write("confit: warning");
    if (status == CONFIT_OK && warning->path != 0) {
      status = confit_host_stderr_write(": ");
    }
    if (status == CONFIT_OK && warning->path != 0) {
      status = confit_host_stderr_write(warning->path);
    }
    if (status == CONFIT_OK && warning->line != 0U) {
      char buffer[64];

      (void)snprintf(buffer, sizeof(buffer), ":%lu",
                     (unsigned long)warning->line);
      status = confit_host_stderr_write(buffer);
    }
    if (status == CONFIT_OK && warning->column != 0U) {
      char buffer[64];

      (void)snprintf(buffer, sizeof(buffer), ":%lu",
                     (unsigned long)warning->column);
      status = confit_host_stderr_write(buffer);
    }
    if (status == CONFIT_OK) {
      status = confit_host_stderr_write(": ");
    }
    if (status == CONFIT_OK && warning->message != 0) {
      status = confit_host_stderr_write(warning->message);
    }
    if (status == CONFIT_OK && warning->option_id != 0 &&
        (warning->path == 0 || strcmp(warning->path, warning->option_id) != 0)) {
      status = confit_host_stderr_write(" [");
      if (status == CONFIT_OK) {
        status = confit_host_stderr_write(warning->option_id);
      }
      if (status == CONFIT_OK) {
        status = confit_host_stderr_write("]");
      }
    }
    if (status == CONFIT_OK) {
      status = confit_host_stderr_write("\n");
    }
  }
  return status;
}

static ConfitStatus confit_cli_parse_check_args(int argc, char **argv,
                                                ConfitCliCheckArgs *args) {
  int index;

  confit_cli_project_args_init(&args->project);
  args->strict = 0;
  for (index = 2; index < argc; ++index) {
    const char *arg = argv[index];

    if (strcmp(arg, "--project") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --project");
      }
      index += 1;
      args->project.project_root = argv[index];
      continue;
    }
    if (strcmp(arg, "--profile") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --profile");
      }
      index += 1;
      args->project.profile_name = argv[index];
      continue;
    }
    if (strcmp(arg, "--target") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --target");
      }
      index += 1;
      args->project.target_name = argv[index];
      continue;
    }
    if (strcmp(arg, "--strict") == 0) {
      args->strict = 1;
      continue;
    }
    if (arg[0] == '-') {
      return confit_cli_write_error("unknown check option");
    }
    return confit_cli_write_error("check does not accept positional arguments");
  }

  if (args->project.project_root == 0) {
    return confit_cli_write_error("check requires --project");
  }
  if (args->project.profile_name == 0) {
    return confit_cli_write_error("check requires --profile");
  }
  return CONFIT_OK;
}

static int confit_cli_run_check(int argc, char **argv) {
  ConfitCliCheckArgs args;
  ConfitSchemaAudit audit;
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitGraph *graph;
  ConfitResolvedConfig *config;
  ConfitStatus status;
  ConfitStatus warning_status;

  status = confit_cli_parse_check_args(argc, argv, &args);
  if (status != CONFIT_OK) {
    return confit_status_exit_code(status);
  }
  status = confit_cli_print_verbose_project("check", &args.project);
  if (status != CONFIT_OK) {
    return confit_status_exit_code(status);
  }

  confit_diagnostic_init(&diagnostic);
  confit_schema_audit_init(&audit);
  project = 0;
  graph = 0;
  config = 0;
  status = confit_cli_load_checked_project(
      args.project.project_root, args.project.profile_name,
      args.project.target_name, 1, &project, &graph, &config, &audit,
      &diagnostic);
  warning_status = confit_cli_print_schema_warnings(&audit);
  if (status == CONFIT_OK && warning_status != CONFIT_OK) {
    status = warning_status;
  }
  if (status == CONFIT_OK && args.strict && audit.warning_count > 0U) {
    confit_diagnostic_set(&diagnostic, CONFIT_ERR_SCHEMA,
                          args.project.project_root, 0, 0,
                          "schema warnings are fatal under --strict");
    status = CONFIT_ERR_SCHEMA;
  }
  confit_resolved_config_free(config);
  confit_graph_free(graph);
  confit_project_free(project);
  confit_schema_audit_clear(&audit);
  if (status != CONFIT_OK) {
    return confit_cli_return_error(status, &diagnostic);
  }
  if (confit_cli_is_quiet()) {
    return confit_status_exit_code(CONFIT_OK);
  }

  status = confit_host_stdout_write_line("check ok");
  return confit_status_exit_code(status);
}

static void confit_cli_resolve_args_init(ConfitCliResolveArgs *args) {
  confit_cli_project_args_init(&args->project);
  args->sets = 0;
  args->set_count = 0U;
  args->format = "text";
}

static void confit_cli_resolve_args_clear(ConfitCliResolveArgs *args) {
  confit_cli_clear_raw_sets(&args->sets, &args->set_count);
}

static ConfitStatus confit_cli_parse_resolve_args(int argc, char **argv,
                                                  ConfitCliResolveArgs *args) {
  int index;

  confit_cli_resolve_args_init(args);
  for (index = 2; index < argc; ++index) {
    const char *arg = argv[index];

    if (strcmp(arg, "--project") == 0) {
      if (index + 1 >= argc) {
        confit_cli_resolve_args_clear(args);
        return confit_cli_write_error("missing value for --project");
      }
      index += 1;
      args->project.project_root = argv[index];
      continue;
    }
    if (strcmp(arg, "--profile") == 0) {
      if (index + 1 >= argc) {
        confit_cli_resolve_args_clear(args);
        return confit_cli_write_error("missing value for --profile");
      }
      index += 1;
      args->project.profile_name = argv[index];
      continue;
    }
    if (strcmp(arg, "--target") == 0) {
      if (index + 1 >= argc) {
        confit_cli_resolve_args_clear(args);
        return confit_cli_write_error("missing value for --target");
      }
      index += 1;
      args->project.target_name = argv[index];
      continue;
    }
    if (strcmp(arg, "--set") == 0) {
      ConfitStatus status;

      if (index + 1 >= argc) {
        confit_cli_resolve_args_clear(args);
        return confit_cli_write_error("missing value for --set");
      }
      index += 1;
      status = confit_cli_add_raw_set(&args->sets, &args->set_count,
                                      argv[index]);
      if (status != CONFIT_OK) {
        confit_cli_resolve_args_clear(args);
        return status;
      }
      continue;
    }
    if (strcmp(arg, "--format") == 0) {
      if (index + 1 >= argc) {
        confit_cli_resolve_args_clear(args);
        return confit_cli_write_error("missing value for --format");
      }
      index += 1;
      args->format = argv[index];
      continue;
    }
    if (arg[0] == '-') {
      confit_cli_resolve_args_clear(args);
      return confit_cli_write_error("unknown resolve option");
    }
    confit_cli_resolve_args_clear(args);
    return confit_cli_write_error("resolve does not accept positional arguments");
  }

  if (args->project.project_root == 0) {
    confit_cli_resolve_args_clear(args);
    return confit_cli_write_error("resolve requires --project");
  }
  if (args->project.profile_name == 0) {
    confit_cli_resolve_args_clear(args);
    return confit_cli_write_error("resolve requires --profile");
  }
  if (strcmp(args->format, "text") != 0 && strcmp(args->format, "json") != 0 &&
      strcmp(args->format, "toml") != 0) {
    confit_cli_resolve_args_clear(args);
    return confit_cli_write_error("resolve --format must be text, json, or toml");
  }
  return CONFIT_OK;
}

static ConfitStatus confit_cli_resolved_config_to_format(
    const ConfitResolvedConfig *config, const char *format, char **out_text,
    ConfitDiagnostic *diagnostic) {
  ConfitStatus status;

  if (strcmp(format, "json") == 0) {
    status = confit_resolved_config_to_json(config, out_text);
  } else if (strcmp(format, "toml") == 0) {
    status = confit_resolved_config_to_toml(config, out_text);
  } else {
    status = confit_resolved_config_to_text(config, out_text);
  }
  if (status != CONFIT_OK) {
    confit_diagnostic_set(diagnostic, status, 0, 0, 0,
                          "failed to serialize resolved config");
  }
  return status;
}

static int confit_cli_run_resolve(int argc, char **argv) {
  ConfitCliResolveArgs args;
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitGraph *graph;
  ConfitResolvedConfig *config;
  ConfitNamedValue *overrides;
  size_t override_count;
  char *output;
  ConfitStatus status;

  status = confit_cli_parse_resolve_args(argc, argv, &args);
  if (status != CONFIT_OK) {
    return confit_status_exit_code(status);
  }

  confit_diagnostic_init(&diagnostic);
  project = 0;
  graph = 0;
  config = 0;
  overrides = 0;
  override_count = 0U;
  output = 0;

  status = confit_cli_load_project_graph(args.project.project_root, &project,
                                         &graph, 0, &diagnostic);
  if (status == CONFIT_OK) {
    status = confit_cli_parse_overrides(project, args.sets, args.set_count,
                                        &overrides, &override_count,
                                        &diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_resolver_resolve(
        project, args.project.profile_name, args.project.target_name,
        overrides, override_count, &config, &diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_resolved_config_to_format(config, args.format,
                                                  &output, &diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write(output);
  }

  confit_resolver_string_free(output);
  confit_resolved_config_free(config);
  confit_cli_named_values_clear(overrides, override_count);
  confit_graph_free(graph);
  confit_project_free(project);
  confit_cli_resolve_args_clear(&args);

  if (status != CONFIT_OK) {
    return confit_cli_return_error(status, &diagnostic);
  }
  return confit_status_exit_code(CONFIT_OK);
}

static ConfitStatus confit_cli_write_artifact(const char *out_dir,
                                              const char *name,
                                              const char *text,
                                              ConfitDiagnostic *diagnostic) {
  char path[1024];
  ConfitStatus status;

  status = confit_host_path_join(path, sizeof(path), out_dir, name, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  return confit_host_write_text_file(path, text, diagnostic);
}

static ConfitStatus confit_cli_write_child_artifact(
    const char *out_dir, const char *child_dir, const char *name,
    const char *text, ConfitDiagnostic *diagnostic) {
  char directory[1024];
  char path[1024];
  ConfitStatus status;

  status = confit_host_path_join(directory, sizeof(directory), out_dir,
                                 child_dir, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_make_directories(directory, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_path_join(path, sizeof(path), directory, name,
                                 diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  return confit_host_write_text_file(path, text, diagnostic);
}

static int confit_cli_gen_wants(const ConfitCliGenArgs *args,
                                unsigned artifact) {
  return (args->artifact_mask & artifact) != 0U;
}

static ConfitStatus confit_cli_gen_artifact_mask(const char *text,
                                                 unsigned *out_mask) {
  if (strcmp(text, "header") == 0) {
    *out_mask = CONFIT_CLI_ARTIFACT_HEADER;
    return CONFIT_OK;
  }
  if (strcmp(text, "reports") == 0) {
    *out_mask = CONFIT_CLI_ARTIFACT_REPORTS;
    return CONFIT_OK;
  }
  if (strcmp(text, "cmake") == 0) {
    *out_mask = CONFIT_CLI_ARTIFACT_CMAKE;
    return CONFIT_OK;
  }
  if (strcmp(text, "qstar") == 0) {
    *out_mask = CONFIT_CLI_ARTIFACT_QSTAR;
    return CONFIT_OK;
  }
  if (strcmp(text, "all") == 0) {
    *out_mask = CONFIT_CLI_ARTIFACT_ALL;
    return CONFIT_OK;
  }
  return confit_cli_write_error(
      "gen --artifact must be header, reports, cmake, qstar, or all");
}

static ConfitStatus confit_cli_gen_check_output(
    const ConfitCliGenArgs *args, const char *name,
    ConfitDiagnostic *diagnostic) {
  char path[1024];
  ConfitStatus status;

  status = confit_host_path_join(path, sizeof(path), args->out_dir, name,
                                 diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  if (!args->force && confit_cli_path_has_file(path)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, path, 0, 0,
                          "generated artifact exists; use --force");
    return CONFIT_ERR_GENERATION;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_cli_gen_preflight(
    const ConfitCliGenArgs *args, ConfitDiagnostic *diagnostic) {
  ConfitStatus status;

  if (args->dry_run) {
    return CONFIT_OK;
  }
  status = CONFIT_OK;
  if (status == CONFIT_OK && confit_cli_gen_wants(args,
                                                   CONFIT_CLI_ARTIFACT_HEADER)) {
    status = confit_cli_gen_check_output(args, "config.h", diagnostic);
  }
  if (status == CONFIT_OK && confit_cli_gen_wants(args,
                                                   CONFIT_CLI_ARTIFACT_REPORTS)) {
    status = confit_cli_gen_check_output(args, "config.report.json",
                                         diagnostic);
  }
  if (status == CONFIT_OK && confit_cli_gen_wants(args,
                                                   CONFIT_CLI_ARTIFACT_REPORTS)) {
    status = confit_cli_gen_check_output(args, "config.explain.txt",
                                         diagnostic);
  }
  if (status == CONFIT_OK && confit_cli_gen_wants(args,
                                                   CONFIT_CLI_ARTIFACT_REPORTS)) {
    status = confit_cli_gen_check_output(args, "config.graph.json",
                                         diagnostic);
  }
  if (status == CONFIT_OK && confit_cli_gen_wants(args,
                                                   CONFIT_CLI_ARTIFACT_REPORTS)) {
    status = confit_cli_gen_check_output(args, "config.inputs.json",
                                         diagnostic);
  }
  if (status == CONFIT_OK && confit_cli_gen_wants(args,
                                                   CONFIT_CLI_ARTIFACT_CMAKE)) {
    status = confit_cli_gen_check_output(args, "config.cmake", diagnostic);
  }
  if (status == CONFIT_OK && confit_cli_gen_wants(args,
                                                   CONFIT_CLI_ARTIFACT_QSTAR)) {
    status = confit_cli_gen_check_output(args, "config/config.qsm",
                                         diagnostic);
  }
  if (status == CONFIT_OK && confit_cli_gen_wants(args,
                                                   CONFIT_CLI_ARTIFACT_QSTAR)) {
    status = confit_cli_gen_check_output(args, "config.qst", diagnostic);
  }
  return status;
}

static ConfitStatus confit_cli_gen_print_dry_run_line(
    const ConfitCliGenArgs *args, const char *name) {
  ConfitStatus status;

  status = confit_host_stdout_write("would write: ");
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write(args->out_dir);
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write("/");
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line(name);
  }
  return status;
}

static ConfitStatus confit_cli_gen_print_dry_run(
    const ConfitCliGenArgs *args) {
  ConfitStatus status;

  status = confit_host_stdout_write("gen dry-run: ");
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line(args->out_dir);
  }
  if (status == CONFIT_OK && confit_cli_gen_wants(args,
                                                   CONFIT_CLI_ARTIFACT_HEADER)) {
    status = confit_cli_gen_print_dry_run_line(args, "config.h");
  }
  if (status == CONFIT_OK && confit_cli_gen_wants(args,
                                                   CONFIT_CLI_ARTIFACT_REPORTS)) {
    status = confit_cli_gen_print_dry_run_line(args, "config.report.json");
  }
  if (status == CONFIT_OK && confit_cli_gen_wants(args,
                                                   CONFIT_CLI_ARTIFACT_REPORTS)) {
    status = confit_cli_gen_print_dry_run_line(args, "config.explain.txt");
  }
  if (status == CONFIT_OK && confit_cli_gen_wants(args,
                                                   CONFIT_CLI_ARTIFACT_REPORTS)) {
    status = confit_cli_gen_print_dry_run_line(args, "config.graph.json");
  }
  if (status == CONFIT_OK && confit_cli_gen_wants(args,
                                                   CONFIT_CLI_ARTIFACT_REPORTS)) {
    status = confit_cli_gen_print_dry_run_line(args, "config.inputs.json");
  }
  if (status == CONFIT_OK && confit_cli_gen_wants(args,
                                                   CONFIT_CLI_ARTIFACT_CMAKE)) {
    status = confit_cli_gen_print_dry_run_line(args, "config.cmake");
  }
  if (status == CONFIT_OK && confit_cli_gen_wants(args,
                                                   CONFIT_CLI_ARTIFACT_QSTAR)) {
    status = confit_cli_gen_print_dry_run_line(args, "config/config.qsm");
  }
  if (status == CONFIT_OK && confit_cli_gen_wants(args,
                                                   CONFIT_CLI_ARTIFACT_QSTAR)) {
    status = confit_cli_gen_print_dry_run_line(args, "config.qst");
  }
  return status;
}

static ConfitStatus confit_cli_generate_artifacts(
    const ConfitProject *project, const ConfitGraph *graph,
    const ConfitResolvedConfig *config, const ConfitCliGenArgs *args,
    ConfitDiagnostic *diagnostic) {
  const char *target_name;
  ConfitConfigHeaderOptions header_options;
  ConfitReportOptions report_options;
  ConfitBuildIntegrationOptions build_options;
  ConfitCliInputFiles input_files;
  char *header;
  char *report_json;
  char *explain_text;
  char *graph_json;
  char *inputs_json;
  char *cmake_fragment;
  char *qstar_config_module;
  char *qstar_manifest;
  ConfitStatus status;

  target_name = confit_cli_effective_target_name(
      project, args->project.profile_name, args->project.target_name);
  header_options.profile_name = args->project.profile_name;
  header_options.target_name = target_name;
  report_options.profile_name = args->project.profile_name;
  report_options.target_name = target_name;
  report_options.input_files = 0;
  report_options.input_file_count = 0U;
  build_options.profile_name = args->project.profile_name;
  build_options.target_name = target_name;
  build_options.header_path = "config.h";
  build_options.report_json_path = "config.report.json";
  build_options.explain_text_path = "config.explain.txt";
  build_options.graph_json_path = "config.graph.json";
  build_options.inputs_json_path = "config.inputs.json";
  header = 0;
  report_json = 0;
  explain_text = 0;
  graph_json = 0;
  inputs_json = 0;
  cmake_fragment = 0;
  qstar_config_module = 0;
  qstar_manifest = 0;
  confit_cli_input_files_init(&input_files);

  status = confit_cli_collect_input_files(args->project.project_root,
                                          &input_files, diagnostic);
  if (status == CONFIT_OK) {
    report_options.input_files = input_files.items;
    report_options.input_file_count = input_files.count;
  }
  if (status == CONFIT_OK) {
    status = confit_cli_gen_preflight(args, diagnostic);
  }
  if (status == CONFIT_OK &&
      confit_cli_gen_wants(args, CONFIT_CLI_ARTIFACT_HEADER)) {
    status = confit_generate_config_header(project, config, &header_options,
                                           &header, diagnostic);
  }
  if (status == CONFIT_OK &&
      confit_cli_gen_wants(args, CONFIT_CLI_ARTIFACT_REPORTS)) {
    status = confit_generate_report_json(project, config, &report_options,
                                         &report_json, diagnostic);
  }
  if (status == CONFIT_OK &&
      confit_cli_gen_wants(args, CONFIT_CLI_ARTIFACT_REPORTS)) {
    status = confit_generate_explain_report(project, config, &report_options,
                                            &explain_text, diagnostic);
  }
  if (status == CONFIT_OK &&
      confit_cli_gen_wants(args, CONFIT_CLI_ARTIFACT_REPORTS)) {
    status = confit_graph_to_json(graph, &graph_json);
    if (status != CONFIT_OK) {
      confit_diagnostic_set(diagnostic, status, args->out_dir, 0, 0,
                            "failed to generate graph report");
    }
  }
  if (status == CONFIT_OK &&
      confit_cli_gen_wants(args, CONFIT_CLI_ARTIFACT_REPORTS)) {
    status = confit_generate_inputs_json(project, &report_options, &inputs_json,
                                         diagnostic);
  }
  if (status == CONFIT_OK &&
      confit_cli_gen_wants(args, CONFIT_CLI_ARTIFACT_CMAKE)) {
    status = confit_generate_cmake_fragment(project, config, &build_options,
                                            &cmake_fragment, diagnostic);
  }
  if (status == CONFIT_OK &&
      confit_cli_gen_wants(args, CONFIT_CLI_ARTIFACT_QSTAR)) {
    status = confit_generate_qstar_config_module(
        project, config, &build_options, &qstar_config_module, diagnostic);
  }
  if (status == CONFIT_OK &&
      confit_cli_gen_wants(args, CONFIT_CLI_ARTIFACT_QSTAR)) {
    status = confit_generate_qstar_manifest(project, config, &build_options,
                                            &qstar_manifest, diagnostic);
  }
  if (status == CONFIT_OK && args->dry_run) {
    status = confit_cli_gen_print_dry_run(args);
  }
  if (status == CONFIT_OK && !args->dry_run) {
    status = confit_host_make_directories(args->out_dir, diagnostic);
  }
  if (status == CONFIT_OK && !args->dry_run &&
      confit_cli_gen_wants(args, CONFIT_CLI_ARTIFACT_HEADER)) {
    status =
        confit_cli_write_artifact(args->out_dir, "config.h", header,
                                  diagnostic);
  }
  if (status == CONFIT_OK && !args->dry_run &&
      confit_cli_gen_wants(args, CONFIT_CLI_ARTIFACT_REPORTS)) {
    status = confit_cli_write_artifact(args->out_dir, "config.report.json",
                                       report_json, diagnostic);
  }
  if (status == CONFIT_OK && !args->dry_run &&
      confit_cli_gen_wants(args, CONFIT_CLI_ARTIFACT_REPORTS)) {
    status = confit_cli_write_artifact(args->out_dir, "config.explain.txt",
                                       explain_text, diagnostic);
  }
  if (status == CONFIT_OK && !args->dry_run &&
      confit_cli_gen_wants(args, CONFIT_CLI_ARTIFACT_REPORTS)) {
    status = confit_cli_write_artifact(args->out_dir, "config.graph.json",
                                       graph_json, diagnostic);
  }
  if (status == CONFIT_OK && !args->dry_run &&
      confit_cli_gen_wants(args, CONFIT_CLI_ARTIFACT_REPORTS)) {
    status = confit_cli_write_artifact(args->out_dir, "config.inputs.json",
                                       inputs_json, diagnostic);
  }
  if (status == CONFIT_OK && !args->dry_run &&
      confit_cli_gen_wants(args, CONFIT_CLI_ARTIFACT_CMAKE)) {
    status = confit_cli_write_artifact(args->out_dir, "config.cmake",
                                       cmake_fragment, diagnostic);
  }
  if (status == CONFIT_OK && !args->dry_run &&
      confit_cli_gen_wants(args, CONFIT_CLI_ARTIFACT_QSTAR)) {
    status = confit_cli_write_child_artifact(
        args->out_dir, "config", "config.qsm", qstar_config_module,
        diagnostic);
  }
  if (status == CONFIT_OK && !args->dry_run &&
      confit_cli_gen_wants(args, CONFIT_CLI_ARTIFACT_QSTAR)) {
    status = confit_cli_write_artifact(args->out_dir, "config.qst",
                                       qstar_manifest, diagnostic);
  }

  confit_generator_string_free(header);
  confit_generator_string_free(report_json);
  confit_generator_string_free(explain_text);
  confit_graph_string_free(graph_json);
  confit_generator_string_free(inputs_json);
  confit_generator_string_free(cmake_fragment);
  confit_generator_string_free(qstar_config_module);
  confit_generator_string_free(qstar_manifest);
  confit_cli_input_files_clear(&input_files);
  return status;
}

static ConfitStatus confit_cli_parse_gen_args(int argc, char **argv,
                                              ConfitCliGenArgs *args) {
  int index;

  confit_cli_project_args_init(&args->project);
  args->out_dir = 0;
  args->artifact_mask = CONFIT_CLI_ARTIFACT_ALL;
  args->artifact_seen = 0;
  args->dry_run = 0;
  args->force = 0;
  for (index = 2; index < argc; ++index) {
    const char *arg = argv[index];

    if (strcmp(arg, "--project") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --project");
      }
      index += 1;
      args->project.project_root = argv[index];
      continue;
    }
    if (strcmp(arg, "--profile") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --profile");
      }
      index += 1;
      args->project.profile_name = argv[index];
      continue;
    }
    if (strcmp(arg, "--target") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --target");
      }
      index += 1;
      args->project.target_name = argv[index];
      continue;
    }
    if (strcmp(arg, "--out") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --out");
      }
      index += 1;
      args->out_dir = argv[index];
      continue;
    }
    if (strcmp(arg, "--artifact") == 0) {
      unsigned mask;
      ConfitStatus status;

      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --artifact");
      }
      index += 1;
      mask = 0U;
      status = confit_cli_gen_artifact_mask(argv[index], &mask);
      if (status != CONFIT_OK) {
        return status;
      }
      if (!args->artifact_seen) {
        args->artifact_mask = 0U;
        args->artifact_seen = 1;
      }
      args->artifact_mask |= mask;
      continue;
    }
    if (strcmp(arg, "--dry-run") == 0) {
      args->dry_run = 1;
      continue;
    }
    if (strcmp(arg, "--force") == 0) {
      args->force = 1;
      continue;
    }
    if (arg[0] == '-') {
      return confit_cli_write_error("unknown gen option");
    }
    return confit_cli_write_error("gen does not accept positional arguments");
  }

  if (args->project.project_root == 0) {
    return confit_cli_write_error("gen requires --project");
  }
  if (args->project.profile_name == 0) {
    return confit_cli_write_error("gen requires --profile");
  }
  if (args->out_dir == 0) {
    return confit_cli_write_error("gen requires --out");
  }
  if (args->artifact_mask == 0U) {
    return confit_cli_write_error("gen requires at least one artifact");
  }
  return CONFIT_OK;
}

static int confit_cli_run_gen(int argc, char **argv) {
  ConfitCliGenArgs args;
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitGraph *graph;
  ConfitResolvedConfig *config;
  ConfitStatus status;

  status = confit_cli_parse_gen_args(argc, argv, &args);
  if (status != CONFIT_OK) {
    return confit_status_exit_code(status);
  }
  status = confit_cli_print_verbose_project("gen", &args.project);
  if (status != CONFIT_OK) {
    return confit_status_exit_code(status);
  }

  confit_diagnostic_init(&diagnostic);
  project = 0;
  graph = 0;
  config = 0;
  status = confit_cli_load_checked_project(
      args.project.project_root, args.project.profile_name,
      args.project.target_name, 1, &project, &graph, &config, 0,
      &diagnostic);
  if (status == CONFIT_OK) {
    status =
        confit_cli_generate_artifacts(project, graph, config, &args,
                                      &diagnostic);
  }
  confit_resolved_config_free(config);
  confit_graph_free(graph);
  confit_project_free(project);

  if (status != CONFIT_OK) {
    return confit_cli_return_error(status, &diagnostic);
  }
  if (confit_cli_is_quiet()) {
    return confit_status_exit_code(CONFIT_OK);
  }
  status = confit_host_stdout_write("gen ok: ");
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line(args.out_dir);
  }
  return confit_status_exit_code(status);
}

static ConfitStatus confit_cli_parse_explain_args(int argc, char **argv,
                                                  ConfitCliExplainArgs *args) {
  int index;

  confit_cli_project_args_init(&args->project);
  args->sets = 0;
  args->set_count = 0U;
  args->option_id = 0;
  for (index = 2; index < argc; ++index) {
    const char *arg = argv[index];

    if (strcmp(arg, "--project") == 0) {
      if (index + 1 >= argc) {
        confit_cli_clear_raw_sets(&args->sets, &args->set_count);
        return confit_cli_write_error("missing value for --project");
      }
      index += 1;
      args->project.project_root = argv[index];
      continue;
    }
    if (strcmp(arg, "--profile") == 0) {
      if (index + 1 >= argc) {
        confit_cli_clear_raw_sets(&args->sets, &args->set_count);
        return confit_cli_write_error("missing value for --profile");
      }
      index += 1;
      args->project.profile_name = argv[index];
      continue;
    }
    if (strcmp(arg, "--target") == 0) {
      if (index + 1 >= argc) {
        confit_cli_clear_raw_sets(&args->sets, &args->set_count);
        return confit_cli_write_error("missing value for --target");
      }
      index += 1;
      args->project.target_name = argv[index];
      continue;
    }
    if (strcmp(arg, "--set") == 0) {
      ConfitStatus status;

      if (index + 1 >= argc) {
        confit_cli_clear_raw_sets(&args->sets, &args->set_count);
        return confit_cli_write_error("missing value for --set");
      }
      index += 1;
      status = confit_cli_add_raw_set(&args->sets, &args->set_count,
                                      argv[index]);
      if (status != CONFIT_OK) {
        confit_cli_clear_raw_sets(&args->sets, &args->set_count);
        return status;
      }
      continue;
    }
    if (arg[0] == '-') {
      confit_cli_clear_raw_sets(&args->sets, &args->set_count);
      return confit_cli_write_error("unknown explain option");
    }
    if (args->option_id != 0) {
      confit_cli_clear_raw_sets(&args->sets, &args->set_count);
      return confit_cli_write_error("too many explain option ids");
    }
    args->option_id = arg;
  }

  if (args->project.project_root == 0) {
    confit_cli_clear_raw_sets(&args->sets, &args->set_count);
    return confit_cli_write_error("explain requires --project");
  }
  if (args->project.profile_name == 0) {
    confit_cli_clear_raw_sets(&args->sets, &args->set_count);
    return confit_cli_write_error("explain requires --profile");
  }
  if (args->option_id == 0) {
    confit_cli_clear_raw_sets(&args->sets, &args->set_count);
    return confit_cli_write_error("explain requires an option id");
  }
  return CONFIT_OK;
}

static int confit_cli_run_explain(int argc, char **argv) {
  ConfitCliExplainArgs args;
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitGraph *graph;
  ConfitResolvedConfig *config;
  ConfitNamedValue *overrides;
  size_t override_count;
  char *explanation;
  ConfitStatus status;

  status = confit_cli_parse_explain_args(argc, argv, &args);
  if (status != CONFIT_OK) {
    return confit_status_exit_code(status);
  }

  confit_diagnostic_init(&diagnostic);
  project = 0;
  graph = 0;
  config = 0;
  overrides = 0;
  override_count = 0U;
  explanation = 0;
  status = confit_cli_load_project_graph(args.project.project_root, &project,
                                         &graph, 0, &diagnostic);
  if (status == CONFIT_OK) {
    status = confit_cli_parse_overrides(project, args.sets, args.set_count,
                                        &overrides, &override_count,
                                        &diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_resolver_resolve(
        project, args.project.profile_name, args.project.target_name,
        overrides, override_count, &config, &diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_explain_option(project, config, args.option_id,
                                   &explanation, &diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write(explanation);
  }

  confit_explain_string_free(explanation);
  confit_resolved_config_free(config);
  confit_cli_named_values_clear(overrides, override_count);
  confit_graph_free(graph);
  confit_project_free(project);
  confit_cli_clear_raw_sets(&args.sets, &args.set_count);

  if (status != CONFIT_OK) {
    return confit_cli_return_error(status, &diagnostic);
  }
  return confit_status_exit_code(CONFIT_OK);
}

static ConfitStatus confit_cli_parse_compat_args(int argc, char **argv,
                                                 ConfitCliCompatArgs *args) {
  int index;

  args->parus_root = 0;
  args->delos_root = 0;
  args->profile_name = 0;
  args->target_name = 0;
  args->compat_root = 0;
  args->format = "text";
  for (index = 2; index < argc; ++index) {
    const char *arg = argv[index];

    if (strcmp(arg, "--parus") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --parus");
      }
      index += 1;
      args->parus_root = argv[index];
      continue;
    }
    if (strcmp(arg, "--delos") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --delos");
      }
      index += 1;
      args->delos_root = argv[index];
      continue;
    }
    if (strcmp(arg, "--profile") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --profile");
      }
      index += 1;
      args->profile_name = argv[index];
      continue;
    }
    if (strcmp(arg, "--target") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --target");
      }
      index += 1;
      args->target_name = argv[index];
      continue;
    }
    if (strcmp(arg, "--compat") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --compat");
      }
      index += 1;
      args->compat_root = argv[index];
      continue;
    }
    if (strcmp(arg, "--format") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --format");
      }
      index += 1;
      args->format = argv[index];
      continue;
    }
    if (arg[0] == '-') {
      return confit_cli_write_error("unknown compat option");
    }
    return confit_cli_write_error("compat does not accept positional arguments");
  }

  if (args->parus_root == 0) {
    return confit_cli_write_error("compat requires --parus");
  }
  if (args->delos_root == 0) {
    return confit_cli_write_error("compat requires --delos");
  }
  if (args->profile_name == 0) {
    return confit_cli_write_error("compat requires --profile");
  }
  if (strcmp(args->format, "text") != 0 && strcmp(args->format, "json") != 0) {
    return confit_cli_write_error("compat --format must be text or json");
  }
  return CONFIT_OK;
}

static void confit_cli_free_project_bundle(ConfitProject *project,
                                           ConfitGraph *graph,
                                           ConfitResolvedConfig *config) {
  confit_resolved_config_free(config);
  confit_graph_free(graph);
  confit_project_free(project);
}

static int confit_cli_run_compat(int argc, char **argv) {
  ConfitCliCompatArgs args;
  ConfitDiagnostic diagnostic;
  ConfitProject *parus;
  ConfitProject *delos;
  ConfitGraph *parus_graph;
  ConfitGraph *delos_graph;
  ConfitResolvedConfig *parus_config;
  ConfitResolvedConfig *delos_config;
  ConfitCompatProject projects[2];
  ConfitCompatSuite *suite;
  ConfitCompatReport *report;
  char *json;
  char compat_path[1024];
  ConfitStatus status;

  status = confit_cli_parse_compat_args(argc, argv, &args);
  if (status != CONFIT_OK) {
    return confit_status_exit_code(status);
  }

  confit_diagnostic_init(&diagnostic);
  parus = 0;
  delos = 0;
  parus_graph = 0;
  delos_graph = 0;
  parus_config = 0;
  delos_config = 0;
  suite = 0;
  report = 0;
  json = 0;

  status = confit_cli_load_checked_project(args.parus_root, args.profile_name,
                                           args.target_name, 1, &parus,
                                           &parus_graph, &parus_config,
                                           0, &diagnostic);
  if (status == CONFIT_OK) {
    status = confit_cli_load_checked_project(args.delos_root, args.profile_name,
                                             args.target_name, 1, &delos,
                                             &delos_graph, &delos_config,
                                             0, &diagnostic);
  }
  if (status == CONFIT_OK) {
    if (args.compat_root != 0) {
      status = confit_host_path_join(compat_path, sizeof(compat_path),
                                     args.compat_root, "", &diagnostic);
    } else {
      status = confit_cli_join3(compat_path, sizeof(compat_path),
                                args.delos_root, "config", "compat",
                                &diagnostic);
    }
  }
  if (status == CONFIT_OK) {
    status = confit_compat_load_directory(compat_path, &suite, &diagnostic);
  }
  if (status == CONFIT_OK) {
    projects[0].project = parus;
    projects[0].config = parus_config;
    projects[1].project = delos;
    projects[1].config = delos_config;
    status = confit_compat_check_report(suite, projects, 2U, &report,
                                        &diagnostic);
  }
  if (status == CONFIT_OK || status == CONFIT_ERR_COMPATIBILITY) {
    if (strcmp(args.format, "json") == 0) {
      ConfitStatus json_status;

      json_status = confit_compat_report_to_json(report, &json);
      if (json_status != CONFIT_OK) {
        status = json_status;
        confit_diagnostic_set(&diagnostic, status, compat_path, 0, 0,
                              "failed to render compatibility report JSON");
      }
    }
  }

  if ((status == CONFIT_OK || status == CONFIT_ERR_COMPATIBILITY) &&
      strcmp(args.format, "json") == 0) {
    ConfitStatus write_status;

    write_status = confit_host_stdout_write(json);
    confit_compat_string_free(json);
    json = 0;
    confit_compat_report_free(report);
    confit_compat_suite_free(suite);
    confit_cli_free_project_bundle(parus, parus_graph, parus_config);
    confit_cli_free_project_bundle(delos, delos_graph, delos_config);
    if (write_status != CONFIT_OK) {
      return confit_status_exit_code(write_status);
    }
    return confit_status_exit_code(status);
  }

  confit_compat_string_free(json);
  confit_compat_report_free(report);
  confit_compat_suite_free(suite);
  confit_cli_free_project_bundle(parus, parus_graph, parus_config);
  confit_cli_free_project_bundle(delos, delos_graph, delos_config);
  if (status != CONFIT_OK) {
    return confit_cli_return_error(status, &diagnostic);
  }
  if (confit_cli_is_quiet()) {
    return confit_status_exit_code(CONFIT_OK);
  }
  status = confit_host_stdout_write_line("compat ok");
  return confit_status_exit_code(status);
}

static ConfitStatus confit_cli_parse_list_args(int argc, char **argv,
                                               ConfitCliListArgs *args) {
  int index;

  args->project_root = 0;
  args->category = 0;
  args->tag = 0;
  for (index = 2; index < argc; ++index) {
    const char *arg = argv[index];

    if (strcmp(arg, "--project") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --project");
      }
      index += 1;
      args->project_root = argv[index];
      continue;
    }
    if (strcmp(arg, "--category") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --category");
      }
      index += 1;
      args->category = argv[index];
      continue;
    }
    if (strcmp(arg, "--tag") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --tag");
      }
      index += 1;
      args->tag = argv[index];
      continue;
    }
    if (arg[0] == '-') {
      return confit_cli_write_error("unknown list option");
    }
    return confit_cli_write_error("list does not accept positional arguments");
  }

  if (args->project_root == 0) {
    return confit_cli_write_error("list requires --project");
  }
  return CONFIT_OK;
}

static int confit_cli_option_has_tag(const ConfitOption *option,
                                     const char *tag) {
  size_t index;

  if (tag == 0) {
    return 1;
  }
  for (index = 0U; index < option->tag_count; ++index) {
    if (option->tags[index] != 0 && strcmp(option->tags[index], tag) == 0) {
      return 1;
    }
  }
  return 0;
}

static int confit_cli_option_matches_list(const ConfitOption *option,
                                          const ConfitCliListArgs *args) {
  if (args->category != 0 &&
      (option->category == 0 ||
       strcmp(option->category, args->category) != 0)) {
    return 0;
  }
  return confit_cli_option_has_tag(option, args->tag);
}

static ConfitStatus confit_cli_print_option_row(const ConfitOption *option) {
  ConfitStatus status;
  const char *category;
  const char *prompt;

  category = option->category != 0 ? option->category : "-";
  prompt = option->prompt != 0 ? option->prompt : "-";
  status = confit_host_stdout_write(option->id);
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write("\t");
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write(confit_option_type_name(option->type));
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write("\t");
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write(category);
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write("\t");
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line(prompt);
  }
  return status;
}

static int confit_cli_run_list(int argc, char **argv) {
  ConfitCliListArgs args;
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitStatus status;
  size_t index;

  status = confit_cli_parse_list_args(argc, argv, &args);
  if (status != CONFIT_OK) {
    return confit_status_exit_code(status);
  }

  confit_diagnostic_init(&diagnostic);
  project = 0;
  status = confit_schema_load_project(args.project_root, &project, &diagnostic);
  if (status != CONFIT_OK) {
    return confit_cli_return_error(status, &diagnostic);
  }

  for (index = 0U; index < project->option_count; ++index) {
    if (!confit_cli_option_matches_list(&project->options[index], &args)) {
      continue;
    }
    status = confit_cli_print_option_row(&project->options[index]);
    if (status != CONFIT_OK) {
      confit_project_free(project);
      return confit_status_exit_code(status);
    }
  }

  confit_project_free(project);
  return confit_status_exit_code(CONFIT_OK);
}

static ConfitCliProfileCommand
confit_cli_profile_command_from_string(const char *text) {
  if (strcmp(text, "list") == 0) {
    return CONFIT_CLI_PROFILE_LIST;
  }
  if (strcmp(text, "show") == 0) {
    return CONFIT_CLI_PROFILE_SHOW;
  }
  if (strcmp(text, "new") == 0) {
    return CONFIT_CLI_PROFILE_NEW;
  }
  if (strcmp(text, "set") == 0) {
    return CONFIT_CLI_PROFILE_SET;
  }
  if (strcmp(text, "unset") == 0) {
    return CONFIT_CLI_PROFILE_UNSET;
  }
  if (strcmp(text, "validate") == 0) {
    return CONFIT_CLI_PROFILE_VALIDATE;
  }
  return CONFIT_CLI_PROFILE_INVALID;
}

static int confit_cli_profile_command_allows_base(
    ConfitCliProfileCommand command) {
  return command == CONFIT_CLI_PROFILE_NEW;
}

static int confit_cli_profile_command_allows_target(
    ConfitCliProfileCommand command) {
  return command == CONFIT_CLI_PROFILE_NEW;
}

static int confit_cli_profile_command_allows_force(
    ConfitCliProfileCommand command) {
  return command == CONFIT_CLI_PROFILE_NEW;
}

static ConfitStatus confit_cli_parse_profile_positional(
    ConfitCliProfileArgs *args, const char *arg, size_t *positional_count) {
  switch (args->command) {
  case CONFIT_CLI_PROFILE_LIST:
    return confit_cli_write_error("profile list does not accept arguments");
  case CONFIT_CLI_PROFILE_SHOW:
  case CONFIT_CLI_PROFILE_NEW:
  case CONFIT_CLI_PROFILE_VALIDATE:
    if (*positional_count != 0U) {
      return confit_cli_write_error("too many profile arguments");
    }
    args->profile_name = arg;
    break;
  case CONFIT_CLI_PROFILE_SET:
    if (*positional_count == 0U) {
      args->profile_name = arg;
    } else if (*positional_count == 1U) {
      args->assignment = arg;
    } else {
      return confit_cli_write_error("too many profile set arguments");
    }
    break;
  case CONFIT_CLI_PROFILE_UNSET:
    if (*positional_count == 0U) {
      args->profile_name = arg;
    } else if (*positional_count == 1U) {
      args->option_id = arg;
    } else {
      return confit_cli_write_error("too many profile unset arguments");
    }
    break;
  case CONFIT_CLI_PROFILE_INVALID:
  default:
    return confit_cli_write_error("unknown profile subcommand");
  }
  *positional_count += 1U;
  return CONFIT_OK;
}

static ConfitStatus confit_cli_parse_profile_args(int argc, char **argv,
                                                  ConfitCliProfileArgs *args) {
  int index;
  size_t positional_count;

  args->command = CONFIT_CLI_PROFILE_INVALID;
  args->project_root = 0;
  args->profile_name = 0;
  args->base_name = 0;
  args->target_name = 0;
  args->assignment = 0;
  args->option_id = 0;
  args->force = 0;

  if (argc < 3) {
    return confit_cli_write_error("profile requires a subcommand");
  }
  args->command = confit_cli_profile_command_from_string(argv[2]);
  if (args->command == CONFIT_CLI_PROFILE_INVALID) {
    return confit_cli_write_error("unknown profile subcommand");
  }

  positional_count = 0U;
  for (index = 3; index < argc; ++index) {
    const char *arg = argv[index];

    if (strcmp(arg, "--project") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --project");
      }
      index += 1;
      args->project_root = argv[index];
      continue;
    }
    if (strcmp(arg, "--base") == 0) {
      if (!confit_cli_profile_command_allows_base(args->command)) {
        return confit_cli_write_error("--base is only valid for profile new");
      }
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --base");
      }
      index += 1;
      args->base_name = argv[index];
      continue;
    }
    if (strcmp(arg, "--target") == 0) {
      if (!confit_cli_profile_command_allows_target(args->command)) {
        return confit_cli_write_error("--target is only valid for profile new");
      }
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --target");
      }
      index += 1;
      args->target_name = argv[index];
      continue;
    }
    if (strcmp(arg, "--force") == 0) {
      if (!confit_cli_profile_command_allows_force(args->command)) {
        return confit_cli_write_error("--force is only valid for profile new");
      }
      args->force = 1;
      continue;
    }
    if (arg[0] == '-') {
      return confit_cli_write_error("unknown profile option");
    }
    {
      ConfitStatus status;

      status =
          confit_cli_parse_profile_positional(args, arg, &positional_count);
      if (status != CONFIT_OK) {
        return status;
      }
    }
  }

  if (args->project_root == 0) {
    return confit_cli_write_error("profile requires --project");
  }
  if (args->command != CONFIT_CLI_PROFILE_LIST && args->profile_name == 0) {
    return confit_cli_write_error("profile command requires a profile name");
  }
  if (args->command == CONFIT_CLI_PROFILE_SET && args->assignment == 0) {
    return confit_cli_write_error("profile set requires option-id=value");
  }
  if (args->command == CONFIT_CLI_PROFILE_UNSET && args->option_id == 0) {
    return confit_cli_write_error("profile unset requires an option id");
  }
  return CONFIT_OK;
}

static int confit_cli_valid_profile_name(const char *name) {
  size_t index;

  if (name == 0 || name[0] == '\0') {
    return 0;
  }
  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
    return 0;
  }
  for (index = 0U; name[index] != '\0'; ++index) {
    unsigned char ch = (unsigned char)name[index];

    if (isalnum(ch) || ch == '_' || ch == '-' || ch == '.') {
      continue;
    }
    return 0;
  }
  return 1;
}

static ConfitProfile *confit_cli_find_profile(ConfitProject *project,
                                              const char *name) {
  size_t index;

  if (project == 0 || name == 0) {
    return 0;
  }
  for (index = 0U; index < project->profile_count; ++index) {
    if (project->profiles[index].name != 0 &&
        strcmp(project->profiles[index].name, name) == 0) {
      return &project->profiles[index];
    }
  }
  return 0;
}

static ConfitTarget *confit_cli_find_target(ConfitProject *project,
                                            const char *name) {
  size_t index;

  if (project == 0 || name == 0) {
    return 0;
  }
  for (index = 0U; index < project->target_count; ++index) {
    if (project->targets[index].name != 0 &&
        strcmp(project->targets[index].name, name) == 0) {
      return &project->targets[index];
    }
  }
  return 0;
}

static const ConfitOption *confit_cli_find_const_option(
    const ConfitProject *project, const char *option_id) {
  size_t index;

  if (project == 0 || option_id == 0) {
    return 0;
  }
  for (index = 0U; index < project->option_count; ++index) {
    if (project->options[index].id != 0 &&
        strcmp(project->options[index].id, option_id) == 0) {
      return &project->options[index];
    }
  }
  return 0;
}

static ConfitStatus confit_cli_named_value_replace(
    ConfitNamedValue *slot, const char *option_id, const ConfitValue *value,
    const char *source) {
  ConfitNamedValue replacement;
  ConfitStatus status;

  if (slot == 0 || option_id == 0 || value == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  confit_cli_named_value_init(&replacement);
  replacement.option_id = confit_cli_copy_string(option_id);
  if (source != 0) {
    replacement.source = confit_cli_copy_string(source);
  }
  if (replacement.option_id == 0 ||
      (source != 0 && replacement.source == 0)) {
    confit_cli_named_value_clear(&replacement);
    return CONFIT_ERR_INTERNAL;
  }
  status = confit_value_copy(&replacement.value, value);
  if (status != CONFIT_OK) {
    confit_cli_named_value_clear(&replacement);
    return status;
  }

  confit_cli_named_value_clear(slot);
  *slot = replacement;
  return CONFIT_OK;
}

static size_t confit_cli_profile_value_index(const ConfitProfile *profile,
                                             const char *option_id,
                                             int *out_found) {
  size_t index;

  *out_found = 0;
  if (profile == 0 || option_id == 0) {
    return 0U;
  }
  for (index = 0U; index < profile->value_count; ++index) {
    if (profile->values[index].option_id != 0 &&
        strcmp(profile->values[index].option_id, option_id) == 0) {
      *out_found = 1;
      return index;
    }
  }
  return 0U;
}

static ConfitStatus confit_cli_profile_set_value(ConfitProfile *profile,
                                                 const char *option_id,
                                                 const ConfitValue *value) {
  size_t index;
  int found;

  index = confit_cli_profile_value_index(profile, option_id, &found);
  if (found) {
    return confit_cli_named_value_replace(&profile->values[index], option_id,
                                          value, 0);
  }
  return confit_profile_add_value(profile, option_id, value, 0);
}

static ConfitStatus confit_cli_profile_unset_value(ConfitProfile *profile,
                                                   const char *option_id,
                                                   ConfitDiagnostic *diagnostic) {
  size_t index;
  int found;

  index = confit_cli_profile_value_index(profile, option_id, &found);
  if (!found) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, option_id, 0,
                          0, "profile value is not set");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  confit_cli_named_value_clear(&profile->values[index]);
  for (; index + 1U < profile->value_count; ++index) {
    profile->values[index] = profile->values[index + 1U];
  }
  profile->value_count -= 1U;
  if (profile->value_count == 0U) {
    free(profile->values);
    profile->values = 0;
  }
  return CONFIT_OK;
}

static void confit_cli_profile_clear_values(ConfitProfile *profile) {
  if (profile == 0) {
    return;
  }
  confit_cli_named_values_clear(profile->values, profile->value_count);
  profile->values = 0;
  profile->value_count = 0U;
}

static int confit_cli_compare_named_value_ptrs(const void *left,
                                               const void *right) {
  const ConfitNamedValue *const *left_value =
      (const ConfitNamedValue *const *)left;
  const ConfitNamedValue *const *right_value =
      (const ConfitNamedValue *const *)right;

  return strcmp((*left_value)->option_id, (*right_value)->option_id);
}

static ConfitStatus confit_cli_append_profile_value_toml(
    ConfitCliTextBuilder *builder, const ConfitProject *project,
    const ConfitNamedValue *value, ConfitDiagnostic *diagnostic) {
  const ConfitOption *option;
  ConfitStatus status;

  option = confit_cli_find_const_option(project, value->option_id);
  if (option == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, value->option_id, 0, 0,
                          "cannot save unknown profile option");
    return CONFIT_ERR_SCHEMA;
  }

  status = confit_cli_text_append_quoted(builder, value->option_id);
  if (status == CONFIT_OK) {
    status = confit_cli_text_append(builder, " = ");
  }
  if (status == CONFIT_OK) {
    status = confit_cli_text_append_value(builder, option, &value->value);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_text_append(builder, "\n");
  }
  return status;
}

static ConfitStatus confit_cli_build_profile_toml(
    const ConfitProject *project, const ConfitProfile *profile,
    char **out_text, ConfitDiagnostic *diagnostic) {
  ConfitCliTextBuilder builder;
  const ConfitNamedValue **sorted_values;
  ConfitStatus status;
  size_t index;

  *out_text = 0;
  sorted_values = 0;
  confit_cli_text_builder_init(&builder);

  if (profile->value_count > 0U) {
    sorted_values = (const ConfitNamedValue **)malloc(
        profile->value_count * sizeof(sorted_values[0]));
    if (sorted_values == 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, profile->name, 0,
                            0, "failed to allocate profile value order");
      return CONFIT_ERR_INTERNAL;
    }
    for (index = 0U; index < profile->value_count; ++index) {
      sorted_values[index] = &profile->values[index];
    }
    qsort(sorted_values, profile->value_count, sizeof(sorted_values[0]),
          confit_cli_compare_named_value_ptrs);
  }

  status = confit_cli_text_append(&builder, "[profile]\nname = ");
  if (status == CONFIT_OK) {
    status = confit_cli_text_append_quoted(&builder, profile->name);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_text_append(&builder, "\nschema_version = 1\n");
  }
  if (status == CONFIT_OK && profile->base != 0) {
    status = confit_cli_text_append(&builder, "base = ");
    if (status == CONFIT_OK) {
      status = confit_cli_text_append_quoted(&builder, profile->base);
    }
    if (status == CONFIT_OK) {
      status = confit_cli_text_append(&builder, "\n");
    }
  }
  if (status == CONFIT_OK && profile->target != 0) {
    status = confit_cli_text_append(&builder, "target = ");
    if (status == CONFIT_OK) {
      status = confit_cli_text_append_quoted(&builder, profile->target);
    }
    if (status == CONFIT_OK) {
      status = confit_cli_text_append(&builder, "\n");
    }
  }
  if (status == CONFIT_OK) {
    status = confit_cli_text_append(&builder, "\n[values]\n");
  }
  for (index = 0U; status == CONFIT_OK && index < profile->value_count;
       ++index) {
    status = confit_cli_append_profile_value_toml(
        &builder, project, sorted_values[index], diagnostic);
  }

  free(sorted_values);
  if (status != CONFIT_OK) {
    free(builder.text);
    return status;
  }
  *out_text = builder.text;
  return CONFIT_OK;
}

static ConfitStatus confit_cli_find_config_root(const char *project_root,
                                                char *out, size_t out_size,
                                                ConfitDiagnostic *diagnostic) {
  char candidate[1024];
  ConfitStatus status;

  if (project_root == 0 || project_root[0] == '\0') {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "missing project root");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status = confit_host_path_join(candidate, sizeof(candidate), project_root,
                                 "project.toml", diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  if (confit_cli_path_has_file(candidate)) {
    if (strlen(project_root) + 1U > out_size) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                            project_root, 0, 0,
                            "config root buffer is too small");
      return CONFIT_ERR_INVALID_ARGUMENT;
    }
    memcpy(out, project_root, strlen(project_root) + 1U);
    return CONFIT_OK;
  }
  return confit_host_path_join(out, out_size, project_root, "config",
                               diagnostic);
}

static ConfitStatus confit_cli_profile_path(const char *project_root,
                                            const char *profile_name,
                                            char *out_dir, size_t out_dir_size,
                                            char *out_path,
                                            size_t out_path_size,
                                            ConfitDiagnostic *diagnostic) {
  char config_root[1024];
  char file_name[256];
  ConfitStatus status;

  status = confit_cli_find_config_root(project_root, config_root,
                                       sizeof(config_root), diagnostic);
  if (status == CONFIT_OK) {
    status = confit_host_path_join(out_dir, out_dir_size, config_root,
                                   "profiles", diagnostic);
  }
  if (status == CONFIT_OK &&
      snprintf(file_name, sizeof(file_name), "%s.toml", profile_name) >=
          (int)sizeof(file_name)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                          profile_name, 0, 0, "profile name is too long");
    status = CONFIT_ERR_INVALID_ARGUMENT;
  }
  if (status == CONFIT_OK) {
    status = confit_host_path_join(out_path, out_path_size, out_dir, file_name,
                                   diagnostic);
  }
  return status;
}

static ConfitStatus confit_cli_validate_profile_identity(
    ConfitProject *project, const char *profile_name, const char *base_name,
    const char *target_name, ConfitDiagnostic *diagnostic) {
  if (!confit_cli_valid_profile_name(profile_name)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                          profile_name, 0, 0, "invalid profile name");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  if (base_name != 0 && strcmp(base_name, profile_name) == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, profile_name, 0, 0,
                          "profile cannot base itself");
    return CONFIT_ERR_SCHEMA;
  }
  if (base_name != 0 && confit_cli_find_profile(project, base_name) == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, base_name, 0, 0,
                          "unknown base profile");
    return CONFIT_ERR_SCHEMA;
  }
  if (target_name != 0 && confit_cli_find_target(project, target_name) == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, target_name, 0, 0,
                          "unknown profile target");
    return CONFIT_ERR_SCHEMA;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_cli_validate_profile_resolve(
    ConfitProject *project, const char *profile_name, const char *target_name,
    ConfitDiagnostic *diagnostic) {
  ConfitResolvedConfig *config;
  ConfitStatus status;

  config = 0;
  status = confit_resolver_resolve(project, profile_name, target_name, 0, 0U,
                                   &config, diagnostic);
  confit_resolved_config_free(config);
  return status;
}

static ConfitStatus confit_cli_write_profile_file(
    const char *project_root, const ConfitProject *project,
    const ConfitProfile *profile, ConfitDiagnostic *diagnostic) {
  char profile_dir[1024];
  char profile_path[1024];
  char *toml;
  ConfitStatus status;

  toml = 0;
  status =
      confit_cli_profile_path(project_root, profile->name, profile_dir,
                              sizeof(profile_dir), profile_path,
                              sizeof(profile_path), diagnostic);
  if (status == CONFIT_OK) {
    status = confit_cli_build_profile_toml(project, profile, &toml,
                                           diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_make_directories(profile_dir, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_write_text_file(profile_path, toml, diagnostic);
  }
  free(toml);
  return status;
}

static ConfitStatus confit_cli_print_profile_list(
    const ConfitProject *project) {
  ConfitStatus status;
  size_t index;

  status = CONFIT_OK;
  for (index = 0U; status == CONFIT_OK && index < project->profile_count;
       ++index) {
    char buffer[64];
    const ConfitProfile *profile = &project->profiles[index];

    status = confit_host_stdout_write(profile->name);
    if (status == CONFIT_OK && profile->base != 0) {
      status = confit_host_stdout_write("\tbase=");
      if (status == CONFIT_OK) {
        status = confit_host_stdout_write(profile->base);
      }
    }
    if (status == CONFIT_OK && profile->target != 0) {
      status = confit_host_stdout_write("\ttarget=");
      if (status == CONFIT_OK) {
        status = confit_host_stdout_write(profile->target);
      }
    }
    if (status == CONFIT_OK) {
      (void)snprintf(buffer, sizeof(buffer), "\tvalues=%lu",
                     (unsigned long)profile->value_count);
      status = confit_host_stdout_write_line(buffer);
    }
  }
  return status;
}

static int confit_cli_run_profile_list(const ConfitCliProfileArgs *args) {
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitStatus status;

  confit_diagnostic_init(&diagnostic);
  project = 0;
  status = confit_schema_load_project(args->project_root, &project,
                                      &diagnostic);
  if (status == CONFIT_OK) {
    status = confit_cli_print_profile_list(project);
  }
  confit_project_free(project);
  if (status != CONFIT_OK) {
    return confit_cli_return_error(status, &diagnostic);
  }
  return confit_status_exit_code(CONFIT_OK);
}

static int confit_cli_run_profile_show(const ConfitCliProfileArgs *args) {
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitProfile *profile;
  char *toml;
  ConfitStatus status;

  confit_diagnostic_init(&diagnostic);
  project = 0;
  toml = 0;
  status = CONFIT_OK;
  if (!confit_cli_valid_profile_name(args->profile_name)) {
    confit_diagnostic_set(&diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                          args->profile_name, 0, 0, "invalid profile name");
    status = CONFIT_ERR_INVALID_ARGUMENT;
  }
  if (status == CONFIT_OK) {
    status = confit_schema_load_project(args->project_root, &project,
                                        &diagnostic);
  }
  profile = status == CONFIT_OK
                ? confit_cli_find_profile(project, args->profile_name)
                : 0;
  if (status == CONFIT_OK && profile == 0) {
    confit_diagnostic_set(&diagnostic, CONFIT_ERR_SCHEMA, args->profile_name, 0,
                          0, "unknown profile");
    status = CONFIT_ERR_SCHEMA;
  }
  if (status == CONFIT_OK) {
    status =
        confit_cli_build_profile_toml(project, profile, &toml, &diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write(toml);
  }

  free(toml);
  confit_project_free(project);
  if (status != CONFIT_OK) {
    return confit_cli_return_error(status, &diagnostic);
  }
  return confit_status_exit_code(CONFIT_OK);
}

static int confit_cli_run_profile_new(const ConfitCliProfileArgs *args) {
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitGraph *graph;
  ConfitProfile *profile;
  char profile_dir[1024];
  char profile_path[1024];
  ConfitStatus status;
  int file_exists;

  confit_diagnostic_init(&diagnostic);
  project = 0;
  graph = 0;
  profile = 0;
  file_exists = 0;

  status = confit_cli_load_project_graph(args->project_root, &project, &graph,
                                         0, &diagnostic);
  if (status == CONFIT_OK) {
    status = confit_cli_validate_profile_identity(
        project, args->profile_name, args->base_name, args->target_name,
        &diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_profile_path(args->project_root, args->profile_name,
                                     profile_dir, sizeof(profile_dir),
                                     profile_path, sizeof(profile_path),
                                     &diagnostic);
  }
  if (status == CONFIT_OK) {
    file_exists = confit_cli_path_has_file(profile_path);
    profile = confit_cli_find_profile(project, args->profile_name);
    if ((profile != 0 || file_exists) && !args->force) {
      confit_diagnostic_set(&diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                            args->profile_name, 0, 0,
                            "profile already exists");
      status = CONFIT_ERR_INVALID_ARGUMENT;
    }
  }
  if (status == CONFIT_OK && profile == 0) {
    status = confit_project_add_profile(project, &profile);
  }
  if (status == CONFIT_OK) {
    confit_cli_profile_clear_values(profile);
    status = confit_profile_set_identity(profile, args->profile_name,
                                         args->base_name);
  }
  if (status == CONFIT_OK) {
    status = confit_profile_set_target(profile, args->target_name);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_validate_profile_resolve(
        project, args->profile_name, args->target_name, &diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_write_profile_file(args->project_root, project, profile,
                                           &diagnostic);
  }

  confit_graph_free(graph);
  confit_project_free(project);
  if (status != CONFIT_OK) {
    return confit_cli_return_error(status, &diagnostic);
  }
  if (confit_cli_is_quiet()) {
    return confit_status_exit_code(CONFIT_OK);
  }
  status = confit_host_stdout_write("profile new ok: ");
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line(args->profile_name);
  }
  return confit_status_exit_code(status);
}

static int confit_cli_run_profile_set(const ConfitCliProfileArgs *args) {
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitGraph *graph;
  ConfitProfile *profile;
  ConfitNamedValue *overrides;
  size_t override_count;
  const char *sets[1];
  ConfitStatus status;

  confit_diagnostic_init(&diagnostic);
  project = 0;
  graph = 0;
  profile = 0;
  overrides = 0;
  override_count = 0U;
  sets[0] = args->assignment;

  status = CONFIT_OK;
  if (!confit_cli_valid_profile_name(args->profile_name)) {
    confit_diagnostic_set(&diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                          args->profile_name, 0, 0, "invalid profile name");
    status = CONFIT_ERR_INVALID_ARGUMENT;
  }
  if (status == CONFIT_OK) {
    status = confit_cli_load_project_graph(args->project_root, &project, &graph,
                                           0, &diagnostic);
  }
  if (status == CONFIT_OK) {
    profile = confit_cli_find_profile(project, args->profile_name);
    if (profile == 0) {
      confit_diagnostic_set(&diagnostic, CONFIT_ERR_SCHEMA,
                            args->profile_name, 0, 0, "unknown profile");
      status = CONFIT_ERR_SCHEMA;
    }
  }
  if (status == CONFIT_OK) {
    status = confit_cli_parse_overrides(project, sets, 1U, &overrides,
                                        &override_count, &diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_profile_set_value(
        profile, overrides[0].option_id, &overrides[0].value);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_validate_profile_resolve(
        project, args->profile_name, profile->target, &diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_write_profile_file(args->project_root, project, profile,
                                           &diagnostic);
  }

  confit_cli_named_values_clear(overrides, override_count);
  confit_graph_free(graph);
  confit_project_free(project);
  if (status != CONFIT_OK) {
    return confit_cli_return_error(status, &diagnostic);
  }
  if (confit_cli_is_quiet()) {
    return confit_status_exit_code(CONFIT_OK);
  }
  status = confit_host_stdout_write("profile set ok: ");
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line(args->profile_name);
  }
  return confit_status_exit_code(status);
}

static int confit_cli_run_profile_unset(const ConfitCliProfileArgs *args) {
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitGraph *graph;
  ConfitProfile *profile;
  ConfitOption *option;
  ConfitStatus status;
  int alias_used;
  int ambiguous;

  confit_diagnostic_init(&diagnostic);
  project = 0;
  graph = 0;
  profile = 0;
  alias_used = 0;
  ambiguous = 0;

  status = CONFIT_OK;
  if (!confit_cli_valid_profile_name(args->profile_name)) {
    confit_diagnostic_set(&diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                          args->profile_name, 0, 0, "invalid profile name");
    status = CONFIT_ERR_INVALID_ARGUMENT;
  }
  if (status == CONFIT_OK) {
    status = confit_cli_load_project_graph(args->project_root, &project, &graph,
                                           0, &diagnostic);
  }
  if (status == CONFIT_OK) {
    profile = confit_cli_find_profile(project, args->profile_name);
    if (profile == 0) {
      confit_diagnostic_set(&diagnostic, CONFIT_ERR_SCHEMA,
                            args->profile_name, 0, 0, "unknown profile");
      status = CONFIT_ERR_SCHEMA;
    }
  }
  option = status == CONFIT_OK
               ? confit_cli_find_option_by_id_or_alias(
                     project, args->option_id, &alias_used, &ambiguous)
               : 0;
  if (status == CONFIT_OK && ambiguous) {
    confit_diagnostic_set(&diagnostic, CONFIT_ERR_SCHEMA, args->option_id, 0,
                          0, "ambiguous deprecated alias");
    status = CONFIT_ERR_SCHEMA;
  }
  if (status == CONFIT_OK && option == 0) {
    confit_diagnostic_set(&diagnostic, CONFIT_ERR_SCHEMA, args->option_id, 0,
                          0, "unknown option");
    status = CONFIT_ERR_SCHEMA;
  }
  if (status == CONFIT_OK) {
    status =
        confit_cli_profile_unset_value(profile, option->id, &diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_validate_profile_resolve(
        project, args->profile_name, profile->target, &diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_write_profile_file(args->project_root, project, profile,
                                           &diagnostic);
  }

  (void)alias_used;
  confit_graph_free(graph);
  confit_project_free(project);
  if (status != CONFIT_OK) {
    return confit_cli_return_error(status, &diagnostic);
  }
  if (confit_cli_is_quiet()) {
    return confit_status_exit_code(CONFIT_OK);
  }
  status = confit_host_stdout_write("profile unset ok: ");
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line(args->profile_name);
  }
  return confit_status_exit_code(status);
}

static int confit_cli_run_profile_validate(const ConfitCliProfileArgs *args) {
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitGraph *graph;
  ConfitResolvedConfig *config;
  ConfitStatus status;

  confit_diagnostic_init(&diagnostic);
  project = 0;
  graph = 0;
  config = 0;
  status = CONFIT_OK;
  if (!confit_cli_valid_profile_name(args->profile_name)) {
    confit_diagnostic_set(&diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                          args->profile_name, 0, 0, "invalid profile name");
    status = CONFIT_ERR_INVALID_ARGUMENT;
  }
  if (status == CONFIT_OK) {
    status = confit_cli_load_checked_project(
        args->project_root, args->profile_name, 0, 1, &project, &graph,
        &config, 0, &diagnostic);
  }
  confit_resolved_config_free(config);
  confit_graph_free(graph);
  confit_project_free(project);
  if (status != CONFIT_OK) {
    return confit_cli_return_error(status, &diagnostic);
  }
  if (confit_cli_is_quiet()) {
    return confit_status_exit_code(CONFIT_OK);
  }
  status = confit_host_stdout_write("profile ok: ");
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line(args->profile_name);
  }
  return confit_status_exit_code(status);
}

static int confit_cli_run_profile(int argc, char **argv) {
  ConfitCliProfileArgs args;
  ConfitStatus status;

  status = confit_cli_parse_profile_args(argc, argv, &args);
  if (status != CONFIT_OK) {
    return confit_status_exit_code(status);
  }

  switch (args.command) {
  case CONFIT_CLI_PROFILE_LIST:
    return confit_cli_run_profile_list(&args);
  case CONFIT_CLI_PROFILE_SHOW:
    return confit_cli_run_profile_show(&args);
  case CONFIT_CLI_PROFILE_NEW:
    return confit_cli_run_profile_new(&args);
  case CONFIT_CLI_PROFILE_SET:
    return confit_cli_run_profile_set(&args);
  case CONFIT_CLI_PROFILE_UNSET:
    return confit_cli_run_profile_unset(&args);
  case CONFIT_CLI_PROFILE_VALIDATE:
    return confit_cli_run_profile_validate(&args);
  case CONFIT_CLI_PROFILE_INVALID:
  default:
    return confit_status_exit_code(
        confit_cli_write_error("unknown profile subcommand"));
  }
}

static ConfitStatus confit_cli_parse_graph_args(int argc, char **argv,
                                                ConfitCliGraphArgs *args) {
  int index;

  confit_cli_project_args_init(&args->project);
  args->format = "json";
  for (index = 2; index < argc; ++index) {
    const char *arg = argv[index];

    if (strcmp(arg, "--project") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --project");
      }
      index += 1;
      args->project.project_root = argv[index];
      continue;
    }
    if (strcmp(arg, "--profile") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --profile");
      }
      index += 1;
      args->project.profile_name = argv[index];
      continue;
    }
    if (strcmp(arg, "--target") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --target");
      }
      index += 1;
      args->project.target_name = argv[index];
      continue;
    }
    if (strcmp(arg, "--format") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --format");
      }
      index += 1;
      args->format = argv[index];
      continue;
    }
    if (arg[0] == '-') {
      return confit_cli_write_error("unknown graph option");
    }
    return confit_cli_write_error("graph does not accept positional arguments");
  }

  if (args->project.project_root == 0) {
    return confit_cli_write_error("graph requires --project");
  }
  if (strcmp(args->format, "json") != 0 && strcmp(args->format, "dot") != 0) {
    return confit_cli_write_error("graph --format must be json or dot");
  }
  return CONFIT_OK;
}

static ConfitStatus confit_cli_print_dot_quoted(const char *text) {
  ConfitStatus status;
  size_t index;

  status = confit_host_stdout_write("\"");
  for (index = 0U; status == CONFIT_OK && text[index] != '\0'; ++index) {
    if (text[index] == '"' || text[index] == '\\') {
      status = confit_host_stdout_write("\\");
    }
    if (status == CONFIT_OK) {
      char buffer[2];
      buffer[0] = text[index];
      buffer[1] = '\0';
      status = confit_host_stdout_write(buffer);
    }
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write("\"");
  }
  return status;
}

static ConfitStatus confit_cli_print_dot_graph(const ConfitGraph *graph) {
  ConfitStatus status;
  size_t index;

  status = confit_host_stdout_write_line("digraph confit {");
  for (index = 0U; status == CONFIT_OK && index < graph->node_count; ++index) {
    status = confit_host_stdout_write("  ");
    if (status == CONFIT_OK) {
      status = confit_cli_print_dot_quoted(graph->nodes[index].id);
    }
    if (status == CONFIT_OK) {
      status = confit_host_stdout_write_line(";");
    }
  }
  for (index = 0U; status == CONFIT_OK && index < graph->edge_count; ++index) {
    status = confit_host_stdout_write("  ");
    if (status == CONFIT_OK) {
      status = confit_cli_print_dot_quoted(graph->edges[index].from);
    }
    if (status == CONFIT_OK) {
      status = confit_host_stdout_write(" -> ");
    }
    if (status == CONFIT_OK) {
      status = confit_cli_print_dot_quoted(graph->edges[index].to);
    }
    if (status == CONFIT_OK) {
      status = confit_host_stdout_write(" [label=");
    }
    if (status == CONFIT_OK) {
      status = confit_cli_print_dot_quoted(
          confit_dependency_kind_name(graph->edges[index].kind));
    }
    if (status == CONFIT_OK) {
      status = confit_host_stdout_write_line("];");
    }
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line("}");
  }
  return status;
}

static int confit_cli_run_graph(int argc, char **argv) {
  ConfitCliGraphArgs args;
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitGraph *graph;
  ConfitResolvedConfig *config;
  char *json;
  ConfitStatus status;

  status = confit_cli_parse_graph_args(argc, argv, &args);
  if (status != CONFIT_OK) {
    return confit_status_exit_code(status);
  }

  confit_diagnostic_init(&diagnostic);
  project = 0;
  graph = 0;
  config = 0;
  json = 0;
  status = confit_cli_load_checked_project(
      args.project.project_root, args.project.profile_name,
      args.project.target_name, args.project.profile_name != 0, &project,
      &graph, &config, 0, &diagnostic);
  if (status == CONFIT_OK && strcmp(args.format, "dot") == 0) {
    status = confit_cli_print_dot_graph(graph);
  } else if (status == CONFIT_OK) {
    status = confit_graph_to_json(graph, &json);
    if (status == CONFIT_OK) {
      status = confit_host_stdout_write(json);
    } else {
      confit_diagnostic_set(&diagnostic, status, args.project.project_root, 0,
                            0, "failed to serialize graph");
    }
  }

  confit_graph_string_free(json);
  confit_resolved_config_free(config);
  confit_graph_free(graph);
  confit_project_free(project);
  if (status != CONFIT_OK) {
    return confit_cli_return_error(status, &diagnostic);
  }
  return confit_status_exit_code(CONFIT_OK);
}

static ConfitStatus confit_cli_parse_diff_args(int argc, char **argv,
                                               ConfitCliDiffArgs *args) {
  int index;

  args->project_root = 0;
  args->profile_name = 0;
  args->base_name = 0;
  args->target_name = 0;
  args->format = "text";
  for (index = 2; index < argc; ++index) {
    const char *arg = argv[index];

    if (strcmp(arg, "--project") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --project");
      }
      index += 1;
      args->project_root = argv[index];
      continue;
    }
    if (strcmp(arg, "--profile") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --profile");
      }
      index += 1;
      args->profile_name = argv[index];
      continue;
    }
    if (strcmp(arg, "--base") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --base");
      }
      index += 1;
      args->base_name = argv[index];
      continue;
    }
    if (strcmp(arg, "--target") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --target");
      }
      index += 1;
      args->target_name = argv[index];
      continue;
    }
    if (strcmp(arg, "--format") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --format");
      }
      index += 1;
      args->format = argv[index];
      continue;
    }
    if (arg[0] == '-') {
      return confit_cli_write_error("unknown diff option");
    }
    return confit_cli_write_error("diff does not accept positional arguments");
  }

  if (args->project_root == 0) {
    return confit_cli_write_error("diff requires --project");
  }
  if (args->profile_name == 0) {
    return confit_cli_write_error("diff requires --profile");
  }
  if (args->base_name == 0) {
    return confit_cli_write_error("diff requires --base");
  }
  if (strcmp(args->format, "text") != 0 && strcmp(args->format, "json") != 0) {
    return confit_cli_write_error("diff --format must be text or json");
  }
  return CONFIT_OK;
}

static int confit_cli_value_equals(const ConfitValue *left,
                                   const ConfitValue *right) {
  if (left == 0 || right == 0 || left->kind != right->kind) {
    return 0;
  }

  switch (left->kind) {
  case CONFIT_VALUE_BOOL:
    return left->as.bool_value == right->as.bool_value;
  case CONFIT_VALUE_INT:
    return left->as.int_value == right->as.int_value;
  case CONFIT_VALUE_UINT:
    return left->as.uint_value == right->as.uint_value;
  case CONFIT_VALUE_FLOAT:
    return left->as.float_value == right->as.float_value;
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
  case CONFIT_VALUE_PATH:
    if (left->as.string_value == 0 || right->as.string_value == 0) {
      return left->as.string_value == right->as.string_value;
    }
    return strcmp(left->as.string_value, right->as.string_value) == 0;
  case CONFIT_VALUE_EMPTY:
  default:
    return 1;
  }
}

static const ConfitOption *confit_cli_project_find_const_option(
    const ConfitProject *project, const char *option_id) {
  size_t index;

  if (project == 0 || option_id == 0) {
    return 0;
  }
  for (index = 0U; index < project->option_count; ++index) {
    if (project->options[index].id != 0 &&
        strcmp(project->options[index].id, option_id) == 0) {
      return &project->options[index];
    }
  }
  return 0;
}

static ConfitStatus confit_cli_append_nullable_json_string(
    ConfitCliTextBuilder *builder, const char *text) {
  if (text == 0 || text[0] == '\0') {
    return confit_cli_text_append(builder, "null");
  }
  return confit_cli_text_append_quoted(builder, text);
}

static ConfitStatus confit_cli_append_json_value(ConfitCliTextBuilder *builder,
                                                 const ConfitValue *value) {
  char buffer[128];

  switch (value->kind) {
  case CONFIT_VALUE_BOOL:
    return confit_cli_text_append(builder,
                                  value->as.bool_value ? "true" : "false");
  case CONFIT_VALUE_INT:
    (void)snprintf(buffer, sizeof(buffer), "%lld",
                   (long long)value->as.int_value);
    return confit_cli_text_append(builder, buffer);
  case CONFIT_VALUE_UINT:
    (void)snprintf(buffer, sizeof(buffer), "%llu",
                   (unsigned long long)value->as.uint_value);
    return confit_cli_text_append(builder, buffer);
  case CONFIT_VALUE_FLOAT:
    (void)snprintf(buffer, sizeof(buffer), "%.17g", value->as.float_value);
    return confit_cli_text_append(builder, buffer);
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
  case CONFIT_VALUE_PATH:
    return confit_cli_text_append_quoted(builder, value->as.string_value);
  case CONFIT_VALUE_EMPTY:
  default:
    return confit_cli_text_append(builder, "null");
  }
}

static ConfitStatus confit_cli_append_diff_text(
    ConfitCliTextBuilder *builder, const ConfitProject *project,
    const ConfitResolvedConfig *base_config,
    const ConfitResolvedConfig *profile_config, const ConfitCliDiffArgs *args,
    const char *base_target, const char *profile_target,
    size_t *out_change_count) {
  ConfitStatus status;
  size_t index;
  size_t change_count;
  char buffer[128];

  change_count = 0U;
  status = confit_cli_text_append(builder, "diff: ");
  if (status == CONFIT_OK) {
    status = confit_cli_text_append(builder, args->base_name);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_text_append(builder, " -> ");
  }
  if (status == CONFIT_OK) {
    status = confit_cli_text_append(builder, args->profile_name);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_text_append(builder, "\nbase target: ");
  }
  if (status == CONFIT_OK) {
    status = confit_cli_text_append(
        builder, base_target != 0 ? base_target : "<none>");
  }
  if (status == CONFIT_OK) {
    status = confit_cli_text_append(builder, "\nprofile target: ");
  }
  if (status == CONFIT_OK) {
    status = confit_cli_text_append(
        builder, profile_target != 0 ? profile_target : "<none>");
  }
  if (status == CONFIT_OK) {
    status = confit_cli_text_append(builder, "\n");
  }

  for (index = 0U; status == CONFIT_OK && index < profile_config->value_count;
       ++index) {
    const ConfitResolvedValue *profile_value = &profile_config->values[index];
    const ConfitResolvedValue *base_value =
        confit_resolved_config_find(base_config, profile_value->option_id);
    const ConfitOption *option;

    if (base_value == 0 ||
        !confit_cli_value_equals(&base_value->value, &profile_value->value)) {
      change_count += 1U;
      option = confit_cli_project_find_const_option(project,
                                                    profile_value->option_id);
      status = confit_cli_text_append(builder, profile_value->option_id);
      if (status == CONFIT_OK) {
        status = confit_cli_text_append(builder, "\n  base: ");
      }
      if (status == CONFIT_OK && base_value != 0) {
        status =
            confit_cli_text_append_value(builder, option, &base_value->value);
      } else if (status == CONFIT_OK) {
        status = confit_cli_text_append(builder, "<missing>");
      }
      if (status == CONFIT_OK) {
        status = confit_cli_text_append(builder, " (");
      }
      if (status == CONFIT_OK) {
        status = confit_cli_text_append(
            builder, base_value != 0 && base_value->source != 0
                         ? base_value->source
                         : "missing");
      }
      if (status == CONFIT_OK) {
        status = confit_cli_text_append(builder, ")\n  profile: ");
      }
      if (status == CONFIT_OK) {
        status = confit_cli_text_append_value(builder, option,
                                              &profile_value->value);
      }
      if (status == CONFIT_OK) {
        status = confit_cli_text_append(builder, " (");
      }
      if (status == CONFIT_OK) {
        status = confit_cli_text_append(
            builder, profile_value->source != 0 ? profile_value->source : "");
      }
      if (status == CONFIT_OK) {
        status = confit_cli_text_append(builder, ")\n");
      }
    }
  }

  (void)snprintf(buffer, sizeof(buffer), "changes: %lu\n",
                 (unsigned long)change_count);
  if (status == CONFIT_OK) {
    status = confit_cli_text_append(builder, buffer);
  }
  if (out_change_count != 0) {
    *out_change_count = change_count;
  }
  return status;
}

static ConfitStatus confit_cli_append_diff_json(
    ConfitCliTextBuilder *builder, const ConfitResolvedConfig *base_config,
    const ConfitResolvedConfig *profile_config, const ConfitCliDiffArgs *args,
    const char *base_target, const char *profile_target,
    size_t *out_change_count) {
  ConfitStatus status;
  size_t index;
  size_t change_count;
  int first_change;
  char buffer[128];

  change_count = 0U;
  first_change = 1;
  status = confit_cli_text_append(builder, "{\n  \"schema\": ");
  if (status == CONFIT_OK) {
    status = confit_cli_text_append_quoted(builder, "confit-diff-v1");
  }
  if (status == CONFIT_OK) {
    status = confit_cli_text_append(builder, ",\n  \"project\": ");
  }
  if (status == CONFIT_OK) {
    status = confit_cli_text_append_quoted(builder, args->project_root);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_text_append(builder, ",\n  \"base\": ");
  }
  if (status == CONFIT_OK) {
    status = confit_cli_text_append_quoted(builder, args->base_name);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_text_append(builder, ",\n  \"profile\": ");
  }
  if (status == CONFIT_OK) {
    status = confit_cli_text_append_quoted(builder, args->profile_name);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_text_append(builder, ",\n  \"base_target\": ");
  }
  if (status == CONFIT_OK) {
    status = confit_cli_append_nullable_json_string(builder, base_target);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_text_append(builder, ",\n  \"profile_target\": ");
  }
  if (status == CONFIT_OK) {
    status = confit_cli_append_nullable_json_string(builder, profile_target);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_text_append(builder, ",\n  \"changes\": [");
  }

  for (index = 0U; status == CONFIT_OK && index < profile_config->value_count;
       ++index) {
    const ConfitResolvedValue *profile_value = &profile_config->values[index];
    const ConfitResolvedValue *base_value =
        confit_resolved_config_find(base_config, profile_value->option_id);

    if (base_value != 0 &&
        confit_cli_value_equals(&base_value->value, &profile_value->value)) {
      continue;
    }

    change_count += 1U;
    status = confit_cli_text_append(builder, first_change ? "\n" : ",\n");
    first_change = 0;
    if (status == CONFIT_OK) {
      status = confit_cli_text_append(builder, "    {\"id\": ");
    }
    if (status == CONFIT_OK) {
      status = confit_cli_text_append_quoted(builder, profile_value->option_id);
    }
    if (status == CONFIT_OK) {
      status = confit_cli_text_append(builder, ", \"base\": ");
    }
    if (status == CONFIT_OK && base_value != 0) {
      status = confit_cli_append_json_value(builder, &base_value->value);
    } else if (status == CONFIT_OK) {
      status = confit_cli_text_append(builder, "null");
    }
    if (status == CONFIT_OK) {
      status = confit_cli_text_append(builder, ", \"base_source\": ");
    }
    if (status == CONFIT_OK) {
      status = confit_cli_append_nullable_json_string(
          builder, base_value != 0 ? base_value->source : 0);
    }
    if (status == CONFIT_OK) {
      status = confit_cli_text_append(builder, ", \"profile\": ");
    }
    if (status == CONFIT_OK) {
      status = confit_cli_append_json_value(builder, &profile_value->value);
    }
    if (status == CONFIT_OK) {
      status = confit_cli_text_append(builder, ", \"profile_source\": ");
    }
    if (status == CONFIT_OK) {
      status =
          confit_cli_append_nullable_json_string(builder, profile_value->source);
    }
    if (status == CONFIT_OK) {
      status = confit_cli_text_append(builder, "}");
    }
  }

  if (status == CONFIT_OK) {
    status = confit_cli_text_append(builder, first_change ? "]" : "\n  ]");
  }
  (void)snprintf(buffer, sizeof(buffer),
                 ",\n  \"summary\": {\"changed\": %lu}\n}\n",
                 (unsigned long)change_count);
  if (status == CONFIT_OK) {
    status = confit_cli_text_append(builder, buffer);
  }
  if (out_change_count != 0) {
    *out_change_count = change_count;
  }
  return status;
}

static ConfitStatus confit_cli_build_diff_output(
    const ConfitProject *project, const ConfitResolvedConfig *base_config,
    const ConfitResolvedConfig *profile_config, const ConfitCliDiffArgs *args,
    char **out_text, ConfitDiagnostic *diagnostic) {
  ConfitCliTextBuilder builder;
  const char *base_target;
  const char *profile_target;
  size_t change_count;
  ConfitStatus status;

  *out_text = 0;
  change_count = 0U;
  base_target =
      args->target_name != 0
          ? args->target_name
          : confit_cli_effective_target_name(project, args->base_name, 0);
  profile_target =
      args->target_name != 0
          ? args->target_name
          : confit_cli_effective_target_name(project, args->profile_name, 0);

  confit_cli_text_builder_init(&builder);
  if (strcmp(args->format, "json") == 0) {
    status = confit_cli_append_diff_json(&builder, base_config, profile_config,
                                         args, base_target, profile_target,
                                         &change_count);
  } else {
    status = confit_cli_append_diff_text(&builder, project, base_config,
                                         profile_config, args, base_target,
                                         profile_target, &change_count);
  }
  (void)change_count;

  if (status != CONFIT_OK) {
    free(builder.text);
    confit_diagnostic_set(diagnostic, status, args->project_root, 0, 0,
                          "failed to serialize diff");
    return status;
  }
  *out_text = builder.text;
  return CONFIT_OK;
}

static int confit_cli_run_diff(int argc, char **argv) {
  ConfitCliDiffArgs args;
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitGraph *graph;
  ConfitResolvedConfig *base_config;
  ConfitResolvedConfig *profile_config;
  char *output;
  ConfitStatus status;

  status = confit_cli_parse_diff_args(argc, argv, &args);
  if (status != CONFIT_OK) {
    return confit_status_exit_code(status);
  }

  confit_diagnostic_init(&diagnostic);
  project = 0;
  graph = 0;
  base_config = 0;
  profile_config = 0;
  output = 0;

  status =
      confit_cli_load_project_graph(args.project_root, &project, &graph, 0,
                                    &diagnostic);
  if (status == CONFIT_OK) {
    status = confit_resolver_resolve(project, args.base_name, args.target_name,
                                     0, 0U, &base_config, &diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_resolver_resolve(project, args.profile_name,
                                     args.target_name, 0, 0U, &profile_config,
                                     &diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_build_diff_output(project, base_config, profile_config,
                                          &args, &output, &diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write(output);
  }

  free(output);
  confit_resolved_config_free(profile_config);
  confit_resolved_config_free(base_config);
  confit_graph_free(graph);
  confit_project_free(project);
  if (status != CONFIT_OK) {
    return confit_cli_return_error(status, &diagnostic);
  }
  return confit_status_exit_code(CONFIT_OK);
}

static ConfitStatus confit_cli_completion_append_command_words(
    ConfitCliTextBuilder *builder) {
  ConfitStatus status;
  size_t index;

  status = confit_cli_text_append(builder, "help");
  for (index = 0U; status == CONFIT_OK && index < confit_cli_command_count;
       ++index) {
    status = confit_cli_text_append(builder, " ");
    if (status == CONFIT_OK) {
      status = confit_cli_text_append(builder, confit_cli_commands[index].name);
    }
  }
  return status;
}

static ConfitStatus confit_cli_completion_bash(char **out_text) {
  ConfitCliTextBuilder builder;
  ConfitStatus status;

  *out_text = 0;
  confit_cli_text_builder_init(&builder);

#define CONFIT_COMPLETION_APPEND(fragment)                                      \
  do {                                                                          \
    status = confit_cli_text_append(&builder, (fragment));                      \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

#define CONFIT_COMPLETION_SECTION(call_expr)                                    \
  do {                                                                          \
    status = (call_expr);                                                       \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

  CONFIT_COMPLETION_APPEND("# confit bash completion\n");
  CONFIT_COMPLETION_APPEND("_confit()\n{\n");
  CONFIT_COMPLETION_APPEND(
      "  local cur prev commands globals artifacts formats shells templates\n");
  CONFIT_COMPLETION_APPEND("  COMPREPLY=()\n");
  CONFIT_COMPLETION_APPEND("  cur=\"${COMP_WORDS[COMP_CWORD]}\"\n");
  CONFIT_COMPLETION_APPEND("  prev=\"${COMP_WORDS[COMP_CWORD-1]}\"\n");
  CONFIT_COMPLETION_APPEND("  commands=\"");
  CONFIT_COMPLETION_SECTION(
      confit_cli_completion_append_command_words(&builder));
  CONFIT_COMPLETION_APPEND("\"\n");
  CONFIT_COMPLETION_APPEND(
      "  globals=\"--help --version --color --quiet --verbose\"\n");
  CONFIT_COMPLETION_APPEND(
      "  artifacts=\"header reports cmake qstar all\"\n");
  CONFIT_COMPLETION_APPEND("  formats=\"text json toml dot\"\n");
  CONFIT_COMPLETION_APPEND("  shells=\"bash zsh fish\"\n");
  CONFIT_COMPLETION_APPEND("  templates=\"minimal delos parus\"\n");
  CONFIT_COMPLETION_APPEND("  case \"$prev\" in\n");
  CONFIT_COMPLETION_APPEND(
      "    --artifact) COMPREPLY=( $(compgen -W \"$artifacts\" -- \"$cur\") ); return 0 ;;\n");
  CONFIT_COMPLETION_APPEND(
      "    --color) COMPREPLY=( $(compgen -W \"auto always never\" -- \"$cur\") ); return 0 ;;\n");
  CONFIT_COMPLETION_APPEND(
      "    --format) COMPREPLY=( $(compgen -W \"$formats\" -- \"$cur\") ); return 0 ;;\n");
  CONFIT_COMPLETION_APPEND(
      "    --shell) COMPREPLY=( $(compgen -W \"$shells\" -- \"$cur\") ); return 0 ;;\n");
  CONFIT_COMPLETION_APPEND(
      "    --template) COMPREPLY=( $(compgen -W \"$templates\" -- \"$cur\") ); return 0 ;;\n");
  CONFIT_COMPLETION_APPEND("  esac\n");
  CONFIT_COMPLETION_APPEND("  if [ \"$COMP_CWORD\" -eq 1 ]; then\n");
  CONFIT_COMPLETION_APPEND(
      "    COMPREPLY=( $(compgen -W \"$commands $globals\" -- \"$cur\") )\n");
  CONFIT_COMPLETION_APPEND("    return 0\n");
  CONFIT_COMPLETION_APPEND("  fi\n");
  CONFIT_COMPLETION_APPEND(
      "  COMPREPLY=( $(compgen -W \"$globals --project --profile --target --out --set --format --strict --dry-run --force --artifact --category --tag --parus --delos --compat --base --schema-edit --template --shell\" -- \"$cur\") )\n");
  CONFIT_COMPLETION_APPEND("}\n");
  CONFIT_COMPLETION_APPEND("complete -F _confit confit\n");

#undef CONFIT_COMPLETION_APPEND
#undef CONFIT_COMPLETION_SECTION

  *out_text = builder.text;
  return CONFIT_OK;
}

static ConfitStatus confit_cli_completion_zsh(char **out_text) {
  ConfitCliTextBuilder builder;
  ConfitStatus status;

  *out_text = 0;
  confit_cli_text_builder_init(&builder);

#define CONFIT_ZSH_APPEND(fragment)                                             \
  do {                                                                          \
    status = confit_cli_text_append(&builder, (fragment));                      \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

#define CONFIT_ZSH_SECTION(call_expr)                                           \
  do {                                                                          \
    status = (call_expr);                                                       \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

  CONFIT_ZSH_APPEND("#compdef confit\n");
  CONFIT_ZSH_APPEND("# confit zsh completion\n\n");
  CONFIT_ZSH_APPEND("_confit() {\n");
  CONFIT_ZSH_APPEND("  local -a commands\n");
  CONFIT_ZSH_APPEND("  commands=(");
  CONFIT_ZSH_SECTION(confit_cli_completion_append_command_words(&builder));
  CONFIT_ZSH_APPEND(")\n");
  CONFIT_ZSH_APPEND("  _arguments -C \\\n");
  CONFIT_ZSH_APPEND(
      "    '--help[show help]' '--version[show version]' \\\n");
  CONFIT_ZSH_APPEND(
      "    '--color[control diagnostic color]:color:(auto always never)' \\\n");
  CONFIT_ZSH_APPEND(
      "    '--quiet[suppress non-essential output]' '--verbose[print diagnostic context]' \\\n");
  CONFIT_ZSH_APPEND("    '1:command:($commands)' \\\n");
  CONFIT_ZSH_APPEND("    '*::arg:->args'\n");
  CONFIT_ZSH_APPEND("  case $words[2] in\n");
  CONFIT_ZSH_APPEND(
      "    completion) _arguments '--shell:shell:(bash zsh fish)' ;;\n");
  CONFIT_ZSH_APPEND(
      "    gen) _arguments '--artifact:artifact:(header reports cmake qstar all)' '--format:format:(text json toml dot)' ;;\n");
  CONFIT_ZSH_APPEND(
      "    init) _arguments '--template:template:(minimal delos parus)' ;;\n");
  CONFIT_ZSH_APPEND("  esac\n");
  CONFIT_ZSH_APPEND("}\n");
  CONFIT_ZSH_APPEND("_confit \"$@\"\n");

#undef CONFIT_ZSH_APPEND
#undef CONFIT_ZSH_SECTION

  *out_text = builder.text;
  return CONFIT_OK;
}

static ConfitStatus confit_cli_completion_fish(char **out_text) {
  ConfitCliTextBuilder builder;
  ConfitStatus status;
  size_t index;

  *out_text = 0;
  confit_cli_text_builder_init(&builder);

#define CONFIT_FISH_APPEND(fragment)                                            \
  do {                                                                          \
    status = confit_cli_text_append(&builder, (fragment));                      \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

  CONFIT_FISH_APPEND("# confit fish completion\n");
  CONFIT_FISH_APPEND("complete -c confit -f\n");
  CONFIT_FISH_APPEND(
      "complete -c confit -n '__fish_use_subcommand' -a 'help doctor init check resolve gen explain list graph diff compat profile tui completion'\n");
  CONFIT_FISH_APPEND("complete -c confit -l help -d 'Show help'\n");
  CONFIT_FISH_APPEND("complete -c confit -l version -d 'Show version'\n");
  CONFIT_FISH_APPEND(
      "complete -c confit -l color -xa 'auto always never' -d 'Control diagnostic color'\n");
  CONFIT_FISH_APPEND(
      "complete -c confit -l quiet -d 'Suppress non-essential output'\n");
  CONFIT_FISH_APPEND(
      "complete -c confit -l verbose -d 'Print diagnostic context'\n");
  for (index = 0U; status == CONFIT_OK && index < confit_cli_command_count;
       ++index) {
    CONFIT_FISH_APPEND("complete -c confit -n '__fish_use_subcommand' -a '");
    CONFIT_FISH_APPEND(confit_cli_commands[index].name);
    CONFIT_FISH_APPEND("' -d ");
    status =
        confit_cli_text_append_quoted(&builder, confit_cli_commands[index].summary);
    if (status != CONFIT_OK) {
      free(builder.text);
      return status;
    }
    CONFIT_FISH_APPEND("\n");
  }
  CONFIT_FISH_APPEND(
      "complete -c confit -n '__fish_seen_subcommand_from completion' -l shell -xa 'bash zsh fish'\n");
  CONFIT_FISH_APPEND(
      "complete -c confit -n '__fish_seen_subcommand_from gen' -l artifact -xa 'header reports cmake qstar all'\n");
  CONFIT_FISH_APPEND(
      "complete -c confit -n '__fish_seen_subcommand_from init' -l template -xa 'minimal delos parus'\n");

#undef CONFIT_FISH_APPEND

  *out_text = builder.text;
  return CONFIT_OK;
}

static ConfitStatus confit_cli_parse_completion_args(
    int argc, char **argv, ConfitCliCompletionArgs *args) {
  int index;

  args->shell = 0;
  for (index = 2; index < argc; ++index) {
    const char *arg = argv[index];

    if (strcmp(arg, "--shell") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --shell");
      }
      index += 1;
      args->shell = argv[index];
      continue;
    }
    if (arg[0] == '-') {
      return confit_cli_write_error("unknown completion option");
    }
    return confit_cli_write_error(
        "completion does not accept positional arguments");
  }
  if (args->shell == 0) {
    return confit_cli_write_error("completion requires --shell");
  }
  if (strcmp(args->shell, "bash") != 0 && strcmp(args->shell, "zsh") != 0 &&
      strcmp(args->shell, "fish") != 0) {
    return confit_cli_write_error("completion --shell must be bash, zsh, or fish");
  }
  return CONFIT_OK;
}

static int confit_cli_run_completion(int argc, char **argv) {
  ConfitCliCompletionArgs args;
  ConfitStatus status;
  char *text;

  status = confit_cli_parse_completion_args(argc, argv, &args);
  if (status != CONFIT_OK) {
    return confit_status_exit_code(status);
  }

  text = 0;
  if (strcmp(args.shell, "bash") == 0) {
    status = confit_cli_completion_bash(&text);
  } else if (strcmp(args.shell, "zsh") == 0) {
    status = confit_cli_completion_zsh(&text);
  } else {
    status = confit_cli_completion_fish(&text);
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write(text);
  }
  free(text);
  return confit_status_exit_code(status);
}

static ConfitStatus confit_cli_parse_tui_args(int argc, char **argv,
                                              ConfitTuiOptions *options) {
  int index;

  options->project_root = 0;
  options->profile_name = 0;
  options->target_name = 0;
  options->schema_edit = 0;

  for (index = 2; index < argc; ++index) {
    const char *arg = argv[index];

    if (strcmp(arg, "--project") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --project");
      }
      index += 1;
      options->project_root = argv[index];
      continue;
    }
    if (strcmp(arg, "--profile") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --profile");
      }
      index += 1;
      options->profile_name = argv[index];
      continue;
    }
    if (strcmp(arg, "--target") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --target");
      }
      index += 1;
      options->target_name = argv[index];
      continue;
    }
    if (strcmp(arg, "--schema-edit") == 0) {
      options->schema_edit = 1;
      continue;
    }
    if (arg[0] == '-') {
      return confit_cli_write_error("unknown tui option");
    }
    return confit_cli_write_error("tui does not accept positional arguments");
  }

  if (options->project_root == 0) {
    return confit_cli_write_error("tui requires --project");
  }
  if (!options->schema_edit && options->profile_name == 0) {
    return confit_cli_write_error("tui requires --profile");
  }
  return CONFIT_OK;
}

static int confit_cli_run_tui(int argc, char **argv) {
  ConfitTuiOptions options;
  ConfitDiagnostic diagnostic;
  ConfitStatus status;

  status = confit_cli_parse_tui_args(argc, argv, &options);
  if (status != CONFIT_OK) {
    return confit_status_exit_code(status);
  }
  confit_diagnostic_init(&diagnostic);
  status = confit_tui_run(&options, &diagnostic);
  if (status != CONFIT_OK) {
    return confit_cli_return_error(status, &diagnostic);
  }
  return confit_status_exit_code(CONFIT_OK);
}

int main(int argc, char **argv) {
  ConfitCliGlobalArgs global_args;
  const ConfitCliCommandSpec *command;
  ConfitStatus status;
  int normalized_argc;
  char **normalized_argv;
  int exit_code;

  if (argc > 0 && argv[0] != 0) {
    confit_cli_executable_path = argv[0];
  }

  normalized_argc = 0;
  normalized_argv = 0;
  exit_code = 0;
  status =
      confit_cli_normalize_args(argc, argv, &global_args, &normalized_argc,
                                &normalized_argv);
  if (status != CONFIT_OK) {
    return confit_status_exit_code(status);
  }
  confit_cli_global_args = global_args;

  if (normalized_argc <= 1) {
    exit_code = confit_status_exit_code(confit_cli_print_help());
    goto cleanup;
  }

  if (strcmp(normalized_argv[1], "--version") == 0 ||
      strcmp(normalized_argv[1], "version") == 0) {
    exit_code = confit_status_exit_code(
        confit_host_stdout_write_line(confit_version_string()));
    goto cleanup;
  }

  if (strcmp(normalized_argv[1], "--help") == 0 ||
      strcmp(normalized_argv[1], "-h") == 0) {
    exit_code = confit_status_exit_code(confit_cli_print_help());
    goto cleanup;
  }

  if (strcmp(normalized_argv[1], "help") == 0) {
    exit_code = confit_cli_run_help(normalized_argc, normalized_argv);
    goto cleanup;
  }

  command = confit_cli_find_command(normalized_argv[1]);
  if (command != 0) {
    if (normalized_argc == 3 && confit_cli_is_help_arg(normalized_argv[2])) {
      exit_code =
          confit_status_exit_code(confit_cli_print_command_help(command));
      goto cleanup;
    }
    if (command->handler == 0) {
      exit_code = confit_cli_run_unsupported_command(command);
      goto cleanup;
    }
    exit_code = command->handler(normalized_argc, normalized_argv);
    goto cleanup;
  }

  status = confit_host_stderr_write("confit: unknown command or option: ");
  if (status == CONFIT_OK) {
    status = confit_host_stderr_write(normalized_argv[1]);
  }
  if (status == CONFIT_OK) {
    status = confit_host_stderr_write("\n");
  }
  if (status == CONFIT_OK) {
    status = confit_host_stderr_write_line("try 'confit help'");
  }
  if (status != CONFIT_OK) {
    exit_code = confit_status_exit_code(status);
    goto cleanup;
  }

  exit_code = confit_status_exit_code(CONFIT_ERR_INVALID_ARGUMENT);

cleanup:
  free(normalized_argv);
  return exit_code;
}
