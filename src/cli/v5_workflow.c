#include "v5_workflow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/diagnostic.h"
#include "confit/digest.h"
#include "confit/generation_v5.h"
#include "confit/host.h"
#include "confit/status.h"
#include "confit/version.h"
#include "confit/workflow_v5.h"

enum { CONFIT_CLI_PATH_BYTES = 4096U };

static int option_value(int argc, char **argv, const char *name,
                        const char **out) {
  int found = 0;
  for (int index = 2; index < argc; ++index) {
    if (strcmp(argv[index], name) != 0) continue;
    if (found != 0 || index + 1 >= argc || argv[index + 1][0] == '\0') return 0;
    *out = argv[index + 1];
    found = 1;
    ++index;
  }
  return found;
}

static ConfitStatus self_identity(ConfitV5ToolIdentity *out,
                                  char path[CONFIT_CLI_PATH_BYTES],
                                  char digest[65],
                                  ConfitDiagnostic *diagnostic) {
  ConfitStatus status = confit_host_self_executable(
      path, CONFIT_CLI_PATH_BYTES, diagnostic);
  if (status == CONFIT_OK)
    status = confit_v5_sha256_file(path, digest, diagnostic);
  if (status == CONFIT_OK) {
    out->path = path;
    out->version = confit_version_string();
    out->sha256 = digest;
  }
  return status;
}

static int report_status(const char *operation, ConfitStatus status,
                         const ConfitDiagnostic *diagnostic) {
  if (status != CONFIT_OK)
    (void)fprintf(stderr, "confit: %s failed: %s: %s\n", operation,
                  confit_status_name(status),
                  diagnostic != 0 && diagnostic->message != 0
                      ? diagnostic->message
                      : "no diagnostic");
  return confit_status_exit_code(status);
}

int confit_cli_v5_configure(int argc, char **argv) {
  const char *repository = 0;
  const char *output = 0;
  const char *architecture = 0;
  const char *kernconf = 0;
  const char *transaction_id = 0;
  ConfitV5ConfigureRequest request;
  ConfitV5GenerationTransaction *transaction = 0;
  ConfitDiagnostic diagnostic;
  ConfitStatus status;
  char self[CONFIT_CLI_PATH_BYTES];
  char digest[65];
  if (argc != 12 ||
      !option_value(argc, argv, "--repository", &repository) ||
      !option_value(argc, argv, "--out", &output) ||
      !option_value(argc, argv, "--arch", &architecture) ||
      !option_value(argc, argv, "--kernconf", &kernconf) ||
      !option_value(argc, argv, "--transaction", &transaction_id)) {
    (void)fprintf(stderr,
                  "confit: configure requires --repository ABS --out ABS "
                  "--arch ATOM --kernconf ATOM --transaction ATOM\n");
    return confit_status_exit_code(CONFIT_ERR_INVALID_ARGUMENT);
  }
  confit_diagnostic_init(&diagnostic);
  memset(&request, 0, sizeof(request));
  status = self_identity(&request.resolver, self, digest, &diagnostic);
  request.verifier = request.resolver;
  request.repository_root = repository;
  request.output_root = output;
  request.architecture = architecture;
  request.kernconf = kernconf;
  request.transaction_id = transaction_id;
  if (status == CONFIT_OK)
    status = confit_v5_generation_preview(&request, &transaction, &diagnostic);
  if (status == CONFIT_OK)
    status = confit_v5_generation_apply(transaction, &diagnostic);
  if (status == CONFIT_OK)
    (void)printf("generation=%s\n",
                 confit_v5_generation_digest(transaction));
  if (transaction != 0) {
    ConfitStatus cancel_status =
        confit_v5_generation_cancel(&transaction, &diagnostic);
    if (status == CONFIT_OK && cancel_status != CONFIT_OK)
      status = cancel_status;
  }
  return report_status("configure", status, &diagnostic);
}

