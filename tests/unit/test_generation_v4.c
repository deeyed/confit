#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "confit/generation_v4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "confit/digest.h"
#include "confit/host.h"
#include "test_assert.h"
#include "test_fs.h"

static void join(char *out, size_t out_size, const char *left,
                 const char *right) {
  CONFIT_TEST_ASSERT(
      confit_test_fs_path_join(out, out_size, left, right));
}

static void make_directory(const char *root, const char *relative) {
  char path[4096];
  join(path, sizeof(path), root, relative);
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(path));
}

static void write_file(const char *root, const char *relative,
                       const char *text) {
  char path[4096];
  char parent[4096];
  char *slash;
  join(path, sizeof(path), root, relative);
  CONFIT_TEST_ASSERT(strlen(path) < sizeof(parent));
  memcpy(parent, path, strlen(path) + 1U);
  slash = strrchr(parent, confit_test_fs_separator());
  CONFIT_TEST_ASSERT(slash != 0);
  *slash = '\0';
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(parent));
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, text));
}

static void setup_repository(char *root, size_t root_size, char *output,
                             size_t output_size) {
  CONFIT_TEST_ASSERT(
      confit_test_fs_make_temp_dir(root, root_size, "confit-generation-v4"));
  {
    char canonical[4096];
    ConfitDiagnostic diagnostic;
    confit_diagnostic_init(&diagnostic);
    CONFIT_TEST_ASSERT(confit_host_path_canonicalize(
                           canonical, sizeof(canonical), root, &diagnostic) ==
                       CONFIT_OK);
    CONFIT_TEST_ASSERT(strlen(canonical) + 1U <= root_size);
    memcpy(root, canonical, strlen(canonical) + 1U);
  }
  make_directory(root, "config/options");
  make_directory(root, "config/menus");
  make_directory(root, "config/choices");
  make_directory(root, "config/constraints");
  make_directory(root, "config/profiles");
  make_directory(root, "config/targets");
  make_directory(root, "config/selections");
  make_directory(root, "sys/dev/audio/cmi8738");
  write_file(root, "config/project.toml",
             "schema_version = 4\n"
             "[project]\nname = \"parus-test\"\nnamespace = \"parus.test\"\n"
             "[discovery]\n"
             "options = [\"config/options\"]\n"
             "menus = [\"config/menus\"]\n"
             "choices = [\"config/choices\"]\n"
             "constraints = [\"config/constraints\"]\n"
             "profiles = [\"config/profiles\"]\n"
             "targets = [\"config/targets\"]\n"
             "selections = [\"config/selections\"]\n"
             "products = [\"sys/dev\"]\n"
             "[defaults]\nowner = \"project.default\"\n"
             "since = \"0.4\"\nstability = \"experimental\"\n"
             "tags = [\"test\"]\nmenu_order = 1\n");
  write_file(root, "config/menus/audio/Config.toml",
             "schema_version = 4\n[menu]\n"
             "id = \"drivers.audio\"\nprompt = \"Audio\"\n"
             "help = \"Audio drivers.\"\norder = 10\n");
  write_file(root, "config/options/audio/Config.toml",
             "schema_version = 4\n[option]\n"
             "symbol = \"AUDIO\"\ntype = \"bool\"\n"
             "prompt = \"Audio\"\nhelp = \"Audio subsystem.\"\n"
             "menu = \"drivers.audio\"\n");
  write_file(root, "config/options/pci/Config.toml",
             "schema_version = 4\n[option]\n"
             "symbol = \"BUS_PCI\"\ntype = \"bool\"\n"
             "prompt = \"PCI\"\nhelp = \"PCI bus.\"\n"
             "menu = \"drivers.audio\"\n");
  write_file(root, "config/options/dma/Config.toml",
             "schema_version = 4\n[option]\n"
             "symbol = \"DMA\"\ntype = \"enum\"\n"
             "prompt = \"DMA\"\nhelp = \"DMA mode.\"\n"
             "menu = \"drivers.audio\"\n"
             "values = [\"absent\", \"mapped\"]\n"
             "enabled_values = [\"mapped\"]\ndefault = \"absent\"\n");
  write_file(root, "sys/dev/audio/cmi8738/Config.toml",
             "schema_version = 4\n[option]\n"
             "symbol = \"DRIVER_AUDIO_CMI8738\"\n"
             "type = \"placement\"\nprompt = \"CMI8738\"\n"
             "help = \"CMI8738 controller.\"\nmenu = \"drivers.audio\"\n"
             "menu_order = 120\nowner = \"drivers.audio\"\n"
             "since = \"0.4\"\nstability = \"experimental\"\n"
             "tags = [\"audio\", \"pci\"]\n"
             "allowed = [\"off\", \"kernel\", \"service\"]\n"
             "[constraints]\nall = [\"AUDIO\", \"BUS_PCI\", \"DMA\"]\n");
  write_file(root, "Makefile",
             "this text is intentionally not valid bmake or TOML\n");
  join(output, output_size, root, "output");
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(output));
}

