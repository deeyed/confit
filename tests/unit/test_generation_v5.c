#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "confit/diagnostic.h"
#include "confit/digest.h"
#include "confit/generation_v5.h"
#include "confit/status.h"
#include "test_assert.h"
#include "test_fs.h"

static void join(char out[4096], const char *root, const char *relative) {
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(out, 4096U, root, relative));
}

static void make_dir(const char *root, const char *relative) {
  char path[4096];
  join(path, root, relative);
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(path));
}

static void write_file(const char *root, const char *relative,
                       const char *text) {
  char path[4096];
  join(path, root, relative);
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, text));
}

static void fixture(char root[4096], char out[4096]) {
  char canonical[4096];
  CONFIT_TEST_ASSERT(
      confit_test_fs_make_temp_dir(root, 4096U, "confit-generation-v5"));
  CONFIT_TEST_ASSERT(realpath(root, canonical) != 0);
  (void)snprintf(root, 4096U, "%s", canonical);
  CONFIT_TEST_ASSERT(
      confit_test_fs_make_temp_dir(out, 4096U, "confit-output-v5"));
  CONFIT_TEST_ASSERT(realpath(out, canonical) != 0);
  (void)snprintf(out, 4096U, "%s", canonical);
  make_dir(root, "config/menus");
  make_dir(root, "config/choices");
  make_dir(root, "config/constraints");
  make_dir(root, "config/options");
  make_dir(root, "config/options/label");
  make_dir(root, "sys/dev");
  make_dir(root, "world");
  make_dir(root, "sys/arch/arm64");
  make_dir(root, "sys/board/arm64");
  make_dir(root, "config/kernconf/arm64");
  write_file(root, "config/project.toml",
             "schema_version = 5\n[project]\nname = \"Generation\"\n"
             "namespace = \"parus.generation\"\n[discovery]\n"
             "menus = \"config/menus\"\nchoices = \"config/choices\"\n"
             "constraints = \"config/constraints\"\n"
             "common_options = [\"config/options\", \"sys/dev\", \"world\"]\n"
             "architecture_root = \"sys/arch\"\n"
             "board_root = \"sys/board\"\n"
             "kernconf_root = \"config/kernconf\"\n");
  write_file(root, "config/menus/Config.toml",
             "schema_version = 5\n[menu]\nid = \"kernel\"\n"
             "prompt = \"Kernel\"\nhelp = \"Kernel options.\"\norder = 1\n");
  write_file(root, "config/options/Config.toml",
             "schema_version = 5\n[option]\nsymbol = \"FEATURE\"\n"
             "type = \"bool\"\nprompt = \"Feature\"\nhelp = \"Feature help.\"\n"
             "menu = \"kernel\"\nmenu_order = 1\nowner = \"sys.feature\"\n"
             "since = \"0.6\"\nstability = \"experimental\"\n"
             "tags = [\"kernel\"]\ndefault = false\n");
  write_file(root, "config/options/label/Config.toml",
             "schema_version = 5\n[option]\nsymbol = \"SYSTEM_LABEL\"\n"
             "type = \"string\"\nprompt = \"System label\"\n"
             "help = \"String projection safety fixture.\"\n"
             "menu = \"kernel\"\nmenu_order = 2\n"
             "owner = \"sys.identity\"\nsince = \"0.6\"\n"
             "stability = \"experimental\"\ntags = [\"identity\"]\n"
             "default = \"safe\"\n");
  write_file(root, "config/kernconf/arm64/vm-v0.toml",
             "schema_version = 5\n[[assignments]]\n"
             "symbol = \"FEATURE\"\nvalue = \"true\"\n"
             "[[assignments]]\nsymbol = \"SYSTEM_LABEL\"\n"
             "value = \"${:sh}#\"\n");
}

static void identity(ConfitV5ToolIdentity *tool, char digest[65]) {
  ConfitDiagnostic diagnostic;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v5_sha256_file("/bin/echo", digest,
                                           &diagnostic) == CONFIT_OK);
  tool->path = "/bin/echo";
  tool->version = "fixture-v1";
  tool->sha256 = digest;
}

