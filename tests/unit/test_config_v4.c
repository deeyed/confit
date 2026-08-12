#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "test_assert.h"
#include "test_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "confit/config_v4.h"
#include "confit/diagnostic.h"

static void canonicalize_root(char *root, size_t root_size) {
#if !defined(_WIN32)
  char canonical[PATH_MAX];
  CONFIT_TEST_ASSERT(realpath(root, canonical) != 0);
  CONFIT_TEST_ASSERT(strlen(canonical) + 1U <= root_size);
  memcpy(root, canonical, strlen(canonical) + 1U);
#else
  (void)root;
  (void)root_size;
#endif
}

static void join_path(char *out, size_t out_size, const char *left,
                      const char *right) {
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(out, out_size, left, right));
}

static void make_relative_directory(const char *root, const char *relative) {
  char path[4096];
  join_path(path, sizeof(path), root, relative);
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(path));
}

static void write_relative(const char *root, const char *relative,
                           const char *text) {
  char path[4096];
  char parent[4096];
  char *separator;
  join_path(path, sizeof(path), root, relative);
  CONFIT_TEST_ASSERT(strlen(path) + 1U <= sizeof(parent));
  memcpy(parent, path, strlen(path) + 1U);
  separator = strrchr(parent, confit_test_fs_separator());
  CONFIT_TEST_ASSERT(separator != 0);
  *separator = '\0';
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(parent));
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, text));
}

static void write_project(const char *root, const char *options_root,
                          const char *products_root,
                          const char *default_fields) {
  char text[8192];
  CONFIT_TEST_ASSERT(snprintf(
      text, sizeof(text),
      "schema_version = 4\n"
      "[project]\n"
      "name = \"parus-v4-test\"\n"
      "namespace = \"parus.test\"\n"
      "[discovery]\n"
      "options = [%s]\n"
      "menus = [\"config/menus\"]\n"
      "choices = [\"config/choices\"]\n"
      "constraints = [\"config/constraints\"]\n"
      "profiles = [\"config/profiles\"]\n"
      "targets = [\"config/targets\"]\n"
      "selections = [\"config/selections\"]\n"
      "products = [%s]\n"
      "%s",
      options_root, products_root, default_fields) > 0);
  write_relative(root, "config/project.toml", text);
  make_relative_directory(root, "config/options");
  make_relative_directory(root, "config/menus");
  make_relative_directory(root, "config/choices");
  make_relative_directory(root, "config/constraints");
  make_relative_directory(root, "config/profiles");
  make_relative_directory(root, "config/targets");
  make_relative_directory(root, "config/selections");
  make_relative_directory(root, "sys/dev");
}

static void write_menu(const char *root) {
  write_relative(root, "config/menus/audio/Config.toml",
                 "schema_version = 4\n"
                 "[menu]\n"
                 "id = \"drivers.audio\"\n"
                 "prompt = \"Audio drivers\"\n"
                 "help = \"Audio driver selection.\"\n"
                 "order = 100\n");
}

static void write_bool_option(const char *root, const char *relative,
                              const char *symbol, const char *constraints,
                              const char *ui, const char *extra) {
  char text[8192];
  CONFIT_TEST_ASSERT(snprintf(
      text, sizeof(text),
      "schema_version = 4\n"
      "[option]\n"
      "symbol = \"%s\"\n"
      "type = \"bool\"\n"
      "prompt = \"%s\"\n"
      "help = \"Boolean fixture for %s.\"\n"
      "menu = \"drivers.audio\"\n"
      "%s%s%s",
      symbol, symbol, symbol, constraints != 0 ? constraints : "",
      ui != 0 ? ui : "", extra != 0 ? extra : "") > 0);
  write_relative(root, relative, text);
}

static void write_driver_option(const char *root, const char *relative,
                                const char *symbol, const char *interfaces,
                                const char *extra) {
  char text[8192];
  CONFIT_TEST_ASSERT(snprintf(
      text, sizeof(text),
      "schema_version = 4\n"
      "[option]\n"
      "symbol = \"%s\"\n"
      "type = \"placement\"\n"
      "prompt = \"%s\"\n"
      "help = \"Selectable driver fixture for %s.\"\n"
      "menu = \"drivers.audio\"\n"
      "allowed = [\"off\", \"kernel\", \"service\"]\n"
      "[constraints]\n"
      "all = [\"AUDIO\", \"BUS_PCI\", \"DMA\"]\n"
      "[ui]\n"
      "visible_all = [\"SHOW_AUDIO\"]\n"
      "%s%s",
      symbol, symbol, symbol, interfaces != 0 ? interfaces : "",
      extra != 0 ? extra : "") > 0);
  write_relative(root, relative, text);
}

