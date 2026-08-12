#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "v4_workflow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

#include "confit/diagnostic.h"
#include "confit/digest.h"
#include "confit/generation_v4.h"
#include "confit/host.h"
#include "confit/status.h"
#include "confit/version.h"

enum { CONFIT_CLI_PATH_BYTES = 4096U, CONFIT_CLI_VERSION_BYTES = 256U };

typedef struct ConfigureArguments {
  const char *repository;
  const char *output;
  const char *profile;
  const char *target;
  const char *transaction;
  const char *toolchain;
  const char *verifier;
  const char *producer;
  const char *receipt_root;
  const char *receipt_relative;
} ConfigureArguments;

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

static int parse_arguments(int argc, char **argv, ConfigureArguments *out) {
  const char *const names[] = {
      "--repository", "--out", "--profile", "--target", "--transaction",
      "--toolchain", "--verifier", "--binding-producer", "--receipt-root",
      "--receipt-relative"};
  const char **slots[] = {
      &out->repository, &out->output, &out->profile, &out->target,
      &out->transaction, &out->toolchain, &out->verifier, &out->producer,
      &out->receipt_root, &out->receipt_relative};
  if (argc != 22) return 0;
  (void)memset(out, 0, sizeof(*out));
  for (size_t index = 0U; index < sizeof(names) / sizeof(names[0]); ++index)
    if (!option_value(argc, argv, names[index], slots[index])) return 0;
  return 1;
}

static ConfitStatus tool_identity(const char *path, const char *version_argument,
                                  ConfitV4ToolIdentity *out, char version[256],
                                  char digest[65], ConfitDiagnostic *diagnostic) {
  char canonical[CONFIT_CLI_PATH_BYTES];
  ConfitStatus status = confit_host_path_canonicalize(
      canonical, sizeof(canonical), path, diagnostic);
  if (status == CONFIT_OK)
    status = confit_host_capture_first_line_argument(
        version, CONFIT_CLI_VERSION_BYTES, canonical, version_argument,
        diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_sha256_file(canonical, digest, diagnostic);
  if (status != CONFIT_OK) return status;
  /* canonical storage는 caller가 소유한 path와 exact 일치해야 한다. */
  if (strcmp(canonical, path) != 0) return CONFIT_ERR_COMPATIBILITY;
  out->path = path;
  out->version = version;
  out->sha256 = digest;
  return CONFIT_OK;
}

#if !defined(_WIN32)
static ConfitStatus publish_binding_receipt(
    const ConfigureArguments *arguments,
    const ConfitV4GenerationTransaction *transaction,
    ConfitDiagnostic *diagnostic) {
  const size_t count = confit_v4_generation_binding_count(transaction);
  char count_text[32];
  char **words;
  pid_t child;
  int wait_result;
  int status;
  const size_t word_count = 6U + count * 4U;
  if (count > 256U || snprintf(count_text, sizeof(count_text), "%zu", count) <= 0)
    return CONFIT_ERR_GENERATION;
  words = (char **)calloc(word_count + 1U, sizeof(words[0]));
  if (words == 0) return CONFIT_ERR_INTERNAL;
  words[0] = (char *)arguments->producer;
  words[1] = (char *)"bake-product-receipt";
  words[2] = (char *)arguments->receipt_root;
  words[3] = (char *)arguments->receipt_relative;
  words[4] = (char *)confit_v4_generation_digest(transaction);
  words[5] = count_text;
  for (size_t index = 0U; index < count; ++index) {
    ConfitV4ProductBinding binding;
    if (!confit_v4_generation_binding(transaction, index, &binding)) {
      free(words);
      return CONFIT_ERR_INTERNAL;
    }
    words[6U + index * 4U] = (char *)binding.symbol;
    words[7U + index * 4U] = (char *)binding.value;
    words[8U + index * 4U] = (char *)binding.product_role;
    words[9U + index * 4U] = (char *)binding.canonical_product;
  }
  status = posix_spawn(&child, arguments->producer, 0, 0, words, environ);
  free(words);
  if (status != 0 || waitpid(child, &wait_result, 0) != child ||
      !WIFEXITED(wait_result) || WEXITSTATUS(wait_result) != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION,
                          arguments->producer, 0U, 0U,
                          "Bake product binding producer failed");
    return CONFIT_ERR_GENERATION;
  }
  return CONFIT_OK;
}
#endif

