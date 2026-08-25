#if !defined(_WIN32) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "confit/config_v5.h"
#include "confit/diagnostic.h"
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

static void fixture(char root[4096]) {
  char canonical[4096];
  CONFIT_TEST_ASSERT(
      confit_test_fs_make_temp_dir(root, 4096U, "confit-config-v5"));
  CONFIT_TEST_ASSERT(realpath(root, canonical) != 0);
  (void)snprintf(root, 4096U, "%s", canonical);
  make_dir(root, "config/menus");
  make_dir(root, "config/choices");
  make_dir(root, "config/constraints");
  make_dir(root, "config/options");
  make_dir(root, "config/options/alt-machine");
  make_dir(root, "config/options/scheduler");
  make_dir(root, "config/options/hostname");
  make_dir(root, "sys/dev/audio/cmi8738");
  make_dir(root, "world");
  make_dir(root, "sys/arch/arm64");
  make_dir(root, "sys/board/arm64/qemu/virt");
  make_dir(root, "config/kernconf/arm64");
  write_file(root, "config/project.toml",
             "schema_version = 5\n"
             "[project]\nname = \"Parus test\"\nnamespace = \"parus.test\"\n"
             "[discovery]\nmenus = \"config/menus\"\n"
             "choices = \"config/choices\"\n"
             "constraints = \"config/constraints\"\n"
             "common_options = [\"config/options\", \"sys/dev\", \"world\"]\n"
             "architecture_root = \"sys/arch\"\n"
             "board_root = \"sys/board\"\n"
             "kernconf_root = \"config/kernconf\"\n");
  write_file(root, "config/menus/Config.toml",
             "schema_version = 5\n[menu]\nid = \"drivers.audio\"\n"
             "prompt = \"Audio drivers\"\nhelp = \"Audio menu.\"\n"
             "order = 10\n");
  write_file(root, "config/options/Config.toml",
             "schema_version = 5\n[option]\nsymbol = \"BUS_PCI\"\n"
             "type = \"bool\"\nprompt = \"PCI bus\"\nhelp = \"PCI support.\"\n"
             "menu = \"drivers.audio\"\nmenu_order = 1\n"
             "owner = \"sys.bus.pci\"\nsince = \"0.6\"\n"
             "stability = \"experimental\"\ntags = [\"bus\"]\ndefault = true\n");
  write_file(root, "config/options/alt-machine/Config.toml",
             "schema_version = 5\n[option]\nsymbol = \"MACHINE_ALT\"\n"
             "type = \"bool\"\nprompt = \"Alternative machine\"\n"
             "help = \"Alternative machine for choice validation.\"\n"
             "menu = \"drivers.audio\"\nmenu_order = 2\n"
             "owner = \"sys.machine.alt\"\nsince = \"0.6\"\n"
             "stability = \"experimental\"\ntags = [\"machine\"]\n"
             "default = false\n");
  write_file(root, "config/options/scheduler/Config.toml",
             "schema_version = 5\n[option]\nsymbol = \"SCHED_POLICY\"\n"
             "type = \"enum\"\nprompt = \"Scheduler policy\"\n"
             "help = \"Select one scheduler policy value.\"\n"
             "menu = \"drivers.audio\"\nmenu_order = 3\n"
             "owner = \"sys.kern.sched\"\nsince = \"0.6\"\n"
             "stability = \"experimental\"\ntags = [\"scheduler\"]\n"
             "values = [\"off\", \"fair\", \"realtime\"]\n"
             "enabled_values = [\"fair\", \"realtime\"]\ndefault = \"off\"\n");
  write_file(root, "config/options/hostname/Config.toml",
             "schema_version = 5\n[option]\nsymbol = \"SYSTEM_LABEL\"\n"
             "type = \"string\"\nprompt = \"System label\"\n"
             "help = \"Human readable development system label.\"\n"
             "menu = \"drivers.audio\"\nmenu_order = 4\n"
             "owner = \"sys.kern.identity\"\nsince = \"0.6\"\n"
             "stability = \"experimental\"\ntags = [\"identity\"]\n"
             "default = \"parus\"\n");
  write_file(root, "sys/dev/audio/cmi8738/Config.toml",
             "schema_version = 5\n[option]\n"
             "symbol = \"DRIVER_AUDIO_CMI8738\"\ntype = \"placement\"\n"
             "prompt = \"CMI8738 audio controller\"\n"
             "help = \"Select the audio driver placement.\"\n"
             "menu = \"drivers.audio\"\nmenu_order = 20\n"
             "owner = \"sys.dev.audio.cmi8738\"\nsince = \"0.6\"\n"
             "stability = \"experimental\"\ntags = [\"driver\", \"audio\"]\n"
             "allowed = [\"off\", \"kernel\", \"service\"]\n"
             "default = \"off\"\n[constraints]\nall = [\"BUS_PCI\"]\n"
             "[ui]\nvisible_all = [\"BUS_PCI\"]\n");
  write_file(root, "sys/arch/arm64/Config.toml",
             "schema_version = 5\n[option]\nsymbol = \"ARCH_ARM64_PAGE_BITS\"\n"
             "type = \"integer\"\nprompt = \"Page bits\"\nhelp = \"Page size exponent.\"\n"
             "menu = \"drivers.audio\"\nmenu_order = 30\n"
             "owner = \"sys.arch.arm64\"\nsince = \"0.6\"\n"
             "stability = \"experimental\"\ntags = [\"arch\"]\n"
             "minimum = 12\nmaximum = 16\ndefault = 12\n");
  write_file(root, "sys/board/arm64/qemu/virt/Config.toml",
             "schema_version = 5\n[option]\nsymbol = \"MACHINE_QEMU_VIRT\"\n"
             "type = \"bool\"\nprompt = \"QEMU virt\"\nhelp = \"QEMU machine.\"\n"
             "menu = \"drivers.audio\"\nmenu_order = 40\n"
             "owner = \"sys.board.arm64.qemu.virt\"\nsince = \"0.6\"\n"
             "stability = \"experimental\"\ntags = [\"board\"]\ndefault = false\n");
  write_file(root, "config/choices/Config.toml",
             "schema_version = 5\n[choice]\nsymbol = \"MACHINE_KIND\"\n"
             "prompt = \"Machine kind\"\nhelp = \"Select exactly one machine.\"\n"
             "members = [\"MACHINE_QEMU_VIRT\", \"MACHINE_ALT\"]\n"
             "cardinality = \"exactly_one\"\n");
  write_file(root, "config/constraints/Config.toml",
             "schema_version = 5\n[[rule]]\nif_all = [\"MACHINE_QEMU_VIRT\"]\n"
             "require_all = [\"BUS_PCI\"]\n"
             "message = \"QEMU virt requires the selected PCI bus option.\"\n");
  write_file(root, "config/kernconf/arm64/vm-v0.toml",
             "schema_version = 5\n"
             "[[assignments]]\nsymbol = \"MACHINE_QEMU_VIRT\"\nvalue = \"true\"\n"
             "[[assignments]]\nsymbol = \"DRIVER_AUDIO_CMI8738\"\n"
             "value = \"kernel\"\n"
             "[[assignments]]\nsymbol = \"SCHED_POLICY\"\nvalue = \"fair\"\n"
             "[[assignments]]\nsymbol = \"SYSTEM_LABEL\"\nvalue = \"vm-v0\"\n");
}