static ConfitV4ToolIdentity tool_identity(const char *path) {
  ConfitV4ToolIdentity identity;
  static char digest[65];
  ConfitDiagnostic diagnostic;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(
      confit_v4_sha256_file(path, digest, &diagnostic) == CONFIT_OK);
  identity.path = path;
  identity.version = "candidate-test-v1";
  identity.sha256 = digest;
  return identity;
}

static void expect_preview_cancel_and_seal(void) {
  char root[4096];
  char output[4096];
  char selected[4096];
  char source[4096];
  ConfitV4GenerationTransaction *transaction = 0;
  ConfitV4GenerationTransaction *duplicate = 0;
  ConfitDiagnostic diagnostic;
  ConfitV4ToolIdentity tool = tool_identity("/usr/bin/cc");
  const ConfitV4LayeredAssignment assignments[] = {
      {{"AUDIO", "true", {"config/profiles/base/Config.toml", 4U, 1U}}, 0},
      {{"AUDIO", "true", {"config/profiles/dev/Config.toml", 5U, 1U}},
       "config/profiles/base/Config.toml"},
      {{"BUS_PCI", "true", {"config/profiles/dev/Config.toml", 6U, 1U}}, 0},
      {{"DMA", "mapped", {"config/targets/qemu/Config.toml", 7U, 1U}}, 0},
      {{"DRIVER_AUDIO_CMI8738", "kernel",
        {"config/selections/dev/Config.toml", 8U, 1U}},
       0},
  };
  ConfitV4ConfigureRequest request;
  setup_repository(root, sizeof(root), output, sizeof(output));
  memset(&request, 0, sizeof(request));
  request.repository_root = root;
  request.output_root = output;
  request.profile_id = "dev";
  request.target_id = "arm64-qemu-virt";
  request.transaction_id = "preview-0001";
  request.resolver = tool;
  request.toolchain = tool;
  request.verifier = tool;
  request.assignments = assignments;
  request.assignment_count = sizeof(assignments) / sizeof(assignments[0]);
  confit_diagnostic_init(&diagnostic);
  {
    const ConfitStatus status = confit_v4_generation_preview(
        &request, &transaction, &diagnostic);
    if (status != CONFIT_OK)
      (void)fprintf(stderr, "preview failed: %d %s %s\n", (int)status,
                    diagnostic.path != 0 ? diagnostic.path : "(no-path)",
                    diagnostic.message != 0 ? diagnostic.message : "(no-message)");
    CONFIT_TEST_ASSERT(status == CONFIT_OK);
  }
  CONFIT_TEST_ASSERT(transaction != 0);
  CONFIT_TEST_ASSERT(strlen(confit_v4_generation_digest(transaction)) == 64U);
  for (size_t index = 0U; index < CONFIT_V4_GENERATION_ARTIFACT_COUNT;
       ++index) {
    ConfitV4GeneratedArtifactView artifact;
    CONFIT_TEST_ASSERT(
        confit_v4_generation_artifact(transaction, index, &artifact));
    CONFIT_TEST_ASSERT(artifact.text != 0 && artifact.size > 0U);
    CONFIT_TEST_ASSERT(strstr(artifact.text, "components.mk") == 0);
    CONFIT_TEST_ASSERT(strstr(artifact.text, "tests.mk") == 0);
    CONFIT_TEST_ASSERT(strstr(artifact.text, "generators.mk") == 0);
    CONFIT_TEST_ASSERT(strstr(artifact.text, "build.policy") == 0);
    if (strcmp(artifact.name, "config.provenance.json") == 0) {
      CONFIT_TEST_ASSERT(strstr(artifact.text, "\"prompt\"") != 0);
      CONFIT_TEST_ASSERT(strstr(artifact.text, "\"help\"") != 0);
      CONFIT_TEST_ASSERT(strstr(artifact.text, "\"menu\"") != 0);
      CONFIT_TEST_ASSERT(strstr(artifact.text, "\"owner\"") != 0);
      CONFIT_TEST_ASSERT(strstr(artifact.text, "\"stability\"") != 0);
      CONFIT_TEST_ASSERT(strstr(artifact.text, "\"since\"") != 0);
      CONFIT_TEST_ASSERT(strstr(artifact.text, "\"tags\"") != 0);
      CONFIT_TEST_ASSERT(strstr(artifact.text, "\"reasons\"") != 0);
    }
  }
  join(selected, sizeof(selected), output, "selected");
  CONFIT_TEST_ASSERT(!confit_test_fs_file_exists(selected));
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v4_generation_preview(
                         &request, &duplicate, &diagnostic) != CONFIT_OK);
  CONFIT_TEST_ASSERT(duplicate == 0);
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v4_configseal_verify(
                         confit_v4_generation_directory(transaction), root,
                         &tool, &tool, &diagnostic) == CONFIT_OK);
  write_file(root, "sys/dev/audio/cmi8738/controller.c",
             "int source_only_change;\n");
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v4_configseal_verify(
                         confit_v4_generation_directory(transaction), root,
                         &tool, &tool, &diagnostic) == CONFIT_OK);
  write_file(root, "config/profiles/new/Config.toml",
             "schema_version = 4\n[profile]\nid = \"new\"\n");
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v4_configseal_verify(
                         confit_v4_generation_directory(transaction), root,
                         &tool, &tool, &diagnostic) != CONFIT_OK);