int confit_cli_v5_verify(int argc, char **argv) {
  const char *repository = 0;
  const char *generation = 0;
  const char *expected_architecture = 0;
  const char *expected_kernconf = 0;
  ConfitV5ToolIdentity verifier;
  ConfitDiagnostic diagnostic;
  ConfitStatus status;
  char self[CONFIT_CLI_PATH_BYTES];
  char digest[65];
  if (argc != 10 ||
      !option_value(argc, argv, "--repository", &repository) ||
      !option_value(argc, argv, "--generation", &generation) ||
      !option_value(argc, argv, "--expected-architecture",
                    &expected_architecture) ||
      !option_value(argc, argv, "--expected-kernconf", &expected_kernconf)) {
    (void)fprintf(stderr,
                  "confit: verify requires --repository ABS "
                  "--generation ABS --expected-architecture ATOM "
                  "--expected-kernconf ATOM\n");
    return confit_status_exit_code(CONFIT_ERR_INVALID_ARGUMENT);
  }
  confit_diagnostic_init(&diagnostic);
  status = self_identity(&verifier, self, digest, &diagnostic);
  if (status == CONFIT_OK)
    status = confit_v5_configseal_verify(
        generation, repository, expected_architecture, expected_kernconf,
        &verifier, &diagnostic);
  if (status == CONFIT_OK) (void)printf("configseal=verified\n");
  return report_status("verify", status, &diagnostic);
}

static ConfitStatus workflow_open(const char *repository,
                                  const char *architecture,
                                  const char *kernconf,
                                  ConfitV5Workflow **out,
                                  ConfitDiagnostic *diagnostic) {
  ConfitV5CatalogRequest request;
  request.repository_root = repository;
  request.architecture = architecture;
  request.kernconf = kernconf;
  return confit_v5_workflow_open(&request, out, diagnostic);
}

static int common_arguments(int argc, char **argv, const char **repository,
                            const char **architecture, const char **kernconf) {
  return argc >= 8 && option_value(argc, argv, "--repository", repository) &&
         option_value(argc, argv, "--arch", architecture) &&
         option_value(argc, argv, "--kernconf", kernconf);
}

static int print_owned_text(char *text) {
  if (text == 0) return confit_status_exit_code(CONFIT_ERR_INTERNAL);
  (void)fputs(text, stdout);
  free(text);
  return 0;
}

static int command_search(int argc, char **argv, const char *repository,
                          const char *architecture, const char *kernconf,
                          ConfitDiagnostic *diagnostic) {
  const char *query = 0;
  ConfitV5Workflow *workflow = 0;
  ConfitStatus status;
  if (argc != 10 || !option_value(argc, argv, "--query", &query))
    return report_status("search", CONFIT_ERR_INVALID_ARGUMENT, diagnostic);
  status = workflow_open(repository, architecture, kernconf, &workflow,
                         diagnostic);
  for (size_t match = 0U; status == CONFIT_OK; ++match) {
    size_t row_index;
    ConfitV5WorkflowRow row;
    if (!confit_v5_workflow_search(workflow, query, match, &row_index)) break;
    if (!confit_v5_workflow_row(workflow, row_index, &row)) {
      status = CONFIT_ERR_INTERNAL;
      break;
    }
    (void)printf("%s\t%s\t%s\t%s\n", row.option.symbol, row.value,
                 row.available ? "available" : "unavailable",
                 row.option.prompt);
  }
  confit_v5_workflow_free(workflow);
  return report_status("search", status, diagnostic);
}

static int command_explain(int argc, char **argv, const char *repository,
                           const char *architecture, const char *kernconf,
                           int blockers, ConfitDiagnostic *diagnostic) {
  const char *symbol = 0;
  ConfitV5Workflow *workflow = 0;
  ConfitStatus status;
  char *text = 0;
  size_t size = 0U;
  if (argc != 10 || !option_value(argc, argv, "--symbol", &symbol))
    return report_status(blockers ? "why-unavailable" : "explain",
                         CONFIT_ERR_INVALID_ARGUMENT, diagnostic);
  status = workflow_open(repository, architecture, kernconf, &workflow,
                         diagnostic);
  if (status == CONFIT_OK)
    status = confit_v5_workflow_explain(workflow, symbol, blockers, &text,
                                        &size, diagnostic);
  (void)size;
  if (status == CONFIT_OK) (void)print_owned_text(text);
  else free(text);
  confit_v5_workflow_free(workflow);
  return report_status(blockers ? "why-unavailable" : "explain", status,
                       diagnostic);
}

