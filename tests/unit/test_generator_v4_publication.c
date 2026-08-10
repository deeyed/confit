#include <stdio.h>
#include <string.h>

#if !defined(_WIN32)
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "confit/constraint_v2.h"
#include "confit/diagnostic.h"
#include "confit/generator_v2.h"
#include "confit/host.h"
#include "confit/link_v2.h"
#include "confit/resolver_v2.h"
#include "confit/schema_v2.h"
#include "confit/status.h"
#include "test_assert.h"
#include "test_fs.h"

#ifndef CONFIT_TEST_SOURCE_DIR
#define CONFIT_TEST_SOURCE_DIR "."
#endif

static ConfitStatus load_snapshot(ConfitV2Snapshot **out_snapshot,
                                  ConfitDiagnostic *diagnostic) {
  ConfitV2Project *project = 0;
  ConfitV2LinkedProject *linked = 0;
  ConfitV2CompiledStructure *compiled = 0;
  char fixture[1024];
  ConfitStatus status;

  *out_snapshot = 0;
  status = confit_host_path_join(fixture, sizeof(fixture),
                                 CONFIT_TEST_SOURCE_DIR,
                                 "tests/fixtures/schema-v2/valid", diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_schema_load_project(fixture, &project, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_schema_link_project(project, &linked, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_compile_structure(linked, &compiled, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_snapshot_resolve(compiled, 0, out_snapshot, diagnostic);
  }
  confit_v2_compiled_structure_free(compiled);
  confit_v2_linked_project_free(linked);
  confit_v2_project_free(project);
  return status;
}

int main(void) {
  ConfitDiagnostic diagnostic;
  ConfitV2Snapshot *snapshot = 0;
  ConfitV4ArtifactSet artifacts;
  ConfitV4ArtifactSet incomplete;
  ConfitV4ArtifactSet targeted;
  ConfitV4ArtifactSet component_first;
  ConfitV4ArtifactSet component_second;
  ConfitV4ArtifactOptions targeted_options;
  ConfitTargetPlan target_plan;
  ConfitV4PublishOptions publication;
  char root[1024];
  char canonical_root[1024];
  char selected_config[1200];
  char generation_config[1200];
  char partial_config[1200];
  char generation_relative[96];
#if !defined(_WIN32)
  char stale_generation[1200];
  char stale_artifact[1200];
  char symlink_root[1024];
  char symlink_alias[1100];
#endif
  char *selected_text;
  char *generation_text;
  size_t changed = 0U;

  confit_diagnostic_init(&diagnostic);
  memset(&artifacts, 0, sizeof(artifacts));
  memset(&targeted, 0, sizeof(targeted));
  memset(&component_first, 0, sizeof(component_first));
  memset(&component_second, 0, sizeof(component_second));
  memset(&targeted_options, 0, sizeof(targeted_options));
  memset(&target_plan, 0, sizeof(target_plan));
  memset(&publication, 0, sizeof(publication));
  CONFIT_TEST_ASSERT(load_snapshot(&snapshot, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(snapshot != 0);
  CONFIT_TEST_ASSERT(confit_v4_generate_artifacts(
                         snapshot, 0, &artifacts, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(strstr(artifacts.config_header,
                            "#define CONFIT_ARTIFACT_ABI 4") != 0);
  CONFIT_TEST_ASSERT(strstr(artifacts.config_mk,
                            "CONFIT_ARTIFACT_ABI:= 4") != 0);
  CONFIT_TEST_ASSERT(strstr(artifacts.config_mk,
                            "/target.mk\"") != 0);
  CONFIT_TEST_ASSERT(strstr(artifacts.config_mk,
                            "/tests.mk\"") != 0);
  CONFIT_TEST_ASSERT(strstr(artifacts.target_mk,
                            "PARUS_TARGET_PLAN_ABI:= 0") != 0);
  CONFIT_TEST_ASSERT(artifacts.reason_json != 0 && artifacts.tests_mk != 0);

  /* schema v3 component selection은 catalog/profile digest, KAPI 의미와
   * deterministic ordering을 selected bundle에 함께 봉인한다. */
  {
    char *features[] = {"world.synthetic@1"};
    char *roots[] = {"world.synthetic@1"};
    const ConfitComponent *ordered[1];
    ConfitComponent component;
    ConfitComponentCatalog catalog;
    ConfitComponentClosure closure;
    ConfitComponentReason reason;
    ConfitV2ArtifactInput profile_input;
    ConfitV4ArtifactOptions component_options;
    memset(&component, 0, sizeof(component));
    memset(&catalog, 0, sizeof(catalog));
    memset(&closure, 0, sizeof(closure));
    memset(&reason, 0, sizeof(reason));
    memset(&profile_input, 0, sizeof(profile_input));
    memset(&component_options, 0, sizeof(component_options));
    component.id = "world.synthetic";
    component.kind = CONFIT_COMPONENT_KIND_WORLD_FEATURE;
    component.summary = "synthetic deterministic world feature";
    component.owner = "world.synthetic";
    component.manifest_path = "components/world.synthetic/component.toml";
    component.makefile_path = "components/world.synthetic/Makefile";
    component.build_include = "parus.world.mk";
    component.feature_provides = features;
    component.feature_provide_count = 1U;
    catalog.project_root = "/tmp";
    catalog.components = &component;
    catalog.component_count = 1U;
    ordered[0] = &component;
    closure.root_features = roots;
    closure.root_feature_count = 1U;
    closure.ordered = ordered;
    closure.component_count = 1U;
    reason.kind = CONFIT_COMPONENT_REASON_ROOT_FEATURE;
    reason.provider_selection = CONFIT_COMPONENT_PROVIDER_SELECTION_UNIQUE;
    reason.component_id = "world.synthetic";
    reason.requirement = "world.synthetic@1";
    reason.source_path = "/tmp/profile.toml";
    reason.source_line = 7U;
    reason.source_column = 11U;
    closure.reasons = &reason;
    closure.reason_count = 1U;
    profile_input.path = "config/profiles/profile.toml";
    profile_input.content_hash =
        "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    profile_input.role = "profile";
    component_options.inputs = &profile_input;
    component_options.input_count = 1U;
    component_options.component_catalog = &catalog;
    component_options.component_closure = &closure;
    {
      const ConfitStatus component_status = confit_v4_generate_artifacts(
          snapshot, &component_options, &component_first, &diagnostic);
      if (component_status != CONFIT_OK) {
        fprintf(stderr, "component serialization diagnostic: %s\n",
                diagnostic.message != 0 ? diagnostic.message : "none");
      }
      CONFIT_TEST_ASSERT(component_status == CONFIT_OK);
    }
    CONFIT_TEST_ASSERT(confit_v4_generate_artifacts(
                           snapshot, &component_options, &component_second,
                           &diagnostic) == CONFIT_OK);
    CONFIT_TEST_ASSERT(strcmp(component_first.bundle_digest,
                              component_second.bundle_digest) == 0);
    CONFIT_TEST_ASSERT(strcmp(component_first.selection_json,
                              component_second.selection_json) == 0);
    CONFIT_TEST_ASSERT(strstr(component_first.selection_json,
                              "\"schema_version\": 3") != 0);
    CONFIT_TEST_ASSERT(strstr(component_first.selection_json,
                              "\"catalog_digest\"") != 0);
    CONFIT_TEST_ASSERT(strstr(component_first.selection_json,
                              profile_input.content_hash) != 0);
    CONFIT_TEST_ASSERT(strstr(component_first.component_catalog_json,
                              "confit-component-catalog-v3") != 0);
    CONFIT_TEST_ASSERT(strstr(component_first.tests_mk,
                              "PARUS_TEST_IDS:=") != 0);
    CONFIT_TEST_ASSERT(strstr(component_first.nucleus_mk,
                              "PARUS_NUCLEUS_UNIT_IDS:=") != 0);
    confit_v4_artifact_set_clear(&component_first);
    confit_v4_artifact_set_clear(&component_second);
  }

  /* Make injection과 path escape는 resolver 밖에서 artifact adapter를 직접
   * 호출하더라도 fail-closed여야 한다. 이 fixture는 target selection 의미를
   * 주장하지 않고 target.mk serializer의 closed byte vocabulary만 검증한다. */
  target_plan.target_id = "synthetic-novel64";
  target_plan.isa = "novel64";
  target_plan.abi = "novel64-abi";
  target_plan.cpu_profile = "novel64-generic";
  target_plan.entry_profile = "novel64-boot-v1";
  target_plan.toolchain_id = "novel64-none-elf";
  target_plan.toolchain_kind = "clang-lld-v1";
  target_plan.target_triple = "novel64-none-elf";
  target_plan.compiler_path = "/usr/bin/clang";
  target_plan.compiler_sha256 =
      "1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  target_plan.compiler_version = "21.0.0";
  target_plan.archiver_path = "/usr/bin/llvm-ar";
  target_plan.archiver_sha256 =
      "2123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  target_plan.archiver_version = "20.1.8";
  target_plan.linker_path = "/usr/bin/ld.lld";
  target_plan.linker_sha256 =
      "3123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  target_plan.linker_version = "22.1.8";
  target_plan.resource_include_path = "/usr/lib/clang/include";
  target_plan.sysroot_path = "lib/libc/novel64-unknown-none-elf";
  target_plan.link_emulation = "novel64elf";
  target_plan.linker_script = "targets/novel64/synthetic/linker.ld";
  target_plan.image_kind = "elf-flat-v1";
  target_plan.package_profile = "manifest-v1";
  target_plan.machine_profile = "synthetic-novel64-v1";
  target_plan.machine_runner = "qemu-v1";
  target_plan.machine_architecture = "novel64";
  target_plan.machine_executable = "qemu-system-novel64";
  target_plan.machine_executable_path = "/usr/bin/qemu-system-novel64";
  target_plan.machine_executable_sha256 =
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  target_plan.machine_executable_version = "11.0.2";
  target_plan.machine_name = "virt";
  target_plan.machine_cpu = "novel64-generic";
  target_plan.machine_serial = "stdio-v1";
  target_plan.machine_artifact = "flat-image-v1";
  target_plan.machine_memory_mib = 1024U;
  target_plan.expected_component = "sys.board.novel64.synthetic";
  target_plan.expected_capability = "image.novel64.synthetic@1";
  target_plan.output_stem = "parus-synthetic-novel64";
  target_plan.required_profile = "release";
  {
    static char *kernel_roles[] = {
        "elf=kernel.elf", "map=kernel.map", "kapi=metadata/kapi.v2.stamp",
        "driverdb=metadata/driverdb.o", "sysinitdb=metadata/sysinitdb.o",
        "release_report=kernel.release.v1"};
    target_plan.kernel_artifact_profile = "elf-v1";
    target_plan.kernel_artifact_roles = kernel_roles;
    target_plan.kernel_artifact_role_count =
        sizeof(kernel_roles) / sizeof(kernel_roles[0]);
  }
  target_plan.max_kernel_bytes = 8388608U;
  target_plan.world_artifact_profile = "world-v1";
  target_plan.world_boot_component = "world.service.synthetic.init";
  target_plan.world_artifact_entry = "_start";
  target_plan.world_artifact_linker_script =
      "world/linker/novel64/user.ld";
  {
    static char *world_roles[] = {
        "service=artifacts/services",
        "library_private=artifacts/lib/private",
        "install_plan=install-plan.v1",
        "terminal=seal/worldgen.v1"};
    target_plan.world_artifact_roles = world_roles;
    target_plan.world_artifact_role_count =
        sizeof(world_roles) / sizeof(world_roles[0]);
  }
  target_plan.max_world_bytes = 20971520U;
  target_plan.max_image_bytes = 67108864U;
  targeted_options.target_plan = &target_plan;
  CONFIT_TEST_ASSERT(confit_v4_generate_artifacts(
                         snapshot, &targeted_options, &targeted,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(strstr(targeted.target_mk,
                            "PARUS_TARGET_PLAN_ABI:= 3") != 0);
  CONFIT_TEST_ASSERT(strstr(targeted.target_mk,
                            "PARUS_TARGET_KERNEL_ARTIFACT_ROLE_elf:= kernel.elf") != 0);
  CONFIT_TEST_ASSERT(strstr(targeted.target_mk,
                            "PARUS_TARGET_ISA:= novel64") != 0);
  CONFIT_TEST_ASSERT(strstr(targeted.target_mk,
                            "PARUS_TARGET_EXPECTED_COMPONENT:= sys.board.novel64.synthetic") != 0);
  CONFIT_TEST_ASSERT(strstr(targeted.target_mk,
                            "PARUS_TARGET_MACHINE_RUNNER:= qemu-v1") != 0);
  CONFIT_TEST_ASSERT(strstr(targeted.target_mk,
                            "PARUS_TARGET_MACHINE_MEMORY_MIB:= 1024") != 0);
  CONFIT_TEST_ASSERT(strstr(targeted.target_mk,
                            "PARUS_TARGET_MACHINE_EXECUTABLE_VERSION:= 11.0.2") != 0);
  CONFIT_TEST_ASSERT(strstr(targeted.target_mk,
                            "PARUS_TARGET_WORLD_ARTIFACT_ROLE_service:= artifacts/services") != 0);
  CONFIT_TEST_ASSERT(strstr(targeted.target_mk,
                            "PARUS_TARGET_WORLD_BOOT_COMPONENT:= world.service.synthetic.init") != 0);
  confit_v4_artifact_set_clear(&targeted);
  target_plan.machine_executable_sha256 =
      "g123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  CONFIT_TEST_ASSERT(confit_v4_generate_artifacts(
                         snapshot, &targeted_options, &targeted,
                         &diagnostic) == CONFIT_ERR_SCHEMA);
  target_plan.machine_executable_sha256 =
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  target_plan.linker_script = "../escape/linker.ld";
  CONFIT_TEST_ASSERT(confit_v4_generate_artifacts(
                         snapshot, &targeted_options, &targeted,
                         &diagnostic) == CONFIT_ERR_SCHEMA);
  target_plan.linker_script = "targets/novel64/synthetic/linker.ld";
  target_plan.compiler_path = "/usr/bin/clang$(touch_bad)";
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v4_generate_artifacts(
                         snapshot, &targeted_options, &targeted,
                         &diagnostic) == CONFIT_ERR_SCHEMA);
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(confit_test_fs_make_temp_dir(
      root, sizeof(root), "confit-publish"));
  CONFIT_TEST_ASSERT(confit_host_path_canonicalize(
      canonical_root, sizeof(canonical_root), root, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(strlen(canonical_root) < sizeof(root));
  memcpy(root, canonical_root, strlen(canonical_root) + 1U);
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      selected_config, sizeof(selected_config), root, "selected/config.mk"));
  CONFIT_TEST_ASSERT(snprintf(generation_relative,
                              sizeof(generation_relative),
                              "generations/%s/config.mk",
                              artifacts.bundle_digest) > 0);
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      generation_config, sizeof(generation_config), root, generation_relative));
  CONFIT_TEST_ASSERT(snprintf(generation_relative,
                              sizeof(generation_relative),
                              ".staging/%s.0000.staging/config.h",
                              artifacts.bundle_digest) > 0);
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      partial_config, sizeof(partial_config), root, generation_relative));

  publication.output_root = root;
  publication.fault_after_artifact = 3U;
  CONFIT_TEST_ASSERT(confit_v4_publish_artifacts(
                         &publication, &artifacts, &changed,
                         &diagnostic) == CONFIT_ERR_GENERATION);
  CONFIT_TEST_ASSERT(!confit_test_fs_file_exists(selected_config));
  CONFIT_TEST_ASSERT(!confit_test_fs_file_exists(generation_config));
  CONFIT_TEST_ASSERT(!confit_test_fs_file_exists(partial_config));

  confit_diagnostic_clear(&diagnostic);
  publication.fault_after_artifact = 0U;
  changed = 0U;
  {
    const ConfitStatus publish_status = confit_v4_publish_artifacts(
        &publication, &artifacts, &changed, &diagnostic);
    if (publish_status != CONFIT_OK) {
      fprintf(stderr, "publication diagnostic: %s\n",
              diagnostic.message != 0 ? diagnostic.message : "none");
    }
    CONFIT_TEST_ASSERT(publish_status == CONFIT_OK);
  }
  CONFIT_TEST_ASSERT(changed == 1U);
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(selected_config));
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(generation_config));
  selected_text = confit_test_fs_read_file(selected_config);
  generation_text = confit_test_fs_read_file(generation_config);
  CONFIT_TEST_ASSERT(selected_text != 0 && generation_text != 0);
  CONFIT_TEST_ASSERT(strcmp(selected_text, generation_text) == 0);
  confit_test_fs_free(selected_text);
  confit_test_fs_free(generation_text);

  changed = 99U;
  CONFIT_TEST_ASSERT(confit_v4_publish_artifacts(
                         &publication, &artifacts, &changed,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(changed == 0U);

#if !defined(_WIN32)
  {
    static const char *const artifact_names[] = {
        "config.h", "config.mk", "config.values.mk",
        "config.selection.json", "config.inputs.json", "config.reason.json",
        "config.report.json", "config.bundle.json", "components.mk",
        "component.catalog.json", "nucleus.mk", "tests.mk", "target.mk"};
    static const char stale_digest[] =
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    size_t artifact_index;
    CONFIT_TEST_ASSERT(snprintf(generation_relative,
                                sizeof(generation_relative),
                                "generations/%s", stale_digest) > 0);
    CONFIT_TEST_ASSERT(confit_test_fs_path_join(
        stale_generation, sizeof(stale_generation), root, generation_relative));
    CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(stale_generation));
    for (artifact_index = 0U;
         artifact_index < sizeof(artifact_names) / sizeof(artifact_names[0]);
         ++artifact_index) {
      CONFIT_TEST_ASSERT(confit_test_fs_path_join(
          stale_artifact, sizeof(stale_artifact), stale_generation,
          artifact_names[artifact_index]));
      CONFIT_TEST_ASSERT(confit_test_fs_write_file(stale_artifact, "stale\n"));
    }
    CONFIT_TEST_ASSERT(chmod(stale_generation, 0555) == 0);
    CONFIT_TEST_ASSERT(confit_v4_publish_artifacts(
                           &publication, &artifacts, &changed,
                           &diagnostic) == CONFIT_OK);
    errno = 0;
    CONFIT_TEST_ASSERT(stat(stale_generation, &(struct stat){0}) != 0 &&
                       errno == ENOENT);
  }

  CONFIT_TEST_ASSERT(confit_test_fs_make_temp_dir(
      symlink_root, sizeof(symlink_root), "confit-publish-real"));
  CONFIT_TEST_ASSERT(snprintf(symlink_alias, sizeof(symlink_alias), "%s-alias",
                              symlink_root) > 0);
  CONFIT_TEST_ASSERT(symlink(symlink_root, symlink_alias) == 0);
  publication.output_root = symlink_alias;
  CONFIT_TEST_ASSERT(confit_v4_publish_artifacts(
                         &publication, &artifacts, &changed,
                         &diagnostic) == CONFIT_ERR_GENERATION);
  CONFIT_TEST_ASSERT(unlink(symlink_alias) == 0);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(symlink_root));
  publication.output_root = root;
#endif

  incomplete = artifacts;
  incomplete.config_header = 0;
  CONFIT_TEST_ASSERT(confit_v4_publish_artifacts(
                         &publication, &incomplete, &changed,
                         &diagnostic) == CONFIT_ERR_INVALID_ARGUMENT);
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(selected_config));

  confit_v4_artifact_set_clear(&artifacts);
  confit_v2_snapshot_free(snapshot);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
  return 0;
}
