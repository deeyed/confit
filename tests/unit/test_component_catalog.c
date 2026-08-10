#include "test_assert.h"
#include "test_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "confit/component_catalog.h"
#include "confit/host.h"
#include "confit/source_catalog.h"

static void join_path(char *out, size_t out_size, const char *left,
                      const char *right) {
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(out, out_size, left, right));
}

static void write_project(const char *root) {
  char config[4096];
  char path[4096];
  join_path(config, sizeof(config), root, "config");
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(config));
  join_path(path, sizeof(path), config, "project.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "[project]\n"
      "name = \"component_v3_test\"\n"
      "namespace = \"component_v3_test\"\n"
      "schema_version = 2\n"
      "component_roots = [\"components\"]\n"));
}

static void write_component(const char *root, const char *directory_name,
                            const char *id, const char *kind,
                            const char *requires, const char *provides,
                            const char *conflicts, const char *kapi_requires,
                            const char *kapi_provides, const char *include_name) {
  char components[4096];
  char directory[4096];
  char path[4096];
  char manifest[8192];
  char makefile[1024];
  join_path(components, sizeof(components), root, "components");
  join_path(directory, sizeof(directory), components, directory_name);
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(directory));
  CONFIT_TEST_ASSERT(snprintf(
      manifest, sizeof(manifest),
      "schema_version = 3\n"
      "[component]\n"
      "id = \"%s\"\n"
      "kind = \"%s\"\n"
      "summary = \"bounded v3 fixture for %s\"\n"
      "owner = \"%s\"\n"
      "[selection]\n"
      "requires = [%s]\n"
      "provides = [%s]\n"
      "conflicts = [%s]\n"
      "default = false\n"
      "[interfaces]\n"
      "kapi_requires = [%s]\n"
      "kapi_provides = [%s]\n",
      id, kind, id, id, requires, provides, conflicts, kapi_requires,
      kapi_provides) > 0);
  join_path(path, sizeof(path), directory, "component.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, manifest));
  join_path(path, sizeof(path), directory, "unit.c");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path, "int confit_component_v3_fixture(void) { return 3; }\n"));
  join_path(path, sizeof(path), directory, "public.h");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path, "#ifndef V3_PUBLIC_H\n#define V3_PUBLIC_H\n#endif\n"));
  CONFIT_TEST_ASSERT(snprintf(
      makefile, sizeof(makefile),
      "# 이 fixture는 자신의 source와 public facade만 등록한다.\n"
      "# 선택 정책과 runtime 권한은 이 Makefile이 소유하지 않는다.\n"
      "PARUS_MK_API = 3\n"
      "SRCS += unit.c\n"
      "PUBLIC_HEADERS += public.h\n"
      ".include <%s>\n",
      include_name) > 0);
  join_path(path, sizeof(path), directory, "Makefile");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, makefile));
}

static void setup_valid_catalog(char *root, size_t root_size) {
  CONFIT_TEST_ASSERT(confit_test_fs_make_temp_dir(
      root, root_size, "confit-component-v3"));
  write_project(root);
  write_component(root, "base", "kernel.foundation", "kernel_feature", "",
                  "\"kernel.foundation@1\"", "", "",
                  "\"parus.foundation.v1\"", "parus.component.mk");
  write_component(root, "driver", "driver.example", "kernel_provider",
                  "\"kernel.foundation@1\"", "\"driver.example@1\"", "",
                  "\"parus.foundation.v1\"", "", "parus.driver.mk");
}

static ConfitV2Project *load_project(const char *root,
                                     ConfitDiagnostic *diagnostic) {
  ConfitV2Project *project = 0;
  CONFIT_TEST_ASSERT(
      confit_v2_schema_load_project(root, &project, diagnostic) == CONFIT_OK);
  return project;
}

