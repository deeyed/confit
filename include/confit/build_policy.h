#ifndef CONFIT_BUILD_POLICY_H
#define CONFIT_BUILD_POLICY_H

#include <stddef.h>
#include <stdint.h>

#include "confit/diagnostic.h"
#include "confit/status.h"
#include "confit/target_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  CONFIT_BUILD_POLICY_ABI = 1,
  CONFIT_BUILD_ACTION_WIRE_VERSION = 1,
  CONFIT_BUILD_ACTION_MAX_INPUTS = 64,
  CONFIT_BUILD_ACTION_MAX_OUTPUTS = 64,
  CONFIT_BUILD_ACTION_MAX_BYTES = 65536,
  CONFIT_BUILD_ACTION_MAX_PATH_BYTES = 1024,
  CONFIT_BUILD_ACTION_MAX_ATOM_BYTES = 192,
  CONFIT_BUILD_ACTION_MAX_TRUSTED_TOOLS = 8,
  CONFIT_BUILD_ACTION_MAX_QUOTA_BYTES = 1024 * 1024 * 1024,
};

/** @brief build mutation을 소유하는 closed Five-GEN/test domain이다. */
typedef enum ConfitBuildDomain {
  CONFIT_BUILD_DOMAIN_INVALID = 0,
  CONFIT_BUILD_DOMAIN_TOOLGEN,
  CONFIT_BUILD_DOMAIN_ENVGEN,
  CONFIT_BUILD_DOMAIN_KERNGEN,
  CONFIT_BUILD_DOMAIN_WORLDGEN,
  CONFIT_BUILD_DOMAIN_IMAGEGEN,
  CONFIT_BUILD_DOMAIN_TEST,
} ConfitBuildDomain;

/** @brief reviewed implementation만 추가할 수 있는 closed action kind다. */
typedef enum ConfitBuildActionKind {
  CONFIT_BUILD_ACTION_KIND_INVALID = 0,
  CONFIT_BUILD_ACTION_KIND_MKDIR,
  CONFIT_BUILD_ACTION_KIND_CLEAN,
  CONFIT_BUILD_ACTION_KIND_COPY,
  CONFIT_BUILD_ACTION_KIND_GENERATE,
  CONFIT_BUILD_ACTION_KIND_COMPILE,
  CONFIT_BUILD_ACTION_KIND_ARCHIVE,
  CONFIT_BUILD_ACTION_KIND_LINK,
  CONFIT_BUILD_ACTION_KIND_PACKAGE,
  CONFIT_BUILD_ACTION_KIND_VERIFY,
  CONFIT_BUILD_ACTION_KIND_PUBLISH,
  CONFIT_BUILD_ACTION_KIND_EXECUTE_TEST,
} ConfitBuildActionKind;

/** @brief action input/output의 ownership과 stage edge를 나타내는 closed role다. */
typedef enum ConfitBuildRole {
  CONFIT_BUILD_ROLE_INVALID = 0,
  CONFIT_BUILD_ROLE_HOST_SOURCE,
  CONFIT_BUILD_ROLE_BOOTSTRAP_TOOL,
  CONFIT_BUILD_ROLE_TOOL_EXECUTABLE,
  CONFIT_BUILD_ROLE_TOOL_SEAL,
  CONFIT_BUILD_ROLE_TOOL_ROOT,
  CONFIT_BUILD_ROLE_ENV_SOURCE,
  CONFIT_BUILD_ROLE_ENV_HEADER,
  CONFIT_BUILD_ROLE_ENV_CRT,
  CONFIT_BUILD_ROLE_ENV_LIBRARY,
  CONFIT_BUILD_ROLE_ENV_SYSROOT_SEAL,
  CONFIT_BUILD_ROLE_ENV_ROOT,
  CONFIT_BUILD_ROLE_KERNEL_SOURCE,
  CONFIT_BUILD_ROLE_KERNEL_OBJECT,
  CONFIT_BUILD_ROLE_KERNEL_ELF,
  CONFIT_BUILD_ROLE_KERNEL_METADATA,
  CONFIT_BUILD_ROLE_KERNEL_SEAL,
  CONFIT_BUILD_ROLE_KERNEL_ROOT,
  CONFIT_BUILD_ROLE_WORLD_SOURCE,
  CONFIT_BUILD_ROLE_WORLD_OBJECT,
  CONFIT_BUILD_ROLE_WORLD_ARTIFACT,
  CONFIT_BUILD_ROLE_WORLD_INSTALL_PLAN,
  CONFIT_BUILD_ROLE_WORLD_SEAL,
  CONFIT_BUILD_ROLE_WORLD_ROOT,
  CONFIT_BUILD_ROLE_IMAGE_POLICY,
  CONFIT_BUILD_ROLE_IMAGE_ARTIFACT,
  CONFIT_BUILD_ROLE_IMAGE_PACKAGE,
  CONFIT_BUILD_ROLE_IMAGE_SEAL,
  CONFIT_BUILD_ROLE_IMAGE_ROOT,
  CONFIT_BUILD_ROLE_TEST_DESCRIPTOR,
  CONFIT_BUILD_ROLE_TEST_IMAGE,
  CONFIT_BUILD_ROLE_TEST_LOG,
  CONFIT_BUILD_ROLE_TEST_RECEIPT,
  CONFIT_BUILD_ROLE_TEST_ROOT,
} ConfitBuildRole;

