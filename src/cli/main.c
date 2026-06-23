#include <string.h>

#include "confit/diagnostic.h"
#include "confit/explain.h"
#include "confit/graph.h"
#include "confit/host.h"
#include "confit/resolver.h"
#include "confit/schema.h"
#include "confit/status.h"
#include "confit/version.h"

typedef struct ConfitCliExplainArgs {
  const char *project_root;
  const char *profile_name;
  const char *target_name;
  const char *option_id;
} ConfitCliExplainArgs;

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
  status = confit_host_stdout_write(
      "  confit explain --project <path> --profile <name> "
      "[--target <name>] <option-id>\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("Commands:\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("  help        Show this help text.\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("  --version   Show Confit version.\n");
  if (status != CONFIT_OK) {
    return status;
  }
  return confit_host_stdout_write(
      "  explain     Explain one resolved option.\n");
}

static ConfitStatus confit_cli_parse_explain_args(int argc, char **argv,
                                                  ConfitCliExplainArgs *args) {
  int index;

  args->project_root = 0;
  args->profile_name = 0;
  args->target_name = 0;
  args->option_id = 0;

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
    if (strcmp(arg, "--target") == 0) {
      if (index + 1 >= argc) {
        return confit_cli_write_error("missing value for --target");
      }
      index += 1;
      args->target_name = argv[index];
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

  if (args->project_root == 0) {
    return confit_cli_write_error("explain requires --project");
  }
  if (args->profile_name == 0) {
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

  status = confit_schema_load_project(args.project_root, &project, &diagnostic);
  if (status != CONFIT_OK) {
    return confit_cli_return_error(status, &diagnostic);
  }

  status = confit_graph_build(project, &graph, &diagnostic);
  if (status == CONFIT_OK) {
    status = confit_graph_validate(graph, &diagnostic);
  }
  if (status != CONFIT_OK) {
    confit_graph_free(graph);
    confit_project_free(project);
    return confit_cli_return_error(status, &diagnostic);
  }

  status = confit_resolver_resolve(project, args.profile_name, args.target_name,
                                   0, 0U, &config, &diagnostic);
  if (status != CONFIT_OK) {
    confit_graph_free(graph);
    confit_project_free(project);
    return confit_cli_return_error(status, &diagnostic);
  }

  status = confit_explain_option(project, config, args.option_id, &explanation,
                                 &diagnostic);
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

  if (strcmp(argv[1], "explain") == 0) {
    return confit_cli_run_explain(argc, argv);
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