static void expect_valid_selection_and_reason_graph(void) {
  char root[4096] = {0};
  ConfitDiagnostic diagnostic;
  ConfitV2Project *project;
  ConfitComponentCatalog catalog;
  ConfitComponentClosure closure;
  const char *required[] = {"driver.example@1"};
  const char *optional[] = {"driver.absent@1"};
  const ConfitComponent *candidate = 0;
  char profile_path[4096];

  setup_valid_catalog(root, sizeof(root));
  confit_diagnostic_init(&diagnostic);
  project = load_project(root, &diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  memset(&closure, 0, sizeof(closure));
  CONFIT_TEST_ASSERT(
      confit_component_catalog_load(project, &catalog, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(catalog.component_count == 2U);
  CONFIT_TEST_ASSERT(strcmp(catalog.components[0].id, "driver.example") == 0);
  CONFIT_TEST_ASSERT(catalog.components[0].public_header_count == 1U);
  CONFIT_TEST_ASSERT(confit_component_catalog_find_feature_providers(
                         &catalog, "driver.example@1", &candidate, 1U) == 1U);
  CONFIT_TEST_ASSERT(candidate != 0 &&
                     strcmp(candidate->id, "driver.example") == 0);
  CONFIT_TEST_ASSERT(confit_component_catalog_resolve_features(
                         &catalog, required, 1U, optional, 1U, 0, 0U, &closure,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(closure.component_count == 2U);
  CONFIT_TEST_ASSERT(strcmp(closure.ordered[0]->id, "kernel.foundation") == 0);
  CONFIT_TEST_ASSERT(strcmp(closure.ordered[1]->id, "driver.example") == 0);
  CONFIT_TEST_ASSERT(closure.reason_count == 3U);
  CONFIT_TEST_ASSERT(closure.reasons[0].kind ==
                     CONFIT_COMPONENT_REASON_ROOT_FEATURE);
  CONFIT_TEST_ASSERT(closure.reasons[0].provider_selection ==
                     CONFIT_COMPONENT_PROVIDER_SELECTION_UNIQUE);
  CONFIT_TEST_ASSERT(closure.reasons[1].kind ==
                     CONFIT_COMPONENT_REASON_FEATURE_REQUIREMENT);
  CONFIT_TEST_ASSERT(strcmp(closure.reasons[1].from_id, "driver.example") == 0);
  CONFIT_TEST_ASSERT(closure.reasons[1].source_line > 0U);
  CONFIT_TEST_ASSERT(closure.reasons[2].kind ==
                     CONFIT_COMPONENT_REASON_KAPI_REQUIREMENT);
  CONFIT_TEST_ASSERT(closure.absent_optional_feature_count == 1U);
  CONFIT_TEST_ASSERT(strcmp(closure.absent_optional_features[0],
                            "driver.absent@1") == 0);
  CONFIT_TEST_ASSERT(closure.kapi_requirement_count == 1U);
  CONFIT_TEST_ASSERT(closure.kapi_provide_count == 1U);
  CONFIT_TEST_ASSERT(confit_component_closure_find_kapi_provider(
                         &closure, "parus.foundation.v1") ==
                     closure.ordered[0]);
  confit_component_closure_clear(&closure);
  join_path(profile_path, sizeof(profile_path), root, "unique-profile.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      profile_path,
      "schema_version = 3\n[profile]\nid = \"test.unique\"\n"
      "target = \"arm64-qemu-virt\"\n[features]\n"
      "enable = [\"driver.example@1\"]\n[providers]\n"));
  {
    char canonical_profile[4096];
    CONFIT_TEST_ASSERT(confit_host_path_canonicalize(
                           canonical_profile, sizeof(canonical_profile),
                           profile_path, &diagnostic) == CONFIT_OK);
    CONFIT_TEST_ASSERT(confit_component_catalog_resolve_profile_file(
                           &catalog, canonical_profile, &closure,
                           &diagnostic) == CONFIT_OK);
    CONFIT_TEST_ASSERT(strcmp(closure.reasons[0].source_path,
                              canonical_profile) == 0);
    CONFIT_TEST_ASSERT(closure.reasons[0].source_line == 6U);
    CONFIT_TEST_ASSERT(closure.reasons[0].source_column > 0U);
  }
  confit_component_closure_clear(&closure);
  confit_component_catalog_clear(&catalog);
  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_explicit_provider_and_ambiguity(void) {
  char root[4096] = {0};
  char profile_path[4096];
  ConfitDiagnostic diagnostic;
  ConfitV2Project *project;
  ConfitComponentCatalog catalog;
  ConfitComponentClosure closure;
  const char *required[] = {"driver.example@1"};
  const char *world_required[] = {"world.runtime@1"};
  ConfitComponentProviderChoice choice;

  setup_valid_catalog(root, sizeof(root));
  write_component(root, "driver_alt", "driver.example.alt", "kernel_provider",
                  "\"kernel.foundation@1\"", "\"driver.example@1\"", "",
                  "\"parus.foundation.v1\"", "", "parus.driver.mk");
  write_component(root, "runtime_a", "world.runtime.a", "world_feature", "",
                  "\"world.runtime@1\"", "", "", "", "parus.world.mk");
  write_component(root, "runtime_b", "world.runtime.b", "world_feature", "",
                  "\"world.runtime@1\"", "", "", "", "parus.world.mk");
  confit_diagnostic_init(&diagnostic);
  project = load_project(root, &diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  memset(&closure, 0, sizeof(closure));
  CONFIT_TEST_ASSERT(
      confit_component_catalog_load(project, &catalog, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_component_catalog_resolve_features(
                         &catalog, required, 1U, 0, 0U, 0, 0U, &closure,
                         &diagnostic) == CONFIT_ERR_SCHEMA);
  confit_component_closure_clear(&closure);
  confit_diagnostic_clear(&diagnostic);
  confit_diagnostic_init(&diagnostic);
  memset(&choice, 0, sizeof(choice));
  choice.feature = "driver.example@1";
  choice.component_id = "driver.example.alt";
  choice.source_path = "profile.toml";
  choice.source_line = 9U;
  choice.source_column = 1U;
  CONFIT_TEST_ASSERT(confit_component_catalog_resolve_features(
                         &catalog, required, 1U, 0, 0U, &choice, 1U, &closure,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(strcmp(closure.ordered[1]->id, "driver.example.alt") == 0);
  CONFIT_TEST_ASSERT(closure.reasons[0].provider_selection ==
                     CONFIT_COMPONENT_PROVIDER_SELECTION_EXPLICIT);
  confit_component_closure_clear(&closure);
  choice.feature = "world.runtime@1";
  choice.component_id = "world.runtime.a";
  CONFIT_TEST_ASSERT(confit_component_catalog_resolve_features(
                         &catalog, world_required, 1U, 0, 0U, &choice, 1U,
                         &closure, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(strcmp(closure.ordered[0]->id, "world.runtime.a") == 0);
  CONFIT_TEST_ASSERT(strcmp(closure.reasons[0].source_path, "profile.toml") == 0);
  confit_component_closure_clear(&closure);
  join_path(profile_path, sizeof(profile_path), root, "explicit-profile.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      profile_path,
      "schema_version = 3\n"
      "[profile]\nid = \"test.explicit\"\ntarget = \"arm64-qemu-virt\"\n"
      "[features]\nenable = [\"driver.example@1\"]\n"
      "[providers]\n\"driver.example@1\" = \"driver.example.alt\"\n"));
  {
    char canonical_profile[4096];
    CONFIT_TEST_ASSERT(confit_host_path_canonicalize(
                           canonical_profile, sizeof(canonical_profile),
                           profile_path, &diagnostic) == CONFIT_OK);
    memcpy(profile_path, canonical_profile, strlen(canonical_profile) + 1U);
  }
  {
    const ConfitStatus profile_status =
        confit_component_catalog_resolve_profile_file(
            &catalog, profile_path, &closure, &diagnostic);
    if (profile_status != CONFIT_OK) {
      fprintf(stderr, "profile diagnostic: %s\n",
              diagnostic.message != 0 ? diagnostic.message : "none");
    }
    CONFIT_TEST_ASSERT(profile_status == CONFIT_OK);
  }
  CONFIT_TEST_ASSERT(strcmp(closure.ordered[1]->id, "driver.example.alt") == 0);
  CONFIT_TEST_ASSERT(closure.reasons[0].provider_selection ==
                     CONFIT_COMPONENT_PROVIDER_SELECTION_EXPLICIT);
  confit_component_closure_clear(&closure);
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      profile_path,
      "schema_version = 3\n"
      "[profile]\nid = \"test.dump\"\ntarget = \"arm64-qemu-virt\"\n"
      "components = [\"kernel.foundation\", \"driver.example.alt\"]\n"
      "[features]\nenable = [\"driver.example@1\"]\n"
      "[providers]\n\"driver.example@1\" = \"driver.example.alt\"\n"));
  CONFIT_TEST_ASSERT(confit_component_catalog_resolve_profile_file(
                         &catalog, profile_path, &closure, &diagnostic) ==
                     CONFIT_ERR_SCHEMA);
  confit_component_closure_clear(&closure);
  confit_component_catalog_clear(&catalog);
  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_schema_rejected(const char *manifest_text) {
  char root[4096] = {0};
  char path[4096];
  ConfitDiagnostic diagnostic;
  ConfitV2Project *project;
  ConfitComponentCatalog catalog;
  setup_valid_catalog(root, sizeof(root));
  join_path(path, sizeof(path), root, "components/driver/component.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, manifest_text));
  confit_diagnostic_init(&diagnostic);
  project = load_project(root, &diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(
      confit_component_catalog_load(project, &catalog, &diagnostic) !=
      CONFIT_OK);
  CONFIT_TEST_ASSERT(diagnostic.path != 0);
  CONFIT_TEST_ASSERT(diagnostic.line > 0U);
  CONFIT_TEST_ASSERT(diagnostic.column > 0U);
  confit_component_catalog_clear(&catalog);
  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_old_and_injected_schema_rejection(void) {
  expect_schema_rejected(
      "schema_version = 2\n[component]\nid = \"driver.old\"\n"
      "kind = \"kernel_driver\"\n[requires]\ncomponents = []\nlinks = []\n"
      "[provides]\ncapabilities = [\"driver.old@1\"]\n");
  expect_schema_rejected(
      "schema_version = 3\n[component]\nid = \"driver.bad\"\n"
      "kind = \"kernel_driver\"\nsummary = \"bad\"\nowner = \"driver.bad\"\n"
      "[selection]\nrequires = []\nprovides = [\"driver.bad@1\"]\n"
      "conflicts = []\ndefault = false\n[interfaces]\nkapi_requires = []\n"
      "kapi_provides = []\n");
  expect_schema_rejected(
      "schema_version = 3\n[component]\nid = \"driver.bad\"\n"
      "kind = \"kernel_provider\"\nsummary = \"bad\"\nowner = \"driver.bad\"\n"
      "pci_vendor = 4660\n[selection]\nrequires = []\n"
      "provides = [\"driver.bad@1\"]\nconflicts = []\ndefault = false\n"
      "[interfaces]\nkapi_requires = []\nkapi_provides = []\n");
  expect_schema_rejected(
      "schema_version = 3\noutput = \"build/bad.o\"\n[component]\n"
      "id = \"driver.bad\"\nkind = \"kernel_provider\"\nsummary = \"bad\"\n"
      "owner = \"driver.bad\"\n[selection]\nrequires = []\n"
      "provides = [\"driver.bad@1\"]\nconflicts = []\ndefault = false\n"
      "[interfaces]\nkapi_requires = []\nkapi_provides = []\n");
  expect_schema_rejected(
      "schema_version = 3\nschema_version = 3\n[component]\n"
      "id = \"driver.bad\"\nkind = \"kernel_provider\"\nsummary = \"bad\"\n"
      "owner = \"driver.bad\"\n[selection]\nrequires = []\n"
      "provides = [\"driver.bad@1\"]\nconflicts = []\ndefault = false\n"
      "[interfaces]\nkapi_requires = []\nkapi_provides = []\n");
  expect_schema_rejected(
      "schema_version = 3\n[component]\nid = \"driver.bad\"\n"
      "kind = \"kernel_provider\"\nsummary = \"bad\xc3\x28\"\n"
      "owner = \"driver.bad\"\n[selection]\nrequires = []\n"
      "provides = [\"driver.bad@1\"]\nconflicts = []\ndefault = false\n"
      "[interfaces]\nkapi_requires = []\nkapi_provides = []\n");
}

static void expect_missing_version_and_duplicate_id_rejection(void) {
  char root[4096] = {0};
  ConfitDiagnostic diagnostic;
  ConfitV2Project *project;
  ConfitComponentCatalog catalog;
  ConfitComponentClosure closure;
  const char *missing[] = {"driver.missing@1"};
  const char *wrong_version[] = {"driver.example@2"};
  setup_valid_catalog(root, sizeof(root));
  confit_diagnostic_init(&diagnostic);
  project = load_project(root, &diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  memset(&closure, 0, sizeof(closure));
  CONFIT_TEST_ASSERT(
      confit_component_catalog_load(project, &catalog, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_component_catalog_resolve_features(
                         &catalog, missing, 1U, 0, 0U, 0, 0U, &closure,
                         &diagnostic) == CONFIT_ERR_SCHEMA);
  confit_component_closure_clear(&closure);
  CONFIT_TEST_ASSERT(confit_component_catalog_resolve_features(
                         &catalog, wrong_version, 1U, 0, 0U, 0, 0U, &closure,
                         &diagnostic) == CONFIT_ERR_SCHEMA);
  confit_component_closure_clear(&closure);
  confit_component_catalog_clear(&catalog);
  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));

  setup_valid_catalog(root, sizeof(root));
  write_component(root, "duplicate", "driver.example", "kernel_provider",
                  "\"kernel.foundation@1\"", "\"driver.duplicate@1\"", "",
                  "\"parus.foundation.v1\"", "", "parus.driver.mk");
  confit_diagnostic_init(&diagnostic);
  project = load_project(root, &diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(
      confit_component_catalog_load(project, &catalog, &diagnostic) ==
      CONFIT_ERR_SCHEMA);
  confit_component_catalog_clear(&catalog);
  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_make_rejection(void) {
  static const char *const invalid_makefiles[] = {
      "PARUS_MK_API = 2\nSRCS += unit.c\n.include <parus.driver.mk>\n",
      "PARUS_MK_API = 3\nall:\n\tcc unit.c\n.include <parus.driver.mk>\n",
      ("PARUS_MK_API = 3\nCFLAGS += -funsafe\nSRCS += unit.c\n"
       ".include <parus.driver.mk>\n"),
      "PARUS_MK_API = 3\nSRCS += ../escape.c\n.include <parus.driver.mk>\n",
      "PARUS_MK_API = 3\nSRCS += $(SHELL).c\n.include <parus.driver.mk>\n"};
  size_t index;
  for (index = 0U; index < sizeof(invalid_makefiles) /
                                  sizeof(invalid_makefiles[0]);
       ++index) {
    char root[4096] = {0};
    char path[4096];
    ConfitDiagnostic diagnostic;
    ConfitV2Project *project;
    ConfitComponentCatalog catalog;
    setup_valid_catalog(root, sizeof(root));
    join_path(path, sizeof(path), root, "components/driver/Makefile");
    CONFIT_TEST_ASSERT(
        confit_test_fs_write_file(path, invalid_makefiles[index]));
    confit_diagnostic_init(&diagnostic);
    project = load_project(root, &diagnostic);
    memset(&catalog, 0, sizeof(catalog));
    CONFIT_TEST_ASSERT(
        confit_component_catalog_load(project, &catalog, &diagnostic) ==
        CONFIT_ERR_SCHEMA);
    confit_component_catalog_clear(&catalog);
    confit_v2_project_free(project);
    CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
  }
}

static void expect_cycle_and_depth_rejection(void) {
  char root[4096] = {0};
  ConfitDiagnostic diagnostic;
  ConfitV2Project *project;
  ConfitComponentCatalog catalog;
  ConfitComponentClosure closure;
  const char *required[] = {"cycle.a@1"};
  setup_valid_catalog(root, sizeof(root));
  write_component(root, "cycle_a", "cycle.a", "kernel_feature",
                  "\"cycle.b@1\"", "\"cycle.a@1\"", "", "", "",
                  "parus.component.mk");
  write_component(root, "cycle_b", "cycle.b", "kernel_feature",
                  "\"cycle.a@1\"", "\"cycle.b@1\"", "", "", "",
                  "parus.component.mk");
  confit_diagnostic_init(&diagnostic);
  project = load_project(root, &diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  memset(&closure, 0, sizeof(closure));
  CONFIT_TEST_ASSERT(
      confit_component_catalog_load(project, &catalog, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_component_catalog_resolve_features(
                         &catalog, required, 1U, 0, 0U, 0, 0U, &closure,
                         &diagnostic) == CONFIT_ERR_SCHEMA);
  confit_component_closure_clear(&closure);
  confit_component_catalog_clear(&catalog);
  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));

  CONFIT_TEST_ASSERT(confit_test_fs_make_temp_dir(
      root, sizeof(root), "confit-component-depth"));
  write_project(root);
  {
    size_t index;
    for (index = 0U; index < 34U; ++index) {
      char directory[32];
      char id[32];
      char provides[64];
      char requires[64];
      (void)snprintf(directory, sizeof(directory), "n%02u", (unsigned)index);
      (void)snprintf(id, sizeof(id), "depth.n%02u", (unsigned)index);
      (void)snprintf(provides, sizeof(provides), "\"depth.n%02u@1\"",
                     (unsigned)index);
      if (index + 1U < 34U) {
        (void)snprintf(requires, sizeof(requires), "\"depth.n%02u@1\"",
                       (unsigned)(index + 1U));
      } else {
        requires[0] = '\0';
      }
      write_component(root, directory, id, "kernel_feature", requires,
                      provides, "", "", "", "parus.component.mk");
    }
  }
  confit_diagnostic_init(&diagnostic);
  project = load_project(root, &diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  memset(&closure, 0, sizeof(closure));
  CONFIT_TEST_ASSERT(
      confit_component_catalog_load(project, &catalog, &diagnostic) == CONFIT_OK);
  {
    const char *depth_root[] = {"depth.n00@1"};
    CONFIT_TEST_ASSERT(confit_component_catalog_resolve_features(
                           &catalog, depth_root, 1U, 0, 0U, 0, 0U, &closure,
                           &diagnostic) == CONFIT_ERR_SCHEMA);
  }
  confit_component_closure_clear(&closure);
  confit_component_catalog_clear(&catalog);
  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_kapi_and_conflict_rejection(void) {
  char root[4096] = {0};
  ConfitDiagnostic diagnostic;
  ConfitV2Project *project;
  ConfitComponentCatalog catalog;
  ConfitComponentClosure closure;
  ConfitNucleusCatalog nucleus;
  const char *required[] = {"driver.example@1"};
  char path[4096];

  setup_valid_catalog(root, sizeof(root));
  join_path(path, sizeof(path), root, "components/base/component.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      path,
      "schema_version = 3\n[component]\nid = \"kernel.foundation\"\n"
      "kind = \"kernel_feature\"\nsummary = \"no kapi\"\n"
      "owner = \"kernel.foundation\"\n[selection]\nrequires = []\n"
      "provides = [\"kernel.foundation@1\"]\nconflicts = []\ndefault = false\n"
      "[interfaces]\nkapi_requires = []\nkapi_provides = []\n"));
  confit_diagnostic_init(&diagnostic);
  project = load_project(root, &diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  memset(&closure, 0, sizeof(closure));
  memset(&nucleus, 0, sizeof(nucleus));
  CONFIT_TEST_ASSERT(
      confit_component_catalog_load(project, &catalog, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_component_catalog_resolve_features(
                         &catalog, required, 1U, 0, 0U, 0, 0U, &closure,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_static_kapi_validate(
                         &closure, &nucleus, &diagnostic) == CONFIT_ERR_SCHEMA);
  confit_component_closure_clear(&closure);
  confit_component_catalog_clear(&catalog);
  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));

  setup_valid_catalog(root, sizeof(root));
  write_component(root, "duplicate-kapi", "kernel.duplicate.kapi",
                  "kernel_provider", "", "\"kernel.duplicate.kapi@1\"",
                  "", "", "\"parus.foundation.v1\"",
                  "parus.component.mk");
  confit_diagnostic_init(&diagnostic);
  project = load_project(root, &diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  memset(&closure, 0, sizeof(closure));
  CONFIT_TEST_ASSERT(
      confit_component_catalog_load(project, &catalog, &diagnostic) == CONFIT_OK);
  {
    const char *duplicate_kapi[] = {
        "driver.example@1", "kernel.duplicate.kapi@1"};
    CONFIT_TEST_ASSERT(confit_component_catalog_resolve_features(
                           &catalog, duplicate_kapi, 2U, 0, 0U, 0, 0U,
                           &closure, &diagnostic) == CONFIT_ERR_SCHEMA);
  }
  confit_component_closure_clear(&closure);
  confit_component_catalog_clear(&catalog);
  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));

  setup_valid_catalog(root, sizeof(root));
  write_component(root, "conflict", "kernel.conflict", "kernel_feature", "",
                  "\"kernel.conflict@1\"", "\"driver.example@1\"", "",
                  "", "parus.component.mk");
  confit_diagnostic_init(&diagnostic);
  project = load_project(root, &diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  memset(&closure, 0, sizeof(closure));
  CONFIT_TEST_ASSERT(
      confit_component_catalog_load(project, &catalog, &diagnostic) == CONFIT_OK);
  {
    const char *conflicting[] = {"driver.example@1", "kernel.conflict@1"};
    CONFIT_TEST_ASSERT(confit_component_catalog_resolve_features(
                           &catalog, conflicting, 2U, 0, 0U, 0, 0U, &closure,
                           &diagnostic) == CONFIT_ERR_SCHEMA);
  }
  confit_component_closure_clear(&closure);
  confit_component_catalog_clear(&catalog);
  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_symlink_rejection(void) {
#if !defined(_WIN32)
  char root[4096] = {0};
  char path[4096];
  char target[4096];
  ConfitDiagnostic diagnostic;
  ConfitV2Project *project;
  ConfitComponentCatalog catalog;
  setup_valid_catalog(root, sizeof(root));
  join_path(path, sizeof(path), root, "components/driver/unit.c");
  join_path(target, sizeof(target), root, "outside.c");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(target, "int outside;\n"));
  CONFIT_TEST_ASSERT(unlink(path) == 0);
  CONFIT_TEST_ASSERT(symlink(target, path) == 0);
  confit_diagnostic_init(&diagnostic);
  project = load_project(root, &diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(
      confit_component_catalog_load(project, &catalog, &diagnostic) ==
      CONFIT_ERR_SCHEMA);
  confit_component_catalog_clear(&catalog);
  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));

  setup_valid_catalog(root, sizeof(root));
  join_path(path, sizeof(path), root, "components/driver/Makefile");
  join_path(target, sizeof(target), root, "outside.Makefile");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      target,
      "PARUS_MK_API = 3\nSRCS += unit.c\n.include <parus.driver.mk>\n"));
  CONFIT_TEST_ASSERT(unlink(path) == 0);
  CONFIT_TEST_ASSERT(symlink(target, path) == 0);
  confit_diagnostic_init(&diagnostic);
  project = load_project(root, &diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(
      confit_component_catalog_load(project, &catalog, &diagnostic) !=
      CONFIT_OK);
  confit_component_catalog_clear(&catalog);
  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));

  setup_valid_catalog(root, sizeof(root));
  join_path(path, sizeof(path), root, "components/driver/component.toml");
  join_path(target, sizeof(target), root, "outside.component.toml");
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      target,
      "schema_version = 3\n[component]\nid = \"driver.example\"\n"
      "kind = \"kernel_provider\"\nsummary = \"outside\"\n"
      "owner = \"driver.example\"\n[selection]\n"
      "requires = [\"kernel.foundation@1\"]\n"
      "provides = [\"driver.example@1\"]\nconflicts = []\n"
      "default = false\n[interfaces]\n"
      "kapi_requires = [\"parus.foundation.v1\"]\n"
      "kapi_provides = []\n"));
  CONFIT_TEST_ASSERT(unlink(path) == 0);
  CONFIT_TEST_ASSERT(symlink(target, path) == 0);
  confit_diagnostic_init(&diagnostic);
  project = load_project(root, &diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(
      confit_component_catalog_load(project, &catalog, &diagnostic) !=
      CONFIT_OK);
  confit_component_catalog_clear(&catalog);
  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
#endif
}

static void expect_oversized_list_and_manifest_rejection(void) {
  char root[4096] = {0};
  char path[4096];
  char manifest[32768];
  size_t offset;
  size_t index;
  offset = (size_t)snprintf(
      manifest, sizeof(manifest),
      "schema_version = 3\n[component]\nid = \"driver.large\"\n"
      "kind = \"kernel_provider\"\nsummary = \"large\"\nowner = \"driver.large\"\n"
      "[selection]\nrequires = []\nprovides = [");
  for (index = 0U; index < 129U; ++index) {
    offset += (size_t)snprintf(manifest + offset, sizeof(manifest) - offset,
                               "%s\"feature.large%03u@1\"",
                               index == 0U ? "" : ",", (unsigned)index);
  }
  (void)snprintf(manifest + offset, sizeof(manifest) - offset,
                 "]\nconflicts = []\ndefault = false\n[interfaces]\n"
                 "kapi_requires = []\nkapi_provides = []\n");
  expect_schema_rejected(manifest);

  setup_valid_catalog(root, sizeof(root));
  join_path(path, sizeof(path), root, "components/driver/component.toml");
  {
    char *oversized = (char *)malloc(140000U);
    CONFIT_TEST_ASSERT(oversized != 0);
    memset(oversized, 'x', 139998U);
    oversized[139998U] = '\n';
    oversized[139999U] = '\0';
    CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, oversized));
    free(oversized);
  }
  {
    ConfitDiagnostic diagnostic;
    ConfitV2Project *project;
    ConfitComponentCatalog catalog;
    confit_diagnostic_init(&diagnostic);
    project = load_project(root, &diagnostic);
    memset(&catalog, 0, sizeof(catalog));
    CONFIT_TEST_ASSERT(
        confit_component_catalog_load(project, &catalog, &diagnostic) !=
        CONFIT_OK);
    confit_component_catalog_clear(&catalog);
    confit_v2_project_free(project);
  }
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

static void expect_catalog_node_budget_rejection(void) {
  char root[4096] = {0};
  size_t index;
  ConfitDiagnostic diagnostic;
  ConfitV2Project *project;
  ConfitComponentCatalog catalog;
  CONFIT_TEST_ASSERT(confit_test_fs_make_temp_dir(
      root, sizeof(root), "confit-component-count"));
  write_project(root);
  for (index = 0U; index < 513U; ++index) {
    char directory[32];
    char id[32];
    char feature[64];
    (void)snprintf(directory, sizeof(directory), "c%03u", (unsigned)index);
    (void)snprintf(id, sizeof(id), "catalog.c%03u", (unsigned)index);
    (void)snprintf(feature, sizeof(feature), "\"catalog.c%03u@1\"",
                   (unsigned)index);
    write_component(root, directory, id, "kernel_feature", "", feature, "",
                    "", "", "parus.component.mk");
  }
  confit_diagnostic_init(&diagnostic);
  project = load_project(root, &diagnostic);
  memset(&catalog, 0, sizeof(catalog));
  CONFIT_TEST_ASSERT(
      confit_component_catalog_load(project, &catalog, &diagnostic) !=
      CONFIT_OK);
  confit_component_catalog_clear(&catalog);
  confit_v2_project_free(project);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

int main(void) {
  expect_valid_selection_and_reason_graph();
  expect_explicit_provider_and_ambiguity();
  expect_old_and_injected_schema_rejection();
  expect_missing_version_and_duplicate_id_rejection();
  expect_make_rejection();
  expect_cycle_and_depth_rejection();
  expect_kapi_and_conflict_rejection();
  expect_symlink_rejection();
  expect_oversized_list_and_manifest_rejection();
  expect_catalog_node_budget_rejection();
  return 0;
}