#if !defined(_WIN32)
  {
    char added[4096];
    char original[4096];
    char moved[4096];
    char owner_directory[4096];
    char case_directory[4096];
    join(added, sizeof(added), root, "config/profiles/new/Config.toml");
    CONFIT_TEST_ASSERT(unlink(added) == 0);
    confit_diagnostic_clear(&diagnostic);
    CONFIT_TEST_ASSERT(confit_v4_configseal_verify(
                           confit_v4_generation_directory(transaction), root,
                           &tool, &tool, &diagnostic) == CONFIT_OK);
    join(owner_directory, sizeof(owner_directory), root,
         "config/options/audio");
    join(case_directory, sizeof(case_directory), root,
         "config/options/AudioCase");
    CONFIT_TEST_ASSERT(rename(owner_directory, case_directory) == 0);
    confit_diagnostic_clear(&diagnostic);
    CONFIT_TEST_ASSERT(confit_v4_configseal_verify(
                           confit_v4_generation_directory(transaction), root,
                           &tool, &tool, &diagnostic) != CONFIT_OK);
    CONFIT_TEST_ASSERT(rename(case_directory, owner_directory) == 0);
    join(original, sizeof(original), root,
         "config/options/audio/Config.toml");
    join(moved, sizeof(moved), root,
         "config/options/audio/saved.toml");
    CONFIT_TEST_ASSERT(rename(original, moved) == 0);
    CONFIT_TEST_ASSERT(symlink(moved, original) == 0);
    confit_diagnostic_clear(&diagnostic);
    CONFIT_TEST_ASSERT(confit_v4_configseal_verify(
                           confit_v4_generation_directory(transaction), root,
                           &tool, &tool, &diagnostic) != CONFIT_OK);
    CONFIT_TEST_ASSERT(unlink(original) == 0);
    CONFIT_TEST_ASSERT(rename(moved, original) == 0);
    CONFIT_TEST_ASSERT(rename(original, moved) == 0);
    CONFIT_TEST_ASSERT(mkdir(original, 0700) == 0);
    confit_diagnostic_clear(&diagnostic);
    CONFIT_TEST_ASSERT(confit_v4_configseal_verify(
                           confit_v4_generation_directory(transaction), root,
                           &tool, &tool, &diagnostic) != CONFIT_OK);
    CONFIT_TEST_ASSERT(rmdir(original) == 0);
    CONFIT_TEST_ASSERT(rename(moved, original) == 0);
  }
