#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "confit/build_policy.h"
#include "confit/diagnostic.h"
#include "confit/target_plan.h"
#include "test_assert.h"
#include "test_fs.h"

static const char kDigestA[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
static const char kDigestB[] =
    "1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
static const char kDigestC[] =
    "2123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
static const char kDigestD[] =
    "3123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

static void initialize_target(ConfitTargetPlan *plan) {
  static char *kernel_roles[] = {
      "elf=kernel.elf", "map=kernel.map", "kapi=metadata/kapi.v2.stamp",
      "driverdb=metadata/driverdb.o", "sysinitdb=metadata/sysinitdb.o",
      "release_report=kernel.release.v1"};
  static char *world_roles[] = {
      "service=artifacts/services", "library_private=artifacts/lib/private",
      "install_plan=install-plan.v1", "terminal=seal/worldgen.v1"};
  static char *image_roles[] = {
      "kernel_payload=artifacts/kernel.img", "package=artifacts/image.pkg",
      "manifest=manifest/imagegen.v1", "terminal=seal/imagegen.v1"};
  memset(plan, 0, sizeof(*plan));
  plan->target_id = "arm64-qemu-virt";
  plan->isa = "arm64";
  plan->abi = "aapcs64";
  plan->cpu_profile = "cortex-a72";
  plan->entry_profile = "el1-entry-v1";
  plan->toolchain_id = "arm64-none-elf";
  plan->toolchain_kind = "clang-lld-v1";
  plan->target_triple = "aarch64-none-elf";
  plan->compiler_path = "/usr/bin/clang";
  plan->compiler_sha256 = (char *)kDigestA;
  plan->compiler_version = "21.0.0";
  plan->archiver_path = "/usr/bin/llvm-ar";
  plan->archiver_sha256 = (char *)kDigestB;
  plan->archiver_version = "21.0.0";
  plan->linker_path = "/usr/bin/ld.lld";
  plan->linker_sha256 = (char *)kDigestC;
  plan->linker_version = "21.0.0";
  plan->kernel_artifact_roles = kernel_roles;
  plan->kernel_artifact_role_count =
      sizeof(kernel_roles) / sizeof(kernel_roles[0]);
  plan->world_artifact_profile = "world-v1";
  plan->world_artifact_roles = world_roles;
  plan->world_artifact_role_count =
      sizeof(world_roles) / sizeof(world_roles[0]);
  plan->image_artifact_roles = image_roles;
  plan->image_artifact_role_count =
      sizeof(image_roles) / sizeof(image_roles[0]);
  plan->support_provider_owner = "sys.arch.arm64.support";
  plan->support_consumer_owner = "sys.board.arm64.qemu_virt";
  plan->expected_component = "sys.board.arm64.qemu_virt";
  plan->support_role = "architecture.facade.v1";
  plan->support_facade_include_root = "sys/include/parus/arch/arm64";
  plan->support_required_kapi = "parus.arch.arm64.support.v1";
  plan->machine_executable = "qemu-system-aarch64";
  plan->machine_executable_path = "/usr/bin/qemu-system-aarch64";
  plan->machine_executable_sha256 = (char *)kDigestD;
  plan->machine_executable_version = "11.0.2";
  plan->machine_trust_profile = "qemu-executable-v1";
  plan->machine_resource_identity = "arm64-qemu-virt-machine-v1";
  plan->machine_evidence_transport = "qemu-fwcfg-challenge-v1";
  plan->machine_evidence_protocol = "parus-qemu-terminal-v1";
  plan->machine_evidence_max_bytes = 65536U;
}

static void expect_policy_rejected(ConfitTargetPlan *plan,
                                   ConfitDiagnostic *diagnostic) {
  char *policy = 0;
  char digest[65];
  CONFIT_TEST_ASSERT(confit_build_policy_generate(
                         "release", kDigestA, plan, kDigestB, &policy, digest,
                         diagnostic) == CONFIT_ERR_SCHEMA);
  CONFIT_TEST_ASSERT(policy == 0);
  confit_diagnostic_clear(diagnostic);
}

static void expect_wire_byte_rejected(const ConfitBuildPolicyRef *policy,
                                      const unsigned char *wire, size_t size,
                                      size_t offset, unsigned char value,
                                      ConfitDiagnostic *diagnostic) {
  unsigned char changed[CONFIT_BUILD_ACTION_MAX_BYTES];
  CONFIT_TEST_ASSERT(size <= sizeof(changed) && offset < size);
  memcpy(changed, wire, size);
  changed[offset] = value;
  CONFIT_TEST_ASSERT(confit_build_action_wire_validate(
                         policy, changed, size, diagnostic) ==
                     CONFIT_ERR_SCHEMA);
  confit_diagnostic_clear(diagnostic);
}

static void expect_retired_private_include_rejected(void) {
  ConfitV2Project project;
  ConfitTargetPlan plan;
  ConfitDiagnostic diagnostic;
  char root[1024];
  char target_directory[1200];
  char target_path[1200];
  char *target_dirs[] = {"targets"};
  confit_diagnostic_init(&diagnostic);
  memset(&project, 0, sizeof(project));
  memset(&plan, 0, sizeof(plan));
  CONFIT_TEST_ASSERT(confit_test_fs_make_temp_dir(
      root, sizeof(root), "confit-retired-private-includes"));
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      target_directory, sizeof(target_directory), root, "targets"));
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(target_directory));
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      target_path, sizeof(target_path), target_directory, "retired.toml"));
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(
      target_path,
      "[target]\nname = \"retired\"\nschema_version = 3\n\n"
      "[values]\n\"parus.target.isa\" = \"arm64\"\n\n"
      "[build]\nprivate_includes = [\"sys\"]\n\n"
      "[support]\nprovider_owner = \"sys.arch.arm64.support\"\n"
      "consumer_owner = \"sys.board.arm64.fixture\"\n"
      "role = \"architecture.facade.v1\"\n"
      "facade_include_root = \"sys/include/parus/arch/arm64\"\n"
      "required_kapi = \"parus.arch.arm64.support.v1\"\n"));
  project.project_root = root;
  project.config_root = root;
  project.target_dirs.items = target_dirs;
  project.target_dirs.count = 1U;
  CONFIT_TEST_ASSERT(confit_target_plan_load(
                         &project, "retired", &plan, &diagnostic) ==
                     CONFIT_ERR_SCHEMA);
  confit_target_plan_clear(&plan);
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
}