static int command_list_new(const char *operation, int argc,
                            const char *repository, const char *architecture,
                            const char *kernconf,
                            ConfitDiagnostic *diagnostic) {
  ConfitV5Workflow *workflow = 0;
  ConfitStatus status;
  if (argc != 8)
    return report_status(operation, CONFIT_ERR_INVALID_ARGUMENT, diagnostic);
  status = workflow_open(repository, architecture, kernconf, &workflow,
                         diagnostic);
  for (size_t index = 0U;
       status == CONFIT_OK && index < confit_v5_workflow_row_count(workflow);
       ++index) {
    ConfitV5WorkflowRow row;
    if (!confit_v5_workflow_row(workflow, index, &row)) {
      status = CONFIT_ERR_INTERNAL;
      break;
    }
    if (row.origin == CONFIT_V5_VALUE_ORIGIN_DEFAULT)
      (void)printf("new=%s\tdefault=%s\t%s\n", row.option.symbol, row.value,
                   row.option.prompt);
  }
  if (status == CONFIT_OK && strcmp(operation, "oldconfig") == 0) {
    char *minimal = 0;
    size_t size = 0U;
    status = confit_v5_workflow_minimal(workflow, &minimal, &size, diagnostic);
    if (status == CONFIT_OK) {
      (void)printf("minimal-bytes=%zu\n", size);
      free(minimal);
    }
  }
  confit_v5_workflow_free(workflow);
  return report_status(operation, status, diagnostic);
}

static int command_save_minimal(int argc, char **argv, const char *repository,
                                const char *architecture,
                                const char *kernconf,
                                ConfitDiagnostic *diagnostic) {
  const char *output = 0;
  ConfitV5Workflow *workflow = 0;
  ConfitStatus status;
  char *minimal = 0;
  size_t size = 0U;
  int changed = 0;
  if (argc != 10 || !option_value(argc, argv, "--output", &output) ||
      output[0] != '/')
    return report_status("save-minimal", CONFIT_ERR_INVALID_ARGUMENT,
                         diagnostic);
  status = workflow_open(repository, architecture, kernconf, &workflow,
                         diagnostic);
  if (status == CONFIT_OK)
    status = confit_v5_workflow_minimal(workflow, &minimal, &size, diagnostic);
  if (status == CONFIT_OK)
    status = confit_host_write_text_file_if_changed_atomic(
        output, minimal, &changed, diagnostic);
  if (status == CONFIT_OK)
    (void)printf("minimal=%s\nbytes=%zu\n", changed ? "updated" : "unchanged",
                 size);
  free(minimal);
  confit_v5_workflow_free(workflow);
  return report_status("save-minimal", status, diagnostic);
}

static int command_diff(int argc, char **argv, const char *repository,
                        const char *architecture, const char *kernconf,
                        ConfitDiagnostic *diagnostic) {
  const char *other = 0;
  ConfitV5Workflow *left = 0;
  ConfitV5Workflow *right = 0;
  ConfitStatus status;
  size_t changes = 0U;
  if (argc != 10 || !option_value(argc, argv, "--other", &other))
    return report_status("diff", CONFIT_ERR_INVALID_ARGUMENT, diagnostic);
  status = workflow_open(repository, architecture, kernconf, &left, diagnostic);
  if (status == CONFIT_OK)
    status = workflow_open(repository, architecture, other, &right, diagnostic);
  if (status == CONFIT_OK && confit_v5_workflow_row_count(left) !=
                                 confit_v5_workflow_row_count(right))
    status = CONFIT_ERR_CONFLICT;
  for (size_t index = 0U;
       status == CONFIT_OK && index < confit_v5_workflow_row_count(left);
       ++index) {
    ConfitV5WorkflowRow left_row;
    ConfitV5WorkflowRow right_row;
    if (!confit_v5_workflow_row(left, index, &left_row) ||
        !confit_v5_workflow_row(right, index, &right_row) ||
        strcmp(left_row.option.symbol, right_row.option.symbol) != 0) {
      status = CONFIT_ERR_CONFLICT;
      break;
    }
    if (strcmp(left_row.value, right_row.value) != 0) {
      (void)printf("%s\t%s -> %s\n", left_row.option.symbol, left_row.value,
                   right_row.value);
      ++changes;
    }
  }
  if (status == CONFIT_OK) (void)printf("changes=%zu\n", changes);
  confit_v5_workflow_free(right);
  confit_v5_workflow_free(left);
  return report_status("diff", status, diagnostic);
}