static void setup_base(char *root, size_t root_size) {
  CONFIT_TEST_ASSERT(
      confit_test_fs_make_temp_dir(root, root_size, "confit-config-v4"));
  canonicalize_root(root, root_size);
  write_project(root, "\"config/options\"", "\"sys/dev\"",
                "[defaults]\n"
                "owner = \"project.default\"\n"
                "since = \"0.4\"\n"
                "stability = \"experimental\"\n"
                "tags = [\"project\"]\n"
                "menu_order = 100\n");
  write_menu(root);
  write_relative(root, "sys/dev/audio/OWNERS.toml",
                 "schema_version = 4\n"
                 "[defaults]\n"
                 "owner = \"drivers.audio\"\n"
                 "tags = [\"audio\", \"pci\"]\n"
                 "menu_order = 120\n");
  write_bool_option(root, "config/options/audio/Config.toml", "AUDIO", 0, 0,
                    0);
  write_bool_option(root, "config/options/pci/Config.toml", "BUS_PCI", 0, 0,
                    0);
  write_bool_option(root, "config/options/show/Config.toml", "SHOW_AUDIO", 0,
                    0, 0);
  write_relative(root, "config/options/dma/Config.toml",
                 "schema_version = 4\n"
                 "[option]\n"
                 "symbol = \"DMA\"\n"
                 "type = \"enum\"\n"
                 "prompt = \"DMA model\"\n"
                 "help = \"Select a semantic DMA configuration.\"\n"
                 "menu = \"drivers.audio\"\n"
                 "values = [\"none\", \"mapped\"]\n"
                 "enabled_values = [\"mapped\"]\n"
                 "default = \"none\"\n");
  write_relative(root, "config/options/buffer/Config.toml",
                 "schema_version = 4\n"
                 "[option]\n"
                 "symbol = \"AUDIO_BUFFER\"\n"
                 "type = \"integer\"\n"
                 "prompt = \"Audio buffer\"\n"
                 "help = \"Audio buffer size.\"\n"
                 "menu = \"drivers.audio\"\n"
                 "minimum = 128\n"
                 "maximum = 4096\n"
                 "default = 1024\n");
  write_relative(root, "config/options/policy/Config.toml",
                 "schema_version = 4\n"
                 "[option]\n"
                 "symbol = \"AUDIO_POLICY\"\n"
                 "type = \"string\"\n"
                 "prompt = \"Audio policy\"\n"
                 "help = \"Audio policy label.\"\n"
                 "menu = \"drivers.audio\"\n"
                 "default = \"balanced\"\n");
  write_driver_option(root, "sys/dev/audio/cmi8738/Config.toml",
                      "DRIVER_AUDIO_CMI8738", 0, 0);
  write_driver_option(root, "sys/dev/audio/hda/Config.toml",
                      "DRIVER_AUDIO_HDA", 0, 0);
  write_driver_option(root, "sys/dev/audio/usb/Config.toml",
                      "DRIVER_AUDIO_USB", 0, 0);
  write_relative(root, "config/choices/board/Config.toml",
                 "schema_version = 4\n"
                 "[choice]\n"
                 "symbol = \"BOARD_BACKEND\"\n"
                 "prompt = \"Board backend\"\n"
                 "help = \"At most one synthetic board backend.\"\n"
                 "members = [\"BOARD_A\", \"BOARD_B\"]\n"
                 "cardinality = \"at_most_one\"\n");
  write_bool_option(root, "config/options/board-a/Config.toml", "BOARD_A", 0,
                    0, 0);
  write_bool_option(root, "config/options/board-b/Config.toml", "BOARD_B", 0,
                    0, 0);
  write_relative(root, "config/constraints/audio/Config.toml",
                 "schema_version = 4\n"
                 "[[rule]]\n"
                 "if_all = [\"DRIVER_AUDIO_CMI8738\"]\n"
                 "require_all = [\"AUDIO\"]\n"
                 "message = \"CMI8738 requires audio\"\n");
}