#endif
  join(source, sizeof(source), root, "Makefile");
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(source));
  CONFIT_TEST_ASSERT(
      confit_v4_generation_cancel(&transaction, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(transaction == 0);
  CONFIT_TEST_ASSERT(!confit_test_fs_file_exists(selected));
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_stale_tool_and_unrelated_override_fail(void) {
  char root[4096];
  char output[4096];
  ConfitV4GenerationTransaction *transaction = 0;
  ConfitDiagnostic diagnostic;
  ConfitV4ToolIdentity tool = tool_identity("/usr/bin/cc");
  ConfitV4LayeredAssignment assignments[] = {
      {{"AUDIO", "true", {"config/profiles/a/Config.toml", 1U, 1U}}, 0},
      {{"AUDIO", "false", {"config/profiles/b/Config.toml", 1U, 1U}}, 0},
  };
  ConfitV4ConfigureRequest request;
  setup_repository(root, sizeof(root), output, sizeof(output));
  memset(&request, 0, sizeof(request));
  request.repository_root = root;
  request.output_root = output;
  request.profile_id = "bad";
  request.target_id = "host-fixture";
  request.transaction_id = "preview-0003";
  request.resolver = tool;
  request.toolchain = tool;
  request.verifier = tool;
  request.assignments = assignments;
  request.assignment_count = 2U;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v4_generation_preview(
                         &request, &transaction, &diagnostic) ==
                     CONFIT_ERR_CONFLICT);
  CONFIT_TEST_ASSERT(transaction == 0);
  assignments[1].overrides_source_path = "config/profiles/a/Config.toml";
  request.toolchain.sha256 =
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v4_generation_preview(
                         &request, &transaction, &diagnostic) ==
                     CONFIT_ERR_COMPATIBILITY);
  CONFIT_TEST_ASSERT(transaction == 0);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

#if !defined(_WIN32)
static void expect_symlink_output_rejected(void) {
  char root[4096];
  char output[4096];
  char real_output[4096];
  ConfitV4GenerationTransaction *transaction = 0;
  ConfitDiagnostic diagnostic;
  ConfitV4ToolIdentity tool = tool_identity("/usr/bin/cc");
  ConfitV4ConfigureRequest request;
  setup_repository(root, sizeof(root), output, sizeof(output));
  CONFIT_TEST_ASSERT(rmdir(output) == 0);
  join(real_output, sizeof(real_output), root, "real-output");
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(real_output));
  CONFIT_TEST_ASSERT(symlink(real_output, output) == 0);
  memset(&request, 0, sizeof(request));
  request.repository_root = root;
  request.output_root = output;
  request.profile_id = "minimal";
  request.target_id = "host-fixture";
  request.transaction_id = "preview-0004";
  request.resolver = tool;
  request.toolchain = tool;
  request.verifier = tool;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v4_generation_preview(
                         &request, &transaction, &diagnostic) != CONFIT_OK);
  CONFIT_TEST_ASSERT(transaction == 0);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}
#endif

static void expect_product_receipt_gate(void) {
  char root[4096];
  char output[4096];
  char canonical[512];
  char receipt_digest[65];
  ConfitV4GenerationTransaction *transaction = 0;
  ConfitDiagnostic diagnostic;
  ConfitV4ToolIdentity tool = tool_identity("/usr/bin/cc");
  const ConfitV4LayeredAssignment assignments[] = {
      {{"AUDIO", "false", {"confit://request", 1U, 1U}}, 0}};
  ConfitV4ConfigureRequest request;
  ConfitV4ProductBindingReceipt receipt;
  setup_repository(root, sizeof(root), output, sizeof(output));
  memset(&request, 0, sizeof(request));
  request.repository_root = root;
  request.output_root = output;
  request.profile_id = "minimal";
  request.target_id = "host-fixture";
  request.transaction_id = "preview-0002";
  request.resolver = tool;
  request.toolchain = tool;
  request.verifier = tool;
  request.assignments = assignments;
  request.assignment_count = 1U;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v4_generation_preview(
                         &request, &transaction, &diagnostic) == CONFIT_OK);
  memset(&receipt, 0, sizeof(receipt));
  receipt.schema = "bake-product-binding-v1";
  receipt.generation_sha256 = confit_v4_generation_digest(transaction);
  CONFIT_TEST_ASSERT(snprintf(canonical, sizeof(canonical),
                             "schema=bake-product-binding-v1\ngeneration=%s\n",
                             receipt.generation_sha256) > 0);
  confit_v4_sha256_hex(canonical, receipt_digest);
  receipt.receipt_sha256 = receipt_digest;
  CONFIT_TEST_ASSERT(confit_v4_product_binding_receipt_verify(
                         transaction, &receipt, &diagnostic) == CONFIT_OK);
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v4_generation_apply(
                         transaction, &receipt, &diagnostic) ==
                     CONFIT_ERR_UNSUPPORTED);
  receipt.generation_sha256 =
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v4_generation_apply(
                         transaction, &receipt, &diagnostic) != CONFIT_OK);