static ConfitStatus kernconf_path(char out[CONFIT_CLI_PATH_BYTES],
                                  const char *repository,
                                  const char *architecture,
                                  const char *kernconf) {
  int length = snprintf(out, CONFIT_CLI_PATH_BYTES,
                        "%s/config/kernconf/%s/%s.toml", repository,
                        architecture, kernconf);
  return length > 0 && length < CONFIT_CLI_PATH_BYTES ? CONFIT_OK
                                                       : CONFIT_ERR_SCHEMA;
}

static int command_tui(int argc, char **argv, const char *repository,
                       const char *architecture, const char *kernconf,
                       ConfitDiagnostic *diagnostic) {
  const char *output = 0;
  const char *transaction_id = 0;
  ConfitV5Workflow *workflow = 0;
  ConfitStatus status;
  size_t selected = 0U;
  char query[CONFIT_V5_WORKFLOW_QUERY_MAX + 1U] = {0};
  char input[512];
  if (argc != 12 || !option_value(argc, argv, "--out", &output) ||
      !option_value(argc, argv, "--transaction", &transaction_id))
    return report_status("tui", CONFIT_ERR_INVALID_ARGUMENT, diagnostic);
  status = workflow_open(repository, architecture, kernconf, &workflow,
                         diagnostic);
  while (status == CONFIT_OK) {
    char *screen = 0;
    size_t screen_size = 0U;
    status = confit_v5_workflow_render(workflow, selected, query, 100U, 30U,
                                       &screen, &screen_size, diagnostic);
    if (status != CONFIT_OK) break;
    (void)fputs("\033[2J\033[H", stdout);
    (void)fwrite(screen, 1U, screen_size, stdout);
    free(screen);
    (void)fputs("confit> ", stdout);
    (void)fflush(stdout);
    if (fgets(input, sizeof(input), stdin) == 0) break;
    input[strcspn(input, "\r\n")] = '\0';
    ConfitV5TuiAction action;
    status = confit_v5_tui_decode(input, &action, diagnostic);
    if (status != CONFIT_OK) {
      (void)fprintf(stderr, "confit: ignored invalid TUI input: %s\n",
                    diagnostic->message != 0 ? diagnostic->message
                                             : "invalid input");
      status = CONFIT_OK;
      continue;
    }
    if (action.kind == CONFIT_V5_TUI_ACTION_CANCEL)
      break;
    if (action.kind == CONFIT_V5_TUI_ACTION_DOWN &&
        selected + 1U < confit_v5_workflow_row_count(workflow))
      ++selected;
    else if (action.kind == CONFIT_V5_TUI_ACTION_UP &&
             selected != 0U)
      --selected;
    else if (action.kind == CONFIT_V5_TUI_ACTION_SEARCH) {
      size_t query_size = strlen(action.value);
      if (query_size > CONFIT_V5_WORKFLOW_QUERY_MAX) {
        status = CONFIT_ERR_INVALID_ARGUMENT;
        break;
      }
      memcpy(query, action.value, query_size + 1U);
    }
    else if (action.kind == CONFIT_V5_TUI_ACTION_SET)
      status = confit_v5_workflow_set(workflow, action.symbol, action.value,
                                      diagnostic);
    else if (action.kind == CONFIT_V5_TUI_ACTION_PREVIEW) {
      char *minimal = 0;
      size_t size = 0U;
      status = confit_v5_workflow_minimal(workflow, &minimal, &size,
                                          diagnostic);
      if (status == CONFIT_OK) {
        (void)printf("\n--- preview (%zu bytes) ---\n%s", size, minimal);
        free(minimal);
      }
    } else if (action.kind == CONFIT_V5_TUI_ACTION_APPLY) {
      char path[CONFIT_CLI_PATH_BYTES];
      char *minimal = 0;
      char *original = 0;
      size_t minimal_size = 0U;
      size_t original_size = 0U;
      int changed = 0;
      ConfitV5ConfigureRequest request;
      ConfitV5GenerationTransaction *generation = 0;
      char self[CONFIT_CLI_PATH_BYTES];
      char digest[65];
      memset(&request, 0, sizeof(request));
      status = kernconf_path(path, repository, architecture, kernconf);
      if (status == CONFIT_OK)
        status = confit_host_read_text_file(path, &original, &original_size,
                                            diagnostic);
      if (status == CONFIT_OK)
        status = confit_v5_workflow_minimal(workflow, &minimal, &minimal_size,
                                            diagnostic);
      if (status == CONFIT_OK)
        status = confit_host_write_text_file_if_changed_atomic(
            path, minimal, &changed, diagnostic);
      if (status == CONFIT_OK)
        status = self_identity(&request.resolver, self, digest, diagnostic);
      request.verifier = request.resolver;
      request.repository_root = repository;
      request.output_root = output;
      request.architecture = architecture;
      request.kernconf = kernconf;
      request.transaction_id = transaction_id;
      if (status == CONFIT_OK)
        status = confit_v5_generation_preview(&request, &generation,
                                              diagnostic);
      if (status == CONFIT_OK)
        status = confit_v5_generation_apply(generation, diagnostic);
      if (status != CONFIT_OK && original != 0) {
        int restored = 0;
        (void)confit_host_write_text_file_if_changed_atomic(
            path, original, &restored, diagnostic);
      }
      if (generation != 0)
        (void)confit_v5_generation_cancel(&generation, diagnostic);
      free(original);
      free(minimal);
      (void)original_size;
      (void)minimal_size;
      (void)changed;
      if (status == CONFIT_OK) break;
    }
  }
  confit_v5_workflow_free(workflow);
  return report_status("tui", status, diagnostic);
}