/** @brief action이 실행할 수 있는 exact reviewed tool class다. */
typedef enum ConfitBuildToolRole {
  CONFIT_BUILD_TOOL_INVALID = 0,
  CONFIT_BUILD_TOOL_BUILTIN,
  CONFIT_BUILD_TOOL_HOST_CC,
  CONFIT_BUILD_TOOL_TARGET_CC,
  CONFIT_BUILD_TOOL_TARGET_AR,
  CONFIT_BUILD_TOOL_TARGET_LD,
  CONFIT_BUILD_TOOL_DTC,
  CONFIT_BUILD_TOOL_QEMU,
} ConfitBuildToolRole;

/** @brief one typed action input이다. 모든 path는 canonical relative role path다. */
typedef struct ConfitBuildActionInput {
  ConfitBuildRole role;
  const char *path;
  const char *sha256;
  const char *owner;
} ConfitBuildActionInput;

/** @brief one typed action output과 effect-before-write maximum이다. */
typedef struct ConfitBuildActionOutput {
  ConfitBuildRole role;
  const char *path;
  uint64_t maximum_bytes;
} ConfitBuildActionOutput;

/** @brief path만으로 신뢰하지 않는 exact tool identity다. */
typedef struct ConfitBuildActionTool {
  ConfitBuildToolRole role;
  const char *path;
  const char *sha256;
  const char *version;
} ConfitBuildActionTool;

/** @brief selected bundle의 immutable policy identity view다. */
typedef struct ConfitBuildPolicyRef {
  uint32_t abi_version;
  const char *profile_id;
  const char *configuration_sha256;
  const char *target_id;
  const char *target_sha256;
  const char *policy_sha256;
  const char *edge_table_id;
  const char *toolchain_plan_sha256;
  const ConfitBuildActionTool *trusted_tools;
  size_t trusted_tool_count;
} ConfitBuildPolicyRef;

/** @brief pointer-bearing authoring view이며 wire representation 자체가 아니다. */
typedef struct ConfitBuildAction {
  ConfitBuildDomain domain;
  ConfitBuildActionKind kind;
  const char *source_owner;
  const char *configuration_sha256;
  const char *target_sha256;
  const char *policy_sha256;
  ConfitBuildActionTool tool;
  const ConfitBuildActionInput *inputs;
  size_t input_count;
  const ConfitBuildActionOutput *outputs;
  size_t output_count;
  uint64_t action_quota_bytes;
} ConfitBuildAction;

/** @brief stable domain spelling을 반환한다. */
const char *confit_build_domain_name(ConfitBuildDomain domain);

/** @brief stable action kind spelling을 반환한다. */
const char *confit_build_action_kind_name(ConfitBuildActionKind kind);

/** @brief stable input/output role spelling을 반환한다. */
const char *confit_build_role_name(ConfitBuildRole role);

/** @brief stable tool role spelling을 반환한다. */
const char *confit_build_tool_role_name(ConfitBuildToolRole role);

/**
 * @brief selected target tuple에서 canonical build.policy bytes를 만든다.
 *
 * configuration_sha256은 selection artifact digest이고 target_sha256은 target.mk
 * digest다. 반환 문자열은 caller가 free한다. out_policy_sha256은 lower-case raw
 * SHA-256이다.
 */
ConfitStatus confit_build_policy_generate(
    const char *profile_id, const char *configuration_sha256,
    const ConfitTargetPlan *target_plan, const char *target_sha256,
    char **out_policy, char out_policy_sha256[65],
    ConfitDiagnostic *diagnostic);

/** @brief policy digest만 노출하는 restricted bmake adapter를 만든다. */
ConfitStatus confit_build_policy_generate_make_adapter(
    const char *policy_sha256, const char *configuration_sha256,
    const char *target_sha256, char **out_make,
    ConfitDiagnostic *diagnostic);

/** @brief logical action이 policy와 closed edge table을 만족하는지 검사한다. */
ConfitStatus confit_build_action_validate(
    const ConfitBuildPolicyRef *policy, const ConfitBuildAction *action,
    ConfitDiagnostic *diagnostic);

/**
 * @brief action을 pointer-free little-endian wire ABI로 직렬화한다.
 *
 * action ID는 canonical body의 SHA-256으로 결정되며 out_action_id에 쓴다.
 */
ConfitStatus confit_build_action_wire_encode(
    const ConfitBuildPolicyRef *policy, const ConfitBuildAction *action,
    unsigned char *out_bytes, size_t capacity, size_t *out_size,
    char out_action_id[65], ConfitDiagnostic *diagnostic);

/**
 * @brief untrusted wire bytes의 version, byte order, bounds, identity와 role edge를
 *        allocation 없이 fail-closed 검증한다.
 */
ConfitStatus confit_build_action_wire_validate(
    const ConfitBuildPolicyRef *policy, const unsigned char *bytes,
    size_t size, ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_BUILD_POLICY_H */