static void expect_generation_and_stale_detection(void) {
  char root[4096], out[4096], digest[65];
  char first_digest[65];
  char generation_path[4096];
  ConfitV5ConfigureRequest request;
  ConfitV5GenerationTransaction *transaction = 0;
  ConfitV5ToolIdentity tool;
  ConfitDiagnostic diagnostic;
  fixture(root, out);
  identity(&tool, digest);
  memset(&request, 0, sizeof(request));
  request.repository_root = root;
  request.output_root = out;
  request.architecture = "arm64";
  request.kernconf = "vm-v0";
  request.transaction_id = "first";
  request.resolver = tool;
  request.verifier = tool;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v5_generation_preview(
                         &request, &transaction, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(strlen(confit_v5_generation_digest(transaction)) == 64U);
  memcpy(first_digest, confit_v5_generation_digest(transaction), 65U);
  for (size_t index = 0U; index < CONFIT_V5_GENERATION_ARTIFACT_COUNT; ++index) {
    ConfitV5GeneratedArtifactView artifact;
    CONFIT_TEST_ASSERT(confit_v5_generation_artifact(transaction, index,
                                                      &artifact));
    CONFIT_TEST_ASSERT(artifact.text != 0 && artifact.size != 0U);
    CONFIT_TEST_ASSERT(strstr(artifact.text, "target.mk") == 0);
    CONFIT_TEST_ASSERT(strstr(artifact.text, "link order") == 0);
    if (strcmp(artifact.name, "config.mk") == 0)
      CONFIT_TEST_ASSERT(strstr(artifact.text, "${:sh}#") == 0);
  }
  (void)snprintf(generation_path, sizeof(generation_path), "%s",
                 confit_v5_generation_directory(transaction));
  CONFIT_TEST_ASSERT(confit_v5_configseal_verify(
                         generation_path, root, &tool, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_v5_generation_apply(transaction, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_v5_generation_cancel(&transaction, &diagnostic) ==
                     CONFIT_OK);

  request.transaction_id = "first";
  CONFIT_TEST_ASSERT(confit_v5_generation_preview(
                         &request, &transaction, &diagnostic) ==
                     CONFIT_ERR_CONFLICT);
  CONFIT_TEST_ASSERT(transaction == 0);

  request.transaction_id = "second";
  CONFIT_TEST_ASSERT(confit_v5_generation_preview(
                         &request, &transaction, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(strcmp(first_digest,
                            confit_v5_generation_digest(transaction)) == 0);
  CONFIT_TEST_ASSERT(confit_v5_generation_cancel(&transaction, &diagnostic) ==
                     CONFIT_OK);

  {
    char selected_path[4096];
    join(selected_path, out, "selected");
    CONFIT_TEST_ASSERT(unlink(selected_path) == 0);
    write_file(out, "selected", "foreign owner\n");
    request.transaction_id = "foreign-selected";
    CONFIT_TEST_ASSERT(confit_v5_generation_preview(
                           &request, &transaction, &diagnostic) == CONFIT_OK);
    CONFIT_TEST_ASSERT(confit_v5_generation_apply(transaction, &diagnostic) ==
                       CONFIT_ERR_CONFLICT);
    CONFIT_TEST_ASSERT(confit_v5_generation_cancel(&transaction, &diagnostic) ==
                       CONFIT_OK);
  }

  request.transaction_id = "symlink";
  CONFIT_TEST_ASSERT(confit_v5_generation_preview(
                         &request, &transaction, &diagnostic) == CONFIT_OK);
  {
    char artifact_path[4096];
    (void)snprintf(artifact_path, sizeof(artifact_path), "%s/config.h",
                   confit_v5_generation_directory(transaction));
    CONFIT_TEST_ASSERT(unlink(artifact_path) == 0);
    CONFIT_TEST_ASSERT(symlink("/bin/echo", artifact_path) == 0);
    CONFIT_TEST_ASSERT(confit_v5_configseal_verify(
                           confit_v5_generation_directory(transaction), root,
                           &tool, &diagnostic) != CONFIT_OK);
  }
  CONFIT_TEST_ASSERT(confit_v5_generation_cancel(&transaction, &diagnostic) ==
                     CONFIT_OK);

  make_dir(root, "config/options/added");
  write_file(root, "config/options/added/Config.toml", "schema_version = 5\n");
  CONFIT_TEST_ASSERT(confit_v5_configseal_verify(
                         generation_path, root, &tool, &diagnostic) !=
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(out));
}

int main(void) {
#if defined(_WIN32)
  return 0;
#else
  expect_generation_and_stale_detection();
  return 0;
#endif
}
