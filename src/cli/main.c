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
  status = confit_host_stdout_write(
      "  confit check --project <path> --profile <name> "
      "[--target <name>]\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write(
      "  confit gen --project <path> --profile <name> "
      "[--target <name>] --out <path>\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write(
      "  confit explain --project <path> --profile <name> "
      "[--target <name>] <option-id>\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write(
      "  confit compat --parus <path> --delos <path> --profile <name> "
      "[--target <name>] [--compat <path>]\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write(
      "  confit list --project <path> [--category <name>] [--tag <name>]\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write(
      "  confit graph --project <path> [--profile <name>] "
      "[--target <name>] [--format json|dot]\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write(
      "  confit tui --project <path> --profile <name> "
      "[--target <name>]\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write(
      "  confit tui --project <path> --schema-edit\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("\nCommands:\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("  check       Validate schema, graph, and profile resolution.\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("  gen         Generate config.h and deterministic reports.\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("  explain     Explain one resolved option.\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("  compat      Check cross-project compatibility assertions.\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("  list        List option schema entries.\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("  graph       Emit dependency graph JSON or DOT.\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("  tui         Start the terminal UI shell.\n");
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_host_stdout_write("  --version   Show Confit version.\n");
  return status;
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

  if (strcmp(argv[1], "check") == 0) {
    return confit_cli_run_check(argc, argv);
  }
  if (strcmp(argv[1], "gen") == 0) {
    return confit_cli_run_gen(argc, argv);
  }
  if (strcmp(argv[1], "explain") == 0) {
    return confit_cli_run_explain(argc, argv);
  }
  if (strcmp(argv[1], "compat") == 0) {
    return confit_cli_run_compat(argc, argv);
  }
  if (strcmp(argv[1], "list") == 0) {
    return confit_cli_run_list(argc, argv);
  }
  if (strcmp(argv[1], "graph") == 0) {
    return confit_cli_run_graph(argc, argv);
  }
  if (strcmp(argv[1], "tui") == 0) {
    return confit_cli_run_tui(argc, argv);
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