static ConfitV4Catalog *load_ok(const char *root) {
  ConfitV4Catalog *catalog = 0;
  ConfitDiagnostic diagnostic;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(
      confit_v4_catalog_load(root, &catalog, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(catalog != 0);
  return catalog;
}

static void expect_multiple_drivers_and_reason_model(void) {
  char root[4096];
  ConfitV4Catalog *catalog;
  ConfitV4Evaluation *evaluation = 0;
  ConfitV4OptionView option;
  ConfitV4ReasonView reason;
  ConfitDiagnostic diagnostic;
  const ConfitV4Assignment assignments[] = {
      {"AUDIO", "true", {"profile.toml", 10U, 3U}},
      {"BUS_PCI", "true", {"profile.toml", 11U, 3U}},
      {"DMA", "mapped", {"profile.toml", 12U, 3U}},
      {"SHOW_AUDIO", "true", {"profile.toml", 13U, 3U}},
      {"DRIVER_AUDIO_CMI8738", "kernel", {"profile.toml", 14U, 3U}},
      {"DRIVER_AUDIO_HDA", "kernel", {"profile.toml", 15U, 3U}},
      {"DRIVER_AUDIO_USB", "service", {"profile.toml", 16U, 3U}},
      {"AUDIO_BUFFER", "2048", {"profile.toml", 17U, 3U}},
      {"AUDIO_POLICY", "latency", {"profile.toml", 18U, 3U}},
  };
  int saw_prerequisite = 0;
  int saw_visibility = 0;
  int saw_choice = 0;
  int saw_rule = 0;
  setup_base(root, sizeof(root));
  catalog = load_ok(root);
  CONFIT_TEST_ASSERT(confit_v4_catalog_option_count(catalog) == 11U);
  CONFIT_TEST_ASSERT(confit_v4_catalog_menu_count(catalog) == 1U);
  CONFIT_TEST_ASSERT(confit_v4_catalog_choice_count(catalog) == 1U);
  CONFIT_TEST_ASSERT(confit_v4_catalog_rule_count(catalog) == 1U);
  CONFIT_TEST_ASSERT(confit_v4_catalog_document_count(catalog) >= 13U);
  CONFIT_TEST_ASSERT(confit_v4_catalog_option(
      catalog, "DRIVER_AUDIO_CMI8738", &option));
  CONFIT_TEST_ASSERT(strcmp(option.projection,
                            "CONFIG_DRIVER_AUDIO_CMI8738") == 0);
  CONFIT_TEST_ASSERT(strcmp(option.owner, "drivers.audio") == 0);
  CONFIT_TEST_ASSERT(option.owner_source.path != 0 &&
                     strstr(option.owner_source.path, "OWNERS.toml") != 0);
  CONFIT_TEST_ASSERT(option.menu_order == 120);
  CONFIT_TEST_ASSERT(option.menu_order_source.line > 0U);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v4_evaluate(
                         catalog, assignments,
                         sizeof(assignments) / sizeof(assignments[0]), 0, 0U,
                         &evaluation, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(
      strcmp(confit_v4_evaluation_value(evaluation,
                                        "DRIVER_AUDIO_CMI8738"),
             "kernel") == 0);
  for (size_t index = 0U;
       index < confit_v4_evaluation_reason_count(evaluation); ++index) {
    CONFIT_TEST_ASSERT(
        confit_v4_evaluation_reason(evaluation, index, &reason));
    CONFIT_TEST_ASSERT(reason.source.path != 0);
    CONFIT_TEST_ASSERT(reason.source.line > 0U);
    CONFIT_TEST_ASSERT(reason.source.column > 0U);
    saw_prerequisite |= reason.kind == CONFIT_V4_REASON_PREREQUISITE;
    saw_visibility |= reason.kind == CONFIT_V4_REASON_VISIBILITY;
    saw_choice |= reason.kind == CONFIT_V4_REASON_CHOICE;
    saw_rule |= reason.kind == CONFIT_V4_REASON_RULE;
  }
  CONFIT_TEST_ASSERT(saw_prerequisite && saw_visibility && saw_choice &&
                     saw_rule);
  confit_v4_evaluation_free(evaluation);
  confit_v4_catalog_free(catalog);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_replacement_failure(const char *relative,
                                       const char *replacement) {
  char root[4096];
  ConfitV4Catalog *catalog = 0;
  ConfitDiagnostic diagnostic;
  ConfitStatus status;
  setup_base(root, sizeof(root));
  write_relative(root, relative, replacement);
  confit_diagnostic_init(&diagnostic);
  status = confit_v4_catalog_load(root, &catalog, &diagnostic);
  if (status == CONFIT_OK) CONFIT_TEST_FAIL(relative);
  CONFIT_TEST_ASSERT(catalog == 0);
  CONFIT_TEST_ASSERT(diagnostic.message != 0);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_v3_and_retired_fields_rejected(void) {
  expect_replacement_failure(
      "config/project.toml",
      "schema_version = 3\n"
      "[project]\n"
      "name = \"retired\"\n"
      "namespace = \"parus.retired\"\n"
      "[discovery]\n"
      "options = []\nmenus = []\nchoices = []\nconstraints = []\n"
      "profiles = []\ntargets = []\nselections = []\nproducts = []\n");
  expect_replacement_failure(
      "sys/dev/audio/cmi8738/Config.toml",
      "schema_version = 3\n"
      "[component]\n"
      "id = \"driver.audio.cmi8738\"\n"
      "kind = \"kernel_provider\"\n"
      "summary = \"retired\"\n"
      "owner = \"drivers.audio\"\n");
  expect_replacement_failure(
      "sys/dev/audio/cmi8738/Config.toml",
      "schema_version = 4\n"
      "[option]\n"
      "symbol = \"DRIVER_AUDIO_CMI8738\"\n"
      "type = \"placement\"\n"
      "prompt = \"CMI\"\n"
      "help = \"CMI driver.\"\n"
      "menu = \"drivers.audio\"\n"
      "allowed = [\"off\", \"kernel\"]\n"
      "[constraints]\n"
      "all = [\"AUDIO\"]\n"
      "needs = [\"dma.mapping\"]\n");
  expect_replacement_failure(
      "sys/dev/audio/cmi8738/Config.toml",
      "schema_version = 4\n"
      "[option]\n"
      "symbol = \"DRIVER_AUDIO_CMI8738\"\n"
      "type = \"placement\"\n"
      "prompt = \"CMI\"\n"
      "help = \"CMI driver.\"\n"
      "menu = \"drivers.audio\"\n"
      "allowed = [\"off\", \"kernel\"]\n"
      "source = [\"driver.c\"]\n"
      "link = [\"pci\"]\n");
  expect_replacement_failure(
      "sys/dev/audio/cmi8738/Config.toml",
      "schema_version = 4\n"
      "[option]\n"
      "symbol = \"DRIVER_AUDIO_CMI8738\"\n"
      "type = \"placement\"\n"
      "prompt = \"CMI\"\n"
      "help = \"CMI driver.\"\n"
      "menu = \"drivers.audio\"\n"
      "allowed = [\"off\", \"kernel\"]\n"
      "[selection]\n"
      "requires = [\"subsystem.audio@1\"]\n"
      "default = false\n");
  expect_replacement_failure(
      "sys/dev/audio/cmi8738/Config.toml",
      "schema_version = 4\n"
      "[option]\n"
      "symbol = \"DRIVER_AUDIO_CMI8738\"\n"
      "type = \"placement\"\n"
      "prompt = \"CMI\"\n"
      "help = \"CMI driver.\"\n"
      "menu = \"drivers.audio\"\n"
      "allowed = [\"off\", \"kernel\"]\n"
      "[interfaces]\n"
      "legacy_requires = [\"dma.mapping\"]\n"
      "legacy_provides = []\n");
  expect_replacement_failure(
      "sys/dev/audio/cmi8738/Config.toml",
      "schema_version = 4\n"
      "[option]\n"
      "symbol = \"DRIVER_AUDIO_CMI8738\"\n"
      "type = \"placement\"\n"
      "prompt = \"CMI\"\n"
      "help = \"CMI driver.\"\n"
      "menu = \"drivers.audio\"\n"
      "allowed = [\"off\", \"kernel\"]\n"
      "[ui]\n"
      "visible_all = [\"AUDIO\"]\n"
      "any = [\"BUS_PCI\", \"DMA\"]\n"
      "hidden_select = \"BUS_PCI\"\n");
  expect_replacement_failure(
      "config/constraints/audio/Config.toml",
      "schema_version = 4\n"
      "[[rule]]\n"
      "if_all = [\"AUDIO\"]\n"
      "require_all = [\"DMA\"]\n"
      "message = \"DMA required\"\n"
      "script = \"probe-dma\"\n");
}

static void expect_dependency_failures(void) {
  expect_replacement_failure(
      "sys/dev/audio/cmi8738/Config.toml",
      "schema_version = 4\n"
      "[option]\n"
      "symbol = \"DRIVER_AUDIO_CMI8738\"\n"
      "type = \"placement\"\n"
      "prompt = \"CMI\"\n"
      "help = \"CMI driver.\"\n"
      "menu = \"drivers.audio\"\n"
      "allowed = [\"off\", \"kernel\"]\n"
      "[constraints]\n"
      "all = [\"UNKNOWN\"]\n");
  expect_replacement_failure(
      "sys/dev/audio/cmi8738/Config.toml",
      "schema_version = 4\n"
      "[option]\n"
      "symbol = \"DRIVER_AUDIO_CMI8738\"\n"
      "type = \"placement\"\n"
      "prompt = \"CMI\"\n"
      "help = \"CMI driver.\"\n"
      "menu = \"drivers.audio\"\n"
      "allowed = [\"off\", \"kernel\"]\n"
      "[constraints]\n"
      "all = [\"AUDIO\", \"AUDIO\"]\n");
  expect_replacement_failure(
      "sys/dev/audio/cmi8738/Config.toml",
      "schema_version = 4\n"
      "[option]\n"
      "symbol = \"DRIVER_AUDIO_CMI8738\"\n"
      "type = \"placement\"\n"
      "prompt = \"CMI\"\n"
      "help = \"CMI driver.\"\n"
      "menu = \"drivers.audio\"\n"
      "allowed = [\"off\", \"kernel\"]\n"
      "[constraints]\n"
      "all = [\"DRIVER_AUDIO_CMI8738\"]\n");
  expect_replacement_failure(
      "sys/dev/audio/cmi8738/Config.toml",
      "schema_version = 4\n"
      "[option]\n"
      "symbol = \"DRIVER_AUDIO_CMI8738\"\n"
      "type = \"placement\"\n"
      "prompt = \"CMI\"\n"
      "help = \"CMI driver.\"\n"
      "menu = \"drivers.audio\"\n"
      "allowed = [\"off\", \"kernel\"]\n"
      "[constraints]\n"
      "all = [\"AUDIO_BUFFER\"]\n");
}

static void expect_no_auto_enable(void) {
  char root[4096];
  ConfitV4Catalog *catalog;
  ConfitV4Evaluation *evaluation = 0;
  ConfitDiagnostic diagnostic;
  const ConfitV4Assignment assignment = {
      "DRIVER_AUDIO_CMI8738", "kernel", {"profile.toml", 4U, 2U}};
  setup_base(root, sizeof(root));
  catalog = load_ok(root);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v4_evaluate(catalog, &assignment, 1U, 0, 0U,
                                        &evaluation, &diagnostic) ==
                     CONFIT_ERR_DEPENDENCY);
  CONFIT_TEST_ASSERT(strcmp(confit_v4_evaluation_value(evaluation, "AUDIO"),
                            "false") == 0);
  CONFIT_TEST_ASSERT(diagnostic.line > 0U && diagnostic.column > 0U);
  confit_v4_evaluation_free(evaluation);
  confit_v4_catalog_free(catalog);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void write_provider(const char *root, const char *relative,
                           const char *symbol, const char *cardinality,
                           const char *absence) {
  char interfaces[1024];
  CONFIT_TEST_ASSERT(snprintf(
      interfaces, sizeof(interfaces),
      "[[interfaces.provides]]\n"
      "namespace = \"dma.mapping\"\n"
      "major = 2\n"
      "cardinality = \"%s\"\n"
      "absence = \"%s\"\n",
      cardinality, absence) > 0);
  write_driver_option(root, relative, symbol, interfaces, 0);
}

static void expect_provider_selection_and_ambiguity(void) {
  char root[4096];
  ConfitV4Catalog *catalog;
  ConfitV4Evaluation *evaluation = 0;
  ConfitDiagnostic diagnostic;
  const ConfitV4Assignment assignments[] = {
      {"AUDIO", "true", {"profile.toml", 1U, 1U}},
      {"BUS_PCI", "true", {"profile.toml", 2U, 1U}},
      {"DMA", "mapped", {"profile.toml", 3U, 1U}},
      {"SHOW_AUDIO", "true", {"profile.toml", 4U, 1U}},
      {"DMA_SMMU", "kernel", {"profile.toml", 5U, 1U}},
      {"DMA_SOFT", "kernel", {"profile.toml", 6U, 1U}},
  };
  const ConfitV4ProviderChoice choice = {
      "dma.mapping", 2U, "DMA_SMMU", {"profile.toml", 20U, 1U}};
  int saw_ambiguity = 0;
  setup_base(root, sizeof(root));
  write_provider(root, "sys/dev/dma/smmu/Config.toml", "DMA_SMMU", "single",
                 "allowed");
  write_provider(root, "sys/dev/dma/soft/Config.toml", "DMA_SOFT", "single",
                 "allowed");
  catalog = load_ok(root);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v4_evaluate(
                         catalog, assignments,
                         sizeof(assignments) / sizeof(assignments[0]), 0, 0U,
                         &evaluation, &diagnostic) == CONFIT_ERR_CONFLICT);
  for (size_t index = 0U;
       index < confit_v4_evaluation_reason_count(evaluation); ++index) {
    ConfitV4ReasonView reason;
    CONFIT_TEST_ASSERT(
        confit_v4_evaluation_reason(evaluation, index, &reason));
    if (reason.kind == CONFIT_V4_REASON_AMBIGUITY) {
      saw_ambiguity = 1;
      CONFIT_TEST_ASSERT(!reason.satisfied);
      CONFIT_TEST_ASSERT(reason.source.line > 0U);
    }
  }
  CONFIT_TEST_ASSERT(saw_ambiguity);
  confit_v4_evaluation_free(evaluation);
  evaluation = 0;
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v4_evaluate(
                         catalog, assignments,
                         sizeof(assignments) / sizeof(assignments[0]), &choice,
                         1U, &evaluation, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(strcmp(confit_v4_evaluation_single_provider(
                                evaluation, "dma.mapping", 2U),
                            "DMA_SMMU") == 0);
  confit_v4_evaluation_free(evaluation);
  confit_v4_catalog_free(catalog);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_provider_absence_and_cardinality(void) {
  char root[4096];
  ConfitV4Catalog *catalog;
  ConfitV4Evaluation *evaluation = 0;
  ConfitDiagnostic diagnostic;
  setup_base(root, sizeof(root));
  write_provider(root, "sys/dev/dma/smmu/Config.toml", "DMA_SMMU", "single",
                 "forbidden");
  catalog = load_ok(root);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v4_evaluate(catalog, 0, 0U, 0, 0U, &evaluation,
                                        &diagnostic) ==
                     CONFIT_ERR_CONFLICT);
  confit_v4_evaluation_free(evaluation);
  confit_v4_catalog_free(catalog);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));

  setup_base(root, sizeof(root));
  write_provider(root, "sys/dev/dma/smmu/Config.toml", "DMA_SMMU", "single",
                 "allowed");
  write_provider(root, "sys/dev/dma/soft/Config.toml", "DMA_SOFT", "multiple",
                 "allowed");
  confit_diagnostic_init(&diagnostic);
  catalog = 0;
  CONFIT_TEST_ASSERT(
      confit_v4_catalog_load(root, &catalog, &diagnostic) ==
      CONFIT_ERR_CONFLICT);
  CONFIT_TEST_ASSERT(catalog == 0);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_multiple_provider_group(void) {
  char root[4096];
  ConfitV4Catalog *catalog;
  ConfitV4Evaluation *evaluation = 0;
  ConfitDiagnostic diagnostic;
  const ConfitV4Assignment assignments[] = {
      {"AUDIO", "true", {"profile.toml", 1U, 1U}},
      {"BUS_PCI", "true", {"profile.toml", 2U, 1U}},
      {"DMA", "mapped", {"profile.toml", 3U, 1U}},
      {"SHOW_AUDIO", "true", {"profile.toml", 4U, 1U}},
      {"DMA_SMMU", "kernel", {"profile.toml", 5U, 1U}},
      {"DMA_SOFT", "kernel", {"profile.toml", 6U, 1U}},
  };
  setup_base(root, sizeof(root));
  write_provider(root, "sys/dev/dma/smmu/Config.toml", "DMA_SMMU",
                 "multiple", "allowed");
  write_provider(root, "sys/dev/dma/soft/Config.toml", "DMA_SOFT",
                 "multiple", "allowed");
  catalog = load_ok(root);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v4_evaluate(
                         catalog, assignments,
                         sizeof(assignments) / sizeof(assignments[0]), 0, 0U,
                         &evaluation, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_v4_evaluation_single_provider(
                         evaluation, "dma.mapping", 2U) == 0);
  confit_v4_evaluation_free(evaluation);
  confit_v4_catalog_free(catalog);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_discovery_hostile_corpus(void) {
  char root[4096];
  ConfitV4Catalog *catalog = 0;
  ConfitDiagnostic diagnostic;
#if !defined(_WIN32)
  char link_path[4096];
#endif

  setup_base(root, sizeof(root));
#if !defined(_WIN32)
  join_path(link_path, sizeof(link_path), root, "sys/dev/audio/escape-link");
  CONFIT_TEST_ASSERT(symlink("cmi8738", link_path) == 0);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(
      confit_v4_catalog_load(root, &catalog, &diagnostic) != CONFIT_OK);
  CONFIT_TEST_ASSERT(catalog == 0);
#endif
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));

  setup_base(root, sizeof(root));
  write_project(root, "\"config/옵션\"", "\"sys/dev\"",
                "[defaults]\nowner = \"project.default\"\n"
                "since = \"0.4\"\nstability = \"experimental\"\n"
                "tags = []\nmenu_order = 1\n");
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(
      confit_v4_catalog_load(root, &catalog, &diagnostic) == CONFIT_ERR_SCHEMA);
  CONFIT_TEST_ASSERT(catalog == 0);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));

  setup_base(root, sizeof(root));
  write_project(root, "\"config/options\"", "\"sys/dev\", \"SYS/DEV\"",
                "[defaults]\nowner = \"project.default\"\n"
                "since = \"0.4\"\nstability = \"experimental\"\n"
                "tags = []\nmenu_order = 1\n");
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(
      confit_v4_catalog_load(root, &catalog, &diagnostic) != CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));

  setup_base(root, sizeof(root));
  write_project(root, "\"../escape\"", "\"sys/dev\"",
                "[defaults]\nowner = \"project.default\"\n"
                "since = \"0.4\"\nstability = \"experimental\"\n"
                "tags = []\nmenu_order = 1\n");
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(
      confit_v4_catalog_load(root, &catalog, &diagnostic) == CONFIT_ERR_SCHEMA);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));

  setup_base(root, sizeof(root));
  write_project(root, "\"sys/dev\"", "\"sys/dev\"",
                "[defaults]\nowner = \"project.default\"\n"
                "since = \"0.4\"\nstability = \"experimental\"\n"
                "tags = []\nmenu_order = 1\n");
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(
      confit_v4_catalog_load(root, &catalog, &diagnostic) != CONFIT_OK);
  CONFIT_TEST_ASSERT(catalog == 0);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));

  setup_base(root, sizeof(root));
  write_bool_option(root, "config/menus/wrong/Config.toml", "WRONG_ROLE", 0,
                    0, 0);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(
      confit_v4_catalog_load(root, &catalog, &diagnostic) == CONFIT_ERR_SCHEMA);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_tui_hierarchy_failures(void) {
  expect_replacement_failure(
      "config/menus/audio/Config.toml",
      "schema_version = 4\n"
      "[menu]\n"
      "id = \"drivers.audio\"\n"
      "prompt = \"Audio drivers\"\n"
      "help = \"Audio driver selection.\"\n"
      "parent = \"drivers.missing\"\n"
      "order = 100\n");

  {
    char root[4096];
    ConfitV4Catalog *catalog = 0;
    ConfitDiagnostic diagnostic;
    setup_base(root, sizeof(root));
    write_relative(root, "config/menus/audio/Config.toml",
                   "schema_version = 4\n"
                   "[menu]\n"
                   "id = \"drivers.audio\"\n"
                   "prompt = \"Audio drivers\"\n"
                   "help = \"Audio driver selection.\"\n"
                   "parent = \"drivers.root\"\n"
                   "order = 100\n");
    write_relative(root, "config/menus/root/Config.toml",
                   "schema_version = 4\n"
                   "[menu]\n"
                   "id = \"drivers.root\"\n"
                   "prompt = \"Drivers\"\n"
                   "help = \"Driver root menu.\"\n"
                   "parent = \"drivers.audio\"\n"
                   "order = 10\n");
    confit_diagnostic_init(&diagnostic);
    CONFIT_TEST_ASSERT(confit_v4_catalog_load(root, &catalog, &diagnostic) ==
                       CONFIT_ERR_DEPENDENCY);
    CONFIT_TEST_ASSERT(catalog == 0);
    CONFIT_TEST_ASSERT(diagnostic.path != 0 && diagnostic.line > 0U);
    CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
  }

  expect_replacement_failure(
      "config/choices/board/Config.toml",
      "schema_version = 4\n"
      "[choice]\n"
      "symbol = \"AUDIO\"\n"
      "prompt = \"Colliding choice\"\n"
      "help = \"Choice symbols do not alias option symbols.\"\n"
      "members = [\"BOARD_A\", \"BOARD_B\"]\n"
      "cardinality = \"at_most_one\"\n");
}