int confit_cli_v4_configure(int argc, char **argv) {
  ConfigureArguments arguments;
  ConfitV4ConfigureRequest request;
  ConfitV4GenerationTransaction *transaction = 0;
  ConfitDiagnostic diagnostic;
  ConfitStatus status;
  char self[CONFIT_CLI_PATH_BYTES];
  char receipt[CONFIT_CLI_PATH_BYTES];
  char self_digest[65], tool_digest[65], verifier_digest[65], producer_digest[65];
  char generation_digest[65] = {0};
  char tool_version[256], verifier_version[256], producer_version[256];
  if (!parse_arguments(argc, argv, &arguments)) {
    (void)fprintf(stderr,
        "confit: configure requires --repository ABS --out ABS --profile ID "
        "--target ID --transaction ID --toolchain ABS --verifier ABS "
        "--binding-producer ABS --receipt-root ABS --receipt-relative REL\n");
    return confit_status_exit_code(CONFIT_ERR_INVALID_ARGUMENT);
  }
  confit_diagnostic_init(&diagnostic);
  status = confit_host_self_executable(self, sizeof(self), &diagnostic);
  if (status == CONFIT_OK) status = confit_v4_sha256_file(self, self_digest, &diagnostic);
  (void)memset(&request, 0, sizeof(request));
  if (status == CONFIT_OK) {
    request.resolver.path = self;
    request.resolver.version = confit_version_string();
    request.resolver.sha256 = self_digest;
    status = tool_identity(arguments.toolchain, "--version", &request.toolchain,
                           tool_version, tool_digest, &diagnostic);
  }
  if (status == CONFIT_OK)
    status = tool_identity(arguments.verifier, "--version", &request.verifier,
                           verifier_version, verifier_digest, &diagnostic);
  if (status == CONFIT_OK)
    status = tool_identity(arguments.producer, "--version",
                           &request.binding_producer, producer_version,
                           producer_digest, &diagnostic);
  request.repository_root = arguments.repository;
  request.output_root = arguments.output;
  request.profile_id = arguments.profile;
  request.target_id = arguments.target;
  request.transaction_id = arguments.transaction;
  if (status == CONFIT_OK)
    status = confit_v4_generation_preview(&request, &transaction, &diagnostic);
  if (status == CONFIT_OK) {
    const char *digest = confit_v4_generation_digest(transaction);
    if (digest == 0 || strlen(digest) != 64U) {
      status = CONFIT_ERR_INTERNAL;
    } else {
      (void)memcpy(generation_digest, digest, sizeof(generation_digest));
    }
  }
#if defined(_WIN32)
  if (status == CONFIT_OK) status = CONFIT_ERR_UNSUPPORTED;
#else
  if (status == CONFIT_OK)
    status = publish_binding_receipt(&arguments, transaction, &diagnostic);
#endif
  if (status == CONFIT_OK &&
      snprintf(receipt, sizeof(receipt), "%s/%s", arguments.receipt_root,
               arguments.receipt_relative) >= (int)sizeof(receipt))
    status = CONFIT_ERR_INVALID_ARGUMENT;
  if (status == CONFIT_OK)
    status = confit_v4_generation_apply_file(transaction, receipt, &diagnostic);
  if (status == CONFIT_OK) {
    (void)printf("generation=%s\n", generation_digest);
  } else {
    (void)fprintf(stderr, "confit: configure failed: %s: %s\n",
                  confit_status_name(status),
                  diagnostic.message != 0 ? diagnostic.message : "no diagnostic");
  }
  if (transaction != 0)
    (void)confit_v4_generation_cancel(&transaction, &diagnostic);
  return confit_status_exit_code(status);
}
