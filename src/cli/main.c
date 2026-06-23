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

typedef struct ConfitCliGenArgs {
  ConfitCliProjectArgs project;
  const char *out_dir;
} ConfitCliGenArgs;

typedef struct ConfitCliExplainArgs {
  ConfitCliProjectArgs project;
  const char *option_id;
} ConfitCliExplainArgs;

typedef struct ConfitCliCompatArgs {
  const char *parus_root;
  const char *delos_root;
  const char *profile_name;
  const char *target_name;
  const char *compat_root;
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

static int confit_cli_run_check(int argc, char **argv);
static int confit_cli_run_gen(int argc, char **argv);
static int confit_cli_run_explain(int argc, char **argv);
static int confit_cli_run_compat(int argc, char **argv);
static int confit_cli_run_list(int argc, char **argv);
static int confit_cli_run_graph(int argc, char **argv);
static int confit_cli_run_tui(int argc, char **argv);

static const ConfitCliCommandSpec confit_cli_help_spec = {
    "help", "Show global help or command-specific help.",
    "confit help [command]", "Use `confit help <command>` for details.", 0};

static const ConfitCliCommandSpec confit_cli_commands[] = {
    {"doctor",
     "Check host installation, platform support, project layout, and "
     "generators.",
     "confit doctor [--project <path>]",
     "--project <path>", 0},
    {"init", "Create a Confit project skeleton from a named template.",
     "confit init --project <path> --template minimal|delos|parus [--force] "
     "[--dry-run]",
     "--project <path>\n  --template minimal|delos|parus\n  --force\n  "
     "--dry-run",
     0},
    {"check",
     "Parse project TOML, validate schema and graph, and resolve a profile.",
     "confit check --project <path> --profile <name> [--target <name>] "
     "[--strict]",
     "--project <path>\n  --profile <name>\n  --target <name>",
     confit_cli_run_check},
    {"resolve", "Emit the resolved configuration without writing artifacts.",
     "confit resolve --project <path> --profile <name> [--target <name>] "
     "[--set <id=value>] [--format text|json|toml]",
     "--project <path>\n  --profile <name>\n  --target <name>\n  "
     "--set <id=value>\n  --format text|json|toml",
     0},
    {"gen", "Generate deterministic configuration artifacts.",
     "confit gen --project <path> --profile <name> [--target <name>] --out "
     "<path> [--artifact header|reports|cmake|qstar|all] [--force] "
     "[--dry-run]",
     "--project <path>\n  --profile <name>\n  --target <name>\n  --out <path>",
     confit_cli_run_gen},
    {"explain", "Explain one resolved option value.",
     "confit explain --project <path> --profile <name> [--target <name>] "
     "<option-id>",
     "--project <path>\n  --profile <name>\n  --target <name>",
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
     0},
    {"compat", "Check compatibility assertions across project roots.",
     "confit compat --parus <path> --delos <path> --profile <name> "
     "[--target <name>] [--compat <path>]",
     "--parus <path>\n  --delos <path>\n  --profile <name>\n  --target "
     "<name>\n  --compat <path>",
     confit_cli_run_compat},
    {"profile", "Manage profile TOML without opening the TUI.",
     "confit profile list|new|set|unset ...",
     "Subcommands: list, new, set, unset", 0},
    {"tui", "Open the terminal UI where supported.",
     "confit tui --project <path> --profile <name> [--target <name>]\n"
     "confit tui --project <path> --schema-edit",
     "--project <path>\n  --profile <name>\n  --target <name>\n  "
     "--schema-edit",
     confit_cli_run_tui},
    {"completion", "Emit shell completion text.",
     "confit completion --shell bash|zsh|fish",
     "--shell bash|zsh|fish", 0},
};

static const size_t confit_cli_command_count =
    sizeof(confit_cli_commands) / sizeof(confit_cli_commands[0]);

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
    status = confit_host_stdout_write("\nUsage:\n  ");
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write_line(command->usage);
  }
  if (status == CONFIT_OK && command->options != 0) {
    status = confit_host_stdout_write("\nOptions:\n  ");
  }
  if (status == CONFIT_OK && command->options != 0) {
    status = confit_host_stdout_write_line(command->options);
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

static void confit_cli_project_args_init(ConfitCliProjectArgs *args) {
  args->project_root = 0;
  args->profile_name = 0;
  args->target_name = 0;
}

static ConfitStatus confit_cli_parse_project_args(
    int argc, char **argv, int start_index, ConfitCliProjectArgs *args,
    int require_profile, const char *unknown_message,
    const char *extra_message) {
  int index;

  confit_cli_project_args_init(args);
  for (index = start_index; index < argc; ++index) {
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
    if (strcmp(arg, "--target") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --target");
      }
      index += 1;
      args->target_name = argv[index];
      continue;
    }
    if (arg[0] == '-') {
      return confit_cli_write_error(unknown_message);
    }
    return confit_cli_write_error(extra_message);
  }

  if (args->project_root == 0) {
    return confit_cli_write_error("command requires --project");
  }
  if (require_profile && args->profile_name == 0) {
    return confit_cli_write_error("command requires --profile");
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

static ConfitStatus confit_cli_load_checked_project(
    const char *project_root, const char *profile_name, const char *target_name,
    int resolve_profile, ConfitProject **out_project, ConfitGraph **out_graph,
    ConfitResolvedConfig **out_config, ConfitDiagnostic *diagnostic) {
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

  status = confit_schema_load_project(project_root, &project, diagnostic);
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

static int confit_cli_run_check(int argc, char **argv) {
  ConfitCliProjectArgs args;
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitGraph *graph;
  ConfitResolvedConfig *config;
  ConfitStatus status;

  status = confit_cli_parse_project_args(
      argc, argv, 2, &args, 1, "unknown check option",
      "check does not accept positional arguments");
  if (status != CONFIT_OK) {
    return confit_status_exit_code(status);
  }

  confit_diagnostic_init(&diagnostic);
  project = 0;
  graph = 0;
  config = 0;
  status = confit_cli_load_checked_project(args.project_root, args.profile_name,
                                           args.target_name, 1, &project,
                                           &graph, &config, &diagnostic);
  confit_resolved_config_free(config);
  confit_graph_free(graph);
  confit_project_free(project);
  if (status != CONFIT_OK) {
    return confit_cli_return_error(status, &diagnostic);
  }

  status = confit_host_stdout_write_line("check ok");
  return confit_status_exit_code(status);
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

static ConfitStatus confit_cli_generate_artifacts(
    const ConfitProject *project, const ConfitGraph *graph,
    const ConfitResolvedConfig *config, const ConfitCliGenArgs *args,
    ConfitDiagnostic *diagnostic) {
  const char *target_name;
  ConfitConfigHeaderOptions header_options;
  ConfitReportOptions report_options;
  char *header;
  char *report_json;
  char *explain_text;
  char *graph_json;
  char *inputs_json;
  ConfitStatus status;

  target_name = confit_cli_effective_target_name(
      project, args->project.profile_name, args->project.target_name);
  header_options.profile_name = args->project.profile_name;
  header_options.target_name = target_name;
  report_options.profile_name = args->project.profile_name;
  report_options.target_name = target_name;
  report_options.input_files = 0;
  report_options.input_file_count = 0U;
  header = 0;
  report_json = 0;
  explain_text = 0;
  graph_json = 0;
  inputs_json = 0;

  status = confit_generate_config_header(project, config, &header_options,
                                         &header, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_generate_report_json(project, config, &report_options,
                                         &report_json, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_generate_explain_report(project, config, &report_options,
                                            &explain_text, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_graph_to_json(graph, &graph_json);
    if (status != CONFIT_OK) {
      confit_diagnostic_set(diagnostic, status, args->out_dir, 0, 0,
                            "failed to generate graph report");
    }
  }
  if (status == CONFIT_OK) {
    status = confit_generate_inputs_json(project, &report_options, &inputs_json,
                                         diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_make_directories(args->out_dir, diagnostic);
  }
  if (status == CONFIT_OK) {
    status =
        confit_cli_write_artifact(args->out_dir, "config.h", header,
                                  diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_write_artifact(args->out_dir, "config.report.json",
                                       report_json, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_write_artifact(args->out_dir, "config.explain.txt",
                                       explain_text, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_write_artifact(args->out_dir, "config.graph.json",
                                       graph_json, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_write_artifact(args->out_dir, "config.inputs.json",
                                       inputs_json, diagnostic);
  }

  confit_generator_string_free(header);
  confit_generator_string_free(report_json);
  confit_generator_string_free(explain_text);
  confit_graph_string_free(graph_json);
  confit_generator_string_free(inputs_json);
  return status;
}

static ConfitStatus confit_cli_parse_gen_args(int argc, char **argv,
                                              ConfitCliGenArgs *args) {
  int index;

  confit_cli_project_args_init(&args->project);
  args->out_dir = 0;
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

  confit_diagnostic_init(&diagnostic);
  project = 0;
  graph = 0;
  config = 0;
  status = confit_cli_load_checked_project(
      args.project.project_root, args.project.profile_name,
      args.project.target_name, 1, &project, &graph, &config, &diagnostic);
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
  args->option_id = 0;
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
    if (arg[0] == '-') {
      return confit_cli_write_error("unknown explain option");
    }
    if (args->option_id != 0) {
      return confit_cli_write_error("too many explain option ids");
    }
    args->option_id = arg;
  }

  if (args->project.project_root == 0) {
    return confit_cli_write_error("explain requires --project");
  }
  if (args->project.profile_name == 0) {
    return confit_cli_write_error("explain requires --profile");
  }
  if (args->option_id == 0) {
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
  explanation = 0;
  status = confit_cli_load_checked_project(
      args.project.project_root, args.project.profile_name,
      args.project.target_name, 1, &project, &graph, &config, &diagnostic);
  if (status == CONFIT_OK) {
    status = confit_explain_option(project, config, args.option_id,
                                   &explanation, &diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_stdout_write(explanation);
  }

  confit_explain_string_free(explanation);
  confit_resolved_config_free(config);
  confit_graph_free(graph);
  confit_project_free(project);

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

  status = confit_cli_load_checked_project(args.parus_root, args.profile_name,
                                           args.target_name, 1, &parus,
                                           &parus_graph, &parus_config,
                                           &diagnostic);
  if (status == CONFIT_OK) {
    status = confit_cli_load_checked_project(args.delos_root, args.profile_name,
                                             args.target_name, 1, &delos,
                                             &delos_graph, &delos_config,
                                             &diagnostic);
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
    status = confit_compat_check(suite, projects, 2U, &diagnostic);
  }

  confit_compat_suite_free(suite);
  confit_cli_free_project_bundle(parus, parus_graph, parus_config);
  confit_cli_free_project_bundle(delos, delos_graph, delos_config);
  if (status != CONFIT_OK) {
    return confit_cli_return_error(status, &diagnostic);
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
      &graph, &config, &diagnostic);
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

  status = confit_cli_parse_global_args(argc, argv, &global_args);
  if (status != CONFIT_OK) {
    return confit_status_exit_code(status);
  }

  if (global_args.command_index >= argc) {
    return confit_status_exit_code(confit_cli_print_help());
  }

  normalized_argc = argc - global_args.command_index + 1;
  normalized_argv = argv + global_args.command_index - 1;

  if (strcmp(normalized_argv[1], "--version") == 0 ||
      strcmp(normalized_argv[1], "version") == 0) {
    return confit_status_exit_code(
        confit_host_stdout_write_line(confit_version_string()));
  }

  if (strcmp(normalized_argv[1], "--help") == 0 ||
      strcmp(normalized_argv[1], "-h") == 0) {
    return confit_status_exit_code(confit_cli_print_help());
  }

  if (strcmp(normalized_argv[1], "help") == 0) {
    return confit_cli_run_help(normalized_argc, normalized_argv);
  }

  command = confit_cli_find_command(normalized_argv[1]);
  if (command != 0) {
    if (normalized_argc == 3 && confit_cli_is_help_arg(normalized_argv[2])) {
      return confit_status_exit_code(confit_cli_print_command_help(command));
    }
    if (command->handler == 0) {
      return confit_cli_run_unsupported_command(command);
    }
    return command->handler(normalized_argc, normalized_argv);
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
    return confit_status_exit_code(status);
  }

  return confit_status_exit_code(CONFIT_ERR_INVALID_ARGUMENT);
}