static void expect_depth_file_and_byte_budgets(void) {
  char root[4096];
  ConfitV4Catalog *catalog = 0;
  ConfitDiagnostic diagnostic;
  char relative[4096] = "sys/dev";
  char *large;

  setup_base(root, sizeof(root));
  large = (char *)malloc(131074U);
  CONFIT_TEST_ASSERT(large != 0);
  memset(large, 'x', 131073U);
  large[131073U] = '\0';
  write_relative(root, "config/project.toml", large);
  free(large);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(
      confit_v4_catalog_load(root, &catalog, &diagnostic) == CONFIT_ERR_SCHEMA);
  CONFIT_TEST_ASSERT(catalog == 0);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));

  setup_base(root, sizeof(root));
  for (size_t index = 0U; index < 34U; ++index) {
    char segment[16];
    CONFIT_TEST_ASSERT(snprintf(segment, sizeof(segment), "/d%02zu", index) >
                       0);
    CONFIT_TEST_ASSERT(strlen(relative) + strlen(segment) +
                           strlen("/Config.toml") + 1U <
                       sizeof(relative));
    memcpy(relative + strlen(relative), segment, strlen(segment) + 1U);
  }
  {
    char config_path[4096];
    CONFIT_TEST_ASSERT(snprintf(config_path, sizeof(config_path),
                                "%s/Config.toml", relative) > 0);
    write_bool_option(root, config_path, "TOO_DEEP", 0, 0, 0);
  }
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(
      confit_v4_catalog_load(root, &catalog, &diagnostic) == CONFIT_ERR_SCHEMA);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));

  setup_base(root, sizeof(root));
  large = (char *)malloc(131074U);
  CONFIT_TEST_ASSERT(large != 0);
  memset(large, 'x', 131073U);
  large[131073U] = '\0';
  write_relative(root, "sys/dev/oversize/Config.toml", large);
  free(large);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(
      confit_v4_catalog_load(root, &catalog, &diagnostic) == CONFIT_ERR_SCHEMA);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));

  setup_base(root, sizeof(root));
  for (size_t index = 0U; index < 1025U; ++index) {
    char path[128];
    CONFIT_TEST_ASSERT(snprintf(path, sizeof(path),
                                "config/options/m%04zu/OWNERS.toml", index) >
                       0);
    write_relative(root, path,
                   "schema_version = 4\n"
                   "[defaults]\n"
                   "owner = \"budget.owner\"\n");
  }
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(
      confit_v4_catalog_load(root, &catalog, &diagnostic) == CONFIT_ERR_SCHEMA);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_projection_enum_and_provenance_failures(void) {
  char root[4096];
  ConfitV4Catalog *catalog = 0;
  ConfitDiagnostic diagnostic;
  setup_base(root, sizeof(root));
  write_bool_option(root, "config/options/duplicate/Config.toml", "AUDIO", 0,
                    0, 0);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(
      confit_v4_catalog_load(root, &catalog, &diagnostic) == CONFIT_ERR_SCHEMA);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));

  expect_replacement_failure(
      "config/options/dma/Config.toml",
      "schema_version = 4\n"
      "[option]\n"
      "symbol = \"DMA\"\n"
      "type = \"enum\"\n"
      "prompt = \"DMA\"\n"
      "help = \"DMA mode.\"\n"
      "menu = \"drivers.audio\"\n"
      "values = [\"none\", \"mapped\"]\n"
      "default = \"none\"\n");

  setup_base(root, sizeof(root));
  write_project(root, "\"config/options\"", "\"sys/dev\"", "");
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(
      confit_v4_catalog_load(root, &catalog, &diagnostic) == CONFIT_ERR_SCHEMA);
  CONFIT_TEST_ASSERT(catalog == 0);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));

  expect_replacement_failure(
      "sys/dev/audio/cmi8738/Config.toml",
      "schema_version = 4\n"
      "[option]\n"
      "symbol = \"DRIVER_AUDIO_CMI8738\"\n"
      "type = \"placement\"\n"
      "prompt = \"CMI\"\n"
      "help = \"CMI driver.\"\n"
      "menu = \"drivers.audio\"\n"
      "allowed = [\"off\", \"kernel\"]\n"
      "default = \"kernel\"\n");
}