static ConfitStatus load(const char *root, const char *arch,
                         const char *kernconf, ConfitV5Catalog **out,
                         ConfitDiagnostic *diagnostic) {
  ConfitV5CatalogRequest request = {root, arch, kernconf};
  return confit_v5_catalog_load(&request, out, diagnostic);
}

static void expect_kernconf_resolution(void) {
  char root[4096];
  ConfitV5Catalog *catalog = 0;
  ConfitV5Evaluation *evaluation = 0;
  ConfitDiagnostic diagnostic;
  ConfitStatus status;
  fixture(root);
  confit_diagnostic_init(&diagnostic);
  status = load(root, "arm64", "vm-v0", &catalog, &diagnostic);
  if (status != CONFIT_OK)
    (void)fprintf(stderr, "Config v5 fixture rejected: %s (%s:%zu)\n",
                  diagnostic.message != 0 ? diagnostic.message : "no diagnostic",
                  diagnostic.path != 0 ? diagnostic.path : "unknown",
                  diagnostic.line);
  CONFIT_TEST_ASSERT(status == CONFIT_OK);
  CONFIT_TEST_ASSERT(strcmp(confit_v5_catalog_architecture(catalog),
                            "arm64") == 0);
  CONFIT_TEST_ASSERT(strcmp(confit_v5_catalog_kernconf(catalog), "vm-v0") == 0);
  CONFIT_TEST_ASSERT(confit_v5_catalog_option_count(catalog) == 7U);
  CONFIT_TEST_ASSERT(confit_v5_catalog_choice_count(catalog) == 1U);
  CONFIT_TEST_ASSERT(confit_v5_catalog_rule_count(catalog) == 1U);
  CONFIT_TEST_ASSERT(confit_v5_catalog_assignment_count(catalog) == 4U);
  CONFIT_TEST_ASSERT(confit_v5_evaluate_kernconf(catalog, &evaluation,
                                                 &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(strcmp(confit_v5_evaluation_value(
                                evaluation, "DRIVER_AUDIO_CMI8738"),
                            "kernel") == 0);
  CONFIT_TEST_ASSERT(strcmp(confit_v5_evaluation_value(
                                evaluation, "MACHINE_QEMU_VIRT"),
                            "true") == 0);
  CONFIT_TEST_ASSERT(strcmp(confit_v5_evaluation_value(evaluation,
                                                        "SCHED_POLICY"),
                            "fair") == 0);
  CONFIT_TEST_ASSERT(strcmp(confit_v5_evaluation_value(evaluation,
                                                        "SYSTEM_LABEL"),
                            "vm-v0") == 0);
  {
    int saw_visibility = 0;
    int saw_choice = 0;
    int saw_rule = 0;
    for (size_t index = 0U;
         index < confit_v5_evaluation_reason_count(evaluation); ++index) {
      ConfitV5ReasonView reason;
      CONFIT_TEST_ASSERT(confit_v5_evaluation_reason(evaluation, index,
                                                      &reason));
      if (reason.kind == CONFIT_V5_REASON_VISIBILITY && reason.satisfied)
        saw_visibility = 1;
      if (reason.kind == CONFIT_V5_REASON_CHOICE && reason.satisfied)
        saw_choice = 1;
      if (reason.kind == CONFIT_V5_REASON_RULE && reason.satisfied)
        saw_rule = 1;
    }
    CONFIT_TEST_ASSERT(saw_visibility && saw_choice && saw_rule);
  }
  confit_v5_evaluation_free(evaluation);
  confit_v5_catalog_free(catalog);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_legacy_and_unknown_rejected(void) {
  char root[4096];
  ConfitV5Catalog *catalog = 0;
  ConfitDiagnostic diagnostic;
  fixture(root);
  write_file(root, "config/kernconf/arm64/vm-v0.toml",
             "schema_version = 5\n[target]\nid = \"legacy\"\n");
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(load(root, "arm64", "vm-v0", &catalog, &diagnostic) ==
                     CONFIT_ERR_SCHEMA);
  write_file(root, "config/kernconf/arm64/vm-v0.toml",
             "schema_version = 4\n[[assignments]]\n"
             "symbol = \"BUS_PCI\"\nvalue = \"true\"\n");
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(load(root, "arm64", "vm-v0", &catalog, &diagnostic) ==
                     CONFIT_ERR_SCHEMA);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_duplicate_needs_and_scope_rejected(void) {
  char root[4096];
  ConfitV5Catalog *catalog = 0;
  ConfitDiagnostic diagnostic;
  fixture(root);
  write_file(root, "config/kernconf/arm64/vm-v0.toml",
             "schema_version = 5\n"
             "[[assignments]]\nsymbol = \"BUS_PCI\"\nvalue = \"true\"\n"
             "[[assignments]]\nsymbol = \"BUS_PCI\"\nvalue = \"false\"\n");
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(load(root, "arm64", "vm-v0", &catalog, &diagnostic) ==
                     CONFIT_ERR_CONFLICT);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
  fixture(root);
  write_file(root, "sys/dev/audio/cmi8738/Config.toml",
             "schema_version = 5\n[option]\nsymbol = \"BAD\"\n"
             "type = \"bool\"\nprompt = \"Bad\"\nhelp = \"Bad.\"\n"
             "menu = \"drivers.audio\"\nmenu_order = 50\n"
             "owner = \"sys.bad\"\nsince = \"0.6\"\n"
             "stability = \"experimental\"\ntags = [\"bad\"]\ndefault = false\n"
             "[constraints]\nneeds = [\"BUS_PCI\"]\n");
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(load(root, "arm64", "vm-v0", &catalog, &diagnostic) ==
                     CONFIT_ERR_SCHEMA);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
  fixture(root);
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(load(root, "../arm64", "vm-v0", &catalog, &diagnostic) ==
                     CONFIT_ERR_INVALID_ARGUMENT);
  CONFIT_TEST_ASSERT(load(root, "amd64", "vm-v0", &catalog, &diagnostic) !=
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_symlink_and_case_collision_rejected(void) {
  char root[4096];
  char link_path[4096];
  ConfitV5Catalog *catalog = 0;
  ConfitDiagnostic diagnostic;
  fixture(root);
  join(link_path, root, "config/options/linked");
  CONFIT_TEST_ASSERT(symlink("../menus", link_path) == 0);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(load(root, "arm64", "vm-v0", &catalog, &diagnostic) ==
                     CONFIT_ERR_SCHEMA);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));

  fixture(root);
  write_file(root, "config/options/config.toml", "schema_version = 5\n");
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(load(root, "arm64", "vm-v0", &catalog, &diagnostic) ==
                     CONFIT_ERR_SCHEMA);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_constraints_do_not_auto_enable(void) {
  char root[4096];
  ConfitV5Catalog *catalog = 0;
  ConfitV5Evaluation *evaluation = 0;
  ConfitDiagnostic diagnostic;
  fixture(root);
  write_file(root, "config/kernconf/arm64/vm-v0.toml",
             "schema_version = 5\n"
             "[[assignments]]\nsymbol = \"BUS_PCI\"\nvalue = \"false\"\n"
             "[[assignments]]\nsymbol = \"DRIVER_AUDIO_CMI8738\"\n"
             "value = \"kernel\"\n");
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(load(root, "arm64", "vm-v0", &catalog, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_v5_evaluate_kernconf(catalog, &evaluation,
                                                 &diagnostic) ==
                     CONFIT_ERR_DEPENDENCY);
  CONFIT_TEST_ASSERT(strcmp(confit_v5_evaluation_value(evaluation, "BUS_PCI"),
                            "false") == 0);
  confit_v5_evaluation_free(evaluation);
  confit_v5_catalog_free(catalog);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

int main(void) {
#if defined(_WIN32)
  return 0;
#else
  expect_kernconf_resolution();
  expect_legacy_and_unknown_rejected();
  expect_duplicate_needs_and_scope_rejected();
  expect_symlink_and_case_collision_rejected();
  expect_constraints_do_not_auto_enable();
  return 0;
#endif
}
