#include "v5_workflow.h"

#include <stdio.h>
#include <string.h>

#include "confit/diagnostic.h"
#include "confit/digest.h"
#include "confit/generation_v5.h"
#include "confit/host.h"
#include "confit/status.h"
#include "confit/version.h"

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
  ConfitV5ToolIdentity verifier;
  ConfitDiagnostic diagnostic;
  ConfitStatus status;
  char self[CONFIT_CLI_PATH_BYTES];
  char digest[65];
  if (argc != 6 ||
      !option_value(argc, argv, "--repository", &repository) ||
      !option_value(argc, argv, "--generation", &generation)) {
    (void)fprintf(stderr,
                  "confit: verify requires --repository ABS "
                  "--generation ABS\n");
    return confit_status_exit_code(CONFIT_ERR_INVALID_ARGUMENT);
  }
  confit_diagnostic_init(&diagnostic);
  status = self_identity(&verifier, self, digest, &diagnostic);
  if (status == CONFIT_OK)
    status = confit_v5_configseal_verify(generation, repository, &verifier,
                                         &diagnostic);
  if (status == CONFIT_OK) (void)printf("configseal=verified\n");
  return report_status("verify", status, &diagnostic);
}