static void expect_cycle_depth_and_fanout_failures(void) {
  char root[4096];
  ConfitV4Catalog *catalog = 0;
  ConfitDiagnostic diagnostic;
  char constraint[8192];

  setup_base(root, sizeof(root));
  write_bool_option(root, "config/options/audio/Config.toml", "AUDIO",
                    "[constraints]\nall = [\"BUS_PCI\"]\n", 0, 0);
  write_bool_option(root, "config/options/pci/Config.toml", "BUS_PCI",
                    "[constraints]\nall = [\"AUDIO\"]\n", 0, 0);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(
      confit_v4_catalog_load(root, &catalog, &diagnostic) ==
      CONFIT_ERR_DEPENDENCY);
  CONFIT_TEST_ASSERT(diagnostic.path != 0 && diagnostic.line > 0U &&
                     diagnostic.column > 0U);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));

  setup_base(root, sizeof(root));
  for (size_t index = 0U; index < 34U; ++index) {
    char path[128];
    char symbol[32];
    char next[32];
    char edge[128];
    CONFIT_TEST_ASSERT(
        snprintf(path, sizeof(path),
                 "config/options/chain-%02zu/Config.toml", index) > 0);
    CONFIT_TEST_ASSERT(
        snprintf(symbol, sizeof(symbol), "CHAIN_%02zu", index) > 0);
    if (index + 1U < 34U) {
      CONFIT_TEST_ASSERT(
          snprintf(next, sizeof(next), "CHAIN_%02zu", index + 1U) > 0);
      CONFIT_TEST_ASSERT(snprintf(edge, sizeof(edge),
                                  "[constraints]\nall = [\"%s\"]\n",
                                  next) > 0);
      write_bool_option(root, path, symbol, edge, 0, 0);
    } else {
      write_bool_option(root, path, symbol, 0, 0, 0);
    }
  }
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(
      confit_v4_catalog_load(root, &catalog, &diagnostic) ==
      CONFIT_ERR_DEPENDENCY);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));

  setup_base(root, sizeof(root));
  memcpy(constraint, "[constraints]\nall = [", strlen("[constraints]\nall = [") + 1U);
  for (size_t index = 0U; index < 129U; ++index) {
    char atom[32];
    CONFIT_TEST_ASSERT(snprintf(atom, sizeof(atom), "%s\"FAN_%03zu\"",
                                index == 0U ? "" : ", ", index) > 0);
    CONFIT_TEST_ASSERT(strlen(constraint) + strlen(atom) + 4U <
                       sizeof(constraint));
    memcpy(constraint + strlen(constraint), atom, strlen(atom) + 1U);
  }
  memcpy(constraint + strlen(constraint), "]\n", 3U);
  write_bool_option(root, "config/options/audio/Config.toml", "AUDIO",
                    constraint, 0, 0);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(
      confit_v4_catalog_load(root, &catalog, &diagnostic) == CONFIT_ERR_SCHEMA);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_choice_source_span_and_cardinality(void) {
  char root[4096];
  ConfitV4Catalog *catalog;
  ConfitV4Evaluation *evaluation = 0;
  ConfitDiagnostic diagnostic;
  const ConfitV4Assignment assignment = {
      "BOARD_A", "true", {"profile.toml", 42U, 7U}};
  int saw_choice = 0;
  setup_base(root, sizeof(root));
  write_relative(root, "config/choices/board/Config.toml",
                 "schema_version = 4\n"
                 "[choice]\n"
                 "symbol = \"BOARD_BACKEND\"\n"
                 "prompt = \"Board backend\"\n"
                 "help = \"Exactly one synthetic board backend.\"\n"
                 "members = [\"BOARD_A\", \"BOARD_B\"]\n"
                 "cardinality = \"exactly_one\"\n");
  catalog = load_ok(root);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v4_evaluate(catalog, 0, 0U, 0, 0U, &evaluation,
                                        &diagnostic) ==
                     CONFIT_ERR_CONFLICT);
  confit_v4_evaluation_free(evaluation);
  evaluation = 0;
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v4_evaluate(catalog, &assignment, 1U, 0, 0U,
                                        &evaluation, &diagnostic) ==
                     CONFIT_OK);
  for (size_t index = 0U;
       index < confit_v4_evaluation_reason_count(evaluation); ++index) {
    ConfitV4ReasonView reason;
    CONFIT_TEST_ASSERT(
        confit_v4_evaluation_reason(evaluation, index, &reason));
    if (reason.kind == CONFIT_V4_REASON_CHOICE) {
      saw_choice = 1;
      CONFIT_TEST_ASSERT(reason.satisfied);
      CONFIT_TEST_ASSERT(reason.source.path != 0 && reason.source.line > 0U &&
                         reason.source.column > 0U);
    }
  }
  CONFIT_TEST_ASSERT(saw_choice);
  confit_v4_evaluation_free(evaluation);
  confit_v4_catalog_free(catalog);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

int main(void) {
#if defined(_WIN32)
  return 0;
#else
  expect_multiple_drivers_and_reason_model();
  expect_v3_and_retired_fields_rejected();
  expect_dependency_failures();
  expect_no_auto_enable();
  expect_provider_selection_and_ambiguity();
  expect_provider_absence_and_cardinality();
  expect_multiple_provider_group();
  expect_discovery_hostile_corpus();
  expect_depth_file_and_byte_budgets();
  expect_projection_enum_and_provenance_failures();
  expect_cycle_depth_and_fanout_failures();
  expect_choice_source_span_and_cardinality();
  expect_tui_hierarchy_failures();
  return 0;
#endif
}