#if !defined(_WIN32)
  {
    char artifact[4096];
    char extra[4096];
    CONFIT_TEST_ASSERT(chmod(confit_v4_generation_directory(transaction),
                             0700) == 0);
    join(extra, sizeof(extra), confit_v4_generation_directory(transaction),
         "unexpected.bin");
    CONFIT_TEST_ASSERT(confit_test_fs_write_file(extra, "extra\n"));
    confit_diagnostic_clear(&diagnostic);
    CONFIT_TEST_ASSERT(confit_v4_configseal_verify(
                           confit_v4_generation_directory(transaction), root,
                           &tool, &tool, &diagnostic) != CONFIT_OK);
    CONFIT_TEST_ASSERT(unlink(extra) == 0);
    join(artifact, sizeof(artifact),
         confit_v4_generation_directory(transaction), "config.mk");
    CONFIT_TEST_ASSERT(unlink(artifact) == 0);
    confit_diagnostic_clear(&diagnostic);
    CONFIT_TEST_ASSERT(confit_v4_configseal_verify(
                           confit_v4_generation_directory(transaction), root,
                           &tool, &tool, &diagnostic) != CONFIT_OK);
  }
#endif
  CONFIT_TEST_ASSERT(
      confit_v4_generation_cancel(&transaction, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void emit_retained_cross_repository_fixture(void) {
  char root[4096];
  char output[4096];
  char resolver_path[4096];
  char resolver_digest[65];
  ConfitV4GenerationTransaction *transaction = 0;
  ConfitDiagnostic diagnostic;
  ConfitV4ToolIdentity tool = tool_identity("/usr/bin/cc");
  ConfitV4ToolIdentity resolver;
  ConfitV4ConfigureRequest request;
  setup_repository(root, sizeof(root), output, sizeof(output));
  write_file(root, "resolver.bin", "resolver candidate removed after configure\n");
  join(resolver_path, sizeof(resolver_path), root, "resolver.bin");
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v4_sha256_file(resolver_path, resolver_digest,
                                           &diagnostic) == CONFIT_OK);
  resolver.path = resolver_path;
  resolver.version = "removed-resolver-v1";
  resolver.sha256 = resolver_digest;
  memset(&request, 0, sizeof(request));
  request.repository_root = root;
  request.output_root = output;
  request.profile_id = "minimal";
  request.target_id = "host-fixture";
  request.transaction_id = "cross-repository";
  request.resolver = resolver;
  request.toolchain = tool;
  request.verifier = tool;
  CONFIT_TEST_ASSERT(confit_v4_generation_preview(
                         &request, &transaction, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(unlink(resolver_path) == 0);
  (void)printf("repository=%s\ngeneration=%s\ntool_path=%s\n"
               "tool_version=%s\ntool_sha256=%s\n",
               root, confit_v4_generation_directory(transaction), tool.path,
               tool.version, tool.sha256);
  fflush(stdout);
  /* Cross-repository consumer가 process 종료 뒤 artifact lifetime을 검증한다. */
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--emit-cross-fixture") == 0) {
    emit_retained_cross_repository_fixture();
    return 0;
  }
  CONFIT_TEST_ASSERT(argc == 1);
  (void)argv;
  expect_preview_cancel_and_seal();
  expect_product_receipt_gate();
  expect_stale_tool_and_unrelated_override_fail();
#if !defined(_WIN32)
  expect_symlink_output_rejected();
#endif
  return 0;
}