int main(void) {
  ConfitDiagnostic diagnostic;
  ConfitTargetPlan plan;
  ConfitBuildActionTool trusted_tools[2];
  ConfitBuildPolicyRef policy_ref;
  ConfitBuildActionInput inputs[2];
  ConfitBuildActionOutput outputs[2];
  ConfitBuildAction action;
  unsigned char wire_a[CONFIT_BUILD_ACTION_MAX_BYTES];
  unsigned char wire_b[CONFIT_BUILD_ACTION_MAX_BYTES];
  char *policy_a = 0;
  char *policy_b = 0;
  char *policy_mk = 0;
  char policy_digest_a[65];
  char policy_digest_b[65];
  char action_id_a[65];
  char action_id_b[65];
  size_t wire_size_a = 0U;
  size_t wire_size_b = 0U;
  char oversized_path[CONFIT_BUILD_ACTION_MAX_PATH_BYTES + 2U];

  confit_diagnostic_init(&diagnostic);
  initialize_target(&plan);
  CONFIT_TEST_ASSERT(confit_build_policy_generate(
                         "release", kDigestA, &plan, kDigestB, &policy_a,
                         policy_digest_a, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_build_policy_generate(
                         "release", kDigestA, &plan, kDigestB, &policy_b,
                         policy_digest_b, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(strcmp(policy_a, policy_b) == 0);
  CONFIT_TEST_ASSERT(strcmp(policy_digest_a, policy_digest_b) == 0);
  CONFIT_TEST_ASSERT_CONTAINS(policy_a, "target.isa=arm64\n");
  CONFIT_TEST_ASSERT_CONTAINS(policy_a, "artifact.world.0=service=");
  CONFIT_TEST_ASSERT_CONTAINS(
      policy_a, "support.required_kapi=parus.arch.arm64.support.v1\n");
  CONFIT_TEST_ASSERT_CONTAINS(
      policy_a, "machine.evidence_transport=qemu-fwcfg-challenge-v1\n");
  CONFIT_TEST_ASSERT_CONTAINS(policy_a, "tool.target_ld.sha256=");
  CONFIT_TEST_ASSERT(confit_build_policy_generate_make_adapter(
                         policy_digest_a, kDigestA, kDigestB, &policy_mk,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT_CONTAINS(policy_mk,
                              "PARUS_BUILD_POLICY_ABI:= 1\n");

  /* Support record는 문자열 표식이 아니라 selected consumer가 요구하고 selected
   * provider가 유일하게 게시하는 exact KAPI edge여야 한다. */
  {
    ConfitComponent components[2];
    ConfitComponentCatalog catalog;
    ConfitComponentClosure closure;
    const ConfitComponent *ordered[2];
    char *board_features[] = {"board.arm64.qemu.virt@1"};
    char *board_requires[] = {"parus.arch.arm64.support.v1"};
    char *arch_provides[] = {"parus.arch.arm64.support.v1"};
    memset(components, 0, sizeof(components));
    memset(&catalog, 0, sizeof(catalog));
    memset(&closure, 0, sizeof(closure));
    components[0].id = "sys.arch.arm64";
    components[0].owner = "sys.arch.arm64.support";
    components[0].kapi_provides = arch_provides;
    components[0].kapi_provide_count = 1U;
    components[1].id = "sys.board.arm64.qemu_virt";
    components[1].owner = "sys.board.arm64.qemu_virt";
    components[1].feature_provides = board_features;
    components[1].feature_provide_count = 1U;
    components[1].kapi_requires = board_requires;
    components[1].kapi_requirement_count = 1U;
    catalog.components = components;
    catalog.component_count = 2U;
    ordered[0] = &components[0];
    ordered[1] = &components[1];
    closure.ordered = ordered;
    closure.component_count = 2U;
    plan.expected_component = "sys.board.arm64.qemu_virt";
    plan.expected_capability = "board.arm64.qemu.virt@1";
    plan.world_artifact_profile = "none";
    CONFIT_TEST_ASSERT(confit_target_plan_validate_selection(
                           &plan, &catalog, &closure,
                           &diagnostic) == CONFIT_OK);
    components[1].kapi_requirement_count = 0U;
    CONFIT_TEST_ASSERT(confit_target_plan_validate_selection(
                           &plan, &catalog, &closure,
                           &diagnostic) == CONFIT_ERR_CONFLICT);
    components[1].kapi_requirement_count = 1U;
    components[0].owner = "sys.arch.arm64.wrong";
    confit_diagnostic_clear(&diagnostic);
    CONFIT_TEST_ASSERT(confit_target_plan_validate_selection(
                           &plan, &catalog, &closure,
                           &diagnostic) == CONFIT_ERR_CONFLICT);
    components[0].owner = "sys.arch.arm64.support";
    plan.expected_component = "sys.board.arm64.qemu_virt";
    plan.expected_capability = 0;
    plan.world_artifact_profile = "world-v1";
    confit_diagnostic_clear(&diagnostic);
  }

  /* Parus의 현재 target ID/ISA 집합을 child 안에서 source와 독립적인
   * serializer fixture로 고정한다. 이는 compile/boot 증거가 아니다. */
  {
    typedef struct SupportedTarget {
      const char *id;
      const char *isa;
      const char *abi;
      const char *cpu;
      const char *entry;
      const char *toolchain;
      const char *triple;
      const char *provider;
      const char *consumer;
      const char *facade;
      const char *kapi;
      const char *qemu;
      const char *qemu_path;
    } SupportedTarget;
    static const SupportedTarget targets[] = {
        {"qemu-raspi4b-aarch64", "arm64", "aapcs64", "cortex-a72",
         "arm64-fdt-v1", "aarch64-none-elf", "aarch64-none-elf",
         "sys.arch.arm64.support", "sys.board.arm64.qemu_raspi4b",
         "sys/include/parus/arch/arm64", "parus.arch.arm64.support.v1",
         "qemu-system-aarch64", "/usr/bin/qemu-system-aarch64"},
        {"raspberrypi-rpi5-rph1", "arm64", "aapcs64", "cortex-a76",
         "arm64-rph1-v1", "aarch64-none-elf", "aarch64-none-elf",
         "sys.arch.arm64.support", "sys.board.arm64.rpi5_rph1",
         "sys/include/parus/arch/arm64", "parus.arch.arm64.support.v1",
         0, 0},
        {"qemu-virt-aarch64", "arm64", "aapcs64", "cortex-a72",
         "arm64-fdt-v1", "aarch64-none-elf", "aarch64-none-elf",
         "sys.arch.arm64.support", "sys.board.arm64.qemu_virt",
         "sys/include/parus/arch/arm64", "parus.arch.arm64.support.v1",
         "qemu-system-aarch64", "/usr/bin/qemu-system-aarch64"},
        {"amd64-uefi", "amd64", "sysv-amd64", "x86_64",
         "amd64-rph1-v1", "x86_64-none-elf", "x86_64-none-elf",
         "sys.arch.amd64.support", "sys.board.amd64.qemu_pc",
         "sys/include/parus/arch/amd64", "parus.arch.amd64.support.v1",
         "qemu-system-x86_64", "/usr/bin/qemu-system-x86_64"},
        {"raspberrypi-rpi5", "arm64", "aapcs64", "cortex-a76",
         "arm64-fdt-v1", "aarch64-none-elf", "aarch64-none-elf",
         "sys.arch.arm64.support", "sys.board.arm64.rpi5",
         "sys/include/parus/arch/arm64", "parus.arch.arm64.support.v1",
         0, 0},
        {"qemu-virt-riscv64-rph1", "riscv64", "riscv-lp64d", "rv64gc",
         "riscv-rph1-v1", "riscv64-none-elf", "riscv64-none-elf",
         "sys.arch.riscv64.support", "sys.board.riscv64.qemu_virt",
         "sys/include/parus/arch/riscv64", "parus.arch.riscv64.support.v1",
         "qemu-system-riscv64", "/usr/bin/qemu-system-riscv64"},
        {"qemu-virt-aarch64-rph1", "arm64", "aapcs64", "cortex-a72",
         "arm64-rph1-v1", "aarch64-none-elf", "aarch64-none-elf",
         "sys.arch.arm64.support", "sys.board.arm64.qemu_virt_rph1",
         "sys/include/parus/arch/arm64", "parus.arch.arm64.support.v1",
         "qemu-system-aarch64", "/usr/bin/qemu-system-aarch64"}};
    size_t target_index;
    for (target_index = 0U;
         target_index < sizeof(targets) / sizeof(targets[0]);
         ++target_index) {
      const SupportedTarget *selected = &targets[target_index];
      free(policy_b);
      policy_b = 0;
      plan.target_id = (char *)selected->id;
      plan.isa = (char *)selected->isa;
      plan.abi = (char *)selected->abi;
      plan.cpu_profile = (char *)selected->cpu;
      plan.entry_profile = (char *)selected->entry;
      plan.toolchain_id = (char *)selected->toolchain;
      plan.target_triple = (char *)selected->triple;
      plan.support_provider_owner = (char *)selected->provider;
      plan.support_consumer_owner = (char *)selected->consumer;
      plan.expected_component = (char *)selected->consumer;
      plan.support_facade_include_root = (char *)selected->facade;
      plan.support_required_kapi = (char *)selected->kapi;
      plan.machine_executable = (char *)selected->qemu;
      plan.machine_executable_path = (char *)selected->qemu_path;
      CONFIT_TEST_ASSERT(confit_build_policy_generate(
                             "release", kDigestA, &plan, kDigestB, &policy_b,
                             policy_digest_b, &diagnostic) == CONFIT_OK);
      CONFIT_TEST_ASSERT_CONTAINS(policy_b, selected->id);
      CONFIT_TEST_ASSERT_CONTAINS(policy_b, selected->isa);
    }
    free(policy_b);
    policy_b = 0;
    initialize_target(&plan);
  }

  /* 같은 ISA의 새 target instance는 중앙 enum 추가 없이 결정적으로 직렬화된다. */
  plan.target_id = "arm64-vendor-board";
  CONFIT_TEST_ASSERT(confit_build_policy_generate(
                         "release", kDigestA, &plan, kDigestB, &policy_b,
                         policy_digest_b, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(strcmp(policy_digest_a, policy_digest_b) != 0);
  free(policy_b);
  policy_b = 0;
  plan.target_id = "arm64-qemu-virt";

  trusted_tools[0].role = CONFIT_BUILD_TOOL_TARGET_CC;
  trusted_tools[0].path = "/usr/bin/clang";
  trusted_tools[0].sha256 = kDigestA;
  trusted_tools[0].version = "21.0.0";
  trusted_tools[1].role = CONFIT_BUILD_TOOL_QEMU;
  trusted_tools[1].path = "/usr/bin/qemu-system-aarch64";
  trusted_tools[1].sha256 = kDigestD;
  trusted_tools[1].version = "11.0.2";
  memset(&policy_ref, 0, sizeof(policy_ref));
  policy_ref.abi_version = CONFIT_BUILD_POLICY_ABI;
  policy_ref.profile_id = "release";
  policy_ref.configuration_sha256 = kDigestA;
  policy_ref.target_id = plan.target_id;
  policy_ref.target_sha256 = kDigestB;
  policy_ref.policy_sha256 = policy_digest_a;
  policy_ref.edge_table_id = "parus-five-gen-edge-v1";
  policy_ref.toolchain_plan_sha256 = kDigestB;
  policy_ref.trusted_tools = trusted_tools;
  policy_ref.trusted_tool_count = 2U;
  inputs[0].role = CONFIT_BUILD_ROLE_KERNEL_SOURCE;
  inputs[0].path = "sys/kern/main.c";
  inputs[0].sha256 = kDigestC;
  inputs[0].owner = "kern.nucleus.core";
  inputs[1] = inputs[0];
  outputs[0].role = CONFIT_BUILD_ROLE_KERNEL_OBJECT;
  outputs[0].path = "kerngen/release/arm64-qemu-virt/obj/main.o";
  outputs[0].maximum_bytes = 1048576U;
  outputs[1] = outputs[0];
  memset(&action, 0, sizeof(action));
  action.domain = CONFIT_BUILD_DOMAIN_KERNGEN;
  action.kind = CONFIT_BUILD_ACTION_KIND_COMPILE;
  action.source_owner = "kern.nucleus.core";
  action.configuration_sha256 = kDigestA;
  action.target_sha256 = kDigestB;
  action.policy_sha256 = policy_digest_a;
  action.tool = trusted_tools[0];
  action.inputs = inputs;
  action.input_count = 1U;
  action.outputs = outputs;
  action.output_count = 1U;
  action.action_quota_bytes = 2097152U;
  CONFIT_TEST_ASSERT(confit_build_action_wire_encode(
                         &policy_ref, &action, wire_a, sizeof(wire_a),
                         &wire_size_a, action_id_a, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_build_action_wire_encode(
                         &policy_ref, &action, wire_b, sizeof(wire_b),
                         &wire_size_b, action_id_b, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(wire_size_a == wire_size_b);
  CONFIT_TEST_ASSERT(memcmp(wire_a, wire_b, wire_size_a) == 0);
  CONFIT_TEST_ASSERT(strcmp(action_id_a, action_id_b) == 0);
  CONFIT_TEST_ASSERT(confit_build_action_wire_validate(
                         &policy_ref, wire_a, wire_size_a,
                         &diagnostic) == CONFIT_OK);

  /* QEMU는 selected executable seal과 typed challenge/terminal 증거 role을
   * 동시에 만족해야만 test domain action이 된다. */
  action.domain = CONFIT_BUILD_DOMAIN_TEST;
  action.kind = CONFIT_BUILD_ACTION_KIND_EXECUTE_TEST;
  action.source_owner = "test.qemu.arm64.boot";
  action.tool = trusted_tools[1];
  inputs[0].role = CONFIT_BUILD_ROLE_TEST_DESCRIPTOR;
  inputs[0].path = "test/release/arm64-qemu-virt/boot.plan";
  inputs[0].owner = "test.qemu.arm64.boot";
  inputs[1].role = CONFIT_BUILD_ROLE_TEST_IMAGE;
  inputs[1].path = "imagegen/release/arm64-qemu-virt/parus.img";
  inputs[1].sha256 = kDigestD;
  inputs[1].owner = "imagegen.arm64.qemu_virt";
  outputs[0].role = CONFIT_BUILD_ROLE_TEST_LOG;
  outputs[0].path = "test/release/arm64-qemu-virt/boot.log";
  outputs[0].maximum_bytes = 65536U;
  outputs[1].role = CONFIT_BUILD_ROLE_TEST_RECEIPT;
  outputs[1].path = "test/release/arm64-qemu-virt/boot.receipt";
  outputs[1].maximum_bytes = 4096U;
  action.input_count = 2U;
  action.output_count = 2U;
  CONFIT_TEST_ASSERT(confit_build_action_wire_encode(
                         &policy_ref, &action, wire_b, sizeof(wire_b),
                         &wire_size_b, action_id_b, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_build_action_wire_validate(
                         &policy_ref, wire_b, wire_size_b,
                         &diagnostic) == CONFIT_OK);

  action.domain = CONFIT_BUILD_DOMAIN_KERNGEN;
  action.kind = CONFIT_BUILD_ACTION_KIND_COMPILE;
  action.source_owner = "kern.nucleus.core";
  action.tool = trusted_tools[0];
  inputs[0].role = CONFIT_BUILD_ROLE_KERNEL_SOURCE;
  inputs[0].path = "sys/kern/main.c";
  inputs[0].owner = "kern.nucleus.core";
  inputs[1] = inputs[0];
  outputs[0].role = CONFIT_BUILD_ROLE_KERNEL_OBJECT;
  outputs[0].path = "kerngen/release/arm64-qemu-virt/obj/main.o";
  outputs[0].maximum_bytes = 1048576U;
  outputs[1] = outputs[0];
  action.input_count = 1U;
  action.output_count = 1U;

  /* Header cardinality, version, endian, closed enum, no-follow와 trailing bytes. */
  expect_wire_byte_rejected(&policy_ref, wire_a, wire_size_a, 8U, 2U,
                            &diagnostic);
  expect_wire_byte_rejected(&policy_ref, wire_a, wire_size_a, 9U, 1U,
                            &diagnostic);
  expect_wire_byte_rejected(&policy_ref, wire_a, wire_size_a, 12U, 0U,
                            &diagnostic);
  expect_wire_byte_rejected(&policy_ref, wire_a, wire_size_a, 16U, 0U,
                            &diagnostic);
  expect_wire_byte_rejected(&policy_ref, wire_a, wire_size_a, 16U, 2U,
                            &diagnostic);
  expect_wire_byte_rejected(&policy_ref, wire_a, wire_size_a, 17U, 255U,
                            &diagnostic);
  expect_wire_byte_rejected(&policy_ref, wire_a, wire_size_a, 18U, 1U,
                            &diagnostic);
  expect_wire_byte_rejected(&policy_ref, wire_a, wire_size_a, 19U, 0U,
                            &diagnostic);
  expect_wire_byte_rejected(&policy_ref, wire_a, wire_size_a, 22U, 0U,
                            &diagnostic);
  expect_wire_byte_rejected(&policy_ref, wire_a, wire_size_a, 22U, 2U,
                            &diagnostic);
  expect_wire_byte_rejected(&policy_ref, wire_a, wire_size_a, 23U, 0U,
                            &diagnostic);
  expect_wire_byte_rejected(&policy_ref, wire_a, wire_size_a, 98U, 'x',
                            &diagnostic);
  CONFIT_TEST_ASSERT(confit_build_action_wire_validate(
                         &policy_ref, wire_a, wire_size_a - 1U,
                         &diagnostic) == CONFIT_ERR_SCHEMA);
  confit_diagnostic_clear(&diagnostic);
  memcpy(wire_b, wire_a, wire_size_a);
  wire_b[wire_size_a] = 0U;
  CONFIT_TEST_ASSERT(confit_build_action_wire_validate(
                         &policy_ref, wire_b, wire_size_a + 1U,
                         &diagnostic) == CONFIT_ERR_SCHEMA);
  confit_diagnostic_clear(&diagnostic);

  /* Logical authoring view도 stale seal, cross-domain edge와 unsafe path를 거부한다. */
  action.configuration_sha256 = kDigestD;
  wire_size_b = 42U;
  memcpy(action_id_b, "stale", 6U);
  memset(wire_b, 0xa5, sizeof(wire_b));
  CONFIT_TEST_ASSERT(confit_build_action_wire_encode(
                         &policy_ref, &action, wire_b, sizeof(wire_b),
                         &wire_size_b, action_id_b,
                         &diagnostic) == CONFIT_ERR_SCHEMA);
  CONFIT_TEST_ASSERT(wire_size_b == 0U && action_id_b[0] == '\0' &&
                     wire_b[0] == 0U);
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(confit_build_action_validate(
                         &policy_ref, &action, &diagnostic) == CONFIT_ERR_SCHEMA);
  action.configuration_sha256 = kDigestA;
  confit_diagnostic_clear(&diagnostic);
  action.domain = CONFIT_BUILD_DOMAIN_INVALID;
  CONFIT_TEST_ASSERT(confit_build_action_validate(
                         &policy_ref, &action, &diagnostic) == CONFIT_ERR_SCHEMA);
  action.domain = CONFIT_BUILD_DOMAIN_KERNGEN;
  confit_diagnostic_clear(&diagnostic);
  action.source_owner = "";
  CONFIT_TEST_ASSERT(confit_build_action_validate(
                         &policy_ref, &action, &diagnostic) == CONFIT_ERR_SCHEMA);
  action.source_owner = "kern.nucleus.core";
  confit_diagnostic_clear(&diagnostic);
  action.tool.sha256 = kDigestD;
  CONFIT_TEST_ASSERT(confit_build_action_validate(
                         &policy_ref, &action, &diagnostic) == CONFIT_ERR_SCHEMA);
  action.tool.sha256 = kDigestA;
  confit_diagnostic_clear(&diagnostic);
  action.tool = trusted_tools[1];
  CONFIT_TEST_ASSERT(confit_build_action_validate(
                         &policy_ref, &action, &diagnostic) == CONFIT_ERR_SCHEMA);
  action.tool = trusted_tools[0];
  confit_diagnostic_clear(&diagnostic);
  outputs[0].role = CONFIT_BUILD_ROLE_WORLD_OBJECT;
  CONFIT_TEST_ASSERT(confit_build_action_validate(
                         &policy_ref, &action, &diagnostic) == CONFIT_ERR_SCHEMA);
  outputs[0].role = CONFIT_BUILD_ROLE_KERNEL_OBJECT;
  confit_diagnostic_clear(&diagnostic);
  inputs[0].path = "sys/kern/../private.c";
  CONFIT_TEST_ASSERT(confit_build_action_validate(
                         &policy_ref, &action, &diagnostic) == CONFIT_ERR_SCHEMA);
  inputs[0].path = "sys/kern/main.c";
  confit_diagnostic_clear(&diagnostic);
  memset(oversized_path, 'a', sizeof(oversized_path));
  oversized_path[sizeof(oversized_path) - 1U] = '\0';
  inputs[0].path = oversized_path;
  CONFIT_TEST_ASSERT(confit_build_action_validate(
                         &policy_ref, &action, &diagnostic) == CONFIT_ERR_SCHEMA);
  inputs[0].path = "sys/kern/main.c";
  confit_diagnostic_clear(&diagnostic);
  inputs[0].path = "/sys/kern/main.c";
  CONFIT_TEST_ASSERT(confit_build_action_validate(
                         &policy_ref, &action, &diagnostic) == CONFIT_ERR_SCHEMA);
  inputs[0].path = "sys/kern/main.c";
  confit_diagnostic_clear(&diagnostic);
  inputs[0].role = CONFIT_BUILD_ROLE_WORLD_SOURCE;
  CONFIT_TEST_ASSERT(confit_build_action_validate(
                         &policy_ref, &action, &diagnostic) == CONFIT_ERR_SCHEMA);
  inputs[0].role = CONFIT_BUILD_ROLE_KERNEL_SOURCE;
  confit_diagnostic_clear(&diagnostic);
  action.input_count = 0U;
  CONFIT_TEST_ASSERT(confit_build_action_validate(
                         &policy_ref, &action, &diagnostic) == CONFIT_ERR_SCHEMA);
  action.input_count = 1U;
  confit_diagnostic_clear(&diagnostic);
  action.input_count = CONFIT_BUILD_ACTION_MAX_INPUTS + 1U;
  CONFIT_TEST_ASSERT(confit_build_action_validate(
                         &policy_ref, &action, &diagnostic) == CONFIT_ERR_SCHEMA);
  action.input_count = 1U;
  confit_diagnostic_clear(&diagnostic);
  action.input_count = 2U;
  CONFIT_TEST_ASSERT(confit_build_action_validate(
                         &policy_ref, &action, &diagnostic) == CONFIT_ERR_SCHEMA);
  action.input_count = 1U;
  confit_diagnostic_clear(&diagnostic);
  inputs[1] = inputs[0];
  inputs[1].role = CONFIT_BUILD_ROLE_KERNEL_METADATA;
  action.input_count = 2U;
  CONFIT_TEST_ASSERT(confit_build_action_validate(
                         &policy_ref, &action, &diagnostic) == CONFIT_ERR_SCHEMA);
  action.input_count = 1U;
  confit_diagnostic_clear(&diagnostic);
  action.output_count = 2U;
  CONFIT_TEST_ASSERT(confit_build_action_validate(
                         &policy_ref, &action, &diagnostic) == CONFIT_ERR_SCHEMA);
  action.output_count = 1U;
  confit_diagnostic_clear(&diagnostic);
  outputs[1] = outputs[0];
  outputs[1].role = CONFIT_BUILD_ROLE_KERNEL_METADATA;
  action.output_count = 2U;
  CONFIT_TEST_ASSERT(confit_build_action_validate(
                         &policy_ref, &action, &diagnostic) == CONFIT_ERR_SCHEMA);
  action.output_count = 1U;
  confit_diagnostic_clear(&diagnostic);
  outputs[0].maximum_bytes = CONFIT_BUILD_ACTION_MAX_QUOTA_BYTES;
  CONFIT_TEST_ASSERT(confit_build_action_validate(
                         &policy_ref, &action, &diagnostic) == CONFIT_ERR_SCHEMA);
  outputs[0].maximum_bytes = 1048576U;
  confit_diagnostic_clear(&diagnostic);
  policy_ref.trusted_tool_count = 0U;
  CONFIT_TEST_ASSERT(confit_build_action_validate(
                         &policy_ref, &action, &diagnostic) == CONFIT_ERR_SCHEMA);
  policy_ref.trusted_tool_count = 2U;
  confit_diagnostic_clear(&diagnostic);
  trusted_tools[1] = trusted_tools[0];
  trusted_tools[1].path = "/opt/vendor/clang";
  policy_ref.trusted_tool_count = 2U;
  CONFIT_TEST_ASSERT(confit_build_action_validate(
                         &policy_ref, &action, &diagnostic) == CONFIT_ERR_SCHEMA);
  trusted_tools[1].role = CONFIT_BUILD_TOOL_QEMU;
  trusted_tools[1].path = "/usr/bin/qemu-system-aarch64";
  trusted_tools[1].sha256 = kDigestD;
  trusted_tools[1].version = "11.0.2";
  confit_diagnostic_clear(&diagnostic);

  plan.support_facade_include_root = "sys";
  expect_policy_rejected(&plan, &diagnostic);
  plan.support_facade_include_root = "sys/include/parus/arch/arm64";
  plan.support_provider_owner = plan.support_consumer_owner;
  expect_policy_rejected(&plan, &diagnostic);
  plan.support_provider_owner = "sys.arch.arm64.support";
  plan.machine_evidence_transport = "unknown-evidence-v1";
  expect_policy_rejected(&plan, &diagnostic);
  plan.machine_evidence_transport = "qemu-fwcfg-challenge-v1";
  plan.machine_trust_profile = "ambient-path-v1";
  expect_policy_rejected(&plan, &diagnostic);
  plan.machine_trust_profile = "qemu-executable-v1";

  expect_retired_private_include_rejected();
  free(policy_a);
  free(policy_b);
  free(policy_mk);
  confit_diagnostic_clear(&diagnostic);
  return 0;
}