int confit_cli_v5_ux(int argc, char **argv) {
  const char *repository = 0;
  const char *architecture = 0;
  const char *kernconf = 0;
  ConfitDiagnostic diagnostic;
  confit_diagnostic_init(&diagnostic);
  if (!common_arguments(argc, argv, &repository, &architecture, &kernconf))
    return report_status(argv[1], CONFIT_ERR_INVALID_ARGUMENT, &diagnostic);
  if (strcmp(argv[1], "search") == 0)
    return command_search(argc, argv, repository, architecture, kernconf,
                          &diagnostic);
  if (strcmp(argv[1], "explain") == 0)
    return command_explain(argc, argv, repository, architecture, kernconf, 0,
                           &diagnostic);
  if (strcmp(argv[1], "why-unavailable") == 0)
    return command_explain(argc, argv, repository, architecture, kernconf, 1,
                           &diagnostic);
  if (strcmp(argv[1], "list-new") == 0 ||
      strcmp(argv[1], "oldconfig") == 0)
    return command_list_new(argv[1], argc, repository, architecture, kernconf,
                            &diagnostic);
  if (strcmp(argv[1], "save-minimal") == 0)
    return command_save_minimal(argc, argv, repository, architecture, kernconf,
                                &diagnostic);
  if (strcmp(argv[1], "diff") == 0)
    return command_diff(argc, argv, repository, architecture, kernconf,
                        &diagnostic);
  return command_tui(argc, argv, repository, architecture, kernconf,
                     &diagnostic);
}
