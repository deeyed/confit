#include <stdio.h>
#include <string.h>

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
  ConfitV4ArtifactOptions targeted_options;
  ConfitTargetPlan target_plan;
  ConfitV4PublishOptions publication;
  char root[1024];
  char selected_config[1200];
  char generation_config[1200];
  char partial_config[1200];
  char generation_relative[96];
  char *selected_text;
  char *generation_text;
  size_t changed = 0U;

  confit_diagnostic_init(&diagnostic);
  memset(&artifacts, 0, sizeof(artifacts));
  memset(&targeted, 0, sizeof(targeted));
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

  /* Make injection과 path escape는 resolver 밖에서 artifact adapter를 직접
   * 호출하더라도 fail-closed여야 한다. 이 fixture는 target selection 의미를
   * 주장하지 않고 target.mk serializer의 closed byte vocabulary만 검증한다. */
  target_plan.target_id = "synthetic-arm64";
  target_plan.isa = "arm64";
  target_plan.abi = "aapcs64";
  target_plan.cpu_profile = "cortex-a72";
  target_plan.entry_profile = "arm64-fdt-v1";
  target_plan.toolchain_id = "aarch64-none-elf";
  target_plan.toolchain_kind = "clang-lld-v1";
  target_plan.target_triple = "aarch64-none-elf";
  target_plan.compiler_path = "/usr/bin/clang";
  target_plan.archiver_path = "/usr/bin/llvm-ar";
  target_plan.linker_path = "/usr/bin/ld.lld";
  target_plan.resource_include_path = "/usr/lib/clang/include";
  target_plan.sysroot_path = "lib/libc/aarch64-unknown-none-elf";
  target_plan.link_emulation = "aarch64elf";
  target_plan.linker_script = "targets/arm64/synthetic/linker.ld";
  target_plan.image_kind = "elf-flat-v1";
  target_plan.package_profile = "manifest-v1";
  target_plan.machine_profile = "synthetic-arm64-v1";
  target_plan.machine_runner = "qemu-v1";
  target_plan.machine_architecture = "arm64";
  target_plan.machine_executable = "qemu-system-aarch64";
  target_plan.machine_name = "virt";
  target_plan.machine_cpu = "cortex-a72";
  target_plan.machine_serial = "stdio-v1";
  target_plan.machine_artifact = "flat-image-v1";
  target_plan.machine_memory_mib = 1024U;
  target_plan.expected_component = "target.arm64.synthetic";
  target_plan.expected_capability = "image.arm64.synthetic@1";
  target_plan.output_stem = "parus-synthetic-arm64";
  target_plan.required_profile = "release";
  target_plan.user_artifact_profile = "none";
  target_plan.max_image_bytes = 67108864U;
  targeted_options.target_plan = &target_plan;
  CONFIT_TEST_ASSERT(confit_v4_generate_artifacts(
                         snapshot, &targeted_options, &targeted,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(strstr(targeted.target_mk,
                            "PARUS_TARGET_PLAN_ABI:= 1") != 0);
  CONFIT_TEST_ASSERT(strstr(targeted.target_mk,
                            "PARUS_TARGET_MACHINE_RUNNER:= qemu-v1") != 0);
  CONFIT_TEST_ASSERT(strstr(targeted.target_mk,
                            "PARUS_TARGET_MACHINE_MEMORY_MIB:= 1024") != 0);
  confit_v4_artifact_set_clear(&targeted);
  target_plan.linker_script = "../escape/linker.ld";
  CONFIT_TEST_ASSERT(confit_v4_generate_artifacts(
                         snapshot, &targeted_options, &targeted,
                         &diagnostic) == CONFIT_ERR_SCHEMA);
  target_plan.linker_script = "targets/arm64/synthetic/linker.ld";
  target_plan.compiler_path = "/usr/bin/clang$(touch_bad)";
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v4_generate_artifacts(
                         snapshot, &targeted_options, &targeted,
                         &diagnostic) == CONFIT_ERR_SCHEMA);
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(confit_test_fs_make_temp_dir(
      root, sizeof(root), "confit-publish"));
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
  CONFIT_TEST_ASSERT(confit_v4_publish_artifacts(
                         &publication, &artifacts, &changed,
                         &diagnostic) == CONFIT_OK);
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
